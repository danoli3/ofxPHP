// Embedded PHP server tests. Build: make test (or CMake with OFXPHP_BUILD_TESTS=ON).
#include "../src/ofxPHP.h"
#include <cstdio>
#include <fstream>
#include <thread>
#include <chrono>
using namespace ofxphp;
namespace fs = std::filesystem;
static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static void write(const fs::path & p, const std::string & t) { fs::create_directories(p.parent_path()); std::ofstream(p) << t; }
static std::string sh(const std::string & cmd) { std::string out; FILE * f = popen(cmd.c_str(), "r"); char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) out.append(b, n); pclose(f); return out; }
int main() {
	fs::path root = fs::temp_directory_path() / "ss-embedded-test";
	fs::remove_all(root);
	write(root / "site/index.php", "<?php echo 'hello ' . PHP_VERSION;");
	write(root / "site/post.php", "<?php header('X-Test: yes'); echo 'got:' . ($_POST['a'] ?? 'none') . ' q:' . ($_GET['q'] ?? '');");
	write(root / "site/modules.php", R"PHP(<?php
foreach (['openssl', 'sqlite3', 'pdo_sqlite', 'dom', 'SimpleXML', 'xml', 'mbstring',
          'session', 'ctype', 'tokenizer', 'fileinfo', 'filter'] as $extension) {
    if (!extension_loaded($extension)) { echo 'missing:' . $extension; return; }
}
$db = new SQLite3(':memory:');
$pdo = new PDO('sqlite::memory:');
$xml = new DOMDocument();
$xml->loadXML('<ok/>');
echo openssl_digest('abc', 'sha256'), '|', $db->querySingle('SELECT 42'), '|',
     $pdo->query('SELECT 43')->fetchColumn(), '|', $xml->documentElement->tagName, '|',
     mb_strlen("h\xc3\xa9", 'UTF-8');
)PHP");
	write(root / "site/page.html", "<h1>static</h1>");
	write(root / "secret.txt", "TOP SECRET");
	CHECK(resolveInsideDocroot(root / "site", "/../secret.txt").empty());
	CHECK(resolveInsideDocroot(root / "site", "/%2e%2e/secret.txt").empty());
	CHECK(resolveInsideDocroot(root / "site", "/%2E%2E%2Fsecret.txt").empty());
	CHECK(!resolveInsideDocroot(root / "site", "/page.html").empty());
	CHECK(!resolveInsideDocroot(root / "site", "/").empty());
	fs::create_symlink(root / "secret.txt", root / "site/link.txt");
	CHECK(resolveInsideDocroot(root / "site", "/link.txt").empty());
	EmbeddedServer srv("t");
	EmbeddedConfig cfg; cfg.docroot = root / "site"; cfg.port = 18431;
	CHECK(srv.start(cfg));
	std::string base = "curl -s -m 10 ";
	std::string u = " http://127.0.0.1:18431";
	CHECK(sh(base + "-o /dev/null -w '%{http_code}'" + u + "/") == "200");
	CHECK(sh(base + u + "/").find("hello 8.") == 0);
	CHECK(sh(base + u + "/page.html") == "<h1>static</h1>");
	CHECK(sh(base + u + "/modules.php") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad|42|43|ok|2");
	CHECK(sh(base + "-d a=42 -D - '" + u.substr(1) + "/post.php?q=z'").find("got:42 q:z") != std::string::npos);
	CHECK(sh(base + "-d a=42 -D - " + u + "/post.php").find("X-Test: yes") != std::string::npos);
	CHECK(sh(base + "-o /dev/null -w '%{http_code}' --path-as-is" + u + "/../secret.txt") == "403");
	CHECK(sh(base + "-o /dev/null -w '%{http_code}' --path-as-is" + u + "/%2e%2e/secret.txt") == "403");
	CHECK(sh(base + "-o /dev/null -w '%{http_code}'" + u + "/link.txt") == "403");
	CHECK(sh(base + "-o /dev/null -w '%{http_code}'" + u + "/nope") == "404");
	srv.stop();
	shutdownEmbeddedPhp();
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
