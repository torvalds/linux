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

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/kernel_stat.h>
#include <linux/kvm_para.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/stop_machine.h>
#include <linux/sysctl.h>
#include <linux/tick.h>

#include <linux/sched/clock.h>
#include <linux/sched/debug.h>
#include <linux/sched/isolation.h>

#include <asm/irq_regs.h>

static DEFINE_MUTEX(watchdog_mutex);

#if defined(CONFIG_HARDLOCKUP_DETECTOR) || defined(CONFIG_HARDLOCKUP_DETECTOR_SPARC64)
# define WATCHDOG_HARDLOCKUP_DEFAULT	1
#else
# define WATCHDOG_HARDLOCKUP_DEFAULT	0
#endif

#define NUM_SAMPLE_PERIODS	5

unsigned long __read_mostly watchdog_enabled;
int __read_mostly watchdog_user_enabled = 1;
static int __read_mostly watchdog_hardlockup_user_enabled = WATCHDOG_HARDLOCKUP_DEFAULT;
static int __read_mostly watchdog_softlockup_user_enabled = 1;
int __read_mostly watchdog_thresh = 10;
static int __read_mostly watchdog_hardlockup_available;

struct cpumask watchdog_cpumask __read_mostly;
unsigned long *watchdog_cpumask_bits = cpumask_bits(&watchdog_cpumask);

#ifdef CONFIG_HARDLOCKUP_DETECTOR

# ifdef CONFIG_SMP
int __read_mostly sysctl_hardlockup_all_cpu_backtrace;
# endif /* CONFIG_SMP */

/*
 * Should we panic when a soft-lockup or hard-lockup occurs:
 */
unsigned int __read_mostly hardlockup_panic =
			IS_ENABLED(CONFIG_BOOTPARAM_HARDLOCKUP_PANIC);
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
	watchdog_hardlockup_user_enabled = 0;
}

static int __init hardlockup_panic_setup(char *str)
{
next:
	if (!strncmp(str, "panic", 5))
		hardlockup_panic = 1;
	else if (!strncmp(str, "nopanic", 7))
		hardlockup_panic = 0;
	else if (!strncmp(str, "0", 1))
		watchdog_hardlockup_user_enabled = 0;
	else if (!strncmp(str, "1", 1))
		watchdog_hardlockup_user_enabled = 1;
	else if (!strncmp(str, "r", 1))
		hardlockup_config_perf_event(str + 1);
	while (*(str++)) {
		if (*str == ',') {
			str++;
			goto next;
		}
	}
	return 1;
}
__setup("nmi_watchdog=", hardlockup_panic_setup);

#endif /* CONFIG_HARDLOCKUP_DETECTOR */

#if defined(CONFIG_HARDLOCKUP_DETECTOR_COUNTS_HRTIMER)

static DEFINE_PER_CPU(atomic_t, hrtimer_interrupts);
static DEFINE_PER_CPU(int, hrtimer_interrupts_saved);
static DEFINE_PER_CPU(bool, watchdog_hardlockup_warned);
static DEFINE_PER_CPU(bool, watchdog_hardlockup_touched);
static unsigned long hard_lockup_nmi_warn;

notrace void arch_touch_nmi_watchdog(void)
{
	/*
	 * Using __raw here because some code paths have
	 * preemption enabled.  If preemption is enabled
	 * then interrupts should be enabled too, in which
	 * case we shouldn't have to worry about the watchdog
	 * going off.
	 */
	raw_cpu_write(watchdog_hardlockup_touched, true);
}
EXPORT_SYMBOL(arch_touch_nmi_watchdog);

void watchdog_hardlockup_touch_cpu(unsigned int cpu)
{
	per_cpu(watchdog_hardlockup_touched, cpu) = true;
}

static bool is_hardlockup(unsigned int cpu)
{
	int hrint = atomic_read(&per_cpu(hrtimer_interrupts, cpu));

	if (per_cpu(hrtimer_interrupts_saved, cpu) == hrint)
		return true;

	/*
	 * NOTE: we don't need any fancy atomic_t or READ_ONCE/WRITE_ONCE
	 * for hrtimer_interrupts_saved. hrtimer_interrupts_saved is
	 * written/read by a single CPU.
	 */
	per_cpu(hrtimer_interrupts_saved, cpu) = hrint;

	return false;
}

