// SPDX-License-Identifier: MIT

#include "DeskbarView.h"

#include "Columns.h"
#include "DockerClient.h"
#include "EngineControl.h"
#include "Json.h"
#include "Messages.h"
#include "Settings.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Deskbar.h>
#include <image.h>
#include <IconUtils.h>
#include <Invoker.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <Window.h>

#include <atomic>
#include <thread>

#include <math.h>
#include <sys/wait.h>
#include <unistd.h>

const char* kDeskbarItemName = "DockerDesktop";

static const char* kSignature = "application/x-vnd.airos.docker-desktop";
static const bigtime_t kPollInterval = 3000000;

extern "C" _EXPORT BView* instantiate_deskbar_item(float maxWidth,
	float maxHeight);

namespace {

enum {
	kPoll = 'dbpl',
	kStatus = 'dbst',
	kOpen = 'dbop',
	kStart = 'dbsa',
	kStop = 'dbso',
	kRestart = 'dbrs',
	kRemove = 'dbrm',
	kActionDone = 'dbad',
	kStopConfirmed = 'dbsc',
};

// A dockerd started from the Deskbar is Deskbar's child: the poll reaps it
// when it ends (it would stay a zombie otherwise).
std::atomic<pid_t> sStartedPid(-1);

status_t StartEngine(BString* error)
{
	pid_t pid;
	status_t status = EngineControl::Start(&pid, error);
	if (status == B_OK)
		sStartedPid = pid;
	return status;
}


// Deskbar unloads a replicant's add-on with its last instance, while a
// worker thread may still run in its code: loading this image once more
// keeps it in memory for as long as Deskbar runs.
void PinImage()
{
	static bool sPinned = false;
	if (sPinned)
		return;
	sPinned = true;
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		uint8* text = (uint8*)info.text;
		uint8* address = (uint8*)&instantiate_deskbar_item;
		if (address >= text && address < text + info.text_size) {
			load_add_on(info.name);
			break;
		}
	}
}

} // namespace


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	float size = maxHeight < maxWidth ? maxHeight : maxWidth;
	return new DeskbarView(BRect(0, 0, size - 1, size - 1));
}


// #pragma mark - DeskbarItem


namespace DeskbarItem {

bool IsInstalled()
{
	return BDeskbar().HasItem(kDeskbarItemName);
}


status_t Install()
{
	app_info info;
	status_t status = be_app->GetAppInfo(&info);
	if (status != B_OK)
		return status;
	return BDeskbar().AddItem(&info.ref);
}


status_t Remove()
{
	return BDeskbar().RemoveItem(kDeskbarItemName);
}

} // namespace DeskbarItem


// #pragma mark - DeskbarView


DeskbarView::DeskbarView(BRect frame)
	:
	BView(frame, kDeskbarItemName, B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW)
{
	_Init();
}


DeskbarView::DeskbarView(BMessage* archive)
	:
	BView(archive)
{
	_Init();
}


DeskbarView::~DeskbarView()
{
	delete fPoller;
	delete fIcon;
}


void DeskbarView::_Init()
{
	fIcon = NULL;
	fPoller = NULL;
	fChecked = false;
	fRunning = false;
	fPolling = false;
	fBusy = false;
	fRunningContainers = 0;
	fContainers = 0;
	PinImage();
	Settings::Default().Load();
	_LoadIcon();
}


DeskbarView* DeskbarView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "DeskbarView"))
		return NULL;
	return new DeskbarView(archive);
}


status_t DeskbarView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kSignature);
	if (status == B_OK)
		status = archive->AddString("class", "DeskbarView");
	return status;
}


void DeskbarView::_LoadIcon()
{
	// The app icon, from the resources of this very image.
	BResources resources;
	if (resources.SetToImage((const void*)&instantiate_deskbar_item)
			!= B_OK)
		return;
	size_t size;
	const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE,
		"BEOS:ICON", &size);
	if (data == NULL)
		return;
	BRect bounds = Bounds();
	fIcon = new BBitmap(BRect(0, 0, bounds.Width(), bounds.Height()),
		B_RGBA32);
	if (BIconUtils::GetVectorIcon((const uint8*)data, size, fIcon) != B_OK) {
		delete fIcon;
		fIcon = NULL;
	}
}


