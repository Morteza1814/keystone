/* Measure the ordinary SDK/driver/monitor lifecycle around one workload. */
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
        std::fprintf(stderr, "KEYSTONE_VECTOR_TIMING_FAIL phase=%s\n", phase);
        finish(1);
    }
}

int
main(int argc, char **argv)
{
    require(argc == 3, "arguments");
    phase_begin(LOC_VECTOR_CREATE);
    auto *enclave = new Keystone::Enclave;
    Keystone::Params params;
    params.setFreeMemSize(512 * 1024);
    params.setUntrustedMem(0x10000000UL, 4096);
    require(enclave->init(argv[1], argv[2], params) ==
        Keystone::Error::Success, "create");
    phase_end(LOC_VECTOR_CREATE);

    uintptr_t value = UINTPTR_MAX;
    phase_begin(LOC_VECTOR_ENTER);
    require(enclave->run(&value) == Keystone::Error::Success, "run");
    phase_end(LOC_VECTOR_EXIT);
    if (value != LOC_VECTOR_PASS)
        std::fprintf(stderr, "KEYSTONE_VECTOR_TIMING_APP_ERROR code=%#lx\n",
            value);
    require(value == LOC_VECTOR_PASS, "app_return");

    phase_begin(LOC_VECTOR_DESTROY);
    require(enclave->destroy() == Keystone::Error::Success, "destroy");
    phase_end(LOC_VECTOR_DESTROY);
    std::printf("KEYSTONE_VECTOR_TIMING_HOST_PASS elements=%lu "
        "bytes_per_array=%lu uploads=2 downloads=1 commands=1 "
        "payload_analytical=1 numeric_result_checked=0\n",
        (unsigned long)LOC_VECTOR_ELEMENTS, (unsigned long)LOC_VECTOR_BYTES);
    finish(0);
}
