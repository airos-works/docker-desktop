// SPDX-License-Identifier: MIT
// Rows and columns for the BColumnListViews of the resource tabs.

#ifndef DOCKER_COLUMNS_H
#define DOCKER_COLUMNS_H

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <String.h>

// A row that knows the Engine object it shows.
class ObjectRow : public BRow {
public:
	ObjectRow(const char* id, const char* name);

	const BString& Id() const { return fId; }
	const BString& Name() const { return fName; }
	void SetName(const char* name) { fName = name; }

	// Free-form extras (a container's state, an image's reference, ...).
	BString state;
	BString extra;
	int32 port;
	bool isGroup;
	bool seen;
	// For ResourceView: whether the row is in the list, and under which row.
	bool added;
	ObjectRow* parent;

private:
	BString fId;
	BString fName;
};

// A string field that sorts by a number (a time, a size, a count) instead
// of its text.
class SortField : public BStringField {
public:
	SortField(const char* text, double key);

	double Key() const { return fKey; }

private:
	double fKey;
};

class SortColumn : public BStringColumn {
public:
	SortColumn(const char* title, float width, float minWidth,
		float maxWidth, alignment align = B_ALIGN_LEFT);

	int CompareFields(BField* field1, BField* field2) override;
	bool AcceptsField(const BField* field) const override;
};

// A string drawn after a coloured dot for the state of a container
// ("running" green, "exited" grey, ...).
class StateField : public BStringField {
public:
	StateField(const char* text, const char* state);

	const BString& State() const { return fState; }

private:
	BString fState;
};

class StateColumn : public BStringColumn {
public:
	StateColumn(const char* title, float width, float minWidth,
		float maxWidth);

	void DrawField(BField* field, BRect rect, BView* parent) override;
	float GetPreferredWidth(BField* field, BView* parent) const override;
	bool AcceptsField(const BField* field) const override;
};

rgb_color StateColor(const char* state);

#endif
