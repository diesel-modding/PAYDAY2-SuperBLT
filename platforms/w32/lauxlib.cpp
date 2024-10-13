#define SIG_INCLUDE_LJ_INTERNAL
#include <lua.h>
#include <stdio.h>
#include <string.h>

#define LUA_QL(x) "'" x "'"
#define LUA_QS LUA_QL("%s")

static void tag_error(lua_State* L, int narg, int tag)
{
	luaL_typerror(L, narg, lua_typename(L, tag));
}

int luaL_argerror(lua_State* L, int narg, const char* extramsg)
{
	return luaL_error(L, "bad argument #%d (%s)", narg, extramsg);
}

int luaL_checkoption(lua_State* L, int narg, const char* def, const char* const lst[])
{
	const char* name = (def) ? luaL_optstring(L, narg, def) : luaL_checkstring(L, narg);
	int i;
	for (i = 0; lst[i]; i++)
		if (strcmp(lst[i], name) == 0)
			return i;

	char buff[1024];
	snprintf(buff, sizeof(buff), "invalid option " LUA_QS, name);
	return luaL_argerror(L, narg, buff);
}

int luaL_typerror(lua_State* L, int narg, const char* tname)
{
	char msg[1024];
	snprintf(msg, sizeof(msg), "%s expected, got %s", tname, luaL_typename(L, narg));
	return luaL_argerror(L, narg, msg);
}

void luaL_checktype(lua_State* L, int narg, int t)
{
	if (lua_type(L, narg) != t)
		tag_error(L, narg, t);
}

void luaL_checkany(lua_State* L, int narg)
{
	if (lua_type(L, narg) == LUA_TNONE)
		luaL_argerror(L, narg, "value expected");
}

const char* luaL_checklstring(lua_State* L, int narg, size_t* len)
{
	const char* s = lua_tolstring(L, narg, len);
	if (!s)
		tag_error(L, narg, LUA_TSTRING);
	return s;
}

const char* luaL_optlstring(lua_State* L, int narg, const char* def, size_t* len)
{
	if (lua_isnoneornil(L, narg))
	{
		if (len)
			*len = (def ? strlen(def) : 0);
		return def;
	}
	else
		return luaL_checklstring(L, narg, len);
}

lua_Number luaL_checknumber(lua_State* L, int narg)
{
	lua_Number d = lua_tonumber(L, narg);
	if (d == 0 && !lua_isnumber(L, narg)) /* avoid extra test when d is not 0 */
		tag_error(L, narg, LUA_TNUMBER);
	return d;
}

lua_Number luaL_optnumber(lua_State* L, int narg, lua_Number def)
{
	return luaL_opt(L, luaL_checknumber, narg, def);
}

lua_Integer luaL_checkinteger(lua_State* L, int narg)
{
	lua_Integer d = lua_tointeger(L, narg);
	if (d == 0 && !lua_isnumber(L, narg)) /* avoid extra test when d is not 0 */
		tag_error(L, narg, LUA_TNUMBER);
	return d;
}

lua_Integer luaL_optinteger(lua_State* L, int narg, lua_Integer def)
{
	return luaL_opt(L, luaL_checkinteger, narg, def);
}

void luaL_checkstack(lua_State* L, int size, const char* msg)
{
	int top = lua_gettop(L);
	if (!lua_checkstack(L, size))
		luaL_error(L, "Could not increase stack size by %d to %d - %s", size, size + top, msg);
}

typedef union TValue TValue;
typedef const TValue cTValue;

int lj_obj_equal(cTValue* o1, cTValue* o2);

// Note we have to implement lua_rawequal ourselves, since it's
// never used in PD2 and thus is eaten by the linker.
int lua_rawequal(lua_State* L, int idx1, int idx2)
{
	cTValue* tv1 = (TValue*)index2adr(L, idx1);
	cTValue* tv2 = (TValue*)index2adr(L, idx2);

	// The following code matches this in LuaJIT:
	// return (o1 == niltv(L) || o2 == niltv(L)) ? 0 : lj_obj_equal(o1, o2);

	const uint8_t* L_d = (const uint8_t*)L;
	uint32_t glref = *(uint32_t*)(L_d + 8);
	cTValue* nilref = (cTValue*)(glref + 144);

	if (tv1 == nilref || tv2 == nilref)
		return false;

	return lj_obj_equal(tv1, tv2);
}

typedef struct GCRef
{
	uint32_t gcptr32; /* Pseudo 32 bit pointer. */
} GCRef;

/* Tagged value. */
union TValue
{
	uint64_t u64; // 64 bit pattern overlaps number.
	lua_Number n;
	struct
	{
		union
		{
			GCRef gcr; // GCobj reference (if any).
			int32_t i; // Integer value.
		};
		uint32_t it; // Internal object tag. Must overlap MSW of number.
	};
	/* struct {
	    GCRef func;	// Function for next frame (or dummy L).
	    FrameLink tp;	// Link to previous frame.
	} fr; */
	struct
	{
		uint32_t lo; // Lower 32 bits of number.
		uint32_t hi; // Upper 32 bits of number.
	} u32;
};

// AFAIK this is true, may be wrong though
#define LJ_DUALNUM false

#define LJ_TNIL (~0u)
#define LJ_TTRUE (~2u)
#define LJ_TLIGHTUD (~3u)
#define LJ_TSTR (~4u)
#define LJ_TFUNC (~8u)
#define LJ_TTRACE (~9u)
#define LJ_TCDATA (~10u)
#define LJ_TTAB (~11u)
#define LJ_TUDATA (~12u)
#define LJ_TNUMX (~13u)
#define LJ_TISNUM LJ_TNUMX
#define LJ_TISPRI LJ_TTRUE

#define itype(o) ((o)->it)
#define tvisnil(o) (itype(o) == LJ_TNIL)
#define tvislightud(o) (itype(o) == LJ_TLIGHTUD)
#define tvisstr(o) (itype(o) == LJ_TSTR)
#define tvisfunc(o) (itype(o) == LJ_TFUNC)
#define tvisudata(o) (itype(o) == LJ_TUDATA)
#define tvisnumber(o) (itype(o) <= LJ_TISNUM)

#define tvispri(o) (itype(o) >= LJ_TISPRI)
#define tvisint(o) (LJ_DUALNUM && itype(o) == LJ_TISNUM)
#define tvisnum(o) (itype(o) < LJ_TISNUM)

#define numV(o) ((o)->n)
#define intV(o) ((int32_t)(o)->i)

#define gcrefeq(r1, r2) ((r1).gcptr32 == (r2).gcptr32)

static lua_Number numberVnum(cTValue* o)
{
	if (tvisint(o))
		return (lua_Number)intV(o);
	else
		return numV(o);
}

int lj_obj_equal(cTValue* o1, cTValue* o2)
{
	if (itype(o1) == itype(o2))
	{
		if (tvispri(o1))
			return 1;
		if (!tvisnum(o1))
			return gcrefeq(o1->gcr, o2->gcr);
	}
	else if (!tvisnumber(o1) || !tvisnumber(o2))
	{
		return 0;
	}
	return numberVnum(o1) == numberVnum(o2);
}
