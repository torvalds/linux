#ifndef __ASM_I8253_H__
#define __ASM_I8253_H__

#include <linux/clockchips.h>

extern spinlock_t i8253_lock;

extern struct clock_event_device *global_clock_event;

#endif	/* __ASM_I8253_H__ */
