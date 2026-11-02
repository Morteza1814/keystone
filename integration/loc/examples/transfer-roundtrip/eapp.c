/* Ordinary Eyrie U-mode app: functional DMA copies, no PIM kernel. */
#include <asm/unistd.h>
#include <sys/mman.h>

#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "loc_pim.h"
#include "protocol.h"

#define GUARD 64UL
#define COPY_PERMISSIONS \
    (LOC_PIM_COPY_TO_PERMISSIONS | LOC_PIM_COPY_FROM_PERMISSIONS)

static volatile unsigned char *source_a, *source_b, *destination;
static uint64_t principal, sequence, checked_bytes;

static void
require(int condition, unsigned long error)
{
    if (!condition)
        EAPP_RETURN(error);
}

static uint64_t
cycles(void)
{
    uint64_t value;
    __asm__ volatile("fence rw,rw; rdcycle %0" : "=r"(value) : : "memory");
    return value;
}

static unsigned char
pattern(unsigned long index, unsigned long seed)
{
    return (unsigned char)(index * 37 + (index >> 8) * 17 +
        (index >> 16) * 29 + (index >> 24) * 43 + seed);
}

static uintptr_t
allocate_private(void)
{
    /* Existing Eyrie mmap allocates private EPM pages after enclave entry.
     * The SDK convenience macro has only five arguments; mmap needs six. */
    register uintptr_t a0 __asm__("a0") = 0;
    register uintptr_t a1 __asm__("a1") = LOC_DMA_PRIVATE_BYTES;
    register uintptr_t a2 __asm__("a2") = PROT_READ | PROT_WRITE;
    register uintptr_t a3 __asm__("a3") = MAP_ANONYMOUS | MAP_PRIVATE;
    register uintptr_t a4 __asm__("a4") = (uintptr_t)-1;
    register uintptr_t a5 __asm__("a5") = 0;
    register uintptr_t a7 __asm__("a7") = __NR_mmap;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3),
        "r"(a4), "r"(a5), "r"(a7) : "memory");
    require(a0 && a0 != UINTPTR_MAX && !(a0 & 4095) &&
        LOC_DMA_PRIVATE_BYTES <= UINTPTR_MAX - a0, 0xd033);
    return a0;
}

static void
report(uint64_t kind, uint64_t case_id, uint64_t bytes, uint64_t elapsed,
    loc_pim_object object)
{
    struct loc_dma_message message =
    {
        kind, case_id, sequence++, bytes, elapsed, principal, object,
        checked_bytes
    };
    unsigned long reply = 0;
    require(!ocall(LOC_DMA_OCALL, &message, sizeof(message), &reply,
        sizeof(reply)) && reply == LOC_DMA_MAGIC, 0xd001);
    require(loc_pim_principal() == principal, 0xd002);
}

static void
prepare(volatile unsigned char *storage, unsigned long offset,
    unsigned long size, unsigned long seed)
{
    for (unsigned long i = offset - GUARD; i < offset + size + GUARD; ++i)
        storage[i] = 0xd3;
    for (unsigned long i = 0; i < size; ++i)
        storage[offset + i] = pattern(i, seed);
}

static void
guards(const volatile unsigned char *storage, unsigned long offset,
    unsigned long size)
{
    for (unsigned long i = 0; i < GUARD; ++i)
        require(storage[offset - GUARD + i] == 0xd3 &&
            storage[offset + size + i] == 0xd3, 0xd003);
    checked_bytes += 2 * GUARD;
}

static void
upload(unsigned long case_id, loc_pim_object object,
    const volatile unsigned char *data, unsigned long size)
{
    const uint64_t start = cycles();
    require(!loc_pim_upload(object, (const void *)data, size), 0xd004);
    const uint64_t elapsed = cycles() - start;
    require(elapsed != 0, 0xd005);
    report(LOC_DMA_UPLOAD, case_id, size, elapsed, object);
}

