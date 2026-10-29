#!/usr/bin/env bash
# Prepare the pinned upstream components with fork-owned compatibility patches.
set -euo pipefail
keystone_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
for component in sdk:24a5ed369ac0606aac486382a0855a459d9362ed buildroot:9624a001f9d4314f8bcc9219df717d4c41fb5373; do
    path=${component%%:*}
    revision=${component#*:}
    actual=$(git -C "$keystone_source/$path" rev-parse HEAD)
    if [ "$actual" != "$revision" ]; then
        printf 'Unexpected %s revision: %s (expected %s)\n' "$path" "$actual" "$revision" >&2
        exit 1
    fi
done
cd "$keystone_source/sdk"
python3 "$keystone_source/integration/loc/apply_patch_once.py" \
    "$keystone_source/integration/loc/sdk-bootlin.patch"
python3 - "$keystone_source" <<'PYTHON'
from pathlib import Path
import shutil
import sys
root = Path(sys.argv[1])
for source in sorted((root / "integration/loc/buildroot").glob("*/*.patch")):
    target = root / "buildroot/package" / source.parent.name / source.name
    if target.exists() and target.read_bytes() != source.read_bytes():
        raise SystemExit(f"Refusing to overwrite a different package patch: {target}")
    shutil.copy2(source, target)
    print("Prepared package patch:", target.relative_to(root))
PYTHON
