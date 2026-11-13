// SuperServerMegaManager - Copyright (c) 2026 Daniel Rosser. MIT License.
// In-process HTTP server for one site: static files plus PHP through libphp's Embed SAPI.
// No child process, so it works under the macOS App Sandbox (where `php -S` cannot be spawned).
// libphp here is non-thread-safe (NTS), so PHP requests are serialised by one process-wide mutex.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ofxphp {

// True where the HTTP server below is built: macOS, Linux and Windows (static files, ranges, mounts, headers).
// It does not mean PHP is available: that is macOS only for now (see embeddedPhpVersion()).
constexpr bool serverAvailable() {
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
	return true;
#else
	return false;
#endif
}

using SocketHandle = std::intptr_t; // an int fd, or a Windows SOCKET

// One finished request, for logs and session tracking.
struct RequestInfo {
	std::string remote, method, path;
	int status = 0;
	size_t bytes = 0; // response body bytes sent
};

struct EmbeddedConfig {
	std::filesystem::path docroot;
	int port = 0;
	// Address to bind. "127.0.0.1" (default) is loopback only; "0.0.0.0" serves the local network.
	std::string bindAddress = "127.0.0.1";
	// Static folders served under a URL prefix ("/ref13" -> a folder anywhere on disk), checked before the
	// docroot. Never runs PHP, never leaves the folder (same ".." and symlink rules as the docroot).
	std::vector<std::pair<std::string, std::filesystem::path>> mounts;
	// Added to every response the server sends (security headers, X-Robots-Tag, Cache-Control...).
	std::vector<std::pair<std::string, std::string>> extraHeaders;
	// Static hosting hardening: answer only GET and HEAD (405 + Allow), and 403 any path segment starting with ".".
	bool readOnly = false;
	bool denyDotFiles = false;
	// Called once per finished request, on the connection's thread (must be thread-safe).
	std::function<void(const RequestInfo &)> onRequest;
	std::string router;            // optional router script (relative to docroot) for unmatched paths
	std::string memoryLimit = "256M";
	int maxExecutionTime = 30;
	std::string timezone = "UTC";
	bool displayErrors = true;
	std::map<std::string, std::string> env; // extra $_SERVER entries
};

struct HttpRequest {
	std::string method, target, path, query, version, body;
	std::map<std::string, std::string> headers; // lower-case names
};

struct HttpResponse {
	int status = 200;
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
};

// Maps a URL path to a file inside docroot; empty when it would leave the docroot (".." or a symlink out).
std::filesystem::path resolveInsideDocroot(const std::filesystem::path & docroot, const std::string & urlPath);
std::string urlDecode(const std::string & s);
std::string mimeType(const std::filesystem::path & file);
std::string statusText(int status);
// "bytes=a-b", "bytes=a-" or "bytes=-n" against a body of `size` bytes: true and first/last (inclusive) when
// satisfiable; false when malformed or outside the body (answer 416). Only a single range is supported.
bool parseByteRange(const std::string & header, size_t size, size_t & first, size_t & last);
bool parseHttpRequest(const std::string & raw, HttpRequest & out, size_t & headerEnd);

// One PHP request (also used directly by the unit tests). Returns false if PHP is unavailable.
bool runEmbeddedPhp(const EmbeddedConfig & cfg, const std::filesystem::path & script, const HttpRequest & req,
                    const std::string & remoteAddr, HttpResponse & res);
std::string embeddedPhpVersion(); // "PHP 8.5.1 (embed)", initialises the runtime on first call
void shutdownEmbeddedPhp();       // call once at app exit

class EmbeddedServer {
public:
	explicit EmbeddedServer(std::string name, std::function<void(const std::string &)> log = {})
	    : name(std::move(name)), log(std::move(log)) {}
	~EmbeddedServer() { stop(); }

	bool start(const EmbeddedConfig & cfg); // binds cfg.bindAddress:port; false if the port is taken
	// Stops accepting, closes the listening port, wakes and waits for open connections (a PHP script already
	// running is waited for up to 10 s). Safe to call twice, from any thread, and from the destructor.
	void stop();
	bool isRunning() const { return running; }
	HttpResponse handle(const HttpRequest & req, const std::string & remoteAddr) const;

private:
	void acceptLoop();
	void serveClient(SocketHandle fd, const std::string & remote);

	std::string name;
	std::function<void(const std::string &)> log;
	EmbeddedConfig cfg;
	SocketHandle listenFd = -1;
	std::atomic<bool> running { false };
	std::thread acceptor;
	std::atomic<int> active { 0 };
	std::mutex stopMutex;     // one stop() at a time; a second caller waits until the port is closed
	std::mutex clientsMutex;
	std::set<SocketHandle> clients;    // open connections, so stop() can wake any that are mid-read
};

} // namespace ofxphp
