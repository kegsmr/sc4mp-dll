#include "GuardDialog.h"
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
#include "cISC4App.h"
#include "cISC4City.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cRZCOMDllDirector.h"
#include "GZServPtrs.h"
#include "SC4UI.h"

#include <windowsx.h>

// A random ID, which must not be used by any other plugin
static constexpr uint32_t kGuardDialogServiceID = 0x7B3E9AC0;

// The UI script in ui/GuardDialog.ui (packed into sc4mp-dll-ui.dat by tools/make-ui-dat.py) and its window IDs
static constexpr uint32_t kDialogGroup = 0xC0E6A1C5;
static constexpr uint32_t kDialogInstance = 0x5D2C7E43;
static constexpr uint32_t kDialogID = 0x7B3E9AC1;
static constexpr uint32_t kOwnerLineID = 0x7B3E9AC4;
static constexpr uint32_t kBackButtonID = 0x7B3E9AC7;
static constexpr uint32_t kStayButtonID = 0x7B3E9AC9;

static constexpr uint32_t kButtonIDs[] = { kBackButtonID, kStayButtonID };

static constexpr int32_t kLineWidth = 284;	// the owner line's text window (in GenBodyMedium), less its gutters

static constexpr uint32_t kGZWinMessageCommand = 3;
static constexpr uint32_t kGZWinCommandButtonClicked = 0x287259F6;
static constexpr int kMaxWinDepth = 32;	// in case a window's parent chain loops

// Returns "<city> belongs to <owner>.", shortening the names only if the line doesn't fit. The city gets what the owner's
// name leaves, but at least half the line, so a very long owner name is shortened too.
static std::string OwnerLine(const std::string& city, const std::string& owner)
{
	int32_t cityWidth = kLineWidth - Text::EstimateWidth(Text::Utf8ToWide(" belongs to " + owner + "."));
	if (cityWidth < kLineWidth / 2)
	{
		cityWidth = kLineWidth / 2;
	}
	std::string shownCity = Text::ShortenToWidth(city, cityWidth, 8);

	int32_t ownerWidth = kLineWidth - Text::EstimateWidth(Text::Utf8ToWide(shownCity + " belongs to ."));
	return shownCity + " belongs to " + Text::ShortenToWidth(owner, ownerWidth, 8) + ".";
}

GuardDialog::GuardDialog()
	: cRZBaseSystemService(kGuardDialogServiceID, 0)
{
}

void GuardDialog::Start(ParentFunction parent)
{
	if (started)
	{
		return;
	}

	getParent = std::move(parent);

	cIGZFrameWork* pFramework = RZGetFrameWork();
	if (!pFramework)
	{
		Log::Line("Unable to get the framework, so the claimed city warning is off");
		return;
	}

	// The framework only ticks running services
	SetServiceRunning(true);
	pFramework->AddSystemService(this);
	pFramework->AddToTick(this);

	started = true;
}

void GuardDialog::Stop()
{
	if (!started)
	{
		return;
	}

	Detach();
	SetMouseFilter(false);

	cIGZFrameWork* pFramework = RZGetFrameWork();
	if (pFramework)
	{
		pFramework->RemoveFromTick(this);
		pFramework->RemoveSystemService(this);
	}

	SetServiceRunning(false);
	started = false;
}

void GuardDialog::Post(const std::string& owner, const std::string& city, const std::string& path)
{
	std::lock_guard<std::mutex> lock(inboxMutex);

	Warning warning;
	warning.owner = owner;
	warning.city = city;
	warning.path = path;
	pendingWarning = warning;
}

