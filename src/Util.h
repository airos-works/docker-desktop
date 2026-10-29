// SPDX-License-Identifier: MIT
// Formatting of Engine data, worker threads and desktop integration
// (Terminal, the web browser, the clipboard).

#ifndef DOCKER_UTIL_H
#define DOCKER_UTIL_H

#include <Messenger.h>
#include <String.h>

#include <functional>
#include <time.h>

namespace djson {
struct Value;
}

class BStringView;
class BWindow;

namespace Util {

// "13.9 MB": decimal units, like the docker CLI.
BString FormatSize(double bytes);
// "5 minutes ago"
BString FormatAge(time_t when);
// An RFC 3339 time ("2026-10-29T18:44:09.37Z") as Unix time; 0 if none.
time_t ParseTime(const char* text);
// The first 12 hex digits of an ID, without "sha256:".
BString ShortId(const char* id);
// A container's published ports: "8080:80/tcp, 443/tcp"
BString FormatPorts(const djson::Value* ports);
// The first host port of a container that is published on the host.
int32 FirstHostPort(const djson::Value* ports);

// Runs work on a thread of its own, then sends the message it filled in
// (whose what is the given code) to target.
void RunTask(const BMessenger& target, uint32 what,
	std::function<void(BMessage& reply)> work);

// Quotes text for /bin/sh.
BString ShellQuote(const char* text);
// The docker CLI ("/boot/system/bin/docker"), with "-H unix://..." when
// the Engine is not at the default socket, quoted for the shell.
BString DockerCommand(const char* socketPath);
// The Engine's operating system ("linux", "haiku"), from its /version.
void SetEngineOS(const char* os);
// The docker CLI options for a command that talks to a person in Terminal:
// "-it", or "-i" where the Engine gives execs no terminal (air/OS), so
// that Terminal stays in line mode and echoes what is typed.
const char* InteractiveFlags();
// A docker exec command line that opens a shell in a container.
BString ShellCommand(const char* socketPath, const char* containerId);
// Opens a Terminal window running a shell command line; keepOpen waits
// for Enter after it ends so its output can be read.
status_t RunInTerminal(const char* title, const char* commandLine,
	bool keepOpen);
status_t OpenURL(const char* url);
void CopyToClipboard(const char* text);

// A modal question; true when the default (destructive) button is chosen.
bool Confirm(const char* text, const char* action);
void ShowError(const char* text);

// Lets a label of changing, possibly long text (a status line) shrink
// with its window, cutting the text short, instead of widening it.
void MakeElastic(BStringView* view);

// Places a newly made window over the middle of parent.
void CenterOver(BWindow* window, BWindow* parent);

} // namespace Util

#endif
