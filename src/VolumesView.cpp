// SPDX-License-Identifier: MIT

#include "VolumesView.h"

#include "Columns.h"
#include "DockerClient.h"
#include "Json.h"
#include "Messages.h"
#include "PromptWindow.h"
#include "TextWindow.h"
#include "Util.h"

#include <Button.h>
#include <ColumnListView.h>
#include <Entry.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Window.h>

#include <map>

namespace {

enum {
	kVolumeName,
	kVolumeDriver,
	kVolumeMount,
	kVolumeCreated,
	kVolumeSize,
	kVolumeStatus,
};

enum {
	kNetworkName,
	kNetworkDriver,
	kNetworkScope,
	kNetworkSubnet,
	kNetworkGateway,
	kNetworkId,
};

// The networks every Engine has, which cannot be removed.
bool IsBuiltinNetwork(const BString& name)
{
	return name == "bridge" || name == "host" || name == "none";
}

void OpenInspect(BWindow* parent, const char* title, const BString& socket,
	const BString& path)
{
	TextWindow* window = new TextWindow(title, parent);
	window->SetLoader([socket, path](BString& text, BString& error) {
		DockerClient client(socket.String());
		BString json;
		status_t status = client.Get(path.String(), &json, &error);
		if (status == B_OK)
			text = djson::Pretty(json.String());
		return status;
	});
	window->Show();
}

} // namespace


// #pragma mark - VolumesView


VolumesView::VolumesView()
	:
	ResourceView("volumes", "Volumes")
{
	BColumnListView* list = List();
	list->AddColumn(new StateColumn("Name", 220, 60, 600), kVolumeName);
	list->AddColumn(new BStringColumn("Driver", 70, 40, 200, B_TRUNCATE_END),
		kVolumeDriver);
	list->AddColumn(new BStringColumn("Mount point", 260, 40, 800,
		B_TRUNCATE_MIDDLE), kVolumeMount);
	list->AddColumn(new SortColumn("Created", 130, 40, 300), kVolumeCreated);
	list->AddColumn(new SortColumn("Size", 80, 40, 200, B_ALIGN_RIGHT),
		kVolumeSize);
	list->AddColumn(new BStringColumn("Status", 80, 40, 200, B_TRUNCATE_END),
		kVolumeStatus);
	list->SetSortColumn(list->ColumnAt(kVolumeName), false, true);

	AddButton("Create…", M_VOLUME_CREATE, "Create a new volume");
	fDeleteButton = AddButton("Delete", M_VOLUME_REMOVE);
	fInspectButton = AddButton("Inspect", M_VOLUME_INSPECT);
	AddButton("Delete unused…", M_PRUNE,
		"Delete the volumes no container uses");
}


const char* VolumesView::EmptyText() const
{
	return "No volumes yet. Volumes keep data that outlives containers.";
}


FetchFunction VolumesView::Fetcher() const
{
	return [](DockerClient& client, BMessage& reply) {
		BString json, usage, error;
		if (client.Get("/volumes", &json, &error) != B_OK) {
			reply.AddString("error", error);
			return;
		}
		reply.AddString("json", json);
		// Sizes and use counts, where the Engine measures them.
		if (client.Get("/system/df?type=volume", &usage) == B_OK)
			reply.AddString("usage", usage);
	};
}


