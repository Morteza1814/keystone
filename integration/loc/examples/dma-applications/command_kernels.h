/* Common guest command loops; protection remains enforced by LOC/adapter. */
#ifndef LOC_APPLICATION_COMMAND_KERNELS_H
#define LOC_APPLICATION_COMMAND_KERNELS_H

#include "loc_pim.h"

/* Execute each resident matrix column sequentially, retaining one measured
 * compute phase per AXPY. Stop on the first failure; count successful commands.
 * The benchmark caller supplies valid vectors and a bounded matrix extent. */
uint64_t loc_command_gemv(loc_pim_object weights, loc_pim_object accumulator,
    const int32_t *vector, size_t columns, size_t column_bytes,
    unsigned long *commands);

/* Backend-independent submission/polling for binary PIM operations. */
uint64_t loc_command_binary(uint64_t operation, loc_pim_object a,
    loc_pim_object b, loc_pim_object destination, unsigned long *commands);

#endif
