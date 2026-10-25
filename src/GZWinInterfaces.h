#pragma once
#include "cIGZUnknown.h"

// The few interfaces that the pinned gzcom-dll doesn't have. Only the first methods are declared,
// since a declared method must sit at the same vtable slot as in the game, and the rest are unused.
// The interface IDs and method order were taken from sc4-region-census, which uses them in a released plugin.
//
// In the pinned cIGZWin.h, SetSize(cRZPoint) is an overload of SetSize(int, int), which MSVC puts next to it, so
// slots 53 to 57 are each one off: its GZWinMoveTo(x, y) calls the game's slot 57, which moves a window by (x, y).
// ChatPanel::PlaceWindow relies on that. Nothing else in slots 53 to 57 is called.

class cGZMessage;
class cIGZString;
class cIGZWin;
class cIGZWinProc;

static const uint32_t kGZIID_cIGZWinGen = 0x5386D516;
static const uint32_t kGZIID_cIGZWinText = 0x212CDC1F;
static const uint32_t kGZIID_cIGZWinTextEdit = 0x231A1C57;	// only queried, to see if the player is typing in the game's own text box

// The messages a window's proc gets (cGZMessage::dwMessageType) that the DLL uses, named as in the game's
// cGZWinProcSpyMessages::CapturedMessage (from 0xC0000054, which lists the rest too,
// https://github.com/sc4multiplayer/sc4mp-dll/issues/9#issuecomment-6308209886)
static const uint32_t kGZWinMessageCommand = 0x03;	// a control's, eg. a button's click: dwData1 is kGZWinCommandButtonClicked and dwData2 the button's ID
static const uint32_t kGZWinCommandButtonClicked = 0x287259F6;

// The keys go to the window with the game's focus. dwData1 is the key's virtual key code (for Character, it's taken to be the
// character, like WM_CHAR's), dwData2 the modifier keys (in the game's own flags) and dwData3 the repeat count.
static const uint32_t kGZWinMessageCharacter = 0x04;
static const uint32_t kGZWinMessageKeyDown = 0x05;
static const uint32_t kGZWinMessageKeyUp = 0x06;

// What a key filter (cIGZWinMessageFilter) gets, as seen in game: each key message as it's sent to the window with the
// focus (and again after, as 0x1B), with that window in dwData1, the key message's type in dwData2 and the key message
// itself (a cGZMessage*) in dwData3
static const uint32_t kGZWinMessageFilterKey = 0x1A;

// dwData1 and dwData2 are the point, from the window's top left corner, and dwData3 has the modifier keys in its high word
// and the wheel's turn (in WHEEL_DELTA units, up if positive) in its low word. The game doesn't send the middle button's.
static const uint32_t kGZWinMessageMouseDownL = 0x07;
static const uint32_t kGZWinMessageMouseDownR = 0x08;
static const uint32_t kGZWinMessageMouseUpL = 0x0A;
static const uint32_t kGZWinMessageMouseUpR = 0x0B;
static const uint32_t kGZWinMessageMouseMove = 0x0D;
static const uint32_t kGZWinMessageMouseWheel = 0x0E;

static const uint32_t kGZWinMessageKillFocus = 0x10;

// The cIGZWin flag (GetFlag, SetFlag) that lets a window take the game's focus, as the current gzcom-dll's cIGZWin.h has it
static const uint32_t kGZWinFlagAcceptFocus = 0x8000;

// A window that holds other windows, eg. a dialog
class cIGZWinGen : public cIGZUnknown
{
public:
	virtual cIGZWin* AsIGZWin() = 0;
	virtual uint32_t GetCLSID() = 0;
	virtual bool GetWinGenFlag(int32_t flag) = 0;
	virtual bool SetWinGenFlag(int32_t flag, bool value) = 0;

	// The window proc gets the messages from the window's children, eg. button clicks
	virtual cIGZWinGen* SetWinProc(cIGZWinProc* proc) = 0;
};

static const uint32_t kGZIID_cIGZWinMessageFilter = 0xC6AE7085;

// What cIGZWinMgr::GZRegisterKeyFilter takes, as in the current gzcom-dll's cIGZWinMessageFilter.h. It's taken to see the
// keyboard's messages before the window with the focus does, and to keep a message from it by returning true.
class cIGZWinMessageFilter : public cIGZUnknown
{
public:
	virtual bool DoMessage(cIGZWin* pWin, cGZMessage& message) = 0;
};

// A text label
class cIGZWinText : public cIGZUnknown
{
public:
	virtual cIGZWin* AsIGZWin() = 0;
	virtual bool SetCaption(cIGZString const& caption) = 0;
};

static const uint32_t kGZIID_cIGZWinScrollbar = 0x61325A2D;

// The game's scrollbar, whose range and value the chat panel sets. Only the methods up to SetPageSize are declared, in
// the order of the current gzcom-dll's cIGZWinScrollbar.h.
class cIGZWinScrollbar : public cIGZUnknown
{
public:
	virtual cIGZWin* AsIGZWin() = 0;
	virtual void* AsIGZWinSlider() = 0;
	virtual bool SetValue(int32_t value) = 0;
	virtual int32_t GetValue() const = 0;
	virtual bool SetMinMax(int32_t min, int32_t max) = 0;
	virtual bool GetMinMax(int32_t& min, int32_t& max) const = 0;
	virtual bool SetOrientation(int32_t orientation) = 0;
	virtual int32_t GetOrientation() const = 0;
	virtual bool SetPageSize(int32_t value) = 0;
};

// The city's vehicle, aircraft and watercraft managers, which the pinned cISC4City returns as plain pointers. Only the
// methods up to IsDrivingMode are declared, in the order of the current gzcom-dll's cISC4AutomataManager.h.
class cISC4AutomataManager : public cIGZUnknown
{
public:
	virtual bool Init() = 0;
	virtual bool Shutdown() = 0;
	virtual bool Activate(bool active) = 0;
	virtual bool IsActive() const = 0;
	virtual bool AddAutomaton(void* pAutomaton) = 0;
	virtual bool RemoveAutomaton(void* pAutomaton, bool unknown) = 0;
	virtual void Pause() = 0;
	virtual void Resume() = 0;
	virtual bool IsPaused() const = 0;
	virtual uint32_t GetAutomataCount() const = 0;

	// Whether the player is driving one of its automata with U-Drive-It
	virtual bool IsDrivingMode() const = 0;
};
