# SC4MP DLL

TODO
## Chat panel

When SimCity 4 is started by the SC4MP launcher, the DLL shows the server's chat in the city and region views. Press Enter (or click the text field or the send button) to type a message, and Enter to send it. The field stays open for the next one; Enter on an empty field or Esc goes back to the game. A click outside the panel goes back to the game and keeps the message for later. A message starting with `/` runs a launcher command, eg. `/status`. The title bar shows the server's name and the save status's icon (hover it for the status). The mouse wheel, the scrollbar and Page Up/Down (while typing) scroll back through the last 200 lines. The panel can be dragged by its title bar. The arrow minimizes it to a round button in the top right corner, whose ring shows the save status and whose red bubble counts new messages (Enter or a click opens it again); it moves out of the way of the game's own windows there. The panel starts minimized, and keeps the player's choice while the game runs.

The panel is a normal SC4 window, made by the game from the UI script in `ui/ChatPanel.ui`. `sc4mp-dll-ui.dat` must be in the Plugins folder next to `sc4mp.dll`; the build makes it with:

```
python tools/make-ui-dat.py sc4mp-dll-ui.dat
```

The panel's images are in `ui/images`, drawn by `tools/make-ui-images.py` (which needs Pillow). After changing the drawing, run it and commit the PNGs. `make-ui-dat.py` lists each image's instance ID.

UI scripts are text, one `<LEGACY>` tag per window. The easiest way to design one is to copy a window from the game's own UI (eg. with the UIEditor or the Reader) and change it; the window ids in the script must match `ChatPanel.cpp`.
