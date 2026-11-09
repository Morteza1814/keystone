/* Compile once with RV64IMAFDC and no LTO; link the same object everywhere.
 * Fixed entry alignment keeps compressed-instruction fetch boundaries equal
 * between enclave/Linux wrappers. No backend, fixture-size or round macros
 * are accepted here, and no runtime/library functions are called. */
#include "host_kernels.h"

#define HOST_KERNEL __attribute__((noinline, aligned(64)))

HOST_KERNEL void
loc_host_pack_points(const struct point *points, int32_t *x, int32_t *y,
    size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        x[i] = points[i].x;
        y[i] = points[i].y;
    }
}

HOST_KERNEL uint32_t
loc_host_knn_select_vote(const struct point *points,
    const int32_t *distances, unsigned long *neighbors, size_t count)
{
    unsigned long best[5];
    for (unsigned k = 0; k < 5; ++k)
        best[k] = count;
    for (size_t i = 0; i < count; ++i) {
        unsigned at = 5;
        for (unsigned k = 0; k < 5; ++k) {
            if (best[k] == count || distances[i] < distances[best[k]] ||
                (distances[i] == distances[best[k]] && i < best[k])) {
                at = k;
                break;
            }
        }
        if (at < 5) {
            for (unsigned k = 4; k > at; --k)
                best[k] = best[k - 1];
            best[at] = i;
        }
    }
    unsigned long votes[3] = {0, 0, 0};
    for (unsigned k = 0; k < 5; ++k) {
        neighbors[k] = best[k];
        ++votes[points[best[k]].label];
    }
    uint32_t winner = 0;
    for (uint32_t c = 1; c < 3; ++c)
        if (votes[c] > votes[winner])
            winner = c;
    return winner;
}

HOST_KERNEL void
loc_host_filter_aggregate(const uint64_t *keys, const unsigned char *mask,
    size_t count, uint64_t *matches, uint64_t *sum)
{
    uint64_t selected = 0, total = 0;
    for (size_t i = 0; i < count; ++i) {
        if ((mask[i / 8] >> (i % 8)) & 1U) {
            ++selected;
            total += keys[i];
        }
    }
    *matches = selected;
    *sum = total;
}

HOST_KERNEL void
loc_host_pool_pack(const int32_t *image, int32_t *const packed[4],
    size_t width, size_t height)
{
    for (size_t row = 0; row < height; ++row) {
        for (size_t col = 0; col < width; ++col) {
            const size_t out = row * width + col;
            const size_t in = 4 * row * width + 2 * col;
            packed[0][out] = image[in];
            packed[1][out] = image[in + 1];
            packed[2][out] = image[in + 2 * width];
            packed[3][out] = image[in + 2 * width + 1];
        }
    }
}

HOST_KERNEL void
loc_host_pool_reconstruct(const int32_t *output, int32_t *result,
    size_t count)
{
    for (size_t i = 0; i < count; ++i)
        result[i] = output[i];
}

HOST_KERNEL void
loc_host_downsample_pack(const uint8_t *image, uint8_t *const packed[4],
    size_t width, size_t height)
{
    for (size_t row = 0; row < height; ++row) {
        for (size_t col = 0; col < width; ++col) {
            const size_t out = row * width + col;
            const size_t in = 4 * row * width + 2 * col;
            packed[0][out] = image[in];
            packed[1][out] = image[in + 1];
            packed[2][out] = image[in + 2 * width];
            packed[3][out] = image[in + 2 * width + 1];
        }
    }
}

HOST_KERNEL void
loc_host_downsample_reconstruct(const uint8_t *output, uint8_t *result,
    size_t count)
{
    for (size_t i = 0; i < count; ++i)
        result[i] = output[i];
}

HOST_KERNEL void
loc_host_gemm_pack(const int32_t *a, const int32_t *b,
    int32_t *a_columns, int32_t *b_columns, size_t dimension)
{
    for (size_t row = 0; row < dimension; ++row) {
        for (size_t col = 0; col < dimension; ++col) {
            a_columns[col * dimension + row] = a[row * dimension + col];
            b_columns[col * dimension + row] = b[row * dimension + col];
        }
    }
}

HOST_KERNEL void
loc_host_gemm_reconstruct(const int32_t *columns, int32_t *result,
    size_t dimension)
{
    for (size_t row = 0; row < dimension; ++row)
        for (size_t col = 0; col < dimension; ++col)
            result[row * dimension + col] = columns[col * dimension + row];
}

