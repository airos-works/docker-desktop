// SPDX-License-Identifier: MIT

#ifndef DOCKER_MAIN_WINDOW_H
#define DOCKER_MAIN_WINDOW_H

#include <List.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <sys/types.h>

class BButton;
class BCheckBox;
class BListView;
class BMessageRunner;
class BStringView;
class BTabView;

class MainWindow : public BWindow {
public:
	MainWindow();
	~MainWindow();

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

	BMessenger& Self() { return fMessenger; }

private:
	void _RefreshLists();
	void _HandleListResult(BMessage* message);
	void _UpdateButtons();
	int32 _SelectedContainerIndex();
	int32 _SelectedImageIndex();
	void _RunAction(const char* id, const char* action, bool isImage);
	void _PollEngine();
	void _HandleEngineStatus(BMessage* message);
	void _StartEngine();
	void _SetStartAtBoot(bool start);
	void _UpdateEngineControls();

	BTabView* fTabs;
	BListView* fContainers;
	BListView* fImages;
	BList* fContainerIds;
	BList* fImageIds;
	BButton* fStartButton;
	BButton* fStopButton;
	BButton* fRestartButton;
	BButton* fRemoveButton;
	BStringView* fStatus;
	BStringView* fEngineState;
	BButton* fEngineStartButton;
	BCheckBox* fStartAtBootCheck;
	BMessageRunner* fEnginePoller;
	BMessenger fMessenger;
	BString fSocketPath;
	bool fBusy;
	bool fPinging;
	bool fEngineChecked;
	bool fEngineRunning;
	bool fEngineStarting;
	pid_t fEnginePid;
	BString fEngineError;
};

#endif
