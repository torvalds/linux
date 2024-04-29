/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TICK_SCHED_H
#define _TICK_SCHED_H

#include <linux/hrtimer.h>

enum tick_device_mode {
	TICKDEV_MODE_PERIODIC,
	TICKDEV_MODE_ONESHOT,
};

struct tick_device {
	struct clock_event_device *evtdev;
	enum tick_device_mode mode;
};

/* The CPU is in the tick idle mode */
#define TS_FLAG_INIDLE		BIT(0)
/* The idle tick has been stopped */
#define TS_FLAG_STOPPED		BIT(1)
/*
 * Indicator that the CPU is actively in the tick idle mode;
 * it is reset during irq handling phases.
 */
#define TS_FLAG_IDLE_ACTIVE	BIT(2)
/* CPU was the last one doing do_timer before going idle */
#define TS_FLAG_DO_TIMER_LAST	BIT(3)
/* NO_HZ is enabled */
#define TS_FLAG_NOHZ		BIT(4)
/* High resolution tick mode */
#define TS_FLAG_HIGHRES		BIT(5)

/**
 * struct tick_sched - sched tick emulation and no idle tick control/stats
 *
 * @flags:		State flags gathering the TS_FLAG_* features
 * @got_idle_tick:	Tick timer function has run with @inidle set
 * @stalled_jiffies:	Number of stalled jiffies detected across ticks
 * @last_tick_jiffies:	Value of jiffies seen on last tick
 * @sched_timer:	hrtimer to schedule the periodic tick in high
 *			resolution mode
 * @last_tick:		Store the last tick expiry time when the tick
 *			timer is modified for nohz sleeps. This is necessary
 *			to resume the tick timer operation in the timeline
 *			when the CPU returns from nohz sleep.
 * @next_tick:		Next tick to be fired when in dynticks mode.
 * @idle_jiffies:	jiffies at the entry to idle for idle time accounting
 * @idle_waketime:	Time when the idle was interrupted
 * @idle_sleeptime_seq:	sequence counter for data consistency
 * @idle_entrytime:	Time when the idle call was entered
 * @last_jiffies:	Base jiffies snapshot when next event was last computed
 * @timer_expires_base:	Base time clock monotonic for @timer_expires
 * @timer_expires:	Anticipated timer expiration time (in case sched tick is stopped)
 * @next_timer:		Expiry time of next expiring timer for debugging purpose only
 * @idle_expires:	Next tick in idle, for debugging purpose only
 * @idle_calls:		Total number of idle calls
 * @idle_sleeps:	Number of idle calls, where the sched tick was stopped
 * @idle_exittime:	Time when the idle state was left
 * @idle_sleeptime:	Sum of the time slept in idle with sched tick stopped
 * @iowait_sleeptime:	Sum of the time slept in idle with sched tick stopped, with IO outstanding
 * @tick_dep_mask:	Tick dependency mask - is set, if someone needs the tick
 * @check_clocks:	Notification mechanism about clocksource changes
 */
struct tick_sched {
	/* Common flags */
	unsigned long			flags;

	/* Tick handling: jiffies stall check */
	unsigned int			stalled_jiffies;
	unsigned long			last_tick_jiffies;

	/* Tick handling */
	struct hrtimer			sched_timer;
	ktime_t				last_tick;
	ktime_t				next_tick;
	unsigned long			idle_jiffies;
	ktime_t				idle_waketime;
	unsigned int			got_idle_tick;

	/* Idle entry */
	seqcount_t			idle_sleeptime_seq;
	ktime_t				idle_entrytime;

	/* Tick stop */
	unsigned long			last_jiffies;
	u64				timer_expires_base;
	u64				timer_expires;
	u64				next_timer;
	ktime_t				idle_expires;
	unsigned long			idle_calls;
	unsigned long			idle_sleeps;

	/* Idle exit */
	ktime_t				idle_exittime;
	ktime_t				idle_sleeptime;
	ktime_t				iowait_sleeptime;

	/* Full dynticks handling */
	atomic_t			tick_dep_mask;

	/* Clocksource changes */
	unsigned long			check_clocks;
};

extern struct tick_sched *tick_get_tick_sched(int cpu);

extern void tick_setup_sched_timer(bool hrtimer);
#if defined CONFIG_TICK_ONESHOT
extern void tick_sched_timer_dying(int cpu);
#else
static inline void tick_sched_timer_dying(int cpu) { }
#endif

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern int __tick_broadcast_oneshot_control(enum tick_broadcast_state state);
#else
static inline int
__tick_broadcast_oneshot_control(enum tick_broadcast_state state)
{
	return -EBUSY;
}
#endif

#endif
