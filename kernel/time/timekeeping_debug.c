// SPDX-License-Identifier: GPL-2.0+
/*
 * debugfs file to track time spent in suspend
 *
 * Copyright (c) 2011, Google, Inc.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/suspend.h>
#include <linux/time.h>

#include "timekeeping_internal.h"

#define NUM_BINS 32

static unsigned int sleep_time_bin[NUM_BINS] = {0};

static int tk_debug_sleep_time_show(struct seq_file *s, void *data)
{
	unsigned int bin;
	seq_puts(s, "      time (secs)        count\n");
	seq_puts(s, "------------------------------\n");
	for (bin = 0; bin < 32; bin++) {
		if (sleep_time_bin[bin] == 0)
			continue;
		seq_printf(s, "%10u - %-10u %4u\n",
			bin ? 1 << (bin - 1) : 0, 1 << bin,
				sleep_time_bin[bin]);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tk_debug_sleep_time);

static int __init tk_debug_sleep_time_init(void)
{
	debugfs_create_file("sleep_time", 0444, NULL, NULL,
			    &tk_debug_sleep_time_fops);
	return 0;
}
late_initcall(tk_debug_sleep_time_init);

void tk_debug_account_sleep_time(const struct timespec64 *t)
{
	/* Cap bin index so we don't overflow the array */
	int bin = min(fls(t->tv_sec), NUM_BINS-1);

	sleep_time_bin[bin]++;
	pm_deferred_pr_dbg("Timekeeping suspended for %lld.%03lu seconds\n",
			   (s64)t->tv_sec, t->tv_nsec / NSEC_PER_MSEC);
}

