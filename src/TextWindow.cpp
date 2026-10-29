// SPDX-License-Identifier: MIT

#include "TextWindow.h"

#include "Util.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>

namespace {

enum {
	kRefresh = 'trfr',
	kLoaded = 'tlod',
	kSave = 'tsav',
	kSaved = 'tsvd',
	kCopy = 'tcpy',
};

} // namespace


TextWindow::TextWindow(const char* title, BWindow* parent)
	:
	BWindow(BRect(0, 0, 640, 520), title, B_DOCUMENT_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fRunner(NULL),
	fLoading(false),
	fFirstLoad(true)
{
	fTextView = new BTextView("text");
	fTextView->MakeEditable(false);
	fTextView->SetStylable(false);
	fTextView->SetWordWrap(false);
	fTextView->SetFontAndColor(be_fixed_font);
	fTextView->SetInsets(6, 4, 6, 4);
	BScrollView* scroll = new BScrollView("scroll", fTextView, 0, true, true,
		B_NO_BORDER);

	fStatus = new BStringView("status", "");
	Util::MakeElastic(fStatus);
	fRefreshButton = new BButton("refresh", "Refresh",
		new BMessage(kRefresh));
	fRefreshButton->SetEnabled(false);
	fSaveButton = new BButton("save", "Save", new BMessage(kSave));
	fSaveButton->SetEnabled(false);
	fSaveButton->Hide();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fStatus)
			.Add(new BButton("copy", "Copy all", new BMessage(kCopy)))
			.Add(fRefreshButton)
			.Add(fSaveButton)
			.SetInsets(B_USE_SMALL_SPACING)
		.End();

	Util::CenterOver(this, parent);
	if (parent != NULL)
		MoveBy(20, 20);
}


TextWindow::~TextWindow()
{
	delete fRunner;
}


void TextWindow::SetText(const char* text)
{
	fTextView->SetText(text);
}


void TextWindow::SetLoader(TextLoader loader, bigtime_t interval)
{
	fLoader = loader;
	fRefreshButton->SetEnabled(true);
	if (interval > 0) {
		BMessage refresh(kRefresh);
		fRunner = new BMessageRunner(BMessenger(this), &refresh, interval);
	}
	fStatus->SetText("Loading…");
	_Load();
}


void TextWindow::SetSaver(TextSaver saver, const char* saveLabel)
{
	fSaver = saver;
	fTextView->MakeEditable(true);
	fSaveButton->SetLabel(saveLabel);
	fSaveButton->SetEnabled(true);
	fSaveButton->Show();
}


void TextWindow::_Load()
{
	if (fLoading || !fLoader)
		return;
	fLoading = true;
	TextLoader loader = fLoader;
	Util::RunTask(BMessenger(this), kLoaded, [loader](BMessage& reply) {
		BString text, error;
		if (loader(text, error) == B_OK)
			reply.AddString("text", text);
		else
			reply.AddString("error", error);
	});
}


void TextWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kRefresh:
		_Load();
		break;

	case kLoaded:
	{
		fLoading = false;
		BString text, error;
		if (message->FindString("error", &error) == B_OK) {
			fStatus->SetText(error.String());
			break;
		}
		message->FindString("text", &text);
		if (text == fTextView->Text()) {
			fStatus->SetText("");
			break;
		}
		// Keep following the end, or keep the place being read.
		float min, max;
		BScrollBar* bar = fTextView->ScrollBar(B_VERTICAL);
		bool atEnd = fFirstLoad;
		if (bar != NULL) {
			bar->GetRange(&min, &max);
			atEnd |= bar->Value() >= max - 2 && fRunner != NULL;
		}
		BPoint position = fTextView->Bounds().LeftTop();
		fTextView->SetText(text.String());
		if (atEnd && fRunner != NULL) {
			fTextView->Select(fTextView->TextLength(),
				fTextView->TextLength());
			fTextView->ScrollToSelection();
		} else if (!fFirstLoad)
			fTextView->ScrollTo(position);
		fFirstLoad = false;
		fStatus->SetText("");
		break;
	}

	case kSave:
	{
		if (!fSaver)
			break;
		fSaveButton->SetEnabled(false);
		fStatus->SetText("Saving…");
		TextSaver saver = fSaver;
		BString text(fTextView->Text());
		Util::RunTask(BMessenger(this), kSaved, [saver, text](BMessage& reply) {
			BString error;
			if (saver(text, error) != B_OK)
				reply.AddString("error", error);
		});
		break;
	}

	case kSaved:
	{
		fSaveButton->SetEnabled(true);
		BString error;
		if (message->FindString("error", &error) == B_OK) {
			fStatus->SetText("Not saved");
			Util::ShowError(error.String());
		} else
			fStatus->SetText("Saved");
		break;
	}

	case kCopy:
		Util::CopyToClipboard(fTextView->Text());
		break;

	default:
		BWindow::MessageReceived(message);
		break;
	}
}
