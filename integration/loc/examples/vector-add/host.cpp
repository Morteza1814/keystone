/* Lifecycle/negative-test launcher for the ordinary Eyrie U-mode app. */
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "edge/edge_common.h"
#include "host/keystone.h"
#include "loc_pim.h"
#include "protocol.h"

struct Record
{
    Keystone::Enclave *enclave = nullptr;
    int fd = -1;
    bool active = false;
    bool timer_armed = false;
    uintptr_t uid = 0;
    uintptr_t eid = UINTPTR_MAX;
    uintptr_t epm_bytes = 0;
    uintptr_t principal = 0;
    uintptr_t object = 0;
    uintptr_t stream = 0;
    uintptr_t command = 0;
    unsigned interrupts = 0;
    unsigned stream_interrupts = 0;
    unsigned resumes = 0;
    int phase = -1;
};

static Record records[3];
static unsigned record_count;
static unsigned busy_checks;
static const char *app_path;
static const char *runtime_path;

extern "C" int __real_ioctl(int, unsigned long, ...);

[[noreturn]] static void
finish(int status)
{
    // The pinned SDK does not invalidate an explicitly destroyed EID.
    // Avoid repeated destruction from its destructor; Linux reclaims FDs.
    std::fflush(nullptr);
    std::_Exit(status);
}

static void
require(bool condition, const char *phase)
{
    if (!condition) {
        std::fprintf(stderr, "KEYSTONE_EYRIE_FAIL phase=%s\n", phase);
        finish(1);
    }
}

static bool
outside_checks(uintptr_t object, uintptr_t stream, uintptr_t command)
{
    return loc_pim_principal() == 0 && loc_pim_alloc(16) == 0 &&
        loc_pim_check_buffer(object, nullptr, 16, LOC_PIM_TO_DEVICE) ==
            M5_PIM_STATUS_NO_ACTIVE_ENCLAVE &&
        loc_pim_stream_begin(object, nullptr, 16, LOC_PIM_TO_DEVICE) == 0 &&
        loc_pim_stream_status(stream) == M5_PIM_STATUS_INVALID_REQUEST &&
        loc_pim_stream_commit(stream) == M5_PIM_STATUS_INVALID_REQUEST &&
        loc_pim_stream_reap(stream) == M5_PIM_STATUS_INVALID_REQUEST &&
        loc_pim_stream_cancel(stream) == M5_PIM_STATUS_INVALID_REQUEST &&
        loc_pim_submit(M5_PIM_OP_ADD_INT32, object, object, object) == 0 &&
        loc_pim_poll(command) == M5_PIM_STATUS_INVALID_REQUEST &&
        loc_pim_wait(command) == M5_PIM_STATUS_INVALID_REQUEST;
}

