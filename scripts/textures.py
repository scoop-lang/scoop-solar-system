#!/usr/bin/env python3
"""Fetch pinned observation-based maps together with their original source notices."""

import json
import shutil
from concurrent.futures import ThreadPoolExecutor

from prepare import ROOT, download


def main() -> None:
    source = ROOT / "assets/sources/textures.json"
    manifest = json.loads(source.read_text())
    base = (
        "https://raw.githubusercontent.com/CelestiaProject/CelestiaContent/"
        + manifest["revision"]
        + "/"
    )
    prepared = ROOT / "assets/prepared"
    notices = prepared / "notices/CelestiaContent"
    tasks = [(base + path, notices / path) for path in manifest["notices"]]
    for entry in manifest["assets"]:
        tasks.append((base + entry["path"], prepared / "textures" / entry["file"]))
        if entry["notice"] != "README":
            tasks.append((base + entry["notice"], notices / entry["notice"]))
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda item: download(*item), tasks))
    shutil.copy2(source, prepared / "notices/textures.json")
    print("Observation maps and original notices ready", flush=True)


if __name__ == "__main__":
    main()
