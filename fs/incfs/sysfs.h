/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 */
#ifndef _INCFS_SYSFS_H
#define _INCFS_SYSFS_H

struct incfs_sysfs_node {
	struct kobject isn_sysfs_node;

	/* Number of reads timed out */
	u32 isn_reads_failed_timed_out;

	/* Number of reads failed because hash verification failed */
	u32 isn_reads_failed_hash_verification;

	/* Number of reads failed for another reason */
	u32 isn_reads_failed_other;

	/* Number of reads delayed because page had to be fetched */
	u32 isn_reads_delayed_pending;

	/* Total time waiting for pages to be fetched */
	u64 isn_reads_delayed_pending_us;

	/*
	 * Number of reads delayed because of per-uid min_time_us or
	 * min_pending_time_us settings
	 */
	u32 isn_reads_delayed_min;

	/* Total time waiting because of per-uid min_time_us or
	 * min_pending_time_us settings.
	 *
	 * Note that if a read is initially delayed because we have to wait for
	 * the page, then further delayed because of min_pending_time_us
	 * setting, this counter gets incremented by only the further delay
	 * time.
	 */
	u64 isn_reads_delayed_min_us;
};

int incfs_init_sysfs(void);
void incfs_cleanup_sysfs(void);
struct incfs_sysfs_node *incfs_add_sysfs_node(const char *name);
void incfs_free_sysfs_node(struct incfs_sysfs_node *node);

#endif
