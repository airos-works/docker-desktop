// SPDX-License-Identifier: MIT

#include "DockerClient.h"

#include "Json.h"

#include <cerrno>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// How long a plain request may wait for the Engine to answer (stopping a
// container waits up to its stop timeout).
const bigtime_t kCallTimeout = 120000000;
const bigtime_t kPingTimeout = 3000000;

void SetReceiveTimeout(int fd, bigtime_t timeout)
{
	struct timeval tv;
	tv.tv_sec = timeout / 1000000;
	tv.tv_usec = timeout % 1000000;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// Incremental decoder of the chunked transfer encoding.
class ChunkDecoder {
public:
	ChunkDecoder() : fRemaining(0), fState(kSize), fDone(false) {}

	bool Done() const { return fDone; }

	// Appends the payload in data to out.
	void Feed(const char* data, size_t length, std::string& out)
	{
		size_t i = 0;
		while (i < length && !fDone) {
			switch (fState) {
			case kSize:
			{
				char c = data[i++];
				if (c == '\n') {
					fRemaining = strtol(fLine.c_str(), NULL, 16);
					fLine.clear();
					if (fRemaining == 0)
						fDone = true;
					else
						fState = kData;
				} else if (c != '\r')
					fLine += c;
				break;
			}
			case kData:
			{
				size_t take = length - i;
				if ((long)take > fRemaining)
					take = fRemaining;
				out.append(data + i, take);
				i += take;
				fRemaining -= take;
				if (fRemaining == 0)
					fState = kDataEnd;
				break;
			}
			case kDataEnd:
				// The CRLF after each chunk's data.
				if (data[i++] == '\n')
					fState = kSize;
				break;
			}
		}
	}

private:
	enum { kSize, kData, kDataEnd };

	std::string fLine;
	long fRemaining;
	int fState;
	bool fDone;
};

struct ResponseHead {
	int32 code;
	bool chunked;
	int64 contentLength; // -1: until the connection closes
};

// Reads from fd until the end of the headers; whatever body bytes came
// along are left in rest. (Bodies can hold NUL bytes, which BString stops
// at: they go in a std::string.)
status_t ReadHead(int fd, ResponseHead* head, std::string* rest)
{
	std::string raw;
	char buffer[4096];
	size_t headerEnd;
	while ((headerEnd = raw.find("\r\n\r\n")) == std::string::npos) {
		ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
		if (n < 0)
			return errno == EAGAIN || errno == EWOULDBLOCK ? B_TIMED_OUT
				: errno;
		if (n == 0)
			return raw.empty() ? B_ERROR : B_BAD_DATA;
		raw.append(buffer, n);
		if (raw.size() > 65536)
			return B_BAD_DATA;
	}

	BString headers(raw.c_str(), headerEnd);
	*rest = raw.substr(headerEnd + 4);

	head->code = 0;
	int32 firstSpace = headers.FindFirst(' ');
	if (firstSpace > 0)
		head->code = atoi(headers.String() + firstSpace + 1);

	head->chunked = false;
	head->contentLength = -1;
	BString lower(headers);
	lower.ToLower();
	if (lower.FindFirst("transfer-encoding: chunked") >= 0)
		head->chunked = true;
	int32 length = lower.FindFirst("content-length:");
	if (length >= 0)
		head->contentLength = atoll(lower.String() + length + 15);
	return B_OK;
}

} // namespace


// #pragma mark - StreamCanceler


StreamCanceler::StreamCanceler()
	:
	fSocket(-1),
	fCancelled(false)
{
}


void StreamCanceler::Cancel()
{
	std::lock_guard<std::mutex> lock(fLock);
	fCancelled = true;
	if (fSocket >= 0)
		shutdown(fSocket, SHUT_RDWR);
}


void StreamCanceler::SetSocket(int fd)
{
	std::lock_guard<std::mutex> lock(fLock);
	fSocket = fd;
	if (fd >= 0 && fCancelled)
		shutdown(fd, SHUT_RDWR);
}


// #pragma mark - DockerClient


DockerClient::DockerClient(const char* socketPath)
	:
	fSocketPath(socketPath)
{
}


status_t DockerClient::_Connect(int* outFd, bigtime_t timeout)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return errno;

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", fSocketPath.String());

	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		int err = errno;
		close(fd);
		return err;
	}
	if (timeout > 0)
		SetReceiveTimeout(fd, timeout);
	*outFd = fd;
	return B_OK;
}


