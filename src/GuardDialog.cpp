#include "GuardDialog.h"
#include "ChatPanel.h"
#include "GZWinInterfaces.h"
#include "Log.h"
#include "Text.h"

#include "cGZMessage.h"
#include "cGZPersistResourceKey.h"
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

#include <cctype>
#include <iterator>
#include <vector>

// A random ID, which must not be used by any other plugin
static constexpr uint32_t kGuardDialogServiceID = 0x7B3E9AC0;

// The UI script in ui/GuardDialog.ui (packed into sc4mp-dll-ui.dat by tools/make-ui-dat.py) and its window IDs
static constexpr uint32_t kDialogGroup = 0xC0E6A1C5;
static constexpr uint32_t kDialogInstance = 0x5D2C7E43;
static constexpr uint32_t kAlertInstance = 0x5D2C7E44;	// ui/AlertDialog.ui: the same dialog with one OK button, for the launcher's alerts
static constexpr uint32_t kTitleID = 0x7B3E9AC3;
static constexpr size_t kMaxAlerts = 3;	// a few failures in a row are shown one after another, not piled up
static constexpr uint32_t kDialogID = 0x7B3E9AC1;
static constexpr uint32_t kSignID = 0x7B3E9ACA;
static constexpr uint32_t kBackButtonID = 0x7B3E9AC7;
static constexpr uint32_t kStayButtonID = 0x7B3E9AC9;

// "<city> belongs to <owner>." wraps onto as many of these lines as it needs, and the windows below it move down
static constexpr uint32_t kOwnerLineIDs[] = { 0x7B3E9AC4, 0x7B3E9AD0, 0x7B3E9AD1, 0x7B3E9AD2, 0x7B3E9AD3, 0x7B3E9AD4 };
static constexpr uint32_t kBelowOwnerIDs[] = { 0x7B3E9AC5, 0x7B3E9AC6, kBackButtonID, kStayButtonID };
static constexpr int32_t kLineWidth = 284;	// the owner lines' text windows (in GenBodyMedium), less their gutters
static constexpr int32_t kLineHeight = 18;
static constexpr ULONGLONG kPromptWait = 2000;	// a dialog of the game's this soon after the city opens (eg. to reconcile the edges) is answered first
static constexpr int32_t kTitleBandHeight = 34;	// the frame's title band, by which the warning is dragged

static constexpr uint32_t kGZWinMessageCommand = 3;
static constexpr uint32_t kGZWinCommandButtonClicked = 0x287259F6;

