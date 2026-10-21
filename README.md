# SC4MP DLL

## Chat panel

When SimCity 4 is started by the SC4MP launcher, the DLL shows the server's chat in the city and region views. Press Enter (or click the text field or the send button) to type a message (up to 500 characters), and Enter to send it. The field stays open for the next one; Enter on an empty field or Esc goes back to the game. A click outside the panel goes back to the game and keeps the message for later. A message starting with `/` runs a launcher command, eg. `/status`. The title bar shows the server's name and the save status's icon (hover it for the status): green when connected or saved, blue while saving, amber while the server can't be reached, red when a save failed, and indigo for a guest. After a failed save, the launcher keeps it red until it's reconnected to the server or a save gets through. The game's "City Saved." message box is hidden, since the launcher puts the save's result in the chat. The mouse wheel, the scrollbar and Page Up/Down (while typing) scroll back through the last 200 lines. The panel can be dragged by its title bar, and resized by pressing within 16 pixels of either bottom corner and dragging (300 to 640 pixels wide and 4 to 16 lines tall). Its size is kept while the game runs. The arrow minimizes it to a round button with a globe on it in the top right corner in the city view (the bottom right in the region view, whose top right has the game's own round buttons), whose ring shows the save status and whose red bubble counts new messages (Enter or a click opens it again); it moves out of the way of the game's own windows there. The panel starts minimized, and keeps the player's choice while the game runs.

The panel is a normal SC4 window, made by the game from the UI script in `ui/ChatPanel.ui`. `sc4mp-dll-ui.dat` must be in the Plugins folder next to `sc4mp.dll`; the build makes it with:

```
python tools/make-ui-dat.py sc4mp-dll-ui.dat
```

The panel's images are in `ui/images`, drawn by `tools/make-ui-images.py` (which needs Pillow). After changing the drawing, run it and commit the PNGs. `make-ui-dat.py` lists each image's instance ID.

UI scripts are text, one `<LEGACY>` tag per window. The easiest way to design one is to copy a window from the game's own UI (eg. with the UIEditor or the Reader) and change it; the window ids in the script must match `ChatPanel.cpp`.
