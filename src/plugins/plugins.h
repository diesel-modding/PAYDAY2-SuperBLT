#pragma once
#include <list>
#include <lua.h>
#include <string>
#include <windows.h>

namespace blt
{
	namespace plugins
	{
		typedef void (*update_func_t)(lua_State* L);
		typedef void (*setup_state_func_t)(lua_State* L);
		typedef int (*push_lua_func_t)(lua_State* L);

		class Plugin
		{
		  public:
			Plugin(std::string file);
			Plugin(Plugin&) = delete;

			void AddToState(lua_State* L);

			void Update(lua_State* L);

			const std::string GetFile() const
			{
				return file;
			}

			int PushLuaValue(lua_State* L);

		  protected:
			void Init();

			update_func_t update_func;
			setup_state_func_t setup_state;
			push_lua_func_t push_lua;

			virtual void* ResolveSymbol(std::string name) const = 0;

		  private:
			std::string file;
		};

		enum PluginLoadResult
		{
			plr_Success,
			plr_AlreadyLoaded
		};

		PluginLoadResult LoadPlugin(std::string file, Plugin** out_plugin = NULL);
		const std::list<Plugin*>& GetPlugins();

		// Implemented in InitiateState
		// TODO find a cleaner solution
		void RegisterPluginForActiveStates(Plugin* plugin);

		// Implemented per-platform, creates the correct plugin object
		// This should NOT be used outside LoadPlugin()
		Plugin* CreateNativePlugin(std::string);
	} // namespace plugins
} // namespace blt