// Splits text into lines that fit `width` pixels, breaking at spaces, and inside a word only if it's longer than a line
static std::vector<std::wstring> WrapToWidth(const std::wstring& text, int32_t width)
{
	std::vector<std::wstring> lines;
	std::wstring line;
	size_t start = 0;

	while (start < text.size())
	{
		size_t end = text.find(L' ', start);
		if (end == std::wstring::npos)
		{
			end = text.size();
		}
		std::wstring word = text.substr(start, end - start);
		start = end + 1;

		std::wstring joined = word;
		if (!line.empty())
		{
			joined = line + L" " + word;
		}
		if (Text::EstimateWidth(joined) <= width)
		{
			line = joined;
			continue;
		}

		if (!line.empty())
		{
			lines.push_back(line);
		}
		while (word.size() > 1 && Text::EstimateWidth(word) > width)
		{
			size_t fit = 1;
			while (fit < word.size() && Text::EstimateWidth(word.substr(0, fit + 1)) <= width)
			{
				fit++;
			}
			lines.push_back(word.substr(0, fit));
			word = word.substr(fit);
		}
		line = word;
	}

	if (!line.empty())
	{
		lines.push_back(line);
	}
	return lines;
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

void GuardDialog::PostAlert(const std::string& title, const std::string& text)
{
	std::lock_guard<std::mutex> lock(inboxMutex);

	for (const Warning& queued : alerts)
	{
		if (queued.text == text)
		{
			return;	// already waiting
		}
	}
	if (alerts.size() >= kMaxAlerts)
	{
		alerts.pop_front();
	}

	Warning alert;
	alert.alert = true;
	alert.title = title;
	alert.text = text;
	alerts.push_back(alert);
}

void GuardDialog::PostClaim(bool reset, const std::string& owner, const std::string& city, const std::string& path)
{
	std::lock_guard<std::mutex> lock(inboxMutex);

	if (reset)
	{
		claims.clear();
		return;
	}

	Warning claim;
	claim.owner = owner;
	claim.city = city;
	claim.path = path;
	claims.push_back(claim);
}

void GuardDialog::PostTakeover(const std::string& path, const std::string& mayor)
{
	std::lock_guard<std::mutex> lock(inboxMutex);

	takeoverPath = path;
	takeoverMayor = mayor;
}

void GuardDialog::CityOpened(const std::string& path)
{
	warnedPath.clear();
	renamedPath.clear();
	cityOpenedTime = GetTickCount64();
	openedTime = cityOpenedTime;

	std::optional<Warning> claim;
	{
		std::lock_guard<std::mutex> lock(inboxMutex);
		for (const Warning& candidate : claims)
		{
			if (IsSameSavegame(candidate.path, path))
			{
				claim = candidate;
				break;
			}
		}
	}

	if (claim)
	{
		claim->path = path;	// the game's spelling, which IsOpenCity compares with
		Log::Line("The opened city is in the launcher's list of other mayors' cities, so it's warned about right away");
		Show(*claim);
	}
}

void GuardDialog::Detach()
{
	// An alert the player hasn't closed (eg. when leaving the city) comes back in the next view
	if (shownAlert && !closeRequested)
	{
		std::lock_guard<std::mutex> lock(inboxMutex);
		alerts.push_front(*shownAlert);
	}
	shownAlert.reset();
	closeRequested = false;
	waitingForGame = false;
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
	std::string newTakeoverPath;
	std::string newTakeoverMayor;
	{
		std::lock_guard<std::mutex> lock(inboxMutex);
		warning.swap(pendingWarning);
		newTakeoverPath.swap(takeoverPath);
		newTakeoverMayor.swap(takeoverMayor);
	}

	if (!newTakeoverPath.empty())
	{
		TakeOver(newTakeoverPath, newTakeoverMayor);
	}

	// Logs when the game's first dialog after the city opened (eg. to reconcile the edges) comes, to check the order
	if (cityOpenedTime)
	{
		cIGZWinMgrPtr pWinMgr;
		ULONGLONG elapsed = GetTickCount64() - cityOpenedTime;
		if (pWinMgr && pWinMgr->IsModal())
		{
			Log::Line("The game showed a dialog " + std::to_string(elapsed) + " ms after the city opened");
			cityOpenedTime = 0;
		}
		else if (elapsed > 30000)
		{
			cityOpenedTime = 0;
		}
	}

	// The game's own prompt just after the city opens comes first, since it's modal and in front: the warning waits for
	// it, hidden, and comes after it
	if (pWin && !waitingForGame && openedTime && GetTickCount64() - openedTime < kPromptWait && IsGameModal())
	{
		waitingForGame = true;
		pWin->HideWindow();
	}
	// Hidden with the game's UI (eg. for a screenshot), like the chat panel
	if (pWin && GetTickCount64() - lastUICheck >= 250)
	{
		lastUICheck = GetTickCount64();
		bool hidden = ChatPanel::IsGameUIHidden();
		if (hidden != uiHidden)
		{
			uiHidden = hidden;
			if (hidden)
			{
				StopDragging();
				pWin->HideWindow();
			}
			else if (!waitingForGame)
			{
				pWin->PullToFront();
				pWin->ShowWindow();
			}
		}
	}

	if (pWin && waitingForGame && !IsGameModal() && !uiHidden)
	{
		waitingForGame = false;
		pWin->PullToFront();
		pWin->ShowWindow();
		Log::Line("Showing the claimed city warning after the game's prompt");
	}

	// The launcher's alerts, one at a time, once nothing else is shown
	if (!pWin)
	{
		std::optional<Warning> alert;
		{
			std::lock_guard<std::mutex> lock(inboxMutex);
			if (!alerts.empty())
			{
				alert = alerts.front();
				alerts.pop_front();
			}
		}
		if (alert)
		{
			Show(*alert);
		}
	}

	if (warning)
	{
		// The player may have left the city (or opened another) before the launcher answered
		if (IsSameSavegame(warning->path, warnedPath))
		{
			Log::Line("Ignored the launcher's claimed city warning, since the city was already warned about when it opened");
		}
		else if (IsOpenCity(warning->path))
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
	Detach();	// a warning shown over an alert puts the alert back, to come after it
	if (warning.alert)
	{
		shownAlert = warning;
	}
	else
	{
		warnedPath = warning.path;
	}

	cIGZWin* pNewParent = nullptr;
	if (getParent)
	{
		pNewParent = getParent();
	}
	if (!pNewParent)
	{
		return;
	}

	uint32_t instance = kDialogInstance;
	if (warning.alert)
	{
		instance = kAlertInstance;
	}
	cGZPersistResourceKey key(0x00000000, kDialogGroup, instance);
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

	// Button clicks and the mouse on the warning go to its window proc, which is this
	cRZAutoRefCount<cIGZWinGen> pWinGen;
	if (pWin->QueryInterface(kGZIID_cIGZWinGen, pWinGen.AsPPVoid()))
	{
		pWinGen->SetWinProc(this);
	}

	if (warning.alert)
	{
		SetText(kTitleID, warning.title);
		ShowOwner(warning.text);
	}
	else
	{
		ShowOwner(warning.city + " belongs to " + warning.owner + ".");
	}

	// Centered in the view. The pinned gzcom-dll's GZWinMoveTo moves by an offset (see GZWinInterfaces.h).
	int32_t dx = (pParent->GetW() - pWin->GetW()) / 2 - pWin->GetL();
	int32_t dy = (pParent->GetH() - pWin->GetH()) / 2 - pWin->GetT();
	pWin->GZWinMoveTo(dx, dy);

	// It waits, hidden, while one of the game's dialogs is up (see OnTick), or while the game's UI is hidden
	uiHidden = ChatPanel::IsGameUIHidden();
	lastUICheck = GetTickCount64();
	if (uiHidden)
	{
		pWin->HideWindow();
	}
	else if (IsGameModal())
	{
		waitingForGame = true;
		pWin->HideWindow();
	}
	else
	{
		pWin->PullToFront();
		pWin->ShowWindow();
	}

	if (warning.alert)
	{
		Log::Line("Showing the launcher's alert: " + warning.title);
	}
	else
	{
		Log::Line("Showing the claimed city warning: " + warning.city + " belongs to " + warning.owner);
	}
}

// The game's button clicks, and the mouse on the warning, which doesn't reach the map under it, like the chat panel's
bool GuardDialog::DoWinProcMessage(cIGZWin* pSender, cGZMessage& msg)
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
		StartDragging(x, y);
		return true;
	case kGZWinMessageMouseUpL:
		StopDragging();
		return true;
	case kGZWinMessageMouseDownR:
	case kGZWinMessageMouseDownM:
	case kGZWinMessageMouseUpR:
	case kGZWinMessageMouseUpM:
	case kGZWinMessageMouseWheel:
		return true;
	case kGZWinMessageMouseMove:
		if (!dragging)
		{
			return false;	// left to the game, which shows the buttons' hover art
		}
		Drag(x, y);
		return true;
	}

	return false;
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
	StopDragging();
	if (pWin)
	{
		pWin->HideWindow();
	}

	closeRequested = true;
}

