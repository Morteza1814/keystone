/* Stable IDs for secure and Linux application profiles. The Linux profiles
 * omit enclave stages; CPU mode maps ID 10 to host_compute and omits all
 * PIM allocation/transfer/free stages. Work stages may repeat sequentially. */
#ifndef KEYSTONE_DMA_APPLICATION_PHASES_H
#define KEYSTONE_DMA_APPLICATION_PHASES_H

#include <gem5/m5ops.h>
#include "metadata.h"

enum loc_dma_application_phase
{
    LOC_DA_BOOT = 1,
    LOC_DA_SETUP,
    LOC_DA_CREATE,
    LOC_DA_ENTER,
    LOC_DA_HOST_ALLOCATE,
    LOC_DA_INITIALIZE,
    LOC_DA_ALLOCATE_GRANT,
    LOC_DA_HOST_PREPARE,
    LOC_DA_UPLOAD,
    LOC_DA_COMPUTE,
    LOC_DA_DOWNLOAD,
    LOC_DA_HOST_FINISH,
    LOC_DA_CHECK,
    LOC_DA_FREE,
    LOC_DA_HOST_FREE,
    LOC_DA_EXIT,
    LOC_DA_DESTROY,
    LOC_DA_HOST_LORA
};

static inline void
phase_begin(enum loc_dma_application_phase phase)
{
    __asm__ volatile("" ::: "memory");
    m5_work_begin(phase, 0);
    __asm__ volatile("" ::: "memory");
}

static inline void
phase_end(enum loc_dma_application_phase phase)
{
    __asm__ volatile("" ::: "memory");
    m5_work_end(phase, 0);
    __asm__ volatile("" ::: "memory");
}

#endif
