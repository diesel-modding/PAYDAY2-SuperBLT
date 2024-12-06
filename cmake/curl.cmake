# file(GLOB_RECURSE curl_src lib/sys/curl/lib/*.c)
# add_library(curl STATIC ${curl_src})
# target_include_directories(curl PUBLIC lib/sys/curl/include lib/sys/curl/lib)
# target_compile_definitions(curl PRIVATE -DHAVE_SIGSETJMP -DHAVE_SETJMP_H -DBUILDING_LIBCURL -DHAVE_FCNTL_O_NONBLOCK
# 	-DHAVE_LONGLONG -DSIZEOF_LONG=8 -DSIZEOF_CURL_OFF_T=8)
# target_compile_options(curl PRIVATE -fPIC)
# target_link_libraries(curl PUBLIC zstatic)

# Turn off all the crap we don't want
option(HTTP_ONLY "" ON)
option(CURL_DISABLE_CRYPTO_AUTH "" ON)

add_subdirectory(lib/sys/curl EXCLUDE_FROM_ALL)
target_compile_options(libcurl PRIVATE -fPIC)
