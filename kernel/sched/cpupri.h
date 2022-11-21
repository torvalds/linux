/* SPDX-License-Identifier: GPL-2.0 */

#define CPUPRI_NR_PRIORITIES	(MAX_RT_PRIO+1)

#define CPUPRI_INVALID		-1
#define CPUPRI_NORMAL		 0
/* values 1-99 are for RT1-RT99 priorities */
#define CPUPRI_HIGHER		100

struct cpupri_vec {
	atomic_t		count;
	cpumask_var_t		mask;
};

struct cpupri {
	struct cpupri_vec	pri_to_cpu[CPUPRI_NR_PRIORITIES];
	int			*cpu_to_pri;
};

#ifdef CONFIG_SMP
int  cpupri_find(struct cpupri *cp, struct task_struct *p,
		 struct cpumask *lowest_mask);
int  cpupri_find_fitness(struct cpupri *cp, struct task_struct *p,
			 struct cpumask *lowest_mask,
			 bool (*fitness_fn)(struct task_struct *p, int cpu));
void cpupri_set(struct cpupri *cp, int cpu, int pri);
int  cpupri_init(struct cpupri *cp);
void cpupri_cleanup(struct cpupri *cp);
#endif
