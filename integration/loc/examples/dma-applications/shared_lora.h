/* Synthetic two-enclave LoRA fixture. Shared data is control/test output only. */
#ifndef LOC_SHARED_LORA_H
#define LOC_SHARED_LORA_H
#ifndef LOC_DMA_APP_VERIFY_RESULTS
#define LOC_DMA_APP_VERIFY_RESULTS 1
#endif
#if LOC_DMA_APP_VERIFY_RESULTS != 0 && LOC_DMA_APP_VERIFY_RESULTS != 1
#error "LOC_DMA_APP_VERIFY_RESULTS must be 0 or 1"
#endif
#include <stdint.h>
#include <limits.h>
#include <gem5/m5ops.h>
#ifndef SHARED_N
#define SHARED_N 32UL
#endif
#define SHARED_RANK 8UL
#ifndef SHARED_BENCHMARK
#define SHARED_BENCHMARK 0
#endif
#ifndef SHARED_DUPLICATED
#define SHARED_DUPLICATED 0
#endif
/* Matched performance modes allocate identical private host buffers. Only
 * PIM residency/upload multiplicity changes. Diagnostic mode stays unchanged. */
#define SHARED_HOST_WEIGHTS(t) (SHARED_BENCHMARK || (t) == 0)
#define SHARED_OWNS_WEIGHTS(t) (SHARED_DUPLICATED || (t) == 0)
#define SHARED_UTM 0x100000000UL
#define SHARED_UTM_BYTES 16384UL
#define SHARED_PASS 0x51a0UL
#define SHARED_VECTOR_BYTES (4UL * SHARED_N)
#define SHARED_WEIGHT_BYTES (SHARED_VECTOR_BYTES * SHARED_N)
#define SHARED_PRIVATE_BYTES ((4UL * (4UL * SHARED_N + 2UL * SHARED_N * SHARED_RANK + SHARED_RANK) + 8191UL) & ~4095UL)
#define SHARED_ARENA_BYTES(owner) (SHARED_PRIVATE_BYTES + ((owner) ? SHARED_WEIGHT_BYTES : 0))
#define SHARED_FREE_BYTES(owner) (SHARED_ARENA_BYTES(owner) + 2UL * 1024UL * 1024UL)
#if SHARED_N < 8 || SHARED_N > 1024
#error "First shared fixture supports 8..1024 rows/columns"
#endif
#if (64UL * SHARED_N + 512UL * SHARED_N * SHARED_RANK + 8UL) > INT32_MAX
#error "Synthetic intermediate/output bound exceeds INT32"
#endif

enum shared_action {
    SH_HELLO, SH_INIT, SH_LOAD, SH_GRANT, SH_ATTACH, SH_PROJECT,
    SH_DENIED, SH_REVOKE, SH_REVOKED, SH_RELEASE, SH_STOP,
    SH_BEFORE_GRANT, SH_STALE
};
enum shared_phase {
    SH_BOOT=1, SH_SETUP, SH_CREATE, SH_ENTER, SH_ALLOC_HOST, SH_INITIALIZE,
    SH_ALLOC_PIM, SH_PACK, SH_UPLOAD, SH_COMPUTE, SH_DOWNLOAD, SH_COMBINE,
    SH_CHECK, SH_FREE_PIM, SH_FREE_HOST, SH_EXIT, SH_DESTROY, SH_ADAPTER,
    SH_SHARE, SH_UNSHARE, SH_DENIALS, SH_SWITCH
};
struct shared_command {
    uint64_t action, tenant, principal, weights, accumulator, round;
};
struct shared_reply {
    uint64_t action, principal, weights, accumulator, checks, round;
    int32_t output[SHARED_N];
};
static inline void sh_begin(unsigned phase) { m5_work_begin(phase, 0); }
static inline void sh_end(unsigned phase) { m5_work_end(phase, 0); }
static inline int32_t sh_w(unsigned long row, unsigned long col) {
    return (int32_t)((row * 7 + col * 11 + (row * col) % 19 + (row >> 3) * 3 + (col >> 3) * 7) % 17) - 8;
}
static inline int32_t sh_x(unsigned long tenant, unsigned long round, unsigned long col) {
    return (int32_t)((tenant * 3 + round * 13 + col * 5 + (round * col) % 11 + (col >> 3) * 3) % 17) - 8;
}
static inline int32_t sh_a(unsigned long tenant, unsigned long i, unsigned long col) {
    return (int32_t)((tenant * 5 + i * 3 + col * 7) % 17) - 8;
}
static inline int32_t sh_b(unsigned long tenant, unsigned long row, unsigned long i) {
    return (int32_t)((tenant * 7 + row * 5 + i * 11) % 17) - 8;
}
static inline int32_t sh_bias(unsigned long tenant, unsigned long row) {
    return (int32_t)((tenant * 2 + row) % 17) - 8;
}
#endif
