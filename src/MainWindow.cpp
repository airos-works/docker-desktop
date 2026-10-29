// SPDX-License-Identifier: MIT

#include "MainWindow.h"

#include "Columns.h"
#include "ContainersView.h"
#include "DeskbarView.h"
#include "DiskUsageView.h"
#include "DockerClient.h"
#include "EngineControl.h"
#include "HubSearchWindow.h"
#include "ImagesView.h"
#include "Json.h"
#include "Messages.h"
#include "PromptWindow.h"
#include "PullWindow.h"
#include "RunWindow.h"
#include "Settings.h"
#include "SettingsWindow.h"
#include "TextWindow.h"
#include "Util.h"
#include "VolumesView.h"

#include <AboutWindow.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <File.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Screen.h>
#include <StringView.h>
#include <TabView.h>

#include <thread>

#include <math.h>
#include <string.h>
#include <sys/wait.h>

// How often the Engine bar asks the Engine whether it is up.
static const bigtime_t kEnginePollInterval = 2000000;
// Every this many polls, the container list is fetched again for its
// "Up 5 minutes" texts.
static const int32 kStatusRefreshPolls = 15;

namespace {

enum {
	kTabChanged = 'tbch',
	kAbout = 'abou',
	kOpenCompose = 'ocmp',
	kOpenBuildFolder = 'obld',
	kOpenImageFile = 'oimg',
	kBuildTagged = 'bldt',
	kFind = 'find',
	kVersion = 'vers',
	kEngineAction = 'eact',
};

// Tells the window when another tab is chosen.
class TabView : public BTabView {
public:
	TabView()
		:
		BTabView("tabs", B_WIDTH_FROM_LABEL)
	{
	}

	void Select(int32 index) override
	{
		BTabView::Select(index);
		if (Window() != NULL)
			Window()->PostMessage(kTabChanged);
	}
};

void PingTaskRun(BMessenger target, BString socketPath)
{
	DockerClient client(socketPath);
	BMessage message(M_ENGINE_STATUS);
	message.AddBool("running", client.Ping() == B_OK);
	message.AddString("socket", socketPath);
	target.SendMessage(&message);
}

} // namespace


// A dot in the colour of the Engine's state.
class EngineDot : public BView {
public:
	EngineDot()
		:
		BView("engineDot", B_WILL_DRAW),
		fColor(make_color(150, 150, 150))
	{
		float size = ceilf(be_plain_font->Size() * 0.9f);
		SetExplicitSize(BSize(size, size));
	}

	void AttachedToWindow() override
	{
		AdoptParentColors();
	}

	void SetColor(rgb_color color)
	{
		if (color == fColor)
			return;
		fColor = color;
		Invalidate();
	}

	void Draw(BRect updateRect) override
	{
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(fColor);
		BRect bounds(Bounds());
		bounds.InsetBy(1, 1);
		FillEllipse(bounds);
		SetHighColor(tint_color(fColor, B_DARKEN_2_TINT));
		StrokeEllipse(bounds);
	}

private:
	rgb_color fColor;
};


