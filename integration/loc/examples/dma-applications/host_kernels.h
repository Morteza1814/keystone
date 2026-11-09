/* Backend-independent host math. Link the exact same compiled object into
 * enclave and Linux binaries; do not compile this unit with workload macros.
 * Callers provide valid extents, labels/clusters 0..2 and at least five kNN
 * points. These are benchmark kernels, not an untrusted-input API. */
#ifndef LOC_APPLICATION_HOST_KERNELS_H
#define LOC_APPLICATION_HOST_KERNELS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct point
{
    int32_t x, y;
    uint32_t label;
};

void loc_host_pack_points(const struct point *points, int32_t *x, int32_t *y,
    size_t count);
uint32_t loc_host_knn_select_vote(const struct point *points,
    const int32_t *distances, unsigned long *neighbors, size_t count);
void loc_host_filter_aggregate(const uint64_t *keys, const unsigned char *mask,
    size_t count, uint64_t *matches, uint64_t *sum);
void loc_host_pool_pack(const int32_t *image, int32_t *const packed[4],
    size_t width, size_t height);
void loc_host_pool_reconstruct(const int32_t *output, int32_t *result,
    size_t count);
void loc_host_downsample_pack(const uint8_t *image, uint8_t *const packed[4],
    size_t width, size_t height);
void loc_host_downsample_reconstruct(const uint8_t *output, uint8_t *result,
    size_t count);
void loc_host_gemm_pack(const int32_t *a, const int32_t *b,
    int32_t *a_columns, int32_t *b_columns, size_t dimension);
void loc_host_gemm_reconstruct(const int32_t *columns, int32_t *result,
    size_t dimension);
void loc_host_gemv_pack(const int32_t *matrix, int32_t *columns,
    size_t rows, size_t column_count);
void loc_host_gemv_store_result(const int32_t *output, int32_t *result,
    size_t rows);
/* Returns zero on arithmetic overflow; intermediates use INT64 so rejection
 * never itself invokes signed overflow. Dimensions are benchmark-bounded. */
int loc_host_lora_adapter(const int32_t *a, const int32_t *b,
    const int32_t *x, int32_t *ax, int32_t *correction,
    size_t rows, size_t columns, size_t rank);
int loc_host_lora_combine(const int32_t *base, const int32_t *correction,
    const int32_t *bias, int32_t *result, size_t rows, int32_t scale);
void loc_host_kmeans_assign(const int32_t *distances, int32_t *best,
    uint32_t *assignment, size_t count, unsigned cluster);
void loc_host_kmeans_update(const struct point *points,
    const uint32_t *assignment, size_t count, int32_t centroids[3][2],
    int32_t *history);

#ifdef __cplusplus
}
#endif

#endif

void loc_host_gemv_pack_columns(const int32_t *matrix, int32_t *columns,
    size_t rows, size_t column_count, size_t first, size_t count);
