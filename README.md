# ofxPHP

Embed PHP in an openFrameworks app: an in-process HTTP server (loopback only) that serves static files and
runs PHP through libphp's **Embed SAPI**. No child process, so it works under the macOS App Sandbox, where
`php -S` cannot be spawned.

- PHP 8.5.1, **NTS** (not thread-safe): PHP requests run one at a time behind one mutex.
- **macOS arm64** binaries are bundled. On any other platform the addon compiles as stubs
  (`ofxphp::available()` is false, nothing runs), so a cross-platform project still builds.
- Extracted from SuperServerMegaManager.

## Use it

Clone into your openFrameworks `addons` directory:

```sh
git clone https://github.com/danoli3/ofxPHP.git
```

Add `ofxPHP` to `addons.make` (or tick it in projectGenerator and press Update). Nothing else: the include
paths, `-DOFX_PHP=1`, the static libraries and the system libraries all come from `addon_config.mk`.

```cpp
#include "ofxPHP.h"

ofxphp::EmbeddedConfig cfg;
cfg.docroot = "/path/to/site";
cfg.port    = 8300;                       // 127.0.0.1 only
ofxphp::EmbeddedServer server("mysite");
if (server.start(cfg)) { /* http://127.0.0.1:8300 */ }
...
server.stop();
ofxphp::shutdownEmbeddedPhp();            // once, at app exit
```

`EmbeddedConfig` also takes a router script, `memory_limit`, `max_execution_time`, timezone, `display_errors`
and extra `$_SERVER` entries. `upload_max_filesize` / `post_max_size` are PHP_INI_PERDIR and are **not** applied.
`ofxphp::embeddedPhpVersion()` returns e.g. `PHP 8.5.1 (embed)`.

Requests that resolve outside the docroot (`..`, `%2e%2e`, symlinks out) get 403.

## Layout

| Path | What |
|---|---|
| `src/ofxPHP.{h,cpp}` | the server and the PHP glue |
| `libs/phpembed/include/php/` | PHP headers (`main`, `Zend`, `TSRM`, `sapi/embed`, ...) |
| `libs/phpembed/lib/macos/` | auto-linked: `libofxphp.a` (libphp merged into one object), `libonig.a`, `libxml2.a`, `libsqlite3.a` |
| `libs/phpembed/lib/standalone/` | `libssl.a`, `libcrypto.a`: **only** for this addon's own test build, never auto-linked |
| `scripts/build_embed_lib.sh` | rebuild `libofxphp.a` from a `libphp.a` |
| `tests/test_ofxPHP.cpp` | HTTP 200, POST + query, response headers, traversal blocked, 404 |
| `Makefile`, `CMakeLists.txt` | standalone build + test, and a CMake target for CMake users |

## Build and test the addon on its own

```sh
make test                                                   # Makefile
cmake -S . -B build -DOFXPHP_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build   # CMake
```

In a CMake project: `add_subdirectory(addons/ofxPHP)` then `target_link_libraries(app PRIVATE ofxPHP)`. Link
your own OpenSSL (see below).

## Things to know

- **OpenSSL is not bundled where oF would pick it up.** PHP's openssl/curl code uses the OpenSSL your app
  already links (openFrameworks provides one). Bundling a second copy in `lib/macos` would put two
  OpenSSLs in one process. If your app has no OpenSSL, add `libs/phpembed/lib/standalone/*.a` to its link line.
- **libsqlite3 and libxml2 are bundled** because Apple's system SQLite lacks `sqlite3_load_extension` and the
  system libxml2 lacks symbols PHP uses.
- `libofxphp.a` hides only its bundled `_pcre2_*` symbols (so they cannot clash with an app's own pcre2).
  Hiding everything but the embed entry points breaks the link: the server calls the PHP API directly.
- The bundled objects were built for macOS 14, the addon targets 12+, so the linker prints
  "built for newer macOS" warnings. They are harmless here.
- arm64 only. An x86_64 or iOS build needs its own `libphp.a`, merged with `scripts/build_embed_lib.sh`
  (adapt `-arch` and `-platform_version`) into `lib/<platform>/`.

## Licences

ofxPHP's original addon code is released under the [MIT License](LICENSE), copyright (c) 2026 Dan Rosser.
Bundled third-party code and libraries retain their own licenses; see [their notices](libs/phpembed/licenses/).

PHP is under the PHP License 3.01, oniguruma BSD-2, libxml2 and SQLite are MIT / public domain, OpenSSL is Apache-2.0.
Keep their notices if you redistribute an app built with this addon.
