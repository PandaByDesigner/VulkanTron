#!/usr/bin/env python3
"""Install VulkanTron and a distinct app-menu entry under a user-local prefix."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MARKER = "Managed VulkanTron launcher"
OBSIDIAN_SELECTION = (b'\n-- VulkanTron bundled Obsidian presentation\n'
                      b'settings.current_artpack = "obsidian"\n'
                      b'settings.current_track = "song_obsidian_arena.wav"\n')


def configuration_directory():
    """Match the launcher's VulkanTron-only profile resolution."""
    explicit = os.environ.get("VULKANTRON_CONFIG_DIR")
    if explicit:
        return Path(explicit).resolve()
    xdg = Path(os.environ.get("XDG_CONFIG_HOME") or ".")
    base = xdg if xdg.is_absolute() else Path.home() / ".config"
    return (base / "vulkantron").resolve()


def checked_profile(directory):
    profile = directory / ".gltronrc"
    originals = {Path.home() / ".gltronrc"}
    if os.environ.get("GLTRON_CONFIG_DIR"):
        originals.add(Path(os.environ["GLTRON_CONFIG_DIR"]) / ".gltronrc")
    if profile.resolve() in {path.resolve() for path in originals}:
        raise ValueError(f"Refusing to change an original GLTron profile: {profile}")
    if profile.is_symlink() or (profile.exists() and not profile.is_file()):
        raise ValueError(f"Refusing a non-regular VulkanTron profile: {profile}")
    return profile