void VolumesView::Populate(const BMessage& result)
{
	djson::Value* root = djson::Parse(result.GetString("json", "{}"));
	if (root == NULL)
		return;
	djson::Value* usage = djson::Parse(result.GetString("usage", "{}"));

	struct Usage {
		double size = -1;
		double references = -1;
	};
	std::map<BString, Usage> usages;
	const djson::Value* usageList = djson::Get(usage, "Volumes");
	for (int32 i = 0; i < djson::Length(usageList); i++) {
		const djson::Value* volume = djson::At(usageList, i);
		const djson::Value* data = djson::Get(volume, "UsageData");
		Usage& entry = usages[djson::GetString(volume, "Name")];
		entry.size = djson::GetNumber(data, "Size", -1);
		entry.references = djson::GetNumber(data, "RefCount", -1);
	}
	djson::FreeValue(usage);

	BeginUpdate();
	const djson::Value* volumes = djson::Get(root, "Volumes");
	for (int32 i = 0; i < djson::Length(volumes); i++) {
		const djson::Value* volume = djson::At(volumes, i);
		BString name = djson::GetString(volume, "Name");
		BString driver = djson::GetString(volume, "Driver");
		BString mount = djson::GetString(volume, "Mountpoint");
		if (!Matches({name.String(), driver.String(), mount.String()}))
			continue;
		time_t created = Util::ParseTime(djson::GetString(volume,
			"CreatedAt"));
		Usage entry = usages[name];
		bool inUse = entry.references > 0;

		ObjectRow* row = UpsertRow(name.String(), name.String());
		row->extra = mount;
		row->state = inUse ? "running" : "exited";
		row->SetField(new StateField(name.String(), row->state.String()),
			kVolumeName);
		row->SetField(new BStringField(driver.String()), kVolumeDriver);
		row->SetField(new BStringField(mount.String()), kVolumeMount);
		row->SetField(new SortField(Util::FormatAge(created).String(),
			created), kVolumeCreated);
		row->SetField(new SortField(entry.size >= 0
			? Util::FormatSize(entry.size).String() : "–", entry.size),
			kVolumeSize);
		row->SetField(new BStringField(entry.references < 0 ? ""
			: inUse ? "In use" : "Unused"), kVolumeStatus);
		PlaceRow(row);
	}
	djson::FreeValue(root);
	EndUpdate();
}


void VolumesView::UpdateButtons()
{
	std::vector<ObjectRow*> rows = SelectedRows();
	fDeleteButton->SetEnabled(!rows.empty());
	fInspectButton->SetEnabled(rows.size() == 1);
}


void VolumesView::BuildContextMenu(BPopUpMenu* menu)
{
	std::vector<ObjectRow*> rows = SelectedRows();
	if (rows.empty())
		return;
	if (rows.size() == 1) {
		menu->AddItem(new BMenuItem("Inspect",
			new BMessage(M_VOLUME_INSPECT)));
		menu->AddItem(new BMenuItem("Open in Tracker",
			new BMessage(M_VOLUME_OPEN)));
		menu->AddSeparatorItem();
	}
	menu->AddItem(new BMenuItem("Copy name", new BMessage(M_COPY_NAME)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Delete", new BMessage(M_VOLUME_REMOVE)));
}


void VolumesView::Invoked(ObjectRow* row)
{
	_Inspect();
}


void VolumesView::_Create(BMessage* message)
{
	BString name;
	if (message->FindString("text", &name) != B_OK) {
		PromptWindow* prompt = new PromptWindow("Create a volume", "Name:",
			"", "Create", BMessenger(this), BMessage(M_VOLUME_CREATE),
			Window());
		prompt->AddField("driver", "Driver:", "local");
		prompt->Show();
		return;
	}
	name.Trim();
	BString driver = message->GetString("driver", "local");
	driver.Trim();
	Perform("Creating the volume",
		[name, driver](DockerClient& client, BString& error) {
			BString body("{");
			if (name.Length() > 0)
				body << "\"Name\":" << djson::Quote(name.String()) << ",";
			body << "\"Driver\":" << djson::Quote(driver.Length() > 0
				? driver.String() : "local") << "}";
			return client.Call("POST", "/volumes/create", &body, NULL,
				&error);
		});
}


void VolumesView::_Remove()
{
	std::vector<BString> names;
	for (ObjectRow* row : SelectedRows())
		names.push_back(row->Id());
	if (names.empty())
		return;
	BString text;
	if (names.size() == 1)
		text.SetToFormat("Delete the volume \"%s\" and all its data?",
			names[0].String());
	else
		text.SetToFormat("Delete %d volumes and all their data?",
			(int)names.size());
	if (!ConfirmDelete(text.String()))
		return;
	PerformOn(names, "Deleting",
		[](DockerClient& client, const BString& name, BString& error) {
			BString path("/volumes/");
			path << DockerClient::Escape(name.String());
			return client.Call("DELETE", path.String(), NULL, NULL, &error);
		});
}


void VolumesView::_Inspect()
{
	ObjectRow* row = SelectedRow();
	if (row == NULL)
		return;
	BString title(row->Name());
	title << " (volume)";
	BString path("/volumes/");
	path << DockerClient::Escape(row->Id().String());
	OpenInspect(Window(), title.String(), SocketPath(), path);
}


void VolumesView::_Open()
{
	ObjectRow* row = SelectedRow();
	if (row == NULL || row->extra.Length() == 0)
		return;
	entry_ref ref;
	if (get_ref_for_path(row->extra.String(), &ref) != B_OK
		|| be_roster->Launch(&ref) != B_OK) {
		BString text("The volume's folder cannot be opened: ");
		text << row->extra;
		Util::ShowError(text.String());
	}
}


void VolumesView::_Prune()
{
	if (!ConfirmDelete("Delete every volume that no container uses, with "
			"all its data?", "Delete unused"))
		return;
	Perform("Deleting unused volumes", [](DockerClient& client,
			BString& error) {
		// "all": named volumes too, not only anonymous ones.
		BString body;
		return client.Call("POST",
			"/volumes/prune?filters=%7B%22all%22%3A%5B%22true%22%5D%7D", NULL,
			&body, &error);
	});
}


void VolumesView::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case M_VOLUME_CREATE:
		_Create(message);
		break;
	case M_VOLUME_REMOVE:
		_Remove();
		break;
	case M_VOLUME_INSPECT:
		_Inspect();
		break;
	case M_VOLUME_OPEN:
		_Open();
		break;
	case M_PRUNE:
		_Prune();
		break;
	default:
		ResourceView::MessageReceived(message);
		break;
	}
}


