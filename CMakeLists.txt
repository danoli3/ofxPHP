# ofxPHP - CMake target for the addon (macOS arm64 + x86_64 XCFrameworks are bundled).
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

set(OFXPHP_MACOS_SUPPORTED OFF)
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	set(OFXPHP_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES})
	if(NOT OFXPHP_ARCHITECTURES)
		set(OFXPHP_ARCHITECTURES ${CMAKE_SYSTEM_PROCESSOR})
	endif()
	set(OFXPHP_MACOS_SUPPORTED ON)
	foreach(arch IN LISTS OFXPHP_ARCHITECTURES)
		if(NOT arch MATCHES "^(arm64|aarch64|x86_64)$")
			set(OFXPHP_MACOS_SUPPORTED OFF)
		endif()
	endforeach()
endif()

if(OFXPHP_MACOS_SUPPORTED)
	set(OFXPHP_SLICE macos-arm64_x86_64)
	set(OFXPHP_LIBDIR ${OFXPHP_PHP}/lib/macos)
	set(OFXPHP_HEADERS ${OFXPHP_LIBDIR}/libofxphp.xcframework/${OFXPHP_SLICE}/Headers/php)
	target_compile_definitions(ofxPHP PUBLIC OFX_PHP=1)
	target_include_directories(ofxPHP SYSTEM PUBLIC
		${OFXPHP_HEADERS}
		${OFXPHP_HEADERS}/main
		${OFXPHP_HEADERS}/Zend
		${OFXPHP_HEADERS}/TSRM
		${OFXPHP_HEADERS}/sapi/embed)
	# Use the universal macOS slice directly, including on CMake versions before XCFramework support.
	target_link_libraries(ofxPHP PUBLIC
		${OFXPHP_LIBDIR}/libofxphp.xcframework/${OFXPHP_SLICE}/libofxphp.a
		${OFXPHP_LIBDIR}/libonig.xcframework/${OFXPHP_SLICE}/libonig.a
		${OFXPHP_LIBDIR}/libxml2.xcframework/${OFXPHP_SLICE}/libxml2.a
		${OFXPHP_LIBDIR}/libsqlite3.xcframework/${OFXPHP_SLICE}/libsqlite3.a
		resolv iconv z)
	# OpenSSL: an app links its own. The standalone test has none, so it uses the copy PHP was built with.
	if(OFXPHP_BUILD_TESTS)
		set(OFXPHP_TEST_LIBS
			${OFXPHP_PHP}/lib/standalone/libssl.xcframework/${OFXPHP_SLICE}/libssl.a
			${OFXPHP_PHP}/lib/standalone/libcrypto.xcframework/${OFXPHP_SLICE}/libcrypto.a)
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
