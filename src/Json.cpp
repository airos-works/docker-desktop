// SPDX-License-Identifier: MIT

#include "Json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace djson {

struct Parser {
	const char* p;
	const char* end;
	int depth = 0;

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

	uint32 ParseHex4()
	{
		uint32 code = 0;
		for (int i = 0; i < 4 && p < end; i++, p++) {
			char c = *p;
			code <<= 4;
			if (c >= '0' && c <= '9')
				code |= c - '0';
			else if (c >= 'a' && c <= 'f')
				code |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				code |= c - 'A' + 10;
		}
		return code;
	}

	static void AppendUtf8(BString* s, uint32 code)
	{
		char buffer[4];
		int length;
		if (code < 0x80) {
			buffer[0] = (char)code;
			length = 1;
		} else if (code < 0x800) {
			buffer[0] = (char)(0xc0 | (code >> 6));
			buffer[1] = (char)(0x80 | (code & 0x3f));
			length = 2;
		} else if (code < 0x10000) {
			buffer[0] = (char)(0xe0 | (code >> 12));
			buffer[1] = (char)(0x80 | ((code >> 6) & 0x3f));
			buffer[2] = (char)(0x80 | (code & 0x3f));
			length = 3;
		} else {
			buffer[0] = (char)(0xf0 | (code >> 18));
			buffer[1] = (char)(0x80 | ((code >> 12) & 0x3f));
			buffer[2] = (char)(0x80 | ((code >> 6) & 0x3f));
			buffer[3] = (char)(0x80 | (code & 0x3f));
			length = 4;
		}
		s->Append(buffer, length);
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
				case 'b': (*s) += '\b'; break;
				case 'f': (*s) += '\f'; break;
				case 'u': {
					p++;
					uint32 code = ParseHex4();
					// A surrogate pair encodes one code point beyond the BMP.
					if (code >= 0xd800 && code < 0xdc00 && p + 1 < end
						&& p[0] == '\\' && p[1] == 'u') {
						p += 2;
						uint32 low = ParseHex4();
						if (low >= 0xdc00 && low < 0xe000)
							code = 0x10000 + ((code - 0xd800) << 10)
								+ (low - 0xdc00);
					}
					// An unpaired surrogate is no character.
					if (code >= 0xd800 && code < 0xe000)
						code = 0xfffd;
					AppendUtf8(s, code);
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
		if (p >= end || depth > 200)
			return NULL;
		switch (*p) {
		case '"': return ParseStringValue();
		case '[':
		case '{':
		{
			depth++;
			Value* v = *p == '[' ? ParseArrayValue() : ParseObjectValue();
			depth--;
			return v;
		}
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

const char* StringAt(const Value* array, int32 index, const char* fallback)
{
	const Value* v = At(array, index);
	if (v != NULL && v->type == T_STRING)
		return v->u.string->String();
	return fallback;
}

BString Join(const Value* array, const char* separator)
{
	BString result;
	for (int32 i = 0; i < Length(array); i++) {
		const Value* v = At(array, i);
		if (v == NULL || v->type != T_STRING)
			continue;
		if (result.Length() > 0)
			result << separator;
		result << *v->u.string;
	}
	return result;
}

int32 CountKeys(const Value* object)
{
	if (object == NULL || object->type != T_OBJECT)
		return 0;
	int32 count = 0;
	for (Object* node = object->u.object; node != NULL; node = node->next)
		count++;
	return count;
}

const Object* Members(const Value* object)
{
	if (object == NULL || object->type != T_OBJECT)
		return NULL;
	return object->u.object;
}

BString Quote(const char* text)
{
	BString result("\"");
	for (const unsigned char* p = (const unsigned char*)text; *p != '\0';
			p++) {
		switch (*p) {
		case '"': result << "\\\""; break;
		case '\\': result << "\\\\"; break;
		case '\n': result << "\\n"; break;
		case '\r': result << "\\r"; break;
		case '\t': result << "\\t"; break;
		case '\b': result << "\\b"; break;
		case '\f': result << "\\f"; break;
		default:
			if (*p < 0x20) {
				char buffer[8];
				snprintf(buffer, sizeof(buffer), "\\u%04x", *p);
				result << buffer;
			} else
				result.Append((const char*)p, 1);
			break;
		}
	}
	result << "\"";
	return result;
}

static void Indent(BString& out, int depth)
{
	out << "\n";
	for (int i = 0; i < depth; i++)
		out << "    ";
}

static void StringifyInto(BString& out, const Value* v, bool pretty, int depth)
{
	if (v == NULL) {
		out << "null";
		return;
	}
	switch (v->type) {
	case T_NULL:
		out << "null";
		break;
	case T_BOOL:
		out << (v->u.boolean ? "true" : "false");
		break;
	case T_NUMBER:
	{
		char buffer[64];
		double d = v->u.number;
		if (d < 1e18 && d > -1e18 && d == (double)(int64)d)
			snprintf(buffer, sizeof(buffer), "%lld", (long long)d);
		else
			snprintf(buffer, sizeof(buffer), "%.17g", d);
		out << buffer;
		break;
	}
	case T_STRING:
		out << Quote(v->u.string->String());
		break;
	case T_ARRAY:
	{
		if (v->u.array == NULL) {
			out << "[]";
			break;
		}
		out << "[";
		for (Array* node = v->u.array; node != NULL; node = node->next) {
			if (pretty)
				Indent(out, depth + 1);
			StringifyInto(out, node->value, pretty, depth + 1);
			if (node->next != NULL)
				out << ",";
		}
		if (pretty)
			Indent(out, depth);
		out << "]";
		break;
	}
	case T_OBJECT:
	{
		if (v->u.object == NULL) {
			out << "{}";
			break;
		}
		out << "{";
		for (Object* node = v->u.object; node != NULL; node = node->next) {
			if (pretty)
				Indent(out, depth + 1);
			out << Quote(node->key.String()) << (pretty ? ": " : ":");
			StringifyInto(out, node->value, pretty, depth + 1);
			if (node->next != NULL)
				out << ",";
		}
		if (pretty)
			Indent(out, depth);
		out << "}";
		break;
	}
	}
}

BString Stringify(const Value* value, bool pretty)
{
	BString out;
	StringifyInto(out, value, pretty, 0);
	return out;
}

BString Pretty(const char* json)
{
	Value* root = Parse(json);
	if (root == NULL)
		return json;
	BString out = Stringify(root, true);
	FreeValue(root);
	return out;
}

namespace {

// A strict recogniser of JSON (RFC 8259), for text people edit.
struct Validator {
	const char* p;
	const char* start;
	BString error;

	void SkipWs()
	{
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
			p++;
	}

	bool Fail(const char* what)
	{
		if (error.Length() == 0) {
			int line = 1, column = 1;
			for (const char* c = start; c < p; c++) {
				if (*c == '\n') {
					line++;
					column = 1;
				} else
					column++;
			}
			error.SetToFormat("%s at line %d, column %d", what, line, column);
		}
		return false;
	}

	bool String()
	{
		p++;
		while (*p != '"') {
			if (*p == '\0' || (unsigned char)*p < 0x20)
				return Fail("Unterminated string");
			if (*p == '\\') {
				p++;
				if (*p == 'u') {
					for (int i = 1; i <= 4; i++) {
						if (!isxdigit((unsigned char)p[i]))
							return Fail("Bad \\u escape");
					}
					p += 4;
				} else if (strchr("\"\\/bfnrt", *p) == NULL || *p == '\0')
					return Fail("Bad escape");
			}
			p++;
		}
		p++;
		return true;
	}

	bool Number()
	{
		if (*p == '-')
			p++;
		if (*p == '0')
			p++;
		else if (*p >= '1' && *p <= '9') {
			while (isdigit((unsigned char)*p))
				p++;
		} else
			return Fail("Bad number");
		if (*p == '.') {
			p++;
			if (!isdigit((unsigned char)*p))
				return Fail("Bad number");
			while (isdigit((unsigned char)*p))
				p++;
		}
		if (*p == 'e' || *p == 'E') {
			p++;
			if (*p == '+' || *p == '-')
				p++;
			if (!isdigit((unsigned char)*p))
				return Fail("Bad number");
			while (isdigit((unsigned char)*p))
				p++;
		}
		return true;
	}

	bool Literal(const char* word)
	{
		size_t length = strlen(word);
		if (strncmp(p, word, length) != 0)
			return Fail("Unexpected text");
		p += length;
		return true;
	}

	bool Value(int depth)
	{
		if (depth > 200)
			return Fail("Nested too deeply");
		SkipWs();
		switch (*p) {
		case '"':
			return String();
		case 't':
			return Literal("true");
		case 'f':
			return Literal("false");
		case 'n':
			return Literal("null");
		case '[':
			p++;
			SkipWs();
			if (*p == ']') {
				p++;
				return true;
			}
			while (true) {
				if (!Value(depth + 1))
					return false;
				SkipWs();
				if (*p == ',') {
					p++;
					continue;
				}
				if (*p == ']') {
					p++;
					return true;
				}
				return Fail("Expected , or ]");
			}
		case '{':
			p++;
			SkipWs();
			if (*p == '}') {
				p++;
				return true;
			}
			while (true) {
				SkipWs();
				if (*p != '"')
					return Fail("Expected a quoted name");
				if (!String())
					return false;
				SkipWs();
				if (*p != ':')
					return Fail("Expected :");
				p++;
				if (!Value(depth + 1))
					return false;
				SkipWs();
				if (*p == ',') {
					p++;
					continue;
				}
				if (*p == '}') {
					p++;
					return true;
				}
				return Fail("Expected , or }");
			}
		default:
			if (*p == '-' || isdigit((unsigned char)*p))
				return Number();
			return Fail(*p == '\0' ? "Unexpected end" : "Unexpected text");
		}
	}
};

} // namespace


bool Validate(const char* text, BString* outError)
{
	Validator validator{text, text, ""};
	bool valid = validator.Value(0);
	if (valid) {
		validator.SkipWs();
		if (*validator.p != '\0')
			valid = validator.Fail("Unexpected text after the end");
	}
	if (!valid && outError != NULL)
		*outError = validator.error;
	return valid;
}

} // namespace djson
