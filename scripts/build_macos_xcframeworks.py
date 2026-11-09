#!/usr/bin/env python3
"""Build static PHP and dependencies for both Mac architectures, then package XCFrameworks.

Requires Xcode, Python 3, CMake, pkg-config, make and Perl. No Homebrew libraries
are linked. Sources are pinned and SHA-256 checked in macos-sources.json.
Builds and logs stay under build/macos; completed dependency builds are reused.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent
SOURCES = json.loads(Path(__file__).with_name("macos-sources.json").read_text())
ARCHES = ("arm64", "x86_64")
LIBRARIES = {"libofxphp": "php", "libonig": "oniguruma", "libxml2": "libxml2",
             "libsqlite3": "sqlite", "libssl": "openssl", "libcrypto": "openssl"}


def run(args, cwd=None, env=None, log=None):
    args = [str(arg) for arg in args]
    if log:
        with log.open("a") as output:
            output.write("\n$ " + " ".join(args) + "\n")
            output.flush()
            result = subprocess.run(args, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            print("\n".join(log.read_text(errors="replace").splitlines()[-30:]), file=sys.stderr)
            raise RuntimeError(f"Command failed; see {log}")
    else:
        subprocess.run(args, cwd=cwd, env=env, check=True)


def download(name, work):
    spec = SOURCES[name]
    archive = work / "downloads" / spec.get("filename", spec["url"].rsplit("/", 1)[1])
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_name(archive.name + ".part")
        run(["curl", "-fL", "--retry", "2", spec["url"], "-o", temporary])
        temporary.rename(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
        raise RuntimeError(f"SHA-256 mismatch: {archive}")
    return archive


def extract(archive, destination):
    if not destination.exists():
        destination.mkdir(parents=True)
        run(["tar", "-xf", archive, "-C", destination])
    directories = [path for path in destination.iterdir() if path.is_dir()]
    if len(directories) != 1:
        raise RuntimeError(f"Expected one source directory in {destination}")
    return directories[0]


def build_arch(arch, options, archives, sdk):
    work = options.work / arch
    prefix = work / "install"
    prefix.mkdir(parents=True, exist_ok=True)
    flags = f"-arch {arch} -isysroot {sdk} -mmacosx-version-min={options.min_macos} -O2 -fPIC"
    env = dict(os.environ, CC="/usr/bin/clang", CXX="/usr/bin/clang++", CFLAGS=flags,
               CXXFLAGS=flags, LDFLAGS=f"-arch {arch} -isysroot {sdk} -mmacosx-version-min={options.min_macos}",
               MACOSX_DEPLOYMENT_TARGET=options.min_macos, SDKROOT=sdk,
               ZLIB_CFLAGS=f"-I{sdk}/usr/include", ZLIB_LIBS="-lz",
               # PHP's older libtool probes sysctl directly; getconf also works in restricted shells.
               lt_cv_sys_max_cmd_len=str(int(subprocess.check_output(["getconf", "ARG_MAX"])) // 4),
               PKG_CONFIG_PATH="", PKG_CONFIG_LIBDIR=str(prefix / "lib/pkgconfig"))
    triplet = ("aarch64" if arch == "arm64" else arch) + "-apple-darwin"
    for name in ("oniguruma", "libxml2", "sqlite", "openssl", "php"):
        stamp = work / (name + ".complete.json")
        signature = dict(source=SOURCES[name], min_macos=options.min_macos, arch=arch)
        if name == "php":
            signature["dependencies"] = {key: value for key, value in SOURCES.items() if key != "php"}
        if stamp.exists() and json.loads(stamp.read_text()) == signature:
            print(f"[{arch}] reusing {name}", flush=True)
            continue
        print(f"[{arch}] building {name}; log: {work / (name + '.log')}", flush=True)
        if stamp.exists() and json.loads(stamp.read_text())["source"] != SOURCES[name]:
            shutil.rmtree(work / "source" / name)
            if name == "libxml2" and (work / "xml2-cmake").exists():
                shutil.rmtree(work / "xml2-cmake")
        source = extract(archives[name], work / "source" / name)
        log = work / (name + ".log")

        def command(args, cwd=source, command_env=env):
            run(args, cwd=cwd, env=command_env, log=log)

        if name == "oniguruma":
            if (source / "Makefile").exists():
                command(["make", "clean"])
            command(["./configure", f"--prefix={prefix}", "--disable-shared", "--enable-static",
                     f"--host={triplet}", f"--build={triplet}"])
            command(["make", f"-j{options.jobs}"])
            command(["make", "install"])
        elif name == "libxml2":
            binary = work / "xml2-cmake"
            command(["cmake", "-S", source, "-B", binary, "-DCMAKE_BUILD_TYPE=Release",
                     f"-DCMAKE_INSTALL_PREFIX={prefix}", f"-DCMAKE_OSX_ARCHITECTURES={arch}",
                     f"-DCMAKE_OSX_SYSROOT={sdk}", f"-DCMAKE_OSX_DEPLOYMENT_TARGET={options.min_macos}",
                     "-DBUILD_SHARED_LIBS=OFF", "-DLIBXML2_WITH_PROGRAMS=OFF",
                     "-DLIBXML2_WITH_PYTHON=OFF", "-DLIBXML2_WITH_TESTS=OFF",
                     "-DLIBXML2_WITH_LZMA=OFF", "-DLIBXML2_WITH_ZLIB=OFF",
                     "-DLIBXML2_WITH_ICONV=ON", "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local",
                     f"-DIconv_INCLUDE_DIR={sdk}/usr/include", f"-DIconv_LIBRARY={sdk}/usr/lib/libiconv.tbd"])
            command(["cmake", "--build", binary, "--parallel", options.jobs])
            command(["cmake", "--install", binary])
        elif name == "sqlite":
            (prefix / "lib/pkgconfig").mkdir(parents=True, exist_ok=True)
            (prefix / "include").mkdir(exist_ok=True)
            command([env["CC"], *flags.split(), "-DSQLITE_ENABLE_COLUMN_METADATA",
                     "-DSQLITE_ENABLE_FTS5", "-DSQLITE_ENABLE_RTREE", "-DSQLITE_THREADSAFE=1",
                     "-c", "sqlite3.c", "-o", "sqlite3.o"])
            command(["xcrun", "libtool", "-static", "-o", prefix / "lib/libsqlite3.a", "sqlite3.o"])
            for header in ("sqlite3.h", "sqlite3ext.h"):
                shutil.copy2(source / header, prefix / "include" / header)
            (prefix / "lib/pkgconfig/sqlite3.pc").write_text(
                f"prefix={prefix}\nlibdir=${{prefix}}/lib\nincludedir=${{prefix}}/include\n"
                "Name: SQLite\nDescription: SQLite database library\nVersion: " + SOURCES[name]["version"] +
                "\nLibs: -L${libdir} -lsqlite3\nLibs.private: -lm -lpthread\nCflags: -I${includedir}\n")
        elif name == "openssl":
            if (source / "Makefile").exists():
                command(["make", "clean"])
            target = "darwin64-" + arch + "-cc"
            command(["perl", "Configure", target, "no-shared", "no-tests", "no-module",
                     f"--prefix={prefix}", "--openssldir=/etc/ssl", "--libdir=lib"])
            command(["make", f"-j{options.jobs}", "build_libs"])
            command(["make", "install_dev"])
        else:
            # Distribution tarballs contain generated parsers; no bison/re2c is needed.
            if (source / "Makefile").exists():
                command(["make", "clean"])
            command(["./configure", f"--prefix={prefix}", f"--host={triplet}", f"--build={triplet}",
                     "--enable-embed=static", "--disable-all", "--disable-cli", "--disable-cgi",
                     "--disable-phpdbg", "--without-pear", "--enable-session", "--enable-ctype",
                     "--enable-tokenizer", "--enable-fileinfo", "--enable-filter", "--enable-mbstring",
                     "--enable-pdo", "--with-pdo-sqlite", "--with-sqlite3", "--with-openssl",
                     "--with-zlib", "--with-libxml", "--enable-dom", "--enable-simplexml", "--enable-xml",
                     f"--with-iconv={sdk}/usr", "--with-config-file-path=/etc/ofxphp",
                     "--with-config-file-scan-dir="])
            command(["make", f"-j{options.jobs}"])
            command(["make", "install"])
            command([ROOT / "scripts/build_embed_lib.sh", prefix / "lib/libphp.a", options.min_macos,
                     arch, prefix / "lib"])
        stamp.write_text(json.dumps(signature, indent=2) + "\n")


def merge_headers(first, second, output):
    """Keep architecture-dependent generated headers next to a compiler-selected wrapper."""
    first_files = {path.relative_to(first) for path in first.rglob("*") if path.is_file()}
    second_files = {path.relative_to(second) for path in second.rglob("*") if path.is_file()}
    if first_files != second_files:
        raise RuntimeError(f"Header sets differ: {first_files ^ second_files}")
    for relative in sorted(first_files):
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        contents = [(base / relative).read_bytes() for base in (first, second)]
        if contents[0] == contents[1]:
            target.write_bytes(contents[0])
        else:
            if target.suffix != ".h":
                raise RuntimeError(f"Non-header differs: {relative}")
            for arch, content in zip(ARCHES, contents):
                target.with_name(target.stem + "-" + arch + ".h").write_bytes(content)
            target.write_text("// Generated architecture selector; see scripts/build_macos_xcframeworks.py.\n"
                              f'#if defined(__arm64__) || defined(__aarch64__)\n#include "{target.stem}-arm64.h"\n'
                              f'#elif defined(__x86_64__)\n#include "{target.stem}-x86_64.h"\n'
                              '#else\n#error "ofxPHP: unsupported macOS architecture"\n#endif\n')


def package(options):
    print("Packaging universal macOS XCFrameworks", flush=True)
    stage = options.work / "package"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir()
    prefixes = [options.work / arch / "install" for arch in ARCHES]
    for arch in ARCHES:
        for name in SOURCES:
            stamp = options.work / arch / (name + ".complete.json")
            expected = dict(source=SOURCES[name], min_macos=options.min_macos, arch=arch)
            if name == "php":
                expected["dependencies"] = {key: value for key, value in SOURCES.items() if key != "php"}
            if not stamp.exists() or json.loads(stamp.read_text()) != expected:
                raise RuntimeError(f"Build {name} for {arch} with the same deployment target before packaging")
    header_groups = {"php": "php", "openssl": "openssl", "libxml2": "libxml2"}
    for group, subdirectory in header_groups.items():
        merge_headers(*(prefix / "include" / subdirectory for prefix in prefixes),
                      stage / group / "include" / subdirectory)
    for group, headers in {"oniguruma": ("oniguruma.h", "oniggnu.h"),
                           "sqlite": ("sqlite3.h", "sqlite3ext.h")}.items():
        output = stage / group / "include"
        output.mkdir(parents=True)
        for header in headers:
            if (prefixes[0] / "include" / header).read_bytes() != (prefixes[1] / "include" / header).read_bytes():
                raise RuntimeError(f"Unexpected architecture-dependent header: {header}")
            shutil.copy2(prefixes[0] / "include" / header, output / header)
    # Remove local build paths from installed PHP metadata.
    for path in (stage / "php/include/php/main").glob("*.h"):
        text = path.read_text()
        for prefix in prefixes:
            text = text.replace(str(prefix), "/ofxphp")
        text = text.replace(str(options.work), "/ofxphp-build")
        path.write_text(text)
    php_source = next((options.work / "arm64/source/php").iterdir())
    license_dir = ROOT / "libs/phpembed/licenses"
    for name, relative, destination in (
            ("php", "LICENSE", "PHP.txt"), ("php", "Zend/LICENSE", "Zend.txt"),
            ("openssl", "LICENSE.txt", "OpenSSL.txt"),
            ("oniguruma", "COPYING", "oniguruma.txt"), ("libxml2", "Copyright", "libxml2.txt")):
        source = next((options.work / "arm64/source" / name).iterdir())
        shutil.copy2(source / relative, license_dir / destination)
    # PHP contains additional bundled components (timelib, libmbfl, PCRE2, etc.).
    for path in php_source.rglob("*"):
        if path.is_file() and (path.name.upper().startswith("LICENSE") or
                               path.name.upper().startswith("LICENCE") or
                               path.name.upper() in ("COPYING", "NOTICE")):
            destination = license_dir / "php-source" / path.relative_to(php_source)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    for library, group in LIBRARIES.items():
        universal = stage / (library + ".a")
        run(["xcrun", "lipo", "-create", *(p / "lib" / (library + ".a") for p in prefixes), "-output", universal])
        for arch in ARCHES:
            run(["xcrun", "lipo", "-verify_arch", arch, universal])
        bundle = stage / (library + ".xcframework")
        run(["xcodebuild", "-create-xcframework", "-library", universal,
             "-headers", stage / group / "include", "-output", bundle])
        destination = ROOT / "libs/phpembed/lib" / ("standalone" if group == "openssl" else "macos") / bundle.name
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(bundle, destination)
    # Shared headers also work with the openFrameworks project generator's existing include paths.
    installed_headers = ROOT / "libs/phpembed/include/php"
    if installed_headers.exists():
        shutil.rmtree(installed_headers)
    shutil.copytree(stage / "php/include/php", installed_headers)
    metadata = dict(platform="macos", architectures=list(ARCHES), deployment_target=options.min_macos,
                    sources=SOURCES, xcode=subprocess.check_output(["xcodebuild", "-version"], text=True).strip())
    (ROOT / "libs/phpembed/build-info.json").write_text(json.dumps(metadata, indent=2) + "\n")
    # These are superseded by the XCFrameworks; leaving both would link every library twice in oF.
    for library, group in LIBRARIES.items():
        legacy = ROOT / "libs/phpembed/lib" / ("standalone" if group == "openssl" else "macos") / (library + ".a")
        if legacy.exists():
            legacy.unlink()
    archive = ROOT / "build/ofxPHP-macos-xcframeworks.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for directory in (ROOT / "libs/phpembed/lib", license_dir):
            for path in sorted(directory.rglob("*")):
                if path.is_file():
                    output.write(path, path.relative_to(ROOT / "libs/phpembed"))
        output.write(ROOT / "libs/phpembed/build-info.json", "build-info.json")
    print(f"Created {archive}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=ARCHES, action="append", help="Build only this architecture (repeatable)")
    parser.add_argument("--min-macos", default="12.0")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    parser.add_argument("--work", type=Path, default=ROOT / "build/macos")
    parser.add_argument("--no-package", action="store_true", help="Build selected architectures without packaging")
    parser.add_argument("--package-only", action="store_true", help="Package existing builds without downloading or compiling")
    options = parser.parse_args()
    options.work = options.work.resolve()
    if sys.platform != "darwin":
        parser.error("Run this script on macOS with Xcode installed")
    if options.jobs < 1:
        parser.error("--jobs must be positive")
    if not options.package_only:
        sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
        archives = {name: download(name, options.work) for name in SOURCES}
        for arch in options.arch or ARCHES:
            build_arch(arch, options, archives, sdk)
    if not options.no_package:
        package(options)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
