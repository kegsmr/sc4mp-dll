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
#include <iterator>
#include <vector>

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
static constexpr uint32_t kScrollbarID = 0x7B3E9A6B;
static constexpr uint32_t kNewHintID = 0x7B3E9A6C;		// "N new below", while the player has scrolled up
static constexpr uint32_t kNewHintTextID = 0x7B3E9A6D;

// The minimized panel, a round button in the corner with the status ring, the globe and the unread count on it
static constexpr uint32_t kChipID = 0x7B3E9AA0;	// the round button, under the status ring and the globe
static constexpr uint32_t kChipCountID = 0x7B3E9AA1;	// the unread count, on the red bubble
static constexpr uint32_t kChipCountBubbleID = 0x7B3E9AA9;
static constexpr uint32_t kChipGlobeID = 0x7B3E9AAA;	// the globe, a button too, so it's clickable and lights up
static constexpr uint32_t kChipIconOffset = 0x10;	// each status ring on the chip has the ID of the title bar's icon plus this
static constexpr int32_t kChipWidth = 56;	// the 50 pixel button, with room for the count bubble over its top right
static constexpr int32_t kChipHeight = 56;
static constexpr int32_t kTitleBandHeight = 34;	// the frame's title band, by which the panel is dragged
static constexpr int32_t kResizeCorner = 16;		// how far into a bottom corner a press resizes the panel, instead of dragging it
static constexpr int32_t kRightMargin = 12;			// the chip and the open panel are in the view's top right corner
static constexpr int32_t kTopMargin = 16;			// in the city view, whose corner is free
static constexpr int32_t kChipBottomMargin = 48;	// above the city view's news ticker, and the region view's bottom bar
static constexpr ULONGLONG kChipCheckInterval = 250;	// how often the chip checks whether a window of the game covers it
static constexpr ULONGLONG kChipMoveDelay = 500;	// how long a new spot has to stay wanted before the chip moves, so it doesn't flicker
static constexpr uint32_t kPuckID = 0x0987B48F;	// the city view's control panel (ui C973B411), which hides with the game's UI
static constexpr ULONGLONG kUICheckInterval = 250;	// how often it's checked

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

// The colors of save results, the status icons' green and red (in tools/make-ui-images.py), in windows after each row's name windows, at
// kFirstNameID + row * kNameLineStep + kResultColorOffset + color. The launcher sends a result's color, and the nearest of these is used.
static constexpr uint8_t kResultColors[][3] = {
	{ 46, 160, 67 },	// saved
	{ 206, 52, 52 },	// failed
};
static constexpr int kResultColorCount = static_cast<int>(std::size(kResultColors));
static constexpr uint32_t kResultColorOffset = 0xC;
static_assert(static_cast<uint32_t>(kNameColorCount) <= kResultColorOffset && kResultColorOffset + static_cast<uint32_t>(kResultColorCount) <= kNameLineStep, "a row's color windows overlap");

// The panel's windows that are clicked like buttons but aren't the game's, so their presses come to the panel. Only the
// shown ones can be clicked.
static constexpr uint32_t kButtonIDs[] = { kFieldID, kNewHintTextID };	// the field is clicked to type

// How the panel's windows follow its size (see FitChildren). The rows' windows are stretched with the title, and the status
// icons move with the title bar's buttons.
static constexpr uint32_t kRightEdgeIDs[] = { kStatusTipID, kMinimizeButtonID, kScrollbarID };
static constexpr uint32_t kBottomEdgeIDs[] = { kFieldID, kTypingFieldID, kInputID };
static constexpr uint32_t kCornerIDs[] = { kChatButtonID, kNewHintID, kNewHintTextID };
static constexpr uint32_t kWideIDs[] = { kTitleID, kFieldID, kTypingFieldID, kInputID };

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

