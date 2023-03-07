/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Scheduler internal SMP callback types and methods between the scheduler
 * and other internal parts of the core kernel:
 */

extern void sched_ttwu_pending(void *arg);

extern bool call_function_single_prep_ipi(int cpu);

#ifdef CONFIG_SMP
extern void flush_smp_call_function_queue(void);
#else
static inline void flush_smp_call_function_queue(void) { }
#endif
