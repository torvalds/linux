// SPDX-License-Identifier: GPL-2.0
/*
 * Detect hard and soft lockups on a system
 *
 * started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 * Note: Most of this code is borrowed heavily from the original softlockup
 * detector, so thanks to Ingo for the initial implementation.
 * Some chunks also taken from the old x86-specific nmi watchdog code, thanks
 * to those contributors as well.
 */

#define pr_fmt(fmt) "watchdog: " fmt

#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/tick.h>
#include <linux/sched/clock.h>
#include <linux/sched/debug.h>
#include <linux/sched/isolation.h>
#include <linux/stop_machine.h>

#include <asm/irq_regs.h>
#include <linux/kvm_para.h>

static DEFINE_MUTEX(watchdog_mutex);

#if defined(CONFIG_HARDLOCKUP_DETECTOR) || defined(CONFIG_HAVE_NMI_WATCHDOG)
# define WATCHDOG_DEFAULT	(SOFT_WATCHDOG_ENABLED | NMI_WATCHDOG_ENABLED)
# define NMI_WATCHDOG_DEFAULT	1
#else
# define WATCHDOG_DEFAULT	(SOFT_WATCHDOG_ENABLED)
# define NMI_WATCHDOG_DEFAULT	0
#endif

unsigned long __read_mostly watchdog_enabled;
int __read_mostly watchdog_user_enabled = 1;
int __read_mostly nmi_watchdog_user_enabled = NMI_WATCHDOG_DEFAULT;
int __read_mostly soft_watchdog_user_enabled = 1;
int __read_mostly watchdog_thresh = 10;
static int __read_mostly nmi_watchdog_available;

static struct cpumask watchdog_allowed_mask __read_mostly;

struct cpumask watchdog_cpumask __read_mostly;
unsigned long *watchdog_cpumask_bits = cpumask_bits(&watchdog_cpumask);

#ifdef CONFIG_HARDLOCKUP_DETECTOR
/*
 * Should we panic when a soft-lockup or hard-lockup occurs:
 */
unsigned int __read_mostly hardlockup_panic =
			CONFIG_BOOTPARAM_HARDLOCKUP_PANIC_VALUE;
/*
 * We may not want to enable hard lockup detection by default in all cases,
 * for example when running the kernel as a guest on a hypervisor. In these
 * cases this function can be called to disable hard lockup detection. This
 * function should only be executed once by the boot processor before the
 * kernel command line parameters are parsed, because otherwise it is not
 * possible to override this in hardlockup_panic_setup().
 */
void __init hardlockup_detector_disable(void)
{
	nmi_watchdog_user_enabled = 0;
}

static int __init hardlockup_panic_setup(char *str)
{
	if (!strncmp(str, "panic", 5))
		hardlockup_panic = 1;
	else if (!strncmp(str, "nopanic", 7))
		hardlockup_panic = 0;
	else if (!strncmp(str, "0", 1))
		nmi_watchdog_user_enabled = 0;
	else if (!strncmp(str, "1", 1))
		nmi_watchdog_user_enabled = 1;
	return 1;
}
__setup("nmi_watchdog=", hardlockup_panic_setup);

# ifdef CONFIG_SMP
int __read_mostly sysctl_hardlockup_all_cpu_backtrace;

static int __init hardlockup_all_cpu_backtrace_setup(char *str)
{
	sysctl_hardlockup_all_cpu_backtrace = !!simple_strtol(str, NULL, 0);
	return 1;
}
__setup("hardlockup_all_cpu_backtrace=", hardlockup_all_cpu_backtrace_setup);
# endif /* CONFIG_SMP */
#endif /* CONFIG_HARDLOCKUP_DETECTOR */

/*
 * These functions can be overridden if an architecture implements its
 * own hardlockup detector.
 *
 * watchdog_nmi_enable/disable can be implemented to start and stop when
 * softlockup watchdog threads start and stop. The arch must select the
 * SOFTLOCKUP_DETECTOR Kconfig.
 */
int __weak watchdog_nmi_enable(unsigned int cpu)
{
	hardlockup_detector_perf_enable();
	return 0;
}

void __weak watchdog_nmi_disable(unsigned int cpu)
{
	hardlockup_detector_perf_disable();
}

