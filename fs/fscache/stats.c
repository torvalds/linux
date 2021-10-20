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
atomic_t fscache_n_cookies;
atomic_t fscache_n_cookies_lru;
atomic_t fscache_n_cookies_lru_expired;
atomic_t fscache_n_cookies_lru_removed;
atomic_t fscache_n_cookies_lru_dropped;

atomic_t fscache_n_acquires;
atomic_t fscache_n_acquires_ok;
atomic_t fscache_n_acquires_oom;

atomic_t fscache_n_invalidates;

atomic_t fscache_n_updates;
EXPORT_SYMBOL(fscache_n_updates);

atomic_t fscache_n_relinquishes;
atomic_t fscache_n_relinquishes_retire;
atomic_t fscache_n_relinquishes_dropped;

atomic_t fscache_n_resizes;
atomic_t fscache_n_resizes_null;

atomic_t fscache_n_read;
EXPORT_SYMBOL(fscache_n_read);
atomic_t fscache_n_write;
EXPORT_SYMBOL(fscache_n_write);

/*
 * display the general statistics
 */
int fscache_stats_show(struct seq_file *m, void *v)
{
	seq_puts(m, "FS-Cache statistics\n");
	seq_printf(m, "Cookies: n=%d v=%d vcol=%u voom=%u\n",
		   atomic_read(&fscache_n_cookies),
		   atomic_read(&fscache_n_volumes),
		   atomic_read(&fscache_n_volumes_collision),
		   atomic_read(&fscache_n_volumes_nomem)
		   );

	seq_printf(m, "Acquire: n=%u ok=%u oom=%u\n",
		   atomic_read(&fscache_n_acquires),
		   atomic_read(&fscache_n_acquires_ok),
		   atomic_read(&fscache_n_acquires_oom));

	seq_printf(m, "LRU    : n=%u exp=%u rmv=%u drp=%u at=%ld\n",
		   atomic_read(&fscache_n_cookies_lru),
		   atomic_read(&fscache_n_cookies_lru_expired),
		   atomic_read(&fscache_n_cookies_lru_removed),
		   atomic_read(&fscache_n_cookies_lru_dropped),
		   timer_pending(&fscache_cookie_lru_timer) ?
		   fscache_cookie_lru_timer.expires - jiffies : 0);

	seq_printf(m, "Invals : n=%u\n",
		   atomic_read(&fscache_n_invalidates));

	seq_printf(m, "Updates: n=%u rsz=%u rsn=%u\n",
		   atomic_read(&fscache_n_updates),
		   atomic_read(&fscache_n_resizes),
		   atomic_read(&fscache_n_resizes_null));

	seq_printf(m, "Relinqs: n=%u rtr=%u drop=%u\n",
		   atomic_read(&fscache_n_relinquishes),
		   atomic_read(&fscache_n_relinquishes_retire),
		   atomic_read(&fscache_n_relinquishes_dropped));

	seq_printf(m, "IO     : rd=%u wr=%u\n",
		   atomic_read(&fscache_n_read),
		   atomic_read(&fscache_n_write));

	netfs_stats_show(m);
	return 0;
}
