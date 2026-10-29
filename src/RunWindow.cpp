// SPDX-License-Identifier: MIT

#include "RunWindow.h"

#include "ContainerWindow.h"
#include "Json.h"
#include "Pull.h"
#include "Settings.h"
#include "Util.h"

#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <map>
#include <thread>
#include <vector>

namespace {

enum {
	kRun = 'rwrn',
	kCancel = 'rwcn',
	kProgress = 'rwpg',
	kDone = 'rwdn',
	kInteractive = 'rwit',
};

const char* kRestartPolicies[] = { "no", "always", "unless-stopped",
	"on-failure" };
const char* kRestartLabels[] = { "Never", "Always", "Unless stopped",
	"On failure" };

// Splits a command line into words, honouring quotes and backslashes.
std::vector<BString> SplitWords(const char* text)
{
	std::vector<BString> words;
	BString word;
	bool inWord = false;
	char quote = 0;
	for (const char* p = text; *p != '\0'; p++) {
		char c = *p;
		if (quote != 0) {
			if (c == quote)
				quote = 0;
			else if (c == '\\' && quote == '"' && p[1] != '\0')
				word.Append(++p, 1);
			else
				word.Append(&c, 1);
		} else if (c == '\'' || c == '"') {
			quote = c;
			inWord = true;
		} else if (c == '\\' && p[1] != '\0') {
			word.Append(++p, 1);
			inWord = true;
		} else if (c == ' ' || c == '\t' || c == '\n') {
			if (inWord) {
				words.push_back(word);
				word = "";
				inWord = false;
			}
		} else {
			word.Append(&c, 1);
			inWord = true;
		}
	}
	if (inWord)
		words.push_back(word);
	return words;
}

// The non-empty lines (or comma separated items) of text.
std::vector<BString> Items(const char* text, bool commas)
{
	std::vector<BString> items;
	BString item;
	for (const char* p = text;; p++) {
		if (*p == '\0' || *p == '\n' || (commas && *p == ',')) {
			item.Trim();
			if (item.Length() > 0)
				items.push_back(item);
			item = "";
			if (*p == '\0')
				break;
		} else
			item.Append(p, 1);
	}
	return items;
}

BTextView* MakeTextArea(const char* name, BScrollView** scroll)
{
	BTextView* view = new BTextView(name);
	view->SetFontAndColor(be_fixed_font);
	view->SetInsets(4, 2, 4, 2);
	*scroll = new BScrollView(name, view, 0, false, true);
	float lineHeight = be_fixed_font->Size() * 1.4f;
	(*scroll)->SetExplicitMinSize(BSize(B_SIZE_UNSET, lineHeight * 3 + 8));
	(*scroll)->SetExplicitPreferredSize(BSize(B_SIZE_UNSET,
		lineHeight * 3 + 8));
	return view;
}

} // namespace


