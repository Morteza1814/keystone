/* Normal U-mode application. Eyrie handles ocalls, timer stops and exit. */
#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "loc_pim.h"
#include "protocol.h"

/* Each input starts 16 bytes before a page boundary. */
static uint32_t storage_a[1280] __attribute__((aligned(4096)));
static uint32_t storage_b[1280] __attribute__((aligned(4096)));
static uint32_t output[LOC_APP_ELEMENTS] __attribute__((aligned(64)));
extern unsigned char _end[];

static void
require(int condition, unsigned long error)
{
    if (!condition)
        EAPP_RETURN(error);
}

static struct loc_app_reply
report(unsigned long phase, unsigned long principal, unsigned long object,
    unsigned long stream, unsigned long command, unsigned long checks,
    unsigned long elapsed)
{
    struct loc_app_message message =
    {
        phase, principal, object, stream, command, checks, elapsed
    };
    struct loc_app_reply reply =
    {
        0};
    require(!ocall(LOC_APP_OCALL, &message, sizeof(message),
        &reply, sizeof(reply)) && reply.magic == LOC_APP_MAGIC,
        0xf080 + phase);
    require(loc_pim_principal() == principal, 0xf084);
    return reply;
}

static unsigned long
clock_time(void)
{
    unsigned long value;
    __asm__ volatile("rdtime %0" : "=r"(value));
    return value;
}

static void
buffer_checks(loc_pim_object a, loc_pim_object c, const void *source)
{
    require(!loc_pim_check_buffer(a, source, LOC_APP_BYTES,
        LOC_PIM_TO_DEVICE), 0xf085);
    require(!loc_pim_check_buffer(c, output, LOC_APP_BYTES,
        LOC_PIM_TO_HOST), 0xf086);
    require(!loc_pim_check_buffer(a, _end - 16, 16,
        LOC_PIM_TO_DEVICE), 0xf087);
    require(loc_pim_check_buffer(a, _end - 16, 32, LOC_PIM_TO_DEVICE) ==
        M5_PIM_STATUS_HOST_TRANSLATION_FAILED, 0xf088);
    require(loc_pim_check_buffer(a, (void *)(UINTPTR_MAX - 7), 16,
        LOC_PIM_TO_DEVICE) == M5_PIM_STATUS_INVALID_HOST_RANGE, 0xf089);
    require(loc_pim_check_buffer(a, source, 0, LOC_PIM_TO_DEVICE) ==
        M5_PIM_STATUS_INVALID_HOST_RANGE, 0xf08a);
    require(loc_pim_check_buffer(a, (void *)0x30000000UL, 16,
        LOC_PIM_TO_DEVICE) == M5_PIM_STATUS_HOST_TRANSLATION_FAILED, 0xf08b);
    /* This Eyrie/SDK maps UTM for S mode only. A U caller is rejected during
     * translation, before private-EPM ownership could grant any access. */
    require(loc_pim_check_buffer(a, (void *)LOC_APP_UTM, 16,
        LOC_PIM_TO_DEVICE) == M5_PIM_STATUS_HOST_TRANSLATION_FAILED, 0xf08c);
    require(loc_pim_check_buffer(a, output, 16, LOC_PIM_TO_HOST) != 0, 0xf08d);
    require(!loc_pim_stream_begin(a, _end - 16, 32, LOC_PIM_TO_DEVICE) &&
        !loc_pim_stream_begin(a, (void *)(UINTPTR_MAX - 7), 16,
            LOC_PIM_TO_DEVICE) &&
        !loc_pim_stream_begin(a, (void *)LOC_APP_UTM, 16, LOC_PIM_TO_DEVICE) &&
        !loc_pim_stream_begin(a, output, 16, LOC_PIM_TO_HOST), 0xf08e);
    /* Rejected pointers must not be dereferenced by convenience helpers. */
    require(loc_pim_upload(a, (void *)0x30000000UL, 16) ==
        M5_PIM_STATUS_COPY_FAILED &&
        loc_pim_download_placeholder((void *)0x30000000UL, c, 16) ==
        M5_PIM_STATUS_COPY_FAILED, 0xf08f);
}