extern "C" int
__wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    Record *record = nullptr;
    for (unsigned i = 0; i < record_count; ++i)
        if (records[i].active && records[i].fd == fd)
            record = &records[i];
    auto *create = static_cast<keystone_ioctl_create_enclave *>(argument);
    if (request == KEYSTONE_IOC_FINALIZE_ENCLAVE) {
        create->epm_paddr = UINTPTR_MAX;
        create->utm_paddr = 0;
    } else if (request == KEYSTONE_IOC_DESTROY_ENCLAVE) {
        create->epm_paddr = 0;
        create->epm_size = 0;
    }
    if (record == &records[0] && request == KEYSTONE_IOC_RESUME_ENCLAVE &&
        record->phase == LOC_APP_STREAM && !record->timer_armed) {
        auto *run = static_cast<keystone_ioctl_run_enclave *>(argument);
        // Test-driver one-shot timer, armed immediately before SM resume.
        // Output fields are otherwise ignored as inputs by the driver.
        run->error = 0x4c4f4354UL;
        run->value = 10000;
        record->timer_armed = true;
    }
    const int result = __real_ioctl(fd, request, argument);
    if (result)
        return result;
    if (request == KEYSTONE_IOC_CREATE_ENCLAVE) {
        require(record_count < 3, "unexpected_create");
        record = &records[record_count++];
        record->fd = fd;
        record->uid = create->eid;
        record->epm_bytes = create->epm_size;
        record->active = true;
    } else if (record && request == KEYSTONE_IOC_FINALIZE_ENCLAVE) {
        require(create->utm_paddr == 0x4b49534fUL, "test_driver_finalize");
        record->eid = create->epm_paddr;
    } else if (record && request == KEYSTONE_IOC_DESTROY_ENCLAVE) {
        require(create->epm_paddr == 0x4b49534fUL &&
            create->epm_size == record->epm_bytes, "epm_scrub");
        record->active = false;
        std::printf("KEYSTONE_EYRIE_SCRUB role=%ld eid=%lu bytes=%lu\n",
            record - records, record->eid, record->epm_bytes);
    } else if (record && (request == KEYSTONE_IOC_RUN_ENCLAVE ||
        request == KEYSTONE_IOC_RESUME_ENCLAVE)) {
        if (request == KEYSTONE_IOC_RESUME_ENCLAVE)
            ++record->resumes;
        const auto *run = static_cast<keystone_ioctl_run_enclave *>(argument);
        if (run->error == 100002) {
            ++record->interrupts;
            if (record->phase == LOC_APP_STREAM)
                ++record->stream_interrupts;
            require(outside_checks(record->object, record->stream,
                record->command), "outside_during_timer");
        }
    }
    return 0;
}

static void dispatch(void *buffer);

static void
initialize(unsigned role)
{
    require(record_count == role, "create_order");
    auto *enclave = new Keystone::Enclave;
    records[role].enclave = enclave;
    Keystone::Params params;
    params.setFreeMemSize(256 * 1024);
    params.setUntrustedMem(LOC_APP_UTM, LOC_APP_UTM_BYTES);
    require(enclave->init(app_path, runtime_path, params) ==
        Keystone::Error::Success && record_count == role + 1, "create");
    enclave->registerOcallDispatch(dispatch);
}

static void
run(unsigned role)
{
    uintptr_t value = UINTPTR_MAX;
    require(records[role].enclave->run(&value) == Keystone::Error::Success,
        "run");
    if (value != LOC_APP_MAGIC)
        std::fprintf(stderr, "KEYSTONE_EYRIE_APP_ERROR role=%u code=%#lx\n",
            role, value);
    require(value == LOC_APP_MAGIC && records[role].phase == LOC_APP_DONE,
        "app_return");
}

static void
destroy(unsigned role)
{
    require(records[role].enclave->destroy() == Keystone::Error::Success &&
        !records[role].active, "destroy");
}

