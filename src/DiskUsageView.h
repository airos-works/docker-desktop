// SPDX-License-Identifier: MIT
// The Engine at a glance: what it is, the disk space its objects take and
// what a clean up would give back, and the clean up itself.

#ifndef DOCKER_DISK_USAGE_VIEW_H
#define DOCKER_DISK_USAGE_VIEW_H

#include <View.h>

class BButton;
class BCheckBox;
class BGridLayout;
class BRadioButton;
class BStringView;

class DiskUsageView : public BView {
public:
	DiskUsageView();

	void AttachedToWindow() override;
	void MessageReceived(BMessage* message) override;

	void Refresh();
	void SetEngineRunning(bool running);

private:
	void _InfoReceived(BMessage* message);
	void _CleanUp();
	void _CleanUpDone(BMessage* message);
	void _UpdateButtons();

	BStringView* fInfo[9];
	BStringView* fUsage[4][4];
	BStringView* fTotal;
	BCheckBox* fContainers;
	BCheckBox* fNetworks;
	BCheckBox* fImages;
	BRadioButton* fDanglingImages;
	BRadioButton* fAllImages;
	BCheckBox* fVolumes;
	BCheckBox* fBuildCache;
	BButton* fCleanUpButton;
	BStringView* fCleanUpStatus;
	bool fEngineRunning;
	bool fLoading;
	bool fCleaning;
};

#endif
