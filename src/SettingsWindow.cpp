// SPDX-License-Identifier: MIT

#include "SettingsWindow.h"

#include "DeskbarView.h"
#include "DockerClient.h"
#include "EngineControl.h"
#include "Json.h"
#include "Messages.h"
#include "Settings.h"
#include "Util.h"

#include <Autolock.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <TextView.h>

#include <math.h>
#include <string.h>

namespace {

enum {
	kSocketChanged = 'swsk',
	kDefaultSocket = 'swds',
	kStartOnLaunch = 'swsl',
	kStartAtBoot = 'swsb',
	kConfirmDeletes = 'swcd',
	kSignIn = 'swsi',
	kSignOut = 'swso',
	kRefreshAccounts = 'swra',
	kSaveConfig = 'swsc',
	kApplyConfig = 'swac',
	kRevertConfig = 'swrc',
	kRestartEngine = 'swre',
	kStopEngine = 'swse',
	kStartEngine = 'swst',
	kShowLog = 'swsg',
	kPurge = 'swpu',
	kPurged = 'swpd',
	kResetSettings = 'swrs',
	kActivate = 'swat',
	kShowInDeskbar = 'swdb',
	kDeskbarChanged = 'swdc',
};

BLocker sLock("settings window");
BMessenger sWindow;

const char* kDefaultDaemonConfig = "{\n}\n";

// A wrapped, read-only text of at most lines lines.
BView* Note(const char* text, int lines = 2)
{
	BTextView* view = new BTextView("note");
	view->SetText(text);
	view->MakeEditable(false);
	view->MakeSelectable(false);
	view->SetWordWrap(true);
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	view->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	rgb_color color = ui_color(B_PANEL_TEXT_COLOR);
	view->SetFontAndColor(be_plain_font, B_FONT_ALL, &color);
	font_height height;
	be_plain_font->GetHeight(&height);
	float lineHeight = ceilf(height.ascent + height.descent + height.leading);
	view->SetExplicitMinSize(BSize(300, lineHeight * lines));
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		lineHeight * lines + 4));
	return view;
}

} // namespace


void SettingsWindow::Open(BWindow* mainWindow, int32 tab)
{
	BAutolock lock(sLock);
	if (sWindow.IsValid()) {
		BMessage activate(kActivate);
		activate.AddInt32("tab", tab);
		sWindow.SendMessage(&activate);
		return;
	}
	SettingsWindow* window = new SettingsWindow(mainWindow);
	sWindow = BMessenger(window);
	window->fTabs->Select(tab);
	window->Show();
}


void SettingsWindow::StartAtBootChanged(bool start)
{
	BAutolock lock(sLock);
	if (!sWindow.IsValid())
		return;
	BMessage update(M_ENGINE_START_AT_BOOT);
	update.AddBool("start", start);
	sWindow.SendMessage(&update);
}


void SettingsWindow::DeskbarChanged()
{
	BAutolock lock(sLock);
	if (sWindow.IsValid())
		sWindow.SendMessage(kDeskbarChanged);
}


