/* One private enclave, one analytical ADD, no negative-test traffic. */
#include "app/eapp_utils.h"
#include "loc_pim.h"
#include "phases.h"

static volatile uint32_t input_a[LOC_VECTOR_ELEMENTS]
    __attribute__((aligned(4096)));
static volatile uint32_t input_b[LOC_VECTOR_ELEMENTS]
    __attribute__((aligned(4096)));
static volatile uint32_t output[LOC_VECTOR_ELEMENTS]
    __attribute__((aligned(4096)));

static void
require(int condition, unsigned long error)
{
    if (!condition)
        EAPP_RETURN(error);
}

void EAPP_ENTRY
eapp_entry(void)
{
    phase_end(LOC_VECTOR_ENTER);
    phase_begin(LOC_VECTOR_INITIALIZE);
    require(loc_pim_principal() != 0, 0xf101);
    for (unsigned long i = 0; i < LOC_VECTOR_ELEMENTS; ++i) {
        input_a[i] = i;
        input_b[i] = 3 * i;
        output[i] = 0xdeadbeef;
    }
    phase_end(LOC_VECTOR_INITIALIZE);

    phase_begin(LOC_VECTOR_ALLOCATE);
    const loc_pim_object a = loc_pim_alloc(LOC_VECTOR_BYTES);
    const loc_pim_object b = loc_pim_alloc(LOC_VECTOR_BYTES);
    const loc_pim_object c = loc_pim_alloc(LOC_VECTOR_BYTES);
    require(a && b && c &&
        !loc_pim_grant(a, LOC_PIM_INPUT_PERMISSIONS) &&
        !loc_pim_grant(b, LOC_PIM_INPUT_PERMISSIONS) &&
        !loc_pim_grant(c, LOC_PIM_OUTPUT_PERMISSIONS), 0xf102);
    phase_end(LOC_VECTOR_ALLOCATE);

    phase_begin(LOC_VECTOR_UPLOAD_A);
    require(!loc_pim_upload(a, (const void *)input_a, LOC_VECTOR_BYTES),
        0xf103);
    phase_end(LOC_VECTOR_UPLOAD_A);
    phase_begin(LOC_VECTOR_UPLOAD_B);
    require(!loc_pim_upload(b, (const void *)input_b, LOC_VECTOR_BYTES),
        0xf104);
    phase_end(LOC_VECTOR_UPLOAD_B);

    phase_begin(LOC_VECTOR_ADD);
    const loc_pim_request add =
        loc_pim_submit(M5_PIM_OP_ADD_INT32, a, b, c);
    require(add && !loc_pim_complete(add), 0xf105);
    phase_end(LOC_VECTOR_ADD);

    phase_begin(LOC_VECTOR_DOWNLOAD);
    require(!loc_pim_download_placeholder((void *)output, c,
        LOC_VECTOR_BYTES), 0xf106);
    phase_end(LOC_VECTOR_DOWNLOAD);

    phase_begin(LOC_VECTOR_CHECK);
    for (unsigned long i = 0; i < LOC_VECTOR_ELEMENTS; ++i) {
        /* Analytical mode checks host integrity and placeholder stores.
         * No numerical vector-add result is claimed. */
        require(input_a[i] == (uint32_t)i &&
            input_b[i] == (uint32_t)(3 * i) && output[i] == 0, 0xf107);
    }
    phase_end(LOC_VECTOR_CHECK);
    phase_begin(LOC_VECTOR_FREE);
    require(!loc_pim_free(a) && !loc_pim_free(b) && !loc_pim_free(c),
        0xf108);
    phase_end(LOC_VECTOR_FREE);
    phase_begin(LOC_VECTOR_EXIT);
    EAPP_RETURN(LOC_VECTOR_PASS);
}
