// SPDX-License-Identifier: MIT
// Creates and starts a container from an image, with the optional settings
// of Docker Desktop's run dialog: name, ports, volumes, environment,
// command and restart policy.

#ifndef DOCKER_RUN_WINDOW_H
#define DOCKER_RUN_WINDOW_H

#include "DockerClient.h"

#include <Window.h>

class BButton;
class BCheckBox;
class BMenuField;
class BStringView;
class BTextControl;
class BTextView;

class RunWindow : public BWindow {
public:
	RunWindow(const char* image, BWindow* parent);
	~RunWindow();

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void _Run();
	void _RunInTerminal();
	status_t _BuildRequest(BString& body, BString& error);

	BTextControl* fImage;
	BTextControl* fName;
	BTextControl* fPorts;
	BTextView* fVolumes;
	BTextView* fEnvironment;
	BTextControl* fCommand;
	BMenuField* fRestart;
	BCheckBox* fAutoRemove;
	BCheckBox* fInteractive;
	BStringView* fStatus;
	BButton* fRunButton;
	CancelerRef fCanceler;
};

#endif
