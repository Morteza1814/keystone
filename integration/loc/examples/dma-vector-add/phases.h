/* Phase IDs match the parent's dma-vector-add-v1 reporting profile. */
#ifndef KEYSTONE_DMA_VECTOR_PHASES_H
#define KEYSTONE_DMA_VECTOR_PHASES_H

#include <stdint.h>

#include <gem5/m5ops.h>

#ifndef LOC_DMA_VECTOR_ELEMENTS
#define LOC_DMA_VECTOR_ELEMENTS 16384UL
#endif
#define LOC_DMA_VECTOR_BYTES (LOC_DMA_VECTOR_ELEMENTS * sizeof(uint32_t))
#define LOC_DMA_VECTOR_STRIDE \
    (((LOC_DMA_VECTOR_BYTES + 8192UL + 4095UL) / 4096UL) * 4096UL)
#define LOC_DMA_VECTOR_PRIVATE_BYTES (3UL * LOC_DMA_VECTOR_STRIDE)
#define LOC_DMA_VECTOR_FREEMEM_BYTES \
    (LOC_DMA_VECTOR_PRIVATE_BYTES + 512UL * 1024UL)
#define LOC_DMA_VECTOR_UTM 0x10000000UL
#define LOC_DMA_VECTOR_PASS 0x4c445641UL

enum loc_dma_vector_phase
{
    LOC_DV_BOOT = 1,
    LOC_DV_SETUP,
    LOC_DV_CREATE,
    LOC_DV_ENTER,
    LOC_DV_HOST_ALLOCATE,
    LOC_DV_INITIALIZE,
    LOC_DV_ALLOCATE_GRANT,
    LOC_DV_UPLOAD_A,
    LOC_DV_UPLOAD_B,
    LOC_DV_ADD,
    LOC_DV_DOWNLOAD,
    LOC_DV_CHECK,
    LOC_DV_FREE,
    LOC_DV_HOST_FREE,
    LOC_DV_EXIT,
    LOC_DV_DESTROY
};

static inline void
phase_begin(enum loc_dma_vector_phase phase)
{
    __asm__ volatile("" ::: "memory");
    m5_work_begin(phase, 0);
    __asm__ volatile("" ::: "memory");
}

static inline void
phase_end(enum loc_dma_vector_phase phase)
{
    __asm__ volatile("" ::: "memory");
    m5_work_end(phase, 0);
    __asm__ volatile("" ::: "memory");
}

#endif
