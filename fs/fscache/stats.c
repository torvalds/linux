/* FS-Cache statistics
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define FSCACHE_DEBUG_LEVEL THREAD
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * operation counters
 */
atomic_t fscache_n_op_pend;
atomic_t fscache_n_op_run;
atomic_t fscache_n_op_enqueue;
atomic_t fscache_n_op_requeue;
atomic_t fscache_n_op_deferred_release;
atomic_t fscache_n_op_release;
atomic_t fscache_n_op_gc;

atomic_t fscache_n_attr_changed;
atomic_t fscache_n_attr_changed_ok;
atomic_t fscache_n_attr_changed_nobufs;
atomic_t fscache_n_attr_changed_nomem;
atomic_t fscache_n_attr_changed_calls;

atomic_t fscache_n_allocs;
atomic_t fscache_n_allocs_ok;
atomic_t fscache_n_allocs_wait;
atomic_t fscache_n_allocs_nobufs;
atomic_t fscache_n_alloc_ops;
atomic_t fscache_n_alloc_op_waits;

atomic_t fscache_n_retrievals;
atomic_t fscache_n_retrievals_ok;
atomic_t fscache_n_retrievals_wait;
atomic_t fscache_n_retrievals_nodata;
atomic_t fscache_n_retrievals_nobufs;
atomic_t fscache_n_retrievals_intr;
atomic_t fscache_n_retrievals_nomem;
atomic_t fscache_n_retrieval_ops;
atomic_t fscache_n_retrieval_op_waits;

atomic_t fscache_n_stores;
atomic_t fscache_n_stores_ok;
atomic_t fscache_n_stores_again;
atomic_t fscache_n_stores_nobufs;
atomic_t fscache_n_stores_oom;
atomic_t fscache_n_store_ops;
atomic_t fscache_n_store_calls;

atomic_t fscache_n_marks;
atomic_t fscache_n_uncaches;

atomic_t fscache_n_acquires;
atomic_t fscache_n_acquires_null;
atomic_t fscache_n_acquires_no_cache;
atomic_t fscache_n_acquires_ok;
atomic_t fscache_n_acquires_nobufs;
atomic_t fscache_n_acquires_oom;

atomic_t fscache_n_updates;
atomic_t fscache_n_updates_null;
atomic_t fscache_n_updates_run;

atomic_t fscache_n_relinquishes;
atomic_t fscache_n_relinquishes_null;
atomic_t fscache_n_relinquishes_waitcrt;

atomic_t fscache_n_cookie_index;
atomic_t fscache_n_cookie_data;
atomic_t fscache_n_cookie_special;

atomic_t fscache_n_object_alloc;
atomic_t fscache_n_object_no_alloc;
atomic_t fscache_n_object_lookups;
atomic_t fscache_n_object_lookups_negative;
atomic_t fscache_n_object_lookups_positive;
atomic_t fscache_n_object_created;
atomic_t fscache_n_object_avail;
atomic_t fscache_n_object_dead;

atomic_t fscache_n_checkaux_none;
atomic_t fscache_n_checkaux_okay;
atomic_t fscache_n_checkaux_update;
atomic_t fscache_n_checkaux_obsolete;

/*
 * display the general statistics
 */
