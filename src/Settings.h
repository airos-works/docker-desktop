// SPDX-License-Identifier: MIT
// Docker Desktop's own settings: a flattened BMessage in
// ~/config/settings/DockerDesktop. (The Engine's start-at-boot switch lives
// in the docker_engine file instead; see EngineControl.h.)

#ifndef DOCKER_SETTINGS_H
#define DOCKER_SETTINGS_H

#include <Locker.h>
#include <Message.h>
#include <Rect.h>
#include <String.h>

extern const char* kDefaultSocketPath;

class Settings {
public:
	static Settings& Default();

	status_t Load();
	status_t Save();

	BString SocketPath();
	void SetSocketPath(const char* path);

	BRect WindowFrame();
	void SetWindowFrame(BRect frame);

	int32 CurrentTab();
	void SetCurrentTab(int32 tab);

	bool OnlyRunning();
	void SetOnlyRunning(bool only);

	bool StartEngineOnLaunch();
	void SetStartEngineOnLaunch(bool start);

	bool ConfirmDeletes();
	void SetConfirmDeletes(bool confirm);

	// The saved layout (column widths, order, sorting) of a column list.
	bool GetListState(const char* name, BMessage* state);
	void SetListState(const char* name, const BMessage& state);

private:
	Settings();

	BLocker fLock;
	BMessage fData;
};

#endif
