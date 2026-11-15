/* Integer-register IEEE-754 FP64 for the regression fit. The current
 * Keystone monitor does not provide an audited floating-register context
 * contract; use the same Berkeley SoftFloat path in both guest backends.
 * Sources retain their upstream licenses in ext/softfloat. */
#ifndef LOC_REGRESSION_SOFTFLOAT_H
#define LOC_REGRESSION_SOFTFLOAT_H
#define SOFTFLOAT_FAST_INT64
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../../../../../softfloat/softfloat_state.c"
#include "../../../../../softfloat/i64_to_f64.c"
#include "../../../../../softfloat/f64_mul.c"
#include "../../../../../softfloat/f64_sub.c"
#include "../../../../../softfloat/f64_div.c"
#include "../../../../../softfloat/s_addMagsF64.c"
#include "../../../../../softfloat/s_mul64To128.c"
#include "../../../../../softfloat/s_normRoundPackToF64.c"
#include "../../../../../softfloat/s_normSubnormalF64Sig.c"
#include "../../../../../softfloat/s_propagateNaNF64UI.c"
#include "../../../../../softfloat/softfloat_raiseFlags.c"
#include "../../../../../softfloat/s_roundPackToF64.c"
#include "../../../../../softfloat/s_subMagsF64.c"
#include "../../../../../softfloat/s_countLeadingZeros64.c"
#include "../../../../../softfloat/s_countLeadingZeros8.c"

#pragma GCC diagnostic pop
#endif
