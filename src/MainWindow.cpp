// SPDX-License-Identifier: MIT

#include "MainWindow.h"

#include "DockerClient.h"
#include "EngineControl.h"
#include "Json.h"
#include "Messages.h"

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MessageRunner.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TabView.h>

#include <thread>

#include <string.h>
#include <sys/wait.h>

static const char* kDefaultSocket
	= "/var/run/docker.sock";

// How often the Engine bar asks the Engine whether it is up.
static const bigtime_t kEnginePollInterval = 2000000;

namespace {

struct ListTask {
	BMessenger target;
	BString socketPath;
	bool images; // false: containers, true: images
};

struct ActionTask {
	BMessenger target;
	BString socketPath;
	BString id;
	BString action;
	bool isImage;
};

void ListTaskRun(ListTask* task)
{
	DockerClient client(task->socketPath);
	BString json;
	status_t status = task->images ? client.ListImages(&json)
		: client.ListContainers(true, &json);

	BMessage message(M_DOCKER_TASK_RESULT);
	message.AddBool("images", task->images);
	message.AddInt32("status", (int32)status);
	if (status == B_OK)
		message.AddString("json", json);
	task->target.SendMessage(&message);
	delete task;
}

void PingTaskRun(BMessenger target, BString socketPath)
{
	DockerClient client(socketPath);
	BString version;
	BMessage message(M_ENGINE_STATUS);
	message.AddBool("running", client.Ping(&version) == B_OK);
	target.SendMessage(&message);
}

void ActionTaskRun(ActionTask* task)
{
	DockerClient client(task->socketPath);
	status_t status;
	if (task->isImage)
		status = client.ImageRemove(task->id);
	else
		status = client.ContainerAction(task->id, task->action);

	BMessage message(M_DOCKER_TASK_RESULT);
	message.AddBool("refresh", true);
	message.AddInt32("status", (int32)status);
	task->target.SendMessage(&message);
	delete task;
}

} // namespace

MainWindow::MainWindow()
	:
	BWindow(BRect(80, 60, 780, 600), "Docker Desktop",
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fMessenger(this),
	fSocketPath(kDefaultSocket),
	fBusy(false),
	fPinging(false),
	fEngineChecked(false),
	fEngineRunning(false),
	fEngineStarting(false),
	fEnginePid(-1)
{
	fContainerIds = new BList(20);
	fImageIds = new BList(20);

	fTabs = new BTabView("tabs");
	fContainers = new BListView("containers", B_SINGLE_SELECTION_LIST);
	fImages = new BListView("images", B_SINGLE_SELECTION_LIST);
	fContainers->SetSelectionMessage(new BMessage(M_CONTAINER_SELECTED));
	fImages->SetSelectionMessage(new BMessage(M_IMAGE_SELECTED));

	BScrollView* containerScroll = new BScrollView("scroll", fContainers,
		B_WILL_DRAW | B_FRAME_EVENTS, false, true);
	BScrollView* imageScroll = new BScrollView("scroll2", fImages,
		B_WILL_DRAW | B_FRAME_EVENTS, false, true);

	BView* containerTab = BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(containerScroll)
		.View();
	containerTab->SetName("Containers");
	BView* imageTab = BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(imageScroll)
		.View();
	imageTab->SetName("Images");
	fTabs->AddTab(containerTab);
	fTabs->AddTab(imageTab);

	fStartButton = new BButton("start", "Start",
		new BMessage(M_CONTAINER_START));
	fStopButton = new BButton("stop", "Stop",
		new BMessage(M_CONTAINER_STOP));
	fRestartButton = new BButton("restart", "Restart",
		new BMessage(M_CONTAINER_RESTART));
	fRemoveButton = new BButton("remove", "Remove",
		new BMessage(M_CONTAINER_REMOVE));

	fStatus = new BStringView("status", "Not connected");
	fStatus->SetAlignment(B_ALIGN_LEFT);

	// The Engine bar, above everything else: whether dockerd is up, a
	// button to start it now and the switch for starting it at boot.
	fEngineState = new BStringView("engineState", "Checking the Docker Engine…");
	fEngineState->SetFont(be_bold_font);
	fEngineState->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fEngineStartButton = new BButton("engineStart", "Start Engine",
		new BMessage(M_ENGINE_START));
	fStartAtBootCheck = new BCheckBox("startAtBoot",
		"Start the Docker Engine at boot",
		new BMessage(M_ENGINE_START_AT_BOOT));
	fStartAtBootCheck->SetValue(EngineControl::StartsAtBoot()
		? B_CONTROL_ON : B_CONTROL_OFF);

	BBox* engineBox = new BBox("engine");
	engineBox->SetLabel("Docker Engine");
	BView* engineGroup = BLayoutBuilder::Group<>(B_VERTICAL, 4)
		.AddGroup(B_HORIZONTAL, 8)
			.Add(fEngineState)
			.Add(fEngineStartButton)
		.End()
		.Add(fStartAtBootCheck)
		.SetInsets(8, 4, 8, 6)
		.View();
	engineBox->AddChild(engineGroup);

	BBox* actionBar = new BBox("actions");
	BLayoutBuilder::Group<>(actionBar, B_HORIZONTAL, 4)
		.Add(fStartButton)
		.Add(fStopButton)
		.Add(fRestartButton)
		.Add(fRemoveButton)
		.Add(new BButton("refresh", "Refresh", new BMessage(M_REFRESH)))
		.SetInsets(6, 4, 6, 4);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 6)
		.Add(engineBox)
		.Add(fTabs)
		.Add(actionBar)
		.Add(fStatus)
		.SetInsets(8, 8, 8, 8);

	_UpdateButtons();
	fEngineStartButton->SetEnabled(false);
		// until the first answer from the Engine

	BMessage poll(M_ENGINE_POLL);
	fEnginePoller = new BMessageRunner(fMessenger, &poll,
		kEnginePollInterval);
	PostMessage(M_ENGINE_POLL);
}

