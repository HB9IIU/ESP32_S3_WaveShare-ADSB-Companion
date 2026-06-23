#!/usr/bin/env python3
"""
Generate 48x32 country flag JPEGs for all ICAO24 countries and save them
to <project_root>/data/flags/{iso2}.jpg.

Dependencies:
    pip install cairosvg pillow requests pycountry

Usage:
    python tools/generate_flags.py
"""

import sys
import requests
import pycountry
import cairosvg
from io import BytesIO
from pathlib import Path
from PIL import Image, ImageDraw

# ── Output ─────────────────────────────────────────────────────────────────────

FLAG_W  = 48
FLAG_H  = 32
OUT_DIR = Path(__file__).resolve().parents[1] / "data" / "flags"

FLAGICONS_VERSION = "7.3.2"
FLAGICONS_FORMAT  = "4x3"   # closest to 48×32 (3:2); slight vertical crop is fine

CDN_URL = "https://cdn.jsdelivr.net/gh/lipis/flag-icons@{version}/flags/{fmt}/{iso2}.svg"

# ── ICAO24 country ranges ──────────────────────────────────────────────────────

ICAO24_COUNTRIES = [
    (0x004000, 0x0047FF, "Zimbabwe"),
    (0x006000, 0x006FFF, "Mozambique"),
    (0x008000, 0x00FFFF, "South Africa"),
    (0x010000, 0x017FFF, "Egypt"),
    (0x018000, 0x01FFFF, "Libya"),
    (0x020000, 0x027FFF, "Morocco"),
    (0x028000, 0x02FFFF, "Tunisia"),
    (0x030000, 0x0307FF, "Botswana"),
    (0x032000, 0x032FFF, "Burundi"),
    (0x034000, 0x034FFF, "Cameroon"),
    (0x035000, 0x0357FF, "Comoros"),
    (0x036000, 0x036FFF, "Republic of the Congo"),
    (0x038000, 0x038FFF, "Côte d'Ivoire"),
    (0x03E000, 0x03EFFF, "Gabon"),
    (0x040000, 0x040FFF, "Ethiopia"),
    (0x042000, 0x042FFF, "Equatorial Guinea"),
    (0x044000, 0x044FFF, "Ghana"),
    (0x046000, 0x046FFF, "Guinea"),
    (0x048000, 0x0487FF, "Guinea-Bissau"),
    (0x04A000, 0x04A7FF, "Lesotho"),
    (0x04C000, 0x04CFFF, "Kenya"),
    (0x050000, 0x050FFF, "Liberia"),
    (0x054000, 0x054FFF, "Madagascar"),
    (0x058000, 0x058FFF, "Malawi"),
    (0x05A000, 0x05A7FF, "Maldives"),
    (0x05C000, 0x05CFFF, "Mali"),
    (0x05E000, 0x05E7FF, "Mauritania"),
    (0x060000, 0x0607FF, "Mauritius"),
    (0x062000, 0x062FFF, "Niger"),
    (0x064000, 0x064FFF, "Nigeria"),
    (0x068000, 0x068FFF, "Uganda"),
    (0x06A000, 0x06AFFF, "Qatar"),
    (0x06C000, 0x06CFFF, "Central African Republic"),
    (0x06E000, 0x06EFFF, "Rwanda"),
    (0x070000, 0x070FFF, "Senegal"),
    (0x074000, 0x0747FF, "Seychelles"),
    (0x076000, 0x0767FF, "Sierra Leone"),
    (0x078000, 0x078FFF, "Somalia"),
    (0x07A000, 0x07A7FF, "Eswatini"),
    (0x07C000, 0x07CFFF, "Sudan"),
    (0x080000, 0x080FFF, "Tanzania"),
    (0x084000, 0x084FFF, "Chad"),
    (0x088000, 0x088FFF, "Togo"),
    (0x08A000, 0x08AFFF, "Zambia"),
    (0x08C000, 0x08CFFF, "DR Congo"),
    (0x090000, 0x090FFF, "Angola"),
    (0x094000, 0x0947FF, "Benin"),
    (0x096000, 0x0967FF, "Cabo Verde"),
    (0x098000, 0x0987FF, "Djibouti"),
    (0x09A000, 0x09AFFF, "Gambia"),
    (0x09C000, 0x09CFFF, "Burkina Faso"),
    (0x09E000, 0x09E7FF, "São Tomé and Príncipe"),
    (0x0A0000, 0x0A7FFF, "Algeria"),
    (0x0A8000, 0x0A8FFF, "Bahamas"),
    (0x0AA000, 0x0AA7FF, "Barbados"),
    (0x0AB000, 0x0AB7FF, "Belize"),
    (0x0AC000, 0x0ADFFF, "Colombia"),
    (0x0AE000, 0x0AEFFF, "Costa Rica"),
    (0x0B0000, 0x0B0FFF, "Cuba"),
    (0x0B2000, 0x0B2FFF, "El Salvador"),
    (0x0B4000, 0x0B4FFF, "Guatemala"),
    (0x0B6000, 0x0B6FFF, "Guyana"),
    (0x0B8000, 0x0B8FFF, "Haiti"),
    (0x0BA000, 0x0BAFFF, "Honduras"),
    (0x0BC000, 0x0BC7FF, "Saint Vincent and the Grenadines"),
    (0x0BE000, 0x0BEFFF, "Jamaica"),
    (0x0C0000, 0x0C0FFF, "Nicaragua"),
    (0x0C2000, 0x0C2FFF, "Panama"),
    (0x0C4000, 0x0C4FFF, "Dominican Republic"),
    (0x0C6000, 0x0C6FFF, "Trinidad and Tobago"),
    (0x0C8000, 0x0C8FFF, "Suriname"),
    (0x0CA000, 0x0CA7FF, "Antigua and Barbuda"),
    (0x0CC000, 0x0CC7FF, "Grenada"),
    (0x0D0000, 0x0D7FFF, "Mexico"),
    (0x0D8000, 0x0DFFFF, "Venezuela"),
    (0x100000, 0x1FFFFF, "Russia"),
    (0x201000, 0x2017FF, "Namibia"),
    (0x202000, 0x2027FF, "Eritrea"),
    (0x300000, 0x33FFFF, "Italy"),
    (0x340000, 0x37FFFF, "Spain"),
    (0x380000, 0x3BFFFF, "France"),
    (0x3C0000, 0x3FFFFF, "Germany"),
    (0x400000, 0x43FFFF, "United Kingdom"),
    (0x440000, 0x447FFF, "Austria"),
    (0x448000, 0x44FFFF, "Belgium"),
    (0x450000, 0x457FFF, "Bulgaria"),
    (0x458000, 0x45FFFF, "Denmark"),
    (0x460000, 0x467FFF, "Finland"),
    (0x468000, 0x46FFFF, "Greece"),
    (0x470000, 0x477FFF, "Hungary"),
    (0x478000, 0x47FFFF, "Norway"),
    (0x480000, 0x487FFF, "Netherlands"),
    (0x488000, 0x48FFFF, "Poland"),
    (0x490000, 0x497FFF, "Portugal"),
    (0x498000, 0x49FFFF, "Czechia"),
    (0x4A0000, 0x4A7FFF, "Romania"),
    (0x4A8000, 0x4AFFFF, "Sweden"),
    (0x4B0000, 0x4B7FFF, "Switzerland"),
    (0x4B8000, 0x4BFFFF, "Turkey"),
    (0x4C0000, 0x4C7FFF, "Serbia"),
    (0x4C8000, 0x4C87FF, "Cyprus"),
    (0x4CA000, 0x4CAFFF, "Ireland"),
    (0x4CC000, 0x4CCFFF, "Iceland"),
    (0x4D0000, 0x4D07FF, "Luxembourg"),
    (0x4D2000, 0x4D27FF, "Malta"),
    (0x4D4000, 0x4D47FF, "Monaco"),
    (0x500000, 0x5007FF, "San Marino"),
    (0x501000, 0x5017FF, "Albania"),
    (0x501800, 0x501FFF, "Croatia"),
    (0x502800, 0x502FFF, "Latvia"),
    (0x503800, 0x503FFF, "Lithuania"),
    (0x504800, 0x504FFF, "Moldova"),
    (0x505800, 0x505FFF, "Slovakia"),
    (0x506800, 0x506FFF, "Slovenia"),
    (0x507800, 0x507FFF, "Uzbekistan"),
    (0x508000, 0x50FFFF, "Ukraine"),
    (0x510000, 0x5107FF, "Belarus"),
    (0x511000, 0x5117FF, "Estonia"),
    (0x512000, 0x5127FF, "North Macedonia"),
    (0x513000, 0x5137FF, "Bosnia and Herzegovina"),
    (0x514000, 0x5147FF, "Georgia"),
    (0x515000, 0x5157FF, "Tajikistan"),
    (0x516000, 0x5167FF, "Montenegro"),
    (0x600000, 0x6007FF, "Armenia"),
    (0x600800, 0x600FFF, "Azerbaijan"),
    (0x601000, 0x6017FF, "Kyrgyzstan"),
    (0x601800, 0x601FFF, "Turkmenistan"),
    (0x680000, 0x6807FF, "Bhutan"),
    (0x681000, 0x6817FF, "Micronesia, Federated States of"),
    (0x682000, 0x6827FF, "Mongolia"),
    (0x683000, 0x6837FF, "Kazakhstan"),
    (0x684000, 0x6847FF, "Palau"),
    (0x700000, 0x700FFF, "Afghanistan"),
    (0x702000, 0x702FFF, "Bangladesh"),
    (0x704000, 0x704FFF, "Myanmar"),
    (0x706000, 0x706FFF, "Kuwait"),
    (0x708000, 0x708FFF, "Laos"),
    (0x70A000, 0x70AFFF, "Nepal"),
    (0x70C000, 0x70C7FF, "Oman"),
    (0x70E000, 0x70EFFF, "Cambodia"),
    (0x710000, 0x717FFF, "Saudi Arabia"),
    (0x718000, 0x71FFFF, "South Korea"),
    (0x720000, 0x727FFF, "North Korea"),
    (0x728000, 0x72FFFF, "Iraq"),
    (0x730000, 0x737FFF, "Iran"),
    (0x738000, 0x73FFFF, "Israel"),
    (0x740000, 0x747FFF, "Jordan"),
    (0x748000, 0x74FFFF, "Lebanon"),
    (0x750000, 0x757FFF, "Malaysia"),
    (0x758000, 0x75FFFF, "Philippines"),
    (0x760000, 0x767FFF, "Pakistan"),
    (0x768000, 0x76FFFF, "Singapore"),
    (0x770000, 0x777FFF, "Sri Lanka"),
    (0x778000, 0x77FFFF, "Syria"),
    (0x789000, 0x789FFF, "Hong Kong"),
    (0x780000, 0x7BFFFF, "China"),
    (0x7C0000, 0x7FFFFF, "Australia"),
    (0x800000, 0x83FFFF, "India"),
    (0x840000, 0x87FFFF, "Japan"),
    (0x880000, 0x887FFF, "Thailand"),
    (0x888000, 0x88FFFF, "Viet Nam"),
    (0x890000, 0x890FFF, "Yemen"),
    (0x894000, 0x894FFF, "Bahrain"),
    (0x895000, 0x8957FF, "Brunei"),
    (0x896000, 0x896FFF, "United Arab Emirates"),
    (0x897000, 0x8977FF, "Solomon Islands"),
    (0x898000, 0x898FFF, "Papua New Guinea"),
    (0x899000, 0x8997FF, "Taiwan"),
    (0x8A0000, 0x8A7FFF, "Indonesia"),
    (0x900000, 0x9007FF, "Marshall Islands"),
    (0x901000, 0x9017FF, "Cook Islands"),
    (0x902000, 0x9027FF, "Samoa"),
    (0xC00000, 0xC3FFFF, "Canada"),
    (0xC80000, 0xC87FFF, "New Zealand"),
    (0xC88000, 0xC88FFF, "Fiji"),
    (0xC8A000, 0xC8A7FF, "Nauru"),
    (0xC8C000, 0xC8C7FF, "Saint Lucia"),
    (0xC8D000, 0xC8D7FF, "Tonga"),
    (0xC8E000, 0xC8E7FF, "Kiribati"),
    (0xC90000, 0xC907FF, "Vanuatu"),
    (0xC91000, 0xC917FF, "Andorra"),
    (0xC92000, 0xC927FF, "Dominica"),
    (0xC93000, 0xC937FF, "Saint Kitts and Nevis"),
    (0xC94000, 0xC947FF, "South Sudan"),
    (0xC95000, 0xC957FF, "Timor-Leste"),
    (0xC97000, 0xC977FF, "Tuvalu"),
    (0xE00000, 0xE3FFFF, "Argentina"),
    (0xE40000, 0xE7FFFF, "Brazil"),
    (0xE80000, 0xE80FFF, "Chile"),
    (0xE84000, 0xE84FFF, "Ecuador"),
    (0xE88000, 0xE88FFF, "Paraguay"),
    (0xE8C000, 0xE8CFFF, "Peru"),
    (0xE90000, 0xE90FFF, "Uruguay"),
    (0xE94000, 0xE94FFF, "Bolivia"),
]

