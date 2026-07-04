
#include "XAudio.h"

#ifdef ENABLE_XAUDIO

#include "XAudioInternal.h"

namespace sblt
{
	namespace xaudio
	{
		double world_scale = 1;

		map<string, xabuffer::XABuffer*> openBuffers;
		unordered_set<xasource::XASource*> openSources;

		bool is_setup = false;
	}; // namespace xaudio

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
		if (!valid)
			return;
		valid = false;

		// Actually close the resource
		// dependent on type
		ALClose();
	}

	// XALuaHandle
	XALuaHandle::XALuaHandle(XAResource* resource) : resource(resource)
	{
		resource->Employ();
	}
	ALuint XALuaHandle::Handle(lua_State* L)
	{
		if (!open)
			luaL_error(L, "Cannot use closed resource!");
		return resource->Handle();
	}
	void XALuaHandle::Close(bool force)
	{
		if (!open)
			return;

		resource->Discard(force);
		open = false;
	}

	static int lX_setup(lua_State* L)
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

	static int lX_issetup(lua_State* L)
	{
		lua_pushboolean(L, is_setup);
		return 1;
	}

	static int lX_reset(lua_State* L)
	{
		reset_cleanup();
		return 0;
	}

	static int lX_getworldscale(lua_State* L)
	{
		lua_pushnumber(L, world_scale);
		return 1;
	}

	static int lX_setworldscale(lua_State* L)
	{
		world_scale = lua_tonumber(L, 1);
		return 0;
	}

	XAudio::XAudio()
	{
		ALCint attributes[] = {/* Disable HRTF */
		                       ALC_HRTF_SOFT, ALC_FALSE,

		                       /* end of list */
		                       0};

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

		PD2HOOK_LOG_LOG("Loaded OpenAL XAudio API");
	}

	XAudio::~XAudio()
	{
		PD2HOOK_LOG_LOG("Closing OpenAL XAudio API");

		// Delete all sources
		reset_cleanup();

		// Delete all buffers
		for (auto const& pair : openBuffers)
		{
			xabuffer::XABuffer* buff = pair.second;

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

	void XAudio::Register(void* state)
	{
		lua_State* L = (lua_State*)state;

		// Buffer metatable
		luaL_Reg XABufferLib[] = {{"close", xabuffer::XABuffer_Close},
		                          {"getsamplecount", xabuffer::XABuffer_GetSampleCount},
		                          {"getsamplerate", xabuffer::XABuffer_GetSampleRate},
		                          {NULL, NULL}};

		luaL_newmetatable(L, "XAudio.buffer");

		lua_pushstring(L, "__index");
		lua_pushvalue(L, -2); /* pushes the metatable */
		lua_settable(L, -3); /* metatable.__index = metatable */

		luaL_openlib(L, NULL, XABufferLib, 0);
		lua_pop(L, 1);

		// Source metatable
		luaL_Reg XASourceLib[] = {{"close", xasource::XASource_Close},
		                          {"setbuffer", xasource::XASource_set_buffer},
		                          {"play", xasource::XASource_play},
		                          {"pause", xasource::XASource_pause},
		                          {"stop", xasource::XASource_stop},
		                          {"getstate", xasource::XASource_get_state},
		                          {"setposition", xasource::XASource_set_position},
		                          {"setvelocity", xasource::XASource_set_velocity},
		                          {"setdirection", xasource::XASource_set_direction},

		                          {"setmindis", xasource::XASource_set_min_distance},
		                          {"setmaxdis", xasource::XASource_set_max_distance},

		                          {"getgain", xasource::XASource_get_gain},
		                          {"setgain", xasource::XASource_set_gain},

		                          {"setlooping", xasource::XASource_SetLooping},
		                          {"setrelative", xasource::XASource_SetRelative},
		                          {NULL, NULL}};

		luaL_newmetatable(L, "XAudio.source");

		lua_pushstring(L, "__index");
		lua_pushvalue(L, -2); /* pushes the metatable */
		lua_settable(L, -3); /* metatable.__index = metatable */

		luaL_openlib(L, NULL, XASourceLib, 0);
		lua_pop(L, 1);

		// blt.xaudio table
		luaL_Reg lib[] = {{"setup", lX_setup},
		                  {"issetup", lX_issetup},
		                  {"reset", lX_reset},
		                  {"loadbuffer", xabuffer::lX_loadbuffer},
		                  {"newsource", xasource::lX_new_source},
		                  {"getworldscale", lX_getworldscale},
		                  {"setworldscale", lX_setworldscale},
		                  {NULL, NULL}};

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

} // namespace sblt

#endif // ENABLE_XAUDIO
