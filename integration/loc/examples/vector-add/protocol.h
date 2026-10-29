#ifndef KEYSTONE_LOC_VECTOR_PROTOCOL_H
#define KEYSTONE_LOC_VECTOR_PROTOCOL_H

#include <stdint.h>

#define LOC_APP_OCALL 1UL
#define LOC_APP_MAGIC 0x4c4f4355UL
#define LOC_APP_ELEMENTS 256UL
#define LOC_APP_BYTES (LOC_APP_ELEMENTS * sizeof(uint32_t))
#define LOC_APP_UTM 0x10000000UL
#define LOC_APP_UTM_BYTES 4096UL
#define LOC_APP_CHECKS 0x3fUL

enum loc_app_phase
{
    LOC_APP_HELLO,
    LOC_APP_STREAM,
    LOC_APP_COMMAND,
    LOC_APP_DONE
};

struct loc_app_message
{
    uintptr_t phase;
    uintptr_t principal;
    uintptr_t object;
    uintptr_t stream;
    uintptr_t command;
    uintptr_t checks;
    uintptr_t elapsed;
};

/* Test metadata only. Receiving a handle never conveys LOC authority. */
struct loc_app_reply
{
    uintptr_t magic;
    uintptr_t role;
    uintptr_t foreign_object;
    uintptr_t foreign_stream;
    uintptr_t foreign_command;
};

#endif
