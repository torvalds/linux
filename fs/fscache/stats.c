// SPDX-License-Identifier: GPL-2.0-or-later
/* FS-Cache statistics
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * operation counters
 */
atomic_t fscache_n_volumes;
atomic_t fscache_n_volumes_collision;
atomic_t fscache_n_volumes_nomem;

/*
 * display the general statistics
 */
int fscache_stats_show(struct seq_file *m, void *v)
{
	seq_puts(m, "FS-Cache statistics\n");
	seq_printf(m, "Cookies: v=%d vcol=%u voom=%u\n",
		   atomic_read(&fscache_n_volumes),
		   atomic_read(&fscache_n_volumes_collision),
		   atomic_read(&fscache_n_volumes_nomem)
		   );

	netfs_stats_show(m);
	return 0;
}