static constexpr size_t kLineCount = 8;				// the rows the UI script lays the panel out for
static constexpr size_t kMinLineCount = 4;			// the rows the player can resize the panel to
static constexpr size_t kMaxLineCount = 16;			// the rows in the UI script, as many as its IDs leave room for
static constexpr int32_t kLineHeight = 18;
static constexpr int32_t kLineWidth = 320;			// the rows' windows in the UI script
static constexpr int32_t kLineGutter = 2;			// their text's gutters on each side
static constexpr int32_t kMinWidth = 300;			// so the title, the status and the text box still fit
static constexpr int32_t kMaxWidth = 640;			// as wide as the text field's middle is drawn (FIELD_MIDDLE_WIDTH in make-ui-images.py)
static constexpr size_t kMaxLines = 200;			// rows kept to scroll back through, wrapped rows included
static constexpr int kWheelRows = 3;				// rows scrolled per notch of the mouse wheel
static constexpr int32_t kInputWidth = 276;			// the input's text window, less its gutters; the end of a longer message is shown while typing it
static constexpr size_t kMaxInputLength = 500;
static constexpr size_t kMaxInbox = 250;			// more than the launcher replays on connecting (GameBridge.RECENT_LINES), which comes all at once
static constexpr int32_t kTitleWidth = 290;			// the title's text window, less its gutters, before the status icon
static constexpr int32_t kTitleFontScale = 9;		// the title's font (DataInsetHeader) is about 9/8 as wide as the chat lines' (see Text::ShortenToWidth)
static constexpr size_t kNameLength = 20;			// a longer player name is shortened
static constexpr int32_t kNameGap = 4;				// between a player's name and their message

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

// Splits a line into pieces that fit `firstWidth` pixels on the first row and `width` on the rest (see
// Text::EstimateWidth), breaking at spaces where possible
static std::vector<std::wstring> Wrap(const std::wstring& text, int32_t firstWidth, int32_t width)
{
	std::vector<std::wstring> pieces;
	size_t start = 0;
	int32_t room = firstWidth;

	while (Text::EstimateWidth(text.substr(start)) > room)
	{
		// The most characters that fit
		size_t fit = start;
		int32_t used = 0;
		while (fit < text.size())
		{
			used += Text::EstimateWidth(std::wstring(1, text[fit]));
			if (used > room)
			{
				break;
			}
			fit++;
		}

		size_t end = text.rfind(L' ', fit);
		if (end == std::wstring::npos || end <= start)
		{
			end = fit;	// a word that's too long is cut
		}
		if (end == start)
		{
			end = start + 1;	// at least a character a row, however narrow
		}

		pieces.push_back(text.substr(start, end - start));

		start = end;
		while (start < text.size() && text[start] == L' ')
		{
			start++;
		}
		room = width;
	}

	pieces.push_back(text.substr(start));
	return pieces;
}


