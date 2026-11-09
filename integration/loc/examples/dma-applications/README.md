# Functional host/PIM applications and Linux baselines

The shared `workload.c` contains application algorithms, deterministic inputs
and independent reference checks. Thin `eapp.c` and `linux.c` wrappers provide
enclave and ordinary Linux lifecycles. All benchmark timing runs execute RV64
full-system guest instructions; a native build of `metadata.c` only describes
build-time constants and never supplies performance measurements.

The original four applications have three backends with identical arithmetic
and fixtures; downsampling, GEMM and GEMV currently have only the two PIM backends:

- `secure`: Keystone enclave, immutable LOC capabilities and protected DMA.
- `unsecured`: ordinary Linux process, object allocation and functional DMA,
  with no capability grants or enclave ownership checks.
- `cpu`: ordinary Linux process computes the complete application, without PIM
  allocation, DMA, fake transfers or offload calls.

The two PIM backends execute the same host algorithm source and PIM command
sequence. Their substantial host math lives in `host_kernels.c`, compiled
independently without backend or fixture macros and linked as the same object
in both binaries. Each function starts on a 64-byte boundary. This preserves
RV64IMAFDC while holding compressed-instruction fetch alignment constant across
the enclave and Linux wrappers; it does not guarantee identical cache state,
memory placement, or wrapper instruction costs. The build records the object
hash and checks final function alignment. CPU mode reuses point packing and
keeps its fused CPU algorithms.

PIM kernels produce actual bytes at PIMeval's analytical completion
time. CPU computations execute guest instructions. These are correctness and
timing fixtures, not trained-model accuracy or calibrated hardware studies.

The parent repository's build/run/report scripts consume `metadata.c`, compiled
natively with exactly the same macros as the corresponding RV64 binaries:

- `LOC_DMA_APP_BACKEND`: 1 (secure, default), 2 (unsecured), or 3 (CPU).
- `LOC_DMA_APP_KIND`: 1 (kNN), 2 (filter), 3 (max pooling), 4 (k-means),
  5 (downsampling), 6 (GEMM), or 7 (GEMV). Kinds 5..7 support secure/unsecured PIM only.
- `LOC_DMA_APP_ELEMENTS`: 8 to 1,048,576, default 128.
- `LOC_DMA_APP_ROUNDS`: 1 to 64, default 1 for GEMV and 3 for other workloads.
- `LOC_DMA_APP_GEMM_DIM`: required for GEMM, 3 to 256; its square must equal
  `LOC_DMA_APP_ELEMENTS`. Other applications do not use this macro.
- `LOC_DMA_APP_GEMV_COLUMNS`: required for GEMV, 1 to 1024. GEMV additionally
  limits rows to 65536, matrix elements to 1048576, and rounds to 32.

`metadata.c` prints the application, backend, timing profile, input specification,
memory requirements, arithmetic contract, expected transfer bytes/counts,
command/grant counts, and phase counts as JSON. The real guest also checks its
actual command/transfer/grant counts.
Every allocated host buffer has checked prefix/suffix guards.

| Workload | PIM work | Host application work | Meaning of elements/rounds |
| --- | --- | --- | --- |
| kNN | Two-dimensional Manhattan distances using scalar SUB, ABS and ADD | Pack feature columns; select five neighbors; vote on labels | Training points / distinct queries |
| Filter | UINT64 less-than producing a packed, LSB-first bitmap | Prepare thresholds; count and sum selected keys | Keys / threshold queries |
| Pool | Three signed INT32 MAX operations per 2x2 window | Pack image windows; reconstruct row-major output | Output windows / repeated executions on resident input |
| k-means | Two-dimensional squared Euclidean distances using scalar SUB, MUL and ADD | Assign clusters, accumulate coordinates, divide to update centroids | Points / fixed Lloyd-style iterations |
| Downsampling | Four UINT8 right shifts by two and three UINT8 additions | Pack grayscale 2x2 windows into four planes; reconstruct row-major output | Output pixels / repeated executions on resident input |
| GEMM | INT32 column-vector scaled accumulation | Transpose A/B into columns; reconstruct row-major C | Elements in each square matrix / repetitions of C=A*B |

