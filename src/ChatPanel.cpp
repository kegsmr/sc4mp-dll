#include "ChatPanel.h"
#include "GZWinInterfaces.h"
#include "Log.h"
#include "Text.h"

#include "cGZMessage.h"
#include "cGZPersistResourceKey.h"
#include "cIGZCanvas.h"
#include "cIGZCanvasW32.h"
#include "cIGZFrameWork.h"
#include "cIGZWin.h"
#include "cIGZWinMgr.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cRZCOMDllDirector.h"
#include "GZServPtrs.h"
#include "SC4UI.h"

#include <vector>
#include <windowsx.h>

// A random ID, which must not be used by any other plugin
static constexpr uint32_t kChatPanelServiceID = 0x7B3E9A5F;

// The UI script in ui/ChatPanel.ui (packed into sc4mp-dll-ui.dat by tools/make-ui-dat.py) and its window IDs
static constexpr uint32_t kPanelGroup = 0xC0E6A1C5;
static constexpr uint32_t kPanelInstance = 0x1A9F3B21;
static constexpr uint32_t kPanelID = 0x7B3E9A60;
static constexpr uint32_t kTitleID = 0x7B3E9A61;
static constexpr uint32_t kHideButtonID = 0x7B3E9A62;
static constexpr uint32_t kShowButtonID = 0x7B3E9A63;		// in the same place as Hide, shown instead of it while collapsed
static constexpr uint32_t kBadgeID = 0x7B3E9A64;
static constexpr uint32_t kBadgeTextID = 0x7B3E9A65;
static constexpr uint32_t kBackgroundID = 0x7B3E9A66;
static constexpr uint32_t kCollapsedBackgroundID = 0x7B3E9A67;
static constexpr uint32_t kFieldID = 0x7B3E9A68;
static constexpr uint32_t kTypingFieldID = 0x7B3E9A69;
static constexpr uint32_t kFirstLineID = 0x7B3E9A70;
static constexpr uint32_t kInputID = 0x7B3E9A80;
static constexpr uint32_t kChatButtonID = 0x7B3E9A81;
static constexpr uint32_t kMinimizeButtonID = 0x7B3E9A6A;
static constexpr uint32_t kStatusPillID = 0x7B3E9A90;
static constexpr uint32_t kStatusTextID = 0x7B3E9A91;

// The minimized panel, a button in the corner with the status icon and unread count on it
static constexpr uint32_t kChipID = 0x7B3E9AA0;
static constexpr uint32_t kChipTextID = 0x7B3E9AA1;
static constexpr uint32_t kChipIconOffset = 0x10;	// each status icon on the chip has the ID of the title bar's one plus this
static constexpr int32_t kChipWidth = 64;
static constexpr int32_t kChipHeight = 26;
static constexpr int32_t kChipRightMargin = 12;
static constexpr int32_t kChipBottomMargin = 48;	// above the city view's news ticker

// The panel's buttons, of which only the shown ones can be clicked
static constexpr uint32_t kButtonIDs[] = { kMinimizeButtonID, kHideButtonID, kShowButtonID, kChatButtonID, kChipID };

// Windows in the title bar, shown unless the panel is minimized
static constexpr uint32_t kTitleBarIDs[] = { kTitleID, kStatusPillID, kStatusTextID, kMinimizeButtonID };

// Where the panel's open, collapsed or minimized state is kept between games
static constexpr wchar_t kSettingsKey[] = L"Software\\SC4MP\\DLL";
static constexpr wchar_t kPanelModeValue[] = L"ChatPanelMode";

// The save status icons, one window each, of which only the current one is shown
static constexpr std::pair<const char*, uint32_t> kStatusIcons[] = {
	{ "saved", 0x7B3E9A92 },
	{ "saving", 0x7B3E9A93 },
	{ "failed", 0x7B3E9A94 },
	{ "not_saved", 0x7B3E9A95 },
	{ "connected", 0x7B3E9A96 },
	{ "disconnected", 0x7B3E9A97 },
	{ "guest", 0x7B3E9A98 },
	{ "refreshing", 0x7B3E9A93 },
	{ "refreshed", 0x7B3E9A92 },
};

