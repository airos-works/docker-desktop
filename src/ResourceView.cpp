// SPDX-License-Identifier: MIT

#include "ResourceView.h"

#include "Columns.h"
#include "DockerClient.h"
#include "Messages.h"
#include "Settings.h"
#include "Util.h"

#include <Button.h>
#include <CardLayout.h>
#include <ColumnListView.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <memory>

namespace {

const bigtime_t kRefreshDelay = 250000;

// Opens the context menu on a right click in the list.
class ContextMenuFilter : public BMessageFilter {
public:
	ContextMenuFilter(ResourceView* owner, BColumnListView* list)
		:
		BMessageFilter(B_MOUSE_DOWN),
		fOwner(owner),
		fList(list)
	{
	}

	filter_result Filter(BMessage* message, BHandler** target) override
	{
		int32 buttons = 0;
		message->FindInt32("buttons", &buttons);
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0)
			return B_DISPATCH_MESSAGE;
		BPoint where;
		if (message->FindPoint("where", &where) != B_OK)
			return B_DISPATCH_MESSAGE;

		BRow* row = fList->RowAt(where);
		if (row == NULL)
			return B_SKIP_MESSAGE;
		bool selected = false;
		for (BRow* r = fList->CurrentSelection(); r != NULL;
				r = fList->CurrentSelection(r)) {
			if (r == row) {
				selected = true;
				break;
			}
		}
		if (!selected) {
			fList->DeselectAll();
			fList->AddToSelection(row);
		}
		fOwner->ShowContextMenu(fList->ScrollView()->ConvertToScreen(where));
		return B_SKIP_MESSAGE;
	}

private:
	ResourceView* fOwner;
	BColumnListView* fList;
};

} // namespace


ResourceView::ResourceView(const char* name, const char* label)
	:
	BView(name, B_WILL_DRAW),
	fLabel(label),
	fHasResult(false),
	fFetching(false),
	fFetchAgain(false),
	fRefreshScheduled(false),
	fEngineRunning(false),
	fRefreshRunner(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fList = new BColumnListView("list", B_NAVIGABLE, B_FANCY_BORDER, false);
	fList->SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(M_SELECTION_CHANGED));
	fList->SetInvocationMessage(new BMessage(M_ROW_INVOKED));

	fSearch = new BTextControl("search", NULL, "",
		new BMessage(M_SEARCH_CHANGED));
	fSearch->SetModificationMessage(new BMessage(M_SEARCH_CHANGED));
	fSearch->SetExplicitMinSize(BSize(be_plain_font->StringWidth("M") * 14,
		B_SIZE_UNSET));
	fSearch->SetExplicitMaxSize(BSize(be_plain_font->StringWidth("M") * 20,
		B_SIZE_UNSET));
	BString placeholder("Search ");
	placeholder << BString(label).ToLower();
	fSearch->TextView()->SetToolTip(placeholder.String());

	fEmptyLabel = new BStringView("empty", "");
	fEmptyLabel->SetAlignment(B_ALIGN_CENTER);
	fEmptyLabel->SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);
	fEmptyLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	BView* empty = BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.AddGlue()
		.Add(fEmptyLabel)
		.AddGlue()
		.View();

	BView* toolbarView = new BView("toolbar", 0);
	fToolbar = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	toolbarView->SetLayout(fToolbar);
	BLayoutBuilder::Group<>(fToolbar)
		.AddGlue()
		.Add(new BStringView("searchLabel", "Search:"))
		.Add(fSearch);

	BView* cards = new BView("cards", 0);
	fCards = new BCardLayout();
	cards->SetLayout(fCards);
	fCards->AddView(fList);
	fCards->AddView(empty);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(toolbarView)
		.Add(cards)
		.SetInsets(B_USE_SMALL_SPACING);
	_UpdateCard();
}


ResourceView::~ResourceView()
{
	delete fRefreshRunner;
}


void ResourceView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fList->SetTarget(this);
	fSearch->SetTarget(this);
	for (int32 i = 0; i < fToolbar->CountItems(); i++) {
		BControl* control
			= dynamic_cast<BControl*>(fToolbar->ItemAt(i)->View());
		if (control != NULL)
			control->SetTarget(this);
	}
	fList->ScrollView()->AddFilter(new ContextMenuFilter(this, fList));

	BMessage state;
	if (Settings::Default().GetListState(Name(), &state))
		fList->LoadState(&state);
	UpdateButtons();
}


