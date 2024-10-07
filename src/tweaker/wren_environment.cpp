//
// Created by znix on 08/04/2021.
//

#include "wren_environment.h"

#include <assert.h>
#include <cstdio>
#include <string.h>
#include <string>

#include "util/util.h"

// Hack to get the stack trace
extern "C"
{
#include "../../lib/wren/src/vm/wren_vm.h"
}

void get_mod_directory_impl(WrenVM* vm, int depth)
{
	// Poke around in Wren's internals to do this - not ideal but hey it works (for now...)
	ObjFiber* fibre = vm->fiber;

	int numFrames = fibre->numFrames;
	int frameId = numFrames - 1 - depth;
	if (frameId >= numFrames || frameId < 0)
	{
		char buff[128];
		snprintf(buff, sizeof(buff), "Failed to find mod directory, bad frame ID %d count %d", frameId, numFrames);
		wrenSetSlotString(vm, 0, buff);
		wrenAbortFiber(vm, 0);
		return;
	}

	ObjFn* caller = fibre->frames[frameId].closure->fn;
	std::string callerModule = caller->module->name->value;

	// Chop it down to the mod name
	std::string mod = callerModule;
	int stroke_index = mod.find_first_of('/');

	// If there's no stroke ('/') character, it's something like the meta package
	// We can afford to be a bit intolerant of invalid inputs here, since this is only designed for
	// namespacing stuff, and they should never just be called directly through meta or anything - and
	// if they did it'll likely result in unintended behaviour that will be hard to diagnose.
	if (stroke_index == -1)
	{
		char buff[128];
		snprintf(buff, sizeof(buff), "Non-mod-directory caller path '%s'", callerModule.c_str());
		wrenSetSlotString(vm, 0, buff);
		wrenAbortFiber(vm, 0);
		return;
	}

	// Erase everything after the stroke to leave us with the first part of the module path
	mod.erase(stroke_index);

	// Convert it to the mod directory
	// If we let people change the mod name distinct from it's directory, we'll have to
	// change this.
	std::string dir = "mods/" + mod;

	wrenSetSlotString(vm, 0, dir.c_str());
}

void get_mod_directory(WrenVM* vm)
{
	get_mod_directory_impl(vm, 0);
}

void get_mod_directory_at_depth(WrenVM* vm)
{
	int type = wrenGetSlotType(vm, 1);
	if (type != WREN_TYPE_NUM)
	{
		char buff[128];
		snprintf(buff, sizeof(buff), "Failed to find mod directory depth, invalid non-number type id=%d", type);
		wrenSetSlotString(vm, 0, buff);
		wrenAbortFiber(vm, 0);
		return;
	}

	int depth = (int)wrenGetSlotDouble(vm, 1);

	get_mod_directory_impl(vm, depth);
}

void is_vr(WrenVM* vm)
{
	bool is_vr = pd2hook::Util::IsVr();
	wrenSetSlotBool(vm, 0, is_vr);
}

WrenForeignMethodFn pd2hook::tweaker::wren_env::bind_wren_env_method(WrenVM* vm, const char* module,
                                                                     const char* className, bool isStatic,
                                                                     const char* signature)
{
	if (strcmp(module, "base/native/Environment_001") == 0)
	{
		if (strcmp(className, "Environment") == 0)
		{
			if (isStatic && strcmp(signature, "mod_directory") == 0)
			{
				return &get_mod_directory;
			}
			else if (isStatic && strcmp(signature, "mod_directory_at_depth(_)") == 0)
			{
				return &get_mod_directory_at_depth;
			}
			else if (isStatic && strcmp(signature, "is_vr") == 0)
			{
				return &is_vr;
			}
		}
	}

	return nullptr;
}
