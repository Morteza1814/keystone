/* Bounded scan/aggregate operator with shared column partitions. */
#ifndef LOC_DATABASE_FILTER_H
#define LOC_DATABASE_FILTER_H
#include "sharing_app.h"
#ifndef DB_N
#define DB_N 65536UL
#endif
#ifndef DB_PARTS
#define DB_PARTS 8UL
#endif
#if DB_N < 64 || DB_N > 65536 || DB_PARTS < 1 || DB_PARTS > 128
#error "Database fixture exceeds validated bounds"
#endif
#if DB_N % DB_PARTS || (DB_N / DB_PARTS) % 8
#error "Partitions must contain a multiple of eight keys"
#endif
#define DB_TENANTS 4U
#define DB_KEYS_PER_PART (DB_N / DB_PARTS)
#define DB_INPUT_BYTES (DB_KEYS_PER_PART * 8UL)
#define DB_OUTPUT_BYTES (DB_KEYS_PER_PART / 8UL)
#define DB_MASK_BYTES (DB_N / 8UL)
#define DB_ARENA_BYTES ((8UL*DB_N + DB_MASK_BYTES + 128UL + 4095UL) & ~4095UL)
#define DB_FREE_BYTES (DB_ARENA_BYTES + 2UL*1024UL*1024UL)
#define DB_CHURN 32U
#define DB_PASS 0x51d0UL
enum db_action {
    DB_HELLO, DB_INIT, DB_LOAD, DB_GRANT, DB_ATTACH, DB_QUERY,
    DB_PRESSURE, DB_REVOKE, DB_REVOKED, DB_SURVIVE, DB_FREE_INPUT,
    DB_FREED, DB_CLEAR_PRESSURE, DB_FREE_PRIVATE, DB_VERIFY,
    DB_FREE_HOST, DB_STOP
};
struct db_command { uint64_t action, tenant, principal, input[DB_PARTS]; };
struct db_reply {
    uint64_t action, principal, input[DB_PARTS], workspace;
    uint64_t checks, digest, matches, sum;
};
static inline uint64_t db_key(unsigned long i) { return (i*37UL+11UL)%1000UL; }
static inline uint64_t db_threshold(unsigned long tenant) { return 100UL*(tenant+1); }
#endif
