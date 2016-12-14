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

#define pr_fmt(fmt) "NMI watchdog: " fmt

#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/smpboot.h>
#include <linux/sched/rt.h>
#include <linux/tick.h>
#include <linux/workqueue.h>

#include <asm/irq_regs.h>
#include <linux/kvm_para.h>
#include <linux/perf_event.h>
#include <linux/kthread.h>

static DEFINE_MUTEX(watchdog_proc_mutex);

#if defined(CONFIG_HAVE_NMI_WATCHDOG) || defined(CONFIG_HARDLOCKUP_DETECTOR)
unsigned long __read_mostly watchdog_enabled = SOFT_WATCHDOG_ENABLED|NMI_WATCHDOG_ENABLED;
#else
unsigned long __read_mostly watchdog_enabled = SOFT_WATCHDOG_ENABLED;
#endif
int __read_mostly nmi_watchdog_enabled;
int __read_mostly soft_watchdog_enabled;
int __read_mostly watchdog_user_enabled;
int __read_mostly watchdog_thresh = 10;

#ifdef CONFIG_SMP
int __read_mostly sysctl_softlockup_all_cpu_backtrace;
int __read_mostly sysctl_hardlockup_all_cpu_backtrace;
#endif
static struct cpumask watchdog_cpumask __read_mostly;
unsigned long *watchdog_cpumask_bits = cpumask_bits(&watchdog_cpumask);

/* Helper for online, unparked cpus. */
#define for_each_watchdog_cpu(cpu) \
	for_each_cpu_and((cpu), cpu_online_mask, &watchdog_cpumask)

/*
 * The 'watchdog_running' variable is set to 1 when the watchdog threads
 * are registered/started and is set to 0 when the watchdog threads are
 * unregistered/stopped, so it is an indicator whether the threads exist.
 */
static int __read_mostly watchdog_running;
/*
 * If a subsystem has a need to deactivate the watchdog temporarily, it
 * can use the suspend/resume interface to achieve this. The content of
 * the 'watchdog_suspended' variable reflects this state. Existing threads
 * are parked/unparked by the lockup_detector_{suspend|resume} functions
 * (see comment blocks pertaining to those functions for further details).
 *
 * 'watchdog_suspended' also prevents threads from being registered/started
 * or unregistered/stopped via parameters in /proc/sys/kernel, so the state
 * of 'watchdog_running' cannot change while the watchdog is deactivated
 * temporarily (see related code in 'proc' handlers).
 */
static int __read_mostly watchdog_suspended;

static u64 __read_mostly sample_period;

static DEFINE_PER_CPU(unsigned long, watchdog_touch_ts);
static DEFINE_PER_CPU(struct task_struct *, softlockup_watchdog);
static DEFINE_PER_CPU(struct hrtimer, watchdog_hrtimer);
static DEFINE_PER_CPU(bool, softlockup_touch_sync);
static DEFINE_PER_CPU(bool, soft_watchdog_warn);
static DEFINE_PER_CPU(unsigned long, hrtimer_interrupts);
static DEFINE_PER_CPU(unsigned long, soft_lockup_hrtimer_cnt);
static DEFINE_PER_CPU(struct task_struct *, softlockup_task_ptr_saved);
#ifdef CONFIG_HARDLOCKUP_DETECTOR
static DEFINE_PER_CPU(bool, hard_watchdog_warn);
static DEFINE_PER_CPU(bool, watchdog_nmi_touch);
static DEFINE_PER_CPU(unsigned long, hrtimer_interrupts_saved);
static DEFINE_PER_CPU(struct perf_event *, watchdog_ev);
#endif
static unsigned long soft_lockup_nmi_warn;

/* boot commands */
/*
 * Should we panic when a soft-lockup or hard-lockup occurs:
 */
#ifdef CONFIG_HARDLOCKUP_DETECTOR
unsigned int __read_mostly hardlockup_panic =
			CONFIG_BOOTPARAM_HARDLOCKUP_PANIC_VALUE;
