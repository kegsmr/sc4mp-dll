#pragma once
#include <cstdint>
#include <map>
#include <string>

// The bridge only needs flat JSON objects, eg. {"type":"chat","message":"hi"}, so this is a tiny writer and reader for those instead of a library.

class JsonWriter
{
public:
	JsonWriter& Add(const char* key, const std::string& value);
	JsonWriter& Add(const char* key, const char* value);	// without this, string literals would pick the bool overload
	JsonWriter& Add(const char* key, bool value);
	JsonWriter& Add(const char* key, int64_t value);

	// One line, without the trailing newline
	std::string ToString() const;

private:
	void AddKey(const char* key);

	std::string body;
};

namespace Json
{
	// Reads a flat object into `fields`. String values are unescaped; other values are kept as written (eg. `true` or `12`). Returns false if the line isn't a flat object.
	bool ParseObject(const std::string& line, std::map<std::string, std::string>& fields);
}
