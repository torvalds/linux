/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Common functions for in-kernel torture tests.
 *
 * Copyright IBM Corporation, 2014
 *
 * Author: Paul E. McKenney <paulmck@linux.ibm.com>
 */

#ifndef __LINUX_TORTURE_H
#define __LINUX_TORTURE_H

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>
#include <linux/lockdep.h>
#include <linux/completion.h>
#include <linux/debugobjects.h>
#include <linux/bug.h>
#include <linux/compiler.h>

/* Definitions for a non-string torture-test module parameter. */
#define torture_param(type, name, init, msg) \
	static type name = init; \
	module_param(name, type, 0444); \
	MODULE_PARM_DESC(name, msg);

#define TORTURE_FLAG "-torture:"
#define TOROUT_STRING(s) \
	pr_alert("%s" TORTURE_FLAG " %s\n", torture_type, s)
#define VERBOSE_TOROUT_STRING(s) \
	do { if (verbose) pr_alert("%s" TORTURE_FLAG " %s\n", torture_type, s); } while (0)
#define VERBOSE_TOROUT_ERRSTRING(s) \
	do { if (verbose) pr_alert("%s" TORTURE_FLAG "!!! %s\n", torture_type, s); } while (0)

/* Definitions for online/offline exerciser. */
typedef void torture_ofl_func(void);
bool torture_offline(int cpu, long *n_onl_attempts, long *n_onl_successes,
		     unsigned long *sum_offl, int *min_onl, int *max_onl);
bool torture_online(int cpu, long *n_onl_attempts, long *n_onl_successes,
		    unsigned long *sum_onl, int *min_onl, int *max_onl);
int torture_onoff_init(long ooholdoff, long oointerval, torture_ofl_func *f);
void torture_onoff_stats(void);
bool torture_onoff_failures(void);

/* Low-rider random number generator. */
struct torture_random_state {
	unsigned long trs_state;
	long trs_count;
};
#define DEFINE_TORTURE_RANDOM(name) struct torture_random_state name = { 0, 0 }
#define DEFINE_TORTURE_RANDOM_PERCPU(name) \
	DEFINE_PER_CPU(struct torture_random_state, name)
unsigned long torture_random(struct torture_random_state *trsp);

/* Task shuffler, which causes CPUs to occasionally go idle. */
void torture_shuffle_task_register(struct task_struct *tp);
int torture_shuffle_init(long shuffint);

/* Test auto-shutdown handling. */
void torture_shutdown_absorb(const char *title);
int torture_shutdown_init(int ssecs, void (*cleanup)(void));

/* Task stuttering, which forces load/no-load transitions. */
bool stutter_wait(const char *title);
int torture_stutter_init(int s);

/* Initialization and cleanup. */
bool torture_init_begin(char *ttype, int v);
void torture_init_end(void);
bool torture_cleanup_begin(void);
void torture_cleanup_end(void);
bool torture_must_stop(void);
bool torture_must_stop_irq(void);
void torture_kthread_stopping(char *title);
int _torture_create_kthread(int (*fn)(void *arg), void *arg, char *s, char *m,
			     char *f, struct task_struct **tp);
void _torture_stop_kthread(char *m, struct task_struct **tp);

#define torture_create_kthread(n, arg, tp) \
	_torture_create_kthread(n, (arg), #n, "Creating " #n " task", \
				"Failed to create " #n, &(tp))
#define torture_stop_kthread(n, tp) \
	_torture_stop_kthread("Stopping " #n " task", &(tp))

#ifdef CONFIG_PREEMPT
#define torture_preempt_schedule() preempt_schedule()
#else
#define torture_preempt_schedule()
#endif

#endif /* __LINUX_TORTURE_H */
