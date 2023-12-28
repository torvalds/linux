// SPDX-License-Identifier: GPL-2.0-or-later
/* FS-Cache statistics viewing interface
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * Add files to /proc/fs/netfs/.
 */
int __init fscache_proc_init(void)
{
	if (!proc_symlink("fs/fscache", NULL, "../netfs"))
		goto error_sym;

	if (!proc_create_seq("fs/netfs/caches", S_IFREG | 0444, NULL,
			     &fscache_caches_seq_ops))
		goto error;

	if (!proc_create_seq("fs/netfs/volumes", S_IFREG | 0444, NULL,
			     &fscache_volumes_seq_ops))
		goto error;

	if (!proc_create_seq("fs/netfs/cookies", S_IFREG | 0444, NULL,
			     &fscache_cookies_seq_ops))
		goto error;
	return 0;

error:
	remove_proc_entry("fs/fscache", NULL);
error_sym:
	return -ENOMEM;
}

/*
 * Clean up the /proc/fs/fscache symlink.
 */
void fscache_proc_cleanup(void)
{
	remove_proc_subtree("fs/fscache", NULL);
}
