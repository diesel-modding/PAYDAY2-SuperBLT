#include <dlfcn.h>

#include "plugins/plugins.h"
#include "util/util.h"
#include "InitState.h"

using namespace std;
using namespace blt::plugins;

namespace blt::plugins
{
	typedef void (*init_func_t)();

	class UnixPlugin : public Plugin
	{
	public:
		UnixPlugin(std::string file);
		~UnixPlugin() override;
		bool Unload() const override;

	protected:
		virtual void *ResolveSymbol(std::string name) const;
	private:
		void *module;
	};

	// Export this same as the Windows one, for sake
	// of compatibility
	void superblt_export_unix_logger(const char* message, int level, const char* file, int line)
	{
		using LT = pd2hook::Logging::LogType;

		char buffer[256];
		snprintf(buffer, sizeof(buffer), "ExtModSO %s", file);

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

	// TODO move common logic somewhere
	UnixPlugin::UnixPlugin(std::string file) : Plugin(file)
	{
		module = dlopen(file.c_str(), RTLD_NOW);

		if (!module) throw string("Failed to load module: ERR") + string(dlerror());

		Init();

		// Start loading everything
		init_func_t init = (init_func_t) dlsym(module, "SuperBLT_Plugin_Setup");
		if (!init) throw "Invalid dlhandle - missing initfunc!";

		init();
	}

	UnixPlugin::~UnixPlugin()
	{
	}

	bool UnixPlugin::Unload() const 
	{
		PD2HOOK_LOG_WARN("Unloading is not natively supported on Unix systems.");
		return false;

		/*
		This implementation should work but could be removed as native Linux support is probably gone for good

		// Execute Plugin unload function if it exists
		unload_func_t unload = (unload_func_t) dlsym(module, "SuperBLT_Plugin_Unload");
		if (!unload) {
			PD2HOOK_LOG_ERROR("SuperBLT_Plugin_Unload not found in Plugin. Skipping unload request...");
			return false;	
		}
		
		if (!unload())
			return false;

		dlclose(module);
		return true;
		*/
	}

	void *UnixPlugin::ResolveSymbol(std::string name) const
	{
		return dlsym(module, name.c_str());
	}

	Plugin *CreateNativePlugin(std::string file)
	{
		return new UnixPlugin(file);
	}
};

