#ifndef _ASM_CRIS_ARCH_TIMEX_H
#define _ASM_CRIS_ARCH_TIMEX_H

#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/timer_defs.h>

/*
 * The clock runs at 100MHz, we divide it by 1000000. If you change anything
 * here you must check time.c as well.
 */

#define CLOCK_TICK_RATE 100000000	/* Underlying frequency of the HZ timer */

/* The timer0 values gives 10 ns resolution but interrupts at HZ. */
#define TIMER0_FREQ (CLOCK_TICK_RATE)
#define TIMER0_DIV (TIMER0_FREQ/(HZ))

/* Convert the value in step of 10 ns to 1us without overflow: */
#define GET_JIFFIES_USEC() \
  ( (TIMER0_DIV - REG_RD(timer, regi_timer, r_tmr0_data)) /100 )

extern unsigned long get_ns_in_jiffie(void);

static inline unsigned long get_us_in_jiffie_highres(void)
{
	return get_ns_in_jiffie() / 1000;
}

#endif

