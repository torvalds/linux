/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dtask
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DTASK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DTASK_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#ifdef __GENKSYMS__
struct mutex;
struct rt_mutex;
struct rw_semaphore;
struct task_struct;
#else
/* struct mutex */
#include <linux/mutex.h>
/* struct rt_mutex */
#include <linux/rtmutex.h>
/* struct rw_semaphore */
#include <linux/rwsem.h>
/* struct task_struct */
#include <linux/sched.h>
#endif /* __GENKSYMS__ */
DECLARE_HOOK(android_vh_mutex_wait_start,
	TP_PROTO(struct mutex *lock),
	TP_ARGS(lock));
DECLARE_HOOK(android_vh_mutex_wait_finish,
	TP_PROTO(struct mutex *lock),
	TP_ARGS(lock));
DECLARE_HOOK(android_vh_mutex_opt_spin_start,
	TP_PROTO(struct mutex *lock, bool *time_out, int *cnt),
	TP_ARGS(lock, time_out, cnt));
DECLARE_HOOK(android_vh_mutex_opt_spin_finish,
	TP_PROTO(struct mutex *lock, bool taken),
	TP_ARGS(lock, taken));
DECLARE_HOOK(android_vh_mutex_can_spin_on_owner,
	TP_PROTO(struct mutex *lock, int *retval),
	TP_ARGS(lock, retval));

DECLARE_HOOK(android_vh_rtmutex_wait_start,
	TP_PROTO(struct rt_mutex *lock),
	TP_ARGS(lock));
DECLARE_HOOK(android_vh_rtmutex_wait_finish,
	TP_PROTO(struct rt_mutex *lock),
	TP_ARGS(lock));

DECLARE_HOOK(android_vh_rwsem_read_wait_start,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_read_wait_finish,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_write_wait_start,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_write_wait_finish,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_opt_spin_start,
	TP_PROTO(struct rw_semaphore *sem, bool *time_out, int *cnt, bool chk_only),
	TP_ARGS(sem, time_out, cnt, chk_only));
DECLARE_HOOK(android_vh_rwsem_opt_spin_finish,
	TP_PROTO(struct rw_semaphore *sem, bool taken, bool wlock),
	TP_ARGS(sem, taken, wlock));
DECLARE_HOOK(android_vh_rwsem_can_spin_on_owner,
	TP_PROTO(struct rw_semaphore *sem, bool *ret, bool wlock),
	TP_ARGS(sem, ret, wlock));

DECLARE_HOOK(android_vh_sched_show_task,
	TP_PROTO(struct task_struct *task),
	TP_ARGS(task));
DECLARE_HOOK(android_vh_alter_mutex_list_add,
	TP_PROTO(struct mutex *lock,
		struct mutex_waiter *waiter,
		struct list_head *list,
		bool *already_on_list),
	TP_ARGS(lock, waiter, list, already_on_list));
DECLARE_HOOK(android_vh_mutex_unlock_slowpath,
	TP_PROTO(struct mutex *lock),
	TP_ARGS(lock));
DECLARE_HOOK(android_vh_mutex_unlock_slowpath_end,
	TP_PROTO(struct mutex *lock, struct task_struct *next),
	TP_ARGS(lock, next));
DECLARE_HOOK(android_vh_record_mutex_lock_starttime,
	TP_PROTO(struct task_struct *tsk, unsigned long settime_jiffies),
	TP_ARGS(tsk, settime_jiffies));
DECLARE_HOOK(android_vh_record_rtmutex_lock_starttime,
	TP_PROTO(struct task_struct *tsk, unsigned long settime_jiffies),
	TP_ARGS(tsk, settime_jiffies));
DECLARE_HOOK(android_vh_record_rwsem_lock_starttime,
	TP_PROTO(struct task_struct *tsk, unsigned long settime_jiffies),
	TP_ARGS(tsk, settime_jiffies));
DECLARE_HOOK(android_vh_record_pcpu_rwsem_starttime,
	TP_PROTO(struct task_struct *tsk, unsigned long settime_jiffies),
	TP_ARGS(tsk, settime_jiffies));

struct percpu_rw_semaphore;
DECLARE_HOOK(android_vh_percpu_rwsem_wq_add,
	TP_PROTO(struct percpu_rw_semaphore *sem, bool reader),
	TP_ARGS(sem, reader));


/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_DTASK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
