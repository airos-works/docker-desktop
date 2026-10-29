// SPDX-License-Identifier: MIT
// A window that shows a text: an object's inspect JSON, an image's
// history, the Engine log. It can load (and reload) the text on a worker
// thread, and optionally let it be edited and saved.

#ifndef DOCKER_TEXT_WINDOW_H
#define DOCKER_TEXT_WINDOW_H

#include <String.h>
#include <Window.h>

#include <functional>

class BButton;
class BMessageRunner;
class BStringView;
class BTextView;

// Fills text, or error, on a worker thread.
typedef std::function<status_t(BString& text, BString& error)> TextLoader;
typedef std::function<status_t(const BString& text, BString& error)>
	TextSaver;

class TextWindow : public BWindow {
public:
	TextWindow(const char* title, BWindow* parent);
	~TextWindow();

	void SetText(const char* text);
	// Loads the text now, and again on Refresh (and every interval, if
	// given, keeping the view scrolled to the end when it was there).
	void SetLoader(TextLoader loader, bigtime_t interval = 0);
	// Makes the text editable; Save runs saver on a worker thread. (Not
	// for windows that reload on an interval.)
	void SetSaver(TextSaver saver, const char* saveLabel = "Save");

	void MessageReceived(BMessage* message) override;

private:
	void _Load();

	BTextView* fTextView;
	BStringView* fStatus;
	BButton* fRefreshButton;
	BButton* fSaveButton;
	TextLoader fLoader;
	TextSaver fSaver;
	BMessageRunner* fRunner;
	bool fLoading;
	bool fFirstLoad;
};

#endif
