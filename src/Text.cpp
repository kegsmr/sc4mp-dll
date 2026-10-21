#include "Text.h"

#include <Windows.h>
#include <cwchar>

std::string Text::ToUtf8(const char* text)
{
	if (!text || !*text)
	{
		return std::string();
	}

	// Already UTF-8 (this includes plain ASCII)
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0) > 0)
	{
		return std::string(text);
	}

	int wideLength = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
	if (wideLength <= 0)
	{
		return std::string();
	}

	std::wstring wide(wideLength, L'\0');
	MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), wideLength);

	int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (length <= 0)
	{
		return std::string();
	}

	std::string utf8(length, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), length, nullptr, nullptr);
	utf8.resize(length - 1);	// drop the null terminator

	return utf8;
}

std::wstring Text::Utf8ToWide(const std::string& text)
{
	if (text.empty())
	{
		return std::wstring();
	}

	int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (length <= 0)
	{
		return std::wstring();
	}

	std::wstring wide(length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);

	return wide;
}

std::string Text::WideToUtf8(const std::wstring& text)
{
	if (text.empty())
	{
		return std::string();
	}

	int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0)
	{
		return std::string();
	}

	std::string utf8(length, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);

	return utf8;
}

// Estimates how wide text is in the chat lines' font. The pinned gzcom-dll has no way to ask the game's font (cIGZFont),
// so this uses widths of the chat lines' font (DataInsetLegend) measured in game at 1024x768, with the pixel between
// letters, rounded up a little so a message doesn't run into the name before it. Eg. "Keg (1):" is about 36 pixels wide
// in game, and 40 here. (The widths before were measured from a scaled down screenshot, so the colon was hidden.)
// The claimed city warning's GenBodyMedium is a little narrower, so this is safe for it too.
int32_t Text::EstimateWidth(const std::wstring& text)
{
	int32_t width = 0;

	for (wchar_t c : text)
	{
		if (wcschr(L"ijl.,:;'!| ", c))
		{
			width += 3;
		}
		else if (wcschr(L"ftr()[]-I", c))
		{
			width += 4;
		}
		else if (wcschr(L"mwMW@", c))
		{
			width += 11;
		}
		else if (c >= L'A' && c <= L'Z')
		{
			width += 7;
		}
		else if (c >= L'0' && c <= L'9')
		{
			width += 7;
		}
		else if (c < 0x2E80)
		{
			width += 6;
		}
		else
		{
			width += 13;	// Chinese, Japanese, Korean, etc.
		}
	}

	return width;
}

// Shortens text to fit `width` pixels, ending it with "..." if it was cut. A text window that's too narrow for its text
// shows only what fits before a space, so text has to be shortened to fit. `scale` / 8 is how much wider than the chat
// lines' font its font is (see EstimateWidth).
std::string Text::ShortenToWidth(const std::string& text, int32_t width, int32_t scale)
{
	std::wstring wide = Utf8ToWide(text);
	if (EstimateWidth(wide) * scale / 8 <= width)
	{
		return text;
	}

	while (!wide.empty() && EstimateWidth(wide + L"...") * scale / 8 > width)
	{
		wide.pop_back();
	}

	return WideToUtf8(wide) + "...";
}
