// SPDX-License-Identifier: GPL-2.0-only
/*
 * Detect Hung Task
 *
 * kernel/hung_task.c - kernel thread for detecting tasks stuck in D state
 *
 */

#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/lockdep.h>
#include <linux/export.h>
#include <linux/panic_notifier.h>
#include <linux/sysctl.h>
#include <linux/suspend.h>
#include <linux/utsname.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/sysctl.h>

#include <trace/events/sched.h>
#undef CREATE_TRACE_POINTS
#include <trace/hooks/hung_task.h>

/*
 * The number of tasks checked:
 */
int __read_mostly sysctl_hung_task_check_count = PID_MAX_LIMIT;

/*
 * Limit number of tasks checked in a batch.
 *
 * This value controls the preemptibility of khungtaskd since preemption
 * is disabled during the critical section. It also controls the size of
 * the RCU grace period. So it needs to be upper-bound.
 */
#define HUNG_TASK_LOCK_BREAK (HZ / 10)

/*
 * Zero means infinite timeout - no checking done:
 */
unsigned long __read_mostly sysctl_hung_task_timeout_secs = CONFIG_DEFAULT_HUNG_TASK_TIMEOUT;

/*
 * Zero (default value) means use sysctl_hung_task_timeout_secs:
 */
unsigned long __read_mostly sysctl_hung_task_check_interval_secs;

int __read_mostly sysctl_hung_task_warnings = 10;

static int __read_mostly did_panic;
static bool hung_task_show_lock;
static bool hung_task_call_panic;
static bool hung_task_show_all_bt;

static struct task_struct *watchdog_task;

#ifdef CONFIG_SMP
/*
 * Should we dump all CPUs backtraces in a hung task event?
 * Defaults to 0, can be changed via sysctl.
 */
static unsigned int __read_mostly sysctl_hung_task_all_cpu_backtrace;
#else
#define sysctl_hung_task_all_cpu_backtrace 0
#endif /* CONFIG_SMP */

/*
 * Should we panic (and reboot, if panic_timeout= is set) when a
 * hung task is detected:
 */
unsigned int __read_mostly sysctl_hung_task_panic =
				IS_ENABLED(CONFIG_BOOTPARAM_HUNG_TASK_PANIC);

static int
hung_task_panic(struct notifier_block *this, unsigned long event, void *ptr)
{
	did_panic = 1;

	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call = hung_task_panic,
};

static void check_hung_task(struct task_struct *t, unsigned long timeout)
{
	unsigned long switch_count = t->nvcsw + t->nivcsw;

	/*
	 * Ensure the task is not frozen.
	 * Also, skip vfork and any other user process that freezer should skip.
	 */
	if (unlikely(READ_ONCE(t->__state) & TASK_FROZEN))
		return;

	/*
	 * When a freshly created task is scheduled once, changes its state to
	 * TASK_UNINTERRUPTIBLE without having ever been switched out once, it
	 * musn't be checked.
	 */
	if (unlikely(!switch_count))
		return;

	if (switch_count != t->last_switch_count) {
		t->last_switch_count = switch_count;
		t->last_switch_time = jiffies;
		return;
	}
	if (time_is_after_jiffies(t->last_switch_time + timeout * HZ))
		return;

	trace_sched_process_hang(t);

	if (sysctl_hung_task_panic) {
		console_verbose();
		hung_task_show_lock = true;
		hung_task_call_panic = true;
	}

	/*
	 * Ok, the task did not get scheduled for more than 2 minutes,
	 * complain:
	 */
	if (sysctl_hung_task_warnings) {
		if (sysctl_hung_task_warnings > 0)
			sysctl_hung_task_warnings--;
		pr_err("INFO: task %s:%d blocked for more than %ld seconds.\n",
		       t->comm, t->pid, (jiffies - t->last_switch_time) / HZ);
		pr_err("      %s %s %.*s\n",
			print_tainted(), init_utsname()->release,
			(int)strcspn(init_utsname()->version, " "),
			init_utsname()->version);
		pr_err("\"echo 0 > /proc/sys/kernel/hung_task_timeout_secs\""
			" disables this message.\n");
		sched_show_task(t);
		hung_task_show_lock = true;

		if (sysctl_hung_task_all_cpu_backtrace)
			hung_task_show_all_bt = true;
	}

	touch_nmi_watchdog();
}

/*
 * To avoid extending the RCU grace period for an unbounded amount of time,
 * periodically exit the critical section and enter a new one.
 *
 * For preemptible RCU it is sufficient to call rcu_read_unlock in order
 * to exit the grace period. For classic RCU, a reschedule is required.
 */
static bool rcu_lock_break(struct task_struct *g, struct task_struct *t)
{
	bool can_cont;

	get_task_struct(g);
	get_task_struct(t);
	rcu_read_unlock();
	cond_resched();
	rcu_read_lock();
	can_cont = pid_alive(g) && pid_alive(t);
	put_task_struct(t);
	put_task_struct(g);

	return can_cont;
}

/*
 * Check whether a TASK_UNINTERRUPTIBLE does not get woken up for
 * a really long time (120 seconds). If that happens, print out
 * a warning.
 */
