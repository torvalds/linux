// SPDX-License-Identifier: GPL-2.0-or-later
/* FS-Cache statistics viewing interface
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL OPERATION
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * initialise the /proc/fs/fscache/ directory
 */
int __init fscache_proc_init(void)
{
	_enter("");

	if (!proc_mkdir("fs/fscache", NULL))
		goto error_dir;

#ifdef CONFIG_FSCACHE_STATS
	if (!proc_create_single("fs/fscache/stats", S_IFREG | 0444, NULL,
			fscache_stats_show))
		goto error_stats;
#endif

#ifdef CONFIG_FSCACHE_HISTOGRAM
	if (!proc_create_seq("fs/fscache/histogram", S_IFREG | 0444, NULL,
			 &fscache_histogram_ops))
		goto error_histogram;
#endif

#ifdef CONFIG_FSCACHE_OBJECT_LIST
	if (!proc_create("fs/fscache/objects", S_IFREG | 0444, NULL,
			 &fscache_objlist_fops))
		goto error_objects;
#endif

	_leave(" = 0");
	return 0;

#ifdef CONFIG_FSCACHE_OBJECT_LIST
error_objects:
#endif
#ifdef CONFIG_FSCACHE_HISTOGRAM
	remove_proc_entry("fs/fscache/histogram", NULL);
error_histogram:
#endif
#ifdef CONFIG_FSCACHE_STATS
	remove_proc_entry("fs/fscache/stats", NULL);
error_stats:
#endif
	remove_proc_entry("fs/fscache", NULL);
error_dir:
	_leave(" = -ENOMEM");
	return -ENOMEM;
}

/*
 * clean up the /proc/fs/fscache/ directory
 */
void fscache_proc_cleanup(void)
{
#ifdef CONFIG_FSCACHE_OBJECT_LIST
	remove_proc_entry("fs/fscache/objects", NULL);
#endif
#ifdef CONFIG_FSCACHE_HISTOGRAM
	remove_proc_entry("fs/fscache/histogram", NULL);
#endif
#ifdef CONFIG_FSCACHE_STATS
	remove_proc_entry("fs/fscache/stats", NULL);
#endif
	remove_proc_entry("fs/fscache", NULL);
}
