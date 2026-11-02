/* Private Eyrie U-mode arrays, real DMA payloads, functional PIM INT32 ADD. */
#include <asm/unistd.h>
#include <sys/mman.h>

#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "loc_pim.h"
#include "phases.h"

#define GUARD_BYTES 64UL
#define DATA_OFFSET 4096UL

static void
require(int condition, unsigned long error)
{
    if (!condition)
        EAPP_RETURN(error);
}

static uintptr_t
allocate_private(void)
{
    /* Existing Eyrie handles this syscall inside the protected runtime.
     * Large input arrays are allocated after enclave measurement. */
    register uintptr_t a0 __asm__("a0") = 0;
    register uintptr_t a1 __asm__("a1") = LOC_DMA_VECTOR_PRIVATE_BYTES;
    register uintptr_t a2 __asm__("a2") = PROT_READ | PROT_WRITE;
    register uintptr_t a3 __asm__("a3") = MAP_ANONYMOUS | MAP_PRIVATE;
    register uintptr_t a4 __asm__("a4") = (uintptr_t)-1;
    register uintptr_t a5 __asm__("a5") = 0;
    register uintptr_t a7 __asm__("a7") = __NR_mmap;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3),
        "r"(a4), "r"(a5), "r"(a7) : "memory");
    require(a0 && a0 != UINTPTR_MAX && !(a0 & 4095) &&
        LOC_DMA_VECTOR_PRIVATE_BYTES <= UINTPTR_MAX - a0, 0xf201);
    return a0;
}

/* Bit patterns represent signed INT32 inputs. Unsigned arithmetic in the
 * reference check defines two's-complement wraparound without C signed UB. */
static uint32_t
value_a(unsigned long index)
{
    switch (index & 7UL) {
      case 0: return (uint32_t)index;
      case 1: return 0x80000000U;
      case 2: return 0x7fffffffU;
      case 3: return 0xffffffffU;
      case 4: return 0x80000000U | ((uint32_t)index * 37U);
      case 5: return 0x7fffffffU - (uint32_t)index;
      case 6: return (uint32_t)index * 0x9e3779b9U + 0x12345678U;
      default: return 0x12345678U ^ ((uint32_t)index * 65537U);
    }
}

static uint32_t
value_b(unsigned long index)
{
    switch (index & 7UL) {
      case 0: return (uint32_t)index * 3U;
      case 1: return 0xffffffffU;
      case 2: return 1U;
      case 3: return 1U;
      case 4: return 0x80000000U;
      case 5: return 0U - (uint32_t)index;
      case 6: return (uint32_t)index * 0x85ebca6bU + 0xabcdef01U;
      default: return ~value_a(index);
    }
}

static void
initialize_guards(volatile uint32_t *array)
{
    volatile unsigned char *bytes = (volatile unsigned char *)array;
    for (unsigned long i = 0; i < GUARD_BYTES; ++i) {
        bytes[-(long)GUARD_BYTES + (long)i] = 0xd3;
        bytes[LOC_DMA_VECTOR_BYTES + i] = 0xd3;
    }
}

static void
check_guards(const volatile uint32_t *array)
{
    const volatile unsigned char *bytes =
        (const volatile unsigned char *)array;
    for (unsigned long i = 0; i < GUARD_BYTES; ++i)
        require(bytes[-(long)GUARD_BYTES + (long)i] == 0xd3 &&
            bytes[LOC_DMA_VECTOR_BYTES + i] == 0xd3, 0xf209);
}

