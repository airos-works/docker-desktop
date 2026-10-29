// SPDX-License-Identifier: MIT
// Searches Docker Hub (through the Engine's /images/search) and pulls or
// runs what it finds.

#ifndef DOCKER_HUB_SEARCH_WINDOW_H
#define DOCKER_HUB_SEARCH_WINDOW_H

#include <Window.h>

class BButton;
class BColumnListView;
class BStringView;
class BTextControl;

class HubSearchWindow : public BWindow {
public:
	// Opens the search window, or brings the open one forward.
	static void Open(BWindow* parent, const char* term = NULL);

	~HubSearchWindow();

	void MessageReceived(BMessage* message) override;

private:
	HubSearchWindow(BWindow* parent);

	void _Search();
	void _Results(BMessage* message);
	BString _SelectedName() const;
	void _UpdateButtons();

	BTextControl* fTerm;
	BColumnListView* fResults;
	BStringView* fStatus;
	BButton* fPullButton;
	BButton* fRunButton;
	BButton* fHubButton;
	int32 fSearchCount;
};

#endif
