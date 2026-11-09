/* Shared application and independent reference checks.
 * Included by the enclave and Linux wrappers so the two PIM variants execute
 * the same host algorithms and submit the same data/commands. Backend 3 uses
 * fused CPU algorithms and contains no DMA or PIM pseudo-instruction calls. */
#include "host_kernels.h"

#define N LOC_DMA_APP_ELEMENTS
#define R LOC_DMA_APP_ROUNDS
#define GUARD_BYTES 64UL
#define WORKSPACE_PERMISSIONS 0x0bULL
#define RESULT_PERMISSIONS 0x1bULL

struct buffer_record
{
    volatile unsigned char *data;
    unsigned long bytes;
};

static struct buffer_record buffers[16];
static unsigned long buffer_count, arena_used;
static uintptr_t arena;
#if LOC_DMA_APP_BACKEND != 3
static loc_pim_object objects[LOC_DMA_APP_OBJECTS];
#endif
static unsigned long object_count, grants, uploads, downloads, commands;
static unsigned long upload_bytes, download_bytes;

static void
require(int condition, unsigned long error)
{
    if (!condition)
        application_fail(error);
}

static void *
new_buffer(unsigned long bytes)
{
    const unsigned long offset = (arena_used + GUARD_BYTES + 63UL) & ~63UL;
    require(bytes && buffer_count < 16 &&
        offset + bytes + GUARD_BYTES <= LOC_DMA_APP_PRIVATE_BYTES, 0xfa02);
    void *data = (void *)(arena + offset);
    buffers[buffer_count].data = data;
    buffers[buffer_count++].bytes = bytes;
    arena_used = offset + bytes + GUARD_BYTES;
    return data;
}

static void
initialize_buffers(void)
{
    for (unsigned long b = 0; b < buffer_count; ++b) {
        volatile unsigned char *data = buffers[b].data;
        for (unsigned long i = 0; i < GUARD_BYTES; ++i) {
            data[(long)i - (long)GUARD_BYTES] = 0xd3;
            data[buffers[b].bytes + i] = 0xd3;
        }
#if LOC_DMA_APP_LORA_RESIDENT
        /* Same fixture bytes/cache lines, fewer guest instructions outside ROI. */
        typedef uint64_t init_word __attribute__((__may_alias__));
        unsigned long i = 0;
        for (; buffers[b].bytes - i >= sizeof(init_word); i += sizeof(init_word))
            *(volatile init_word *)(data + i) = UINT64_C(0xa5a5a5a5a5a5a5a5);
        for (; i < buffers[b].bytes; ++i)
            data[i] = 0xa5;
#else
        for (unsigned long i = 0; i < buffers[b].bytes; ++i)
            data[i] = 0xa5;
#endif
    }
}

static void
check_buffers(void)
{
    for (unsigned long b = 0; b < buffer_count; ++b) {
        const volatile unsigned char *data = buffers[b].data;
        for (unsigned long i = 0; i < GUARD_BYTES; ++i)
            require(data[(long)i - (long)GUARD_BYTES] == 0xd3 &&
                data[buffers[b].bytes + i] == 0xd3, 0xfa03);
    }
    require(object_count == LOC_DMA_APP_OBJECTS &&
        grants == LOC_DMA_APP_GRANTS && uploads == LOC_DMA_APP_UPLOADS &&
        downloads == LOC_DMA_APP_DOWNLOADS && commands == LOC_DMA_APP_COMMANDS &&
        upload_bytes == LOC_DMA_APP_UPLOAD_BYTES &&
        download_bytes == LOC_DMA_APP_DOWNLOAD_BYTES, 0xfa04);
}

#if LOC_DMA_APP_BACKEND != 3
static loc_pim_object __attribute__((unused))
record_object(loc_pim_object object, unsigned long permissions)
{
    require(object && object_count < LOC_DMA_APP_OBJECTS, 0xfa06);
#if LOC_DMA_APP_BACKEND == 1
    require(!loc_pim_grant(object, permissions), 0xfa06);
    ++grants;
#else
    (void)permissions;
#endif
    objects[object_count++] = object;
    return object;
}

static loc_pim_object __attribute__((unused))
new_object(unsigned long bytes, unsigned long permissions)
{
    require(object_count < LOC_DMA_APP_OBJECTS, 0xfa05);
    const loc_pim_object object = loc_pim_alloc(bytes);
    require(object, 0xfa06);
#if LOC_DMA_APP_BACKEND == 1
    require(!loc_pim_grant(object, permissions), 0xfa06);
    ++grants;
#else
    (void)permissions;
#endif
    for (unsigned long i = 0; i < object_count; ++i)
        require(object != objects[i], 0xfa07);
    objects[object_count++] = object;
    return object;
}

static void
upload(loc_pim_object object, const void *data, unsigned long bytes)
{
    phase_begin(LOC_DA_UPLOAD);
    require(!loc_pim_upload(object, data, bytes), 0xfa08);
    ++uploads;
    upload_bytes += bytes;
    phase_end(LOC_DA_UPLOAD);
}

static void
download(void *data, loc_pim_object object, unsigned long bytes)
{
    phase_begin(LOC_DA_DOWNLOAD);
    require(!loc_pim_download(data, object, bytes), 0xfa09);
    ++downloads;
    download_bytes += bytes;
    phase_end(LOC_DA_DOWNLOAD);
}

#if LOC_DMA_APP_KIND != 2 && LOC_DMA_APP_KIND != 6 && \
    LOC_DMA_APP_KIND != 7 && LOC_DMA_APP_KIND != 8
static void
command(unsigned long operation, loc_pim_object a, loc_pim_object b,
    loc_pim_object destination)
{
    const loc_pim_request request = loc_pim_submit(operation, a, b, destination);
    require(request && !loc_pim_complete(request), 0xfa0a);
    ++commands;
}
#endif

#if LOC_DMA_APP_KIND != 3 && !LOC_DMA_APP_LORA_RESIDENT
static void
scalar_command(unsigned long operation, loc_pim_object source, int64_t scalar,
    loc_pim_object destination)
{
    const loc_pim_request request =
        loc_pim_submit_scalar(operation, source, scalar, destination);
    require(request && !loc_pim_complete(request), 0xfa0b);
    ++commands;
}
#endif

