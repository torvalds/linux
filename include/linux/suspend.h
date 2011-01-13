#ifndef _LINUX_SUSPEND_H
#define _LINUX_SUSPEND_H

#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <asm/errno.h>

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_VT) && defined(CONFIG_VT_CONSOLE)
extern void pm_set_vt_switch(int);
extern int pm_prepare_console(void);
extern void pm_restore_console(void);
#else
static inline void pm_set_vt_switch(int do_switch)
{
}

static inline int pm_prepare_console(void)
{
	return 0;
}

static inline void pm_restore_console(void)
{
}
#endif

typedef int __bitwise suspend_state_t;

#define PM_SUSPEND_ON		((__force suspend_state_t) 0)
#define PM_SUSPEND_STANDBY	((__force suspend_state_t) 1)
#define PM_SUSPEND_MEM		((__force suspend_state_t) 3)
#define PM_SUSPEND_MAX		((__force suspend_state_t) 4)

/**
 * struct platform_suspend_ops - Callbacks for managing platform dependent
 *	system sleep states.
 *
 * @valid: Callback to determine if given system sleep state is supported by
 *	the platform.
 *	Valid (ie. supported) states are advertised in /sys/power/state.  Note
 *	that it still may be impossible to enter given system sleep state if the
 *	conditions aren't right.
 *	There is the %suspend_valid_only_mem function available that can be
 *	assigned to this if the platform only supports mem sleep.
 *
 * @begin: Initialise a transition to given system sleep state.
 *	@begin() is executed right prior to suspending devices.  The information
 *	conveyed to the platform code by @begin() should be disregarded by it as
 *	soon as @end() is executed.  If @begin() fails (ie. returns nonzero),
 *	@prepare(), @enter() and @finish() will not be called by the PM core.
 *	This callback is optional.  However, if it is implemented, the argument
 *	passed to @enter() is redundant and should be ignored.
 *
 * @prepare: Prepare the platform for entering the system sleep state indicated
 *	by @begin().
 *	@prepare() is called right after devices have been suspended (ie. the
 *	appropriate .suspend() method has been executed for each device) and
 *	before device drivers' late suspend callbacks are executed.  It returns
 *	0 on success or a negative error code otherwise, in which case the
 *	system cannot enter the desired sleep state (@prepare_late(), @enter(),
 *	and @wake() will not be called in that case).
 *
 * @prepare_late: Finish preparing the platform for entering the system sleep
 *	state indicated by @begin().
 *	@prepare_late is called before disabling nonboot CPUs and after
 *	device drivers' late suspend callbacks have been executed.  It returns
 *	0 on success or a negative error code otherwise, in which case the
 *	system cannot enter the desired sleep state (@enter() will not be
 *	executed).
 *
 * @enter: Enter the system sleep state indicated by @begin() or represented by
 *	the argument if @begin() is not implemented.
 *	This callback is mandatory.  It returns 0 on success or a negative
 *	error code otherwise, in which case the system cannot enter the desired
 *	sleep state.
 *
 * @wake: Called when the system has just left a sleep state, right after
 *	the nonboot CPUs have been enabled and before device drivers' early
 *	resume callbacks are executed.
 *	This callback is optional, but should be implemented by the platforms
 *	that implement @prepare_late().  If implemented, it is always called
 *	after @prepare_late and @enter(), even if one of them fails.
 *
 * @finish: Finish wake-up of the platform.
 *	@finish is called right prior to calling device drivers' regular suspend
 *	callbacks.
 *	This callback is optional, but should be implemented by the platforms
 *	that implement @prepare().  If implemented, it is always called after
 *	@enter() and @wake(), even if any of them fails.  It is executed after
 *	a failing @prepare.
 *
 * @end: Called by the PM core right after resuming devices, to indicate to
 *	the platform that the system has returned to the working state or
 *	the transition to the sleep state has been aborted.
 *	This callback is optional, but should be implemented by the platforms
 *	that implement @begin().  Accordingly, platforms implementing @begin()
 *	should also provide a @end() which cleans up transitions aborted before
 *	@enter().
 *
 * @recover: Recover the platform from a suspend failure.
 *	Called by the PM core if the suspending of devices fails.
 *	This callback is optional and should only be implemented by platforms
 *	which require special recovery actions in that situation.
 */