MainWindow::MainWindow()
	:
	BWindow(BRect(80, 60, 980, 680), "Docker Desktop", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	fOpenPanel(NULL),
	fFolderPanel(NULL),
	fSocketPath(Settings::Default().SocketPath()),
	fPinging(false),
	fEngineChecked(false),
	fEngineRunning(false),
	fEngineStarting(false),
	fEngineStopping(false),
	fRestartPending(false),
	fEnginePid(-1),
	fStoppedPid(-1),
	fPollCount(0),
	fEventsGeneration(0)
{
	BMenuBar* menuBar = _BuildMenuBar();

	fContainersView = new ContainersView();
	fImagesView = new ImagesView();
	fVolumesView = new VolumesView();
	fNetworksView = new NetworksView();
	fDiskUsageView = new DiskUsageView();

	fTabs = new TabView();
	fTabs->AddTab(fContainersView);
	fTabs->AddTab(fImagesView);
	fTabs->AddTab(fVolumesView);
	fTabs->AddTab(fNetworksView);
	fTabs->AddTab(fDiskUsageView);
	for (int32 i = 0; i < 4; i++)
		fTabs->TabAt(i)->SetLabel(_ResourceView(i)->Label());
	fTabs->TabAt(4)->SetLabel("Disk usage");
	fTabs->SetBorder(B_NO_BORDER);

	// The Engine bar: whether dockerd is up, buttons to start, stop and
	// restart it, and the switch for starting it at boot.
	fEngineDot = new EngineDot();
	fEngineState = new BStringView("engineState",
		"Checking the Docker Engine…");
	Util::MakeElastic(fEngineState);
	fEngineState->SetFont(be_bold_font);
	fEngineVersion = new BStringView("engineVersion", "");
	Util::MakeElastic(fEngineVersion);
	fEngineVersion->SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);
	fEngineVersion->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fEngineStartButton = new BButton("engineStart", "Start Engine",
		new BMessage(kEngineAction));
	fEngineRestartButton = new BButton("engineRestart", "Restart",
		new BMessage(M_ENGINE_RESTART));
	fStartAtBootCheck = new BCheckBox("startAtBoot", "Start at boot",
		new BMessage(M_ENGINE_START_AT_BOOT));
	fStartAtBootCheck->SetToolTip("Start the Docker Engine at boot (ticking "
		"it also starts it now)");
	fStartAtBootCheck->SetValue(EngineControl::StartsAtBoot()
		? B_CONTROL_ON : B_CONTROL_OFF);

	BBox* engineBox = new BBox("engine");
	BLayoutBuilder::Group<>(engineBox, B_HORIZONTAL, B_USE_SMALL_SPACING)
		.Add(fEngineDot)
		.Add(fEngineState)
		.Add(fEngineVersion)
		.Add(fStartAtBootCheck)
		.Add(fEngineRestartButton)
		.Add(fEngineStartButton)
		.SetInsets(B_USE_SMALL_SPACING);

	fStatus = new BStringView("status", "");
	Util::MakeElastic(fStatus);
	fCounts = new BStringView("counts", "");
	fCounts->SetAlignment(B_ALIGN_RIGHT);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(engineBox)
			.SetInsets(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING,
				B_USE_SMALL_SPACING, 0)
		.End()
		.Add(fTabs)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fStatus)
			.Add(fCounts)
			.SetInsets(B_USE_DEFAULT_SPACING, 2, B_USE_DEFAULT_SPACING, 4)
		.End();

	BRect frame = Settings::Default().WindowFrame();
	if (frame.IsValid() && BScreen(this).Frame().Intersects(frame)) {
		MoveTo(frame.LeftTop());
		ResizeTo(frame.Width(), frame.Height());
	}
	int32 tab = Settings::Default().CurrentTab();
	if (tab >= 0 && tab < fTabs->CountTabs())
		fTabs->Select(tab);

	_UpdateEngineControls();
	fEngineStartButton->SetEnabled(false);
		// until the first answer from the Engine

	BMessage poll(M_ENGINE_POLL);
	fEnginePoller = new BMessageRunner(BMessenger(this), &poll,
		kEnginePollInterval);
	PostMessage(M_ENGINE_POLL);
}


MainWindow::~MainWindow()
{
	delete fEnginePoller;
	delete fOpenPanel;
	delete fFolderPanel;
	_StopEvents();
}


