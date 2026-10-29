// SPDX-License-Identifier: MIT

#include "ContainersView.h"

#include "Columns.h"
#include "ContainerWindow.h"
#include "DockerClient.h"
#include "Json.h"
#include "Messages.h"
#include "PromptWindow.h"
#include "RunWindow.h"
#include "Settings.h"
#include "Util.h"

#include <Button.h>
#include <CheckBox.h>
#include <ColumnListView.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Window.h>

#include <map>
#include <set>

namespace {

enum {
	kNameColumn,
	kImageColumn,
	kStatusColumn,
	kPortsColumn,
	kCreatedColumn,
	kIdColumn,
};

const char* kComposeProject = "com.docker.compose.project";

} // namespace


ContainersView::ContainersView()
	:
	ResourceView("containers", "Containers"),
	fRunning(0),
	fTotal(0)
{
	BColumnListView* list = List();
	list->AddColumn(new StateColumn("Name", 200, 60, 600), kNameColumn);
	list->AddColumn(new BStringColumn("Image", 160, 40, 600, B_TRUNCATE_END),
		kImageColumn);
	list->AddColumn(new BStringColumn("Status", 170, 40, 400, B_TRUNCATE_END),
		kStatusColumn);
	list->AddColumn(new BStringColumn("Port(s)", 120, 40, 400,
		B_TRUNCATE_END), kPortsColumn);
	list->AddColumn(new SortColumn("Created", 130, 40, 300), kCreatedColumn);
	list->AddColumn(new BStringColumn("Container ID", 110, 40, 300,
		B_TRUNCATE_END), kIdColumn);
	list->SetSortColumn(list->ColumnAt(kNameColumn), false, true);

	fStartButton = AddButton("Start", M_CONTAINER_START);
	fStopButton = AddButton("Stop", M_CONTAINER_STOP);
	fRestartButton = AddButton("Restart", M_CONTAINER_RESTART);
	fPauseButton = AddButton("Pause", M_CONTAINER_PAUSE);
	fDeleteButton = AddButton("Delete", M_CONTAINER_REMOVE,
		"Stop and remove the selected containers");
	fDetailsButton = AddButton("Details", M_CONTAINER_DETAILS,
		"Logs, inspect and stats of the selected container");
	AddButton("Run…", M_RUN_CONTAINER, "Create and start a new container");

	fOnlyRunning = new BCheckBox("onlyRunning", "Only running",
		new BMessage(M_ONLY_RUNNING));
	fOnlyRunning->SetValue(Settings::Default().OnlyRunning());
	AddToolView(fOnlyRunning);
}


const char* ContainersView::EmptyText() const
{
	if (fOnlyRunning->Value() == B_CONTROL_ON && fTotal > 0)
		return "No containers are running.";
	return "No containers yet: run one from the Images tab.";
}


FetchFunction ContainersView::Fetcher() const
{
	return [](DockerClient& client, BMessage& reply) {
		BString json, error;
		if (client.Get("/containers/json?all=1", &json, &error) == B_OK)
			reply.AddString("json", json);
		else
			reply.AddString("error", error);
	};
}


