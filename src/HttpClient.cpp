#include "HttpClient.h"
#include "Strings.h"

#include <NetworkAddress.h>
#include <Socket.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Http {

namespace {

const char* kCABundle = "/boot/system/data/ssl/CARootCertificates.pem";
const bigtime_t kTimeout = 30000000;


struct Url {
	bool tls = false;
	std::string host;
	int port = 0;
	std::string path = "/";
	bool valid = false;
};


Url
ParseUrl(const std::string& url)
{
	Url r;
	std::string rest;
	if (strncmp(url.c_str(), "https://", 8) == 0) {
		r.tls = true;
		r.port = 443;
		rest = url.substr(8);
	} else if (strncmp(url.c_str(), "http://", 7) == 0) {
		r.port = 80;
		rest = url.substr(7);
	} else
		return r;

	size_t slash = rest.find('/');
	std::string authority = slash == std::string::npos
		? rest : rest.substr(0, slash);
	if (slash != std::string::npos)
		r.path = rest.substr(slash);
	size_t colon = authority.find(':');
	if (colon == std::string::npos)
		r.host = authority;
	else {
		r.host = authority.substr(0, colon);
		r.port = atoi(authority.c_str() + colon + 1);
	}
	r.valid = !r.host.empty() && r.port > 0;
	return r;
}


std::string
Resolve(const std::string& base, const std::string& location)
{
	if (strncmp(location.c_str(), "http://", 7) == 0
		|| strncmp(location.c_str(), "https://", 8) == 0)
		return location;
	size_t schemeEnd = base.find("://");
	if (schemeEnd == std::string::npos)
		return location;
	size_t pathStart = base.find('/', schemeEnd + 3);
	std::string origin = pathStart == std::string::npos
		? base : base.substr(0, pathStart);
	if (!location.empty() && location[0] == '/')
		return origin + location;
	std::string dir = pathStart == std::string::npos
		? origin + "/" : base.substr(0, base.rfind('/') + 1);
	return dir + location;
}


std::string
Header(const std::string& headers, const char* nameLower)
{
	size_t nameLen = strlen(nameLower);
	size_t pos = 0;
	while (pos < headers.size()) {
		size_t end = headers.find("\r\n", pos);
		if (end == std::string::npos)
			end = headers.size();
		size_t colon = headers.find(':', pos);
		if (colon != std::string::npos && colon < end
			&& colon - pos == nameLen) {
			bool match = true;
			for (size_t i = 0; i < nameLen && match; i++) {
				match = tolower((unsigned char)headers[pos + i])
					== (unsigned char)nameLower[i];
			}
			if (match) {
				size_t v = colon + 1;
				while (v < end && (headers[v] == ' ' || headers[v] == '\t'))
					v++;
				return headers.substr(v, end - v);
			}
		}
		pos = end + 2;
	}
	return std::string();
}


// One connection: the socket, TLS on top of it if asked for, and whatever has
// been read but not consumed yet.
class Connection {
public:
	~Connection()
	{
		if (fSSL != NULL)
			SSL_free(fSSL);
		if (fContext != NULL)
			SSL_CTX_free(fContext);
	}

	bool Open(const Url& url, std::string& error)
	{
		BNetworkAddress address;
		if (address.SetTo(url.host.c_str(), url.port) != B_OK) {
			error = TF("Cannot find %1 on the network.", url.host);
			return false;
		}
		if (fSocket.Connect(address, kTimeout) != B_OK) {
			error = TF("Cannot connect to %1.", url.host);
			return false;
		}
		fSocket.SetTimeout(kTimeout);
		if (!url.tls)
			return true;

		fContext = SSL_CTX_new(TLS_client_method());
		if (fContext == NULL) {
			error = TF("The secure connection to %1 failed.", url.host);
			return false;
		}
		SSL_CTX_set_verify(fContext, SSL_VERIFY_PEER, NULL);
		if (SSL_CTX_load_verify_locations(fContext, kCABundle, NULL) != 1)
			SSL_CTX_set_default_verify_paths(fContext);
		fSSL = SSL_new(fContext);
		SSL_set_fd(fSSL, fSocket.Socket());
		SSL_set_tlsext_host_name(fSSL, url.host.c_str());
		SSL_set1_host(fSSL, url.host.c_str());
		if (SSL_connect(fSSL) != 1) {
			long verify = SSL_get_verify_result(fSSL);
			if (verify != X509_V_OK) {
				error = TF("The certificate of %1 is not trusted: %2. Is the "
					"system clock right?", url.host,
					X509_verify_cert_error_string(verify));
			} else
				error = TF("The secure connection to %1 failed.", url.host);
			return false;
		}
		return true;
	}

	bool Write(const std::string& data)
	{
		if (fSSL != NULL)
			return SSL_write(fSSL, data.data(), (int)data.size()) > 0;
		return fSocket.Write(data.data(), data.size()) == (ssize_t)data.size();
	}

	// Reads more into the buffer; false at end of stream or on error.
	bool Fill()
	{
		char chunk[16384];
		int n = fSSL != NULL ? SSL_read(fSSL, chunk, sizeof(chunk))
			: (int)fSocket.Read(chunk, sizeof(chunk));
		if (n <= 0)
			return false;
		fBuffer.append(chunk, n);
		return true;
	}

