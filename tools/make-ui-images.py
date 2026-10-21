"""Draws the chat panel's images into `ui/images`, which `make-ui-dat.py` packs into `sc4mp-dll-ui.dat`.

The PNGs are committed, so building the .dat doesn't need Pillow. Run this after changing the drawing, then run `make-ui-dat.py`.
Everything is drawn at 4x and scaled down, which smooths the edges like the game's own images.

Backgrounds are drawn at the exact size of their window in ui/ChatPanel.ui, so they look the same however the game
blits them. Button images are strips of four frames, like the game's: disabled, normal, pressed and hover.

Usage: python tools/make-ui-images.py   (needs Pillow: pip install pillow)
"""

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SCALE = 4

# Pale steel panels with a navy title strip, like SC4's info panels
PANEL_TOP = (206, 214, 230)
PANEL_BOTTOM = (176, 188, 210)
PANEL_ALPHA = 235	# a little see-through
EDGE_DARK = (70, 84, 120)
EDGE_LIGHT = (240, 244, 252)
TITLE_TOP = (88, 108, 156)
TITLE_BOTTOM = (54, 70, 112)
INSET = (232, 236, 245)
INSET_EDGE = (120, 134, 168)
NAVY = (32, 40, 80)
FOCUS = (52, 120, 220)

# Status colours, matching the launcher's overlay (resources/overlay-*.png in sc4mp-client)
GREEN = (46, 160, 67)
BLUE = (52, 120, 220)
RED = (206, 52, 52)
AMBER = (222, 150, 30)
GREY = (128, 128, 128)
INDIGO = (60, 70, 190)

# The launcher's region map colours (resources/launcher-map-tile-*.png in sc4mp-client)
TILE_YOU = (136, 203, 116)
TILE_CLAIMED = (179, 196, 208)
TILE_UNCLAIMED = (92, 151, 190)

# The layout, which must match ui/ChatPanel.ui
PANEL_WIDTH = 360
PANEL_HEIGHT = 228
TITLE_HEIGHT = 36	# the collapsed panel
SHADOW = 6	# room right of and below the panel for its shadow
HISTORY = (8, 38, 352, 190)
FIELD_SIZE = (308, 26)
PILL_SIZE = (120, 20)
GUARD_HEIGHT = 156	# the claimed city warning is as wide as the panel
GUARD_BUTTON_SIZE = (156, 28)

STATES = ("disabled", "normal", "pressed", "hover")


def scaled(*values):
	return [round(v * SCALE) for v in values]


def gradient(size, top, bottom):
	"""Returns an image of `size` (already scaled) going from `top` to `bottom`."""

	width, height = size
	image = Image.new("RGBA", size)
	draw = ImageDraw.Draw(image)
	for y in range(height):
		t = y / max(1, height - 1)
		draw.line([(0, y), (width, y)], fill=tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
	return image


def rounded(size, radius, alpha=255):
	mask = Image.new("L", size, 0)
	ImageDraw.Draw(mask).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], radius, fill=alpha)
	return mask


def bevel_box(width, height, top, bottom, radius, alpha=255):
	"""Returns a rounded box at 4x with a dark outline and a light inner rim, like the game's bevels."""

	size = (width * SCALE, height * SCALE)
	box = gradient(size, top, bottom)
	box.putalpha(rounded(size, radius * SCALE, alpha))
	draw = ImageDraw.Draw(box)
	draw.rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], radius * SCALE, outline=EDGE_DARK + (255,), width=SCALE)
	draw.rounded_rectangle([SCALE, SCALE, size[0] - 1 - SCALE, size[1] - 1 - SCALE], (radius - 1) * SCALE, outline=EDGE_LIGHT + (160,), width=SCALE)
	return box


def finish(image):
	return image.resize((image.width // SCALE, image.height // SCALE), Image.LANCZOS)


def draw_panel(height, history):
	"""Draws the panel with its shadow and title strip, and the sunken box behind the chat lines if `history`."""

	image = Image.new("RGBA", ((PANEL_WIDTH + SHADOW) * SCALE, (height + SHADOW) * SCALE))

	shadow = Image.new("RGBA", image.size)
	ImageDraw.Draw(shadow).rounded_rectangle(scaled(3, 4, PANEL_WIDTH + 2, height + 3), 10 * SCALE, fill=(0, 0, 0, 110))
	image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(3 * SCALE)))

	image.alpha_composite(bevel_box(PANEL_WIDTH, height, PANEL_TOP, PANEL_BOTTOM, 10, PANEL_ALPHA), (0, 0))
	image.alpha_composite(bevel_box(PANEL_WIDTH - 8, 28, TITLE_TOP, TITLE_BOTTOM, 7), (4 * SCALE, 4 * SCALE))

	if history:
		left, top, right, bottom = HISTORY
		ImageDraw.Draw(image).rounded_rectangle(scaled(left, top, right - 1, bottom - 1), 6 * SCALE, fill=INSET + (240,), outline=INSET_EDGE + (255,), width=SCALE)

	return finish(image)


