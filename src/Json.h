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

// The string at index of an array, and all of an array's strings joined.
const char* StringAt(const Value* array, int32 index,
	const char* fallback = "");
BString Join(const Value* array, const char* separator);

// An object's members, in document order (NULL when empty or no object).
const Object* Members(const Value* object);
int32 CountKeys(const Value* object);

// Serialising: a JSON string literal, and a whole value.
BString Quote(const char* text);
BString Stringify(const Value* value, bool pretty = false);
// Re-indents a JSON document for reading (returns it as is if unparsable).
BString Pretty(const char* json);

// Whether text is valid JSON; outError says where it is not.
bool Validate(const char* text, BString* outError = NULL);

void FreeValue(Value* value);

} // namespace djson

#endif