// The warning is dragged by its title band, at a point from its top left corner. Its buttons take their presses themselves.
void GuardDialog::StartDragging(int32_t x, int32_t y)
{
	if (y >= kTitleBandHeight)
	{
		return;
	}

	dragging = true;
	dragX = x;
	dragY = y;

	// So the moves keep coming when a fast drag leaves the warning, and the release ends it
	cIGZWinMgrPtr pWinMgr;
	if (pWinMgr)
	{
		bool captured = pWinMgr->GZSetCapture(pWin);
		Log::Line("DIAG warning capture " + std::to_string(captured) + " at " + std::to_string(x) + "," + std::to_string(y));
	}
}

// Moves the warning to follow the mouse, keeping it inside the view
void GuardDialog::Drag(int32_t x, int32_t y)
{
	cIGZWin* pView = nullptr;
	if (pWin)
	{
		pView = pWin->GetParentWin();
	}
	if (!pView || GetKeyState(VK_LBUTTON) >= 0)
	{
		StopDragging();	// the release was missed, eg. the game lost the focus
		return;
	}

	Log::Line("DIAG warning drag " + std::to_string(x) + "," + std::to_string(y));
	// The point is from the warning's top left corner, which moves with it
	int32_t newLeft = pWin->GetL() + x - dragX;
	int32_t newTop = pWin->GetT() + y - dragY;
	if (newLeft > pView->GetW() - pWin->GetW())
	{
		newLeft = pView->GetW() - pWin->GetW();
	}
	if (newTop > pView->GetH() - pWin->GetH())
	{
		newTop = pView->GetH() - pWin->GetH();
	}
	if (newLeft < 0)
	{
		newLeft = 0;
	}
	if (newTop < 0)
	{
		newTop = 0;
	}

	// The pinned gzcom-dll's GZWinMoveTo moves a window by an offset (see GZWinInterfaces.h)
	pWin->GZWinMoveTo(newLeft - pWin->GetL(), newTop - pWin->GetT());
	pView->InvalidateSelf();
}

void GuardDialog::StopDragging()
{
	Log::Line("DIAG warning stop dragging");
	cIGZWinMgrPtr pWinMgr;
	if (dragging && pWin && pWinMgr && pWinMgr->GZGetCapture() == pWin)
	{
		pWinMgr->GZReleaseCapture(pWin);
	}

	dragging = false;
}

