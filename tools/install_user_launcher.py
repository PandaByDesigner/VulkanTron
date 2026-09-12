#!/usr/bin/env python3
"""Install a separate, user-local remaster and app-menu entry from a CMake build."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/release')
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local',
                        help='user-local prefix; a temporary prefix can stage the installation')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    prefix = args.prefix.expanduser().resolve()
    executable = build / 'bin/gltron'
    version_line = subprocess.check_output([executable, '--version'], text=True).strip()
    match = re.match(r'GLTron Faithful Remaster ([0-9][A-Za-z0-9.-]*) ', version_line)
    if not match:
        parser.error('The build does not identify itself as GLTron Faithful Remaster')
    version = match.group(1)
    app = prefix / 'opt/gltron-faithful' / version
    wrapper = prefix / 'bin/gltron-faithful'
    desktop = prefix / 'share/applications/gltron-faithful.desktop'
    receipt = app / 'launcher-install.json'
    if app.exists() and not receipt.is_file():
        parser.error(f'Refusing to overwrite an unrecognized installation: {app}')
    for path in (wrapper, desktop):
        if path.exists() and 'GLTron Faithful Remaster' not in path.read_text():
            parser.error(f'Refusing to replace an unrelated launcher: {path}')
    subprocess.run(['cmake', '--install', str(build), '--prefix', str(app)], check=True)
    icon = app / 'share/gltron/gltron-faithful.png'
    shutil.copyfile(ROOT / 'packaging/icons/gltron-faithful.png', icon)
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    desktop.parent.mkdir(parents=True, exist_ok=True)
    program = shlex.quote(str(app / 'bin/gltron'))
    wrapper.write_text('''#!/bin/sh
# GLTron Faithful Remaster: separate app identity, preferences and captures.
set -eu
case "${1:-}" in
  --help|--version) exec ''' + program + ''' "$@" ;;
esac
export SDL_APP_ID=gltron-faithful
export SDL_APP_NAME="GLTron Faithful Remaster"
export GLTRON_CONFIG_DIR="${GLTRON_CONFIG_DIR:-${XDG_CONFIG_HOME:-$HOME/.config}/gltron-faithful}"
export GLTRON_SCREENSHOT_DIR="${GLTRON_SCREENSHOT_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/gltron-faithful/screenshots}"
mkdir -p "$GLTRON_CONFIG_DIR" "$GLTRON_SCREENSHOT_DIR"
if [ ! -e "$GLTRON_CONFIG_DIR/.gltronrc" ]; then
  seed=$(mktemp "$GLTRON_CONFIG_DIR/.gltronrc.XXXXXX")
  trap 'rm -f "$seed"' EXIT HUP INT TERM
  if [ -f "$HOME/.gltronrc" ]; then
    cat "$HOME/.gltronrc" > "$seed"
  else
    printf 'settings.version = 0.70\\nsave_completed = 1\\n' > "$seed"
  fi
  printf '\\nsettings.current_artpack = "faithful"\\n' >> "$seed"
  # Install only if still absent; another simultaneous launch may have seeded it.
  if ! ln "$seed" "$GLTRON_CONFIG_DIR/.gltronrc" 2>/dev/null; then
    test -f "$GLTRON_CONFIG_DIR/.gltronrc"
  fi
  rm -f "$seed"
  trap - EXIT HUP INT TERM
fi
exec ''' + program + ''' "$@"
''')
    wrapper.chmod(0o755)
    # Desktop Exec quoting differs from shell quoting. Escape field-code percent
    # signs, then the characters special inside a double-quoted argument.
    command = str(wrapper).replace('%', '%%').replace('\\', '\\\\\\\\')
    for character in ('"', '`', '$'):
        command = command.replace(character, '\\\\' + character)
    desktop.write_text(f'''[Desktop Entry]
Type=Application
Version=1.0
Name=GLTron Faithful Remaster
GenericName=Lightcycle game
Comment=The faithful SDL3/OpenGL remaster with optional 4x original artwork
Exec="{command}"
TryExec={wrapper}
Icon={icon}
Terminal=false
StartupNotify=false
StartupWMClass=gltron-faithful
Categories=Game;ArcadeGame;
Keywords=GLTron;Tron;Faithful;Remaster;Lightcycle;Arcade;
''')
    desktop.chmod(0o644)
    subprocess.run(['sh', '-n', str(wrapper)], check=True)
    subprocess.run(['desktop-file-validate', str(desktop)], check=True)
    subprocess.run(['update-desktop-database', str(desktop.parent)], check=True)
    receipt.write_text(json.dumps({
        'version': version_line, 'launcher': str(wrapper), 'desktop': str(desktop),
        'executable_sha256': hashlib.sha256((app / 'bin/gltron').read_bytes()).hexdigest(),
        'icon_sha256': hashlib.sha256(icon.read_bytes()).hexdigest(),
    }, indent=2) + '\n')
    print(f'Installed GLTron Faithful Remaster: {desktop}')
    print(f'Executable: {app / "bin/gltron"}')
    print('The original GLTron launcher and home preferences were not changed.')


if __name__ == '__main__':
    main()