RunWindow::RunWindow(const char* image, BWindow* parent)
	:
	BWindow(BRect(0, 0, 520, 480), "Run a new container", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
	fImage = new BTextControl("image", "Image:", image, NULL);
	fName = new BTextControl("name", "Container name:", "", NULL);
	fName->SetToolTip("Leave empty for a random name");
	fPorts = new BTextControl("ports", "Ports:", "", NULL);
	fPorts->SetToolTip("Host port : container port, comma separated; "
		"for example 8080:80, 8443:443/tcp");
	fCommand = new BTextControl("command", "Command:", "", NULL);
	fCommand->SetToolTip("Leave empty for the image's own command");

	BPopUpMenu* restartMenu = new BPopUpMenu("restart");
	for (size_t i = 0; i < sizeof(kRestartPolicies) / sizeof(char*); i++) {
		BMenuItem* item = new BMenuItem(kRestartLabels[i], NULL);
		restartMenu->AddItem(item);
		if (i == 0)
			item->SetMarked(true);
	}
	fRestart = new BMenuField("restart", "Restart:", restartMenu);

	BScrollView* volumesScroll;
	BScrollView* environmentScroll;
	fVolumes = MakeTextArea("volumes", &volumesScroll);
	fVolumes->SetToolTip("One per line: host path or volume name : path in "
		"the container[:ro]\nfor example /boot/home/site:/usr/share/nginx/html");
	fEnvironment = MakeTextArea("environment", &environmentScroll);
	fEnvironment->SetToolTip("One per line: NAME=value");

	fAutoRemove = new BCheckBox("autoRemove",
		"Remove the container when it exits", NULL);
	fInteractive = new BCheckBox("interactive",
		"Run interactively in a Terminal window", new BMessage(kInteractive));
	fInteractive->SetToolTip("Like docker run -it: for shells and other "
		"programs that read the keyboard");

	fStatus = new BStringView("status", "");
	Util::MakeElastic(fStatus);
	fRunButton = new BButton("run", "Run", new BMessage(kRun));

	BBox* optional = new BBox("optional");
	optional->SetLabel("Optional settings");
	BLayoutBuilder::Group<>(optional, B_VERTICAL, B_USE_SMALL_SPACING)
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.AddTextControl(fName, 0, 0)
			.AddTextControl(fPorts, 0, 1)
			.Add(new BStringView("volumesLabel", "Volumes:"), 0, 2)
			.Add(volumesScroll, 1, 2)
			.Add(new BStringView("envLabel", "Environment:"), 0, 3)
			.Add(environmentScroll, 1, 3)
			.AddTextControl(fCommand, 0, 4)
			// (In the grid, the menu field would cap the width of the
			// text fields at its own.)
			.Add(fRestart->CreateLabelLayoutItem(), 0, 5)
			.AddGroup(B_HORIZONTAL, 0, 1, 5)
				.Add(fRestart->CreateMenuBarLayoutItem())
				.AddGlue()
			.End()
		.End()
		.Add(fAutoRemove)
		.Add(fInteractive)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.AddTextControl(fImage, 0, 0)
		.End()
		.Add(optional)
		.Add(BSpaceLayoutItem::CreateHorizontalStrut(
			be_plain_font->StringWidth("M") * 34))
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(kCancel)))
			.Add(fRunButton)
		.End()
		.SetInsets(B_USE_WINDOW_SPACING);
	SetDefaultButton(fRunButton);
	Util::CenterOver(this, parent);
	if (image != NULL && image[0] != '\0')
		fName->MakeFocus(true);
	else
		fImage->MakeFocus(true);
}


RunWindow::~RunWindow()
{
	if (fCanceler)
		fCanceler->Cancel();
}


bool RunWindow::QuitRequested()
{
	if (fCanceler)
		fCanceler->Cancel();
	return true;
}


