/* Bounded, sequential real-enclave sharing experiments. */
#ifndef LOC_SHARING_APP_H
#define LOC_SHARING_APP_H
#include <stdint.h>
#include <limits.h>
#include <gem5/m5ops.h>
#ifndef SHARE_KIND
#define SHARE_KIND 1
#endif
#ifndef SHARE_N
#define SHARE_N 32UL
#endif
#ifndef SHARE_TENANTS
#define SHARE_TENANTS 4
#endif
#ifndef SHARE_DUPLICATED
#define SHARE_DUPLICATED 0
#endif
#ifndef SHARE_DIAGNOSTIC
#define SHARE_DIAGNOSTIC 0
#endif
#if SHARE_TENANTS != 1 && SHARE_TENANTS != 2 && SHARE_TENANTS != 4
#error "Validated tenant counts are 1, 2, 4"
#endif
#if SHARE_KIND < 1 || SHARE_KIND > 3 || SHARE_N < 8 || SHARE_N > 65536
#error "Invalid sharing fixture"
#endif
#if SHARE_KIND == 1 && SHARE_N > 1024
#error "LoRA dimension exceeds the bounded fixture"
#endif
#if SHARE_DIAGNOSTIC && (SHARE_DUPLICATED || SHARE_TENANTS != 4)
#error "Selective-revocation diagnostic requires four shared tenants"
#endif
#define SHARE_RANK 8UL
#define SHARE_UTM 0x100000000UL
#define SHARE_UTM_BYTES 16384UL
#define SHARE_PASS 0x51c0UL
#define SHARE_INPUTS (SHARE_KIND == 2 ? 2UL : 1UL)
#define SHARE_PRIVATE (SHARE_KIND == 2 ? 2UL : 1UL)
#define SHARE_INPUT_BYTES (SHARE_KIND == 1 ? 4UL*SHARE_N*SHARE_N : SHARE_KIND == 2 ? 4UL*SHARE_N : 8UL*SHARE_N)
#define SHARE_OUTPUT_BYTES (SHARE_KIND == 3 ? (SHARE_N+7UL)/8UL : 4UL*SHARE_N)
/* Identical host arenas in shared and duplicated modes. Includes supplied
 * input layout, private query/adapter, output and five-neighbor workspace. */
#define SHARE_ARENA_BYTES (((SHARE_KIND == 1 ? 4UL*SHARE_N*SHARE_N+128UL*SHARE_N : 32UL*SHARE_N)+16383UL) & ~4095UL)
#define SHARE_FREE_BYTES (SHARE_ARENA_BYTES + 2UL*1024UL*1024UL)
enum share_action { SA_HELLO, SA_INIT, SA_LOAD, SA_GRANT, SA_ATTACH,
    SA_PROJECT, SA_REVOKE, SA_REVOKED, SA_RELEASE, SA_VERIFY,
    SA_FREE_HOST, SA_STOP, SA_DENIED };
enum share_phase { SP_BOOT=1, SP_SETUP, SP_CREATE, SP_ENTER, SP_ALLOC_HOST,
    SP_INITIALIZE, SP_ALLOC_PIM, SP_PREPARE, SP_UPLOAD, SP_COMPUTE,
    SP_DOWNLOAD, SP_COMBINE, SP_CHECK, SP_FREE_PIM, SP_FREE_HOST,
    SP_EXIT, SP_DESTROY, SP_ADAPTER, SP_GRANT, SP_REVOKE,
    SP_DENIALS, SP_SWITCH, SP_OWN_GRANT, SP_WINDOW_BEGIN, SP_WINDOW_END };
struct share_command { uint64_t action, tenant, principal, input[2], foreign; };
struct share_reply {
    uint64_t action, principal, input[2], workspace[2], checks, digest;
    uint64_t result[6];
};
static inline void sp_begin(unsigned p) { m5_work_begin(p,0); }
static inline void sp_end(unsigned p) { m5_work_end(p,0); }
static inline int32_t share_w(unsigned long r,unsigned long c) {
    return (int32_t)((r*7+c*11+(r*c)%19+(r>>3)*3+(c>>3)*7)%17)-8;
}
static inline int32_t share_x(unsigned long t,unsigned long c) {
    return (int32_t)((t*3+c*5+(c>>3)*3)%17)-8;
}
static inline int32_t share_a(unsigned long t,unsigned long i,unsigned long c) {
    return (int32_t)((t*5+i*3+c*7)%17)-8;
}
static inline int32_t share_b(unsigned long t,unsigned long r,unsigned long i) {
    return (int32_t)((t*7+r*5+i*11)%17)-8;
}
static inline int32_t share_bias(unsigned long t,unsigned long r) {
    return (int32_t)((t*2+r)%17)-8;
}
static inline int32_t share_feature(unsigned long i,unsigned long axis) {
    return (int32_t)((i*(axis ? 29UL : 17UL)+(i>>4)*3+axis*11)%1021)-510;
}
static inline int32_t share_query(unsigned long t,unsigned long axis) {
    return (int32_t)(t*31+axis*17)-47;
}
static inline uint64_t share_key(unsigned long i) {
    return (i*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407));
}
static inline uint64_t share_threshold(unsigned long t) {
    return UINT64_C(0x2000000000000000)*(t+1);
}
static inline uint64_t share_hash(const unsigned char *p,unsigned long n) {
    uint64_t h=UINT64_C(14695981039346656037);
    for(unsigned long i=0;i<n;++i) h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}
#endif
