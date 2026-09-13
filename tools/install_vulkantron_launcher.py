#!/usr/bin/env python3
"""Install VulkanTron and a distinct app-menu entry under a user-local prefix."""
import argparse
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
    args = parser.parse_args()
    build = args.build_dir.resolve(strict=True)
    prefix = args.prefix.expanduser().resolve()
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
  printf 'settings.version = 0.70\\nsettings.current_artpack = "faithful"\\nsave_completed = 1\\n' > "$seed"
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
Comment=Faithful GLTron gameplay with a direct Vulkan renderer
Exec={desktop_argument(shutil.which("sh"))} {desktop_argument(wrapper)}
TryExec={desktop_value(wrapper)}
Icon={desktop_value(icon)}
Terminal=false
StartupNotify=false
StartupWMClass=io.github.PandaByDesigner.VulkanTron
Categories=Game;ArcadeGame;
Keywords=GLTron;Tron;Vulkan;Lightcycle;Arcade;
'''
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    desktop.parent.mkdir(parents=True, exist_ok=True)
    write_checked(wrapper, wrapper_text, 0o755, ["sh", "-n"])
    write_checked(desktop, desktop_text, 0o644, ["desktop-file-validate"])
    if shutil.which("update-desktop-database"):
        subprocess.run(["update-desktop-database", str(desktop.parent)], check=True)
    print(f"Installed VulkanTron: {desktop}")
    print(f"Executable: {app / 'bin/vulkantron'}")


if __name__ == "__main__":
    main()
