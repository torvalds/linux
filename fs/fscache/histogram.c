// SPDX-License-Identifier: GPL-2.0-or-later
/* FS-Cache latency histogram
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL THREAD
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "internal.h"

atomic_t fscache_obj_instantiate_histogram[HZ];
atomic_t fscache_objs_histogram[HZ];
atomic_t fscache_ops_histogram[HZ];
atomic_t fscache_retrieval_delay_histogram[HZ];
atomic_t fscache_retrieval_histogram[HZ];

/*
 * display the time-taken histogram
 */
static int fscache_histogram_show(struct seq_file *m, void *v)
{
	unsigned long index;
	unsigned n[5], t;

	switch ((unsigned long) v) {
	case 1:
		seq_puts(m, "JIFS  SECS  OBJ INST  OP RUNS   OBJ RUNS  RETRV DLY RETRIEVLS\n");
		return 0;
	case 2:
		seq_puts(m, "===== ===== ========= ========= ========= ========= =========\n");
		return 0;
	default:
		index = (unsigned long) v - 3;
		n[0] = atomic_read(&fscache_obj_instantiate_histogram[index]);
		n[1] = atomic_read(&fscache_ops_histogram[index]);
		n[2] = atomic_read(&fscache_objs_histogram[index]);
		n[3] = atomic_read(&fscache_retrieval_delay_histogram[index]);
		n[4] = atomic_read(&fscache_retrieval_histogram[index]);
		if (!(n[0] | n[1] | n[2] | n[3] | n[4]))
			return 0;

		t = (index * 1000) / HZ;

		seq_printf(m, "%4lu  0.%03u %9u %9u %9u %9u %9u\n",
			   index, t, n[0], n[1], n[2], n[3], n[4]);
		return 0;
	}
}

/*
 * set up the iterator to start reading from the first line
 */
static void *fscache_histogram_start(struct seq_file *m, loff_t *_pos)
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
static void *fscache_histogram_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return (unsigned long long)*pos > HZ + 2 ?
		NULL : (void *)(unsigned long) *pos;
}

/*
 * clean up after reading
 */
static void fscache_histogram_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations fscache_histogram_ops = {
	.start		= fscache_histogram_start,
	.stop		= fscache_histogram_stop,
	.next		= fscache_histogram_next,
	.show		= fscache_histogram_show,
};
