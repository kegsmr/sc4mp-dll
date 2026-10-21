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
#include "cIGZWinBtn.h"
#include "cIGZWinMgr.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cRZCOMDllDirector.h"
#include "GZServPtrs.h"
#include "SC4UI.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <vector>
#include <windowsx.h>

// A random ID, which must not be used by any other plugin
static constexpr uint32_t kChatPanelServiceID = 0x7B3E9A5F;

// The UI script in ui/ChatPanel.ui (packed into sc4mp-dll-ui.dat by tools/make-ui-dat.py) and its window IDs
static constexpr uint32_t kPanelGroup = 0xC0E6A1C5;
static constexpr uint32_t kPanelInstance = 0x1A9F3B21;
static constexpr uint32_t kPanelID = 0x7B3E9A60;
static constexpr uint32_t kTitleID = 0x7B3E9A61;
static constexpr uint32_t kBackgroundID = 0x7B3E9A66;
static constexpr uint32_t kFieldID = 0x7B3E9A68;
static constexpr uint32_t kTypingFieldID = 0x7B3E9A69;
static constexpr uint32_t kFirstLineID = 0x7B3E9A70;
static constexpr uint32_t kFirstInfoLineID = 0x7B3E9AB0;	// the same lines in grey, for lines that aren't chat
static constexpr uint32_t kFirstNameID = 0x7B3E9B00;		// a window for each line and name color, at kFirstNameID + line * kNameLineStep + color
static constexpr uint32_t kNameLineStep = 0x10;
static constexpr uint32_t kInputID = 0x7B3E9A80;
static constexpr uint32_t kChatButtonID = 0x7B3E9A81;
static constexpr uint32_t kMinimizeButtonID = 0x7B3E9A6A;
static constexpr uint32_t kStatusTipID = 0x7B3E9A90;	// a see-through button over the status icon, for its tooltip

// The minimized panel, a button in the corner with the status icon and unread count on it
static constexpr uint32_t kScrollbarID = 0x7B3E9A6B;
static constexpr uint32_t kNewHintID = 0x7B3E9A6C;		// "N new below", while the player has scrolled up
static constexpr uint32_t kNewHintTextID = 0x7B3E9A6D;
static constexpr uint32_t kChipID = 0x7B3E9AA0;	// the round button, under the status ring and the speech bubble
static constexpr uint32_t kChipCountID = 0x7B3E9AA1;	// the unread count, on the red bubble
static constexpr uint32_t kChipCountBubbleID = 0x7B3E9AA9;
static constexpr uint32_t kChipBubbleID = 0x7B3E9AAA;	// the game's speech bubble, a button too, so it's clickable and lights up
static constexpr uint32_t kChipIconOffset = 0x10;	// each status ring on the chip has the ID of the title bar's icon plus this
static constexpr int32_t kChipWidth = 56;	// the 50 pixel button, with room for the count bubble over its top right
static constexpr int32_t kChipHeight = 56;
static constexpr int32_t kTitleBandHeight = 34;	// the frame's title band, by which the panel is dragged
static constexpr int32_t kRightMargin = 12;			// the chip and the open panel are in the view's top right corner
static constexpr int32_t kTopMargin = 16;			// in the city view, whose corner is free
static constexpr int32_t kChipBottomMargin = 48;	// above the city view's news ticker, and the region view's bottom bar
static constexpr ULONGLONG kChipCheckInterval = 250;	// how often the chip checks whether a window of the game covers it
static constexpr uint32_t kSavedDialogID = 0xAA8DEF97;	// the game's "City Saved." message box
static constexpr uint32_t kSavedDialogTextID = 0x4A8CC4A9;	// and its text
static constexpr ULONGLONG kSavedDialogWait = 6000;	// how long after a save the game's "City Saved." is looked for
static constexpr int32_t kSavedDialogScanTop = 120;	// the middle of the game is searched from this far from its top to as far from its bottom
static constexpr int32_t kSavedDialogScanStep = 20;
static constexpr ULONGLONG kSavedDialogInterval = 100;	// how often it's looked for on the game window's messages
static constexpr int32_t kSavedDialogStep = 6;		// the spacing of the points it's searched at
static constexpr ULONGLONG kChipMoveDelay = 500;
static constexpr uint32_t kPuckID = 0x0987B48F;	// the city view's control panel (ui C973B411), which hides with the game's UI
static constexpr ULONGLONG kUICheckInterval = 250;	// how long that has to stay so before the chip moves, so it doesn't flicker

// The launcher's colors for player names (CHAT_NAME_COLORS in sc4mpclient.py), in the same order, as the name windows in the UI script have them.
// A few are a little darker than the launcher's, to read on the panel's background. The launcher sends a name's color, and the nearest of these is used.
static constexpr uint8_t kNameColors[][3] = {
	{ 44, 120, 48 },	// residential
	{ 21, 101, 192 },	// commercial
	{ 153, 91, 0 },		// industrial
	{ 106, 61, 154 },	// civic
	{ 0, 119, 105 },	// utilities
	{ 178, 58, 30 },	// fire
	{ 86, 113, 29 },	// parks
	{ 13, 71, 161 },	// water
	{ 141, 79, 43 },	// transportation
	{ 173, 20, 87 },	// health
	{ 55, 71, 79 },		// police
	{ 123, 94, 0 },		// farms
};
static constexpr int kNameColorCount = static_cast<int>(std::size(kNameColors));

// The panel's windows that are clicked like buttons but aren't the game's, so their presses come to the panel. Only the
// shown ones can be clicked.
static constexpr uint32_t kButtonIDs[] = { kFieldID, kNewHintTextID };	// the field is clicked to type

