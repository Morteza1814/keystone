/* Compile natively with the same -D options as the guest binary. */
#include <stdio.h>
#include "metadata.h"

int
main(void)
{
#if LOC_DMA_APP_KIND == 6
    printf("{\"gemm_dimension\":%lu,", (unsigned long)LOC_DMA_APP_GEMM_DIM);
#elif LOC_DMA_APP_KIND == 7 || LOC_DMA_APP_KIND == 8
    printf("{\"gemv_rows\":%lu,\"gemv_columns\":%lu,",
        (unsigned long)LOC_DMA_APP_ELEMENTS, (unsigned long)LOC_DMA_APP_GEMV_COLUMNS);
#if LOC_DMA_APP_LORA_PLACED
    printf("\"input_layout\":\"column_major\",\"host_packing\":\"none_preformatted_weights\","
        "\"packing_timing_scope\":\"not_required_for_declared_input_layout\",");
#else
    printf("\"input_layout\":\"row_major\",\"host_packing\":\"blocked_16x16_row_to_column\","
        "\"packing_timing_scope\":\"host_prepare_inside_application\",");
#endif
#else
    printf("{");
#endif
#if (LOC_DMA_APP_KIND >= 9 && LOC_DMA_APP_KIND <= 12) || \
    LOC_DMA_APP_KIND == 1 || LOC_DMA_APP_KIND == 2 || \
    LOC_DMA_APP_KIND == 3 || LOC_DMA_APP_KIND == 5
    printf("\"complete_pim_lifecycles\":%d,\"pim_lifecycles\":%lu,",
        LOC_DMA_APP_COMPLETE_LIFECYCLES, (unsigned long)LOC_DMA_APP_LIFECYCLES);
#endif
#if LOC_DMA_APP_KIND == 3 || LOC_DMA_APP_KIND == 5
    unsigned long width = 1;
    for (unsigned long candidate = 2; candidate <= LOC_DMA_APP_ELEMENTS / candidate; ++candidate)
        if (LOC_DMA_APP_ELEMENTS % candidate == 0) width = candidate;
    printf("\"image_output_width\":%lu,\"image_output_height\":%lu,"
        "\"image_input_elements\":%lu,\"image_element_bytes\":%lu,"
        "\"input_layout\":\"single_channel_row_major\","
        "\"packing_timing_scope\":\"host_prepare_inside_application\",",
        width, (unsigned long)LOC_DMA_APP_ELEMENTS / width,
        4UL * LOC_DMA_APP_ELEMENTS, (unsigned long)LOC_DMA_APP_ELEMENT_BYTES);
#endif
#if LOC_DMA_APP_KIND == 1 || LOC_DMA_APP_KIND == 2
    printf("\"request_policy\":\"%s\",",
        LOC_DMA_APP_COMPLETE_LIFECYCLES ? "fixed_request_per_lifecycle" : "vary_by_round");
#if LOC_DMA_APP_KIND == 1
    printf("\"knn_dimensions\":2,\"knn_k\":5,\"knn_distance\":\"manhattan\","
        "\"input_layout\":\"array_of_points_x_y_label\","
        "\"host_packing\":\"two_feature_vectors\","
        "\"distance_bound\":294,");
#else
    printf("\"key_bits\":64,\"result_layout\":\"packed_lsb_first_bitmap\","
        "\"sum_semantics\":\"modulo_2_64\",");
#endif
#endif
#if LOC_DMA_APP_KIND == 12
    printf("\"brightness_offset\":%ld,", LOC_DMA_APP_BRIGHTNESS_OFFSET);
#endif
#if LOC_DMA_APP_KIND == 10
    printf("\"axpy_scalar\":%ld,", LOC_DMA_APP_AXPY_SCALAR);
#endif
#if LOC_DMA_APP_KIND == 8
    printf("\"lora_lifecycle_warmup_rounds\":%lu,\"lora_lifecycles\":%lu,",
        (unsigned long)LOC_DMA_APP_LORA_LIFECYCLE_WARMUP_ROUNDS,
        (unsigned long)LOC_DMA_APP_LORA_LIFECYCLES);
    if (LOC_DMA_APP_LORA_LIFECYCLE_WARMUP_ROUNDS)
        printf("\"lora_lifecycle_measured_rounds\":%lu,",
            (unsigned long)(LOC_DMA_APP_ROUNDS - LOC_DMA_APP_LORA_LIFECYCLE_WARMUP_ROUNDS));
    printf("\"lora_warmup_rounds\":%lu,\"lora_measured_rounds\":%lu,",
        (unsigned long)LOC_DMA_APP_LORA_WARMUP_ROUNDS,
        (unsigned long)(LOC_DMA_APP_ROUNDS - LOC_DMA_APP_LORA_WARMUP_ROUNDS));
    if (LOC_DMA_APP_LORA_RESIDENT)
        printf("\"lora_weight_layout\":\"%s\",", LOC_DMA_APP_LORA_PLACED ?
            "pimeval_placed_matrix_v2" : "matrix_object_v1");
    printf("\"lora_rank\":%lu,\"lora_scale\":1,\"lora_resident\":%d,"
        "\"fixture_scope\":\"synthetic_layer_level_inference_operator\","
        "\"ax_bound\":%llu,\"bax_bound\":%llu,\"y_bound\":%llu,",
        (unsigned long)LOC_DMA_APP_LORA_RANK, LOC_DMA_APP_LORA_RESIDENT,
        LOC_DMA_APP_LORA_AX_BOUND, LOC_DMA_APP_LORA_BAX_BOUND,
        LOC_DMA_APP_LORA_Y_BOUND);
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
#if LOC_DMA_APP_KIND == 8
    printf("\"host_lora\":%lu,", (unsigned long)LOC_DMA_APP_ROUNDS);
#endif
    printf("\"host_prepare\":1,");
#endif
    printf("\"host_compute\":%lu},\"operation_counts\":{",
        (unsigned long)LOC_DMA_APP_COMPUTE_PHASES);
#else
#if LOC_DMA_APP_KIND == 8
    printf("\"host_lora\":%lu,", (unsigned long)LOC_DMA_APP_ROUNDS);
#endif
    if (LOC_DMA_APP_PREPARE_PHASES)
        printf("\"host_prepare\":%lu,", (unsigned long)LOC_DMA_APP_PREPARE_PHASES);
    printf("\"upload_inputs\":%lu,\"pim_compute\":%lu,"
        "\"download_results\":%lu",
        (unsigned long)LOC_DMA_APP_UPLOADS,
        (unsigned long)LOC_DMA_APP_COMPUTE_PHASES,
        (unsigned long)LOC_DMA_APP_DOWNLOADS);
    if (LOC_DMA_APP_HOST_FINISH_PHASES)
        printf(",\"host_finish\":%lu", (unsigned long)LOC_DMA_APP_HOST_FINISH_PHASES);
    printf("},\"operation_counts\":{");
#if LOC_DMA_APP_KIND == 9
    printf("\"addInt32\":%lu", (unsigned long)LOC_DMA_APP_ROUNDS);
#elif LOC_DMA_APP_KIND == 11
    printf("\"reluInt32\":%lu", (unsigned long)LOC_DMA_APP_ROUNDS);
#elif LOC_DMA_APP_KIND == 12
    printf("\"addScalarInt16\":%lu,\"minScalarInt16\":%lu,\"maxScalarInt16\":%lu",
        (unsigned long)LOC_DMA_APP_ROUNDS, (unsigned long)LOC_DMA_APP_ROUNDS,
        (unsigned long)LOC_DMA_APP_ROUNDS);
#elif LOC_DMA_APP_KIND == 1
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
