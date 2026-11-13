/* Ordinary Linux U-mode application. No Keystone SDK/runtime is linked. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "loc_pim.h"
#include "phases.h"

#if LOC_DMA_APP_BACKEND == 1
#error "The Linux wrapper requires LOC_DMA_APP_BACKEND=2 or 3"
#endif
#if LOC_DMA_APP_BACKEND == 2
#include "pim_pin_uapi.h"
static int pin_fd = -1;
#endif

static uintptr_t allocate_host_arena(void);
static void free_host_arena(uintptr_t address);
static void application_fail(unsigned long error) __attribute__((noreturn));

#include "workload.c"

static void
application_fail(unsigned long error)
{
    fprintf(stderr, "LINUX_DMA_APPLICATION_FAIL backend=%s code=%#lx\n",
        LOC_DMA_APP_BACKEND_NAME, error);
    exit(1);
}

static uintptr_t
allocate_host_arena(void)
{
    void *memory = mmap(NULL, LOC_DMA_APP_PRIVATE_BYTES,
        PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    require(memory != MAP_FAILED && memory && !((uintptr_t)memory & 4095),
        0xfa01);
#if LOC_DMA_APP_BACKEND == 2
    pin_fd = open("/dev/pim_pin", O_RDWR | O_CLOEXEC);
    require(pin_fd >= 0, 0xfa50);
    const struct pim_pin_buffer buffer = {
        .address = (uintptr_t)memory, .length = LOC_DMA_APP_PRIVATE_BYTES
    };
    require(!ioctl(pin_fd, PIM_PIN_IOCTL_PIN, &buffer), 0xfa51);
#endif
    return (uintptr_t)memory;
}

static void
free_host_arena(uintptr_t address)
{
#if LOC_DMA_APP_BACKEND == 2
    require(!ioctl(pin_fd, PIM_PIN_IOCTL_UNPIN), 0xfa52);
    require(!close(pin_fd), 0xfa53);
    pin_fd = -1;
#endif
    require(!munmap((void *)address, LOC_DMA_APP_PRIVATE_BYTES), 0xfa0d);
}

int
main(void)
{
    phase_end(LOC_DA_SETUP);
    run_workload();
    printf("LINUX_DMA_APPLICATION_HOST_PASS backend=%s application=%s "
        "elements=%lu rounds=%lu uploads=%lu downloads=%lu commands=%lu "
        "functional_payload=1 numeric_result_checked=%d "
        "compute_timing=%s grants=%lu arithmetic=%s\n",
        LOC_DMA_APP_BACKEND_NAME, LOC_DMA_APP_NAME,
        (unsigned long)LOC_DMA_APP_ELEMENTS,
        (unsigned long)LOC_DMA_APP_ROUNDS,
        (unsigned long)LOC_DMA_APP_UPLOADS,
        (unsigned long)LOC_DMA_APP_DOWNLOADS,
        (unsigned long)LOC_DMA_APP_COMMANDS, LOC_DMA_APP_VERIFY_RESULTS,
        LOC_DMA_APP_COMPUTE_TIMING, (unsigned long)LOC_DMA_APP_GRANTS,
        LOC_DMA_APP_ARITHMETIC);
    return 0;
}