struct platform_suspend_ops {
	int (*valid)(suspend_state_t state);
	int (*begin)(suspend_state_t state);
	int (*prepare)(void);
	int (*prepare_late)(void);
	int (*enter)(suspend_state_t state);
	void (*wake)(void);
	void (*finish)(void);
	void (*end)(void);
	void (*recover)(void);
};

#ifdef CONFIG_SUSPEND
/**
 * suspend_set_ops - set platform dependent suspend operations
 * @ops: The new suspend operations to set.
 */
extern void suspend_set_ops(struct platform_suspend_ops *ops);
extern int suspend_valid_only_mem(suspend_state_t state);

/**
 * arch_suspend_disable_irqs - disable IRQs for suspend
 *
 * Disables IRQs (in the default case). This is a weak symbol in the common
 * code and thus allows architectures to override it if more needs to be
 * done. Not called for suspend to disk.
 */
extern void arch_suspend_disable_irqs(void);

/**
 * arch_suspend_enable_irqs - enable IRQs after suspend
 *
 * Enables IRQs (in the default case). This is a weak symbol in the common
 * code and thus allows architectures to override it if more needs to be
 * done. Not called for suspend to disk.
 */
extern void arch_suspend_enable_irqs(void);

extern int pm_suspend(suspend_state_t state);
#else /* !CONFIG_SUSPEND */
#define suspend_valid_only_mem	NULL

static inline void suspend_set_ops(struct platform_suspend_ops *ops) {}
static inline int pm_suspend(suspend_state_t state) { return -ENOSYS; }
#endif /* !CONFIG_SUSPEND */

/* struct pbe is used for creating lists of pages that should be restored
 * atomically during the resume from disk, because the page frames they have
 * occupied before the suspend are in use.
 */
struct pbe {
	void *address;		/* address of the copy */
	void *orig_address;	/* original address of a page */
	struct pbe *next;
};

/* mm/page_alloc.c */
extern void mark_free_pages(struct zone *zone);

/**
 * struct platform_hibernation_ops - hibernation platform support
 *
 * The methods in this structure allow a platform to carry out special
 * operations required by it during a hibernation transition.
 *
 * All the methods below, except for @recover(), must be implemented.
 *
 * @begin: Tell the platform driver that we're starting hibernation.
 *	Called right after shrinking memory and before freezing devices.
 *
 * @end: Called by the PM core right after resuming devices, to indicate to
 *	the platform that the system has returned to the working state.
 *
 * @pre_snapshot: Prepare the platform for creating the hibernation image.
 *	Called right after devices have been frozen and before the nonboot
 *	CPUs are disabled (runs with IRQs on).
 *
 * @finish: Restore the previous state of the platform after the hibernation
 *	image has been created *or* put the platform into the normal operation
 *	mode after the hibernation (the same method is executed in both cases).
 *	Called right after the nonboot CPUs have been enabled and before
 *	thawing devices (runs with IRQs on).
 *
 * @prepare: Prepare the platform for entering the low power state.
 *	Called right after the hibernation image has been saved and before
 *	devices are prepared for entering the low power state.
 *
 * @enter: Put the system into the low power state after the hibernation image
 *	has been saved to disk.
 *	Called after the nonboot CPUs have been disabled and all of the low
 *	level devices have been shut down (runs with IRQs off).
 *
 * @leave: Perform the first stage of the cleanup after the system sleep state
 *	indicated by @set_target() has been left.
 *	Called right after the control has been passed from the boot kernel to
 *	the image kernel, before the nonboot CPUs are enabled and before devices
 *	are resumed.  Executed with interrupts disabled.
 *
 * @pre_restore: Prepare system for the restoration from a hibernation image.
 *	Called right after devices have been frozen and before the nonboot
 *	CPUs are disabled (runs with IRQs on).
 *
 * @restore_cleanup: Clean up after a failing image restoration.
 *	Called right after the nonboot CPUs have been enabled and before
 *	thawing devices (runs with IRQs on).
 *
 * @recover: Recover the platform from a failure to suspend devices.
 *	Called by the PM core if the suspending of devices during hibernation
 *	fails.  This callback is optional and should only be implemented by
 *	platforms which require special recovery actions in that situation.
 */
