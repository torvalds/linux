/*
 * Nothing to see here yet
 */
#ifndef _ARCH_ARM_HW_IRQ_H
#define _ARCH_ARM_HW_IRQ_H

#include <asm/mach/irq.h>

#if defined(CONFIG_NO_IDLE_HZ)
# include <asm/dyntick.h>
# define handle_dynamic_tick(action)					\
	if (!(action->flags & IRQF_TIMER) && system_timer->dyn_tick) {	\
		write_seqlock(&xtime_lock);				\
		if (system_timer->dyn_tick->state & DYN_TICK_ENABLED)	\
			system_timer->dyn_tick->handler(irq, NULL);	\
		write_sequnlock(&xtime_lock);				\
	}
#endif

#endif
