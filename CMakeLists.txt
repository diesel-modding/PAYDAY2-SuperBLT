cmake_minimum_required(VERSION 3.18)
project(SuperBLT)

# Allow us to attach the winmm.lib dependency to OpenAL on Windows
cmake_policy(SET CMP0079 NEW)

if(CMAKE_SYSTEM_NAME MATCHES "Linux") # Add other systems, if they get PD2 support
	set(UNIX true)
endif()

###############################################################################
## misc. definitions ##########################################################
###############################################################################

# Use CXX 17
set(CMAKE_CXX_STANDARD 17)

# Enable XAudio
add_definitions(-DENABLE_XAUDIO)

###############################################################################
## file globbing ##############################################################
###############################################################################

# these instructions search the directory tree when cmake is
# invoked and put all files that match the pattern in the variables
# `sources` and `data`
file(GLOB_RECURSE sources		src/*.cpp src/*.c src/*.h)
file(GLOB_RECURSE win32_sources		platforms/w32/*.cpp platforms/w32/*.h)
set(		  win32_loader_path	platforms/w32/loader)
file(GLOB_RECURSE win32_loaders		${win32_loader_path}/*.cpp)
list(REMOVE_ITEM  win32_sources		${win32_loaders})
file(GLOB_RECURSE linux_sources		platforms/linux/src/*.cpp platforms/linux/include/*.hh)
set(		  linux_loader_file	${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/src/blt_main.cpp)
list(REMOVE_ITEM  linux_sources		${linux_loader_file})
set(		  linux_link_script	${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/link.ld)
# you can use set(sources src/main.cpp) etc if you don't want to
# use globing to find files automatically

###############################################################################
## target definitions #########################################################
###############################################################################

set(mxml_sources mxml-attr.c mxml-entity.c mxml-file.c mxml-get.c mxml-index.c
	mxml-node.c mxml-search.c mxml-set.c mxml-private.c mxml-string.c)
list(TRANSFORM mxml_sources PREPEND lib/mxml/)
add_library(mxml STATIC ${mxml_sources})
target_include_directories(mxml PRIVATE lib/configs/mxml)
if(UNIX)
	target_compile_options(mxml PRIVATE -std=gnu99 -Wall -fPIC)
elseif(WIN32)
	target_compile_options(mxml PRIVATE -D_CRT_SECURE_NO_WARNINGS)
endif()
target_include_directories(mxml PUBLIC lib/mxml)

file(GLOB_RECURSE wren_sources
	lib/wren/src/include/wren.h # Public source file
		lib/wren/src/vm/*.c lib/wren/src/vm/*.h # Core VM files
		lib/wren/src/optional/*.c lib/wren/src/optional/*.h # Optional components (Meta and Random)
)
add_library(wren STATIC ${wren_sources})
target_include_directories(wren PRIVATE lib/wren/src/optional lib/wren/src/vm)
if(MSVC)
	target_compile_options(wren PRIVATE -D_CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(wren PRIVATE -std=c99 -Wall -fPIC)
endif()
target_include_directories(wren PUBLIC lib/wren/src/include)

add_library(subhook STATIC lib/subhook/subhook.c)
if(UNIX)
	target_compile_options(subhook PRIVATE -std=c99 -Wall -fPIC)
elseif(WIN32)
	target_compile_options(subhook PRIVATE -DSUBHOOK_EXPORT=;)
endif()
target_include_directories(subhook PUBLIC lib/subhook)

###############################################################################
## Wren source generation #####################################################
###############################################################################

find_package(Python3 REQUIRED COMPONENTS Interpreter)
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

if(UNIX)
	add_library(SuperBLT STATIC ${sources} ${linux_sources})
	include_directories (platforms/linux/include)

	# Use modern C++, and show all warnings
	target_compile_options(SuperBLT PUBLIC -Wall -Werror -fPIC)

	# Enable this to see missing references:
	#set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -z defs")

	# Yay fantastic. If you link something with -fPIC, then *every* function call
	# will go through a PLT entry so that a dynamic library can override it. This
	# causes something of a natural problem for us - we're compiling our own version
	# of zlib specifically so that it is separate from the game's version (which will
	# be used by the dynamic linker in preference to another library).
	# This lovely linker flag stops the linker from doing that. It'll still properly
	# make PLT entries for symbols imported from the basegame or other libraries, but
	# it won't let anything mess with ours.
	target_link_libraries(SuperBLT -Wl,-Bsymbolic-functions)

	# Error for anyone using USE_LIBCXX
	if (USE_LIBCXX)
		message(WARNING "USE_LIBCXX: LibCXX is no longer required for "
			"the asset system, the flag will be ignored, and the default compiler used.")
	endif()

	# Debugging shim - there's no harm in always building this
	add_library(debug_shim SHARED platforms/linux/sblt_shim/sblt_shim.c)
	target_link_libraries(debug_shim dl)

elseif(WIN32)
	add_library(SuperBLT STATIC ${sources} ${win32_sources})
	target_compile_options(SuperBLT PRIVATE
		-DAL_LIBTYPE_STATIC
		-DCURL_STATICLIB
		-DSUBHOOK_STATIC
	)

	# Link against libraries
	target_link_libraries(SuperBLT ws2_32.lib)
	target_link_libraries(SuperBLT crypt32.lib)
else()
	# TODO raise error
endif()

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

if(WIN32)
	# Disable all the install targets, otherwise it causes errors when we add dependencies to curl
	function(install)
	endfunction()

	# Note that libressl must be downloaded seperately, since it requires some pre-configuration that
	# is inconvenient on Windows. Here's the download URL:
	# https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-3.7.3.tar.gz
	set(LSSL_SRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/sys/libressl-3.7.3)
	set(LSSL_TARBALL ${LSSL_SRCDIR}.tar.gz)
	if(NOT EXISTS ${LSSL_TARBALL})
		# TODO move this to my (ZNix's) server, in case the paths ever change
		message("Downloading LibreSSL")
		file(DOWNLOAD "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-3.7.3.tar.gz" ${LSSL_TARBALL}
			TIMEOUT 60  # seconds
			EXPECTED_HASH SHA256=7948c856a90c825bd7268b6f85674a8dcd254bae42e221781b24e3f8dc335db3
			TLS_VERIFY ON
		)
	endif()
	if(NOT EXISTS ${LSSL_SRCDIR})
		message("Extracting LibreSSL")
		file(ARCHIVE_EXTRACT INPUT ${LSSL_TARBALL} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/sys/ PATTERNS libressl-3.7.3)
	endif()

	######
	###### Dependency stuff
	######

	# Required to build LibreSSL and curl as static libraries
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build as static, set by SBLT's buildscript")

	# # zlib requires an old version of cmake before target_include_directories was added
	add_subdirectory(lib/sys/zlib EXCLUDE_FROM_ALL)
	target_include_directories(zlibstatic PUBLIC ${zlib_BINARY_DIR} ${zlib_SOURCE_DIR})
	target_link_libraries(SuperBLT zlibstatic)

	# Unfortunately cmake doesn't have namespacing, so we have to set stuff here
	set(LIBTYPE STATIC)
	add_subdirectory(lib/sys/openalsoft EXCLUDE_FROM_ALL)
	target_link_libraries(OpenAL PRIVATE winmm.lib) # Not set up by default?
	target_link_libraries(SuperBLT OpenAL)
	# target_include_directories(SuperBLT PRIVATE ${OpenAL_SOURCE_DIR}/include) # No longer needed

	add_subdirectory(lib/sys/libressl-3.7.3 EXCLUDE_FROM_ALL)
	target_include_directories(SuperBLT PRIVATE ${LSSL_SRCDIR}/include) # Required for latest version of LibreSSL
	target_link_libraries(SuperBLT crypto)

	#### CURL ####
	# Turn off all the crap we don't want
	option(HTTP_ONLY "" ON)
	option(CURL_DISABLE_CRYPTO_AUTH "" ON)

	# Use WinSSL rather than our static-linked copy of LibreSSL so we can use the
	# default Windows certificate store without any hassle
	option(CMAKE_USE_WINSSL "" ON)

	# TODO enable zlib

	add_subdirectory(lib/sys/curl EXCLUDE_FROM_ALL)
	target_link_libraries(libcurl crypto ssl)
	target_link_libraries(SuperBLT libcurl)
else()
	# TODO merge this and the Windows dependencies section together, where useful

	# Build our own version of zlib - the game has it's own copy of zlib compiled in so
	# the dynamic linker will use that rather than the system version, leading to all
	# kinds of horrid and hard-to-diagnose issues.
	include(cmake/zlib.cmake)
	target_link_libraries(SuperBLT zstatic)

	option(USE_SYSTEM_LIBRARIES "Use the standard Linux libraries instead of building our own. Can cause linkage issues.")

	if (USE_SYSTEM_LIBRARIES)
		find_package(PNG REQUIRED)
		include_directories(${PNG_INCLUDE_DIRS})
		target_link_libraries(SuperBLT ${PNG_LIBRARY})

		find_package(CURL REQUIRED)
		include_directories(${CURL_INCLUDE_DIRS})
		target_link_libraries(SuperBLT ${CURL_LIBRARY})
	else ()
		# We need LibPNG for the image support. We have to compile this ourselves so we can
		# use our built-in copy of zlib.
		include(cmake/libpng.cmake)
		target_link_libraries(SuperBLT png_static)

		# Build our own version of CURL. Using the system version was causing a variety of
		# issues, ranging from not running inside the steam runtime to crashes from longjmp
		# caused by a DNS lookup timeout.
		include(cmake/curl.cmake)
		target_link_libraries(SuperBLT libcurl)
	endif ()

	find_package (OpenSSL REQUIRED)
	include_directories (${OPENSSL_INCLUDE_DIR})
	target_link_libraries(SuperBLT ${OPENSSL_LIBRARIES})

	find_package (OpenAL REQUIRED)
	include_directories (${OPENAL_INCLUDE_DIR})
	target_link_libraries(SuperBLT ${OPENAL_LIBRARY})
endif()

###############################################################################
## loader binary (this generates a seperate target for both Windows loaders) ##
###############################################################################

# Define the macro
macro(Add_Loader library_name)
	add_library(${library_name} SHARED ${ARGN})
	target_link_libraries(${library_name} LINK_PRIVATE SuperBLT)
	if(UNIX)
		# Use modern C++, and show all warnings
		target_compile_options(${library_name} PUBLIC -Wall -Werror)

		# Use a custom link script, to ensure the correct initialization order
		target_link_libraries(${library_name} LINK_PRIVATE "-Wl,-T,\"${linux_link_script}\"") # Use the link script
		set_target_properties(${library_name} PROPERTIES LINK_DEPENDS ${linux_link_script}) # Relink if the script is edited

		# Print the build script for debugging - comment out the custom link script and you can use this
		# to retrieve the original script.
		#target_link_libraries(${library_name} "-Wl,--verbose")
	endif()
endmacro()

if(UNIX)
	Add_Loader("superblt_loader" ${linux_loader_file})
elseif(WIN32)
	Add_Loader("IPHLPAPI" ${win32_loader_path}/iphp.cpp ${win32_loader_path}/iphp.def)
	Add_Loader("WSOCK32" ${win32_loader_path}/wsock.cpp ${win32_loader_path}/wsock.def)
	target_compile_options(IPHLPAPI PRIVATE -DBLT_USE_IPHLPAPI)
	target_compile_options(WSOCK32 PRIVATE -DBLT_USE_WSOCK)
else()
	message(FATAL_ERROR "Unspported OS; if unix based, please add it in CMakeLists.txt")
endif()
