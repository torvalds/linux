#ifndef __ASM_I8253_H__
#define __ASM_I8253_H__

/* i8253A PIT registers */
#define PIT_MODE		0x43
#define PIT_CH0			0x40
#define PIT_CH2			0x42

extern spinlock_t i8253_lock;

#ifdef CONFIG_GENERIC_CLOCKEVENTS

extern struct clock_event_device *global_clock_event;

extern void setup_pit_timer(void);

#endif

#endif	/* __ASM_I8253_H__ */
