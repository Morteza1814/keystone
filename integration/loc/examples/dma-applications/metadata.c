/* Compile natively with the same -D options as the guest binary. */
#include <stdio.h>
#include "metadata.h"

int
main(void)
{
#if LOC_DMA_APP_KIND == 6
    printf("{\"gemm_dimension\":%lu,", (unsigned long)LOC_DMA_APP_GEMM_DIM);
#elif LOC_DMA_APP_KIND == 7
    printf("{\"gemv_rows\":%lu,\"gemv_columns\":%lu,"
        "\"input_layout\":\"row_major\","
        "\"host_packing\":\"blocked_16x16_row_to_column\","
        "\"packing_timing_scope\":\"host_prepare_inside_application\",",
        (unsigned long)LOC_DMA_APP_ELEMENTS,
        (unsigned long)LOC_DMA_APP_GEMV_COLUMNS);
#else
    printf("{");
#endif
    printf("\"application\":\"%s\",\"backend\":\"%s\","
        "\"phase_profile\":\"%s\",\"compute_timing\":\"%s\","
        "\"input_spec\":\"deterministic-dma-applications-v1\","
        "\"elements\":%lu,\"rounds\":%lu,"
        "\"host_allocation_bytes\":%lu,\"freemem_bytes\":%lu,"
        "\"arithmetic\":\"%s\",\"expected\":{"
        "\"objects\":%lu,\"grants\":%lu,\"uploads\":%lu,"
        "\"downloads\":%lu,\"commands\":%lu,"
        "\"upload_bytes\":%lu,\"download_bytes\":%lu,\"phase_counts\":{",
        LOC_DMA_APP_NAME, LOC_DMA_APP_BACKEND_NAME,
        LOC_DMA_APP_PHASE_PROFILE, LOC_DMA_APP_COMPUTE_TIMING,
        (unsigned long)LOC_DMA_APP_ELEMENTS,
        (unsigned long)LOC_DMA_APP_ROUNDS,
        (unsigned long)LOC_DMA_APP_PRIVATE_BYTES,
        (unsigned long)LOC_DMA_APP_FREEMEM_BYTES, LOC_DMA_APP_ARITHMETIC,
        (unsigned long)LOC_DMA_APP_OBJECTS,
        (unsigned long)LOC_DMA_APP_GRANTS,
        (unsigned long)LOC_DMA_APP_UPLOADS,
        (unsigned long)LOC_DMA_APP_DOWNLOADS,
        (unsigned long)LOC_DMA_APP_COMMANDS,
        (unsigned long)LOC_DMA_APP_UPLOAD_BYTES,
        (unsigned long)LOC_DMA_APP_DOWNLOAD_BYTES);
#if LOC_DMA_APP_BACKEND == 3
#if LOC_DMA_APP_KIND != 3
    printf("\"host_prepare\":1,");
#endif
    printf("\"host_compute\":%lu},\"operation_counts\":{",
        (unsigned long)LOC_DMA_APP_COMPUTE_PHASES);
#else
    printf("\"host_prepare\":1,\"upload_inputs\":%lu,\"pim_compute\":%lu,"
        "\"download_results\":%lu,\"host_finish\":%lu},"
        "\"operation_counts\":{",
        (unsigned long)LOC_DMA_APP_UPLOADS,
        (unsigned long)LOC_DMA_APP_COMPUTE_PHASES,
        (unsigned long)LOC_DMA_APP_DOWNLOADS,
        (unsigned long)LOC_DMA_APP_HOST_FINISH_PHASES);
#if LOC_DMA_APP_KIND == 1
    printf("\"addInt32\":%lu,\"subScalarInt32\":%lu,\"absInt32\":%lu",
        (unsigned long)LOC_DMA_APP_ROUNDS,
        2UL * LOC_DMA_APP_ROUNDS, 2UL * LOC_DMA_APP_ROUNDS);
#elif LOC_DMA_APP_KIND == 2
    printf("\"ltScalarUint64Bool\":%lu", (unsigned long)LOC_DMA_APP_ROUNDS);
#elif LOC_DMA_APP_KIND == 3
    printf("\"maxInt32\":%lu", 3UL * LOC_DMA_APP_ROUNDS);
#elif LOC_DMA_APP_KIND == 4
    printf("\"addInt32\":%lu,\"subScalarInt32\":%lu,\"mulInt32\":%lu",
        3UL * LOC_DMA_APP_ROUNDS, 6UL * LOC_DMA_APP_ROUNDS,
        6UL * LOC_DMA_APP_ROUNDS);
#elif LOC_DMA_APP_KIND == 5
    printf("\"addUint8\":%lu,\"shiftBitsRightUint8\":%lu",
        3UL * LOC_DMA_APP_ROUNDS, 4UL * LOC_DMA_APP_ROUNDS);
#else
    printf("\"axpyInt32\":%lu", (unsigned long)LOC_DMA_APP_COMMANDS);
#endif
#endif
    puts("}}}");
    return 0;
}
