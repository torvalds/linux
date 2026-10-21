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
	ltl_atom_set(mon, LTL_SCHEDULE_IN, false);
	ltl_atom_set(mon, LTL_ABORT_SLEEP, false);
	ltl_atom_set(mon, LTL_WOKEN_BY_HARDIRQ, false);
	ltl_atom_set(mon, LTL_WOKEN_BY_NMI, false);
	ltl_atom_set(mon, LTL_WOKEN_BY_EQUAL_OR_HIGHER_PRIO, false);

	if (task_creation) {
		ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_REALTIME, false);
		ltl_atom_set(mon, LTL_NANOSLEEP_TIMER_ABSTIME, false);
		ltl_atom_set(mon, LTL_CLOCK_NANOSLEEP, false);
		ltl_atom_set(mon, LTL_FUTEX_WAIT, false);
		ltl_atom_set(mon, LTL_EPOLL_WAIT, false);
		ltl_atom_set(mon, LTL_FUTEX_LOCK_PI, false);
		ltl_atom_set(mon, LTL_BLOCK_ON_RT_MUTEX, false);
	}

	ltl_atom_set(mon, LTL_USER_THREAD, !(task->flags & PF_KTHREAD));
}

static void handle_sched_set_state(void *data, struct task_struct *task, int state)
{
	if (state & TASK_INTERRUPTIBLE)
		ltl_atom_pulse(task, LTL_SLEEP, true);
	else if (state == TASK_RUNNING)
		ltl_atom_pulse(task, LTL_ABORT_SLEEP, true);
}

static void handle_sched_exit(void *data, bool is_switch)
{
	ltl_atom_pulse(rv_get_current(), LTL_SCHEDULE_IN, true);
}

static void handle_sched_waking(void *data, struct task_struct *task)
{
	if (in_hardirq()) {
		ltl_atom_pulse(task, LTL_WOKEN_BY_HARDIRQ, true);
	} else if (in_task()) {
		if (rv_get_current()->prio <= task->prio)
			ltl_atom_pulse(task, LTL_WOKEN_BY_EQUAL_OR_HIGHER_PRIO, true);
	} else if (in_nmi()) {
		ltl_atom_pulse(task, LTL_WOKEN_BY_NMI, true);
	}
}

static void handle_contention_begin(void *data, void *lock, unsigned int flags)
{
	if (flags & LCB_F_RT)
		ltl_atom_update(rv_get_current(), LTL_BLOCK_ON_RT_MUTEX, true);
}

static void handle_contention_end(void *data, void *lock, int ret)
{
	ltl_atom_update(rv_get_current(), LTL_BLOCK_ON_RT_MUTEX, false);
}

static void handle_sys_enter(void *data, struct pt_regs *regs, long id)
{
	struct ltl_monitor *mon;
	unsigned long args[6];
	int op, cmd;

	mon = ltl_get_monitor(rv_get_current());

	switch (id) {
#ifdef __NR_clock_nanosleep
	case __NR_clock_nanosleep:
#endif
#ifdef __NR_clock_nanosleep_time64
	case __NR_clock_nanosleep_time64:
#endif
		syscall_get_arguments(rv_get_current(), regs, args);
		ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_REALTIME, args[0] == CLOCK_REALTIME);
		ltl_atom_set(mon, LTL_NANOSLEEP_TIMER_ABSTIME, args[1] == TIMER_ABSTIME);
		ltl_atom_update(rv_get_current(), LTL_CLOCK_NANOSLEEP, true);
		break;

#ifdef __NR_futex
	case __NR_futex:
#endif
#ifdef __NR_futex_time64
	case __NR_futex_time64:
#endif
		syscall_get_arguments(rv_get_current(), regs, args);
		op = args[1];
		cmd = op & FUTEX_CMD_MASK;

		switch (cmd) {
		case FUTEX_LOCK_PI:
		case FUTEX_LOCK_PI2:
			ltl_atom_update(rv_get_current(), LTL_FUTEX_LOCK_PI, true);
			break;
		case FUTEX_WAIT:
		case FUTEX_WAIT_BITSET:
		case FUTEX_WAIT_REQUEUE_PI:
			ltl_atom_update(rv_get_current(), LTL_FUTEX_WAIT, true);
			break;
		}
		break;
#ifdef __NR_epoll_wait
	case __NR_epoll_wait:
		ltl_atom_update(rv_get_current(), LTL_EPOLL_WAIT, true);
		break;
#endif
	}
}

