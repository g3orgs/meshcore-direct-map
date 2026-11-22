#!/usr/bin/env python3
"""Build the single-file Linux OnAir plugin package from a CMake build tree."""

import argparse
import json
import re
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parent.parent
FILES = {
    "manifest.json": ROOT / "manifest.json",
    "meshcore-direct-map.so": None,
    "LICENSE": ROOT / "LICENSE",
    "THIRD_PARTY.md": ROOT / "THIRD_PARTY.md",
    "KURZANLEITUNG-DE.md": ROOT / "KURZANLEITUNG-DE.md",
    "BEISPIEL-FIKTIV.csv": ROOT / "BEISPIEL-FIKTIV.csv",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()

    manifest = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("id") != "meshcore-direct-map" or not re.fullmatch(r"\d+\.\d+\.\d+", manifest.get("version", "")):
        parser.error("manifest id or version is invalid")
    build_dir = args.build_dir.resolve()
    built = build_dir / "plugins/meshcore-direct-map"
    built_manifest = json.loads((built / "manifest.json").read_text(encoding="utf-8"))
    if built_manifest != manifest:
        parser.error("built manifest differs from source; rebuild the plugin first")

    files = dict(FILES)
    files["meshcore-direct-map.so"] = built / "meshcore-direct-map.so"
    for path in files.values():
        if not path.is_file():
            parser.error(f"missing package input: {path}")

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"meshcore-direct-map-{manifest['version']}.onairplugin"
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=9) as package:
        for name, path in files.items():
            info = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            info.external_attr = (0o755 if name.endswith(".so") else 0o644) << 16
            package.writestr(info, path.read_bytes(), compress_type=ZIP_DEFLATED, compresslevel=9)
    with ZipFile(output) as package:
        if package.testzip() is not None:
            raise RuntimeError("package failed ZIP integrity check")
    print(output)


if __name__ == "__main__":
    main()
