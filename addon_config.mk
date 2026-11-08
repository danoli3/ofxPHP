# ofxPHP - embed PHP (libphp, Embed SAPI) in an openFrameworks app.
# macOS arm64 binaries are included (libs/phpembed/lib/macos). On other platforms the addon still compiles,
# as stubs: ofxphp::available() is false and no PHP is run.

meta:
	ADDON_NAME = ofxPHP
	ADDON_DESCRIPTION = Embedded PHP (libphp Embed SAPI): an in-process HTTP server that serves static files and PHP, no child process, App Sandbox safe
	ADDON_AUTHOR = Dan Rosser
	ADDON_TAGS = "php" "server" "web" "embed"
	ADDON_URL = https://github.com/danoli3/ofxPHP

common:
	# src/ is picked up automatically; libs/phpembed/lib/<platform>/*.a is linked automatically.
	ADDON_INCLUDES = src

osx:
	# PHP's own headers are laid out as include/php/{main,Zend,TSRM,sapi/embed,...}: each is an include root.
	ADDON_INCLUDES += libs/phpembed/include/php
	ADDON_INCLUDES += libs/phpembed/include/php/main
	ADDON_INCLUDES += libs/phpembed/include/php/Zend
	ADDON_INCLUDES += libs/phpembed/include/php/TSRM
	ADDON_INCLUDES += libs/phpembed/include/php/sapi/embed
	# Turns on the real implementation in src/ofxPHP.cpp (without it the addon builds as stubs).
	ADDON_CFLAGS = -DOFX_PHP=1
	# libphp's own dependencies that macOS provides. libofxphp.a (libphp merged into one object), libonig.a and
	# libxml2.a and libsqlite3.a are linked from libs/phpembed/lib/macos automatically. OpenSSL is NOT bundled there: PHP's
	# openssl/curl code links against the OpenSSL openFrameworks already provides.
	ADDON_LDFLAGS = -lresolv -liconv -lz
