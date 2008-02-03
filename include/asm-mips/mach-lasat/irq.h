#ifndef _ASM_MACH_LASAT_IRQ_H
#define _ASM_MACH_LASAT_IRQ_H

#define LASAT_CASCADE_IRQ	(MIPS_CPU_IRQ_BASE + 0)

#define LASAT_IRQ_BASE		8
#define LASAT_IRQ_END		23

#define NR_IRQS			24

#include_next <irq.h>

#endif /* _ASM_MACH_LASAT_IRQ_H */
