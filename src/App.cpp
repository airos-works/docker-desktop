// SPDX-License-Identifier: MIT

#include "App.h"

#include "MainWindow.h"
#include "Messages.h"

#include <Window.h>

DockerApp::DockerApp()
	:
	BApplication("application/x-vnd.airos.docker-desktop"),
	fWindow(NULL)
{
}

void DockerApp::ReadyToRun()
{
	fWindow = new MainWindow();
	fWindow->Show();
	fWindow->PostMessage(M_REFRESH);
}

bool DockerApp::QuitRequested()
{
	return BApplication::QuitRequested();
}
