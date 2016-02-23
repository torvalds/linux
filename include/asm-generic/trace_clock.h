#ifndef _ASM_GENERIC_TRACE_CLOCK_H
#define _ASM_GENERIC_TRACE_CLOCK_H
/*
 * Arch-specific trace clocks.
 */

/*
 * Additional trace clocks added to the trace_clocks
 * array in kernel/trace/trace.c
 * None if the architecture has not defined it.
 */
#ifndef ARCH_TRACE_CLOCKS
# define ARCH_TRACE_CLOCKS
#endif

#endif  /* _ASM_GENERIC_TRACE_CLOCK_H */
