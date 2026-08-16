set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(HOST "x86_64-w64-mingw32")

# specify the cross compiler
set(CMAKE_C_COMPILER "${HOST}-gcc")
set(CMAKE_CXX_COMPILER "${HOST}-g++")
set(CMAKE_RC_COMPILER "${HOST}-windres")
set(CMAKE_ASM_MASM_COMPILER jwasm -win64 -e999)

# where is the target environment
set(CMAKE_FIND_ROOT_PATH "/usr/${HOST}")
#set(CMAKE_SYSROOT "/usr/${HOST}")
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH 0)

# search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CXX_FLAGS_INIT "-I${CMAKE_FIND_ROOT_PATH}/include")

# Disable it detecting my system pkg-config
set(PKG_CONFIG_LIBDIR "${CMAKE_FIND_ROOT_PATH}/lib/pkgconfig")
set(PKG_CONFIG_PATH "")

set(ALSOFT_EXAMPLES OFF)
set(ALSOFT_BACKEND_PIPEWIRE 0)

# CMake determines how to examine dependencies based on the *host* system, leading to
# a `file unknown error` unless the target platform is explicitly specified.
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
