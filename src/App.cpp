// SPDX-License-Identifier: MIT

#include "App.h"

#include "MainWindow.h"
#include "Messages.h"
#include "Settings.h"

#include <Window.h>

DockerApp::DockerApp()
	:
	BApplication("application/x-vnd.airos.docker-desktop"),
	fWindow(NULL)
{
	Settings::Default().Load();
}


void DockerApp::ReadyToRun()
{
	fWindow = new MainWindow();
	fWindow->Show();
	if (fPending.what != 0)
		fWindow->PostMessage(&fPending);
}


void DockerApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case B_SILENT_RELAUNCH:
		// Launched again (from the Deskbar, say): show the window.
		if (fWindow != NULL)
			fWindow->Activate();
		break;

	// From the Deskbar replicant's menu
	case M_SHOW_SETTINGS:
	case M_IMAGE_PULL:
	case M_RUN_CONTAINER:
	case M_HUB_SEARCH:
		if (fWindow == NULL) {
			fPending = *message;
			break;
		}
		fWindow->PostMessage(message);
		fWindow->Activate();
		break;

	default:
		BApplication::MessageReceived(message);
		break;
	}
}


bool DockerApp::QuitRequested()
{
	return BApplication::QuitRequested();
}
