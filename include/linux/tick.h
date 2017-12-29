/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tick related global functions
 */
#ifndef _LINUX_TICK_H
#define _LINUX_TICK_H

#include <linux/clockchips.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/context_tracking_state.h>
#include <linux/cpumask.h>
#include <linux/sched.h>

#ifdef CONFIG_GENERIC_CLOCKEVENTS
extern void __init tick_init(void);
/* Should be core only, but ARM BL switcher requires it */
extern void tick_suspend_local(void);
/* Should be core only, but XEN resume magic and ARM BL switcher require it */
extern void tick_resume_local(void);
extern void tick_handover_do_timer(void);
extern void tick_cleanup_dead_cpu(int cpu);
#else /* CONFIG_GENERIC_CLOCKEVENTS */
static inline void tick_init(void) { }
static inline void tick_suspend_local(void) { }
static inline void tick_resume_local(void) { }
static inline void tick_handover_do_timer(void) { }
static inline void tick_cleanup_dead_cpu(int cpu) { }
#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

#if defined(CONFIG_GENERIC_CLOCKEVENTS) && defined(CONFIG_SUSPEND)
extern void tick_freeze(void);
extern void tick_unfreeze(void);
#else
static inline void tick_freeze(void) { }
static inline void tick_unfreeze(void) { }
#endif

#ifdef CONFIG_TICK_ONESHOT
extern void tick_irq_enter(void);
#  ifndef arch_needs_cpu
#   define arch_needs_cpu() (0)
#  endif
# else
static inline void tick_irq_enter(void) { }
#endif

#if defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST) && defined(CONFIG_TICK_ONESHOT)
extern void hotplug_cpu__broadcast_tick_pull(int dead_cpu);
#else
static inline void hotplug_cpu__broadcast_tick_pull(int dead_cpu) { }
#endif

enum tick_broadcast_mode {
	TICK_BROADCAST_OFF,
	TICK_BROADCAST_ON,
	TICK_BROADCAST_FORCE,
};

enum tick_broadcast_state {
	TICK_BROADCAST_EXIT,
	TICK_BROADCAST_ENTER,
};

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern void tick_broadcast_control(enum tick_broadcast_mode mode);
#else
static inline void tick_broadcast_control(enum tick_broadcast_mode mode) { }
#endif /* BROADCAST */

#ifdef CONFIG_GENERIC_CLOCKEVENTS
extern int tick_broadcast_oneshot_control(enum tick_broadcast_state state);
#else
static inline int tick_broadcast_oneshot_control(enum tick_broadcast_state state)
{
	return 0;
}
#endif

static inline void tick_broadcast_enable(void)
{
	tick_broadcast_control(TICK_BROADCAST_ON);
}
static inline void tick_broadcast_disable(void)
{
	tick_broadcast_control(TICK_BROADCAST_OFF);
}
static inline void tick_broadcast_force(void)
{
	tick_broadcast_control(TICK_BROADCAST_FORCE);
}
static inline int tick_broadcast_enter(void)
{
	return tick_broadcast_oneshot_control(TICK_BROADCAST_ENTER);
}
static inline void tick_broadcast_exit(void)
{
	tick_broadcast_oneshot_control(TICK_BROADCAST_EXIT);
}

enum tick_dep_bits {
	TICK_DEP_BIT_POSIX_TIMER	= 0,
	TICK_DEP_BIT_PERF_EVENTS	= 1,
	TICK_DEP_BIT_SCHED		= 2,
	TICK_DEP_BIT_CLOCK_UNSTABLE	= 3
};

#define TICK_DEP_MASK_NONE		0
#define TICK_DEP_MASK_POSIX_TIMER	(1 << TICK_DEP_BIT_POSIX_TIMER)
#define TICK_DEP_MASK_PERF_EVENTS	(1 << TICK_DEP_BIT_PERF_EVENTS)
#define TICK_DEP_MASK_SCHED		(1 << TICK_DEP_BIT_SCHED)
#define TICK_DEP_MASK_CLOCK_UNSTABLE	(1 << TICK_DEP_BIT_CLOCK_UNSTABLE)

#ifdef CONFIG_NO_HZ_COMMON
extern bool tick_nohz_enabled;
extern int tick_nohz_tick_stopped(void);
extern void tick_nohz_idle_enter(void);
extern void tick_nohz_idle_exit(void);
extern void tick_nohz_irq_exit(void);
extern ktime_t tick_nohz_get_sleep_length(void);
extern unsigned long tick_nohz_get_idle_calls(void);
extern u64 get_cpu_idle_time_us(int cpu, u64 *last_update_time);
extern u64 get_cpu_iowait_time_us(int cpu, u64 *last_update_time);
#else /* !CONFIG_NO_HZ_COMMON */
#define tick_nohz_enabled (0)
static inline int tick_nohz_tick_stopped(void) { return 0; }
static inline void tick_nohz_idle_enter(void) { }
static inline void tick_nohz_idle_exit(void) { }