#endif /* PIM backend helpers */

#if LOC_DMA_APP_KIND == 1 || LOC_DMA_APP_KIND == 4
static int32_t
feature(unsigned long index, unsigned long axis)
{
    const int32_t center = ((int32_t)(index % 3UL) - 1) * 64;
    const int32_t noise = (int32_t)((index * (axis ? 11UL : 7UL) +
        (axis ? 5UL : 2UL)) % 23UL) - 11;
    return center + noise;
}

static void
initialize_points(struct point *points)
{
    for (unsigned long i = 0; i < N; ++i) {
        points[i].x = feature(i, 0);
        points[i].y = feature(i, 1);
        points[i].label = i % 3UL;
    }
}

static void
pack_points(const struct point *points, int32_t *x, int32_t *y)
{
    phase_begin(LOC_DA_HOST_PREPARE);
    loc_host_pack_points(points, x, y, N);
    phase_end(LOC_DA_HOST_PREPARE);
}

static void
check_points(const struct point *points, const int32_t *x, const int32_t *y)
{
    for (unsigned long i = 0; i < N; ++i)
        require(points[i].x == feature(i, 0) &&
            points[i].y == feature(i, 1) && points[i].label == i % 3UL &&
            x[i] == points[i].x && y[i] == points[i].y, 0xfa10);
}

#if LOC_DMA_APP_BACKEND != 3
static void
distance(loc_pim_object x, loc_pim_object y, loc_pim_object dx,
    loc_pim_object dy, int32_t qx, int32_t qy)
{
    phase_begin(LOC_DA_COMPUTE);
    scalar_command(M5_PIM_OP_SUB_SCALAR_INT32, x, qx, dx);
    scalar_command(M5_PIM_OP_SUB_SCALAR_INT32, y, qy, dy);
#if LOC_DMA_APP_KIND == 1
    command(M5_PIM_OP_ABS_INT32, dx, 0, dx);
    command(M5_PIM_OP_ABS_INT32, dy, 0, dy);
#else
    command(M5_PIM_OP_MUL_INT32, dx, dx, dx);
    command(M5_PIM_OP_MUL_INT32, dy, dy, dy);
#endif
    command(M5_PIM_OP_ADD_INT32, dx, dy, dx);
    phase_end(LOC_DA_COMPUTE);
}
#endif /* PIM distance */
#endif

#if LOC_DMA_APP_KIND == 1
static int32_t
query_coordinate(unsigned long round, unsigned long axis)
{
    return ((int32_t)(round % 3UL) - 1) * 64 +
        (int32_t)((round * 5UL + axis * 3UL) % 17UL) - 8;
}

