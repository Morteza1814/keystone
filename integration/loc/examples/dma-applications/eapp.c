/* Keystone wrapper; the algorithms are shared with the Linux benchmarks. */
#include <asm/unistd.h>
#include <sys/mman.h>

#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "loc_pim.h"
#include "phases.h"

#if LOC_DMA_APP_BACKEND != 1
#error "The enclave wrapper requires LOC_DMA_APP_BACKEND=1"
#endif

static uintptr_t allocate_host_arena(void);
static void free_host_arena(uintptr_t address);
static void application_fail(unsigned long error) __attribute__((noreturn));

#include "workload.c"

static void
application_fail(unsigned long error)
{
    EAPP_RETURN(error);
}

static uintptr_t
allocate_host_arena(void)
{
    register uintptr_t a0 __asm__("a0") = 0;
    register uintptr_t a1 __asm__("a1") = LOC_DMA_APP_PRIVATE_BYTES;
    register uintptr_t a2 __asm__("a2") = PROT_READ | PROT_WRITE;
    register uintptr_t a3 __asm__("a3") = MAP_ANONYMOUS | MAP_PRIVATE;
    register uintptr_t a4 __asm__("a4") = (uintptr_t)-1;
    register uintptr_t a5 __asm__("a5") = 0;
    register uintptr_t a7 __asm__("a7") = __NR_mmap;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3),
        "r"(a4), "r"(a5), "r"(a7) : "memory");
    require(a0 && a0 != UINTPTR_MAX && !(a0 & 4095) &&
        LOC_DMA_APP_PRIVATE_BYTES <= UINTPTR_MAX - a0, 0xfa01);
    return a0;
}

static void
free_host_arena(uintptr_t address)
{
    require(!SYSCALL_2(__NR_munmap, address, LOC_DMA_APP_PRIVATE_BYTES), 0xfa0d);
}

void EAPP_ENTRY
eapp_entry(void)
{
    phase_end(LOC_DA_ENTER);
    run_workload();
    phase_begin(LOC_DA_EXIT);
    EAPP_RETURN(LOC_DMA_APP_PASS);
}
