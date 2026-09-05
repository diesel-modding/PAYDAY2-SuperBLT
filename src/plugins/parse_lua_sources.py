# Read the Lua headers and spit out a big JSON with all the symbols
# This requires clang to be installed, so we don't run it at build-time and instead
# check the JSON file it generates into git.

import dataclasses
import json
from pathlib import Path
from typing import Optional, List

import clang.cindex as cl

import gen_lua_bindings

DIR = Path(__file__).parent.resolve()
LUA_SRC = (DIR / '../../lib/luajit/src').resolve()

# Copied from the wwise generate_bindngs.py
BASE_TYPE_KINDS = {
    cl.TypeKind.INVALID,
    cl.TypeKind.UNEXPOSED,
    cl.TypeKind.VOID,
    cl.TypeKind.BOOL,
    cl.TypeKind.CHAR_U,
    cl.TypeKind.UCHAR,
    cl.TypeKind.CHAR16,
    cl.TypeKind.CHAR32,
    cl.TypeKind.USHORT,
    cl.TypeKind.UINT,
    cl.TypeKind.ULONG,
    cl.TypeKind.ULONGLONG,
    cl.TypeKind.UINT128,
    cl.TypeKind.CHAR_S,
    cl.TypeKind.SCHAR,
    cl.TypeKind.WCHAR,
    cl.TypeKind.SHORT,
    cl.TypeKind.INT,
    cl.TypeKind.LONG,
    cl.TypeKind.LONGLONG,
    cl.TypeKind.INT128,
    cl.TypeKind.FLOAT,
    cl.TypeKind.DOUBLE,
    cl.TypeKind.LONGDOUBLE,
    cl.TypeKind.NULLPTR,
}


# Copied from the wwise generate_bindngs.py
def resolve_type(t: cl.Type):
    # print("==")
    # print(t.spelling)
    # print(t.get_declaration().location)
    # print(t.kind)

    # The basic types - these are all already fully-qualified
    if t.kind in BASE_TYPE_KINDS:
        return t.spelling

    prefix = ""

    if t.is_const_qualified():
        prefix += "const "
    if t.is_volatile_qualified():
        prefix += "volatile "
    if t.is_restrict_qualified():
        prefix += "restrict "

    match t.kind:
        case cl.TypeKind.ELABORATED:
            # Get the declaration this type refers to
            named_type = t.get_named_type()

            # Keep track of all the headers we need
            # file is none for AK::AkDeviceStatusCallbackFunc?
            loc = named_type.get_declaration().location
            if loc.file is not None:
                path = Path(loc.file.name)
                # info.required_headers.add(path)

            # This gets the fully-qualified name
            return prefix + named_type.spelling
        case cl.TypeKind.POINTER:
            pointee = resolve_type(t.get_pointee())
            pointer = pointee + "*"
            return prefix + pointer
        case cl.TypeKind.LVALUEREFERENCE:
            pointee = resolve_type(t.get_pointee())
            pointer = pointee + "&"
            return prefix + pointer
        case _:
            raise Exception(f'Unhandled type {t.kind}')


def main():
    tu = cl.TranslationUnit.from_source(
        filename=LUA_SRC / 'lauxlib.h',  # This imports lua.h
        args=[
            "-x", "c",
            f"-I{LUA_SRC}",
        ],
    )

    found_err = False
    for d in tu.diagnostics:
        print(d)
        if d.severity == d.Error:
            found_err = True

    if found_err:
        exit(1)

    functions: List[gen_lua_bindings.Function] = []

    for child in tu.cursor.get_children():
        child: cl.Cursor

        # Ignore Windows SDK files etc
        loc: cl.SourceLocation = child.location
        path = Path(loc.file.name)
        if LUA_SRC != path.parent:
            continue

        if child.kind != cl.CursorKind.FUNCTION_DECL:
            continue

        if child.spelling == 'luaL_checkoption':
            continue  # We don't support the INCOMPLETEARRAY type

        params = []
        for arg in child.get_arguments():
            name = arg.displayname
            arg_type = resolve_type(arg.type)
            params.append(gen_lua_bindings.Parameter(None if name == '' else name, arg_type))

        # print(child.spelling)
        fn = gen_lua_bindings.Function(
            name=child.spelling,
            args=params,
            ret=resolve_type(child.result_type),
        )

        print(f"{child.spelling}  {child.kind}")
        print(fn)

        functions.append(fn)

    data = gen_lua_bindings.JsonData(functions=functions)

    with open(DIR / 'lua_functions.json', 'w') as f:
        json.dump(dataclasses.asdict(data), f, indent=4, sort_keys=True)


if __name__ == '__main__':
    main()