static void watchdog_hardlockup_kick(void)
{
	int new_interrupts;

	new_interrupts = atomic_inc_return(this_cpu_ptr(&hrtimer_interrupts));
	watchdog_buddy_check_hardlockup(new_interrupts);
}

void watchdog_hardlockup_check(unsigned int cpu, struct pt_regs *regs)
{
	if (per_cpu(watchdog_hardlockup_touched, cpu)) {
		per_cpu(watchdog_hardlockup_touched, cpu) = false;
		return;
	}

	/*
	 * Check for a hardlockup by making sure the CPU's timer
	 * interrupt is incrementing. The timer interrupt should have
	 * fired multiple times before we overflow'd. If it hasn't
	 * then this is a good indication the cpu is stuck
	 */
	if (is_hardlockup(cpu)) {
		unsigned int this_cpu = smp_processor_id();
		unsigned long flags;

		/* Only print hardlockups once. */
		if (per_cpu(watchdog_hardlockup_warned, cpu))
			return;

		/*
		 * Prevent multiple hard-lockup reports if one cpu is already
		 * engaged in dumping all cpu back traces.
		 */
		if (sysctl_hardlockup_all_cpu_backtrace) {
			if (test_and_set_bit_lock(0, &hard_lockup_nmi_warn))
				return;
		}

		/*
		 * NOTE: we call printk_cpu_sync_get_irqsave() after printing
		 * the lockup message. While it would be nice to serialize
		 * that printout, we really want to make sure that if some
		 * other CPU somehow locked up while holding the lock associated
		 * with printk_cpu_sync_get_irqsave() that we can still at least
		 * get the message about the lockup out.
		 */
		pr_emerg("CPU%u: Watchdog detected hard LOCKUP on cpu %u\n", this_cpu, cpu);
		printk_cpu_sync_get_irqsave(flags);

		print_modules();
		print_irqtrace_events(current);
		if (cpu == this_cpu) {
			if (regs)
				show_regs(regs);
			else
				dump_stack();
			printk_cpu_sync_put_irqrestore(flags);
		} else {
			printk_cpu_sync_put_irqrestore(flags);
			trigger_single_cpu_backtrace(cpu);
		}

		if (sysctl_hardlockup_all_cpu_backtrace) {
			trigger_allbutcpu_cpu_backtrace(cpu);
			if (!hardlockup_panic)
				clear_bit_unlock(0, &hard_lockup_nmi_warn);
		}

		if (hardlockup_panic)
			nmi_panic(regs, "Hard LOCKUP");

		per_cpu(watchdog_hardlockup_warned, cpu) = true;
	} else {
		per_cpu(watchdog_hardlockup_warned, cpu) = false;
	}
}

#else /* CONFIG_HARDLOCKUP_DETECTOR_COUNTS_HRTIMER */

static inline void watchdog_hardlockup_kick(void) { }

#endif /* !CONFIG_HARDLOCKUP_DETECTOR_COUNTS_HRTIMER */

/*
 * These functions can be overridden based on the configured hardlockdup detector.
 *
 * watchdog_hardlockup_enable/disable can be implemented to start and stop when
 * softlockup watchdog start and stop. The detector must select the
 * SOFTLOCKUP_DETECTOR Kconfig.
 */
void __weak watchdog_hardlockup_enable(unsigned int cpu) { }

void __weak watchdog_hardlockup_disable(unsigned int cpu) { }

/*
 * Watchdog-detector specific API.
 *
 * Return 0 when hardlockup watchdog is available, negative value otherwise.
 * Note that the negative value means that a delayed probe might
 * succeed later.
 */
int __weak __init watchdog_hardlockup_probe(void)
{
	return -ENODEV;
}

/**
 * watchdog_hardlockup_stop - Stop the watchdog for reconfiguration
 *
 * The reconfiguration steps are:
 * watchdog_hardlockup_stop();
 * update_variables();
 * watchdog_hardlockup_start();
 */
void __weak watchdog_hardlockup_stop(void) { }

/**
 * watchdog_hardlockup_start - Start the watchdog after reconfiguration
 *
 * Counterpart to watchdog_hardlockup_stop().
 *
 * The following variables have been updated in update_variables() and
 * contain the currently valid configuration:
 * - watchdog_enabled
 * - watchdog_thresh
 * - watchdog_cpumask
 */
