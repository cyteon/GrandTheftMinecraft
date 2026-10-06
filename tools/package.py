#!/usr/bin/env python3
"""Package a release into dist/: an OpenIV package (.oiv) and a manual-install zip.

Contents (nothing from Minecraft or GTA): GrandTheftMinecraft.asi, GrandTheftMinecraft/defs.txt + dlc_tex.txt, and
the block pack dlc.rpf with blank placeholder textures. The ASI fills in everything Minecraft from the player's own
Minecraft (or Mojang's servers) on first launch.

  python tools/package.py            (after ./build.sh, make_dlc_src.py and gtmpack build)
"""
import re
import uuid
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
VERSION = re.search(r'GTM_VERSION "([^"]+)"', (ROOT / "gta" / "src" / "version.h").read_text()).group(1)
REPO = "https://github.com/cyteon/GrandTheftMinecraft"
# stable package id so OpenIV treats new versions as updates of the same package
PACKAGE_ID = "{" + str(uuid.uuid5(uuid.NAMESPACE_URL, REPO)).upper() + "}"

FILES = {  # source -> path in the game folder
    ROOT / "build" / "GrandTheftMinecraft.asi": "GrandTheftMinecraft.asi",
    ROOT / "data" / "defs.txt": "GrandTheftMinecraft\\defs.txt",
    ROOT / "build" / "dlc" / "gtm" / "dlc_tex.txt": "GrandTheftMinecraft\\dlc_tex.txt",
    ROOT / "build" / "dlc_src" / "rigs.txt": "GrandTheftMinecraft\\rigs.txt",
    ROOT / "build" / "dlc_src" / "icons.txt": "GrandTheftMinecraft\\icons.txt",
    ROOT / "build" / "dlc_src" / "shapes.txt": "GrandTheftMinecraft\\shapes.txt",
    ROOT / "build" / "dlc" / "gtm" / "dlc.rpf": "update\\x64\\dlcpacks\\gtm\\dlc.rpf",
}

DESCRIPTION = """Minecraft creative mode inside GTA V story mode: the Minecraft hotbar, creative inventory, blocks, TNT,
ender pearls, flint and steel, sword, bow and crossbow, in GTA's world with GTA's lighting, shadows and physics.

Requires: GTA V Legacy (Steam/Rockstar/Epic, story mode only), ScriptHookV, OpenIV.asi (OpenIV > Tools > ASI Manager).

No Minecraft files are included. On the first launch the mod uses your own Minecraft Java 1.21.11 install, or
downloads the official 1.21.11 files from Mojang's servers (SHA1-checked), and builds its textures and sounds
from them. Restart GTA once after that first setup for the textured blocks.

F6 Minecraft mode on/off, E inventory, 1-9 / wheel hotbar, LMB break/hit, RMB place/use, MMB pick block,
double-tap Space to fly. Never use mods in GTA Online."""


def icon():
    """Our own 128x128 icon: a flat-shaded isometric grass-style cube (no Minecraft art)."""
    S = 128
    img = np.zeros((S, S, 4), np.uint8)
    T, L, C, R = np.array([64, 8]), np.array([10, 36]), np.array([64, 64]), np.array([118, 36])
    down = np.array([0, 56])
    faces = [(L, T - L, C - L, (106, 170, 60)), (L, C - L, down, (121, 85, 58)), (C, R - C, down, (96, 67, 46))]
    for o, u, v, col in faces:
        m = np.linalg.inv(np.array([[u[0], v[0]], [u[1], v[1]]]))
        for y in range(S):
            for x in range(S):
                a, b = m @ (np.array([x + 0.5, y + 0.5]) - o)
                if 0 <= a < 1 and 0 <= b < 1:
                    # a 4x4 pixel grid gives it a blocky feel
                    shade = 0.9 + 0.1 * (((int(a * 8) + int(b * 8)) % 2))
                    img[y, x] = (*[int(c * shade) for c in col], 255)
    return Image.fromarray(img, "RGBA")


def assembly():
    # sources sit flat in content/ (the common .oiv layout); targets are game-folder paths
    adds = "\n".join(f'\t\t<add source="{src.name}">{dst}</add>' for src, dst in FILES.items())
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<package version="2.1" id="{PACKAGE_ID}" target="Five">
	<metadata>
		<name>GrandTheftMinecraft</name>
		<version>
			<major>{VERSION.split('.')[0]}</major>
			<minor>{VERSION.split('.')[1]}</minor>
			<tag>{VERSION}</tag>
		</version>
		<author>
			<displayName>cyteon</displayName>
			<actionLink>{REPO}</actionLink>
			<web>{REPO}</web>
		</author>
		<description><![CDATA[{DESCRIPTION}]]></description>
		<largeDescription displayName="Read me"><![CDATA[{(ROOT / "README.md").read_text(encoding="utf-8")}]]></largeDescription>
		<licence displayName="License"><![CDATA[{(ROOT / "LICENSE").read_text(encoding="utf-8")}]]></licence>
	</metadata>
	<colors>
		<headerBackground useBlackTextColor="False">$FF3B7A2A</headerBackground>
		<iconBackground>$FF3B7A2A</iconBackground>
	</colors>
	<content>
{adds}
		<archive path="update\\update.rpf" createIfNotExist="False" type="RPF7">
			<xml path="common\\data\\dlclist.xml">
				<add xpath="/SMandatoryPacksData/Paths" append="Last"><Item>dlcpacks:/gtm/</Item></add>
			</xml>
		</archive>
	</content>
