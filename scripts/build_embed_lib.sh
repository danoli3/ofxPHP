#!/bin/sh
# Rebuild libs/phpembed/lib/macos/libofxphp.a from a PHP static library built with --enable-embed=static.
#   scripts/build_embed_lib.sh /path/to/libphp.a [min-macos] [arch] [output-dir]
# libphp.a is merged into one object with `ld -r -all_load`; only its bundled pcre2 symbols are hidden
# (`_pcre2_*`) so they cannot clash with the pcre2 an app already links. Everything else stays exported:
# hiding it breaks the link, because the server calls the PHP API directly.
set -e
LIBPHP=${1:?usage: build_embed_lib.sh /path/to/libphp.a [min-macos]}
MIN=${2:-12.0}
ARCH=${3:-$(uname -m)}
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${4:-$HERE/build/embed/$ARCH}
mkdir -p "$OUT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
printf '_pcre2_*\n' > "$TMP/hide.txt"
ld -r -arch "$ARCH" -platform_version macos "$MIN" "$(xcrun --show-sdk-version)" -all_load \
   -unexported_symbols_list "$TMP/hide.txt" -o "$TMP/libphp_embedded.o" "$LIBPHP"
libtool -static -o "$OUT/libofxphp.a" "$TMP/libphp_embedded.o"
echo "wrote $OUT/libofxphp.a"
nm -g "$OUT/libofxphp.a" | grep -E " T _php_embed_(init|shutdown)"
