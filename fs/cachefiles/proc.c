// SPDX-License-Identifier: GPL-2.0-or-later
/* CacheFiles statistics
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "internal.h"

atomic_t cachefiles_lookup_histogram[HZ];
atomic_t cachefiles_mkdir_histogram[HZ];
atomic_t cachefiles_create_histogram[HZ];

/*
 * display the latency histogram
 */
static int cachefiles_histogram_show(struct seq_file *m, void *v)
{
	unsigned long index;
	unsigned x, y, z, t;

	switch ((unsigned long) v) {
	case 1:
		seq_puts(m, "JIFS  SECS  LOOKUPS   MKDIRS    CREATES\n");
		return 0;
	case 2:
		seq_puts(m, "===== ===== ========= ========= =========\n");
		return 0;
	default:
		index = (unsigned long) v - 3;
		x = atomic_read(&cachefiles_lookup_histogram[index]);
		y = atomic_read(&cachefiles_mkdir_histogram[index]);
		z = atomic_read(&cachefiles_create_histogram[index]);
		if (x == 0 && y == 0 && z == 0)
			return 0;

		t = (index * 1000) / HZ;

		seq_printf(m, "%4lu  0.%03u %9u %9u %9u\n", index, t, x, y, z);
		return 0;
	}
}

/*
 * set up the iterator to start reading from the first line
 */
static void *cachefiles_histogram_start(struct seq_file *m, loff_t *_pos)
{
	if ((unsigned long long)*_pos >= HZ + 2)
		return NULL;
	if (*_pos == 0)
		*_pos = 1;
	return (void *)(unsigned long) *_pos;
}

/*
 * move to the next line
 */
static void *cachefiles_histogram_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return (unsigned long long)*pos > HZ + 2 ?
		NULL : (void *)(unsigned long) *pos;
}

/*
 * clean up after reading
 */
static void cachefiles_histogram_stop(struct seq_file *m, void *v)
{
}

static const struct seq_operations cachefiles_histogram_ops = {
	.start		= cachefiles_histogram_start,
	.stop		= cachefiles_histogram_stop,
	.next		= cachefiles_histogram_next,
	.show		= cachefiles_histogram_show,
};

/*
 * initialise the /proc/fs/cachefiles/ directory
 */
int __init cachefiles_proc_init(void)
{
	_enter("");

	if (!proc_mkdir("fs/cachefiles", NULL))
		goto error_dir;

	if (!proc_create_seq("fs/cachefiles/histogram", S_IFREG | 0444, NULL,
			 &cachefiles_histogram_ops))
		goto error_histogram;

	_leave(" = 0");
	return 0;

error_histogram:
	remove_proc_entry("fs/cachefiles", NULL);
error_dir:
	_leave(" = -ENOMEM");
	return -ENOMEM;
}

/*
 * clean up the /proc/fs/cachefiles/ directory
 */
void cachefiles_proc_cleanup(void)
{
	remove_proc_entry("fs/cachefiles/histogram", NULL);
	remove_proc_entry("fs/cachefiles", NULL);
}