def draw_field(active):
	"""Draws the text field, white with a blue outline while the player types."""

	width, height = FIELD_SIZE
	image = Image.new("RGBA", (width * SCALE, height * SCALE))
	if active:
		ImageDraw.Draw(image).rounded_rectangle(scaled(0, 0, width - 1, height - 1), 6 * SCALE, fill=(255, 255, 255, 250), outline=FOCUS + (255,), width=2 * SCALE)
	else:
		ImageDraw.Draw(image).rounded_rectangle(scaled(0, 0, width - 1, height - 1), 6 * SCALE, fill=INSET + (230,), outline=INSET_EDGE + (255,), width=SCALE)
	return finish(image)


def draw_pill():
	"""Draws the white pill behind the save status. The icon and text are separate windows on top of it."""

	width, height = PILL_SIZE
	image = Image.new("RGBA", (width * SCALE, height * SCALE))
	ImageDraw.Draw(image).rounded_rectangle(scaled(0, 0, width - 1, height - 1), height / 2 * SCALE, fill=(255, 255, 255, 235), outline=INSET_EDGE + (255,), width=SCALE)
	return finish(image)


def draw_badge():
	"""Draws the red pill for the unread count, which is a text window on top of it."""

	width, height = 22, 15
	image = Image.new("RGBA", (width * SCALE, height * SCALE))
	ImageDraw.Draw(image).rounded_rectangle(scaled(0, 0, width - 1, height - 1), 7.5 * SCALE, fill=(222, 70, 60, 255), outline=(150, 30, 24, 255), width=SCALE)
	return finish(image)


def draw_button(width, height, draw_icon, radius=6):
	"""Draws a button's four frames side by side. `draw_icon(draw, cx, cy, colour)` draws its icon at 4x."""

	strip = Image.new("RGBA", (width * len(STATES), height))
	for i, state in enumerate(STATES):
		top, bottom, colour = (238, 241, 248), (196, 204, 222), NAVY
		if state == "disabled":
			top, bottom, colour = (220, 222, 226), (190, 193, 200), (150, 154, 164)
		elif state == "pressed":
			top, bottom = (184, 194, 214), (220, 226, 238)	# lit from below, so it looks pushed in
		elif state == "hover":
			top, bottom = (252, 253, 255), (212, 222, 242)

		frame = bevel_box(width, height, top, bottom, radius)
		if state == "hover":
			ImageDraw.Draw(frame).rounded_rectangle(scaled(1, 1, width - 2, height - 2), (radius - 1) * SCALE, outline=FOCUS + (200,), width=SCALE)

		shift = SCALE if state == "pressed" else 0	# the icon sinks with the button
		draw_icon(ImageDraw.Draw(frame), width * SCALE / 2 + shift, height * SCALE / 2 + shift, colour + (255,))
		strip.paste(finish(frame), (i * width, 0))
	return strip


def chevron(up):
	def draw_icon(draw, cx, cy, colour):
		h = 3 * SCALE if up else -3 * SCALE
		draw.line([(cx - 6 * SCALE, cy + h), (cx, cy - h), (cx + 6 * SCALE, cy + h)], fill=colour, width=2 * SCALE, joint="curve")
	return draw_icon


def minimize_arrow(draw, cx, cy, colour):
	"""An arrow into the bottom right corner, where the minimized panel goes."""

	draw.line([(cx - 5 * SCALE, cy - 5 * SCALE), (cx + 3 * SCALE, cy + 3 * SCALE)], fill=colour, width=2 * SCALE)
	draw.polygon([(cx + 5 * SCALE, cy + 5 * SCALE), (cx - 2 * SCALE, cy + 5 * SCALE), (cx + 5 * SCALE, cy - 2 * SCALE)], fill=colour)
	draw.line([(cx - 7 * SCALE, cy + 7 * SCALE), (cx + 7 * SCALE, cy + 7 * SCALE)], fill=colour, width=SCALE)


def no_icon(draw, cx, cy, colour):
	pass


def send_arrow(draw, cx, cy, colour):
	draw.polygon([(cx - 7 * SCALE, cy - 6 * SCALE), (cx + 8 * SCALE, cy), (cx - 7 * SCALE, cy + 6 * SCALE), (cx - 3 * SCALE, cy)], fill=colour)


