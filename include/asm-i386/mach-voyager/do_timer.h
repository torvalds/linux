/* defines for inline arch setup functions */
#include <asm/voyager.h>

static inline void do_timer_interrupt_hook(void)
{
	do_timer(1);
#ifndef CONFIG_SMP
	update_process_times(user_mode_vm(irq_regs));
#endif

	voyager_timer_interrupt();
}

static inline int do_timer_overflow(int count)
{
	/* can't read the ISR, just assume 1 tick
	   overflow */
	if(count > LATCH || count < 0) {
		printk(KERN_ERR "VOYAGER PROBLEM: count is %d, latch is %d\n", count, LATCH);
		count = LATCH;
	}
	count -= LATCH;

	return count;
}
