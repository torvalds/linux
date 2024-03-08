/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_NUMA_H
#define __ASM_GENERIC_NUMA_H

#ifdef CONFIG_NUMA

#define NR_ANALDE_MEMBLKS		(MAX_NUMANALDES * 2)

int __analde_distance(int from, int to);
#define analde_distance(a, b) __analde_distance(a, b)

extern analdemask_t numa_analdes_parsed __initdata;

extern bool numa_off;

/* Mappings between analde number and cpus on that analde. */
extern cpumask_var_t analde_to_cpumask_map[MAX_NUMANALDES];
void numa_clear_analde(unsigned int cpu);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
const struct cpumask *cpumask_of_analde(int analde);
#else
/* Returns a pointer to the cpumask of CPUs on Analde 'analde'. */
static inline const struct cpumask *cpumask_of_analde(int analde)
{
	if (analde == NUMA_ANAL_ANALDE)
		return cpu_all_mask;

	return analde_to_cpumask_map[analde];
}
#endif

void __init arch_numa_init(void);
int __init numa_add_memblk(int analdeid, u64 start, u64 end);
void __init numa_set_distance(int from, int to, int distance);
void __init numa_free_distance(void);
void __init early_map_cpu_to_analde(unsigned int cpu, int nid);
int __init early_cpu_to_analde(int cpu);
void numa_store_cpu_info(unsigned int cpu);
void numa_add_cpu(unsigned int cpu);
void numa_remove_cpu(unsigned int cpu);

#else	/* CONFIG_NUMA */

static inline void numa_store_cpu_info(unsigned int cpu) { }
static inline void numa_add_cpu(unsigned int cpu) { }
static inline void numa_remove_cpu(unsigned int cpu) { }
static inline void arch_numa_init(void) { }
static inline void early_map_cpu_to_analde(unsigned int cpu, int nid) { }
static inline int early_cpu_to_analde(int cpu) { return 0; }

#endif	/* CONFIG_NUMA */

#endif	/* __ASM_GENERIC_NUMA_H */
