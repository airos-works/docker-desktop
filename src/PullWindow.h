// SPDX-License-Identifier: MIT
// Pulls an image from a registry, showing its progress.

#ifndef DOCKER_PULL_WINDOW_H
#define DOCKER_PULL_WINDOW_H

#include "DockerClient.h"

#include <Window.h>

class BButton;
class BStatusBar;
class BStringView;
class BTextControl;
class BTextView;

class PullWindow : public BWindow {
public:
	// With autoStart, pulls reference right away.
	PullWindow(const char* reference, BWindow* parent,
		bool autoStart = false);
	~PullWindow();

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void _Start();
	void _Finished(BMessage* message);

	BTextControl* fReference;
	BTextControl* fPlatform;
	BStatusBar* fProgress;
	BStringView* fStatus;
	BTextView* fDetails;
	BButton* fPullButton;
	BButton* fRunButton;
	BButton* fCloseButton;
	CancelerRef fCanceler;
	BString fPulled;
};

#endif
