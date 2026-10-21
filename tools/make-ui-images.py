"""Draws the chat panel's images into `ui/images`, which `make-ui-dat.py` packs into `sc4mp-dll-ui.dat`.

The PNGs are committed, so building the .dat doesn't need Pillow. Run this after changing the drawing, then run `make-ui-dat.py`.
Everything is drawn at 4x and scaled down, which smooths the edges like the game's own images.

The panels, buttons and fonts are the game's own, referenced by key in the UI scripts (eg. the steel frame
{46a006b0,144161ee} and the caption button {46a006b0,144161eb}), so they look like the rest of SimCity 4 and aren't
redistributed. Only what the game has no image for is drawn here, in the game's colours.

Backgrounds are drawn at the exact size of their window in ui/ChatPanel.ui, so they look the same however the game
blits them. Button images are strips of four frames, like the game's: disabled, normal, pressed and hover.
The text field gets wider with the panel, so its middle is the same in every column, drawn as wide as the field can get,
and its rounded ends are images of their own.

Usage: python tools/make-ui-images.py   (needs Pillow: pip install pillow)
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SCALE = 4

# The game's UI colours, from its panel frames ({46a006b0,144161ee}) and its text (forecolor in its UI scripts)
OUTLINE = (57, 68, 114)
STEEL = (156, 178, 194)
INSET = (218, 224, 229)
TEXT = (63, 73, 103)

# Status colours, matching the launcher's overlay (resources/overlay-*.png in sc4mp-client)
GREEN = (46, 160, 67)
BLUE = (52, 120, 220)
RED = (206, 52, 52)
AMBER = (222, 150, 30)
GREY = (128, 128, 128)
INDIGO = (60, 70, 190)

# The layout, which must match ui/ChatPanel.ui
FIELD_SIZE = (290, 26)
FIELD_MIDDLE_WIDTH = 640	# at least the field's width in the widest panel (kMaxWidth in ChatPanel.cpp)
FIELD_END_WIDTH = 8
CHIP_BUTTON_SIZE = 50
HINT_SIZE = (84, 18)
STATUS_SIZE = 14
TITLE_BUTTON_SIZE = (22, 20)	# like the game's

STATES = ("disabled", "normal", "pressed", "hover")

# The saving icon's hourglass on its 14 pixel disc: bars at the top and bottom, and two triangles meeting at a narrow waist
HOURGLASS = (
	"..............",
	"..............",
	"..............",
	"....######....",
	"....######....",
	".....####.....",
	"......##......",
	"......##......",
	".....####.....",
	"....######....",
	"....######....",
	"..............",
	"..............",
	"..............",
)


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


def finish(image):
	return image.resize((image.width // SCALE, image.height // SCALE), Image.LANCZOS)


def draw_round_button():
	"""Draws the minimized panel's round button in four frames, at 50 pixels, in the style of the game's round buttons (eg.
	{46a006b0,cbcb6e9b}, which is 88 pixels and can't be scaled in game): a navy disc in a silver ring, with a soft shadow.
	The status ring, the speech bubble and the unread count are windows on top."""

	size = CHIP_BUTTON_SIZE
	discs = {"disabled": ((120, 124, 134), (96, 100, 110)), "normal": ((84, 104, 160), (58, 74, 119)), "pressed": ((58, 74, 119), (70, 89, 143)), "hover": ((104, 126, 186), (70, 89, 143))}

	strip = Image.new("RGBA", (size * len(STATES), size))
	for i, state in enumerate(STATES):
		top, bottom = discs[state]
		frame = Image.new("RGBA", (size * SCALE, size * SCALE))

		# Pressed, it sinks onto its shadow
		drop = 0
		if state == "pressed":
			drop = 1
		else:
			shadow = Image.new("RGBA", frame.size)
			ImageDraw.Draw(shadow).ellipse(scaled(3, 4, size - 2, size - 1), fill=(0, 0, 0, 110))
			frame.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(1.5 * SCALE)))

		# The silver ring, lit from the top left
		ring = gradient(frame.size, (247, 255, 255), (150, 160, 172))
		mask = Image.new("L", frame.size, 0)
		ImageDraw.Draw(mask).ellipse(scaled(1, 1 + drop, size - 3, size - 3 + drop), fill=255)
		ring.putalpha(mask)
		frame.alpha_composite(ring)
		draw = ImageDraw.Draw(frame)
		draw.ellipse(scaled(1, 1 + drop, size - 3, size - 3 + drop), outline=(32, 41, 66, 255), width=SCALE)

		# The navy disc, darker at its top edge like the game's
		disc = gradient(frame.size, top, bottom)
		mask = Image.new("L", frame.size, 0)
		ImageDraw.Draw(mask).ellipse(scaled(5, 5 + drop, size - 7, size - 7 + drop), fill=255)
		disc.putalpha(mask)
		frame.alpha_composite(disc)
		draw.ellipse(scaled(5, 5 + drop, size - 7, size - 7 + drop), outline=(32, 41, 66, 255), width=SCALE)
		strip.paste(finish(frame), (i * size, 0))
	return strip


def draw_ring(colour):
	"""Draws the round button's status ring, inside its silver one, in a status colour."""

	size = CHIP_BUTTON_SIZE
	image = Image.new("RGBA", (size * SCALE, size * SCALE))
	ImageDraw.Draw(image).ellipse(scaled(5, 5, size - 7, size - 7), outline=colour + (255,), width=3 * SCALE)
	return finish(image)


def draw_count_bubble():
	"""Draws the red bubble for the chip's unread count, which is a text window on top of it."""

	size = 20
	image = Image.new("RGBA", (size * SCALE, size * SCALE))
	ImageDraw.Draw(image).ellipse(scaled(0, 0, size - 1, size - 1), fill=RED + (255,), outline=OUTLINE + (255,), width=SCALE)
	return finish(image)


def draw_field(active):
	"""Draws the text field, sunken like the game's insets, and white while the player types."""

	width, height = FIELD_SIZE
	fill = INSET + (255,)
	if active:
		fill = (255, 255, 255, 255)

	image = Image.new("RGBA", (width * SCALE, height * SCALE))
	draw = ImageDraw.Draw(image)
	draw.rounded_rectangle(scaled(0, 0, width - 1, height - 1), 4 * SCALE, fill=fill, outline=OUTLINE + (255,), width=SCALE)
	if active:
		draw.rounded_rectangle(scaled(1, 1, width - 2, height - 2), 3 * SCALE, outline=TEXT + (255,), width=SCALE)
	else:
		draw.line(scaled(3, 1.5, width - 4, 1.5), fill=STEEL + (255,), width=SCALE)	# a shadow under the top edge, so it looks sunken
	return finish(image)


def draw_field_middle(active):
	"""Draws the text field's middle, a column from the middle of the field repeated, so it looks the same however wide
	the field is and whether the game crops or stretches it."""

	field = draw_field(active)
	column = field.crop((field.width // 2, 0, field.width // 2 + 1, field.height))
	return column.resize((FIELD_MIDDLE_WIDTH, field.height), Image.NEAREST)


def draw_field_end(active, right):
	"""Draws the text field's rounded left or right end, on the panel's steel, so it covers the corner of the middle under it."""

	field = Image.new("RGBA", FIELD_SIZE, STEEL + (255,))
	field.alpha_composite(draw_field(active))
	if right:
		return field.crop((field.width - FIELD_END_WIDTH, 0, field.width, field.height))
	return field.crop((0, 0, FIELD_END_WIDTH, field.height))


def draw_status_tip():
	"""Draws a see-through button the size of a status icon, in four frames, which is over the icon for its tooltip."""

	return Image.new("RGBA", (STATUS_SIZE * len(STATES), STATUS_SIZE))


def draw_hint():
	"""Draws the small inset behind the "N new below" hint, shown while the player has scrolled up."""

	width, height = HINT_SIZE
	image = Image.new("RGBA", (width * SCALE, height * SCALE))
	ImageDraw.Draw(image).rounded_rectangle(scaled(0, 0, width - 1, height - 1), 4 * SCALE, fill=INSET + (255,), outline=OUTLINE + (255,), width=SCALE)
	return finish(image)


def draw_title_button(draw_icon):
	"""Draws a small title bar button's four frames, like the game's up, down and close buttons ({46a006b0,144161e0},
	144161e2 and 144161f9): a light rim around a see-through dark square, with a light icon. `draw_icon(draw, cx, cy, colour)`
	draws the icon at 4x."""

	width, height = TITLE_BUTTON_SIZE
	rims = {"disabled": (194, 194, 194), "normal": (222, 232, 227), "pressed": (255, 255, 255), "hover": (255, 255, 255)}

	strip = Image.new("RGBA", (width * len(STATES), height))
	for i, state in enumerate(STATES):
		rim = rims[state] + (255,)
		frame = Image.new("RGBA", (width * SCALE, height * SCALE))

		# Pressed, it drops a pixel onto its shadow
		drop = 0
		inside = (0, 0, 0, 120)
		if state == "pressed":
			drop = 1
			inside = (0, 0, 0, 30)
		else:
			shadow = Image.new("RGBA", frame.size)
			ImageDraw.Draw(shadow).rounded_rectangle(scaled(2, 2, 19, 18), 3 * SCALE, fill=(0, 0, 0, 90))
			frame.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(SCALE)))

		draw = ImageDraw.Draw(frame)
		draw.rounded_rectangle(scaled(1, drop, 17.75, 15.75 + drop), 3 * SCALE, fill=inside, outline=rim, width=2 * SCALE)
		draw_icon(draw, 9.5 * SCALE, (8 + drop) * SCALE, rim)
		strip.paste(finish(frame), (i * width, 0))
	return strip


def minus_bar(draw, cx, cy, colour):
	"""A minus, for minimizing the panel to its round button."""

	draw.rectangle([cx - 4.5 * SCALE, cy - 1 * SCALE, cx + 4.5 * SCALE, cy + 1 * SCALE], fill=colour)


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
	image = image.resize((size, size), Image.LANCZOS)

	# The hourglass is set pixel by pixel, since at 14 pixels a scaled down one blurs into a blob
	if symbol == "hourglass":
		for y, row in enumerate(HOURGLASS):
			for x, pixel in enumerate(row):
				if pixel == "#":
					image.putpixel((x, y), (255, 255, 255, 255))
	return image


def make_images():
	"""Returns the images by file name."""

	return {
		"chat-field.png": draw_field_middle(active=False),
		"chat-field-typing.png": draw_field_middle(active=True),
		"chat-field-left.png": draw_field_end(active=False, right=False),
		"chat-field-right.png": draw_field_end(active=False, right=True),
		"chat-field-typing-left.png": draw_field_end(active=True, right=False),
		"chat-field-typing-right.png": draw_field_end(active=True, right=True),
		"chat-minimize.png": draw_title_button(minus_bar),
		"chat-chip.png": draw_round_button(),
		"chat-count.png": draw_count_bubble(),
		"chat-hint.png": draw_hint(),
		"ring-green.png": draw_ring(GREEN),
		"ring-blue.png": draw_ring(BLUE),
		"ring-amber.png": draw_ring(AMBER),
		"ring-red.png": draw_ring(RED),
		"ring-grey.png": draw_ring(GREY),
		"status-tip.png": draw_status_tip(),
		"status-saved.png": draw_status(GREEN, "check"),
		"status-saving.png": draw_status(BLUE, "hourglass"),
		"status-failed.png": draw_status(RED, "cross"),
		"status-not-saved.png": draw_status(AMBER, "bang"),
		"status-connected.png": draw_status(GREEN, "dot"),
		"status-disconnected.png": draw_status(AMBER, "dash"),	# amber like a save that can't reach the server
		"status-guest.png": draw_status(INDIGO, "person"),
	}


def main():
	output = Path(__file__).resolve().parent.parent / "ui" / "images"
	output.mkdir(exist_ok=True)

	for name, image in make_images().items():
		image.save(output / name, optimize=True)
		print(f"Wrote {output / name} ({image.width}x{image.height})")


if __name__ == "__main__":
	main()
