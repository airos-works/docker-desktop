// SPDX-License-Identifier: MIT
// Minimal JSON parser, sufficient for the Docker Engine API responses.

#ifndef DOCKER_JSON_H
#define DOCKER_JSON_H

#include <List.h>
#include <String.h>
#include <SupportDefs.h>


namespace djson {

struct Value;
struct Object;
struct Array;

struct Object {
	BString key;
	Value* value;
	Object* next;
};

struct Array {
	Value* value;
	Array* next;
};

enum ValueType {
	T_NULL,
	T_BOOL,
	T_NUMBER,
	T_STRING,
	T_ARRAY,
	T_OBJECT
};

struct Value {
	ValueType type;
	union {
		bool boolean;
		double number;
		BString* string;
		Array* array;
		Object* object;
	} u;
};

// Parses the whole input; returns NULL on error. Free with FreeValue.
Value* Parse(const char* text);

// Convenience accessors: safe on any value (wrong type -> defaults).
const char* GetString(const Value* object, const char* key,
	const char* fallback = "");
double GetNumber(const Value* object, const char* key, double fallback = 0);
bool GetBool(const Value* object, const char* key, bool fallback = false);
const Value* Get(const Value* object, const char* key);
const Value* At(const Value* array, int32 index);
int32 Length(const Value* array);

void FreeValue(Value* value);

} // namespace djson

#endif
