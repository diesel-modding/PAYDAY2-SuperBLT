cmake_minimum_required(VERSION 3.18)

project(SuperBLT CXX C)

###############################################################################
## misc. definitions ##########################################################
###############################################################################

enable_language(ASM_MASM)

# Use C 11, no gnu ext
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Use C++ 23, no gnu ext
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# find python3
find_package(Python3 REQUIRED COMPONENTS Interpreter)

###############################################################################
## file globbing ##############################################################
###############################################################################

# these instructions search the directory tree when cmake is
# invoked and put all files that match the pattern in the variable
# `sources`
file(GLOB_RECURSE	sources		src/*.cpp src/*.c src/*.h)

###############################################################################
## target definitions #########################################################
###############################################################################

set(mxml_sources mxml-attr.c mxml-entity.c mxml-file.c mxml-get.c mxml-index.c
	mxml-node.c mxml-search.c mxml-set.c mxml-private.c mxml-string.c)
list(TRANSFORM mxml_sources PREPEND lib/mxml/)
add_library(mxml STATIC ${mxml_sources})
target_include_directories(mxml PRIVATE lib/configs/mxml)
target_compile_options(mxml PRIVATE -D_CRT_SECURE_NO_WARNINGS)
target_include_directories(mxml PUBLIC lib/mxml)

file(GLOB_RECURSE wren_sources
	# Public source file
	lib/wren/src/include/wren.h
	# Core VM files
	lib/wren/src/vm/*.c
	lib/wren/src/vm/*.h
	# Optional components (Meta and Random)
	lib/wren/src/optional/*.c
	lib/wren/src/optional/*.h
)
add_library(wren STATIC ${wren_sources})
target_include_directories(wren PRIVATE lib/wren/src/optional lib/wren/src/vm)
target_compile_options(wren PRIVATE -D_CRT_SECURE_NO_WARNINGS)
target_include_directories(wren PUBLIC lib/wren/src/include)

add_library(subhook STATIC lib/subhook/subhook.c)
target_compile_options(subhook PRIVATE -DSUBHOOK_EXPORT=;)
target_include_directories(subhook PUBLIC lib/subhook)

###############################################################################
## Wren source generation #####################################################
###############################################################################

set(wren_src_files
	wren/native.wren
	wren/DB_001.wren
	wren/internal_001.wren
	wren/LuaInterface_001.wren
	wren/Environment_001.wren
	wren/Utils_001.wren
)
add_custom_command(
	OUTPUT wren_generated_src.c gen/wren_generated_src.h
	COMMAND ${Python3_EXECUTABLE} wren/generate.py ${CMAKE_CURRENT_BINARY_DIR} ${wren_src_files}
	MAIN_DEPENDENCY wren/generate.py
	DEPENDS ${wren_src_files}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	VERBATIM
)
list(APPEND sources ${CMAKE_CURRENT_BINARY_DIR}/wren_generated_src.c)

###############################################################################
## Base (non-platform-specific) part of SuperBLT ##############################
###############################################################################

add_library(SuperBLT STATIC ${sources})

target_compile_options(SuperBLT PRIVATE
	-DCURL_STATICLIB
	-DSUBHOOK_STATIC
	-DWIN32_LEAN_AND_MEAN
)

# General optimisation breaks calls to certain lua functions, so replace it.
# We statically link to reduce dependencies
foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
	if(${flag_var} MATCHES "/O2")
		string(REGEX REPLACE "/O2" "/Od /Oi /Ot /Oy /Ob2 /GF /Gy" ${flag_var} "${${flag_var}}")
	endif(${flag_var} MATCHES "/O2")
endforeach(flag_var)

# Link against libraries
target_link_libraries(SuperBLT crypt32.lib ws2_32.lib version.lib)

# Allow both SuperBLT and the loaders access to the headers
target_include_directories(SuperBLT PUBLIC src)

# Make the generated wren header available
target_include_directories(SuperBLT PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)

###############################################################################
## dependencies ###############################################################
###############################################################################

target_link_libraries(SuperBLT mxml)
target_link_libraries(SuperBLT wren)
target_link_libraries(SuperBLT subhook)

# Build libs as static
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libs as static, set by SBLT's buildscript")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)



#### libpsl ####

set(LIBPSL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/sys/libpsl")

# Read version from version.txt
file(READ "${LIBPSL_DIR}/version.txt" LIBPSL_VERSION)
string(STRIP "${LIBPSL_VERSION}" LIBPSL_VERSION)

# Split version into major, minor, patch
string(REPLACE "." ";" LIBPSL_VERSION_LIST "${LIBPSL_VERSION}")
list(GET LIBPSL_VERSION_LIST 0 LIBPSL_VERSION_MAJOR)
list(GET LIBPSL_VERSION_LIST 1 LIBPSL_VERSION_MINOR)
list(GET LIBPSL_VERSION_LIST 2 LIBPSL_VERSION_PATCH)

message(STATUS "Using libpsl version: ${LIBPSL_VERSION}")

# Compute numerical value
math(EXPR LIBPSL_VERSION_VALUE "(${LIBPSL_VERSION_MAJOR} << 16) + (${LIBPSL_VERSION_MINOR} << 8) + ${LIBPSL_VERSION_PATCH}")

# Convert to padded hex string without 0x
string(HEX ${LIBPSL_VERSION_VALUE} LIBPSL_HEX_VALUE)
string(LENGTH "${LIBPSL_HEX_VALUE}" HEX_LEN)
math(EXPR PAD_LEN "6 - ${HEX_LEN}")
if(PAD_LEN GREATER 0)
    string(REPEAT "0" ${PAD_LEN} PADDING)
    set(LIBPSL_HEX_VALUE "${PADDING}${LIBPSL_HEX_VALUE}")
endif()

# Set hex with 0x for substitution
set(LIBPSL_VERSION_NUMBER "0x${LIBPSL_HEX_VALUE}")

set(PACKAGE_NAME "libpsl")
set(PACKAGE_VERSION "${LIBPSL_VERSION}")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "libpsl")
set(PACKAGE_URL "https://github.com/rockdaboot/libpsl")
set(PACKAGE_BUGREPORT "tim.ruehsen@gmx.de")

# Generated dir
set(PSL_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/psl")
file(MAKE_DIRECTORY "${PSL_BINARY_DIR}")

# Generate libpsl.h
configure_file(
    "${LIBPSL_DIR}/include/libpsl.h.in"
    "${PSL_BINARY_DIR}/libpsl.h"
    @ONLY
)

# Generate config.h from MSVC template
configure_file(
    "${LIBPSL_DIR}/msvc/config.h.win32.in"
    "${PSL_BINARY_DIR}/config.h"
    @ONLY
)

# Generate suffixes_dafsa.h using the fresh data file
set(PSL_DATA_FILE      "${CMAKE_CURRENT_SOURCE_DIR}/lib/sys/psl-list/public_suffix_list.dat")
set(SUFFIXES_DAFSA_H   "${PSL_BINARY_DIR}/suffixes_dafsa.h")
add_custom_command(
    OUTPUT "${SUFFIXES_DAFSA_H}"
    COMMAND ${Python3_EXECUTABLE}
            "${LIBPSL_DIR}/src/psl-make-dafsa"
            --output-format=cxx+
            "${PSL_DATA_FILE}"
            "${SUFFIXES_DAFSA_H}"
    MAIN_DEPENDENCY "${LIBPSL_DIR}/src/psl-make-dafsa"
    DEPENDS
        "${PSL_DATA_FILE}"
    WORKING_DIRECTORY "${LIBPSL_DIR}"
    COMMENT "Generating suffixes_dafsa.h from latest Public Suffix List data"
    VERBATIM
)
add_custom_target(generate_psl_files DEPENDS
    "${PSL_BINARY_DIR}/libpsl.h"
    "${PSL_BINARY_DIR}/config.h"
    "${SUFFIXES_DAFSA_H}"
)

add_library(psl STATIC
    "${LIBPSL_DIR}/src/lookup_string_in_fixed_set.c"
    "${LIBPSL_DIR}/src/psl.c"
)
target_compile_definitions(psl PRIVATE
	HAVE_CONFIG_H=1

	# TODO: build libpsl (and curl) with libidn2
	# WITH_LIBIDN2=1
	# ENABLE_RUNTIME=libidn2
)
target_compile_definitions(psl PUBLIC PSL_STATIC)
target_compile_options(psl PRIVATE -D_CRT_SECURE_NO_WARNINGS -DENABLE_BUILTIN)
target_include_directories(psl PUBLIC "${PSL_BINARY_DIR}")
add_dependencies(psl generate_psl_files)

set(LIBPSL_LIBRARY psl CACHE INTERNAL "")
set(LIBPSL_INCLUDE_DIR "${PSL_BINARY_DIR}" CACHE INTERNAL "")
target_link_libraries(SuperBLT psl)

message(STATUS "libpsl generated files in: ${PSL_BINARY_DIR}")



#### zlib-ng ####

set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)  # Ensures zlib-ng mimics zlib 1.2.x API
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)

add_subdirectory(lib/sys/zlib EXCLUDE_FROM_ALL)

# Link to the expected CURL-compatible zlib target
set(ZLIB_LIBRARY zlib-ng CACHE INTERNAL "")
set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} CACHE INTERNAL "")
target_link_libraries(SuperBLT zlib-ng)



#### CURL ####

# Use Windows SSPI for HTTPS
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)

# Turn off all the crap we don't want
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE) # disable all protocols, but http(s) and ws(s)
set(ENABLE_UNIX_SOCKETS OFF CACHE BOOL "" FORCE) # disable unix-socket support [ http(s):// ]
set(CURL_DISABLE_NEGOTIATE_AUTH ON CACHE BOOL "" FORCE) # included in SSPI
set(CURL_DISABLE_KERBEROS_AUTH ON CACHE BOOL "" FORCE) # included in SSPI
set(CURL_DISABLE_NTLM ON CACHE BOOL "" FORCE) # included in SSPI
set(CURL_DISABLE_WEBSOCKETS ON CACHE BOOL "" FORCE) # disable websocket support [ ws(s):// ]

# link curl against our included static zlib, to enable it's libz features
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)        # prevent it from trying to find system zlib
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON CACHE BOOL "" FORCE)  # disable external find
set(ZLIB_LIBRARY zlib-ng CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} CACHE PATH "" FORCE)

add_subdirectory(lib/sys/curl EXCLUDE_FROM_ALL)

target_link_libraries(SuperBLT libcurl_static)



#### LuaJIT ####

add_subdirectory(src/luajit EXCLUDE_FROM_ALL)
target_link_libraries(SuperBLT lua51)



#### DieselEngineFormats ####

add_library(DieselEngineFormats STATIC
	lib/DieselFormats/dieselformats/src/diesel/modern/scriptdata.cpp
	lib/DieselFormats/dieselformats/src/diesel/modern/hash.cpp
	lib/DieselFormats/dieselformats/src/diesel/modern/modern_shared.cpp
    lib/DieselFormats/dieselformats/src/diesel/shared.cpp
    lib/DieselFormats/dieselformats/src/diesel/animation.cpp
    lib/DieselFormats/dieselformats/src/diesel/font.cpp

	lib/DieselFormats/dieselformats/src/fileio/reader.cpp
    lib/DieselFormats/dieselformats/src/fileio/writer.cpp
    lib/DieselFormats/dieselformats/src/fileio/zlibcompression.cpp
)
target_include_directories(DieselEngineFormats PUBLIC lib/DieselFormats/dieselformats/src)
target_link_libraries(DieselEngineFormats PRIVATE zlib-ng)
target_link_libraries(SuperBLT DieselEngineFormats)


#### diesel-wwise-bnk-version-converter ####

file(GLOB_RECURSE wwise_converter_sources lib/diesel-wwise-bnk-version-converter/DieselWwiseSoundbank/src/*.cpp)
add_library(diesel-wwise-bnk-version-converter STATIC ${wwise_converter_sources})
target_include_directories(diesel-wwise-bnk-version-converter PRIVATE lib/diesel-wwise-bnk-version-converter/DieselWwiseSoundbank/src)
# Include some namespace in the public include path, even if DieselWwiseSoundbank/src/ is ugly
target_include_directories(diesel-wwise-bnk-version-converter PUBLIC lib/diesel-wwise-bnk-version-converter)
target_link_libraries(SuperBLT diesel-wwise-bnk-version-converter)

###############################################################################
## loader binary (this generates a seperate target for both Windows loaders) ##
###############################################################################

set(LOADER_GEN_ASM "${CMAKE_CURRENT_BINARY_DIR}/loader/loader_asm.gen.asm")
set(LOADER_GEN_H "${CMAKE_CURRENT_BINARY_DIR}/loader/loader_asm.gen.h")
set(LOADER_GEN_DEF "${CMAKE_CURRENT_BINARY_DIR}/loader/loader_asm.gen.def")
add_custom_command(
	OUTPUT "${LOADER_GEN_ASM}" "${LOADER_GEN_H}" "${LOADER_GEN_DEF}"
	COMMAND ${Python3_EXECUTABLE} loader/generate_asm.py --asm "${LOADER_GEN_ASM}" --header "${LOADER_GEN_H}" --def "${LOADER_GEN_DEF}"
	MAIN_DEPENDENCY loader/generate_asm.py
	DEPENDS loader/iphp.def loader/wsock.def
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	VERBATIM
)

add_library(WSOCK32 SHARED loader/loader.cpp loader/loader.asm src/Resource.rc "${LOADER_GEN_ASM}" "${LOADER_GEN_DEF}")
target_link_libraries(WSOCK32 LINK_PRIVATE SuperBLT)
target_include_directories(WSOCK32 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/loader")

set_property(SOURCE loader/loader.asm PROPERTY LANGUAGE ASM_MASM)
set_property(SOURCE "${LOADER_GEN_ASM}" PROPERTY LANGUAGE ASM_MASM)

target_compile_options(WSOCK32 PRIVATE -DBLT_USE_WSOCK)

###############################################################################
## build external updater for dll                                            ##
###############################################################################
add_executable(SBLT_DLL_UPDATER updater/updater.cpp updater/compression.cpp updater/Resource.rc)
target_link_libraries(SBLT_DLL_UPDATER crypt32.lib ws2_32.lib version.lib)
target_link_libraries(SBLT_DLL_UPDATER psl zlib-ng libcurl_static)
target_include_directories(SBLT_DLL_UPDATER PRIVATE updater)