void __weak watchdog_hardlockup_start(void) { }

/**
 * lockup_detector_update_enable - Update the sysctl enable bit
 *
 * Caller needs to make sure that the hard watchdogs are off, so this
 * can't race with watchdog_hardlockup_disable().
 */
static void lockup_detector_update_enable(void)
{
	watchdog_enabled = 0;
	if (!watchdog_user_enabled)
		return;
	if (watchdog_hardlockup_available && watchdog_hardlockup_user_enabled)
		watchdog_enabled |= WATCHDOG_HARDLOCKUP_ENABLED;
	if (watchdog_softlockup_user_enabled)
		watchdog_enabled |= WATCHDOG_SOFTOCKUP_ENABLED;
}

#ifdef CONFIG_SOFTLOCKUP_DETECTOR

/*
 * Delay the soflockup report when running a known slow code.
 * It does _not_ affect the timestamp of the last successdul reschedule.
 */
#define SOFTLOCKUP_DELAY_REPORT	ULONG_MAX

#ifdef CONFIG_SMP
int __read_mostly sysctl_softlockup_all_cpu_backtrace;
#endif

static struct cpumask watchdog_allowed_mask __read_mostly;

/* Global variables, exported for sysctl */
unsigned int __read_mostly softlockup_panic =
			IS_ENABLED(CONFIG_BOOTPARAM_SOFTLOCKUP_PANIC);

static bool softlockup_initialized __read_mostly;
static u64 __read_mostly sample_period;

/* Timestamp taken after the last successful reschedule. */
static DEFINE_PER_CPU(unsigned long, watchdog_touch_ts);
/* Timestamp of the last softlockup report. */
static DEFINE_PER_CPU(unsigned long, watchdog_report_ts);
static DEFINE_PER_CPU(struct hrtimer, watchdog_hrtimer);
static DEFINE_PER_CPU(bool, softlockup_touch_sync);
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
	watchdog_softlockup_user_enabled = 0;
	return 1;
}
__setup("nosoftlockup", nosoftlockup_setup);

static int __init watchdog_thresh_setup(char *str)
{
	get_option(&str, &watchdog_thresh);
	return 1;
}
__setup("watchdog_thresh=", watchdog_thresh_setup);

static void __lockup_detector_cleanup(void);

#ifdef CONFIG_SOFTLOCKUP_DETECTOR_INTR_STORM
enum stats_per_group {
	STATS_SYSTEM,
	STATS_SOFTIRQ,
	STATS_HARDIRQ,
	STATS_IDLE,
	NUM_STATS_PER_GROUP,
};

static const enum cpu_usage_stat tracked_stats[NUM_STATS_PER_GROUP] = {
	CPUTIME_SYSTEM,
	CPUTIME_SOFTIRQ,
	CPUTIME_IRQ,
	CPUTIME_IDLE,
};

static DEFINE_PER_CPU(u16, cpustat_old[NUM_STATS_PER_GROUP]);
static DEFINE_PER_CPU(u8, cpustat_util[NUM_SAMPLE_PERIODS][NUM_STATS_PER_GROUP]);
static DEFINE_PER_CPU(u8, cpustat_tail);

/*
 * We don't need nanosecond resolution. A granularity of 16ms is
 * sufficient for our precision, allowing us to use u16 to store
 * cpustats, which will roll over roughly every ~1000 seconds.
 * 2^24 ~= 16 * 10^6
 */
static u16 get_16bit_precision(u64 data_ns)
{
	return data_ns >> 24LL; /* 2^24ns ~= 16.8ms */
}

static void update_cpustat(void)
{
	int i;
	u8 util;
	u16 old_stat, new_stat;
	struct kernel_cpustat kcpustat;
	u64 *cpustat = kcpustat.cpustat;
	u8 tail = __this_cpu_read(cpustat_tail);
	u16 sample_period_16 = get_16bit_precision(sample_period);

	kcpustat_cpu_fetch(&kcpustat, smp_processor_id());

	for (i = 0; i < NUM_STATS_PER_GROUP; i++) {
		old_stat = __this_cpu_read(cpustat_old[i]);
		new_stat = get_16bit_precision(cpustat[tracked_stats[i]]);
		util = DIV_ROUND_UP(100 * (new_stat - old_stat), sample_period_16);
		__this_cpu_write(cpustat_util[tail][i], util);
		__this_cpu_write(cpustat_old[i], new_stat);
	}

	__this_cpu_write(cpustat_tail, (tail + 1) % NUM_SAMPLE_PERIODS);
}

