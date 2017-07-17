#ifndef _LINUX_SCHED_CLOCK_H
#define _LINUX_SCHED_CLOCK_H

#include <linux/smp.h>

/*
 * Do not use outside of architecture code which knows its limitations.
 *
 * sched_clock() has no promise of monotonicity or bounded drift between
 * CPUs, use (which you should not) requires disabling IRQs.
 *
 * Please use one of the three interfaces below.
 */
extern unsigned long long notrace sched_clock(void);

/*
 * See the comment in kernel/sched/clock.c
 */
extern u64 running_clock(void);
extern u64 sched_clock_cpu(int cpu);


extern void sched_clock_init(void);

#ifndef CONFIG_HAVE_UNSTABLE_SCHED_CLOCK
static inline void sched_clock_tick(void)
{
}

static inline void clear_sched_clock_stable(void)
{
}

static inline void sched_clock_idle_sleep_event(void)
{
}

static inline void sched_clock_idle_wakeup_event(void)
{
}

static inline u64 cpu_clock(int cpu)
{
	return sched_clock();
}

static inline u64 local_clock(void)
{
	return sched_clock();
}
#else
extern int sched_clock_stable(void);
extern void clear_sched_clock_stable(void);

/*
 * When sched_clock_stable(), __sched_clock_offset provides the offset
 * between local_clock() and sched_clock().
 */
extern u64 __sched_clock_offset;

extern void sched_clock_tick(void);
extern void sched_clock_tick_stable(void);
extern void sched_clock_idle_sleep_event(void);
extern void sched_clock_idle_wakeup_event(void);

/*
 * As outlined in clock.c, provides a fast, high resolution, nanosecond
 * time source that is monotonic per cpu argument and has bounded drift
 * between cpus.
 *
 * ######################### BIG FAT WARNING ##########################
 * # when comparing cpu_clock(i) to cpu_clock(j) for i != j, time can #
 * # go backwards !!                                                  #
 * ####################################################################
 */
static inline u64 cpu_clock(int cpu)
{
	return sched_clock_cpu(cpu);
}

static inline u64 local_clock(void)
{
	return sched_clock_cpu(raw_smp_processor_id());
}
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
/*
 * An i/f to runtime opt-in for irq time accounting based off of sched_clock.
 * The reason for this explicit opt-in is not to have perf penalty with
 * slow sched_clocks.
 */
extern void enable_sched_clock_irqtime(void);
extern void disable_sched_clock_irqtime(void);
#else
static inline void enable_sched_clock_irqtime(void) {}
static inline void disable_sched_clock_irqtime(void) {}
#endif

#endif /* _LINUX_SCHED_CLOCK_H */
