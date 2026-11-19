"""Packs browser-extension/ into store-ready zips:
   dist/extension/keyboard-changer-chromium.zip  (Chrome Web Store, Edge Add-ons, Opera, also for Brave/Vivaldi/Yandex)
   dist/extension/keyboard-changer-firefox.zip   (addons.mozilla.org)
Usage: python scripts/pack_extension.py
"""
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "browser-extension"
OUT = ROOT / "dist" / "extension"
COMMON = ["background.js", "options.html", "options.js"]


def pack(name: str, manifest: str) -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / f"keyboard-changer-{name}.zip"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", (SRC / manifest).read_text(encoding="utf-8"))
        for f in COMMON:
            z.write(SRC / f, f)
        for f in sorted((SRC / "icons").glob("*.png")) + sorted((SRC / "_locales").rglob("*.json")):
            z.write(f, f.relative_to(SRC).as_posix())
    json.loads((SRC / manifest).read_text(encoding="utf-8"))  # sanity check
    return target


if __name__ == "__main__":
    for name, manifest in (("chromium", "manifest.json"), ("firefox", "manifest.firefox.json")):
        print(pack(name, manifest))
