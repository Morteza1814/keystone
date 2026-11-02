/* Ordinary SDK lifecycle for the private-buffer functional DMA fixture. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "edge/edge_common.h"
#include "host/keystone.h"
#include "loc_pim.h"
#include "protocol.h"

static Keystone::Enclave *enclave;
static uint64_t sequence, principal;
static bool done;

[[noreturn]] static void
finish(int status)
{
    // The pinned SDK does not invalidate an explicitly destroyed EID.
    std::fflush(nullptr);
    std::_Exit(status);
}

static void
require(bool condition, const char *phase)
{
    if (!condition) {
        std::fprintf(stderr, "KEYSTONE_DMA_TRANSFER_FAIL phase=%s\n", phase);
        finish(1);
    }
}

static void
dispatch(void *buffer)
{
    require(buffer == enclave->getSharedBuffer(), "ocall_owner");
    auto *edge = static_cast<edge_call *>(buffer);
    const size_t capacity = enclave->getSharedBufferSize();
    require(capacity >= sizeof(loc_dma_message) &&
        edge->call_id == LOC_DMA_OCALL &&
        edge->call_arg_size == sizeof(loc_dma_message) &&
        edge->call_arg_offset >= sizeof(edge_call) &&
        edge->call_arg_offset <= capacity - sizeof(loc_dma_message),
        "ocall_arguments");
    loc_dma_message message;
    std::memcpy(&message, static_cast<char *>(buffer) + edge->call_arg_offset,
        sizeof(message));
    require(!done && message.sequence == sequence++ && message.principal &&
        (!principal || principal == message.principal), "ocall_order");
    principal = message.principal;
    require(!loc_pim_principal() && !loc_pim_alloc(16) &&
        !loc_pim_stream_begin(message.object, buffer, 1, LOC_PIM_TO_DEVICE),
        "outside_authority");
    if (message.kind == LOC_DMA_DONE) {
        require(message.case_id == LOC_DMA_CASES &&
            message.bytes == LOC_DMA_BYTES &&
            sequence == LOC_DMA_CASES * LOC_DMA_TRANSFERS_PER_CASE + 1 &&
            message.checks > LOC_DMA_BYTES, "done_checks");
        done = true;
        std::printf("KEYSTONE_DMA_CHECKED bytes=%lu cases=%lu checks=%lu "
            "principal=%lu\n", message.bytes, message.case_id,
            message.checks, message.principal);
    } else {
        require((message.kind == LOC_DMA_UPLOAD ||
            message.kind == LOC_DMA_DOWNLOAD) && message.cycles &&
            message.bytes && message.object && message.case_id < LOC_DMA_CASES,
            "sample_fields");
        std::printf("KEYSTONE_DMA_SAMPLE case=%lu sequence=%lu direction=%s "
            "bytes=%lu cycles=%lu object=%lu\n", message.case_id,
            message.sequence, message.kind == LOC_DMA_UPLOAD ?
                "upload" : "download", message.bytes, message.cycles,
            message.object);
    }
    const unsigned long reply = LOC_DMA_MAGIC;
    static_assert(sizeof(edge_call) + sizeof(reply) <= LOC_DMA_UTM_BYTES,
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
    enclave = new Keystone::Enclave;
    Keystone::Params params;
    params.setFreeMemSize(LOC_DMA_FREEMEM_BYTES);
    params.setUntrustedMem(LOC_DMA_UTM, LOC_DMA_UTM_BYTES);
    require(enclave->init(argv[1], argv[2], params) ==
        Keystone::Error::Success, "create");
    enclave->registerOcallDispatch(dispatch);
    uintptr_t value = UINTPTR_MAX;
    require(enclave->run(&value) == Keystone::Error::Success, "run");
    if (value != LOC_DMA_MAGIC)
        std::fprintf(stderr, "KEYSTONE_DMA_APP_ERROR code=%#lx\n", value);
    require(value == LOC_DMA_MAGIC && done, "app_return");
    require(enclave->destroy() == Keystone::Error::Success, "destroy");
    std::printf("KEYSTONE_DMA_HOST_PASS bytes=%lu cases=%lu transfers=%lu "
        "functional_payload=1 pim_commands=0\n", (unsigned long)LOC_DMA_BYTES,
        LOC_DMA_CASES, LOC_DMA_CASES * LOC_DMA_TRANSFERS_PER_CASE);
    finish(0);
}
