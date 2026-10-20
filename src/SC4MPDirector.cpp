#include "version.h"
#include "Bridge.h"
#include "ChatPanel.h"
#include "Json.h"
#include "Log.h"
#include "Text.h"

#include "cIGZCheatCodeManager.h"
#include "cIGZCOM.h"
#include "cIGZFrameWork.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZString.h"
#include "cIGZWin.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4Region.h"
#include "cRZBaseString.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "SC4String.h"

#include <string>

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kSC4MessageCityEstablished = 0x26D31EC4;
static constexpr uint32_t kSC4MessagePreSave = 0x26C63343;
static constexpr uint32_t kSC4MessagePostSave = 0x26C63345;
static constexpr uint32_t kSC4MessagePostRegionInit = 0xCBB5BB45;
static constexpr uint32_t kSC4MessagePreRegionShutdown = 0x8BB5BB46;
static constexpr uint32_t kGZMessageCheatIssued = 0x230E27AC;

// The game's windows that the chat panel goes in
static constexpr uint32_t kGZWin_WinSC4App = 0x6104489A;
static constexpr uint32_t kGZWin_SC4View3DWin = 0x9A47B417;
static constexpr uint32_t kGZWin_SC4WinRegionScreen = 0xEA659793;

// Random IDs for our cheat codes, which must not be used by any other plugin
static constexpr uint32_t kCheatSay = 0x7B3E9A52;
static constexpr uint32_t kCheatSC4MP = 0x7B3E9A53;

// This must be unique for every plugin. Generate a 32-bit random number and use it.
// DO NOT REUSE DIRECTOR IDS EVER.
static constexpr uint32_t kSC4MPDirectorID = 0x51ACC690;

// Reports what happens in game to the SC4MP launcher (see Bridge), shows the chat in game (see ChatPanel),
// and adds cheat codes for talking to the launcher: `say <message>` sends a chat message, and
// `sc4mp <command>` runs a launcher command.
class SC4MPDirector final : public cRZMessage2COMDirector
{
public:
	// cRZMessage2COMDirector's own QueryInterface calls itself for other interfaces, so pass those to the base director instead
	bool QueryInterface(uint32_t riid, void** ppvObj)
	{
		if (riid == GZCLSID::kcIGZMessageTarget2)
		{
			*ppvObj = static_cast<cIGZMessageTarget2*>(this);
			AddRef();
			return true;
		}

		return cRZCOMDllDirector::QueryInterface(riid, ppvObj);
	}

	uint32_t GetDirectorID() const
	{
		return kSC4MPDirectorID;
	}

	bool OnStart(cIGZCOM* pCOM)
	{
		Log::Line("SC4MP DLL " PLUGIN_VERSION_STR " started");

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
			uint32_t messageIDs[] = {
				kSC4MessagePostCityInit,
				kSC4MessagePreCityShutdown,
				kSC4MessageCityEstablished,
				kSC4MessagePreSave,
				kSC4MessagePostSave,
				kSC4MessagePostRegionInit,
				kSC4MessagePreRegionShutdown,
			};

			for (uint32_t messageID : messageIDs)
			{
				pMsgServ->AddNotification(this, messageID);
			}
		}

		// The bridge thread only queues the lines; the panel shows them on the game thread
		bridge.SetNotifyHandler([this](const std::string& text, bool history) { chat.Post(text, history); });

		// In single player, only log what happens
		if (!bridge.Start())
		{
			Log::Line("Not started by the SC4MP launcher (SC4MP_BRIDGE_PORT isn't set), so the bridge is off");
			return true;
		}

		RegisterCheats();

		chat.Start(
			[this](const std::string& text) { SendChat(text); },
			[this](const std::string& command) { SendCommand(command); });

		return true;
	}

	// The game's windows still exist here, but not after the app shuts down
	bool PreAppShutdown()
	{
		chat.Stop();
		return true;
	}

	bool PostAppShutdown()
	{
		bridge.Stop();
		return true;
	}

	bool DoMessage(cIGZMessage2* pMsg)
	{
		// All of SC4's messages are cIGZMessage2Standard, so we always cast to it.
		cIGZMessage2Standard* pStandardMsg = static_cast<cIGZMessage2Standard*>(pMsg);

		switch (pStandardMsg->GetType())
		{
		case kSC4MessagePostCityInit:
			PostCityInit(static_cast<cISC4City*>(pStandardMsg->GetVoid1()));
			break;
		case kSC4MessageCityEstablished:
			SendCityEvent("city_established", "Established city");
			break;
		case kSC4MessagePreSave:
			SendCityEvent("pre_save", "Saving city");
			break;
		case kSC4MessagePostSave:
			SendCityEvent("saved", "Saved city");
			break;
		case kSC4MessagePreCityShutdown:
			chat.Detach();
			SendCityEvent("city_exited", "Exiting city");
			break;
		case kSC4MessagePostRegionInit:
			RegionLoaded();
			chat.Attach(GetSC4AppChild(kGZWin_SC4WinRegionScreen));
			break;
		case kSC4MessagePreRegionShutdown:
			chat.Detach();
			break;
		case kGZMessageCheatIssued:
			CheatIssued(pStandardMsg->GetData1(), static_cast<cIGZString*>(pStandardMsg->GetVoid2()));
			break;
		}

		return true;
	}

