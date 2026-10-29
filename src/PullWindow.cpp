// SPDX-License-Identifier: MIT

#include "PullWindow.h"

#include "Pull.h"
#include "RunWindow.h"
#include "Settings.h"
#include "Util.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <thread>

namespace {

enum {
	kPull = 'pwpl',
	kProgress = 'pwpg',
	kDone = 'pwdn',
	kRun = 'pwrn',
	kClose = 'pwcl',
};

} // namespace


PullWindow::PullWindow(const char* reference, BWindow* parent,
	bool autoStart)
	:
	BWindow(BRect(0, 0, 520, 360), "Pull an image", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
	fReference = new BTextControl("reference", "Image:", reference,
		new BMessage(kPull));
	fReference->SetToolTip("A name such as nginx, nginx:1.27 or "
		"ghcr.io/owner/app:tag");
	fPlatform = new BTextControl("platform", "Platform:", "", NULL);
	fPlatform->SetToolTip("Leave empty to let Docker Desktop choose: the "
		"Engine's own platform, or else the Linux one of its architecture "
		"(linux/amd64, linux/arm64)");
	fProgress = new BStatusBar("progress");
	fProgress->SetMaxValue(1);
	fStatus = new BStringView("status", "");
	Util::MakeElastic(fStatus);
	fDetails = new BTextView("details");
	fDetails->MakeEditable(false);
	fDetails->SetFontAndColor(be_fixed_font);
	fDetails->SetInsets(4, 2, 4, 2);
	BScrollView* scroll = new BScrollView("scroll", fDetails, 0, false, true);
	scroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 140));

	fPullButton = new BButton("pull", "Pull", new BMessage(kPull));
	fRunButton = new BButton("run", "Run…", new BMessage(kRun));
	fRunButton->SetEnabled(false);
	fCloseButton = new BButton("close", "Close", new BMessage(kClose));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.AddTextControl(fReference, 0, 0)
			.AddTextControl(fPlatform, 0, 1)
		.End()
		.Add(fProgress)
		.Add(fStatus)
		.Add(BSpaceLayoutItem::CreateHorizontalStrut(
			be_plain_font->StringWidth("M") * 36))
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fCloseButton)
			.Add(fRunButton)
			.Add(fPullButton)
		.End()
		.SetInsets(B_USE_WINDOW_SPACING);
	SetDefaultButton(fPullButton);
	Util::CenterOver(this, parent);
	fReference->MakeFocus(true);

	if (autoStart && reference != NULL && reference[0] != '\0')
		PostMessage(kPull);
}


PullWindow::~PullWindow()
{
	if (fCanceler)
		fCanceler->Cancel();
}


bool PullWindow::QuitRequested()
{
	if (fCanceler)
		fCanceler->Cancel();
	return true;
}


void PullWindow::_Start()
{
	if (fCanceler)
		return;
	BString reference(fReference->Text());
	reference.Trim();
	if (reference.Length() == 0)
		return;

	fPullButton->SetEnabled(false);
	fRunButton->SetEnabled(false);
	fCloseButton->SetLabel("Cancel");
	fProgress->Reset();
	fProgress->SetMaxValue(1);
	fDetails->SetText("");
	BString text("Pulling ");
	text << reference << "…";
	fStatus->SetText(text.String());

	CancelerRef canceler(new StreamCanceler);
	fCanceler = canceler;
	BMessenger target(this);
	BString socket = Settings::Default().SocketPath();
	BString platform(fPlatform->Text());
	platform.Trim();
	std::thread([socket, reference, platform, canceler, target]() {
		DockerClient client(socket.String());
		BString error;
		bigtime_t lastUpdate = 0;
		status_t status = Pull::Run(client, reference.String(),
			platform.String(), canceler,
			[&](const Pull::Progress& progress) {
				// At most ten updates a second.
				bigtime_t now = system_time();
				if (now - lastUpdate < 100000)
					return;
				lastUpdate = now;
				double current, total;
				progress.Totals(&current, &total);
				BMessage message(kProgress);
				message.AddDouble("current", current);
				message.AddDouble("total", total);
				message.AddString("details", progress.Describe());
				target.SendMessage(&message);
			}, error);
		BMessage done(kDone);
		done.AddString("reference", reference);
		if (status != B_OK)
			done.AddString("error", error);
		target.SendMessage(&done);
	}).detach();
}


void PullWindow::_Finished(BMessage* message)
{
	fCanceler.reset();
	fPullButton->SetEnabled(true);
	fCloseButton->SetLabel("Close");
	BString error, reference;
	message->FindString("reference", &reference);
	if (message->FindString("error", &error) == B_OK) {
		BString text("Pull failed: ");
		text << error;
		fStatus->SetText(text.String());
		fStatus->SetToolTip(error.String());
		BString details("\n");
		details << text << "\n";
		fDetails->Insert(fDetails->TextLength(), details.String(),
			details.Length());
		return;
	}
	fProgress->SetTo(fProgress->MaxValue());
	BString text("Pulled ");
	text << reference;
	fStatus->SetText(text.String());
	fPulled = reference;
	fRunButton->SetEnabled(true);
	SetDefaultButton(fRunButton);
}


void PullWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kPull:
		_Start();
		break;

	case kProgress:
	{
		double current = message->GetDouble("current", 0);
		double total = message->GetDouble("total", 0);
		if (total > 0) {
			fProgress->SetMaxValue(total);
			fProgress->SetTo(current, NULL,
				(Util::FormatSize(current) << " / "
					<< Util::FormatSize(total)).String());
		}
		fDetails->SetText(message->GetString("details", ""));
		break;
	}

	case kDone:
		_Finished(message);
		break;

	case kRun:
		if (fPulled.Length() > 0) {
			(new RunWindow(fPulled.String(), this))->Show();
			Quit();
		}
		break;

	case kClose:
		if (fCanceler) {
			fCanceler->Cancel();
			fStatus->SetText("Cancelling…");
		} else
			Quit();
		break;

	default:
		BWindow::MessageReceived(message);
		break;
	}
}
