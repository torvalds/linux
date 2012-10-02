/*
 * tsacct_kern.h - kernel header for system accounting over taskstats interface
 *
 * Copyright (C) Jay Lan	SGI
 */

#ifndef _LINUX_TSACCT_KERN_H
#define _LINUX_TSACCT_KERN_H

#include <linux/taskstats.h>

#ifdef CONFIG_TASKSTATS
extern void bacct_add_tsk(struct user_namespace *user_ns,
			  struct pid_namespace *pid_ns,
			  struct taskstats *stats, struct task_struct *tsk);
#else
static inline void bacct_add_tsk(struct user_namespace *user_ns,
				 struct pid_namespace *pid_ns,
				 struct taskstats *stats, struct task_struct *tsk)
{}
#endif /* CONFIG_TASKSTATS */

#ifdef CONFIG_TASK_XACCT
extern void xacct_add_tsk(struct taskstats *stats, struct task_struct *p);
extern void acct_update_integrals(struct task_struct *tsk);
extern void acct_clear_integrals(struct task_struct *tsk);
#else
static inline void xacct_add_tsk(struct taskstats *stats, struct task_struct *p)
{}
static inline void acct_update_integrals(struct task_struct *tsk)
{}
static inline void acct_clear_integrals(struct task_struct *tsk)
{}
#endif /* CONFIG_TASK_XACCT */

#endif