static constexpr size_t kLineCount = 8;
static constexpr size_t kLineLength = 48;			// characters that fit on a line, roughly, since the font isn't monospaced
static constexpr size_t kShownInputLength = 40;		// the end of a longer message is shown while typing it
static constexpr size_t kMaxInputLength = 200;
static constexpr size_t kMaxInbox = 100;
static constexpr size_t kTitleLength = 19;			// before the title runs into the unread badge
static constexpr size_t kStatusLength = 20;			// before the status runs out of its pill
static constexpr int32_t kCollapsedHeight = 42;		// just the title bar (and its shadow), as in chat-panel-collapsed.png

static constexpr uint32_t kGZWinMessageCommand = 3;
static constexpr uint32_t kGZWinCommandButtonClicked = 0x287259F6;
static constexpr LPARAM kKeyRepeatFlag = 1 << 30;
static constexpr int kMaxWinDepth = 32;	// in case a window's parent chain loops

static const char* const kTitle = "SC4MP Chat";
static const char* const kInputHint = "Press Enter to chat";

// Shortens text to `length` characters, ending it with "..." if it was cut
static std::string Shorten(const std::string& text, size_t length)
{
	std::wstring wide = Text::Utf8ToWide(text);
	if (wide.size() <= length)
	{
		return text;
	}

	return Text::WideToUtf8(wide.substr(0, length - 3)) + "...";
}

// Splits a line into pieces that fit the panel, breaking at spaces where possible
static std::vector<std::wstring> Wrap(const std::wstring& text)
{
	std::vector<std::wstring> pieces;
	size_t start = 0;

	while (text.size() - start > kLineLength)
	{
		size_t end = text.rfind(L' ', start + kLineLength);
		if (end == std::wstring::npos || end <= start)
		{
			end = start + kLineLength;	// a word that's too long is cut
		}

		pieces.push_back(text.substr(start, end - start));

		start = end;
		while (start < text.size() && text[start] == L' ')
		{
			start++;
		}
	}

	pieces.push_back(text.substr(start));
	return pieces;
}

static std::string Trim(const std::string& text)
{
	size_t start = text.find_first_not_of(" \t");
	if (start == std::string::npos)
	{
		return std::string();
	}

	size_t end = text.find_last_not_of(" \t");
	return text.substr(start, end - start + 1);
}

ChatPanel::ChatPanel()
	: cRZBaseSystemService(kChatPanelServiceID, 0)
{
}

void ChatPanel::Start(SendFunction chat, SendFunction command)
{
	if (started)
	{
		return;
	}

	sendChat = std::move(chat);
	sendCommand = std::move(command);
	LoadMode();

	cIGZFrameWork* pFramework = RZGetFrameWork();
	if (!pFramework)
	{
		Log::Line("Unable to get the framework, so the chat panel is off");
		return;
	}

	// The framework only ticks running services
	SetServiceRunning(true);
	pFramework->AddSystemService(this);
	pFramework->AddToTick(this);

	started = true;
}

void ChatPanel::Stop()
{
	if (!started)
	{
		return;
	}

	Detach();
	SetKeyFilter(false);

	cIGZFrameWork* pFramework = RZGetFrameWork();
	if (pFramework)
	{
		pFramework->RemoveFromTick(this);
		pFramework->RemoveSystemService(this);
	}

	SetServiceRunning(false);
	started = false;
}