SettingsWindow::SettingsWindow(BWindow* mainWindow)
	:
	BWindow(BRect(0, 0, 560, 480), "Docker Desktop settings",
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fMainWindow(mainWindow)
{
	Settings& settings = Settings::Default();

	// General
	fSocket = new BTextControl("socket", "Docker Engine socket:",
		settings.SocketPath().String(), new BMessage(kSocketChanged));
	fSocket->SetToolTip("The unix socket of the Engine API (like DOCKER_HOST "
		"without unix://). Press Enter to apply.");
	fStartOnLaunch = new BCheckBox("startOnLaunch",
		"Start the Docker Engine when Docker Desktop opens",
		new BMessage(kStartOnLaunch));
	fStartOnLaunch->SetValue(settings.StartEngineOnLaunch());
	fStartAtBoot = new BCheckBox("startAtBoot",
		"Start the Docker Engine at boot", new BMessage(kStartAtBoot));
	fStartAtBoot->SetValue(EngineControl::StartsAtBoot());
	fConfirmDeletes = new BCheckBox("confirmDeletes",
		"Ask before deleting containers, images, volumes and networks",
		new BMessage(kConfirmDeletes));
	fConfirmDeletes->SetValue(settings.ConfirmDeletes());
	fShowInDeskbar = new BCheckBox("showInDeskbar",
		"Show Docker Desktop in the Deskbar (the Engine's state, and a menu "
		"to start and stop it)", new BMessage(kShowInDeskbar));
	fShowInDeskbar->SetValue(DeskbarItem::IsInstalled());

	fAccounts = new BStringView("accounts", "");
	Util::MakeElastic(fAccounts);
	BBox* hubBox = new BBox("hub");
	hubBox->SetLabel("Docker Hub and registries");
	BLayoutBuilder::Group<>(hubBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fAccounts)
		.AddGroup(B_HORIZONTAL)
			.Add(new BButton("signIn", "Sign in…", new BMessage(kSignIn)))
			.Add(new BButton("signOut", "Sign out…", new BMessage(kSignOut)))
			.AddGlue()
			.Add(new BButton("refreshAccounts", "Refresh",
				new BMessage(kRefreshAccounts)))
		.End()
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);

	BView* general = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fSocket)
			.Add(new BButton("defaultSocket", "Default",
				new BMessage(kDefaultSocket)))
		.End()
		.Add(fStartOnLaunch)
		.Add(fStartAtBoot)
		.Add(fConfirmDeletes)
		.Add(fShowInDeskbar)
		.Add(hubBox)
		.AddGlue()
		.SetInsets(B_USE_WINDOW_SPACING)
		.View();
	general->SetName("General");

	// Docker Engine
	fDaemonConfig = new BTextView("daemonConfig");
	fDaemonConfig->SetFontAndColor(be_fixed_font);
	fDaemonConfig->SetInsets(6, 4, 6, 4);
	fDaemonConfig->SetWordWrap(false);
	fDaemonStatus = new BStringView("daemonStatus", "");
	Util::MakeElastic(fDaemonStatus);
	BString configNote;
	configNote.SetToFormat("The Engine's configuration, %s, in JSON: for "
		"example registry mirrors, insecure registries, log options or the "
		"data root. The Engine reads it when it starts.",
		EngineControl::ConfigPath().String());
	BView* engine = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(Note(configNote.String(), 3))
		.Add(new BScrollView("configScroll", fDaemonConfig, 0, true, true))
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fDaemonStatus)
			.Add(new BButton("revert", "Revert", new BMessage(kRevertConfig)))
			.Add(new BButton("save", "Save", new BMessage(kSaveConfig)))
			.Add(new BButton("apply", "Apply & restart",
				new BMessage(kApplyConfig)))
		.End()
		.SetInsets(B_USE_WINDOW_SPACING)
		.View();
	engine->SetName("Docker Engine");

	// Troubleshoot
	fTroubleStatus = new BStringView("troubleStatus", "");
	Util::MakeElastic(fTroubleStatus);
	fPurgeButton = new BButton("purge", "Clean / Purge data…",
		new BMessage(kPurge));
	BString logNote("The Engine writes its log to ");
	logNote << EngineControl::LogPath() << ".";
	BView* troubleshoot = BLayoutBuilder::Group<>(B_VERTICAL,
			B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
			.Add(new BButton("restart", "Restart Docker Engine",
				new BMessage(kRestartEngine)), 0, 0)
			.Add(Note("Stops the Engine and starts it again; containers "
				"stop unless they have a restart policy."), 1, 0)
			.Add(new BButton("start", "Start Docker Engine",
				new BMessage(kStartEngine)), 0, 1)
			.Add(new BButton("stop", "Stop Docker Engine",
				new BMessage(kStopEngine)), 0, 2)
			.Add(Note("Stops the Engine, and with it all containers."), 1, 2)
			.Add(new BButton("log", "Show Engine log",
				new BMessage(kShowLog)), 0, 3)
			.Add(Note(logNote.String()), 1, 3)
			.Add(fPurgeButton, 0, 4)
			.Add(Note("Deletes all containers, images, volumes, networks and "
				"the build cache."), 1, 4)
			.Add(new BButton("reset", "Reset Docker Desktop settings",
				new BMessage(kResetSettings)), 0, 5)
			.Add(Note("Window layout, socket and options go back to their "
				"defaults. The Engine is not affected."), 1, 5)
		.End()
		.Add(fTroubleStatus)
		.AddGlue()
		.SetInsets(B_USE_WINDOW_SPACING)
		.View();
	troubleshoot->SetName("Troubleshoot");
	// The buttons as wide as the widest
	for (int32 i = 0; BView* child = troubleshoot->ChildAt(i); i++) {
		if (dynamic_cast<BButton*>(child) != NULL)
			child->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	}

	fTabs = new BTabView("tabs", B_WIDTH_FROM_LABEL);
	fTabs->AddTab(general);
	fTabs->AddTab(engine);
	fTabs->AddTab(troubleshoot);
	fTabs->SetBorder(B_NO_BORDER);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fTabs);

	_LoadDaemonConfig();
	_UpdateAccounts();
	Util::CenterOver(this, mainWindow);
}


