#ifndef __ASM_I8253_H__
#define __ASM_I8253_H__

#include <linux/clockchips.h>

extern spinlock_t i8253_lock;

extern struct clock_event_device *global_clock_event;

/**
 * pit_interrupt_hook - hook into timer tick
 * @regs:	standard registers from interrupt
 *
 * Call the global clock event handler.
 **/
static inline void pit_interrupt_hook(void)
{
	global_clock_event->event_handler(global_clock_event);
}

#endif	/* __ASM_I8253_H__ */
