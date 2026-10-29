// SPDX-License-Identifier: MIT

#include "ContainerWindow.h"

#include "Columns.h"
#include "Json.h"
#include "Settings.h"
#include "Util.h"

#include <Autolock.h>
#include <Button.h>
#include <CheckBox.h>
#include <GridLayoutBuilder.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <MessageRunner.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TabView.h>
#include <TextView.h>

#include <map>
#include <thread>

#include <ctype.h>
#include <stdlib.h>

namespace {

enum {
	kPoll = 'cwpl',
	kState = 'cwst',
	kLogData = 'cwld',
	kLogEnded = 'cwle',
	kStatsData = 'cwsd',
	kStatsEnded = 'cwse',
	kInspectLoaded = 'cwil',
	kActionDone = 'cwad',
	kStartStop = 'cwss',
	kRestart = 'cwrs',
	kPause = 'cwpa',
	kTerminal = 'cwte',
	kBrowser = 'cwbr',
	kDelete = 'cwde',
	kFollow = 'cwfo',
	kTimestamps = 'cwts',
	kClear = 'cwcl',
	kCopyLogs = 'cwcp',
	kRefreshInspect = 'cwri',
	kSelectTab = 'cwtb',
};

const bigtime_t kPollInterval = 2000000;
// The log view keeps this much text, dropping the oldest.
const int32 kMaxLogLength = 4 * 1024 * 1024;

BLocker sWindowsLock("container windows");
std::map<BString, BMessenger> sWindows;

} // namespace


void ContainerWindow::Open(const char* id, const char* name, int32 tab,
	BWindow* parent)
{
	{
		BAutolock lock(sWindowsLock);
		auto it = sWindows.find(id);
		if (it != sWindows.end() && it->second.IsValid()) {
			BMessage select(kSelectTab);
			select.AddInt32("tab", tab);
			it->second.SendMessage(&select);
			return;
		}
	}
	ContainerWindow* window = new ContainerWindow(id, name, parent);
	{
		BAutolock lock(sWindowsLock);
		sWindows[id] = BMessenger(window);
	}
	window->fTabs->Select(tab);
	window->Show();
}


