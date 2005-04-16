/* defines for inline arch setup functions */

#include <asm/apic.h>

/**
 * do_timer_interrupt_hook - hook into timer tick
 * @regs:	standard registers from interrupt
 *
 * Description:
 *	This hook is called immediately after the timer interrupt is ack'd.
 *	It's primary purpose is to allow architectures that don't possess
 *	individual per CPU clocks (like the CPU APICs supply) to broadcast the
 *	timer interrupt as a means of triggering reschedules etc.
 **/

static inline void do_timer_interrupt_hook(struct pt_regs *regs)
{
	do_timer(regs);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
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


/* you can safely undefine this if you don't have the Neptune chipset */

#define BUGGY_NEPTUN_TIMER

/**
 * do_timer_overflow - process a detected timer overflow condition
 * @count:	hardware timer interrupt count on overflow
 *
 * Description:
 *	This call is invoked when the jiffies count has not incremented but
 *	the hardware timer interrupt has.  It means that a timer tick interrupt
 *	came along while the previous one was pending, thus a tick was missed
 **/
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
#ifdef BUGGY_NEPTUN_TIMER
		/*
		 * for the Neptun bug we know that the 'latch'
		 * command doesn't latch the high and low value
		 * of the counter atomically. Thus we have to 
		 * substract 256 from the counter 
		 * ... funny, isnt it? :)
		 */
		
		count -= 256;
#else
		printk("do_slow_gettimeoffset(): hardware timer problem?\n");
#endif
	}
	return count;
}
