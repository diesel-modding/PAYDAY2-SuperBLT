#include "vr.h"
#include "openvr.h"
#include "subhook.h"
#include "util/util.h"

using namespace vr;

static subhook::Hook gameUpdateDetour;

namespace pd2hook
{

	typedef void*(__cdecl* VR_GetGenericInterface_t)(const char* pchInterfaceVersion, EVRInitError* peError);

	VR_GetGenericInterface_t func;
	IVRSystem* steamvr = 0;
	void* VR_CALLTYPE __cdecl VR_GetGenericInterface_hook(const char* pchInterfaceVersion, EVRInitError* peError)
	{
		subhook::ScopedHookRemove scoped_remove(&gameUpdateDetour);

		void* res = func(pchInterfaceVersion, peError);

		if (!strcmp(IVRSystem_Version, pchInterfaceVersion))
		{
			steamvr = (IVRSystem*)res;
		}

		return res;
	}

	VRManager::VRManager()
	{
		PD2HOOK_LOG_LOG("Hooking SteamVR");

		HMODULE api = GetModuleHandle("openvr_api.dll");
		func = (VR_GetGenericInterface_t)GetProcAddress(api, "VR_GetGenericInterface");

		gameUpdateDetour.Install((void**)&func, VR_GetGenericInterface_hook);

#ifdef INJECTABLE_BLT
		// We will be too late to get the SteamVR call, so load it ourselves
		// FIXME this doesn't seem to work, but it at least prevents crashes
		EVRInitError err;
		func(IVRSystem_Version, &err);
#endif
	}

	VRManager::~VRManager()
	{
	}

	bool VRManager::IsLoaded()
	{
		return steamvr;
	}

	std::string VRManager::GetHMDBrand()
	{
		if (steamvr == NULL)
			return "";

		char name[100];
		uint32_t len = steamvr->GetStringTrackedDeviceProperty(k_unTrackedDeviceIndex_Hmd, Prop_ManufacturerName_String,
		                                                       name, 100);

		if (len == 0)
			return "";

		return std::string(name, len - 1); // SteamVR includes null, std::string doesn't.
	}

	uint64_t VRManager::GetButtonsStatus(int hand)
	{
		if (steamvr == NULL)
			return 0;

		VRControllerState_t state;
		ETrackedControllerRole role = hand == 1 ? TrackedControllerRole_LeftHand : TrackedControllerRole_RightHand;
		int id = steamvr->GetTrackedDeviceIndexForControllerRole(role);
		steamvr->GetControllerState(id, &state, sizeof(VRControllerState_t));

		return state.ulButtonPressed;
	}

	VRManager* VRManager::GetInstance()
	{
		// Just keep an instance open
		static VRManager vrSingleton;
		return &vrSingleton;
	}

	void VRManager::CheckAndLoad()
	{
		if (GetModuleHandle("openvr_api.dll"))
			GetInstance();
	}

} // namespace pd2hook
