// SPDX-License-Identifier: MIT

#include "ImagesView.h"

#include "Columns.h"
#include "DockerClient.h"
#include "HubSearchWindow.h"
#include "Json.h"
#include "Messages.h"
#include "PromptWindow.h"
#include "Pull.h"
#include "PullWindow.h"
#include "RunWindow.h"
#include "Settings.h"
#include "TextWindow.h"
#include "Util.h"

#include <Button.h>
#include <ColumnListView.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Window.h>

#include <set>

namespace {

enum {
	kNameColumn,
	kTagColumn,
	kIdColumn,
	kCreatedColumn,
	kSizeColumn,
	kStatusColumn,
};

} // namespace


ImagesView::ImagesView()
	:
	ResourceView("images", "Images"),
	fCount(0),
	fTotalSize(0)
{
	BColumnListView* list = List();
	list->AddColumn(new StateColumn("Name", 220, 60, 600), kNameColumn);
	list->AddColumn(new BStringColumn("Tag", 100, 30, 300, B_TRUNCATE_END),
		kTagColumn);
	list->AddColumn(new BStringColumn("Image ID", 110, 40, 300,
		B_TRUNCATE_END), kIdColumn);
	list->AddColumn(new SortColumn("Created", 130, 40, 300), kCreatedColumn);
	list->AddColumn(new SortColumn("Size", 90, 40, 200, B_ALIGN_RIGHT),
		kSizeColumn);
	list->AddColumn(new BStringColumn("Status", 80, 40, 200, B_TRUNCATE_END),
		kStatusColumn);
	list->SetSortColumn(list->ColumnAt(kNameColumn), false, true);

	fRunButton = AddButton("Run…", M_IMAGE_RUN,
		"Run a new container from the selected image");
	AddButton("Pull…", M_IMAGE_PULL, "Pull an image from a registry");
	AddButton("Search Hub…", M_HUB_SEARCH, "Search Docker Hub for images");
	fDeleteButton = AddButton("Delete", M_IMAGE_REMOVE);
	fInspectButton = AddButton("Inspect", M_IMAGE_INSPECT);
	fHistoryButton = AddButton("History", M_IMAGE_HISTORY,
		"The layers of the image and the commands that made them");
}


const char* ImagesView::EmptyText() const
{
	return "No images yet: pull one, or search Docker Hub.";
}


FetchFunction ImagesView::Fetcher() const
{
	return [](DockerClient& client, BMessage& reply) {
		BString images, containers, error;
		if (client.Get("/images/json", &images, &error) != B_OK) {
			reply.AddString("error", error);
			return;
		}
		reply.AddString("images", images);
		// For what is in use.
		if (client.Get("/containers/json?all=1", &containers) == B_OK)
			reply.AddString("containers", containers);
	};
}


void ImagesView::Populate(const BMessage& result)
{
	djson::Value* images = djson::Parse(result.GetString("images", "[]"));
	if (images == NULL)
		return;
	djson::Value* containers = djson::Parse(
		result.GetString("containers", "[]"));

	std::set<BString> used;
	for (int32 i = 0; i < djson::Length(containers); i++)
		used.insert(djson::GetString(djson::At(containers, i), "ImageID"));
	djson::FreeValue(containers);

	fCount = djson::Length(images);
	fTotalSize = 0;
	BeginUpdate();
	for (int32 i = 0; i < fCount; i++) {
		const djson::Value* image = djson::At(images, i);
		BString id = djson::GetString(image, "Id");
		double size = djson::GetNumber(image, "Size");
		fTotalSize += size;
		time_t created = (time_t)djson::GetNumber(image, "Created");
		bool inUse = used.find(id) != used.end();
		const djson::Value* tags = djson::Get(image, "RepoTags");

		int32 count = djson::Length(tags);
		for (int32 t = 0; t < (count > 0 ? count : 1); t++) {
			BString reference = count > 0 ? djson::StringAt(tags, t) : "";
			BString name("<none>"), tag("<none>");
			if (reference.Length() > 0 && reference != "<none>:<none>") {
				Pull::SplitReference(reference.String(), name, tag);
			} else {
				// A dangling image: its digest names it, if anything.
				BString digest = djson::StringAt(
					djson::Get(image, "RepoDigests"), 0);
				int32 at = digest.FindFirst('@');
				if (at > 0)
					digest.Truncate(at);
				if (digest.Length() > 0 && digest != "<none>")
					name = digest;
				reference = "";
			}
			if (!Matches({name.String(), tag.String(), id.String()}))
				continue;

			BString key(id);
			key << "|" << reference;
			ObjectRow* row = UpsertRow(key.String(),
				reference.Length() > 0 ? reference.String()
					: Util::ShortId(id.String()).String());
			row->extra = id;
			row->state = inUse ? "running" : "unused";
			row->SetField(new StateField(name.String(),
				inUse ? "running" : "exited"), kNameColumn);
			row->SetField(new BStringField(tag.String()), kTagColumn);
			row->SetField(new BStringField(
				Util::ShortId(id.String()).String()), kIdColumn);
			row->SetField(new SortField(Util::FormatAge(created).String(),
				created), kCreatedColumn);
			row->SetField(new SortField(Util::FormatSize(size).String(),
				size), kSizeColumn);
			row->SetField(new BStringField(inUse ? "In use" : "Unused"),
				kStatusColumn);
			PlaceRow(row);
		}
	}
	djson::FreeValue(images);
	EndUpdate();
}


