#pragma once
#include "lua.h"

namespace blt
{
#define idstring_none 0

#define IDPF "%016llx" // IDPF=IDstring PrintF
	typedef unsigned long long idstring;

#define IDPFP IDPF "." IDPF

	class idfile
	{
	  public:
		idfile() : name(idstring_none), ext(idstring_none)
		{
		}
		idfile(idstring name, idstring ext) : name(name), ext(ext)
		{
		}
		idstring name;
		idstring ext;

		inline bool operator==(const idfile& other) const
		{
			return other.name == name && other.ext == ext;
		}

		// Required for std::less to function on Windows
		inline bool operator<(const idfile& other) const
		{
			return (name != other.name) ? name < other.name : ext < other.ext;
		}

		[[nodiscard]] inline bool is_empty() const
		{
			return name == idstring_none && ext == idstring_none;
		}
	};

	namespace platform
	{
		extern idstring *last_loaded_name, *last_loaded_ext;

		void InitPlatform();
		void ClosePlatform();

		void GetPlatformInformation(lua_State* L);

		namespace lua
		{
			bool GetForcePCalls();
			void SetForcePCalls(bool);
		}; // namespace lua

		namespace win32
		{
			void OpenConsole();
			void* get_lua_func(const char* name);
		}; // namespace win32


		namespace wwise
		{
			// Custom soundbanks that need to be loaded must be registered somewhere since Diesel tries to find
			// hashed names
			void RegisterCustomSoundbank(const char* dbPath);
			void UnregisterCustomSoundbank(const char* dbPath);

			void RegisterOverridenStreamedWem(unsigned int wemId);
			void RegisterCustomStreamedWemPath(unsigned int wemId, const char* dbPath);
			void UnregisterCustomStreamedWemPath(unsigned int wemId);
		};

	}; // namespace platform
}; // namespace blt