status_t DockerClient::_SendRequest(int fd, const char* method,
	const char* path, const BString* body, const char* extraHeaders)
{
	BString request;
	request.SetToFormat(
		"%s %s HTTP/1.1\r\n"
		"Host: docker\r\n"
		"User-Agent: docker-desktop-airos/0.2\r\n"
		"Accept: application/json\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %" B_PRId32 "\r\n"
		"Connection: close\r\n"
		"%s"
		"\r\n", method, path, body != NULL ? body->Length() : 0,
		extraHeaders != NULL ? extraHeaders : "");
	if (body != NULL)
		request << *body;

	const char* data = request.String();
	ssize_t left = request.Length();
	while (left > 0) {
		// MSG_NOSIGNAL: a socket shut down by a StreamCanceler (or a
		// dockerd going away) must not raise SIGPIPE, which would end the
		// team (Deskbar, for the Deskbar item).
		ssize_t sent = send(fd, data, left, MSG_NOSIGNAL);
		if (sent < 0) {
			if (errno == EINTR)
				continue;
			return errno;
		}
		data += sent;
		left -= sent;
	}
	return B_OK;
}


status_t DockerClient::Call(const char* method, const char* path,
	const BString* body, BString* outBody, BString* outError)
{
	BString response;
	BString error;
	CancelerRef canceler(new StreamCanceler);
	status_t status = Stream(method, path, body, canceler,
		[&response](const char* data, size_t length) {
			response.Append(data, length);
			return true;
		}, &error);

	if (outBody != NULL)
		*outBody = response;
	if (status != B_OK && outError != NULL)
		*outError = error;
	return status;
}


status_t DockerClient::Stream(const char* method, const char* path,
	const BString* body, const CancelerRef& canceler,
	const StreamHandler& handler, BString* outError,
	const char* extraHeaders)
{
	BString ignored;
	if (outError == NULL)
		outError = &ignored;

	int fd;
	// Plain calls time out; long-lived streams wait for as long as it takes.
	bool streaming = strstr(path, "follow=1") != NULL
		|| strstr(path, "stream=1") != NULL
		|| strncmp(path, "/events", 7) == 0
		|| strncmp(path, "/images/create", 14) == 0
		|| strncmp(path, "/build", 6) == 0;
	status_t status = _Connect(&fd, streaming ? 0 : kCallTimeout);
	if (status != B_OK) {
		outError->SetToFormat("Cannot connect to the Docker Engine at %s: %s",
			fSocketPath.String(), strerror(status));
		return status;
	}
	canceler->SetSocket(fd);
	if (canceler->IsCancelled()) {
		canceler->SetSocket(-1);
		close(fd);
		return B_CANCELED;
	}

	status = _SendRequest(fd, method, path, body, extraHeaders);
	if (status != B_OK) {
		canceler->SetSocket(-1);
		close(fd);
		*outError = strerror(status);
		return status;
	}

	ResponseHead head;
	std::string rest;
	status = ReadHead(fd, &head, &rest);
	if (status != B_OK) {
		canceler->SetSocket(-1);
		close(fd);
		if (canceler->IsCancelled())
			return B_CANCELED;
		*outError = status == B_TIMED_OUT
			? "The Docker Engine did not answer in time"
			: "No response from the Docker Engine";
		return status;
	}

	// 304: the object already was in the state asked for.
	bool success = (head.code >= 200 && head.code < 300) || head.code == 304;
	BString errorBody;
	ChunkDecoder decoder;
	int64 received = 0;
	bool keepGoing = true;

	auto deliver = [&](const char* data, size_t length) {
		std::string decoded;
		const char* payload = data;
		size_t payloadLength = length;
		if (head.chunked) {
			decoder.Feed(data, length, decoded);
			payload = decoded.data();
			payloadLength = decoded.size();
		}
		if (payloadLength == 0)
			return;
		if (success)
			keepGoing = handler(payload, payloadLength);
		else
			errorBody.Append(payload, payloadLength);
	};

	if (!rest.empty()) {
		received += rest.size();
		deliver(rest.data(), rest.size());
	}

	char buffer[16384];
	while (keepGoing && !(head.chunked && decoder.Done())
		&& !(head.contentLength >= 0 && received >= head.contentLength)) {
		ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		received += n;
		deliver(buffer, n);
	}

	canceler->SetSocket(-1);
	close(fd);

	if (canceler->IsCancelled())
		return B_CANCELED;
	if (!success) {
		*outError = ErrorMessage(errorBody, head.code);
		return B_ERROR;
	}
	// A body cut short (a timeout, or dockerd going away) is no answer: a
	// partial list would make rows disappear.
	bool complete = !keepGoing
		|| (head.chunked ? decoder.Done()
			: head.contentLength < 0 || received >= head.contentLength);
	if (!complete) {
		*outError = "The Docker Engine's answer was cut short";
		return B_BAD_DATA;
	}
	return B_OK;
}


