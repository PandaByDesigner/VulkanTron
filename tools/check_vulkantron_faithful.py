#!/usr/bin/env python3
"""Compare deterministic full-game OpenGL/Vulkan fixtures in a real desktop.

A repeated OpenGL run must agree first. State hashes and dimensions must match
exactly; images are never resized, aligned, blurred or masked for the gate.
The general error budgets accommodate raster edge/line coverage differences,
not missing UI, textures, lighting or effects. Three documented coplanar-effect
scenes have a separate, tighter full-frame budget with a modest local allowance.
A visual review remains separate; general-budget results are always retained.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageEnhance

# General values are in 8-bit RGB units, chosen before running the renderer.
# A local 32px tile gate prevents small missing labels/effects disappearing into
# a low full-frame mean. The explicit coplanar policy below followed visual and
# source review plus isolated A/B experiments; no limit changes automatically.
LIMITS = {"mean_abs": 2.0, "fraction_over_16": 0.02,
          "p99_abs": 32.0, "max_tile_mean_abs": 12.0}
COPLANAR_SCENES = frozenset(("crash-late", "winner", "draw-result"))
COPLANAR_LIMITS = {**LIMITS, "mean_abs": 0.10, "fraction_over_16": 0.002,
                   "max_tile_mean_abs": 18.0}
COPLANAR_REASON = (
    "The original shockwave strips and spires overlap at coplanar depths. "
    "Full-resolution visual review and isolated strip-triangulation, matrix, "
    "FMA and native-depth experiments identified red/white depth-tie coverage, "
    "with the same effect geometry and materials. These three scenes retain "
    "the original geometry and use tighter full-frame limits with a local "
    "tile allowance. The general-budget result remains visible.")
EXPECTED_SCENES = {
    "menu-root", "menu-game", "menu-video", "menu-details", "menu-audio",
    "menu-keys", "configure-key", "font-glyphs", "credits", "single", "split",
    "fourway", "pause-single", "pause-split", "pause-fourway", "camera-circling",
    "camera-follow", "camera-cockpit", "camera-mouse", "ai-hud", "recognizer", "effects-stencil",
    "effects-shadows-simple", "effects-transparent-trails", "floor-grid-fog",
    "crash-early", "crash-late", "winner", "draw-result", "trail-155", "trail-243",
    "trail-999", "trail-2005", "artpack-alternate", "artpack-restored", "resized-odd",
    "restored-visibility", "fullscreen", "restored-window",
}
STATE_KEYS = ("scene", "kind", "artpack", "width", "height", "logical_width",
              "logical_height", "time_ms", "gameplay_hash", "visual_before", "visual_after")
FAILURE_PATTERNS = (
    r"Vulkan validation ERROR", r"\[Vulkan cleanup\]", r"ERROR: AddressSanitizer",
    r"runtime error:", r"Assertion .* failed", r"\[fatal\]", r"\[glError:",
)


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_manifest(path):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if len(rows) != len(EXPECTED_SCENES):
        raise RuntimeError(f"{path}: expected {len(EXPECTED_SCENES)} scenes, received {len(rows)}")
    entries = {row["scene"]: row for row in rows}
    if len(entries) != len(rows) or set(entries) != EXPECTED_SCENES:
        raise RuntimeError(f"{path}: missing, extra or duplicated scene names")
    for row in rows:
        if not all(key in row for key in STATE_KEYS):
            raise RuntimeError(f"{path}: incomplete scene metadata")
        if not (0 < row["width"] <= 16384 and 0 < row["height"] <= 16384):
            raise RuntimeError(f"{path}: invalid captured dimensions")
    return entries


def load_rgb(path, row):
    with Image.open(path) as source:
        source.load()  # Full PNG decode; a header alone cannot pass this check.
        if source.format != "PNG" or source.size != (row["width"], row["height"]):
            raise RuntimeError(f"{path}: image format/dimensions disagree with manifest")
        return source.convert("RGB")


def pixel_metrics(reference, candidate):
    a = np.asarray(reference, dtype=np.int16)
    b = np.asarray(candidate, dtype=np.int16)
    if np.array_equal(a, b):
        return {"mean_abs": 0.0, "fraction_over_16": 0.0, "p99_abs": 0.0,
                "max_tile_mean_abs": 0.0, "max_abs": 0, "exact_pixel_fraction": 1.0,
                "rmse": 0.0}
    diff = np.abs(a - b)
    pixel_diff = diff.max(axis=2)
    tile_max = 0.0
    for y in range(0, diff.shape[0], 32):
        for x in range(0, diff.shape[1], 32):
            tile_max = max(tile_max, float(diff[y:y+32, x:x+32].mean()))
    return {"mean_abs": float(diff.mean()),
            "fraction_over_16": float((pixel_diff > 16).mean()),
            "p99_abs": float(np.percentile(pixel_diff, 99)),
            "max_tile_mean_abs": tile_max,
            "max_abs": int(diff.max()),
            "exact_pixel_fraction": float((pixel_diff == 0).mean()),
            "rmse": float(np.sqrt(np.square(diff.astype(np.float64)).mean()))}


def image_acceptance(scene, metrics, exact=False):
    general = all(metrics[key] <= limit for key, limit in LIMITS.items())
    scoped = not exact and scene in COPLANAR_SCENES
    limits = COPLANAR_LIMITS if scoped else LIMITS
    accepted = metrics["max_abs"] == 0 if exact else all(
        metrics[key] <= limit for key, limit in limits.items())
    return {"passed": accepted, "general_budget_passed": general,
            "coplanar_policy_applied": scoped,
            "acceptance_limits": {"max_abs": 0} if exact else dict(limits)}


def compare_pair(reference_dir, candidate_dir, diagnostic_dir, exact=False):
    reference = load_manifest(reference_dir / "scenes.jsonl")
    candidate = load_manifest(candidate_dir / "scenes.jsonl")
    if (reference_dir.parent / "lifecycle.json").read_text() != (candidate_dir.parent / "lifecycle.json").read_text():
        raise RuntimeError("Renderer runs disagree about native minimization/visibility support")
    diagnostic_dir.mkdir(parents=True, exist_ok=False)
    rows = []
    for name, expected in reference.items():
        actual = candidate[name]
        mismatched = [key for key in STATE_KEYS if expected[key] != actual[key]]
        row = {"scene": name, "kind": expected["kind"], "state_equal": not mismatched,
               "state_mismatches": mismatched, "passed": False}
        if mismatched:
            rows.append(row)
            continue  # Comparing unrelated state/geometry would be misleading.
        ref_image = load_rgb(reference_dir / (name + ".png"), expected)
        actual_image = load_rgb(candidate_dir / (name + ".png"), actual)
        metrics = pixel_metrics(ref_image, actual_image)
        row["metrics"] = metrics
        row.update(image_acceptance(name, metrics, exact))
        row["reference_sha256"] = hashlib.sha256(ref_image.tobytes()).hexdigest()
        row["candidate_sha256"] = hashlib.sha256(actual_image.tobytes()).hexdigest()
        if not row["passed"] or not row["general_budget_passed"]:
            difference = ImageChops.difference(ref_image, actual_image)
            # Diagnostic amplification only; the numerical gate uses raw pixels.
            ImageEnhance.Brightness(difference).enhance(4).save(diagnostic_dir / (name + "-diff.png"))
        rows.append(row)
        ref_image.close()
        actual_image.close()
    return rows


def run_fixture(executable, artpack, directory, assets, driver, vulkan):
    directory.mkdir(parents=True, exist_ok=False)
    config = directory / "config"
    captures = directory / "screenshots"
    config.mkdir(); captures.mkdir()
    # Production configuration loader reads this after the original defaults.
    (config / ".gltronrc").write_text(
        'settings.version = 0.70\nsettings.use_stencil = 1\n'
        'settings.current_artpack = "default"\nsettings.width = 800\n'
        'settings.height = 600\nsettings.windowMode = 1\nsave_completed = 1\n')
    env = os.environ.copy()
    for key in ("VULKANTRON_CONFIG_DIR", "VULKANTRON_SCREENSHOT_DIR", "VULKANTRON_DATA_DIR"):
        env.pop(key, None)
    env.update({"GLTRON_CONFIG_DIR": str(config), "GLTRON_SCREENSHOT_DIR": str(captures),
                "GLTRON_DATA_DIR": str(assets), "SDL_VIDEO_DRIVER": driver,
                "SDL_AUDIO_DRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",
                "VULKANTRON_VALIDATION": "1", "VK_LAYER_VALIDATE_SYNC": "1",
                "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
                "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
    if vulkan:
        env["VULKANTRON_CONFIG_DIR"] = str(config)
        env["VULKANTRON_SCREENSHOT_DIR"] = str(captures)
    # No loader disable/limit is set: validation must actually be available.
    env.pop("VK_LAYER_ENABLES", None)
    log_path = directory / "run.log"
    command = [str(executable), artpack]
    (directory / "invocation.json").write_text(json.dumps(
        {"command": command, "driver": driver, "assets": str(assets),
         "vulkan": vulkan, "seed": 12313, "clock_step_ms": 20,
         "executable_sha256": file_sha256(executable),
         "validation": env["VULKANTRON_VALIDATION"],
         "synchronization_validation": env["VK_LAYER_VALIDATE_SYNC"],
         "native_clip_depth_override": env.get("VULKANTRON_NATIVE_CLIP_DEPTH")},
        indent=2) + "\n")
    with log_path.open("w") as log:
        try:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=300, check=False)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"{directory.name}: native fixture exceeded 300 seconds; inspect {log_path}") from error
    output = log_path.read_text()
    if result.returncode or any(re.search(pattern, output) for pattern in FAILURE_PATTERNS):
        raise RuntimeError(f"{directory.name}: native fixture failed ({result.returncode}); inspect {log_path}")
    if not re.search(rf"FAITHFUL_SMOKE_OK captures={len(EXPECTED_SCENES)} settings_roundtrips=2", output):
        raise RuntimeError(f"{directory.name}: missing completion evidence; inspect {log_path}")
    lifecycle = re.search(r"minimize_supported=(\d) visibility_fallback=(\d)", output)
    if not lifecycle or lifecycle.groups() not in (("1", "0"), ("0", "1")):
        raise RuntimeError(f"{directory.name}: missing native visibility lifecycle result")
    (directory / "lifecycle.json").write_text(json.dumps({
        "native_minimize_supported": lifecycle.group(1) == "1",
        "hide_show_verified_instead": lifecycle.group(2) == "1"}, indent=2) + "\n")
    if vulkan:
        if env.get("VULKANTRON_NATIVE_CLIP_DEPTH") == "1" and (
                "[Vulkan] faithful depth volume: native GL [-1,1]" not in output):
            raise RuntimeError(f"{directory.name}: requested native depth diagnostic was not enabled")
        matches = re.findall(r"VT_FAITHFUL_STATS[^\n]*validation_errors=(\d+)[^\n]*adapter_errors=(\d+)[^\n]*failed=(\d+)", output)
        if not matches or any(errors != "0" or adapter != "0" or failed != "0"
                              for errors, adapter, failed in matches):
            raise RuntimeError(f"{directory.name}: missing/failed post-shutdown Vulkan validation statistics")
    load_manifest(captures / "scenes.jsonl")
    return captures


def write_report(output, results, driver):
    passed = all(row["passed"] for pack in results.values()
                 for category in pack.values() for row in category)
    complete = bool(results) and all("vulkan" in pack for pack in results.values())
    exceptions = [{"artpack": artpack, "scene": row["scene"],
                   "accepted": row["passed"]}
                  for artpack, pack in results.items() for row in pack.get("vulkan", [])
                  if row.get("metrics") and not row.get("general_budget_passed", row["passed"])]
    report = {"passed": passed, "driver": driver, "limits": LIMITS,
              "coplanar_limits": COPLANAR_LIMITS,
              "coplanar_scenes": sorted(COPLANAR_SCENES),
              "coplanar_reason": COPLANAR_REASON,
              "general_budget_exceptions": exceptions,
              "comparison_complete": complete,
              "reference_repeat_requires_exact_pixels": True, "results": results,
              "visual_review": "Required separately; numerical thresholds do not certify every visual detail."}
    (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = ["# VulkanTron faithful renderer comparison", "",
             f"Numerical acceptance: **{'PASS' if passed else 'FAIL'}**. Driver: `{driver}`.", "",
             "Scope: " + ("OpenGL repeat and Vulkan comparison." if complete else
                           "Reference determinism only; Vulkan has not yet been compared."), "",
             "Every scene must first match game state, visual state, time and drawable dimensions exactly.",
             "The repeated OpenGL reference must also match decoded pixels exactly.", "",
             "General Vulkan image budgets (8-bit RGB): mean absolute error ≤2; pixels with any channel error >16 ≤2%;",
             "99th-percentile per-pixel error ≤32; worst 32×32 tile mean error ≤12.",
             "The `crash-late`, `winner` and `draw-result` scenes instead require mean error ≤0.10,",
             "pixels with any channel error >16 ≤0.2%, and worst tile mean ≤18; the percentile limit is unchanged.",
             COPLANAR_REASON,
             f"General-budget exceptions in the Vulkan results below: **{len(exceptions)}**. These are not bit-identical images.",
             "No resize, alignment, blur or region masking is used. Differences are amplified only in diagnostic PNGs.", "",
             "A numerical pass requires a separate visual review before claiming complete rendering fidelity.", "",
             "| Artpack / check | Scene | State | Mean error | Pixels >16 | Worst tile | General budget | Acceptance |",
             "| --- | --- | --- | ---: | ---: | ---: | --- | --- |"]
    for artpack, categories in results.items():
        for category, rows in categories.items():
            for row in rows:
                metrics = row.get("metrics")
                values = (f'{metrics["mean_abs"]:.3f}', f'{metrics["fraction_over_16"]:.3%}',
                          f'{metrics["max_tile_mean_abs"]:.3f}') if metrics else ("—", "—", "—")
                lines.append(f'| {artpack} / {category} | {row["scene"]} | '
                             f'{"equal" if row["state_equal"] else ", ".join(row["state_mismatches"])} | '
                             f'{values[0]} | {values[1]} | {values[2]} | '
                             f'{"PASS" if row.get("general_budget_passed", row["passed"]) else "EXCEEDS"} | '
                             f'{"PASS (coplanar policy)" if row["passed"] and row.get("coplanar_policy_applied") else "PASS" if row["passed"] else "FAIL"} |')
    lines += ["", "Logs, exact invocations, full-frame PNGs and per-scene manifests are stored in each run directory.",
              "Every failed general or acceptance budget has an amplified difference PNG under the comparison directory.",
              "Each run's lifecycle.json records native minimization support. Where the compositor ignored minimization,",
              "hide/show restoration was verified separately; this does not certify native minimization on that compositor."]
    timings = []
    for path in sorted(output.glob("*/screenshots/timing.json")):
        timing = json.loads(path.read_text())
        timings.append({"run": path.parent.parent.name, **timing})
    if timings:
        report["presentation_wall_timings"] = timings
        (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
        lines += ["", "## Warmed display and presentation wall time", "",
                  "Eight warmup draws and 32 samples of the same paused scene. These measure CPU drawing, submission",
                  "and presentation waits together, not GPU execution time; differing swap policies prevent a direct GPU-speed claim.", "",
                  "| Run | Drawable | Median ms | 95th percentile ms |", "| --- | --- | ---: | ---: |"]
        for timing in timings:
            lines.append(f'| {timing["run"]} | {timing["width"]}×{timing["height"]} | '
                         f'{timing["median_ms"]:.3f} | {timing["p95_ms"]:.3f} |')
    (output / "comparison.md").write_text("\n".join(lines) + "\n")
    return passed


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test_policy()
    parser = argparse.ArgumentParser(
        description=__doc__, epilog="Use --self-test alone for the headless acceptance-policy regression.")
    parser.add_argument("--opengl", required=True, type=Path, help="vulkantron-reference-smoke executable")
    parser.add_argument("--vulkan", required=True, type=Path, help="vulkantron-faithful-smoke executable")
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="New directory; existing contents are never replaced")
    parser.add_argument("--driver", required=True, choices=("x11", "wayland"))
    parser.add_argument("--artpacks", nargs="+", choices=("default", "faithful"), default=["default", "faithful"])
    args = parser.parse_args()
    opengl, vulkan, assets = (path.resolve(strict=True) for path in (args.opengl, args.vulkan, args.assets))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    results = {}
    try:
        for artpack in dict.fromkeys(args.artpacks):
            reference = run_fixture(opengl, artpack, output / (artpack + "-opengl"), assets, args.driver, False)
            repeated = run_fixture(opengl, artpack, output / (artpack + "-opengl-repeat"), assets, args.driver, False)
            repeat_rows = compare_pair(reference, repeated, output / (artpack + "-reference-repeat"), exact=True)
            results[artpack] = {"reference-repeat": repeat_rows}
            if not all(row["passed"] for row in repeat_rows):
                write_report(output, results, args.driver)
                raise RuntimeError("OpenGL reference is not deterministic; Vulkan comparison would be misleading")
            candidate = run_fixture(vulkan, artpack, output / (artpack + "-vulkan"), assets, args.driver, True)
            results[artpack]["vulkan"] = compare_pair(reference, candidate, output / (artpack + "-comparison"))
        passed = write_report(output, results, args.driver)
        print(f'{"PASS" if passed else "FAIL"}: faithful comparison; {output / "comparison.md"}')
        return 0 if passed else 1
    except Exception as error:
        (output / "failure.txt").write_text(str(error) + "\n")
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


def self_test_policy():
    """A missing small glyph cannot hide in the scoped local allowance."""
    import tempfile
    reference = Image.new("RGB", (1024, 1024))
    glyph = ImageDraw.Draw(reference)
    for rectangle in ((512, 256, 514, 271), (524, 256, 526, 271), (515, 262, 523, 264)):
        glyph.rectangle(rectangle, fill="white")
    missing = Image.new("RGB", reference.size)
    metrics = pixel_metrics(reference, missing)
    assert metrics["mean_abs"] < COPLANAR_LIMITS["mean_abs"]
    assert metrics["fraction_over_16"] < COPLANAR_LIMITS["fraction_over_16"]
    assert metrics["max_tile_mean_abs"] > COPLANAR_LIMITS["max_tile_mean_abs"]
    for name in ("menu-root", *sorted(COPLANAR_SCENES)):
        assert not image_acceptance(name, metrics)["passed"]
    bounded = {"mean_abs": 0.08, "fraction_over_16": 0.0015,
               "p99_abs": 0.0, "max_tile_mean_abs": 16.0, "max_abs": 255}
    rows = []
    for name in sorted(COPLANAR_SCENES):
        result = image_acceptance(name, bounded)
        assert result["passed"] and not result["general_budget_passed"]
        assert result["coplanar_policy_applied"]
        assert not image_acceptance(name, bounded, exact=True)["passed"]
        for key, value in (("mean_abs", 0.11), ("fraction_over_16", 0.0021)):
            assert not image_acceptance(name, {**bounded, key: value})["passed"]
        rows.append({"scene": name, "kind": "effects", "state_equal": True,
                     "state_mismatches": [], "metrics": bounded, **result})
    assert not image_acceptance("single", bounded)["passed"]
    with tempfile.TemporaryDirectory(prefix="vulkantron-policy-") as directory:
        output = Path(directory)
        assert write_report(output, {"default": {"vulkan": rows}}, "self-test")
        report = json.loads((output / "comparison.json").read_text())
        assert report["limits"] == LIMITS and report["coplanar_limits"] == COPLANAR_LIMITS
        assert {row["scene"] for row in report["general_budget_exceptions"]} == COPLANAR_SCENES
        assert all(row["accepted"] for row in report["general_budget_exceptions"])
        assert "EXCEEDS | PASS (coplanar policy)" in (output / "comparison.md").read_text()
    reference.close(); missing.close()
    print("PASS: missing glyph rejected; scoped limits, exact references and exception metadata verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