// Shows the text (the warning's "<city> belongs to <owner>.", or an alert's) in full, on as many lines as it needs, and
// grows the dialog to fit. Names are never shortened: the game allows long city names (up to 128 characters), and the
// line is the player's only way to tell which city it is.
void GuardDialog::ShowOwner(const std::string& text)
{
	// Each of its own lines (an alert can have a few) is wrapped to fit
	std::vector<std::wstring> lines;
	std::wstring wide = Text::Utf8ToWide(text);
	size_t start = 0;
	while (start <= wide.size())
	{
		size_t end = wide.find(L'\n', start);
		if (end == std::wstring::npos)
		{
			end = wide.size();
		}
		for (const std::wstring& line : WrapToWidth(wide.substr(start, end - start), kLineWidth))
		{
			lines.push_back(line);
		}
		start = end + 1;
	}
	size_t count = std::size(kOwnerLineIDs);

	// Six lines take any city name the game allows, but just in case, the rest goes on the last line
	while (lines.size() > count)
	{
		lines[count - 1] += L" " + lines[count];
		lines.erase(lines.begin() + count);
	}

	for (size_t i = 0; i < count; i++)
	{
		cIGZWin* pLine = pWin->GetChildWindowFromID(kOwnerLineIDs[i]);
		if (!pLine)
		{
			continue;
		}

		if (i < lines.size())
		{
			SetText(kOwnerLineIDs[i], Text::WideToUtf8(lines[i]));
			pLine->ShowWindow();
		}
		else
		{
			pLine->HideWindow();
		}
	}

	if (lines.size() <= 1)
	{
		return;
	}

	// The pinned gzcom-dll's GZWinMoveTo moves a window by an offset (see GZWinInterfaces.h). The frame is an edge
	// image, so its inset grows with the dialog.
	int32_t grow = static_cast<int32_t>(lines.size() - 1) * kLineHeight;
	for (uint32_t id : kBelowOwnerIDs)
	{
		cIGZWin* pBelow = pWin->GetChildWindowFromID(id);
		if (pBelow)
		{
			pBelow->GZWinMoveTo(0, grow);
		}
	}

	// The sign stays in the middle of the message
	cIGZWin* pSign = pWin->GetChildWindowFromID(kSignID);
	if (pSign)
	{
		pSign->GZWinMoveTo(0, grow / 2);
	}

	pWin->SetH(pWin->GetH() + grow);
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

// Whether the city saved at `path` is the one open in game
// Whether two paths are the same savegame. Only the region's folder and the file are compared, without case, since the
// launcher and the game may spell the rest differently (eg. through Wine, or with a different user folder).
bool GuardDialog::IsSameSavegame(const std::string& a, const std::string& b)
{
	auto tail = [](const std::string& path)
	{
		std::string lower = path;
		for (char& c : lower)
		{
			if (c == '/')
			{
				c = '\\';
			}
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		}

		size_t file = lower.find_last_of('\\');
		if (file == std::string::npos || file == 0)
		{
			return lower;
		}
		size_t folder = lower.find_last_of('\\', file - 1);
		if (folder == std::string::npos)
		{
			return lower;
		}
		return lower.substr(folder + 1);
	};

	if (a.empty() || b.empty())
	{
		return false;
	}
	return tail(a) == tail(b);
}

// Whether a dialog of the game's is up, which has the mouse and keys until it's answered
bool GuardDialog::IsGameModal()
{
	cIGZWinMgrPtr pWinMgr;
	return pWinMgr && pWinMgr->IsModal();
}

// The region view, the city's info and the server (its `mayor_name`) read the mayor name from the savegame, so the
// player's name shows there after their first save
void GuardDialog::TakeOver(const std::string& path, const std::string& mayor)
{
	if (mayor.empty())
	{
		return;
	}
	if (IsSameSavegame(path, renamedPath))
	{
		Log::Line("Ignored the launcher's takeover, since the mayor name was already set since the city opened");
		return;
	}
	if (!IsOpenCity(path))
	{
		Log::Line("Ignored a takeover for a city that isn't open anymore: " + path);
		return;
	}

	cISC4AppPtr pApp;
	cISC4City* pCity = pApp->GetCity();

	// Slot 17, as in upstream gzcom-dll, which declares it void, so its result isn't used. The game keeps text as UTF-8.
	pCity->SetMayorName(cRZBaseString(mayor.c_str()));
	renamedPath = path;

	Log::Line("Set the mayor name to " + mayor + " for this takeover");
}

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
