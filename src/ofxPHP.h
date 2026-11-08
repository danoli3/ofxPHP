// SuperServerMegaManager - Copyright (c) 2026 Daniel Rosser. MIT License.
// In-process HTTP server for one site: static files plus PHP through libphp's Embed SAPI.
// No child process, so it works under the macOS App Sandbox (where `php -S` cannot be spawned).
// libphp here is non-thread-safe (NTS), so PHP requests are serialised by one process-wide mutex.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace ofxphp {

struct EmbeddedConfig {
	std::filesystem::path docroot;
	int port = 0;
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

	bool start(const EmbeddedConfig & cfg); // binds 127.0.0.1:port; false if the port is taken
	void stop();
	bool isRunning() const { return running; }
	HttpResponse handle(const HttpRequest & req, const std::string & remoteAddr) const;

private:
	void acceptLoop();
	void serveClient(int fd);

	std::string name;
	std::function<void(const std::string &)> log;
	EmbeddedConfig cfg;
	int listenFd = -1;
	std::atomic<bool> running { false };
	std::thread acceptor;
	std::atomic<int> active { 0 };
};

} // namespace ofxphp
