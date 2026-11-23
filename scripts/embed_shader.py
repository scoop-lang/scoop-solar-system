#!/usr/bin/env python3
"""Embed the Metal source for offline runtime compilation by the native adapter."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / "native/shaders/solar.metal"
target = ROOT / "native/macos/shaders.m"
if not target.exists() or source.stat().st_mtime_ns > target.stat().st_mtime_ns:
    lines = ["@" + json.dumps(line + "\n") for line in source.read_text().splitlines()]
    target.write_text(
        "#import <Foundation/Foundation.h>\n"
        "NSString *solar_shader_source(void) {\n  return "
        + "\n         ".join(lines)
        + ";\n}\n"
    )
