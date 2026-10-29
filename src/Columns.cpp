// SPDX-License-Identifier: MIT

#include "Columns.h"

#include <View.h>

#include <math.h>
#include <string.h>

static const float kDotSize = 8;

ObjectRow::ObjectRow(const char* id, const char* name)
	:
	port(-1),
	isGroup(false),
	seen(true),
	added(false),
	parent(NULL),
	fId(id),
	fName(name)
{
}


SortField::SortField(const char* text, double key)
	:
	BStringField(text),
	fKey(key)
{
}


SortColumn::SortColumn(const char* title, float width, float minWidth,
	float maxWidth, alignment align)
	:
	BStringColumn(title, width, minWidth, maxWidth, B_TRUNCATE_END, align)
{
}


int SortColumn::CompareFields(BField* field1, BField* field2)
{
	double key1 = static_cast<SortField*>(field1)->Key();
	double key2 = static_cast<SortField*>(field2)->Key();
	return key1 < key2 ? -1 : key1 > key2 ? 1 : 0;
}


bool SortColumn::AcceptsField(const BField* field) const
{
	return dynamic_cast<const SortField*>(field) != NULL;
}


StateField::StateField(const char* text, const char* state)
	:
	BStringField(text),
	fState(state)
{
}


StateColumn::StateColumn(const char* title, float width, float minWidth,
	float maxWidth)
	:
	BStringColumn(title, width, minWidth, maxWidth, B_TRUNCATE_END)
{
}


rgb_color StateColor(const char* state)
{
	if (strcmp(state, "running") == 0)
		return make_color(47, 168, 79);
	if (strcmp(state, "paused") == 0 || strcmp(state, "restarting") == 0)
		return make_color(230, 160, 30);
	if (strcmp(state, "dead") == 0 || strcmp(state, "removing") == 0)
		return make_color(210, 60, 50);
	if (strcmp(state, "partial") == 0)
		return make_color(120, 190, 120);
	return make_color(150, 150, 150);
}


void StateColumn::DrawField(BField* field, BRect rect, BView* parent)
{
	StateField* stateField = static_cast<StateField*>(field);
	float dot = ceilf(kDotSize * be_plain_font->Size() / 12);
	float left = rect.left + 6;
	BRect dotRect(left, rect.top + (rect.Height() - dot) / 2, left + dot,
		rect.top + (rect.Height() + dot) / 2);

	rgb_color old = parent->HighColor();
	parent->SetDrawingMode(B_OP_ALPHA);
	parent->SetHighColor(StateColor(stateField->State().String()));
	parent->FillEllipse(dotRect);
	parent->SetHighColor(old);
	parent->SetDrawingMode(B_OP_OVER);

	BRect textRect(rect);
	textRect.left = dotRect.right + 2;
	BStringColumn::DrawField(field, textRect, parent);
}


float StateColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	return BStringColumn::GetPreferredWidth(field, parent) + kDotSize + 8;
}


bool StateColumn::AcceptsField(const BField* field) const
{
	return dynamic_cast<const StateField*>(field) != NULL;
}
