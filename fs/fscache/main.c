// SPDX-License-Identifier: GPL-2.0-or-later
/* General filesystem local caching manager
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include <linux/init.h>
#define CREATE_TRACE_POINTS
#include "internal.h"

MODULE_DESCRIPTION("FS Cache Manager");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

unsigned fscache_debug;
module_param_named(debug, fscache_debug, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(fscache_debug,
		 "FS-Cache debugging mask");

struct workqueue_struct *fscache_wq;
EXPORT_SYMBOL(fscache_wq);

/*
 * initialise the fs caching module
 */
static int __init fscache_init(void)
{
	int ret = -ENOMEM;

	fscache_wq = alloc_workqueue("fscache", WQ_UNBOUND | WQ_FREEZABLE, 0);
	if (!fscache_wq)
		goto error_wq;

	ret = fscache_proc_init();
	if (ret < 0)
		goto error_proc;

	pr_notice("Loaded\n");
	return 0;

error_proc:
	destroy_workqueue(fscache_wq);
error_wq:
	return ret;
}

fs_initcall(fscache_init);

/*
 * clean up on module removal
 */
static void __exit fscache_exit(void)
{
	_enter("");

	fscache_proc_cleanup();
	destroy_workqueue(fscache_wq);
	pr_notice("Unloaded\n");
}

module_exit(fscache_exit);
