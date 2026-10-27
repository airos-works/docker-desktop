// SPDX-License-Identifier: MIT

#ifndef DOCKER_APP_H
#define DOCKER_APP_H

#include <Application.h>

class MainWindow;

class DockerApp : public BApplication {
public:
	DockerApp();

	void ReadyToRun() override;
	bool QuitRequested() override;

private:
	MainWindow* fWindow;
};

#endif