MainWindow::~MainWindow()
{
	delete fEnginePoller;
	// Items and id strings are owned by the list views / lists.
	for (int32 i = 0; i < fContainerIds->CountItems(); i++)
		delete (BString*)fContainerIds->ItemAt(i);
	delete fContainerIds;
	for (int32 i = 0; i < fImageIds->CountItems(); i++)
		delete (BString*)fImageIds->ItemAt(i);
	delete fImageIds;
}

bool MainWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

int32 MainWindow::_SelectedContainerIndex()
{
	return fContainers->CurrentSelection();
}

int32 MainWindow::_SelectedImageIndex()
{
	return fImages->CurrentSelection();
}

void MainWindow::_UpdateButtons()
{
	bool container = _SelectedContainerIndex() >= 0;
	bool image = _SelectedImageIndex() >= 0;
	fStartButton->SetEnabled(container && !fBusy);
	fStopButton->SetEnabled(container && !fBusy);
	fRestartButton->SetEnabled(container && !fBusy);
	fRemoveButton->SetEnabled((container || image) && !fBusy);
}

void MainWindow::_RefreshLists()
{
	if (fBusy)
		return;
	fBusy = true;
	fStatus->SetText("Connecting to Docker Engine…");
	_UpdateButtons();

	ListTask* containers = new ListTask{fMessenger, fSocketPath, false};
	std::thread(ListTaskRun, containers).detach();
	ListTask* images = new ListTask{fMessenger, fSocketPath, true};
	std::thread(ListTaskRun, images).detach();
}

void MainWindow::_HandleListResult(BMessage* message)
{
	bool images = false;
	message->FindBool("images", &images);

	BString json;
	if (message->FindString("json", &json) != B_OK) {
		fStatus->SetText("No response from Docker Engine");
		return;
	}

	djson::Value* root = djson::Parse(json.String());
	if (root == NULL) {
		fStatus->SetText("Failed to parse Engine response");
		return;
	}

	if (images) {
		fImages->MakeEmpty();
		for (int32 i = 0; i < fImageIds->CountItems(); i++)
			delete (BString*)fImageIds->ItemAt(i);
		fImageIds->MakeEmpty();
		for (int32 i = 0; i < djson::Length(root); i++) {
			const djson::Value* img = djson::At(root, i);
			BString tags(djson::GetString(img, "RepoTags"));
			tags.ReplaceAll("[\"", "");
			tags.ReplaceAll("\"]", "");
			if (tags.Length() == 0)
				tags = djson::GetString(img, "Id");
			BString line;
			line.SetToFormat("%s   %.1f MB", tags.String(),
				djson::GetNumber(img, "Size") / 1048576.0);
			fImages->AddItem(new BStringItem(line));
			fImageIds->AddItem(new BString(djson::GetString(img, "Id")));
		}
	} else {
		fContainers->MakeEmpty();
		for (int32 i = 0; i < fContainerIds->CountItems(); i++)
			delete (BString*)fContainerIds->ItemAt(i);
		fContainerIds->MakeEmpty();
		for (int32 i = 0; i < djson::Length(root); i++) {
			const djson::Value* c = djson::At(root, i);
			BString names(djson::GetString(c, "Names"));
			names.ReplaceAll("[\"", "");
			names.ReplaceAll("\"]", "");
			if (names.Length() == 0)
				names = djson::GetString(c, "Id");
			BString line;
			line.SetToFormat("%s    %s    %s", names.String(),
				djson::GetString(c, "Image"),
				djson::GetString(c, "State"));
			fContainers->AddItem(new BStringItem(line));
			fContainerIds->AddItem(
				new BString(djson::GetString(c, "Id")));
		}
	}
	djson::FreeValue(root);

	fBusy = false;
	fStatus->SetText("Connected");
	_UpdateButtons();
}