status_t DockerClient::Ping(BString* outError)
{
	int fd;
	status_t status = _Connect(&fd, kPingTimeout);
	if (status != B_OK) {
		if (outError != NULL)
			*outError = strerror(status);
		return status;
	}
	status = _SendRequest(fd, "GET", "/_ping", NULL, NULL);
	ResponseHead head;
	std::string rest;
	if (status == B_OK)
		status = ReadHead(fd, &head, &rest);
	close(fd);
	if (status == B_OK && head.code != 200)
		status = B_ERROR;
	if (status != B_OK && outError != NULL)
		*outError = strerror(status);
	return status;
}


status_t DockerClient::ListContainers(bool all, BString* outJson)
{
	return Get(all ? "/containers/json?all=1" : "/containers/json", outJson);
}


status_t DockerClient::ListImages(BString* outJson)
{
	return Get("/images/json", outJson);
}


status_t DockerClient::ContainerAction(const char* id, const char* action,
	BString* outError)
{
	BString path;
	if (strcmp(action, "remove") == 0) {
		path.SetToFormat("/containers/%s?force=1", id);
		return Call("DELETE", path.String(), NULL, NULL, outError);
	}
	path.SetToFormat("/containers/%s/%s", id, action);
	return Call("POST", path.String(), NULL, NULL, outError);
}


status_t DockerClient::ImageRemove(const char* id, bool force,
	BString* outError)
{
	BString path;
	path.SetToFormat("/images/%s%s", Escape(id).String(),
		force ? "?force=1" : "");
	return Call("DELETE", path.String(), NULL, NULL, outError);
}


BString DockerClient::Escape(const char* text)
{
	static const char* kHex = "0123456789ABCDEF";
	BString result;
	for (const unsigned char* p = (const unsigned char*)text; *p != '\0';
			p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
			|| (*p >= '0' && *p <= '9') || strchr("-_.~", *p) != NULL) {
			result.Append((const char*)p, 1);
		} else {
			char encoded[3] = { '%', kHex[*p >> 4], kHex[*p & 15] };
			result.Append(encoded, 3);
		}
	}
	return result;
}


BString DockerClient::ErrorMessage(const BString& body, int32 code)
{
	BString message;
	djson::Value* root = djson::Parse(body.String());
	if (root != NULL) {
		message = djson::GetString(root, "message");
		djson::FreeValue(root);
	}
	if (message.Length() == 0) {
		message = body;
		message.Trim();
	}
	if (message.Length() == 0) {
		switch (code) {
		case 304: message = "Nothing to do (already in that state)"; break;
		case 404: message = "No such object"; break;
		case 409: message = "Conflict"; break;
		default:
			message.SetToFormat("The Docker Engine answered %" B_PRId32,
				code);
			break;
		}
	}
	return message;
}


// #pragma mark - LineSplitter


bool LineSplitter::Feed(const char* data, size_t length,
	const std::function<bool(const BString& line)>& handler)
{
	fPending.append(data, length);
	size_t newline;
	while ((newline = fPending.find('\n')) != std::string::npos) {
		BString line(fPending.c_str(), newline);
		fPending.erase(0, newline + 1);
		line.Trim();
		if (line.Length() > 0 && !handler(line))
			return false;
	}
	return true;
}


// #pragma mark - LogDemuxer


LogDemuxer::LogDemuxer()
	:
	fMode(-1)
{
}


void LogDemuxer::Feed(const char* data, size_t length,
	const std::function<void(int, const char*, size_t)>& handler)
{
	if (fMode == 0) {
		handler(1, data, length);
		return;
	}
	fBuffer.append(data, length);

	if (fMode < 0) {
		const unsigned char* p = (const unsigned char*)fBuffer.data();
		// A frame header: stream 0-2, then three zero bytes. Anything else
		// in the first byte is raw (TTY) output, however short.
		if (!fBuffer.empty() && p[0] > 2)
			fMode = 0;
		else if (fBuffer.size() < 8)
			return;
		else
			fMode = p[1] == 0 && p[2] == 0 && p[3] == 0 ? 1 : 0;
		if (fMode == 0) {
			handler(1, fBuffer.data(), fBuffer.size());
			fBuffer.clear();
			return;
		}
	}

	size_t offset = 0;
	while (fBuffer.size() - offset >= 8) {
		const unsigned char* p = (const unsigned char*)fBuffer.data()
			+ offset;
		uint32 size = ((uint32)p[4] << 24) | ((uint32)p[5] << 16)
			| ((uint32)p[6] << 8) | p[7];
		if (fBuffer.size() - offset < 8 + size)
			break;
		handler(p[0] == 2 ? 2 : 1, (const char*)p + 8, size);
		offset += 8 + size;
	}
	fBuffer.erase(0, offset);
}