static unsigned long hardlockup_allcpu_dumped;
/*
 * We may not want to enable hard lockup detection by default in all cases,
 * for example when running the kernel as a guest on a hypervisor. In these
 * cases this function can be called to disable hard lockup detection. This
 * function should only be executed once by the boot processor before the
 * kernel command line parameters are parsed, because otherwise it is not
 * possible to override this in hardlockup_panic_setup().
 */
void hardlockup_detector_disable(void)
{
	watchdog_enabled &= ~NMI_WATCHDOG_ENABLED;
}

static int __init hardlockup_panic_setup(char *str)
{
	if (!strncmp(str, "panic", 5))
		hardlockup_panic = 1;
	else if (!strncmp(str, "nopanic", 7))
		hardlockup_panic = 0;
	else if (!strncmp(str, "0", 1))
		watchdog_enabled &= ~NMI_WATCHDOG_ENABLED;
	else if (!strncmp(str, "1", 1))
		watchdog_enabled |= NMI_WATCHDOG_ENABLED;
	return 1;
}
__setup("nmi_watchdog=", hardlockup_panic_setup);
#endif

unsigned int __read_mostly softlockup_panic =
			CONFIG_BOOTPARAM_SOFTLOCKUP_PANIC_VALUE;

static int __init softlockup_panic_setup(char *str)
{
	softlockup_panic = simple_strtoul(str, NULL, 0);

	return 1;
}
__setup("softlockup_panic=", softlockup_panic_setup);

static int __init nowatchdog_setup(char *str)
{
	watchdog_enabled = 0;
	return 1;
}
__setup("nowatchdog", nowatchdog_setup);

static int __init nosoftlockup_setup(char *str)
{
	watchdog_enabled &= ~SOFT_WATCHDOG_ENABLED;
	return 1;
}
__setup("nosoftlockup", nosoftlockup_setup);

#ifdef CONFIG_SMP
static int __init softlockup_all_cpu_backtrace_setup(char *str)
{
	sysctl_softlockup_all_cpu_backtrace =
		!!simple_strtol(str, NULL, 0);
	return 1;
}
__setup("softlockup_all_cpu_backtrace=", softlockup_all_cpu_backtrace_setup);
static int __init hardlockup_all_cpu_backtrace_setup(char *str)
{
	sysctl_hardlockup_all_cpu_backtrace =
		!!simple_strtol(str, NULL, 0);
	return 1;
}
__setup("hardlockup_all_cpu_backtrace=", hardlockup_all_cpu_backtrace_setup);
#endif

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
void touch_softlockup_watchdog_sched(void)
{
	/*
	 * Preemption can be enabled.  It doesn't matter which CPU's timestamp
	 * gets zeroed here, so use the raw_ operation.
	 */
	raw_cpu_write(watchdog_touch_ts, 0);
}

void touch_softlockup_watchdog(void)
{
	touch_softlockup_watchdog_sched();
	wq_watchdog_touch(raw_smp_processor_id());
}
EXPORT_SYMBOL(touch_softlockup_watchdog);

void touch_all_softlockup_watchdogs(void)
{
	int cpu;

	/*
	 * this is done lockless
	 * do we care if a 0 races with a timestamp?
	 * all it means is the softlock check starts one cycle later
	 */
	for_each_watchdog_cpu(cpu)
		per_cpu(watchdog_touch_ts, cpu) = 0;
	wq_watchdog_touch(-1);
}

#ifdef CONFIG_HARDLOCKUP_DETECTOR
void touch_nmi_watchdog(void)
{
	/*
	 * Using __raw here because some code paths have
	 * preemption enabled.  If preemption is enabled
	 * then interrupts should be enabled too, in which
	 * case we shouldn't have to worry about the watchdog
	 * going off.
	 */
	raw_cpu_write(watchdog_nmi_touch, true);
	touch_softlockup_watchdog();
}
EXPORT_SYMBOL(touch_nmi_watchdog);

