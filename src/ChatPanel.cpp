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

// A random ID, which must not be used by any other plugin
static constexpr uint32_t kChatPanelServiceID = 0x7B3E9A5F;

// The UI script in ui/ChatPanel.ui (packed into sc4mp-dll-ui.dat by tools/make-ui-dat.py) and its window IDs
static constexpr uint32_t kPanelGroup = 0xC0E6A1C5;
static constexpr uint32_t kPanelInstance = 0x1A9F3B21;
static constexpr uint32_t kPanelID = 0x7B3E9A60;
static constexpr uint32_t kTitleID = 0x7B3E9A61;
static constexpr uint32_t kHideButtonID = 0x7B3E9A62;
static constexpr uint32_t kFirstLineID = 0x7B3E9A70;
static constexpr uint32_t kInputID = 0x7B3E9A80;
static constexpr uint32_t kChatButtonID = 0x7B3E9A81;

static constexpr size_t kLineCount = 8;
static constexpr size_t kLineLength = 60;			// characters that fit on a line, roughly, since the font isn't monospaced
static constexpr size_t kShownInputLength = 50;		// the end of a longer message is shown while typing it
static constexpr size_t kMaxInputLength = 200;
static constexpr size_t kMaxInbox = 100;
static constexpr int32_t kCollapsedHeight = 34;		// just the title bar

static constexpr uint32_t kGZWinMessageCommand = 3;
static constexpr uint32_t kGZWinCommandButtonClicked = 0x287259F6;
static constexpr LPARAM kKeyRepeatFlag = 1 << 30;

static const char* const kTitle = "SC4MP Chat";
static const char* const kInputHint = "Press Enter to chat";

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

	expandedHeight = pWin->GetH();

	SetCollapsed(collapsed);
	ShowLines();
	ShowInput();
	ShowTitle();

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

	if (collapsed)
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
	{
		std::lock_guard<std::mutex> lock(inboxMutex);
		if (inbox.empty())
		{
			return true;
		}
		received.swap(inbox);
	}

	for (const auto& [text, history] : received)
	{
		AddLine(text, history);
	}

	ShowLines();
	ShowTitle();

	return true;
}

bool ChatPanel::DoWinProcMessage(cIGZWin* pSender, cGZMessage& msg)
{
	if (msg.dwMessageType != kGZWinMessageCommand || msg.dwData1 != kGZWinCommandButtonClicked)
	{
		return false;
	}

	switch (msg.dwData2)	// the button's ID
	{
	case kHideButtonID:
		if (typing)
		{
			StopTyping();
		}
		SetCollapsed(!collapsed);
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

bool ChatPanel::DoWinMsg(cIGZWin* pSender, uint32_t dwMessageID, uint32_t data1, uint32_t data2, uint32_t data3)
{
	return false;
}

LRESULT ChatPanel::FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled)
{
	handled = false;

	if (!pWin)
	{
		return 0;	// no panel in this view
	}

	if (typing)
	{
		handled = HandleTypingKey(hWnd, uMsg, wParam, lParam);
	}
	else if (uMsg == WM_KEYDOWN && wParam == VK_RETURN && (lParam & kKeyRepeatFlag) == 0 && !GameWantsEnter())
	{
		StartTyping();
		handled = true;
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
				// A message is one line
				Type(*text == L'\r' || *text == L'\n' || *text == L'\t' ? L' ' : *text);
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
		if (sendCommand)
		{
			sendCommand(command.empty() ? "help" : command);
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

	if (!history && collapsed)
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

	if (!pWin)
	{
		return;
	}

	for (uint32_t i = 0; i < kLineCount; i++)
	{
		SetChildVisible(kFirstLineID + i, !collapsed);
	}
	SetChildVisible(kInputID, !collapsed);
	SetChildVisible(kChatButtonID, !collapsed);

	pWin->SetH(collapsed ? kCollapsedHeight : expandedHeight);

	SetCaption(kHideButtonID, collapsed ? "Show" : "Hide");
	ShowTitle();
}

// Shows the newest lines at the bottom
void ChatPanel::ShowLines()
{
	size_t blank = kLineCount - lines.size();

	for (size_t i = 0; i < kLineCount; i++)
	{
		SetText(kFirstLineID + static_cast<uint32_t>(i), i < blank ? std::string() : lines[i - blank]);
	}
}

void ChatPanel::ShowInput()
{
	if (!typing)
	{
		SetText(kInputID, kInputHint);
		SetCaption(kChatButtonID, "Chat");
		return;
	}

	std::wstring shown = input;
	if (shown.size() > kShownInputLength)
	{
		shown = L"..." + shown.substr(shown.size() - kShownInputLength);
	}

	SetText(kInputID, "> " + Text::WideToUtf8(shown) + "_");
	SetCaption(kChatButtonID, "Send");
}

void ChatPanel::ShowTitle()
{
	std::string title = kTitle;
	if (collapsed && unread > 0)
	{
		title += " (" + std::to_string(unread) + " new)";
	}

	SetText(kTitleID, title);
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

// For buttons, which have no cIGZWinText
void ChatPanel::SetCaption(uint32_t id, const std::string& text)
{
	if (!pWin)
	{
		return;
	}

	cIGZWin* pChild = pWin->GetChildWindowFromID(id);
	if (pChild)
	{
		pChild->SetCaption(cRZBaseString(text));
		pChild->InvalidateSelf();
	}
}

void ChatPanel::SetChildVisible(uint32_t id, bool visible)
{
	cIGZWin* pChild = pWin ? pWin->GetChildWindowFromID(id) : nullptr;
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
