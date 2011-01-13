/*
 * Detect hard and soft lockups on a system
 *
 * started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 * this code detects hard lockups: incidents in where on a CPU
 * the kernel does not respond to anything except NMI.
 *
 * Note: Most of this code is borrowed heavily from softlockup.c,
 * so thanks to Ingo for the initial implementation.
 * Some chunks also taken from arch/x86/kernel/apic/nmi.c, thanks
 * to those contributors as well.
 */

#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/lockdep.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/sysctl.h>

#include <asm/irq_regs.h>
#include <linux/perf_event.h>

int watchdog_enabled;
int __read_mostly softlockup_thresh = 60;

static DEFINE_PER_CPU(unsigned long, watchdog_touch_ts);
static DEFINE_PER_CPU(struct task_struct *, softlockup_watchdog);
static DEFINE_PER_CPU(struct hrtimer, watchdog_hrtimer);
static DEFINE_PER_CPU(bool, softlockup_touch_sync);
static DEFINE_PER_CPU(bool, soft_watchdog_warn);
#ifdef CONFIG_HARDLOCKUP_DETECTOR
static DEFINE_PER_CPU(bool, hard_watchdog_warn);
static DEFINE_PER_CPU(bool, watchdog_nmi_touch);
static DEFINE_PER_CPU(unsigned long, hrtimer_interrupts);
static DEFINE_PER_CPU(unsigned long, hrtimer_interrupts_saved);
static DEFINE_PER_CPU(struct perf_event *, watchdog_ev);
#endif

static int no_watchdog;


/* boot commands */
/*
 * Should we panic when a soft-lockup or hard-lockup occurs:
 */
#ifdef CONFIG_HARDLOCKUP_DETECTOR
static int hardlockup_panic;

static int __init hardlockup_panic_setup(char *str)
{
	if (!strncmp(str, "panic", 5))
		hardlockup_panic = 1;
	else if (!strncmp(str, "0", 1))
		no_watchdog = 1;
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
	no_watchdog = 1;
	return 1;
}
__setup("nowatchdog", nowatchdog_setup);

/* deprecated */
static int __init nosoftlockup_setup(char *str)
{
	no_watchdog = 1;
	return 1;
}
__setup("nosoftlockup", nosoftlockup_setup);
/*  */


/*
 * Returns seconds, approximately.  We don't need nanosecond
 * resolution, and we don't need to waste time with a big divide when
 * 2^30ns == 1.074s.
 */
static unsigned long get_timestamp(int this_cpu)
{
	return cpu_clock(this_cpu) >> 30LL;  /* 2^30 ~= 10^9 */
}

static unsigned long get_sample_period(void)
{
	/*
	 * convert softlockup_thresh from seconds to ns
	 * the divide by 5 is to give hrtimer 5 chances to
	 * increment before the hardlockup detector generates
	 * a warning
	 */
	return softlockup_thresh / 5 * NSEC_PER_SEC;
}

/* Commands for resetting the watchdog */
static void __touch_watchdog(void)
{
	int this_cpu = smp_processor_id();

	__this_cpu_write(watchdog_touch_ts, get_timestamp(this_cpu));
}

void touch_softlockup_watchdog(void)
{
	__this_cpu_write(watchdog_touch_ts, 0);
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
	for_each_online_cpu(cpu)
		per_cpu(watchdog_touch_ts, cpu) = 0;
}

#ifdef CONFIG_HARDLOCKUP_DETECTOR
void touch_nmi_watchdog(void)
{
	if (watchdog_enabled) {
		unsigned cpu;

		for_each_present_cpu(cpu) {
			if (per_cpu(watchdog_nmi_touch, cpu) != true)
				per_cpu(watchdog_nmi_touch, cpu) = true;
		}
	}
	touch_softlockup_watchdog();
}
EXPORT_SYMBOL(touch_nmi_watchdog);

#endif

void touch_softlockup_watchdog_sync(void)
{
	__raw_get_cpu_var(softlockup_touch_sync) = true;
	__raw_get_cpu_var(watchdog_touch_ts) = 0;
}

#ifdef CONFIG_HARDLOCKUP_DETECTOR
/* watchdog detector functions */
static int is_hardlockup(void)
{
	unsigned long hrint = __this_cpu_read(hrtimer_interrupts);

	if (__this_cpu_read(hrtimer_interrupts_saved) == hrint)
		return 1;

	__this_cpu_write(hrtimer_interrupts_saved, hrint);
	return 0;
}
#endif