status_t RunWindow::_BuildRequest(BString& body, BString& error)
{
	BString image(fImage->Text());
	image.Trim();
	if (image.Length() == 0) {
		error = "Which image should the container run?";
		return B_BAD_VALUE;
	}

	std::map<BString, BString> portBindings;
	for (const BString& spec : Items(fPorts->Text(), true)) {
		// [ip:]host:container[/protocol], or container[/protocol] alone
		BString port(spec), protocol("tcp");
		int32 slash = port.FindFirst('/');
		if (slash >= 0) {
			port.CopyInto(protocol, slash + 1, port.Length() - slash - 1);
			port.Truncate(slash);
		}
		BString hostIp, hostPort, containerPort(port);
		int32 colon = port.FindLast(':');
		if (colon >= 0) {
			port.CopyInto(containerPort, colon + 1, port.Length() - colon - 1);
			port.Truncate(colon);
			hostPort = port;
			colon = hostPort.FindLast(':');
			if (colon >= 0) {
				hostPort.CopyInto(hostIp, 0, colon);
				hostPort.Remove(0, colon + 1);
			}
		}
		if (atoi(containerPort.String()) <= 0) {
			error.SetToFormat("\"%s\" is not a port mapping such as 8080:80",
				spec.String());
			return B_BAD_VALUE;
		}
		BString key(containerPort);
		key << "/" << protocol;
		// One container port can be published on several host ports.
		BString& binding = portBindings[key];
		if (binding.Length() > 0)
			binding << ",";
		binding << "{\"HostIp\":" << djson::Quote(hostIp.String())
			<< ",\"HostPort\":" << djson::Quote(hostPort.String()) << "}";
	}
	BString exposed, bindings;
	for (const auto& entry : portBindings) {
		if (exposed.Length() > 0) {
			exposed << ",";
			bindings << ",";
		}
		exposed << djson::Quote(entry.first.String()) << ":{}";
		bindings << djson::Quote(entry.first.String()) << ":["
			<< entry.second << "]";
	}

	BString binds;
	for (const BString& spec : Items(fVolumes->Text(), false)) {
		if (spec.FindFirst(':') < 0) {
			error.SetToFormat("\"%s\" is not a volume such as "
				"/boot/home/data:/data", spec.String());
			return B_BAD_VALUE;
		}
		if (binds.Length() > 0)
			binds << ",";
		binds << djson::Quote(spec.String());
	}

	BString environment;
	for (const BString& spec : Items(fEnvironment->Text(), false)) {
		if (environment.Length() > 0)
			environment << ",";
		environment << djson::Quote(spec.String());
	}

	BString command;
	for (const BString& word : SplitWords(fCommand->Text())) {
		if (command.Length() > 0)
			command << ",";
		command << djson::Quote(word.String());
	}

	int32 policy = fRestart->Menu()->IndexOf(fRestart->Menu()->FindMarked());
	if (policy < 0)
		policy = 0;

	body << "{\"Image\":" << djson::Quote(image.String());
	if (command.Length() > 0)
		body << ",\"Cmd\":[" << command << "]";
	if (environment.Length() > 0)
		body << ",\"Env\":[" << environment << "]";
	if (exposed.Length() > 0)
		body << ",\"ExposedPorts\":{" << exposed << "}";
	body << ",\"HostConfig\":{\"AutoRemove\":"
		<< (fAutoRemove->Value() == B_CONTROL_ON ? "true" : "false")
		<< ",\"RestartPolicy\":{\"Name\":"
		<< djson::Quote(kRestartPolicies[policy]) << "}";
	if (bindings.Length() > 0)
		body << ",\"PortBindings\":{" << bindings << "}";
	if (binds.Length() > 0)
		body << ",\"Binds\":[" << binds << "]";
	body << "}}";
	return B_OK;
}


void RunWindow::_RunInTerminal()
{
	BString image(fImage->Text());
	image.Trim();
	if (image.Length() == 0)
		return;
	BString command(Util::DockerCommand(
		Settings::Default().SocketPath().String()));
	command << " run " << Util::InteractiveFlags();
	if (fAutoRemove->Value() == B_CONTROL_ON)
		command << " --rm";
	BString name(fName->Text());
	name.Trim();
	if (name.Length() > 0)
		command << " --name " << Util::ShellQuote(name.String());
	for (const BString& spec : Items(fPorts->Text(), true))
		command << " -p " << Util::ShellQuote(spec.String());
	for (const BString& spec : Items(fVolumes->Text(), false))
		command << " -v " << Util::ShellQuote(spec.String());
	for (const BString& spec : Items(fEnvironment->Text(), false))
		command << " -e " << Util::ShellQuote(spec.String());
	int32 policy = fRestart->Menu()->IndexOf(fRestart->Menu()->FindMarked());
	if (policy > 0)
		command << " --restart " << kRestartPolicies[policy];
	command << " " << Util::ShellQuote(image.String());
	for (const BString& word : SplitWords(fCommand->Text()))
		command << " " << Util::ShellQuote(word.String());

	if (Util::RunInTerminal(image.String(), command.String(), true) != B_OK) {
		Util::ShowError("Terminal could not be started.");
		return;
	}
	Quit();
}


