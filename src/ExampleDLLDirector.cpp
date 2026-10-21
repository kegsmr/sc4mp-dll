#include "version.h"
#include "DebugUtil.h"

#include "cIGZApp.h"
#include "cIGZCOM.h"
#include "cIGZFrameWork.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cRZBaseString.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"

#include <cstdio>
#include <Windows.h>

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessageCityEstablished = 0x26D31EC4;

// This must be unique for every plugin. Generate a 32-bit random number and use it.
// DO NOT REUSE DIRECTOR IDS EVER.
static constexpr uint32_t kExampleDirectorID = 0x51ACC690;

// Writes a line to SC4MP.log next to the DLL, so we can see the DLL loaded without attaching a debugger.
static void LogLine(const char* const line)
{
	HMODULE module = nullptr;
	GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&LogLine),
		&module);

	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(module, path, MAX_PATH);

	wchar_t* lastSlash = wcsrchr(path, L'\\');
	if (lastSlash)
	{
		wcscpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - path), L"SC4MP.log");
	}

	FILE* file = nullptr;
	if (_wfopen_s(&file, path, L"a") == 0 && file)
	{
		std::fprintf(file, "%s\n", line);
		std::fclose(file);
	}
}

// An example DLL director that logs a message to the debug console when a city is
// loaded or established.
//
// This class is derived from cRZMessage2COMDirector to allow/ it to interact with
// the game's messaging system.

class ExampleDllDirector final : public cRZMessage2COMDirector
{
public:
	ExampleDllDirector()
	{
	}

	uint32_t GetDirectorID() const
	{
		return kExampleDirectorID;
	}

	bool DoMessage(cIGZMessage2* pMsg)
	{
		// All of SC4's messages are cIGZMessage2Standard, so we always cast to it.

		cIGZMessage2Standard* pStandardMsg = static_cast<cIGZMessage2Standard*>(pMsg);

		switch (pStandardMsg->GetType())
		{
		case kSC4MessageCityEstablished:
			CityEstablished(pStandardMsg);
			break;
		case kSC4MessagePostCityInit:
			PostCityInit(pStandardMsg);
			break;
		}

		return true;
	}

	void CityEstablished(cIGZMessage2Standard* pStandardMsg)
	{
		// The first message parameter is a pointer to the city.

		cISC4City* pCity = static_cast<cISC4City*>(pStandardMsg->GetVoid1());

		if (pCity)
		{
			// Get the city name and print it to the debugger console.

			cRZBaseString cityName;
			pCity->GetCityName(cityName);

			DebugUtil::PrintLineToDebugOutputFormatted("Established city: %s", cityName.ToChar());

			char line[512]{};
			std::snprintf(line, sizeof(line), "Established city: %s", cityName.ToChar());
			LogLine(line);
		}
	}

	void PostCityInit(cIGZMessage2Standard* pStandardMsg)
	{
		// The first message parameter is a pointer to the city.

		cISC4City* pCity = static_cast<cISC4City*>(pStandardMsg->GetVoid1());

		if (pCity)
		{
			if (pCity->GetEstablished())
			{
				// Get the city name and print it to the debugger console.

				cRZBaseString cityName;
				pCity->GetCityName(cityName);

				DebugUtil::PrintLineToDebugOutputFormatted("Loaded city: %s", cityName.ToChar());

				char line[512]{};
				std::snprintf(line, sizeof(line), "Loaded city: %s", cityName.ToChar());
				LogLine(line);

				// Proof of concept: show that the DLL can talk to the player in game
				char message[512]{};
				std::snprintf(message, sizeof(message), "SC4MP DLL is working.\n\nLoaded city: %s", cityName.ToChar());
				MessageBoxA(nullptr, message, "SC4MP", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
			}
			else
			{
				DebugUtil::PrintLineToDebugOutput("In God Mode");
				LogLine("Loaded city in God Mode");
			}
		}
	}

	bool OnStart(cIGZCOM* pCOM)
	{
		LogLine("SC4MP DLL started");

		cIGZFrameWork* const pFramework = pCOM->FrameWork();

		if (pFramework->GetState() < cIGZFrameWork::kStatePreAppInit)
		{
			// Add a hook so that we receive the framework event callbacks.
			pFramework->AddHook(this);
		}
		else
		{
			PreAppInit();
		}

		return true;
	}

	bool PostAppInit()
	{
		cIGZMessageServer2Ptr pMsgServ;

		if (pMsgServ)
		{
			// Subscribe to the message notifications you require here.
			//
			// In this example plugin we are subscribing to events that
			// occur when a city is loaded or established.

			std::vector<uint32_t> requiredNotifications;
			requiredNotifications.push_back(kSC4MessageCityEstablished);
			requiredNotifications.push_back(kSC4MessagePostCityInit);

			for (uint32_t messageID : requiredNotifications)
			{
				pMsgServ->AddNotification(this, messageID);
			}
		}

		return true;
	}
};

// This method is required for the game to create an instance of our director.

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static ExampleDllDirector sDirector;
	return &sDirector;
}