void ResourceView::SaveState()
{
	BMessage state;
	fList->SaveState(&state);
	Settings::Default().SetListState(Name(), state);
}


void ResourceView::FocusSearch()
{
	fSearch->MakeFocus(true);
}


BButton* ResourceView::AddButton(const char* label, uint32 what,
	const char* tip)
{
	BButton* button = new BButton(label, label, new BMessage(what));
	if (tip != NULL)
		button->SetToolTip(tip);
	AddToolView(button);
	return button;
}


void ResourceView::AddToolView(BView* view)
{
	// Before the glue that pushes the search field to the right.
	fToolbar->AddView(fToolbar->CountItems() - 3, view);
}


BString ResourceView::SocketPath() const
{
	return Settings::Default().SocketPath();
}


void ResourceView::Refresh()
{
	if (!fEngineRunning)
		return;
	if (fFetching) {
		fFetchAgain = true;
		return;
	}
	fFetching = true;
	FetchFunction fetch = Fetcher();
	BString socket = SocketPath();
	Util::RunTask(BMessenger(this), M_LIST_RESULT,
		[fetch, socket](BMessage& reply) {
			DockerClient client(socket.String());
			fetch(client, reply);
		});
}


void ResourceView::ScheduleRefresh()
{
	if (fRefreshScheduled || Window() == NULL)
		return;
	fRefreshScheduled = true;
	delete fRefreshRunner;
	BMessage refresh(M_REFRESH);
	fRefreshRunner = new BMessageRunner(BMessenger(this), &refresh,
		kRefreshDelay, 1);
}


void ResourceView::SetEngineRunning(bool running)
{
	if (running == fEngineRunning)
		return;
	fEngineRunning = running;
	if (running)
		Refresh();
	else {
		BeginUpdate();
		EndUpdate();
		fHasResult = false;
	}
	_UpdateCard();
	UpdateButtons();
}


void ResourceView::_UpdateCard()
{
	bool empty = fList->CountRows() == 0;
	if (!fEngineRunning)
		fEmptyLabel->SetText("The Docker Engine is not running.");
	else if (!fHasResult)
		fEmptyLabel->SetText("Loading…");
	else if (fSearch->Text()[0] != '\0')
		fEmptyLabel->SetText("Nothing matches your search.");
	else
		fEmptyLabel->SetText(EmptyText());
	int32 card = empty ? 1 : 0;
	if (fCards->VisibleIndex() != card)
		fCards->SetVisibleItem(card);
}


void ResourceView::BeginUpdate()
{
	for (auto& entry : fRows)
		entry.second->seen = false;
}


ObjectRow* ResourceView::UpsertRow(const char* id, const char* name)
{
	ObjectRow* row = FindRow(id);
	if (row == NULL) {
		row = new ObjectRow(id, name);
		fRows[id] = row;
	} else
		row->SetName(name);
	row->seen = true;
	return row;
}


void ResourceView::PlaceRow(ObjectRow* row, ObjectRow* parent)
{
	if (!row->added) {
		fList->AddRow(row, parent);
		row->added = true;
		row->parent = parent;
	} else if (row->parent != parent) {
		fList->RemoveRow(row);
		fList->AddRow(row, parent);
		row->parent = parent;
	} else
		fList->UpdateRow(row);
}


void ResourceView::EndUpdate()
{
	// Children go before their parents.
	for (int pass = 0; pass < 2; pass++) {
		for (auto it = fRows.begin(); it != fRows.end();) {
			ObjectRow* row = it->second;
			if (row->seen || row->isGroup != (pass == 1)) {
				++it;
				continue;
			}
			if (row->added)
				fList->RemoveRow(row);
			delete row;
			it = fRows.erase(it);
		}
	}
	_UpdateCard();
}


ObjectRow* ResourceView::FindRow(const char* id) const
{
	auto it = fRows.find(id);
	return it != fRows.end() ? it->second : NULL;
}


bool ResourceView::Matches(std::initializer_list<const char*> texts) const
{
	const char* search = fSearch->Text();
	if (search[0] == '\0')
		return true;
	for (const char* text : texts) {
		if (text != NULL && BString(text).IFindFirst(search) >= 0)
			return true;
	}
	return false;
}


