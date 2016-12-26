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
#include <net/net_namespace.h>
#include <linux/sched/rt.h>

#include <asm/thread_info.h>

#ifdef CONFIG_SMP
# define INIT_PUSHABLE_TASKS(tsk)					\
	.pushable_tasks = PLIST_NODE_INIT(tsk.pushable_tasks, MAX_PRIO),
#else
# define INIT_PUSHABLE_TASKS(tsk)
#endif

extern struct files_struct init_files;
extern struct fs_struct init_fs;

#ifdef CONFIG_CPUSETS
#define INIT_CPUSET_SEQ(tsk)							\
	.mems_allowed_seq = SEQCNT_ZERO(tsk.mems_allowed_seq),
#else
#define INIT_CPUSET_SEQ(tsk)
#endif

#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
#define INIT_PREV_CPUTIME(x)	.prev_cputime = {			\
	.lock = __RAW_SPIN_LOCK_UNLOCKED(x.prev_cputime.lock),		\
},
#else
#define INIT_PREV_CPUTIME(x)
#endif

#define INIT_SIGNALS(sig) {						\
	.nr_threads	= 1,						\
	.thread_head	= LIST_HEAD_INIT(init_task.thread_node),	\
	.wait_chldexit	= __WAIT_QUEUE_HEAD_INITIALIZER(sig.wait_chldexit),\
	.shared_pending	= { 						\
		.list = LIST_HEAD_INIT(sig.shared_pending.list),	\
		.signal =  {{0}}},					\
	.posix_timers	 = LIST_HEAD_INIT(sig.posix_timers),		\
	.cpu_timers	= INIT_CPU_TIMERS(sig.cpu_timers),		\
	.rlim		= INIT_RLIMITS,					\
	.cputimer	= { 						\
		.cputime_atomic	= INIT_CPUTIME_ATOMIC,			\
		.running	= false,				\
		.checking_timer = false,				\
	},								\
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
		.pid_chain	= { .next = NULL, .pprev = NULL },	\
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

#ifdef CONFIG_AUDITSYSCALL
#define INIT_IDS \
	.loginuid = INVALID_UID, \
	.sessionid = (unsigned int)-1,
#else
#define INIT_IDS
#endif

#ifdef CONFIG_PREEMPT_RCU
#define INIT_TASK_RCU_TREE_PREEMPT()					\
	.rcu_blocked_node = NULL,
#else
#define INIT_TASK_RCU_TREE_PREEMPT(tsk)
#endif
#ifdef CONFIG_PREEMPT_RCU
#define INIT_TASK_RCU_PREEMPT(tsk)					\
	.rcu_read_lock_nesting = 0,					\
	.rcu_read_unlock_special.s = 0,					\
	.rcu_node_entry = LIST_HEAD_INIT(tsk.rcu_node_entry),		\
	INIT_TASK_RCU_TREE_PREEMPT()
#else
#define INIT_TASK_RCU_PREEMPT(tsk)
#endif
#ifdef CONFIG_TASKS_RCU
#define INIT_TASK_RCU_TASKS(tsk)					\
	.rcu_tasks_holdout = false,					\
	.rcu_tasks_holdout_list =					\
		LIST_HEAD_INIT(tsk.rcu_tasks_holdout_list),		\
	.rcu_tasks_idle_cpu = -1,
#else
#define INIT_TASK_RCU_TASKS(tsk)
#endif

extern struct cred init_cred;

extern struct task_group root_task_group;

#ifdef CONFIG_CGROUP_SCHED
# define INIT_CGROUP_SCHED(tsk)						\
	.sched_task_group = &root_task_group,
#else
# define INIT_CGROUP_SCHED(tsk)
#endif

#ifdef CONFIG_PERF_EVENTS
# define INIT_PERF_EVENTS(tsk)						\
	.perf_event_mutex = 						\
		 __MUTEX_INITIALIZER(tsk.perf_event_mutex),		\
	.perf_event_list = LIST_HEAD_INIT(tsk.perf_event_list),
#else
# define INIT_PERF_EVENTS(tsk)
#endif

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
# define INIT_VTIME(tsk)						\
	.vtime_seqcount = SEQCNT_ZERO(tsk.vtime_seqcount),	\
	.vtime_snap = 0,				\
	.vtime_snap_whence = VTIME_SYS,
#else
# define INIT_VTIME(tsk)
#endif

#define INIT_TASK_COMM "swapper"

#ifdef CONFIG_RT_MUTEXES
# define INIT_RT_MUTEXES(tsk)						\
	.pi_waiters = RB_ROOT,						\
	.pi_waiters_leftmost = NULL,
