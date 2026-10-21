#pragma once
#include <cstdint>
#include <string>

namespace Text
{
	// Returns the game's text as UTF-8. Text that isn't valid UTF-8 is assumed to be in the Windows code page.
	std::string ToUtf8(const char* text);

	std::wstring Utf8ToWide(const std::string& text);

	std::string WideToUtf8(const std::wstring& text);

	// Estimates how wide text is, in pixels, in the chat lines' font (DataInsetLegend), which GenBodyMedium is about as wide as
	int32_t EstimateWidth(const std::wstring& text);

	// Shortens text to fit `width` pixels, ending it with "..." if it was cut. `scale` / 8 is how much wider its font is than EstimateWidth's.
	std::string ShortenToWidth(const std::string& text, int32_t width, int32_t scale);
}
