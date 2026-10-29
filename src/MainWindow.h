// SPDX-License-Identifier: MIT

#ifndef DOCKER_MAIN_WINDOW_H
#define DOCKER_MAIN_WINDOW_H

#include "DockerClient.h"

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <sys/types.h>

class BButton;
class BCheckBox;
class BFilePanel;
class BMenuBar;
class BMenuItem;
class BMessageRunner;
class BStringView;
class BTabView;
class ContainersView;
class DiskUsageView;
class EngineDot;
class ImagesView;
class NetworksView;
class ResourceView;
class VolumesView;

class MainWindow : public BWindow {
public:
	MainWindow();
	~MainWindow();

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;
	void MenusBeginning() override;

private:
	BMenuBar* _BuildMenuBar();
	ResourceView* _ResourceView(int32 tab) const;
	void _RefreshAll();
	void _TabChanged();

	void _PollEngine();
	void _HandleEngineStatus(BMessage* message);
	void _StartEngine();
	void _StopEngine();
	void _RestartEngine();
	void _SetStartAtBoot(bool start);
	void _UpdateEngineControls();
	void _FetchVersion();
	void _StartEvents();
	void _StopEvents();
	void _HandleEvent(BMessage* message);
	void _SocketChanged();
	void _UpdateCounts();

	void _ShowEngineLog();
	void _ShowAbout();
	void _OpenPanel(uint32 purpose);
	void _RefsReceived(BMessage* message);

	BTabView* fTabs;
	ContainersView* fContainersView;
	ImagesView* fImagesView;
	VolumesView* fVolumesView;
	NetworksView* fNetworksView;
	DiskUsageView* fDiskUsageView;

	EngineDot* fEngineDot;
	BStringView* fEngineState;
	BStringView* fEngineVersion;
	BButton* fEngineStartButton;
	BButton* fEngineRestartButton;
	BCheckBox* fStartAtBootCheck;
	BMenuItem* fStartAtBootItem;
	BMenuItem* fDeskbarItem;
	BStringView* fStatus;
	BStringView* fCounts;

	BMessageRunner* fEnginePoller;
	BFilePanel* fOpenPanel;
	BFilePanel* fFolderPanel;
	BString fSocketPath;
	BString fVersion;
	CancelerRef fEvents;
	bool fPinging;
	bool fEngineChecked;
	bool fEngineRunning;
	bool fEngineStarting;
	bool fEngineStopping;
	bool fRestartPending;
	pid_t fEnginePid;
	pid_t fStoppedPid;
	BString fEngineError;
	int32 fPollCount;
	int32 fEventsGeneration;
};

#endif
