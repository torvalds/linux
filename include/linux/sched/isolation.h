#ifndef _LINUX_SCHED_ISOLATION_H
#define _LINUX_SCHED_ISOLATION_H

#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/tick.h>

#ifdef CONFIG_NO_HZ_FULL
extern cpumask_var_t housekeeping_mask;
extern void __init housekeeping_init(void);
#else
static inline void housekeeping_init(void) { }
#endif /* CONFIG_NO_HZ_FULL */

static inline int housekeeping_any_cpu(void)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled())
		return cpumask_any_and(housekeeping_mask, cpu_online_mask);
#endif
	return smp_processor_id();
}

static inline const struct cpumask *housekeeping_cpumask(void)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled())
		return housekeeping_mask;
#endif
	return cpu_possible_mask;
}

static inline bool is_housekeeping_cpu(int cpu)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled())
		return cpumask_test_cpu(cpu, housekeeping_mask);
#endif
	return true;
}

static inline void housekeeping_affine(struct task_struct *t)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled())
		set_cpus_allowed_ptr(t, housekeeping_mask);

#endif
}

#endif /* _LINUX_SCHED_ISOLATION_H */
