#ifndef __ASSEMBLY__

typedef int vector_irq_t[NR_VECTORS];
DECLARE_PER_CPU(vector_irq_t, vector_irq);
extern void __setup_vector_irq(int cpu);
extern spinlock_t vector_lock;

/*
 * Various low-level irq details needed by irq.c, process.c,
 * time.c, io_apic.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

#include <asm/ptrace.h>

#endif
