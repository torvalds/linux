/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/include/linux/nmi.h
 */
#ifndef LINUX_NMI_H
#define LINUX_NMI_H

#include <linux/sched.h>
#include <asm/irq.h>

/* Arch specific watchdogs might need to share extra watchdog-related APIs. */
#if defined(CONFIG_HARDLOCKUP_DETECTOR_ARCH) || defined(CONFIG_HARDLOCKUP_DETECTOR_SPARC64)
#include <asm/nmi.h>
#endif

#ifdef CONFIG_LOCKUP_DETECTOR
void lockup_detector_init(void);
void lockup_detector_retry_init(void);
void lockup_detector_soft_poweroff(void);
void lockup_detector_cleanup(void);

extern int watchdog_user_enabled;
extern int watchdog_thresh;
extern unsigned long watchdog_enabled;

extern struct cpumask watchdog_cpumask;
extern unsigned long *watchdog_cpumask_bits;
#ifdef CONFIG_SMP
extern int sysctl_softlockup_all_cpu_backtrace;
extern int sysctl_hardlockup_all_cpu_backtrace;
#else
#define sysctl_softlockup_all_cpu_backtrace 0
#define sysctl_hardlockup_all_cpu_backtrace 0
#endif /* !CONFIG_SMP */

#else /* CONFIG_LOCKUP_DETECTOR */
static inline void lockup_detector_init(void) { }
static inline void lockup_detector_retry_init(void) { }
static inline void lockup_detector_soft_poweroff(void) { }
static inline void lockup_detector_cleanup(void) { }
#endif /* !CONFIG_LOCKUP_DETECTOR */

#ifdef CONFIG_SOFTLOCKUP_DETECTOR
extern void touch_softlockup_watchdog_sched(void);
extern void touch_softlockup_watchdog(void);
extern void touch_softlockup_watchdog_sync(void);
extern void touch_all_softlockup_watchdogs(void);
extern unsigned int  softlockup_panic;

extern int lockup_detector_online_cpu(unsigned int cpu);
extern int lockup_detector_offline_cpu(unsigned int cpu);
#else /* CONFIG_SOFTLOCKUP_DETECTOR */
static inline void touch_softlockup_watchdog_sched(void) { }
static inline void touch_softlockup_watchdog(void) { }
static inline void touch_softlockup_watchdog_sync(void) { }
static inline void touch_all_softlockup_watchdogs(void) { }

#define lockup_detector_online_cpu	NULL
#define lockup_detector_offline_cpu	NULL
#endif /* CONFIG_SOFTLOCKUP_DETECTOR */

#ifdef CONFIG_DETECT_HUNG_TASK
void reset_hung_task_detector(void);
#else
static inline void reset_hung_task_detector(void) { }
#endif

/*
 * The run state of the lockup detectors is controlled by the content of the
 * 'watchdog_enabled' variable. Each lockup detector has its dedicated bit -
 * bit 0 for the hard lockup detector and bit 1 for the soft lockup detector.
 *
 * 'watchdog_user_enabled', 'watchdog_hardlockup_user_enabled' and
 * 'watchdog_softlockup_user_enabled' are variables that are only used as an
 * 'interface' between the parameters in /proc/sys/kernel and the internal
 * state bits in 'watchdog_enabled'. The 'watchdog_thresh' variable is
 * handled differently because its value is not boolean, and the lockup
 * detectors are 'suspended' while 'watchdog_thresh' is equal zero.
 */
#define WATCHDOG_HARDLOCKUP_ENABLED_BIT  0
#define WATCHDOG_SOFTOCKUP_ENABLED_BIT   1
#define WATCHDOG_HARDLOCKUP_ENABLED     (1 << WATCHDOG_HARDLOCKUP_ENABLED_BIT)
#define WATCHDOG_SOFTOCKUP_ENABLED      (1 << WATCHDOG_SOFTOCKUP_ENABLED_BIT)

#if defined(CONFIG_HARDLOCKUP_DETECTOR)
extern void hardlockup_detector_disable(void);
extern unsigned int hardlockup_panic;
#else
static inline void hardlockup_detector_disable(void) {}
#endif

/* Sparc64 has special implemetantion that is always enabled. */
#if defined(CONFIG_HARDLOCKUP_DETECTOR) || defined(CONFIG_HARDLOCKUP_DETECTOR_SPARC64)
void arch_touch_nmi_watchdog(void);
#else
static inline void arch_touch_nmi_watchdog(void) { }
#endif

#if defined(CONFIG_HARDLOCKUP_DETECTOR_COUNTS_HRTIMER)
void watchdog_hardlockup_touch_cpu(unsigned int cpu);
void watchdog_hardlockup_check(unsigned int cpu, struct pt_regs *regs);
#endif