#endif

void touch_softlockup_watchdog_sync(void)
{
	__this_cpu_write(softlockup_touch_sync, true);
	__this_cpu_write(watchdog_touch_ts, 0);
}

#ifdef CONFIG_HARDLOCKUP_DETECTOR
/* watchdog detector functions */
bool is_hardlockup(void)
{
	unsigned long hrint = __this_cpu_read(hrtimer_interrupts);

	if (__this_cpu_read(hrtimer_interrupts_saved) == hrint)
		return true;

	__this_cpu_write(hrtimer_interrupts_saved, hrint);
	return false;
}
#endif

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

#ifdef CONFIG_HARDLOCKUP_DETECTOR

static struct perf_event_attr wd_hw_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES,
	.size		= sizeof(struct perf_event_attr),
	.pinned		= 1,
	.disabled	= 1,
};

/* Callback function for perf event subsystem */
static void watchdog_overflow_callback(struct perf_event *event,
		 struct perf_sample_data *data,
		 struct pt_regs *regs)
{
	/* Ensure the watchdog never gets throttled */
	event->hw.interrupts = 0;

	if (__this_cpu_read(watchdog_nmi_touch) == true) {
		__this_cpu_write(watchdog_nmi_touch, false);
		return;
	}

	/* check for a hardlockup
	 * This is done by making sure our timer interrupt
	 * is incrementing.  The timer interrupt should have
	 * fired multiple times before we overflow'd.  If it hasn't
	 * then this is a good indication the cpu is stuck
	 */
	if (is_hardlockup()) {
		int this_cpu = smp_processor_id();

		/* only print hardlockups once */
		if (__this_cpu_read(hard_watchdog_warn) == true)
			return;

		pr_emerg("Watchdog detected hard LOCKUP on cpu %d", this_cpu);
		print_modules();
		print_irqtrace_events(current);
		if (regs)
			show_regs(regs);
		else
			dump_stack();

		/*
		 * Perform all-CPU dump only once to avoid multiple hardlockups
		 * generating interleaving traces
		 */
		if (sysctl_hardlockup_all_cpu_backtrace &&
				!test_and_set_bit(0, &hardlockup_allcpu_dumped))
			trigger_allbutself_cpu_backtrace();

		if (hardlockup_panic)
			nmi_panic(regs, "Hard LOCKUP");

		__this_cpu_write(hard_watchdog_warn, true);
		return;
	}

	__this_cpu_write(hard_watchdog_warn, false);
	return;
}
#endif /* CONFIG_HARDLOCKUP_DETECTOR */

static void watchdog_interrupt_count(void)
{
	__this_cpu_inc(hrtimer_interrupts);
}

static int watchdog_nmi_enable(unsigned int cpu);
static void watchdog_nmi_disable(unsigned int cpu);

static int watchdog_enable_all_cpus(void);
static void watchdog_disable_all_cpus(void);

/* watchdog kicker functions */
static enum hrtimer_restart watchdog_timer_fn(struct hrtimer *hrtimer)
{
	unsigned long touch_ts = __this_cpu_read(watchdog_touch_ts);
	struct pt_regs *regs = get_irq_regs();
	int duration;
	int softlockup_all_cpu_backtrace = sysctl_softlockup_all_cpu_backtrace;

	/* kick the hardlockup detector */
	watchdog_interrupt_count();

