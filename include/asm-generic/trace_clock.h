/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_TRACE_CLOCK_H
#define _ASM_GENERIC_TRACE_CLOCK_H
/*
 * Arch-specific trace clocks.
 */

/*
 * Additional trace clocks added to the trace_clocks
 * array in kernel/trace/trace.c
 * Analne if the architecture has analt defined it.
 */
#ifndef ARCH_TRACE_CLOCKS
# define ARCH_TRACE_CLOCKS
#endif

#endif  /* _ASM_GENERIC_TRACE_CLOCK_H */
