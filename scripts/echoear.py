#!/usr/bin/env python3
"""One source tree, board-specific build packages and guarded USB installation."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOARDS = {'waveshare': 'echoear-waveshare-1.75b', 'viewe': 'echoear-viewe-1.75'}
KNOWN_DEVICES = {'30:ed:a0:29:a5:20': 'waveshare', '10:51:db:8e:d9:7c': 'viewe'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_device(board, mac, expected=None):
    mac = mac.lower()
    if mac in KNOWN_DEVICES:
        if KNOWN_DEVICES[mac] != board:
            raise ValueError('Connected device belongs to a different board; refusing flash')
    elif expected is None or mac != expected.lower():
        raise ValueError('Unknown device: verify its model and supply --expected-mac')
    if expected and mac != expected.lower():
        raise ValueError('Device MAC does not match --expected-mac')


def load_package(folder, board):
    m = json.loads((folder / 'manifest.json').read_text())
    if m['board'] != BOARDS[board] or m['chip'] != 'esp32s3' or m['review_mode']:
        raise ValueError('Wrong board or development-only firmware package')
    for entry in m['files']:
        p = (folder / entry['file']).resolve()
        if p.parent != folder.resolve() or digest(p) != entry['sha256']:
            raise ValueError('Package file missing, unsafe or checksum mismatch')
    return m


def build(board, output):
    if not os.environ.get('IDF_PATH'):
        raise ValueError('Source ESP-IDF export.sh before building')
    target = BOARDS[board]
    subprocess.run([sys.executable, 'scripts/build.py', target, '--name', target,
                    '--language', 'zh-CN'], cwd=ROOT, check=True)
    config = (ROOT / 'sdkconfig').read_text()
    symbol = 'CONFIG_BOARD_TYPE_' + target.upper().replace('-', '_').replace('.', '_')
    if symbol + '=y' not in config or 'CONFIG_ECHOEAR_UI_REVIEW=y' in config:
        raise ValueError('Unexpected build configuration')
    args = json.loads((ROOT / 'build/flasher_args.json').read_text())
    dest = output / board
    temp = output / (board + '.pending')
    if temp.exists():
        shutil.rmtree(temp)
    temp.mkdir(parents=True)
    manifest = {'board': target, 'chip': 'esp32s3', 'review_mode': False,
                'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)),
                'write_flash_args': args['write_flash_args'], 'files': []}
    for offset, name in args['flash_files'].items():
        source = (ROOT / 'build' / name).resolve()
        filename = offset + '-' + source.name
        shutil.copy2(source, temp / filename)
        manifest['files'].append({'offset': offset, 'file': filename,
                                  'app': offset == args['app']['offset'],
                                  'sha256': digest(temp / filename)})
    (temp / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    load_package(temp, board)
    if dest.exists():
        backup = output / (board + '.previous')
        if backup.exists():
            shutil.rmtree(backup)
        dest.rename(backup)
    temp.rename(dest)
    print('Package:', dest)


def flash(board, folder, port, full, expected):
    m = load_package(folder, board)
    command = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', port, '-b', '460800']
    identity = subprocess.check_output(command + ['read-mac'], text=True)
    macs = re.findall(r'MAC:\s*([0-9a-fA-F:]{17})', identity)
    if not macs:
        raise ValueError('Could not read device identity')
    validate_device(board, macs[-1], expected)
    files = m['files'] if full else [e for e in m['files'] if e['app']]
    if not files:
        raise ValueError('No firmware files in package')
    args = command + ['write-flash'] + m['write_flash_args']
    for entry in files:
        args.extend([entry['offset'], str(folder / entry['file'])])
    subprocess.run(args, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    b = commands.add_parser('build')
    b.add_argument('board', choices=[*BOARDS, 'all'])
    b.add_argument('--output', type=Path, default=ROOT / 'dist/echoear')
    f = commands.add_parser('flash')
    f.add_argument('board', choices=BOARDS)
    f.add_argument('--port', required=True)
    f.add_argument('--package', type=Path)
    f.add_argument('--full', action='store_true', help='Install bootloader, partition table and assets as well as app')
    f.add_argument('--expected-mac', help='Explicitly confirm an additional device after checking its physical model')
    a = parser.parse_args()
    lockdir = ROOT / 'dist/echoear'
    lockdir.mkdir(parents=True, exist_ok=True)
    with (lockdir / '.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if a.action == 'build':
            for board in BOARDS if a.board == 'all' else [a.board]:
                build(board, a.output.resolve())
        else:
            flash(a.board, (a.package or lockdir / a.board).resolve(), a.port, a.full, a.expected_mac)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