struct platform_hibernation_ops {
	int (*begin)(void);
	void (*end)(void);
	int (*pre_snapshot)(void);
	void (*finish)(void);
	int (*prepare)(void);
	int (*enter)(void);
	void (*leave)(void);
	int (*pre_restore)(void);
	void (*restore_cleanup)(void);
	void (*recover)(void);
};

#ifdef CONFIG_HIBERNATION
/* kernel/power/snapshot.c */
extern void __register_nosave_region(unsigned long b, unsigned long e, int km);
static inline void __init register_nosave_region(unsigned long b, unsigned long e)
{
	__register_nosave_region(b, e, 0);
}
static inline void __init register_nosave_region_late(unsigned long b, unsigned long e)
{
	__register_nosave_region(b, e, 1);
}
extern int swsusp_page_is_forbidden(struct page *);
extern void swsusp_set_page_free(struct page *);
extern void swsusp_unset_page_free(struct page *);
extern unsigned long get_safe_page(gfp_t gfp_mask);

extern void hibernation_set_ops(struct platform_hibernation_ops *ops);
extern int hibernate(void);
extern bool system_entering_hibernation(void);
#else /* CONFIG_HIBERNATION */
static inline int swsusp_page_is_forbidden(struct page *p) { return 0; }
static inline void swsusp_set_page_free(struct page *p) {}
static inline void swsusp_unset_page_free(struct page *p) {}

static inline void hibernation_set_ops(struct platform_hibernation_ops *ops) {}
static inline int hibernate(void) { return -ENOSYS; }
static inline bool system_entering_hibernation(void) { return false; }
#endif /* CONFIG_HIBERNATION */

#ifdef CONFIG_SUSPEND_NVS
extern int suspend_nvs_register(unsigned long start, unsigned long size);
extern int suspend_nvs_alloc(void);
extern void suspend_nvs_free(void);
extern void suspend_nvs_save(void);
extern void suspend_nvs_restore(void);
#else /* CONFIG_SUSPEND_NVS */
static inline int suspend_nvs_register(unsigned long a, unsigned long b)
{
	return 0;
}
static inline int suspend_nvs_alloc(void) { return 0; }
static inline void suspend_nvs_free(void) {}
static inline void suspend_nvs_save(void) {}
static inline void suspend_nvs_restore(void) {}
#endif /* CONFIG_SUSPEND_NVS */

#ifdef CONFIG_PM_SLEEP
void save_processor_state(void);
void restore_processor_state(void);

/* kernel/power/main.c */
extern int register_pm_notifier(struct notifier_block *nb);
extern int unregister_pm_notifier(struct notifier_block *nb);

#define pm_notifier(fn, pri) {				\
	static struct notifier_block fn##_nb =			\
		{ .notifier_call = fn, .priority = pri };	\
	register_pm_notifier(&fn##_nb);			\
}

/* drivers/base/power/wakeup.c */
extern bool events_check_enabled;

extern bool pm_wakeup_pending(void);
extern bool pm_get_wakeup_count(unsigned int *count);
extern bool pm_save_wakeup_count(unsigned int count);
#else /* !CONFIG_PM_SLEEP */

static inline int register_pm_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int unregister_pm_notifier(struct notifier_block *nb)
{
	return 0;
}

#define pm_notifier(fn, pri)	do { (void)(fn); } while (0)

static inline bool pm_wakeup_pending(void) { return false; }
#endif /* !CONFIG_PM_SLEEP */

extern struct mutex pm_mutex;

#ifndef CONFIG_HIBERNATION
static inline void register_nosave_region(unsigned long b, unsigned long e)
{
}
static inline void register_nosave_region_late(unsigned long b, unsigned long e)
{
}

static inline void lock_system_sleep(void) {}
static inline void unlock_system_sleep(void) {}

#else

/* Let some subsystems like memory hotadd exclude hibernation */

static inline void lock_system_sleep(void)
{
	mutex_lock(&pm_mutex);
}

static inline void unlock_system_sleep(void)
{
	mutex_unlock(&pm_mutex);
}
#endif

#endif /* _LINUX_SUSPEND_H */
