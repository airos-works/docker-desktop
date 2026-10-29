// SPDX-License-Identifier: MIT

#ifndef DOCKER_VOLUMES_VIEW_H
#define DOCKER_VOLUMES_VIEW_H

#include "ResourceView.h"

class VolumesView : public ResourceView {
public:
	VolumesView();

	void MessageReceived(BMessage* message) override;

protected:
	FetchFunction Fetcher() const override;
	void Populate(const BMessage& result) override;
	void UpdateButtons() override;
	void BuildContextMenu(BPopUpMenu* menu) override;
	void Invoked(ObjectRow* row) override;
	const char* EmptyText() const override;

private:
	void _Create(BMessage* message);
	void _Remove();
	void _Inspect();
	void _Open();
	void _Prune();

	BButton* fDeleteButton;
	BButton* fInspectButton;
};

class NetworksView : public ResourceView {
public:
	NetworksView();

	void MessageReceived(BMessage* message) override;

protected:
	FetchFunction Fetcher() const override;
	void Populate(const BMessage& result) override;
	void UpdateButtons() override;
	void BuildContextMenu(BPopUpMenu* menu) override;
	void Invoked(ObjectRow* row) override;
	const char* EmptyText() const override;

private:
	void _Create(BMessage* message);
	void _Remove();
	void _Inspect();
	void _Prune();

	BButton* fDeleteButton;
	BButton* fInspectButton;
};

#endif