For secure PIM, objects and capabilities are created once before application
stages. Capabilities are never rewritten; final free revokes them. The
unsecured backend creates the same objects without grants. Both PIM paths
use the same input residency/streaming policy and reuse scratch/output storage for the same
number of rounds. There is one application timing scope with component
breakdowns, without a separate amortized or first-use experiment. The kNN
labels remain in host memory and are never included in PIM distance computation.
Queries/centroids are scalar command arguments; no extra query-buffer DMA is claimed.

The kNN neighbor order is `(distance, training index)`; equal label-vote counts
prefer the smallest label. The independent CPU reference selects each neighbor
by rescanning all candidates, rather than reusing the PIM distances or host
insertion-list implementation.

Filter thresholds include zero, a value above INT64_MAX, and UINT64_MAX. The
application outputs are count and sum modulo 2^64. PIM uses an intermediate
bitmap of exactly `ceil(elements/8)` bytes, with unused high bits zero. CPU
mode directly aggregates comparisons without materializing that intermediate.
The reference checks every query's count/sum and the PIM path's final bitmap.

Pooling chooses the greatest output width at most eight that divides the
element count. Its input has twice the output height and width, uses stride two
without padding, and contains both negative and positive values. The reference
reads each 2x2 window directly from the original image and checks every pixel
of every round's reconstructed result. Repeated executions reuse the same
input; they are not distinct images. Output reconstruction is a real host copy
in this mapping and could be eliminated by a different layout.

Downsampling follows PIMbench's integer operation order: each of the four UINT8
pixels is shifted right by two before addition. It computes
`(a >> 2) + (b >> 2) + (c >> 2) + (d >> 2)`, which can differ from
`(a + b + c + d) / 4`. Values remain in 0..252. The input is one grayscale
raster, with twice the output height and width and no padding; BMP parsing,
RGB channels, image-file output and arbitrary scale factors are not modeled.
The output width is the largest divisor of N no greater than sqrt(N), so
4096 and 65536 elements represent 64x64 and 256x256 output images respectively.
Odd sizes such as 129 remain valid rectangular fixtures.

The four packed input planes upload once and remain unchanged in PIM. Six
objects hold those four planes plus result and scratch. Every round shifts
the first plane into the result, shifts each remaining plane into scratch,
and adds scratch into the result. Thus each round issues seven commands and
downloads N bytes. A real host copy reconstructs each round's output raster;
as with pooling, a different output mapping could eliminate that copy. The
reference reads the original raster, divides each sample by four with wider
sum arithmetic, checks all output pixels for every round, and checks that
host inputs/packed planes and surrounding guards remain intact. Explicit
windows cover 0/255, low-bit rounding and mixed values; the remainder of the
pattern includes high address bits. The private arena reserves its actual
`(9 + rounds) * N` payload bytes plus guard/alignment slack.

K-means uses three clusters, bounded integer coordinates, and squared Euclidean
distance. Centroid means truncate toward zero; empty clusters retain their
previous centroid, and equal distances prefer the smaller cluster ID. This
is an integer-centroid variant, not floating-point k-means. Bounded inputs
keep all PIM intermediates within INT32; host sums use INT64. A separate
point-major CPU reference checks every centroid update and final assignments.
The supplied fixture does not guarantee an empty cluster or exercise every
possible tie; those policies are explicit for reproducibility.

GEMM computes square `C=A*B` (`alpha=1`, `beta=0`) with signed INT32 operands;
it does not implement general floating-point BLAS. With dimension D, `elements`
is D squared. Both A and B start row-major in host memory and are transposed
once by the shared host kernel. For each output column, a zero vector uploads
to a PIM accumulator; each A column then uploads to a second PIM object and
is scaled by the corresponding B entry and accumulated. B stays in host memory
and its entries are scalar command arguments, not DMA payload. One AXPY LOC
command represents `Y=scalar*X+Y`; its existing PIMeval `pimScaledAdd` timing
models the multiply-scalar/add work. This follows PIMbench's streamed-column
GEMM mapping, with one AXPY command in place of separate multiply/add commands
and a zero-vector DMA in place of PIM broadcast initialization.

