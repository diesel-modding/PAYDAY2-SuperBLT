#pragma once
#include <lua.h>
#include <string>
#include <list>

#ifdef WIN32
#include <windows.h>
#endif

namespace blt
{
	namespace plugins
	{
		typedef void(*update_func_t)(lua_State *L);
		typedef void(*setup_state_func_t)(lua_State *L);
		typedef int(*push_lua_func_t)(lua_State *L);
		typedef bool(*unload_func_t)();

		class Plugin
		{
		public:
			Plugin(std::string file);
			Plugin(Plugin&) = delete;
			virtual ~Plugin();

			void AddToState(lua_State *L);

			void Update(lua_State *L);

			const std::string GetFile() const
			{
				return file;
			}

			int PushLuaValue(lua_State *L);
			virtual bool Unload() const = 0;

		protected:
			void Init();

			update_func_t update_func;
			setup_state_func_t setup_state;
			push_lua_func_t push_lua;

			virtual void *ResolveSymbol(std::string name) const = 0;

		private:
			std::string file;
		};

		enum PluginLoadResult
		{
			plr_Success,
			plr_AlreadyLoaded
		};

		PluginLoadResult LoadPlugin(std::string file, Plugin **out_plugin = NULL);
		const std::list<Plugin*> &GetPlugins();

		enum PluginUnloadResult
		{
			pur_Failure,
			pur_Success,
			pur_NotLoaded
		};

		PluginUnloadResult UnloadPlugin(std::string file);

		// Implemented in InitiateState
		// TODO find a cleaner solution
		void RegisterPluginForActiveStates(Plugin *plugin);

		// Implemented per-platform, creates the correct plugin object
		// This should NOT be used outside LoadPlugin()
		Plugin *CreateNativePlugin(std::string);
	}
}