private:
	void PostCityInit(cISC4City* pCity)
	{
		if (!pCity)
		{
			return;
		}

		bool established = pCity->GetEstablished();
		std::string name = GetCityName(pCity);

		if (established)
		{
			Log::Line("Loaded city: " + name);
		}
		else
		{
			Log::Line("Loaded city in God Mode: " + name);
		}

		JsonWriter message;
		message.Add("type", "city_loaded");
		message.Add("name", name);
		message.Add("established", established);
		bridge.Send(message);

		chat.Attach(GetSC4AppChild(kGZWin_SC4View3DWin));
	}

	// Sends an event about the open city. The current city is read from the app, since not every message carries it.
	void SendCityEvent(const char* type, const char* logText)
	{
		cISC4AppPtr pApp;
		std::string name;
		if (pApp)
		{
			name = GetCityName(pApp->GetCity());
		}

		Log::Line(std::string(logText) + ": " + name);

		JsonWriter message;
		message.Add("type", type);
		message.Add("name", name);
		bridge.Send(message);
	}

	void RegionLoaded()
	{
		std::string name;

		cISC4AppPtr pApp;
		if (pApp)
		{
			cISC4Region* pRegion = pApp->GetRegion();
			if (pRegion && pRegion->GetName())
			{
				name = Text::ToUtf8(pRegion->GetName()->AsIGZString()->ToChar());
			}
		}

		Log::Line("Loaded region: " + name);

		JsonWriter message;
		message.Add("type", "region_loaded");
		message.Add("name", name);
		bridge.Send(message);
	}

	void RegisterCheats()
	{
		cISC4AppPtr pApp;
		if (!pApp)
		{
			return;
		}

		cIGZCheatCodeManager* pCheatMgr = pApp->GetCheatCodeManager();
		if (!pCheatMgr)
		{
			Log::Line("Unable to get the cheat code manager, so the cheat codes are off");
			return;
		}

		// The message ID has no effect, so pass zero like the rest of the game does
		pCheatMgr->AddNotification2(this, 0);
		pCheatMgr->RegisterCheatCode(kCheatSay, cRZBaseString("say"));
		pCheatMgr->RegisterCheatCode(kCheatSC4MP, cRZBaseString("sc4mp"));

		Log::Line("Registered the cheat codes \"say\" and \"sc4mp\"");
	}

	void CheatIssued(uint32_t cheatID, cIGZString* pCheatText)
	{
		if (cheatID != kCheatSay && cheatID != kCheatSC4MP)
		{
			return;	// another plugin's or the game's cheat
		}

		// The message has the whole line that was typed, eg. "say hello everyone"
		std::string line = pCheatText ? Text::ToUtf8(pCheatText->ToChar()) : std::string();
		std::string arguments = GetArguments(line);

		// Logged, to find out why these don't arrive in the region view
		Log::Line("Cheat code: \"" + line + "\"" + (pCheatText ? "" : " (no text)"));

		if (cheatID == kCheatSay)
		{
			if (arguments.empty())
			{
				chat.StartTyping();	// `say` alone opens the chat panel to type in
				return;
			}

			SendChat(arguments);
		}
		else
		{
			SendCommand(arguments.empty() ? "help" : arguments);
		}
	}

	void SendChat(const std::string& text)
	{
		Log::Line("Chat: " + text);

		JsonWriter message;
		message.Add("type", "chat");
		message.Add("message", text);
		bridge.Send(message);
	}

	void SendCommand(const std::string& command)
	{
		Log::Line("Command: " + command);

		JsonWriter message;
		message.Add("type", "command");
		message.Add("command", command);
		bridge.Send(message);
	}

	// Returns one of the game's top windows, eg. the region screen, or nullptr if it doesn't exist right now
	static cIGZWin* GetSC4AppChild(uint32_t id)
	{
		cISC4AppPtr pApp;
		if (!pApp)
		{
			return nullptr;
		}

		cIGZWin* pMainWin = pApp->GetMainWindow();
		cIGZWin* pSC4AppWin = pMainWin ? pMainWin->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
		cIGZWin* pChild = pSC4AppWin ? pSC4AppWin->GetChildWindowFromID(id) : nullptr;

		if (!pChild)
		{
			Log::Line("Unable to find the game's window for the chat panel");
		}

		return pChild;
	}

	// Returns what was typed after the cheat code, without surrounding spaces
	static std::string GetArguments(const std::string& line)
	{
		size_t start = line.find(' ');
		if (start == std::string::npos)
		{
			return std::string();
		}

		start = line.find_first_not_of(' ', start);
		if (start == std::string::npos)
		{
			return std::string();
		}

		size_t end = line.find_last_not_of(' ');
		return line.substr(start, end - start + 1);
	}

	static std::string GetCityName(cISC4City* pCity)
	{
		if (!pCity)
		{
			return std::string();
		}

		cRZBaseString name;
		pCity->GetCityName(name);
		return Text::ToUtf8(name.ToChar());
	}

	// Declared first, so it outlives the bridge, which may still be posting to it
	ChatPanel chat;
	Bridge bridge;
};

// This method is required for the game to create an instance of our director.

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static SC4MPDirector sDirector;
	return &sDirector;
}