static int is_softlockup(unsigned long touch_ts)
{
	unsigned long now = get_timestamp(smp_processor_id());

	/* Warn about unreasonable delays: */
	if (time_after(now, touch_ts + softlockup_thresh))
		return now - touch_ts;

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
static void watchdog_overflow_callback(struct perf_event *event, int nmi,
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

		if (hardlockup_panic)
			panic("Watchdog detected hard LOCKUP on cpu %d", this_cpu);
		else
			WARN(1, "Watchdog detected hard LOCKUP on cpu %d", this_cpu);

		__this_cpu_write(hard_watchdog_warn, true);
		return;
	}

	__this_cpu_write(hard_watchdog_warn, false);
	return;
}
static void watchdog_interrupt_count(void)
{
	__this_cpu_inc(hrtimer_interrupts);
}
#else
static inline void watchdog_interrupt_count(void) { return; }
#endif /* CONFIG_HARDLOCKUP_DETECTOR */

/* watchdog kicker functions */
static enum hrtimer_restart watchdog_timer_fn(struct hrtimer *hrtimer)
{
	unsigned long touch_ts = __this_cpu_read(watchdog_touch_ts);
	struct pt_regs *regs = get_irq_regs();
	int duration;

	/* kick the hardlockup detector */
	watchdog_interrupt_count();

	/* kick the softlockup detector */
	wake_up_process(__this_cpu_read(softlockup_watchdog));

	/* .. and repeat */
	hrtimer_forward_now(hrtimer, ns_to_ktime(get_sample_period()));

	if (touch_ts == 0) {
		if (unlikely(__this_cpu_read(softlockup_touch_sync))) {
			/*
			 * If the time stamp was touched atomically
			 * make sure the scheduler tick is up to date.
			 */
			__this_cpu_write(softlockup_touch_sync, false);
			sched_clock_tick();
		}
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
		/* only warn once */
		if (__this_cpu_read(soft_watchdog_warn) == true)
			return HRTIMER_RESTART;

		printk(KERN_ERR "BUG: soft lockup - CPU#%d stuck for %us! [%s:%d]\n",
			smp_processor_id(), duration,
			current->comm, task_pid_nr(current));
		print_modules();
		print_irqtrace_events(current);
		if (regs)
			show_regs(regs);
		else
			dump_stack();

		if (softlockup_panic)
			panic("softlockup: hung tasks");
		__this_cpu_write(soft_watchdog_warn, true);
	} else
		__this_cpu_write(soft_watchdog_warn, false);

	return HRTIMER_RESTART;
}


/*
 * The watchdog thread - touches the timestamp.
 */
static int watchdog(void *unused)
{
	static struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };
	struct hrtimer *hrtimer = &__raw_get_cpu_var(watchdog_hrtimer);

	sched_setscheduler(current, SCHED_FIFO, &param);

	/* initialize timestamp */
	__touch_watchdog();

	/* kick off the timer for the hardlockup detector */
	/* done here because hrtimer_start can only pin to smp_processor_id() */
	hrtimer_start(hrtimer, ns_to_ktime(get_sample_period()),
		      HRTIMER_MODE_REL_PINNED);

	set_current_state(TASK_INTERRUPTIBLE);
	/*
	 * Run briefly once per second to reset the softlockup timestamp.
	 * If this gets delayed for more than 60 seconds then the
	 * debug-printout triggers in watchdog_timer_fn().
	 */
	while (!kthread_should_stop()) {
		__touch_watchdog();
		schedule();

		if (kthread_should_stop())
			break;

		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}


#ifdef CONFIG_HARDLOCKUP_DETECTOR
static int watchdog_nmi_enable(int cpu)
{
	struct perf_event_attr *wd_attr;
	struct perf_event *event = per_cpu(watchdog_ev, cpu);

	/* is it already setup and enabled? */
	if (event && event->state > PERF_EVENT_STATE_OFF)
		goto out;

	/* it is setup but not enabled */
	if (event != NULL)
		goto out_enable;

	/* Try to register using hardware perf events */
	wd_attr = &wd_hw_attr;
	wd_attr->sample_period = hw_nmi_get_sample_period();
	event = perf_event_create_kernel_counter(wd_attr, cpu, NULL, watchdog_overflow_callback);
	if (!IS_ERR(event)) {
		printk(KERN_INFO "NMI watchdog enabled, takes one hw-pmu counter.\n");
		goto out_save;
	}

	printk(KERN_ERR "NMI watchdog disabled for cpu%i: unable to create perf event: %ld\n",
	       cpu, PTR_ERR(event));
	return PTR_ERR(event);

	/* success path */
out_save:
	per_cpu(watchdog_ev, cpu) = event;
out_enable:
	perf_event_enable(per_cpu(watchdog_ev, cpu));
out:
	return 0;
}

