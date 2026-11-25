#!/usr/bin/env python3
"""Embed shader sources for runtime compilation by the native adapters."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def embed(source: Path, target: Path, prefix: str, header: str, footer: str) -> None:
    if target.exists() and source.stat().st_mtime_ns <= target.stat().st_mtime_ns:
        return
    lines = [
        prefix + json.dumps(line + "\n") for line in source.read_text().splitlines()
    ]
    target.write_text(header + "\n         ".join(lines) + footer)


embed(
    ROOT / "native/shaders/solar.metal",
    ROOT / "native/macos/shaders.m",
    "@",
    "#import <Foundation/Foundation.h>\nNSString *solar_shader_source(void) {\n  return ",
    ";\n}\n",
)
embed(
    ROOT / "native/shaders/solar.glsl",
    ROOT / "native/linux/shaders.c",
    "",
    '#include "renderer_internal.h"\nconst char *solar_shader_source(void) {\n  return ',
    ";\n}\n",
)
