// SPDX-License-Identifier: MIT
// Docker Desktop in the Deskbar, like its whale in other systems' menu bars
// and trays: the Docker icon with a dot for the Engine's state, and a menu
// to open Docker Desktop and to start, stop or restart the Engine.
//
// Deskbar loads the DockerDesktop executable as an add-on and calls its
// instantiate_deskbar_item(); the view then runs in Deskbar's team, so it
// keeps to the Engine socket and EngineControl, and never blocks there.

#ifndef DOCKER_DESKBAR_VIEW_H
#define DOCKER_DESKBAR_VIEW_H

#include <String.h>
#include <View.h>

class BBitmap;
class BMessageRunner;

extern const char* kDeskbarItemName;

namespace DeskbarItem {

bool IsInstalled();
// Adds the replicant of the running Docker Desktop's executable.
status_t Install();
status_t Remove();

} // namespace DeskbarItem

class _EXPORT DeskbarView : public BView {
public:
	DeskbarView(BRect frame);
	DeskbarView(BMessage* archive);
	~DeskbarView();

	static DeskbarView* Instantiate(BMessage* archive);
	status_t Archive(BMessage* archive, bool deep = true) const override;

	void AttachedToWindow() override;
	void DetachedFromWindow() override;
	void Draw(BRect updateRect) override;
	void MouseDown(BPoint where) override;
	void MessageReceived(BMessage* message) override;

private:
	void _Init();
	void _LoadIcon();
	void _Poll();
	void _ShowMenu(BPoint where);
	void _OpenApp(uint32 what = 0);
	void _EngineAction(uint32 what);

	BBitmap* fIcon;
	BMessageRunner* fPoller;
	bool fChecked;
	bool fRunning;
	bool fPolling;
	bool fBusy;
	int32 fRunningContainers;
	int32 fContainers;
};

#endif
