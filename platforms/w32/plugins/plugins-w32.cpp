#include "InitState.h"
#include "platform.h"
#include "plugins/plugins.h"
#include "util/util.h"

#include "tweaker/db_hooks.h"

using namespace blt::plugins;
using namespace pd2hook::tweaker::dbhook;

// Don't use these funcdefs when porting to GNU+Linux, as it doesn't need
// any kind of getter function because the PD2 binary isn't stripped
typedef void* (*lua_access_func_t)(const char*);
typedef void (*init_func_t)(lua_access_func_t get_lua_func_by_name);

static void pd2_log(const char* message, int level, const char* file, int line)
{
	using LT = pd2hook::Logging::LogType;

	char buffer[256];
	sprintf_s(buffer, sizeof(buffer), "ExtModDLL %s", file);

	switch ((LT)level)
	{
	case LT::LOGGING_FUNC:
	case LT::LOGGING_LOG:
	case LT::LOGGING_LUA:
		PD2HOOK_LOG_LEVEL(message, (LT)level, buffer, line, FOREGROUND_RED, FOREGROUND_BLUE, FOREGROUND_INTENSITY);
		break;
	case LT::LOGGING_WARN:
		PD2HOOK_LOG_LEVEL(message, (LT)level, buffer, line, FOREGROUND_RED, FOREGROUND_GREEN, FOREGROUND_INTENSITY);
		break;
	case LT::LOGGING_ERROR:
		PD2HOOK_LOG_LEVEL(message, (LT)level, buffer, line, FOREGROUND_RED, FOREGROUND_INTENSITY);
		break;
	}
}

static bool is_active_state(lua_State* L)
{
	return pd2hook::check_active_state(L);
}

void db_hook_asset_file_replacer(blt::idstring name, blt::idstring ext, db_file_replacer_t replacer)
{
	pd2hook::tweaker::dbhook::DBTargetFile* target = nullptr;

	pd2hook::tweaker::dbhook::register_asset_hook(name, ext, false, &target);

	target->SetReplacer(replacer);
}

void db_hook_asset_file_plain_file(blt::idstring name, blt::idstring ext, const char* plain_file)
{
	pd2hook::tweaker::dbhook::DBTargetFile* target = nullptr;

	pd2hook::tweaker::dbhook::register_asset_hook(name, ext, false, &target);

	target->SetPlainFile(plain_file);
}

void db_hook_asset_file_direct_bundle(blt::idstring name, blt::idstring ext, blt::idstring direct_bundle_name,
                                      blt::idstring direct_bundle_ext)
{
	pd2hook::tweaker::dbhook::DBTargetFile* target = nullptr;

	pd2hook::tweaker::dbhook::register_asset_hook(name, ext, false, &target);

	blt::idfile bundle = blt::idfile(direct_bundle_name, direct_bundle_ext);

	target->SetDirectBundle(bundle);
}

FileData* db_read_file(blt::idstring name, blt::idstring ext)
{
	FileData fd = find_file(name, ext);
	return new FileData(fd);
}

void db_free_file(FileData* data)
{
	delete[] data->data;
	delete data;
}

bool db_file_exists(blt::idstring name, blt::idstring ext)
{
	return file_exists(name, ext);
}

bool is_vr()
{
	return pd2hook::Util::IsVr();
}

unsigned long long create_hash(const char* str)
{
	return blt::idstring_hash(str);
}

static void* get_func(const char* name)
{
	std::string str = name;

	if (str == "pd2_log")
	{
		return &pd2_log;
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
	// wren and lua function parity
	else if (str == "db_hook_asset_file" || str == "db_hook_asset_file_replacer")
	{
		return &db_hook_asset_file_replacer;
	}
	else if (str == "db_hook_asset_file_plain_file")
	{
		return &db_hook_asset_file_plain_file;
	}
	else if (str == "db_hook_asset_file_direct_bundle")
	{
		return &db_hook_asset_file_direct_bundle;
	}
	else if (str == "db_read_file")
	{
		return &db_read_file;
	}
	else if (str == "db_free_file")
	{
		return &db_free_file;
	}
	else if (str == "db_file_exists")
	{
		return &db_file_exists;
	}
	else if (str == "create_hash")
	{
		return &create_hash;
	}
	else if (str == "is_vr")
	{
		return &is_vr;
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
	module = LoadLibraryA(file.c_str());

	if (!module)
		throw std::string("Failed to load module: ERR") + std::to_string(GetLastError());

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
