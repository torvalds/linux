// SPDX-License-Identifier: GPL-2.0
/*
 * This file setups defines to compile arch specific binary from the
 * generic one.
 *
 * The function 'LIBUNWIND__ARCH_REG_ID' name is set according to arch
 * name and the definition of this function is included directly from
 * 'arch/arm64/util/unwind-libunwind.c', to make sure that this function
 * is defined no matter what arch the host is.
 *
 * Finally, the arch specific unwind methods are exported which will
 * be assigned to each arm64 thread.
 */

#define REMOTE_UNWIND_LIBUNWIND

/* Define arch specific functions & regs for libunwind, should be
 * defined before including "unwind.h"
 */
#define LIBUNWIND__ARCH_REG_ID(regnum) libunwind__arm64_reg_id(regnum)

#include "unwind.h"
#include "libunwind-aarch64.h"
#define perf_event_arm_regs perf_event_arm64_regs
#include <../../../arch/arm64/include/uapi/asm/perf_regs.h>
#undef perf_event_arm_regs
#include "../../arch/arm64/util/unwind-libunwind.c"

/* NO_LIBUNWIND_DEBUG_FRAME is a feature flag for local libunwind,
 * assign NO_LIBUNWIND_DEBUG_FRAME_AARCH64 to it for compiling arm64
 * unwind methods.
 */
#undef NO_LIBUNWIND_DEBUG_FRAME
#ifdef NO_LIBUNWIND_DEBUG_FRAME_AARCH64
#define NO_LIBUNWIND_DEBUG_FRAME
#endif
#include "util/unwind-libunwind-local.c"

struct unwind_libunwind_ops *
arm64_unwind_libunwind_ops = &_unwind_libunwind_ops;
