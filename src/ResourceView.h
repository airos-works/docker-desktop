// SPDX-License-Identifier: MIT
// The common part of the Containers, Images, Volumes and Networks tabs: a
// toolbar with a search field, a column list of Engine objects that is
// updated in place, a context menu, and Engine calls on worker threads.

#ifndef DOCKER_RESOURCE_VIEW_H
#define DOCKER_RESOURCE_VIEW_H

#include <Message.h>
#include <String.h>
#include <View.h>

#include <functional>
#include <map>
#include <vector>

class BButton;
class BCardLayout;
class BColumnListView;
class BGroupLayout;
class BMessageRunner;
class BPopUpMenu;
class BStringView;
class BTextControl;
class DockerClient;
class ObjectRow;

typedef std::function<void(DockerClient& client, BMessage& reply)>
	FetchFunction;
typedef std::function<status_t(DockerClient& client, const BString& id,
	BString& error)> RowAction;

class ResourceView : public BView {
public:
	ResourceView(const char* name, const char* label);
	~ResourceView();

	void AttachedToWindow() override;
	void MessageReceived(BMessage* message) override;

	const char* Label() const { return fLabel.String(); }

	// Fetches the list again; calls in quick succession are coalesced.
	void Refresh();
	void ScheduleRefresh();
	void SetEngineRunning(bool running);
	void SaveState();
	void FocusSearch();

	// Shows a context menu for the selection at screenPoint.
	void ShowContextMenu(BPoint screenPoint);

protected:
	// Returns the work that fetches the list on a worker thread; it may
	// only use its arguments (the view can be gone by the time it runs).
	virtual FetchFunction Fetcher() const = 0;
	// Shows a fetched list (on the window thread).
	virtual void Populate(const BMessage& result) = 0;
	virtual void UpdateButtons() {}
	virtual void BuildContextMenu(BPopUpMenu* menu) {}
	virtual void Invoked(ObjectRow* row) {}
	virtual const char* EmptyText() const = 0;

	BColumnListView* List() const { return fList; }
	BGroupLayout* Toolbar() const { return fToolbar; }
	BButton* AddButton(const char* label, uint32 what,
		const char* tip = NULL);
	void AddToolView(BView* view);
	BString SocketPath() const;

	// Row bookkeeping for in-place updates: BeginUpdate; then for each
	// object UpsertRow, set its fields and PlaceRow; then EndUpdate removes
	// the rows not seen.
	void BeginUpdate();
	ObjectRow* UpsertRow(const char* id, const char* name);
	void PlaceRow(ObjectRow* row, ObjectRow* parent = NULL);
	void EndUpdate();
	ObjectRow* FindRow(const char* id) const;

	// Whether one of texts contains the search field's text.
	bool Matches(std::initializer_list<const char*> texts) const;

	std::vector<ObjectRow*> SelectedRows() const;
	ObjectRow* SelectedRow() const;

	// Runs action for each id on a worker thread, with a status text such
	// as "Stopping"; failures are shown and the list is refreshed after.
	void PerformOn(const std::vector<BString>& ids, const char* status,
		RowAction action);
	void Perform(const char* status,
		std::function<status_t(DockerClient& client, BString& error)> work);

	void SetStatus(const char* text);
	bool ConfirmDelete(const char* text, const char* action = "Delete");

private:
	void _ResultReceived(BMessage* message);
	void _ActionDone(BMessage* message);
	void _UpdateCard();

	BString fLabel;
	BColumnListView* fList;
	BGroupLayout* fToolbar;
	BTextControl* fSearch;
	BCardLayout* fCards;
	BStringView* fEmptyLabel;
	std::map<BString, ObjectRow*> fRows;
	BMessage fLastResult;
	bool fHasResult;
	bool fFetching;
	bool fFetchAgain;
	bool fRefreshScheduled;
	bool fEngineRunning;
	BMessageRunner* fRefreshRunner;
};

#endif
