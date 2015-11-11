/*
 *  This code maintains a list of active profiling data structures.
 *
 *    Copyright IBM Corp. 2009
 *    Author(s): Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *
 *    Uses gcc-internal data definitions.
 *    Based on the gcov-kernel patch by:
 *		 Hubertus Franke <frankeh@us.ibm.com>
 *		 Nigel Hinds <nhinds@us.ibm.com>
 *		 Rajan Ravindran <rajancr@us.ibm.com>
 *		 Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *		 Paul Larson
 */

#define pr_fmt(fmt)	"gcov: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include "gcov.h"

static int gcov_events_enabled;
static DEFINE_MUTEX(gcov_lock);

/*
 * __gcov_init is called by gcc-generated constructor code for each object
 * file compiled with -fprofile-arcs.
 */
void __gcov_init(struct gcov_info *info)
{
	static unsigned int gcov_version;

	mutex_lock(&gcov_lock);
	if (gcov_version == 0) {
		gcov_version = gcov_info_version(info);
		/*
		 * Printing gcc's version magic may prove useful for debugging
		 * incompatibility reports.
		 */
		pr_info("version magic: 0x%x\n", gcov_version);
	}
	/*
	 * Add new profiling data structure to list and inform event
	 * listener.
	 */
	gcov_info_link(info);
	if (gcov_events_enabled)
		gcov_event(GCOV_ADD, info);
	mutex_unlock(&gcov_lock);
}
EXPORT_SYMBOL(__gcov_init);

/*
 * These functions may be referenced by gcc-generated profiling code but serve
 * no function for kernel profiling.
 */
void __gcov_flush(void)
{
	/* Unused. */
}
EXPORT_SYMBOL(__gcov_flush);

void __gcov_merge_add(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}
EXPORT_SYMBOL(__gcov_merge_add);

void __gcov_merge_single(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}
EXPORT_SYMBOL(__gcov_merge_single);

void __gcov_merge_delta(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}
EXPORT_SYMBOL(__gcov_merge_delta);

void __gcov_merge_ior(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}
EXPORT_SYMBOL(__gcov_merge_ior);

void __gcov_merge_time_profile(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}
EXPORT_SYMBOL(__gcov_merge_time_profile);

void __gcov_merge_icall_topn(gcov_type *counters, unsigned int n_counters)
{
	/* Unused. */
}
EXPORT_SYMBOL(__gcov_merge_icall_topn);

/**
 * gcov_enable_events - enable event reporting through gcov_event()
 *
 * Turn on reporting of profiling data load/unload-events through the
 * gcov_event() callback. Also replay all previous events once. This function
 * is needed because some events are potentially generated too early for the
 * callback implementation to handle them initially.
 */
void gcov_enable_events(void)
{
	struct gcov_info *info = NULL;

	mutex_lock(&gcov_lock);
	gcov_events_enabled = 1;

	/* Perform event callback for previously registered entries. */
	while ((info = gcov_info_next(info))) {
		gcov_event(GCOV_ADD, info);
		cond_resched();
	}

	mutex_unlock(&gcov_lock);
}

#ifdef CONFIG_MODULES
static inline int within(void *addr, void *start, unsigned long size)
{
	return ((addr >= start) && (addr < start + size));
}

/* Update list and generate events when modules are unloaded. */
static int gcov_module_notifier(struct notifier_block *nb, unsigned long event,
				void *data)
{
	struct module *mod = data;
	struct gcov_info *info = NULL;
	struct gcov_info *prev = NULL;

	if (event != MODULE_STATE_GOING)
		return NOTIFY_OK;
	mutex_lock(&gcov_lock);

	/* Remove entries located in module from linked list. */
	while ((info = gcov_info_next(info))) {
		if (within(info, mod->module_core, mod->core_size)) {
			gcov_info_unlink(prev, info);
			if (gcov_events_enabled)
				gcov_event(GCOV_REMOVE, info);
		} else
			prev = info;
	}

	mutex_unlock(&gcov_lock);

	return NOTIFY_OK;
}

static struct notifier_block gcov_nb = {
	.notifier_call	= gcov_module_notifier,
};

static int __init gcov_init(void)
{
	return register_module_notifier(&gcov_nb);
}
device_initcall(gcov_init);
#endif /* CONFIG_MODULES */