void EAPP_ENTRY
eapp_entry(void)
{
    const unsigned long principal = loc_pim_principal();
    require(principal != 0, 0xf090);
    const struct loc_app_reply setup =
        report(LOC_APP_HELLO, principal, 0, 0, 0, 0, 0);
    require(setup.role <= 2, 0xf091);
    uint32_t *input_a = storage_a + 1020;
    uint32_t *input_b = storage_b + 1020;
    for (unsigned long i = 0; i < LOC_APP_ELEMENTS; ++i) {
        input_a[i] = i;
        input_b[i] = 3 * i;
        output[i] = 0xdeadbeef;
    }
    const loc_pim_object a = loc_pim_alloc(LOC_APP_BYTES);
    const loc_pim_object b = loc_pim_alloc(LOC_APP_BYTES);
    const loc_pim_object c = loc_pim_alloc(LOC_APP_BYTES);
    require(a && b && c &&
        !loc_pim_grant(a, LOC_PIM_INPUT_PERMISSIONS) &&
        !loc_pim_grant(b, LOC_PIM_INPUT_PERMISSIONS) &&
        !loc_pim_grant(c, LOC_PIM_OUTPUT_PERMISSIONS), 0xf092);
    buffer_checks(a, c, input_a);

    if (setup.role) {
        require(setup.foreign_object && setup.foreign_stream &&
            loc_pim_stream_status(setup.foreign_stream) ==
                M5_PIM_STATUS_INVALID_REQUEST &&
            loc_pim_stream_commit(setup.foreign_stream) ==
                M5_PIM_STATUS_INVALID_REQUEST &&
            loc_pim_stream_reap(setup.foreign_stream) ==
                M5_PIM_STATUS_INVALID_REQUEST &&
            loc_pim_stream_cancel(setup.foreign_stream) ==
                M5_PIM_STATUS_INVALID_REQUEST, 0xf093);
        require(loc_pim_free(setup.foreign_object) != 0 &&
            loc_pim_grant(setup.foreign_object,
                LOC_PIM_INPUT_PERMISSIONS) != 0 &&
            loc_pim_check_buffer(setup.foreign_object, input_a,
                LOC_APP_BYTES, LOC_PIM_TO_DEVICE) != 0, 0xf094);
        const loc_pim_request denied = loc_pim_submit(M5_PIM_OP_ADD_INT32,
            setup.foreign_object, b, c);
        require(denied && loc_pim_complete(denied) != 0, 0xf095);
        if (setup.role == 2) {
            require(setup.foreign_command &&
                loc_pim_poll(setup.foreign_command) ==
                    M5_PIM_STATUS_INVALID_REQUEST &&
                loc_pim_wait(setup.foreign_command) ==
                    M5_PIM_STATUS_INVALID_REQUEST, 0xf096);
        }
    } else {
        require(!loc_pim_grant(a, LOC_PIM_COPY_TO_PERMISSIONS), 0xf097);
        const loc_pim_request denied =
            loc_pim_submit(M5_PIM_OP_ADD_INT32, a, b, c);
        require(denied && loc_pim_complete(denied) != 0 &&
            !loc_pim_grant(a, LOC_PIM_INPUT_PERMISSIONS), 0xf098);
        const loc_pim_request duplicate =
            loc_pim_stream_begin(a, input_a, 16, LOC_PIM_TO_DEVICE);
        require(duplicate != 0, 0xf099);
        const volatile unsigned char *byte = (const void *)input_a;
        (void)*byte;
        (void)*byte;
        require(loc_pim_stream_commit(duplicate) ==
            M5_PIM_STATUS_QUEUE_RETRY && !loc_pim_stream_cancel(duplicate) &&
            loc_pim_stream_status(duplicate) ==
                M5_PIM_STATUS_INVALID_REQUEST, 0xf09a);
    }

    loc_pim_request first_stream = 0;
    unsigned long elapsed = 0;
    if (!setup.role) {
        first_stream = loc_pim_stream_begin(a, input_a,
            LOC_APP_BYTES, LOC_PIM_TO_DEVICE);
        require(first_stream != 0, 0xf09b);
        report(LOC_APP_STREAM, principal, a, first_stream, 0, 0, 0);
        /* Keep the host phase pinned across a real timer interruption/resume.
         * The test driver arms a 10000-tick timer before SM resume. */
        const unsigned long start = clock_time();
        do {
            elapsed = clock_time() - start;
        } while (elapsed < 20000);
        require(loc_pim_principal() == principal &&
            loc_pim_stream_status(first_stream) ==
                M5_PIM_TRANSFER_HOST_READY &&
            loc_pim_stream_commit(first_stream) ==
                M5_PIM_STATUS_QUEUE_RETRY, 0xf09c);
        const volatile unsigned char *bytes = (const void *)input_a;
        for (unsigned long i = 0; i < LOC_APP_BYTES; ++i)
            (void)bytes[i];
        require(loc_pim_stream_commit(first_stream) ==
            M5_PIM_STATUS_QUEUE_RETRY, 0xf09d);
        while (loc_pim_stream_status(first_stream) ==
            M5_PIM_STATUS_QUEUE_RETRY) {}
        require(!loc_pim_stream_reap(first_stream), 0xf09e);
    } else {
        require(!loc_pim_upload(a, input_a, LOC_APP_BYTES), 0xf09f);
    }
    require(!loc_pim_upload(b, input_b, LOC_APP_BYTES), 0xf0a0);
    const loc_pim_request command =
        loc_pim_submit(M5_PIM_OP_ADD_INT32, a, b, c);
    require(command != 0, 0xf0a1);
    if (!setup.role) {
        require(loc_pim_poll(command) == M5_PIM_STATUS_QUEUE_RETRY &&
            loc_pim_wait(command) == M5_PIM_STATUS_QUEUE_RETRY &&
            !loc_pim_submit(M5_PIM_OP_ADD_INT32, a, b, c) &&
            !loc_pim_stream_begin(c, output, LOC_APP_BYTES,
                LOC_PIM_TO_HOST), 0xf0a2);
        report(LOC_APP_COMMAND, principal, a, first_stream, command,
            0, elapsed);
        require(loc_pim_poll(command) == 0, 0xf0a3);
    }
    require(!loc_pim_complete(command) &&
        loc_pim_poll(command) == M5_PIM_STATUS_INVALID_REQUEST &&
        loc_pim_wait(command) == M5_PIM_STATUS_INVALID_REQUEST, 0xf0a4);
    if (setup.role == 1) {
        const loc_pim_request scalar = loc_pim_submit_scalar(
            M5_PIM_OP_ADD_SCALAR_INT16, a, -7, c);
        require(scalar && !loc_pim_complete(scalar), 0xf0a9);
    }
    require(!loc_pim_download_placeholder(output, c, LOC_APP_BYTES), 0xf0a5);
    for (unsigned long i = 0; i < LOC_APP_ELEMENTS; ++i)
        require(input_a[i] == i && input_b[i] == 3 * i && output[i] == 0,
            0xf0a6);
    require(!loc_pim_free(a) && !loc_pim_free(b) && !loc_pim_free(c), 0xf0a7);
    require(loc_pim_principal() == principal &&
        ((uintptr_t)input_a & 4095) + LOC_APP_BYTES > 4096, 0xf0a8);
    report(LOC_APP_DONE, principal, a, first_stream, command,
        LOC_APP_CHECKS, elapsed);
    EAPP_RETURN(LOC_APP_MAGIC);
}
