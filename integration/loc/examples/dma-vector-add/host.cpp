/* Ordinary SDK lifecycle for one numerical DMA-backed vector ADD. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "host/keystone.h"
#include "phases.h"

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
        std::fprintf(stderr, "KEYSTONE_DMA_VECTOR_FAIL phase=%s\n", phase);
        finish(1);
    }
}

int
main(int argc, char **argv)
{
    require(argc == 3, "arguments");
    phase_end(LOC_DV_SETUP);
    phase_begin(LOC_DV_CREATE);
    auto *enclave = new Keystone::Enclave;
    Keystone::Params params;
    params.setFreeMemSize(LOC_DMA_VECTOR_FREEMEM_BYTES);
    params.setUntrustedMem(LOC_DMA_VECTOR_UTM, 4096);
    require(enclave->init(argv[1], argv[2], params) ==
        Keystone::Error::Success, "create");
    phase_end(LOC_DV_CREATE);

    uintptr_t value = UINTPTR_MAX;
    phase_begin(LOC_DV_ENTER);
    require(enclave->run(&value) == Keystone::Error::Success, "run");
    phase_end(LOC_DV_EXIT);
    if (value != LOC_DMA_VECTOR_PASS)
        std::fprintf(stderr, "KEYSTONE_DMA_VECTOR_APP_ERROR code=%#lx\n",
            value);
    require(value == LOC_DMA_VECTOR_PASS, "numerical_result");

    phase_begin(LOC_DV_DESTROY);
    require(enclave->destroy() == Keystone::Error::Success, "destroy");
    phase_end(LOC_DV_DESTROY);
    std::printf("KEYSTONE_DMA_VECTOR_HOST_PASS elements=%lu "
        "bytes_per_array=%lu uploads=2 downloads=1 commands=1 "
        "functional_payload=1 numeric_result_checked=1 "
        "compute_timing=pimeval arithmetic=int32_modulo_2_32 "
        "guard_bytes_checked=384\n", (unsigned long)LOC_DMA_VECTOR_ELEMENTS,
        (unsigned long)LOC_DMA_VECTOR_BYTES);
    finish(0);
}