void ChatPanel::Attach(cIGZWin* pNewParent)
{
	Detach();

	if (!started || !pNewParent)
	{
		return;
	}

	cGZPersistResourceKey key(0x00000000, kPanelGroup, kPanelInstance);
	cIGZWin* pNewWin = nullptr;

	if (!SC4UI::CreateWindowFromScript(key, pNewParent, kPanelID, GZIID_cIGZWin, reinterpret_cast<void**>(&pNewWin)) || !pNewWin)
	{
		if (pNewWin)
		{
			pNewWin->Release();
		}

		Log::Line("Unable to create the chat panel. Is sc4mp-dll-ui.dat in the Plugins folder?");
		return;
	}

	// Kept, so the panel can be removed from it even after the game starts tearing the view down
	pParent = pNewParent;
	pParent->AddRef();
	loggedCoveredClick = false;

	pWin = pNewWin;
	pParent->ChildAdd(pWin);

	// Button clicks go to the panel's window proc, which is this
	cRZAutoRefCount<cIGZWinGen> pWinGen;
	if (pWin->QueryInterface(kGZIID_cIGZWinGen, pWinGen.AsPPVoid()))
	{
		pWinGen->SetWinProc(this);
	}
	else
	{
		Log::Line("The chat panel isn't a GZWinGen, so its buttons won't work");
	}

	// Where the UI script puts the open panel
	openLeft = pWin->GetL();
	openTop = pWin->GetT();
	openWidth = pWin->GetW();
	openHeight = pWin->GetH();

	Layout();
	ShowLines();

	pWin->PullToFront();
	pWin->ShowWindow();

	if (!SetKeyFilter(true))
	{
		Log::Line("Unable to listen for keys, so Enter won't open the chat (the Chat button still works)");
	}

	Log::Line("Showing the chat panel");
}

void ChatPanel::Detach()
{
	typing = false;
	input.clear();
	buttonsDown = 0;

	if (pWin)
	{
		cRZAutoRefCount<cIGZWinGen> pWinGen;
		if (pWin->QueryInterface(kGZIID_cIGZWinGen, pWinGen.AsPPVoid()))
		{
			pWinGen->SetWinProc(nullptr);
		}

		if (pParent)
		{
			pParent->ChildDelete(pWin);
		}

		pWin->Release();
		pWin = nullptr;
	}

	if (pParent)
	{
		pParent->Release();
		pParent = nullptr;
	}
}

void ChatPanel::StartTyping()
{
	if (!pWin)
	{
		return;
	}

	if (minimized)
	{
		SetMinimized(false);
	}
	else if (collapsed)
	{
		SetCollapsed(false);
	}

	typing = true;
	ShowInput();
}

void ChatPanel::Post(const std::string& text, bool history)
{
	std::lock_guard<std::mutex> lock(inboxMutex);

	inbox.emplace_back(text, history);
	while (inbox.size() > kMaxInbox)
	{
		inbox.pop_front();
	}
}

void ChatPanel::PostStatus(const std::string& state, const std::string& text)
{
	std::lock_guard<std::mutex> lock(inboxMutex);

	// Only the latest status matters
	pendingStatus = std::make_pair(state, text);
}

void ChatPanel::PostServerName(const std::string& name)
{
	std::lock_guard<std::mutex> lock(inboxMutex);
	pendingServerName = name;
}

bool ChatPanel::QueryInterface(uint32_t riid, void** ppvObj)
{
	if (riid == GZIID_cIGZWinProc)
	{
		*ppvObj = static_cast<cIGZWinProc*>(this);
		AddRef();
		return true;
	}

	return cRZBaseSystemService::QueryInterface(riid, ppvObj);
}

uint32_t ChatPanel::AddRef()
{
	return cRZBaseSystemService::AddRef();
}

uint32_t ChatPanel::Release()
{
	return cRZBaseSystemService::Release();
}

bool ChatPanel::Init()
{
	return true;
}

bool ChatPanel::Shutdown()
{
	return true;
}

