CREATE_NORMAL_CALLABLE_SIGNATURE(lua_call_exe, void, "\x48\x63\xC2\x4C\x8B\xD1\x48\x8B\x51", "xxxxxxxxx", 0, lua_State*,
                                 int, int)
CREATE_NORMAL_CALLABLE_SIGNATURE(
	lua_close_exe, void,
	"\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x00\x48\x8B\x79\x00\x48\x8B\x9F",
	"xxxx?xxxx?xxxx?xxxx?xxx?xxx", 0, lua_State*)

// CREATE_CALLABLE_CLASS_SIGNATURE(ctor_lua_Alloc, void*,
// "\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x4C\x89\x4C\x24\x00\x56\x57\x41\x56\x48\x83\xEC\x00\x49\x8B\xE8",
// "xxxx?xxxx?xxxx?xxxxxxx?xxx", 0, void*, size_t, size_t)
CREATE_CALLABLE_CLASS_SIGNATURE(
	LuaInterface__newstate, void,
	"\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x00\x41\x0F\xB6\xF8\x0F\xB6\xF2",
	"xxxx?xxxx?xxxx?xxxxxxx", 0, bool, bool, char)
CREATE_CALLABLE_CLASS_SIGNATURE(
	application_update, void*,
	"\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x56\x57\x41\x56\x48\x83\xEC\x00\x0F\x29\x74\x24\x00\x0F\x29\x7C\x24",
	"xxxx?xxxx?xxxxxxx?xxxx?xxxx", 0, int*, int*)
CREATE_NORMAL_CALLABLE_SIGNATURE(node_from_xml, void, "\x48\x89\x5C\x24\x00\x55\x56\x57\x48\x83\xEC\x00\x49\x8B\xD8",
                                 "xxxx?xxxxxx?xxx", 0, void*, char*, int*)
CREATE_CALLABLE_CLASS_SIGNATURE(try_open_property_match_resolver, int,
                                "\x48\x89\x54\x24\x10\x55\x53\x56\x57\x41\x55\x41\x56\x41\x57\x48\x8D\x6C\x24\xE9",
                                "xxxxxxxxxxxxxxxxxxxx", 0)

CREATE_NORMAL_CALLABLE_SIGNATURE(Archive_ctor_datastore_thing, int64_t,
                                 "\x48\x89\x5C\x24\x00\x57\x48\x83\xEC\x00\x48\x8D\x3D\x00\x00\x00\x00\x48\x89\x7C\x24",
                                 "xxxx?xxxx?xxx????xxxx", 0)
CREATE_NORMAL_CALLABLE_SIGNATURE(index2adr, void*, "\x4C\x8B\xC1\x85\xD2\x7E\x00\x48\x8B\x41\x00\x48\x63\xD2",
                                 "xxxxxx?xxx?xxx", 0, lua_State*, int)