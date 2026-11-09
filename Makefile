# ofxPHP - standalone build of the addon's tests, to prove the bundled binaries link.
#   make test      build and run tests/test_ofxPHP.cpp against libs/phpembed (native macOS architecture)
#   make test ARCHS=x86_64
#   make test ARCHS="arm64 x86_64"   universal executable
#   make xcframeworks               rebuild and package both architectures from pinned sources
#   make lib       merge a libphp.a into build/embed/<arch>/libofxphp.a (LIBPHP=/path/to/libphp.a)
#   make clean
# Inside an openFrameworks project you do not use this file: add `ofxPHP` to addons.make and the
# project makefile / projectGenerator read addon_config.mk instead.

ADDON_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PHP       := $(ADDON_DIR)libs/phpembed
ARCHS     ?= $(shell uname -m)
empty :=
space := $(empty) $(empty)
OUT       ?= $(ADDON_DIR)build/$(subst $(space),_,$(strip $(ARCHS)))
MIN_MACOS ?= 12.0
RUNNER    ?=
SLICE     := macos-arm64_x86_64
MACLIB    := $(PHP)/lib/macos
SSLLIB    := $(PHP)/lib/standalone
PHP_HEADERS := $(MACLIB)/libofxphp.xcframework/$(SLICE)/Headers/php
PHP_LIBS  := $(foreach lib,ofxphp onig xml2 sqlite3,$(MACLIB)/lib$(lib).xcframework/$(SLICE)/lib$(lib).a)
SSL_LIBS  := $(foreach lib,ssl crypto,$(SSLLIB)/lib$(lib).xcframework/$(SLICE)/lib$(lib).a)

CXX      ?= clang++
CXXFLAGS += -std=c++17 -DOFX_PHP=1 $(foreach arch,$(ARCHS),-arch $(arch)) -mmacosx-version-min=$(MIN_MACOS) -I$(ADDON_DIR)src \
            -I$(PHP_HEADERS) -I$(PHP_HEADERS)/main -I$(PHP_HEADERS)/Zend \
            -I$(PHP_HEADERS)/TSRM -I$(PHP_HEADERS)/sapi/embed
# Inside an oF app OpenSSL comes from oF; the standalone test links the copy PHP was built with.
LDLIBS   := $(PHP_LIBS) $(SSL_LIBS) -lresolv -liconv -lz

.PHONY: test lib xcframeworks clean
test: $(OUT)/test_ofxPHP
	$(RUNNER) $(OUT)/test_ofxPHP

$(OUT)/test_ofxPHP: $(ADDON_DIR)src/ofxPHP.cpp $(ADDON_DIR)src/ofxPHP.h $(ADDON_DIR)tests/test_ofxPHP.cpp $(PHP_LIBS) $(SSL_LIBS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) $(ADDON_DIR)src/ofxPHP.cpp $(ADDON_DIR)tests/test_ofxPHP.cpp $(LDLIBS) -o $@

lib:
	$(ADDON_DIR)scripts/build_embed_lib.sh $(LIBPHP) $(MIN_MACOS) $(firstword $(ARCHS))

xcframeworks:
	python3 $(ADDON_DIR)scripts/build_macos_xcframeworks.py --min-macos $(MIN_MACOS)

clean:
	rm -rf $(OUT)