static void check_hung_uninterruptible_tasks(unsigned long timeout)
{
	int max_count = sysctl_hung_task_check_count;
	unsigned long last_break = jiffies;
	struct task_struct *g, *t;
	bool need_check = true;

	/*
	 * If the system crashed already then all bets are off,
	 * do not report extra hung tasks:
	 */
	if (test_taint(TAINT_DIE) || did_panic)
		return;

	hung_task_show_lock = false;
	rcu_read_lock();
	for_each_process_thread(g, t) {
		unsigned int state;

		if (!max_count--)
			goto unlock;
		if (time_after(jiffies, last_break + HUNG_TASK_LOCK_BREAK)) {
			if (!rcu_lock_break(g, t))
				goto unlock;
			last_break = jiffies;
		}
		/*
		 * skip the TASK_KILLABLE tasks -- these can be killed
		 * skip the TASK_IDLE tasks -- those are genuinely idle
		 */
		trace_android_vh_check_uninterrupt_tasks(t, timeout, &need_check);
		if (need_check) {
			state = READ_ONCE(t->__state);
			if ((state & TASK_UNINTERRUPTIBLE) &&
			    !(state & TASK_WAKEKILL) &&
			    !(state & TASK_NOLOAD))
				check_hung_task(t, timeout);
		}
	}
	trace_android_vh_check_uninterrupt_tasks_done(NULL);
 unlock:
	rcu_read_unlock();
	if (hung_task_show_lock)
		debug_show_all_locks();

	if (hung_task_show_all_bt) {
		hung_task_show_all_bt = false;
		trigger_all_cpu_backtrace();
	}

	if (hung_task_call_panic)
		panic("hung_task: blocked tasks");
}

static long hung_timeout_jiffies(unsigned long last_checked,
				 unsigned long timeout)
{
	/* timeout of 0 will disable the watchdog */
	return timeout ? last_checked - jiffies + timeout * HZ :
		MAX_SCHEDULE_TIMEOUT;
}

#ifdef CONFIG_SYSCTL
/*
 * Process updating of timeout sysctl
 */
static int proc_dohung_task_timeout_secs(struct ctl_table *table, int write,
				  void *buffer,
				  size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_doulongvec_minmax(table, write, buffer, lenp, ppos);

	if (ret || !write)
		goto out;

	wake_up_process(watchdog_task);

 out:
	return ret;
}

/*
 * This is needed for proc_doulongvec_minmax of sysctl_hung_task_timeout_secs
 * and hung_task_check_interval_secs
 */
static const unsigned long hung_task_timeout_max = (LONG_MAX / HZ);
static struct ctl_table hung_task_sysctls[] = {
#ifdef CONFIG_SMP
	{
		.procname	= "hung_task_all_cpu_backtrace",
		.data		= &sysctl_hung_task_all_cpu_backtrace,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
#endif /* CONFIG_SMP */
	{
		.procname	= "hung_task_panic",
		.data		= &sysctl_hung_task_panic,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "hung_task_check_count",
		.data		= &sysctl_hung_task_check_count,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "hung_task_timeout_secs",
		.data		= &sysctl_hung_task_timeout_secs,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_dohung_task_timeout_secs,
		.extra2		= (void *)&hung_task_timeout_max,
	},
	{
		.procname	= "hung_task_check_interval_secs",
		.data		= &sysctl_hung_task_check_interval_secs,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_dohung_task_timeout_secs,
		.extra2		= (void *)&hung_task_timeout_max,
	},
	{
		.procname	= "hung_task_warnings",
		.data		= &sysctl_hung_task_warnings,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_NEG_ONE,
	},
	{}
};

static void __init hung_task_sysctl_init(void)
{
	register_sysctl_init("kernel", hung_task_sysctls);
}
#else
#define hung_task_sysctl_init() do { } while (0)
#endif /* CONFIG_SYSCTL */


static atomic_t reset_hung_task = ATOMIC_INIT(0);

void reset_hung_task_detector(void)
{
	atomic_set(&reset_hung_task, 1);
}
EXPORT_SYMBOL_GPL(reset_hung_task_detector);

static bool hung_detector_suspended;

static int hungtask_pm_notify(struct notifier_block *self,
			      unsigned long action, void *hcpu)
{
	switch (action) {
	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
		hung_detector_suspended = true;
		break;
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		hung_detector_suspended = false;
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

/*
 * kthread which checks for tasks stuck in D state
 */
static int watchdog(void *dummy)
{
	unsigned long hung_last_checked = jiffies;

	set_user_nice(current, 0);

	for ( ; ; ) {
		unsigned long timeout = sysctl_hung_task_timeout_secs;
		unsigned long interval = sysctl_hung_task_check_interval_secs;
		long t;

		if (interval == 0)
			interval = timeout;
		interval = min_t(unsigned long, interval, timeout);
		t = hung_timeout_jiffies(hung_last_checked, interval);
		if (t <= 0) {
			if (!atomic_xchg(&reset_hung_task, 0) &&
			    !hung_detector_suspended)
				check_hung_uninterruptible_tasks(timeout);
			hung_last_checked = jiffies;
			continue;
		}
		schedule_timeout_interruptible(t);
	}

	return 0;
}

static int __init hung_task_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);

	/* Disable hung task detector on suspend */
	pm_notifier(hungtask_pm_notify, 0);

	watchdog_task = kthread_run(watchdog, NULL, "khungtaskd");
	hung_task_sysctl_init();

	return 0;
}
subsys_initcall(hung_task_init);
