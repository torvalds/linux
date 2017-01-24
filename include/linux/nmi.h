/*
 *  linux/include/linux/nmi.h
 */
#ifndef LINUX_NMI_H
#define LINUX_NMI_H

#include <linux/sched.h>
#include <asm/irq.h>

/*
 * The run state of the lockup detectors is controlled by the content of the
 * 'watchdog_enabled' variable. Each lockup detector has its dedicated bit -
 * bit 0 for the hard lockup detector and bit 1 for the soft lockup detector.
 *
 * 'watchdog_user_enabled', 'nmi_watchdog_enabled' and 'soft_watchdog_enabled'
 * are variables that are only used as an 'interface' between the parameters
 * in /proc/sys/kernel and the internal state bits in 'watchdog_enabled'. The
 * 'watchdog_thresh' variable is handled differently because its value is not
 * boolean, and the lockup detectors are 'suspended' while 'watchdog_thresh'
 * is equal zero.
 */
#define NMI_WATCHDOG_ENABLED_BIT   0
#define SOFT_WATCHDOG_ENABLED_BIT  1
#define NMI_WATCHDOG_ENABLED      (1 << NMI_WATCHDOG_ENABLED_BIT)
#define SOFT_WATCHDOG_ENABLED     (1 << SOFT_WATCHDOG_ENABLED_BIT)

/**
 * touch_nmi_watchdog - restart NMI watchdog timeout.
 * 
 * If the architecture supports the NMI watchdog, touch_nmi_watchdog()
 * may be used to reset the timeout - for code which intentionally
 * disables interrupts for a long time. This call is stateless.
 */
#if defined(CONFIG_HAVE_NMI_WATCHDOG) || defined(CONFIG_HARDLOCKUP_DETECTOR)
#include <asm/nmi.h>
extern void touch_nmi_watchdog(void);
#else
static inline void touch_nmi_watchdog(void)
{
	touch_softlockup_watchdog();
}
#endif

#if defined(CONFIG_HARDLOCKUP_DETECTOR)
extern void hardlockup_detector_disable(void);
#else
static inline void hardlockup_detector_disable(void) {}
#endif

/*
 * Create trigger_all_cpu_backtrace() out of the arch-provided
 * base function. Return whether such support was available,
 * to allow calling code to fall back to some other mechanism:
 */
#ifdef arch_trigger_cpumask_backtrace
static inline bool trigger_all_cpu_backtrace(void)
{
	arch_trigger_cpumask_backtrace(cpu_online_mask, false);
	return true;
}

static inline bool trigger_allbutself_cpu_backtrace(void)
{
	arch_trigger_cpumask_backtrace(cpu_online_mask, true);
	return true;
}

static inline bool trigger_cpumask_backtrace(struct cpumask *mask)
{
	arch_trigger_cpumask_backtrace(mask, false);
	return true;
}

static inline bool trigger_single_cpu_backtrace(int cpu)
{
	arch_trigger_cpumask_backtrace(cpumask_of(cpu), false);
	return true;
}

/* generic implementation */
void nmi_trigger_cpumask_backtrace(const cpumask_t *mask,
				   bool exclude_self,
				   void (*raise)(cpumask_t *mask));
bool nmi_cpu_backtrace(struct pt_regs *regs);

#else
static inline bool trigger_all_cpu_backtrace(void)
{
	return false;
}
static inline bool trigger_allbutself_cpu_backtrace(void)
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

#ifdef CONFIG_LOCKUP_DETECTOR
u64 hw_nmi_get_sample_period(int watchdog_thresh);
extern int nmi_watchdog_enabled;
extern int soft_watchdog_enabled;
extern int watchdog_user_enabled;
extern int watchdog_thresh;
extern unsigned long watchdog_enabled;
extern unsigned long *watchdog_cpumask_bits;
#ifdef CONFIG_SMP
extern int sysctl_softlockup_all_cpu_backtrace;
extern int sysctl_hardlockup_all_cpu_backtrace;
#else
#define sysctl_softlockup_all_cpu_backtrace 0
#define sysctl_hardlockup_all_cpu_backtrace 0
#endif
extern bool is_hardlockup(void);
struct ctl_table;
extern int proc_watchdog(struct ctl_table *, int ,
			 void __user *, size_t *, loff_t *);
extern int proc_nmi_watchdog(struct ctl_table *, int ,
			     void __user *, size_t *, loff_t *);
extern int proc_soft_watchdog(struct ctl_table *, int ,
			      void __user *, size_t *, loff_t *);
extern int proc_watchdog_thresh(struct ctl_table *, int ,
				void __user *, size_t *, loff_t *);
extern int proc_watchdog_cpumask(struct ctl_table *, int,
				 void __user *, size_t *, loff_t *);
extern int lockup_detector_suspend(void);
extern void lockup_detector_resume(void);
#else
static inline int lockup_detector_suspend(void)
{
	return 0;
}

static inline void lockup_detector_resume(void)
{
}
#endif

#ifdef CONFIG_HAVE_ACPI_APEI_NMI
#include <asm/nmi.h>
#endif

#endif
