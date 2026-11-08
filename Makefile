# ofxPHP - standalone build of the addon's tests, to prove the bundled binaries link.
#   make test      build and run tests/test_ofxPHP.cpp against libs/phpembed (macOS arm64)
#   make lib       rebuild libs/phpembed/lib/macos/libofxphp.a from a libphp.a (see scripts/build_embed_lib.sh)
#   make clean
# Inside an openFrameworks project you do not use this file: add `ofxPHP` to addons.make and the
# project makefile / projectGenerator read addon_config.mk instead.

ADDON_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PHP       := $(ADDON_DIR)libs/phpembed
OUT       ?= $(ADDON_DIR)build
MIN_MACOS ?= 12.0

CXX      ?= clang++
CXXFLAGS += -std=c++17 -DOFX_PHP=1 -mmacosx-version-min=$(MIN_MACOS) -I$(ADDON_DIR)src \
            -I$(PHP)/include/php -I$(PHP)/include/php/main -I$(PHP)/include/php/Zend \
            -I$(PHP)/include/php/TSRM -I$(PHP)/include/php/sapi/embed
# Inside an oF app OpenSSL comes from oF; the standalone test links the copy PHP was built with.
LDLIBS   := $(PHP)/lib/macos/libofxphp.a $(PHP)/lib/macos/libonig.a $(PHP)/lib/macos/libxml2.a $(PHP)/lib/macos/libsqlite3.a \
            $(PHP)/lib/standalone/libssl.a $(PHP)/lib/standalone/libcrypto.a -lresolv -liconv -lz

.PHONY: test lib clean
test: $(OUT)/test_ofxPHP
	$(OUT)/test_ofxPHP

$(OUT)/test_ofxPHP: $(ADDON_DIR)src/ofxPHP.cpp $(ADDON_DIR)src/ofxPHP.h $(ADDON_DIR)tests/test_ofxPHP.cpp $(PHP)/lib/macos/libofxphp.a
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) $(ADDON_DIR)src/ofxPHP.cpp $(ADDON_DIR)tests/test_ofxPHP.cpp $(LDLIBS) -o $@

lib:
	$(ADDON_DIR)scripts/build_embed_lib.sh $(LIBPHP)

clean:
	rm -rf $(OUT)
