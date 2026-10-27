// SPDX-License-Identifier: MIT

#include "Json.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace djson {

struct Parser {
	const char* p;
	const char* end;

	void SkipWs()
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;
	}

	bool Consume(char c)
	{
		SkipWs();
		if (p < end && *p == c) {
			p++;
			return true;
		}
		return false;
	}

	Value* ParseStringValue()
	{
		// *p == '"'
		p++;
		BString* s = new BString;
		while (p < end && *p != '"') {
			if (*p == '\\' && p + 1 < end) {
				p++;
				switch (*p) {
				case 'n': (*s) += '\n'; break;
				case 't': (*s) += '\t'; break;
				case 'r': (*s) += '\r'; break;
				case 'u': {
					// Skip \uXXXX (BMP-only handling is enough for our data).
					p++;
					for (int i = 0; i < 4 && p < end; i++)
						p++;
					s->Append("?", 1);
					continue;
				}
				default: (*s) += *p; break;
				}
				p++;
			} else {
				(*s) += *p;
				p++;
			}
		}
		if (p < end)
			p++; // closing quote
		Value* v = new Value;
		v->type = T_STRING;
		v->u.string = s;
		return v;
	}

	Value* ParseNumberValue()
	{
		char* next = NULL;
		double d = strtod(p, &next);
		if (next == p)
			return NULL;
		p = next;
		Value* v = new Value;
		v->type = T_NUMBER;
		v->u.number = d;
		return v;
	}

	Value* ParseArrayValue()
	{
		Consume('[');
		Value* v = new Value;
		v->type = T_ARRAY;
		v->u.array = NULL;
		Array** tail = &v->u.array;
		SkipWs();
		if (Consume(']'))
			return v;
		while (p < end) {
			Value* item = ParseValue();
			if (item == NULL)
				return v;
			Array* node = new Array;
			node->value = item;
			node->next = NULL;
			*tail = node;
			tail = &node->next;
			SkipWs();
			if (Consume(','))
				continue;
			Consume(']');
			break;
		}
		return v;
	}

	Value* ParseObjectValue()
	{
		Consume('{');
		Value* v = new Value;
		v->type = T_OBJECT;
		v->u.object = NULL;
		Object** tail = &v->u.object;
		SkipWs();
		if (Consume('}'))
			return v;
		while (p < end) {
			SkipWs();
			if (p >= end || *p != '"')
				break;
			Value* key = ParseStringValue();
			if (key == NULL || !Consume(':'))
				break;
			Value* val = ParseValue();
			if (val == NULL)
				val = new Value{T_NULL, {0}};
			Object* node = new Object;
			node->key = *key->u.string;
			node->value = val;
			node->next = NULL;
			*tail = node;
			tail = &node->next;
			delete key->u.string;
			delete key;
			SkipWs();
			if (Consume(','))
				continue;
			Consume('}');
			break;
		}
		return v;
	}

	Value* ParseValue()
	{
		SkipWs();
		if (p >= end)
			return NULL;
		switch (*p) {
		case '"': return ParseStringValue();
		case '[': return ParseArrayValue();
		case '{': return ParseObjectValue();
		case 't':
			if (p + 4 <= end && strncmp(p, "true", 4) == 0) {
				p += 4;
				Value* v = new Value;
				v->type = T_BOOL;
				v->u.boolean = true;
				return v;
			}
			return NULL;
		case 'f':
			if (p + 5 <= end && strncmp(p, "false", 5) == 0) {
				p += 5;
				Value* v = new Value;
				v->type = T_BOOL;
				v->u.boolean = false;
				return v;
			}
			return NULL;
		case 'n':
			if (p + 4 <= end && strncmp(p, "null", 4) == 0) {
				p += 4;
				Value* v = new Value;
				v->type = T_NULL;
				return v;
			}
			return NULL;
		default:
			if (*p == '-' || isdigit(*p))
				return ParseNumberValue();
			return NULL;
		}
	}
};

Value* Parse(const char* text)
{
	if (text == NULL)
		return NULL;
	Parser parser{text, text + strlen(text)};
	return parser.ParseValue();
}

const Value* Get(const Value* object, const char* key)
{
	if (object == NULL || object->type != T_OBJECT)
		return NULL;
	for (Object* node = object->u.object; node != NULL; node = node->next) {
		if (node->key == key)
			return node->value;
	}
	return NULL;
}

const Value* At(const Value* array, int32 index)
{
	if (array == NULL || array->type != T_ARRAY)
		return NULL;
	int32 i = 0;
	for (Array* node = array->u.array; node != NULL; node = node->next) {
		if (i == index)
			return node->value;
		i++;
	}
	return NULL;
}

int32 Length(const Value* array)
{
	if (array == NULL || array->type != T_ARRAY)
		return 0;
	int32 i = 0;
	for (Array* node = array->u.array; node != NULL; node = node->next)
		i++;
	return i;
}

const char* GetString(const Value* object, const char* key, const char* fallback)
{
	const Value* v = Get(object, key);
	if (v != NULL && v->type == T_STRING)
		return v->u.string->String();
	return fallback;
}

double GetNumber(const Value* object, const char* key, double fallback)
{
	const Value* v = Get(object, key);
	if (v != NULL && v->type == T_NUMBER)
		return v->u.number;
	return fallback;
}

bool GetBool(const Value* object, const char* key, bool fallback)
{
	const Value* v = Get(object, key);
	if (v != NULL && v->type == T_BOOL)
		return v->u.boolean;
	return fallback;
}

void FreeValue(Value* v)
{
	if (v == NULL)
		return;
	switch (v->type) {
	case T_STRING:
		delete v->u.string;
		break;
	case T_ARRAY: {
		Array* node = v->u.array;
		while (node != NULL) {
			Array* next = node->next;
			FreeValue(node->value);
			delete node;
			node = next;
		}
		break;
	}
	case T_OBJECT: {
		Object* node = v->u.object;
		while (node != NULL) {
			Object* next = node->next;
			FreeValue(node->value);
			delete node;
			node = next;
		}
		break;
	}
	default:
		break;
	}
	delete v;
}

} // namespace djson