SettingsWindow::~SettingsWindow()
{
	Settings::Default().Save();
	BAutolock lock(sLock);
	sWindow = BMessenger();
}


void SettingsWindow::_LoadDaemonConfig()
{
	BFile file(EngineControl::ConfigPath().String(), B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK) {
		fDaemonConfig->SetText(kDefaultDaemonConfig);
		fDaemonStatus->SetText("No configuration file yet: the defaults "
			"apply.");
		return;
	}
	BString text;
	char* buffer = text.LockBuffer(size + 1);
	ssize_t read = file.Read(buffer, size);
	text.UnlockBuffer(read > 0 ? read : 0);
	fDaemonConfig->SetText(text.String());
	fDaemonStatus->SetText("");
}


void SettingsWindow::_SaveDaemonConfig(bool restart)
{
	BString text(fDaemonConfig->Text());
	BString error;
	if (!djson::Validate(text.String(), &error)) {
		BString message("This is not valid JSON: ");
		message << error << ".";
		fDaemonStatus->SetText(message.String());
		return;
	}

	BPath path(EngineControl::ConfigPath().String());
	BPath parent;
	path.GetParent(&parent);
	create_directory(parent.Path(), 0755);
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status == B_OK) {
		if (!text.EndsWith("\n"))
			text << "\n";
		ssize_t written = file.Write(text.String(), text.Length());
		if (written != text.Length())
			status = written < 0 ? written : B_IO_ERROR;
	}
	if (status != B_OK) {
		BString message;
		message.SetToFormat("%s could not be saved: %s", path.Path(),
			strerror(status));
		Util::ShowError(message.String());
		return;
	}

	if (restart) {
		fDaemonStatus->SetText("Saved; restarting the Engine…");
		fMainWindow.SendMessage(M_ENGINE_RESTART);
	} else
		fDaemonStatus->SetText("Saved; it applies when the Engine starts "
			"again.");
}


void SettingsWindow::_UpdateAccounts()
{
	// The registries the docker CLI has credentials for.
	BPath path;
	BString text("Not signed in.");
	if (find_directory(B_USER_DIRECTORY, &path) == B_OK
		&& path.Append(".docker/config.json") == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		off_t size;
		if (file.InitCheck() == B_OK && file.GetSize(&size) == B_OK
			&& size < 1024 * 1024) {
			BString json;
			char* buffer = json.LockBuffer(size + 1);
			ssize_t read = file.Read(buffer, size);
			json.UnlockBuffer(read > 0 ? read : 0);
			djson::Value* root = djson::Parse(json.String());
			BString registries;
			for (const djson::Object* member = djson::Members(
					djson::Get(root, "auths")); member != NULL;
					member = member->next) {
				BString name(member->key);
				if (name.FindFirst("index.docker.io") >= 0)
					name = "Docker Hub";
				if (registries.Length() > 0)
					registries << ", ";
				registries << name;
			}
			djson::FreeValue(root);
			if (registries.Length() > 0)
				text.SetToFormat("Signed in to %s.", registries.String());
		}
	}
	fAccounts->SetText(text.String());
}


void SettingsWindow::_Purge()
{
	if (!Util::Confirm("Delete ALL containers (running ones too), images, "
			"volumes with their data, networks and the build cache?\n\nThis "
			"cannot be undone.", "Purge data"))
		return;
	fPurgeButton->SetEnabled(false);
	fTroubleStatus->SetText("Deleting all data…");
	BString socket = Settings::Default().SocketPath();
	Util::RunTask(BMessenger(this), kPurged, [socket](BMessage& reply) {
		DockerClient client(socket.String());
		BString json, error;
		if (client.Get("/containers/json?all=1", &json, &error) != B_OK) {
			reply.AddString("error", error);
			return;
		}
		djson::Value* root = djson::Parse(json.String());
		for (int32 i = 0; i < djson::Length(root); i++) {
			BString id = djson::GetString(djson::At(root, i), "Id");
			if (client.ContainerAction(id.String(), "remove", &error) != B_OK)
				reply.AddString("error", error);
		}
		djson::FreeValue(root);
		const char* prunes[] = {
			"/images/prune?filters=%7B%22dangling%22%3A%5B%22false%22%5D%7D",
			"/volumes/prune?filters=%7B%22all%22%3A%5B%22true%22%5D%7D",
			"/networks/prune",
			"/build/prune?all=1",
		};
		for (const char* path : prunes) {
			if (client.Call("POST", path, NULL, NULL, &error) != B_OK)
				reply.AddString("error", error);
		}
	});
}