static inline ktime_t tick_nohz_get_sleep_length(void)
{
	return NSEC_PER_SEC / HZ;
}
static inline u64 get_cpu_idle_time_us(int cpu, u64 *unused) { return -1; }
static inline u64 get_cpu_iowait_time_us(int cpu, u64 *unused) { return -1; }
#endif /* !CONFIG_NO_HZ_COMMON */

#ifdef CONFIG_NO_HZ_FULL
extern bool tick_nohz_full_running;
extern cpumask_var_t tick_nohz_full_mask;

static inline bool tick_nohz_full_enabled(void)
{
	if (!context_tracking_is_enabled())
		return false;

	return tick_nohz_full_running;
}

static inline bool tick_nohz_full_cpu(int cpu)
{
	if (!tick_nohz_full_enabled())
		return false;

	return cpumask_test_cpu(cpu, tick_nohz_full_mask);
}

static inline void tick_nohz_full_add_cpus_to(struct cpumask *mask)
{
	if (tick_nohz_full_enabled())
		cpumask_or(mask, mask, tick_nohz_full_mask);
}

extern void tick_nohz_dep_set(enum tick_dep_bits bit);
extern void tick_nohz_dep_clear(enum tick_dep_bits bit);
extern void tick_nohz_dep_set_cpu(int cpu, enum tick_dep_bits bit);
extern void tick_nohz_dep_clear_cpu(int cpu, enum tick_dep_bits bit);
extern void tick_nohz_dep_set_task(struct task_struct *tsk,
				   enum tick_dep_bits bit);
extern void tick_nohz_dep_clear_task(struct task_struct *tsk,
				     enum tick_dep_bits bit);
extern void tick_nohz_dep_set_signal(struct signal_struct *signal,
				     enum tick_dep_bits bit);
extern void tick_nohz_dep_clear_signal(struct signal_struct *signal,
				       enum tick_dep_bits bit);

/*
 * The below are tick_nohz_[set,clear]_dep() wrappers that optimize off-cases
 * on top of static keys.
 */
static inline void tick_dep_set(enum tick_dep_bits bit)
{
	if (tick_nohz_full_enabled())
		tick_nohz_dep_set(bit);
}

static inline void tick_dep_clear(enum tick_dep_bits bit)
{
	if (tick_nohz_full_enabled())
		tick_nohz_dep_clear(bit);
}

static inline void tick_dep_set_cpu(int cpu, enum tick_dep_bits bit)
{
	if (tick_nohz_full_cpu(cpu))
		tick_nohz_dep_set_cpu(cpu, bit);
}

static inline void tick_dep_clear_cpu(int cpu, enum tick_dep_bits bit)
{
	if (tick_nohz_full_cpu(cpu))
		tick_nohz_dep_clear_cpu(cpu, bit);
}

static inline void tick_dep_set_task(struct task_struct *tsk,
				     enum tick_dep_bits bit)
{
	if (tick_nohz_full_enabled())
		tick_nohz_dep_set_task(tsk, bit);
}
static inline void tick_dep_clear_task(struct task_struct *tsk,
				       enum tick_dep_bits bit)
{
	if (tick_nohz_full_enabled())
		tick_nohz_dep_clear_task(tsk, bit);
}
static inline void tick_dep_set_signal(struct signal_struct *signal,
				       enum tick_dep_bits bit)
{
	if (tick_nohz_full_enabled())
		tick_nohz_dep_set_signal(signal, bit);
}
static inline void tick_dep_clear_signal(struct signal_struct *signal,
					 enum tick_dep_bits bit)
{
	if (tick_nohz_full_enabled())
		tick_nohz_dep_clear_signal(signal, bit);
}

extern void tick_nohz_full_kick_cpu(int cpu);
extern void __tick_nohz_task_switch(void);
extern void __init tick_nohz_full_setup(cpumask_var_t cpumask);
#else
static inline bool tick_nohz_full_enabled(void) { return false; }
static inline bool tick_nohz_full_cpu(int cpu) { return false; }
static inline void tick_nohz_full_add_cpus_to(struct cpumask *mask) { }

static inline void tick_dep_set(enum tick_dep_bits bit) { }
static inline void tick_dep_clear(enum tick_dep_bits bit) { }
static inline void tick_dep_set_cpu(int cpu, enum tick_dep_bits bit) { }
static inline void tick_dep_clear_cpu(int cpu, enum tick_dep_bits bit) { }
static inline void tick_dep_set_task(struct task_struct *tsk,
				     enum tick_dep_bits bit) { }
static inline void tick_dep_clear_task(struct task_struct *tsk,
				       enum tick_dep_bits bit) { }
static inline void tick_dep_set_signal(struct signal_struct *signal,
				       enum tick_dep_bits bit) { }
static inline void tick_dep_clear_signal(struct signal_struct *signal,
					 enum tick_dep_bits bit) { }

static inline void tick_nohz_full_kick_cpu(int cpu) { }
static inline void __tick_nohz_task_switch(void) { }
static inline void tick_nohz_full_setup(cpumask_var_t cpumask) { }
#endif

static inline void tick_nohz_task_switch(void)
{
	if (tick_nohz_full_enabled())
		__tick_nohz_task_switch();
}

#endif
