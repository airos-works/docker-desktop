// SPDX-License-Identifier: MIT
// A small dialog asking for one or more lines of text.

#ifndef DOCKER_PROMPT_WINDOW_H
#define DOCKER_PROMPT_WINDOW_H

#include <Message.h>
#include <Messenger.h>
#include <Window.h>

#include <vector>

class BButton;
class BGridLayout;
class BTextControl;

class PromptWindow : public BWindow {
public:
	// Sends reply to target, with the first field's text as "text" and
	// every field's text under its name.
	PromptWindow(const char* title, const char* label, const char* initial,
		const char* action, const BMessenger& target, const BMessage& reply,
		BWindow* parent = NULL);

	// Adds a field (call before Show); name is the reply's field name.
	void AddField(const char* name, const char* label, const char* initial,
		const char* tip = NULL);

	void Show() override;
	void MessageReceived(BMessage* message) override;

private:
	BGridLayout* fGrid;
	std::vector<BTextControl*> fFields;
	BButton* fOkButton;
	BMessenger fTarget;
	BMessage fReply;
	BWindow* fParent;
};

#endif
