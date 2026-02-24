#include "wrenloader.h"

#include <assert.h>
#include <fstream>
#include <vector>

#include "db_hooks.h"
#include "global.h"
#include "plugins/plugins.h"
#include "util/util.h"
#include "wren_environment.h"
#include "wren_lua_interface.h"
#include "wren_sblt_utils.h"
#include "wrenxml.h"
#include "xmltweaker_internal.h"

#include <wren.hpp>

// Suboptimal hack for IO.dynamic_import - see the resolver function
extern "C"
{
#include "../../lib/wren/src/vm/wren_vm.h"
}

#include "wren_generated_src.h"

using namespace pd2hook;
using namespace pd2hook::tweaker;
using namespace std;

struct ModData
{
	std::string name;
	std::string scripts_root;
};
std::map<std::string, ModData> mod_metadata;

static void err([[maybe_unused]] WrenVM* vm, [[maybe_unused]] WrenErrorType type, const char* module, int line,
                const char* message)
{
	if (module == nullptr)
		module = "<unknown>";
	PD2HOOK_LOG_LOG(string("[WREN ERR] ") + string(module) + ":" + to_string(line) + " ] " + message);
}

static void log(WrenVM* vm)
{
	const char* text = wrenGetSlotString(vm, 1);
	PD2HOOK_LOG_LOG(string("[WREN] ") + text);
}

