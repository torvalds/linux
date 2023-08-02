/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CPU_SMT_H_
#define _LINUX_CPU_SMT_H_

enum cpuhp_smt_control {
	CPU_SMT_ENABLED,
	CPU_SMT_DISABLED,
	CPU_SMT_FORCE_DISABLED,
	CPU_SMT_NOT_SUPPORTED,
	CPU_SMT_NOT_IMPLEMENTED,
};

#if defined(CONFIG_SMP) && defined(CONFIG_HOTPLUG_SMT)
extern enum cpuhp_smt_control cpu_smt_control;
extern unsigned int cpu_smt_num_threads;
extern void cpu_smt_disable(bool force);
extern void cpu_smt_set_num_threads(unsigned int num_threads,
				    unsigned int max_threads);
extern bool cpu_smt_possible(void);
extern int cpuhp_smt_enable(void);
extern int cpuhp_smt_disable(enum cpuhp_smt_control ctrlval);
#else
# define cpu_smt_control               (CPU_SMT_NOT_IMPLEMENTED)
# define cpu_smt_num_threads 1
static inline void cpu_smt_disable(bool force) { }
static inline void cpu_smt_set_num_threads(unsigned int num_threads,
					   unsigned int max_threads) { }
static inline bool cpu_smt_possible(void) { return false; }
static inline int cpuhp_smt_enable(void) { return 0; }
static inline int cpuhp_smt_disable(enum cpuhp_smt_control ctrlval) { return 0; }
#endif

#endif /* _LINUX_CPU_SMT_H_ */