Only two PIM objects/capabilities are allocated, once, across all rounds. The
accumulator has upload, download, read, write and execute permissions because
it receives the zero vector and is both an operand and destination. Input
storage permits upload, read and execute. Each round issues D squared AXPY
commands, D*(D+1) uploads of 4*D bytes, and D downloads of 4*D bytes. A columns
are uploaded again for each output column and round; this is a streaming
baseline, not an optimized resident-matrix mapping. Output downloads form a
column-major matrix which a real host transpose reconstructs into row-major
storage for each round. All these packing, transfers, submissions and
reconstruction phases contribute to application time.

GEMM deterministic inputs span [-8,8]; D<=256 keeps every accumulated result
within INT32. An independent row-major INT64 dot-product reference checks every
element of every round after the application interval. It also checks both
original/packed inputs, the final downloaded matrix, the zero vector, and all
buffer guards. The private arena reserves its actual `4*(5+rounds)*D*D+4*D`
payload bytes plus alignment/guard slack. Initial validation sizes are 16x16
and 32x32, each with three rounds.

GEMV computes rectangular `y=A*x` (`alpha=1`, `beta=0`) using bounded signed
INT32 inputs. `elements` is the number of matrix rows/output vector elements;
`LOC_DMA_APP_GEMV_COLUMNS` is the number of columns/input vector elements.
The common host kernel packs the row-major matrix into columns once, using
16x16 tiles with contiguous stores within each column segment. This bounds
the active pages and reuses neighboring input cache lines. Conversion remains
inside the timed `host_prepare` application phase in both modes; input-value
initialization does not perform the conversion. Build metadata explicitly
records row-major input and the packing timing scope. Each
round uploads zero into the accumulator, streams each matrix column into the
second object, and executes `pimScaledAdd` through LOC AXPY with the private
host input-vector entry as its scalar. This follows the generic PIMbench GEMV
column mapping, including its scaled-add operation; zero initialization uses
DMA here in place of PIM broadcast. The matrix is unchanged across rounds,
while deterministic input vectors vary by round. No capability is modified.

For N rows, K columns and R rounds, GEMV uses two PIM objects, R*(K+1) uploads,
R downloads and R*K AXPY commands. Each transferred vector has 4*N bytes.
After every download, a shared host function copies the output to that round's
result storage before the download buffer is reused. This is an explicitly
timed result-materialization copy, not additional GEMV arithmetic; directing
DMA into final storage could eliminate it. Both backends use the same copy.
The independent INT64 reference reads the original row-major matrix and each
original input vector, checks all outputs of every round, verifies the final
download and packed/input buffers, and checks the surrounding guards.
Inputs span [-8,8] and K<=1024 keeps every partial sum within INT32.

GEMV accepts 8..65536 rows, 1..1024 columns, at most 1048576 matrix elements
and 1..32 rounds. Its six buffers occupy `4*(2*N*K+(R+2)*N+R*K)` bytes plus
guard/alignment slack. The current measured cases are 256x64 and 2048x64,
with one product per run (65 uploads, 64 AXPYs and one download at K=64).
The earlier three-product runs remain as historical validation evidence.
2048x64 matches the checked-in PIMbench code default and
is not asserted to be a paper evaluation size. GEMV currently supports only
the secure and unsecured PIM backends.

In secure mode, phases 1..7 cover boot through allocation/grants. Phases 8..12 mark real host
preparation, each upload, PIM submissions/waits, each download, and host
postprocessing; these stage IDs may repeat, sequentially and without nesting.
Phases 13..17 cover independent correctness checks, PIM free, host unmapping,
enclave exit, and destruction. Reference computation is outside application
timing. No native server `std::chrono` value is added to simulated time.

Secure mode uses the normal SDK lifecycle and existing Eyrie private
`mmap`/`munmap`. Its launcher receives a pass/fail code and does not perform
application arithmetic. No monitor or Eyrie source changes are required.

The unsecured Linux wrapper allocates a page-aligned arena with `mmap`, opens
`/dev/pim_pin`, and pins that arena with `PIM_PIN_IOCTL_PIN`. The ordinary
Linux helper module maintains physical page references until every transfer
and computation completes. Unpin happens in host-free before `munmap`/close;
this is not an enclave identity or capability service. CPU mode uses ordinary
`mmap`/`munmap` without opening the pin device.