static void
run_application(void)
{
    struct point *points = new_buffer(N * sizeof(*points));
    int32_t *x = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *y = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
#if LOC_DMA_APP_BACKEND != 3
    int32_t *distances = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
#endif
    uint32_t *predictions = new_buffer(R * sizeof(*predictions));
    unsigned long *neighbors = new_buffer(R * LOC_DMA_APP_K * sizeof(*neighbors));
    phase_end(LOC_DA_HOST_ALLOCATE);
    phase_begin(LOC_DA_INITIALIZE);
    initialize_buffers();
    initialize_points(points);
    phase_end(LOC_DA_INITIALIZE);
#if LOC_DMA_APP_BACKEND != 3
    phase_begin(LOC_DA_ALLOCATE_GRANT);
    const loc_pim_object px = new_object(LOC_DMA_APP_VECTOR_BYTES,
        LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object py = new_object(LOC_DMA_APP_VECTOR_BYTES,
        LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object dx = new_object(LOC_DMA_APP_VECTOR_BYTES,
        RESULT_PERMISSIONS);
    const loc_pim_object dy = new_object(LOC_DMA_APP_VECTOR_BYTES,
        WORKSPACE_PERMISSIONS);
    phase_end(LOC_DA_ALLOCATE_GRANT);
#endif
    pack_points(points, x, y);
#if LOC_DMA_APP_BACKEND != 3
    upload(px, x, LOC_DMA_APP_VECTOR_BYTES);
    upload(py, y, LOC_DMA_APP_VECTOR_BYTES);
#endif

    for (unsigned long r = 0; r < R; ++r) {
#if LOC_DMA_APP_BACKEND == 3
        phase_begin(LOC_DA_COMPUTE);
        const int32_t qx = query_coordinate(r, 0);
        const int32_t qy = query_coordinate(r, 1);
#else
        distance(px, py, dx, dy, query_coordinate(r, 0), query_coordinate(r, 1));
        download(distances, dx, LOC_DMA_APP_VECTOR_BYTES);
        phase_begin(LOC_DA_HOST_FINISH);
#endif
#if LOC_DMA_APP_BACKEND == 3
        unsigned long best[LOC_DMA_APP_K];
        int32_t best_distances[LOC_DMA_APP_K];
        for (unsigned long k = 0; k < LOC_DMA_APP_K; ++k) {
            best[k] = N;
            best_distances[k] = INT32_MAX;
        }
        /* Sorted bounded list; ties prefer the lower training-point index. */
        for (unsigned long i = 0; i < N; ++i) {
            const int32_t a = x[i] - qx, b = y[i] - qy;
            const int32_t distance = (a < 0 ? -a : a) + (b < 0 ? -b : b);
            unsigned long at = LOC_DMA_APP_K;
            for (unsigned long k = 0; k < LOC_DMA_APP_K; ++k) {
                if (best[k] == N || distance < best_distances[k] ||
                    (distance == best_distances[k] && i < best[k])) {
                    at = k;
                    break;
                }
            }
            if (at < LOC_DMA_APP_K) {
                for (unsigned long k = LOC_DMA_APP_K - 1; k > at; --k) {
                    best[k] = best[k - 1];
                    best_distances[k] = best_distances[k - 1];
                }
                best[at] = i;
                best_distances[at] = distance;
            }
        }
        unsigned long votes[3] = {0, 0, 0};
        for (unsigned long k = 0; k < LOC_DMA_APP_K; ++k) {
            neighbors[r * LOC_DMA_APP_K + k] = best[k];
            ++votes[points[best[k]].label];
        }
        uint32_t winner = 0;
        for (uint32_t c = 1; c < 3; ++c)
            if (votes[c] > votes[winner])
                winner = c;
        predictions[r] = winner;
#else
        predictions[r] = loc_host_knn_select_vote(points, distances,
            neighbors + r * LOC_DMA_APP_K, N);
#endif
#if LOC_DMA_APP_BACKEND == 3
        phase_end(LOC_DA_COMPUTE);
#else
        phase_end(LOC_DA_HOST_FINISH);
#endif
    }
    phase_begin(LOC_DA_CHECK);
    check_points(points, x, y);
    for (unsigned long r = 0; r < R; ++r) {
        unsigned long chosen[LOC_DMA_APP_K];
        unsigned long votes[3] = {0, 0, 0};
        /* Independent reference: rescan every point to select each neighbor.
         * It uses host arithmetic and never trusts the downloaded distances. */
        for (unsigned long k = 0; k < LOC_DMA_APP_K; ++k) {
            int64_t minimum = INT64_MAX;
            unsigned long index = N;
            for (unsigned long i = 0; i < N; ++i) {
                int used = 0;
                for (unsigned long j = 0; j < k; ++j)
                    used |= chosen[j] == i;
                if (used)
                    continue;
                int64_t a = (int64_t)points[i].x - query_coordinate(r, 0);
                int64_t b = (int64_t)points[i].y - query_coordinate(r, 1);
                const int64_t value = (a < 0 ? -a : a) + (b < 0 ? -b : b);
                if (value < minimum) {
                    minimum = value;
                    index = i;
                }
            }
            chosen[k] = index;
            require(index == neighbors[r * LOC_DMA_APP_K + k], 0xfa11);
            ++votes[points[index].label];
        }
        uint32_t winner = 0;
        for (uint32_t c = 1; c < 3; ++c)
            if (votes[c] > votes[winner])
                winner = c;
        require(predictions[r] == winner, 0xfa12);
    }
}
#elif LOC_DMA_APP_KIND == 2
static uint64_t
key_value(unsigned long index)
{
    switch (index % 5UL) {
      case 0: return 0;
      case 1: return UINT64_MAX;
      case 2: return UINT64_C(0x8000000000000000) + index;
      default: return (index * UINT64_C(0x123456789abcdef)) ^ UINT64_C(0xabcdef);
    }
}

static uint64_t
threshold_value(unsigned long round)
{
    switch (round % 3UL) {
      case 0: return UINT64_C(0x8000000000000040);
      case 1: return 0;
      default: return UINT64_MAX;
    }
}

static void
run_application(void)
{
    uint64_t *keys = new_buffer(N * sizeof(*keys));
#if LOC_DMA_APP_BACKEND != 3
    unsigned char *mask = new_buffer(LOC_DMA_APP_MASK_BYTES);
#endif
    uint64_t *counts = new_buffer(R * sizeof(*counts));
    uint64_t *sums = new_buffer(R * sizeof(*sums));
    uint64_t *thresholds = new_buffer(R * sizeof(*thresholds));
    phase_end(LOC_DA_HOST_ALLOCATE);
    phase_begin(LOC_DA_INITIALIZE);
    initialize_buffers();
    for (unsigned long i = 0; i < N; ++i)
        keys[i] = key_value(i);
    phase_end(LOC_DA_INITIALIZE);
#if LOC_DMA_APP_BACKEND != 3
    phase_begin(LOC_DA_ALLOCATE_GRANT);
    const loc_pim_object source = new_object(N * sizeof(*keys),
        LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object bitmap = new_object(LOC_DMA_APP_MASK_BYTES,
        LOC_PIM_OUTPUT_PERMISSIONS);
    phase_end(LOC_DA_ALLOCATE_GRANT);
#endif
    phase_begin(LOC_DA_HOST_PREPARE);
    for (unsigned long r = 0; r < R; ++r)
        thresholds[r] = threshold_value(r);
    phase_end(LOC_DA_HOST_PREPARE);
#if LOC_DMA_APP_BACKEND != 3
    upload(source, keys, N * sizeof(*keys));
#endif
    for (unsigned long r = 0; r < R; ++r) {
        phase_begin(LOC_DA_COMPUTE);
#if LOC_DMA_APP_BACKEND != 3
        scalar_command(M5_PIM_OP_LT_SCALAR_UINT64_BOOL, source,
            (int64_t)thresholds[r], bitmap);
        phase_end(LOC_DA_COMPUTE);
        download(mask, bitmap, LOC_DMA_APP_MASK_BYTES);
        phase_begin(LOC_DA_HOST_FINISH);
#endif
#if LOC_DMA_APP_BACKEND == 3
        uint64_t count = 0, sum = 0;
        for (unsigned long i = 0; i < N; ++i) {
            const int match = keys[i] < thresholds[r];
            if (match) {
                ++count;
                sum += keys[i];
            }
        }
        counts[r] = count;
        sums[r] = sum;
#else
        loc_host_filter_aggregate(keys, mask, N, &counts[r], &sums[r]);
#endif
#if LOC_DMA_APP_BACKEND == 3
        phase_end(LOC_DA_COMPUTE);
#else
        phase_end(LOC_DA_HOST_FINISH);
#endif
    }
    phase_begin(LOC_DA_CHECK);
    for (unsigned long r = 0; r < R; ++r) {
        uint64_t expected_count = 0, expected_sum = 0;
        require(thresholds[r] == threshold_value(r), 0xfa20);
        for (unsigned long i = 0; i < N; ++i) {
            require(keys[i] == key_value(i), 0xfa21);
            if (keys[i] < thresholds[r]) {
                ++expected_count;
                expected_sum += keys[i];
            }
#if LOC_DMA_APP_BACKEND != 3
            if (r == R - 1)
                require(((mask[i / 8UL] >> (i % 8UL)) & 1U) ==
                    (keys[i] < thresholds[r]), 0xfa22);
#endif
        }
        require(counts[r] == expected_count && sums[r] == expected_sum, 0xfa23);
    }
#if LOC_DMA_APP_BACKEND != 3
    if (N % 8UL)
        require((mask[LOC_DMA_APP_MASK_BYTES - 1] >> (N % 8UL)) == 0, 0xfa24);
#endif
}
#elif LOC_DMA_APP_KIND == 3
static int32_t
pixel(unsigned long index)
{
    return (int32_t)((index * 37UL + index / 7UL) % 8191UL) - 4095;
}

static void
run_application(void)
{
    int32_t *image = new_buffer(4UL * LOC_DMA_APP_VECTOR_BYTES);
#if LOC_DMA_APP_BACKEND != 3
    int32_t *packed[4];
    for (unsigned long i = 0; i < 4; ++i)
        packed[i] = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *output = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
#endif
    int32_t *result = new_buffer(R * LOC_DMA_APP_VECTOR_BYTES);
    phase_end(LOC_DA_HOST_ALLOCATE);
    phase_begin(LOC_DA_INITIALIZE);
    initialize_buffers();
    for (unsigned long i = 0; i < 4UL * N; ++i)
        image[i] = pixel(i);
    phase_end(LOC_DA_INITIALIZE);
#if LOC_DMA_APP_BACKEND != 3
    phase_begin(LOC_DA_ALLOCATE_GRANT);
    loc_pim_object input[4];
    for (unsigned long i = 0; i < 4; ++i)
        input[i] = new_object(LOC_DMA_APP_VECTOR_BYTES,
            LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object destination = new_object(LOC_DMA_APP_VECTOR_BYTES,
        RESULT_PERMISSIONS);
    phase_end(LOC_DA_ALLOCATE_GRANT);
#endif
#if LOC_DMA_APP_BACKEND != 3
    phase_begin(LOC_DA_HOST_PREPARE);
#endif
    unsigned long width = 8;
    while (N % width)
        --width;
    const unsigned long height = N / width;
#if LOC_DMA_APP_BACKEND != 3
    loc_host_pool_pack(image, packed, width, height);
    phase_end(LOC_DA_HOST_PREPARE);
    for (unsigned long i = 0; i < 4; ++i)
        upload(input[i], packed[i], LOC_DMA_APP_VECTOR_BYTES);
#endif
    for (unsigned long r = 0; r < R; ++r) {
        phase_begin(LOC_DA_COMPUTE);
#if LOC_DMA_APP_BACKEND == 3
        for (unsigned long row = 0; row < height; ++row) {
            for (unsigned long col = 0; col < width; ++col) {
                const unsigned long in = 4UL * row * width + 2UL * col;
                const int32_t a = image[in], b = image[in + 1];
                const int32_t c = image[in + 2UL * width];
                const int32_t d = image[in + 2UL * width + 1];
                const int32_t ab = a > b ? a : b, cd = c > d ? c : d;
                result[r * N + row * width + col] = ab > cd ? ab : cd;
            }
        }
        phase_end(LOC_DA_COMPUTE);
#else
        command(M5_PIM_OP_MAX_INT32, input[0], input[1], destination);
        command(M5_PIM_OP_MAX_INT32, destination, input[2], destination);
        command(M5_PIM_OP_MAX_INT32, destination, input[3], destination);
        phase_end(LOC_DA_COMPUTE);
        download(output, destination, LOC_DMA_APP_VECTOR_BYTES);
        phase_begin(LOC_DA_HOST_FINISH);
        loc_host_pool_reconstruct(output, result + r * N, N);
        phase_end(LOC_DA_HOST_FINISH);
#endif
    }
    phase_begin(LOC_DA_CHECK);
    for (unsigned long i = 0; i < 4UL * N; ++i)
        require(image[i] == pixel(i), 0xfa30);
    for (unsigned long row = 0; row < height; ++row) {
        for (unsigned long col = 0; col < width; ++col) {
            int32_t expected = INT32_MIN;
            for (unsigned long y = 0; y < 2; ++y) {
                for (unsigned long x = 0; x < 2; ++x) {
                    const int32_t value = image[(2UL * row + y) *
                        (2UL * width) + 2UL * col + x];
                    if (value > expected)
                        expected = value;
#if LOC_DMA_APP_BACKEND != 3
                    require(packed[y * 2UL + x][row * width + col] == value,
                        0xfa31);
#endif
                }
            }
#if LOC_DMA_APP_BACKEND != 3
            require(output[row * width + col] == expected, 0xfa32);
#endif
            for (unsigned long r = 0; r < R; ++r)
                require(result[r * N + row * width + col] == expected, 0xfa33);
        }
    }
}
#elif LOC_DMA_APP_KIND == 4
static void
run_application(void)
{
    struct point *points = new_buffer(N * sizeof(*points));
    int32_t *x = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *y = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
#if LOC_DMA_APP_BACKEND != 3
    int32_t *distances = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *best = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
#endif
    uint32_t *assignment = new_buffer(N * sizeof(*assignment));
    int32_t *history = new_buffer(R * 6UL * sizeof(*history));
    uint32_t *reference_assignment = new_buffer(N * sizeof(*reference_assignment));
    int32_t centroids[3][2];
    phase_end(LOC_DA_HOST_ALLOCATE);
    phase_begin(LOC_DA_INITIALIZE);
    initialize_buffers();
    initialize_points(points);
    for (unsigned long c = 0; c < 3; ++c) {
        centroids[c][0] = points[c].x;
        centroids[c][1] = points[c].y;
    }
    phase_end(LOC_DA_INITIALIZE);
#if LOC_DMA_APP_BACKEND != 3
    phase_begin(LOC_DA_ALLOCATE_GRANT);
    const loc_pim_object px = new_object(LOC_DMA_APP_VECTOR_BYTES,
        LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object py = new_object(LOC_DMA_APP_VECTOR_BYTES,
        LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object dx = new_object(LOC_DMA_APP_VECTOR_BYTES,
        RESULT_PERMISSIONS);
    const loc_pim_object dy = new_object(LOC_DMA_APP_VECTOR_BYTES,
        WORKSPACE_PERMISSIONS);
    phase_end(LOC_DA_ALLOCATE_GRANT);
#endif
    pack_points(points, x, y);
#if LOC_DMA_APP_BACKEND != 3
    upload(px, x, LOC_DMA_APP_VECTOR_BYTES);
    upload(py, y, LOC_DMA_APP_VECTOR_BYTES);
#endif
    for (unsigned long r = 0; r < R; ++r) {
#if LOC_DMA_APP_BACKEND == 3
        phase_begin(LOC_DA_COMPUTE);
        int64_t sums[3][2] = {{0, 0}, {0, 0}, {0, 0}};
        int64_t counts[3] = {0, 0, 0};
        for (unsigned long i = 0; i < N; ++i) {
            int32_t minimum = INT32_MAX;
            unsigned long chosen = 0;
            for (unsigned long c = 0; c < 3; ++c) {
                const int32_t a = x[i] - centroids[c][0];
                const int32_t b = y[i] - centroids[c][1];
                const int32_t value = a * a + b * b;
                if (value < minimum) {
                    minimum = value;
                    chosen = c;
                }
            }
            assignment[i] = chosen;
            sums[chosen][0] += x[i];
            sums[chosen][1] += y[i];
            ++counts[chosen];
        }
#else
        for (unsigned long c = 0; c < 3; ++c) {
            distance(px, py, dx, dy, centroids[c][0], centroids[c][1]);
            download(distances, dx, LOC_DMA_APP_VECTOR_BYTES);
            phase_begin(LOC_DA_HOST_FINISH);
            loc_host_kmeans_assign(distances, best, assignment, N, c);
            phase_end(LOC_DA_HOST_FINISH);
        }
        phase_begin(LOC_DA_HOST_FINISH);
        loc_host_kmeans_update(points, assignment, N, centroids,
            history + 6UL * r);
#endif
#if LOC_DMA_APP_BACKEND == 3
        for (unsigned long c = 0; c < 3; ++c) {
            /* Fixed-point/integer means truncate toward zero; empty clusters
             * retain their previous centroid. Ties prefer smaller cluster ID. */
            if (counts[c]) {
                centroids[c][0] = sums[c][0] / counts[c];
                centroids[c][1] = sums[c][1] / counts[c];
            }
            history[6UL * r + 2UL * c] = centroids[c][0];
            history[6UL * r + 2UL * c + 1] = centroids[c][1];
        }
        phase_end(LOC_DA_COMPUTE);
#else
        phase_end(LOC_DA_HOST_FINISH);
#endif
    }
    phase_begin(LOC_DA_CHECK);
    check_points(points, x, y);
    int32_t reference[3][2];
    for (unsigned long c = 0; c < 3; ++c) {
        reference[c][0] = points[c].x;
        reference[c][1] = points[c].y;
    }
    for (unsigned long r = 0; r < R; ++r) {
        /* Independent point-major CPU Lloyd iteration. Distances use int64
         * intermediates and no downloaded PIM values. */
        for (unsigned long i = 0; i < N; ++i) {
            int64_t minimum = INT64_MAX;
            unsigned long chosen = 0;
            for (unsigned long c = 0; c < 3; ++c) {
                const int64_t a = (int64_t)points[i].x - reference[c][0];
                const int64_t b = (int64_t)points[i].y - reference[c][1];
                const int64_t value = a * a + b * b;
                if (value < minimum) {
                    minimum = value;
                    chosen = c;
                }
            }
            reference_assignment[i] = chosen;
        }
        for (unsigned long c = 0; c < 3; ++c) {
            int64_t sum_x = 0, sum_y = 0, count = 0;
            for (unsigned long i = 0; i < N; ++i) {
                if (reference_assignment[i] == c) {
                    sum_x += points[i].x;
                    sum_y += points[i].y;
                    ++count;
                }
            }
            if (count) {
                reference[c][0] = sum_x / count;
                reference[c][1] = sum_y / count;
            }
            require(history[6UL * r + 2UL * c] == reference[c][0] &&
                history[6UL * r + 2UL * c + 1] == reference[c][1], 0xfa40);
        }
    }
    for (unsigned long i = 0; i < N; ++i)
        require(assignment[i] == reference_assignment[i], 0xfa41);
    for (unsigned long c = 0; c < 3; ++c)
        require(centroids[c][0] == reference[c][0] &&
            centroids[c][1] == reference[c][1], 0xfa42);
}
#elif LOC_DMA_APP_KIND == 5
static uint8_t
downsample_pixel(unsigned long index, unsigned long width)
{
    /* Explicit boundary/rounding windows, followed by a pattern incorporating
     * high address bits so large-block permutations cannot hide behind a
     * short repeating byte sequence. */
    static const uint8_t edges[8][4] = {
        {0, 0, 0, 0}, {255, 255, 255, 255}, {3, 3, 3, 3}, {0, 1, 2, 3},
        {4, 5, 6, 7}, {252, 253, 254, 255}, {1, 127, 128, 254}, {7, 8, 9, 10}
    };
    const unsigned long row = index / (2UL * width);
    const unsigned long col = index % (2UL * width);
    const unsigned long window = (row / 2UL) * width + col / 2UL;
    if (window < 8)
        return edges[window][2UL * (row % 2UL) + col % 2UL];
    return (uint8_t)(index * 73UL + (index >> 7) * 19UL +
        (index >> 16) * 31UL + 17UL);
}

static void
run_application(void)
{
    uint8_t *image = new_buffer(4UL * N);
    uint8_t *packed[4];
    for (unsigned long i = 0; i < 4; ++i)
        packed[i] = new_buffer(N);
    uint8_t *output = new_buffer(N);
    uint8_t *result = new_buffer(R * N);
    /* Use the closest rectangular factorization to a square. Odd fixture
     * sizes remain valid; N=4096/65536 gives 64x64/256x256 output images. */
    unsigned long width = 1;
    for (unsigned long candidate = 2; candidate <= N / candidate; ++candidate)
        if (N % candidate == 0)
            width = candidate;
    const unsigned long height = N / width;
    phase_end(LOC_DA_HOST_ALLOCATE);
    phase_begin(LOC_DA_INITIALIZE);
    initialize_buffers();
    for (unsigned long i = 0; i < 4UL * N; ++i)
        image[i] = downsample_pixel(i, width);
    phase_end(LOC_DA_INITIALIZE);

    phase_begin(LOC_DA_ALLOCATE_GRANT);
    loc_pim_object input[4];
    for (unsigned long i = 0; i < 4; ++i)
        input[i] = new_object(N, LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object destination = new_object(N, RESULT_PERMISSIONS);
    const loc_pim_object scratch = new_object(N, WORKSPACE_PERMISSIONS);
    phase_end(LOC_DA_ALLOCATE_GRANT);

    phase_begin(LOC_DA_HOST_PREPARE);
    loc_host_downsample_pack(image, packed, width, height);
    phase_end(LOC_DA_HOST_PREPARE);
    for (unsigned long i = 0; i < 4; ++i)
        upload(input[i], packed[i], N);

    for (unsigned long r = 0; r < R; ++r) {
        phase_begin(LOC_DA_COMPUTE);
        /* Match PIMbench's UINT8 operation order: truncate each pixel before
         * adding. Shifting the sum would implement a different rounding rule.
         * Inputs remain unchanged across rounds; only scratch/result mutate. */
        scalar_command(M5_PIM_OP_SHIFT_BITS_RIGHT_UINT8, input[0], 2,
            destination);
        for (unsigned long i = 1; i < 4; ++i) {
            scalar_command(M5_PIM_OP_SHIFT_BITS_RIGHT_UINT8, input[i], 2,
                scratch);
            command(M5_PIM_OP_ADD_UINT8, destination, scratch, destination);
        }
        phase_end(LOC_DA_COMPUTE);
        download(output, destination, N);
        phase_begin(LOC_DA_HOST_FINISH);
        loc_host_downsample_reconstruct(output, result + r * N, N);
        phase_end(LOC_DA_HOST_FINISH);
    }

    phase_begin(LOC_DA_CHECK);
    for (unsigned long i = 0; i < 4UL * N; ++i)
        require(image[i] == downsample_pixel(i, width), 0xfa60);
    for (unsigned long row = 0; row < height; ++row) {
        for (unsigned long col = 0; col < width; ++col) {
            unsigned expected = 0;
            /* Independent raster-window reference uses integer division and
             * a wider sum, without trusting downloaded or packed operands. */
            for (unsigned long y = 0; y < 2; ++y) {
                for (unsigned long x = 0; x < 2; ++x) {
                    const uint8_t value = image[(2UL * row + y) *
                        (2UL * width) + 2UL * col + x];
                    expected += (unsigned)value / 4U;
                    require(packed[y * 2UL + x][row * width + col] == value,
                        0xfa61);
                }
            }
            require(expected <= 252 && output[row * width + col] == expected,
                0xfa62);
            for (unsigned long r = 0; r < R; ++r)
                require(result[r * N + row * width + col] == expected,
                    0xfa63);
        }
    }
}
#elif LOC_DMA_APP_KIND == 6
#define D LOC_DMA_APP_GEMM_DIM
#define COLUMN_BYTES (4UL * D)

static int32_t
gemm_value(unsigned long row, unsigned long col, unsigned long matrix)
{
    /* Distinct signed A/B patterns depend on both axes, including high bits.
     * With D<=256 and values in [-8,8], every accumulated sum fits int32. */
    return (int32_t)((row * (matrix ? 13UL : 7UL) +
        col * (matrix ? 5UL : 11UL) + (row * col) % 19UL +
        (row >> 3) * 3UL + (col >> 3) * 7UL + matrix * 2UL) % 17UL) - 8;
}

static void
run_application(void)
{
    int32_t *a = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *b = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *a_columns = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *b_columns = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *zero = new_buffer(COLUMN_BYTES);
    int32_t *columns = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *result = new_buffer(R * LOC_DMA_APP_VECTOR_BYTES);
    phase_end(LOC_DA_HOST_ALLOCATE);
    phase_begin(LOC_DA_INITIALIZE);
    initialize_buffers();
    for (unsigned long row = 0; row < D; ++row) {
        zero[row] = 0;
        for (unsigned long col = 0; col < D; ++col) {
            a[row * D + col] = gemm_value(row, col, 0);
            b[row * D + col] = gemm_value(row, col, 1);
        }
    }
    phase_end(LOC_DA_INITIALIZE);

    phase_begin(LOC_DA_ALLOCATE_GRANT);
    const loc_pim_object input = new_object(COLUMN_BYTES,
        LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object accumulator = new_object(COLUMN_BYTES,
        RESULT_PERMISSIONS | LOC_PIM_COPY_TO_PERMISSIONS);
    phase_end(LOC_DA_ALLOCATE_GRANT);

    phase_begin(LOC_DA_HOST_PREPARE);
    loc_host_gemm_pack(a, b, a_columns, b_columns, D);
    phase_end(LOC_DA_HOST_PREPARE);

    for (unsigned long r = 0; r < R; ++r) {
        for (unsigned long col = 0; col < D; ++col) {
            /* Two objects and their capabilities remain allocated throughout
             * the application. A columns are streamed as in PIMbench GEMM;
             * B stays on the host and supplies the scalar in each command. */
            upload(accumulator, zero, COLUMN_BYTES);
            for (unsigned long k = 0; k < D; ++k) {
                upload(input, a_columns + k * D, COLUMN_BYTES);
                phase_begin(LOC_DA_COMPUTE);
                scalar_command(M5_PIM_OP_AXPY_INT32, input,
                    b_columns[col * D + k], accumulator);
                phase_end(LOC_DA_COMPUTE);
            }
            download(columns + col * D, accumulator, COLUMN_BYTES);
        }
        phase_begin(LOC_DA_HOST_FINISH);
        loc_host_gemm_reconstruct(columns, result + r * N, D);
        phase_end(LOC_DA_HOST_FINISH);
    }

    phase_begin(LOC_DA_CHECK);
    for (unsigned long row = 0; row < D; ++row) {
        require(zero[row] == 0, 0xfa70);
        for (unsigned long col = 0; col < D; ++col) {
            require(a[row * D + col] == gemm_value(row, col, 0) &&
                b[row * D + col] == gemm_value(row, col, 1), 0xfa71);
            require(a_columns[col * D + row] == a[row * D + col] &&
                b_columns[col * D + row] == b[row * D + col], 0xfa72);
            /* Independent row-major dot product with wide arithmetic. This
             * reference does not trust the packed or downloaded operands. */
            int64_t expected = 0;
            for (unsigned long k = 0; k < D; ++k)
                expected += (int64_t)a[row * D + k] * b[k * D + col];
            require(expected >= INT32_MIN && expected <= INT32_MAX &&
                columns[col * D + row] == expected, 0xfa73);
            for (unsigned long r = 0; r < R; ++r)
                require(result[r * N + row * D + col] == expected, 0xfa74);
        }
    }
}
#undef COLUMN_BYTES
#undef D
#elif LOC_DMA_APP_KIND == 7 || LOC_DMA_APP_KIND == 8
#define K LOC_DMA_APP_GEMV_COLUMNS
#define MATRIX_BYTES (LOC_DMA_APP_VECTOR_BYTES * K)

static int32_t
gemv_matrix_value(unsigned long row, unsigned long col)
{
    return (int32_t)((row * 7UL + col * 11UL + (row * col) % 19UL +
        (row >> 3) * 3UL + (col >> 3) * 7UL) % 17UL) - 8;
}

static int32_t
gemv_vector_value(unsigned long round, unsigned long col)
{
    return (int32_t)((round * 13UL + col * 5UL +
        (round * col) % 11UL + (col >> 3) * 3UL) % 17UL) - 8;
}

static void
run_application(void)
{
    int32_t *matrix = new_buffer(MATRIX_BYTES);
#if !LOC_DMA_APP_LORA_PLACED
    int32_t *columns = new_buffer(LOC_DMA_APP_LORA_RESIDENT ?
        LOC_DMA_APP_VECTOR_BYTES * LOC_DMA_APP_PACK_COLUMNS : MATRIX_BYTES);
#endif
    int32_t *vectors = new_buffer(4UL * R * K);
    int32_t *zero = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *output = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
    int32_t *result = new_buffer(R * LOC_DMA_APP_VECTOR_BYTES);
#if LOC_DMA_APP_KIND == 8
    int32_t *a = new_buffer(4UL * LOC_DMA_APP_LORA_RANK * K);
    int32_t *b = new_buffer(4UL * N * LOC_DMA_APP_LORA_RANK);
    int32_t *ax = new_buffer(4UL * R * LOC_DMA_APP_LORA_RANK);
    int32_t *correction = new_buffer(R * LOC_DMA_APP_VECTOR_BYTES);
    int32_t *bias = new_buffer(LOC_DMA_APP_VECTOR_BYTES);
#endif
    phase_end(LOC_DA_HOST_ALLOCATE);
    phase_begin(LOC_DA_INITIALIZE);
    initialize_buffers();
#if LOC_DMA_APP_LORA_PLACED
    for (unsigned long row = 0; row < N; ++row) zero[row] = 0;
    for (unsigned long col = 0; col < K; ++col)
        for (unsigned long row = 0; row < N; ++row)
            matrix[col * N + row] = gemv_matrix_value(row, col);
#else
    for (unsigned long row = 0; row < N; ++row) {
        zero[row] = 0;
        for (unsigned long col = 0; col < K; ++col)
            matrix[row * K + col] = gemv_matrix_value(row, col);
    }
#endif
    for (unsigned long r = 0; r < R; ++r)
        for (unsigned long col = 0; col < K; ++col)
            vectors[r * K + col] = gemv_vector_value(r, col);
#if LOC_DMA_APP_KIND == 8
    for (unsigned long i = 0; i < LOC_DMA_APP_LORA_RANK; ++i)
        for (unsigned long col = 0; col < K; ++col)
            a[i * K + col] = (int32_t)((i * 3UL + col * 7UL) % 17UL) - 8;
    for (unsigned long row = 0; row < N; ++row) {
        bias[row] = (int32_t)(row % 17UL) - 8;
        for (unsigned long i = 0; i < LOC_DMA_APP_LORA_RANK; ++i)
            b[row * LOC_DMA_APP_LORA_RANK + i] =
                (int32_t)((row * 5UL + i * 11UL) % 17UL) - 8;
    }
#endif
    phase_end(LOC_DA_INITIALIZE);

    phase_begin(LOC_DA_ALLOCATE_GRANT);
#if LOC_DMA_APP_LORA_PLACED
    const loc_pim_object weights = record_object(loc_pim_alloc_placed(N, K, 0),
        LOC_PIM_INPUT_PERMISSIONS);
    const loc_pim_object accumulator = record_object(loc_pim_alloc_placed(N, 1, weights),
        RESULT_PERMISSIONS | LOC_PIM_COPY_TO_PERMISSIONS);
#else
#if LOC_DMA_APP_LORA_RESIDENT
    const loc_pim_object weights = new_object(MATRIX_BYTES,
        LOC_PIM_INPUT_PERMISSIONS);
#else
    const loc_pim_object input = new_object(LOC_DMA_APP_VECTOR_BYTES,
        LOC_PIM_INPUT_PERMISSIONS);
#endif
    const loc_pim_object accumulator = new_object(LOC_DMA_APP_VECTOR_BYTES,
        RESULT_PERMISSIONS | LOC_PIM_COPY_TO_PERMISSIONS);
#endif
    phase_end(LOC_DA_ALLOCATE_GRANT);

#if LOC_DMA_APP_LORA_RESIDENT
    /* One parent object; column-major blocks extend its initialized prefix. */
    for (unsigned long first = 0; first < K; first += LOC_DMA_APP_PACK_COLUMNS) {
        const unsigned long count = K - first < LOC_DMA_APP_PACK_COLUMNS ?
            K - first : LOC_DMA_APP_PACK_COLUMNS;
#if !LOC_DMA_APP_LORA_PLACED
        phase_begin(LOC_DA_HOST_PREPARE);
        loc_host_gemv_pack_columns(matrix, columns, N, K, first, count);
        phase_end(LOC_DA_HOST_PREPARE);
#endif
        phase_begin(LOC_DA_UPLOAD);
        const unsigned long bytes = count * LOC_DMA_APP_VECTOR_BYTES;
        require(!loc_pim_upload_at(weights, first * LOC_DMA_APP_VECTOR_BYTES,
#if LOC_DMA_APP_LORA_PLACED
            matrix + first * N,
#else
            columns,
#endif
            bytes), 0xfa92);
        ++uploads;
        upload_bytes += bytes;
        phase_end(LOC_DA_UPLOAD);
    }
#else
    phase_begin(LOC_DA_HOST_PREPARE);
    loc_host_gemv_pack(matrix, columns, N, K);
    phase_end(LOC_DA_HOST_PREPARE);
#endif
    for (unsigned long r = 0; r < R; ++r) {
        upload(accumulator, zero, LOC_DMA_APP_VECTOR_BYTES);
        for (unsigned long col = 0; col < K; ++col) {
#if LOC_DMA_APP_LORA_RESIDENT
            phase_begin(LOC_DA_COMPUTE);
            const loc_pim_request request = loc_pim_axpy_range(weights,
                accumulator, vectors[r * K + col],
                col * LOC_DMA_APP_VECTOR_BYTES, LOC_DMA_APP_VECTOR_BYTES);
            require(request && !loc_pim_complete(request), 0xfa93);
            ++commands;
#else
            upload(input, columns + col * N, LOC_DMA_APP_VECTOR_BYTES);
            phase_begin(LOC_DA_COMPUTE);
            scalar_command(M5_PIM_OP_AXPY_INT32, input,
                vectors[r * K + col], accumulator);
#endif
            phase_end(LOC_DA_COMPUTE);
        }
        download(output, accumulator, LOC_DMA_APP_VECTOR_BYTES);
#if LOC_DMA_APP_KIND == 8
        phase_begin(LOC_DA_HOST_LORA);
        require(loc_host_lora_adapter(a, b, vectors + r * K,
            ax + r * LOC_DMA_APP_LORA_RANK, correction + r * N,
            N, K, LOC_DMA_APP_LORA_RANK), 0xfa90);
        phase_end(LOC_DA_HOST_LORA);
        phase_begin(LOC_DA_HOST_FINISH);
        require(loc_host_lora_combine(output, correction + r * N, bias,
            result + r * N, N, LOC_DMA_APP_LORA_SCALE), 0xfa91);
#else
        phase_begin(LOC_DA_HOST_FINISH);
        loc_host_gemv_store_result(output, result + r * N, N);
#endif
        phase_end(LOC_DA_HOST_FINISH);
    }

    phase_begin(LOC_DA_CHECK);
#if LOC_DMA_APP_KIND == 8
    int64_t reference_ax[R * LOC_DMA_APP_LORA_RANK];
    for (unsigned long r = 0; r < R; ++r)
        for (unsigned long i = 0; i < LOC_DMA_APP_LORA_RANK; ++i) {
            int64_t sum = 0;
            for (unsigned long col = 0; col < K; ++col)
                sum += (int64_t)a[i * K + col] * vectors[r * K + col];
            reference_ax[r * LOC_DMA_APP_LORA_RANK + i] = sum;
            require(sum >= -(int64_t)LOC_DMA_APP_LORA_AX_BOUND &&
                sum <= (int64_t)LOC_DMA_APP_LORA_AX_BOUND &&
                ax[r * LOC_DMA_APP_LORA_RANK + i] == sum, 0xfa92);
        }
#endif
    for (unsigned long r = 0; r < R; ++r)
        for (unsigned long col = 0; col < K; ++col)
            require(vectors[r * K + col] == gemv_vector_value(r, col), 0xfa80);
    for (unsigned long row = 0; row < N; ++row) {
        require(zero[row] == 0, 0xfa81);
        for (unsigned long col = 0; col < K; ++col) {
            require(matrix[LOC_DMA_APP_LORA_PLACED ? col * N + row : row * K + col]
                == gemv_matrix_value(row, col),
                0xfa82);
#if LOC_DMA_APP_LORA_PLACED
            /* All original weight values were independently checked above. */
#elif !LOC_DMA_APP_LORA_RESIDENT
            require(columns[col * N + row] == matrix[row * K + col], 0xfa83);
#else
            const unsigned long last = ((K - 1) / LOC_DMA_APP_PACK_COLUMNS) * LOC_DMA_APP_PACK_COLUMNS;
            if (col >= last)
                require(columns[(col - last) * N + row] == matrix[row * K + col], 0xfa83);
#endif
        }
        for (unsigned long r = 0; r < R; ++r) {
            /* Independent row-major dot product, without using the packed
             * operands. K<=1024 and inputs in [-8,8] bound |sum|<=65536. */
            int64_t expected = 0;
            for (unsigned long col = 0; col < K; ++col)
                expected += (int64_t)(LOC_DMA_APP_LORA_PLACED ?
                    gemv_matrix_value(row, col) : matrix[row * K + col]) *
                    vectors[r * K + col];
            require(expected >= INT32_MIN && expected <= INT32_MAX &&
                (r + 1UL != R || output[row] == expected), 0xfa85);
#if LOC_DMA_APP_KIND == 8
            /* Independent reference recomputes each rank component directly
             * from row-major A/B; it does not trust ax or correction. */
            int64_t reference_correction = 0;
            for (unsigned long i = 0; i < LOC_DMA_APP_LORA_RANK; ++i) {
                reference_correction += reference_ax[r * LOC_DMA_APP_LORA_RANK + i] *
                    b[row * LOC_DMA_APP_LORA_RANK + i];
            }
            require(correction[r * N + row] == reference_correction, 0xfa93);
            expected += reference_correction * LOC_DMA_APP_LORA_SCALE +
                bias[row];
#endif
            require(expected >= INT32_MIN && expected <= INT32_MAX &&
                result[r * N + row] == expected, 0xfa84);
        }
    }
}
#undef MATRIX_BYTES
#undef K
#endif

static void
run_workload(void)
{
    phase_begin(LOC_DA_HOST_ALLOCATE);
#if LOC_DMA_APP_BACKEND == 1
    require(loc_pim_principal(), 0xfa00);
#endif
#if LOC_DMA_APP_BACKEND != 3
    require(loc_pim_transfer_model() == M5_PIM_TRANSFER_MODEL_DMA, 0xfa00);
#endif
    arena = allocate_host_arena();
    run_application();
    check_buffers();
    phase_end(LOC_DA_CHECK);
#if LOC_DMA_APP_BACKEND != 3
    phase_begin(LOC_DA_FREE);
    for (unsigned long i = 0; i < object_count; ++i)
        require(!loc_pim_free(objects[i]), 0xfa0c);
    phase_end(LOC_DA_FREE);
#endif
    phase_begin(LOC_DA_HOST_FREE);
    free_host_arena(arena);
    phase_end(LOC_DA_HOST_FREE);
}
