# SC4MP DLL

TODO
## Chat panel

When SimCity 4 is started by the SC4MP launcher, the DLL shows the server's chat in the city and region views. Press Enter (or click **Chat**) to type a message, Enter to send it and Esc to cancel. A message starting with `/` runs a launcher command, eg. `/status`. **Hide** collapses the panel to its title bar, which then counts new messages.

The panel is a normal SC4 window, made by the game from the UI script in `ui/ChatPanel.ui`. `sc4mp-dll-ui.dat` must be in the Plugins folder next to `sc4mp.dll`; the build makes it with:

```
python tools/make-ui-dat.py sc4mp-dll-ui.dat
```

UI scripts are text, one `<LEGACY>` tag per window. The easiest way to design one is to copy a window from the game's own UI (eg. with the UIEditor or the Reader) and change it; the window ids in the script must match `ChatPanel.cpp`.
