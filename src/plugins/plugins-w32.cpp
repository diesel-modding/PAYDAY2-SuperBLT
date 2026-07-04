#include "InitState.h"
#include "platform.h"
#include "plugins/plugins.h"
#include "util/util.h"

using namespace std;
using namespace blt::plugins;

typedef void* (*lua_access_func_t)(const char*);
typedef void (*init_func_t)(lua_access_func_t get_lua_func_by_name);

static void raid_log(const char* message, int level, const char* file, int line)
{
	using LT = raidhook::Logging::LogType;

	char buffer[256];
	sprintf_s(buffer, sizeof(buffer), "ExtModDLL %s", file);

	switch ((LT)level)
	{
	case LT::LOGGING_FUNC:
	case LT::LOGGING_LOG:
	case LT::LOGGING_LUA:
		RAIDHOOK_LOG_LEVEL(message, (LT)level, buffer, line, FOREGROUND_RED, FOREGROUND_BLUE, FOREGROUND_INTENSITY);
		break;
	case LT::LOGGING_WARN:
		RAIDHOOK_LOG_LEVEL(message, (LT)level, buffer, line, FOREGROUND_RED, FOREGROUND_GREEN, FOREGROUND_INTENSITY);
		break;
	case LT::LOGGING_ERROR:
		RAIDHOOK_LOG_LEVEL(message, (LT)level, buffer, line, FOREGROUND_RED, FOREGROUND_INTENSITY);
		break;
	}
}

static bool is_active_state(lua_State* L)
{
	return raidhook::check_active_state(L);
}

static void* get_func(const char* name)
{
	string str = name;

	if (str == "raid_log")
	{
		return &raid_log;
	}
	else if (str == "is_active_state")
	{
		return &is_active_state;
	}
	else if (str == "luaL_checkstack")
	{
		return &luaL_checkstack;
	}
	else if (str == "lua_rawequal")
	{
		return &lua_rawequal;
	}

	return blt::platform::win32::get_lua_func(name);
}

class WindowsPlugin : public Plugin
{
  public:
	WindowsPlugin(std::string file);

  protected:
	virtual void* ResolveSymbol(std::string name) const;

  private:
	HMODULE module;
};

WindowsPlugin::WindowsPlugin(std::string file) : Plugin(file)
{
	module = LoadLibrary(file.c_str());

	if (!module)
		throw string("Failed to load module: ERR") + to_string(GetLastError());

	Init();

	// Start loading everything
	init_func_t init = (init_func_t)GetProcAddress(module, "SuperBLT_Plugin_Setup");
	if (!init)
		throw "Invalid module - missing initfunc!";

	init(get_func);
}

void* WindowsPlugin::ResolveSymbol(std::string name) const
{
	return GetProcAddress(module, name.c_str());
}

Plugin* blt::plugins::CreateNativePlugin(std::string file)
{
	return new WindowsPlugin(file);
}
