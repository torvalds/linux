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

extern void enable_IO_APIC(void);
extern void native_init_IRQ(void);

#include <asm/ptrace.h>

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)

/*
 *	SMP has a few special interrupts for IPI messages
 */

#define BUILD_IRQ(nr)				\
	asmlinkage void IRQ_NAME(nr);		\
	asm("\n.p2align\n"			\
	    "IRQ" #nr "_interrupt:\n\t"		\
	    "push $~(" #nr ") ; "		\
	    "jmp common_interrupt");

#endif
