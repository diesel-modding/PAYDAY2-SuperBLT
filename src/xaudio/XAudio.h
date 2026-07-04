#pragma once

#ifdef ENABLE_XAUDIO

namespace sblt
{

	class XAudio
	{
	  public:
		static void Register(void* state);

		// Please don't use, for internal use only
		static XAudio* GetXAudioInstance();

	  private:
		XAudio();
		~XAudio();
	};

}; // namespace sblt

#endif