#else
# define INIT_RT_MUTEXES(tsk)
#endif

#ifdef CONFIG_NUMA_BALANCING
# define INIT_NUMA_BALANCING(tsk)					\
	.numa_preferred_nid = -1,					\
	.numa_group = NULL,						\
	.numa_faults = NULL,
#else
# define INIT_NUMA_BALANCING(tsk)
#endif

#ifdef CONFIG_KASAN
# define INIT_KASAN(tsk)						\
	.kasan_depth = 1,
#else
# define INIT_KASAN(tsk)
#endif

#ifdef CONFIG_THREAD_INFO_IN_TASK
# define INIT_TASK_TI(tsk)			\
	.thread_info = INIT_THREAD_INFO(tsk),	\
	.stack_refcount = ATOMIC_INIT(1),
#else
# define INIT_TASK_TI(tsk)
#endif

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK(tsk)	\
{									\
	INIT_TASK_TI(tsk)						\
	.state		= 0,						\
	.stack		= init_stack,					\
	.usage		= ATOMIC_INIT(2),				\
	.flags		= PF_KTHREAD,					\
	.prio		= MAX_PRIO-20,					\
	.static_prio	= MAX_PRIO-20,					\
	.normal_prio	= MAX_PRIO-20,					\
	.policy		= SCHED_NORMAL,					\
	.cpus_allowed	= CPU_MASK_ALL,					\
	.nr_cpus_allowed= NR_CPUS,					\
	.mm		= NULL,						\
	.active_mm	= &init_mm,					\
	.restart_block = {						\
		.fn = do_no_restart_syscall,				\
	},								\
	.se		= {						\
		.group_node 	= LIST_HEAD_INIT(tsk.se.group_node),	\
	},								\
	.rt		= {						\
		.run_list	= LIST_HEAD_INIT(tsk.rt.run_list),	\
		.time_slice	= RR_TIMESLICE,				\
	},								\
	.tasks		= LIST_HEAD_INIT(tsk.tasks),			\
	INIT_PUSHABLE_TASKS(tsk)					\
	INIT_CGROUP_SCHED(tsk)						\
	.ptraced	= LIST_HEAD_INIT(tsk.ptraced),			\
	.ptrace_entry	= LIST_HEAD_INIT(tsk.ptrace_entry),		\
	.real_parent	= &tsk,						\
	.parent		= &tsk,						\
	.children	= LIST_HEAD_INIT(tsk.children),			\
	.sibling	= LIST_HEAD_INIT(tsk.sibling),			\
	.group_leader	= &tsk,						\
	RCU_POINTER_INITIALIZER(real_cred, &init_cred),			\
	RCU_POINTER_INITIALIZER(cred, &init_cred),			\
	.comm		= INIT_TASK_COMM,				\
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
	.pi_lock	= __RAW_SPIN_LOCK_UNLOCKED(tsk.pi_lock),	\
	.timer_slack_ns = 50000, /* 50 usec default slack */		\
	.pids = {							\
		[PIDTYPE_PID]  = INIT_PID_LINK(PIDTYPE_PID),		\
		[PIDTYPE_PGID] = INIT_PID_LINK(PIDTYPE_PGID),		\
		[PIDTYPE_SID]  = INIT_PID_LINK(PIDTYPE_SID),		\
	},								\
	.thread_group	= LIST_HEAD_INIT(tsk.thread_group),		\
	.thread_node	= LIST_HEAD_INIT(init_signals.thread_head),	\
	INIT_IDS							\
	INIT_PERF_EVENTS(tsk)						\
	INIT_TRACE_IRQFLAGS						\
	INIT_LOCKDEP							\
	INIT_FTRACE_GRAPH						\
	INIT_TRACE_RECURSION						\
	INIT_TASK_RCU_PREEMPT(tsk)					\
	INIT_TASK_RCU_TASKS(tsk)					\
	INIT_CPUSET_SEQ(tsk)						\
	INIT_RT_MUTEXES(tsk)						\
	INIT_PREV_CPUTIME(tsk)						\
	INIT_VTIME(tsk)							\
	INIT_NUMA_BALANCING(tsk)					\
	INIT_KASAN(tsk)							\
}


#define INIT_CPU_TIMERS(cpu_timers)					\
{									\
	LIST_HEAD_INIT(cpu_timers[0]),					\
	LIST_HEAD_INIT(cpu_timers[1]),					\
	LIST_HEAD_INIT(cpu_timers[2]),					\
}

/* Attach to the init_task data structure for proper alignment */
#define __init_task_data __attribute__((__section__(".data..init_task")))


#endif
