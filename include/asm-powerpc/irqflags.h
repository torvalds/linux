/*
 * include/asm-powerpc/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() macros from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

/*
 * Get definitions for raw_local_save_flags(x), etc.
 */
#include <asm-powerpc/hw_irq.h>

/*
 * Do the CPU's IRQ-state tracing from assembly code. We call a
 * C function, so save all the C-clobbered registers:
 */
#ifdef CONFIG_TRACE_IRQFLAGS

#error No support on PowerPC yet for CONFIG_TRACE_IRQFLAGS

#else
# define TRACE_IRQS_ON
# define TRACE_IRQS_OFF
#endif

#endif
