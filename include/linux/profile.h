#ifndef _LINUX_PROFILE_H
#define _LINUX_PROFILE_H

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/cache.h>

#include <asm/errno.h>

extern int prof_on __read_mostly;

#define CPU_PROFILING	1
#define SCHED_PROFILING	2
#define SLEEP_PROFILING	3
#define KVM_PROFILING	4

struct proc_dir_entry;
struct pt_regs;
struct notifier_block;

/* init basic kernel profiler */
void __init profile_init(void);
void profile_tick(int);

/*
 * Add multiple profiler hits to a given address:
 */
void profile_hits(int, void *ip, unsigned int nr_hits);

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

#ifdef CONFIG_PROC_FS
void create_prof_cpu_mask(struct proc_dir_entry *);
#else
#define create_prof_cpu_mask(x)			do { (void)(x); } while (0)
#endif

enum profile_type {
	PROFILE_TASK_EXIT,
	PROFILE_MUNMAP
};

#ifdef CONFIG_PROFILING

struct task_struct;
struct mm_struct;

/* task is in do_exit() */
void profile_task_exit(struct task_struct * task);

/* task is dead, free task struct ? Returns 1 if
 * the task was taken, 0 if the task should be freed.
 */
int profile_handoff_task(struct task_struct * task);

/* sys_munmap */
void profile_munmap(unsigned long addr);

int task_handoff_register(struct notifier_block * n);
int task_handoff_unregister(struct notifier_block * n);

int profile_event_register(enum profile_type, struct notifier_block * n);
int profile_event_unregister(enum profile_type, struct notifier_block * n);

int register_timer_hook(int (*hook)(struct pt_regs *));
void unregister_timer_hook(int (*hook)(struct pt_regs *));

/* Timer based profiling hook */
extern int (*timer_hook)(struct pt_regs *);

struct pt_regs;

#else

static inline int task_handoff_register(struct notifier_block * n)
{
	return -ENOSYS;
}

static inline int task_handoff_unregister(struct notifier_block * n)
{
	return -ENOSYS;
}

static inline int profile_event_register(enum profile_type t, struct notifier_block * n)
{
	return -ENOSYS;
}

static inline int profile_event_unregister(enum profile_type t, struct notifier_block * n)
{
	return -ENOSYS;
}

#define profile_task_exit(a) do { } while (0)
#define profile_handoff_task(a) (0)
#define profile_munmap(a) do { } while (0)

static inline int register_timer_hook(int (*hook)(struct pt_regs *))
{
	return -ENOSYS;
}

static inline void unregister_timer_hook(int (*hook)(struct pt_regs *))
{
	return;
}

#endif /* CONFIG_PROFILING */

#endif /* __KERNEL__ */

#endif /* _LINUX_PROFILE_H */