	/* kick the softlockup detector */
	wake_up_process(__this_cpu_read(softlockup_watchdog));

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

static void watchdog_set_prio(unsigned int policy, unsigned int prio)
{
	struct sched_param param = { .sched_priority = prio };

	sched_setscheduler(current, policy, &param);
}

static void watchdog_enable(unsigned int cpu)
{
	struct hrtimer *hrtimer = raw_cpu_ptr(&watchdog_hrtimer);

	/* kick off the timer for the hardlockup detector */
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = watchdog_timer_fn;

	/* Enable the perf event */
	watchdog_nmi_enable(cpu);

	/* done here because hrtimer_start can only pin to smp_processor_id() */
	hrtimer_start(hrtimer, ns_to_ktime(sample_period),
		      HRTIMER_MODE_REL_PINNED);

	/* initialize timestamp */
	watchdog_set_prio(SCHED_FIFO, MAX_RT_PRIO - 1);
	__touch_watchdog();
}

static void watchdog_disable(unsigned int cpu)
{
	struct hrtimer *hrtimer = raw_cpu_ptr(&watchdog_hrtimer);

	watchdog_set_prio(SCHED_NORMAL, 0);
	hrtimer_cancel(hrtimer);
	/* disable the perf event */
	watchdog_nmi_disable(cpu);
}

static void watchdog_cleanup(unsigned int cpu, bool online)
{
	watchdog_disable(cpu);
}

static int watchdog_should_run(unsigned int cpu)
{
	return __this_cpu_read(hrtimer_interrupts) !=
		__this_cpu_read(soft_lockup_hrtimer_cnt);
}

/*
 * The watchdog thread function - touches the timestamp.
 *
 * It only runs once every sample_period seconds (4 seconds by
 * default) to reset the softlockup timestamp. If this gets delayed
 * for more than 2*watchdog_thresh seconds then the debug-printout
 * triggers in watchdog_timer_fn().
 */
static void watchdog(unsigned int cpu)
{
	__this_cpu_write(soft_lockup_hrtimer_cnt,
			 __this_cpu_read(hrtimer_interrupts));
	__touch_watchdog();

	/*
	 * watchdog_nmi_enable() clears the NMI_WATCHDOG_ENABLED bit in the
	 * failure path. Check for failures that can occur asynchronously -
	 * for example, when CPUs are on-lined - and shut down the hardware
	 * perf event on each CPU accordingly.
	 *
	 * The only non-obvious place this bit can be cleared is through
	 * watchdog_nmi_enable(), so a pr_info() is placed there.  Placing a
	 * pr_info here would be too noisy as it would result in a message
	 * every few seconds if the hardlockup was disabled but the softlockup
	 * enabled.
	 */
	if (!(watchdog_enabled & NMI_WATCHDOG_ENABLED))
		watchdog_nmi_disable(cpu);
}

#ifdef CONFIG_HARDLOCKUP_DETECTOR
/*
 * People like the simple clean cpu node info on boot.
 * Reduce the watchdog noise by only printing messages
 * that are different from what cpu0 displayed.
 */
static unsigned long cpu0_err;

static int watchdog_nmi_enable(unsigned int cpu)
{
	struct perf_event_attr *wd_attr;
	struct perf_event *event = per_cpu(watchdog_ev, cpu);

	/* nothing to do if the hard lockup detector is disabled */
	if (!(watchdog_enabled & NMI_WATCHDOG_ENABLED))
		goto out;

	/* is it already setup and enabled? */
	if (event && event->state > PERF_EVENT_STATE_OFF)
		goto out;

	/* it is setup but not enabled */
	if (event != NULL)
		goto out_enable;

	wd_attr = &wd_hw_attr;
	wd_attr->sample_period = hw_nmi_get_sample_period(watchdog_thresh);

	/* Try to register using hardware perf events */
	event = perf_event_create_kernel_counter(wd_attr, cpu, NULL, watchdog_overflow_callback, NULL);

	/* save cpu0 error for future comparision */
	if (cpu == 0 && IS_ERR(event))
		cpu0_err = PTR_ERR(event);

	if (!IS_ERR(event)) {
		/* only print for cpu0 or different than cpu0 */
		if (cpu == 0 || cpu0_err)
			pr_info("enabled on all CPUs, permanently consumes one hw-PMU counter.\n");
		goto out_save;
	}

	/*
	 * Disable the hard lockup detector if _any_ CPU fails to set up
	 * set up the hardware perf event. The watchdog() function checks
	 * the NMI_WATCHDOG_ENABLED bit periodically.
	 *
	 * The barriers are for syncing up watchdog_enabled across all the
	 * cpus, as clear_bit() does not use barriers.
	 */
	smp_mb__before_atomic();
	clear_bit(NMI_WATCHDOG_ENABLED_BIT, &watchdog_enabled);
	smp_mb__after_atomic();

	/* skip displaying the same error again */
	if (cpu > 0 && (PTR_ERR(event) == cpu0_err))
		return PTR_ERR(event);

	/* vary the KERN level based on the returned errno */
	if (PTR_ERR(event) == -EOPNOTSUPP)
		pr_info("disabled (cpu%i): not supported (no LAPIC?)\n", cpu);
	else if (PTR_ERR(event) == -ENOENT)
		pr_warn("disabled (cpu%i): hardware events not enabled\n",
			 cpu);
	else
		pr_err("disabled (cpu%i): unable to create perf event: %ld\n",
			cpu, PTR_ERR(event));

	pr_info("Shutting down hard lockup detector on all cpus\n");

	return PTR_ERR(event);

	/* success path */
out_save:
	per_cpu(watchdog_ev, cpu) = event;
out_enable:
	perf_event_enable(per_cpu(watchdog_ev, cpu));
out:
	return 0;
}

static void watchdog_nmi_disable(unsigned int cpu)
{
	struct perf_event *event = per_cpu(watchdog_ev, cpu);

	if (event) {
		perf_event_disable(event);
		per_cpu(watchdog_ev, cpu) = NULL;

		/* should be in cleanup, but blocks oprofile */
		perf_event_release_kernel(event);
	}
	if (cpu == 0) {
		/* watchdog_nmi_enable() expects this to be zero initially. */
		cpu0_err = 0;
	}
}

#else
static int watchdog_nmi_enable(unsigned int cpu) { return 0; }
static void watchdog_nmi_disable(unsigned int cpu) { return; }
#endif /* CONFIG_HARDLOCKUP_DETECTOR */

static struct smp_hotplug_thread watchdog_threads = {
	.store			= &softlockup_watchdog,
	.thread_should_run	= watchdog_should_run,
	.thread_fn		= watchdog,
	.thread_comm		= "watchdog/%u",
	.setup			= watchdog_enable,
	.cleanup		= watchdog_cleanup,
	.park			= watchdog_disable,
	.unpark			= watchdog_enable,
};

/*
 * park all watchdog threads that are specified in 'watchdog_cpumask'
 *
 * This function returns an error if kthread_park() of a watchdog thread
 * fails. In this situation, the watchdog threads of some CPUs can already
 * be parked and the watchdog threads of other CPUs can still be runnable.
 * Callers are expected to handle this special condition as appropriate in
 * their context.
 *
 * This function may only be called in a context that is protected against
 * races with CPU hotplug - for example, via get_online_cpus().
 */
static int watchdog_park_threads(void)
{
	int cpu, ret = 0;

	for_each_watchdog_cpu(cpu) {
		ret = kthread_park(per_cpu(softlockup_watchdog, cpu));
		if (ret)
			break;
	}

	return ret;
}

/*
 * unpark all watchdog threads that are specified in 'watchdog_cpumask'
 *
 * This function may only be called in a context that is protected against
 * races with CPU hotplug - for example, via get_online_cpus().
 */
static void watchdog_unpark_threads(void)
{
	int cpu;

	for_each_watchdog_cpu(cpu)
		kthread_unpark(per_cpu(softlockup_watchdog, cpu));
}

/*
 * Suspend the hard and soft lockup detector by parking the watchdog threads.
 */
int lockup_detector_suspend(void)
{
	int ret = 0;

	get_online_cpus();
	mutex_lock(&watchdog_proc_mutex);
	/*
	 * Multiple suspend requests can be active in parallel (counted by
	 * the 'watchdog_suspended' variable). If the watchdog threads are
	 * running, the first caller takes care that they will be parked.
	 * The state of 'watchdog_running' cannot change while a suspend
	 * request is active (see related code in 'proc' handlers).
	 */
	if (watchdog_running && !watchdog_suspended)
		ret = watchdog_park_threads();

	if (ret == 0)
		watchdog_suspended++;
	else {
		watchdog_disable_all_cpus();
		pr_err("Failed to suspend lockup detectors, disabled\n");
		watchdog_enabled = 0;
	}

	mutex_unlock(&watchdog_proc_mutex);

	return ret;
}

/*
 * Resume the hard and soft lockup detector by unparking the watchdog threads.
 */
void lockup_detector_resume(void)
{
	mutex_lock(&watchdog_proc_mutex);

	watchdog_suspended--;
	/*
	 * The watchdog threads are unparked if they were previously running
	 * and if there is no more active suspend request.
	 */
	if (watchdog_running && !watchdog_suspended)
		watchdog_unpark_threads();

	mutex_unlock(&watchdog_proc_mutex);
	put_online_cpus();
}

static int update_watchdog_all_cpus(void)
{
	int ret;

	ret = watchdog_park_threads();
	if (ret)
		return ret;

	watchdog_unpark_threads();

	return 0;
}

static int watchdog_enable_all_cpus(void)
{
	int err = 0;

	if (!watchdog_running) {
		err = smpboot_register_percpu_thread_cpumask(&watchdog_threads,
							     &watchdog_cpumask);
		if (err)
			pr_err("Failed to create watchdog threads, disabled\n");
		else
			watchdog_running = 1;
	} else {
		/*
		 * Enable/disable the lockup detectors or
		 * change the sample period 'on the fly'.
		 */
		err = update_watchdog_all_cpus();

		if (err) {
			watchdog_disable_all_cpus();
			pr_err("Failed to update lockup detectors, disabled\n");
		}
	}

	if (err)
		watchdog_enabled = 0;

	return err;
}

static void watchdog_disable_all_cpus(void)
{
	if (watchdog_running) {
		watchdog_running = 0;
		smpboot_unregister_percpu_thread(&watchdog_threads);
	}
}

#ifdef CONFIG_SYSCTL

/*
 * Update the run state of the lockup detectors.
 */
static int proc_watchdog_update(void)
{
	int err = 0;

	/*
	 * Watchdog threads won't be started if they are already active.
	 * The 'watchdog_running' variable in watchdog_*_all_cpus() takes
	 * care of this. If those threads are already active, the sample
	 * period will be updated and the lockup detectors will be enabled
	 * or disabled 'on the fly'.
	 */
	if (watchdog_enabled && watchdog_thresh)
		err = watchdog_enable_all_cpus();
	else
		watchdog_disable_all_cpus();

	return err;

}

/*
 * common function for watchdog, nmi_watchdog and soft_watchdog parameter
 *
 * caller             | table->data points to | 'which' contains the flag(s)
 * -------------------|-----------------------|-----------------------------
 * proc_watchdog      | watchdog_user_enabled | NMI_WATCHDOG_ENABLED or'ed
 *                    |                       | with SOFT_WATCHDOG_ENABLED
 * -------------------|-----------------------|-----------------------------
 * proc_nmi_watchdog  | nmi_watchdog_enabled  | NMI_WATCHDOG_ENABLED
 * -------------------|-----------------------|-----------------------------
 * proc_soft_watchdog | soft_watchdog_enabled | SOFT_WATCHDOG_ENABLED
 */
static int proc_watchdog_common(int which, struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int err, old, new;
	int *watchdog_param = (int *)table->data;

	get_online_cpus();
	mutex_lock(&watchdog_proc_mutex);

	if (watchdog_suspended) {
		/* no parameter changes allowed while watchdog is suspended */
		err = -EAGAIN;
		goto out;
	}

	/*
	 * If the parameter is being read return the state of the corresponding
	 * bit(s) in 'watchdog_enabled', else update 'watchdog_enabled' and the
	 * run state of the lockup detectors.
	 */
	if (!write) {
		*watchdog_param = (watchdog_enabled & which) != 0;
		err = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	} else {
		err = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
		if (err)
			goto out;

		/*
		 * There is a race window between fetching the current value
		 * from 'watchdog_enabled' and storing the new value. During
		 * this race window, watchdog_nmi_enable() can sneak in and
		 * clear the NMI_WATCHDOG_ENABLED bit in 'watchdog_enabled'.
		 * The 'cmpxchg' detects this race and the loop retries.
		 */
		do {
			old = watchdog_enabled;
			/*
			 * If the parameter value is not zero set the
			 * corresponding bit(s), else clear it(them).
			 */
			if (*watchdog_param)
				new = old | which;
			else
				new = old & ~which;
		} while (cmpxchg(&watchdog_enabled, old, new) != old);

		/*
		 * Update the run state of the lockup detectors. There is _no_
		 * need to check the value returned by proc_watchdog_update()
		 * and to restore the previous value of 'watchdog_enabled' as
		 * both lockup detectors are disabled if proc_watchdog_update()
		 * returns an error.
		 */
		if (old == new)
			goto out;

		err = proc_watchdog_update();
	}
out:
	mutex_unlock(&watchdog_proc_mutex);
	put_online_cpus();
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
	int err, old, new;

	get_online_cpus();
	mutex_lock(&watchdog_proc_mutex);

	if (watchdog_suspended) {
		/* no parameter changes allowed while watchdog is suspended */
		err = -EAGAIN;
		goto out;
	}

	old = ACCESS_ONCE(watchdog_thresh);
	err = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (err || !write)
		goto out;

	/*
	 * Update the sample period. Restore on failure.
	 */
	new = ACCESS_ONCE(watchdog_thresh);
	if (old == new)
		goto out;

	set_sample_period();
	err = proc_watchdog_update();
	if (err) {
		watchdog_thresh = old;
		set_sample_period();
	}
out:
	mutex_unlock(&watchdog_proc_mutex);
	put_online_cpus();
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

	get_online_cpus();
	mutex_lock(&watchdog_proc_mutex);

	if (watchdog_suspended) {
		/* no parameter changes allowed while watchdog is suspended */
		err = -EAGAIN;
		goto out;
	}

	err = proc_do_large_bitmap(table, write, buffer, lenp, ppos);
	if (!err && write) {
		/* Remove impossible cpus to keep sysctl output cleaner. */
		cpumask_and(&watchdog_cpumask, &watchdog_cpumask,
			    cpu_possible_mask);

		if (watchdog_running) {
			/*
			 * Failure would be due to being unable to allocate
			 * a temporary cpumask, so we are likely not in a
			 * position to do much else to make things better.
			 */
			if (smpboot_update_cpumask_percpu_thread(
				    &watchdog_threads, &watchdog_cpumask) != 0)
				pr_err("cpumask update failed\n");
		}
	}
out:
	mutex_unlock(&watchdog_proc_mutex);
	put_online_cpus();
	return err;
}

#endif /* CONFIG_SYSCTL */

void __init lockup_detector_init(void)
{
	set_sample_period();

#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled()) {
		pr_info("Disabling watchdog on nohz_full cores by default\n");
		cpumask_copy(&watchdog_cpumask, housekeeping_mask);
	} else
		cpumask_copy(&watchdog_cpumask, cpu_possible_mask);
#else
	cpumask_copy(&watchdog_cpumask, cpu_possible_mask);
#endif

	if (watchdog_enabled)
		watchdog_enable_all_cpus();
}