// #pragma mark - NetworksView


NetworksView::NetworksView()
	:
	ResourceView("networks", "Networks")
{
	BColumnListView* list = List();
	list->AddColumn(new BStringColumn("Name", 200, 60, 600, B_TRUNCATE_END),
		kNetworkName);
	list->AddColumn(new BStringColumn("Driver", 80, 40, 200, B_TRUNCATE_END),
		kNetworkDriver);
	list->AddColumn(new BStringColumn("Scope", 70, 40, 200, B_TRUNCATE_END),
		kNetworkScope);
	list->AddColumn(new BStringColumn("Subnet", 130, 40, 300,
		B_TRUNCATE_END), kNetworkSubnet);
	list->AddColumn(new BStringColumn("Gateway", 110, 40, 300,
		B_TRUNCATE_END), kNetworkGateway);
	list->AddColumn(new BStringColumn("Network ID", 110, 40, 300,
		B_TRUNCATE_END), kNetworkId);
	list->SetSortColumn(list->ColumnAt(kNetworkName), false, true);

	AddButton("Create…", M_NETWORK_CREATE, "Create a new network");
	fDeleteButton = AddButton("Delete", M_NETWORK_REMOVE);
	fInspectButton = AddButton("Inspect", M_NETWORK_INSPECT);
	AddButton("Delete unused…", M_PRUNE,
		"Delete the networks no container uses");
}


const char* NetworksView::EmptyText() const
{
	return "No networks.";
}


FetchFunction NetworksView::Fetcher() const
{
	return [](DockerClient& client, BMessage& reply) {
		BString json, error;
		if (client.Get("/networks", &json, &error) == B_OK)
			reply.AddString("json", json);
		else
			reply.AddString("error", error);
	};
}


void NetworksView::Populate(const BMessage& result)
{
	djson::Value* root = djson::Parse(result.GetString("json", "[]"));
	if (root == NULL)
		return;
	BeginUpdate();
	for (int32 i = 0; i < djson::Length(root); i++) {
		const djson::Value* network = djson::At(root, i);
		BString id = djson::GetString(network, "Id");
		BString name = djson::GetString(network, "Name");
		BString driver = djson::GetString(network, "Driver");
		const djson::Value* config = djson::At(djson::Get(djson::Get(network,
			"IPAM"), "Config"), 0);
		BString subnet = djson::GetString(config, "Subnet");
		BString gateway = djson::GetString(config, "Gateway");
		if (!Matches({name.String(), driver.String(), id.String(),
				subnet.String()}))
			continue;

		ObjectRow* row = UpsertRow(id.String(), name.String());
		row->state = IsBuiltinNetwork(name) ? "builtin" : "";
		row->SetField(new BStringField(name.String()), kNetworkName);
		row->SetField(new BStringField(driver.String()), kNetworkDriver);
		row->SetField(new BStringField(djson::GetString(network, "Scope")),
			kNetworkScope);
		row->SetField(new BStringField(subnet.String()), kNetworkSubnet);
		row->SetField(new BStringField(gateway.String()), kNetworkGateway);
		row->SetField(new BStringField(Util::ShortId(id.String()).String()),
			kNetworkId);
		PlaceRow(row);
	}
	djson::FreeValue(root);
	EndUpdate();
}


