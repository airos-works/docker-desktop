// SPDX-License-Identifier: MIT
// Docker Engine API client: HTTP/1.1 over a unix domain socket.

#ifndef DOCKER_CLIENT_H
#define DOCKER_CLIENT_H

#include <Errors.h>
#include <String.h>
#include <SupportDefs.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

// Lets another thread end a streaming request (logs, events, pull
// progress) that is blocked waiting for the Engine.
class StreamCanceler {
public:
	StreamCanceler();

	void Cancel();
	bool IsCancelled() const { return fCancelled; }

	// For DockerClient: the socket of the request in progress (-1 before
	// closing it, so that Cancel never shuts down a reused descriptor).
	void SetSocket(int fd);

private:
	std::mutex fLock;
	int fSocket;
	std::atomic<bool> fCancelled;
};

typedef std::shared_ptr<StreamCanceler> CancelerRef;

// Receives the decoded response body of a streaming request as it arrives;
// returns false to end the request.
typedef std::function<bool(const char* data, size_t length)> StreamHandler;

class DockerClient {
public:
	DockerClient(const char* socketPath);

	status_t Ping(BString* outError = NULL);

	// One request; outBody is the whole response body. B_OK only for a
	// 2xx answer: otherwise outError holds the Engine's message.
	status_t Call(const char* method, const char* path,
		const BString* body = NULL, BString* outBody = NULL,
		BString* outError = NULL);
	status_t Get(const char* path, BString* outBody,
		BString* outError = NULL)
		{ return Call("GET", path, NULL, outBody, outError); }

	// A request whose response is read as it comes; handler gets the body.
	status_t Stream(const char* method, const char* path,
		const BString* body, const CancelerRef& canceler,
		const StreamHandler& handler, BString* outError = NULL,
		const char* extraHeaders = NULL);

	// Convenience wrappers around Call.
	status_t ListContainers(bool all, BString* outJson);
	status_t ListImages(BString* outJson);
	status_t ContainerAction(const char* id, const char* action,
		BString* outError = NULL);
	status_t ImageRemove(const char* id, bool force,
		BString* outError = NULL);

	const BString& SocketPath() const { return fSocketPath; }

	// Percent-encodes a query parameter value.
	static BString Escape(const char* text);
	// The "message" of an Engine error body, or a fallback text.
	static BString ErrorMessage(const BString& body, int32 code);

private:
	status_t _Connect(int* outFd, bigtime_t timeout);
	status_t _SendRequest(int fd, const char* method, const char* path,
		const BString* body, const char* extraHeaders);

	BString fSocketPath;
};

// Splits a byte stream into lines (events and pull progress are one JSON
// object per line).
class LineSplitter {
public:
	// Calls handler for each complete line; returns false if it did.
	bool Feed(const char* data, size_t length,
		const std::function<bool(const BString& line)>& handler);

private:
	std::string fPending;
};

// Undoes the Engine's multiplexed log/attach framing (an 8-byte header
// before every frame: stream type, 3 zero bytes, big-endian size), for
// containers without a TTY. Data without the framing passes through.
class LogDemuxer {
public:
	LogDemuxer();

	// Calls handler(stream, text, length) for each frame's payload, where
	// stream is 1 for stdout and 2 for stderr.
	void Feed(const char* data, size_t length,
		const std::function<void(int stream, const char* text,
			size_t length)>& handler);

private:
	std::string fBuffer;
	int fMode; // -1 unknown, 0 raw, 1 framed
};

#endif
