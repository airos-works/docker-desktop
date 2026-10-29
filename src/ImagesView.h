// SPDX-License-Identifier: MIT

#ifndef DOCKER_IMAGES_VIEW_H
#define DOCKER_IMAGES_VIEW_H

#include "ResourceView.h"

class ImagesView : public ResourceView {
public:
	ImagesView();

	void MessageReceived(BMessage* message) override;

	int32 CountImages() const { return fCount; }
	double TotalSize() const { return fTotalSize; }

protected:
	FetchFunction Fetcher() const override;
	void Populate(const BMessage& result) override;
	void UpdateButtons() override;
	void BuildContextMenu(BPopUpMenu* menu) override;
	void Invoked(ObjectRow* row) override;
	const char* EmptyText() const override;

private:
	void _Remove(bool force);
	void _Inspect();
	void _History();
	void _Tag(BMessage* message);
	void _Push();

	BButton* fRunButton;
	BButton* fDeleteButton;
	BButton* fInspectButton;
	BButton* fHistoryButton;
	int32 fCount;
	double fTotalSize;
};

#endif
