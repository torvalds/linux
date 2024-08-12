// SPDX-License-Identifier: GPL-2.0
/*
 * Detect hard lockups on a system using perf
 *
 * started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 * Note: Most of this code is borrowed heavily from the original softlockup
 * detector, so thanks to Ingo for the initial implementation.
 * Some chunks also taken from the old x86-specific nmi watchdog code, thanks
 * to those contributors as well.
 */

#define pr_fmt(fmt) "NMI watchdog: " fmt

#include <linux/nmi.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/sched/debug.h>

#include <asm/irq_regs.h>
#include <linux/perf_event.h>

static DEFINE_PER_CPU(struct perf_event *, watchdog_ev);
static DEFINE_PER_CPU(struct perf_event *, dead_event);
static struct cpumask dead_events_mask;

static atomic_t watchdog_cpus = ATOMIC_INIT(0);

#ifdef CONFIG_HARDLOCKUP_CHECK_TIMESTAMP
static DEFINE_PER_CPU(ktime_t, last_timestamp);
static DEFINE_PER_CPU(unsigned int, nmi_rearmed);
static ktime_t watchdog_hrtimer_sample_threshold __read_mostly;

void watchdog_update_hrtimer_threshold(u64 period)
{
	/*
	 * The hrtimer runs with a period of (watchdog_threshold * 2) / 5
	 *
	 * So it runs effectively with 2.5 times the rate of the NMI
	 * watchdog. That means the hrtimer should fire 2-3 times before
	 * the NMI watchdog expires. The NMI watchdog on x86 is based on
	 * unhalted CPU cycles, so if Turbo-Mode is enabled the CPU cycles
	 * might run way faster than expected and the NMI fires in a
	 * smaller period than the one deduced from the nominal CPU
	 * frequency. Depending on the Turbo-Mode factor this might be fast
	 * enough to get the NMI period smaller than the hrtimer watchdog
	 * period and trigger false positives.
	 *
	 * The sample threshold is used to check in the NMI handler whether
	 * the minimum time between two NMI samples has elapsed. That
	 * prevents false positives.
	 *
	 * Set this to 4/5 of the actual watchdog threshold period so the
	 * hrtimer is guaranteed to fire at least once within the real
	 * watchdog threshold.
	 */
	watchdog_hrtimer_sample_threshold = period * 2;
}

static bool watchdog_check_timestamp(void)
{
	ktime_t delta, now = ktime_get_mono_fast_ns();

	delta = now - __this_cpu_read(last_timestamp);
	if (delta < watchdog_hrtimer_sample_threshold) {
		/*
		 * If ktime is jiffies based, a stalled timer would prevent
		 * jiffies from being incremented and the filter would look
		 * at a stale timestamp and never trigger.
		 */
		if (__this_cpu_inc_return(nmi_rearmed) < 10)
			return false;
	}
	__this_cpu_write(nmi_rearmed, 0);
	__this_cpu_write(last_timestamp, now);
	return true;
}

static void watchdog_init_timestamp(void)
{
	__this_cpu_write(nmi_rearmed, 0);
	__this_cpu_write(last_timestamp, ktime_get_mono_fast_ns());
}
#else
static inline bool watchdog_check_timestamp(void) { return true; }
static inline void watchdog_init_timestamp(void) { }
#endif

static struct perf_event_attr wd_hw_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES,
	.size		= sizeof(struct perf_event_attr),
	.pinned		= 1,
	.disabled	= 1,
};

static struct perf_event_attr fallback_wd_hw_attr = {
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

	if (!watchdog_check_timestamp())
		return;

	watchdog_hardlockup_check(smp_processor_id(), regs);
}

static int hardlockup_detector_event_create(void)
{
	unsigned int cpu;
	struct perf_event_attr *wd_attr;
	struct perf_event *evt;

	/*
	 * Preemption is not disabled because memory will be allocated.
	 * Ensure CPU-locality by calling this in per-CPU kthread.
	 */
	WARN_ON(!is_percpu_thread());
	cpu = raw_smp_processor_id();
	wd_attr = &wd_hw_attr;
	wd_attr->sample_period = hw_nmi_get_sample_period(watchdog_thresh);

	/* Try to register using hardware perf events */
	evt = perf_event_create_kernel_counter(wd_attr, cpu, NULL,
					       watchdog_overflow_callback, NULL);
	if (IS_ERR(evt)) {
		wd_attr = &fallback_wd_hw_attr;
		wd_attr->sample_period = hw_nmi_get_sample_period(watchdog_thresh);
		evt = perf_event_create_kernel_counter(wd_attr, cpu, NULL,
						       watchdog_overflow_callback, NULL);
	}

	if (IS_ERR(evt)) {
		pr_debug("Perf event create on CPU %d failed with %ld\n", cpu,
			 PTR_ERR(evt));
		return PTR_ERR(evt);
	}
	this_cpu_write(watchdog_ev, evt);
	return 0;
}

