// SPDX-License-Identifier: MIT

#include "DockerClient.h"

#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

DockerClient::DockerClient(const char* socketPath)
	:
	fSocketPath(socketPath)
{
}

status_t DockerClient::Connect(int* outFd)
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
	*outFd = fd;
	return B_OK;
}

status_t DockerClient::Request(const char* method, const char* path,
	BString* outBody, int32* outCode)
{
	int fd;
	status_t status = Connect(&fd);
	if (status != B_OK)
		return status;

	BString request;
	request.SetToFormat(
		"%s %s HTTP/1.1\r\n"
		"Host: docker\r\n"
		"User-Agent: docker-desktop-airos/0.1\r\n"
		"Accept: application/json\r\n"
		"Content-Length: 0\r\n"
		"Connection: close\r\n"
		"\r\n", method, path);

	ssize_t sent = send(fd, request.String(), request.Length(), 0);
	if (sent < 0) {
		int err = errno;
		close(fd);
		return err;
	}

	BString raw;
	char buffer[16384];
	ssize_t n;
	while ((n = recv(fd, buffer, sizeof(buffer), 0)) > 0)
		raw.Append(buffer, n);
	close(fd);

	if (raw.Length() == 0)
		return B_ERROR;

	// Split headers and body.
	int32 headerEnd = raw.FindFirst("\r\n\r\n");
	if (headerEnd < 0)
		return B_BAD_DATA;

	// Status line: HTTP/1.1 200 OK
	BString statusLine;
	raw.CopyInto(statusLine, 0, raw.FindFirst("\r\n"));
	int32 firstSpace = statusLine.FindFirst(' ');
	if (firstSpace > 0)
		*outCode = atoi(statusLine.String() + firstSpace + 1);

	BString headers;
	raw.CopyInto(headers, 0, headerEnd);
	bool chunked = headers.FindFirst("Transfer-Encoding: chunked") >= 0
		|| headers.FindFirst("transfer-encoding: chunked") >= 0;

	BString body;
	raw.CopyInto(body, headerEnd + 4, raw.Length() - headerEnd - 4);

	if (chunked) {
		// Minimal de-chunking.
		const char* p = body.String();
		const char* end = p + body.Length();
		while (p < end) {
			char* next;
			long size = strtol(p, &next, 16);
			if (next == p || size <= 0)
				break;
			p = strchr(next, '\n');
			if (p == NULL)
				break;
			p++;
			if (p + size > end)
				size = end - p;
			outBody->Append(p, size);
			p += size + 2; // skip CRLF
		}
	} else
		*outBody = body;

	return B_OK;
}

status_t DockerClient::Ping(BString* outVersion)
{
	BString body;
	int32 code = 0;
	status_t status = Request("GET", "/_ping", &body, &code);
	if (status == B_OK && body == "OK")
		return B_OK;
	if (status == B_OK)
		return B_ERROR;
	return status;
}

status_t DockerClient::ListContainers(bool all, BString* outJson)
{
	int32 code = 0;
	return Request("GET", all ? "/containers/json?all=1" : "/containers/json",
		outJson, &code);
}

status_t DockerClient::ListImages(BString* outJson)
{
	int32 code = 0;
	return Request("GET", "/images/json", outJson, &code);
}

status_t DockerClient::ContainerAction(const char* id, const char* action)
{
	BString path;
	path.SetToFormat("/containers/%s/%s", id, action);
	BString body;
	int32 code = 0;
	return Request("POST", path.String(), &body, &code);
}

status_t DockerClient::ImageRemove(const char* id)
{
	BString path;
	path.SetToFormat("/images/%s", id);
	BString body;
	int32 code = 0;
	return Request("DELETE", path.String(), &body, &code);
}
