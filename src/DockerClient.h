// SPDX-License-Identifier: MIT
// Tiny Docker Engine API client: HTTP/1.1 over a unix domain socket.

#ifndef DOCKER_CLIENT_H
#define DOCKER_CLIENT_H

#include <Errors.h>
#include <String.h>
#include <SupportDefs.h>

class BString;

class DockerClient {
public:
	DockerClient(const char* socketPath);

	status_t Ping(BString* outVersion);
	// Each of the List methods fills raw JSON (the caller parses it).
	status_t ListContainers(bool all, BString* outJson);
	status_t ListImages(BString* outJson);
	status_t ContainerAction(const char* id, const char* action);
	status_t ImageRemove(const char* id);

	const BString& SocketPath() const { return fSocketPath; }

private:
	status_t Request(const char* method, const char* path, BString* outBody,
		int32* outCode);
	status_t Connect(int* outFd);

	BString fSocketPath;
};

#endif