/**
 * watchdog_hardlockup_enable - Enable the local event
 * @cpu: The CPU to enable hard lockup on.
 */
void watchdog_hardlockup_enable(unsigned int cpu)
{
	WARN_ON_ONCE(cpu != smp_processor_id());

	if (hardlockup_detector_event_create())
		return;

	/* use original value for check */
	if (!atomic_fetch_inc(&watchdog_cpus))
		pr_info("Enabled. Permanently consumes one hw-PMU counter.\n");

	watchdog_init_timestamp();
	perf_event_enable(this_cpu_read(watchdog_ev));
}

/**
 * watchdog_hardlockup_disable - Disable the local event
 * @cpu: The CPU to enable hard lockup on.
 */
void watchdog_hardlockup_disable(unsigned int cpu)
{
	struct perf_event *event = this_cpu_read(watchdog_ev);

	WARN_ON_ONCE(cpu != smp_processor_id());

	if (event) {
		perf_event_disable(event);
		this_cpu_write(watchdog_ev, NULL);
		this_cpu_write(dead_event, event);
		cpumask_set_cpu(smp_processor_id(), &dead_events_mask);
		atomic_dec(&watchdog_cpus);
	}
}

/**
 * hardlockup_detector_perf_cleanup - Cleanup disabled events and destroy them
 *
 * Called from lockup_detector_cleanup(). Serialized by the caller.
 */
void hardlockup_detector_perf_cleanup(void)
{
	int cpu;

	for_each_cpu(cpu, &dead_events_mask) {
		struct perf_event *event = per_cpu(dead_event, cpu);

		/*
		 * Required because for_each_cpu() reports  unconditionally
		 * CPU0 as set on UP kernels. Sigh.
		 */
		if (event)
			perf_event_release_kernel(event);
		per_cpu(dead_event, cpu) = NULL;
	}
	cpumask_clear(&dead_events_mask);
}

/**
 * hardlockup_detector_perf_stop - Globally stop watchdog events
 *
 * Special interface for x86 to handle the perf HT bug.
 */
void __init hardlockup_detector_perf_stop(void)
{
	int cpu;

	lockdep_assert_cpus_held();

	for_each_online_cpu(cpu) {
		struct perf_event *event = per_cpu(watchdog_ev, cpu);

		if (event)
			perf_event_disable(event);
	}
}

/**
 * hardlockup_detector_perf_restart - Globally restart watchdog events
 *
 * Special interface for x86 to handle the perf HT bug.
 */
void __init hardlockup_detector_perf_restart(void)
{
	int cpu;

	lockdep_assert_cpus_held();

	if (!(watchdog_enabled & WATCHDOG_HARDLOCKUP_ENABLED))
		return;

	for_each_online_cpu(cpu) {
		struct perf_event *event = per_cpu(watchdog_ev, cpu);

		if (event)
			perf_event_enable(event);
	}
}

bool __weak __init arch_perf_nmi_is_available(void)
{
	return true;
}

/**
 * watchdog_hardlockup_probe - Probe whether NMI event is available at all
 */
int __init watchdog_hardlockup_probe(void)
{
	int ret;

	if (!arch_perf_nmi_is_available())
		return -ENODEV;

	ret = hardlockup_detector_event_create();

	if (ret) {
		pr_info("Perf NMI watchdog permanently disabled\n");
	} else {
		perf_event_release_kernel(this_cpu_read(watchdog_ev));
		this_cpu_write(watchdog_ev, NULL);
	}
	return ret;
}

/**
 * hardlockup_config_perf_event - Overwrite config of wd_hw_attr.
 * @str: number which identifies the raw perf event to use
 */
void __init hardlockup_config_perf_event(const char *str)
{
	u64 config;
	char buf[24];
	char *comma = strchr(str, ',');

	if (!comma) {
		if (kstrtoull(str, 16, &config))
			return;
	} else {
		unsigned int len = comma - str;

		if (len >= sizeof(buf))
			return;

		if (strscpy(buf, str, sizeof(buf)) < 0)
			return;
		buf[len] = 0;
		if (kstrtoull(buf, 16, &config))
			return;
	}

	wd_hw_attr.type = PERF_TYPE_RAW;
	wd_hw_attr.config = config;
}
