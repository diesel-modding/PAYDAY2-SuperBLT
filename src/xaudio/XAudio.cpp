
#include "XAudio.h"

#ifdef ENABLE_XAUDIO

#include "XAudioInternal.h"

#ifndef ALC_ALL_DEVICES_SPECIFIER // ALC_ENUMERATE_ALL_EXT

#define ALC_ALL_DEVICES_SPECIFIER 0x1013

#endif

#ifndef ALC_DEFAULT_ALL_DEVICES_SPECIFIER

#define ALC_DEFAULT_ALL_DEVICES_SPECIFIER 0x1012

#endif

#ifndef ALC_CONNECTED // ALC_EXT_disconnect

#define ALC_CONNECTED 0x313

#endif



namespace pd2hook
{
	namespace xaudio
	{
		static LPALCREOPENDEVICESOFT palcReopenDeviceSOFT = nullptr;
		static bool has_disconnect_ext = false;
		static bool has_all_devices_ext = false;
		static std::string last_default_name;
		double world_scale = 1;

		map<string, xabuffer::XABuffer*> openBuffers;
		unordered_set<xasource::XASource*> openSources;

		bool is_setup = false;
		
		
	};

	using namespace xaudio;

	static void reset_cleanup()
	{
		// When changing heists, we don't need old sounds anymore.

		// Delete all sources.
		// Iterate over a copy so there aren't issues when the items are deleted
		unordered_set<xasource::XASource*> openSourcesCopy = openSources;
		for (xasource::XASource* source : openSourcesCopy)
		{
			source->Close();

			delete source;
		}

		// In case any of the sources didn't remove themselves
		openSources.clear();
	}

	// XAResource
	void XAResource::Discard(bool force)
	{
		usecount--;

		if (force && usecount <= 0)
		{
			Close();
		}
	}
	void XAResource::Close()
	{
		if (!valid) return;
		valid = false;

		// Actually close the resource
		// dependent on type
		ALClose();
	}

	// XALuaHandle
	XALuaHandle::XALuaHandle(XAResource *resource) : resource(resource)
	{
		resource->Employ();
	}
	ALuint XALuaHandle::Handle(lua_State *L)
	{
		if (!open) luaL_error(L, "Cannot use closed resource!");
		return resource->Handle();
	}
	void XALuaHandle::Close(bool force)
	{
		if (!open) return;

		resource->Discard(force);
		open = false;
	}

	static int lX_setup(lua_State *L)
	{
		try
		{
			XAudio::GetXAudioInstance();
		}
		catch (string msg)
		{
			PD2HOOK_LOG_ERROR("Exception while loading XAudio API: " + msg);
			lua_pushboolean(L, false);
			return 1;
		}
		lua_pushboolean(L, true);
		return 1;
	}

	static int lX_issetup(lua_State *L)
	{
		lua_pushboolean(L, is_setup);
		return 1;
	}

	static int lX_reset(lua_State *L)
	{
		reset_cleanup();
		return 0;
	}

	static int lX_getworldscale(lua_State *L)
	{
		lua_pushnumber(L, world_scale);
		return 1;
	}

	static int lX_setworldscale(lua_State *L)
	{
		world_scale = lua_tonumber(L, 1);
		return 0;
	}

	static int lX_checkdevice(lua_State* L)
	{
		// Never force XAudio to initialise just because someone polls;
		// if no mod has opened the device there is nothing to follow.
		if (!is_setup)
		{
			lua_pushboolean(L, false);
			return 1;
		}
		lua_pushboolean(L, XAudio::GetXAudioInstance()->CheckDevice(false));
		return 1;
	}

	static int lX_reopendevice(lua_State* L)
	{
		if (!is_setup)
		{
			lua_pushboolean(L, false);
			return 1;
		}
		lua_pushboolean(L, XAudio::GetXAudioInstance()->CheckDevice(true));
		return 1;
	}

	static int lX_getdevicename(lua_State* L)
	{
		if (!is_setup)
		{
			lua_pushnil(L);
			return 1;
		}
		const char* name = XAudio::GetXAudioInstance()->GetDeviceName();
		if (name)
			lua_pushstring(L, name);
		else
			lua_pushnil(L);
		return 1;
	}