void DeskbarView::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();
	if (Parent() == NULL)
		SetViewColor(B_TRANSPARENT_COLOR);
	else
		SetViewColor(Parent()->ViewColor());
	SetLowColor(ViewColor());

	BMessage poll(kPoll);
	fPoller = new BMessageRunner(BMessenger(this), &poll, kPollInterval);
	_Poll();
}


void DeskbarView::DetachedFromWindow()
{
	delete fPoller;
	fPoller = NULL;
	BView::DetachedFromWindow();
}


void DeskbarView::Draw(BRect updateRect)
{
	SetDrawingMode(B_OP_ALPHA);
	if (fIcon != NULL)
		DrawBitmap(fIcon, BPoint(0, 0));

	// The Engine's state, as a dot in the lower right corner.
	BRect bounds = Bounds();
	float dot = floorf(bounds.Height() * 0.42f);
	BRect badge(bounds.right - dot, bounds.bottom - dot, bounds.right,
		bounds.bottom);
	rgb_color color = !fChecked ? StateColor("exited")
		: fBusy ? StateColor("paused")
		: fRunning ? StateColor("running") : StateColor("dead");
	SetHighColor(color);
	FillEllipse(badge);
	SetHighColor(tint_color(color, B_DARKEN_3_TINT));
	StrokeEllipse(badge);
	SetDrawingMode(B_OP_COPY);
}


void DeskbarView::_Poll()
{
	if (fPolling)
		return;
	fPolling = true;
	BString socket = Settings::Default().SocketPath();
	BMessenger target(this);
	std::thread([socket, target]() {
		DockerClient client(socket.String());
		BMessage status(kStatus);
		bool running = client.Ping() == B_OK;
		status.AddBool("running", running);
		BString json;
		if (running && client.Get("/containers/json?all=1", &json) == B_OK) {
			djson::Value* root = djson::Parse(json.String());
			int32 runningCount = 0;
			for (int32 i = 0; i < djson::Length(root); i++) {
				if (BString(djson::GetString(djson::At(root, i), "State"))
						== "running")
					runningCount++;
			}
			status.AddInt32("containers", djson::Length(root));
			status.AddInt32("running containers", runningCount);
			djson::FreeValue(root);
		}
		target.SendMessage(&status);
	}).detach();
}


void DeskbarView::_OpenApp(uint32 what)
{
	if (what != 0) {
		BMessage message(what);
		be_roster->Launch(kSignature, &message);
	} else
		be_roster->Launch(kSignature);
	// An instance that runs already is only told; bring it forward.
	app_info info;
	if (be_roster->GetAppInfo(kSignature, &info) == B_OK)
		be_roster->ActivateApp(info.team);
}


void DeskbarView::_EngineAction(uint32 what)
{
	fBusy = true;
	Invalidate();
	BMessenger target(this);
	std::thread([what, target]() {
		BString error;
		status_t status = B_OK;
		if (what == kStop || what == kRestart) {
			pid_t pid = EngineControl::RunningPid();
			status = EngineControl::Stop(&error);
			// Wait (at most a minute) for it to be all gone.
			for (int i = 0; status == B_OK && i < 120
					&& EngineControl::IsAlive(pid); i++) {
				usleep(500000);
			}
		}
		if (status == B_OK && (what == kStart || what == kRestart))
			status = StartEngine(&error);
		BMessage done(kActionDone);
		if (status != B_OK)
			done.AddString("error", error);
		target.SendMessage(&done);
	}).detach();
}


