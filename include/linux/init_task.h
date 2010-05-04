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
#include <net/net_namespace.h>

extern struct files_struct init_files;
extern struct fs_struct init_fs;

#define INIT_SIGNALS(sig) {						\
	.count		= ATOMIC_INIT(1), 				\
	.wait_chldexit	= __WAIT_QUEUE_HEAD_INITIALIZER(sig.wait_chldexit),\
	.shared_pending	= { 						\
		.list = LIST_HEAD_INIT(sig.shared_pending.list),	\
		.signal =  {{0}}},					\
	.posix_timers	 = LIST_HEAD_INIT(sig.posix_timers),		\
	.cpu_timers	= INIT_CPU_TIMERS(sig.cpu_timers),		\
	.rlim		= INIT_RLIMITS,					\
	.cputimer	= { 						\
		.cputime = INIT_CPUTIME,				\
		.running = 0,						\
		.lock = __SPIN_LOCK_UNLOCKED(sig.cputimer.lock),	\
	},								\
}

extern struct nsproxy init_nsproxy;

#define INIT_SIGHAND(sighand) {						\
	.count		= ATOMIC_INIT(1), 				\
	.action		= { { { .sa_handler = NULL, } }, },		\
	.siglock	= __SPIN_LOCK_UNLOCKED(sighand.siglock),	\
	.signalfd_wqh	= __WAIT_QUEUE_HEAD_INITIALIZER(sighand.signalfd_wqh),	\
}

extern struct group_info init_groups;

#define INIT_STRUCT_PID {						\
	.count 		= ATOMIC_INIT(1),				\
	.tasks		= {						\
		{ .first = &init_task.pids[PIDTYPE_PID].node },		\
		{ .first = &init_task.pids[PIDTYPE_PGID].node },	\
		{ .first = &init_task.pids[PIDTYPE_SID].node },		\
	},								\
	.rcu		= RCU_HEAD_INIT,				\
	.level		= 0,						\
	.numbers	= { {						\
		.nr		= 0,					\
		.ns		= &init_pid_ns,				\
		.pid_chain	= { .next = NULL, .pprev = NULL },	\
	}, }								\
}

#define INIT_PID_LINK(type) 					\
{								\
	.node = {						\
		.next = NULL,					\
		.pprev = &init_struct_pid.tasks[type].first,	\
	},							\
	.pid = &init_struct_pid,				\
}

#ifdef CONFIG_AUDITSYSCALL
#define INIT_IDS \
	.loginuid = -1, \
	.sessionid = -1,
#else
#define INIT_IDS
#endif

/*
 * Because of the reduced scope of CAP_SETPCAP when filesystem
 * capabilities are in effect, it is safe to allow CAP_SETPCAP to
 * be available in the default configuration.
 */
# define CAP_INIT_BSET  CAP_FULL_SET

#ifdef CONFIG_TREE_PREEMPT_RCU
#define INIT_TASK_RCU_PREEMPT(tsk)					\
	.rcu_read_lock_nesting = 0,					\
	.rcu_read_unlock_special = 0,					\
	.rcu_blocked_node = NULL,					\
	.rcu_node_entry = LIST_HEAD_INIT(tsk.rcu_node_entry),
#else
#define INIT_TASK_RCU_PREEMPT(tsk)
#endif

extern struct cred init_cred;

#ifdef CONFIG_PERF_EVENTS
# define INIT_PERF_EVENTS(tsk)					\
	.perf_event_mutex = 						\
		 __MUTEX_INITIALIZER(tsk.perf_event_mutex),		\
	.perf_event_list = LIST_HEAD_INIT(tsk.perf_event_list),
#else
# define INIT_PERF_EVENTS(tsk)
#endif

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK(tsk)	\
{									\
	.state		= 0,						\
	.stack		= &init_thread_info,				\
	.usage		= ATOMIC_INIT(2),				\
	.flags		= PF_KTHREAD,					\
	.lock_depth	= -1,						\
	.prio		= MAX_PRIO-20,					\
	.static_prio	= MAX_PRIO-20,					\
	.normal_prio	= MAX_PRIO-20,					\
	.policy		= SCHED_NORMAL,					\
	.cpus_allowed	= CPU_MASK_ALL,					\
	.mm		= NULL,						\
	.active_mm	= &init_mm,					\
	.se		= {						\
		.group_node 	= LIST_HEAD_INIT(tsk.se.group_node),	\
	},								\
	.rt		= {						\
		.run_list	= LIST_HEAD_INIT(tsk.rt.run_list),	\
		.time_slice	= HZ, 					\
		.nr_cpus_allowed = NR_CPUS,				\
	},								\
	.tasks		= LIST_HEAD_INIT(tsk.tasks),			\
	.pushable_tasks = PLIST_NODE_INIT(tsk.pushable_tasks, MAX_PRIO), \
	.ptraced	= LIST_HEAD_INIT(tsk.ptraced),			\
	.ptrace_entry	= LIST_HEAD_INIT(tsk.ptrace_entry),		\
	.real_parent	= &tsk,						\
	.parent		= &tsk,						\
	.children	= LIST_HEAD_INIT(tsk.children),			\
	.sibling	= LIST_HEAD_INIT(tsk.sibling),			\
	.group_leader	= &tsk,						\
	.real_cred	= &init_cred,					\
	.cred		= &init_cred,					\
	.cred_guard_mutex =						\
		 __MUTEX_INITIALIZER(tsk.cred_guard_mutex),		\
	.comm		= "swapper",					\
	.thread		= INIT_THREAD,					\
	.fs		= &init_fs,					\
	.files		= &init_files,					\
	.signal		= &init_signals,				\
	.sighand	= &init_sighand,				\
	.nsproxy	= &init_nsproxy,				\
	.pending	= {						\
		.list = LIST_HEAD_INIT(tsk.pending.list),		\
		.signal = {{0}}},					\
	.blocked	= {{0}},					\
	.alloc_lock	= __SPIN_LOCK_UNLOCKED(tsk.alloc_lock),		\
	.journal_info	= NULL,						\
	.cpu_timers	= INIT_CPU_TIMERS(tsk.cpu_timers),		\
	.fs_excl	= ATOMIC_INIT(0),				\
	.pi_lock	= __RAW_SPIN_LOCK_UNLOCKED(tsk.pi_lock),	\
	.timer_slack_ns = 50000, /* 50 usec default slack */		\
	.pids = {							\
		[PIDTYPE_PID]  = INIT_PID_LINK(PIDTYPE_PID),		\
		[PIDTYPE_PGID] = INIT_PID_LINK(PIDTYPE_PGID),		\
		[PIDTYPE_SID]  = INIT_PID_LINK(PIDTYPE_SID),		\
	},								\
	.dirties = INIT_PROP_LOCAL_SINGLE(dirties),			\
	INIT_IDS							\
	INIT_PERF_EVENTS(tsk)						\
	INIT_TRACE_IRQFLAGS						\
	INIT_LOCKDEP							\
	INIT_FTRACE_GRAPH						\
	INIT_TRACE_RECURSION						\
	INIT_TASK_RCU_PREEMPT(tsk)					\
}


#define INIT_CPU_TIMERS(cpu_timers)					\
{									\
	LIST_HEAD_INIT(cpu_timers[0]),					\
	LIST_HEAD_INIT(cpu_timers[1]),					\
	LIST_HEAD_INIT(cpu_timers[2]),					\
}

/* Attach to the init_task data structure for proper alignment */
#define __init_task_data __attribute__((__section__(".data.init_task")))


#endif
