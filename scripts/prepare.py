#!/usr/bin/env python3
"""Prepare the official CSPICE toolkit and a bounded offline ephemeris package."""

import argparse
import platform
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOWNLOADS = ROOT / "vendor" / "downloads"
TOOLKIT = ROOT / "vendor" / "cspice"
KERNELS = ROOT / "assets" / "prepared" / "kernels"
NAIF = "https://naif.jpl.nasa.gov/pub/naif/"
# Official N0067 packages for the platforms with an implemented renderer.
TOOLKITS = {
    ("Darwin", "arm64"): ("MacM1_OSX_clang_64bit", "macos-arm64"),
    ("Linux", "x86_64"): ("PC_Linux_GCC_64bit", "linux-x86_64"),
}
SOURCES = [
    ("de442s.bsp", "planets/de442s.bsp", "1,2,3,4,5,6,7,8,9,10,199,299,301,399"),
    ("mar099.bsp", "satellites/mar099.bsp", "401,402,499"),
    ("jup365.bsp", "satellites/jup365.bsp", "501,502,503,504,599"),
    ("sat441.bsp", "satellites/sat441.bsp", "601,602,603,604,605,606,608,699"),
    ("ura111l.bsp", "satellites/a_old_versions/ura111l.bsp", "701,702,703,704,705,799"),
    ("nep097.bsp", "satellites/nep097.bsp", "801,899"),
    ("plu060.bsp", "satellites/plu060.bsp", "901,999"),
]


def download(url: str, destination: Path) -> Path:
    if destination.exists():
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_name(destination.name + ".part")
    print(f"Downloading {destination.name}", flush=True)
    subprocess.run(
        [
            "curl",
            "--fail",
            "--location",
            "--silent",
            "--show-error",
            "--retry",
            "4",
            "--connect-timeout",
            "25",
            "--continue-at",
            "-",
            "--output",
            str(partial),
            url,
        ],
        check=True,
    )
    partial.replace(destination)
    print(
        f"Ready {destination.name}: {destination.stat().st_size / 1048576:.1f} MiB",
        flush=True,
    )
    return destination


def prepare_toolkit() -> None:
    if not (TOOLKIT / "lib" / "cspice.a").exists():
        key = (platform.system(), platform.machine())
        if key not in TOOLKITS:
            raise SystemExit(f"No CSPICE package configured for {key[0]} {key[1]}")
        remote, label = TOOLKITS[key]
        package = download(
            NAIF + f"toolkit/C/{remote}/packages/cspice.tar.Z",
            DOWNLOADS / f"cspice-n0067-{label}.tar.Z",
        )
        subprocess.run(
            ["tar", "-xf", str(package), "-C", str(ROOT / "vendor")], check=True
        )
    if not (TOOLKIT / "lib" / "libcspice.a").exists():
        shutil.copy2(TOOLKIT / "lib" / "cspice.a", TOOLKIT / "lib" / "libcspice.a")
    notices = ROOT / "assets/prepared/notices"
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(TOOLKIT / "include/SpiceUsr.h", notices / "CSPICE-SpiceUsr.h")
    shutil.copy2(TOOLKIT / "doc/version.txt", notices / "CSPICE-version.txt")
    shutil.copy2(ROOT / "THIRD-PARTY-NOTICES.md", notices / "THIRD-PARTY-NOTICES.md")
    print("CSPICE N0067 ready", flush=True)


def prepare_kernel(item: tuple[str, str, str]) -> None:
    filename, remote, bodies = item
    target = KERNELS / ("solar-" + filename)
    if target.exists():
        return
    original = download(NAIF + "generic_kernels/spk/" + remote, DOWNLOADS / filename)
    staged = target.with_name(target.stem + "-preparing.bsp")
    staged.unlink(missing_ok=True)
    commands = DOWNLOADS / (filename + ".merge")
    comments = DOWNLOADS / (filename + ".comments")
    comments.write_text(
        "Scoop Solar System ephemeris subset prepared with NAIF SPKMERGE.\n"
        "Prepared by: Scoop Solar System project.\n"
        f"Original source: {NAIF}generic_kernels/spk/{remote}\n"
        "The original model and contributors are documented in the preserved comments.\n"
        "Selected bodies and interval are listed in the SPKMERGE log.\n"
    )
    commands.write_text(
        f"LEAPSECONDS_KERNEL = {KERNELS / 'naif0012.tls'}\n"
        f"SPK_KERNEL = {staged}\n"
        f"  LOG_FILE = {DOWNLOADS / (filename + '.merge.log')}\n"
        f"  INCLUDE_TEXT_FILE = {comments}\n"
        f"  SOURCE_SPK_KERNEL = {original}\n"
        "    INCLUDE_COMMENTS = YES\n"
        f"    BODIES = {bodies}\n"
        "    BEGIN_TIME = 1899 DEC 31 00:00:00\n"
        "    END_TIME = 2100 JAN 02 00:00:00\n"
    )
    subprocess.run([str(TOOLKIT / "exe" / "spkmerge"), str(commands)], check=True)
    staged.replace(target)
    print(
        f"Subset {target.name}: {target.stat().st_size / 1048576:.1f} MiB", flush=True
    )


def prepare_kernels() -> None:
    prepare_toolkit()
    KERNELS.mkdir(parents=True, exist_ok=True)
    for name, remote in [
        ("naif0012.tls", "lsk/naif0012.tls"),
        ("pck00011.tpc", "pck/pck00011.tpc"),
        ("gm_de440.tpc", "pck/gm_de440.tpc"),
    ]:
        download(NAIF + "generic_kernels/" + remote, KERNELS / name)
    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(prepare_kernel, SOURCES))
    print("All ephemeris subsets ready", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "part", choices=["toolkit", "kernels"], nargs="?", default="kernels"
    )
    args = parser.parse_args()
    if args.part == "toolkit":
        prepare_toolkit()
    else:
        prepare_kernels()


if __name__ == "__main__":
    main()
