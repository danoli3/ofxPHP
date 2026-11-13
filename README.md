# ofxPHP

Embed PHP in an openFrameworks app: an in-process HTTP server (loopback only) that serves static files and
runs PHP through libphp's **Embed SAPI**. No child process, so it works under the macOS App Sandbox, where
`php -S` cannot be spawned.

- PHP 8.5.1, **NTS** (not thread-safe): PHP requests run one at a time behind one mutex.
- **macOS 12+, arm64 and x86_64** static XCFrameworks are bundled. On other platforms the addon compiles as stubs
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

## Hosting static sites (also on Windows)

`EmbeddedConfig` can also serve plain static folders with no PHP at all, which is why the server part builds on
macOS, Linux and Windows (Winsock; PHP stays macOS-only). `ofxphp::serverAvailable()` says whether it is built.

| Option | What |
|---|---|
| `bindAddress` | `"127.0.0.1"` (default) or `"0.0.0.0"` to serve the local network (IPv4 literal) |
| `mounts` | `{"/ref13", folder}` pairs: a URL prefix served from any folder, never runs PHP, same `..`/symlink rules as the docroot. `/ref13` redirects to `/ref13/` |
| `extraHeaders` | added to every response (security headers, `Cache-Control`...) |
| `readOnly` | only GET and HEAD, else 405 with `Allow` |
| `denyDotFiles` | 403 for any path segment starting with `.` |
| `onRequest` | called per finished request with remote address, method, path, status and body bytes (thread-safe callback) |

Static files are served with byte ranges (`206`, `416`, `Accept-Ranges`), which Safari on iPad needs for audio.
`stop()` closes the listening port, wakes open connections and waits for them (10 s at most); it is safe to call
twice and from the destructor. A client that hangs up mid-transfer cannot raise SIGPIPE. On Windows paths containing
`\` or `:` are refused (separators and NTFS streams) and the port is bound with exclusive use.

Requests that resolve outside the docroot (`..`, `%2e%2e`, symlinks out) get 403.

## Layout

| Path | What |
|---|---|
| `src/ofxPHP.{h,cpp}` | the server and the PHP glue |
| `libs/phpembed/include/php/` | PHP headers (`main`, `Zend`, `TSRM`, `sapi/embed`, ...) |
| `libs/phpembed/lib/macos/` | auto-linked: `libofxphp.xcframework` (libphp merged into one object), `libonig.xcframework`, `libxml2.xcframework`, `libsqlite3.xcframework` |
| `libs/phpembed/lib/standalone/` | `libssl.xcframework`, `libcrypto.xcframework`: **only** for this addon's own test build, never auto-linked |
| `libs/phpembed/build-info.json` | source versions, checksums, architectures and deployment target |
| `scripts/build_macos_xcframeworks.py` | build both architectures and package all six XCFrameworks |
| `scripts/macos-sources.json` | pinned source URLs and SHA-256 checksums |
| `scripts/build_embed_lib.sh` | rebuild `libofxphp.a` from a `libphp.a` |
| `tests/test_ofxPHP.cpp` | HTTP, POST + query, response headers, traversal blocked, 404, OpenSSL, SQLite/PDO, XML and mbstring |
| `Makefile`, `CMakeLists.txt` | standalone build + test, and a CMake target for CMake users |

## Build and test the addon on its own

```sh
make test                                                   # Makefile
make test ARCHS=x86_64                                       # Intel; runs under Rosetta on Apple silicon
make test ARCHS="arm64 x86_64"                                # Universal executable
cmake -S . -B build -DOFXPHP_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build   # CMake
```

In a CMake project: `add_subdirectory(addons/ofxPHP)` then `target_link_libraries(app PRIVATE ofxPHP)`. Link
your own OpenSSL (see below).

For an Intel or universal CMake build, set `-DCMAKE_OSX_ARCHITECTURES=x86_64` or
`'-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'`. Set `-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0` when configuring.

## Rebuild the XCFrameworks

With Xcode, Python 3, CMake and pkg-config installed:

```sh
make xcframeworks
# or
python3 scripts/build_macos_xcframeworks.py --jobs 8 --min-macos 12.0
```

The script verifies source checksums, builds PHP 8.5.1, oniguruma 6.9.10, libxml2 2.15.4,
SQLite 3.54.1 and standalone OpenSSL 3.5.9 from source for each architecture, then combines each
library into one universal `macos-arm64_x86_64` XCFramework slice. Both architectures use the same
features and deployment target; no Homebrew libraries are linked. Generated PHP and OpenSSL
headers select the correct architecture at compile time.

Downloads, intermediate builds and logs stay in `build/macos/`. Completed dependency builds are
reused. Use `--arch x86_64 --no-package` to build just Intel, or `--package-only` to repackage
completed builds. Building and running Intel configure checks on Apple silicon requires Rosetta.
The script also creates `build/ofxPHP-macos-xcframeworks.zip` with all six bundles, source metadata
and license notices. The bundles contain macOS slices; iOS is not included.

## Things to know

- **OpenSSL is not bundled where oF would pick it up.** PHP's openssl extension uses the OpenSSL your app
  already links (openFrameworks provides one). Bundling a second copy in `lib/macos` would put two
  OpenSSLs in one process. If your app has no OpenSSL, link both XCFrameworks in `libs/phpembed/lib/standalone/`.
- **libsqlite3 and libxml2 are bundled** because Apple's system SQLite lacks `sqlite3_load_extension` and the
  system libxml2 lacks symbols PHP uses.
- `libofxphp.a` hides only its bundled `_pcre2_*` symbols (so they cannot clash with an app's own pcre2).
  Hiding everything but the embed entry points breaks the link: the server calls the PHP API directly.
- All bundled objects target macOS 12.0 on both arm64 and x86_64.

## Licences

ofxPHP's original addon code is released under the [MIT License](LICENSE), copyright (c) 2026 Dan Rosser.
Bundled third-party code and libraries retain their own licenses; see [their notices](libs/phpembed/licenses/).

PHP is under the PHP License 3.01, oniguruma BSD-2, libxml2 and SQLite are MIT / public domain, OpenSSL is Apache-2.0.
Keep their notices if you redistribute an app built with this addon.