static void
download(unsigned long case_id, loc_pim_object object, unsigned long offset,
    unsigned long size, unsigned long seed, unsigned long prefix,
    unsigned long prefix_seed)
{
    /* These writes deliberately leave dirty cache lines around the target. */
    prepare(destination, offset, size, 0xa6);
    const uint64_t start = cycles();
    require(!loc_pim_download((void *)(destination + offset), object, size),
        0xd006);
    const uint64_t elapsed = cycles() - start;
    require(elapsed != 0, 0xd007);
    for (unsigned long i = 0; i < size; ++i)
        require(destination[offset + i] ==
            pattern(i, i < prefix ? prefix_seed : seed), 0xd008);
    checked_bytes += size;
    guards(destination, offset, size);
    report(LOC_DMA_DOWNLOAD, case_id, size, elapsed, object);
}

static void
negative_checks(loc_pim_object object, const volatile unsigned char *source,
    unsigned long size)
{
    require(loc_pim_check_buffer(object, (const void *)source, size,
        LOC_PIM_TO_HOST) == M5_PIM_STATUS_UNINITIALIZED_OBJECT, 0xd010);
    require(!loc_pim_stream_begin(object, (const void *)source, size,
        LOC_PIM_TO_HOST), 0xd011);
    require(loc_pim_check_buffer(object, (const void *)source, 0,
        LOC_PIM_TO_DEVICE) == M5_PIM_STATUS_INVALID_HOST_RANGE, 0xd012);
    require(loc_pim_check_buffer(object, (void *)(UINTPTR_MAX - 7), 16,
        LOC_PIM_TO_DEVICE) == M5_PIM_STATUS_INVALID_HOST_RANGE, 0xd013);
    require(loc_pim_check_buffer(object, (const void *)source, size + 1,
        LOC_PIM_TO_DEVICE) != 0, 0xd014);
    require(loc_pim_check_buffer(object, (void *)0x30000000UL, 1,
        LOC_PIM_TO_DEVICE) != 0 &&
        loc_pim_check_buffer(object, (void *)LOC_DMA_UTM, 1,
            LOC_PIM_TO_DEVICE) != 0, 0xd015);
    require(!loc_pim_stream_begin(object, (void *)LOC_DMA_UTM, 1,
        LOC_PIM_TO_DEVICE), 0xd016);
}

static void
initialized_checks(loc_pim_object a, loc_pim_object b,
    const volatile unsigned char *source, unsigned long size)
{
    /* Establish that both directions work before removing one permission. */
    require(!loc_pim_check_buffer(a, (const void *)source, size,
        LOC_PIM_TO_HOST) && !loc_pim_check_buffer(a, (const void *)source,
        size, LOC_PIM_TO_DEVICE), 0xd017);
    require(!loc_pim_grant(a, LOC_PIM_COPY_TO_PERMISSIONS), 0xd018);
    require(loc_pim_check_buffer(a, (const void *)source, size,
        LOC_PIM_TO_HOST) != 0 &&
        !loc_pim_stream_begin(a, (const void *)source, size,
            LOC_PIM_TO_HOST) &&
        !loc_pim_check_buffer(a, (const void *)source, size,
            LOC_PIM_TO_DEVICE), 0xd019);
    require(!loc_pim_grant(a, LOC_PIM_COPY_FROM_PERMISSIONS), 0xd01a);
    require(loc_pim_check_buffer(a, (const void *)source, size,
        LOC_PIM_TO_DEVICE) != 0 &&
        !loc_pim_stream_begin(a, (const void *)source, size,
            LOC_PIM_TO_DEVICE) &&
        !loc_pim_check_buffer(a, (const void *)source, size,
            LOC_PIM_TO_HOST), 0xd01b);
    const uint64_t compute_permissions =
        LOC_PIM_INPUT_PERMISSIONS | LOC_PIM_OUTPUT_PERMISSIONS;
    require(!loc_pim_grant(a, compute_permissions) &&
        !loc_pim_grant(b, compute_permissions), 0xd01c);
    /* Initialized, equal-size UINT8 operands with read/write/execute rights.
     * DMA mode must reject the otherwise valid in-place analytical kernel. */
    require(!loc_pim_submit(M5_PIM_OP_ADD_UINT8, a, b, a), 0xd01d);
    require(!loc_pim_grant(a, COPY_PERMISSIONS) &&
        !loc_pim_grant(b, COPY_PERMISSIONS), 0xd01e);
}

