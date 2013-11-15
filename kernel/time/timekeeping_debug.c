/*
 * debugfs file to track time spent in suspend
 *
 * Copyright (c) 2011, Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/time.h>

static unsigned int sleep_time_bin[32] = {0};

static int tk_debug_show_sleep_time(struct seq_file *s, void *data)
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

static int tk_debug_sleep_time_open(struct inode *inode, struct file *file)
{
	return single_open(file, tk_debug_show_sleep_time, NULL);
}

static const struct file_operations tk_debug_sleep_time_fops = {
	.open		= tk_debug_sleep_time_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init tk_debug_sleep_time_init(void)
{
	struct dentry *d;

	d = debugfs_create_file("sleep_time", 0444, NULL, NULL,
		&tk_debug_sleep_time_fops);
	if (!d) {
		pr_err("Failed to create sleep_time debug file\n");
		return -ENOMEM;
	}

	return 0;
}
late_initcall(tk_debug_sleep_time_init);

void tk_debug_account_sleep_time(struct timespec *t)
{
	sleep_time_bin[fls(t->tv_sec)]++;
}