ContainerWindow::ContainerWindow(const char* id, const char* name,
	BWindow* parent)
	:
	BWindow(BRect(0, 0, 760, 560), name, B_DOCUMENT_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fId(id),
	fName(name),
	fSocket(Settings::Default().SocketPath()),
	fPort(-1),
	fTty(false),
	fLogsEnded(0),
	fLogGeneration(0),
	fPoller(NULL),
	fPolling(false),
	fBusy(false),
	fGone(false)
{
	fTitle = new BStringView("title", name);
	BFont font(be_bold_font);
	font.SetSize(font.Size() * 1.4);
	fTitle->SetFont(&font);
	fSubtitle = new BStringView("subtitle", Util::ShortId(id).String());
	fSubtitle->SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);
	fStatus = new BStringView("status", "");
	Util::MakeElastic(fStatus);

	fStartStopButton = new BButton("startStop", "Stop",
		new BMessage(kStartStop));
	fRestartButton = new BButton("restart", "Restart", new BMessage(kRestart));
	fPauseButton = new BButton("pause", "Pause", new BMessage(kPause));
	fTerminalButton = new BButton("terminal", "Terminal",
		new BMessage(kTerminal));
	fTerminalButton->SetToolTip("Open a shell in the container");
	fBrowserButton = new BButton("browser", "Open in browser",
		new BMessage(kBrowser));
	fDeleteButton = new BButton("delete", "Delete", new BMessage(kDelete));

	// Logs
	fLogView = new BTextView("logs");
	fLogView->MakeEditable(false);
	fLogView->SetStylable(true);
	fLogView->SetWordWrap(true);
	fLogView->SetFontAndColor(be_fixed_font);
	fLogView->SetInsets(6, 4, 6, 4);
	fFollow = new BCheckBox("follow", "Follow", new BMessage(kFollow));
	fFollow->SetValue(B_CONTROL_ON);
	fFollow->SetToolTip("Keep showing new output as it comes");
	fTimestamps = new BCheckBox("timestamps", "Timestamps",
		new BMessage(kTimestamps));
	BView* logsTab = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(new BScrollView("logScroll", fLogView, 0, false, true))
		.AddGroup(B_HORIZONTAL)
			.Add(fFollow)
			.Add(fTimestamps)
			.AddGlue()
			.Add(new BButton("copy", "Copy", new BMessage(kCopyLogs)))
			.Add(new BButton("clear", "Clear", new BMessage(kClear)))
		.End()
		.SetInsets(B_USE_SMALL_SPACING)
		.View();
	logsTab->SetName("Logs");

	// Inspect
	fInspectView = new BTextView("inspect");
	fInspectView->MakeEditable(false);
	fInspectView->SetWordWrap(false);
	fInspectView->SetFontAndColor(be_fixed_font);
	fInspectView->SetInsets(6, 4, 6, 4);
	BView* inspectTab = BLayoutBuilder::Group<>(B_VERTICAL,
			B_USE_SMALL_SPACING)
		.Add(new BScrollView("inspectScroll", fInspectView, 0, true, true))
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("refreshInspect", "Refresh",
				new BMessage(kRefreshInspect)))
		.End()
		.SetInsets(B_USE_SMALL_SPACING)
		.View();
	inspectTab->SetName("Inspect");

	// Stats
	fCpu = new BStringView("cpu", "–");
	fMemory = new BStringView("memory", "–");
	fNetwork = new BStringView("network", "–");
	fBlock = new BStringView("block", "–");
	fPids = new BStringView("pids", "–");
	fStatsNote = new BStringView("note", "");
	Util::MakeElastic(fStatsNote);
	fStatsNote->SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);
	fStatsNote->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	// (The glue lets the window grow wider than the string views.)
	BView* statsTab = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(new BStringView("cpuLabel", "CPU usage:"), 0, 0)
			.Add(fCpu, 1, 0)
			.Add(new BStringView("memLabel", "Memory usage:"), 0, 1)
			.Add(fMemory, 1, 1)
			.Add(new BStringView("netLabel", "Network I/O:"), 0, 2)
			.Add(fNetwork, 1, 2)
			.Add(new BStringView("blockLabel", "Disk I/O:"), 0, 3)
			.Add(fBlock, 1, 3)
			.Add(new BStringView("pidsLabel", "Processes:"), 0, 4)
			.Add(fPids, 1, 4)
		.End()
		.AddGlue()
		.End()
		.Add(fStatsNote)
		.AddGlue()
		.SetInsets(B_USE_WINDOW_SPACING)
		.View();
	statsTab->SetName("Stats");

	fTabs = new BTabView("tabs", B_WIDTH_FROM_LABEL);
	fTabs->AddTab(logsTab);
	fTabs->AddTab(inspectTab);
	fTabs->AddTab(statsTab);
	fTabs->SetBorder(B_NO_BORDER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(fTitle)
				.Add(fSubtitle)
				.AddGlue()
			.End()
			.Add(fStatus)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(fStartStopButton)
				.Add(fRestartButton)
				.Add(fPauseButton)
				.Add(fTerminalButton)
				.Add(fBrowserButton)
				.AddGlue()
				.Add(fDeleteButton)
			.End()
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
				B_USE_WINDOW_SPACING, B_USE_SMALL_SPACING)
		.End()
		.Add(fTabs);

	_UpdateButtons();
	Util::CenterOver(this, parent);
	MoveBy(30, 30);

	BMessage poll(kPoll);
	fPoller = new BMessageRunner(BMessenger(this), &poll, kPollInterval);
	_Poll();
	_LoadInspect();
}


ContainerWindow::~ContainerWindow()
{
	delete fPoller;
	_StopLogs();
	_StopStats();
	BAutolock lock(sWindowsLock);
	sWindows.erase(fId);
}


bool ContainerWindow::QuitRequested()
{
	return true;
}