ALIASES = {
    "DR Congo": "Congo, The Democratic Republic of the",
    "Republic of the Congo": "Congo",
    "Côte d'Ivoire": "Cote d'Ivoire",
    "Cabo Verde": "Cabo Verde",
    "Viet Nam": "Viet Nam",
    "Eswatini": "Eswatini",
    "United Kingdom": "United Kingdom",
    "Russia": "Russian Federation",
    "South Korea": "Korea, Republic of",
    "North Korea": "Korea, Democratic People's Republic of",
    "Syria": "Syrian Arab Republic",
    "Laos": "Lao People's Democratic Republic",
    "Taiwan": "Taiwan, Province of China",
    "Tanzania": "Tanzania, United Republic of",
    "Iran": "Iran, Islamic Republic of",
    "Moldova": "Moldova, Republic of",
    "Bolivia": "Bolivia, Plurinational State of",
    "Venezuela": "Venezuela, Bolivarian Republic of",
    "Palestine": "Palestine, State of",
    "Hong Kong": "Hong Kong",
    "Micronesia, Federated States of": "Micronesia, Federated States of",
    "São Tomé and Príncipe": "Sao Tome and Principe",
    "Turkey": "Türkiye",
    "Brunei": "Brunei Darussalam",
}


def country_to_iso2(name: str) -> str | None:
    # Normalise both curly and straight apostrophes to straight before lookup
    name = name.strip().replace("’", "’").replace("‘", "’")
    lookup = ALIASES.get(name, name)
    try:
        return pycountry.countries.lookup(lookup).alpha_2.lower()
    except LookupError:
        return None


