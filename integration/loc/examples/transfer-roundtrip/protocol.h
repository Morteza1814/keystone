#ifndef LOC_DMA_ROUNDTRIP_PROTOCOL_H
#define LOC_DMA_ROUNDTRIP_PROTOCOL_H

#include <stdint.h>

#ifndef LOC_DMA_BYTES
#define LOC_DMA_BYTES 65536UL
#endif

#define LOC_DMA_MAGIC 0x4c444d41UL
#define LOC_DMA_OCALL 1UL
#define LOC_DMA_UTM 0x10000000UL
#define LOC_DMA_UTM_BYTES 4096UL
#define LOC_DMA_CASES 3UL
#define LOC_DMA_TRANSFERS_PER_CASE 7UL
#define LOC_DMA_BUFFER_STRIDE \
    (((LOC_DMA_BYTES + 8320UL + 4095UL) / 4096UL) * 4096UL)
#define LOC_DMA_PRIVATE_BYTES (3UL * LOC_DMA_BUFFER_STRIDE)
#define LOC_DMA_FREEMEM_BYTES (LOC_DMA_PRIVATE_BYTES + 512UL * 1024UL)

struct loc_dma_message
{
    uint64_t kind, case_id, sequence, bytes, cycles, principal, object, checks;
};

enum { LOC_DMA_UPLOAD = 0, LOC_DMA_DOWNLOAD = 1, LOC_DMA_DONE = 2 };

#endif