void ContainersView::Populate(const BMessage& result)
{
	djson::Value* root = djson::Parse(result.GetString("json", "[]"));
	if (root == NULL)
		return;

	bool onlyRunning = fOnlyRunning->Value() == B_CONTROL_ON;
	fRunning = 0;
	fTotal = djson::Length(root);

	// Compose projects: their containers show under one row each.
	struct Project {
		int32 running = 0;
		int32 total = 0;
		time_t created = 0;
		BString ids;
	};
	std::map<BString, Project> projects;
	for (int32 i = 0; i < fTotal; i++) {
		const djson::Value* c = djson::At(root, i);
		BString state = djson::GetString(c, "State");
		if (state == "running")
			fRunning++;
		BString project = djson::GetString(djson::Get(c, "Labels"),
			kComposeProject);
		if (project.Length() == 0)
			continue;
		Project& entry = projects[project];
		entry.total++;
		if (state == "running")
			entry.running++;
		time_t created = (time_t)djson::GetNumber(c, "Created");
		if (created > entry.created)
			entry.created = created;
		if (entry.ids.Length() > 0)
			entry.ids << " ";
		entry.ids << djson::GetString(c, "Id");
	}

	BeginUpdate();
	std::map<BString, ObjectRow*> groupRows;

	for (int32 i = 0; i < fTotal; i++) {
		const djson::Value* c = djson::At(root, i);
		BString id = djson::GetString(c, "Id");
		BString name = djson::StringAt(djson::Get(c, "Names"), 0);
		if (name.StartsWith("/"))
			name.Remove(0, 1);
		BString image = djson::GetString(c, "Image");
		if (image.StartsWith("sha256:"))
			image = Util::ShortId(image.String());
		BString state = djson::GetString(c, "State");
		BString project = djson::GetString(djson::Get(c, "Labels"),
			kComposeProject);

		if (onlyRunning && state != "running" && state != "paused"
			&& state != "restarting")
			continue;
		if (!Matches({name.String(), image.String(), id.String(),
				project.String()}))
			continue;

		ObjectRow* parent = NULL;
		if (project.Length() > 0) {
			auto it = groupRows.find(project);
			if (it != groupRows.end())
				parent = it->second;
			else {
				const Project& entry = projects[project];
				BString groupId("compose:");
				groupId << project;
				parent = UpsertRow(groupId.String(), project.String());
				parent->isGroup = true;
				parent->extra = entry.ids;
				parent->state = entry.running == entry.total ? "running"
					: entry.running > 0 ? "partial" : "exited";
				BString status;
				if (entry.running > 0)
					status.SetToFormat("Running (%" B_PRId32 "/%" B_PRId32 ")",
						entry.running, entry.total);
				else
					status.SetToFormat("Exited (%" B_PRId32 " containers)",
						entry.total);
				parent->SetField(new StateField(project.String(),
					parent->state.String()), kNameColumn);
				parent->SetField(new BStringField("Compose project"),
					kImageColumn);
				parent->SetField(new BStringField(status.String()),
					kStatusColumn);
				parent->SetField(new BStringField(""), kPortsColumn);
				parent->SetField(new SortField(
					Util::FormatAge(entry.created).String(), entry.created),
					kCreatedColumn);
				parent->SetField(new BStringField(""), kIdColumn);
				bool isNew = !parent->added;
				PlaceRow(parent);
				// Projects start out expanded; the user decides after that.
				if (isNew)
					List()->ExpandOrCollapse(parent, true);
				groupRows[project] = parent;
			}
		}

		ObjectRow* row = UpsertRow(id.String(), name.String());
		row->state = state;
		row->extra = image;
		row->port = Util::FirstHostPort(djson::Get(c, "Ports"));
		time_t created = (time_t)djson::GetNumber(c, "Created");
		row->SetField(new StateField(name.String(), state.String()),
			kNameColumn);
		row->SetField(new BStringField(image.String()), kImageColumn);
		row->SetField(new BStringField(djson::GetString(c, "Status")),
			kStatusColumn);
		row->SetField(new BStringField(
			Util::FormatPorts(djson::Get(c, "Ports")).String()),
			kPortsColumn);
		row->SetField(new SortField(Util::FormatAge(created).String(),
			created), kCreatedColumn);
		row->SetField(new BStringField(Util::ShortId(id.String()).String()),
			kIdColumn);
		PlaceRow(row, parent);
	}
	djson::FreeValue(root);
	EndUpdate();
}


std::vector<BString> ContainersView::_SelectedIds(const char* onlyInState,
	bool runningOrPaused) const
{
	std::vector<BString> ids;
	std::set<BString> seen;
	auto add = [&](const BString& id) {
		if (seen.insert(id).second)
			ids.push_back(id);
	};
	for (ObjectRow* row : SelectedRows()) {
		if (row->isGroup) {
			BString all = row->extra;
			int32 start = 0;
			while (start < all.Length()) {
				int32 end = all.FindFirst(' ', start);
				if (end < 0)
					end = all.Length();
				BString id;
				all.CopyInto(id, start, end - start);
				ObjectRow* child = FindRow(id.String());
				bool include = true;
				if (child != NULL && onlyInState != NULL)
					include = child->state == onlyInState;
				if (child != NULL && runningOrPaused)
					include = child->state == "running"
						|| child->state == "paused"
						|| child->state == "restarting";
				if (include)
					add(id);
				start = end + 1;
			}
			continue;
		}
		if (onlyInState != NULL && row->state != onlyInState)
			continue;
		if (runningOrPaused && row->state != "running"
			&& row->state != "paused" && row->state != "restarting")
			continue;
		add(row->Id());
	}
	return ids;
}


