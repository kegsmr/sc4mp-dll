# SC4MP DLL
A plugin for the PC videogame [SimCity 4](https://en.wikipedia.org/wiki/SimCity_4) that brings multiplayer chat and save status into the game. Uses C++ and the game's GZCOM interfaces through [gzcom-dll](https://github.com/nsgomez/gzcom-dll). Built to work with the [SC4MP Client](https://github.com/sc4multiplayer/sc4mp-client) and [SC4MP Server](https://github.com/sc4multiplayer/sc4mp-server).

# Installation
The DLL comes with the SC4MP Launcher, which copies `sc4mp.dll` and `sc4mp-dll-ui.dat` into the game's `Plugins` folder when it starts SimCity 4. To install a build by hand, put both files in the top of the `Plugins` folder (DLL plugins don't load from subfolders). It works on Windows 7 SP1 and newer.

# Usage
Join a server with the SC4MP Launcher. In game:
- A chat panel shows the server's chat in the city and region views. Press Enter (or click the text field) to type, Enter to send, and Esc to go back to the game. A message starting with `/` runs a launcher command, eg. `/status`. The `say <message>` and `sc4mp <command>` cheat codes work too.
- The title bar shows the server's name and the save status (hover the icon for it). While saving, the chat's last line shows how much of the city has been sent, and the result is shown in green or red.
- The arrow minimizes the panel to a round button with a globe on it, in the top right corner in the city view and the bottom right in the region view. Its ring shows the save status and its red bubble counts new messages. It moves out of the way of the game's own windows.
- The panel can be dragged by its title bar and resized by its bottom corners. The mouse wheel, the scrollbar and Page Up/Down scroll back through the last 200 lines.
- Opening a city that belongs to another mayor shows a warning, since your saves there won't be kept, with "Back to region" and "Look around". The launcher's alerts (eg. a failed save) are shown in the same kind of dialog.
- When you take over a city, its mayor name is set to yours, and saved with your next save.
- The panel and dialogs hide with the game's UI.

What happens is logged to `SC4MP.log` next to the DLL.

# Compiling
Clone with `git clone --recurse-submodules` (or run `git submodule update --init`) to get [gzcom-dll](https://github.com/nsgomez/gzcom-dll) in `vendor/gzcom-dll`. Then build `src/SC4MP.sln` in Visual Studio (Release, x86). Every push is also built by GitHub Actions, which checks the DLL still loads on Windows 7 and uploads `sc4mp.dll` and `sc4mp-dll-ui.dat` as an artifact.

The panel and dialogs are SimCity 4 windows made by the game from the UI scripts in `ui`, so `sc4mp-dll-ui.dat` must go in `Plugins` with the DLL. To build it, run:

```
python tools/make-ui-dat.py sc4mp-dll-ui.dat
```

The images in `ui/images` are drawn by `tools/make-ui-images.py` (which needs Pillow). After changing the drawing, run it and commit the PNGs. UI scripts are text, one `<LEGACY>` tag per window. The easiest way to make one is to copy a window from the game's own UI (eg. with the UIEditor or the Reader) and change it. The window IDs must match the ones in `ChatPanel.cpp` or `GuardDialog.cpp`.
