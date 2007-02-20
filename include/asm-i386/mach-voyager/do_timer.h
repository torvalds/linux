/* defines for inline arch setup functions */
#include <linux/clockchips.h>

#include <asm/voyager.h>
#include <asm/i8253.h>

/**
 * do_timer_interrupt_hook - hook into timer tick
 * @regs:     standard registers from interrupt
 *
 * Call the pit clock event handler. see asm/i8253.h
 **/
static inline void do_timer_interrupt_hook(void)
{
	pit_interrupt_hook();
	voyager_timer_interrupt();
}

