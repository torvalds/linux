/*
 * Common functions for in-kernel torture tests.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2014
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
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
	pr_alert("%s" TORTURE_FLAG s "\n", torture_type)
#define VERBOSE_TOROUT_STRING(s) \
	do { if (verbose) pr_alert("%s" TORTURE_FLAG s "\n", torture_type); } while (0)
#define VERBOSE_TOROUT_ERRSTRING(s) \
	do { if (verbose) pr_alert("%s" TORTURE_FLAG "!!! " s "\n", torture_type); } while (0)

/* Definitions for a non-string torture-test module parameter. */
#define torture_parm(type, name, init, msg) \
	static type name = init; \
	module_param(name, type, 0444); \
	MODULE_PARM_DESC(name, msg);

/* Definitions for online/offline exerciser. */
int torture_onoff_init(long ooholdoff, long oointerval);
char *torture_onoff_stats(char *page);
bool torture_onoff_failures(void);

/* Low-rider random number generator. */
struct torture_random_state {
	unsigned long trs_state;
	long trs_count;
};
#define DEFINE_TORTURE_RANDOM(name) struct torture_random_state name = { 0, 0 }
unsigned long torture_random(struct torture_random_state *trsp);

/* Task shuffler, which causes CPUs to occasionally go idle. */
void torture_shuffle_task_register(struct task_struct *tp);
int torture_shuffle_init(long shuffint);

/* Shutdown task absorption, for when the tasks cannot safely be killed. */
void torture_shutdown_absorb(const char *title);

/* Initialization and cleanup. */
void torture_init_begin(char *ttype, bool v);
void torture_init_end(void);
bool torture_cleanup(void);
bool torture_must_stop(void);
bool torture_must_stop_irq(void);

#endif /* __LINUX_TORTURE_H */
