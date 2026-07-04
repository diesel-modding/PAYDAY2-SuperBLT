
#include "XAudio.h"

#ifdef ENABLE_XAUDIO

#include "XAudioInternal.h"

namespace sblt
{
	using namespace pd2hook::xaudio;

	static void set_vector_property(lua_State* L, ALenum type)
	{
		// TODO we can use the listener property in L.1

		alListener3f(type, WORLD_VEC(L, 2, 3, 4));
	}

	int xalistener::XAListener_set_position(lua_State* L)
	{
		set_vector_property(L, AL_POSITION);
		return 0;
	}

	int xalistener::XAListener_set_velocity(lua_State* L)
	{
		set_vector_property(L, AL_VELOCITY);
		return 0;
	}

	int xalistener::XAListener_set_orientation(lua_State* L)
	{
		// TODO we can use the listener property in L.1

		ALfloat orientation[6] = {
			WORLD_VEC_CUSTOMSCALE(1, L, 2, 3, 4), // Forward vector
			WORLD_VEC_CUSTOMSCALE(1, L, 5, 6, 7) // Up vector
		};

		alListenerfv(AL_ORIENTATION, orientation);

		return 0;
	}

	int xalistener::XAListener_get_gain(lua_State* L)
	{
		ALfloat value;
		alGetListenerf(AL_GAIN, &value);
		lua_pushnumber(L, value);

		return 1;
	}

	int xalistener::XAListener_set_gain(lua_State* L)
	{
		ALfloat value = lua_tonumber(L, 2);
		alListenerf(AL_GAIN, value);

		return 0;
	}

	void xalistener::add_members(lua_State* L)
	{
		// blt.xaudio.listener table
		luaL_Reg lib[] = {{"setposition", xalistener::XAListener_set_position},
		                  {"setvelocity", xalistener::XAListener_set_velocity},
		                  {"setorientation", xalistener::XAListener_set_orientation},
		                  {"getgain", xalistener::XAListener_get_gain},
		                  {"setgain", xalistener::XAListener_set_gain},
		                  {NULL, NULL}};

		// Make a new table and populate it with XAudio stuff
		lua_newtable(L);
		luaL_openlib(L, NULL, lib, 0);
	}
}; // namespace sblt

#endif