	XAudio::XAudio()
	{
		ALCint attributes[] = {
			/* Disable HRTF */
			ALC_HRTF_SOFT, ALC_FALSE,

			/* end of list */
			0
		};

		dev = alcOpenDevice(NULL);
		if (!dev)
		{
			throw string("Cannot open OpenAL Device");
		}
		ctx = alcCreateContext(dev, attributes);
		alcMakeContextCurrent(ctx);
		if (!ctx)
		{
			throw string("Could not create OpenAL Context");
		}

		alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
	
		if (alcIsExtensionPresent(dev, "ALC_SOFT_reopen_device"))
		{
			palcReopenDeviceSOFT = (LPALCREOPENDEVICESOFT)alcGetProcAddress(dev, "alcReopenDeviceSOFT");
		}
		has_disconnect_ext = alcIsExtensionPresent(dev, "ALC_EXT_disconnect") == ALC_TRUE;
		has_all_devices_ext = alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE;

		if (palcReopenDeviceSOFT)
		{
			PD2HOOK_LOG_LOG("XAudio: ALC_SOFT_reopen_device available, will follow default audio device changes");
		}
		else
		{
			PD2HOOK_LOG_WARN("XAudio: ALC_SOFT_reopen_device NOT available (OpenAL Soft older than 1.22); "
			                 "sounds will not survive audio device changes");
		}

		PD2HOOK_LOG_LOG("Loaded OpenAL XAudio API");

		if (const ALCchar* def =
			alcGetString(NULL, has_all_devices_ext ? ALC_DEFAULT_ALL_DEVICES_SPECIFIER : ALC_DEFAULT_DEVICE_SPECIFIER))
		{
			last_default_name = def;
		}
	}

	XAudio::~XAudio()
	{
		PD2HOOK_LOG_LOG("Closing OpenAL XAudio API");

		// Delete all sources
		reset_cleanup();

		// Delete all buffers
		for (auto const& pair : openBuffers)
		{
			xabuffer::XABuffer *buff = pair.second;

			buff->Close();

			delete buff;
		}

		// Same as above
		openBuffers.clear();

		// TODO: Make sure above works properly.

		// Close the OpenAL context/device
		alcMakeContextCurrent(NULL);

		// HACK it seems PD2 will happily kill off the OpenAL thread, along with all the
		// others when it's exiting. Calling alcDestroyContext will post a message to this
		// now-dead thread and wait forever for a message to come back.
		// Thus don't stop OpenAL.
		// See https://gitlab.com/znixian/payday2-superblt/-/issues/72
		// alcDestroyContext(ctx);
		// alcCloseDevice(dev);
	}

	XAudio* XAudio::GetXAudioInstance()
	{
		static XAudio audio;
		is_setup = true;
		return &audio;
	}

	void XAudio::Register(void * state)
	{
		lua_State *L = (lua_State*)state;

		// Buffer metatable
		luaL_Reg XABufferLib[] =
		{
			{ "close", xabuffer::XABuffer_Close },
			{ "getsamplecount", xabuffer::XABuffer_GetSampleCount },
			{ "getsamplerate", xabuffer::XABuffer_GetSampleRate },
			{ NULL, NULL }
		};

		luaL_newmetatable(L, "XAudio.buffer");

		lua_pushstring(L, "__index");
		lua_pushvalue(L, -2);  /* pushes the metatable */
		lua_settable(L, -3);  /* metatable.__index = metatable */

		luaL_openlib(L, NULL, XABufferLib, 0);
		lua_pop(L, 1);

		// Source metatable
		luaL_Reg XASourceLib[] =
		{
			{ "close", xasource::XASource_Close },
			{ "setbuffer", xasource::XASource_set_buffer },
			{ "play", xasource::XASource_play },
			{ "pause", xasource::XASource_pause },
			{ "stop", xasource::XASource_stop },
			{ "getstate", xasource::XASource_get_state },
			{ "setposition", xasource::XASource_set_position },
			{ "setvelocity", xasource::XASource_set_velocity },
			{ "setdirection", xasource::XASource_set_direction },

			{ "setmindis", xasource::XASource_set_min_distance },
			{ "setmaxdis", xasource::XASource_set_max_distance },

			{ "getgain", xasource::XASource_get_gain },
			{ "setgain", xasource::XASource_set_gain },

			{ "setlooping", xasource::XASource_SetLooping },
			{ "setrelative", xasource::XASource_SetRelative },
			{ NULL, NULL }
		};

		luaL_newmetatable(L, "XAudio.source");

		lua_pushstring(L, "__index");
		lua_pushvalue(L, -2);  /* pushes the metatable */
		lua_settable(L, -3);  /* metatable.__index = metatable */

		luaL_openlib(L, NULL, XASourceLib, 0);
		lua_pop(L, 1);

		// blt.xaudio table
		luaL_Reg lib[] =
		{
			{ "setup", lX_setup },
			{ "issetup", lX_issetup },
			{ "reset", lX_reset },
			{ "loadbuffer", xabuffer::lX_loadbuffer },
			{ "newsource", xasource::lX_new_source },
			{ "getworldscale", lX_getworldscale },
			{ "setworldscale", lX_setworldscale },
		    {"checkdevice", lX_checkdevice},
		    {"reopendevice", lX_reopendevice},
			{"getdevicename", lX_getdevicename},
			{ NULL, NULL }
		};

		// Grab the BLT table
		lua_getglobal(L, "blt");

		// Make a new table and populate it with XAudio stuff
		lua_newtable(L);
		luaL_openlib(L, NULL, lib, 0);

		// Add the blt.xaudio.listener table
		xalistener::add_members(L);
		// Put listener into xaudio
		lua_setfield(L, -2, "listener");

		// Put that table into the BLT table, calling it XAudio. This removes said table from the stack.
		lua_setfield(L, -2, "xaudio");

		// Remove the BLT table from the stack.
		lua_pop(L, 1);
	}

