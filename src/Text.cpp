#include "Text.h"

#include <Windows.h>

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