void ContainersView::UpdateButtons()
{
	bool any = false, startable = false, stoppable = false, running = false,
		paused = false, single = false;
	std::vector<ObjectRow*> rows = SelectedRows();
	for (ObjectRow* row : rows) {
		any = true;
		const BString& state = row->state;
		if (row->isGroup) {
			startable |= state != "running";
			stoppable |= state != "exited";
			continue;
		}
		startable |= state == "exited" || state == "created"
			|| state == "dead";
		stoppable |= state == "running" || state == "paused"
			|| state == "restarting";
		running |= state == "running";
		paused |= state == "paused";
	}
	single = rows.size() == 1 && !rows[0]->isGroup;

	fStartButton->SetEnabled(startable);
	fStopButton->SetEnabled(stoppable);
	fRestartButton->SetEnabled(any);
	fPauseButton->SetEnabled(running || paused);
	if (paused && !running) {
		fPauseButton->SetLabel("Resume");
		fPauseButton->Message()->what = M_CONTAINER_UNPAUSE;
	} else {
		fPauseButton->SetLabel("Pause");
		fPauseButton->Message()->what = M_CONTAINER_PAUSE;
	}
	fDeleteButton->SetEnabled(any);
	fDetailsButton->SetEnabled(single);
}


void ContainersView::BuildContextMenu(BPopUpMenu* menu)
{
	std::vector<ObjectRow*> rows = SelectedRows();
	if (rows.empty())
		return;
	bool single = rows.size() == 1 && !rows[0]->isGroup;
	ObjectRow* row = rows[0];

	if (single) {
		menu->AddItem(new BMenuItem("View details",
			new BMessage(M_CONTAINER_DETAILS)));
		menu->AddItem(new BMenuItem("View logs",
			new BMessage(M_CONTAINER_LOGS)));
		menu->AddItem(new BMenuItem("Inspect",
			new BMessage(M_CONTAINER_INSPECT)));
		BMenuItem* terminal = new BMenuItem("Open in Terminal",
			new BMessage(M_CONTAINER_TERMINAL));
		terminal->SetEnabled(row->state == "running");
		menu->AddItem(terminal);
		if (row->port > 0) {
			BString label;
			label.SetToFormat("Open localhost:%" B_PRId32 " in browser",
				row->port);
			menu->AddItem(new BMenuItem(label.String(),
				new BMessage(M_CONTAINER_BROWSER)));
		}
		menu->AddSeparatorItem();
	}

	menu->AddItem(new BMenuItem("Start", new BMessage(M_CONTAINER_START)));
	menu->AddItem(new BMenuItem("Stop", new BMessage(M_CONTAINER_STOP)));
	menu->AddItem(new BMenuItem("Restart",
		new BMessage(M_CONTAINER_RESTART)));
	if (single && row->state == "paused")
		menu->AddItem(new BMenuItem("Resume",
			new BMessage(M_CONTAINER_UNPAUSE)));
	else
		menu->AddItem(new BMenuItem("Pause", new BMessage(M_CONTAINER_PAUSE)));
	menu->AddItem(new BMenuItem("Kill", new BMessage(M_CONTAINER_KILL)));
	if (single)
		menu->AddItem(new BMenuItem("Rename…",
			new BMessage(M_CONTAINER_RENAME)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Copy ID", new BMessage(M_COPY_ID)));
	menu->AddItem(new BMenuItem("Copy name", new BMessage(M_COPY_NAME)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Delete", new BMessage(M_CONTAINER_REMOVE)));
}


void ContainersView::Invoked(ObjectRow* row)
{
	if (row->isGroup) {
		List()->ExpandOrCollapse(row, !row->IsExpanded());
		return;
	}
	_OpenDetails(ContainerWindow::kLogsTab);
}


void ContainersView::_Action(const char* verb, const char* status,
	const char* state, bool runningOrPaused)
{
	std::vector<BString> ids = _SelectedIds(state, runningOrPaused);
	if (ids.empty()) {
		// Fall back to everything selected; the Engine says what is wrong.
		ids = _SelectedIds();
	}
	BString action(verb);
	PerformOn(ids, status,
		[action](DockerClient& client, const BString& id, BString& error) {
			return client.ContainerAction(id.String(), action.String(),
				&error);
		});
}


void ContainersView::_Remove()
{
	std::vector<BString> ids = _SelectedIds();
	if (ids.empty())
		return;
	BString text;
	if (ids.size() == 1) {
		ObjectRow* row = FindRow(ids[0].String());
		text.SetToFormat("Delete the container \"%s\"? A running container "
			"is stopped first. This cannot be undone.",
			row != NULL ? row->Name().String() : ids[0].String());
	} else {
		text.SetToFormat("Delete %d containers? Running containers are "
			"stopped first. This cannot be undone.", (int)ids.size());
	}
	if (!ConfirmDelete(text.String()))
		return;
	PerformOn(ids, "Deleting",
		[](DockerClient& client, const BString& id, BString& error) {
			return client.ContainerAction(id.String(), "remove", &error);
		});
}


void ContainersView::_Rename(BMessage* message)
{
	BString id, name;
	if (message->FindString("id", &id) != B_OK) {
		ObjectRow* row = SelectedRow();
		if (row == NULL || row->isGroup)
			return;
		BMessage reply(M_CONTAINER_RENAME);
		reply.AddString("id", row->Id());
		PromptWindow* prompt = new PromptWindow("Rename container",
			"New name:", row->Name().String(), "Rename", BMessenger(this),
			reply);
		prompt->Show();
		return;
	}
	if (message->FindString("text", &name) != B_OK || name.Length() == 0)
		return;
	Perform("Renaming", [id, name](DockerClient& client, BString& error) {
		BString path;
		path.SetToFormat("/containers/%s/rename?name=%s", id.String(),
			DockerClient::Escape(name.String()).String());
		return client.Call("POST", path.String(), NULL, NULL, &error);
	});
}


void ContainersView::_OpenTerminal()
{
	ObjectRow* row = SelectedRow();
	if (row == NULL || row->isGroup)
		return;
	BString command = Util::ShellCommand(SocketPath().String(),
		row->Id().String());
	BString title(row->Name());
	title << " (container)";
	if (Util::RunInTerminal(title.String(), command.String(), false) != B_OK)
		Util::ShowError("Terminal could not be started.");
}


void ContainersView::_OpenBrowser()
{
	ObjectRow* row = SelectedRow();
	if (row == NULL || row->port <= 0)
		return;
	BString url;
	url.SetToFormat("http://localhost:%" B_PRId32 "/", row->port);
	Util::OpenURL(url.String());
}


void ContainersView::_OpenDetails(int32 tab)
{
	ObjectRow* row = SelectedRow();
	if (row == NULL || row->isGroup)
		return;
	ContainerWindow::Open(row->Id().String(), row->Name().String(), tab,
		Window());
}


void ContainersView::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case M_CONTAINER_START:
		_Action("start", "Starting", NULL, false);
		break;
	case M_CONTAINER_STOP:
		_Action("stop", "Stopping", NULL, true);
		break;
	case M_CONTAINER_RESTART:
		_Action("restart", "Restarting", NULL, false);
		break;
	case M_CONTAINER_PAUSE:
		_Action("pause", "Pausing", "running", false);
		break;
	case M_CONTAINER_UNPAUSE:
		_Action("unpause", "Resuming", "paused", false);
		break;
	case M_CONTAINER_KILL:
		_Action("kill", "Killing", NULL, true);
		break;
	case M_CONTAINER_REMOVE:
		_Remove();
		break;
	case M_CONTAINER_RENAME:
		_Rename(message);
		break;
	case M_CONTAINER_DETAILS:
		_OpenDetails(ContainerWindow::kLogsTab);
		break;
	case M_CONTAINER_LOGS:
		_OpenDetails(ContainerWindow::kLogsTab);
		break;
	case M_CONTAINER_INSPECT:
		_OpenDetails(ContainerWindow::kInspectTab);
		break;
	case M_CONTAINER_TERMINAL:
		_OpenTerminal();
		break;
	case M_CONTAINER_BROWSER:
		_OpenBrowser();
		break;
	case M_RUN_CONTAINER:
		(new RunWindow("", Window()))->Show();
		break;
	case M_ONLY_RUNNING:
		Settings::Default().SetOnlyRunning(
			fOnlyRunning->Value() == B_CONTROL_ON);
	{
		// Shows the last list again with the new filter.
		BMessage filter(M_SEARCH_CHANGED);
		ResourceView::MessageReceived(&filter);
		break;
	}
	default:
		ResourceView::MessageReceived(message);
		break;
	}
}
