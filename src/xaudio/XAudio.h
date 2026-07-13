#pragma once

#ifdef ENABLE_XAUDIO

#include <AL/alc.h>

namespace pd2hook
{

	class XAudio
	{
	public:
		static void Register(void *state);
		bool CheckDevice(bool force_reopen);
		const char* GetDeviceName();

		// Please don't use, for internal use only
		static XAudio* GetXAudioInstance();
	private:
		XAudio();
		~XAudio();

		ALCdevice *dev;
		ALCcontext *ctx;
	};

};

#endif