static string file_to_string(ifstream& in)
{
	return string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

void io_listDirectory(WrenVM* vm)
{
	string filename = wrenGetSlotString(vm, 1);
	bool dir = wrenGetSlotBool(vm, 2);
	vector<string> files = Util::GetDirectoryContents(filename, dir);

	wrenSetSlotNewList(vm, 0);

	for (string const& file : files)
	{
		if (file == "." || file == "..")
			continue;

		wrenSetSlotString(vm, 1, file.c_str());
		wrenInsertInList(vm, 0, -1, 1);
	}
}

void io_info(WrenVM* vm)
{
	const char* path = wrenGetSlotString(vm, 1);

	Util::FileType type = Util::GetFileType(path);

	if (type == Util::FileType_None)
	{
		wrenSetSlotString(vm, 0, "none");
	}
	else if (type == Util::FileType_Directory)
	{
		wrenSetSlotString(vm, 0, "dir");
	}
	else
	{
		wrenSetSlotString(vm, 0, "file");
	}
}

void io_read(WrenVM* vm)
{
	string file = wrenGetSlotString(vm, 1);

	ifstream handle(file);
	if (!handle.good())
	{
		PD2HOOK_LOG_ERROR("Wren IO.read: Could not load file " + file);
		exit(1);
	}

	string contents = file_to_string(handle);
	wrenSetSlotString(vm, 0, contents.c_str());
}

void io_idstring_hash(WrenVM* vm)
{
	blt::idstring hash = blt::idstring_hash(wrenGetSlotString(vm, 1));

	char hex[17]; // 16-chars long +1 for the null
	snprintf(hex, sizeof(hex), IDPF, hash);
	wrenSetSlotString(vm, 0, hex);
}

static void io_load_plugin(WrenVM* vm)
{
	const char* plugin_filename = wrenGetSlotString(vm, 1);
	try
	{
		blt::plugins::LoadPlugin(plugin_filename);
	}
	catch (const string& err)
	{
		string msg = string("LoadPlugin: ") + string(plugin_filename) + string(" : ") + err;
		wrenSetSlotString(vm, 0, msg.c_str());
		wrenAbortFiber(vm, 0);
	}
}

static void io_has_native_module(WrenVM* vm)
{
	const char* module = wrenGetSlotString(vm, 1);
	const char* source = nullptr;
	lookup_builtin_wren_src(module, &source);
	wrenSetSlotBool(vm, 0, source != nullptr);
}

static void internal_set_tweaker_enabled(WrenVM* vm)
{
	pd2hook::tweaker::tweaker_enabled = wrenGetSlotBool(vm, 1);
}

static void internal_register_mod_v1(WrenVM* vm)
{
	int slotType;

	if ((slotType = wrenGetSlotType(vm, 1)) != WREN_TYPE_STRING)
	{
		char str[64];
		memset(str, 0, sizeof(str));
		snprintf(str, sizeof(str) - 1, "Invalid mod data: name is non-string type %d", slotType);
		wrenSetSlotString(vm, 0, str);
		wrenAbortFiber(vm, 0);
		return;
	}

	if ((slotType = wrenGetSlotType(vm, 2)) != WREN_TYPE_STRING)
	{
		char str[64];
		memset(str, 0, sizeof(str));
		snprintf(str, sizeof(str) - 1, "Invalid mod data: scripts_root is non-string type %d", slotType);
		wrenSetSlotString(vm, 0, str);
		wrenAbortFiber(vm, 0);
		return;
	}

	std::string name = wrenGetSlotString(vm, 1);
	ModData data = {};
	data.name = name;
	data.scripts_root = wrenGetSlotString(vm, 2);
	mod_metadata[name] = std::move(data); // Can't use data.name as the index value, the order is undefined
}

static void internal_warn_bad_mod(WrenVM* vm)
{
	std::string file = wrenGetSlotString(vm, 1);
	std::string err = wrenGetSlotString(vm, 2);
	std::string message = "Failed to load Wren mod file '" + file + "': '" + err + "'";

	// Always log, even on Windows, so a mod author can track it down if the user doesn't mention
	// it in a bug report for whatever reason.
	PD2HOOK_LOG_ERROR(message.c_str());

#ifdef _WIN32
	MessageBoxA(0, message.c_str(), "SuperBLT: Failed to load Wren mod", MB_OK);
#endif
}

static WrenForeignClassMethods bindForeignClass(WrenVM* vm, const char* module, const char* class_name)
{
	WrenForeignClassMethods methods = wrenxml::get_XML_class_def(vm, module, class_name);
	if (methods.allocate || methods.finalize)
		return methods;

	methods = dbhook::bind_dbhook_class(vm, module, class_name);

	return methods;
}

static WrenForeignMethodFn bindForeignMethod(WrenVM* vm, const char* module, const char* className, bool isStatic,
                                             const char* signature)
{
	WrenForeignMethodFn wxml_method = wrenxml::bind_wxml_method(vm, module, className, isStatic, signature);
	if (wxml_method)
		return wxml_method;

	WrenForeignMethodFn dbhook_method = dbhook::bind_dbhook_method(vm, module, className, isStatic, signature);
	if (dbhook_method)
		return dbhook_method;

	WrenForeignMethodFn lua_io_method = lua_io::bind_wren_lua_method(vm, module, className, isStatic, signature);
	if (lua_io_method)
		return lua_io_method;

	WrenForeignMethodFn env_method = wren_env::bind_wren_env_method(vm, module, className, isStatic, signature);
	if (env_method)
		return env_method;

	WrenForeignMethodFn util_method = wren_utils::bind_wren_utils_method(vm, module, className, isStatic, signature);
	if (util_method)
		return util_method;

	if (strcmp(module, "base/native") == 0)
	{
		if (strcmp(className, "Logger") == 0)
		{
			if (isStatic && strcmp(signature, "log(_)") == 0)
			{
				return &log; // C function for Math.add(_,_).
			}
			// Other foreign methods on Math...
		}
		else if (strcmp(className, "IO") == 0)
		{
			if (isStatic && strcmp(signature, "listDirectory(_,_)") == 0)
			{
				return &io_listDirectory;
			}
			if (isStatic && strcmp(signature, "info(_)") == 0)
			{
				return &io_info;
			}
			if (isStatic && strcmp(signature, "read(_)") == 0)
			{
				return &io_read;
			}
			if (isStatic && strcmp(signature, "idstring_hash(_)") == 0)
			{
				return &io_idstring_hash;
			}
			if (isStatic && strcmp(signature, "load_plugin(_)") == 0)
			{
				return &io_load_plugin;
			}
			if (isStatic && strcmp(signature, "has_native_module(_)") == 0)
			{
				return &io_has_native_module;
			}
		}
		// Other classes in main...
	}
	else if (strcmp(module, "base/native/internal_001") == 0)
	{
		if (strcmp(className, "Internal") == 0)
		{
			if (isStatic && strcmp(signature, "tweaker_enabled=(_)") == 0)
			{
				return &internal_set_tweaker_enabled;
			}
			else if (isStatic && strcmp(signature, "warn_bad_mod(_,_)") == 0)
			{
				return &internal_warn_bad_mod;
			}
			else if (isStatic && strcmp(signature, "register_mod_v1(_,_)") == 0)
			{
				return &internal_register_mod_v1;
			}
		}
	}
	// Other modules...

	return nullptr;
}

const char* resolveModule(WrenVM* vm, const char* importer, const char* name)
{
	// If this is being imported by IO.dynamic_import, find out who called that
	// (filter out meta since that's the one file that base/native imports itself, and that always resolves
	//  to itself anyway so skipping this wouldn't matter)
	// (also note we can get away with this since we now embed base/native anyway)
	if (strcmp(importer, "base/native") == 0 && strcmp(name, "meta") != 0)
	{
		// Poke around in Wren's internals to do this - not ideal but hey it works (for now...)
		ObjFiber* fibre = vm->fiber;

		// At this point, the callstack will look like this (bottom=newest, same as python):
		//
		// -4  «caller module»           caller()
		// -3      base/native  dynamic_import(_)
		// -2             meta            eval(_)  <- This is the Wren function which wraps the native _compile function
		// -1      base/native           (script)  <- This is a script that contains only an import statement
		//
		// (also if it's not obvious already, this is very much reliant on Wren's implementation details - be
		//  careful when updating it should they decide to change how meta is implemented)
		// This means we have to subtract four from the frame count to get our frame - one to go from the length
		//  of the array to the index of the last element, then three to skip dynamic_import, eval and the import
		//  script (the bit of code that contains the actual import statement that base/native builds).

		assert(fibre->numFrames >= 4); // Make sure we have enough frames here
		ObjModule* origin = fibre->frames[fibre->numFrames - 4].closure->fn->module;
		importer = origin->name->value;

		// Little utility to generate traces like above:
		// printf("Caught native import: %s -> %s\n", importer, name);
		// for (int i = 0; i < fibre->numFrames; i++)
		// {
		// 	CallFrame* frame = &fibre->frames[i];
		// 	printf("Frame: %30s %30s\n", frame->closure->fn->module->name->value, frame->closure->fn->debug->name);
		// }
	}

	// Rules for imports:
	// Anything starting with ./ means relative to this module
	// Anything starting with .../ means relative to this mod
	// Anything starting with base/ refers to files in the basemod or DLL
	// Anything starting with mods/ refers to published files in another mod - yet to figure out how this will work
	// Anything starting with __raw_force_load/ refers to the rest of that string directly (please don't abuse this!)
	// The 'meta' and 'random' modules from Wren resolve directly to themselves
	// Anything else is reserved and may be used for something else later

	// The actual module name is still in the format of mod/path/a/b/c which would load to mods/mod/path/a/b/c.wren

	// Relative imports
	if (strncmp(name, "./", 2) == 0)
	{
		string new_module = importer;
		size_t last_stroke = new_module.find_last_of('/');

		// Must always be there
		assert(last_stroke != string::npos);

		// Chop off the module name and replace it with what we wanted
		new_module.erase(last_stroke + 1);
		new_module.append(name + 2);
		return strdup(new_module.c_str());
	}

	// Mod-relative imports
	if (strncmp(name, ".../", 4) == 0)
	{
		string new_module = importer;
		size_t first_stroke = new_module.find_first_of('/');

		// Must always be there
		assert(first_stroke != string::npos);

		// Chop off the module name and replace it with what we wanted
		new_module.erase(first_stroke + 1);
		new_module.append(name + 4);
		return strdup(new_module.c_str());
	}

	// Base imports
	if (strncmp(name, "base/", 5) == 0)
	{
		// Block importing anything other than base/native and base/native/* unless this is coming from the base module
		if (strcmp(name, "base/native") == 0 || strncmp(name, "base/native/", 12) == 0)
			return name;

		// Also permit base/api/* which is where APIs exported by the basemod go
		if (strncmp(name, "base/api/", 9) == 0)
			return name;

		// So since it's not one of the above, make sure this is coming from the base module
		if (strncmp(importer, "base/", 5) == 0)
			return name;
	}

	// Meta and random module
	if (strcmp(name, "meta") == 0 || strcmp(name, "random") == 0)
	{
		return name;
	}

	// The __raw_force_load/ thing
	if (strncmp(name, "__raw_force_load/", strlen("__raw_force_load/")) == 0)
	{
		return strdup(name + strlen("__raw_force_load/"));
	}

	// Special cases:

	// If it's being loaded by the root module (which is the entry point into wren), use the path directly
	if (strcmp(importer, "__root") == 0)
	{
		return name;
	}

	// Anything else is an invalid path
	return nullptr;
}

static WrenLoadModuleResult getModulePath([[maybe_unused]] WrenVM* vm, const char* name_c)
{
	// First see if this is a module that's embedded within SuperBLT
	const char* builtin_string = nullptr;
	lookup_builtin_wren_src(name_c, &builtin_string);
	if (builtin_string)
	{
		WrenLoadModuleResult result{};
		result.source = builtin_string;
		return result;
	}

	// Otherwise it's a normal wren file, load it from the appropriate mod
	string name = name_c;
	string mod = name.substr(0, name.find_first_of('/'));
	string file = name.substr(name.find_first_of('/') + 1);

	// Use the metadata to find where the Wren files are
	const auto& meta_pair = mod_metadata.find(mod);
	std::string scripts_root = "wren";
	if (meta_pair != mod_metadata.end())
	{
		scripts_root = meta_pair->second.scripts_root;
	}

	ifstream handle("mods/" + mod + "/" + scripts_root + "/" + file + ".wren");
	if (!handle.good())
	{
		WrenLoadModuleResult result{};
		return result;
	}

	string str = file_to_string(handle);

	// Perhaps unwisely I used 'continue' as a variable name in xml_loader.wren in the basemod, which
	// is now a keyword. To avoid crashes if someone updates their DLL before updating their basemod, check
	// for that and hack around it as necessary.
	if (name == "base/private/xml_loader" && str.find("var continue = dive_tweak_elem") != std::string::npos)
	{
		// Oh by the way, thanks C++ for not having a string find-replace function (unless I can't find it)
		size_t pos = 0;
		while ((pos = str.find("continue", pos)) != std::string::npos)
		{
			str.replace(pos, 8, "cont");
		}
		PD2HOOK_LOG_WARN("Patching around an old use of the variable name 'continue'. Please update your basemod.");
	}

	size_t length = str.length() + 1;
	char* output = (char*)malloc(length); // +1 for the null
	portable_strncpy(output, str.c_str(), length);

	WrenLoadModuleResult result{};
	result.source = output;
	result.onComplete = [](WrenVM*, const char* module, WrenLoadModuleResult result) { free((void*)result.source); };
	return result;
}

std::lock_guard<std::recursive_mutex> pd2hook::wren::lock_wren_vm()
{
	static std::recursive_mutex vm_mutex;
	return std::lock_guard<std::recursive_mutex>(vm_mutex);
}

WrenVM* pd2hook::wren::get_wren_vm()
{
	auto lock = lock_wren_vm();

	static bool available = true;
	static WrenVM* vm = nullptr;

	if (vm == nullptr)
	{
		if (available)
		{
			// If the main file doesn't exist, do nothing
			Util::FileType ftyp = Util::GetFileType("mods/base/wren/base.wren");
			if (ftyp == Util::FileType_None)
			{
				PD2HOOK_LOG_WARN("Wren base file not found, Wren VM disabled - the basemod may be corrupted");
				available = false;
			}
		}

		if (!available)
			return nullptr;

		WrenConfiguration config;
		wrenInitConfiguration(&config);
		config.errorFn = &err;
		config.bindForeignMethodFn = &bindForeignMethod;
		config.bindForeignClassFn = &bindForeignClass;
		config.resolveModuleFn = &resolveModule;
		config.loadModuleFn = &getModulePath;
		vm = wrenNewVM(&config);

		WrenInterpretResult result = wrenInterpret(vm, "__root", R"!( import "base/base" )!");
		if (result == WREN_RESULT_COMPILE_ERROR || result == WREN_RESULT_RUNTIME_ERROR)
		{
			PD2HOOK_LOG_ERROR("Wren init failed: compile or runtime error!");

#ifdef _WIN32
			MessageBox(nullptr, "Failed to initialise the Wren system - see the log in mods/logs/ for details", "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}
	}

	return vm;
}

const char* tweaker::transform_file(const char* text)
{
	auto lock = pd2hook::wren::lock_wren_vm();
	WrenVM* vm = pd2hook::wren::get_wren_vm();

	// If the Wren runtime is unavailable, obviously we can't apply any tweaks
	if (!vm)
		return text;

	wrenEnsureSlots(vm, 4);

	wrenGetVariable(vm, "base/base", "BaseTweaker", 0);
	WrenHandle* tweakerClass = wrenGetSlotHandle(vm, 0);
	WrenHandle* sig = wrenMakeCallHandle(vm, "tweak(_,_,_)");

	char hex[17]; // 16-chars long +1 for the null

	wrenSetSlotHandle(vm, 0, tweakerClass);

	snprintf(hex, sizeof(hex), IDPF, *blt::platform::last_loaded_name);
	wrenSetSlotString(vm, 1, hex);

	snprintf(hex, sizeof(hex), IDPF, *blt::platform::last_loaded_ext);
	wrenSetSlotString(vm, 2, hex);

	wrenSetSlotString(vm, 3, text);

	// TODO give a reasonable amount of information on what happened.
	WrenInterpretResult result2 = wrenCall(vm, sig);
	if (result2 == WREN_RESULT_COMPILE_ERROR)
	{
		PD2HOOK_LOG_ERROR("Wren tweak file failed: compile error!");
		return text;
	}
	else if (result2 == WREN_RESULT_RUNTIME_ERROR)
	{
		PD2HOOK_LOG_ERROR("Wren tweak file failed: runtime error!");
		return text;
	}

	wrenReleaseHandle(vm, tweakerClass);
	wrenReleaseHandle(vm, sig);

	const char* new_text = wrenGetSlotString(vm, 0);

	return new_text;
}
