// SPDX-License-Identifier: MIT

#include "HubSearchWindow.h"

#include "Columns.h"
#include "DockerClient.h"
#include "Json.h"
#include "PullWindow.h"
#include "RunWindow.h"
#include "Settings.h"
#include "Util.h"

#include <Autolock.h>
#include <Button.h>
#include <ColumnListView.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <StringView.h>
#include <TextControl.h>

namespace {

enum {
	kSearch = 'hwse',
	kResults = 'hwrs',
	kSelected = 'hwsl',
	kPull = 'hwpl',
	kRun = 'hwrn',
	kHub = 'hwhb',
	kActivate = 'hwac',
};

enum {
	kNameColumn,
	kDescriptionColumn,
	kStarsColumn,
	kOfficialColumn,
};

BLocker sLock("hub search");
BMessenger sWindow;

} // namespace


void HubSearchWindow::Open(BWindow* parent, const char* term)
{
	BAutolock lock(sLock);
	if (sWindow.IsValid()) {
		BMessage activate(kActivate);
		if (term != NULL)
			activate.AddString("term", term);
		sWindow.SendMessage(&activate);
		return;
	}
	HubSearchWindow* window = new HubSearchWindow(parent);
	sWindow = BMessenger(window);
	if (term != NULL && term[0] != '\0') {
		window->fTerm->SetText(term);
		window->PostMessage(kSearch);
	}
	window->Show();
}


HubSearchWindow::HubSearchWindow(BWindow* parent)
	:
	BWindow(BRect(0, 0, 680, 460), "Search Docker Hub", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fSearchCount(0)
{
	fTerm = new BTextControl("term", NULL, "", new BMessage(kSearch));
	fTerm->SetToolTip("Search for images, for example nginx or postgres");
	fResults = new BColumnListView("results", B_NAVIGABLE, B_FANCY_BORDER,
		false);
	fResults->AddColumn(new BStringColumn("Name", 200, 60, 500,
		B_TRUNCATE_END), kNameColumn);
	fResults->AddColumn(new BStringColumn("Description", 300, 60, 1000,
		B_TRUNCATE_END), kDescriptionColumn);
	fResults->AddColumn(new SortColumn("Stars", 70, 40, 200, B_ALIGN_RIGHT),
		kStarsColumn);
	fResults->AddColumn(new BStringColumn("Official", 70, 40, 200,
		B_TRUNCATE_END), kOfficialColumn);
	fResults->SetSelectionMessage(new BMessage(kSelected));
	fResults->SetInvocationMessage(new BMessage(kPull));
	fResults->SetTarget(this);
	fResults->SetSortColumn(fResults->ColumnAt(kStarsColumn), false, false);

	fStatus = new BStringView("status", "");
	Util::MakeElastic(fStatus);
	fPullButton = new BButton("pull", "Pull", new BMessage(kPull));
	fRunButton = new BButton("run", "Run…", new BMessage(kRun));
	fHubButton = new BButton("hub", "View on Docker Hub", new BMessage(kHub));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fTerm)
			.Add(new BButton("search", "Search", new BMessage(kSearch)))
		.End()
		.Add(fResults)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fStatus)
			.Add(fHubButton)
			.Add(fRunButton)
			.Add(fPullButton)
		.End()
		.SetInsets(B_USE_WINDOW_SPACING);
	fTerm->MakeFocus(true);
	_UpdateButtons();
	Util::CenterOver(this, parent);
}


HubSearchWindow::~HubSearchWindow()
{
	BAutolock lock(sLock);
	sWindow = BMessenger();
}


void HubSearchWindow::_Search()
{
	BString term(fTerm->Text());
	term.Trim();
	if (term.Length() == 0)
		return;
	fSearchCount++;
	fStatus->SetText("Searching…");
	BString socket = Settings::Default().SocketPath();
	int32 search = fSearchCount;
	Util::RunTask(BMessenger(this), kResults,
		[socket, term, search](BMessage& reply) {
			DockerClient client(socket.String());
			BString json, error;
			BString path("/images/search?limit=50&term=");
			path << DockerClient::Escape(term.String());
			reply.AddInt32("search", search);
			if (client.Get(path.String(), &json, &error) == B_OK)
				reply.AddString("json", json);
			else
				reply.AddString("error", error);
		});
}


void HubSearchWindow::_Results(BMessage* message)
{
	// Only the latest search counts.
	if (message->GetInt32("search", 0) != fSearchCount)
		return;
	BString error;
	if (message->FindString("error", &error) == B_OK) {
		fStatus->SetText(error.String());
		return;
	}
	fResults->Clear();
	djson::Value* root = djson::Parse(message->GetString("json", "[]"));
	int32 count = djson::Length(root);
	for (int32 i = 0; i < count; i++) {
		const djson::Value* result = djson::At(root, i);
		double stars = djson::GetNumber(result, "star_count");
		BString starsText;
		starsText.SetToFormat("%.0f", stars);
		BRow* row = new BRow();
		row->SetField(new BStringField(djson::GetString(result, "name")),
			kNameColumn);
		row->SetField(new BStringField(djson::GetString(result,
			"description")), kDescriptionColumn);
		row->SetField(new SortField(starsText.String(), stars), kStarsColumn);
		row->SetField(new BStringField(djson::GetBool(result, "is_official")
			? "Official" : ""), kOfficialColumn);
		fResults->AddRow(row);
	}
	djson::FreeValue(root);
	BString status;
	status.SetToFormat("%" B_PRId32 " result%s", count, count == 1 ? "" : "s");
	fStatus->SetText(status.String());
	_UpdateButtons();
}


BString HubSearchWindow::_SelectedName() const
{
	BRow* row = fResults->CurrentSelection();
	if (row == NULL)
		return "";
	BStringField* field = static_cast<BStringField*>(
		row->GetField(kNameColumn));
	return field != NULL ? field->String() : "";
}


void HubSearchWindow::_UpdateButtons()
{
	bool selected = fResults->CurrentSelection() != NULL;
	fPullButton->SetEnabled(selected);
	fRunButton->SetEnabled(selected);
	fHubButton->SetEnabled(selected);
}


void HubSearchWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kActivate:
	{
		BString term;
		if (message->FindString("term", &term) == B_OK && term.Length() > 0) {
			fTerm->SetText(term.String());
			_Search();
		}
		Activate();
		break;
	}
	case kSearch:
		_Search();
		break;
	case kResults:
		_Results(message);
		break;
	case kSelected:
		_UpdateButtons();
		break;
	case kPull:
	{
		BString name = _SelectedName();
		if (name.Length() > 0)
			(new PullWindow(name.String(), this, true))->Show();
		break;
	}
	case kRun:
	{
		BString name = _SelectedName();
		if (name.Length() > 0)
			(new RunWindow(name.String(), this))->Show();
		break;
	}
	case kHub:
	{
		BString name = _SelectedName();
		if (name.Length() == 0)
			break;
		BString url("https://hub.docker.com/");
		if (name.FindFirst('/') < 0)
			url << "_/" << name;
		else
			url << "r/" << name;
		Util::OpenURL(url.String());
		break;
	}
	default:
		BWindow::MessageReceived(message);
		break;
	}
}
