/*
 * This file setups defines to compile arch specific binary from the
 * generic one.
 *
 * The function 'LIBUNWIND__ARCH_REG_ID' name is set according to arch
 * name and the defination of this function is included directly from
 * 'arch/x86/util/unwind-libunwind.c', to make sure that this function
 * is defined no matter what arch the host is.
 *
 * Finally, the arch specific unwind methods are exported which will
 * be assigned to each x86 thread.
 */

#define REMOTE_UNWIND_LIBUNWIND

/* Define arch specific functions & regs for libunwind, should be
 * defined before including "unwind.h"
 */
#define LIBUNWIND__ARCH_REG_ID(regnum) libunwind__x86_reg_id(regnum)
#define LIBUNWIND__ARCH_REG_IP PERF_REG_X86_IP
#define LIBUNWIND__ARCH_REG_SP PERF_REG_X86_SP

#include "unwind.h"
#include "debug.h"
#include "libunwind-x86.h"
#include <../../../../arch/x86/include/uapi/asm/perf_regs.h>

/* HAVE_ARCH_X86_64_SUPPORT is used in'arch/x86/util/unwind-libunwind.c'
 * for x86_32, we undef it to compile code for x86_32 only.
 */
#undef HAVE_ARCH_X86_64_SUPPORT
#include "../../arch/x86/util/unwind-libunwind.c"

/* Explicitly define NO_LIBUNWIND_DEBUG_FRAME, because non-ARM has no
 * dwarf_find_debug_frame() function.
 */
#ifndef NO_LIBUNWIND_DEBUG_FRAME
#define NO_LIBUNWIND_DEBUG_FRAME
#endif
#include "util/unwind-libunwind-local.c"

struct unwind_libunwind_ops *
x86_32_unwind_libunwind_ops = &_unwind_libunwind_ops;
