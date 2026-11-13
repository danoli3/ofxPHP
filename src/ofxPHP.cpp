// SuperServerMegaManager - Copyright (c) 2026 Daniel Rosser. MIT License.
#include "ofxPHP.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef OFX_PHP
extern "C" {
#include "sapi/embed/php_embed.h"
#include "ext/standard/php_standard.h"
#include "SAPI.h"
#include "php_main.h"
#include "php_variables.h"
}
#endif

namespace fs = std::filesystem;

namespace ofxphp {

// ---- pure helpers (no PHP) ------------------------------------------------------------------------

std::string urlDecode(const std::string & s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
			out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
			i += 2;
		} else if (s[i] == '+') out += '+';
		else out += s[i];
	}
	return out;
}

fs::path resolveInsideDocroot(const fs::path & docroot, const std::string & urlPath) {
	std::string decoded = urlDecode(urlPath);
	if (decoded.find('\0') != std::string::npos) return {};
#ifdef _WIN32
	// "\" is a path separator here and ":" opens NTFS alternate streams ("page.php::$DATA" would read PHP source)
	if (decoded.find_first_of("\\:") != std::string::npos) return {};
#endif
	std::error_code ec;
	fs::path base = fs::weakly_canonical(docroot, ec);
	if (ec) return {};
	std::string rel = decoded;
	while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
#ifdef _WIN32
	fs::path relPath = fs::u8path(rel); // the URL is UTF-8; fs::path(std::string) would read it as the ANSI code page
#else
	fs::path relPath = rel;
#endif
	fs::path full = fs::weakly_canonical(base / relPath, ec); // resolves "..", "." and symlinks
	if (ec) return {};
	// full must be base itself or live under base/
	auto b = base.begin(), f = full.begin();
	for (; b != base.end(); ++b, ++f)
		if (f == full.end() || *b != *f) return {};
	return full;
}

std::string mimeType(const fs::path & file) {
	static const std::map<std::string, std::string> types = {
		{ ".html", "text/html; charset=utf-8" }, { ".htm", "text/html; charset=utf-8" }, { ".css", "text/css" },
		{ ".js", "application/javascript" }, { ".mjs", "application/javascript" }, { ".json", "application/json" },
		{ ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".gif", "image/gif" },
		{ ".svg", "image/svg+xml" }, { ".webp", "image/webp" }, { ".ico", "image/x-icon" }, { ".txt", "text/plain; charset=utf-8" },
		{ ".xml", "application/xml" }, { ".pdf", "application/pdf" }, { ".woff", "font/woff" }, { ".woff2", "font/woff2" },
		{ ".ttf", "font/ttf" }, { ".mp4", "video/mp4" }, { ".mp3", "audio/mpeg" }, { ".wasm", "application/wasm" },
	};
	std::string ext = file.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
	auto it = types.find(ext);
	return it == types.end() ? "application/octet-stream" : it->second;
}

std::string statusText(int s) {
	switch (s) {
	case 200: return "OK";
	case 206: return "Partial Content";
	case 301: return "Moved Permanently";
	case 304: return "Not Modified";
	case 400: return "Bad Request";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 413: return "Payload Too Large";
	case 416: return "Range Not Satisfiable";
	case 500: return "Internal Server Error";
	default: return s < 300 ? "OK" : s < 400 ? "Redirect" : "Error";
	}
}

bool parseByteRange(const std::string & h, size_t size, size_t & first, size_t & last) {
	if (h.compare(0, 6, "bytes=") != 0 || h.find(',') != std::string::npos || size == 0) return false;
	std::string spec = h.substr(6);
	size_t dash = spec.find('-');
	if (dash == std::string::npos) return false;
	std::string a = spec.substr(0, dash), b = spec.substr(dash + 1);
	auto digits = [](const std::string & t) { return !t.empty() && t.size() < 19 && std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); }); };
	if (a.empty()) { // suffix: the last n bytes
		if (!digits(b)) return false;
		size_t n = std::stoull(b);
		if (n == 0) return false;
		first = n >= size ? 0 : size - n;
		last = size - 1;
		return true;
	}
	if (!digits(a) || (!b.empty() && !digits(b))) return false;
	first = std::stoull(a);
	last = b.empty() ? size - 1 : std::min<size_t>(std::stoull(b), size - 1);
	return first < size && first <= last;
}

