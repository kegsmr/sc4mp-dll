#pragma once
#include "cIGZWinProc.h"
#include "cIGZWinProcFilterW32.h"
#include "cRZBaseSystemService.h"

#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <Windows.h>

class cIGZWin;

// A chat window in the city and region views. It shows the last lines the launcher sent, and the
// player presses Enter (or clicks the send button) to type a message, Enter again to send it and Esc to cancel.
// The title bar shows the server's name and the launcher's save status. The panel can be minimized to a small button
// in the bottom right corner, which it starts as. The choice is kept while the game runs.
//
// The window is made by the game from the UI script in ui/ChatPanel.ui, so it looks like the game's own.
// Only Post() may be called from another thread: it queues the line, and the queue is emptied every
// frame on the game thread (OnTick), since the game's windows must only be used there.
// Keys come from the game window's own Win32 messages (FilterMessage), which also run on the game thread.
// So do the mouse buttons on the panel, so the map under it doesn't also get them.
class ChatPanel final : public cRZBaseSystemService, private cIGZWinProc, private cIGZWinProcFilterW32
{
public:
	using SendFunction = std::function<void(const std::string&)>;

	ChatPanel();

	// Starts updating every frame and listening for keys. The functions are called on the game thread.
	void Start(SendFunction sendChat, SendFunction sendCommand);
	void Stop();

	// Shows the panel in a view's window (the region screen or the city's 3D view), removing it from the previous view
	void Attach(cIGZWin* pNewParent, bool region);
	void Detach();

	// Opens the panel with the cursor in the text box, eg. for the `say` cheat code without a message
	void StartTyping();

	// Adds a line from the launcher. Safe to call from any thread. History lines (sent on connecting) don't count as unread.
	// Chat comes with the player's name, which starts the text, and the launcher's color for it (eg. "[46,125,50]"). Other lines are shown in grey.
	void Post(const std::string& text, bool history, const std::string& name = std::string(), const std::string& color = std::string(), bool own = false);

	// Closes the game's "City Saved." message box if it shows within a few seconds, since a city isn't really saved
	// until the server takes it, which the panel's status shows. Called after the game saves.
	void HideSavedDialog();

	// Whether the player hid the city view's UI, which the panel and the claimed city warning hide with
	static bool IsGameUIHidden();

	// Shows the launcher's save status (eg. "saved", with "Saved 13:01") and the server's name as the title. Safe to call from any thread.
	void PostStatus(const std::string& state, const std::string& text);
	void PostServerName(const std::string& name);

	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	uint32_t AddRef() override;
	uint32_t Release() override;

