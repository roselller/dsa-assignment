#pragma once

#include <cstdio>
#include <string>

#include "Vector.h"

/* Builds a JSON document as text. Calls must nest properly (every begin has an end,
   and a key is followed by exactly one value); the writer inserts the commas.

   Strings are escaped for JSON and also for HTML (<, > and & become <, > and
   &), so a response can never be mistaken for markup even if a browser sniffs it. */
class JsonWriter
{
	std::string out_;
	Vector<bool> hasItems_;  /* one entry per open object/array: has it got a member yet? */
	bool afterKey_ = false;

	void beforeValue()
	{
		if (afterKey_)
		{
			afterKey_ = false;
			return;
		}
		commaIfNeeded();
	}

	void commaIfNeeded()
	{
		if (hasItems_.count() == 0)
			return;
		bool& any = hasItems_[hasItems_.count() - 1];
		if (any)
			out_ += ',';
		any = true;
	}

	static void quote(std::string& out, const std::string& s)
	{
		out += '"';
		for (unsigned char c : s)
		{
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '<': out += "\\u003c"; break;
			case '>': out += "\\u003e"; break;
			case '&': out += "\\u0026"; break;
			default:
				if (c < 0x20)
				{
					char buf[8];
					std::snprintf(buf, sizeof buf, "\\u%04x", c);
					out += buf;
				}
				else
				{
					out += static_cast<char>(c);
				}
			}
		}
		out += '"';
	}

public:
	JsonWriter& beginObject()
	{
		beforeValue();
		out_ += '{';
		hasItems_.push(false);
		return *this;
	}
	JsonWriter& endObject()
	{
		hasItems_.removeAt(hasItems_.count() - 1);
		out_ += '}';
		return *this;
	}
	JsonWriter& beginArray()
	{
		beforeValue();
		out_ += '[';
		hasItems_.push(false);
		return *this;
	}
	JsonWriter& endArray()
	{
		hasItems_.removeAt(hasItems_.count() - 1);
		out_ += ']';
		return *this;
	}

	/* Object member name; the next call supplies its value */
	JsonWriter& key(const std::string& name)
	{
		commaIfNeeded();
		quote(out_, name);
		out_ += ':';
		afterKey_ = true;
		return *this;
	}

	JsonWriter& value(const std::string& s)
	{
		beforeValue();
		quote(out_, s);
		return *this;
	}
	JsonWriter& value(const char* s) { return value(std::string(s)); }
	JsonWriter& value(long long n)
	{
		beforeValue();
		out_ += std::to_string(n);
		return *this;
	}
	JsonWriter& value(int n) { return value(static_cast<long long>(n)); }
	JsonWriter& value(bool b)
	{
		beforeValue();
		out_ += b ? "true" : "false";
		return *this;
	}
	JsonWriter& null()
	{
		beforeValue();
		out_ += "null";
		return *this;
	}

	/* key + value in one call */
	template <class T>
	JsonWriter& field(const std::string& name, const T& v)
	{
		key(name);
		return value(v);
	}

	const std::string& str() const { return out_; }
};