bool parseHttpRequest(const std::string & raw, HttpRequest & out, size_t & headerEnd) {
	headerEnd = raw.find("\r\n\r\n");
	if (headerEnd == std::string::npos) return false;
	std::istringstream in(raw.substr(0, headerEnd));
	std::string line;
	if (!std::getline(in, line)) return false;
	if (!line.empty() && line.back() == '\r') line.pop_back();
	std::istringstream first(line);
	if (!(first >> out.method >> out.target >> out.version)) return false;
	size_t q = out.target.find('?');
	out.path = out.target.substr(0, q);
	out.query = q == std::string::npos ? "" : out.target.substr(q + 1);
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		size_t colon = line.find(':');
		if (colon == std::string::npos) continue;
		std::string k = line.substr(0, colon), v = line.substr(colon + 1);
		std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::tolower(c); });
		v.erase(0, v.find_first_not_of(" \t"));
		out.headers[k] = v;
	}
	headerEnd += 4;
	return true;
}

// ---- PHP (Embed SAPI) -----------------------------------------------------------------------------

#ifdef OFX_PHP
namespace {

std::mutex phpMutex; // NTS libphp: one request at a time, process-wide
bool phpUp = false;

struct Ctx {
	const EmbeddedConfig * cfg;
	const HttpRequest * req;
	const std::string * remote;
	fs::path script;
	size_t bodyPos = 0;
	std::string cookie;
	HttpResponse * res;
};

size_t ubWrite(const char * s, size_t n) {
	auto * c = static_cast<Ctx *>(SG(server_context));
	if (c) c->res->body.append(s, n);
	return n;
}
int sendHeaders(sapi_headers_struct * h) {
	auto * c = static_cast<Ctx *>(SG(server_context));
	if (!c) return SAPI_HEADER_SENT_SUCCESSFULLY;
	c->res->status = h->http_response_code ? h->http_response_code : 200;
	zend_llist_position pos;
	for (auto * e = static_cast<sapi_header_struct *>(zend_llist_get_first_ex(&h->headers, &pos)); e;
	     e = static_cast<sapi_header_struct *>(zend_llist_get_next_ex(&h->headers, &pos))) {
		std::string line(e->header, e->header_len);
		size_t colon = line.find(':');
		if (colon == std::string::npos) continue;
		std::string v = line.substr(colon + 1);
		v.erase(0, v.find_first_not_of(" \t"));
		c->res->headers.emplace_back(line.substr(0, colon), v);
	}
	return SAPI_HEADER_SENT_SUCCESSFULLY;
}
size_t readPost(char * buf, size_t len) {
	auto * c = static_cast<Ctx *>(SG(server_context));
	if (!c) return 0;
	size_t n = std::min(len, c->req->body.size() - c->bodyPos);
	std::memcpy(buf, c->req->body.data() + c->bodyPos, n);
	c->bodyPos += n;
	return n;
}
char * readCookies() {
	auto * c = static_cast<Ctx *>(SG(server_context));
	return c && !c->cookie.empty() ? &c->cookie[0] : nullptr;
}
void addVar(zval * arr, const char * k, const std::string & v) { php_register_variable_safe(k, v.c_str(), v.size(), arr); }
void registerServerVars(zval * arr) {
	auto * c = static_cast<Ctx *>(SG(server_context));
	if (!c) return;
	const HttpRequest & r = *c->req;
	std::string docroot = c->cfg->docroot.string();
	std::string rel = "/" + fs::relative(c->script, fs::weakly_canonical(c->cfg->docroot)).generic_string();
	addVar(arr, "SERVER_SOFTWARE", "SuperServerMegaManager/embedded PHP " PHP_VERSION);
	addVar(arr, "SERVER_PROTOCOL", "HTTP/1.1");
	addVar(arr, "SERVER_NAME", "127.0.0.1");
	addVar(arr, "SERVER_PORT", std::to_string(c->cfg->port));
	addVar(arr, "REQUEST_METHOD", r.method);
	addVar(arr, "REQUEST_URI", r.target);
	addVar(arr, "QUERY_STRING", r.query);
	addVar(arr, "DOCUMENT_ROOT", docroot);
	addVar(arr, "SCRIPT_FILENAME", c->script.string());
	addVar(arr, "SCRIPT_NAME", rel);
	addVar(arr, "PHP_SELF", rel);
	addVar(arr, "REMOTE_ADDR", *c->remote);
	for (auto & [k, v] : r.headers) {
		std::string name = "HTTP_";
		for (char ch : k) name += ch == '-' ? '_' : static_cast<char>(std::toupper((unsigned char)ch));
		addVar(arr, name.c_str(), v);
	}
	auto ct = r.headers.find("content-type");
	if (ct != r.headers.end()) addVar(arr, "CONTENT_TYPE", ct->second);
	addVar(arr, "CONTENT_LENGTH", std::to_string(r.body.size()));
	for (auto & [k, v] : c->cfg->env) addVar(arr, k.c_str(), v);
}
void logMessage(const char * msg, int) { (void)msg; }

// Opcache is statically built in and tries to create a lock file in /tmp at
// startup, which the App Sandbox forbids (fatal). An in-process host has no use
// for a shared-memory cache anyway, so switch it off before PHP reads its ini.
void iniDefaults(HashTable * cfg) {
	zval v;
	ZVAL_NEW_STR(&v, zend_string_init("0", 1, 1));
	zend_hash_str_update(cfg, "opcache.enable", sizeof("opcache.enable") - 1, &v);
	ZVAL_NEW_STR(&v, zend_string_init("0", 1, 1));
	zend_hash_str_update(cfg, "opcache.enable_cli", sizeof("opcache.enable_cli") - 1, &v);
}

bool ensurePhp() {
	if (phpUp) return true;
	php_embed_module.ub_write = ubWrite;
	php_embed_module.flush = nullptr;
	php_embed_module.send_headers = sendHeaders;
	php_embed_module.read_post = readPost;
	php_embed_module.read_cookies = readCookies;
	php_embed_module.register_server_variables = registerServerVars;
	php_embed_module.log_message = logMessage;
	php_embed_module.phpinfo_as_text = 0;
	php_embed_module.ini_defaults = iniDefaults;
	static char name[] = "ofxPHP";
	static char * argv[] = { name, nullptr };
	if (php_embed_init(1, argv) == FAILURE) return false;
	php_request_shutdown(nullptr); // embed_init opens a request of its own; we open one per HTTP request
	phpUp = true;
	return true;
}

} // namespace