// Returns the color in `colors` (eg. kNameColors) nearest to the launcher's, which comes as "[red,green,blue]", or -1 if there's none
static int FindNearestColor(const std::string& color, const uint8_t colors[][3], int colorCount)
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
	for (int i = 0; i < colorCount; i++)
	{
		int distance = 0;
		for (int channel = 0; channel < 3; channel++)
		{
			int difference = rgb[channel] - colors[i][channel];
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
	SetCanvasFilter(false);

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

	// The UI script's size, which its windows are laid out for, and the size the player has resized the panel to this game
	layoutWidth = pWin->GetW();
	layoutHeight = pWin->GetH();
	openWidth = layoutWidth + extraWidth;
	openHeight = layoutHeight + (static_cast<int32_t>(lineCount) - static_cast<int32_t>(kLineCount)) * kLineHeight;

	Layout();
	ShowLines();

	pWin->PullToFront();
	pWin->ShowWindow();

	if (!SetKeyFilter(true))
	{
		Log::Line("Unable to listen for keys, so Enter won't open the chat (the Chat button still works)");
	}
	if (!SetCanvasFilter(true))
	{
		Log::Line("Unable to watch the game window's messages, so typing some characters and pasting may not work");
	}

	Log::Line("Showing the chat panel");
}

void ChatPanel::Detach()
{
	typing = false;	// the unsent text is kept, and shown in the next view's panel
	StopDragging();
	ReleaseFocus();

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
	TakeFocus();
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

void ChatPanel::PostProgress(const std::string& text)
{
	std::lock_guard<std::mutex> lock(inboxMutex);

	// Only the latest progress matters
	pendingProgress = text;
}

bool ChatPanel::QueryInterface(uint32_t riid, void** ppvObj)
{
	if (riid == GZIID_cIGZWinProc)
	{
		*ppvObj = static_cast<cIGZWinProc*>(this);
		AddRef();
		return true;
	}

	if (riid == kGZIID_cIGZWinMessageFilter)
	{
		*ppvObj = static_cast<cIGZWinMessageFilter*>(this);
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
	std::optional<std::string> newProgress;
	{
		std::lock_guard<std::mutex> lock(inboxMutex);
		received.swap(inbox);
		newStatus.swap(pendingStatus);
		newServerName.swap(pendingServerName);
		newProgress.swap(pendingProgress);
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

	if (newProgress && *newProgress != progress)
	{
		// While the player has scrolled up, the lines they're reading stay put when the progress row comes or goes
		if (scrollRows > 0 && progress.empty())
		{
			scrollRows++;
		}
		else if (scrollRows > 0 && newProgress->empty())
		{
			scrollRows--;
			if (scrollRows == 0)
			{
				newBelow = 0;
			}
		}
		progress = *newProgress;
		ShowLines();
	}

	// The corner moves when the game's resolution changes, and the open panel is kept inside the view
	if (pWin && pParent && (pParent->GetW() != parentWidth || pParent->GetH() != parentHeight))
	{
		Layout();
	}

	AvoidCoveringWindows();
	FollowGameUI();
	FollowScrollbar();

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

// The game's button clicks, the mouse on the panel, and the keys while it has the focus. What the panel handles doesn't
// reach the map under it or the game's shortcuts, and the rest (eg. drawing) is left to the game.
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
	case kGZWinMessageMouseUpR:
		return true;
	case kGZWinMessageMouseMove:
		if (resizing)
		{
			Resize(x, y);
			return true;
		}
		if (!dragging)
		{
			return false;	// left to the game, which shows the buttons' tooltips and hover art
		}
		Drag(x, y);
		return true;
	case kGZWinMessageMouseWheel:
		Wheel(msg);
		return true;
	case kGZWinMessageCharacter:
	case kGZWinMessageKeyDown:
	case kGZWinMessageKeyUp:
		if (&msg == filteredKey)
		{
			filteredKey = nullptr;
			return true;	// the key filter has typed it already
		}
		return HandleKey(msg);
	case kGZWinMessageKillFocus:
		// The player turned back to the game, eg. by clicking the map, so the typed text is kept for next time. The focus
		// is the game's to give then.
		hasFocus = false;
		if (typing)
		{
			PauseTyping();
		}
		return false;
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
	case kChipGlobeID:
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

// The game's key filter, which sees the keys before the window with the focus. Only Enter is taken, to open the chat
// while the panel doesn't have the focus.
bool ChatPanel::DoMessage(cIGZWin* pSender, cGZMessage& filtered)
{
	if (filtered.dwMessageType != kGZWinMessageFilterKey || filtered.dwData3 == 0)
	{
		return false;
	}
	cGZMessage& msg = *reinterpret_cast<cGZMessage*>(filtered.dwData3);

	// While the player types, every key is the chat's, so the game's shortcuts (and Esc) don't act on it. The game may
	// still send it on to the panel, which then leaves it alone.
	if (typing)
	{
		HandleKey(msg);
		filteredKey = &msg;
		return true;
	}

	if (msg.dwData1 != VK_RETURN)
	{
		return false;
	}

	if (msg.dwMessageType == kGZWinMessageKeyUp)
	{
		enterDown = false;
		return false;
	}

	if (msg.dwMessageType != kGZWinMessageKeyDown)
	{
		return false;
	}

	// The game's messages don't say if the key was already down, so a held Enter is kept track of here
	bool repeat = enterDown;
	enterDown = true;
	if (repeat || typing || !pWin || uiHidden)
	{
		return false;	// held, already typing, no panel in this view, or it's hidden with the game's UI
	}

	if (GameWantsEnter())
	{
		return false;	// one of the game's dialogs or text boxes has it, or the player is driving
	}

	StartTyping();
	return true;
}

// The game window's messages, which are only watched to know the game's window (for typing and the clipboard)
LRESULT ChatPanel::FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled)
{
	handled = false;
	gameWindow = hWnd;
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
	else if (y >= openHeight - kResizeCorner && x < kResizeCorner)
	{
		StartResizing(x, y, true);
	}
	else if (y >= openHeight - kResizeCorner && x >= openWidth - kResizeCorner)
	{
		StartResizing(x, y, false);
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

// Ends dragging or resizing the panel
void ChatPanel::StopDragging()
{
	cIGZWinMgrPtr pWinMgr;
	if ((dragging || resizing) && pWin && pWinMgr && pWinMgr->GZGetCapture() == pWin)
	{
		pWinMgr->GZReleaseCapture(pWin);
	}

	dragging = false;
	resizing = false;
}

// The panel is resized by its bottom corners, from a press on one at a point from its top left corner
void ChatPanel::StartResizing(int32_t x, int32_t y, bool left)
{
	resizing = true;
	resizingLeft = left;
	resizeX = pWin->GetL() + x;
	resizeY = pWin->GetT() + y;
	resizeLeft = openLeft;
	resizeWidth = openWidth;
	resizeHeight = openHeight;
	resizeLineCount = lineCount;

	// Like dragging, so the moves keep coming when the mouse leaves the panel, and the release ends it
	cIGZWinMgrPtr pWinMgr;
	if (pWinMgr)
	{
		pWinMgr->GZSetCapture(pWin);
	}
}

// Resizes the panel to follow the mouse. The pressed corner follows it, and the opposite edges stay put. The height
// is whole rows, the panel is kept inside the view, and the size is kept for the rest of the game.
void ChatPanel::Resize(int32_t x, int32_t y)
{
	if (!pWin || !pParent || minimized || GetKeyState(VK_LBUTTON) >= 0)
	{
		StopDragging();	// the release was missed, eg. the game lost the focus
		return;
	}

	// How far the mouse has moved since the press. The point is from the panel's top left corner, which moves with the left edge.
	int32_t moveX = pWin->GetL() + x - resizeX;
	int32_t moveY = pWin->GetT() + y - resizeY;

	int32_t width = resizeWidth + moveX;
	int32_t maxWidth = pParent->GetW() - resizeLeft;
	if (resizingLeft)
	{
		width = resizeWidth - moveX;
		maxWidth = resizeLeft + resizeWidth;
	}
	if (width > maxWidth)
	{
		width = maxWidth;
	}
	if (width > kMaxWidth)
	{
		width = kMaxWidth;
	}
	if (width < kMinWidth)
	{
		width = kMinWidth;
	}

	// The rows nearest to the height, between the title band and the bottom, which stay the same
	int32_t frame = resizeHeight - static_cast<int32_t>(resizeLineCount) * kLineHeight;
	int32_t rows = (resizeHeight + moveY - frame + kLineHeight / 2) / kLineHeight;
	int32_t maxRows = (pParent->GetH() - openTop - frame) / kLineHeight;
	if (rows > maxRows)
	{
		rows = maxRows;
	}
	if (rows > static_cast<int32_t>(kMaxLineCount))
	{
		rows = static_cast<int32_t>(kMaxLineCount);
	}
	if (rows < static_cast<int32_t>(kMinLineCount))
	{
		rows = static_cast<int32_t>(kMinLineCount);
	}

	int32_t left = resizeLeft;
	if (resizingLeft)
	{
		left = resizeLeft + resizeWidth - width;
	}
	if (width == openWidth && static_cast<size_t>(rows) == lineCount && left == openLeft)
	{
		return;
	}

	int32_t oldWidth = GetLineWidth();
	extraWidth += width - openWidth;
	openWidth = width;
	openLeft = left;
	lineCount = static_cast<size_t>(rows);
	openHeight = frame + rows * kLineHeight;
	moved = true;	// its place is the player's now, so it isn't put back in the corner at its new size

	if (GetLineWidth() != oldWidth)
	{
		Rewrap();
	}
	if (scrollRows > GetMaxScroll())
	{
		scrollRows = GetMaxScroll();
	}

	Layout();
}

// The lines, and the save's progress after them while there's one
size_t ChatPanel::GetRowCount() const
{
	if (progress.empty())
	{
		return lines.size();
	}
	return lines.size() + 1;
}

size_t ChatPanel::GetMaxScroll() const
{
	if (GetRowCount() <= lineCount)
	{
		return 0;
	}
	return GetRowCount() - lineCount;
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
		pScrollbar->SetPageSize(static_cast<int32_t>(lineCount));
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

// Whether a point from the panel's top left corner is on one of its shown windows
bool ChatPanel::IsOnChild(uint32_t id, int32_t x, int32_t y) const
{
	cIGZWin* pChild = pWin->GetChildWindowFromID(id);
	return pChild && pChild->IsVisible()
		&& x >= pChild->GetL() && y >= pChild->GetT() && x < pChild->GetL() + pChild->GetW() && y < pChild->GetT() + pChild->GetH();
}

// The keys that come to the panel while it has the focus. They're all kept from the game while the player types, so it
// doesn't also act on them (eg. B for bulldoze).
bool ChatPanel::HandleKey(const cGZMessage& msg)
{
	// The key filter may not see Enter's release while the panel takes it
	if (msg.dwMessageType == kGZWinMessageKeyUp && msg.dwData1 == VK_RETURN)
	{
		enterDown = false;
	}

	if (!typing)
	{
		return false;
	}

	if (msg.dwMessageType == kGZWinMessageCharacter)
	{
		TypeCharacter(msg.dwData1);
	}
	else if (msg.dwMessageType == kGZWinMessageKeyDown)
	{
		switch (msg.dwData1)
		{
		case VK_RETURN:
			enterDown = true;
			Send();
			break;
		case VK_PRIOR:
			ScrollBy(static_cast<int>(lineCount) - 1);
			break;
		case VK_NEXT:
			ScrollBy(1 - static_cast<int>(lineCount));
			break;
		case VK_ESCAPE:
			StopTyping();
			break;
		case 'V':
			if (GetKeyState(VK_CONTROL) < 0)
			{
				Paste();
			}
			break;
		}
	}

	return true;
}

// Types a Character message's character, which is taken to be WM_CHAR's. An ANSI window gets the characters past ASCII in
// the Windows code page, as a byte, which the game may pass on sign-extended (its cIGZWin::GZOnCharacter takes an int8_t).
void ChatPanel::TypeCharacter(uint32_t code)
{
	bool ansiByte = code >= 0xFFFFFF80;
	if (code >= 0x80 && code <= 0xFF && (!gameWindow || !IsWindowUnicode(gameWindow)))
	{
		ansiByte = true;
	}

	wchar_t c = static_cast<wchar_t>(code);
	if (ansiByte)
	{
		char byte = static_cast<char>(code);
		MultiByteToWideChar(CP_ACP, 0, &byte, 1, &c, 1);
	}

	Type(c);
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

void ChatPanel::Paste()
{
	if (!OpenClipboard(gameWindow))
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
	filteredKey = nullptr;
	input.clear();
	ShowInput();
	ReleaseFocus();
}

// Stops typing but keeps the text, which is shown in the field until the player types again
void ChatPanel::PauseTyping()
{
	typing = false;
	filteredKey = nullptr;
	ShowInput();
	ReleaseFocus();
}

// The keys go to the window with the game's focus, so the panel takes it while the player types. The panel only accepts
// it then, so a click on it doesn't take the keys from the game's shortcuts otherwise.
void ChatPanel::TakeFocus()
{
	cIGZWinMgrPtr pWinMgr;
	if (!pWin || !pWinMgr || hasFocus)
	{
		return;
	}

	cIGZWin* pFocus = pWinMgr->GZGetFocus();
	if (pFocus != pWin)
	{
		pPreviousFocus = pFocus;
	}

	pWin->SetFlag(kGZWinFlagAcceptFocus, true);
	if (!pWinMgr->GZSetFocus(pWin))
	{
		Log::Line("Unable to give the chat panel the game's focus, so it won't get the keys");
		return;
	}
	hasFocus = true;
}

// Gives the focus back to what had it before typing (or else the view, or no window), so the game's shortcuts work again
void ChatPanel::ReleaseFocus()
{
	cIGZWin* pPrevious = pPreviousFocus;
	bool held = hasFocus;
	hasFocus = false;
	pPreviousFocus = nullptr;

	if (!pWin)
	{
		return;
	}
	pWin->SetFlag(kGZWinFlagAcceptFocus, false);

	cIGZWinMgrPtr pWinMgr;
	if (!held || !pWinMgr || pWinMgr->GZGetFocus() != pWin)
	{
		return;	// it went elsewhere already
	}

	cIGZWin* candidates[] = { pPrevious, pParent, nullptr };
	for (cIGZWin* pCandidate : candidates)
	{
		if (pCandidate && !pWinMgr->IsWindowValid(pCandidate))
		{
			continue;
		}

		if (pWinMgr->GZSetFocus(pCandidate) && pWinMgr->GZGetFocus() != pWin)
		{
			return;
		}
	}

	Log::Line("Unable to give the game's focus back from the chat panel");
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
	bool chat = !notice.name.empty();
	size_t before = lines.size();

	notices.push_back(notice);
	while (notices.size() > kMaxLines)
	{
		notices.pop_front();
	}

	WrapNotice(notice);

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

// Adds a message's rows to `lines`, wrapped to the panel's width
void ChatPanel::WrapNotice(const Notice& notice)
{
	const std::string& text = notice.text;
	bool chat = !notice.name.empty();
	int color = -1;
	int resultColor = -1;
	if (chat)
	{
		color = FindNearestColor(notice.color, kNameColors, kNameColorCount);
	}
	else if (!notice.color.empty())
	{
		resultColor = FindNearestColor(notice.color, kResultColors, kResultColorCount);
	}

	std::string namePrefix = notice.name + ":";
	bool first = true;

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
			// Chat starts with "<name>: ", and the name is split off to be shown in its color, so the message starts after
			// it on the first row. The rows it wraps onto have no name.
			std::string name;
			int32_t width = GetLineWidth();
			int32_t firstWidth = width;
			if (first && chat && color >= 0 && line.compare(0, namePrefix.size(), namePrefix) == 0)
			{
				name = Shorten(notice.name, kNameLength);
				if (notice.own)
				{
					name += " (You)";	// after shortening, so a long name can't cut it off
				}
				line = Trim(line.substr(namePrefix.size()));
				firstWidth = width - Text::EstimateWidth(Text::Utf8ToWide(name + ":")) - kNameGap;
			}

			for (const std::wstring& piece : Wrap(Text::Utf8ToWide(line), firstWidth, width))
			{
				Line shown;
				shown.text = Text::WideToUtf8(piece);
				shown.chat = chat;
				shown.resultColor = resultColor;
				if (!name.empty())
				{
					shown.name = name;
					shown.color = color;
					name.clear();
				}
				lines.push_back(shown);
			}
			first = false;
		}

		start = end + 1;
	}
}

// Wraps the kept messages again, after the panel's width changed
void ChatPanel::Rewrap()
{
	lines.clear();
	for (const Notice& notice : notices)
	{
		WrapNotice(notice);
	}

	while (lines.size() > kMaxLines)
	{
		lines.pop_front();
	}
}

// How wide a row's text can be at the panel's width, in pixels, less its window's gutters
int32_t ChatPanel::GetLineWidth() const
{
	return kLineWidth + extraWidth - 2 * kLineGutter;
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

	FitChildren();
	ShowLines();
	SetChildVisible(kBackgroundID, open);
	SetChildVisible(kInputID, open);
	SetChildVisible(kChatButtonID, open);
	for (uint32_t id : kTitleBarIDs)
	{
		SetChildVisible(id, !minimized);
	}
	SetChildVisible(kChipID, minimized);
	SetChildVisible(kChipGlobeID, minimized);

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

// Moves and stretches the panel's windows from the size they're laid out for to the open panel's size. Each keeps to
// the edges it's anchored to: the title bar's buttons to the right, the text box to the bottom, and so on.
void ChatPanel::FitChildren()
{
	int32_t dw = openWidth - layoutWidth;
	int32_t dh = openHeight - layoutHeight;
	if (dw == 0 && dh == 0)
	{
		return;
	}

	for (uint32_t id : kRightEdgeIDs)
	{
		FitChild(id, dw, 0, 0, 0);
	}

	// Some states share an icon, which is moved once
	std::vector<uint32_t> icons;
	for (const auto& [state, id] : kStatusIcons)
	{
		if (std::find(icons.begin(), icons.end(), id) == icons.end())
		{
			icons.push_back(id);
			FitChild(id, dw, 0, 0, 0);
		}
	}
	for (uint32_t id : kBottomEdgeIDs)
	{
		FitChild(id, 0, dh, 0, 0);
	}
	for (uint32_t id : kCornerIDs)
	{
		FitChild(id, dw, dh, 0, 0);
	}
	for (uint32_t id : kWideIDs)
	{
		FitChild(id, 0, 0, dw, 0);
	}
	FitChild(kBackgroundID, 0, 0, dw, dh);	// the frame, an edge image, which stretches
	FitChild(kScrollbarID, 0, 0, 0, dh);

	// The rows stay at the top. Their chat text windows are placed after the name by ShowLine.
	for (uint32_t row = 0; row < kMaxLineCount; row++)
	{
		FitChild(kFirstInfoLineID + row, 0, 0, dw, 0);
		for (uint32_t color = 0; color < static_cast<uint32_t>(kNameColorCount); color++)
		{
			FitChild(kFirstNameID + row * kNameLineStep + color, 0, 0, dw, 0);
		}
		for (uint32_t color = 0; color < static_cast<uint32_t>(kResultColorCount); color++)
		{
			FitChild(kFirstNameID + row * kNameLineStep + kResultColorOffset + color, 0, 0, dw, 0);
		}
	}

	layoutWidth = openWidth;
	layoutHeight = openHeight;
}

// Moves one of the panel's windows by (dx, dy) and makes it dw wider and dh taller
void ChatPanel::FitChild(uint32_t id, int32_t dx, int32_t dy, int32_t dw, int32_t dh)
{
	cIGZWin* pChild = pWin->GetChildWindowFromID(id);
	if (!pChild)
	{
		return;
	}

	PlaceWindow(pChild, pChild->GetL() + dx, pChild->GetT() + dy);
	if (dw != 0)
	{
		pChild->SetW(pChild->GetW() + dw);
	}
	if (dh != 0)
	{
		pChild->SetH(pChild->GetH() + dh);
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

	// The save's progress is a row after the lines, which isn't kept with them
	Line progressLine;
	progressLine.text = Text::ShortenToWidth(progress, GetLineWidth(), 8);

	// The rows shown end `scrollRows` above the newest one. The UI script's rows past the ones that fit are cleared.
	size_t end = GetRowCount() - scrollRows;
	size_t shown = end;
	if (shown > lineCount)
	{
		shown = lineCount;
	}
	size_t blank = lineCount - shown;

	for (size_t i = 0; i < kMaxLineCount; i++)
	{
		const Line* line = nullptr;
		if (i >= blank && i < lineCount)
		{
			size_t index = end - shown + i - blank;
			if (index < lines.size())
			{
				line = &lines[index];
			}
			else
			{
				line = &progressLine;
			}
		}

		ShowLine(static_cast<uint32_t>(i), line, open);
	}

	ShowScroll(open);
}

// Shows a line in a row of the panel, or clears the row if `line` is null. Chat is in navy, after the player's name in its color,
// save results in their color, and other lines are in grey. Each is a window of its own, since a text window has one color.
void ChatPanel::ShowLine(uint32_t row, const Line* line, bool open)
{
	if (!pWin)
	{
		return;
	}

	bool chat = line && line->chat;
	bool info = line && !line->chat && line->resultColor < 0;
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

	for (int color = 0; color < kResultColorCount; color++)
	{
		uint32_t id = kFirstNameID + row * kNameLineStep + kResultColorOffset + static_cast<uint32_t>(color);
		bool shown = line && !line->chat && line->resultColor == color;
		if (shown)
		{
			SetText(id, line->text);
		}
		SetChildVisible(id, open && shown);
	}

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
	int32_t width = kInputWidth + extraWidth;
	if (Text::EstimateWidth(shown + L"|") > width)
	{
		while (!shown.empty() && Text::EstimateWidth(L"..." + shown + L"|") > width)
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
		SetText(kTitleID, Text::ShortenToWidth(serverName, kTitleWidth + extraWidth, kTitleFontScale));
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
	SetTip(kChipGlobeID, tip);
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
	GetWindowOrigin(pWin, viewLeft, viewTop);
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

	const char* const names[] = { "home", "below a window of the game's that covers its home", "to the bottom right corner, since a window of the game's covers its home" };
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

// The game's key filter, for Enter
bool ChatPanel::SetKeyFilter(bool add)
{
	if (keyFilterAdded == add)
	{
		return true;
	}

	cIGZWinMgrPtr pWinMgr;
	if (!pWinMgr)
	{
		return false;
	}

	bool done = false;
	if (add)
	{
		done = pWinMgr->GZRegisterKeyFilter(this);
	}
	else
	{
		done = pWinMgr->GZUnregisterKeyFilter(this);
	}
	if (!done)
	{
		return false;
	}

	keyFilterAdded = add;
	return true;
}

// A Win32 filter on the game's canvas, which sees the game window's messages
bool ChatPanel::SetCanvasFilter(bool add)
{
	if (canvasFilterAdded == add)
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

	canvasFilterAdded = add;
	return true;
}