void ContainerWindow::_Poll()
{
	if (fPolling || fGone)
		return;
	fPolling = true;
	BString socket = fSocket, id = fId;
	Util::RunTask(BMessenger(this), kState, [socket, id](BMessage& reply) {
		DockerClient client(socket.String());
		BString json, error;
		BString path("/containers/");
		path << id << "/json";
		if (client.Get(path.String(), &json, &error) == B_OK)
			reply.AddString("json", json);
		else
			reply.AddString("error", error);
	});
}


void ContainerWindow::_StateReceived(BMessage* message)
{
	fPolling = false;
	BString json, error;
	if (message->FindString("json", &json) != B_OK) {
		message->FindString("error", &error);
		if (error.IFindFirst("No such container") >= 0) {
			fGone = true;
			fState = "removed";
			fStatus->SetText("This container has been deleted.");
			_StopLogs();
			_StopStats();
			_UpdateButtons();
		} else
			fStatus->SetText(error.String());
		return;
	}

	djson::Value* root = djson::Parse(json.String());
	if (root == NULL)
		return;
	const djson::Value* state = djson::Get(root, "State");
	BString newState = djson::GetString(state, "Status");
	BString name = djson::GetString(root, "Name");
	if (name.StartsWith("/"))
		name.Remove(0, 1);
	if (name.Length() > 0 && name != fName) {
		fName = name;
		fTitle->SetText(name.String());
		SetTitle(name.String());
	}
	const djson::Value* config = djson::Get(root, "Config");
	fImage = djson::GetString(config, "Image");
	fTty = djson::GetBool(config, "Tty");

	BString subtitle(fImage);
	subtitle << "  ·  " << Util::ShortId(fId.String());
	fSubtitle->SetText(subtitle.String());

	// The first published host port.
	fPort = -1;
	const djson::Value* ports = djson::Get(djson::Get(root, "NetworkSettings"),
		"Ports");
	for (const djson::Object* member = djson::Members(ports);
			member != NULL && fPort < 0; member = member->next) {
		const djson::Value* bindings = member->value;
		for (int32 i = 0; i < djson::Length(bindings); i++) {
			int32 port = atoi(djson::GetString(djson::At(bindings, i),
				"HostPort"));
			if (port > 0) {
				fPort = port;
				break;
			}
		}
	}

	BString status;
	time_t started = Util::ParseTime(djson::GetString(state, "StartedAt"));
	time_t finished = Util::ParseTime(djson::GetString(state, "FinishedAt"));
	if (newState == "running") {
		status << "Running";
		if (started > 0)
			status << " · started " << Util::FormatAge(started).ToLower();
	} else if (newState == "exited") {
		status.SetToFormat("Exited (%d)",
			(int)djson::GetNumber(state, "ExitCode"));
		if (finished > 0)
			status << " · " << Util::FormatAge(finished).ToLower();
		BString stateError = djson::GetString(state, "Error");
		if (stateError.Length() > 0)
			status << " · " << stateError;
	} else {
		status = newState;
		if (status.Length() > 0)
			status.SetByteAt(0, toupper(status[0]));
	}
	fStatus->SetText(status.String());
	djson::FreeValue(root);

	bool wasRunning = fState == "running";
	bool firstTime = fState.Length() == 0;
	// A restart between two polls shows only as a new start time.
	BString startedAt = djson::GetString(state, "StartedAt");
	bool restarted = !firstTime && startedAt != fStartedAt;
	fStartedAt = startedAt;
	fState = newState;
	_UpdateButtons();

	if (firstTime) {
		_StartLogs(true);
		_StartStats();
	} else if (fState == "running" && (!wasRunning || restarted)) {
		// It runs again: pick up its new output and usage.
		if (!fLogStream && fFollow->Value() == B_CONTROL_ON)
			_StartLogs(false);
		_StartStats();
	}
}


void ContainerWindow::_UpdateButtons()
{
	bool running = fState == "running";
	bool paused = fState == "paused";
	fStartStopButton->SetLabel(running || paused ? "Stop" : "Start");
	fStartStopButton->SetEnabled(!fBusy && !fGone && fState.Length() > 0);
	fRestartButton->SetEnabled(!fBusy && !fGone && fState.Length() > 0);
	fPauseButton->SetLabel(paused ? "Resume" : "Pause");
	fPauseButton->SetEnabled(!fBusy && (running || paused));
	fTerminalButton->SetEnabled(running);
	fBrowserButton->SetEnabled(running && fPort > 0);
	if (fPort > 0) {
		BString tip;
		tip.SetToFormat("http://localhost:%" B_PRId32 "/", fPort);
		fBrowserButton->SetToolTip(tip.String());
	}
	fDeleteButton->SetEnabled(!fBusy && !fGone);
}


