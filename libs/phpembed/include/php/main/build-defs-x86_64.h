/*
   +----------------------------------------------------------------------+
   | Copyright (c) The PHP Group                                          |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | https://www.php.net/license/3_01.txt                                 |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Stig Sæther Bakken <ssb@php.net>                             |
   +----------------------------------------------------------------------+
*/

#define CONFIGURE_COMMAND " './configure'  '--prefix=/ofxphp' '--host=x86_64-apple-darwin' '--build=x86_64-apple-darwin' '--enable-embed=static' '--disable-all' '--disable-cli' '--disable-cgi' '--disable-phpdbg' '--without-pear' '--enable-session' '--enable-ctype' '--enable-tokenizer' '--enable-fileinfo' '--enable-filter' '--enable-mbstring' '--enable-pdo' '--with-pdo-sqlite' '--with-sqlite3' '--with-openssl' '--with-zlib' '--with-libxml' '--enable-dom' '--enable-simplexml' '--enable-xml' '--with-iconv=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.2.sdk/usr' '--with-config-file-path=/etc/ofxphp' '--with-config-file-scan-dir=' 'build_alias=x86_64-apple-darwin' 'host_alias=x86_64-apple-darwin' 'PKG_CONFIG_PATH=' 'PKG_CONFIG_LIBDIR=/ofxphp/lib/pkgconfig' 'ZLIB_CFLAGS=-I/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.2.sdk/usr/include' 'ZLIB_LIBS=-lz'"
#define PHP_PROG_SENDMAIL	"/usr/sbin/sendmail"
#define PEAR_INSTALLDIR         ""
#define PHP_INCLUDE_PATH	".:"
#define PHP_EXTENSION_DIR       "/ofxphp/lib/php/extensions/no-debug-non-zts-20250925"
#define PHP_PREFIX              "/ofxphp"
#define PHP_BINDIR              "/ofxphp/bin"
#define PHP_SBINDIR             "/ofxphp/sbin"
#define PHP_MANDIR              "/ofxphp/php/man"
#define PHP_LIBDIR              "/ofxphp/lib/php"
#define PHP_DATADIR             "/ofxphp/share/php"
#define PHP_SYSCONFDIR          "/ofxphp/etc"
#define PHP_LOCALSTATEDIR       "/ofxphp/var"
#define PHP_CONFIG_FILE_PATH    "/etc/ofxphp"
#define PHP_CONFIG_FILE_SCAN_DIR    ""
#define PHP_SHLIB_SUFFIX        "so"
#define PHP_SHLIB_EXT_PREFIX    ""
