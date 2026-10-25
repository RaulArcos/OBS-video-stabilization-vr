#!/usr/bin/env python3
"""Zip the staged plugin (release/staging/<name>) into release/<name>-<version>-windows-x64.zip
plus a .sha256 file. Name and version come from buildspec.json; nothing is built or installed."""
import argparse
import hashlib
import json
import os
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = ['bin/64bit/obs-vr-stabilizer.dll', 'data/shaders/luma-downsample.effect',
            'data/shaders/warp.effect', 'data/locale/en-US.ini', 'data/locale/es-ES.ini']
MAX_FILES, MAX_BYTES = 64, 256 * 1024 * 1024


def rel(path):
    return path.relative_to(ROOT).as_posix()


def linked(path):
    return path.is_symlink() or os.path.isjunction(path)


def collect(spec):
    """Return (errors, {archive name: path}) without touching any output."""
    name, version = spec['name'], spec['version']
    stage = ROOT / 'release' / 'staging' / name
    errors, files = [], {}
    for path in (stage.parent, stage):
        if linked(path):
            errors.append(f'refusing symlink: {rel(path)}')
    if errors:
        return errors, files
    for base, dirs, names in os.walk(stage):
        for entry in sorted(dirs + names):
            path = Path(base, entry)
            if linked(path):
                errors.append(f'refusing symlink: {rel(path)}')
            elif path.is_file() and path.suffix != '.pdb':
                files[f'{name}/{path.relative_to(stage).as_posix()}'] = path
    wanted = [(f'{name}/{r}', stage / r, True) for r in REQUIRED]
    wanted += [(r, ROOT / r, True) for r in ('README.md', 'LICENSE')]
    wanted += [(r, ROOT / r, False) for r in ('docs/INSTALL.md', f'docs/RELEASE-{version}.md', 'docs/demo.gif')]
    for arc, path, required in wanted:
        if linked(path):
            errors.append(f'refusing symlink: {rel(path)}')
        elif not path.is_file():
            if required:
                errors.append(f'missing required file: {rel(path)}')
        elif path.stat().st_size == 0:
            errors.append(f'empty required file: {rel(path)}')
        else:
            files[arc] = path
    if len(files) > MAX_FILES or sum(p.stat().st_size for p in files.values()) > MAX_BYTES:
        errors.append(f'staged files exceed {MAX_FILES} files or {MAX_BYTES} bytes')
    return sorted(set(errors)), files


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    try:
        spec = json.loads((ROOT / 'buildspec.json').read_text(encoding='utf-8'))
        errors, files = collect(spec)
    except (OSError, ValueError, KeyError) as exc:
        errors, files = [f'cannot read buildspec.json: {exc}'], {}
    if errors:
        for error in errors:
            print(f'package_release: {error}', file=sys.stderr)
        return 1
    out = ROOT / 'release'
    zip_path = out / f"{spec['name']}-{spec['version']}-windows-x64.zip"
    sum_path = zip_path.with_name(zip_path.name + '.sha256')
    out.mkdir(exist_ok=True)
    fd, tmp_zip = tempfile.mkstemp(dir=out, suffix='.tmp')
    os.close(fd)
    tmp_sum = tmp_zip + '.sha256'
    try:
        with zipfile.ZipFile(tmp_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
            for arc in sorted(files):
                archive.write(files[arc], arc)
        digest = hashlib.sha256(Path(tmp_zip).read_bytes()).hexdigest()
        Path(tmp_sum).write_bytes(f'{digest}  {zip_path.name}\n'.encode())
        os.replace(tmp_zip, zip_path)
        os.replace(tmp_sum, sum_path)
    finally:
        for tmp in (tmp_zip, tmp_sum):
            if os.path.exists(tmp):
                os.unlink(tmp)
    print(f'wrote {rel(zip_path)}\nwrote {rel(sum_path)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