void ContainerWindow::_StartLogs(bool clear)
{
	_StopLogs();
	if (fGone)
		return;
	if (clear) {
		fLogView->SetText("");
		fLogsEnded = 0;
	}

	bool follow = fFollow->Value() == B_CONTROL_ON;
	BString path;
	path.SetToFormat("/containers/%s/logs?stdout=1&stderr=1%s%s",
		fId.String(), follow ? "&follow=1" : "",
		fTimestamps->Value() == B_CONTROL_ON ? "&timestamps=1" : "");
	if (fLogsEnded > 0)
		path << "&since=" << (int64)fLogsEnded;
	else
		path << "&tail=2000";

	CancelerRef canceler(new StreamCanceler);
	fLogStream = canceler;
	int32 generation = ++fLogGeneration;
	BMessenger target(this);
	BString socket = fSocket;
	std::thread([socket, path, canceler, target, generation]() {
		DockerClient client(socket.String());
		LogDemuxer demuxer;
		BString error;
		status_t status = client.Stream("GET", path.String(), NULL, canceler,
			[&](const char* data, size_t length) {
				BMessage message(kLogData);
				demuxer.Feed(data, length,
					[&](int stream, const char* text, size_t textLength) {
						message.AddInt32("stream", stream);
						message.AddData("text", B_RAW_TYPE, text, textLength,
							false);
					});
				if (!message.IsEmpty())
					target.SendMessage(&message);
				return !canceler->IsCancelled();
			}, &error);
		BMessage ended(kLogEnded);
		ended.AddInt32("generation", generation);
		if (status != B_OK && status != B_CANCELED && !canceler->IsCancelled())
			ended.AddString("error", error);
		target.SendMessage(&ended);
	}).detach();
}


void ContainerWindow::_StopLogs()
{
	if (fLogStream) {
		fLogStream->Cancel();
		fLogStream.reset();
	}
}


void ContainerWindow::_AppendLog(BMessage* message)
{
	BScrollBar* bar = fLogView->ScrollBar(B_VERTICAL);
	bool atEnd = true;
	if (bar != NULL) {
		float min, max;
		bar->GetRange(&min, &max);
		atEnd = bar->Value() >= max - 2;
	}

	rgb_color normal = ui_color(B_DOCUMENT_TEXT_COLOR);
	rgb_color error = make_color(200, 50, 40);
	if (normal.IsDark() == false)
		error = make_color(255, 120, 110);

	const void* data;
	ssize_t length;
	int32 stream;
	for (int32 i = 0; message->FindData("text", B_RAW_TYPE, i, &data,
			&length) == B_OK; i++) {
		if (message->FindInt32("stream", i, &stream) != B_OK)
			stream = 1;
		text_run_array runs;
		runs.count = 1;
		runs.runs[0].offset = 0;
		runs.runs[0].font = *be_fixed_font;
		runs.runs[0].color = stream == 2 ? error : normal;
		fLogView->Insert(fLogView->TextLength(), (const char*)data, length,
			&runs);
	}

	// Drop the oldest output, at a line break.
	if (fLogView->TextLength() > kMaxLogLength) {
		int32 cut = fLogView->TextLength() - kMaxLogLength * 3 / 4;
		const char* text = fLogView->Text();
		while (cut < fLogView->TextLength() && text[cut] != '\n')
			cut++;
		fLogView->Delete(0, cut + 1);
	}

	if (atEnd) {
		fLogView->Select(fLogView->TextLength(), fLogView->TextLength());
		fLogView->ScrollToSelection();
	}
}