def select_obsidian_profile(directory):
    """Back up an existing profile and append only the two presentation values."""
    profile = checked_profile(directory)
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    existing = profile.exists()
    original = profile.read_bytes() if existing else b""
    if existing and original.endswith(OBSIDIAN_SELECTION):
        return profile, None
    original_stat = profile.stat() if existing else None
    mode = original_stat.st_mode & 0o777 if existing else 0o600
    backup = None
    if existing:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        backup = profile.with_name(f".gltronrc.pre-obsidian-{stamp}.bak")
        with backup.open("xb") as destination:
            destination.write(original)
        backup.chmod(mode)
        os.utime(backup, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
    content = original + OBSIDIAN_SELECTION if existing else (
        b"settings.version = 0.70\n" + OBSIDIAN_SELECTION + b"save_completed = 1\n")
    descriptor, temporary = tempfile.mkstemp(prefix=".gltronrc.obsidian-", dir=directory)
    staged = Path(temporary)
    try:
        with os.fdopen(descriptor, "wb") as destination:
            destination.write(content)
            destination.flush()
            os.fsync(destination.fileno())
        staged.chmod(mode)
        checked_profile(directory)
        if existing:
            if not profile.is_file() or profile.read_bytes() != original:
                raise RuntimeError(f"Profile changed during installation; kept backup at {backup}")
            staged.replace(profile)
        else:
            # Do not overwrite a profile created by a concurrent first launch.
            os.link(staged, profile)
    finally:
        staged.unlink(missing_ok=True)
    return profile, backup


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def tree_digest(directory):
    result = hashlib.sha256()
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or path.name == "launcher-install.json":
            continue
        result.update(str(path.relative_to(directory)).encode() + b"\0")
        result.update(digest(path).encode() + b"\n")
    return result.hexdigest()


def desktop_argument(value):
    value = str(value).replace("%", "%%").replace("\\", "\\\\\\\\")
    for character in ('"', '`', '$'):
        value = value.replace(character, "\\\\" + character)
    return '"' + value + '"'


def desktop_value(value):
    return str(value).replace("\\", "\\\\")


def write_checked(path, content, mode, checker):
    descriptor, temporary = tempfile.mkstemp(prefix=".vulkantron-", suffix=path.suffix,
                                             dir=path.parent)
    staged = Path(temporary)
    try:
        with os.fdopen(descriptor, "w") as output:
            output.write(content)
        staged.chmod(mode)
        subprocess.run([*checker, str(staged)], check=True)
        staged.replace(path)
    finally:
        staged.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/release")
    parser.add_argument("--prefix", type=Path, default=Path.home() / ".local")
    parser.add_argument("--select-obsidian", action="store_true",
                        help="Back up this user's VulkanTron profile and select the bundled Obsidian art and music")
    args = parser.parse_args()
    build = args.build_dir.resolve(strict=True)
    prefix = args.prefix.expanduser().resolve()
    profile_directory = configuration_directory() if args.select_obsidian else None
    if profile_directory is not None:
        try:
            checked_profile(profile_directory)
        except ValueError as error:
            parser.error(str(error))
    if any(ord(character) < 32 for character in str(prefix)):
        parser.error("The installation prefix must not contain control characters")
    executable = build / "bin/vulkantron"
    version_line = subprocess.check_output([executable, "--version"], text=True).strip()
    match = re.fullmatch(r"VulkanTron ([0-9][A-Za-z0-9.-]*) \(direct Vulkan; SDL3; faithful GLTron 0\.70 gameplay\)", version_line)
    if not match:
        parser.error("The executable is not the complete direct-Vulkan game")
    for command in ("cmake", "sh", "desktop-file-validate"):
        if not shutil.which(command):
            parser.error(f"Required installation tool is missing: {command}")
    version, executable_hash = match.group(1), digest(executable)
    app_parent = prefix / "opt/vulkantron"
    wrapper = prefix / "bin/vulkantron"
    desktop = prefix / "share/applications/io.github.PandaByDesigner.VulkanTron.desktop"
    for path in (wrapper, desktop):
        if path.is_symlink() or (path.exists() and MARKER not in path.read_text()):
            parser.error(f"Refusing to replace an unrelated launcher: {path}")
    app_parent.mkdir(parents=True, exist_ok=True)
    # CMake interprets backslashes in its install prefix as path separators.
    # Stage in a neutral path, then use Python filesystem operations for the
    # user's literal prefix. The final rename stays on the destination volume.
    with tempfile.TemporaryDirectory(prefix="vulkantron-install-", dir="/tmp") as temporary:
        staged = Path(temporary) / "app"
        subprocess.run(["cmake", "--install", str(build), "--prefix", str(staged)], check=True)
        if digest(staged / "bin/vulkantron") != executable_hash:
            raise RuntimeError("Installed executable does not match the tested build")
        for asset in ("art/obsidian/artpack.lua", "music/song_obsidian_arena.wav"):
            if not (staged / "share/gltron" / asset).is_file():
                raise RuntimeError(f"Obsidian launch assets are missing from the staged package: {asset}")
        # Shader-only changes need a new installation too, even if the linked
        # executable is identical. Include the complete installed content.
        content_hash = tree_digest(staged)
        app = app_parent / f"{version}-{content_hash[:12]}"
        receipt = app / "launcher-install.json"
        if app.exists():
            if app.is_symlink() or not receipt.is_file():
                parser.error(f"Refusing an unrecognized installation: {app}")
            previous = json.loads(receipt.read_text())
            if previous.get("content_sha256") != content_hash or tree_digest(app) != content_hash:
                parser.error(f"Existing installation verification failed: {app}")
        else:
            (staged / "launcher-install.json").write_text(json.dumps({
                "version": version_line, "executable_sha256": executable_hash,
                "content_sha256": content_hash,
                "icon_sha256": digest(staged / "share/icons/hicolor/scalable/apps/vulkantron.svg"),
                "launcher": str(wrapper), "desktop": str(desktop),
            }, indent=2) + "\n")
            with tempfile.TemporaryDirectory(prefix=".install-", dir=app_parent) as local:
                payload = Path(local) / "app"
                shutil.copytree(staged, payload)
                if tree_digest(payload) != content_hash:
                    raise RuntimeError("Installation copy verification failed")
                payload.rename(app)
    program = shlex.quote(str(app / "bin/vulkantron"))
    wrapper_text = '''#!/bin/sh
# Managed VulkanTron launcher: distinct application, preferences and captures.
set -eu
case "${1:-}" in
  --help|--version) exec ''' + program + ''' "$@" ;;
esac
export SDL_APP_ID=io.github.PandaByDesigner.VulkanTron
export SDL_APP_NAME=VulkanTron
if [ -z "${VULKANTRON_CONFIG_DIR:-}" ]; then
  case "${XDG_CONFIG_HOME:-}" in
    /*) config_base="$XDG_CONFIG_HOME" ;;
    *) config_base="$HOME/.config" ;;
  esac
  export VULKANTRON_CONFIG_DIR="$config_base/vulkantron"
fi
mkdir -p "$VULKANTRON_CONFIG_DIR"
if [ ! -e "$VULKANTRON_CONFIG_DIR/.gltronrc" ]; then
  seed=$(mktemp "$VULKANTRON_CONFIG_DIR/.gltronrc.XXXXXX")
  trap 'rm -f "$seed"' EXIT HUP INT TERM
  printf 'settings.version = 0.70\\nsettings.current_artpack = "obsidian"\\nsettings.current_track = "song_obsidian_arena.wav"\\nsave_completed = 1\\n' > "$seed"
  if ! ln "$seed" "$VULKANTRON_CONFIG_DIR/.gltronrc" 2>/dev/null; then
    test -f "$VULKANTRON_CONFIG_DIR/.gltronrc"
  fi
  rm -f "$seed"
  trap - EXIT HUP INT TERM
fi
exec ''' + program + ''' "$@"
'''
    icon = app / "share/icons/hicolor/scalable/apps/vulkantron.svg"
    desktop_text = f'''[Desktop Entry]
# {MARKER}
Type=Application
Version=1.0
Name=VulkanTron
GenericName=Lightcycle game
Comment=Obsidian lightcycle arena with original music and direct Vulkan rendering
Exec={desktop_argument(shutil.which("sh"))} {desktop_argument(wrapper)}
TryExec={desktop_value(wrapper)}
Icon={desktop_value(icon)}
Terminal=false
StartupNotify=false
StartupWMClass=io.github.PandaByDesigner.VulkanTron
Categories=Game;ArcadeGame;
Keywords=GLTron;Tron;Vulkan;Lightcycle;Arcade;Obsidian;
'''
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    desktop.parent.mkdir(parents=True, exist_ok=True)
    write_checked(wrapper, wrapper_text, 0o755, ["sh", "-n"])
    write_checked(desktop, desktop_text, 0o644, ["desktop-file-validate"])
    if shutil.which("update-desktop-database"):
        subprocess.run(["update-desktop-database", str(desktop.parent)], check=True)
    if profile_directory is not None:
        profile, backup = select_obsidian_profile(profile_directory)
        print(f"Obsidian profile: {profile}")
        if backup:
            print(f"Previous profile retained: {backup}")
    print(f"Installed VulkanTron: {desktop}")
    print(f"Executable: {app / 'bin/vulkantron'}")


if __name__ == "__main__":
    main()
