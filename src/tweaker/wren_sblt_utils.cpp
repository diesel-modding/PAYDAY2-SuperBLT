//
// Note this and it's associated header file are named wren_sblt_utils not wren_utils to
// avoid conflicting with wren_utils.c in Wren.
//
// Created by znix on 08/05/2021.
//

#include "wren_sblt_utils.h"

#include <util/util.h>

#include <stdio.h>
#include <string.h>

#include <string>

static void normalise_hash(WrenVM* vm)
{
	int slotType = wrenGetSlotType(vm, 1);
	if (slotType != WREN_TYPE_STRING)
	{
		char str[64];
		memset(str, 0, sizeof(str));
		snprintf(str, sizeof(str) - 1, "Invalid hash path: path is non-string type %d", slotType);
		wrenSetSlotString(vm, 0, str);
		wrenAbortFiber(vm, 0);
		return;
	}

	std::string str = wrenGetSlotString(vm, 1);

	blt::idstring hash;
	if (str.empty() || str.at(0) != '@')
	{
		hash = blt::idstring_hash(str);
	}
	else if (str.size() != 17)
	{
		char msg[128];
		memset(msg, 0, sizeof(msg));
		snprintf(msg, sizeof(msg) - 1, "Invalid hash path: bad length %d for %s", (int)str.size(), str.c_str());
		wrenSetSlotString(vm, 0, msg);
		wrenAbortFiber(vm, 0);
		return;
	}
	else
	{
		char* end_ptr = nullptr;
		hash = strtoull(str.c_str() + 1, &end_ptr, 16);
		if (*end_ptr)
		{
			int pos = (int)(end_ptr - str.c_str());
			char msg[128];
			memset(msg, 0, sizeof(msg));
			snprintf(msg, sizeof(msg) - 1, "Invalid hash path: bad hash at char %d ('%c'): %s", pos, *end_ptr,
			         str.c_str());
			wrenSetSlotString(vm, 0, msg);
			wrenAbortFiber(vm, 0);
			return;
		}
	}

	char result[24];
	memset(result, 0, sizeof(result));
	snprintf(result, sizeof(result) - 1, "@" IDPF, hash);
	wrenSetSlotString(vm, 0, result);
}

WrenForeignMethodFn raidhook::tweaker::wren_utils::bind_wren_utils_method(WrenVM* vm, const char* module,
                                                                          const char* className, bool isStatic,
                                                                          const char* signature)
{
	if (strcmp(module, "base/native/Utils_001") == 0)
	{
		if (strcmp(className, "Utils") == 0)
		{
			if (isStatic && strcmp(signature, "normalise_hash(_)") == 0)
			{
				return &normalise_hash;
			}
		}
	}

	return nullptr;
}
