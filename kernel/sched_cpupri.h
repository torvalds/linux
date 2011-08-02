#ifndef _LINUX_CPUPRI_H
#define _LINUX_CPUPRI_H

#include <linux/sched.h>

#define CPUPRI_NR_PRIORITIES	(MAX_RT_PRIO + 2)
#define CPUPRI_NR_PRI_WORDS	BITS_TO_LONGS(CPUPRI_NR_PRIORITIES)

#define CPUPRI_INVALID -1
#define CPUPRI_IDLE     0
#define CPUPRI_NORMAL   1
/* values 2-101 are RT priorities 0-99 */

struct cpupri_vec {
	atomic_t	count;
	cpumask_var_t	mask;
};

struct cpupri {
	struct cpupri_vec pri_to_cpu[CPUPRI_NR_PRIORITIES];
	long              pri_active[CPUPRI_NR_PRI_WORDS];
	int               cpu_to_pri[NR_CPUS];
};

#ifdef CONFIG_SMP
int  cpupri_find(struct cpupri *cp,
		 struct task_struct *p, struct cpumask *lowest_mask);
void cpupri_set(struct cpupri *cp, int cpu, int pri);
int cpupri_init(struct cpupri *cp);
void cpupri_cleanup(struct cpupri *cp);
#else
#define cpupri_set(cp, cpu, pri) do { } while (0)
#define cpupri_init() do { } while (0)
#endif

#endif /* _LINUX_CPUPRI_H */
