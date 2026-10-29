#!/usr/bin/env python3
"""Apply a dependency patch, or verify that the exact patch is already applied."""

import argparse
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strip", type=int, choices=range(10), default=1)
    parser.add_argument("patch", type=Path)
    args = parser.parse_args()
    patch = str(args.patch.resolve(strict=True))
    strip = f"-p{args.strip}"
    forward = subprocess.run(
        ["git", "apply", strip, "--check", patch],
        capture_output=True,
        text=True,
    )
    if forward.returncode == 0:
        subprocess.run(["git", "apply", strip, patch], check=True)
        print("Applied dependency patch:", patch)
        return 0
    reverse = subprocess.run(
        ["git", "apply", strip, "--reverse", "--check", patch],
        capture_output=True,
        text=True,
    )
    if reverse.returncode == 0:
        print("Dependency patch already applied:", patch)
        return 0
    sys.stderr.write(forward.stderr + reverse.stderr)
    print(
        "Dependency patch neither applicable nor already applied",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