HOST_KERNEL void
loc_host_gemv_pack(const int32_t *matrix, int32_t *columns,
    size_t rows, size_t column_count)
{
    /* Keep the row-major conversion in the application ROI. A small tile
     * bounds the active source/destination pages instead of cycling through
     * every output column on every row. Inner stores fill contiguous column
     * segments; neighboring columns reuse the same source cache lines.
     * Both wrappers link this function from the same compiled object. */
    const size_t tile = 16;
    for (size_t row_base = 0; row_base < rows; row_base += tile) {
        const size_t row_end = rows - row_base < tile ? rows : row_base + tile;
        for (size_t col_base = 0; col_base < column_count; col_base += tile) {
            const size_t col_end = column_count - col_base < tile ?
                column_count : col_base + tile;
            for (size_t col = col_base; col < col_end; ++col) {
                int32_t *dst = columns + col * rows + row_base;
                for (size_t row = row_base; row < row_end; ++row)
                    *dst++ = matrix[row * column_count + col];
            }
        }
    }
}

HOST_KERNEL void
loc_host_gemv_store_result(const int32_t *output, int32_t *result,
    size_t rows)
{
    for (size_t row = 0; row < rows; ++row)
        result[row] = output[row];
}

HOST_KERNEL void
loc_host_kmeans_assign(const int32_t *distances, int32_t *best,
    uint32_t *assignment, size_t count, unsigned cluster)
{
    for (size_t i = 0; i < count; ++i) {
        if (cluster == 0 || distances[i] < best[i]) {
            best[i] = distances[i];
            assignment[i] = cluster;
        }
    }
}

HOST_KERNEL void
loc_host_kmeans_update(const struct point *points,
    const uint32_t *assignment, size_t count, int32_t centroids[3][2],
    int32_t *history)
{
    int64_t sums[3][2] = {{0, 0}, {0, 0}, {0, 0}};
    int64_t counts[3] = {0, 0, 0};
    for (size_t i = 0; i < count; ++i) {
        const unsigned c = assignment[i];
        sums[c][0] += points[i].x;
        sums[c][1] += points[i].y;
        ++counts[c];
    }
    for (unsigned c = 0; c < 3; ++c) {
        if (counts[c]) {
            centroids[c][0] = sums[c][0] / counts[c];
            centroids[c][1] = sums[c][1] / counts[c];
        }
        history[2 * c] = centroids[c][0];
        history[2 * c + 1] = centroids[c][1];
    }
}

/* With at most 1024 terms of INT32*INT32, a general caller could exceed
 * INT64. Check each addition too; fixture bounds guarantee the fast path. */
static int
lora_dot(const int32_t *a, const int32_t *x, size_t count, int32_t *out)
{
    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const int64_t product = (int64_t)a[i] * x[i];
        if ((product > 0 && sum > INT64_MAX - product) ||
            (product < 0 && sum < INT64_MIN - product))
            return 0;
        sum += product;
    }
    if (sum < INT32_MIN || sum > INT32_MAX)
        return 0;
    *out = (int32_t)sum;
    return 1;
}

HOST_KERNEL int
loc_host_lora_adapter(const int32_t *a, const int32_t *b,
    const int32_t *x, int32_t *ax, int32_t *correction,
    size_t rows, size_t columns, size_t rank)
{
    for (size_t i = 0; i < rank; ++i)
        if (!lora_dot(a + i * columns, x, columns, ax + i))
            return 0;
    for (size_t row = 0; row < rows; ++row)
        if (!lora_dot(b + row * rank, ax, rank, correction + row))
            return 0;
    return 1;
}

HOST_KERNEL int
loc_host_lora_combine(const int32_t *base, const int32_t *correction,
    const int32_t *bias, int32_t *result, size_t rows, int32_t scale)
{
    for (size_t row = 0; row < rows; ++row) {
        const int64_t value = (int64_t)base[row] +
            (int64_t)scale * correction[row] + bias[row];
        if (value < INT32_MIN || value > INT32_MAX)
            return 0;
        result[row] = (int32_t)value;
    }
    return 1;
}

/* Bounded packing workspace: source stays row-major, destination holds count columns. */
HOST_KERNEL void
loc_host_gemv_pack_columns(const int32_t *matrix, int32_t *columns,
    size_t rows, size_t column_count, size_t first, size_t count)
{
    for (size_t rb = 0; rb < rows; rb += 16) {
        const size_t end = rows - rb < 16 ? rows : rb + 16;
        for (size_t col = 0; col < count; ++col)
            for (size_t row = rb; row < end; ++row)
                columns[col * rows + row] = matrix[row * column_count + first + col];
    }
}