static void watchdog_nmi_disable(int cpu)
{
	struct perf_event *event = per_cpu(watchdog_ev, cpu);

	if (event) {
		perf_event_disable(event);
		per_cpu(watchdog_ev, cpu) = NULL;

		/* should be in cleanup, but blocks oprofile */
		perf_event_release_kernel(event);
	}
	return;
}
#else
static int watchdog_nmi_enable(int cpu) { return 0; }
static void watchdog_nmi_disable(int cpu) { return; }
#endif /* CONFIG_HARDLOCKUP_DETECTOR */

/* prepare/enable/disable routines */
static int watchdog_prepare_cpu(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(watchdog_hrtimer, cpu);

	WARN_ON(per_cpu(softlockup_watchdog, cpu));
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = watchdog_timer_fn;

	return 0;
}

static int watchdog_enable(int cpu)
{
	struct task_struct *p = per_cpu(softlockup_watchdog, cpu);
	int err;

	/* enable the perf event */
	err = watchdog_nmi_enable(cpu);
	if (err)
		return err;

	/* create the watchdog thread */
	if (!p) {
		p = kthread_create(watchdog, (void *)(unsigned long)cpu, "watchdog/%d", cpu);
		if (IS_ERR(p)) {
			printk(KERN_ERR "softlockup watchdog for %i failed\n", cpu);
			return PTR_ERR(p);
		}
		kthread_bind(p, cpu);
		per_cpu(watchdog_touch_ts, cpu) = 0;
		per_cpu(softlockup_watchdog, cpu) = p;
		wake_up_process(p);
	}

	/* if any cpu succeeds, watchdog is considered enabled for the system */
	watchdog_enabled = 1;

	return 0;
}

static void watchdog_disable(int cpu)
{
	struct task_struct *p = per_cpu(softlockup_watchdog, cpu);
	struct hrtimer *hrtimer = &per_cpu(watchdog_hrtimer, cpu);

	/*
	 * cancel the timer first to stop incrementing the stats
	 * and waking up the kthread
	 */
	hrtimer_cancel(hrtimer);

	/* disable the perf event */
	watchdog_nmi_disable(cpu);

	/* stop the watchdog thread */
	if (p) {
		per_cpu(softlockup_watchdog, cpu) = NULL;
		kthread_stop(p);
	}
}

static void watchdog_enable_all_cpus(void)
{
	int cpu;
	int result = 0;

	for_each_online_cpu(cpu)
		result += watchdog_enable(cpu);

	if (result)
		printk(KERN_ERR "watchdog: failed to be enabled on some cpus\n");

}

static void watchdog_disable_all_cpus(void)
{
	int cpu;

	if (no_watchdog)
		return;

	for_each_online_cpu(cpu)
		watchdog_disable(cpu);

	/* if all watchdogs are disabled, then they are disabled for the system */
	watchdog_enabled = 0;
}


/* sysctl functions */
#ifdef CONFIG_SYSCTL
/*
 * proc handler for /proc/sys/kernel/nmi_watchdog
 */

int proc_dowatchdog_enabled(struct ctl_table *table, int write,
		     void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec(table, write, buffer, length, ppos);

	if (watchdog_enabled)
		watchdog_enable_all_cpus();
	else
		watchdog_disable_all_cpus();
	return 0;
}

int proc_dowatchdog_thresh(struct ctl_table *table, int write,
			     void __user *buffer,
			     size_t *lenp, loff_t *ppos)
{
	return proc_dointvec_minmax(table, write, buffer, lenp, ppos);
}
#endif /* CONFIG_SYSCTL */


/*
 * Create/destroy watchdog threads as CPUs come and go:
 */
static int __cpuinit
cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;
	int err = 0;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		err = watchdog_prepare_cpu(hotcpu);
		break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		err = watchdog_enable(hotcpu);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		watchdog_disable(hotcpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		watchdog_disable(hotcpu);
		break;
#endif /* CONFIG_HOTPLUG_CPU */
	}
	return notifier_from_errno(err);
}

static struct notifier_block __cpuinitdata cpu_nfb = {
	.notifier_call = cpu_callback
};

void __init lockup_detector_init(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int err;

	if (no_watchdog)
		return;

	err = cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);
	WARN_ON(notifier_to_errno(err));

	cpu_callback(&cpu_nfb, CPU_ONLINE, cpu);
	register_cpu_notifier(&cpu_nfb);

	return;
}