static void print_cpustat(void)
{
	int i, group;
	u8 tail = __this_cpu_read(cpustat_tail);
	u64 sample_period_second = sample_period;

	do_div(sample_period_second, NSEC_PER_SEC);

	/*
	 * Outputting the "watchdog" prefix on every line is redundant and not
	 * concise, and the original alarm information is sufficient for
	 * positioning in logs, hence here printk() is used instead of pr_crit().
	 */
	printk(KERN_CRIT "CPU#%d Utilization every %llus during lockup:\n",
	       smp_processor_id(), sample_period_second);

	for (i = 0; i < NUM_SAMPLE_PERIODS; i++) {
		group = (tail + i) % NUM_SAMPLE_PERIODS;
		printk(KERN_CRIT "\t#%d: %3u%% system,\t%3u%% softirq,\t"
			"%3u%% hardirq,\t%3u%% idle\n", i + 1,
			__this_cpu_read(cpustat_util[group][STATS_SYSTEM]),
			__this_cpu_read(cpustat_util[group][STATS_SOFTIRQ]),
			__this_cpu_read(cpustat_util[group][STATS_HARDIRQ]),
			__this_cpu_read(cpustat_util[group][STATS_IDLE]));
	}
}

#define HARDIRQ_PERCENT_THRESH          50
#define NUM_HARDIRQ_REPORT              5
struct irq_counts {
	int irq;
	u32 counts;
};

static DEFINE_PER_CPU(bool, snapshot_taken);

/* Tabulate the most frequent interrupts. */
static void tabulate_irq_count(struct irq_counts *irq_counts, int irq, u32 counts, int rank)
{
	int i;
	struct irq_counts new_count = {irq, counts};

	for (i = 0; i < rank; i++) {
		if (counts > irq_counts[i].counts)
			swap(new_count, irq_counts[i]);
	}
}

/*
 * If the hardirq time exceeds HARDIRQ_PERCENT_THRESH% of the sample_period,
 * then the cause of softlockup might be interrupt storm. In this case, it
 * would be useful to start interrupt counting.
 */
static bool need_counting_irqs(void)
{
	u8 util;
	int tail = __this_cpu_read(cpustat_tail);

	tail = (tail + NUM_HARDIRQ_REPORT - 1) % NUM_HARDIRQ_REPORT;
	util = __this_cpu_read(cpustat_util[tail][STATS_HARDIRQ]);
	return util > HARDIRQ_PERCENT_THRESH;
}

static void start_counting_irqs(void)
{
	if (!__this_cpu_read(snapshot_taken)) {
		kstat_snapshot_irqs();
		__this_cpu_write(snapshot_taken, true);
	}
}

static void stop_counting_irqs(void)
{
	__this_cpu_write(snapshot_taken, false);
}

static void print_irq_counts(void)
{
	unsigned int i, count;
	struct irq_counts irq_counts_sorted[NUM_HARDIRQ_REPORT] = {
		{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}
	};

	if (__this_cpu_read(snapshot_taken)) {
		for_each_active_irq(i) {
			count = kstat_get_irq_since_snapshot(i);
			tabulate_irq_count(irq_counts_sorted, i, count, NUM_HARDIRQ_REPORT);
		}

		/*
		 * Outputting the "watchdog" prefix on every line is redundant and not
		 * concise, and the original alarm information is sufficient for
		 * positioning in logs, hence here printk() is used instead of pr_crit().
		 */
		printk(KERN_CRIT "CPU#%d Detect HardIRQ Time exceeds %d%%. Most frequent HardIRQs:\n",
		       smp_processor_id(), HARDIRQ_PERCENT_THRESH);

		for (i = 0; i < NUM_HARDIRQ_REPORT; i++) {
			if (irq_counts_sorted[i].irq == -1)
				break;

			printk(KERN_CRIT "\t#%u: %-10u\tirq#%d\n",
			       i + 1, irq_counts_sorted[i].counts,
			       irq_counts_sorted[i].irq);
		}

		/*
		 * If the hardirq time is less than HARDIRQ_PERCENT_THRESH% in the last
		 * sample_period, then we suspect the interrupt storm might be subsiding.
		 */
		if (!need_counting_irqs())
			stop_counting_irqs();
	}
}