void SettingsWindow::MessageReceived(BMessage* message)
{
	Settings& settings = Settings::Default();
	switch (message->what) {
	case kActivate:
	{
		int32 tab;
		if (message->FindInt32("tab", &tab) == B_OK)
			fTabs->Select(tab);
		Activate();
		break;
	}

	case kSocketChanged:
	case kDefaultSocket:
	{
		if (message->what == kDefaultSocket)
			fSocket->SetText(kDefaultSocketPath);
		BString path(fSocket->Text());
		path.Trim();
		if (path.Length() == 0) {
			path = kDefaultSocketPath;
			fSocket->SetText(path.String());
		}
		if (path != settings.SocketPath()) {
			settings.SetSocketPath(path.String());
			settings.Save();
			fMainWindow.SendMessage(M_SOCKET_PATH_CHANGED);
		}
		break;
	}

	case kStartOnLaunch:
		settings.SetStartEngineOnLaunch(
			fStartOnLaunch->Value() == B_CONTROL_ON);
		settings.Save();
		break;

	case kStartAtBoot:
	{
		BMessage change(M_ENGINE_START_AT_BOOT);
		change.AddBool("start", fStartAtBoot->Value() == B_CONTROL_ON);
		fMainWindow.SendMessage(&change);
		break;
	}

	case kShowInDeskbar:
	{
		BMessage toggle(M_TOGGLE_DESKBAR);
		toggle.AddBool("show", fShowInDeskbar->Value() == B_CONTROL_ON);
		fMainWindow.SendMessage(&toggle);
		break;
	}
	case kDeskbarChanged:
		fShowInDeskbar->SetValue(DeskbarItem::IsInstalled());
		break;

	case kConfirmDeletes:
		settings.SetConfirmDeletes(fConfirmDeletes->Value() == B_CONTROL_ON);
		settings.Save();
		break;

	case kSignIn:
	{
		BString command(Util::DockerCommand(settings.SocketPath().String()));
		command << " login";
		Util::RunInTerminal("Sign in to Docker Hub", command.String(), true);
		break;
	}
	case kSignOut:
	{
		BString command(Util::DockerCommand(settings.SocketPath().String()));
		command << " logout";
		Util::RunInTerminal("Sign out of Docker Hub", command.String(), true);
		break;
	}
	case kRefreshAccounts:
		_UpdateAccounts();
		break;

	case kSaveConfig:
		_SaveDaemonConfig(false);
		break;
	case kApplyConfig:
		_SaveDaemonConfig(true);
		break;
	case kRevertConfig:
		_LoadDaemonConfig();
		break;

	case kRestartEngine:
		fMainWindow.SendMessage(M_ENGINE_RESTART);
		fTroubleStatus->SetText("Restarting the Docker Engine…");
		break;
	case kStartEngine:
		fMainWindow.SendMessage(M_ENGINE_START);
		break;
	case kStopEngine:
		if (Util::Confirm("Stop the Docker Engine? All running containers "
				"stop with it.", "Stop Engine")) {
			fMainWindow.SendMessage(M_ENGINE_STOP);
			fTroubleStatus->SetText("Stopping the Docker Engine…");
		}
		break;
	case kShowLog:
		fMainWindow.SendMessage(M_SHOW_ENGINE_LOG);
		break;

	case kPurge:
		_Purge();
		break;
	case kPurged:
	{
		fPurgeButton->SetEnabled(true);
		BString errors, error;
		for (int32 i = 0; message->FindString("error", i, &error) == B_OK;
				i++) {
			if (errors.Length() > 0)
				errors << "\n";
			errors << error;
		}
		if (errors.Length() > 0) {
			fTroubleStatus->SetText("Some data could not be deleted.");
			Util::ShowError(errors.String());
		} else
			fTroubleStatus->SetText("All data was deleted.");
		fMainWindow.SendMessage(M_REFRESH_ALL);
		break;
	}

	case kResetSettings:
		if (Util::Confirm("Reset Docker Desktop's settings to their "
				"defaults?", "Reset")) {
			settings.SetSocketPath(kDefaultSocketPath);
			settings.SetStartEngineOnLaunch(false);
			settings.SetConfirmDeletes(true);
			settings.SetOnlyRunning(false);
			settings.Save();
			fSocket->SetText(kDefaultSocketPath);
			fStartOnLaunch->SetValue(B_CONTROL_OFF);
			fConfirmDeletes->SetValue(B_CONTROL_ON);
			fMainWindow.SendMessage(M_SOCKET_PATH_CHANGED);
			fTroubleStatus->SetText("Settings were reset.");
		}
		break;

	case M_ENGINE_START_AT_BOOT:
		// The main window tells which way the switch went.
		fStartAtBoot->SetValue(message->GetBool("start", false));
		break;

	default:
		BWindow::MessageReceived(message);
		break;
	}
}