void MainWindow::_RunAction(const char* id, const char* action, bool isImage)
{
	if (id == NULL || fBusy)
		return;
	ActionTask* task = new ActionTask{fMessenger, fSocketPath, id, action,
		isImage};
	std::thread(ActionTaskRun, task).detach();
	fBusy = true;
	fStatus->SetText("Talking to Engine…");
	_UpdateButtons();
}

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

	if (fPinging)
		return;
	fPinging = true;
	std::thread(PingTaskRun, fMessenger, fSocketPath).detach();
}

void MainWindow::_HandleEngineStatus(BMessage* message)
{
	fPinging = false;
	bool running = message->GetBool("running", false);
	if (fEngineChecked && running == fEngineRunning)
		return;

	fEngineChecked = true;
	fEngineRunning = running;
	if (running) {
		fEngineStarting = false;
		fEngineError = "";
		_RefreshLists();
	} else
		fStatus->SetText("Docker Engine is not running");
	_UpdateEngineControls();
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

void MainWindow::_SetStartAtBoot(bool start)
{
	status_t status = EngineControl::SetStartsAtBoot(start);
	if (status != B_OK) {
		BString text;
		text.SetToFormat("The setting could not be saved: %s",
			strerror(status));
		(new BAlert("Docker Desktop", text.String(), "OK", NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
		fStartAtBootCheck->SetValue(start ? B_CONTROL_OFF : B_CONTROL_ON);
		return;
	}

	// Turning it on means wanting the Engine: start it now, too.
	if (start)
		_StartEngine();
}

void MainWindow::_UpdateEngineControls()
{
	if (fEngineRunning)
		fEngineState->SetText("Docker Engine is running");
	else if (fEngineStarting)
		fEngineState->SetText("Docker Engine is starting…");
	else if (fEngineError.Length() > 0) {
		BString text("Docker Engine is stopped: ");
		text << fEngineError;
		fEngineState->SetText(text.String());
	} else
		fEngineState->SetText("Docker Engine is stopped");

	fEngineStartButton->SetEnabled(!fEngineRunning && !fEngineStarting);
}

void MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case M_ENGINE_POLL:
		_PollEngine();
		break;

	case M_ENGINE_STATUS:
		_HandleEngineStatus(message);
		break;

	case M_ENGINE_START:
		_StartEngine();
		break;

	case M_ENGINE_START_AT_BOOT:
		_SetStartAtBoot(fStartAtBootCheck->Value() == B_CONTROL_ON);
		break;

	case M_REFRESH:
		_RefreshLists();
		break;

	case M_DOCKER_TASK_RESULT:
	{
		status_t status = B_ERROR;
		message->FindInt32("status", &status);
		bool refresh = message->GetBool("refresh", false);
		if (refresh) {
			if (status == B_OK)
				_RefreshLists();
			else {
				fBusy = false;
				fStatus->SetText("Engine action failed");
				_UpdateButtons();
			}
		} else if (status == B_OK)
			_HandleListResult(message);
		else {
			fBusy = false;
			fStatus->SetText(fEngineRunning ? "No response from Docker Engine"
				: "Docker Engine is not running; start it above");
			_UpdateButtons();
		}
		break;
	}

	case M_CONTAINER_SELECTED:
	case M_IMAGE_SELECTED:
		_UpdateButtons();
		break;

	case M_CONTAINER_START:
	{
		int32 index = _SelectedContainerIndex();
		if (index >= 0) {
			BString* id = (BString*)fContainerIds->ItemAt(index);
			_RunAction(id->String(), "start", false);
		}
		break;
	}

	case M_CONTAINER_STOP:
	{
		int32 index = _SelectedContainerIndex();
		if (index >= 0) {
			BString* id = (BString*)fContainerIds->ItemAt(index);
			_RunAction(id->String(), "stop", false);
		}
		break;
	}

	case M_CONTAINER_RESTART:
	{
		int32 index = _SelectedContainerIndex();
		if (index >= 0) {
			BString* id = (BString*)fContainerIds->ItemAt(index);
			_RunAction(id->String(), "restart", false);
		}
		break;
	}

	case M_CONTAINER_REMOVE:
	{
		int32 index = _SelectedContainerIndex();
		if (index >= 0) {
			BString* id = (BString*)fContainerIds->ItemAt(index);
			_RunAction(id->String(), "remove", false);
		}
		break;
	}

	case M_IMAGE_REMOVE:
	{
		int32 index = _SelectedImageIndex();
		if (index >= 0) {
			BString* id = (BString*)fImageIds->ItemAt(index);
			_RunAction(id->String(), "remove", true);
		}
		break;
	}

	default:
		BWindow::MessageReceived(message);
		break;
	}
}
