// Generated architecture selector; see scripts/build_macos_xcframeworks.py.
#if defined(__arm64__) || defined(__aarch64__)
#include "build-defs-arm64.h"
#elif defined(__x86_64__)
#include "build-defs-x86_64.h"
#else
#error "ofxPHP: unsupported macOS architecture"
#endif