	bool XAudio::CheckDevice(bool force_reopen)
	{
		if (!palcReopenDeviceSOFT)
			return false;

		// Back off after a failed reopen so we don't hammer WASAPI every poll
		static int backoff = 0;
		static int confirm_polls = 0;
		static std::string pending_name;

		if (backoff > 0 && !force_reopen)
		{
			backoff--;
			return false;
		}

		bool want_reopen = force_reopen;

		// 1) Device invalidated? (endpoint removed or its format changed)
		if (!want_reopen && has_disconnect_ext)
		{
			ALCint connected = ALC_TRUE;
			alcGetIntegerv(dev, ALC_CONNECTED, 1, &connected);
			alcGetError(dev);
			if (!connected)
				want_reopen = true;
		}

		// 2) Has the system default changed since we last (re)opened?
		// Compare against our own history of the default-device string rather
		// than the opened device's specifier: the two query paths can format
		// the same endpoint's name differently, which would cause a reopen
		// loop. Require the new default to be stable for two consecutive
		// polls to ignore transient flapping during device negotiation.
		if (!want_reopen)
		{
			ALCenum def_spec = has_all_devices_ext ? ALC_DEFAULT_ALL_DEVICES_SPECIFIER : ALC_DEFAULT_DEVICE_SPECIFIER;
			const ALCchar* def = alcGetString(NULL, def_spec);
			if (!def)
				return false;

			if (last_default_name.empty())
			{
				last_default_name = def;
				return false;
			}

			if (last_default_name == def)
			{
				confirm_polls = 0;
				return false;
			}

			if (pending_name != def)
			{
				pending_name = def;
				confirm_polls = 1;
				return false;
			}

			if (++confirm_polls < 2)
				return false;

			want_reopen = true;
		}

		if (!want_reopen)
			return false;

		// Reopen on the current default; all buffers, sources and the context
		// survive the move, so playing sounds simply continue on the new output.
		if (palcReopenDeviceSOFT(dev, NULL, NULL))
		{
			ALCenum def_spec = has_all_devices_ext ? ALC_DEFAULT_ALL_DEVICES_SPECIFIER : ALC_DEFAULT_DEVICE_SPECIFIER;
			if (const ALCchar* def = alcGetString(NULL, def_spec))
				last_default_name = def;
			confirm_polls = 0;
			pending_name.clear();

			const char* name = GetDeviceName();
			PD2HOOK_LOG_LOG(string("XAudio: moved OpenAL output to: ") + (name ? name : "(unknown device)"));
			backoff = 0;
			return true;
		}

		PD2HOOK_LOG_WARN("XAudio: alcReopenDeviceSOFT failed, will retry later");
		backoff = 5;
		return false;
	}

	const char* XAudio::GetDeviceName()
	{
		ALCenum cur_spec = has_all_devices_ext ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER;
		return alcGetString(dev, cur_spec);
	}

}

#endif // ENABLE_XAUDIO
