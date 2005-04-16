/* timex.h: FR-V architecture timex specifications
 */
#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#define CLOCK_TICK_RATE		1193180 /* Underlying HZ */
#define CLOCK_TICK_FACTOR	20	/* Factor of both 1000000 and CLOCK_TICK_RATE */

#define FINETUNE							\
((((((long)LATCH * HZ - CLOCK_TICK_RATE) << SHIFT_HZ) *			\
   (1000000/CLOCK_TICK_FACTOR) / (CLOCK_TICK_RATE/CLOCK_TICK_FACTOR))	\
  << (SHIFT_SCALE-SHIFT_HZ)) / HZ)

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return 0;
}

#define vxtime_lock()		do {} while (0)
#define vxtime_unlock()		do {} while (0)

#endif

