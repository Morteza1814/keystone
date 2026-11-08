/* Normal Keystone SDK launcher; all application arithmetic is in the enclave. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "host/keystone.h"
#include "phases.h"

#if LOC_DMA_APP_BACKEND != 1
#error "The Keystone launcher requires LOC_DMA_APP_BACKEND=1"
#endif

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
        std::fprintf(stderr, "KEYSTONE_DMA_APPLICATION_FAIL phase=%s\n", phase);
        finish(1);
    }
}

int
main(int argc, char **argv)
{
    require(argc == 3, "arguments");
    phase_end(LOC_DA_SETUP);
    phase_begin(LOC_DA_CREATE);
    auto *enclave = new Keystone::Enclave;
    Keystone::Params params;
    params.setFreeMemSize(LOC_DMA_APP_FREEMEM_BYTES);
    params.setUntrustedMem(LOC_DMA_APP_UTM, 4096);
    require(enclave->init(argv[1], argv[2], params) ==
        Keystone::Error::Success, "create");
    phase_end(LOC_DA_CREATE);

    uintptr_t value = UINTPTR_MAX;
    phase_begin(LOC_DA_ENTER);
    require(enclave->run(&value) == Keystone::Error::Success, "run");
    phase_end(LOC_DA_EXIT);
    if (value != LOC_DMA_APP_PASS)
        std::fprintf(stderr, "KEYSTONE_DMA_APPLICATION_APP_ERROR code=%#lx\n",
            value);
    require(value == LOC_DMA_APP_PASS, "numerical_result");

    phase_begin(LOC_DA_DESTROY);
    require(enclave->destroy() == Keystone::Error::Success, "destroy");
    phase_end(LOC_DA_DESTROY);
    std::printf("KEYSTONE_DMA_APPLICATION_HOST_PASS application=%s "
        "elements=%lu rounds=%lu uploads=%lu downloads=%lu commands=%lu "
        "functional_payload=1 numeric_result_checked=1 "
        "compute_timing=pimeval grants_immutable=1 arithmetic=%s\n",
        LOC_DMA_APP_NAME, (unsigned long)LOC_DMA_APP_ELEMENTS,
        (unsigned long)LOC_DMA_APP_ROUNDS,
        (unsigned long)LOC_DMA_APP_UPLOADS,
        (unsigned long)LOC_DMA_APP_DOWNLOADS,
        (unsigned long)LOC_DMA_APP_COMMANDS, LOC_DMA_APP_ARITHMETIC);
    finish(0);
}