Linux PIM uses `linux-pim-applications-v1`: IDs 1,2,5,6,7, work IDs 8..12,
then 13,14,15. It omits all enclave lifecycle markers and names ID 7
`pim_allocate`. CPU uses `linux-cpu-applications-v1`: IDs 1,2,5,6, meaningful
work stages 8/10, then 13,15. ID 10 is `host_compute`; no fake PIM or transfer
phases are emitted. CPU pooling has no packing preparation phase.

CPU algorithms fuse work where appropriate: kNN retains only five distances
while maintaining the neighbor list; filtering compares and aggregates in
one pass without a bitmap; pooling directly reads image windows without packing;
k-means finds the closest centroid and accumulates coordinate sums in one
point-major scan. kNN and k-means consume their prepared feature columns.
All results still pass independent reference checks outside application timing.

LoRA (`LOC_DMA_APP_KIND=8`) is a synthetic layer-level inference operator:
`y = W*x + B*(A*x) + bias`, rank 8, scale 1, one input vector per run.
W/x use the GEMV fixture; A/B and bias are signed deterministic INT32 values
in [-8,8]. For K<=1024, |Ax|<=64K, |B(Ax)|<=4096K and |y|<=4160K+8,
so all partial sums fit INT32. Host helpers additionally reject overflow
using checked INT64 arithmetic. The independent reference checks every output.

The base W*x reuses the two-object streamed-column GEMV mapping, including
one zero-vector upload, K weight-column uploads, K AXPYs and one download.
A/B, x and bias remain on the host; host_lora measures A*x followed by B*(Ax),
and host_finish measures scaling, bias and final combination. Both functions
run inside the same enclave in secure mode and as the same compiled host
kernels in Linux mode. All stages are sequential. Capabilities remain fixed
from setup through free/revocation. This version does not keep W resident.

Use `KEYSTONE_DMA_APPLICATION=lora` with the existing GEMV rows/columns
controls. `bash configs/keystone/run_lora_batch.sh NEW_TAG` automates native
ASan/UBSan checks, relevant regressions, a 64x64 full-system smoke pair and
512x512/1024x1024 secure/unsecured full-boot Timing runs. LeakSanitizer is
unavailable under the command sandbox's ptrace; the native harness checks
allocation release explicitly. Results are under experiment/keystone/lora/.
A trained model, quantization scales and model-quality claims are outside
this synthetic operator fixture.


### Persistent PIMeval placement with preformatted weights

Set `KEYSTONE_DMA_APP_LORA_RESIDENT=1` and `KEYSTONE_DMA_APP_LORA_PLACED=1`.
The manifest identifies `pimeval_placed_matrix_v2`. W is supplied column-major,
matching PIMbench GEMV's host input layout. No runtime transpose is performed;
this assumes an already-formatted model artifact. An upstream row-major model
conversion must be accounted for separately if required. The synthetic fixture
is generated directly in that declared layout; it is not a trained-layer test.

One LOC matrix object owns persistent associated PIMeval column allocations;
one separately protected associated accumulator receives each AXPY. Only two
LOC capabilities are created. All weights are uploaded through functional Timing
DMA in blocks of up to 16 columns before the first projection. x[j] remains a
scalar command argument. A/B and the final combination remain private host work.

Exported allocator regions provide the actual bank and bank-local row mapping.
DMA uploads/downloads and functional AXPY use the same immutable mapping.
PIMeval's existing associated-operand AXPY model supplies computation timing.
Every backend region reserves its full PIM row; logical byte bounds exclude
padding. Internal backend handles are not additional LOC capability objects.
The parent lifetime covers all regions and pending operations. Allocation and
mapping bookkeeping costs remain functional apart from existing LOC service
budgets; no calibrated allocator or placement-table hardware is claimed.

Legacy contiguous allocations and placed allocations cannot coexist in a run's
live allocation set; attempts fail rather than letting two allocators overlap.
This delivery supports single-enclave/Linux projections and same-owner reuse.
Multi-enclave sharing and new matrix commands are not implemented.
