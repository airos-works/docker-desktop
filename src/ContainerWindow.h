// SPDX-License-Identifier: MIT
// One container in detail: its state and actions, and tabs with its logs
// (followed live), its inspect data and its resource usage.

#ifndef DOCKER_CONTAINER_WINDOW_H
#define DOCKER_CONTAINER_WINDOW_H

#include "DockerClient.h"

#include <String.h>
#include <Window.h>

class BButton;
class BCheckBox;
class BMessageRunner;
class BStringView;
class BTabView;
class BTextView;

class ContainerWindow : public BWindow {
public:
	enum {
		kLogsTab = 0,
		kInspectTab,
		kStatsTab,
	};

	// Opens the window of a container, or brings its open one forward.
	static void Open(const char* id, const char* name, int32 tab,
		BWindow* parent);

	~ContainerWindow();

	bool QuitRequested() override;
	void MessageReceived(BMessage* message) override;

private:
	ContainerWindow(const char* id, const char* name, BWindow* parent);

	void _Poll();
	void _StateReceived(BMessage* message);
	void _StartLogs(bool clear);
	void _StopLogs();
	void _AppendLog(BMessage* message);
	void _StartStats();
	void _StopStats();
	void _StatsReceived(BMessage* message);
	void _LoadInspect();
	void _Action(const char* action, const char* status);
	void _UpdateButtons();

	BString fId;
	BString fName;
	BString fSocket;
	BString fState;
	BString fStartedAt;
	BString fImage;
	int32 fPort;
	bool fTty;

	BStringView* fTitle;
	BStringView* fSubtitle;
	BStringView* fStatus;
	BButton* fStartStopButton;
	BButton* fRestartButton;
	BButton* fPauseButton;
	BButton* fTerminalButton;
	BButton* fBrowserButton;
	BButton* fDeleteButton;
	BTabView* fTabs;

	BTextView* fLogView;
	BCheckBox* fFollow;
	BCheckBox* fTimestamps;
	CancelerRef fLogStream;
	time_t fLogsEnded;
	int32 fLogGeneration;

	BTextView* fInspectView;

	BStringView* fCpu;
	BStringView* fMemory;
	BStringView* fNetwork;
	BStringView* fBlock;
	BStringView* fPids;
	BStringView* fStatsNote;
	CancelerRef fStatsStream;

	BMessageRunner* fPoller;
	bool fPolling;
	bool fBusy;
	bool fGone;
};

#endif
