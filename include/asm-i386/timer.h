#ifndef _ASMi386_TIMER_H
#define _ASMi386_TIMER_H
#include <linux/init.h>
#include <linux/pm.h>

#define TICK_SIZE (tick_nsec / 1000)

void setup_pit_timer(void);
unsigned long long native_sched_clock(void);

/* Modifiers for buggy PIT handling */
extern int pit_latch_buggy;
extern int timer_ack;
extern int no_timer_check;
extern int no_sync_cmos_clock;
extern int recalibrate_cpu_khz(void);

#ifndef CONFIG_PARAVIRT
#define get_scheduled_cycles(val) rdtscll(val)
#endif

#endif
