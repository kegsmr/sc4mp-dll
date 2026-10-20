#pragma once
#include "cIGZWinProc.h"
#include "cIGZWinProcFilterW32.h"
#include "cRZBaseSystemService.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <Windows.h>

class cIGZWin;

// A warning shown when the player opens a city that belongs to another mayor, so they know before playing that their
// saves won't be kept. The launcher decides (it knows who owns each tile) and sends a `guard` message after `city_loaded`.
// "Back to region" exits to the region view without saving, and "Look around" closes the warning.
//
// The window is made by the game from the UI script in ui/GuardDialog.ui, like the chat panel.
// Only Post() may be called from another thread: the warning is shown on the next frame (OnTick), on the game thread.
class GuardDialog final : public cRZBaseSystemService, private cIGZWinProc, private cIGZWinProcFilterW32
{
public:
	using ParentFunction = std::function<cIGZWin*()>;

	GuardDialog();

	// Starts updating every frame. `getParent` returns the city view's window, which the warning is shown in.
	void Start(ParentFunction getParent);
	void Stop();

	// Shows the warning for the city saved at `path`, if it's still the open city by then. Safe to call from any thread.
	void Post(const std::string& owner, const std::string& city, const std::string& path);

	// Removes the warning, eg. when the city closes
	void Detach();

	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	uint32_t AddRef() override;
	uint32_t Release() override;

	bool Init() override;
	bool Shutdown() override;
	bool OnTick(uint32_t unknown1) override;

private:
	struct Warning
	{
		std::string owner;
		std::string city;
		std::string path;
	};

	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	bool DoWinMsg(cIGZWin* pWin, uint32_t dwMessageID, uint32_t data1, uint32_t data2, uint32_t data3) override;
	LRESULT FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled) override;

	void Show(const Warning& warning);
	void Close();
	bool Click(uint32_t buttonID);
	bool HandleMouse(HWND hWnd, UINT uMsg, LPARAM lParam);
	bool IsOnDialog(int32_t x, int32_t y) const;
	void GetOrigin(int32_t& left, int32_t& top) const;
	uint32_t GetButtonAt(int32_t x, int32_t y) const;
	void StartDragging(HWND hWnd, int32_t x, int32_t y);
	void Drag(int32_t x, int32_t y);
	void StopDragging();
	void ShowOwner(const std::string& text);
	void SetText(uint32_t id, const std::string& text);
	bool SetMouseFilter(bool add);

	static bool IsOpenCity(const std::string& path);

	ParentFunction getParent;
	bool started = false;
	bool mouseFilterAdded = false;
	bool closeRequested = false;	// a button was clicked; the warning is removed on the next frame, outside the click
	bool exitRequested = false;	// "Back to region" was clicked; the game is asked on the next frame, outside the click

	cIGZWin* pParent = nullptr;
	cIGZWin* pWin = nullptr;	// the warning, while it's shown
	bool dragging = false;
	int32_t dragX = 0;	// where the warning was grabbed, from its top left corner
	int32_t dragY = 0;
	HWND dragCapture = nullptr;
	uint32_t buttonsDown = 0;	// the mouse buttons pressed on the warning, whose release is kept from the game too

	std::mutex inboxMutex;
	std::optional<Warning> pendingWarning;
};