bool ChatPanel::OnTick(uint32_t unknown1)
{
	std::deque<std::pair<std::string, bool>> received;
	std::optional<std::pair<std::string, std::string>> newStatus;
	std::optional<std::string> newServerName;
	{
		std::lock_guard<std::mutex> lock(inboxMutex);
		received.swap(inbox);
		newStatus.swap(pendingStatus);
		newServerName.swap(pendingServerName);
	}

	if (newServerName)
	{
		serverName = *newServerName;
		ShowTitle();
	}

	if (newStatus)
	{
		status = *newStatus;
		ShowStatus();
	}

	// The corner moves when the game's resolution changes
	if (pWin && pParent && minimized && (pParent->GetW() != parentWidth || pParent->GetH() != parentHeight))
	{
		Layout();
	}

	if (!received.empty())
	{
		for (const auto& [text, history] : received)
		{
			AddLine(text, history);
		}

		ShowLines();
		ShowTitle();
	}

	return true;
}

bool ChatPanel::DoWinProcMessage(cIGZWin* pSender, cGZMessage& msg)
{
	if (msg.dwMessageType != kGZWinMessageCommand || msg.dwData1 != kGZWinCommandButtonClicked)
	{
		return false;
	}

	return Click(msg.dwData2);	// the button's ID
}

bool ChatPanel::DoWinMsg(cIGZWin* pSender, uint32_t dwMessageID, uint32_t data1, uint32_t data2, uint32_t data3)
{
	return false;
}

// Does what the button does. Called for the game's button clicks, and for the clicks FilterMessage keeps from the game.
bool ChatPanel::Click(uint32_t buttonID)
{
	switch (buttonID)
	{
	case kHideButtonID:
	case kShowButtonID:
		if (typing)
		{
			StopTyping();
		}
		SetCollapsed(!collapsed);
		return true;
	case kMinimizeButtonID:
		if (typing)
		{
			StopTyping();
		}
		SetMinimized(true);
		return true;
	case kChipID:
		SetMinimized(false);
		return true;
	case kChatButtonID:
		if (typing)
		{
			Send();
		}
		else
		{
			StartTyping();
		}
		return true;
	}

	return false;
}

LRESULT ChatPanel::FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled)
{
	handled = false;

	if (!pWin)
	{
		return 0;	// no panel in this view
	}

	if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)
	{
		handled = HandleMouse(hWnd, uMsg, lParam);
	}
	else if (typing)
	{
		handled = HandleTypingKey(hWnd, uMsg, wParam, lParam);
	}
	else if (uMsg == WM_KEYDOWN && wParam == VK_RETURN && (lParam & kKeyRepeatFlag) == 0)
	{
		if (GameWantsEnter())
		{
			Log::Line("Enter was left to the game, since one of its dialogs or text boxes has it");
		}
		else
		{
			StartTyping();
			handled = true;
		}
	}

	return 0;
}

// The views pick the region tile or city lot under the cursor from the game window's own mouse messages,
// whichever window the game's window manager then gives them to, so a click on the panel also clicked the map.
// So the mouse buttons on the panel are taken here, before the game sees them, and the panel does the clicked button itself.
bool ChatPanel::HandleMouse(HWND hWnd, UINT uMsg, LPARAM lParam)
{
	POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	uint32_t button = 0;
	bool down = false;

	switch (uMsg)
	{
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		button = MK_LBUTTON;
		down = true;
		break;
	case WM_RBUTTONDOWN:
	case WM_RBUTTONDBLCLK:
		button = MK_RBUTTON;
		down = true;
		break;
	case WM_MBUTTONDOWN:
	case WM_MBUTTONDBLCLK:
		button = MK_MBUTTON;
		down = true;
		break;
	case WM_LBUTTONUP:
		button = MK_LBUTTON;
		break;
	case WM_RBUTTONUP:
		button = MK_RBUTTON;
		break;
	case WM_MBUTTONUP:
		button = MK_MBUTTON;
		break;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		// So the wheel doesn't zoom the map under the panel. Its position is on the screen, unlike the buttons'.
		return ScreenToClient(hWnd, &point) && IsOnPanel(point.x, point.y);
	default:
		return false;	// moving is left to the game, which shows the buttons' tooltips and hover art
	}

	if (!down)
	{
		// The release goes where the press went, even if the panel has moved since (eg. the chip opened it),
		// so neither the game nor the panel gets half a click
		bool pressedOnPanel = (buttonsDown & button) != 0;
		buttonsDown &= ~button;
		return pressedOnPanel;
	}

	if (!IsOnPanel(point.x, point.y))
	{
		return false;
	}

	buttonsDown |= button;

	// Done on the press rather than the release, so there's no pressed button to keep track of
	if (button == MK_LBUTTON)
	{
		Click(GetButtonAt(point.x, point.y));
	}

	return true;
}

