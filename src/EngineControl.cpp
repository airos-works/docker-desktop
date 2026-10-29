// SPDX-License-Identifier: MIT

#include "EngineControl.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <OS.h>
#include <Path.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vector>

extern char** environ;

namespace {

const char* kSettingsName = "docker_engine";
const char* kPidFile = "/var/run/docker.pid";
const char* kStartAtBootField = "start_at_boot";

status_t SettingsPath(BPath* path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, path, true);
	if (status != B_OK)
		return status;
	return path->Append(kSettingsName);
}

// The installed dockerd: the package's, or a non-packaged build.
status_t DockerdPath(BPath* path)
{
	static const directory_which kDirectories[] = {
		B_SYSTEM_BIN_DIRECTORY,
		B_USER_NONPACKAGED_BIN_DIRECTORY,
		B_SYSTEM_NONPACKAGED_BIN_DIRECTORY,
	};
	for (size_t i = 0; i < sizeof(kDirectories) / sizeof(kDirectories[0]);
			i++) {
		if (find_directory(kDirectories[i], path) != B_OK
			|| path->Append("dockerd") != B_OK)
			continue;
		if (access(path->Path(), X_OK) == 0)
			return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}

} // namespace

namespace EngineControl {

bool StartsAtBoot()
{
	BPath path;
	if (SettingsPath(&path) != B_OK)
		return false;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;
	if (file.InitCheck() != B_OK || settings.Unflatten(&file) != B_OK)
		return false;
	return settings.GetBool(kStartAtBootField, false);
}

status_t SetStartsAtBoot(bool start)
{
	BPath path;
	status_t status = SettingsPath(&path);
	if (status != B_OK)
		return status;

	// Keep whatever else the file holds.
	BMessage settings;
	{
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			settings.Unflatten(&file);
	}
	settings.RemoveName(kStartAtBootField);
	settings.AddBool(kStartAtBootField, start);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return settings.Flatten(&file);
}

BString LogPath()
{
	BPath path;
	if (find_directory(B_SYSTEM_LOG_DIRECTORY, &path, true) != B_OK
		|| path.Append("dockerd.log") != B_OK)
		return "/var/log/dockerd.log";
	return path.Path();
}

status_t Start(pid_t* outPid, BString* outError)
{
	BPath dockerd;
	if (DockerdPath(&dockerd) != B_OK) {
		*outError = "dockerd is not installed (the docker_engine package)";
		return B_ENTRY_NOT_FOUND;
	}

	// The same arguments as the boot job.
	const char* argv[] = { dockerd.Path(), "--userland-proxy=false", NULL };

	// dockerd runs containerd and its shims from the PATH, which can be
	// bare when Docker Desktop was not started from a shell.
	BString path("PATH=");
	const char* inherited = getenv("PATH");
	path << "/boot/home/config/non-packaged/bin:/boot/home/config/bin:"
		"/boot/system/non-packaged/bin:/boot/system/bin";
	if (inherited != NULL && inherited[0] != '\0')
		path << ":" << inherited;
	std::vector<const char*> environment;
	for (char** variable = environ; *variable != NULL; variable++) {
		if (strncmp(*variable, "PATH=", 5) != 0)
			environment.push_back(*variable);
	}
	environment.push_back(path.String());
	environment.push_back(NULL);

	BString log = LogPath();
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
		O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.String(),
		O_WRONLY | O_CREAT | O_APPEND, 0644);
	posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

	posix_spawnattr_t attributes;
	posix_spawnattr_init(&attributes);
	posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);

	int error = posix_spawn(outPid, argv[0], &actions, &attributes,
		(char* const*)argv, (char* const*)environment.data());

	posix_spawnattr_destroy(&attributes);
	posix_spawn_file_actions_destroy(&actions);

	if (error != 0) {
		outError->SetToFormat("Could not start dockerd: %s", strerror(error));
		return B_FROM_POSIX_ERROR(error);
	}
	return B_OK;
}

pid_t RunningPid()
{
	long pid = -1;
	FILE* file = fopen(kPidFile, "r");
	if (file != NULL) {
		if (fscanf(file, "%ld", &pid) != 1)
			pid = -1;
		fclose(file);
	}
	// A stale file outlives a dockerd that was killed.
	if (pid > 0 && (kill((pid_t)pid, 0) == 0 || errno != ESRCH))
		return (pid_t)pid;

	// No (usable) pid file: look for the team.
	int32 cookie = 0;
	team_info info;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		BString command(info.args);
		int32 space = command.FindFirst(' ');
		if (space >= 0)
			command.Truncate(space);
		if (command == "dockerd" || command.EndsWith("/dockerd"))
			return info.team;
	}
	return -1;
}


bool IsAlive(pid_t pid)
{
	return pid > 0 && (kill(pid, 0) == 0 || errno != ESRCH);
}


status_t Stop(BString* outError)
{
	pid_t pid = RunningPid();
	if (pid < 0) {
		*outError = "No running dockerd was found (no /var/run/docker.pid)";
		return B_ENTRY_NOT_FOUND;
	}
	if (kill(pid, SIGTERM) != 0) {
		outError->SetToFormat("Could not stop dockerd: %s", strerror(errno));
		return B_FROM_POSIX_ERROR(errno);
	}
	return B_OK;
}


BString ConfigPath()
{
	return "/etc/docker/daemon.json";
}

} // namespace EngineControl
