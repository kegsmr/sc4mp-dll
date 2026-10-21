#pragma once
#include "cIGZUnknown.h"

// The few UI interfaces that the pinned gzcom-dll doesn't have. Only the first methods are declared,
// since a declared method must sit at the same vtable slot as in the game, and the rest are unused.
// The interface IDs and method order were taken from sc4-region-census, which uses them in a released plugin.

class cIGZString;
class cIGZWin;
class cIGZWinProc;

static const uint32_t kGZIID_cIGZWinGen = 0x5386D516;
static const uint32_t kGZIID_cIGZWinText = 0x212CDC1F;
static const uint32_t kGZIID_cIGZWinTextEdit = 0x231A1C57;	// only queried, to see if the player is typing in the game's own text box

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

// A text label
class cIGZWinText : public cIGZUnknown
{
public:
	virtual cIGZWin* AsIGZWin() = 0;
	virtual bool SetCaption(cIGZString const& caption) = 0;
};