void ContainerWindow::_StartStats()
{
	_StopStats();
	if (fGone || fState != "running")
		return;
	BString path("/containers/");
	path << fId << "/stats?stream=1";
	CancelerRef canceler(new StreamCanceler);
	fStatsStream = canceler;
	BMessenger target(this);
	BString socket = fSocket;
	std::thread([socket, path, canceler, target]() {
		DockerClient client(socket.String());
		LineSplitter lines;
		client.Stream("GET", path.String(), NULL, canceler,
			[&](const char* data, size_t length) {
				return lines.Feed(data, length, [&](const BString& line) {
					BMessage message(kStatsData);
					message.AddString("json", line);
					target.SendMessage(&message);
					return !canceler->IsCancelled();
				});
			});
		BMessage ended(kStatsEnded);
		target.SendMessage(&ended);
	}).detach();
}


void ContainerWindow::_StopStats()
{
	if (fStatsStream) {
		fStatsStream->Cancel();
		fStatsStream.reset();
	}
}


void ContainerWindow::_StatsReceived(BMessage* message)
{
	djson::Value* root = djson::Parse(message->GetString("json", ""));
	if (root == NULL)
		return;

	// The air/OS Engine does not measure containers (yet): its stats are
	// all zero, with no read time.
	BString read = djson::GetString(root, "read");
	if (read.StartsWith("0001-")) {
		fStatsNote->SetText("This Docker Engine does not report resource "
			"usage of containers.");
		djson::FreeValue(root);
		return;
	}
	fStatsNote->SetText("");

	const djson::Value* cpu = djson::Get(root, "cpu_stats");
	const djson::Value* precpu = djson::Get(root, "precpu_stats");
	double cpuDelta = djson::GetNumber(djson::Get(cpu, "cpu_usage"),
			"total_usage")
		- djson::GetNumber(djson::Get(precpu, "cpu_usage"), "total_usage");
	double systemDelta = djson::GetNumber(cpu, "system_cpu_usage")
		- djson::GetNumber(precpu, "system_cpu_usage");
	double cpus = djson::GetNumber(cpu, "online_cpus", 1);
	BString text;
	if (systemDelta > 0 && cpuDelta >= 0) {
		text.SetToFormat("%.2f %% (of %.0f CPUs)",
			cpuDelta / systemDelta * cpus * 100, cpus);
		fCpu->SetText(text.String());
	}

	const djson::Value* memory = djson::Get(root, "memory_stats");
	double usage = djson::GetNumber(memory, "usage");
	double cache = djson::GetNumber(djson::Get(memory, "stats"),
		"inactive_file", djson::GetNumber(djson::Get(memory, "stats"),
			"cache"));
	double limit = djson::GetNumber(memory, "limit");
	text = Util::FormatSize(usage - cache);
	if (limit > 0)
		text << " / " << Util::FormatSize(limit);
	fMemory->SetText(text.String());

	double rx = 0, tx = 0;
	for (const djson::Object* network = djson::Members(
			djson::Get(root, "networks")); network != NULL;
			network = network->next) {
		rx += djson::GetNumber(network->value, "rx_bytes");
		tx += djson::GetNumber(network->value, "tx_bytes");
	}
	text = Util::FormatSize(rx);
	text << " received / " << Util::FormatSize(tx) << " sent";
	fNetwork->SetText(text.String());

	double readBytes = 0, writeBytes = 0;
	const djson::Value* io = djson::Get(djson::Get(root, "blkio_stats"),
		"io_service_bytes_recursive");
	for (int32 i = 0; i < djson::Length(io); i++) {
		const djson::Value* entry = djson::At(io, i);
		BString op = djson::GetString(entry, "op");
		if (op.ICompare("read") == 0)
			readBytes += djson::GetNumber(entry, "value");
		else if (op.ICompare("write") == 0)
			writeBytes += djson::GetNumber(entry, "value");
	}
	text = Util::FormatSize(readBytes);
	text << " read / " << Util::FormatSize(writeBytes) << " written";
	fBlock->SetText(text.String());

	text.SetToFormat("%.0f", djson::GetNumber(djson::Get(root, "pids_stats"),
		"current"));
	fPids->SetText(text.String());
	djson::FreeValue(root);
}


