// SPDX-License-Identifier: MIT

#ifndef DOCKER_APP_H
#define DOCKER_APP_H

#include <Application.h>
#include <Message.h>

class MainWindow;

class DockerApp : public BApplication {
public:
	DockerApp();

	void ReadyToRun() override;
	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	MainWindow* fWindow;
	// What was asked for (from the Deskbar menu) before the window was up
	BMessage fPending;
};

#endif
