// Embedded PHP server tests. Build: make test (or CMake with OFXPHP_BUILD_TESTS=ON).
#include "../src/ofxPHP.h"
#include <cstdio>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
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
	CHECK(serverAvailable());
#ifdef _WIN32
	CHECK(resolveInsideDocroot(root / "site", "/..\\secret.txt").empty());
	CHECK(resolveInsideDocroot(root / "site", "/%5c..%5csecret.txt").empty());
	CHECK(resolveInsideDocroot(root / "site", "/page.html::$DATA").empty());
#endif
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

	// ---- byte ranges, mounts, extra headers, bind address, request hook
	size_t a = 0, b = 0;
	CHECK(parseByteRange("bytes=0-3", 10, a, b) && a == 0 && b == 3);
	CHECK(parseByteRange("bytes=4-", 10, a, b) && a == 4 && b == 9);
	CHECK(parseByteRange("bytes=-3", 10, a, b) && a == 7 && b == 9);
	CHECK(parseByteRange("bytes=8-99", 10, a, b) && b == 9);
	CHECK(!parseByteRange("bytes=10-", 10, a, b));
	CHECK(!parseByteRange("bytes=0-1,4-5", 10, a, b));
	CHECK(!parseByteRange("items=0-1", 10, a, b));
	CHECK(!parseByteRange("bytes=5-2", 10, a, b));
	CHECK(statusText(206) == "Partial Content" && statusText(416) == "Range Not Satisfiable");

	write(root / "games/LOPIR - Ref13/index.html", "<h1>ref13</h1>");
	write(root / "games/LOPIR - Ref13/voice.mp3", "0123456789");
	write(root / "games/LOPIR - Ref13/evil.php", "<?php echo 'RAN';");
	fs::create_symlink(root / "secret.txt", root / "games/LOPIR - Ref13/link.txt");
	EmbeddedServer srv2("t2");
	EmbeddedConfig c2; c2.docroot = root / "site"; c2.port = 18432; c2.bindAddress = "0.0.0.0";
	c2.mounts.push_back({ "/ref13", root / "games/LOPIR - Ref13" });
	c2.extraHeaders.push_back({ "X-Content-Type-Options", "nosniff" });
	std::vector<RequestInfo> seen;
	std::mutex seenLock;
	c2.onRequest = [&](const RequestInfo & r) { std::lock_guard<std::mutex> l(seenLock); seen.push_back(r); };
	CHECK(srv2.start(c2));
	std::string v = " http://127.0.0.1:18432";
	CHECK(sh(base + v + "/ref13/") == "<h1>ref13</h1>");
	CHECK(sh(base + v + "/ref13/voice.mp3") == "0123456789");
	CHECK(sh(base + "-H 'Range: bytes=2-5'" + v + "/ref13/voice.mp3") == "2345");
	CHECK(sh(base + "-o /dev/null -w '%{http_code}' -H 'Range: bytes=2-5'" + v + "/ref13/voice.mp3") == "206");
	CHECK(sh(base + "-H 'Range: bytes=-3'" + v + "/ref13/voice.mp3") == "789");
	CHECK(sh(base + "-o /dev/null -w '%{http_code}' -H 'Range: bytes=50-'" + v + "/ref13/voice.mp3") == "416");
	CHECK(sh(base + "-D - -o /dev/null -H 'Range: bytes=0-1'" + v + "/ref13/voice.mp3").find("Content-Range: bytes 0-1/10") != std::string::npos);
	CHECK(sh(base + "-D - -o /dev/null" + v + "/ref13/voice.mp3").find("Accept-Ranges: bytes") != std::string::npos);
	CHECK(sh(base + "-D - -o /dev/null" + v + "/ref13/voice.mp3").find("X-Content-Type-Options: nosniff") != std::string::npos);
	CHECK(sh(base + "-D - -o /dev/null" + v + "/page.html").find("X-Content-Type-Options: nosniff") != std::string::npos);
	CHECK(sh(base + "-o /dev/null -w '%{http_code}'" + v + "/ref13/evil.php") == "404"); // mounts never run PHP
	CHECK(sh(base + "-o /dev/null -w '%{http_code}'" + v + "/ref13/link.txt") == "403");
	CHECK(sh(base + "-o /dev/null -w '%{http_code}' --path-as-is" + v + "/ref13/../../secret.txt") == "403");
	CHECK(sh(base + "-o /dev/null -w '%{http_code} %{redirect_url}'" + v + "/ref13") == "301 http://127.0.0.1:18432/ref13/");
	CHECK(sh(base + v + "/") .find("hello 8.") == 0); // docroot still works beside the mount
	{
		std::lock_guard<std::mutex> l(seenLock);
		bool found = false;
		for (auto & r : seen) if (r.path == "/ref13/voice.mp3" && r.status == 206 && r.bytes == 4 && r.remote == "127.0.0.1" && r.method == "GET") found = true;
		CHECK(found);
	}
	srv2.stop();
	write(root / "games/LOPIR - Ref13/.hidden", "nope");
	c2.port = 18434; c2.readOnly = true; c2.denyDotFiles = true;
	EmbeddedServer srv4("t4");
	CHECK(srv4.start(c2));
	std::string w = " http://127.0.0.1:18434";
	CHECK(sh(base + "-o /dev/null -w '%{http_code}'" + w + "/ref13/.hidden") == "403");
	CHECK(sh(base + "-o /dev/null -w '%{http_code}'" + w + "/.git/config") == "403");
	CHECK(sh(base + "-o /dev/null -w '%{http_code}' -X POST -d a=1" + w + "/post.php") == "405");
	CHECK(sh(base + "-D - -o /dev/null -X DELETE" + w + "/ref13/").find("Allow: GET, HEAD") != std::string::npos);
	CHECK(sh(base + "-o /dev/null -w '%{http_code}' -I" + w + "/ref13/voice.mp3") == "200");
	CHECK(sh(base + w + "/ref13/voice.mp3") == "0123456789");
	srv4.stop();
	EmbeddedServer srv3("t3");
	EmbeddedConfig c3; c3.docroot = root / "site"; c3.port = 18433; c3.bindAddress = "not-an-address";
	CHECK(!srv3.start(c3));

	// ---- stopping: the port is free right away, a stalled client can't hold the server up, stop twice is fine,
	// and a client that hangs up mid-transfer doesn't kill the process (SIGPIPE)
	{
		// "free" = nothing listens (connect is refused). A bind() test would also trip on TIME_WAIT leftovers.
		auto closedPort = [](int port) {
			int fd = ::socket(AF_INET, SOCK_STREAM, 0);
			sockaddr_in a {}; a.sin_family = AF_INET; a.sin_port = htons(port); inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
			bool refused = ::connect(fd, (sockaddr *)&a, sizeof a) != 0;
			::close(fd);
			return refused;
		};
		auto portFree = [&](int port) {
			if (!closedPort(port)) return false;
			int fd = ::socket(AF_INET, SOCK_STREAM, 0);
			int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); // what start() does
			sockaddr_in a {}; a.sin_family = AF_INET; a.sin_port = htons(port); inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
			bool ok = ::bind(fd, (sockaddr *)&a, sizeof a) == 0 && ::listen(fd, 1) == 0;
			::close(fd);
			return ok;
		};
		write(root / "big/index.html", std::string(8 * 1024 * 1024, 'x'));
		EmbeddedServer s5("t5");
		EmbeddedConfig c5; c5.docroot = root / "big"; c5.port = 18435;
		CHECK(s5.start(c5));
		CHECK(!portFree(18435));
		// a client that connects and never sends a request
		int idle = ::socket(AF_INET, SOCK_STREAM, 0);
		sockaddr_in a {}; a.sin_family = AF_INET; a.sin_port = htons(18435); inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
		CHECK(::connect(idle, (sockaddr *)&a, sizeof a) == 0);
		// a client that asks for 8 MB and hangs up after the first bytes
		int quitter = ::socket(AF_INET, SOCK_STREAM, 0);
		CHECK(::connect(quitter, (sockaddr *)&a, sizeof a) == 0);
		const char * get = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
		::send(quitter, get, strlen(get), 0);
		char b[64]; ::recv(quitter, b, sizeof b, 0);
		::close(quitter);
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		auto t0 = std::chrono::steady_clock::now();
		s5.stop();
		double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		CHECK(took < 2.0);          // not held up by the idle client's 10 s read timeout
		CHECK(!s5.isRunning());
		CHECK(portFree(18435));     // the port is closed the moment stop() returns
		s5.stop();                  // twice is fine
		::close(idle);
		// restart on the same port straight away, then let the destructor do the stopping
		{
			EmbeddedServer again("t6");
			CHECK(again.start(c5));
			CHECK(sh(base + "-o /dev/null -w '%{http_code}' http://127.0.0.1:18435/") == "200");
		}
		CHECK(portFree(18435));     // ~EmbeddedServer closed it
	}
	shutdownEmbeddedPhp();
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