void GuardDialog::Detach()
{
	closeRequested = false;

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
			pParent->InvalidateSelf();
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

bool GuardDialog::QueryInterface(uint32_t riid, void** ppvObj)
{
	if (riid == GZIID_cIGZWinProc)
	{
		*ppvObj = static_cast<cIGZWinProc*>(this);
		AddRef();
		return true;
	}

	return cRZBaseSystemService::QueryInterface(riid, ppvObj);
}

uint32_t GuardDialog::AddRef()
{
	return cRZBaseSystemService::AddRef();
}

uint32_t GuardDialog::Release()
{
	return cRZBaseSystemService::Release();
}

bool GuardDialog::Init()
{
	return true;
}

bool GuardDialog::Shutdown()
{
	return true;
}

bool GuardDialog::OnTick(uint32_t unknown1)
{
	// Removed here rather than in the click, since the click is still being handled by the window's own button
	if (closeRequested)
	{
		Detach();
	}

	// Likewise, so the city isn't torn down while the game is still handling the click
	if (exitRequested)
	{
		exitRequested = false;

		cISC4AppPtr pApp;
		if (pApp && pApp->GetCity())
		{
			Log::Line("Going back to the region without saving");

			// False: no "save before exiting?" prompt. This doesn't save, like sc4-auto-update-city-thumbnails,
			// which saves first with RequestSaveCity when it wants to.
			pApp->RequestGoToRegionView(false);
		}
	}

	std::optional<Warning> warning;
	{
		std::lock_guard<std::mutex> lock(inboxMutex);
		warning.swap(pendingWarning);
	}

	if (warning)
	{
		// The player may have left the city (or opened another) before the launcher answered
		if (IsOpenCity(warning->path))
		{
			Show(*warning);
		}
		else
		{
			Log::Line("Ignored a claimed city warning for a city that isn't open anymore: " + warning->city);
		}
	}

	return true;
}

void GuardDialog::Show(const Warning& warning)
{
	Detach();

	cIGZWin* pNewParent = nullptr;
	if (getParent)
	{
		pNewParent = getParent();
	}
	if (!pNewParent)
	{
		return;
	}

	cGZPersistResourceKey key(0x00000000, kDialogGroup, kDialogInstance);
	cIGZWin* pNewWin = nullptr;

	if (!SC4UI::CreateWindowFromScript(key, pNewParent, kDialogID, GZIID_cIGZWin, reinterpret_cast<void**>(&pNewWin)) || !pNewWin)
	{
		if (pNewWin)
		{
			pNewWin->Release();
		}

		Log::Line("Unable to create the claimed city warning. Is sc4mp-dll-ui.dat in the Plugins folder?");
		return;
	}

	pParent = pNewParent;
	pParent->AddRef();

	pWin = pNewWin;
	pParent->ChildAdd(pWin);

	// Button clicks go to the window proc, which is this
	cRZAutoRefCount<cIGZWinGen> pWinGen;
	if (pWin->QueryInterface(kGZIID_cIGZWinGen, pWinGen.AsPPVoid()))
	{
		pWinGen->SetWinProc(this);
	}

	SetText(kOwnerLineID, OwnerLine(warning.city, warning.owner));

	// Centered in the view. The pinned gzcom-dll's GZWinMoveTo moves by an offset (see GZWinInterfaces.h).
	int32_t dx = (pParent->GetW() - pWin->GetW()) / 2 - pWin->GetL();
	int32_t dy = (pParent->GetH() - pWin->GetH()) / 2 - pWin->GetT();
	pWin->GZWinMoveTo(dx, dy);

	pWin->PullToFront();
	pWin->ShowWindow();

	// Keeps the clicks on the warning from the map under it, like the chat panel. It stays until Stop(), since removing
	// it while the game is calling it (eg. from a click) isn't safe.
	if (!SetMouseFilter(true))
	{
		Log::Line("Unable to listen for the mouse, so clicks on the claimed city warning may also reach the map");
	}

	Log::Line("Showing the claimed city warning: " + warning.city + " belongs to " + warning.owner);
}

bool GuardDialog::DoWinProcMessage(cIGZWin* pSender, cGZMessage& msg)
{
	if (msg.dwMessageType != kGZWinMessageCommand || msg.dwData1 != kGZWinCommandButtonClicked)
	{
		return false;
	}

	return Click(msg.dwData2);	// the button's ID
}

bool GuardDialog::DoWinMsg(cIGZWin* pSender, uint32_t dwMessageID, uint32_t data1, uint32_t data2, uint32_t data3)
{
	return false;
}

bool GuardDialog::Click(uint32_t buttonID)
{
	switch (buttonID)
	{
	case kBackButtonID:
		Log::Line("The player chose to go back to the region");
		Close();
		exitRequested = true;
		return true;
	case kStayButtonID:
		Log::Line("The player chose to look around");
		Close();
		return true;
	}

	return false;
}

// Hides the warning right away, and removes it on the next frame
void GuardDialog::Close()
{
	if (pWin)
	{
		pWin->HideWindow();
	}

	closeRequested = true;
}

LRESULT GuardDialog::FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled)
{
	handled = false;

	// Also after the warning closes, so the release of a click on it doesn't reach the map
	if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)
	{
		handled = HandleMouse(hWnd, uMsg, lParam);
	}

	return 0;
}