	std::string& Buffer() { return fBuffer; }

private:
	BSocket fSocket;
	SSL_CTX* fContext = NULL;
	SSL* fSSL = NULL;
	std::string fBuffer;
};


// Moves up to `count` bytes (all of them for count < 0) from the connection
// into the sink. False if the stream ended early, the sink refused, or
// (count >= 0) fewer bytes arrived than promised.
bool
Pump(Connection& c, off_t count, const SinkFunc& sink,
	const ProgressFunc& progress, off_t& done, off_t total, bool& aborted)
{
	for (;;) {
		std::string& buffer = c.Buffer();
		if (!buffer.empty()) {
			size_t take = buffer.size();
			if (count >= 0 && (off_t)take > count)
				take = (size_t)count;
			if (!sink(buffer.data(), take)) {
				aborted = true;
				return false;
			}
			buffer.erase(0, take);
			done += take;
			if (count >= 0)
				count -= take;
			if (progress)
				progress(done, total);
		}
		if (count == 0)
			return true;
		if (!c.Fill())
			return count < 0;
	}
}


bool
ReadLine(Connection& c, std::string& line)
{
	size_t end;
	while ((end = c.Buffer().find("\r\n")) == std::string::npos) {
		if (!c.Fill())
			return false;
	}
	line = c.Buffer().substr(0, end);
	c.Buffer().erase(0, end + 2);
	return true;
}

} // namespace


bool
Get(const std::string& startUrl, const std::vector<std::string>& headers,
	const SinkFunc& sink, const ProgressFunc& progress, int& status,
	std::string& error)
{
	std::string current = startUrl;
	for (int redirect = 0; redirect < 8; redirect++) {
		Url url = ParseUrl(current);
		if (!url.valid) {
			error = "not an http(s) URL: " + current;
			return false;
		}
		Connection c;
		if (!c.Open(url, error))
			return false;

		std::string request = "GET " + url.path + " HTTP/1.1\r\n"
			"Host: " + url.host + "\r\n"
			"User-Agent: RInstallWizard/1.0 (Haiku)\r\n"
			"Accept-Encoding: identity\r\n"
			"Connection: close\r\n";
		for (size_t i = 0; i < headers.size(); i++)
			request += headers[i] + "\r\n";
		request += "\r\n";
		if (!c.Write(request)) {
			error = TF("Cannot connect to %1.", url.host);
			return false;
		}

		size_t headerEnd;
		while ((headerEnd = c.Buffer().find("\r\n\r\n")) == std::string::npos) {
			if (c.Buffer().size() > 65536 || !c.Fill()) {
				error = TF("%1 did not answer properly.", url.host);
				return false;
			}
		}
		std::string head = c.Buffer().substr(0, headerEnd);
		c.Buffer().erase(0, headerEnd + 4);
		size_t space = head.find(' ');
		status = space == std::string::npos ? 0 : atoi(head.c_str() + space + 1);

		if (status >= 300 && status < 400 && status != 304) {
			std::string location = Header(head, "location");
			if (location.empty()) {
				error = TF("%1 did not answer properly.", url.host);
				return false;
			}
			current = Resolve(current, location);
			continue;
		}

		// Error pages are not what the caller wants in its file.
		SinkFunc target = sink;
		std::string discard;
		if (status < 200 || status >= 300) {
			target = [&discard](const char* data, size_t size) {
				if (discard.size() < 65536)
					discard.append(data, size);
				return true;
			};
		}

		std::string length = Header(head, "content-length");
		off_t total = length.empty() ? -1 : (off_t)atoll(length.c_str());
		off_t done = 0;
		bool aborted = false;
		bool ok;
		if (Header(head, "transfer-encoding").find("chunked")
				!= std::string::npos) {
			ok = true;
			for (;;) {
				std::string line;
				if (!ReadLine(c, line)) {
					ok = false;
					break;
				}
				off_t size = (off_t)strtoll(line.c_str(), NULL, 16);
				if (size <= 0)
					break;
				if (!Pump(c, size, target, progress, done, total, aborted)
					|| !ReadLine(c, line)) {
					ok = false;
					break;
				}
			}
		} else
			ok = Pump(c, total, target, progress, done, total, aborted);

		if (aborted) {
			error = "cancelled";
			return false;
		}
		if (!ok) {
			error = TF("The connection to %1 broke off.", url.host);
			return false;
		}
		if (status < 200 || status >= 300) {
			// Hand the error body to string callers (API messages live there).
			sink(discard.data(), discard.size());
		}
		return true;
	}
	error = T("Too many redirects.");
	return false;
}


bool
GetString(const std::string& url, const std::vector<std::string>& headers,
	std::string& body, int& status, std::string& error)
{
	body.clear();
	// API answers are small; a server that keeps sending is cut off rather
	// than allowed to fill memory.
	const size_t kMaxBody = 32 * 1024 * 1024;
	return Get(url, headers,
		[&body, kMaxBody](const char* data, size_t size) {
			if (body.size() + size > kMaxBody)
				return false;
			body.append(data, size);
			return true;
		},
		ProgressFunc(), status, error);
}


bool
GetFile(const std::string& url, const std::string& path,
	const ProgressFunc& progress, std::string& error)
{
	FILE* file = fopen(path.c_str(), "wb");
	if (file == NULL) {
		error = TF("Cannot write %1. Is the disk full?", path);
		return false;
	}
	bool writeFailed = false;
	int status = 0;
	bool ok = Get(url, std::vector<std::string>(),
		[file, &writeFailed](const char* data, size_t size) {
			if (fwrite(data, 1, size, file) != size) {
				writeFailed = true;
				return false;
			}
			return true;
		},
		progress, status, error);
	if (fclose(file) != 0)
		writeFailed = true;
	if (ok && (status < 200 || status >= 300)) {
		char s[16];
		snprintf(s, sizeof(s), "%d", status);
		error = TF("The server answered %1.", s);
		ok = false;
	}
	if (writeFailed) {
		error = TF("Cannot write %1. Is the disk full?", path);
		ok = false;
	}
	if (!ok)
		unlink(path.c_str());
	return ok;
}

} // namespace Http
