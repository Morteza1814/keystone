# Pinned Keystone integration for LOC/gem5

The `keystone-host-integration` branch is based on Keystone commit
`a1842a1ec959c4e40ffd55091795bb577894c545`, the version used for the initial
gem5 full-system validation. The `sm` submodule uses
`https://github.com/Morteza1814/sm.git` at
`0989261855cd768067f3b14bebc58a7cd002d8e8`. The initial CPU-isolation baseline
used monitor commit `843d3509b8a514d43729d779a0cf6bdb1d0c94e0`. Retain these
historical bases when reproducing earlier results; the forks' default branches
may differ.

This directory owns compatibility patches needed for that version on the
prepared build host. The pinned monitor also provides an opt-in LOC lifetime
identity platform, selected with `KEYSTONE_LOC=1` when building its generic
OpenSBI platform. The containing gem5 workspace's
`configs/keystone/build_identity.sh` builds this variant separately and its
`configs/keystone/README.md` documents the validated Atomic and Timing tests.
The default generic monitor remains available. The LOC platform also supplies
a destruction preflight: gem5 can refuse teardown while an analytical
CPU/cache transfer retains the private EPM. The monitor returns
`SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE` before changing the enclave state,
scrubbing memory or removing PMP protection, so the host can resume the
enclave and finish/cancel the stream before retrying. The generic monitor uses
a weak default preflight which permits normal destruction.

The containing workspace's `configs/keystone/build_transfers.sh` builds the
busy-aware monitor and a separate transfer guest. These CPU/cache streams keep
PIMeval analytical; they do not add numerical PIM payloads or DMA support.

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

## LOC application helpers

`include/loc_pim.h` and `app/loc_pim.c` provide a small RV64 application
library for LOC-enabled gem5 full-system runs. They use gem5's canonical
`m5ops.h` and RISC-V instruction stubs. The monitor supplies the active
enclave lifetime identity; applications provide only opaque object/request
handles. No LOC syscall, Eyrie modification or SDK submodule patch is needed.

From the parent gem5 checkout, build with:

```bash
bash configs/keystone/build_eyrie_app.sh
```

This installs `loc_pim.h`, the matching gem5 headers and
`lib/libkeystone-loc.a` under `build/keystone/eyrie-app/sdk/`. Add that
directory's `include` to an application's include path and link the library
alongside the normal Keystone application library. The build also extracts
Eyrie `44dda3a36f81a20eb8557ba02888c73bc3b0766f` from the prepared local
mirror, applies only the existing toolchain compatibility patch, and builds
the standard `freemem` runtime. Override `KEYSTONE_EYRIE_SOURCE_DIR` to use
another local repository containing that commit. Source stays under
`ext/keystone`; generated artifacts go under the parent `build/` directory.

The main interfaces are:

| Interface | Meaning |
| --- | --- |
| `loc_pim_principal` | Query the monitor-bound active lifetime; cannot set it. |
| `loc_pim_alloc/grant/free` | Own private device objects and set exact self permissions. |
| `loc_pim_check_buffer` | Validate the full private host range for a direction; moves no data. |
| `loc_pim_stream_*` | Admit, observe/commit, poll, reap or cancel a protected transfer. |
| `loc_pim_submit[_scalar]` | Submit a capability-checked analytical command. |
| `loc_pim_poll/wait` | Poll without consumption, or consume a completed command; both return pending without blocking the hart. |
| `loc_pim_complete` | Application-side polling loop that consumes the final result. |
| `loc_pim_upload` | Protected admission, real CPU reads, modeled device transfer, reap. |
| `loc_pim_download_placeholder` | Modeled device transfer, real CPU zero stores, commit, reap. |

Use `LOC_PIM_INPUT_PERMISSIONS` for READ + EXECUTE + COPY_TO and
`LOC_PIM_OUTPUT_PERMISSIONS` for WRITE + EXECUTE + COPY_FROM. Grants replace
permissions. A successful buffer check is not a reusable authorization;
stream admission checks again and retains private EPM/object ownership.
The check establishes page accessibility and region ownership, not exact
`malloc` bounds. Keep the admitted virtual mapping stable until completion.

`examples/vector-add/eapp.c` is an ordinary U-mode application using these
interfaces. It allocates its arrays in private enclave memory, uploads both
inputs, submits ADD, consumes the result, downloads and frees the objects.
Its output is zero analytical placeholder data: the test checks host input
integrity and output stores, rather than numerical vector sums. The library
preserves PIMeval's analytical mode; neither uploads nor downloads implement
functional payload movement.

The example's launcher adds lifecycle and negative tests around that flow:
two concurrent enclave lifetimes, private inputs spanning pages, rejected
buffers, foreign/stale handles, signed scalar submission, duplicate-byte
coverage, real Eyrie timer interruption/resume, and two busy destruction
attempts. The test-only Linux driver records EIDs, verifies EPM scrubbing,
and arms a real one-shot timer immediately before A resumes. The normal
monitor interrupt path stops A; the SDK resumes it. The application, SDK,
Eyrie and monitor follow their ordinary interfaces. The fixture enables synthetic 100 us
minimum device latencies to force pending commands; it is not a performance
measurement. See the parent's `configs/keystone/README.md` for run commands
and evidence verification.
