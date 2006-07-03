#ifndef _ASMi386_TIMER_H
#define _ASMi386_TIMER_H
#include <linux/init.h>
#include <linux/pm.h>

#define TICK_SIZE (tick_nsec / 1000)
void setup_pit_timer(void);
/* Modifiers for buggy PIT handling */
extern int pit_latch_buggy;
extern int timer_ack;
extern int recalibrate_cpu_khz(void);

#endif