void RunWindow::_Run()
{
	if (fCanceler)
		return;
	if (fInteractive->Value() == B_CONTROL_ON) {
		_RunInTerminal();
		return;
	}

	BString body, error;
	if (_BuildRequest(body, error) != B_OK) {
		fStatus->SetText(error.String());
		return;
	}
	BString image(fImage->Text());
	image.Trim();
	BString name(fName->Text());
	name.Trim();

	fRunButton->SetEnabled(false);
	fStatus->SetText("Creating the container…");
	CancelerRef canceler(new StreamCanceler);
	fCanceler = canceler;
	BMessenger target(this);
	BString socket = Settings::Default().SocketPath();
	std::thread([socket, image, name, body, canceler, target]() {
		DockerClient client(socket.String());
		BString path("/containers/create");
		if (name.Length() > 0)
			path << "?name=" << DockerClient::Escape(name.String());

		BString response, error;
		status_t status = client.Call("POST", path.String(), &body, &response,
			&error);
		if (status != B_OK && error.IFindFirst("No such image") >= 0) {
			// Docker Desktop pulls what it does not have yet.
			BMessage progress(kProgress);
			progress.AddString("text", "Pulling the image…");
			target.SendMessage(&progress);
			bigtime_t lastUpdate = 0;
			status = Pull::Run(client, image.String(), NULL, canceler,
				[&](const Pull::Progress& pull) {
					bigtime_t now = system_time();
					if (now - lastUpdate < 200000)
						return;
					lastUpdate = now;
					double current, total;
					pull.Totals(&current, &total);
					BString text("Pulling the image…");
					if (total > 0) {
						text << " " << Util::FormatSize(current) << " / "
							<< Util::FormatSize(total);
					}
					BMessage update(kProgress);
					update.AddString("text", text);
					target.SendMessage(&update);
				}, error);
			if (status == B_OK) {
				BMessage update(kProgress);
				update.AddString("text", "Creating the container…");
				target.SendMessage(&update);
				error = "";
				status = client.Call("POST", path.String(), &body, &response,
					&error);
			}
		}

		BString id;
		if (status == B_OK) {
			djson::Value* root = djson::Parse(response.String());
			id = djson::GetString(root, "Id");
			djson::FreeValue(root);
			BMessage update(kProgress);
			update.AddString("text", "Starting the container…");
			target.SendMessage(&update);
			status = client.ContainerAction(id.String(), "start", &error);
		}

		BMessage done(kDone);
		done.AddString("id", id);
		if (status != B_OK)
			done.AddString("error", error);
		target.SendMessage(&done);
	}).detach();
}


void RunWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kRun:
		_Run();
		break;

	case kCancel:
		Quit();
		break;

	case kInteractive:
		fRunButton->SetLabel(fInteractive->Value() == B_CONTROL_ON
			? "Run in Terminal" : "Run");
		break;

	case kProgress:
		fStatus->SetText(message->GetString("text", ""));
		break;

	case kDone:
	{
		fCanceler.reset();
		fRunButton->SetEnabled(true);
		BString error, id;
		message->FindString("id", &id);
		if (message->FindString("error", &error) == B_OK) {
			fStatus->SetText("The container could not be run.");
			BString text(error);
			if (id.Length() > 0) {
				text << "\n\nThe container was created; it is in the "
					"Containers tab.";
			}
			Util::ShowError(text.String());
			break;
		}
		// Like Docker Desktop: go to the new container.
		BString name(fName->Text());
		name.Trim();
		ContainerWindow::Open(id.String(),
			name.Length() > 0 ? name.String()
				: Util::ShortId(id.String()).String(),
			ContainerWindow::kLogsTab, this);
		Quit();
		break;
	}

	default:
		BWindow::MessageReceived(message);
		break;
	}
}