/* Return 0, if a NMI watchdog is available. Error code otherwise */
int __weak __init watchdog_nmi_probe(void)
{
	return hardlockup_detector_perf_init();
}

/**
 * watchdog_nmi_stop - Stop the watchdog for reconfiguration
 *
 * The reconfiguration steps are:
 * watchdog_nmi_stop();
 * update_variables();
 * watchdog_nmi_start();
 */
void __weak watchdog_nmi_stop(void) { }

/**
 * watchdog_nmi_start - Start the watchdog after reconfiguration
 *
 * Counterpart to watchdog_nmi_stop().
 *
 * The following variables have been updated in update_variables() and
 * contain the currently valid configuration:
 * - watchdog_enabled
 * - watchdog_thresh
 * - watchdog_cpumask
 */
void __weak watchdog_nmi_start(void) { }

/**
 * lockup_detector_update_enable - Update the sysctl enable bit
 *
 * Caller needs to make sure that the NMI/perf watchdogs are off, so this
 * can't race with watchdog_nmi_disable().
 */
static void lockup_detector_update_enable(void)
{
	watchdog_enabled = 0;
	if (!watchdog_user_enabled)
		return;
	if (nmi_watchdog_available && nmi_watchdog_user_enabled)
		watchdog_enabled |= NMI_WATCHDOG_ENABLED;
	if (soft_watchdog_user_enabled)
		watchdog_enabled |= SOFT_WATCHDOG_ENABLED;
}

#ifdef CONFIG_SOFTLOCKUP_DETECTOR

/* Global variables, exported for sysctl */
unsigned int __read_mostly softlockup_panic =
			CONFIG_BOOTPARAM_SOFTLOCKUP_PANIC_VALUE;

static bool softlockup_initialized __read_mostly;
static u64 __read_mostly sample_period;

static DEFINE_PER_CPU(unsigned long, watchdog_touch_ts);
static DEFINE_PER_CPU(struct hrtimer, watchdog_hrtimer);
static DEFINE_PER_CPU(bool, softlockup_touch_sync);
static DEFINE_PER_CPU(bool, soft_watchdog_warn);
static DEFINE_PER_CPU(unsigned long, hrtimer_interrupts);
static DEFINE_PER_CPU(unsigned long, soft_lockup_hrtimer_cnt);
static DEFINE_PER_CPU(struct task_struct *, softlockup_task_ptr_saved);
static DEFINE_PER_CPU(unsigned long, hrtimer_interrupts_saved);
static unsigned long soft_lockup_nmi_warn;