def unique_countries() -> list[tuple[str, str]]:
    seen = set()
    out = []
    for _, _, name in ICAO24_COUNTRIES:
        if name in seen:
            continue
        seen.add(name)
        iso2 = country_to_iso2(name)
        if iso2:
            out.append((name, iso2))
        else:
            print(f"  WARNING: no ISO2 for '{name}' — skipped")
    return out


def fetch_and_render(iso2: str) -> Image.Image:
    url = CDN_URL.format(version=FLAGICONS_VERSION, fmt=FLAGICONS_FORMAT, iso2=iso2)
    response = requests.get(url, timeout=30)
    response.raise_for_status()
    # Render at full 4:3 width (48×36) then center-crop to target height (48×32)
    render_h = FLAG_W * 3 // 4   # 36 for FLAG_W=48
    png_bytes = cairosvg.svg2png(
        bytestring=response.content,
        output_width=FLAG_W,
        output_height=render_h,
    )
    img = Image.open(BytesIO(png_bytes)).convert("RGBA")
    # Center-crop vertically to FLAG_H
    top = (render_h - FLAG_H) // 2
    img = img.crop((0, top, FLAG_W, top + FLAG_H))
    # Flatten to RGB (JPEG has no alpha)
    bg = Image.new("RGB", img.size, (255, 255, 255))
    bg.paste(img, mask=img.getchannel("A"))
    return bg


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"Output : {OUT_DIR}")
    print(f"Size   : {FLAG_W}x{FLAG_H}")
    print()

    countries = unique_countries()
    ok = 0
    failures = []

    for name, iso2 in countries:
        out_path = OUT_DIR / f"{iso2}.jpg"
        try:
            img = fetch_and_render(iso2)
            img.save(out_path, "JPEG", quality=90, progressive=False)
            print(f"  OK  {iso2}  ({name})")
            ok += 1
        except Exception as e:
            print(f"  FAIL {iso2}  ({name}): {e}")
            failures.append(f"{iso2} / {name}: {e}")

    print()
    print(f"Done. {ok} flags saved to {OUT_DIR}")
    if failures:
        print(f"{len(failures)} failures:")
        for f in failures:
            print(f"  - {f}")
    sys.exit(0 if not failures else 1)


if __name__ == "__main__":
    main()
