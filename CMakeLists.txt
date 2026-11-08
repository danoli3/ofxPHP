# ofxPHP - CMake target for the addon (macOS arm64 binaries are bundled).
#
#   add_subdirectory(path/to/ofxPHP)
#   target_link_libraries(my_app PRIVATE ofxPHP)
#
# Standalone test:  cmake -S . -B build -DOFXPHP_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
# On platforms without bundled binaries the target still builds, as stubs (ofxphp::available() == false).

cmake_minimum_required(VERSION 3.16)
project(ofxPHP LANGUAGES CXX)

option(OFXPHP_BUILD_TESTS "Build the ofxPHP test" OFF)

set(OFXPHP_DIR ${CMAKE_CURRENT_LIST_DIR})
set(OFXPHP_PHP ${OFXPHP_DIR}/libs/phpembed)

add_library(ofxPHP STATIC ${OFXPHP_DIR}/src/ofxPHP.cpp)
target_compile_features(ofxPHP PUBLIC cxx_std_17)
target_include_directories(ofxPHP PUBLIC ${OFXPHP_DIR}/src)

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" OR (APPLE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
	target_compile_definitions(ofxPHP PUBLIC OFX_PHP=1)
	target_include_directories(ofxPHP SYSTEM PUBLIC
		${OFXPHP_PHP}/include/php
		${OFXPHP_PHP}/include/php/main
		${OFXPHP_PHP}/include/php/Zend
		${OFXPHP_PHP}/include/php/TSRM
		${OFXPHP_PHP}/include/php/sapi/embed)
	# libofxphp.a is libphp merged into one object; consumers link it whole through this target.
	target_link_libraries(ofxPHP PUBLIC
		${OFXPHP_PHP}/lib/macos/libofxphp.a
		${OFXPHP_PHP}/lib/macos/libonig.a
		${OFXPHP_PHP}/lib/macos/libxml2.a
		${OFXPHP_PHP}/lib/macos/libsqlite3.a
		resolv iconv z)
	# OpenSSL: an app links its own. The standalone test has none, so it uses the copy PHP was built with.
	if(OFXPHP_BUILD_TESTS)
		set(OFXPHP_TEST_LIBS ${OFXPHP_PHP}/lib/standalone/libssl.a ${OFXPHP_PHP}/lib/standalone/libcrypto.a)
	endif()
else()
	message(STATUS "ofxPHP: no bundled PHP binaries for this platform; building stubs")
endif()

if(OFXPHP_BUILD_TESTS)
	enable_testing()
	add_executable(test_ofxPHP ${OFXPHP_DIR}/tests/test_ofxPHP.cpp)
	target_link_libraries(test_ofxPHP PRIVATE ofxPHP ${OFXPHP_TEST_LIBS})
	add_test(NAME ofxPHP COMMAND test_ofxPHP)
endif()