def draw_status(colour, symbol, size=14):
	"""Draws a save status icon: a white-rimmed coloured circle with a white symbol."""

	s = size * 16	# drawn larger than the rest, since the symbols are tiny
	image = Image.new("RGBA", (s, s))
	draw = ImageDraw.Draw(image)
	draw.ellipse([s * .03, s * .03, s * .97, s * .97], fill=colour + (255,), outline=(255, 255, 255, 255), width=round(s * .07))
	w = round(s * .12)
	if symbol == "check":
		draw.line([(s * .27, s * .52), (s * .44, s * .68), (s * .74, s * .34)], fill="white", width=w, joint="curve")
	elif symbol == "cross":
		draw.line([(s * .32, s * .32), (s * .68, s * .68)], fill="white", width=w)
		draw.line([(s * .68, s * .32), (s * .32, s * .68)], fill="white", width=w)
	elif symbol == "arrows":
		draw.arc([s * .26, s * .26, s * .74, s * .74], 200, 470, fill="white", width=w)
		draw.polygon([(s * .70, s * .22), (s * .84, s * .46), (s * .56, s * .46)], fill="white")
	elif symbol == "bang":
		draw.line([(s * .5, s * .24), (s * .5, s * .56)], fill="white", width=w)
		draw.ellipse([s * .43, s * .64, s * .57, s * .78], fill="white")
	elif symbol == "dash":
		draw.line([(s * .3, s * .5), (s * .7, s * .5)], fill="white", width=w)
	elif symbol == "dot":
		draw.ellipse([s * .34, s * .34, s * .66, s * .66], fill="white")
	elif symbol == "person":
		draw.ellipse([s * .38, s * .22, s * .62, s * .46], fill="white")
		draw.chord([s * .26, s * .50, s * .74, s * .98], 180, 360, fill="white")
	return image.resize((size, size), Image.LANCZOS)


def draw_tile(colour, kind):
	"""Draws a small region tile badge, for showing who owns a tile (not used by the panel yet)."""

	size = 20
	image = bevel_box(size, size, tuple(min(255, c + 40) for c in colour), colour, 4)
	draw = ImageDraw.Draw(image)
	if kind == "abandoned":
		# Claimed grey with unclaimed blue stripes, like the launcher's abandoned tile
		stripes = Image.new("RGBA", image.size)
		stripe_draw = ImageDraw.Draw(stripes)
		for x in range(-size, size * 2, 5):
			stripe_draw.line(scaled(x, size + 2, x + size + 4, -2), fill=TILE_UNCLAIMED + (255,), width=round(1.8 * SCALE))
		inner = rounded(image.size, 3 * SCALE)
		inner = inner.crop((0, 0) + image.size)
		image.paste(stripes, (0, 0), Image.composite(stripes.getchannel("A"), Image.new("L", image.size, 0), inner))
		ImageDraw.Draw(image).rounded_rectangle([0, 0, image.width - 1, image.height - 1], 4 * SCALE, outline=EDGE_DARK + (255,), width=SCALE)
	elif kind == "you":
		points = []
		for i in range(10):
			r = 6 if i % 2 == 0 else 2.6
			a = math.pi / 2 + i * math.pi / 5
			points.append(((10 + r * math.cos(a)) * SCALE, (10.5 - r * math.sin(a)) * SCALE))
		draw.polygon(points, fill=(255, 255, 255, 255), outline=NAVY + (255,), width=SCALE // 2)
	elif kind == "claimed":
		house = [(5, 10), (10, 5), (15, 10), (13.5, 10), (13.5, 15), (6.5, 15), (6.5, 10)]
		draw.polygon([(x * SCALE, y * SCALE) for x, y in house], fill=(255, 255, 255, 255), outline=NAVY + (255,), width=SCALE // 2)
	return finish(image)


def make_images():
	"""Returns the images by file name."""

	return {
		"chat-panel.png": draw_panel(PANEL_HEIGHT, history=True),
		"chat-panel-collapsed.png": draw_panel(TITLE_HEIGHT, history=False),
		"chat-field.png": draw_field(active=False),
		"chat-field-typing.png": draw_field(active=True),
		"chat-hide.png": draw_button(24, 20, chevron(up=True)),
		"chat-show.png": draw_button(24, 20, chevron(up=False)),
		"chat-send.png": draw_button(32, 26, send_arrow),
		"chat-minimize.png": draw_button(24, 20, minimize_arrow),
		"chat-chip.png": draw_button(64, 26, no_icon, radius=13),	# the minimized panel; its icon and text are windows on top
		"chat-badge.png": draw_badge(),
		"status-pill.png": draw_pill(),
		"status-saved.png": draw_status(GREEN, "check"),
		"status-saving.png": draw_status(BLUE, "arrows"),
		"status-failed.png": draw_status(RED, "cross"),
		"status-not-saved.png": draw_status(AMBER, "bang"),
		"status-connected.png": draw_status(GREEN, "dot"),
		"status-disconnected.png": draw_status(GREY, "dash"),
		"status-guest.png": draw_status(INDIGO, "person"),
		"guard-panel.png": draw_panel(GUARD_HEIGHT, history=False),	# the claimed city warning (ui/GuardDialog.ui)
		"guard-button.png": draw_button(*GUARD_BUTTON_SIZE, no_icon),	# its labels are text windows on top
		"tile-you.png": draw_tile(TILE_YOU, "you"),
		"tile-claimed.png": draw_tile(TILE_CLAIMED, "claimed"),
		"tile-unclaimed.png": draw_tile(TILE_UNCLAIMED, "unclaimed"),
		"tile-abandoned.png": draw_tile(TILE_CLAIMED, "abandoned"),
	}


def main():
	output = Path(__file__).resolve().parent.parent / "ui" / "images"
	output.mkdir(exist_ok=True)

	for name, image in make_images().items():
		image.save(output / name, optimize=True)
		print(f"Wrote {output / name} ({image.width}x{image.height})")


if __name__ == "__main__":
	main()
