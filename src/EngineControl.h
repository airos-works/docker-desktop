// SPDX-License-Identifier: MIT
// Starting the Docker Engine (dockerd) now, and at boot.
//
// At boot, launch_daemon starts it: the docker_engine package's user session
// job (data/user_launch/docker_engine) runs dockerd when the flattened
// BMessage in ~/config/settings/docker_engine has start_at_boot set to true.

#ifndef DOCKER_ENGINE_CONTROL_H
#define DOCKER_ENGINE_CONTROL_H

#include <String.h>
#include <SupportDefs.h>

#include <sys/types.h>

namespace EngineControl {

// Whether launch_daemon starts the Engine at boot.
bool StartsAtBoot();
status_t SetStartsAtBoot(bool start);

// Starts dockerd in a session of its own, so it outlives Docker Desktop;
// its output goes to LogPath().
status_t Start(pid_t* outPid, BString* outError);
BString LogPath();

} // namespace EngineControl

#endif
