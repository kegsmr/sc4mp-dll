"""Packs the SC4MP UI scripts in `ui` into `sc4mp-dll-ui.dat`, which goes in the Plugins folder with `sc4mp.dll`.

The game reads UI scripts (type 0) from any DBPF file in Plugins, so the DLL can create windows from them by group and instance (see `ChatPanel.cpp`). The files are stored uncompressed, which SimCity 4 reads fine.

Usage: python tools/make-ui-dat.py [output.dat]
"""

import struct
import sys
import time
from pathlib import Path

# File name in `ui` -> (group, instance). These must match the keys in the DLL's source.
UI_FILES = {
	"ChatPanel.ui": (0xC0E6A1C5, 0x1A9F3B21),
}

UI_TYPE = 0x00000000


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

	output.write_bytes(make_dbpf(entries))
	print(f"Wrote {output}")


if __name__ == "__main__":
	main()
