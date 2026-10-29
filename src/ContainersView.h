// SPDX-License-Identifier: MIT

#ifndef DOCKER_CONTAINERS_VIEW_H
#define DOCKER_CONTAINERS_VIEW_H

#include "ResourceView.h"

class BCheckBox;

class ContainersView : public ResourceView {
public:
	ContainersView();

	void MessageReceived(BMessage* message) override;

	// Counts from the last list, for the status bar.
	int32 CountRunning() const { return fRunning; }
	int32 CountAll() const { return fTotal; }

protected:
	FetchFunction Fetcher() const override;
	void Populate(const BMessage& result) override;
	void UpdateButtons() override;
	void BuildContextMenu(BPopUpMenu* menu) override;
	void Invoked(ObjectRow* row) override;
	const char* EmptyText() const override;

private:
	// The selected containers, with those of selected compose projects.
	std::vector<BString> _SelectedIds(const char* onlyInState = NULL,
		bool runningOrPaused = false) const;
	void _Action(const char* verb, const char* status, const char* state,
		bool runningOrPaused);
	void _Remove();
	void _Rename(BMessage* message);
	void _OpenTerminal();
	void _OpenBrowser();
	void _OpenDetails(int32 tab);

	BButton* fStartButton;
	BButton* fStopButton;
	BButton* fRestartButton;
	BButton* fPauseButton;
	BButton* fDeleteButton;
	BButton* fDetailsButton;
	BCheckBox* fOnlyRunning;
	int32 fRunning;
	int32 fTotal;
};

#endif
