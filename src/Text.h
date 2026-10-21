#pragma once
#include <string>

namespace Text
{
	// Returns the game's text as UTF-8. Text that isn't valid UTF-8 is assumed to be in the Windows code page.
	std::string ToUtf8(const char* text);

	std::wstring Utf8ToWide(const std::string& text);
}
