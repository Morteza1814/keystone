# Pinned Keystone integration for LOC/gem5

The `keystone-host-integration` branch is based on Keystone commit
`a1842a1ec959c4e40ffd55091795bb577894c545`, the version used for the initial
gem5 full-system validation. The `sm` submodule uses
`https://github.com/Morteza1814/sm.git` at
`843d3509b8a514d43729d779a0cf6bdb1d0c94e0`. Retain these historical bases when
reproducing the validated setup; the forks' default branches may differ.

This directory owns compatibility patches needed for that version on the
prepared build host. It does not implement the monitor-to-LOC identity bridge
or protected host/PIM transfers.

## Prepare sources

From the Keystone checkout:

```bash
git submodule update --init sdk buildroot linux linux-keystone-driver sm
git -C sm submodule update --init opensbi
bash scripts/prepare-loc.sh
```

`prepare-loc.sh` requires the pinned SDK and Buildroot commits. It applies
`sdk-bootlin.patch` and copies the m4/fakeroot host-package patches into
Buildroot. It accepts patches already applied exactly and rejects conflicting
package-patch contents. The nested dependencies remain pinned to upstream
commits; their applied worktree changes are reproduced from this fork's patches.

The SDK patch disables stack-protector generation for freestanding enclave and
edge libraries/test applications, fixes an uninitialized test counter, captures
compiler-probe output in a CMake variable, and adds the Eyrie external-project
controls. The host SDK retains its compiler's
normal protection defaults. The top-level CMake build applies the existing
Linux/OpenSBI integration patches with `apply_patch_once.py`. It applies
`eyrie-bootlin.patch` during the Eyrie external project's patch step.

## Toolchain and output directories

The tested toolchains are Bootlin
`riscv64-lp64d--glibc--stable-2021.11-1` (GCC 10.3, glibc 2.34, Linux 5.4
headers) for Linux/SDK applications, and Ubuntu's RISC-V bare-metal GCC 10.2
for the monitor and freestanding programs. The Linux compiler must be available
through `riscv64-unknown-linux-gnu-*` wrapper names; the bare-metal tools use
`riscv64-unknown-elf-*`. Set `RISCV` to the Bootlin installation and include both
compiler families, CMake, make, rsync, patch and makeself on `PATH`. The prepared
makeself wrapper normalizes tar owner/group IDs for this shared host.

In the containing gem5 workspace, source `configs/keystone/env.sh` from its root.
That selects `ext/keystone` as the source, `build/keystone/tools` for local tools,
and these separate generated-output directories:

```bash
cmake -S "$KEYSTONE_SOURCE_DIR/sdk" -B "$KEYSTONE_SDK_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$KEYSTONE_SDK_BUILD_DIR" --target install --parallel 4
cmake -S "$KEYSTONE_SOURCE_DIR" -B "$KEYSTONE_BUILD_DIR" -DSM_PLATFORM=generic
cmake --build "$KEYSTONE_BUILD_DIR" --target tests --parallel 4
cmake --build "$KEYSTONE_BUILD_DIR" --target image --parallel 4
```

The defaults are `build/keystone/sdk`, `build/keystone/sdk-install` and
`build/keystone/image`. For a standalone checkout, choose equivalent absolute
source/build paths, export `KEYSTONE_SDK_DIR` for SDK installation, and supply
the compiler environment before running CMake. Use the `tests` and `image`
targets for gem5; the default `all` target also builds unrelated QEMU/bootrom
components. The containing gem5 README describes the guest overlay files and
simulation commands.

CMake defaults to this directory's patch helper and Eyrie patch. Its
`KEYSTONE_EYRIE_REVISION` defaults to
`44dda3a36f81a20eb8557ba02888c73bc3b0766f` and
`KEYSTONE_EYRIE_REPOSITORY` defaults to the upstream runtime repository; a local
mirror containing the pinned commit may be selected instead. Runtime updates
are disconnected after the initial clone. Changing runtime revisions requires
a fresh build and checking the runtime checkout's actual `HEAD`.

Buildroot downloads default to `${CMAKE_BINARY_DIR}/downloads`, overridable
through `KEYSTONE_DOWNLOAD_DIR`. Build caches contain absolute paths and must
be regenerated after a source/tool/output relocation. Keep simulation results
under the containing project's `experiment/keystone`, separate from sources
and generated builds.
