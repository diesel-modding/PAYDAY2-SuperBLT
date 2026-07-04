
#include "XAudio.h"

#ifdef ENABLE_XAUDIO

#include "XAudioInternal.h"
#include "stb_vorbis.h"

namespace sblt
{
	using namespace xaudio;

	void xabuffer::XABuffer::ALClose()
	{
		openBuffers.erase(filename);
		alDeleteBuffers(1, &alhandle);
	}

	int xabuffer::lX_loadbuffer(lua_State* L)
	{
		ALERR;

		size_t count = lua_gettop(L);

		ALuint buffers[32];

		if (count > 32)
		{
			XAERR("Attempted to create more than 32 ALbuffers in a single call!");
		}

		vector<string> filenames;
		for (size_t i = 0; i < count; i++)
		{
			// i+1 because the Lua stack starts at 1, not 0
			filenames.push_back(lua_tostring(L, i + 1));
		}
		lua_settop(L, 0);

		alGenBuffers(count, buffers);
		ALERR;

		for (size_t i = 0; i < count; i++)
		{
			string filename = filenames[i];

			if (openBuffers.contains(filename))
			{
				void* handle_mem = lua_newuserdata(L, sizeof(XALuaHandle));
				new (handle_mem) XALuaHandle(openBuffers[filename]);
				// TODO don't create buffers for cached stuff
			}
			else
			{
				// Load the contents of the buffer

				int samples, channels, sampleRate;
				short* data;

				samples = stb_vorbis_decode_filename(filename.c_str(), &channels, &sampleRate, &data);

				// Find the relevent errors
				if (samples == -1)
					luaL_error(L, "blt.xaudio.loadbuffer: FileNotFound %s", filename.c_str());
				if (samples == -2)
					luaL_error(L, "blt.xaudio.loadbuffer: OutOfMemory");

				// Copy the file into our buffer
				// TODO do this in the background
				alBufferData(buffers[i], channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, data,
				             samples * sizeof(short) * channels, sampleRate);

				free(data);

				// Create the Lua object
				XABuffer* buff = new XABuffer(buffers[i], filename, samples, sampleRate);
				*(XALuaHandle*)lua_newuserdata(L, sizeof(XALuaHandle)) = XALuaHandle(buff);

				// Cache it
				openBuffers[filename] = buff;

				ALERR;
			}

			// Set the metatable
			luaL_getmetatable(L, "XAudio.buffer");
			lua_setmetatable(L, -2);
		}

		return count;
	}

	XA_CLASS_LUA_CLOSE(xabuffer::XABuffer, lua_toboolean(L, 2));
	XA_CLASS_LUA_METHOD(xabuffer::XABuffer, GetSampleCount, integer);
	XA_CLASS_LUA_METHOD(xabuffer::XABuffer, GetSampleRate, integer);
}; // namespace sblt

#endif