static void
run_case(unsigned long case_id, unsigned long size, unsigned long offset)
{
    const unsigned long second_offset = offset + 64;
    const unsigned long output_offset = offset == 4093 ? 4091 : offset;
    const unsigned long prefix = size / 2 + 1;
    prepare(source_a, offset, size, 11);
    prepare(source_b, second_offset, size, 73);
    const loc_pim_object a = loc_pim_alloc(size);
    const loc_pim_object b = loc_pim_alloc(size);
    require(a && b && a != b && !loc_pim_grant(a, COPY_PERMISSIONS) &&
        !loc_pim_grant(b, COPY_PERMISSIONS), 0xd020);
    negative_checks(a, source_a + offset, size);
    upload(case_id, a, source_a + offset, size);
    upload(case_id, b, source_b + second_offset, size);
    initialized_checks(a, b, source_a + offset, size);
    /* A later download must use device storage, not the current source. */
    for (unsigned long i = 0; i < size; ++i) {
        source_a[offset + i] = pattern(i, 151);
        source_b[second_offset + i] = pattern(i, 211);
    }
    download(case_id, a, output_offset, size, 11, 0, 0);
    download(case_id, b, output_offset, size, 73, 0, 0);
    upload(case_id, a, source_a + offset, prefix);
    download(case_id, a, output_offset, size, 11, prefix, 151);
    download(case_id, b, output_offset, size, 73, 0, 0);
    guards(source_a, offset, size);
    guards(source_b, second_offset, size);
    for (unsigned long i = 0; i < size; ++i)
        require(source_a[offset + i] == pattern(i, 151) &&
            source_b[second_offset + i] == pattern(i, 211), 0xd021);
    checked_bytes += 2 * size;
    require(!loc_pim_free(a) && !loc_pim_free(b), 0xd022);
    require(loc_pim_download((void *)(destination + output_offset), a, size)
        == M5_PIM_STATUS_COPY_FAILED, 0xd023);
    const loc_pim_object fresh = loc_pim_alloc(size);
    require(fresh && fresh != a && fresh != b &&
        !loc_pim_grant(fresh, COPY_PERMISSIONS), 0xd024);
    require(loc_pim_check_buffer(fresh,
        (const void *)(destination + output_offset), size, LOC_PIM_TO_HOST)
        == M5_PIM_STATUS_UNINITIALIZED_OBJECT, 0xd025);
    require(!loc_pim_free(fresh), 0xd026);
}

void EAPP_ENTRY
eapp_entry(void)
{
    principal = loc_pim_principal();
    require(principal && loc_pim_transfer_model() == M5_PIM_TRANSFER_MODEL_DMA,
        0xd030);
    require(LOC_DMA_BYTES >= 4097, 0xd031);
    const uintptr_t memory = allocate_private();
    source_a = (volatile unsigned char *)memory;
    source_b = source_a + LOC_DMA_BUFFER_STRIDE;
    destination = source_b + LOC_DMA_BUFFER_STRIDE;
    run_case(0, 17, 67);
    run_case(1, 4097, 4093);
    run_case(2, LOC_DMA_BYTES, 4096);
    require(sequence == LOC_DMA_CASES * LOC_DMA_TRANSFERS_PER_CASE, 0xd032);
    require(!SYSCALL_2(__NR_munmap, memory, LOC_DMA_PRIVATE_BYTES), 0xd034);
    source_a = source_b = destination = 0;
    report(LOC_DMA_DONE, LOC_DMA_CASES, LOC_DMA_BYTES, 0, 0);
    EAPP_RETURN(LOC_DMA_MAGIC);
}
