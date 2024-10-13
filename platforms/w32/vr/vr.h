#ifndef __VR_HEADER__
#define __VR_HEADER__

#include <string>

namespace pd2hook
{
	class VRManager
	{
	  private:
		VRManager();

	  public:
		~VRManager();

		bool IsLoaded();

		std::string GetHMDBrand();

		uint64_t GetButtonsStatus(int hand);

		static VRManager* VRManager::GetInstance();

		static void CheckAndLoad();

	  private:
	};
} // namespace pd2hook

#endif // __VR_HEADER__
