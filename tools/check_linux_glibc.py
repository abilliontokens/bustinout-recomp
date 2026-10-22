#!/usr/bin/env python3
"""Inspect a staged Linux package's ELF glibc needs without running its code."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def version(text):
    if not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", text):
        raise argparse.ArgumentTypeError("expected a version such as 2.43")
    parts = tuple(map(int, text.split(".")))
    return parts + (0,) * (3 - len(parts))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--max", type=version, dest="maximum")
    args = parser.parse_args()
    if not args.package.is_dir():
        parser.error("package must be an existing directory")
    failed = False
    count = 0
    for path in sorted(args.package.rglob("*")):
        if not path.is_file():
            continue
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        count += 1
        result = subprocess.run(
            ["readelf", "--version-info", "--wide", str(path)],
            text=True, capture_output=True, check=True,
            env={**os.environ, "LC_ALL": "C"},
        )
        needs = result.stdout.partition("Version needs section")[2]
        versions = re.findall(r"\bName: GLIBC_([0-9]+\.[0-9]+(?:\.[0-9]+)?)\b", needs)
        highest = max(versions, key=version) if versions else None
        print(f"{path.relative_to(args.package)}: "
              f"{('GLIBC_' + highest) if highest else 'no numeric glibc requirement'}")
        if highest and args.maximum and version(highest) > args.maximum:
            print(f"error: {path} exceeds the requested glibc baseline", file=sys.stderr)
            failed = True
    if not count:
        parser.error("package contains no ELF files")
    return int(failed)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