void DeskbarView::_ShowMenu(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("docker", false, false);
	menu->SetFont(be_plain_font);

	BString state;
	if (!fChecked)
		state = "Checking the Docker Engine…";
	else if (fBusy)
		state = "Docker Engine is changing state…";
	else if (fRunning) {
		state.SetToFormat("Docker Engine is running (%" B_PRId32 " of %"
			B_PRId32 " containers up)", fRunningContainers, fContainers);
	} else
		state = "Docker Engine is stopped";
	BMenuItem* header = new BMenuItem(state.String(), NULL);
	header->SetEnabled(false);
	menu->AddItem(header);
	menu->AddSeparatorItem();

	menu->AddItem(new BMenuItem("Open Docker Desktop", new BMessage(kOpen)));
	BMessage* pull = new BMessage(kOpen);
	pull->AddInt32("what", M_IMAGE_PULL);
	menu->AddItem(new BMenuItem("Pull an image…", pull));
	BMessage* settings = new BMessage(kOpen);
	settings->AddInt32("what", M_SHOW_SETTINGS);
	menu->AddItem(new BMenuItem("Settings…", settings));
	menu->AddSeparatorItem();

	BMenuItem* item = new BMenuItem("Start Docker Engine",
		new BMessage(kStart));
	item->SetEnabled(fChecked && !fRunning && !fBusy);
	menu->AddItem(item);
	item = new BMenuItem("Restart Docker Engine", new BMessage(kRestart));
	item->SetEnabled(fRunning && !fBusy);
	menu->AddItem(item);
	item = new BMenuItem("Stop Docker Engine", new BMessage(kStop));
	item->SetEnabled(fRunning && !fBusy);
	menu->AddItem(item);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Remove from Deskbar", new BMessage(kRemove)));

	menu->SetTargetForItems(this);
	ConvertToScreen(&where);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, BRect(where - BPoint(4, 4),
		where + BPoint(4, 4)), true);
}


void DeskbarView::MouseDown(BPoint where)
{
	_ShowMenu(where);
}


void DeskbarView::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kPoll:
	{
		pid_t started = sStartedPid;
		int result;
		if (started > 0 && waitpid(started, &result, WNOHANG) == started)
			sStartedPid = -1;
		_Poll();
		break;
	}

	case kStatus:
	{
		fPolling = false;
		bool running = message->GetBool("running", false);
		int32 containers = message->GetInt32("containers", 0);
		int32 runningContainers = message->GetInt32("running containers", 0);
		if (fChecked && running == fRunning && containers == fContainers
			&& runningContainers == fRunningContainers)
			break;
		fChecked = true;
		fRunning = running;
		fContainers = containers;
		fRunningContainers = runningContainers;
		BString tip;
		if (running) {
			tip.SetToFormat("Docker Engine is running\n%" B_PRId32
				" of %" B_PRId32 " containers up", runningContainers,
				containers);
		} else
			tip = "Docker Engine is stopped";
		SetToolTip(tip.String());
		Invalidate();
		break;
	}

	case kOpen:
		_OpenApp(message->GetInt32("what", 0));
		break;

	case kStop:
		if (fRunningContainers > 0) {
			// Asked without blocking Deskbar; the answer comes back here.
			BAlert* alert = new BAlert("Docker Desktop", "Stop the Docker "
				"Engine? Its running containers stop with it.", "Cancel",
				"Stop Engine", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			alert->Go(new BInvoker(new BMessage(kStopConfirmed), this));
			break;
		}
		_EngineAction(kStop);
		break;
	case kStopConfirmed:
		if (message->GetInt32("which", 0) == 1)
			_EngineAction(kStop);
		break;
	case kStart:
	case kRestart:
		_EngineAction(message->what);
		break;

	case kActionDone:
	{
		fBusy = false;
		BString error;
		if (message->FindString("error", &error) == B_OK) {
			BAlert* alert = new BAlert("Docker Desktop", error.String(), "OK",
				NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go(NULL);
		}
		_Poll();
		Invalidate();
		break;
	}

	case kRemove:
		DeskbarItem::Remove();
		break;

	default:
		BView::MessageReceived(message);
		break;
	}
}
