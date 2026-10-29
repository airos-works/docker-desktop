// SPDX-License-Identifier: MIT

#include "PromptWindow.h"

#include "Util.h"

#include <Button.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <TextControl.h>

static const uint32 kOk = 'okay';
static const uint32 kCancel = 'cncl';

PromptWindow::PromptWindow(const char* title, const char* label,
	const char* initial, const char* action, const BMessenger& target,
	const BMessage& reply, BWindow* parent)
	:
	BWindow(BRect(0, 0, 360, 100), title, B_TITLED_WINDOW_LOOK,
		B_MODAL_APP_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fReply(reply),
	fParent(parent)
{
	BView* gridView = new BView("grid", 0);
	fGrid = new BGridLayout(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING);
	gridView->SetLayout(fGrid);

	fOkButton = new BButton("ok", action, new BMessage(kOk));
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(gridView)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(kCancel)))
			.Add(fOkButton)
		.End()
		.SetInsets(B_USE_WINDOW_SPACING);
	SetDefaultButton(fOkButton);

	AddField("text", label, initial);
}


void PromptWindow::AddField(const char* name, const char* label,
	const char* initial, const char* tip)
{
	BTextControl* field = new BTextControl(name, label, initial, NULL);
	field->SetExplicitMinSize(BSize(be_plain_font->StringWidth("M") * 22,
		B_SIZE_UNSET));
	if (tip != NULL)
		field->SetToolTip(tip);
	int32 row = fFields.size();
	fGrid->AddItem(field->CreateLabelLayoutItem(), 0, row);
	fGrid->AddItem(field->CreateTextViewLayoutItem(), 1, row);
	fFields.push_back(field);
}


void PromptWindow::Show()
{
	Util::CenterOver(this, fParent);
	fFields[0]->MakeFocus(true);
	fFields[0]->TextView()->SelectAll();
	BWindow::Show();
}


void PromptWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kOk:
	{
		BMessage reply(fReply);
		for (BTextControl* field : fFields)
			reply.AddString(field->Name(), field->Text());
		fTarget.SendMessage(&reply);
		Quit();
		break;
	}
	case kCancel:
		Quit();
		break;
	default:
		BWindow::MessageReceived(message);
		break;
	}
}