static void report_cpu_status(void)
{
	print_cpustat();
	print_irq_counts();
}
#else
static inline void update_cpustat(void) { }
static inline void report_cpu_status(void) { }
static inline bool need_counting_irqs(void) { return false; }
static inline void start_counting_irqs(void) { }
static inline void stop_counting_irqs(void) { }
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
	sample_period = get_softlockup_thresh() * ((u64)NSEC_PER_SEC / NUM_SAMPLE_PERIODS);
	watchdog_update_hrtimer_threshold(sample_period);
}

static void update_report_ts(void)
{
	__this_cpu_write(watchdog_report_ts, get_timestamp());
}

/* Commands for resetting the watchdog */
static void update_touch_ts(void)
{
	__this_cpu_write(watchdog_touch_ts, get_timestamp());
	update_report_ts();
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
	 * Preemption can be enabled.  It doesn't matter which CPU's watchdog
	 * report period gets restarted here, so use the raw_ operation.
	 */
	raw_cpu_write(watchdog_report_ts, SOFTLOCKUP_DELAY_REPORT);
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
	for_each_cpu(cpu, &watchdog_allowed_mask) {
		per_cpu(watchdog_report_ts, cpu) = SOFTLOCKUP_DELAY_REPORT;
		wq_watchdog_touch(cpu);
	}
}

void touch_softlockup_watchdog_sync(void)
{
	__this_cpu_write(softlockup_touch_sync, true);
	__this_cpu_write(watchdog_report_ts, SOFTLOCKUP_DELAY_REPORT);
}

static int is_softlockup(unsigned long touch_ts,
			 unsigned long period_ts,
			 unsigned long now)
{
	if ((watchdog_enabled & WATCHDOG_SOFTOCKUP_ENABLED) && watchdog_thresh) {
		/*
		 * If period_ts has not been updated during a sample_period, then
		 * in the subsequent few sample_periods, period_ts might also not
		 * be updated, which could indicate a potential softlockup. In
		 * this case, if we suspect the cause of the potential softlockup
		 * might be interrupt storm, then we need to count the interrupts
		 * to find which interrupt is storming.
		 */
		if (time_after_eq(now, period_ts + get_softlockup_thresh() / NUM_SAMPLE_PERIODS) &&
		    need_counting_irqs())
			start_counting_irqs();

		/*
		 * A poorly behaving BPF scheduler can live-lock the system into
		 * soft lockups. Tell sched_ext to try ejecting the BPF
		 * scheduler when close to a soft lockup.
		 */
		if (time_after_eq(now, period_ts + get_softlockup_thresh() * 3 / 4))
			scx_softlockup(now - touch_ts);

		/* Warn about unreasonable delays. */
		if (time_after(now, period_ts + get_softlockup_thresh()))
			return now - touch_ts;
	}
	return 0;
}

/* watchdog detector functions */
static DEFINE_PER_CPU(struct completion, softlockup_completion);
static DEFINE_PER_CPU(struct cpu_stop_work, softlockup_stop_work);

/*
 * The watchdog feed function - touches the timestamp.
 *
 * It only runs once every sample_period seconds (4 seconds by
 * default) to reset the softlockup timestamp. If this gets delayed
 * for more than 2*watchdog_thresh seconds then the debug-printout
 * triggers in watchdog_timer_fn().
 */
static int softlockup_fn(void *data)
{
	update_touch_ts();
	stop_counting_irqs();
	complete(this_cpu_ptr(&softlockup_completion));

	return 0;
}

/* watchdog kicker functions */
static enum hrtimer_restart watchdog_timer_fn(struct hrtimer *hrtimer)
{
	unsigned long touch_ts, period_ts, now;
	struct pt_regs *regs = get_irq_regs();
	int duration;
	int softlockup_all_cpu_backtrace = sysctl_softlockup_all_cpu_backtrace;
	unsigned long flags;

	if (!watchdog_enabled)
		return HRTIMER_NORESTART;

	watchdog_hardlockup_kick();

