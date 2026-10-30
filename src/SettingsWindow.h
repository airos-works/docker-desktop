// SPDX-License-Identifier: MIT
// Settings: Docker Desktop's own, the Engine's daemon.json, Docker Hub
// sign-in, the Linux VM, and the troubleshooting tools.

#ifndef DOCKER_SETTINGS_WINDOW_H
#define DOCKER_SETTINGS_WINDOW_H

#include <Messenger.h>
#include <Window.h>

class BButton;
class BCheckBox;
class BStringView;
class BTabView;
class BTextControl;
class BTextView;

class SettingsWindow : public BWindow {
public:
	enum {
		kGeneralTab = 0,
		kEngineTab,
		kLinuxVMTab,
		kTroubleshootTab,
	};

	// Opens the window (at a tab), or brings the open one forward.
	static void Open(BWindow* mainWindow, int32 tab = kGeneralTab);
	// Keeps the open window's check box in line with the main window's.
	static void StartAtBootChanged(bool start);
	// Has the open window look at the Deskbar again.
	static void DeskbarChanged();

	~SettingsWindow();

	void MessageReceived(BMessage* message) override;

private:
	SettingsWindow(BWindow* mainWindow);

	void _LoadDaemonConfig();
	void _SaveDaemonConfig(bool restart);
	void _UpdateAccounts();
	void _Purge();
	void _LoadVMSettings();
	void _SaveVMSettings();
	void _UpdateVMStatus();
	void _ShowVMLog();

	BMessenger fMainWindow;
	BTabView* fTabs;
	BTextControl* fSocket;
	BCheckBox* fStartOnLaunch;
	BCheckBox* fStartAtBoot;
	BCheckBox* fConfirmDeletes;
	BCheckBox* fShowInDeskbar;
	BStringView* fAccounts;
	BTextView* fDaemonConfig;
	BStringView* fDaemonStatus;
	BStringView* fVMStatus;
	BTextControl* fVMMemory;
	BTextControl* fVMCpus;
	BStringView* fVMSettingsStatus;
	BButton* fVMStopButton;
	BStringView* fTroubleStatus;
	BButton* fPurgeButton;
};

#endif