	bool Init() override;
	bool Shutdown() override;
	bool OnTick(uint32_t unknown1) override;

private:
	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	bool DoWinMsg(cIGZWin* pWin, uint32_t dwMessageID, uint32_t data1, uint32_t data2, uint32_t data3) override;
	LRESULT FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled) override;

	bool Click(uint32_t buttonID);
	bool HandleMouse(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	bool IsOnPanel(int32_t x, int32_t y);
	void GetPanelOrigin(int32_t& left, int32_t& top) const;
	uint32_t GetButtonAt(int32_t x, int32_t y) const;
	bool IsOnChild(uint32_t id, int32_t x, int32_t y) const;
	void CheckSavedDialog(bool fromMessage);
	void ResetCaret();
	void FollowGameUI();
	void CloseSavedDialog();
	void StartDragging(HWND hWnd, int32_t x, int32_t y);
	void Drag(int32_t x, int32_t y);
	void StopDragging();
	void ClampToView(int32_t& left, int32_t& top) const;
	bool HandleTypingKey(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void Type(wchar_t c);
	void Paste(HWND hWnd);
	void Send();
	void StopTyping();
	void PauseTyping();
	bool GameWantsEnter() const;

	struct Notice
	{
		std::string text;
		bool history = false;
		std::string name;
		std::string color;
		bool own = false;	// the player's own message, whose name gets " (You)"
	};

	// A line as shown, already wrapped to fit. The first line of a chat message has the player's name, shown before the text in its color.
	struct Line
	{
		std::string text;
		std::string name;
		int color = -1;		// in kNameColors
		bool chat = false;	// shown in navy, or in grey if not
	};

	void AddLine(const Notice& notice);
	void ShowLine(uint32_t row, const Line* line, bool open);
	void SetMinimized(bool value);
	void Layout();
	void PlaceWindow(cIGZWin* pTarget, int32_t left, int32_t top);
	void ShowLines();
	void ShowInput();
	void ShowTitle();
	void ShowStatus();
	void SetText(uint32_t id, const std::string& text);
	void SetTip(uint32_t id, const std::string& text);
	void ShowChipTip();
	size_t GetMaxScroll() const;
	void ScrollBy(int rows);
	void ScrollToBottom();
	void PressScrollbar(int32_t y, bool press);
	void ShowScroll(bool open);

	enum class ChipSpot
	{
		Home,	// the view's top right corner
		Below,	// just below a window of the game's that covers home
		Corner,	// the bottom right corner, if that one is too tall
	};

	int32_t GetTopMargin() const;
	void GetChipPlace(ChipSpot spot, int32_t& left, int32_t& top) const;
	cIGZWin* GetCoveringWindow(int32_t left, int32_t top) const;
	void AvoidCoveringWindows();
	void SetChildVisible(uint32_t id, bool visible);
	bool SetKeyFilter(bool add);

	SendFunction sendChat;
	SendFunction sendCommand;
	bool started = false;
	bool keyFilterAdded = false;

	cIGZWin* pParent = nullptr;
	cIGZWin* pWin = nullptr;	// the panel, while it's shown in a view
	int32_t openLeft = 0;
	int32_t openTop = 0;
	int32_t openWidth = 0;
	int32_t openHeight = 0;
	int32_t parentWidth = 0;	// the view's size when the panel was last placed in its corner
	int32_t parentHeight = 0;
	bool regionView = false;
	bool uiHidden = false;	// hidden with the game's UI
	ULONGLONG lastUICheck = 0;	// in the region view, the top right corner is below its top bar
	ChipSpot chipSpot = ChipSpot::Home;
	ChipSpot wantedChipSpot = ChipSpot::Home;
	ULONGLONG wantedSince = 0;	// when the chip first wanted to move to wantedChipSpot
	ULONGLONG lastChipCheck = 0;
	int32_t coverBottom = 0;	// the chip's top while it's below a covering window
	bool moved = false;	// the player dragged the open panel, to openLeft and openTop, which are kept while the game runs
	bool dragging = false;
	bool scrollDragging = false;	// the player holds the mouse on the scrollbar's track
	int32_t dragX = 0;	// where the panel was grabbed, from its top left corner
	int32_t dragY = 0;
	HWND dragCapture = nullptr;
	uint32_t buttonsDown = 0;	// the mouse buttons (MK_LBUTTON, etc.) pressed on the panel, whose release is kept from the game too
	bool loggedCoveredClick = false;
	ULONGLONG savedDialogDeadline = 0;	// until when the game's "City Saved." is looked for, after a save
	ULONGLONG lastSavedDialogCheck = 0;
	HWND gameWindow = nullptr;	// the game's window, from the messages it gets

	std::mutex inboxMutex;
	std::deque<Notice> inbox;	// lines from the bridge thread, waiting for the next frame
	std::optional<std::pair<std::string, std::string>> pendingStatus;
	std::optional<std::string> pendingServerName;

	std::deque<Line> lines;	// the last kMaxLines rows, oldest first
	size_t scrollRows = 0;	// how far the player has scrolled up from the newest row
	size_t newBelow = 0;	// messages that came while scrolled up
	std::wstring input;
	bool typing = false;
	bool caretShown = true;	// the blinking caret, while typing
	ULONGLONG caretTime = 0;
	bool minimized = true;	// only a small button in the bottom right corner is shown, as when the game starts
	int unread = 0;
	std::string serverName;
	std::pair<std::string, std::string> status{ "connected", "Connected" };	// until the launcher says otherwise
};
