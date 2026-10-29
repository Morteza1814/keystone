#include "loc_pim.h"

uint64_t
loc_pim_principal(void)
{
    return m5_pim_enclave(M5_PIM_ENCLAVE_QUERY, 0, 0, 0);
}

loc_pim_object
loc_pim_alloc(size_t bytes)
{
    return m5_pim_obj_alloc(bytes);
}

uint64_t
loc_pim_grant(loc_pim_object object, uint64_t permissions)
{
    return m5_pim_obj_grant(object, permissions);
}

uint64_t
loc_pim_free(loc_pim_object object)
{
    return m5_pim_free(object);
}

uint64_t
loc_pim_check_buffer(loc_pim_object object, const void *buffer,
    size_t bytes, enum loc_pim_direction direction)
{
    return m5_pim_check_buffer(object, (uintptr_t)buffer, bytes, direction);
}

loc_pim_request
loc_pim_stream_begin(loc_pim_object object, const void *buffer,
    size_t bytes, enum loc_pim_direction direction)
{
    return m5_pim_transfer(M5_PIM_TRANSFER_BEGIN, object,
        (uintptr_t)buffer, bytes, direction);
}

uint64_t
loc_pim_stream_status(loc_pim_request request)
{
    return m5_pim_transfer(M5_PIM_TRANSFER_STATUS, request, 0, 0, 0);
}

uint64_t
loc_pim_stream_commit(loc_pim_request request)
{
    return m5_pim_transfer(M5_PIM_TRANSFER_COMMIT, request, 0, 0, 0);
}

uint64_t
loc_pim_stream_reap(loc_pim_request request)
{
    return m5_pim_transfer(M5_PIM_TRANSFER_REAP, request, 0, 0, 0);
}

uint64_t
loc_pim_stream_cancel(loc_pim_request request)
{
    return m5_pim_transfer(M5_PIM_TRANSFER_CANCEL, request, 0, 0, 0);
}

loc_pim_request
loc_pim_submit(uint64_t operation, loc_pim_object source_a,
    loc_pim_object source_b, loc_pim_object destination)
{
    return m5_pim_submit(operation, source_a, source_b, destination);
}

loc_pim_request
loc_pim_submit_scalar(uint64_t operation, loc_pim_object source,
    int64_t scalar, loc_pim_object destination)
{
    return m5_pim_submit_scalar(operation, source, destination, scalar);
}

uint64_t
loc_pim_poll(loc_pim_request request)
{
    return m5_pim_poll(request);
}

uint64_t
loc_pim_wait(loc_pim_request request)
{
    return m5_pim_wait(request);
}

uint64_t
loc_pim_complete(loc_pim_request request)
{
    if (!request)
        return M5_PIM_STATUS_INVALID_REQUEST;
    uint64_t status;
    do {
        status = loc_pim_wait(request);
    } while (status == M5_PIM_STATUS_QUEUE_RETRY);
    return status;
}

static uint64_t
stream_ready(loc_pim_request request)
{
    uint64_t status;
    do {
        status = loc_pim_stream_status(request);
    } while (status == M5_PIM_STATUS_QUEUE_RETRY);
    return status;
}

static uint64_t
stream_finish(loc_pim_request request)
{
    uint64_t status = loc_pim_stream_commit(request);
    if (status == M5_PIM_STATUS_QUEUE_RETRY)
        status = stream_ready(request);
    if (status == M5_PIM_TRANSFER_HOST_READY) {
        /* CPU coverage was incomplete. Release the host-phase lease. */
        loc_pim_stream_cancel(request);
        return M5_PIM_STATUS_COPY_FAILED;
    }
    const uint64_t reaped = loc_pim_stream_reap(request);
    return status ? status : reaped;
}

uint64_t
loc_pim_upload(loc_pim_object object, const void *source, size_t bytes)
{
    const loc_pim_request request = loc_pim_stream_begin(object, source,
        bytes, LOC_PIM_TO_DEVICE);
    if (!request)
        return M5_PIM_STATUS_COPY_FAILED;
    const volatile unsigned char *data = source;
    for (size_t i = 0; i < bytes; ++i)
        (void)data[i];
    return stream_finish(request);
}

uint64_t
loc_pim_download_placeholder(void *destination,
    loc_pim_object object, size_t bytes)
{
    const loc_pim_request request = loc_pim_stream_begin(object, destination,
        bytes, LOC_PIM_TO_HOST);
    if (!request)
        return M5_PIM_STATUS_COPY_FAILED;
    const uint64_t status = stream_ready(request);
    if (status != M5_PIM_TRANSFER_HOST_READY) {
        loc_pim_stream_reap(request);
        return status;
    }
    volatile unsigned char *data = destination;
    for (size_t i = 0; i < bytes; ++i)
        data[i] = 0;
    return stream_finish(request);
}
