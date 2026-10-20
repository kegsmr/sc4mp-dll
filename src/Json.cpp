#include "Json.h"

#include <cstdio>

JsonWriter& JsonWriter::Add(const char* key, const std::string& value)
{
	AddKey(key);

	body += '"';
	for (unsigned char c : value)
	{
		switch (c)
		{
		case '"':
			body += "\\\"";
			break;
		case '\\':
			body += "\\\\";
			break;
		case '\n':
			body += "\\n";
			break;
		case '\r':
			body += "\\r";
			break;
		case '\t':
			body += "\\t";
			break;
		default:
			if (c < 0x20)
			{
				char escaped[8]{};
				std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
				body += escaped;
			}
			else
			{
				body += static_cast<char>(c);	// UTF-8 bytes are written as they are
			}
			break;
		}
	}
	body += '"';

	return *this;
}

JsonWriter& JsonWriter::Add(const char* key, const char* value)
{
	if (!value)
	{
		return Add(key, std::string());
	}

	return Add(key, std::string(value));
}

JsonWriter& JsonWriter::Add(const char* key, bool value)
{
	AddKey(key);
	if (value)
	{
		body += "true";
	}
	else
	{
		body += "false";
	}
	return *this;
}

JsonWriter& JsonWriter::Add(const char* key, int64_t value)
{
	AddKey(key);
	body += std::to_string(value);
	return *this;
}

std::string JsonWriter::ToString() const
{
	return "{" + body + "}";
}

void JsonWriter::AddKey(const char* key)
{
	if (!body.empty())
	{
		body += ',';
	}

	// Keys are always our own plain ASCII names, so they don't need escaping
	body += '"';
	body += key;
	body += "\":";
}

namespace
{
	void SkipSpace(const std::string& text, size_t& i)
	{
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n'))
		{
			i++;
		}
	}

	void AppendUtf8(std::string& out, uint32_t codePoint)
	{
		if (codePoint < 0x80)
		{
			out += static_cast<char>(codePoint);
		}
		else if (codePoint < 0x800)
		{
			out += static_cast<char>(0xC0 | (codePoint >> 6));
			out += static_cast<char>(0x80 | (codePoint & 0x3F));
		}
		else if (codePoint < 0x10000)
		{
			out += static_cast<char>(0xE0 | (codePoint >> 12));
			out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (codePoint & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (codePoint >> 18));
			out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (codePoint & 0x3F));
		}
	}

	bool ReadHex4(const std::string& text, size_t& i, uint32_t& value)
	{
		if (i + 4 > text.size())
		{
			return false;
		}

		value = 0;
		for (size_t end = i + 4; i < end; i++)
		{
			char c = text[i];
			value <<= 4;
			if (c >= '0' && c <= '9')
			{
				value |= c - '0';
			}
			else if (c >= 'a' && c <= 'f')
			{
				value |= c - 'a' + 10;
			}
			else if (c >= 'A' && c <= 'F')
			{
				value |= c - 'A' + 10;
			}
			else
			{
				return false;
			}
		}

		return true;
	}

	// Reads a string starting at the opening quote, and leaves `i` after the closing quote
	bool ReadString(const std::string& text, size_t& i, std::string& out)
	{
		if (i >= text.size() || text[i] != '"')
		{
			return false;
		}
		i++;

		while (i < text.size())
		{
			char c = text[i++];

			if (c == '"')
			{
				return true;
			}

			if (c != '\\')
			{
				out += c;
				continue;
			}

			if (i >= text.size())
			{
				return false;
			}

			char escaped = text[i++];
			switch (escaped)
			{
			case '"':
			case '\\':
			case '/':
				out += escaped;
				break;
			case 'b':
				out += '\b';
				break;
			case 'f':
				out += '\f';
				break;
			case 'n':
				out += '\n';
				break;
			case 'r':
				out += '\r';
				break;
			case 't':
				out += '\t';
				break;
			case 'u':
			{
				uint32_t codePoint = 0;
				if (!ReadHex4(text, i, codePoint))
				{
					return false;
				}

				// Characters outside the BMP (eg. emoji) come as two escapes (a surrogate pair), since Python escapes everything that isn't ASCII by default
				if (codePoint >= 0xD800 && codePoint <= 0xDBFF && text.compare(i, 2, "\\u") == 0)
				{
					size_t next = i + 2;
					uint32_t low = 0;
					if (ReadHex4(text, next, low) && low >= 0xDC00 && low <= 0xDFFF)
					{
						codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
						i = next;
					}
				}

				// A lone surrogate can't be written as UTF-8
				if (codePoint >= 0xD800 && codePoint <= 0xDFFF)
				{
					codePoint = 0xFFFD;
				}

				AppendUtf8(out, codePoint);
				break;
			}
			default:
				return false;
			}
		}

		return false;
	}

	// Reads any other value (number, true, false, null, or a nested object or array) as it's written
	bool ReadOther(const std::string& text, size_t& i, std::string& out)
	{
		size_t start = i;
		int depth = 0;

		while (i < text.size())
		{
			char c = text[i];

			if (c == '"')
			{
				std::string ignored;
				if (!ReadString(text, i, ignored))
				{
					return false;
				}
				continue;
			}

			if (c == '{' || c == '[')
			{
				depth++;
			}
			else if (c == '}' || c == ']')
			{
				if (depth == 0)
				{
					break;
				}
				depth--;
			}
			else if (c == ',' && depth == 0)
			{
				break;
			}

			i++;
		}

		out = text.substr(start, i - start);
		while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r' || out.back() == '\n'))
		{
			out.pop_back();
		}

		return depth == 0 && !out.empty();
	}
}

bool Json::ParseObject(const std::string& line, std::map<std::string, std::string>& fields)
{
	size_t i = 0;

	SkipSpace(line, i);
	if (i >= line.size() || line[i] != '{')
	{
		return false;
	}
	i++;

	SkipSpace(line, i);
	if (i < line.size() && line[i] == '}')
	{
		return true;
	}

	while (i < line.size())
	{
		std::string key;
		SkipSpace(line, i);
		if (!ReadString(line, i, key))
		{
			return false;
		}

		SkipSpace(line, i);
		if (i >= line.size() || line[i] != ':')
		{
			return false;
		}
		i++;

		SkipSpace(line, i);
		std::string value;
		if (i < line.size() && line[i] == '"')
		{
			if (!ReadString(line, i, value))
			{
				return false;
			}
		}
		else if (!ReadOther(line, i, value))
		{
			return false;
		}
		fields[key] = value;

		SkipSpace(line, i);
		if (i >= line.size())
		{
			return false;
		}
		if (line[i] == '}')
		{
			return true;
		}
		if (line[i] != ',')
		{
			return false;
		}
		i++;
	}

	return false;
}