void ContainerWindow::_LoadInspect()
{
	BString socket = fSocket, id = fId;
	Util::RunTask(BMessenger(this), kInspectLoaded,
		[socket, id](BMessage& reply) {
			DockerClient client(socket.String());
			BString json, error;
			BString path("/containers/");
			path << id << "/json";
			if (client.Get(path.String(), &json, &error) == B_OK)
				reply.AddString("text", djson::Pretty(json.String()));
			else
				reply.AddString("text", error);
		});
}


void ContainerWindow::_Action(const char* action, const char* status)
{
	fBusy = true;
	_UpdateButtons();
	BString text(status);
	text << "…";
	fStatus->SetText(text.String());
	BString socket = fSocket, id = fId, verb = action;
	Util::RunTask(BMessenger(this), kActionDone,
		[socket, id, verb](BMessage& reply) {
			DockerClient client(socket.String());
			BString error;
			if (client.ContainerAction(id.String(), verb.String(), &error)
					!= B_OK)
				reply.AddString("error", error);
			reply.AddString("action", verb);
		});
}


void ContainerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kPoll:
		_Poll();
		break;
	case kState:
		_StateReceived(message);
		break;

	case kLogData:
		_AppendLog(message);
		break;
	case kLogEnded:
	{
		if (fLogStream
			&& message->GetInt32("generation", -1) == fLogGeneration) {
			fLogStream.reset();
			fLogsEnded = time(NULL);
		}
		BString error;
		if (message->FindString("error", &error) == B_OK) {
			BString text("\n[");
			text << error << "]\n";
			fLogView->Insert(fLogView->TextLength(), text.String(),
				text.Length());
		}
		break;
	}

	case kStatsData:
		_StatsReceived(message);
		break;
	case kStatsEnded:
		break;

	case kInspectLoaded:
		fInspectView->SetText(message->GetString("text", ""));
		break;
	case kRefreshInspect:
		_LoadInspect();
		break;

	case kActionDone:
	{
		fBusy = false;
		BString error;
		if (message->FindString("error", &error) == B_OK)
			Util::ShowError(error.String());
		if (BString(message->GetString("action", "")) == "remove") {
			if (error.Length() == 0) {
				Quit();
				return;
			}
		}
		_UpdateButtons();
		_Poll();
		_LoadInspect();
		break;
	}

	case kStartStop:
		if (fState == "running" || fState == "paused")
			_Action("stop", "Stopping");
		else
			_Action("start", "Starting");
		break;
	case kRestart:
		_Action("restart", "Restarting");
		break;
	case kPause:
		if (fState == "paused")
			_Action("unpause", "Resuming");
		else
			_Action("pause", "Pausing");
		break;
	case kDelete:
	{
		BString text;
		text.SetToFormat("Delete the container \"%s\"? A running container "
			"is stopped first. This cannot be undone.", fName.String());
		if (!Settings::Default().ConfirmDeletes()
			|| Util::Confirm(text.String(), "Delete")) {
			_StopLogs();
			_StopStats();
			_Action("remove", "Deleting");
		}
		break;
	}
	case kTerminal:
	{
		BString command = Util::ShellCommand(fSocket.String(),
			fId.String());
		BString title(fName);
		title << " (container)";
		if (Util::RunInTerminal(title.String(), command.String(), false)
				!= B_OK)
			Util::ShowError("Terminal could not be started.");
		break;
	}
	case kBrowser:
		if (fPort > 0) {
			BString url;
			url.SetToFormat("http://localhost:%" B_PRId32 "/", fPort);
			Util::OpenURL(url.String());
		}
		break;

	case kFollow:
		if (fFollow->Value() == B_CONTROL_ON)
			_StartLogs(true);
		else
			_StopLogs();
		break;
	case kTimestamps:
		_StartLogs(true);
		break;
	case kClear:
		fLogView->SetText("");
		break;
	case kCopyLogs:
		Util::CopyToClipboard(fLogView->Text());
		break;

	case kSelectTab:
	{
		int32 tab;
		if (message->FindInt32("tab", &tab) == B_OK)
			fTabs->Select(tab);
		Activate();
		break;
	}

	default:
		BWindow::MessageReceived(message);
		break;
	}
}
