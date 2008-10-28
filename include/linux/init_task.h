#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/rcupdate.h>
#include <linux/irqflags.h>
#include <linux/utsname.h>
#include <linux/lockdep.h>
#include <linux/ipc.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/securebits.h>
#include <net/net_namespace.h>

extern struct files_struct init_files;

#define INIT_KIOCTX(name, which_mm) \
{							\
	.users		= ATOMIC_INIT(1),		\
	.dead		= 0,				\
	.mm		= &which_mm,			\
	.user_id	= 0,				\
	.next		= NULL,				\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER(name.wait), \
	.ctx_lock	= __SPIN_LOCK_UNLOCKED(name.ctx_lock), \
	.reqs_active	= 0U,				\
	.max_reqs	= ~0U,				\
}

#define INIT_MM(name) \
{			 					\
	.mm_rb		= RB_ROOT,				\
	.pgd		= swapper_pg_dir, 			\
	.mm_users	= ATOMIC_INIT(2), 			\
	.mm_count	= ATOMIC_INIT(1), 			\
	.mmap_sem	= __RWSEM_INITIALIZER(name.mmap_sem),	\
	.page_table_lock =  __SPIN_LOCK_UNLOCKED(name.page_table_lock),	\
	.mmlist		= LIST_HEAD_INIT(name.mmlist),		\
	.cpu_vm_mask	= CPU_MASK_ALL,				\
}

#define INIT_SIGNALS(sig) {						\
	.count		= ATOMIC_INIT(1), 				\
	.wait_chldexit	= __WAIT_QUEUE_HEAD_INITIALIZER(sig.wait_chldexit),\
	.shared_pending	= { 						\
		.list = LIST_HEAD_INIT(sig.shared_pending.list),	\
		.signal =  {{0}}},					\
	.posix_timers	 = LIST_HEAD_INIT(sig.posix_timers),		\
	.cpu_timers	= INIT_CPU_TIMERS(sig.cpu_timers),		\
	.rlim		= INIT_RLIMITS,					\
}

extern struct nsproxy init_nsproxy;
#define INIT_NSPROXY(nsproxy) {						\
	.pid_ns		= &init_pid_ns,					\
	.count		= ATOMIC_INIT(1),				\
	.uts_ns		= &init_uts_ns,					\
	.mnt_ns		= NULL,						\
	INIT_NET_NS(net_ns)                                             \
	INIT_IPC_NS(ipc_ns)						\
	.user_ns	= &init_user_ns,				\
}

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

#ifdef CONFIG_SECURITY_FILE_CAPABILITIES
/*
 * Because of the reduced scope of CAP_SETPCAP when filesystem
 * capabilities are in effect, it is safe to allow CAP_SETPCAP to
 * be available in the default configuration.
 */
# define CAP_INIT_BSET  CAP_FULL_SET
#else
# define CAP_INIT_BSET  CAP_INIT_EFF_SET
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
	.ptraced	= LIST_HEAD_INIT(tsk.ptraced),			\
	.ptrace_entry	= LIST_HEAD_INIT(tsk.ptrace_entry),		\
	.real_parent	= &tsk,						\
	.parent		= &tsk,						\
	.children	= LIST_HEAD_INIT(tsk.children),			\
	.sibling	= LIST_HEAD_INIT(tsk.sibling),			\
	.group_leader	= &tsk,						\
	.group_info	= &init_groups,					\
	.cap_effective	= CAP_INIT_EFF_SET,				\
	.cap_inheritable = CAP_INIT_INH_SET,				\
	.cap_permitted	= CAP_FULL_SET,					\
	.cap_bset 	= CAP_INIT_BSET,				\
	.securebits     = SECUREBITS_DEFAULT,				\
	.user		= INIT_USER,					\
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
	.pi_lock	= __SPIN_LOCK_UNLOCKED(tsk.pi_lock),		\
	.timer_slack_ns = 50000, /* 50 usec default slack */		\
	.pids = {							\
		[PIDTYPE_PID]  = INIT_PID_LINK(PIDTYPE_PID),		\
		[PIDTYPE_PGID] = INIT_PID_LINK(PIDTYPE_PGID),		\
		[PIDTYPE_SID]  = INIT_PID_LINK(PIDTYPE_SID),		\
	},								\
	.dirties = INIT_PROP_LOCAL_SINGLE(dirties),			\
	INIT_IDS							\
	INIT_TRACE_IRQFLAGS						\
	INIT_LOCKDEP							\
}


#define INIT_CPU_TIMERS(cpu_timers)					\
{									\
	LIST_HEAD_INIT(cpu_timers[0]),					\
	LIST_HEAD_INIT(cpu_timers[1]),					\
	LIST_HEAD_INIT(cpu_timers[2]),					\
}


#endif