static void
dispatch(void *buffer)
{
    unsigned role = 3;
    for (unsigned i = 0; i < record_count; ++i)
        if (records[i].active &&
            records[i].enclave->getSharedBuffer() == buffer)
            role = i;
    require(role < 3, "ocall_owner");
    auto &record = records[role];
    auto *edge = static_cast<edge_call *>(buffer);
    const size_t capacity = record.enclave->getSharedBufferSize();
    require(edge->call_id == LOC_APP_OCALL &&
        edge->call_arg_size == sizeof(loc_app_message) &&
        edge->call_arg_offset >= sizeof(edge_call) &&
        edge->call_arg_offset <= capacity - sizeof(loc_app_message),
        "ocall_arguments");
    loc_app_message message;
    std::memcpy(&message, static_cast<char *>(buffer) + edge->call_arg_offset,
        sizeof(message));
    require(message.principal != 0 && (record.principal == 0 ||
        message.principal == record.principal), "ocall_identity");
    record.principal = message.principal;
    loc_app_reply reply = {LOC_APP_MAGIC, role, records[0].object,
        records[0].stream, records[0].command};
    if (message.phase == LOC_APP_HELLO) {
        require(record.phase == -1, "hello_order");
        for (unsigned i = 0; i < role; ++i)
            require(records[i].principal != message.principal,
                "fresh_principal");
    } else if (message.phase == LOC_APP_STREAM ||
        message.phase == LOC_APP_COMMAND) {
        require(role == 0 && message.object && message.stream &&
            ((message.phase == LOC_APP_STREAM && record.phase == LOC_APP_HELLO)
             || (message.phase == LOC_APP_COMMAND &&
                 record.phase == LOC_APP_STREAM && message.command &&
                 record.stream_interrupts > 0 && message.elapsed >= 20000)),
            "pending_order");
        // Call the real ioctl first, while the stopped owner still has a
        // pending lease. Bypass SDK destroy(), which is not a busy probe API.
        keystone_ioctl_create_enclave pending = {};
        pending.eid = record.uid;
        require(__real_ioctl(record.fd, KEYSTONE_IOC_DESTROY_ENCLAVE,
            &pending) == -1, "destroy_pending");
        ++busy_checks;
        record.object = message.object;
        record.stream = message.stream;
        record.command = message.command;
        require(outside_checks(record.object, record.stream, record.command),
            "outside_during_ocall");
        if (message.phase == LOC_APP_STREAM) {
            initialize(1);
            run(1);
            destroy(1);
        } else {
            // Force the analytical command to complete while Linux runs.
            usleep(10000);
        }
        std::printf("KEYSTONE_EYRIE_PENDING phase=%lu busy_destroy=1\n",
            message.phase);
    } else if (message.phase == LOC_APP_DONE) {
        require(message.checks == LOC_APP_CHECKS && message.object &&
            message.command && ((role == 0 && record.phase == LOC_APP_COMMAND)
             || (role != 0 && record.phase == LOC_APP_HELLO)), "done_checks");
        record.object = message.object;
        record.command = message.command;
        std::printf("KEYSTONE_EYRIE_APP role=%u principal=%lu object=%lu "
            "command=%lu checks=%lu elements=%lu uploads=2 downloads=1 "
            "payload_analytical=1 elapsed=%lu\n", role, record.principal,
            record.object, record.command, message.checks,
            LOC_APP_ELEMENTS, message.elapsed);
    } else {
        require(false, "unknown_ocall");
    }
    record.phase = message.phase;
    // No global edge-buffer state: B may have made nested ocalls while A was
    // stopped. Bound the reply in the calling enclave's own shared region.
    static_assert(sizeof(edge_call) + sizeof(reply) <= LOC_APP_UTM_BYTES,
        "reply must fit");
    std::memcpy(static_cast<char *>(buffer) + sizeof(edge_call),
        &reply, sizeof(reply));
    edge->return_data.call_ret_offset = sizeof(edge_call);
    edge->return_data.call_ret_size = sizeof(reply);
    edge->return_data.call_status = CALL_STATUS_OK;
    std::fflush(stdout);
}

int
main(int argc, char **argv)
{
    require(argc == 3, "arguments");
    app_path = argv[1];
    runtime_path = argv[2];
    require(outside_checks(1, 1, 1), "initial_linux_authority");
    initialize(0);
    run(0);
    destroy(0);
    initialize(2);
    require(records[2].eid == records[0].eid &&
        records[1].eid != records[0].eid, "eid_reuse");
    run(2);
    destroy(2);
    require(record_count == 3 && busy_checks == 2 &&
        records[0].stream_interrupts > 0 && records[0].resumes > 0 &&
        outside_checks(records[0].object, records[0].stream,
            records[0].command), "final_checks");
    std::printf("KEYSTONE_EYRIE_LIFECYCLE a_eid=%lu b_eid=%lu c_eid=%lu "
        "a_principal=%lu b_principal=%lu c_principal=%lu "
        "stream_interrupts=%u resumes=%u busy_destroy=%u\n",
        records[0].eid, records[1].eid, records[2].eid,
        records[0].principal, records[1].principal, records[2].principal,
        records[0].stream_interrupts, records[0].resumes, busy_checks);
    std::puts("KEYSTONE_EYRIE_HOST_PASS");
    finish(0);
}
