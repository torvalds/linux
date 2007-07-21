#ifndef _ASM_X8664_HPET_H
#define _ASM_X8664_HPET_H 1

#include <asm-i386/hpet.h>

#define HPET_TICK_RATE (HZ * 100000UL)

extern int hpet_rtc_timer_init(void);
extern int hpet_arch_init(void);
extern int hpet_timer_stop_set_go(unsigned long tick);
extern int hpet_reenable(void);
extern unsigned int hpet_calibrate_tsc(void);

extern int hpet_use_timer;
extern unsigned long hpet_period;
extern unsigned long hpet_tick;

#endif
