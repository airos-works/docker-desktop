// SPDX-License-Identifier: MIT

#include "Util.h"

#include "Json.h"

#include <Alert.h>
#include <Autolock.h>
#include <Locker.h>
#include <Clipboard.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <StringView.h>
#include <Window.h>

#include <thread>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* kDefaultSocket = "/var/run/docker.sock";

static BLocker sEngineLock("engine os");
static BString sEngineOS;

namespace Util {

BString FormatSize(double bytes)
{
	static const char* kUnits[] = { "B", "kB", "MB", "GB", "TB" };
	int unit = 0;
	while (bytes >= 1000 && unit < 4) {
		bytes /= 1000;
		unit++;
	}
	BString text;
	if (unit == 0)
		text.SetToFormat("%.0f B", bytes);
	else
		text.SetToFormat(bytes < 10 ? "%.2f %s" : "%.1f %s", bytes,
			kUnits[unit]);
	return text;
}


BString FormatAge(time_t when)
{
	if (when <= 0)
		return "";
	time_t seconds = time(NULL) - when;
	BString text;
	if (seconds < 60)
		text = "Less than a minute ago";
	else if (seconds < 3600) {
		long minutes = seconds / 60;
		text.SetToFormat("%ld minute%s ago", minutes, minutes == 1 ? "" : "s");
	} else if (seconds < 86400) {
		long hours = seconds / 3600;
		text.SetToFormat("%ld hour%s ago", hours, hours == 1 ? "" : "s");
	} else if (seconds < 86400 * 30) {
		long days = seconds / 86400;
		text.SetToFormat("%ld day%s ago", days, days == 1 ? "" : "s");
	} else if (seconds < 86400 * 365) {
		long months = seconds / (86400 * 30);
		text.SetToFormat("%ld month%s ago", months, months == 1 ? "" : "s");
	} else {
		long years = seconds / (86400 * 365);
		text.SetToFormat("%ld year%s ago", years, years == 1 ? "" : "s");
	}
	return text;
}


time_t ParseTime(const char* text)
{
	if (text == NULL || strncmp(text, "0001-", 5) == 0)
		return 0;
	int year, month, day, hour, minute, second;
	if (sscanf(text, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour,
			&minute, &second) != 6)
		return 0;
	// Days since 1970-01-01 of the proleptic Gregorian calendar.
	int y = month <= 2 ? year - 1 : year;
	int era = (y >= 0 ? y : y - 399) / 400;
	int yearOfEra = y - era * 400;
	int dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100
		+ dayOfYear;
	int64 days = (int64)era * 146097 + dayOfEra - 719468;
	time_t result = (time_t)(days * 86400 + hour * 3600 + minute * 60
		+ second);

	// A zone offset after the seconds: "Z", "+02:00" or "-05:00".
	const char* zone = text + 19;
	while (*zone == '.' || (*zone >= '0' && *zone <= '9'))
		zone++;
	if (*zone == '+' || *zone == '-') {
		int hours = 0, minutes = 0;
		sscanf(zone + 1, "%d:%d", &hours, &minutes);
		long offset = hours * 3600 + minutes * 60;
		result += *zone == '+' ? -offset : offset;
	}
	return result;
}


BString ShortId(const char* id)
{
	BString text(id);
	if (text.StartsWith("sha256:"))
		text.Remove(0, 7);
	text.Truncate(12);
	return text;
}


BString FormatPorts(const djson::Value* ports)
{
	BString text;
	for (int32 i = 0; i < djson::Length(ports); i++) {
		const djson::Value* port = djson::At(ports, i);
		double publicPort = djson::GetNumber(port, "PublicPort", 0);
		BString entry;
		if (publicPort > 0) {
			entry.SetToFormat("%.0f:%.0f/%s", publicPort,
				djson::GetNumber(port, "PrivatePort"),
				djson::GetString(port, "Type", "tcp"));
		} else {
			entry.SetToFormat("%.0f/%s", djson::GetNumber(port, "PrivatePort"),
				djson::GetString(port, "Type", "tcp"));
		}
		// IPv4 and IPv6 bindings of one port come as two entries.
		if (text.FindFirst(entry) >= 0)
			continue;
		if (text.Length() > 0)
			text << ", ";
		text << entry;
	}
	return text;
}


int32 FirstHostPort(const djson::Value* ports)
{
	for (int32 i = 0; i < djson::Length(ports); i++) {
		double publicPort = djson::GetNumber(djson::At(ports, i),
			"PublicPort", 0);
		if (publicPort > 0)
			return (int32)publicPort;
	}
	return -1;
}


void RunTask(const BMessenger& target, uint32 what,
	std::function<void(BMessage& reply)> work)
{
	std::thread([target, what, work]() {
		BMessage reply(what);
		work(reply);
		target.SendMessage(&reply);
	}).detach();
}


BString ShellQuote(const char* text)
{
	BString quoted(text);
	quoted.ReplaceAll("'", "'\\''");
	quoted.Prepend("'");
	quoted.Append("'");
	return quoted;
}


BString DockerCommand(const char* socketPath)
{
	BString command("docker");
	static const directory_which kDirectories[] = {
		B_SYSTEM_BIN_DIRECTORY,
		B_USER_NONPACKAGED_BIN_DIRECTORY,
		B_SYSTEM_NONPACKAGED_BIN_DIRECTORY,
	};
	for (size_t i = 0; i < sizeof(kDirectories) / sizeof(kDirectories[0]);
			i++) {
		BPath path;
		if (find_directory(kDirectories[i], &path) != B_OK
			|| path.Append("docker") != B_OK)
			continue;
		if (access(path.Path(), X_OK) == 0) {
			command = ShellQuote(path.Path());
			break;
		}
	}
	if (socketPath != NULL && strcmp(socketPath, kDefaultSocket) != 0) {
		BString host("unix://");
		host << socketPath;
		command << " -H " << ShellQuote(host.String());
	}
	return command;
}


void SetEngineOS(const char* os)
{
	BAutolock lock(sEngineLock);
	sEngineOS = os;
}


const char* InteractiveFlags()
{
	BAutolock lock(sEngineLock);
	return sEngineOS == "haiku" ? "-i" : "-it";
}


BString ShellCommand(const char* socketPath, const char* containerId)
{
	BString command(DockerCommand(socketPath));
	bool tty = strcmp(InteractiveFlags(), "-it") == 0;
	// Without a terminal, -i makes the shell prompt anyway.
	command << " exec " << InteractiveFlags() << " "
		<< ShellQuote(containerId) << (tty
			? " sh -c 'if command -v bash >/dev/null 2>&1; then exec bash;"
				" else exec sh; fi'"
			: " sh -c 'if command -v bash >/dev/null 2>&1; then exec bash -i;"
				" else exec sh -i; fi'");
	return command;
}


status_t RunInTerminal(const char* title, const char* commandLine,
	bool keepOpen)
{
	BString script(commandLine);
	if (keepOpen) {
		script << "; status=$?; echo; if [ $status -eq 0 ]; then"
			" echo 'Done. Press Enter to close this window.';"
			" else echo \"Failed (exit status $status). Press Enter to close"
			" this window.\"; fi; read dummy";
	}
	const char* argv[] = { "-t", title, "/bin/sh", "-c", script.String(),
		NULL };
	return be_roster->Launch("application/x-vnd.Haiku-Terminal", 5,
		(char**)argv);
}


status_t OpenURL(const char* url)
{
	const char* argv[] = { url, NULL };
	BString type("application/x-vnd.Be.URL.");
	BString scheme(url);
	int32 colon = scheme.FindFirst(':');
	if (colon > 0)
		scheme.Truncate(colon);
	else
		scheme = "http";
	type << scheme;
	return be_roster->Launch(type.String(), 1, (char**)argv);
}


void CopyToClipboard(const char* text)
{
	if (!be_clipboard->Lock())
		return;
	be_clipboard->Clear();
	BMessage* clip = be_clipboard->Data();
	if (clip != NULL) {
		clip->AddData("text/plain", B_MIME_TYPE, text, strlen(text));
		be_clipboard->Commit();
	}
	be_clipboard->Unlock();
}


bool Confirm(const char* text, const char* action)
{
	BAlert* alert = new BAlert("Docker Desktop", text, "Cancel", action, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	return alert->Go() == 1;
}


void ShowError(const char* text)
{
	BAlert* alert = new BAlert("Docker Desktop", text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go(NULL);
}


void MakeElastic(BStringView* view)
{
	view->SetTruncation(B_TRUNCATE_END);
	view->SetExplicitMinSize(BSize(be_plain_font->StringWidth("M") * 4,
		B_SIZE_UNSET));
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
}


void CenterOver(BWindow* window, BWindow* parent)
{
	if (parent == NULL) {
		window->CenterOnScreen();
		return;
	}
	BRect frame = parent->Frame();
	window->Layout(false);
	BSize size = window->Frame().Size();
	window->MoveTo(frame.left + (frame.Width() - size.width) / 2,
		frame.top + (frame.Height() - size.height) / 3);
	window->MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
}

} // namespace Util