static int __init softlockup_panic_setup(char *str)
{
	softlockup_panic = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("softlockup_panic=", softlockup_panic_setup);

static int __init nowatchdog_setup(char *str)
{
	watchdog_user_enabled = 0;
	return 1;
}
__setup("nowatchdog", nowatchdog_setup);

static int __init nosoftlockup_setup(char *str)
{
	soft_watchdog_user_enabled = 0;
	return 1;
}
__setup("nosoftlockup", nosoftlockup_setup);

static int __init watchdog_thresh_setup(char *str)
{
	get_option(&str, &watchdog_thresh);
	return 1;
}
__setup("watchdog_thresh=", watchdog_thresh_setup);

#ifdef CONFIG_SMP
int __read_mostly sysctl_softlockup_all_cpu_backtrace;

static int __init softlockup_all_cpu_backtrace_setup(char *str)
{
	sysctl_softlockup_all_cpu_backtrace = !!simple_strtol(str, NULL, 0);
	return 1;
}
__setup("softlockup_all_cpu_backtrace=", softlockup_all_cpu_backtrace_setup);
#endif

static void __lockup_detector_cleanup(void);

/*
 * Hard-lockup warnings should be triggered after just a few seconds. Soft-
 * lockups can have false positives under extreme conditions. So we generally
 * want a higher threshold for soft lockups than for hard lockups. So we couple
 * the thresholds with a factor: we make the soft threshold twice the amount of
 * time the hard threshold is.
 */
static int get_softlockup_thresh(void)
{
	return watchdog_thresh * 2;
}

/*
 * Returns seconds, approximately.  We don't need nanosecond
 * resolution, and we don't need to waste time with a big divide when
 * 2^30ns == 1.074s.
 */
static unsigned long get_timestamp(void)
{
	return running_clock() >> 30LL;  /* 2^30 ~= 10^9 */
}

static void set_sample_period(void)
{
	/*
	 * convert watchdog_thresh from seconds to ns
	 * the divide by 5 is to give hrtimer several chances (two
	 * or three with the current relation between the soft
	 * and hard thresholds) to increment before the
	 * hardlockup detector generates a warning
	 */
	sample_period = get_softlockup_thresh() * ((u64)NSEC_PER_SEC / 5);
	watchdog_update_hrtimer_threshold(sample_period);
}

/* Commands for resetting the watchdog */
static void __touch_watchdog(void)
{
	__this_cpu_write(watchdog_touch_ts, get_timestamp());
}

/**
 * touch_softlockup_watchdog_sched - touch watchdog on scheduler stalls
 *
 * Call when the scheduler may have stalled for legitimate reasons
 * preventing the watchdog task from executing - e.g. the scheduler
 * entering idle state.  This should only be used for scheduler events.
 * Use touch_softlockup_watchdog() for everything else.
 */
notrace void touch_softlockup_watchdog_sched(void)
{
	/*
	 * Preemption can be enabled.  It doesn't matter which CPU's timestamp
	 * gets zeroed here, so use the raw_ operation.
	 */
	raw_cpu_write(watchdog_touch_ts, 0);
}

notrace void touch_softlockup_watchdog(void)
{
	touch_softlockup_watchdog_sched();
	wq_watchdog_touch(raw_smp_processor_id());
}
EXPORT_SYMBOL(touch_softlockup_watchdog);

void touch_all_softlockup_watchdogs(void)
{
	int cpu;

	/*
	 * watchdog_mutex cannpt be taken here, as this might be called
	 * from (soft)interrupt context, so the access to
	 * watchdog_allowed_cpumask might race with a concurrent update.
	 *
	 * The watchdog time stamp can race against a concurrent real
	 * update as well, the only side effect might be a cycle delay for
	 * the softlockup check.
	 */
	for_each_cpu(cpu, &watchdog_allowed_mask)
		per_cpu(watchdog_touch_ts, cpu) = 0;
	wq_watchdog_touch(-1);
}

void touch_softlockup_watchdog_sync(void)
{
	__this_cpu_write(softlockup_touch_sync, true);
	__this_cpu_write(watchdog_touch_ts, 0);
}

static int is_softlockup(unsigned long touch_ts)
{
	unsigned long now = get_timestamp();

	if ((watchdog_enabled & SOFT_WATCHDOG_ENABLED) && watchdog_thresh){
		/* Warn about unreasonable delays. */
		if (time_after(now, touch_ts + get_softlockup_thresh()))
			return now - touch_ts;
	}
	return 0;
}

/* watchdog detector functions */
bool is_hardlockup(void)
{
	unsigned long hrint = __this_cpu_read(hrtimer_interrupts);

	if (__this_cpu_read(hrtimer_interrupts_saved) == hrint)
		return true;

	__this_cpu_write(hrtimer_interrupts_saved, hrint);
	return false;
}

static void watchdog_interrupt_count(void)
{
	__this_cpu_inc(hrtimer_interrupts);
}

static DEFINE_PER_CPU(struct completion, softlockup_completion);
static DEFINE_PER_CPU(struct cpu_stop_work, softlockup_stop_work);

/*
 * The watchdog thread function - touches the timestamp.
 *
 * It only runs once every sample_period seconds (4 seconds by
 * default) to reset the softlockup timestamp. If this gets delayed
 * for more than 2*watchdog_thresh seconds then the debug-printout
 * triggers in watchdog_timer_fn().
 */
static int softlockup_fn(void *data)
{
	__this_cpu_write(soft_lockup_hrtimer_cnt,
			 __this_cpu_read(hrtimer_interrupts));
	__touch_watchdog();
	complete(this_cpu_ptr(&softlockup_completion));

	return 0;
}

/* watchdog kicker functions */
static enum hrtimer_restart watchdog_timer_fn(struct hrtimer *hrtimer)
{
	unsigned long touch_ts = __this_cpu_read(watchdog_touch_ts);
	struct pt_regs *regs = get_irq_regs();
	int duration;
	int softlockup_all_cpu_backtrace = sysctl_softlockup_all_cpu_backtrace;

	if (!watchdog_enabled)
		return HRTIMER_NORESTART;

	/* kick the hardlockup detector */
	watchdog_interrupt_count();

	/* kick the softlockup detector */
	if (completion_done(this_cpu_ptr(&softlockup_completion))) {
		reinit_completion(this_cpu_ptr(&softlockup_completion));
		stop_one_cpu_nowait(smp_processor_id(),
				softlockup_fn, NULL,
				this_cpu_ptr(&softlockup_stop_work));
	}

	/* .. and repeat */
	hrtimer_forward_now(hrtimer, ns_to_ktime(sample_period));

	if (touch_ts == 0) {
		if (unlikely(__this_cpu_read(softlockup_touch_sync))) {
			/*
			 * If the time stamp was touched atomically
			 * make sure the scheduler tick is up to date.
			 */
			__this_cpu_write(softlockup_touch_sync, false);
			sched_clock_tick();
		}

		/* Clear the guest paused flag on watchdog reset */
		kvm_check_and_clear_guest_paused();
		__touch_watchdog();
		return HRTIMER_RESTART;
	}

	/* check for a softlockup
	 * This is done by making sure a high priority task is
	 * being scheduled.  The task touches the watchdog to
	 * indicate it is getting cpu time.  If it hasn't then
	 * this is a good indication some task is hogging the cpu
	 */
	duration = is_softlockup(touch_ts);
	if (unlikely(duration)) {
		/*
		 * If a virtual machine is stopped by the host it can look to
		 * the watchdog like a soft lockup, check to see if the host
		 * stopped the vm before we issue the warning
		 */
		if (kvm_check_and_clear_guest_paused())
			return HRTIMER_RESTART;

		/* only warn once */
		if (__this_cpu_read(soft_watchdog_warn) == true) {
			/*
			 * When multiple processes are causing softlockups the
			 * softlockup detector only warns on the first one
			 * because the code relies on a full quiet cycle to
			 * re-arm.  The second process prevents the quiet cycle
			 * and never gets reported.  Use task pointers to detect
			 * this.
			 */
			if (__this_cpu_read(softlockup_task_ptr_saved) !=
			    current) {
				__this_cpu_write(soft_watchdog_warn, false);
				__touch_watchdog();
			}
			return HRTIMER_RESTART;
		}

		if (softlockup_all_cpu_backtrace) {
			/* Prevent multiple soft-lockup reports if one cpu is already
			 * engaged in dumping cpu back traces
			 */
			if (test_and_set_bit(0, &soft_lockup_nmi_warn)) {
				/* Someone else will report us. Let's give up */
				__this_cpu_write(soft_watchdog_warn, true);
				return HRTIMER_RESTART;
			}
		}

		pr_emerg("BUG: soft lockup - CPU#%d stuck for %us! [%s:%d]\n",
			smp_processor_id(), duration,
			current->comm, task_pid_nr(current));
		__this_cpu_write(softlockup_task_ptr_saved, current);
		print_modules();
		print_irqtrace_events(current);
		if (regs)
			show_regs(regs);
		else
			dump_stack();

		if (softlockup_all_cpu_backtrace) {
			/* Avoid generating two back traces for current
			 * given that one is already made above
			 */
			trigger_allbutself_cpu_backtrace();

			clear_bit(0, &soft_lockup_nmi_warn);
			/* Barrier to sync with other cpus */
			smp_mb__after_atomic();
		}

		add_taint(TAINT_SOFTLOCKUP, LOCKDEP_STILL_OK);
		if (softlockup_panic)
			panic("softlockup: hung tasks");
		__this_cpu_write(soft_watchdog_warn, true);
	} else
		__this_cpu_write(soft_watchdog_warn, false);

	return HRTIMER_RESTART;
}

static void watchdog_enable(unsigned int cpu)
{
	struct hrtimer *hrtimer = this_cpu_ptr(&watchdog_hrtimer);
	struct completion *done = this_cpu_ptr(&softlockup_completion);

	WARN_ON_ONCE(cpu != smp_processor_id());

	init_completion(done);
	complete(done);

	/*
	 * Start the timer first to prevent the NMI watchdog triggering
	 * before the timer has a chance to fire.
	 */
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = watchdog_timer_fn;
	hrtimer_start(hrtimer, ns_to_ktime(sample_period),
		      HRTIMER_MODE_REL_PINNED);

	/* Initialize timestamp */
	__touch_watchdog();
	/* Enable the perf event */
	if (watchdog_enabled & NMI_WATCHDOG_ENABLED)
		watchdog_nmi_enable(cpu);
}

static void watchdog_disable(unsigned int cpu)
{
	struct hrtimer *hrtimer = this_cpu_ptr(&watchdog_hrtimer);

	WARN_ON_ONCE(cpu != smp_processor_id());

	/*
	 * Disable the perf event first. That prevents that a large delay
	 * between disabling the timer and disabling the perf event causes
	 * the perf NMI to detect a false positive.
	 */
	watchdog_nmi_disable(cpu);
	hrtimer_cancel(hrtimer);
	wait_for_completion(this_cpu_ptr(&softlockup_completion));
}

static int softlockup_stop_fn(void *data)
{
	watchdog_disable(smp_processor_id());
	return 0;
}

static void softlockup_stop_all(void)
{
	int cpu;

	if (!softlockup_initialized)
		return;

	for_each_cpu(cpu, &watchdog_allowed_mask)
		smp_call_on_cpu(cpu, softlockup_stop_fn, NULL, false);

	cpumask_clear(&watchdog_allowed_mask);
}

static int softlockup_start_fn(void *data)
{
	watchdog_enable(smp_processor_id());
	return 0;
}

static void softlockup_start_all(void)
{
	int cpu;

	cpumask_copy(&watchdog_allowed_mask, &watchdog_cpumask);
	for_each_cpu(cpu, &watchdog_allowed_mask)
		smp_call_on_cpu(cpu, softlockup_start_fn, NULL, false);
}

int lockup_detector_online_cpu(unsigned int cpu)
{
	if (cpumask_test_cpu(cpu, &watchdog_allowed_mask))
		watchdog_enable(cpu);
	return 0;
}

int lockup_detector_offline_cpu(unsigned int cpu)
{
	if (cpumask_test_cpu(cpu, &watchdog_allowed_mask))
		watchdog_disable(cpu);
	return 0;
}

static void lockup_detector_reconfigure(void)
{
	cpus_read_lock();
	watchdog_nmi_stop();

	softlockup_stop_all();
	set_sample_period();
	lockup_detector_update_enable();
	if (watchdog_enabled && watchdog_thresh)
		softlockup_start_all();

	watchdog_nmi_start();
	cpus_read_unlock();
	/*
	 * Must be called outside the cpus locked section to prevent
	 * recursive locking in the perf code.
	 */
	__lockup_detector_cleanup();
}

/*
 * Create the watchdog thread infrastructure and configure the detector(s).
 *
 * The threads are not unparked as watchdog_allowed_mask is empty.  When
 * the threads are successfully initialized, take the proper locks and
 * unpark the threads in the watchdog_cpumask if the watchdog is enabled.
 */
static __init void lockup_detector_setup(void)
{
	/*
	 * If sysctl is off and watchdog got disabled on the command line,
	 * nothing to do here.
	 */
	lockup_detector_update_enable();

	if (!IS_ENABLED(CONFIG_SYSCTL) &&
	    !(watchdog_enabled && watchdog_thresh))
		return;

	mutex_lock(&watchdog_mutex);
	lockup_detector_reconfigure();
	softlockup_initialized = true;
	mutex_unlock(&watchdog_mutex);
}

#else /* CONFIG_SOFTLOCKUP_DETECTOR */
static void lockup_detector_reconfigure(void)
{
	cpus_read_lock();
	watchdog_nmi_stop();
	lockup_detector_update_enable();
	watchdog_nmi_start();
	cpus_read_unlock();
}
static inline void lockup_detector_setup(void)
{
	lockup_detector_reconfigure();
}
#endif /* !CONFIG_SOFTLOCKUP_DETECTOR */

static void __lockup_detector_cleanup(void)
{
	lockdep_assert_held(&watchdog_mutex);
	hardlockup_detector_perf_cleanup();
}

/**
 * lockup_detector_cleanup - Cleanup after cpu hotplug or sysctl changes
 *
 * Caller must not hold the cpu hotplug rwsem.
 */
void lockup_detector_cleanup(void)
{
	mutex_lock(&watchdog_mutex);
	__lockup_detector_cleanup();
	mutex_unlock(&watchdog_mutex);
}

/**
 * lockup_detector_soft_poweroff - Interface to stop lockup detector(s)
 *
 * Special interface for parisc. It prevents lockup detector warnings from
 * the default pm_poweroff() function which busy loops forever.
 */
void lockup_detector_soft_poweroff(void)
{
	watchdog_enabled = 0;
}

#ifdef CONFIG_SYSCTL

/* Propagate any changes to the watchdog threads */
static void proc_watchdog_update(void)
{
	/* Remove impossible cpus to keep sysctl output clean. */
	cpumask_and(&watchdog_cpumask, &watchdog_cpumask, cpu_possible_mask);
	lockup_detector_reconfigure();
}

/*
 * common function for watchdog, nmi_watchdog and soft_watchdog parameter
 *
 * caller             | table->data points to      | 'which'
 * -------------------|----------------------------|--------------------------
 * proc_watchdog      | watchdog_user_enabled      | NMI_WATCHDOG_ENABLED |
 *                    |                            | SOFT_WATCHDOG_ENABLED
 * -------------------|----------------------------|--------------------------
 * proc_nmi_watchdog  | nmi_watchdog_user_enabled  | NMI_WATCHDOG_ENABLED
 * -------------------|----------------------------|--------------------------
 * proc_soft_watchdog | soft_watchdog_user_enabled | SOFT_WATCHDOG_ENABLED
 */
static int proc_watchdog_common(int which, struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int err, old, *param = table->data;

	mutex_lock(&watchdog_mutex);

	if (!write) {
		/*
		 * On read synchronize the userspace interface. This is a
		 * racy snapshot.
		 */
		*param = (watchdog_enabled & which) != 0;
		err = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	} else {
		old = READ_ONCE(*param);
		err = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
		if (!err && old != READ_ONCE(*param))
			proc_watchdog_update();
	}
	mutex_unlock(&watchdog_mutex);
	return err;
}

/*
 * /proc/sys/kernel/watchdog
 */
int proc_watchdog(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return proc_watchdog_common(NMI_WATCHDOG_ENABLED|SOFT_WATCHDOG_ENABLED,
				    table, write, buffer, lenp, ppos);
}

/*
 * /proc/sys/kernel/nmi_watchdog
 */
int proc_nmi_watchdog(struct ctl_table *table, int write,
		      void __user *buffer, size_t *lenp, loff_t *ppos)
{
	if (!nmi_watchdog_available && write)
		return -ENOTSUPP;
	return proc_watchdog_common(NMI_WATCHDOG_ENABLED,
				    table, write, buffer, lenp, ppos);
}

/*
 * /proc/sys/kernel/soft_watchdog
 */
int proc_soft_watchdog(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return proc_watchdog_common(SOFT_WATCHDOG_ENABLED,
				    table, write, buffer, lenp, ppos);
}

/*
 * /proc/sys/kernel/watchdog_thresh
 */
int proc_watchdog_thresh(struct ctl_table *table, int write,
			 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int err, old;

	mutex_lock(&watchdog_mutex);

	old = READ_ONCE(watchdog_thresh);
	err = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (!err && write && old != READ_ONCE(watchdog_thresh))
		proc_watchdog_update();

	mutex_unlock(&watchdog_mutex);
	return err;
}

/*
 * The cpumask is the mask of possible cpus that the watchdog can run
 * on, not the mask of cpus it is actually running on.  This allows the
 * user to specify a mask that will include cpus that have not yet
 * been brought online, if desired.
 */
int proc_watchdog_cpumask(struct ctl_table *table, int write,
			  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int err;

	mutex_lock(&watchdog_mutex);

	err = proc_do_large_bitmap(table, write, buffer, lenp, ppos);
	if (!err && write)
		proc_watchdog_update();

	mutex_unlock(&watchdog_mutex);
	return err;
}
#endif /* CONFIG_SYSCTL */

void __init lockup_detector_init(void)
{
	if (tick_nohz_full_enabled())
		pr_info("Disabling watchdog on nohz_full cores by default\n");

	cpumask_copy(&watchdog_cpumask,
		     housekeeping_cpumask(HK_FLAG_TIMER));

	if (!watchdog_nmi_probe())
		nmi_watchdog_available = true;
	lockup_detector_setup();
}
