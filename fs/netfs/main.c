// SPDX-License-Identifier: GPL-2.0-or-later
/* Miscellaneous bits for the netfs support library.
 *
 * Copyright (C) 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
#include <linux/export.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "internal.h"
#define CREATE_TRACE_POINTS
#include <trace/events/netfs.h>

MODULE_DESCRIPTION("Network fs support");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

unsigned netfs_debug;
module_param_named(debug, netfs_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(netfs_debug, "Netfs support debugging mask");

static int __init netfs_init(void)
{
	int ret = -ENOMEM;

	if (!proc_mkdir("fs/netfs", NULL))
		goto error;

#ifdef CONFIG_FSCACHE_STATS
	if (!proc_create_single("fs/netfs/stats", S_IFREG | 0444, NULL,
				netfs_stats_show))
		goto error_proc;
#endif

	ret = fscache_init();
	if (ret < 0)
		goto error_proc;
	return 0;

error_proc:
	remove_proc_entry("fs/netfs", NULL);
error:
	return ret;
}
fs_initcall(netfs_init);

static void __exit netfs_exit(void)
{
	fscache_exit();
	remove_proc_entry("fs/netfs", NULL);
}
module_exit(netfs_exit);