#if defined(CONFIG_HARDLOCKUP_DETECTOR_PERF)
extern void hardlockup_detector_perf_stop(void);
extern void hardlockup_detector_perf_restart(void);
extern void hardlockup_detector_perf_cleanup(void);
extern void hardlockup_config_perf_event(const char *str);
#else
static inline void hardlockup_detector_perf_stop(void) { }
static inline void hardlockup_detector_perf_restart(void) { }
static inline void hardlockup_detector_perf_cleanup(void) { }
static inline void hardlockup_config_perf_event(const char *str) { }
#endif

void watchdog_hardlockup_stop(void);
void watchdog_hardlockup_start(void);
int watchdog_hardlockup_probe(void);
void watchdog_hardlockup_enable(unsigned int cpu);
void watchdog_hardlockup_disable(unsigned int cpu);

void lockup_detector_reconfigure(void);

#ifdef CONFIG_HARDLOCKUP_DETECTOR_BUDDY
void watchdog_buddy_check_hardlockup(int hrtimer_interrupts);
#else
static inline void watchdog_buddy_check_hardlockup(int hrtimer_interrupts) {}
#endif

/**
 * touch_nmi_watchdog - manually reset the hardlockup watchdog timeout.
 *
 * If we support detecting hardlockups, touch_nmi_watchdog() may be
 * used to pet the watchdog (reset the timeout) - for code which
 * intentionally disables interrupts for a long time. This call is stateless.
 *
 * Though this function has "nmi" in the name, the hardlockup watchdog might
 * not be backed by NMIs. This function will likely be renamed to
 * touch_hardlockup_watchdog() in the future.
 */
static inline void touch_nmi_watchdog(void)
{
	/*
	 * Pass on to the hardlockup detector selected via CONFIG_. Note that
	 * the hardlockup detector may not be arch-specific nor using NMIs
	 * and the arch_touch_nmi_watchdog() function will likely be renamed
	 * in the future.
	 */
	arch_touch_nmi_watchdog();

	touch_softlockup_watchdog();
}

/*
 * Create trigger_all_cpu_backtrace() out of the arch-provided
 * base function. Return whether such support was available,
 * to allow calling code to fall back to some other mechanism:
 */
#ifdef arch_trigger_cpumask_backtrace
static inline bool trigger_all_cpu_backtrace(void)
{
	arch_trigger_cpumask_backtrace(cpu_online_mask, -1);
	return true;
}

static inline bool trigger_allbutcpu_cpu_backtrace(int exclude_cpu)
{
	arch_trigger_cpumask_backtrace(cpu_online_mask, exclude_cpu);
	return true;
}

static inline bool trigger_cpumask_backtrace(struct cpumask *mask)
{
	arch_trigger_cpumask_backtrace(mask, -1);
	return true;
}

static inline bool trigger_single_cpu_backtrace(int cpu)
{
	arch_trigger_cpumask_backtrace(cpumask_of(cpu), -1);
	return true;
}

/* generic implementation */
void nmi_trigger_cpumask_backtrace(const cpumask_t *mask,
				   int exclude_cpu,
				   void (*raise)(cpumask_t *mask));
bool nmi_cpu_backtrace(struct pt_regs *regs);

#else
static inline bool trigger_all_cpu_backtrace(void)
{
	return false;
}
static inline bool trigger_allbutcpu_cpu_backtrace(int exclude_cpu)
{
	return false;
}
static inline bool trigger_cpumask_backtrace(struct cpumask *mask)
{
	return false;
}
static inline bool trigger_single_cpu_backtrace(int cpu)
{
	return false;
}
#endif

#ifdef CONFIG_HARDLOCKUP_DETECTOR_PERF
u64 hw_nmi_get_sample_period(int watchdog_thresh);
bool arch_perf_nmi_is_available(void);
#endif

#if defined(CONFIG_HARDLOCKUP_CHECK_TIMESTAMP) && \
    defined(CONFIG_HARDLOCKUP_DETECTOR_PERF)
void watchdog_update_hrtimer_threshold(u64 period);
#else
static inline void watchdog_update_hrtimer_threshold(u64 period) { }
#endif

#ifdef CONFIG_HAVE_ACPI_APEI_NMI
#include <asm/nmi.h>
#endif

#ifdef CONFIG_NMI_CHECK_CPU
void nmi_backtrace_stall_snap(const struct cpumask *btp);
void nmi_backtrace_stall_check(const struct cpumask *btp);
#else
static inline void nmi_backtrace_stall_snap(const struct cpumask *btp) {}
static inline void nmi_backtrace_stall_check(const struct cpumask *btp) {}
#endif

#endif
