/* Compile independently of backend/size macros, with no LTO or relaxation.
 * Shared entry alignment fixes the RV64 compressed-instruction fetch layout
 * of submission, polling, phase markers and inter-column control. */
#include <limits.h>

#include "command_kernels.h"
#include "phases.h"

__attribute__((noinline, aligned(64))) uint64_t
loc_command_binary(uint64_t operation, loc_pim_object a, loc_pim_object b,
    loc_pim_object destination, unsigned long *commands)
{
    if (*commands == ULONG_MAX)
        return M5_PIM_STATUS_INVALID_REQUEST;
    phase_begin(LOC_DA_COMPUTE);
    const loc_pim_request request = loc_pim_submit(operation, a, b, destination);
    const uint64_t status = request ? loc_pim_complete(request) :
        M5_PIM_STATUS_INVALID_REQUEST;
    if (!status)
        ++*commands;
    phase_end(LOC_DA_COMPUTE);
    return status;
}

__attribute__((noinline, aligned(64))) uint64_t
loc_command_gemv(loc_pim_object weights, loc_pim_object accumulator,
    const int32_t *vector, size_t columns, size_t column_bytes,
    unsigned long *commands)
{
    if (!columns || !column_bytes || columns > SIZE_MAX / column_bytes ||
        columns > ULONG_MAX - *commands)
        return M5_PIM_STATUS_INVALID_REQUEST;
    for (size_t col = 0; col < columns; ++col) {
        phase_begin(LOC_DA_COMPUTE);
        const loc_pim_request request = loc_pim_axpy_range(weights,
            accumulator, vector[col], col * column_bytes, column_bytes);
        const uint64_t status = request ? loc_pim_complete(request) :
            M5_PIM_STATUS_INVALID_REQUEST;
        if (!status)
            ++*commands;
        phase_end(LOC_DA_COMPUTE);
        if (status)
            return status;
    }
    return 0;
}