static int fscache_stats_show(struct seq_file *m, void *v)
{
	seq_puts(m, "FS-Cache statistics\n");

	seq_printf(m, "Cookies: idx=%u dat=%u spc=%u\n",
		   atomic_read(&fscache_n_cookie_index),
		   atomic_read(&fscache_n_cookie_data),
		   atomic_read(&fscache_n_cookie_special));

	seq_printf(m, "Objects: alc=%u nal=%u avl=%u ded=%u\n",
		   atomic_read(&fscache_n_object_alloc),
		   atomic_read(&fscache_n_object_no_alloc),
		   atomic_read(&fscache_n_object_avail),
		   atomic_read(&fscache_n_object_dead));
	seq_printf(m, "ChkAux : non=%u ok=%u upd=%u obs=%u\n",
		   atomic_read(&fscache_n_checkaux_none),
		   atomic_read(&fscache_n_checkaux_okay),
		   atomic_read(&fscache_n_checkaux_update),
		   atomic_read(&fscache_n_checkaux_obsolete));

	seq_printf(m, "Pages  : mrk=%u unc=%u\n",
		   atomic_read(&fscache_n_marks),
		   atomic_read(&fscache_n_uncaches));

	seq_printf(m, "Acquire: n=%u nul=%u noc=%u ok=%u nbf=%u"
		   " oom=%u\n",
		   atomic_read(&fscache_n_acquires),
		   atomic_read(&fscache_n_acquires_null),
		   atomic_read(&fscache_n_acquires_no_cache),
		   atomic_read(&fscache_n_acquires_ok),
		   atomic_read(&fscache_n_acquires_nobufs),
		   atomic_read(&fscache_n_acquires_oom));

	seq_printf(m, "Lookups: n=%u neg=%u pos=%u crt=%u\n",
		   atomic_read(&fscache_n_object_lookups),
		   atomic_read(&fscache_n_object_lookups_negative),
		   atomic_read(&fscache_n_object_lookups_positive),
		   atomic_read(&fscache_n_object_created));

	seq_printf(m, "Updates: n=%u nul=%u run=%u\n",
		   atomic_read(&fscache_n_updates),
		   atomic_read(&fscache_n_updates_null),
		   atomic_read(&fscache_n_updates_run));

	seq_printf(m, "Relinqs: n=%u nul=%u wcr=%u\n",
		   atomic_read(&fscache_n_relinquishes),
		   atomic_read(&fscache_n_relinquishes_null),
		   atomic_read(&fscache_n_relinquishes_waitcrt));

	seq_printf(m, "AttrChg: n=%u ok=%u nbf=%u oom=%u run=%u\n",
		   atomic_read(&fscache_n_attr_changed),
		   atomic_read(&fscache_n_attr_changed_ok),
		   atomic_read(&fscache_n_attr_changed_nobufs),
		   atomic_read(&fscache_n_attr_changed_nomem),
		   atomic_read(&fscache_n_attr_changed_calls));

	seq_printf(m, "Allocs : n=%u ok=%u wt=%u nbf=%u\n",
		   atomic_read(&fscache_n_allocs),
		   atomic_read(&fscache_n_allocs_ok),
		   atomic_read(&fscache_n_allocs_wait),
		   atomic_read(&fscache_n_allocs_nobufs));
	seq_printf(m, "Allocs : ops=%u owt=%u\n",
		   atomic_read(&fscache_n_alloc_ops),
		   atomic_read(&fscache_n_alloc_op_waits));

	seq_printf(m, "Retrvls: n=%u ok=%u wt=%u nod=%u nbf=%u"
		   " int=%u oom=%u\n",
		   atomic_read(&fscache_n_retrievals),
		   atomic_read(&fscache_n_retrievals_ok),
		   atomic_read(&fscache_n_retrievals_wait),
		   atomic_read(&fscache_n_retrievals_nodata),
		   atomic_read(&fscache_n_retrievals_nobufs),
		   atomic_read(&fscache_n_retrievals_intr),
		   atomic_read(&fscache_n_retrievals_nomem));
	seq_printf(m, "Retrvls: ops=%u owt=%u\n",
		   atomic_read(&fscache_n_retrieval_ops),
		   atomic_read(&fscache_n_retrieval_op_waits));

	seq_printf(m, "Stores : n=%u ok=%u agn=%u nbf=%u oom=%u\n",
		   atomic_read(&fscache_n_stores),
		   atomic_read(&fscache_n_stores_ok),
		   atomic_read(&fscache_n_stores_again),
		   atomic_read(&fscache_n_stores_nobufs),
		   atomic_read(&fscache_n_stores_oom));
	seq_printf(m, "Stores : ops=%u run=%u\n",
		   atomic_read(&fscache_n_store_ops),
		   atomic_read(&fscache_n_store_calls));

	seq_printf(m, "Ops    : pend=%u run=%u enq=%u\n",
		   atomic_read(&fscache_n_op_pend),
		   atomic_read(&fscache_n_op_run),
		   atomic_read(&fscache_n_op_enqueue));
	seq_printf(m, "Ops    : dfr=%u rel=%u gc=%u\n",
		   atomic_read(&fscache_n_op_deferred_release),
		   atomic_read(&fscache_n_op_release),
		   atomic_read(&fscache_n_op_gc));
	return 0;
}

/*
 * open "/proc/fs/fscache/stats" allowing provision of a statistical summary
 */
static int fscache_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, fscache_stats_show, NULL);
}

const struct file_operations fscache_stats_fops = {
	.owner		= THIS_MODULE,
	.open		= fscache_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
