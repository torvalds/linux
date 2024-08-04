/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PROFILE_H
#define _LINUX_PROFILE_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/cache.h>

#include <asm/errno.h>

#define CPU_PROFILING	1
#define SCHED_PROFILING	2
#define KVM_PROFILING	4

struct proc_dir_entry;
struct notifier_block;

#if defined(CONFIG_PROFILING) && defined(CONFIG_PROC_FS)
void create_prof_cpu_mask(void);
int create_proc_profile(void);
#else
static inline void create_prof_cpu_mask(void)
{
}

static inline int create_proc_profile(void)
{
	return 0;
}
#endif

#ifdef CONFIG_PROFILING

extern int prof_on __read_mostly;

/* init basic kernel profiler */
int profile_init(void);
int profile_setup(char *str);
void profile_tick(int type);
int setup_profiling_timer(unsigned int multiplier);

/*
 * Add multiple profiler hits to a given address:
 */
void profile_hits(int type, void *ip, unsigned int nr_hits);

/*
 * Single profiler hit:
 */
static inline void profile_hit(int type, void *ip)
{
	/*
	 * Speedup for the common (no profiling enabled) case:
	 */
	if (unlikely(prof_on == type))
		profile_hits(type, ip, 1);
}

struct task_struct;
struct mm_struct;

#else

#define prof_on 0

static inline int profile_init(void)
{
	return 0;
}

static inline void profile_tick(int type)
{
	return;
}

static inline void profile_hits(int type, void *ip, unsigned int nr_hits)
{
	return;
}

static inline void profile_hit(int type, void *ip)
{
	return;
}


#endif /* CONFIG_PROFILING */

#endif /* _LINUX_PROFILE_H */
