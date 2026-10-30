/* Shared measurement boundaries; IDs match the parent phase reporter. */
#ifndef KEYSTONE_LOC_VECTOR_TIMING_PHASES_H
#define KEYSTONE_LOC_VECTOR_TIMING_PHASES_H

#include <stdint.h>

#include <gem5/m5ops.h>

#ifndef LOC_VECTOR_ELEMENTS
#define LOC_VECTOR_ELEMENTS 16384UL
#endif
#define LOC_VECTOR_BYTES (LOC_VECTOR_ELEMENTS * sizeof(uint32_t))
#define LOC_VECTOR_PASS 0x4c4f4356UL

enum loc_vector_phase
{
    LOC_VECTOR_CREATE = 1,
    LOC_VECTOR_ENTER,
    LOC_VECTOR_INITIALIZE,
    LOC_VECTOR_ALLOCATE,
    LOC_VECTOR_UPLOAD_A,
    LOC_VECTOR_UPLOAD_B,
    LOC_VECTOR_ADD,
    LOC_VECTOR_DOWNLOAD,
    LOC_VECTOR_CHECK,
    LOC_VECTOR_FREE,
    LOC_VECTOR_EXIT,
    LOC_VECTOR_DESTROY
};

static inline void
phase_begin(enum loc_vector_phase phase)
{
    __asm__ volatile("" ::: "memory");
    m5_work_begin(phase, 0);
    __asm__ volatile("" ::: "memory");
}

static inline void
phase_end(enum loc_vector_phase phase)
{
    __asm__ volatile("" ::: "memory");
    m5_work_end(phase, 0);
    __asm__ volatile("" ::: "memory");
}

#endif
