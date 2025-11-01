// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rv.h>
#include <linux/sched/deadline.h>
#include <linux/sched/rt.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "sleep"

#include <trace/events/syscalls.h>
#include <trace/events/sched.h>
#include <trace/events/lock.h>
#include <uapi/linux/futex.h>
#include <rv_trace.h>
#include <monitors/rtapp/rtapp.h>

#include "sleep.h"
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
	ltl_atom_set(mon, LTL_SLEEP, false);
	ltl_atom_set(mon, LTL_WAKE, false);
	ltl_atom_set(mon, LTL_ABORT_SLEEP, false);
	ltl_atom_set(mon, LTL_WOKEN_BY_HARDIRQ, false);
	ltl_atom_set(mon, LTL_WOKEN_BY_NMI, false);
	ltl_atom_set(mon, LTL_WOKEN_BY_EQUAL_OR_HIGHER_PRIO, false);

	if (task_creation) {
		ltl_atom_set(mon, LTL_KTHREAD_SHOULD_STOP, false);
		ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_MONOTONIC, false);
		ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_TAI, false);
		ltl_atom_set(mon, LTL_NANOSLEEP_TIMER_ABSTIME, false);
		ltl_atom_set(mon, LTL_CLOCK_NANOSLEEP, false);
		ltl_atom_set(mon, LTL_FUTEX_WAIT, false);
		ltl_atom_set(mon, LTL_FUTEX_LOCK_PI, false);
		ltl_atom_set(mon, LTL_BLOCK_ON_RT_MUTEX, false);
	}

	if (task->flags & PF_KTHREAD) {
		ltl_atom_set(mon, LTL_KERNEL_THREAD, true);

		/* kernel tasks do not do syscall */
		ltl_atom_set(mon, LTL_FUTEX_WAIT, false);
		ltl_atom_set(mon, LTL_FUTEX_LOCK_PI, false);
		ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_MONOTONIC, false);
		ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_TAI, false);
		ltl_atom_set(mon, LTL_NANOSLEEP_TIMER_ABSTIME, false);
		ltl_atom_set(mon, LTL_CLOCK_NANOSLEEP, false);

		if (strstarts(task->comm, "migration/"))
			ltl_atom_set(mon, LTL_TASK_IS_MIGRATION, true);
		else
			ltl_atom_set(mon, LTL_TASK_IS_MIGRATION, false);

		if (strstarts(task->comm, "rcu"))
			ltl_atom_set(mon, LTL_TASK_IS_RCU, true);
		else
			ltl_atom_set(mon, LTL_TASK_IS_RCU, false);
	} else {
		ltl_atom_set(mon, LTL_KTHREAD_SHOULD_STOP, false);
		ltl_atom_set(mon, LTL_KERNEL_THREAD, false);
		ltl_atom_set(mon, LTL_TASK_IS_RCU, false);
		ltl_atom_set(mon, LTL_TASK_IS_MIGRATION, false);
	}

}

static void handle_sched_set_state(void *data, struct task_struct *task, int state)
{
	if (state & TASK_INTERRUPTIBLE)
		ltl_atom_pulse(task, LTL_SLEEP, true);
	else if (state == TASK_RUNNING)
		ltl_atom_pulse(task, LTL_ABORT_SLEEP, true);
}

static void handle_sched_wakeup(void *data, struct task_struct *task)
{
	ltl_atom_pulse(task, LTL_WAKE, true);
}

