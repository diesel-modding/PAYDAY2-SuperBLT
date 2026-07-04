#ifndef __INIT_STATE__
#define __INIT_STATE__

namespace raidhook
{
	void InitiateStates();
	void DestroyStates();

	bool check_active_state(void* L);
} // namespace raidhook

#endif