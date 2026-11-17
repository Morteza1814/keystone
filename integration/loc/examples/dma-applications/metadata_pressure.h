/* Full-system metadata-locality fixture; no synthetic tenant identities. */
#ifndef LOC_METADATA_PRESSURE_H
#define LOC_METADATA_PRESSURE_H
#include <stdint.h>
#include <gem5/m5ops.h>
#ifndef PRESSURE_TENANTS
#define PRESSURE_TENANTS 4
#endif
#ifndef PRESSURE_CASES
#define PRESSURE_CASES 8,16,32,64,128
#endif
#ifndef PRESSURE_WARMUPS
#define PRESSURE_WARMUPS 5
#endif
#ifndef PRESSURE_MEASURED
#define PRESSURE_MEASURED 16
#endif
#define PRESSURE_MAX_OBJECTS 128UL
#define PRESSURE_CALLS 128UL
#define PRESSURE_BYTES 64UL
#define PRESSURE_PASS 0x51b0UL
#define PRESSURE_UTM 0x100000000UL
#define PRESSURE_UTM_BYTES 16384UL
#ifndef PRESSURE_REVOCATION
#define PRESSURE_REVOCATION 0
#endif
#define PRESSURE_CHURN 96UL
#if PRESSURE_TENANTS < 2 || PRESSURE_TENANTS > 4
#error "Validate the pinned Keystone PMP limits before extending beyond four enclaves"
#endif
enum pressure_action { MP_HELLO, MP_INIT, MP_ALLOC, MP_OWNER_GRANT, MP_UPLOAD,
    MP_ATTACH, MP_GRANT, MP_ACCESS, MP_REVOKE, MP_DENIED, MP_FREE,
    MP_HOST_FREE, MP_STOP, MP_VERIFY, MP_CHURN, MP_CHURN_FREE, MP_AUDIT };
enum pressure_phase { MP_BOOT=1, MP_SETUP, MP_CREATE, MP_ENTER, MP_HOST_ALLOC,
    MP_INITIALIZE, MP_PIM_ALLOC, MP_UNUSED8, MP_LOAD, MP_PROBE, MP_UNUSED11,
    MP_UNUSED12, MP_CHECK, MP_PIM_FREE, MP_RELEASE_HOST, MP_EXIT, MP_DESTROY,
    MP_UNUSED18, MP_SHARE, MP_UNSHARE, MP_NEGATIVE, MP_SWITCH, MP_OWNER_CAP,
    MP_WINDOW_BEGIN, MP_WINDOW_END, MP_CONDITION, MP_CLEANUP };
struct pressure_command {
    uint64_t action, tenant, principal, count, round, variant;
    uint64_t objects[PRESSURE_MAX_OBJECTS];
};
struct pressure_reply {
    uint64_t action, principal, count, calls, checks, round;
    uint64_t objects[PRESSURE_MAX_OBJECTS];
};
static inline void mp_begin(unsigned phase) { m5_work_begin(phase,0); }
static inline void mp_end(unsigned phase) { m5_work_end(phase,0); }
static inline uint32_t mp_value(unsigned object,unsigned word) {
    return 0x12340000U + object*37U + word*11U;
}
#endif
