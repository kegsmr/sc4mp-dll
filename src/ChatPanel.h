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
#include <Windows.h>

class cIGZWin;

// A chat window in the city and region views. It shows the last lines the launcher sent, and the
// player presses Enter (or clicks the send button) to type a message, Enter again to send it and Esc to cancel.
// The title bar shows the server's name and the launcher's save status. The panel can be collapsed to its title bar,
// or minimized to a small button in the bottom right corner, which is remembered between games.
//
// The window is made by the game from the UI script in ui/ChatPanel.ui, so it looks like the game's own.
// Only Post() may be called from another thread: it queues the line, and the queue is emptied every
// frame on the game thread (OnTick), since the game's windows must only be used there.
// Keys come from the game window's own Win32 messages (FilterMessage), which also run on the game thread.
class ChatPanel final : public cRZBaseSystemService, private cIGZWinProc, private cIGZWinProcFilterW32
{
public:
	using SendFunction = std::function<void(const std::string&)>;

	ChatPanel();

	// Starts updating every frame and listening for keys. The functions are called on the game thread.
	void Start(SendFunction sendChat, SendFunction sendCommand);
	void Stop();

	// Shows the panel in a view's window (the region screen or the city's 3D view), removing it from the previous view
	void Attach(cIGZWin* pNewParent);
	void Detach();

	// Opens the panel with the cursor in the text box, eg. for the `say` cheat code without a message
	void StartTyping();

	// Adds a line from the launcher. Safe to call from any thread. History lines (sent on connecting) don't count as unread.
	void Post(const std::string& text, bool history);

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

	bool HandleTypingKey(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void Type(wchar_t c);
	void Paste(HWND hWnd);
	void Send();
	void StopTyping();
	bool GameWantsEnter() const;

	void AddLine(const std::string& text, bool history);
	void SetCollapsed(bool value);
	void SetMinimized(bool value);
	void Layout();
	void MovePanel(int32_t left, int32_t top);
	void LoadMode();
	void SaveMode() const;
	void ShowLines();
	void ShowInput();
	void ShowTitle();
	void ShowStatus();
	void SetText(uint32_t id, const std::string& text);
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

	std::mutex inboxMutex;
	std::deque<std::pair<std::string, bool>> inbox;	// lines from the bridge thread, waiting for the next frame
	std::optional<std::pair<std::string, std::string>> pendingStatus;
	std::optional<std::string> pendingServerName;

	std::deque<std::string> lines;	// the lines shown, oldest first, already wrapped to fit
	std::wstring input;
	bool typing = false;
	bool collapsed = false;	// only the title bar is shown
	bool minimized = false;	// only a small button in the bottom right corner is shown
	int unread = 0;
	std::string serverName;
	std::pair<std::string, std::string> status{ "connected", "Connected" };	// until the launcher says otherwise
};