BMenuBar* MainWindow::_BuildMenuBar()
{
	BMenuBar* menuBar = new BMenuBar("menu");

	BMenu* menu = new BMenu("Docker");
	menu->AddItem(new BMenuItem("About Docker Desktop…", new BMessage(kAbout)));
	menu->AddItem(new BMenuItem("Settings…", new BMessage(M_SHOW_SETTINGS),
		','));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Sign in to Docker Hub…",
		new BMessage(M_HUB_SIGN_IN)));
	fDeskbarItem = new BMenuItem("Show in Deskbar",
		new BMessage(M_TOGGLE_DESKBAR));
	menu->AddItem(fDeskbarItem);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(menu);

	menu = new BMenu("Engine");
	menu->AddItem(new BMenuItem("Start", new BMessage(M_ENGINE_START)));
	menu->AddItem(new BMenuItem("Stop", new BMessage(M_ENGINE_STOP)));
	menu->AddItem(new BMenuItem("Restart", new BMessage(M_ENGINE_RESTART)));
	menu->AddSeparatorItem();
	fStartAtBootItem = new BMenuItem("Start at boot",
		new BMessage(M_ENGINE_START_AT_BOOT));
	menu->AddItem(fStartAtBootItem);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Show Engine log",
		new BMessage(M_SHOW_ENGINE_LOG), 'L'));
	BMessage* engineSettings = new BMessage(M_SHOW_SETTINGS);
	engineSettings->AddInt32("tab", SettingsWindow::kEngineTab);
	menu->AddItem(new BMenuItem("Engine configuration (daemon.json)…",
		engineSettings));
	BMessage* troubleshoot = new BMessage(M_SHOW_SETTINGS);
	troubleshoot->AddInt32("tab", SettingsWindow::kTroubleshootTab);
	menu->AddItem(new BMenuItem("Troubleshoot…", troubleshoot));
	menuBar->AddItem(menu);

	menu = new BMenu("Containers");
	menu->AddItem(new BMenuItem("Run a container…",
		new BMessage(M_RUN_CONTAINER), 'N'));
	menu->AddItem(new BMenuItem("Compose up…", new BMessage(kOpenCompose)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Refresh", new BMessage(M_REFRESH_ALL), 'R'));
	menuBar->AddItem(menu);

	menu = new BMenu("Images");
	menu->AddItem(new BMenuItem("Pull an image…", new BMessage(M_IMAGE_PULL),
		'P'));
	menu->AddItem(new BMenuItem("Search Docker Hub…",
		new BMessage(M_HUB_SEARCH), 'K'));
	menu->AddItem(new BMenuItem("Build an image…",
		new BMessage(kOpenBuildFolder), 'B'));
	menu->AddItem(new BMenuItem("Load an image from a file…",
		new BMessage(kOpenImageFile)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Clean up…", new BMessage(M_SHOW_CLEANUP)));
	menuBar->AddItem(menu);

	menu = new BMenu("View");
	const char* tabs[] = { "Containers", "Images", "Volumes", "Networks",
		"Disk usage" };
	for (int32 i = 0; i < 5; i++) {
		BMessage* show = new BMessage(M_SHOW_TAB);
		show->AddInt32("tab", i);
		menu->AddItem(new BMenuItem(tabs[i], show, '1' + i));
	}
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Search", new BMessage(kFind), 'F'));
	menuBar->AddItem(menu);

	menu = new BMenu("Help");
	BMessage* url = new BMessage(M_OPEN_URL);
	url->AddString("url", "https://docs.docker.com/");
	menu->AddItem(new BMenuItem("Docker documentation", url));
	url = new BMessage(M_OPEN_URL);
	url->AddString("url",
		"https://github.com/airos-works/moby/blob/master/docs/airos.md");
	menu->AddItem(new BMenuItem("Docker on air/OS", url));
	url = new BMessage(M_OPEN_URL);
	url->AddString("url",
		"https://github.com/airos-works/docker-desktop/issues");
	menu->AddItem(new BMenuItem("Report an issue", url));
	menuBar->AddItem(menu);

	return menuBar;
}


void MainWindow::MenusBeginning()
{
	fDeskbarItem->SetMarked(DeskbarItem::IsInstalled());
	fStartAtBootItem->SetMarked(fStartAtBootCheck->Value() == B_CONTROL_ON);
	BMenu* engine = fStartAtBootItem->Menu();
	engine->ItemAt(0)->SetEnabled(!fEngineRunning && !fEngineStarting);
	engine->ItemAt(1)->SetEnabled(fEngineRunning && !fEngineStopping);
	engine->ItemAt(2)->SetEnabled(fEngineRunning && !fEngineStopping);
}


bool MainWindow::QuitRequested()
{
	Settings& settings = Settings::Default();
	settings.SetWindowFrame(Frame());
	settings.SetCurrentTab(fTabs->Selection());
	for (int32 i = 0; i < 4; i++)
		_ResourceView(i)->SaveState();
	settings.Save();

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


ResourceView* MainWindow::_ResourceView(int32 tab) const
{
	switch (tab) {
	case 0: return fContainersView;
	case 1: return fImagesView;
	case 2: return fVolumesView;
	case 3: return fNetworksView;
	default: return NULL;
	}
}


void MainWindow::_RefreshAll()
{
	for (int32 i = 0; i < 4; i++)
		_ResourceView(i)->Refresh();
	fDiskUsageView->Refresh();
}


void MainWindow::_TabChanged()
{
	if (fTabs->Selection() == 4)
		fDiskUsageView->Refresh();
}


// #pragma mark - The Engine


void MainWindow::_PollEngine()
{
	// A dockerd this window started that has exited again
	if (fEnginePid > 0) {
		int status;
		if (waitpid(fEnginePid, &status, WNOHANG) == fEnginePid) {
			fEnginePid = -1;
			if (fEngineStarting) {
				fEngineStarting = false;
				fEngineError.SetToFormat("dockerd stopped; see %s",
					EngineControl::LogPath().String());
				_UpdateEngineControls();
			}
		}
	}

	// A restart starts the Engine again once the old one is all gone.
	if (fRestartPending && !fEngineStopping && !fEngineRunning
		&& !EngineControl::IsAlive(fStoppedPid)) {
		fRestartPending = false;
		_StartEngine();
	}

	if (++fPollCount % kStatusRefreshPolls == 0 && fEngineRunning)
		fContainersView->Refresh();
	_UpdateCounts();

	if (fPinging)
		return;
	fPinging = true;
	std::thread(PingTaskRun, BMessenger(this), fSocketPath).detach();
}


void MainWindow::_HandleEngineStatus(BMessage* message)
{
	fPinging = false;
	// An answer about a socket no longer used
	if (fSocketPath != message->GetString("socket", ""))
		return;

	bool running = message->GetBool("running", false);
	if (fEngineChecked && running == fEngineRunning) {
		if (running && !fEvents)
			_StartEvents();
		return;
	}

	bool first = !fEngineChecked;
	fEngineChecked = true;
	fEngineRunning = running;
	if (running) {
		fEngineStarting = false;
		fEngineError = "";
		fStatus->SetText("Connected to the Docker Engine");
		_StartEvents();
		_FetchVersion();
	} else {
		_StopEvents();
		fVersion = "";
		fStatus->SetText("");
		if (fEngineStopping)
			fEngineStopping = false;
				// (a pending restart goes on in _PollEngine)
		else if (first && Settings::Default().StartEngineOnLaunch())
			_StartEngine();
	}

	for (int32 i = 0; i < 4; i++)
		_ResourceView(i)->SetEngineRunning(running);
	fDiskUsageView->SetEngineRunning(running);
	_UpdateEngineControls();
	_UpdateCounts();
}


void MainWindow::_StartEngine()
{
	if (fEngineRunning || fEngineStarting)
		return;

	pid_t pid;
	BString error;
	if (EngineControl::Start(&pid, &error) != B_OK) {
		fEngineError = error;
		_UpdateEngineControls();
		return;
	}
	fEnginePid = pid;
	fEngineStarting = true;
	fEngineError = "";
	_UpdateEngineControls();
}


void MainWindow::_StopEngine()
{
	if (!fEngineRunning || fEngineStopping)
		return;
	BString error;
	fStoppedPid = EngineControl::RunningPid();
	if (EngineControl::Stop(&error) != B_OK) {
		fRestartPending = false;
		Util::ShowError(error.String());
		return;
	}
	fEngineStopping = true;
	_UpdateEngineControls();
}


void MainWindow::_RestartEngine()
{
	if (!fEngineRunning) {
		_StartEngine();
		return;
	}
	fRestartPending = true;
	_StopEngine();
}


void MainWindow::_SetStartAtBoot(bool start)
{
	status_t status = EngineControl::SetStartsAtBoot(start);
	if (status != B_OK) {
		BString text;
		text.SetToFormat("The setting could not be saved: %s",
			strerror(status));
		Util::ShowError(text.String());
		start = !start;
	} else if (start) {
		// Turning it on means wanting the Engine: start it now, too.
		_StartEngine();
	}
	fStartAtBootCheck->SetValue(start ? B_CONTROL_ON : B_CONTROL_OFF);
	SettingsWindow::StartAtBootChanged(start);
}


void MainWindow::_UpdateEngineControls()
{
	rgb_color color;
	if (fRestartPending && !fEngineRunning) {
		fEngineState->SetText("Docker Engine is restarting…");
		color = StateColor("paused");
	} else if (fEngineStopping) {
		fEngineState->SetText(fRestartPending
			? "Docker Engine is restarting…" : "Docker Engine is stopping…");
		color = StateColor("paused");
	} else if (fEngineRunning) {
		fEngineState->SetText("Docker Engine is running");
		color = StateColor("running");
	} else if (fEngineStarting) {
		fEngineState->SetText("Docker Engine is starting…");
		color = StateColor("paused");
	} else if (fEngineError.Length() > 0) {
		BString text("Docker Engine is stopped: ");
		text << fEngineError;
		fEngineState->SetText(text.String());
		color = StateColor("dead");
	} else {
		fEngineState->SetText(fEngineChecked ? "Docker Engine is stopped"
			: "Checking the Docker Engine…");
		color = StateColor("exited");
	}
	// As wide as its text (it only gets cut short when the window is too
	// narrow); a little more than the text's width keeps the last letters
	// from being truncated over a fraction of a pixel.
	float width = ceilf(fEngineState->StringWidth(fEngineState->Text())) + 4;
	fEngineState->SetExplicitPreferredSize(BSize(width, B_SIZE_UNSET));
	fEngineState->SetExplicitMaxSize(BSize(width, B_SIZE_UNSET));
	fEngineDot->SetColor(color);
	fEngineVersion->SetText(fEngineRunning ? fVersion.String() : "");

	bool busy = fEngineStarting || fEngineStopping || fRestartPending;
	fEngineStartButton->SetLabel(fEngineRunning ? "Stop Engine"
		: "Start Engine");
	fEngineStartButton->SetEnabled(fEngineChecked && !busy);
	fEngineRestartButton->SetEnabled(fEngineRunning && !busy);
}


void MainWindow::_FetchVersion()
{
	BString socket = fSocketPath;
	Util::RunTask(BMessenger(this), kVersion, [socket](BMessage& reply) {
		DockerClient client(socket.String());
		BString json;
		if (client.Get("/version", &json) == B_OK)
			reply.AddString("json", json);
	});
}


void MainWindow::_StartEvents()
{
	_StopEvents();
	CancelerRef canceler(new StreamCanceler);
	fEvents = canceler;
	int32 generation = ++fEventsGeneration;
	BMessenger target(this);
	BString socket = fSocketPath;
	std::thread([socket, canceler, target, generation]() {
		DockerClient client(socket.String());
		LineSplitter lines;
		client.Stream("GET", "/events", NULL, canceler,
			[&](const char* data, size_t length) {
				return lines.Feed(data, length, [&](const BString& line) {
					djson::Value* root = djson::Parse(line.String());
					BMessage event(M_ENGINE_EVENT);
					event.AddString("type", djson::GetString(root, "Type"));
					event.AddString("action", djson::GetString(root,
						"Action"));
					djson::FreeValue(root);
					target.SendMessage(&event);
					return !canceler->IsCancelled();
				});
			});
		BMessage ended(M_EVENTS_ENDED);
		ended.AddInt32("generation", generation);
		target.SendMessage(&ended);
	}).detach();
}


void MainWindow::_StopEvents()
{
	if (fEvents) {
		fEvents->Cancel();
		fEvents.reset();
	}
}


void MainWindow::_HandleEvent(BMessage* message)
{
	BString type = message->GetString("type", "");
	BString action = message->GetString("action", "");
	// Execs and health checks run in containers all the time without
	// changing what the lists show.
	if (action.StartsWith("exec_") || action.StartsWith("health_status")
		|| action == "top" || action == "attach" || action == "resize")
		return;

	if (type == "container") {
		fContainersView->ScheduleRefresh();
		// What is in use
		fImagesView->ScheduleRefresh();
		fVolumesView->ScheduleRefresh();
	} else if (type == "image")
		fImagesView->ScheduleRefresh();
	else if (type == "volume")
		fVolumesView->ScheduleRefresh();
	else if (type == "network")
		fNetworksView->ScheduleRefresh();
	else
		return;
	if (fTabs->Selection() == 4) {
		BMessage refresh(M_REFRESH);
		BMessenger(fDiskUsageView).SendMessage(&refresh);
	}
}


void MainWindow::_SocketChanged()
{
	fSocketPath = Settings::Default().SocketPath();
	_StopEvents();
	fEngineChecked = false;
	fEngineRunning = false;
	for (int32 i = 0; i < 4; i++)
		_ResourceView(i)->SetEngineRunning(false);
	fDiskUsageView->SetEngineRunning(false);
	_UpdateEngineControls();
	_PollEngine();
}


void MainWindow::_UpdateCounts()
{
	if (!fEngineRunning) {
		fCounts->SetText("");
		return;
	}
	BString text;
	text.SetToFormat("%" B_PRId32 " running · %" B_PRId32 " containers · "
		"%" B_PRId32 " images (%s)", fContainersView->CountRunning(),
		fContainersView->CountAll(), fImagesView->CountImages(),
		Util::FormatSize(fImagesView->TotalSize()).String());
	fCounts->SetText(text.String());
}


// #pragma mark - Other windows


void MainWindow::_ShowEngineLog()
{
	BString path = EngineControl::LogPath();
	TextWindow* window = new TextWindow("Docker Engine log", this);
	window->SetLoader([path](BString& text, BString& error) {
		BFile file(path.String(), B_READ_ONLY);
		status_t status = file.InitCheck();
		if (status != B_OK) {
			error.SetToFormat("%s: %s", path.String(), strerror(status));
			return status;
		}
		// The last 512 kB are plenty.
		off_t size = 0;
		file.GetSize(&size);
		off_t start = size > 512 * 1024 ? size - 512 * 1024 : 0;
		char* buffer = text.LockBuffer(size - start + 1);
		ssize_t read = file.ReadAt(start, buffer, size - start);
		text.UnlockBuffer(read > 0 ? read : 0);
		if (start > 0) {
			int32 newline = text.FindFirst('\n');
			if (newline >= 0)
				text.Remove(0, newline + 1);
		}
		return B_OK;
	}, 2000000);
	window->Show();
}


void MainWindow::_ShowAbout()
{
	BAboutWindow* about = new BAboutWindow("Docker Desktop",
		"application/x-vnd.airos.docker-desktop");
	about->AddDescription("A native Docker Desktop for air/OS: manage the "
		"containers, images, volumes and networks of the Docker Engine, "
		"follow logs, pull and run images, search Docker Hub and keep disk "
		"usage in check.\n\nDocker and the Docker logo are trademarks of "
		"Docker, Inc.");
	about->AddCopyright(2026, "air/OS contributors");
	about->Show();
}


void MainWindow::_OpenPanel(uint32 purpose)
{
	BMessage message(B_REFS_RECEIVED);
	message.AddInt32("purpose", purpose);
	if (purpose == kOpenBuildFolder) {
		if (fFolderPanel == NULL) {
			fFolderPanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL,
				B_DIRECTORY_NODE, false, NULL, NULL, false, true);
		}
		fFolderPanel->SetTarget(BMessenger(this));
		fFolderPanel->SetMessage(&message);
		fFolderPanel->Window()->SetTitle(
			"Docker Desktop: choose the folder with the Dockerfile");
		fFolderPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Build");
		fFolderPanel->Show();
		return;
	}
	if (fOpenPanel == NULL) {
		fOpenPanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL, B_FILE_NODE,
			false, NULL, NULL, false, true);
	}
	fOpenPanel->SetTarget(BMessenger(this));
	fOpenPanel->SetMessage(&message);
	if (purpose == kOpenCompose) {
		fOpenPanel->Window()->SetTitle(
			"Docker Desktop: choose a compose file");
		fOpenPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Compose up");
	} else {
		fOpenPanel->Window()->SetTitle(
			"Docker Desktop: choose an image archive (.tar)");
		fOpenPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Load");
	}
	fOpenPanel->Show();
}


void MainWindow::_RefsReceived(BMessage* message)
{
	entry_ref ref;
	if (message->FindRef("refs", &ref) != B_OK)
		return;
	BPath path(&ref);
	if (path.InitCheck() != B_OK)
		return;
	BString docker = Util::DockerCommand(fSocketPath.String());

	switch (message->GetInt32("purpose", 0)) {
	case kOpenCompose:
	{
		BString command(docker);
		command << " compose -f " << Util::ShellQuote(path.Path())
			<< " up -d";
		BString title("Compose up: ");
		title << path.Leaf();
		Util::RunInTerminal(title.String(), command.String(), true);
		break;
	}
	case kOpenImageFile:
	{
		BString command(docker);
		command << " load -i " << Util::ShellQuote(path.Path());
		BString title("Load ");
		title << path.Leaf();
		Util::RunInTerminal(title.String(), command.String(), true);
		break;
	}
	case kOpenBuildFolder:
	{
		BMessage reply(kBuildTagged);
		reply.AddString("path", path.Path());
		BString name(path.Leaf());
		name.ToLower();
		name << ":latest";
		PromptWindow* prompt = new PromptWindow("Build an image",
			"Image name:", name.String(), "Build", BMessenger(this), reply,
			this);
		prompt->AddField("dockerfile", "Dockerfile:", "Dockerfile",
			"The Dockerfile's name, in the chosen folder");
		prompt->Show();
		break;
	}
	}
}


// #pragma mark -


void MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case M_ENGINE_POLL:
		_PollEngine();
		break;

	case M_ENGINE_STATUS:
		_HandleEngineStatus(message);
		break;

	case kEngineAction:
		if (fEngineRunning) {
			if (Util::Confirm("Stop the Docker Engine? All running "
					"containers stop with it.", "Stop Engine"))
				_StopEngine();
		} else
			_StartEngine();
		break;
	case M_ENGINE_START:
		_StartEngine();
		break;
	case M_ENGINE_STOP:
		_StopEngine();
		break;
	case M_ENGINE_RESTART:
		_RestartEngine();
		break;

	case M_ENGINE_START_AT_BOOT:
	{
		// From the settings window, the menu item (which toggles) or the
		// check box (which already did).
		bool start;
		if (message->FindBool("start", &start) != B_OK) {
			void* source = NULL;
			message->FindPointer("source", &source);
			if (source == fStartAtBootItem)
				start = !fStartAtBootItem->IsMarked();
			else
				start = fStartAtBootCheck->Value() == B_CONTROL_ON;
		}
		_SetStartAtBoot(start);
		break;
	}

	case kVersion:
	{
		djson::Value* root = djson::Parse(message->GetString("json", "{}"));
		fVersion = "";
		if (root != NULL) {
			fVersion.SetToFormat("%s · API %s · %s/%s",
				djson::GetString(root, "Version", "?"),
				djson::GetString(root, "ApiVersion", "?"),
				djson::GetString(root, "Os", "?"),
				djson::GetString(root, "Arch", "?"));
			Util::SetEngineOS(djson::GetString(root, "Os"));
			djson::FreeValue(root);
		}
		_UpdateEngineControls();
		break;
	}

	case M_ENGINE_EVENT:
		_HandleEvent(message);
		break;

	case M_EVENTS_ENDED:
	{
		if (fEvents
			&& message->GetInt32("generation", -1) == fEventsGeneration)
			fEvents.reset();
			// the next poll starts it again if the Engine still runs
		break;
	}

	case M_SOCKET_PATH_CHANGED:
		_SocketChanged();
		break;

	case M_SET_STATUS:
		fStatus->SetText(message->GetString("text", ""));
		break;

	case M_LIST_UPDATED:
		_UpdateCounts();
		break;

	case M_REFRESH:
	case M_REFRESH_ALL:
		_RefreshAll();
		break;

	case kTabChanged:
		_TabChanged();
		break;

	case M_SHOW_TAB:
	{
		int32 tab;
		if (message->FindInt32("tab", &tab) == B_OK)
			fTabs->Select(tab);
		break;
	}

	case kFind:
	{
		ResourceView* view = _ResourceView(fTabs->Selection());
		if (view != NULL)
			view->FocusSearch();
		break;
	}

	case M_SHOW_CLEANUP:
		fTabs->Select(4);
		break;

	case M_SHOW_SETTINGS:
		SettingsWindow::Open(this, message->GetInt32("tab",
			SettingsWindow::kGeneralTab));
		break;

	case M_SHOW_ENGINE_LOG:
		_ShowEngineLog();
		break;

	case M_HUB_SIGN_IN:
	{
		BString command(Util::DockerCommand(fSocketPath.String()));
		command << " login";
		Util::RunInTerminal("Sign in to Docker Hub", command.String(), true);
		break;
	}

	case M_RUN_CONTAINER:
		(new RunWindow("", this))->Show();
		break;
	case M_IMAGE_PULL:
		(new PullWindow("", this))->Show();
		break;
	case M_HUB_SEARCH:
		HubSearchWindow::Open(this);
		break;

	case kOpenCompose:
	case kOpenBuildFolder:
	case kOpenImageFile:
		_OpenPanel(message->what);
		break;

	case B_REFS_RECEIVED:
		_RefsReceived(message);
		break;

	case kBuildTagged:
	{
		BString path = message->GetString("path", "");
		BString tag = message->GetString("text", "");
		BString dockerfile = message->GetString("dockerfile", "Dockerfile");
		tag.Trim();
		dockerfile.Trim();
		BString command("cd ");
		command << Util::ShellQuote(path.String()) << " && "
			<< Util::DockerCommand(fSocketPath.String()) << " build";
		if (tag.Length() > 0)
			command << " -t " << Util::ShellQuote(tag.String());
		if (dockerfile.Length() > 0 && dockerfile != "Dockerfile")
			command << " -f " << Util::ShellQuote(dockerfile.String());
		command << " .";
		BString title("Build ");
		title << (tag.Length() > 0 ? tag.String() : path.String());
		Util::RunInTerminal(title.String(), command.String(), true);
		break;
	}

	case M_OPEN_URL:
		Util::OpenURL(message->GetString("url", ""));
		break;

	case M_TOGGLE_DESKBAR:
	{
		bool show = message->GetBool("show", !DeskbarItem::IsInstalled());
		status_t status = show ? DeskbarItem::Install()
			: DeskbarItem::Remove();
		if (status != B_OK) {
			BString text;
			text.SetToFormat("The Deskbar item could not be %s: %s",
				show ? "added" : "removed", strerror(status));
			Util::ShowError(text.String());
		}
		SettingsWindow::DeskbarChanged();
		break;
	}

	case kAbout:
		_ShowAbout();
		break;

	default:
		BWindow::MessageReceived(message);
		break;
	}
}
