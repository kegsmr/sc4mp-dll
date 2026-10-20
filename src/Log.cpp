#include "Log.h"

#include <cstdio>
#include <ctime>
#include <mutex>
#include <Windows.h>

static std::mutex sLogMutex;

static void GetLogPath(wchar_t (&path)[MAX_PATH])
{
	HMODULE module = nullptr;
	GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&GetLogPath),
		&module);

	GetModuleFileNameW(module, path, MAX_PATH);

	wchar_t* lastSlash = wcsrchr(path, L'\\');
	if (lastSlash)
	{
		wcscpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - path), L"SC4MP.log");
	}
}

void Log::Line(const std::string& line)
{
	// The bridge thread logs too, so lines must not interleave
	std::lock_guard<std::mutex> lock(sLogMutex);

	OutputDebugStringA(line.c_str());
	OutputDebugStringA("\n");

	wchar_t path[MAX_PATH]{};
	GetLogPath(path);

	std::time_t now = std::time(nullptr);
	std::tm local{};
	localtime_s(&local, &now);

	char timeText[16]{};
	std::strftime(timeText, sizeof(timeText), "%H:%M:%S", &local);

	FILE* file = nullptr;
	if (_wfopen_s(&file, path, L"a") == 0 && file)
	{
		std::fprintf(file, "[%s] %s\n", timeText, line.c_str());
		std::fclose(file);
	}
}