// Whether a point in the game window (where the mouse messages are) is on the panel, and the panel is on top there
bool ChatPanel::IsOnPanel(int32_t x, int32_t y)
{
	cIGZWinMgrPtr pWinMgr;
	if (pWinMgr && pWinMgr->IsModal())
	{
		return false;	// a dialog has the mouse
	}

	int32_t left = 0;
	int32_t top = 0;
	GetPanelOrigin(left, top);
	if (x < left || y < top || x >= left + pWin->GetW() || y >= top + pWin->GetH())
	{
		return false;
	}

	// Another of the game's windows may be over the panel, eg. a query window dragged there, and it keeps its clicks.
	// If the window manager can't say, the panel takes the click.
	cIGZWin* pHit = nullptr;
	if (pWinMgr)
	{
		pHit = pWinMgr->GetWindowFromPoint(x, y);
	}

	if (!pHit)
	{
		return true;
	}

	for (int depth = 0; pHit && depth < kMaxWinDepth; depth++)
	{
		if (pHit == pWin)
		{
			return true;
		}
		pHit = pHit->GetParentWin();
	}

	// Logged once, since it also happens if the window manager's coordinates aren't the game window's
	if (!loggedCoveredClick)
	{
		Log::Line("A click on the chat panel went to another of the game's windows over it");
		loggedCoveredClick = true;
	}

	return false;
}

// Where the panel is in the game window. Its own place is in its view's window, which is in its parent's, and so on.
void ChatPanel::GetPanelOrigin(int32_t& left, int32_t& top) const
{
	left = 0;
	top = 0;

	cIGZWin* pCurrent = pWin;
	for (int depth = 0; pCurrent && depth < kMaxWinDepth; depth++)
	{
		left += pCurrent->GetL();
		top += pCurrent->GetT();
		pCurrent = pCurrent->GetParentWin();
	}
}

// Returns the ID of the shown button at a point in the game window, or 0 if there's none
uint32_t ChatPanel::GetButtonAt(int32_t x, int32_t y) const
{
	int32_t left = 0;
	int32_t top = 0;
	GetPanelOrigin(left, top);
	x -= left;
	y -= top;

	for (uint32_t id : kButtonIDs)
	{
		cIGZWin* pButton = pWin->GetChildWindowFromID(id);
		if (pButton && pButton->IsVisible()
			&& x >= pButton->GetL() && y >= pButton->GetT() && x < pButton->GetL() + pButton->GetW() && y < pButton->GetT() + pButton->GetH())
		{
			return id;
		}
	}

	return 0;
}