	/* kick the softlockup detector */
	if (completion_done(this_cpu_ptr(&softlockup_completion))) {
		reinit_completion(this_cpu_ptr(&softlockup_completion));
		stop_one_cpu_nowait(smp_processor_id(),
				softlockup_fn, NULL,
				this_cpu_ptr(&softlockup_stop_work));
	}

	/* .. and repeat */
	hrtimer_forward_now(hrtimer, ns_to_ktime(sample_period));

	/*
	 * Read the current timestamp first. It might become invalid anytime
	 * when a virtual machine is stopped by the host or when the watchog
	 * is touched from NMI.
	 */
	now = get_timestamp();
	/*
	 * If a virtual machine is stopped by the host it can look to
	 * the watchdog like a soft lockup. This function touches the watchdog.
	 */
	kvm_check_and_clear_guest_paused();
	/*
	 * The stored timestamp is comparable with @now only when not touched.
	 * It might get touched anytime from NMI. Make sure that is_softlockup()
	 * uses the same (valid) value.
	 */
	period_ts = READ_ONCE(*this_cpu_ptr(&watchdog_report_ts));

	update_cpustat();

	/* Reset the interval when touched by known problematic code. */
	if (period_ts == SOFTLOCKUP_DELAY_REPORT) {
		if (unlikely(__this_cpu_read(softlockup_touch_sync))) {
			/*
			 * If the time stamp was touched atomically
			 * make sure the scheduler tick is up to date.
			 */
			__this_cpu_write(softlockup_touch_sync, false);
			sched_clock_tick();
		}

		update_report_ts();
		return HRTIMER_RESTART;
	}

	/* Check for a softlockup. */
	touch_ts = __this_cpu_read(watchdog_touch_ts);
	duration = is_softlockup(touch_ts, period_ts, now);
	if (unlikely(duration)) {
		/*
		 * Prevent multiple soft-lockup reports if one cpu is already
		 * engaged in dumping all cpu back traces.
		 */
		if (softlockup_all_cpu_backtrace) {
			if (test_and_set_bit_lock(0, &soft_lockup_nmi_warn))
				return HRTIMER_RESTART;
		}

		/* Start period for the next softlockup warning. */
		update_report_ts();

		printk_cpu_sync_get_irqsave(flags);
		pr_emerg("BUG: soft lockup - CPU#%d stuck for %us! [%s:%d]\n",
			smp_processor_id(), duration,
			current->comm, task_pid_nr(current));
		report_cpu_status();
		print_modules();
		print_irqtrace_events(current);
		if (regs)
			show_regs(regs);
		else
			dump_stack();
		printk_cpu_sync_put_irqrestore(flags);

		if (softlockup_all_cpu_backtrace) {
			trigger_allbutcpu_cpu_backtrace(smp_processor_id());
			if (!softlockup_panic)
				clear_bit_unlock(0, &soft_lockup_nmi_warn);
		}

		add_taint(TAINT_SOFTLOCKUP, LOCKDEP_STILL_OK);
		if (softlockup_panic)
			panic("softlockup: hung tasks");
	}

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
	 * Start the timer first to prevent the hardlockup watchdog triggering
	 * before the timer has a chance to fire.
	 */
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_HARD);
	hrtimer->function = watchdog_timer_fn;
	hrtimer_start(hrtimer, ns_to_ktime(sample_period),
		      HRTIMER_MODE_REL_PINNED_HARD);

	/* Initialize timestamp */
	update_touch_ts();
	/* Enable the hardlockup detector */
	if (watchdog_enabled & WATCHDOG_HARDLOCKUP_ENABLED)
		watchdog_hardlockup_enable(cpu);
}

