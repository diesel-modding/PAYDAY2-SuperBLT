//
// Created by znix on 24/01/2021.
//

#include <wren.hpp>

#include "wren_lua_interface.h"

#include <map>
#include <mutex>
#include <string.h>

#include "wrenloader.h"

// Hacky, see find_wren_caller
extern "C"
{
#include "../../lib/wren/src/vm/wren_vm.h"
}

static std::mutex wren_exposed_objects_mutex;
static std::map<std::string, WrenHandle*> wren_exposed_objects;

/////////// Wren side ///////////

static std::string find_wren_caller(WrenVM* vm)
{
	// Yes I do admit this is a bit... unsupported - however I do really want to autodetect the mod like this
	const ObjFiber* fiber = vm->fiber;
	const CallFrame* frame = &fiber->frames[fiber->numFrames - 1];
	const ObjFn* fn = frame->closure->fn;
	const ObjString* module_wren = fn->module->name;
	std::string module(module_wren->value, module_wren->length);

	// Trim off everything except for the mod name
	size_t stroke_pos = module.find('/');
	if (stroke_pos != std::string::npos)
		module.erase(stroke_pos);

	return module;
}

static void wren_register_object(WrenVM* vm)
{
	std::string name = wrenGetSlotString(vm, 1);
	std::string mod = find_wren_caller(vm);
	std::string full_name = mod + "/" + name;

	std::lock_guard lock(wren_exposed_objects_mutex);
	if (wren_exposed_objects.count(full_name))
	{
		char buff[1024];
		snprintf(buff, sizeof(buff) - 1, "Failed to register Wren/Lua interface object - name '%s' already taken",
		         full_name.c_str());
		wrenSetSlotString(vm, 0, buff);
		wrenAbortFiber(vm, 0);
	}
	else
	{
		wren_exposed_objects[full_name] = wrenGetSlotHandle(vm, 2);
		wrenSetSlotNull(vm, 0);
	}
}

WrenForeignMethodFn raidhook::tweaker::lua_io::bind_wren_lua_method(WrenVM* vm, const char* module,
                                                                    const char* class_name, bool is_static,
                                                                    const char* signature)
{
	if (strcmp(module, "base/native/LuaInterface_001") != 0)
		return nullptr;

	if (strcmp(class_name, "LuaInterface") == 0)
	{
		if (strcmp(signature, "register_object(_,_)") == 0 && is_static)
		{
			return &wren_register_object;
		}
	}

	return nullptr;
}

/////////// Lua side ///////////

static void push_wren_to_lua(WrenVM* vm, int wren_slot, lua_State* L, bool allow_complex)
{
	switch (wrenGetSlotType(vm, wren_slot))
	{
	case WREN_TYPE_BOOL:
		lua_pushboolean(L, wrenGetSlotBool(vm, wren_slot));
		break;
	case WREN_TYPE_NUM:
		lua_pushnumber(L, wrenGetSlotDouble(vm, wren_slot));
		break;
	case WREN_TYPE_STRING:
		lua_pushstring(L, wrenGetSlotString(vm, wren_slot));
		break;
	case WREN_TYPE_LIST:
		if (allow_complex)
		{
			// +1 to convert from an index to a count, plus one for the temporary value
			wrenEnsureSlots(vm, wren_slot + 2);

			int count = wrenGetListCount(vm, wren_slot);
			lua_createtable(L, count, 0);
			for (int i = 0; i < count; i++)
			{
				wrenGetListElement(vm, wren_slot, i, wren_slot + 1);
				push_wren_to_lua(vm, wren_slot + 1, L, false);
				lua_rawseti(L, -2, i + 1);
			}
			break;
		}
	case WREN_TYPE_MAP:
		if (allow_complex)
		{
			// +1 to convert from an index to a slot, +1 for the key and +1 for the value
			wrenEnsureSlots(vm, wren_slot + 3);

			lua_newtable(L);

			// FIXME there doesn't seem to be any way to iterate over a map, so do it the hacky way
			ObjMap* map = AS_MAP(vm->apiStack[wren_slot]);

			for (uint32_t i = 0; i < map->capacity; i++)
			{
				MapEntry& entry = map->entries[i];
				Value value = entry.value;
				if (entry.key == UNDEFINED_VAL)
					continue;

				if (!IS_STRING(entry.key))
					continue;

				if (IS_NUM(value))
					lua_pushnumber(L, AS_NUM(value));
				else if (IS_STRING(value))
					lua_pushstring(L, AS_CSTRING(value));
				else if (IS_BOOL(value))
					lua_pushboolean(L, AS_BOOL(value));
				else
					continue;

				lua_setfield(L, -2, AS_CSTRING(entry.key));
			}

			break;
		}
	default:
		// Just ignore any unknown types and convert them to null
		lua_pushnil(L);
		break;
	}
}