static void handle_sys_exit(void *data, struct pt_regs *regs, long ret)
{
	struct ltl_monitor *mon = ltl_get_monitor(rv_get_current());

	ltl_atom_set(mon, LTL_FUTEX_LOCK_PI, false);
	ltl_atom_set(mon, LTL_FUTEX_WAIT, false);
	ltl_atom_set(mon, LTL_NANOSLEEP_CLOCK_REALTIME, false);
	ltl_atom_set(mon, LTL_NANOSLEEP_TIMER_ABSTIME, false);
	ltl_atom_set(mon, LTL_EPOLL_WAIT, false);
	ltl_atom_update(rv_get_current(), LTL_CLOCK_NANOSLEEP, false);
}

static int enable_sleep(void)
{
	int retval;

	retval = ltl_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("rtapp_sleep", sched_waking, handle_sched_waking);
	rv_attach_trace_probe("rtapp_sleep", sched_exit_tp, handle_sched_exit);
	rv_attach_trace_probe("rtapp_sleep", sched_set_state_tp, handle_sched_set_state);
	rv_attach_trace_probe("rtapp_sleep", contention_begin, handle_contention_begin);
	rv_attach_trace_probe("rtapp_sleep", contention_end, handle_contention_end);
	rv_attach_trace_probe("rtapp_sleep", sys_enter, handle_sys_enter);
	rv_attach_trace_probe("rtapp_sleep", sys_exit, handle_sys_exit);
	return 0;
}

static void disable_sleep(void)
{
	rv_detach_trace_probe("rtapp_sleep", sched_waking, handle_sched_waking);
	rv_detach_trace_probe("rtapp_sleep", sched_exit_tp, handle_sched_exit);
	rv_detach_trace_probe("rtapp_sleep", sched_set_state_tp, handle_sched_set_state);
	rv_detach_trace_probe("rtapp_sleep", contention_begin, handle_contention_begin);
	rv_detach_trace_probe("rtapp_sleep", contention_end, handle_contention_end);
	rv_detach_trace_probe("rtapp_sleep", sys_enter, handle_sys_enter);
	rv_detach_trace_probe("rtapp_sleep", sys_exit, handle_sys_exit);

	ltl_monitor_destroy();
}

static struct rv_monitor rv_this = {
	.name = "sleep",
	.description = "Monitor that RT tasks do not undesirably sleep",
	.enable = enable_sleep,
	.disable = disable_sleep,
};

static int __init register_sleep(void)
{
	return rv_register_monitor(&rv_this, &rv_rtapp);
}

static void __exit unregister_sleep(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_sleep);
module_exit(unregister_sleep);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nam Cao <namcao@linutronix.de>");
MODULE_DESCRIPTION("sleep: Monitor that RT tasks do not undesirably sleep");

#if IS_ENABLED(CONFIG_RV_MONITORS_KUNIT_TEST)
#include <kunit/visibility.h>
#include "sleep_kunit.h"

const struct rv_sleep_ops rv_sleep_ops = {
	.mon = RV_MON_OPS_INIT(),
	.handle_sched_waking = handle_sched_waking,
	.handle_sched_exit = handle_sched_exit,
	.handle_sched_set_state = handle_sched_set_state,
	.handle_contention_begin = handle_contention_begin,
	.handle_contention_end = handle_contention_end,
	.handle_sys_enter = handle_sys_enter,
	.handle_sys_exit = handle_sys_exit,
	.handle_task_newtask = handle_task_newtask,
};
EXPORT_SYMBOL_IF_KUNIT(rv_sleep_ops);
#endif
