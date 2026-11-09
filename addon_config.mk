# ofxPHP - embed PHP (libphp, Embed SAPI) in an openFrameworks app.
# macOS arm64 + x86_64 XCFrameworks are included (libs/phpembed/lib/macos). On other platforms the addon still compiles,
# as stubs: ofxphp::available() is false and no PHP is run.

meta:
	ADDON_NAME = ofxPHP
	ADDON_DESCRIPTION = Embedded PHP (libphp Embed SAPI): an in-process HTTP server that serves static files and PHP, no child process, App Sandbox safe
	ADDON_AUTHOR = Dan Rosser
	ADDON_TAGS = "php" "server" "web" "embed"
	ADDON_URL = https://github.com/danoli3/ofxPHP

common:
	# src/ and libs/phpembed/lib/<platform>/*.xcframework are picked up automatically.
	ADDON_INCLUDES = src
	# Use the shared PHP include tree; standalone OpenSSL must not override the app's headers.
	ADDON_INCLUDES_EXCLUDE = libs/phpembed/lib/%

osx:
	# PHP's own headers are laid out as include/php/{main,Zend,TSRM,sapi/embed,...}: each is an include root.
	ADDON_INCLUDES += libs/phpembed/include/php
	ADDON_INCLUDES += libs/phpembed/include/php/main
	ADDON_INCLUDES += libs/phpembed/include/php/Zend
	ADDON_INCLUDES += libs/phpembed/include/php/TSRM
	ADDON_INCLUDES += libs/phpembed/include/php/sapi/embed
	# Turns on the real implementation in src/ofxPHP.cpp (without it the addon builds as stubs).
	ADDON_CFLAGS = -DOFX_PHP=1
	# Universal PHP, oniguruma, libxml2 and SQLite XCFrameworks are linked from lib/macos automatically.
	# OpenSSL is kept in lib/standalone: PHP uses the OpenSSL openFrameworks already provides.
	ADDON_LDFLAGS = -lresolv -liconv -lz

macos:
	ADDON_INCLUDES += libs/phpembed/include/php
	ADDON_INCLUDES += libs/phpembed/include/php/main
	ADDON_INCLUDES += libs/phpembed/include/php/Zend
	ADDON_INCLUDES += libs/phpembed/include/php/TSRM
	ADDON_INCLUDES += libs/phpembed/include/php/sapi/embed
	ADDON_CFLAGS = -DOFX_PHP=1
	ADDON_LDFLAGS = -lresolv -liconv -lz
