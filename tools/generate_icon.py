from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
ASSETS.mkdir(exist_ok=True)

S = 1024


def rounded_gradient(mask, top, bottom):
    image = Image.new("RGBA", (S, S))
    px = image.load()
    for y in range(S):
        t = y / (S - 1)
        color = tuple(round(top[i] * (1 - t) + bottom[i] * t) for i in range(4))
        for x in range(S):
            px[x, y] = color
    image.putalpha(mask)
    return image


canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))

# Soft shadow behind the document tile.
shadow = Image.new("L", (S, S), 0)
sd = ImageDraw.Draw(shadow)
sd.rounded_rectangle((142, 100, 850, 914), radius=184, fill=190)
shadow = shadow.filter(ImageFilter.GaussianBlur(42))
shadow_layer = Image.new("RGBA", (S, S), (20, 32, 72, 0))
shadow_layer.putalpha(shadow)
canvas.alpha_composite(shadow_layer, (16, 28))

# Blue-indigo document tile.
tile_mask = Image.new("L", (S, S), 0)
td = ImageDraw.Draw(tile_mask)
td.rounded_rectangle((142, 84, 850, 898), radius=184, fill=255)
tile = rounded_gradient(tile_mask, (37, 99, 235, 255), (109, 40, 217, 255))
canvas.alpha_composite(tile)

# Folded white PDF page.
page = Image.new("RGBA", (S, S), (0, 0, 0, 0))
pd = ImageDraw.Draw(page)
pd.rounded_rectangle((282, 194, 750, 794), radius=70, fill=(250, 252, 255, 255))
pd.polygon([(606, 194), (750, 338), (606, 338)], fill=(214, 224, 247, 255))
pd.line([(606, 194), (606, 338), (750, 338)], fill=(181, 197, 232, 255), width=12)

# Three short content lines keep the mark readable at small sizes.
pd.rounded_rectangle((356, 392, 650, 424), radius=16, fill=(55, 82, 157, 190))
pd.rounded_rectangle((356, 458, 606, 490), radius=16, fill=(55, 82, 157, 135))
pd.rounded_rectangle((356, 524, 560, 556), radius=16, fill=(55, 82, 157, 105))
canvas.alpha_composite(page)

# Teal unlocked padlock, deliberately oversized for 16x16 legibility.
lock = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ld = ImageDraw.Draw(lock)
teal = (20, 184, 166, 255)
teal_dark = (13, 148, 136, 255)
ld.rounded_rectangle((440, 566, 800, 824), radius=72, fill=teal, outline=(255, 255, 255, 235), width=18)
ld.arc((498, 380, 744, 662), start=190, end=352, fill=(255, 255, 255, 255), width=78)
ld.arc((498, 380, 744, 662), start=190, end=352, fill=teal_dark, width=46)
ld.ellipse((592, 642, 650, 700), fill=(255, 255, 255, 255))
ld.rounded_rectangle((610, 680, 632, 744), radius=11, fill=(255, 255, 255, 255))
canvas.alpha_composite(lock)

# Tiny cyan shine accents.
shine = ImageDraw.Draw(canvas)
shine.polygon([(214, 228), (230, 270), (272, 286), (230, 302), (214, 344), (198, 302), (156, 286), (198, 270)], fill=(94, 234, 212, 255))
shine.ellipse((776, 160, 824, 208), fill=(199, 255, 247, 240))

preview = ASSETS / "app_icon_1024.png"
ico = ASSETS / "app_icon.ico"
canvas.save(preview)
canvas.save(ico, format="ICO", sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)])
print(preview)
print(ico)