// Takes the keys while the player types, so the game doesn't also act on them (eg. B for bulldoze)
bool ChatPanel::HandleTypingKey(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_RETURN)
		{
			Send();
		}
		else if (wParam == VK_ESCAPE)
		{
			StopTyping();
		}
		else if (wParam == 'V' && GetKeyState(VK_CONTROL) < 0)
		{
			Paste(hWnd);
		}
		else
		{
			// The game's message loop normally turns the key into a WM_CHAR, which comes next.
			// If it didn't, turn it into text here.
			MSG pending;
			if (!PeekMessageW(&pending, nullptr, WM_CHAR, WM_DEADCHAR, PM_NOREMOVE))
			{
				BYTE keys[256]{};
				wchar_t chars[4]{};
				if (GetKeyboardState(keys))
				{
					int count = ToUnicode(static_cast<UINT>(wParam), (lParam >> 16) & 0xFF, keys, chars, 4, 0);
					for (int i = 0; i < count; i++)
					{
						Type(chars[i]);
					}
				}
			}
		}
		return true;
	case WM_CHAR:
	{
		wchar_t c = static_cast<wchar_t>(wParam);
		if (!IsWindowUnicode(hWnd) && wParam >= 0x80)
		{
			// An ANSI window gets characters in the Windows code page
			char byte = static_cast<char>(wParam);
			MultiByteToWideChar(CP_ACP, 0, &byte, 1, &c, 1);
		}
		Type(c);
		return true;
	}
	case WM_KEYUP:
	case WM_DEADCHAR:
		return true;
	}

	return false;
}

void ChatPanel::Type(wchar_t c)
{
	if (c == L'\b')
	{
		if (!input.empty())
		{
			input.pop_back();
			if (!input.empty() && IS_HIGH_SURROGATE(input.back()))
			{
				input.pop_back();	// the rest of a character outside the BMP, eg. an emoji
			}
		}
	}
	else if (c < 0x20 || c == 0x7F)
	{
		return;	// Enter, Esc, Ctrl+letter, etc.
	}
	else if (input.size() < kMaxInputLength)
	{
		input.push_back(c);
	}

	ShowInput();
}

void ChatPanel::Paste(HWND hWnd)
{
	if (!OpenClipboard(hWnd))
	{
		return;
	}

	HANDLE data = GetClipboardData(CF_UNICODETEXT);
	if (data)
	{
		const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
		if (text)
		{
			for (; *text; text++)
			{
				wchar_t c = *text;

				// A message is one line
				if (c == L'\r' || c == L'\n' || c == L'\t')
				{
					c = L' ';
				}

				Type(c);
			}
			GlobalUnlock(data);
		}
	}

	CloseClipboard();
}

void ChatPanel::Send()
{
	std::string text = Trim(Text::WideToUtf8(input));
	StopTyping();

	if (text.empty())
	{
		return;
	}

	// "/status" runs a launcher command, like the `sc4mp status` cheat code
	if (text[0] == '/')
	{
		std::string command = Trim(text.substr(1));
		if (command.empty())
		{
			command = "help";
		}

		if (sendCommand)
		{
			sendCommand(command);
		}
	}
	else if (sendChat)
	{
		sendChat(text);
	}
}

void ChatPanel::StopTyping()
{
	typing = false;
	input.clear();
	ShowInput();
}

// Whether Enter is meant for the game instead, eg. to finish typing a cheat code or close a message box
bool ChatPanel::GameWantsEnter() const
{
	cIGZWinMgrPtr pWinMgr;
	if (!pWinMgr)
	{
		return false;
	}

	if (pWinMgr->IsModal())
	{
		return true;
	}

	cIGZWin* pFocus = pWinMgr->GZGetFocus();
	if (!pFocus)
	{
		return false;
	}

	cIGZUnknown* pTextEdit = nullptr;
	if (pFocus->QueryInterface(kGZIID_cIGZWinTextEdit, reinterpret_cast<void**>(&pTextEdit)) && pTextEdit)
	{
		pTextEdit->Release();
		return true;
	}

	return false;
}

void ChatPanel::AddLine(const std::string& text, bool history)
{
	// Alerts can have several lines
	size_t start = 0;
	while (start <= text.size())
	{
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
		{
			end = text.size();
		}

		std::string line = text.substr(start, end - start);
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		line = Trim(line);

		if (!line.empty())
		{
			for (const std::wstring& piece : Wrap(Text::Utf8ToWide(line)))
			{
				lines.push_back(Text::WideToUtf8(piece));
			}
		}

		start = end + 1;
	}

	while (lines.size() > kLineCount)
	{
		lines.pop_front();
	}

	if (!history && (collapsed || minimized))
	{
		unread++;
	}
}

