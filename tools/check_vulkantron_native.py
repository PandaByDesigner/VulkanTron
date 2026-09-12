#!/usr/bin/env python3
"""Opt-in real GPU checks. Requires an available desktop and Vulkan validation."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="New directory for logs and captures")
    parser.add_argument("--driver", choices=("x11", "wayland"), required=True)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env["SDL_VIDEO_DRIVER"] = args.driver
    env.pop("VK_LAYER_ENABLES", None)
    env["VK_LAYER_VALIDATE_SYNC"] = "1"
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")

    def run(label, arguments, expected_error=None):
        result = subprocess.run([str(executable), *arguments], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=45, text=True)
        (args.output / (label + ".log")).write_text(result.stdout)
        if "Vulkan validation ERROR" in result.stdout or "[Vulkan cleanup]" in result.stdout:
            raise RuntimeError(f"{label}: Vulkan validation/cleanup error; inspect log")
        if "ERROR: AddressSanitizer" in result.stdout or "runtime error:" in result.stdout:
            raise RuntimeError(f"{label}: memory or undefined-behavior error")
        if expected_error:
            if result.returncode == 0 or expected_error not in result.stdout:
                raise RuntimeError(f"{label}: expected failure was not reported")
            return None
        if result.returncode:
            raise RuntimeError(f"{label}: process exited {result.returncode}; inspect log")
        match = re.search(r"VT_STATS .*frames=(\d+).*validation_errors=(\d+).*state_hash=([0-9a-f]+)", result.stdout)
        if not match or match.group(1) != "160" or match.group(2) != "0":
            raise RuntimeError(f"{label}: incomplete or invalid frame/validation statistics")
        capture = args.output / (label + ".png")
        data = capture.read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
            raise RuntimeError(f"{label}: no valid PNG header")
        width, height = struct.unpack(">II", data[16:24])
        if not width or not height:
            raise RuntimeError(f"{label}: empty screenshot")
        print(f"PASS {label}: 160 frames, {width}x{height}, zero validation errors")
        return match.group(3)

    common = ["--demo", "--validation", "--fixed-step", "--frames", "160"]
    first = run("overview", [*common, "--overview", "--capture", str(args.output / "overview.png")])
    second = run("follow-window", [*common, "--exercise-window", "--capture", str(args.output / "follow-window.png")])
    if first != second:
        raise RuntimeError("Camera/window changes altered the fixed-step production game state")
    existing = args.output / "overview.png"
    digest = hashlib.sha256(existing.read_bytes()).digest()
    run("capture-collision", ["--demo", "--validation", "--fixed-step", "--frames", "1",
                              "--capture", str(existing)], "Cannot exclusively create screenshot")
    if hashlib.sha256(existing.read_bytes()).digest() != digest:
        raise RuntimeError("Existing screenshot changed during rejected overwrite")
    bad_shaders = args.output / "bad-shaders"
    bad_shaders.mkdir()
    (bad_shaders / "scene.vert.spv").write_bytes(bytes(4))
    run("bad-shader", ["--demo", "--validation", "--frames", "1", "--shaders", str(bad_shaders)],
        "Invalid SPIR-V")
    print(f"PASS capture protection, invalid-shader cleanup, renderer-independent state {first}")


if __name__ == "__main__":
    main()