static void handle_sched_waking(void *data, struct task_struct *task)
{
	if (this_cpu_read(hardirq_context)) {
		ltl_atom_pulse(task, LTL_WOKEN_BY_HARDIRQ, true);
	} else if (in_task()) {
		if (current->prio <= task->prio)
			ltl_atom_pulse(task, LTL_WOKEN_BY_EQUAL_OR_HIGHER_PRIO, true);
	} else if (in_nmi()) {
		ltl_atom_pulse(task, LTL_WOKEN_BY_NMI, true);
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
	struct ltl_monitor *mon;
	unsigned long args[6];
	int op, cmd;

	mon = ltl_get_monitor(current);

	switch (id) {
#ifdef __NR_clock_nanosleep
	case __NR_clock_nanosleep:
#endif
#ifdef __NR_clock_nanosleep_time64
	case __NR_clock_nanosleep_time64:
#endif
		syscall_get_arguments(current, regs, args);
		ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_MONOTONIC, args[0] == CLOCK_MONOTONIC);
		ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_TAI, args[0] == CLOCK_TAI);
		ltl_atom_set(mon, LTL_NANOSLEEP_TIMER_ABSTIME, args[1] == TIMER_ABSTIME);
		ltl_atom_update(current, LTL_CLOCK_NANOSLEEP, true);
		break;

#ifdef __NR_futex
	case __NR_futex:
#endif
#ifdef __NR_futex_time64
	case __NR_futex_time64:
#endif
		syscall_get_arguments(current, regs, args);
		op = args[1];
		cmd = op & FUTEX_CMD_MASK;

		switch (cmd) {
		case FUTEX_LOCK_PI:
		case FUTEX_LOCK_PI2:
			ltl_atom_update(current, LTL_FUTEX_LOCK_PI, true);
			break;
		case FUTEX_WAIT:
		case FUTEX_WAIT_BITSET:
		case FUTEX_WAIT_REQUEUE_PI:
			ltl_atom_update(current, LTL_FUTEX_WAIT, true);
			break;
		}
		break;
	}
}

static void handle_sys_exit(void *data, struct pt_regs *regs, long ret)
{
	struct ltl_monitor *mon = ltl_get_monitor(current);

	ltl_atom_set(mon, LTL_FUTEX_LOCK_PI, false);
	ltl_atom_set(mon, LTL_FUTEX_WAIT, false);
	ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_MONOTONIC, false);
	ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_TAI, false);
	ltl_atom_set(mon, LTL_NANOSLEEP_TIMER_ABSTIME, false);
	ltl_atom_update(current, LTL_CLOCK_NANOSLEEP, false);
}

static void handle_kthread_stop(void *data, struct task_struct *task)
{
	/* FIXME: this could race with other tracepoint handlers */
	ltl_atom_update(task, LTL_KTHREAD_SHOULD_STOP, true);
}

static int enable_sleep(void)
{
	int retval;

	retval = ltl_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("rtapp_sleep", sched_waking, handle_sched_waking);
	rv_attach_trace_probe("rtapp_sleep", sched_wakeup, handle_sched_wakeup);
	rv_attach_trace_probe("rtapp_sleep", sched_set_state_tp, handle_sched_set_state);
	rv_attach_trace_probe("rtapp_sleep", contention_begin, handle_contention_begin);
	rv_attach_trace_probe("rtapp_sleep", contention_end, handle_contention_end);
	rv_attach_trace_probe("rtapp_sleep", sched_kthread_stop, handle_kthread_stop);
	rv_attach_trace_probe("rtapp_sleep", sys_enter, handle_sys_enter);
	rv_attach_trace_probe("rtapp_sleep", sys_exit, handle_sys_exit);
	return 0;
}

static void disable_sleep(void)
{
	rv_detach_trace_probe("rtapp_sleep", sched_waking, handle_sched_waking);
	rv_detach_trace_probe("rtapp_sleep", sched_wakeup, handle_sched_wakeup);
	rv_detach_trace_probe("rtapp_sleep", sched_set_state_tp, handle_sched_set_state);
	rv_detach_trace_probe("rtapp_sleep", contention_begin, handle_contention_begin);
	rv_detach_trace_probe("rtapp_sleep", contention_end, handle_contention_end);
	rv_detach_trace_probe("rtapp_sleep", sched_kthread_stop, handle_kthread_stop);
	rv_detach_trace_probe("rtapp_sleep", sys_enter, handle_sys_enter);
	rv_detach_trace_probe("rtapp_sleep", sys_exit, handle_sys_exit);

	ltl_monitor_destroy();
}

static struct rv_monitor rv_sleep = {
	.name = "sleep",
	.description = "Monitor that RT tasks do not undesirably sleep",
	.enable = enable_sleep,
	.disable = disable_sleep,
};

static int __init register_sleep(void)
{
	return rv_register_monitor(&rv_sleep, &rv_rtapp);
}

static void __exit unregister_sleep(void)
{
	rv_unregister_monitor(&rv_sleep);
}

module_init(register_sleep);
module_exit(unregister_sleep);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nam Cao <namcao@linutronix.de>");
MODULE_DESCRIPTION("sleep: Monitor that RT tasks do not undesirably sleep");