void EAPP_ENTRY
eapp_entry(void)
{
    phase_end(LOC_DV_ENTER);
    phase_begin(LOC_DV_HOST_ALLOCATE);
    require(loc_pim_principal() &&
        loc_pim_transfer_model() == M5_PIM_TRANSFER_MODEL_DMA &&
        LOC_DMA_VECTOR_ELEMENTS >= 8, 0xf202);
    const uintptr_t memory = allocate_private();
    volatile uint32_t *input_a =
        (volatile uint32_t *)(memory + DATA_OFFSET);
    volatile uint32_t *input_b = (volatile uint32_t *)
        (memory + LOC_DMA_VECTOR_STRIDE + DATA_OFFSET);
    volatile uint32_t *output = (volatile uint32_t *)
        (memory + 2UL * LOC_DMA_VECTOR_STRIDE + DATA_OFFSET);
    phase_end(LOC_DV_HOST_ALLOCATE);

    phase_begin(LOC_DV_INITIALIZE);
    initialize_guards(input_a);
    initialize_guards(input_b);
    initialize_guards(output);
    for (unsigned long i = 0; i < LOC_DMA_VECTOR_ELEMENTS; ++i) {
        input_a[i] = value_a(i);
        input_b[i] = value_b(i);
        output[i] = 0xdeadbeefU;
    }
    /* No separate cache warmup or flush: initialization is explicit above. */
    phase_end(LOC_DV_INITIALIZE);

    phase_begin(LOC_DV_ALLOCATE_GRANT);
    const loc_pim_object a = loc_pim_alloc(LOC_DMA_VECTOR_BYTES);
    const loc_pim_object b = loc_pim_alloc(LOC_DMA_VECTOR_BYTES);
    const loc_pim_object c = loc_pim_alloc(LOC_DMA_VECTOR_BYTES);
    require(a && b && c && a != b && a != c && b != c &&
        !loc_pim_grant(a, LOC_PIM_INPUT_PERMISSIONS) &&
        !loc_pim_grant(b, LOC_PIM_INPUT_PERMISSIONS) &&
        !loc_pim_grant(c, LOC_PIM_OUTPUT_PERMISSIONS), 0xf203);
    phase_end(LOC_DV_ALLOCATE_GRANT);

    phase_begin(LOC_DV_UPLOAD_A);
    require(!loc_pim_upload(a, (const void *)input_a, LOC_DMA_VECTOR_BYTES),
        0xf204);
    phase_end(LOC_DV_UPLOAD_A);
    phase_begin(LOC_DV_UPLOAD_B);
    require(!loc_pim_upload(b, (const void *)input_b, LOC_DMA_VECTOR_BYTES),
        0xf205);
    phase_end(LOC_DV_UPLOAD_B);

    phase_begin(LOC_DV_ADD);
    const loc_pim_request add =
        loc_pim_submit(M5_PIM_OP_ADD_INT32, a, b, c);
    require(add && !loc_pim_complete(add), 0xf206);
    phase_end(LOC_DV_ADD);

    phase_begin(LOC_DV_DOWNLOAD);
    require(!loc_pim_download((void *)output, c, LOC_DMA_VECTOR_BYTES),
        0xf207);
    phase_end(LOC_DV_DOWNLOAD);

    phase_begin(LOC_DV_CHECK);
    for (unsigned long i = 0; i < LOC_DMA_VECTOR_ELEMENTS; ++i) {
        const uint32_t expected_a = value_a(i);
        const uint32_t expected_b = value_b(i);
        require(input_a[i] == expected_a && input_b[i] == expected_b &&
            output[i] == (uint32_t)(expected_a + expected_b), 0xf208);
    }
    check_guards(input_a);
    check_guards(input_b);
    check_guards(output);
    phase_end(LOC_DV_CHECK);

    phase_begin(LOC_DV_FREE);
    require(!loc_pim_free(a) && !loc_pim_free(b) && !loc_pim_free(c),
        0xf20a);
    phase_end(LOC_DV_FREE);
    phase_begin(LOC_DV_HOST_FREE);
    require(!SYSCALL_2(__NR_munmap, memory, LOC_DMA_VECTOR_PRIVATE_BYTES),
        0xf20b);
    phase_end(LOC_DV_HOST_FREE);
    phase_begin(LOC_DV_EXIT);
    EAPP_RETURN(LOC_DMA_VECTOR_PASS);
}
