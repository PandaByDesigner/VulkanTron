#!/usr/bin/env python3
"""Create VulkanTron's Obsidian audio assets; YuE2 renders remain explicit/opt-in.

Requires Python 3 + NumPy and FFmpeg. No downloads or package changes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import wave

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
MUSIC = ROOT / "music" / "obsidian"
EFFECTS = ROOT / "data" / "obsidian"
SR = 22050


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_wav(path: Path, audio: np.ndarray, sample_rate: int = SR) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    audio = np.asarray(audio, dtype=np.float64)
    if not np.all(np.isfinite(audio)) or np.max(np.abs(audio)) > 1:
        raise ValueError("Invalid or overloaded signal")
    pcm = np.rint(audio * 32767).astype("<i2")
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1 if audio.ndim == 1 else audio.shape[1])
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(pcm.tobytes())


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as stream:
        if stream.getsampwidth() != 2:
            raise ValueError("Expected 16-bit PCM")
        data = np.frombuffer(stream.readframes(stream.getnframes()), dtype="<i2")
        return data.reshape(-1, stream.getnchannels()).astype(float) / 32768, stream.getframerate()


def metrics(path: Path, loop: bool = False, raw_generation: bool = False) -> dict:
    audio, sample_rate = read_wav(path)
    peak = float(np.max(np.abs(audio)))
    result = {
        "file": str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path),
        "sha256": digest(path), "sample_rate": sample_rate,
        "channels": audio.shape[1], "frames": len(audio),
        "duration_seconds": len(audio) / sample_rate,
        "peak_dbfs": round(20 * math.log10(max(peak, 1e-12)), 3),
        "rms_dbfs": round(20 * math.log10(max(float(np.sqrt(np.mean(audio**2))), 1e-12)), 3),
        "dc_offset": float(np.mean(audio)),
        "clipped_samples": int(np.sum(np.abs(audio) >= 32767 / 32768)),
        "format": "signed 16-bit little-endian PCM WAV",
    }
    if loop:
        delta = np.abs(np.diff(audio, axis=0))
        result["loop_seam_delta"] = float(np.max(np.abs(audio[0] - audio[-1])))
        result["interior_delta_p99"] = float(np.quantile(delta, .99))
        result["loop_seam_below_interior_p99"] = result["loop_seam_delta"] <= result["interior_delta_p99"]
    result["clipped_fraction"] = result["clipped_samples"] / audio.size
    # A generated raw source can touch full scale very occasionally. Record it
    # explicitly; all shipped/normalized assets still require zero clipped samples.
    excessive_clipping = result["clipped_fraction"] > (1e-5 if raw_generation else 0)
    if excessive_clipping or peak < .001:
        raise ValueError(f"Signal check failed: {path}")
    return result


def record(name: str, result: dict) -> None:
    MUSIC.mkdir(parents=True, exist_ok=True)
    (MUSIC / name).write_text(json.dumps(result, indent=2) + "\n")


def generate(args: argparse.Namespace) -> None:
    request = json.loads((MUSIC / "request.json").read_text())
    install = args.yue2.resolve()
    binary = install / "audio.cpp/build/local/bin/audiocpp_cli"
    models = install / "models/YuE2-Q4"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    wav = output / "obsidian-circuit-raw.wav"
    if wav.exists():
        raise FileExistsError(f"Retaining existing render: {wav}")
    command = [str(binary), "--task", "gen", "--family", "yue2", "--model", str(models),
               "--backend", "cpu", "--threads", str(request["threads"]),
               "--session-option", "yue2.model_gguf=" + request["main_model"],
               "--session-option", "yue2.vae_gguf=" + request["vae_model"],
               "--lyrics", request["lyrics"], "--request-option", "style=" + request["style"],
               "--request-option", "cot=" + request["planning"],
               "--request-option", "cfg_scale=1.0",
               "--request-option", "semantic_min_tokens=" + str(request["min_seconds"] * 25),
               "--request-option", "semantic_max_tokens=" + str(request["seconds"] * 25),
               "--request-option", "num_inference_steps=" + str(request["steps"]),
               "--seed", str(request["seed"]), "--out", str(wav), "--log", "--metrics"]
    receipt = {"request": request, "command": command, "binary_sha256": digest(binary),
               "raw_output": str(wav), "subjective_listening_review": False}
    (output / "request.json").write_text(json.dumps(receipt, indent=2) + "\n")
    started = time.monotonic()
    os.nice(10)
    print(f"Generating {wav}; detailed log at {output / 'generation.log'}", flush=True)
    with (output / "generation.log").open("w") as log:
        result = subprocess.run(command, cwd=output, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
    receipt["exit_code"] = result.returncode
    receipt["wall_seconds"] = round(time.monotonic() - started, 3)
    if result.returncode == 0:
        receipt["raw_audio"] = metrics(wav, raw_generation=True)
    (output / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    record("generation.json", receipt)
    print(json.dumps(receipt, indent=2), flush=True)
    result.check_returncode()


def peak_scale(audio: np.ndarray, peak_db: float) -> np.ndarray:
    audio = audio - np.mean(audio, axis=0)
    return audio * (10 ** (peak_db / 20) / np.max(np.abs(audio)))


def colored_noise(frames: int, seed: int, cutoff: float) -> np.ndarray:
    """A deterministic, exactly periodic low-pass noise buffer."""
    rng = np.random.default_rng(seed)
    spectrum = np.fft.rfft(rng.standard_normal(frames))
    frequencies = np.fft.rfftfreq(frames, 1 / SR)
    spectrum *= 1 / np.sqrt(1 + (frequencies / cutoff) ** 6)
    spectrum[0] = 0
    signal = np.fft.irfft(spectrum, n=frames)
    return signal / np.std(signal)


def make_effects() -> None:
    descriptions = {}
    t = np.arange(SR * 2) / SR
    phase = 2 * np.pi * 73.5 * t
    engine = (.53 * np.sin(phase + .65 * np.sin(2 * phase))
              + .21 * np.sin(2 * phase + .4 * np.sin(2 * np.pi * 4 * t))
              + .10 * np.sin(2 * np.pi * 36.5 * t)
              + .025 * colored_noise(len(t), 913261, 1700))
    engine *= .85 + .15 * np.cos(2 * np.pi * 2 * t)
    write_wav(EFFECTS / "game_engine.wav", peak_scale(engine, -9))
    descriptions["game_engine.wav"] = "2-second periodic electric drive: FM harmonics, low pulse, soft air; pitch follows game speed."

    t = np.arange(SR * 4) / SR
    recognizer = (.55 * np.sin(2 * np.pi * 41 * t + .45 * np.sin(2 * np.pi * .5 * t))
                  + .22 * np.sin(2 * np.pi * 82 * t)
                  + .08 * np.sin(2 * np.pi * 123.25 * t)
                  + .055 * colored_noise(len(t), 913262, 450))
    recognizer *= .88 + .12 * np.cos(2 * np.pi * .25 * t)
    write_wav(EFFECTS / "game_recognizer.wav", peak_scale(recognizer, -12))
    descriptions["game_recognizer.wav"] = "4-second periodic distant vehicle/arena hum, low and restrained."

    duration = .62
    t = np.arange(round(SR * duration)) / SR
    ramp = t / duration
    envelope = (1 - np.exp(-t / .005)) * np.exp(-t / .16) * (1 - ramp) ** 2
    boost = (.40 * np.sin(2 * np.pi * (130 * t + 580 * t**2))
             + .30 * colored_noise(len(t), 913263, 3500)) * envelope
    boost = peak_scale(boost, -6)
    boost *= np.minimum(t / .002, 1) * np.minimum((duration - t - 1/SR) / .006, 1).clip(0, 1)
    write_wav(EFFECTS / "game_boost.wav", boost)
    descriptions["game_boost.wav"] = "0.62-second rising electrical thrust one-shot with airy transient; zero-ended envelope."

    duration = 1.25
    t = np.arange(round(SR * duration)) / SR
    attack = 1 - np.exp(-t / .0015)
    thud_phase = 2 * np.pi * (32 * t + 50 * .045 * (1 - np.exp(-t / .045)))
    thud = .62 * np.sin(thud_phase) * np.exp(-t / .14)
    snap = .40 * colored_noise(len(t), 913264, 6000) * np.exp(-t / .034)
    shards = np.zeros_like(t)
    for frequency, decay in [(653, .12), (1103, .09), (1777, .13), (2927, .055)]:
        shards += .06 * np.sin(2 * np.pi * frequency * t) * np.exp(-t / decay)
    debris = .09 * colored_noise(len(t), 913265, 2500) * np.exp(-t / .24)
    crash = peak_scale((thud + snap + shards + debris) * attack, -3.5)
    crash *= np.minimum(t / .001, 1) * np.minimum((duration - t - 1/SR) / .025, 1).clip(0, 1)
    write_wav(EFFECTS / "game_crash.wav", crash)
    descriptions["game_crash.wav"] = "1.25-second impact: descending bass strike, brittle synthesized shards, short debris tail."

    results = []
    for name, description in descriptions.items():
        result = metrics(EFFECTS / name, loop=name in ("game_engine.wav", "game_recognizer.wav"))
        result["design"] = description
        results.append(result)
    record("effects.json", {"method": "Deterministic mathematical synthesis, no sampled audio", "generator": "tools/create_obsidian_audio.py effects", "subjective_listening_review": False, "effects": results})
    print(json.dumps(results, indent=2))


def integrated_loudness(path: Path) -> float:
    result = subprocess.run(["ffmpeg", "-hide_banner", "-i", str(path), "-af", "ebur128=peak=true", "-f", "null", "-"], capture_output=True, text=True, check=True)
    readings = re.findall(r"I:\s+(-?\d+\.\d+) LUFS", result.stderr)
    if not readings:
        raise ValueError("FFmpeg did not return integrated loudness")
    return float(readings[-1])


def finish_music(args: argparse.Namespace) -> None:
    raw = args.raw.resolve()
    target = ROOT / "music" / "song_obsidian_arena.wav"
    with tempfile.TemporaryDirectory(prefix="obsidian-audio-") as temporary:
        converted = Path(temporary) / "resampled.wav"
        subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-i", str(raw), "-ar", str(SR), "-ac", "2", "-c:a", "pcm_s16le", str(converted)], check=True)
        audio, sample_rate = read_wav(converted)
        if len(audio) < SR * 45:
            raise ValueError("Expected at least 45 seconds for the arena cue")
        trim = SR * 2
        audio = audio[trim:-trim]
        overlap = SR * 2
        phase = np.linspace(0, np.pi, overlap)
        ramp = ((1 - np.cos(phase)) * .5)[:, None]
        blend = audio[-overlap:] * (1 - ramp) + audio[:overlap] * ramp
        loop = np.concatenate([blend, audio[overlap:-overlap]])
        loop -= np.mean(loop, axis=0)
        loop = peak_scale(loop, -3.5)
        unnormalized = Path(temporary) / "loop.wav"
        write_wav(unnormalized, loop)
        measured_lufs = integrated_loudness(unnormalized)
        gain_db = min(-20 - measured_lufs, 0)
        write_wav(target, loop * 10 ** (gain_db / 20))
    result = metrics(target, loop=True)
    result.update({"title": "Obsidian Circuit", "raw_source": str(raw), "raw_sha256": digest(raw),
                   "edit": "Resample to 22050 stereo; trim 2s from each end; wrap with a 2s raised-cosine crossfade; remove DC; peak ceiling -3.5 dBFS; attenuate towards -20 LUFS without limiting.",
                   "integrated_lufs": integrated_loudness(target), "linear_gain_db_after_peak_normalization": gain_db,
                   "requested_instrumental": True, "subjective_listening_review": False,
                   "note": "Technical integrity and seam verified; instrumental adherence and musical loop phrasing require listening review."})
    if not result["loop_seam_below_interior_p99"]:
        raise ValueError("Loop boundary discontinuity exceeds interior 99th percentile")
    record("track.json", result)
    print(json.dumps(result, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="task", required=True)
    render = sub.add_parser("generate", help="Render the saved request with the existing YuE2 CPU CLI")
    render.add_argument("--yue2", type=Path, required=True)
    render.add_argument("--output", type=Path, required=True)
    sub.add_parser("effects", help="Recreate deterministic 22050 Hz mono game effects")
    finish = sub.add_parser("finish", help="Create the game-ready looping music asset")
    finish.add_argument("--raw", type=Path, required=True)
    args = parser.parse_args()
    if args.task == "generate":
        generate(args)
    elif args.task == "effects":
        make_effects()
    elif args.task == "finish":
        finish_music(args)


if __name__ == "__main__":
    main()