// The views pick the lot under the cursor from the game window's own mouse messages (see ChatPanel::HandleMouse),
// so the mouse buttons on the warning are taken here, and the warning does the clicked button itself.
bool GuardDialog::HandleMouse(HWND hWnd, UINT uMsg, LPARAM lParam)
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
		// Its position is on the screen, unlike the buttons'
		return ScreenToClient(hWnd, &point) && IsOnDialog(point.x, point.y);
	default:
		return false;	// moving is left to the game, which shows the buttons' hover art
	}

	if (!down)
	{
		// The release goes where the press went
		bool pressedOnDialog = (buttonsDown & button) != 0;
		buttonsDown &= ~button;
		return pressedOnDialog;
	}

	if (!IsOnDialog(point.x, point.y))
	{
		return false;
	}

	buttonsDown |= button;

	if (button == MK_LBUTTON)
	{
		Click(GetButtonAt(point.x, point.y));
	}

	return true;
}

// Whether a point in the game window is on the warning, and no dialog of the game's has the mouse
bool GuardDialog::IsOnDialog(int32_t x, int32_t y) const
{
	if (!pWin || !pWin->IsVisible())
	{
		return false;
	}

	cIGZWinMgrPtr pWinMgr;
	if (pWinMgr && pWinMgr->IsModal())
	{
		return false;
	}

	int32_t left = 0;
	int32_t top = 0;
	GetOrigin(left, top);
	return x >= left && y >= top && x < left + pWin->GetW() && y < top + pWin->GetH();
}

// Where the warning is in the game window, adding up the places of its parents
void GuardDialog::GetOrigin(int32_t& left, int32_t& top) const
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

// Returns the ID of the button at a point in the game window, or 0 if there's none
uint32_t GuardDialog::GetButtonAt(int32_t x, int32_t y) const
{
	int32_t left = 0;
	int32_t top = 0;
	GetOrigin(left, top);
	x -= left;
	y -= top;

	for (uint32_t id : kButtonIDs)
	{
		cIGZWin* pButton = pWin->GetChildWindowFromID(id);
		if (pButton && x >= pButton->GetL() && y >= pButton->GetT() && x < pButton->GetL() + pButton->GetW() && y < pButton->GetT() + pButton->GetH())
		{
			return id;
		}
	}

	return 0;
}

void GuardDialog::SetText(uint32_t id, const std::string& text)
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

bool GuardDialog::SetMouseFilter(bool add)
{
	if (mouseFilterAdded == add)
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

	mouseFilterAdded = add;
	return true;
}

// Whether the city saved at `path` is the one open in game
bool GuardDialog::IsOpenCity(const std::string& path)
{
	cISC4AppPtr pApp;
	if (!pApp || path.empty())
	{
		return false;
	}

	cISC4City* pCity = pApp->GetCity();
	if (!pCity)
	{
		return false;
	}

	cRZBaseString openPath;
	pCity->GetCitySaveFilePath(openPath);
	return Text::ToUtf8(openPath.ToChar()) == path;
}
