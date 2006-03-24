/* defines for inline arch setup functions */

#include <asm/fixmap.h>
#include <asm/i8259.h>
#include "cobalt.h"

static inline void do_timer_interrupt_hook(struct pt_regs *regs)
{
	/* Clear the interrupt */
	co_cpu_write(CO_CPU_STAT,co_cpu_read(CO_CPU_STAT) & ~CO_STAT_TIMEINTR);

	do_timer(regs);
#ifndef CONFIG_SMP
	update_process_times(user_mode_vm(regs));
#endif
/*
 * In the SMP case we use the local APIC timer interrupt to do the
 * profiling, except when we simulate SMP mode on a uniprocessor
 * system, in that case we have to call the local interrupt handler.
 */
#ifndef CONFIG_X86_LOCAL_APIC
	profile_tick(CPU_PROFILING, regs);
#else
	if (!using_apic_timer)
		smp_local_timer_interrupt(regs);
#endif
}

static inline int do_timer_overflow(int count)
{
	int i;

	spin_lock(&i8259A_lock);
	/*
	 * This is tricky when I/O APICs are used;
	 * see do_timer_interrupt().
	 */
	i = inb(0x20);
	spin_unlock(&i8259A_lock);
	
	/* assumption about timer being IRQ0 */
	if (i & 0x01) {
		/*
		 * We cannot detect lost timer interrupts ... 
		 * well, that's why we call them lost, don't we? :)
		 * [hmm, on the Pentium and Alpha we can ... sort of]
		 */
		count -= LATCH;
	} else {
		printk("do_slow_gettimeoffset(): hardware timer problem?\n");
	}
	return count;
}