void ChatPanel::SetCollapsed(bool value)
{
	collapsed = value;
	if (!collapsed)
	{
		unread = 0;
	}

	SaveMode();
	Layout();
}

void ChatPanel::SetMinimized(bool value)
{
	minimized = value;
	if (!minimized)
	{
		collapsed = false;	// the chip opens the whole panel
		unread = 0;
	}

	SaveMode();
	Layout();
}

// Shows the windows for the panel's state, and sizes and places it
void ChatPanel::Layout()
{
	if (!pWin)
	{
		return;
	}

	bool open = !collapsed && !minimized;

	// Everything below the title bar is hidden, and the background is swapped for one that's only the title bar,
	// so nothing is left under it even if the game doesn't shrink the panel
	for (uint32_t i = 0; i < kLineCount; i++)
	{
		SetChildVisible(kFirstLineID + i, open);
	}
	SetChildVisible(kBackgroundID, open);
	SetChildVisible(kInputID, open);
	SetChildVisible(kChatButtonID, open);
	SetChildVisible(kHideButtonID, open);
	SetChildVisible(kCollapsedBackgroundID, collapsed && !minimized);
	SetChildVisible(kShowButtonID, collapsed && !minimized);
	for (uint32_t id : kTitleBarIDs)
	{
		SetChildVisible(id, !minimized);
	}
	SetChildVisible(kChipID, minimized);
	SetChildVisible(kChipTextID, minimized);

	// Shrunk too, so the empty part doesn't take clicks meant for the view under it
	int32_t width = openWidth;
	int32_t height = openHeight;
	if (minimized)
	{
		width = kChipWidth;
		height = kChipHeight;
	}
	else if (collapsed)
	{
		height = kCollapsedHeight;
	}

	pWin->SetW(width);
	pWin->SetH(height);

	if (minimized && pParent)
	{
		parentWidth = pParent->GetW();
		parentHeight = pParent->GetH();
		MovePanel(parentWidth - kChipWidth - kChipRightMargin, parentHeight - kChipHeight - kChipBottomMargin);
	}
	else
	{
		MovePanel(openLeft, openTop);
	}

	ShowInput();
	ShowTitle();
	ShowStatus();

	// The view under the panel has to be redrawn where the panel was
	pWin->InvalidateSelf();
	if (pParent)
	{
		pParent->InvalidateSelf();
	}
}

// The pinned gzcom-dll declares GZWinMoveTo one vtable slot late (see GZWinInterfaces.h), so calling it runs the
// game's next method, which moves the window by an offset. So it's given the distance to the new place.
void ChatPanel::MovePanel(int32_t left, int32_t top)
{
	int32_t dx = left - pWin->GetL();
	int32_t dy = top - pWin->GetT();
	if (dx == 0 && dy == 0)
	{
		return;
	}

	pWin->GZWinMoveTo(dx, dy);

	if (pWin->GetL() != left || pWin->GetT() != top)
	{
		Log::Line("The chat panel didn't move to (" + std::to_string(left) + ", " + std::to_string(top) + "), it's at (" + std::to_string(pWin->GetL()) + ", " + std::to_string(pWin->GetT()) + ")");
	}
}

void ChatPanel::LoadMode()
{
	DWORD mode = 0;
	DWORD size = sizeof(mode);
	if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kPanelModeValue, RRF_RT_REG_DWORD, nullptr, &mode, &size) == ERROR_SUCCESS)
	{
		collapsed = mode == 1;
		minimized = mode == 2;
	}
}

void ChatPanel::SaveMode() const
{
	DWORD mode = 0;
	if (minimized)
	{
		mode = 2;
	}
	else if (collapsed)
	{
		mode = 1;
	}

	RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kPanelModeValue, REG_DWORD, &mode, sizeof(mode));
}

