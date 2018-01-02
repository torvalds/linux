// SPDX-License-Identifier: GPL-2.0
#include <linux/init_task.h>
#include <linux/export.h>
#include <linux/mqueue.h>
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>

#include <asm/pgtable.h>
#include <linux/uaccess.h>

static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);


/*
 * Set up the first task table, touch at your own risk!. Base=0,
 * limit=0x1fffff (=2MB)
 */
struct task_struct init_task
#ifdef CONFIG_ARCH_TASK_STRUCT_ON_STACK
	__init_task_data
#endif
= {
	INIT_TASK_TI(init_task)
	.state		= 0,
	.stack		= init_stack,
	.usage		= ATOMIC_INIT(2),
	.flags		= PF_KTHREAD,
	.prio		= MAX_PRIO-20,
	.static_prio	= MAX_PRIO-20,
	.normal_prio	= MAX_PRIO-20,
	.policy		= SCHED_NORMAL,
	.cpus_allowed	= CPU_MASK_ALL,
	.nr_cpus_allowed= NR_CPUS,
	.mm		= NULL,
	.active_mm	= &init_mm,
	.restart_block = {
		.fn = do_no_restart_syscall,
	},
	.se		= {
		.group_node 	= LIST_HEAD_INIT(init_task.se.group_node),
	},
	.rt		= {
		.run_list	= LIST_HEAD_INIT(init_task.rt.run_list),
		.time_slice	= RR_TIMESLICE,
	},
	.tasks		= LIST_HEAD_INIT(init_task.tasks),
	INIT_PUSHABLE_TASKS(init_task)
	INIT_CGROUP_SCHED(init_task)
	.ptraced	= LIST_HEAD_INIT(init_task.ptraced),
	.ptrace_entry	= LIST_HEAD_INIT(init_task.ptrace_entry),
	.real_parent	= &init_task,
	.parent		= &init_task,
	.children	= LIST_HEAD_INIT(init_task.children),
	.sibling	= LIST_HEAD_INIT(init_task.sibling),
	.group_leader	= &init_task,
	RCU_POINTER_INITIALIZER(real_cred, &init_cred),
	RCU_POINTER_INITIALIZER(cred, &init_cred),
	.comm		= INIT_TASK_COMM,
	.thread		= INIT_THREAD,
	.fs		= &init_fs,
	.files		= &init_files,
	.signal		= &init_signals,
	.sighand	= &init_sighand,
	.nsproxy	= &init_nsproxy,
	.pending	= {
		.list = LIST_HEAD_INIT(init_task.pending.list),
		.signal = {{0}}
	},
	.blocked	= {{0}},
	.alloc_lock	= __SPIN_LOCK_UNLOCKED(init_task.alloc_lock),
	.journal_info	= NULL,
	INIT_CPU_TIMERS(init_task)
	.pi_lock	= __RAW_SPIN_LOCK_UNLOCKED(init_task.pi_lock),
	.timer_slack_ns = 50000, /* 50 usec default slack */
	.pids = {
		[PIDTYPE_PID]  = INIT_PID_LINK(PIDTYPE_PID),
		[PIDTYPE_PGID] = INIT_PID_LINK(PIDTYPE_PGID),
		[PIDTYPE_SID]  = INIT_PID_LINK(PIDTYPE_SID),
	},
	.thread_group	= LIST_HEAD_INIT(init_task.thread_group),
	.thread_node	= LIST_HEAD_INIT(init_signals.thread_head),
	INIT_IDS
	INIT_PERF_EVENTS(init_task)
	INIT_TRACE_IRQFLAGS
	INIT_LOCKDEP
	INIT_FTRACE_GRAPH
	INIT_TRACE_RECURSION
	INIT_TASK_RCU_PREEMPT(init_task)
	INIT_TASK_RCU_TASKS(init_task)
	INIT_CPUSET_SEQ(init_task)
	INIT_RT_MUTEXES(init_task)
	INIT_PREV_CPUTIME(init_task)
	INIT_VTIME(init_task)
	INIT_NUMA_BALANCING(init_task)
	INIT_KASAN(init_task)
	INIT_LIVEPATCH(init_task)
	INIT_TASK_SECURITY
};

EXPORT_SYMBOL(init_task);

/*
 * Initial thread structure. Alignment of this is handled by a special
 * linker map entry.
 */
#ifndef CONFIG_THREAD_INFO_IN_TASK
struct thread_info init_thread_info __init_thread_info = INIT_THREAD_INFO(init_task);
#endif
