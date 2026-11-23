#!/usr/bin/env python3
"""Compare the application adapter with saved, independently queried JPL Horizons states."""

import ctypes
import json
import math
import subprocess

from prepare import ROOT


class Vector(ctypes.Structure):
    _fields_ = [(axis, ctypes.c_double) for axis in ("x", "y", "z")]


class State(ctypes.Structure):
    _fields_ = [
        (name, Vector)
        for name in ("position", "velocity", "axis_x", "axis_y", "axis_z")
    ] + [("valid", ctypes.c_int32)]


def distance(vector: Vector, expected: list[float]) -> float:
    return math.sqrt(
        sum((getattr(vector, key) - value) ** 2 for key, value in zip("xyz", expected))
    )


def main() -> None:
    output = ROOT / "target/libsolar-reference.dylib"
    subprocess.run(
        [
            "xcrun",
            "clang",
            "-dynamiclib",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Inative/include",
            "-Ivendor/cspice/include",
            "native/astronomy/ephemeris.c",
            "vendor/cspice/lib/cspice.a",
            "-o",
            str(output),
        ],
        cwd=ROOT,
        check=True,
    )
    adapter = ctypes.CDLL(str(output))
    adapter.solar_spice_open.argtypes = [ctypes.c_char_p]
    adapter.solar_spice_state.argtypes = [ctypes.c_int32, ctypes.c_double]
    adapter.solar_spice_state.restype = State
    adapter.solar_spice_error.restype = ctypes.c_char_p
    if not adapter.solar_spice_open(str(ROOT / "assets/prepared").encode()):
        raise RuntimeError(adapter.solar_spice_error().decode())
    samples = json.loads((ROOT / "tests-native/horizons-j2000.json").read_text())
    try:
        for reference in samples["samples"]:
            state = adapter.solar_spice_state(reference["naif"], 0.0)
            if not state.valid:
                raise RuntimeError(adapter.solar_spice_error().decode())
            position = distance(state.position, reference["position_km"])
            velocity = distance(state.velocity, reference["velocity_km_s"])
            print(
                f"NAIF {reference['naif']}: position {position:.6f} km; velocity {velocity:.9f} km/s"
            )
            # Horizons can use a different DE planetary solution from the bundled DE442s.
            assert position < 5.0
            assert velocity < 0.00001
    finally:
        adapter.solar_spice_close()


if __name__ == "__main__":
    main()