</package>
"""


MANUAL = """GrandTheftMinecraft {v} - manual install (the .oiv does all of this for you)

1. Install ScriptHookV (https://www.dev-c.com/gtav/scripthookv/) and OpenIV.asi (OpenIV > Tools > ASI Manager).
2. Copy GrandTheftMinecraft.asi and the GrandTheftMinecraft folder next to GTA5.exe.
3. Copy mods\\update\\x64\\dlcpacks\\gtm\\dlc.rpf into your GTA folder (create the folders).
4. In OpenIV (edit mode), copy update\\update.rpf into mods\\update\\ if it isn't there yet, open
   mods\\update\\update.rpf\\common\\data\\dlclist.xml and add  <Item>dlcpacks:/gtm/</Item>  before </Paths>.
5. Start GTA (story mode). First launch builds the Minecraft assets; restart once for the textured blocks.
"""


RELEASE_NOTES = """Minecraft creative mode inside GTA V story mode: the Minecraft HUD and creative inventory, 69 blocks that
are real GTA objects (lit, shadowed, solid), Minecraft mobs that fight GTA's people, Steve, elytra, TNT and more.

## What's new in {v}

{changes}

## Downloads

| File | For |
|---|---|
| **GrandTheftMinecraft-{v}.oiv** | Recommended: install with OpenIV's Package Installer |
| GrandTheftMinecraft-{v}-manual.zip | Manual install (instructions in `INSTALL.txt`) |

## Requirements

- GTA V **Legacy** (Steam / Rockstar / Epic), **story mode only**. GTA V Enhanced isn't supported.
- [ScriptHookV](https://www.dev-c.com/gtav/scripthookv/) for your game version.
- [OpenIV](https://openiv.com/) with **OpenIV.asi** installed: OpenIV > Tools > ASI Manager > OpenIV.asi > Install.
- Internet on the first launch, unless Minecraft Java 1.21.11 is installed on the PC.

## Install

1. Install ScriptHookV and OpenIV.asi (above).
2. Download **GrandTheftMinecraft-{v}.oiv**.
3. In OpenIV: **Tools > Package Installer**, open the `.oiv` and install it **to the mods folder**.
4. Start GTA V and load story mode. The first time, the top-left corner shows the setup for a few seconds while the
   mod builds its textures and sounds from your Minecraft 1.21.11 (or Mojang's official servers).
5. **Restart GTA once** when it says so. The textured blocks load from then on.

Press **F6** to switch Minecraft mode on and off, **E** for the creative inventory. All controls and settings are in
the [README](https://github.com/cyteon/GrandTheftMinecraft#controls).

> No Minecraft files are included: the mod reads them from your own Minecraft or downloads them from Mojang on your
> PC. Never use mods in GTA Online; the mod switches itself off if an online session starts.

**Uninstall:** delete `GrandTheftMinecraft.asi`, the `GrandTheftMinecraft` folder and
`mods\\update\\x64\\dlcpacks\\gtm` from the GTA folder, and remove the `dlcpacks:/gtm/` line from
`mods\\update\\update.rpf\\common\\data\\dlclist.xml` in OpenIV.
"""


def changes(version):
    """This version's section of CHANGELOG.md (the release notes start with it)."""
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    m = re.search(rf"^## {re.escape(version)}\s*$(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m:
        raise SystemExit(f"CHANGELOG.md has no '## {version}' section: add this release's changes first")
    return m.group(1).strip()


def main():
    for src in FILES:
        if not src.exists():
            raise SystemExit(f"missing {src} (build first)")
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    oiv = dist / f"GrandTheftMinecraft-{VERSION}.oiv"
    with zipfile.ZipFile(oiv, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("assembly.xml", assembly())
        ic = dist / "icon.png"
        icon().save(ic)
        z.write(ic, "icon.png")
        for src in FILES:
            z.write(src, "content/" + Path(src).name)
    man = dist / f"GrandTheftMinecraft-{VERSION}-manual.zip"
    with zipfile.ZipFile(man, "w", zipfile.ZIP_DEFLATED) as z:
        for src, dst in FILES.items():
            rel = dst.replace("\\", "/")
            if rel.startswith("update/"):
                rel = "mods/" + rel
            z.write(src, rel)
        z.writestr("INSTALL.txt", MANUAL.format(v=VERSION))
        z.write(ROOT / "README.md", "README.md")
        z.write(ROOT / "LICENSE", "LICENSE")
        z.write(ROOT / "NOTICE.md", "NOTICE.md")
    (dist / "RELEASE_NOTES.md").write_text(RELEASE_NOTES.format(v=VERSION, changes=changes(VERSION)), encoding="utf-8")
    for f in (oiv, man):
        print(f"{f.name}: {f.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
