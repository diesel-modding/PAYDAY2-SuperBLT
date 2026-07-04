#pragma once

#include "XAudio.h"

#ifdef ENABLE_XAUDIO

#include <map>
#include <string>
#include <unordered_set>

#include "lua.h"
#include "util/util.h"

using namespace std;

#define WORLD_VEC_CUSTOMSCALE(scale, L, ix, iy, iz) \
	(float)(lua_tonumber(L, ix) / scale), (float)(lua_tonumber(L, iy) / scale), (float)(lua_tonumber(L, iz) / scale)
#define WORLD_VEC(L, ix, iy, iz) WORLD_VEC_CUSTOMSCALE(xaudio::world_scale, L, ix, iy, iz)

#define XAERR(str)                                                                        \
	luaL_error(L, "XAudio Error at %s:%d : %s", __FILE__, __LINE__, string(str).c_str()); \
	throw "luaL_error returned."
/*
#define ALERR { \
    if(!xaudio::is_setup) PD2HOOK_LOG_WARN("XAudio Warning: blt.xaudio.setup() has not been called!"); \
    ALenum error; \
    if ((error = alGetError()) != AL_NO_ERROR) { \
        XAERR("alErr : " + to_string(error)); \
    } \
}*/

#define ALERR

#define XA_CLASS_LUA_METHOD_DEC(klass, method) int klass##_##method(lua_State* L);

#define XA_CLASS_LUA_METHOD_BASE(klass, method)                                   \
	int klass##_##method(lua_State* L)                                            \
	{                                                                             \
		ALERR                                                                     \
		/* TODO: verify type */                                                   \
		xaudio::XALuaHandle* handle = (xaudio::XALuaHandle*)lua_touserdata(L, 1); \
		klass* val = (klass*)handle->Resource();

#define XA_CLASS_LUA_METHOD(klass, method, type, ...) \
	XA_CLASS_LUA_METHOD_BASE(klass, method)           \
	lua_push##type(L, val->method(__VA_ARGS__));      \
	ALERR                                             \
	return 1;                                         \
	}

#define XA_CLASS_LUA_METHOD_VOID(klass, method, ...) \
	XA_CLASS_LUA_METHOD_BASE(klass, method)          \
	val->method(__VA_ARGS__);                        \
	ALERR                                            \
	return 0;                                        \
	}

#define XA_CLASS_LUA_CLOSE(klass, force)                                          \
	int klass##_Close(lua_State* L)                                               \
	{                                                                             \
		xaudio::XALuaHandle* handle = (xaudio::XALuaHandle*)lua_touserdata(L, 1); \
		handle->Close(force);                                                     \
		ALERR                                                                     \
		return 0;                                                                 \
	}

namespace sblt
{
	namespace xasource
	{
		class XASource;
	};
	namespace xabuffer
	{
		class XABuffer;
	};

	namespace xaudio
	{
		class XAResource
		{
		  public:
			XAResource(ALuint alhandle) : alhandle(alhandle)
			{
			}
			virtual ~XAResource()
			{
			}
			ALuint Handle()
			{
				return alhandle;
			}
			void Employ()
			{
				usecount++;
			}
			void Discard(bool force);
			void Close();

		  protected:
			virtual void ALClose() = 0;
			const ALuint alhandle;

		  private:
			bool valid = true;
			int usecount = 0;
		};

		class XALuaHandle
		{
		  public:
			XALuaHandle(XAResource* resource);
			ALuint Handle(lua_State* L);
			void Close(bool force);
			bool Ready()
			{
				return open;
			}
			XAResource* Resource()
			{
				return resource;
			}

		  private:
			XAResource* resource;
			bool open = true;
		};

		extern map<string, xabuffer::XABuffer*> openBuffers;
		extern unordered_set<xasource::XASource*> openSources;
		extern double world_scale;
		extern bool is_setup;
	};

	namespace xabuffer
	{
		class XABuffer : public xaudio::XAResource
		{
		  public:
			XABuffer(ALuint handle, string filename, int sampleCount, int sampleRate)
				: xaudio::XAResource::XAResource(handle), sampleCount(sampleCount), sampleRate(sampleRate),
				  filename(filename)
			{
			}
			double GetSampleCount()
			{
				return sampleCount;
			}
			double GetSampleRate()
			{
				return sampleRate;
			}

		  protected:
			virtual void ALClose();

		  private:
			const int sampleCount;
			const int sampleRate;
			const string filename;
		};

		int lX_loadbuffer(lua_State* L);
		XA_CLASS_LUA_METHOD_DEC(XABuffer, Close);
		XA_CLASS_LUA_METHOD_DEC(XABuffer, GetSampleCount);
		XA_CLASS_LUA_METHOD_DEC(XABuffer, GetSampleRate);
	};

	namespace xasource
	{
		class XASource : public xaudio::XAResource
		{
		  public:
			using xaudio::XAResource::XAResource;
			void SetLooping(bool looping);
			void SetRelative(bool relative);

		  protected:
			virtual void ALClose();
		};

		int lX_new_source(lua_State* L);
		XA_CLASS_LUA_METHOD_DEC(XASource, Close);
		int XASource_set_buffer(lua_State* L);
		int XASource_play(lua_State* L);
		int XASource_pause(lua_State* L);
		int XASource_stop(lua_State* L);
		int XASource_get_state(lua_State* L);

		int XASource_set_position(lua_State* L);
		int XASource_set_velocity(lua_State* L);
		int XASource_set_direction(lua_State* L);

		int XASource_set_min_distance(lua_State* L);
		int XASource_set_max_distance(lua_State* L);

		int XASource_get_gain(lua_State* L);
		int XASource_set_gain(lua_State* L);

		XA_CLASS_LUA_METHOD_DEC(XASource, SetLooping);
		XA_CLASS_LUA_METHOD_DEC(XASource, SetRelative);
	};

	namespace xalistener
	{
		int XAListener_set_velocity(lua_State* L);
		int XAListener_set_position(lua_State* L);
		int XAListener_set_orientation(lua_State* L);
		int XAListener_get_gain(lua_State* L);
		int XAListener_set_gain(lua_State* L);
		void add_members(lua_State* L);
	};
};

#endif