std::string embeddedPhpVersion() {
	std::lock_guard<std::mutex> lock(phpMutex);
	return ensurePhp() ? "PHP " PHP_VERSION " (embed)" : "";
}

void shutdownEmbeddedPhp() {
	std::lock_guard<std::mutex> lock(phpMutex);
	if (!phpUp) return;
	php_request_startup(); // php_embed_shutdown() closes the request embed_init opened; give it one
	php_embed_shutdown();
	phpUp = false;
}

bool runEmbeddedPhp(const EmbeddedConfig & cfg, const fs::path & script, const HttpRequest & req,
                    const std::string & remoteAddr, HttpResponse & res) {
	std::lock_guard<std::mutex> lock(phpMutex);
	if (!ensurePhp()) return false;
	Ctx ctx { &cfg, &req, &remoteAddr, script };
	ctx.res = &res;
	auto cookie = req.headers.find("cookie");
	if (cookie != req.headers.end()) ctx.cookie = cookie->second;
	auto ct = req.headers.find("content-type");
	std::string scriptStr = script.string(), method = req.method, query = req.query, uri = req.target;
	std::string contentType = ct == req.headers.end() ? "" : ct->second;

	SG(server_context) = &ctx;
	SG(request_info).request_method = method.c_str();
	// PHP efree()s query_string / request_uri / path_translated on shutdown, so they must be PHP-allocated.
	SG(request_info).query_string = estrdup(query.c_str());
	SG(request_info).request_uri = estrdup(uri.c_str());
	SG(request_info).path_translated = estrdup(scriptStr.c_str());
	SG(request_info).content_type = contentType.empty() ? nullptr : estrdup(contentType.c_str());
	SG(request_info).content_length = static_cast<zend_long>(req.body.size());
	SG(sapi_headers).http_response_code = 200;

	std::error_code ec;
	fs::path oldCwd = fs::current_path(ec);
	fs::current_path(script.parent_path(), ec);
	bool ok = php_request_startup() == SUCCESS;
	if (ok) {
		zend_first_try {
			zend_alter_ini_entry_chars(zend_string_init("memory_limit", 12, 0), cfg.memoryLimit.c_str(), cfg.memoryLimit.size(), PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
			std::string t = std::to_string(cfg.maxExecutionTime);
			zend_alter_ini_entry_chars(zend_string_init("max_execution_time", 18, 0), t.c_str(), t.size(), PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
			zend_alter_ini_entry_chars(zend_string_init("display_errors", 14, 0), cfg.displayErrors ? "1" : "0", 1, PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
			zend_alter_ini_entry_chars(zend_string_init("date.timezone", 13, 0), cfg.timezone.c_str(), cfg.timezone.size(), PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
			zend_file_handle fh;
			zend_stream_init_filename(&fh, scriptStr.c_str());
			php_execute_script(&fh);
			zend_destroy_file_handle(&fh);
		} zend_end_try();
		php_request_shutdown(nullptr);
	}
	SG(server_context) = nullptr;
	SG(request_info).request_method = nullptr;
	SG(request_info).query_string = nullptr;
	SG(request_info).request_uri = nullptr;
	SG(request_info).path_translated = nullptr;
	SG(request_info).content_type = nullptr;
	fs::current_path(oldCwd, ec);
	return ok;
}
#else
std::string embeddedPhpVersion() { return ""; }
void shutdownEmbeddedPhp() {}
bool runEmbeddedPhp(const EmbeddedConfig &, const fs::path &, const HttpRequest &, const std::string &, HttpResponse & res) {
	res.status = 500;
	res.body = "embedded PHP not compiled in";
	return false;
}
#endif

// ---- the server -----------------------------------------------------------------------------------

static HttpResponse simple(int status, const std::string & text) {
	HttpResponse r;
	r.status = status;
	r.headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
	r.body = text + "\n";
	return r;
}

static HttpResponse serveFile(const fs::path & file, const HttpRequest & req);
static bool serveMount(const EmbeddedConfig & cfg, const HttpRequest & req, HttpResponse & res);

HttpResponse EmbeddedServer::handle(const HttpRequest & req, const std::string & remoteAddr) const {
	if (cfg.readOnly && req.method != "GET" && req.method != "HEAD") {
		HttpResponse r = simple(405, "Method Not Allowed");
		r.headers.emplace_back("Allow", "GET, HEAD");
		return r;
	}
	if (cfg.denyDotFiles) {
		std::string decoded = urlDecode(req.path);
		if (decoded.find("/.") != std::string::npos) return simple(403, "Forbidden");
	}
	if (req.method != "GET" && req.method != "POST" && req.method != "HEAD" && req.method != "PUT" && req.method != "DELETE" && req.method != "OPTIONS")
		return simple(405, "Method Not Allowed");
	{
		HttpResponse mounted;
		if (serveMount(cfg, req, mounted)) return mounted;
	}
	fs::path file = resolveInsideDocroot(cfg.docroot, req.path);
	if (file.empty()) return simple(403, "Forbidden");
	std::error_code ec;
	if (fs::is_directory(file, ec)) {
		fs::path found;
		for (const char * idx : { "index.php", "index.html", "index.htm" })
			if (fs::is_regular_file(file / idx, ec)) { found = file / idx; break; }
		if (found.empty()) file.clear();
		else file = found;
	}
	if (file.empty() || !fs::is_regular_file(file, ec)) {
		// not a file: hand to the router script if the site has one
		if (!cfg.router.empty()) {
			fs::path router = resolveInsideDocroot(cfg.docroot, cfg.router);
			if (!router.empty() && fs::is_regular_file(router, ec)) file = router;
		}
		if (file.empty() || !fs::is_regular_file(file, ec)) return simple(404, "Not Found");
	}
	if (file.extension() == ".php") {
		HttpResponse res;
		if (!runEmbeddedPhp(cfg, file, req, remoteAddr, res) && res.body.empty()) return simple(500, "PHP failed to start");
		return res;
	}
	return serveFile(file, req);
}

// A static file, with byte ranges (Safari on iPad will not play audio without them).
static HttpResponse serveFile(const fs::path & file, const HttpRequest & req) {
	std::ifstream in(file, std::ios::binary);
	HttpResponse res;
	res.headers.emplace_back("Content-Type", mimeType(file));
	res.headers.emplace_back("Accept-Ranges", "bytes");
	std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	auto range = req.headers.find("range");
	if (range != req.headers.end() && req.method != "HEAD") {
		size_t first = 0, last = 0;
		if (!parseByteRange(range->second, all.size(), first, last)) {
			res.status = 416;
			res.headers.emplace_back("Content-Range", "bytes */" + std::to_string(all.size()));
			return res;
		}
		res.status = 206;
		res.headers.emplace_back("Content-Range", "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(all.size()));
		res.body = all.substr(first, last - first + 1);
		return res;
	}
	res.body = std::move(all);
	return res;
}

// A mounted static folder: a response when the path belongs to a mount, otherwise false.
static bool serveMount(const EmbeddedConfig & cfg, const HttpRequest & req, HttpResponse & res) {
	std::string path = urlDecode(req.path);
	for (auto & [prefix, folder] : cfg.mounts) {
		if (path != prefix && path.compare(0, prefix.size() + 1, prefix + "/") != 0) continue;
		if (path == prefix) { // "/ref13" -> "/ref13/" so relative links resolve
			res = simple(301, "Moved Permanently");
			res.headers.emplace_back("Location", prefix + "/" + (req.query.empty() ? "" : "?" + req.query));
			return true;
		}
		fs::path file = resolveInsideDocroot(folder, path.substr(prefix.size()));
		std::error_code ec;
		if (file.empty()) { res = simple(403, "Forbidden"); return true; }
		if (fs::is_directory(file, ec)) file /= "index.html";
		if (file.extension() == ".php" || !fs::is_regular_file(file, ec)) { res = simple(404, "Not Found"); return true; }
		res = serveFile(file, req);
		return true;
	}
	return false;
}

// Sockets: one small layer so the server below reads the same on POSIX and Windows (Winsock).
namespace sock {
using Handle = SocketHandle;
constexpr Handle invalid = -1;
#ifdef _WIN32
inline bool init() {
	static const bool ok = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
	return ok;
}
inline Handle make() { SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0); return s == INVALID_SOCKET ? invalid : static_cast<Handle>(s); }
inline void closeIt(Handle h) { ::closesocket(static_cast<SOCKET>(h)); }
inline void wake(Handle h) { ::shutdown(static_cast<SOCKET>(h), SD_BOTH); }
inline bool readable(Handle h, int ms) { WSAPOLLFD p { static_cast<SOCKET>(h), POLLRDNORM, 0 }; return ::WSAPoll(&p, 1, ms) > 0; }
inline long long recvSome(Handle h, char * b, size_t n) { return ::recv(static_cast<SOCKET>(h), b, static_cast<int>(n), 0); }
inline long long sendSome(Handle h, const char * b, size_t n) { return ::send(static_cast<SOCKET>(h), b, static_cast<int>(n), 0); }
inline void recvTimeout(Handle h, int seconds) { DWORD ms = static_cast<DWORD>(seconds) * 1000; setsockopt(static_cast<SOCKET>(h), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&ms), sizeof(ms)); }
// SO_REUSEADDR on Windows lets another program take over a port that is in use; exclusive use is the safe equivalent
inline void reuse(Handle h) { int on = 1; setsockopt(static_cast<SOCKET>(h), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&on), sizeof(on)); }
inline void noSigpipe(Handle) {}
inline bool bindListen(Handle h, const sockaddr_in & a) { return ::bind(static_cast<SOCKET>(h), reinterpret_cast<const sockaddr *>(&a), sizeof(a)) == 0 && ::listen(static_cast<SOCKET>(h), 32) == 0; }
inline Handle acceptOne(Handle h, sockaddr_in & peer) {
	int len = sizeof(peer);
	SOCKET s = ::accept(static_cast<SOCKET>(h), reinterpret_cast<sockaddr *>(&peer), &len);
	return s == INVALID_SOCKET ? invalid : static_cast<Handle>(s);
}
#else
inline bool init() { return true; }
inline Handle make() { return ::socket(AF_INET, SOCK_STREAM, 0); }
inline void closeIt(Handle h) { ::close(static_cast<int>(h)); }
inline void wake(Handle h) { ::shutdown(static_cast<int>(h), SHUT_RDWR); }
inline bool readable(Handle h, int ms) { pollfd p { static_cast<int>(h), POLLIN, 0 }; return ::poll(&p, 1, ms) > 0; }
inline long long recvSome(Handle h, char * b, size_t n) { return ::recv(static_cast<int>(h), b, n, 0); }
inline long long sendSome(Handle h, const char * b, size_t n) {
#ifdef MSG_NOSIGNAL
	return ::send(static_cast<int>(h), b, n, MSG_NOSIGNAL);   // a client that went away must not raise SIGPIPE
#else
	return ::send(static_cast<int>(h), b, n, 0);              // SO_NOSIGPIPE is set on the socket instead
#endif
}
inline void recvTimeout(Handle h, int seconds) { timeval tv { seconds, 0 }; setsockopt(static_cast<int>(h), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); }
inline void reuse(Handle h) { int on = 1; setsockopt(static_cast<int>(h), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)); }
inline void noSigpipe(Handle h) {
#ifdef SO_NOSIGPIPE
	int on = 1;
	setsockopt(static_cast<int>(h), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
	(void)h;
#endif
}
inline bool bindListen(Handle h, const sockaddr_in & a) { return ::bind(static_cast<int>(h), reinterpret_cast<const sockaddr *>(&a), sizeof(a)) == 0 && ::listen(static_cast<int>(h), 32) == 0; }
inline Handle acceptOne(Handle h, sockaddr_in & peer) {
	socklen_t len = sizeof(peer);
	return ::accept(static_cast<int>(h), reinterpret_cast<sockaddr *>(&peer), &len);
}
#endif
} // namespace sock

bool EmbeddedServer::start(const EmbeddedConfig & c) {
	stop();
	cfg = c;
	if (!sock::init()) return false;
	listenFd = sock::make();
	if (listenFd == sock::invalid) return false;
	sock::reuse(listenFd);
	sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<uint16_t>(cfg.port));
	if (inet_pton(AF_INET, cfg.bindAddress.c_str(), &addr.sin_addr) != 1 || !sock::bindListen(listenFd, addr)) { // IPv4 literal only
		sock::closeIt(listenFd);
		listenFd = sock::invalid;
		return false;
	}
	running = true;
	acceptor = std::thread([this] { acceptLoop(); });
	return true;
}

void EmbeddedServer::stop() {
	std::lock_guard<std::mutex> once(stopMutex);
	if (!running.exchange(false)) return;
	// the accept loop polls with a short timeout and sees running == false; shutdown() on a listening socket
	// does not reliably wake accept() on every platform, so don't depend on it
	if (acceptor.joinable()) acceptor.join();
	if (listenFd != sock::invalid) { sock::closeIt(listenFd); listenFd = sock::invalid; } // the port is free from here on
	{
		// wake connections blocked in recv(); the socket is still open while it is in the set
		std::lock_guard<std::mutex> lock(clientsMutex);
		for (SocketHandle fd : clients) sock::wake(fd);
	}
	for (int i = 0; i < 500 && active > 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(20)); // in-flight requests finish (10 s at most)
}

void EmbeddedServer::acceptLoop() {
	while (running) {
		if (!sock::readable(listenFd, 100)) continue;
		sockaddr_in peer {};
		SocketHandle fd = sock::acceptOne(listenFd, peer);
		if (fd == sock::invalid) continue;
		if (!running) { sock::closeIt(fd); break; }
		sock::noSigpipe(fd); // a browser that aborts a transfer must not kill the app
		{
			std::lock_guard<std::mutex> lock(clientsMutex);
			clients.insert(fd);
		}
		active++;
		char ip[INET_ADDRSTRLEN] = "?";
		inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
		std::string remote = ip;
		std::thread([this, fd, remote] {
			serveClient(fd, remote);
			{
				std::lock_guard<std::mutex> lock(clientsMutex);
				clients.erase(fd);
			}
			sock::closeIt(fd);
			active--;
		}).detach();
	}
}

// Writes everything, or stops at the first error (a client that went away).
static void sendAll(SocketHandle fd, const std::string & data) {
	size_t sent = 0;
	while (sent < data.size()) {
		long long n = sock::sendSome(fd, data.data() + sent, data.size() - sent);
		if (n <= 0) break;
		sent += static_cast<size_t>(n);
	}
}

void EmbeddedServer::serveClient(SocketHandle fd, const std::string & remote) {
	sock::recvTimeout(fd, 10);
	std::string raw;
	char buf[16384];
	HttpRequest req;
	size_t headerEnd = 0;
	bool parsed = false;
	const size_t maxBody = 256u * 1024 * 1024;
	while (!parsed) {
		long long n = sock::recvSome(fd, buf, sizeof(buf));
		if (n <= 0 || raw.size() > 1 << 20) return;
		raw.append(buf, static_cast<size_t>(n));
		parsed = parseHttpRequest(raw, req, headerEnd);
	}
	size_t want = 0;
	auto cl = req.headers.find("content-length");
	if (cl != req.headers.end()) want = static_cast<size_t>(std::strtoull(cl->second.c_str(), nullptr, 10));
	if (want > maxBody) {
		HttpResponse r = simple(413, "Payload Too Large");
		std::string out = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\nConnection: close\r\n\r\n" + r.body;
		sendAll(fd, out);
		return;
	}
	req.body = raw.substr(headerEnd);
	while (req.body.size() < want) {
		long long n = sock::recvSome(fd, buf, sizeof(buf));
		if (n <= 0) break;
		req.body.append(buf, static_cast<size_t>(n));
	}
	if (req.body.size() > want) req.body.resize(want);

	HttpResponse res = handle(req, remote);
	bool hasLen = false;
	std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + statusText(res.status) + "\r\n";
	for (auto & [k, v] : res.headers) {
		std::string lk = k;
		std::transform(lk.begin(), lk.end(), lk.begin(), [](unsigned char c) { return std::tolower(c); });
		if (lk == "content-length" || lk == "connection" || lk == "transfer-encoding") { hasLen |= lk == "content-length"; continue; }
		out += k + ": " + v + "\r\n";
	}
	(void)hasLen;
	for (auto & [k, v] : cfg.extraHeaders) out += k + ": " + v + "\r\n";
	out += "Content-Length: " + std::to_string(req.method == "HEAD" ? res.body.size() : res.body.size()) + "\r\nConnection: close\r\n\r\n";
	if (req.method != "HEAD") out += res.body;
	sendAll(fd, out);
	if (cfg.onRequest) cfg.onRequest({ remote, req.method, req.path, res.status, req.method == "HEAD" ? 0 : res.body.size() });
	if (log) log(req.method + " " + req.target + " -> " + std::to_string(res.status));
}

} // namespace ofxphp
