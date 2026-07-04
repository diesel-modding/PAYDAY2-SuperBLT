//
// Created by ZNix on 01/02/2021.
//

#include "LuaAsyncIO.h"

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#include <errno.h>
#include <string.h>

#include <InitState.h>
#include <threading/queue.h>
#include <util/util.h>

static const int MAX_THREADS = 4;

struct IOTask
{
	std::function<void()> func;
};

struct IOCompletion
{
	lua_State* L;
	std::function<void()> func;
};

static std::mutex task_mutex;
static std::queue<IOTask> task_list;
static std::condition_variable condition_var;
static int thread_count;

RAIDHOOK_REGISTER_EVENTQUEUE(IOCompletion, Completions);

// TODO deduplicate with that in InitiateState
static void handled_pcall(lua_State* L, int nargs, int nresults)
{
	int err = lua_pcall(L, nargs, nresults, 0);
	if (err == LUA_ERRRUN)
	{
		std::string msg = std::string("Failed to make async IO call: ") + lua_tostring(L, -1);
		RAIDHOOK_LOG_ERROR(msg.c_str());
		lua_pop(L, 1);
	}
}

static void invoke_on_update(lua_State* L, std::function<void()> func)
{
	IOCompletion completion{L, std::move(func)};

	// NOLINTNEXTLINE(performance-unnecessary-value-param)
	GetCompletionsQueue().AddToQueue(
		[](IOCompletion completion)
		{
			if (!raidhook::check_active_state(completion.L))
				return;

			int old_top = lua_gettop(completion.L);
			completion.func();
			int new_top = lua_gettop(completion.L);

			if (old_top != new_top)
			{
				char buff[128];
				snprintf(buff, sizeof(buff) - 1, "Lua stack size deviation for async invoke_on_update: %d vs %d\n",
			             old_top, new_top);
				RAIDHOOK_LOG_ERROR(buff);
				abort();
			}
		},
		completion);
}

// MUST BE CALLED UNDER task_mutex
static void start_task_thread()
{
	thread_count++;

	RAIDHOOK_LOG_LOG("Starting async IO thread");

	std::thread thread(
		[]()
		{
			while (true)
			{
				// Try and get a task, or timeout
			    // The timeout ensures that we don't hold a bunch of threads if we're not using them
				IOTask task;
				const auto timeout = std::chrono::milliseconds(500);
				{
					std::unique_lock lock(task_mutex);
					bool has_item = condition_var.wait_for(lock, timeout, []() { return !task_list.empty(); });
					if (!has_item)
						break;
					task = std::move(task_list.front());
					task_list.pop();
				}

				// Execute this task
				task.func();
			}

			RAIDHOOK_LOG_LOG("Exiting async IO thread");

			{
				std::lock_guard guard(task_mutex);
				thread_count--;
			}
		});
	thread.detach();
}

static void dispatch_task(IOTask&& task)
{
	{
		std::lock_guard guard(task_mutex);
		task_list.push(std::move(task));

		// Check if we need to start a new thread
		// Do this under the mutex, since it should not occur very often and we wouldn't want many
		// threads being started concurrently.
		if (thread_count == 0 || (thread_count < MAX_THREADS && task_list.size() > 5))
			start_task_thread();
	}
	condition_var.notify_one();
}

static void dispatch_task(std::function<void()> func)
{
	IOTask task{};
	task.func = std::move(func);
	dispatch_task(std::move(task));
}

// Arguments: string(filename) function(callback) optional table(options)
static int aio_read(lua_State* L)
{
	std::string filename = luaL_checkstring(L, 1);

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_pushvalue(L, 2);
	int completion_func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	dispatch_task(
		[filename, completion_func_ref, L]()
		{
			std::vector<char> data;
			bool success = true;

			// Since there's more steps involved with seeking around to find the length, just use
		    // exceptions for this rather than checking goodbit.
			errno = 0; // Make sure pre-existing errors can't leak in
			try
			{
				std::ifstream stream;
				stream.exceptions(std::ios::eofbit | std::ios::failbit);
				stream.open(filename, std::ios::binary);

				stream.seekg(0, std::ios::end);
				size_t length = stream.tellg();
				stream.seekg(0, std::ios::beg);

				data.resize(length);
				stream.read(data.data(), data.size());
			}
			catch (const std::ios::failure& ex)
			{
				data.clear();
				success = false;
			}

			invoke_on_update(L,
		                     [L, func_ref{completion_func_ref}, &data, success, err{errno}]()
		                     {
								 lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);
								 if (success)
								 {
									 lua_pushlstring(L, data.data(), data.size());
									 data.clear();
									 handled_pcall(L, 1, 0);
								 }
								 else
								 {
									 lua_pushnil(L);
									 lua_pushstring(L, strerror(err));
									 handled_pcall(L, 2, 0);
								 }
								 luaL_unref(L, LUA_REGISTRYINDEX, func_ref);
							 });
		});

	return 0;
}

// Arguments: string(filename) string(contents) function(callback) optional table(options)
static int aio_write(lua_State* L)
{
	std::string filename = luaL_checkstring(L, 1);

	luaL_checktype(L, 3, LUA_TFUNCTION);

	size_t contents_len = 0;
	const char* contents_ptr = luaL_checklstring(L, 2, &contents_len);
	std::vector<char> contents(contents_len);
	memcpy(contents.data(), contents_ptr, contents_len);

	lua_pushvalue(L, 3);
	int completion_func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	dispatch_task(
		[filename, contents{std::move(contents)}, completion_func_ref, L]()
		{
			errno = 0; // Make sure pre-existing errors can't leak in

			std::ofstream stream;
			if (stream.good())
				stream.open(filename, std::ios::binary);
			if (stream.good())
				stream.write(contents.data(), contents.size());

			invoke_on_update(L,
		                     [L, func_ref{completion_func_ref}, status{stream.good()}, err{errno}]()
		                     {
								 lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);
								 lua_pushboolean(L, status);
								 if (status)
								 {
									 handled_pcall(L, 1, 0);
								 }
								 else
								 {
									 lua_pushstring(L, strerror(err));
									 handled_pcall(L, 2, 0);
								 }
								 luaL_unref(L, LUA_REGISTRYINDEX, func_ref);
							 });
		});

	return 0;
}

void load_lua_async_io(lua_State* L)
{
	luaL_Reg vmLib[] = {
		{"read", aio_read},
		{"write", aio_write},

		{nullptr, nullptr},
	};

	lua_newtable(L);
	luaL_register(L, nullptr, vmLib);
	lua_setfield(L, -2, "async_io");
}
