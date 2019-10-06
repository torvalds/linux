// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include "gcov.h"

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

void __gcov_exit(void)
{
	/* Unused. */
}
EXPORT_SYMBOL(__gcov_exit);