void NetworksView::UpdateButtons()
{
	std::vector<ObjectRow*> rows = SelectedRows();
	bool removable = false;
	for (ObjectRow* row : rows)
		removable |= row->state != "builtin";
	fDeleteButton->SetEnabled(removable);
	fInspectButton->SetEnabled(rows.size() == 1);
}


void NetworksView::BuildContextMenu(BPopUpMenu* menu)
{
	std::vector<ObjectRow*> rows = SelectedRows();
	if (rows.empty())
		return;
	if (rows.size() == 1) {
		menu->AddItem(new BMenuItem("Inspect",
			new BMessage(M_NETWORK_INSPECT)));
		menu->AddSeparatorItem();
	}
	menu->AddItem(new BMenuItem("Copy ID", new BMessage(M_COPY_ID)));
	menu->AddItem(new BMenuItem("Copy name", new BMessage(M_COPY_NAME)));
	menu->AddSeparatorItem();
	BMenuItem* remove = new BMenuItem("Delete",
		new BMessage(M_NETWORK_REMOVE));
	remove->SetEnabled(fDeleteButton->IsEnabled());
	menu->AddItem(remove);
}


void NetworksView::Invoked(ObjectRow* row)
{
	_Inspect();
}


void NetworksView::_Create(BMessage* message)
{
	BString name;
	if (message->FindString("text", &name) != B_OK) {
		PromptWindow* prompt = new PromptWindow("Create a network", "Name:",
			"", "Create", BMessenger(this), BMessage(M_NETWORK_CREATE),
			Window());
		prompt->AddField("driver", "Driver:", "bridge");
		prompt->AddField("subnet", "Subnet:", "",
			"Optional, for example 172.30.0.0/16");
		prompt->Show();
		return;
	}
	name.Trim();
	if (name.Length() == 0)
		return;
	BString driver = message->GetString("driver", "bridge");
	BString subnet = message->GetString("subnet", "");
	driver.Trim();
	subnet.Trim();
	Perform("Creating the network",
		[name, driver, subnet](DockerClient& client, BString& error) {
			BString body("{\"Name\":");
			body << djson::Quote(name.String()) << ",\"Driver\":"
				<< djson::Quote(driver.Length() > 0 ? driver.String()
					: "bridge");
			if (subnet.Length() > 0) {
				body << ",\"IPAM\":{\"Config\":[{\"Subnet\":"
					<< djson::Quote(subnet.String()) << "}]}";
			}
			body << "}";
			return client.Call("POST", "/networks/create", &body, NULL,
				&error);
		});
}


void NetworksView::_Remove()
{
	std::vector<BString> ids;
	BString names;
	for (ObjectRow* row : SelectedRows()) {
		if (row->state == "builtin")
			continue;
		ids.push_back(row->Id());
		if (names.Length() > 0)
			names << ", ";
		names << row->Name();
	}
	if (ids.empty())
		return;
	BString text;
	text.SetToFormat("Delete the network%s %s?", ids.size() == 1 ? "" : "s",
		names.String());
	if (!ConfirmDelete(text.String()))
		return;
	PerformOn(ids, "Deleting",
		[](DockerClient& client, const BString& id, BString& error) {
			BString path("/networks/");
			path << id;
			return client.Call("DELETE", path.String(), NULL, NULL, &error);
		});
}


void NetworksView::_Inspect()
{
	ObjectRow* row = SelectedRow();
	if (row == NULL)
		return;
	BString title(row->Name());
	title << " (network)";
	BString path("/networks/");
	path << row->Id();
	OpenInspect(Window(), title.String(), SocketPath(), path);
}


void NetworksView::_Prune()
{
	if (!ConfirmDelete("Delete every network that no container uses? The "
			"built-in networks stay.", "Delete unused"))
		return;
	Perform("Deleting unused networks", [](DockerClient& client,
			BString& error) {
		BString body;
		return client.Call("POST", "/networks/prune", NULL, &body, &error);
	});
}


void NetworksView::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case M_NETWORK_CREATE:
		_Create(message);
		break;
	case M_NETWORK_REMOVE:
		_Remove();
		break;
	case M_NETWORK_INSPECT:
		_Inspect();
		break;
	case M_PRUNE:
		_Prune();
		break;
	default:
		ResourceView::MessageReceived(message);
		break;
	}
}
