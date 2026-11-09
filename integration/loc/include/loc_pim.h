/* Keystone application interface to gem5's LOC model. */
#ifndef KEYSTONE_LOC_PIM_H
#define KEYSTONE_LOC_PIM_H

#include <stddef.h>
#include <stdint.h>

#include <gem5/m5ops.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t loc_pim_object;
typedef uint64_t loc_pim_request;

/* Input: READ | EXECUTE | COPY_TO. Output: WRITE | EXECUTE | COPY_FROM.
 * Grants replace the caller's permissions; they never install an identity. */
#define LOC_PIM_INPUT_PERMISSIONS 0x29ULL
#define LOC_PIM_OUTPUT_PERMISSIONS 0x1aULL
#define LOC_PIM_COPY_TO_PERMISSIONS M5_PIM_PERM_DMA_WRITE_OBJECT
#define LOC_PIM_COPY_FROM_PERMISSIONS M5_PIM_PERM_DMA_READ_OBJECT

enum loc_pim_direction
{
    LOC_PIM_TO_DEVICE = 0,
    LOC_PIM_TO_HOST = 1
};

/* Identity is supplied by the monitor. Zero means no active enclave. */
uint64_t loc_pim_principal(void);
/* Trusted selection: CPU=0, analytical bulk=1, functional DMA=2. */
uint64_t loc_pim_transfer_model(void);
loc_pim_object loc_pim_alloc(size_t bytes);
loc_pim_object loc_pim_alloc_placed(size_t rows, size_t columns, loc_pim_object associated);
uint64_t loc_pim_grant(loc_pim_object object, uint64_t permissions);
/* Owner-only FS sharing. Recipient is a live monitor-issued lifetime
 * principal, never a Linux PID, EID alone, or caller-selected identity.
 * Revoke rejects new admissions; already admitted work retains its holds
 * and may finish. Passing a principal selects a recipient, not the caller.
 */
uint64_t loc_pim_grant_to(loc_pim_object object, uint64_t principal,
    uint64_t permissions);
uint64_t loc_pim_revoke_from(loc_pim_object object, uint64_t principal);
uint64_t loc_pim_free(loc_pim_object object);

/* Validation only, not a transferable authorization or an allocation-bound
 * proof. Admission validates again and pins the EPM/object lifetime. */
uint64_t loc_pim_check_buffer(loc_pim_object object, const void *buffer,
    size_t bytes, enum loc_pim_direction direction);

/* BEGIN returns zero on denial. Other stream operations return status codes.
 * A TO_DEVICE stream starts HOST_READY: read every admitted byte, then COMMIT.
 * A TO_HOST stream starts pending: await HOST_READY, store every byte, COMMIT.
 * REAP consumes a completed stream. CANCEL releases a host-phase stream;
 * device work must drain before cancellation. Only the owner can use a handle.
 * In bulk/DMA mode BEGIN schedules device work, and STATUS/REAP suffice;
 * no host CPU coverage or COMMIT is used. DMA BEGIN holds the requesting
 * CPU while timed admission captures authorized physical spans; device work
 * then runs asynchronously. Device completion does not wake a hart. */
loc_pim_request loc_pim_stream_begin(loc_pim_object object, const void *buffer,
    size_t bytes, enum loc_pim_direction direction);
uint64_t loc_pim_stream_status(loc_pim_request request);
uint64_t loc_pim_stream_commit(loc_pim_request request);
uint64_t loc_pim_stream_reap(loc_pim_request request);
uint64_t loc_pim_stream_cancel(loc_pim_request request);

/* SUBMIT returns zero if no request was admitted; a denied command can have a
 * nonzero request with a failure result. POLL preserves the result. WAIT is
 * nonblocking in FS: pending or a consumed final result. COMPLETE spins on
 * WAIT until final. One unfinished command/stream is allowed per lifetime. */
loc_pim_request loc_pim_submit(uint64_t operation, loc_pim_object source_a,
    loc_pim_object source_b, loc_pim_object destination);
loc_pim_request loc_pim_submit_scalar(uint64_t operation,
    loc_pim_object source, int64_t scalar, loc_pim_object destination);
uint64_t loc_pim_poll(loc_pim_request request);
uint64_t loc_pim_wait(loc_pim_request request);
uint64_t loc_pim_complete(loc_pim_request request);

/* Raw CPU loops for calibration: no LOC admission or PIM transfer. The caller
 * supplies accessible memory. Aligned 64-bit accesses are unrolled across
 * 64-byte blocks; byte accesses handle unaligned heads and short tails. */
void loc_pim_cpu_read(const void *source, size_t bytes);
void loc_pim_cpu_zero(void *destination, size_t bytes);

/* Synchronous helpers spin until completion. Failed BEGIN returns COPY_FAILED
 * without dereferencing the rejected host buffer. In CPU mode UPLOAD reads
 * host bytes and DOWNLOAD_PLACEHOLDER writes zeroes, with analytical PIM
 * payloads. Bulk mode waits for analytical copies without accessing bytes.
 * DMA mode makes UPLOAD functional. DOWNLOAD is the matching functional API
 * and rejects other modes; DOWNLOAD_PLACEHOLDER rejects DMA mode. */
uint64_t loc_pim_upload(loc_pim_object object, const void *source,
    size_t bytes);
uint64_t loc_pim_download(void *destination, loc_pim_object object,
    size_t bytes);
/* Parent-bounded functional DMA; uploads cannot leave uninitialized holes. */
uint64_t loc_pim_upload_at(loc_pim_object object, size_t offset,
    const void *source, size_t bytes);
uint64_t loc_pim_download_at(void *destination, loc_pim_object object,
    size_t offset, size_t bytes);
loc_pim_request loc_pim_axpy_range(loc_pim_object source,
    loc_pim_object destination, int64_t scalar, size_t offset, size_t bytes);
uint64_t loc_pim_download_placeholder(void *destination,
    loc_pim_object object, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
