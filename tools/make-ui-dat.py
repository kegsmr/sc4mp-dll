"""Packs the SC4MP UI scripts in `ui` and their images in `ui/images` into `sc4mp-dll-ui.dat`, which goes in the Plugins folder with `sc4mp.dll`.

The game reads UI scripts (type 0) from any DBPF file in Plugins, so the DLL can create windows from them by group and instance (see `ChatPanel.cpp`). The files are stored uncompressed, which SimCity 4 reads fine.
The scripts use the images as `image={46a006b0,<instance>}`, like the game's own UI images and the launcher's `sc4mp-ui.dat`. The images are drawn by `make-ui-images.py`.

Usage: python tools/make-ui-dat.py [output.dat]
"""

import struct
import sys
import time
from pathlib import Path

# File name in `ui` -> (group, instance). These must match the keys in the DLL's source.
UI_FILES = {
	"ChatPanel.ui": (0xC0E6A1C5, 0x1A9F3B21),
	"GuardDialog.ui": (0xC0E6A1C5, 0x5D2C7E43),
	"AlertDialog.ui": (0xC0E6A1C5, 0x5D2C7E44),
}

UI_TYPE = 0x00000000

# File name in `ui/images` -> instance. They're PNGs in the game's UI image group. The instances are random, so they
# don't clash with the game's, the launcher's (0x14416302, 0x14416312, 0x14416314, 0xEA7F0EAE) or other plugins' images.
# The panel frames and most buttons are the game's own images, which the scripts use by their keys, so they aren't here.
IMAGE_FILES = {
	"chat-field.png": 0xF9C0707C,
	"chat-field-typing.png": 0x62225A7B,
	"chat-field-left.png": 0x2E5B71C3,
	"chat-field-right.png": 0x2E5B71C4,
	"chat-field-typing-left.png": 0x2E5B71C5,
	"chat-field-typing-right.png": 0x2E5B71C6,
	"chat-grip-left.png": 0x2E5B71C7,
	"chat-grip-right.png": 0x2E5B71C8,
	"chat-minimize.png": 0x391B92E8,
	"chat-chip.png": 0x3B71C9C9,
	"chat-count.png": 0x4D2E7A26,
	"chat-hint.png": 0x4D2E7A27,
	"ring-green.png": 0x4D2E7A21,
	"ring-blue.png": 0x4D2E7A22,
	"ring-amber.png": 0x4D2E7A23,
	"ring-red.png": 0x4D2E7A24,
	"ring-grey.png": 0x4D2E7A25,
	"status-tip.png": 0x5C3B8E21,
	"status-saved.png": 0xEB0C124A,
	"status-saving.png": 0x8ED6ED86,
	"status-failed.png": 0x8E5FC42E,
	"status-not-saved.png": 0xCA21110A,
	"status-connected.png": 0xBD4F44DD,
	"status-disconnected.png": 0xBEF3434A,
	"status-guest.png": 0x71DB29A5,
}

IMAGE_TYPE = 0x856DDBAC
IMAGE_GROUP = 0x46A006B0


def make_dbpf(entries):
	"""Returns a DBPF 1.0 file with index version 7.0, like the ones UIEditor and the Reader write. `entries` is a list of (type, group, instance, data)."""

	header_size = 96
	body = b""
	index = b""
	for type_id, group, instance, data in entries:
		index += struct.pack("<5I", type_id, group, instance, header_size + len(body), len(data))
		body += data

	now = int(time.time())
	header = b"DBPF" + struct.pack("<2I", 1, 0) + bytes(12)
	header += struct.pack("<9I", now, now, 7, len(entries), header_size + len(body), len(index), 0, 0, 0)
	header += struct.pack("<I", 0) + bytes(32)	# index minor version
	assert len(header) == header_size

	return header + body + index


def main():
	root = Path(__file__).resolve().parent.parent
	output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "sc4mp-dll-ui.dat"

	entries = []
	for name, (group, instance) in UI_FILES.items():
		# The game wants Windows line endings, like the scripts it ships with
		text = (root / "ui" / name).read_text(encoding="utf-8").replace("\r\n", "\n").replace("\n", "\r\n")
		entries.append((UI_TYPE, group, instance, text.encode("utf-8")))

	for name, instance in IMAGE_FILES.items():
		entries.append((IMAGE_TYPE, IMAGE_GROUP, instance, (root / "ui" / "images" / name).read_bytes()))

	output.write_bytes(make_dbpf(entries))
	print(f"Wrote {output}")


if __name__ == "__main__":
	main()