static int wren_lua_invoke(lua_State* L)
{
	// Load the arguments list
	std::string mod_id = luaL_checkstring(L, 1);
	std::string obj_name = luaL_checkstring(L, 2);

	if (!lua_isnoneornil(L, 3) && !lua_istable(L, 3)) // Reserve an options table
		luaL_error(L, "wren_io.invoke: Bad argument #3 - should be an options table or nil");

	std::string func_name = luaL_checkstring(L, 4);

	int arg_count = lua_gettop(L) - 4;

	// Process the function name to add the argument list
	func_name += "(";
	for (int i = 0; i < arg_count; i++)
	{
		if (i > 0)
			func_name += ",";
		func_name += "_";
	}
	func_name += ")";

	// Make sure this isn't a private function
	if (!func_name.empty() && func_name[0] == '_')
		luaL_error(L, "Cannot call function %s: name starts with an underscore", func_name.c_str());

	// Under the mutex, load the object
	WrenHandle* handle = nullptr;
	std::string full_name = mod_id + "/" + obj_name;
	{
		std::lock_guard lock(wren_exposed_objects_mutex);
		if (wren_exposed_objects.count(full_name))
			handle = wren_exposed_objects[full_name];
	}

	if (!handle)
		luaL_error(L, "No such Wren IO object: '%s'", full_name.c_str());

	bool run_success = true;
	char run_err_str[128];
	memset(run_err_str, 0, sizeof(run_err_str));

	// Invoke the Wren function under the only-wren-can-run lock
	{
		auto lock = raidhook::wren::lock_wren_vm();
		WrenVM* vm = raidhook::wren::get_wren_vm();

		if (!vm)
			luaL_error(L, "Wren runtime unavailable - check for Wren-related errors in the log");

		WrenHandle* res = wrenMakeCallHandle(vm, func_name.c_str());

		wrenEnsureSlots(vm, 1 + arg_count);
		wrenSetSlotHandle(vm, 0, handle);
		for (int i = 0; i < arg_count; i++)
		{
			int lua_idx = i + 5;
			switch (lua_type(L, lua_idx))
			{
			case LUA_TNUMBER:
				wrenSetSlotDouble(vm, i + 1, lua_tonumber(L, lua_idx));
				break;
			case LUA_TSTRING:
				wrenSetSlotString(vm, i + 1, lua_tostring(L, lua_idx));
				break;
			default:
				snprintf(run_err_str, sizeof(run_err_str) - 1, "Bad arg %d: invalid type %s", i + 1,
				         lua_typename(L, lua_idx));
				run_success = false;
				goto done;
			}
		}

		if (wrenCall(vm, res) != WREN_RESULT_SUCCESS)
		{
			snprintf(run_err_str, sizeof(run_err_str) - 1, "Wren error occurred during invocation");
			run_success = false;
		}
		else
		{
			// Push the return value of the Wren function onto the Lua stack
			push_wren_to_lua(vm, 0, L, true);
		}

	done:
		wrenReleaseHandle(vm, res);
	}

	if (!run_success)
		luaL_error(L, "Failed to run Wren function %s.%s: %s", full_name.c_str(), func_name.c_str(), run_err_str);

	// TODO return the Wren return value

	return 1;
}

void raidhook::tweaker::lua_io::register_lua_functions(lua_State* L)
{
	luaL_Reg vmLib[] = {
		{"invoke", &wren_lua_invoke},
		{nullptr, nullptr},
	};

	lua_newtable(L);
	luaL_register(L, nullptr, vmLib);
	lua_setfield(L, -2, "wren_io");
}
