// SPDX-License-Identifier: MIT

#ifndef DOCKER_MAIN_WINDOW_H
#define DOCKER_MAIN_WINDOW_H

#include <List.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

class BButton;
class BListView;
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
	BMessenger fMessenger;
	BString fSocketPath;
	bool fBusy;
};

#endif
