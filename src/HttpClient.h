#ifndef RIW_HTTP_CLIENT_H
#define RIW_HTTP_CLIENT_H

#include <sys/types.h>

#include <functional>
#include <string>
#include <vector>

// HTTP/1.1 GET over a plain socket or OpenSSL. Haiku's own Network Kit has
// no TLS on some builds (the arm64 image, the gcc2 x86 libs), so https is
// spoken here directly; the server certificate is checked against Haiku's CA
// bundle and the host name.
namespace Http {

// Called with each piece of the body; return false to stop the transfer.
typedef std::function<bool(const char* data, size_t size)> SinkFunc;
// done/total in bytes; total is -1 when the server did not say.
typedef std::function<void(off_t done, off_t total)> ProgressFunc;

// Follows redirects. Returns false on a transport error (error says why).
// A completed request with any status returns true; status holds it.
bool Get(const std::string& url, const std::vector<std::string>& headers,
	const SinkFunc& sink, const ProgressFunc& progress, int& status,
	std::string& error);

// The whole body into a string, for small API responses.
bool GetString(const std::string& url,
	const std::vector<std::string>& headers, std::string& body, int& status,
	std::string& error);

// The body into a file; false unless the final status is 2xx.
bool GetFile(const std::string& url, const std::string& path,
	const ProgressFunc& progress, std::string& error);

} // namespace Http

#endif
