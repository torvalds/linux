#ifndef __ASM_SH_TIMER_H
#define __ASM_SH_TIMER_H

#include <linux/sysdev.h>
#include <asm/cpu/timer.h>

struct sys_timer_ops {
	int (*init)(void);
	unsigned long (*get_offset)(void);
	unsigned long (*get_frequency)(void);
};

struct sys_timer {
	const char		*name;

	struct sys_device	dev;
	struct sys_timer_ops	*ops;
};

#define TICK_SIZE (tick_nsec / 1000)

extern struct sys_timer tmu_timer;
extern struct sys_timer *sys_timer;

static inline unsigned long get_timer_offset(void)
{
	return sys_timer->ops->get_offset();
}

static inline unsigned long get_timer_frequency(void)
{
	return sys_timer->ops->get_frequency();
}

/* arch/sh/kernel/timers/timer.c */
struct sys_timer *get_sys_timer(void);

/* arch/sh/kernel/time.c */
void handle_timer_tick(struct pt_regs *);

#endif /* __ASM_SH_TIMER_H */