std::vector<ObjectRow*> ResourceView::SelectedRows() const
{
	std::vector<ObjectRow*> rows;
	for (BRow* row = fList->CurrentSelection(); row != NULL;
			row = fList->CurrentSelection(row)) {
		rows.push_back(static_cast<ObjectRow*>(row));
	}
	return rows;
}


ObjectRow* ResourceView::SelectedRow() const
{
	return static_cast<ObjectRow*>(fList->CurrentSelection());
}


void ResourceView::PerformOn(const std::vector<BString>& ids,
	const char* status, RowAction action)
{
	if (ids.empty())
		return;
	BString text(status);
	if (ids.size() == 1)
		text << "…";
	else
		text << " " << (int32)ids.size() << " items…";
	SetStatus(text.String());

	BString socket = SocketPath();
	Util::RunTask(BMessenger(this), M_ACTION_DONE,
		[ids, socket, action](BMessage& reply) {
			DockerClient client(socket.String());
			for (const BString& id : ids) {
				BString error;
				if (action(client, id, error) != B_OK)
					reply.AddString("error", error);
			}
		});
}


void ResourceView::Perform(const char* status,
	std::function<status_t(DockerClient& client, BString& error)> work)
{
	BString text(status);
	text << "…";
	SetStatus(text.String());
	BString socket = SocketPath();
	Util::RunTask(BMessenger(this), M_ACTION_DONE,
		[socket, work](BMessage& reply) {
			DockerClient client(socket.String());
			BString error;
			if (work(client, error) != B_OK)
				reply.AddString("error", error);
		});
}


void ResourceView::SetStatus(const char* text)
{
	BMessage message(M_SET_STATUS);
	message.AddString("text", text);
	if (Window() != NULL)
		Window()->PostMessage(&message);
}


bool ResourceView::ConfirmDelete(const char* text, const char* action)
{
	if (!Settings::Default().ConfirmDeletes())
		return true;
	return Util::Confirm(text, action);
}


void ResourceView::ShowContextMenu(BPoint screenPoint)
{
	BPopUpMenu* menu = new BPopUpMenu("context", false, false);
	BuildContextMenu(menu);
	if (menu->CountItems() == 0) {
		delete menu;
		return;
	}
	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(screenPoint, true, true, true);
}


void ResourceView::_ResultReceived(BMessage* message)
{
	fFetching = false;
	if (fEngineRunning) {
		BString error;
		if (message->FindString("error", &error) == B_OK) {
			SetStatus(error.String());
		} else {
			fLastResult = *message;
			fHasResult = true;
			Populate(fLastResult);
			_UpdateCard();
			UpdateButtons();
			if (Window() != NULL)
				Window()->PostMessage(M_LIST_UPDATED);
		}
	}
	if (fFetchAgain) {
		fFetchAgain = false;
		Refresh();
	}
}


void ResourceView::_ActionDone(BMessage* message)
{
	BString errors;
	BString error;
	for (int32 i = 0; message->FindString("error", i, &error) == B_OK; i++) {
		if (errors.Length() > 0)
			errors << "\n\n";
		errors << error;
	}
	if (errors.Length() > 0) {
		SetStatus("The Docker Engine reported an error");
		Util::ShowError(errors.String());
	} else
		SetStatus(message->GetString("status", "Done"));
	Refresh();
}


void ResourceView::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case M_REFRESH:
		fRefreshScheduled = false;
		Refresh();
		break;

	case M_LIST_RESULT:
		_ResultReceived(message);
		break;

	case M_ACTION_DONE:
		_ActionDone(message);
		break;

	case M_SEARCH_CHANGED:
		if (fHasResult) {
			Populate(fLastResult);
			_UpdateCard();
			UpdateButtons();
		}
		break;

	case M_SELECTION_CHANGED:
		UpdateButtons();
		break;

	case M_ROW_INVOKED:
	{
		ObjectRow* row = SelectedRow();
		if (row != NULL)
			Invoked(row);
		break;
	}

	case M_COPY_ID:
	case M_COPY_NAME:
	{
		BString text;
		for (ObjectRow* row : SelectedRows()) {
			if (row->isGroup)
				continue;
			if (text.Length() > 0)
				text << "\n";
			text << (message->what == M_COPY_ID ? row->Id() : row->Name());
		}
		if (text.Length() > 0)
			Util::CopyToClipboard(text.String());
		break;
	}

	default:
		BView::MessageReceived(message);
		break;
	}
}
