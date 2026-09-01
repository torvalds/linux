// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "wakeup"

#include <trace/events/syscalls.h>
#include <trace/events/sched.h>
#include <trace/events/lock.h>
#include <uapi/linux/futex.h>

#include <rv_trace.h>
#include <monitors/rtapp/rtapp.h>


#ifndef __NR_futex
#define __NR_futex (-__COUNTER__)
#endif
#ifndef __NR_futex_time64
#define __NR_futex_time64 (-__COUNTER__)
#endif

#include "wakeup.h"
#include <rv/ltl_monitor.h>

static void ltl_atoms_fetch(struct task_struct *task, struct ltl_monitor *mon)
{
	/*
	 * This includes "actual" real-time tasks and also PI-boosted
	 * tasks. A task being PI-boosted means it is blocking an "actual"
	 * real-task, therefore it should also obey the monitor's rule,
	 * otherwise the "actual" real-task may be delayed.
	 */
	ltl_atom_set(mon, LTL_RT, rt_or_dl_task(task));
}

static void ltl_atoms_init(struct task_struct *task, struct ltl_monitor *mon, bool task_creation)
{
	ltl_atom_set(mon, LTL_WOKEN_BY_LOWER_PRIO, false);
	ltl_atom_set(mon, LTL_WOKEN_BY_SOFTIRQ, false);

	if (task_creation) {
		ltl_atom_set(mon, LTL_BLOCK_ON_RT_MUTEX, false);
		ltl_atom_set(mon, LTL_FUTEX_LOCK_PI, false);
	}

	ltl_atom_set(mon, LTL_USER_THREAD, !(task->flags & PF_KTHREAD));
}

static void handle_sched_waking(void *data, struct task_struct *task)
{
	if (in_task()) {
		if (current->prio > task->prio)
			ltl_atom_pulse(task, LTL_WOKEN_BY_LOWER_PRIO, true);
	} else if (in_serving_softirq()) {
		ltl_atom_pulse(task, LTL_WOKEN_BY_SOFTIRQ, true);
	}
}

static void handle_contention_begin(void *data, void *lock, unsigned int flags)
{
	if (flags & LCB_F_RT)
		ltl_atom_update(current, LTL_BLOCK_ON_RT_MUTEX, true);
}

static void handle_contention_end(void *data, void *lock, int ret)
{
	ltl_atom_update(current, LTL_BLOCK_ON_RT_MUTEX, false);
}

static void handle_sys_enter(void *data, struct pt_regs *regs, long id)
{
	unsigned long args[6];
	int op, cmd;

	switch (id) {
	case __NR_futex:
	case __NR_futex_time64:
		syscall_get_arguments(current, regs, args);
		op = args[1];
		cmd = op & FUTEX_CMD_MASK;

		switch (cmd) {
		case FUTEX_LOCK_PI:
		case FUTEX_LOCK_PI2:
			ltl_atom_update(current, LTL_FUTEX_LOCK_PI, true);
			break;
		}
		break;
	}
}

static void handle_sys_exit(void *data, struct pt_regs *regs, long ret)
{
	ltl_atom_update(current, LTL_FUTEX_LOCK_PI, false);
}

static int enable_wakeup(void)
{
	int retval;

	retval = ltl_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("rtapp_wakeup", sched_waking, handle_sched_waking);
	rv_attach_trace_probe("rtapp_wakeup", contention_begin, handle_contention_begin);
	rv_attach_trace_probe("rtapp_wakeup", contention_end, handle_contention_end);
	rv_attach_trace_probe("rtapp_wakeup", sys_enter, handle_sys_enter);
	rv_attach_trace_probe("rtapp_wakeup", sys_exit, handle_sys_exit);

	return 0;
}

static void disable_wakeup(void)
{
	rv_detach_trace_probe("rtapp_wakeup", sched_waking, handle_sched_waking);
	rv_detach_trace_probe("rtapp_wakeup", contention_begin, handle_contention_begin);
	rv_detach_trace_probe("rtapp_wakeup", contention_end, handle_contention_end);
	rv_detach_trace_probe("rtapp_wakeup", sys_enter, handle_sys_enter);
	rv_detach_trace_probe("rtapp_wakeup", sys_exit, handle_sys_exit);

	ltl_monitor_destroy();
}

static struct rv_monitor rv_wakeup = {
	.name = "wakeup",
	.description = "Monitor that real-time tasks are not woken by lower-priority tasks",
	.enable = enable_wakeup,
	.disable = disable_wakeup,
};

static int __init register_wakeup(void)
{
	return rv_register_monitor(&rv_wakeup, &rv_rtapp);
}

static void __exit unregister_wakeup(void)
{
	rv_unregister_monitor(&rv_wakeup);
}

module_init(register_wakeup);
module_exit(unregister_wakeup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nam Cao <namcao@linutronix.de>");
MODULE_DESCRIPTION("Monitor that real-time tasks are not woken by lower-priority tasks");
