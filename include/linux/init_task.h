#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/file.h>
#include <linux/rcupdate.h>

#define INIT_FDTABLE \
{							\
	.max_fds	= NR_OPEN_DEFAULT, 		\
	.max_fdset	= __FD_SETSIZE, 		\
	.next_fd	= 0, 				\
	.fd		= &init_files.fd_array[0], 	\
	.close_on_exec	= &init_files.close_on_exec_init, \
	.open_fds	= &init_files.open_fds_init, 	\
	.rcu		= RCU_HEAD_INIT, 		\
	.free_files	= NULL,		 		\
	.next		= NULL,		 		\
}

#define INIT_FILES \
{ 							\
	.count		= ATOMIC_INIT(1), 		\
	.file_lock	= SPIN_LOCK_UNLOCKED, 		\
	.fdt		= &init_files.fdtab, 		\
	.fdtab		= INIT_FDTABLE,			\
	.close_on_exec_init = { { 0, } }, 		\
	.open_fds_init	= { { 0, } }, 			\
	.fd_array	= { NULL, } 			\
}

#define INIT_KIOCTX(name, which_mm) \
{							\
	.users		= ATOMIC_INIT(1),		\
	.dead		= 0,				\
	.mm		= &which_mm,			\
	.user_id	= 0,				\
	.next		= NULL,				\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER(name.wait), \
	.ctx_lock	= SPIN_LOCK_UNLOCKED,		\
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
	.page_table_lock =  SPIN_LOCK_UNLOCKED, 		\
	.mmlist		= LIST_HEAD_INIT(name.mmlist),		\
	.cpu_vm_mask	= CPU_MASK_ALL,				\
	.default_kioctx = INIT_KIOCTX(name.default_kioctx, name),	\
}

#define INIT_SIGNALS(sig) {	\
	.count		= ATOMIC_INIT(1), 		\
	.wait_chldexit	= __WAIT_QUEUE_HEAD_INITIALIZER(sig.wait_chldexit),\
	.shared_pending	= { 				\
		.list = LIST_HEAD_INIT(sig.shared_pending.list),	\
		.signal =  {{0}}}, \
	.posix_timers	 = LIST_HEAD_INIT(sig.posix_timers),		\
	.cpu_timers	= INIT_CPU_TIMERS(sig.cpu_timers),		\
	.rlim		= INIT_RLIMITS,					\
}

#define INIT_SIGHAND(sighand) {						\
	.count		= ATOMIC_INIT(1), 				\
	.action		= { { { .sa_handler = NULL, } }, },		\
	.siglock	= SPIN_LOCK_UNLOCKED, 				\
}

extern struct group_info init_groups;

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK(tsk)	\
{									\
	.state		= 0,						\
	.thread_info	= &init_thread_info,				\
	.usage		= ATOMIC_INIT(2),				\
	.flags		= 0,						\
	.lock_depth	= -1,						\
	.prio		= MAX_PRIO-20,					\
	.static_prio	= MAX_PRIO-20,					\
	.policy		= SCHED_NORMAL,					\
	.cpus_allowed	= CPU_MASK_ALL,					\
	.mm		= NULL,						\
	.active_mm	= &init_mm,					\
	.run_list	= LIST_HEAD_INIT(tsk.run_list),			\
	.ioprio		= 0,						\
	.time_slice	= HZ,						\
	.tasks		= LIST_HEAD_INIT(tsk.tasks),			\
	.ptrace_children= LIST_HEAD_INIT(tsk.ptrace_children),		\
	.ptrace_list	= LIST_HEAD_INIT(tsk.ptrace_list),		\
	.real_parent	= &tsk,						\
	.parent		= &tsk,						\
	.children	= LIST_HEAD_INIT(tsk.children),			\
	.sibling	= LIST_HEAD_INIT(tsk.sibling),			\
	.group_leader	= &tsk,						\
	.group_info	= &init_groups,					\
	.cap_effective	= CAP_INIT_EFF_SET,				\
	.cap_inheritable = CAP_INIT_INH_SET,				\
	.cap_permitted	= CAP_FULL_SET,					\
	.keep_capabilities = 0,						\
	.user		= INIT_USER,					\
	.comm		= "swapper",					\
	.thread		= INIT_THREAD,					\
	.fs		= &init_fs,					\
	.files		= &init_files,					\
	.signal		= &init_signals,				\
	.sighand	= &init_sighand,				\
	.pending	= {						\
		.list = LIST_HEAD_INIT(tsk.pending.list),		\
		.signal = {{0}}},					\
	.blocked	= {{0}},					\
	.alloc_lock	= SPIN_LOCK_UNLOCKED,				\
	.proc_lock	= SPIN_LOCK_UNLOCKED,				\
	.journal_info	= NULL,						\
	.cpu_timers	= INIT_CPU_TIMERS(tsk.cpu_timers),		\
	.fs_excl	= ATOMIC_INIT(0),				\
}


#define INIT_CPU_TIMERS(cpu_timers)					\
{									\
	LIST_HEAD_INIT(cpu_timers[0]),					\
	LIST_HEAD_INIT(cpu_timers[1]),					\
	LIST_HEAD_INIT(cpu_timers[2]),					\
}


#endif