static void watchdog_disable(unsigned int cpu)
{
	struct hrtimer *hrtimer = this_cpu_ptr(&watchdog_hrtimer);

	WARN_ON_ONCE(cpu != smp_processor_id());

	/*
	 * Disable the hardlockup detector first. That prevents that a large
	 * delay between disabling the timer and disabling the hardlockup
	 * detector causes a false positive.
	 */
	watchdog_hardlockup_disable(cpu);
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

static void __lockup_detector_reconfigure(void)
{
	cpus_read_lock();
	watchdog_hardlockup_stop();

	softlockup_stop_all();
	set_sample_period();
	lockup_detector_update_enable();
	if (watchdog_enabled && watchdog_thresh)
		softlockup_start_all();

	watchdog_hardlockup_start();
	cpus_read_unlock();
	/*
	 * Must be called outside the cpus locked section to prevent
	 * recursive locking in the perf code.
	 */
	__lockup_detector_cleanup();
}

void lockup_detector_reconfigure(void)
{
	mutex_lock(&watchdog_mutex);
	__lockup_detector_reconfigure();
	mutex_unlock(&watchdog_mutex);
}

/*
 * Create the watchdog infrastructure and configure the detector(s).
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
	__lockup_detector_reconfigure();
	softlockup_initialized = true;
	mutex_unlock(&watchdog_mutex);
}

#else /* CONFIG_SOFTLOCKUP_DETECTOR */
static void __lockup_detector_reconfigure(void)
{
	cpus_read_lock();
	watchdog_hardlockup_stop();
	lockup_detector_update_enable();
	watchdog_hardlockup_start();
	cpus_read_unlock();
}
void lockup_detector_reconfigure(void)
{
	__lockup_detector_reconfigure();
}
static inline void lockup_detector_setup(void)
{
	__lockup_detector_reconfigure();
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

/* Propagate any changes to the watchdog infrastructure */
static void proc_watchdog_update(void)
{
	/* Remove impossible cpus to keep sysctl output clean. */
	cpumask_and(&watchdog_cpumask, &watchdog_cpumask, cpu_possible_mask);
	__lockup_detector_reconfigure();
}

/*
 * common function for watchdog, nmi_watchdog and soft_watchdog parameter
 *
 * caller             | table->data points to            | 'which'
 * -------------------|----------------------------------|-------------------------------
 * proc_watchdog      | watchdog_user_enabled            | WATCHDOG_HARDLOCKUP_ENABLED |
 *                    |                                  | WATCHDOG_SOFTOCKUP_ENABLED
 * -------------------|----------------------------------|-------------------------------
 * proc_nmi_watchdog  | watchdog_hardlockup_user_enabled | WATCHDOG_HARDLOCKUP_ENABLED
 * -------------------|----------------------------------|-------------------------------
 * proc_soft_watchdog | watchdog_softlockup_user_enabled | WATCHDOG_SOFTOCKUP_ENABLED
 */
static int proc_watchdog_common(int which, const struct ctl_table *table, int write,
				void *buffer, size_t *lenp, loff_t *ppos)
{
	int err, old, *param = table->data;

	mutex_lock(&watchdog_mutex);

	old = *param;
	if (!write) {
		/*
		 * On read synchronize the userspace interface. This is a
		 * racy snapshot.
		 */
		*param = (watchdog_enabled & which) != 0;
		err = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
		*param = old;
	} else {
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
static int proc_watchdog(const struct ctl_table *table, int write,
			 void *buffer, size_t *lenp, loff_t *ppos)
{
	return proc_watchdog_common(WATCHDOG_HARDLOCKUP_ENABLED |
				    WATCHDOG_SOFTOCKUP_ENABLED,
				    table, write, buffer, lenp, ppos);
}

/*
 * /proc/sys/kernel/nmi_watchdog
 */
static int proc_nmi_watchdog(const struct ctl_table *table, int write,
			     void *buffer, size_t *lenp, loff_t *ppos)
{
	if (!watchdog_hardlockup_available && write)
		return -ENOTSUPP;
	return proc_watchdog_common(WATCHDOG_HARDLOCKUP_ENABLED,
				    table, write, buffer, lenp, ppos);
}

#ifdef CONFIG_SOFTLOCKUP_DETECTOR
/*
 * /proc/sys/kernel/soft_watchdog
 */
static int proc_soft_watchdog(const struct ctl_table *table, int write,
			      void *buffer, size_t *lenp, loff_t *ppos)
{
	return proc_watchdog_common(WATCHDOG_SOFTOCKUP_ENABLED,
				    table, write, buffer, lenp, ppos);
}
#endif

/*
 * /proc/sys/kernel/watchdog_thresh
 */
static int proc_watchdog_thresh(const struct ctl_table *table, int write,
				void *buffer, size_t *lenp, loff_t *ppos)
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
static int proc_watchdog_cpumask(const struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	int err;

	mutex_lock(&watchdog_mutex);

	err = proc_do_large_bitmap(table, write, buffer, lenp, ppos);
	if (!err && write)
		proc_watchdog_update();

	mutex_unlock(&watchdog_mutex);
	return err;
}

static const int sixty = 60;

static const struct ctl_table watchdog_sysctls[] = {
	{
		.procname       = "watchdog",
		.data		= &watchdog_user_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler   = proc_watchdog,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "watchdog_thresh",
		.data		= &watchdog_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_watchdog_thresh,
		.extra1		= SYSCTL_ZERO,
		.extra2		= (void *)&sixty,
	},
	{
		.procname	= "watchdog_cpumask",
		.data		= &watchdog_cpumask_bits,
		.maxlen		= NR_CPUS,
		.mode		= 0644,
		.proc_handler	= proc_watchdog_cpumask,
	},
#ifdef CONFIG_SOFTLOCKUP_DETECTOR
	{
		.procname       = "soft_watchdog",
		.data		= &watchdog_softlockup_user_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler   = proc_soft_watchdog,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "softlockup_panic",
		.data		= &softlockup_panic,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
#ifdef CONFIG_SMP
	{
		.procname	= "softlockup_all_cpu_backtrace",
		.data		= &sysctl_softlockup_all_cpu_backtrace,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
#endif /* CONFIG_SMP */
#endif
#ifdef CONFIG_HARDLOCKUP_DETECTOR
	{
		.procname	= "hardlockup_panic",
		.data		= &hardlockup_panic,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
#ifdef CONFIG_SMP
	{
		.procname	= "hardlockup_all_cpu_backtrace",
		.data		= &sysctl_hardlockup_all_cpu_backtrace,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
#endif /* CONFIG_SMP */
#endif
};

static struct ctl_table watchdog_hardlockup_sysctl[] = {
	{
		.procname       = "nmi_watchdog",
		.data		= &watchdog_hardlockup_user_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler   = proc_nmi_watchdog,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
};

static void __init watchdog_sysctl_init(void)
{
	register_sysctl_init("kernel", watchdog_sysctls);

	if (watchdog_hardlockup_available)
		watchdog_hardlockup_sysctl[0].mode = 0644;
	register_sysctl_init("kernel", watchdog_hardlockup_sysctl);
}

#else
#define watchdog_sysctl_init() do { } while (0)
#endif /* CONFIG_SYSCTL */

static void __init lockup_detector_delay_init(struct work_struct *work);
static bool allow_lockup_detector_init_retry __initdata;

static struct work_struct detector_work __initdata =
		__WORK_INITIALIZER(detector_work, lockup_detector_delay_init);

static void __init lockup_detector_delay_init(struct work_struct *work)
{
	int ret;

	ret = watchdog_hardlockup_probe();
	if (ret) {
		if (ret == -ENODEV)
			pr_info("NMI not fully supported\n");
		else
			pr_info("Delayed init of the lockup detector failed: %d\n", ret);
		pr_info("Hard watchdog permanently disabled\n");
		return;
	}

	allow_lockup_detector_init_retry = false;

	watchdog_hardlockup_available = true;
	lockup_detector_setup();
}

/*
 * lockup_detector_retry_init - retry init lockup detector if possible.
 *
 * Retry hardlockup detector init. It is useful when it requires some
 * functionality that has to be initialized later on a particular
 * platform.
 */
void __init lockup_detector_retry_init(void)
{
	/* Must be called before late init calls */
	if (!allow_lockup_detector_init_retry)
		return;

	schedule_work(&detector_work);
}

/*
 * Ensure that optional delayed hardlockup init is proceed before
 * the init code and memory is freed.
 */
static int __init lockup_detector_check(void)
{
	/* Prevent any later retry. */
	allow_lockup_detector_init_retry = false;

	/* Make sure no work is pending. */
	flush_work(&detector_work);

	watchdog_sysctl_init();

	return 0;

}
late_initcall_sync(lockup_detector_check);

void __init lockup_detector_init(void)
{
	if (tick_nohz_full_enabled())
		pr_info("Disabling watchdog on nohz_full cores by default\n");

	cpumask_copy(&watchdog_cpumask,
		     housekeeping_cpumask(HK_TYPE_TIMER));

	if (!watchdog_hardlockup_probe())
		watchdog_hardlockup_available = true;
	else
		allow_lockup_detector_init_retry = true;

	lockup_detector_setup();
}