void ImagesView::UpdateButtons()
{
	std::vector<ObjectRow*> rows = SelectedRows();
	bool single = rows.size() == 1;
	fRunButton->SetEnabled(single);
	fDeleteButton->SetEnabled(!rows.empty());
	fInspectButton->SetEnabled(single);
	fHistoryButton->SetEnabled(single);
}


void ImagesView::BuildContextMenu(BPopUpMenu* menu)
{
	std::vector<ObjectRow*> rows = SelectedRows();
	if (rows.empty())
		return;
	bool single = rows.size() == 1;
	bool tagged = single && rows[0]->Name() != Util::ShortId(
		rows[0]->extra.String());
	if (single) {
		menu->AddItem(new BMenuItem("Run…", new BMessage(M_IMAGE_RUN)));
		BMenuItem* pull = new BMenuItem("Pull again (update)",
			new BMessage(M_IMAGE_REPULL));
		pull->SetEnabled(tagged);
		menu->AddItem(pull);
		menu->AddItem(new BMenuItem("Tag…", new BMessage(M_IMAGE_TAG)));
		BMenuItem* push = new BMenuItem("Push to registry",
			new BMessage(M_IMAGE_PUSH));
		push->SetEnabled(tagged);
		menu->AddItem(push);
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Inspect", new BMessage(M_IMAGE_INSPECT)));
		menu->AddItem(new BMenuItem("History",
			new BMessage(M_IMAGE_HISTORY)));
		menu->AddSeparatorItem();
	}
	menu->AddItem(new BMenuItem("Copy ID", new BMessage(M_COPY_ID)));
	menu->AddItem(new BMenuItem("Copy name", new BMessage(M_COPY_NAME)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Delete", new BMessage(M_IMAGE_REMOVE)));
	menu->AddItem(new BMenuItem("Force delete",
		new BMessage(M_IMAGE_FORCE_REMOVE)));
}


void ImagesView::Invoked(ObjectRow* row)
{
	(new RunWindow(row->Name().String(), Window()))->Show();
}


void ImagesView::_Remove(bool force)
{
	std::vector<BString> references;
	for (ObjectRow* row : SelectedRows()) {
		// A tag is removed by its name, which deletes the image with its
		// last tag; an untagged image by its ID.
		references.push_back(row->Name() == Util::ShortId(row->extra.String())
			? row->extra : row->Name());
	}
	if (references.empty())
		return;
	BString text;
	if (references.size() == 1) {
		text.SetToFormat("Delete the image \"%s\"?%s",
			references[0].String(), force ? " Containers using it lose "
				"their image." : "");
	} else {
		text.SetToFormat("Delete %d images?", (int)references.size());
	}
	if (!ConfirmDelete(text.String(), force ? "Force delete" : "Delete"))
		return;
	PerformOn(references, "Deleting",
		[force](DockerClient& client, const BString& id, BString& error) {
			return client.ImageRemove(id.String(), force, &error);
		});
}


void ImagesView::_Inspect()
{
	ObjectRow* row = SelectedRow();
	if (row == NULL)
		return;
	BString title(row->Name());
	title << " (image)";
	TextWindow* window = new TextWindow(title.String(), Window());
	BString socket = SocketPath(), id = row->extra;
	window->SetLoader([socket, id](BString& text, BString& error) {
		DockerClient client(socket.String());
		BString json;
		BString path("/images/");
		path << DockerClient::Escape(id.String()) << "/json";
		status_t status = client.Get(path.String(), &json, &error);
		if (status == B_OK)
			text = djson::Pretty(json.String());
		return status;
	});
	window->Show();
}


void ImagesView::_History()
{
	ObjectRow* row = SelectedRow();
	if (row == NULL)
		return;
	BString title(row->Name());
	title << " (history)";
	TextWindow* window = new TextWindow(title.String(), Window());
	BString socket = SocketPath(), id = row->extra;
	window->SetLoader([socket, id](BString& text, BString& error) {
		DockerClient client(socket.String());
		BString json;
		BString path("/images/");
		path << DockerClient::Escape(id.String()) << "/history";
		status_t status = client.Get(path.String(), &json, &error);
		if (status != B_OK)
			return status;
		djson::Value* root = djson::Parse(json.String());
		text.SetToFormat("%-22s %10s   %s\n", "CREATED", "SIZE",
			"CREATED BY");
		for (int32 i = 0; i < djson::Length(root); i++) {
			const djson::Value* layer = djson::At(root, i);
			BString createdBy = djson::GetString(layer, "CreatedBy");
			createdBy.ReplaceAll("/bin/sh -c #(nop) ", "");
			createdBy.ReplaceAll("\n", " ");
			BString line;
			line.SetToFormat("%-22s %10s   %s\n",
				Util::FormatAge((time_t)djson::GetNumber(layer, "Created"))
					.String(),
				Util::FormatSize(djson::GetNumber(layer, "Size")).String(),
				createdBy.String());
			text << line;
		}
		djson::FreeValue(root);
		return B_OK;
	});
	window->Show();
}


void ImagesView::_Tag(BMessage* message)
{
	BString id, target;
	if (message->FindString("id", &id) != B_OK) {
		ObjectRow* row = SelectedRow();
		if (row == NULL)
			return;
		BMessage reply(M_IMAGE_TAG);
		reply.AddString("id", row->extra);
		BString initial(row->Name());
		if (initial == Util::ShortId(row->extra.String()))
			initial = "";
		PromptWindow* prompt = new PromptWindow("Tag image", "New tag:",
			initial.String(), "Tag", BMessenger(this), reply, Window());
		prompt->Show();
		return;
	}
	if (message->FindString("text", &target) != B_OK || target.Length() == 0)
		return;
	Perform("Tagging", [id, target](DockerClient& client, BString& error) {
		BString name, tag;
		Pull::SplitReference(target.String(), name, tag);
		BString path;
		path.SetToFormat("/images/%s/tag?repo=%s&tag=%s",
			DockerClient::Escape(id.String()).String(),
			DockerClient::Escape(name.String()).String(),
			DockerClient::Escape(tag.String()).String());
		return client.Call("POST", path.String(), NULL, NULL, &error);
	});
}


void ImagesView::_Push()
{
	ObjectRow* row = SelectedRow();
	if (row == NULL)
		return;
	BString command(Util::DockerCommand(SocketPath().String()));
	command << " push " << Util::ShellQuote(row->Name().String());
	BString title("Push ");
	title << row->Name();
	if (Util::RunInTerminal(title.String(), command.String(), true) != B_OK)
		Util::ShowError("Terminal could not be started.");
}


void ImagesView::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case M_IMAGE_RUN:
	{
		ObjectRow* row = SelectedRow();
		(new RunWindow(row != NULL ? row->Name().String() : "",
			Window()))->Show();
		break;
	}
	case M_IMAGE_PULL:
		(new PullWindow("", Window()))->Show();
		break;
	case M_IMAGE_REPULL:
	{
		ObjectRow* row = SelectedRow();
		if (row != NULL)
			(new PullWindow(row->Name().String(), Window(), true))->Show();
		break;
	}
	case M_HUB_SEARCH:
		HubSearchWindow::Open(Window());
		break;
	case M_IMAGE_REMOVE:
		_Remove(false);
		break;
	case M_IMAGE_FORCE_REMOVE:
		_Remove(true);
		break;
	case M_IMAGE_INSPECT:
		_Inspect();
		break;
	case M_IMAGE_HISTORY:
		_History();
		break;
	case M_IMAGE_TAG:
		_Tag(message);
		break;
	case M_IMAGE_PUSH:
		_Push();
		break;
	default:
		ResourceView::MessageReceived(message);
		break;
	}
}