// Shows the newest lines at the bottom
void ChatPanel::ShowLines()
{
	size_t blank = kLineCount - lines.size();

	for (size_t i = 0; i < kLineCount; i++)
	{
		uint32_t id = kFirstLineID + static_cast<uint32_t>(i);

		if (i < blank)
		{
			SetText(id, std::string());
		}
		else
		{
			SetText(id, lines[i - blank]);
		}
	}
}

void ChatPanel::ShowInput()
{
	bool open = !collapsed && !minimized;
	SetChildVisible(kFieldID, open && !typing);
	SetChildVisible(kTypingFieldID, open && typing);

	if (!typing)
	{
		SetText(kInputID, kInputHint);
		return;
	}

	std::wstring shown = input;
	if (shown.size() > kShownInputLength)
	{
		shown = L"..." + shown.substr(shown.size() - kShownInputLength);
	}

	SetText(kInputID, "> " + Text::WideToUtf8(shown) + "_");
}

void ChatPanel::ShowTitle()
{
	if (serverName.empty())
	{
		SetText(kTitleID, kTitle);
	}
	else
	{
		SetText(kTitleID, Shorten(serverName, kTitleLength));
	}

	std::string count = std::to_string(unread);
	if (unread > 99)
	{
		count = "99+";
	}

	bool badge = collapsed && !minimized && unread > 0;
	SetChildVisible(kBadgeID, badge);
	SetChildVisible(kBadgeTextID, badge);
	SetText(kBadgeTextID, count);

	if (unread > 0)
	{
		SetText(kChipTextID, count + " new");
	}
	else
	{
		SetText(kChipTextID, "Chat");
	}
}

// Shows the launcher's save status, which stays in the title bar while the panel is collapsed
void ChatPanel::ShowStatus()
{
	const auto& [state, text] = status;

	// The icon is shown on the title bar, or on the chip when minimized. An unknown state from a newer launcher has none.
	uint32_t shown = 0;
	for (const auto& [iconState, id] : kStatusIcons)
	{
		if (state == iconState)
		{
			if (minimized)
			{
				shown = id + kChipIconOffset;
			}
			else
			{
				shown = id;
			}
			break;
		}
	}
	for (const auto& [iconState, id] : kStatusIcons)
	{
		SetChildVisible(id, id == shown);
		SetChildVisible(id + kChipIconOffset, id + kChipIconOffset == shown);
	}

	SetText(kStatusTextID, Shorten(text, kStatusLength));
}

void ChatPanel::SetText(uint32_t id, const std::string& text)
{
	if (!pWin)
	{
		return;
	}

	cRZAutoRefCount<cIGZWinText> pText;
	if (pWin->GetChildAs(id, kGZIID_cIGZWinText, pText.AsPPVoid()))
	{
		pText->SetCaption(cRZBaseString(text));
	}
}

void ChatPanel::SetChildVisible(uint32_t id, bool visible)
{
	if (!pWin)
	{
		return;
	}

	cIGZWin* pChild = pWin->GetChildWindowFromID(id);
	if (!pChild)
	{
		return;
	}

	if (visible)
	{
		pChild->ShowWindow();
	}
	else
	{
		pChild->HideWindow();
	}
}

bool ChatPanel::SetKeyFilter(bool add)
{
	if (keyFilterAdded == add)
	{
		return true;
	}

	cIGZCanvasPtr pCanvas;
	if (!pCanvas)
	{
		return false;
	}

	cRZAutoRefCount<cIGZCanvasW32> pCanvasW32;
	if (!pCanvas->QueryInterface(GZIID_cIGZCanvasW32, pCanvasW32.AsPPVoid()) || !pCanvasW32->AddWinProcFilter(this, add))
	{
		return false;
	}

	keyFilterAdded = add;
	return true;
}
