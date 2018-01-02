/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/rcupdate.h>
#include <linux/irqflags.h>
#include <linux/utsname.h>
#include <linux/lockdep.h>
#include <linux/ftrace.h>
#include <linux/ipc.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/securebits.h>
#include <linux/seqlock.h>
#include <linux/rbtree.h>
#include <linux/sched/autogroup.h>
#include <net/net_namespace.h>
#include <linux/sched/rt.h>
#include <linux/livepatch.h>
#include <linux/mm_types.h>

#include <asm/thread_info.h>

extern struct files_struct init_files;
extern struct fs_struct init_fs;

#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
#define INIT_PREV_CPUTIME(x)	.prev_cputime = {			\
	.lock = __RAW_SPIN_LOCK_UNLOCKED(x.prev_cputime.lock),		\
},
#else
#define INIT_PREV_CPUTIME(x)
#endif

#ifdef CONFIG_POSIX_TIMERS
#define INIT_POSIX_TIMERS(s)						\
	.posix_timers = LIST_HEAD_INIT(s.posix_timers),
#define INIT_CPU_TIMERS(s)						\
	.cpu_timers = {							\
		LIST_HEAD_INIT(s.cpu_timers[0]),			\
		LIST_HEAD_INIT(s.cpu_timers[1]),			\
		LIST_HEAD_INIT(s.cpu_timers[2]),								\
	},
#define INIT_CPUTIMER(s)						\
	.cputimer	= { 						\
		.cputime_atomic	= INIT_CPUTIME_ATOMIC,			\
		.running	= false,				\
		.checking_timer = false,				\
	},
#else
#define INIT_POSIX_TIMERS(s)
#define INIT_CPU_TIMERS(s)
#define INIT_CPUTIMER(s)
#endif

#define INIT_SIGNALS(sig) {						\
	.nr_threads	= 1,						\
	.thread_head	= LIST_HEAD_INIT(init_task.thread_node),	\
	.wait_chldexit	= __WAIT_QUEUE_HEAD_INITIALIZER(sig.wait_chldexit),\
	.shared_pending	= { 						\
		.list = LIST_HEAD_INIT(sig.shared_pending.list),	\
		.signal =  {{0}}},					\
	INIT_POSIX_TIMERS(sig)						\
	INIT_CPU_TIMERS(sig)						\
	.rlim		= INIT_RLIMITS,					\
	INIT_CPUTIMER(sig)						\
	INIT_PREV_CPUTIME(sig)						\
	.cred_guard_mutex =						\
		 __MUTEX_INITIALIZER(sig.cred_guard_mutex),		\
}

extern struct nsproxy init_nsproxy;

#define INIT_SIGHAND(sighand) {						\
	.count		= ATOMIC_INIT(1), 				\
	.action		= { { { .sa_handler = SIG_DFL, } }, },		\
	.siglock	= __SPIN_LOCK_UNLOCKED(sighand.siglock),	\
	.signalfd_wqh	= __WAIT_QUEUE_HEAD_INITIALIZER(sighand.signalfd_wqh),	\
}

extern struct group_info init_groups;

#define INIT_STRUCT_PID {						\
	.count 		= ATOMIC_INIT(1),				\
	.tasks		= {						\
		{ .first = NULL },					\
		{ .first = NULL },					\
		{ .first = NULL },					\
	},								\
	.level		= 0,						\
	.numbers	= { {						\
		.nr		= 0,					\
		.ns		= &init_pid_ns,				\
	}, }								\
}

#define INIT_PID_LINK(type) 					\
{								\
	.node = {						\
		.next = NULL,					\
		.pprev = NULL,					\
	},							\
	.pid = &init_struct_pid,				\
}

extern struct cred init_cred;

#define INIT_TASK_COMM "swapper"

/* Attach to the init_task data structure for proper alignment */
#ifdef CONFIG_ARCH_TASK_STRUCT_ON_STACK
#define __init_task_data __attribute__((__section__(".data..init_task")))
#else
#define __init_task_data /**/
#endif

/* Attach to the thread_info data structure for proper alignment */
#define __init_thread_info __attribute__((__section__(".data..init_thread_info")))

#endif