// Windows in the title bar, shown unless the panel is minimized
static constexpr uint32_t kTitleBarIDs[] = { kTitleID, kStatusTipID, kMinimizeButtonID };

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
static constexpr size_t kMaxLines = 200;			// rows kept to scroll back through, wrapped rows included
static constexpr int kWheelRows = 3;				// rows scrolled per notch of the mouse wheel
static constexpr size_t kLineLength = 44;			// characters that fit on a line, roughly, since the font isn't monospaced
static constexpr int32_t kInputWidth = 276;			// the input's text window, less its gutters; the end of a longer message is shown while typing it
static constexpr size_t kMaxInputLength = 500;
static constexpr size_t kMaxInbox = 250;			// more than the launcher replays on connecting (GameBridge.RECENT_LINES), which comes all at once
static constexpr int32_t kTitleWidth = 290;			// the title's text window, less its gutters, before the status icon
static constexpr int32_t kTitleFontScale = 9;		// the title's font (DataInsetHeader) is about 9/8 as wide as the chat lines' (see Text::ShortenToWidth)
static constexpr size_t kNameLength = 20;			// a longer player name is shortened
static constexpr int32_t kNameGap = 4;				// between a player's name and their message

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

// Eg. 0x7B3E9A60 as "7B3E9A60", for the log
static std::string ToHex(uint32_t value)
{
	char text[9];
	snprintf(text, sizeof(text), "%08X", value);
	return text;
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

// Returns the name color (in kNameColors) nearest to the launcher's, which comes as "[red,green,blue]", or -1 if there's none
static int FindNameColor(const std::string& color)
{
	// Read by hand rather than with a string stream, which would make the DLL a lot bigger
	int rgb[3]{};
	int count = 0;
	bool inNumber = false;
	for (char c : color)
	{
		if (c >= '0' && c <= '9')
		{
			if (!inNumber)
			{
				if (count == 3)
				{
					break;
				}
				count++;
				inNumber = true;
			}
			rgb[count - 1] = rgb[count - 1] * 10 + (c - '0');
		}
		else
		{
			inNumber = false;
		}
	}

	if (count < 3)
	{
		return -1;
	}

	int nearest = -1;
	int nearestDistance = 0;
	for (int i = 0; i < kNameColorCount; i++)
	{
		int distance = 0;
		for (int channel = 0; channel < 3; channel++)
		{
			int difference = rgb[channel] - kNameColors[i][channel];
			distance += difference * difference;
		}

		if (nearest < 0 || distance < nearestDistance)
		{
			nearest = i;
			nearestDistance = distance;
		}
	}

	return nearest;
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

void ChatPanel::Attach(cIGZWin* pNewParent, bool region)
{
	Detach();
	regionView = region;
	uiHidden = false;
	chipSpot = ChipSpot::Home;
	wantedChipSpot = ChipSpot::Home;

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

	// Button clicks and the mouse on the panel go to its window proc, which is this
	cRZAutoRefCount<cIGZWinGen> pWinGen;
	if (pWin->QueryInterface(kGZIID_cIGZWinGen, pWinGen.AsPPVoid()))
	{
		pWinGen->SetWinProc(this);
	}
	else
	{
		Log::Line("The chat panel isn't a GZWinGen, so its buttons won't work");
	}

	// Where the UI script puts the open panel, unless the player has dragged it somewhere this game
	if (!moved)
	{
		openLeft = pWin->GetL();
		openTop = pWin->GetT();
	}
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
	StopDragging();

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

	typing = true;
	ResetCaret();
	ShowInput();
}

void ChatPanel::Post(const std::string& text, bool history, const std::string& name, const std::string& color, bool own)
{
	std::lock_guard<std::mutex> lock(inboxMutex);

	inbox.push_back(Notice{ text, history, name, color, own });
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
	std::deque<Notice> received;
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

	// The corner moves when the game's resolution changes, and the open panel is kept inside the view
	if (pWin && pParent && (pParent->GetW() != parentWidth || pParent->GetH() != parentHeight))
	{
		Layout();
	}

	AvoidCoveringWindows();
	FollowGameUI();
	FollowScrollbar();

	CheckSavedDialog(false);

	// The caret blinks like Windows' own
	if (typing && GetTickCount64() - caretTime >= GetCaretBlinkTime())
	{
		caretShown = !caretShown;
		caretTime = GetTickCount64();
		ShowInput();
	}

	if (!received.empty())
	{
		for (const Notice& notice : received)
		{
			AddLine(notice);
		}

		ShowLines();
		ShowTitle();
	}

	return true;
}

// The game's button clicks, and the mouse on the panel. What the panel handles doesn't reach the map under it, and the
// rest (eg. drawing) is left to the game.
bool ChatPanel::DoWinProcMessage(cIGZWin* pSender, cGZMessage& msg)
{
	int32_t x = static_cast<int32_t>(msg.dwData1);
	int32_t y = static_cast<int32_t>(msg.dwData2);

	switch (msg.dwMessageType)
	{
	case kGZWinMessageCommand:
		if (msg.dwData1 != kGZWinCommandButtonClicked)
		{
			return false;
		}
		return Click(msg.dwData2);	// the button's ID
	case kGZWinMessageMouseDownL:
		Press(x, y);
		return true;
	case kGZWinMessageMouseUpL:
		StopDragging();
		return true;
	case kGZWinMessageMouseDownR:
	case kGZWinMessageMouseDownM:
	case kGZWinMessageMouseUpR:
	case kGZWinMessageMouseUpM:
		return true;
	case kGZWinMessageMouseMove:
		if (!dragging)
		{
			return false;	// left to the game, which shows the buttons' tooltips and hover art
		}
		Drag(x, y);
		return true;
	case kGZWinMessageMouseWheel:
		Wheel(msg);
		return true;
	}

	return false;
}

bool ChatPanel::DoWinMsg(cIGZWin* pSender, uint32_t dwMessageID, uint32_t data1, uint32_t data2, uint32_t data3)
{
	return false;
}

// Does what the button does. Called for the game's button clicks, and for presses on the panel's other buttons.
bool ChatPanel::Click(uint32_t buttonID)
{
	switch (buttonID)
	{
	case kMinimizeButtonID:
		if (typing)
		{
			StopTyping();
		}
		SetMinimized(true);
		return true;
	case kNewHintTextID:
		ScrollToBottom();
		return true;
	case kChipID:
	case kChipBubbleID:
		SetMinimized(false);
		return true;
	case kFieldID:
		StartTyping();
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
	gameWindow = hWnd;
	CheckSavedDialog(true);

	if (!pWin || uiHidden)
	{
		return 0;	// no panel in this view, or it's hidden with the game's UI
	}

	if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)
	{
		// The mouse is left to the game, which gives the panel its own. A press elsewhere is only watched for: it's the
		// player turning back to the game, so the keys go to it again, and the typed text is kept for next time.
		bool press = uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN;
		if (typing && press && !IsOnPanel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
		{
			PauseTyping();
		}
	}
	else if (typing)
	{
		handled = HandleTypingKey(hWnd, uMsg, wParam, lParam);
	}
	else if (uMsg == WM_KEYDOWN && wParam == VK_RETURN && (lParam & kKeyRepeatFlag) == 0)
	{
		if (GameWantsEnter())
		{
			Log::Line("Enter was left to the game, since one of its dialogs or text boxes has it, or the player is driving");
		}
		else
		{
			StartTyping();
			handled = true;
		}
	}

	return 0;
}

// A left press on the panel, at a point from its top left corner. The game's own buttons and scrollbar take their
// presses themselves, so these are on the rest of the panel. Done on the press rather than the release, so there's no
// pressed button to keep track of.
void ChatPanel::Press(int32_t x, int32_t y)
{
	// Like a window, the panel comes over the game's panels when it's clicked, eg. the city view's bottom panels, which
	// the game adds after it
	pWin->PullToFront();

	if (minimized)
	{
		// The minimized panel is just the round button, so a press anywhere on it opens the panel
		SetMinimized(false);
		return;
	}

	uint32_t clicked = GetButtonAt(x, y);
	if (clicked)
	{
		Click(clicked);
	}
	else
	{
		StartDragging(x, y);
	}
}

// Scrolls the chat, so the wheel doesn't zoom the map under the panel
void ChatPanel::Wheel(const cGZMessage& msg)
{
	// Only the low word is the turn (like WM_MOUSEWHEEL's), so a turn down isn't read as a big turn up
	int32_t delta = static_cast<int16_t>(msg.dwData3 & 0xFFFF);

	// Once a game, since what the message holds was only seen once
	if (!loggedWheel)
	{
		Log::Line("The mouse wheel on the chat panel: d1=0x" + ToHex(msg.dwData1) + " d2=0x" + ToHex(msg.dwData2) + " d3=0x" + ToHex(msg.dwData3) + " (" + std::to_string(delta) + ")");
		loggedWheel = true;
	}

	if (minimized)
	{
		return;
	}

	int rows = delta * kWheelRows / WHEEL_DELTA;
	if (rows == 0 && delta > 0)
	{
		rows = 1;	// a touchpad's small steps
	}
	else if (rows == 0 && delta < 0)
	{
		rows = -1;
	}
	ScrollBy(rows);
}

// The panel is dragged by its title band, like the game's dialogs
void ChatPanel::StartDragging(int32_t x, int32_t y)
{
	if (y >= kTitleBandHeight)
	{
		return;
	}

	dragging = true;
	dragX = x;
	dragY = y;

	// So the moves keep coming when a fast drag leaves the panel, and the release ends it
	cIGZWinMgrPtr pWinMgr;
	if (pWinMgr)
	{
		pWinMgr->GZSetCapture(pWin);
	}
}

// Moves the panel to follow the mouse, keeping it inside the view, and remembers the place for the rest of the game
void ChatPanel::Drag(int32_t x, int32_t y)
{
	if (!pWin || minimized || GetKeyState(VK_LBUTTON) >= 0)
	{
		StopDragging();	// the release was missed, eg. the game lost the focus
		return;
	}

	// The point is from the panel's top left corner, which moves with it
	openLeft = pWin->GetL() + x - dragX;
	openTop = pWin->GetT() + y - dragY;
	ClampToView(openLeft, openTop);
	PlaceWindow(pWin, openLeft, openTop);
	moved = true;

	if (pParent)
	{
		pParent->InvalidateSelf();	// where the panel was has to be redrawn
	}
}

void ChatPanel::StopDragging()
{
	cIGZWinMgrPtr pWinMgr;
	if (dragging && pWin && pWinMgr && pWinMgr->GZGetCapture() == pWin)
	{
		pWinMgr->GZReleaseCapture(pWin);
	}

	dragging = false;
}

size_t ChatPanel::GetMaxScroll() const
{
	if (lines.size() <= kLineCount)
	{
		return 0;
	}
	return lines.size() - kLineCount;
}

// Scrolls the chat by `rows`, back to older lines if positive
void ChatPanel::ScrollBy(int rows)
{
	int64_t scrolled = static_cast<int64_t>(scrollRows) + rows;
	if (scrolled < 0)
	{
		scrolled = 0;
	}
	if (scrolled > static_cast<int64_t>(GetMaxScroll()))
	{
		scrolled = static_cast<int64_t>(GetMaxScroll());
	}

	scrollRows = static_cast<size_t>(scrolled);
	if (scrollRows == 0)
	{
		newBelow = 0;
	}

	ShowLines();
}

void ChatPanel::ScrollToBottom()
{
	scrollRows = 0;
	newBelow = 0;
	ShowLines();
}

// The game's scrollbar takes its own clicks, so the chat follows when the player moves it
void ChatPanel::FollowScrollbar()
{
	if (!pWin || minimized)
	{
		return;
	}

	cRZAutoRefCount<cIGZWinScrollbar> pScrollbar;
	if (!pWin->GetChildAs(kScrollbarID, kGZIID_cIGZWinScrollbar, pScrollbar.AsPPVoid()))
	{
		return;
	}

	int32_t value = pScrollbar->GetValue();
	if (value == scrollbarValue)
	{
		return;
	}

	// Its value is the top row shown (see ShowScroll), and ScrollBy sets it back if it's past the rows there are
	int64_t maxScroll = static_cast<int64_t>(GetMaxScroll());
	ScrollBy(static_cast<int>(maxScroll - value - static_cast<int64_t>(scrollRows)));
}

// Sets the game's scrollbar to the shown lines, and the hint for new lines below them
void ChatPanel::ShowScroll(bool open)
{
	// The game's scrollbar puts its (fixed size) thumb at the value's place between the minimum and the maximum, so the
	// range is the top rows that can be shown, and the value is the top row shown. While all the rows fit, the thumb is
	// at the bottom, with the newest lines.
	cRZAutoRefCount<cIGZWinScrollbar> pScrollbar;
	if (pWin->GetChildAs(kScrollbarID, kGZIID_cIGZWinScrollbar, pScrollbar.AsPPVoid()))
	{
		int32_t maxScroll = static_cast<int32_t>(GetMaxScroll());
		int32_t value = maxScroll - static_cast<int32_t>(scrollRows);
		if (maxScroll == 0)
		{
			maxScroll = 1;
			value = 1;
		}
		pScrollbar->SetMinMax(0, maxScroll);
		pScrollbar->SetPageSize(static_cast<int32_t>(kLineCount));
		pScrollbar->SetValue(value);
		scrollbarValue = pScrollbar->GetValue();	// as the scrollbar keeps it
	}
	SetChildVisible(kScrollbarID, open);

	bool hint = open && scrollRows > 0 && newBelow > 0;
	SetText(kNewHintTextID, std::to_string(newBelow) + " new below");
	SetChildVisible(kNewHintID, hint);
	SetChildVisible(kNewHintTextID, hint);
}

// Keeps the open panel's place inside its view
void ChatPanel::ClampToView(int32_t& left, int32_t& top) const
{
	if (!pParent)
	{
		return;
	}

	int32_t maxLeft = pParent->GetW() - openWidth;
	int32_t maxTop = pParent->GetH() - openHeight;
	if (left > maxLeft)
	{
		left = maxLeft;
	}
	if (top > maxTop)
	{
		top = maxTop;
	}
	if (left < 0)
	{
		left = 0;
	}
	if (top < 0)
	{
		top = 0;
	}
}

// Whether a point in the game window (where its Win32 mouse messages are) is on the panel
bool ChatPanel::IsOnPanel(int32_t x, int32_t y) const
{
	int32_t left = 0;
	int32_t top = 0;
	GetPanelOrigin(left, top);
	return x >= left && y >= top && x < left + pWin->GetW() && y < top + pWin->GetH();
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

// Returns the ID of the shown button (in kButtonIDs) at a point from the panel's top left corner, or 0 if there's none
uint32_t ChatPanel::GetButtonAt(int32_t x, int32_t y) const
{
	for (uint32_t id : kButtonIDs)
	{
		if (IsOnChild(id, x, y))
		{
			return id;
		}
	}

	return 0;
}

// Where a window is in the game window, adding up the places of its parents
static void GetWindowOrigin(cIGZWin* pWindow, int32_t& left, int32_t& top)
{
	left = 0;
	top = 0;
	for (int depth = 0; pWindow && depth < kMaxWinDepth; depth++)
	{
		left += pWindow->GetL();
		top += pWindow->GetT();
		pWindow = pWindow->GetParentWin();
	}
}

// Shows the caret, and starts its blink again, so it doesn't blink away while the player types
void ChatPanel::ResetCaret()
{
	caretShown = true;
	caretTime = GetTickCount64();
}

// Whether the player hid the city view's UI (with the corner button on its control panel, or Num Lock), eg. for a
// screenshot. The game hides its control panel then, which is looked for, since the command isn't seen here.
bool ChatPanel::IsGameUIHidden()
{
	cIGZWinMgrPtr pWinMgr;
	if (!pWinMgr || !pWinMgr->GetMainWindow())
	{
		return false;
	}

	cIGZWin* pPuck = pWinMgr->GetMainWindow()->GetChildWindowFromIDRecursive(kPuckID);
	return pPuck && !pPuck->IsVisible();
}

// Hides the panel (or its round button) with the game's UI, and shows it again with it. New messages are still
// counted meanwhile.
void ChatPanel::FollowGameUI()
{
	ULONGLONG now = GetTickCount64();
	if (!pWin || regionView || now - lastUICheck < kUICheckInterval)
	{
		return;
	}
	lastUICheck = now;

	bool hidden = IsGameUIHidden();
	if (hidden == uiHidden)
	{
		return;
	}
	uiHidden = hidden;

	if (hidden)
	{
		if (typing)
		{
			PauseTyping();	// the keys go back to the game, and the text is kept
		}
		StopDragging();
		pWin->HideWindow();
	}
	else
	{
		pWin->ShowWindow();
		Layout();
	}
}

void ChatPanel::HideSavedDialog()
{
	savedDialogDeadline = GetTickCount64() + kSavedDialogWait;
}

// Looks for the game's "City Saved." until the time's up. Called every frame, and from the game window's messages.
void ChatPanel::CheckSavedDialog(bool fromMessage)
{
	if (!savedDialogDeadline)
	{
		return;
	}

	ULONGLONG now = GetTickCount64();
	if (now > savedDialogDeadline)
	{
		savedDialogDeadline = 0;
		return;
	}

	// The game window gets many messages, so they only check every so often
	if (fromMessage && now - lastSavedDialogCheck < kSavedDialogInterval)
	{
		return;
	}
	lastSavedDialogCheck = now;

	CloseSavedDialog();
}

// Closes the game's "City Saved." message box, if it's up. While the game is modal, it's looked for down the middle of
// the game (it's above the centre) and known by its ID, or else by its text, or else by its shape if no text can be
// read. Its OK button (found at points across it, since the pinned gzcom-dll can't list a window's children) is then
// clicked like the player would, so the game closes it and ends its modal state itself.
void ChatPanel::CloseSavedDialog()
{
	cIGZWinMgrPtr pWinMgr;
	if (!pWinMgr || !gameWindow || !pWinMgr->IsModal())
	{
		return;	// not up (yet)
	}

	cIGZWin* pMain = pWinMgr->GetMainWindow();
	if (!pMain)
	{
		return;
	}
	int32_t width = pMain->GetW();
	int32_t height = pMain->GetH();

	// Down the middle, the first window smaller than the game: from there up, the outermost one that's smaller is the dialog
	cIGZWin* pDialog = nullptr;
	for (int32_t y = kSavedDialogScanTop; y < height - kSavedDialogScanTop && !pDialog; y += kSavedDialogScanStep)
	{
		cIGZWin* pHit = pWinMgr->GetWindowFromPoint(width / 2, y);
		for (int depth = 0; pHit && pHit != pMain && depth < kMaxWinDepth; depth++)
		{
			if (pHit->GetW() < width || pHit->GetH() < height)
			{
				pDialog = pHit;
			}
			pHit = pHit->GetParentWin();
		}

		// The chat panel may be there too, if the player dragged it over the middle, so the search goes on under it
		if (pDialog == pWin)
		{
			pDialog = nullptr;
		}
	}
	if (!pDialog)
	{
		return;
	}

	int32_t left = 0;
	int32_t top = 0;
	GetWindowOrigin(pDialog, left, top);

	bool saved = false;
	bool captions = false;
	int buttons = 0;
	int32_t okX = -1;
	int32_t okY = -1;
	int32_t buttonX = -1;
	int32_t buttonY = -1;
	std::vector<cIGZWin*> seen;
	for (int32_t y = top + 3; y < top + pDialog->GetH(); y += kSavedDialogStep)
	{
		for (int32_t x = left + 3; x < left + pDialog->GetW(); x += kSavedDialogStep)
		{
			cIGZWin* pAt = pWinMgr->GetWindowFromPoint(x, y);
			if (!pAt || std::find(seen.begin(), seen.end(), pAt) != seen.end())
			{
				continue;
			}
			seen.push_back(pAt);

			int32_t atLeft = 0;
			int32_t atTop = 0;
			GetWindowOrigin(pAt, atLeft, atTop);

			cRZAutoRefCount<cIGZWinBtn> pButton;
			bool isButton = pAt->QueryInterface(GZIID_cIGZWinBtn, pButton.AsPPVoid());
			if (isButton)
			{
				buttons++;
				buttonX = atLeft + pAt->GetW() / 2;
				buttonY = atTop + pAt->GetH() / 2;
			}

			std::string caption;
			cIGZString* pCaption = pAt->GetCaption();
			if (pCaption && pCaption->ToChar())
			{
				caption = pCaption->ToChar();
			}
			if (!caption.empty())
			{
				captions = true;
			}

			if (pAt->GetID() == kSavedDialogTextID || caption.find("City Saved") != std::string::npos)
			{
				saved = true;
			}
			else if (isButton && caption == "OK" && okX < 0)
			{
				okX = atLeft + pAt->GetW() / 2;
				okY = atTop + pAt->GetH() / 2;
			}
		}
	}

	// Without readable captions, a message box sized like "City Saved." with a single button, right after the save
	bool shaped = pDialog->GetW() >= 200 && pDialog->GetW() <= 360 && pDialog->GetH() >= 90 && pDialog->GetH() <= 220;
	bool byID = pDialog->GetID() == kSavedDialogID && okX >= 0;
	std::string details;	// how it was found, if not by its ID, to know if its ID changes
	if (!byID && saved && okX >= 0)
	{
		details = " (ID 0x" + ToHex(pDialog->GetID()) + ", found by its text)";
	}
	else if (!byID && !captions && shaped && buttons == 1)
	{
		details = " (ID 0x" + ToHex(pDialog->GetID()) + ", " + std::to_string(pDialog->GetW()) + "x" + std::to_string(pDialog->GetH()) + ", found by its shape, as its texts couldn't be read)";
		okX = buttonX;
		okY = buttonY;
	}
	else if (!byID)
	{
		return;	// another dialog of the game's, which is left alone
	}

	Log::Line("Hid the game's City Saved dialog" + details);
	savedDialogDeadline = 0;

	PostMessageW(gameWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(okX, okY));
	PostMessageW(gameWindow, WM_LBUTTONUP, 0, MAKELPARAM(okX, okY));
}

// Whether a point from the panel's top left corner is on one of its shown windows
bool ChatPanel::IsOnChild(uint32_t id, int32_t x, int32_t y) const
{
	cIGZWin* pChild = pWin->GetChildWindowFromID(id);
	return pChild && pChild->IsVisible()
		&& x >= pChild->GetL() && y >= pChild->GetT() && x < pChild->GetL() + pChild->GetW() && y < pChild->GetT() + pChild->GetH();
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
		else if (wParam == VK_PRIOR)
		{
			ScrollBy(static_cast<int>(kLineCount) - 1);
		}
		else if (wParam == VK_NEXT)
		{
			ScrollBy(1 - static_cast<int>(kLineCount));
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
	ResetCaret();

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

	// Enter on an empty field goes back to the game, so Enter, Enter does
	if (text.empty())
	{
		StopTyping();
		return;
	}

	// The player keeps typing, since chat often comes in a few messages in a row
	input.clear();
	ShowInput();
	ScrollToBottom();	// to see it arrive

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

// Stops typing but keeps the text, which is shown in the field until the player types again
void ChatPanel::PauseTyping()
{
	typing = false;
	ShowInput();
}

// Whether Enter is meant for the game instead, eg. to finish typing a cheat code, close a message box or in U-Drive-It
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

	// U-Drive-It binds Enter to "Alternate Special", in its key bindings (0x0C0EAAD0), while the player drives
	cISC4AppPtr pApp;
	cISC4City* pCity = nullptr;
	if (pApp)
	{
		pCity = pApp->GetCity();
	}
	if (pCity)
	{
		intptr_t managers[] = { pCity->GetVehicleManager(), pCity->GetAircraftManager(), pCity->GetWatercraftManager() };
		for (intptr_t manager : managers)
		{
			cISC4AutomataManager* pManager = reinterpret_cast<cISC4AutomataManager*>(manager);
			if (pManager && pManager->IsDrivingMode())
			{
				return true;
			}
		}
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

void ChatPanel::AddLine(const Notice& notice)
{
	const std::string& text = notice.text;
	bool chat = !notice.name.empty();
	int color = -1;
	if (chat)
	{
		color = FindNameColor(notice.color);
	}

	std::string namePrefix = notice.name + ":";
	bool first = true;
	size_t before = lines.size();

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
				Line shown;
				shown.text = Text::WideToUtf8(piece);
				shown.chat = chat;

				// Chat starts with "<name>: ", and the name is split off to be shown in its color. The lines it wraps onto have no name.
				if (first && chat && color >= 0 && shown.text.compare(0, namePrefix.size(), namePrefix) == 0)
				{
					shown.name = Shorten(notice.name, kNameLength);
					if (notice.own)
					{
						shown.name += " (You)";	// after shortening, so a long name can't cut it off
					}
					shown.color = color;
					shown.text = Trim(shown.text.substr(namePrefix.size()));
				}
				first = false;

				lines.push_back(shown);
			}
		}

		start = end + 1;
	}

	// While the player has scrolled up, the lines they're reading stay put, and the new ones are counted below
	if (scrollRows > 0 && lines.size() > before)
	{
		scrollRows += lines.size() - before;
		newBelow++;
	}

	while (lines.size() > kMaxLines)
	{
		lines.pop_front();
	}
	if (scrollRows > GetMaxScroll())
	{
		scrollRows = GetMaxScroll();
	}

	// Only other players' chat counts as new. Lines without a name (saves, joins, new cities, the message of the day)
	// and the player's own messages are shown, but not counted.
	if (!notice.history && minimized && chat && !notice.own)
	{
		unread++;
	}
}

void ChatPanel::SetMinimized(bool value)
{
	minimized = value;
	if (minimized)
	{
		StopDragging();
	}
	else
	{
		unread = 0;
		scrollRows = 0;	// reopened at the newest lines
		newBelow = 0;
	}

	Layout();
}

// Shows the windows for the panel's state, and sizes and places it
void ChatPanel::Layout()
{
	if (!pWin)
	{
		return;
	}

	bool open = !minimized;

	ShowLines();
	SetChildVisible(kBackgroundID, open);
	SetChildVisible(kInputID, open);
	SetChildVisible(kChatButtonID, open);
	for (uint32_t id : kTitleBarIDs)
	{
		SetChildVisible(id, !minimized);
	}
	SetChildVisible(kChipID, minimized);
	SetChildVisible(kChipBubbleID, minimized);

	// Shrunk too, so the empty part doesn't take clicks meant for the view under it
	int32_t width = openWidth;
	int32_t height = openHeight;
	if (minimized)
	{
		width = kChipWidth;
		height = kChipHeight;
	}

	pWin->SetW(width);
	pWin->SetH(height);

	if (pParent)
	{
		parentWidth = pParent->GetW();
		parentHeight = pParent->GetH();
	}

	// The open panel opens in the chip's corner (the top right in the city view, the bottom right in the region view),
	// unless the player has dragged it somewhere
	if (!moved)
	{
		openLeft = parentWidth - openWidth - kRightMargin;
		openTop = kTopMargin;
		if (regionView)
		{
			openTop = parentHeight - openHeight - kChipBottomMargin;
		}
	}

	if (minimized && pParent)
	{
		int32_t left = 0;
		int32_t top = 0;
		GetChipPlace(chipSpot, left, top);
		PlaceWindow(pWin, left, top);
		pWin->PullToFront();	// over windows the view added after it
	}
	else
	{
		ClampToView(openLeft, openTop);
		PlaceWindow(pWin, openLeft, openTop);
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

// Moves the panel, or one of its windows, to a place in its parent.
// The pinned gzcom-dll declares GZWinMoveTo one vtable slot late (see GZWinInterfaces.h), so calling it runs the
// game's next method, which moves the window by an offset. So it's given the distance to the new place.
void ChatPanel::PlaceWindow(cIGZWin* pTarget, int32_t left, int32_t top)
{
	int32_t dx = left - pTarget->GetL();
	int32_t dy = top - pTarget->GetT();
	if (dx == 0 && dy == 0)
	{
		return;
	}

	pTarget->GZWinMoveTo(dx, dy);

	if (pTarget->GetL() != left || pTarget->GetT() != top)
	{
		Log::Line("A chat panel window didn't move to (" + std::to_string(left) + ", " + std::to_string(top) + "), it's at (" + std::to_string(pTarget->GetL()) + ", " + std::to_string(pTarget->GetT()) + ")");
	}
}

// Shows the newest lines at the bottom, if the panel is open
void ChatPanel::ShowLines()
{
	if (!pWin)
	{
		return;
	}

	bool open = !minimized;

	// The rows shown end `scrollRows` above the newest line
	size_t end = lines.size() - scrollRows;
	size_t shown = end;
	if (shown > kLineCount)
	{
		shown = kLineCount;
	}
	size_t blank = kLineCount - shown;

	for (size_t i = 0; i < kLineCount; i++)
	{
		const Line* line = nullptr;
		if (i >= blank)
		{
			line = &lines[end - shown + i - blank];
		}

		ShowLine(static_cast<uint32_t>(i), line, open);
	}

	ShowScroll(open);
}

// Shows a line in a row of the panel, or clears the row if `line` is null. Chat is in navy, after the player's name in its color,
// and other lines are in grey. Each is a window of its own, since a text window has one color.
void ChatPanel::ShowLine(uint32_t row, const Line* line, bool open)
{
	if (!pWin)
	{
		return;
	}

	bool chat = line && line->chat;
	bool info = line && !line->chat;
	bool named = chat && !line->name.empty();

	// The message goes after the name, so its window is moved right of it. The name windows stay where the UI script puts them.
	cIGZWin* pRow = pWin->GetChildWindowFromID(kFirstNameID + row * kNameLineStep);
	cIGZWin* pText = pWin->GetChildWindowFromID(kFirstLineID + row);
	if (pRow && pText)
	{
		int32_t indent = 0;
		if (named)
		{
			indent = Text::EstimateWidth(Text::Utf8ToWide(line->name + ":")) + kNameGap;
		}

		PlaceWindow(pText, pRow->GetL() + indent, pText->GetT());
		pText->SetW(pRow->GetW() - indent);
	}

	if (chat)
	{
		SetText(kFirstLineID + row, line->text);
	}
	else
	{
		SetText(kFirstLineID + row, std::string());
	}
	SetChildVisible(kFirstLineID + row, open && chat);

	if (info)
	{
		SetText(kFirstInfoLineID + row, line->text);
	}
	else
	{
		SetText(kFirstInfoLineID + row, std::string());
	}
	SetChildVisible(kFirstInfoLineID + row, open && info);

	for (int color = 0; color < kNameColorCount; color++)
	{
		uint32_t id = kFirstNameID + row * kNameLineStep + static_cast<uint32_t>(color);
		bool shown = named && line->color == color;
		if (shown)
		{
			SetText(id, line->name + ":");
		}
		SetChildVisible(id, open && shown);
	}
}

void ChatPanel::ShowInput()
{
	bool open = !minimized;
	SetChildVisible(kFieldID, open && !typing);
	SetChildVisible(kTypingFieldID, open && typing);

	if (!typing && input.empty())
	{
		SetText(kInputID, kInputHint);
		return;
	}

	// A blinking line shows where the player types, but not for text kept from a click outside the panel. The text is
	// shortened to fit with it either way, so it doesn't jump when it blinks.
	std::wstring caret;
	if (typing && caretShown)
	{
		caret = L"|";
	}

	std::wstring shown = input;
	if (Text::EstimateWidth(shown + L"|") > kInputWidth)
	{
		while (!shown.empty() && Text::EstimateWidth(L"..." + shown + L"|") > kInputWidth)
		{
			shown.erase(0, 1);
		}
		shown = L"..." + shown;
	}

	SetText(kInputID, Text::WideToUtf8(shown + caret));
}

void ChatPanel::ShowTitle()
{
	if (serverName.empty())
	{
		SetText(kTitleID, kTitle);
	}
	else
	{
		SetText(kTitleID, Text::ShortenToWidth(serverName, kTitleWidth, kTitleFontScale));
	}

	std::string count = std::to_string(unread);
	if (unread > 99)
	{
		count = "99+";
	}

	bool countShown = minimized && unread > 0;
	SetText(kChipCountID, count);
	SetChildVisible(kChipCountBubbleID, countShown);
	SetChildVisible(kChipCountID, countShown);

	ShowChipTip();
}

// The chip's tooltip has the save status and the unread count, eg. "Saved 14:13, 3 new"
void ChatPanel::ShowChipTip()
{
	std::string tip = status.second;
	if (unread > 0)
	{
		if (!tip.empty())
		{
			tip += ", ";
		}
		tip += std::to_string(unread) + " new";
	}
	if (tip.empty())
	{
		tip = "Chat";
	}

	SetTip(kChipID, tip);
	SetTip(kChipBubbleID, tip);
}

// Where the chip goes in its view: home in the top right corner (the bottom right in the region view, where its top
// right has the game's buttons), just below a window of the game's that covers it there, or in the bottom right corner
// if that one's too tall
void ChatPanel::GetChipPlace(ChipSpot spot, int32_t& left, int32_t& top) const
{
	left = parentWidth - kChipWidth - kRightMargin;
	top = kTopMargin;
	if (regionView)
	{
		top = parentHeight - kChipHeight - kChipBottomMargin;
	}

	if (spot == ChipSpot::Below)
	{
		top = coverBottom;
	}
	else if (spot == ChipSpot::Corner)
	{
		top = parentHeight - kChipHeight - kChipBottomMargin;
	}
}

// Returns the window of the game's that covers a place on the chip, as the view's child it belongs to, or null. Full
// size layers (eg. the region view's map) don't count, nor do the chip's own windows.
cIGZWin* ChatPanel::GetCoveringWindow(int32_t left, int32_t top) const
{
	cIGZWinMgrPtr pWinMgr;
	if (!pWinMgr || !pParent)
	{
		return nullptr;
	}

	// The view's place in the game window, where the window manager's points are
	int32_t viewLeft = 0;
	int32_t viewTop = 0;
	GetPanelOrigin(viewLeft, viewTop);
	viewLeft -= pWin->GetL();
	viewTop -= pWin->GetT();

	const int32_t points[][2] = { { 6, 12 }, { kChipWidth - 6, 12 }, { kChipWidth / 2, kChipHeight / 2 }, { 6, kChipHeight - 4 }, { kChipWidth - 6, kChipHeight - 4 } };
	for (const auto& point : points)
	{
		cIGZWin* pHit = pWinMgr->GetWindowFromPoint(viewLeft + left + point[0], viewTop + top + point[1]);

		// Up to the view's child it's in. Hits on the view itself, or outside it, don't count.
		cIGZWin* pTop = nullptr;
		for (int depth = 0; pHit && depth < kMaxWinDepth; depth++)
		{
			if (pHit == pWin)
			{
				pTop = nullptr;
				break;
			}
			if (pHit->GetParentWin() == pParent)
			{
				pTop = pHit;
				break;
			}
			pHit = pHit->GetParentWin();
		}

		if (pTop && pTop->IsVisible() && (pTop->GetW() < parentWidth || pTop->GetH() < parentHeight))
		{
			return pTop;
		}
	}

	return nullptr;
}

// Moves the chip out of the way of a window of the game's that covers its home (eg. the bridge picker or the tutorial),
// and back home once it's clear. Checked a few times a second, and only moved once the new spot has been wanted for a while.
void ChatPanel::AvoidCoveringWindows()
{
	if (!pWin || !pParent || !minimized)
	{
		return;
	}

	ULONGLONG now = GetTickCount64();
	if (now - lastChipCheck < kChipCheckInterval)
	{
		return;
	}
	lastChipCheck = now;

	int32_t homeLeft = 0;
	int32_t homeTop = 0;
	GetChipPlace(ChipSpot::Home, homeLeft, homeTop);

	ChipSpot wanted = ChipSpot::Home;
	int32_t wantedBottom = coverBottom;
	cIGZWin* pCover = GetCoveringWindow(homeLeft, homeTop);
	if (pCover)
	{
		wanted = ChipSpot::Corner;
		wantedBottom = pCover->GetT() + pCover->GetH() + 8;
		if (wantedBottom + kChipHeight <= parentHeight - kChipBottomMargin && !GetCoveringWindow(homeLeft, wantedBottom))
		{
			wanted = ChipSpot::Below;
		}
	}

	if (wanted != wantedChipSpot)
	{
		wantedChipSpot = wanted;
		wantedSince = now;
	}

	if (wanted == chipSpot && (wanted != ChipSpot::Below || wantedBottom == coverBottom))
	{
		return;
	}
	if (now - wantedSince < kChipMoveDelay)
	{
		return;
	}

	const char* const names[] = { "home, in the top right corner", "below a window of the game's that covers its home", "to the bottom right corner, since a window of the game's covers its home" };
	Log::Line(std::string("Moved the chat chip ") + names[static_cast<int>(wanted)]);

	chipSpot = wanted;
	coverBottom = wantedBottom;
	Layout();
}

// Shows the launcher's save status in the title bar, or on the chip when minimized
void ChatPanel::ShowStatus()
{
	const auto& [state, text] = status;

	// The icon is shown in the title bar, or on the chip when minimized, and the text is its tooltip. An unknown state
	// from a newer launcher has no icon.
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

	SetTip(kStatusTipID, text);
	ShowChipTip();
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

// Sets a button's tooltip, which the pinned gzcom-dll's cIGZWinBtn can change after the UI script made it
void ChatPanel::SetTip(uint32_t id, const std::string& text)
{
	if (!pWin)
	{
		return;
	}

	cRZAutoRefCount<cIGZWinBtn> pButton;
	if (pWin->GetChildAs(id, GZIID_cIGZWinBtn, pButton.AsPPVoid()))
	{
		pButton->SetTipText(cRZBaseString(text));
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
