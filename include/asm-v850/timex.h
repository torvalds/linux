/*
 * linux/include/asm-v850/timex.h
 *
 * v850 architecture timex specifications
 */
#ifndef __V850_TIMEX_H__
#define __V850_TIMEX_H__

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return 0;
}

#endif /* __V850_TIMEX_H__ */
