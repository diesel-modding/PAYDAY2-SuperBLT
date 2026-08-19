cmake_minimum_required (VERSION 3.15)

set(LUA51_SOURCES
    ../../lib/luajit/src/lib_aux.c
    ../../lib/luajit/src/lib_base.c
    ../../lib/luajit/src/lib_bit.c
    ../../lib/luajit/src/lib_buffer.c
    ../../lib/luajit/src/lib_debug.c
    ../../lib/luajit/src/lib_ffi.c
    ../../lib/luajit/src/lib_init.c
    ../../lib/luajit/src/lib_io.c
    ../../lib/luajit/src/lib_jit.c
    ../../lib/luajit/src/lib_math.c
    ../../lib/luajit/src/lib_os.c
    ../../lib/luajit/src/lib_package.c
    ../../lib/luajit/src/lib_string.c
    ../../lib/luajit/src/lib_table.c
    ../../lib/luajit/src/lj_alloc.c
    ../../lib/luajit/src/lj_api.c
    ../../lib/luajit/src/lj_asm.c
    ../../lib/luajit/src/lj_assert.c
    ../../lib/luajit/src/lj_bc.c
    ../../lib/luajit/src/lj_bcread.c
    ../../lib/luajit/src/lj_bcwrite.c
    ../../lib/luajit/src/lj_buf.c
    ../../lib/luajit/src/lj_carith.c
    ../../lib/luajit/src/lj_ccall.c
    ../../lib/luajit/src/lj_ccallback.c
    ../../lib/luajit/src/lj_cconv.c
    ../../lib/luajit/src/lj_cdata.c
    ../../lib/luajit/src/lj_char.c
    ../../lib/luajit/src/lj_clib.c
    ../../lib/luajit/src/lj_cparse.c
    ../../lib/luajit/src/lj_crecord.c
    ../../lib/luajit/src/lj_ctype.c
    ../../lib/luajit/src/lj_debug.c
    ../../lib/luajit/src/lj_dispatch.c
    ../../lib/luajit/src/lj_err.c
    ../../lib/luajit/src/lj_ffrecord.c
    ../../lib/luajit/src/lj_func.c
    ../../lib/luajit/src/lj_gc.c
    ../../lib/luajit/src/lj_gdbjit.c
    ../../lib/luajit/src/lj_ir.c
    ../../lib/luajit/src/lj_lex.c
    ../../lib/luajit/src/lj_lib.c
    ../../lib/luajit/src/lj_load.c
    ../../lib/luajit/src/lj_mcode.c
    ../../lib/luajit/src/lj_meta.c
    ../../lib/luajit/src/lj_obj.c
    ../../lib/luajit/src/lj_opt_dce.c
    ../../lib/luajit/src/lj_opt_fold.c
    ../../lib/luajit/src/lj_opt_loop.c
    ../../lib/luajit/src/lj_opt_mem.c
    ../../lib/luajit/src/lj_opt_narrow.c
    ../../lib/luajit/src/lj_opt_sink.c
    ../../lib/luajit/src/lj_opt_split.c
    ../../lib/luajit/src/lj_parse.c
    ../../lib/luajit/src/lj_prng.c
    ../../lib/luajit/src/lj_profile.c
    ../../lib/luajit/src/lj_record.c
    ../../lib/luajit/src/lj_serialize.c
    ../../lib/luajit/src/lj_snap.c
    ../../lib/luajit/src/lj_state.c
    ../../lib/luajit/src/lj_str.c
    ../../lib/luajit/src/lj_strfmt.c
    ../../lib/luajit/src/lj_strfmt_num.c
    ../../lib/luajit/src/lj_strscan.c
    ../../lib/luajit/src/lj_tab.c
    ../../lib/luajit/src/lj_trace.c
    ../../lib/luajit/src/lj_udata.c
    ../../lib/luajit/src/lj_vmevent.c
    ../../lib/luajit/src/lj_vmmath.c
)

set(LUA51_BUILDVM_SOURCES
    ../../lib/luajit/src/host/buildvm.c
    ../../lib/luajit/src/host/buildvm_asm.c
    ../../lib/luajit/src/host/buildvm_peobj.c
    ../../lib/luajit/src/host/buildvm_fold.c
    ../../lib/luajit/src/host/buildvm_lib.c
)

set(LUAJIT_SOURCES
    ../../lib/luajit/src/luajit.c
)

add_library(lua51 STATIC ${LUA51_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/lj_vm.obj)
add_library(lua51-shared SHARED ${LUA51_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/lj_vm.obj)
add_executable(LuaBuildVM ${LUA51_BUILDVM_SOURCES})

add_executable(luajit ${LUAJIT_SOURCES})

target_link_libraries(luajit lua51 ${CMAKE_CURRENT_SOURCE_DIR}/lj_vm.obj)

target_include_directories(lua51 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/luajit/src)
target_include_directories(lua51-shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/luajit/src)
target_include_directories(LuaBuildVM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/luajit/src)
target_include_directories(luajit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/luajit/src)

target_compile_definitions(lua51-shared PUBLIC LUA_BUILD_AS_DLL LUAJIT_DISABLE_JIT LUAJIT_ENABLE_GC64 LUAJIT_DISABLE_FFI)
target_compile_definitions(lua51 PUBLIC LUAJIT_DISABLE_JIT LUAJIT_ENABLE_GC64 LUAJIT_DISABLE_FFI)
target_compile_definitions(luajit PUBLIC LUAJIT_DISABLE_JIT LUAJIT_ENABLE_GC64 LUAJIT_DISABLE_FFI)

set_property(TARGET lua51 PROPERTY
             MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(TARGET lua51 PROPERTY CXX_STANDARD 20)

set_property(TARGET lua51-shared PROPERTY
             MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(TARGET lua51-shared PROPERTY CXX_STANDARD 20)

set_property(TARGET LuaBuildVM PROPERTY
             MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(TARGET LuaBuildVM PROPERTY CXX_STANDARD 20)

set_property(TARGET luajit PROPERTY
             MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(TARGET luajit PROPERTY CXX_STANDARD 20)