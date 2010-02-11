/* General filesystem local caching manager
 *
 * Copyright (C) 2004-2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include "internal.h"

MODULE_DESCRIPTION("FS Cache Manager");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

unsigned fscache_defer_lookup = 1;
module_param_named(defer_lookup, fscache_defer_lookup, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(fscache_defer_lookup,
		 "Defer cookie lookup to background thread");

unsigned fscache_defer_create = 1;
module_param_named(defer_create, fscache_defer_create, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(fscache_defer_create,
		 "Defer cookie creation to background thread");

unsigned fscache_debug;
module_param_named(debug, fscache_debug, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(fscache_debug,
		 "FS-Cache debugging mask");

struct kobject *fscache_root;

/*
 * initialise the fs caching module
 */
static int __init fscache_init(void)
{
	int ret;

	ret = slow_work_register_user(THIS_MODULE);
	if (ret < 0)
		goto error_slow_work;

	ret = fscache_proc_init();
	if (ret < 0)
		goto error_proc;

	fscache_cookie_jar = kmem_cache_create("fscache_cookie_jar",
					       sizeof(struct fscache_cookie),
					       0,
					       0,
					       fscache_cookie_init_once);
	if (!fscache_cookie_jar) {
		printk(KERN_NOTICE
		       "FS-Cache: Failed to allocate a cookie jar\n");
		ret = -ENOMEM;
		goto error_cookie_jar;
	}

	fscache_root = kobject_create_and_add("fscache", kernel_kobj);
	if (!fscache_root)
		goto error_kobj;

	printk(KERN_NOTICE "FS-Cache: Loaded\n");
	return 0;

error_kobj:
	kmem_cache_destroy(fscache_cookie_jar);
error_cookie_jar:
	fscache_proc_cleanup();
error_proc:
	slow_work_unregister_user(THIS_MODULE);
error_slow_work:
	return ret;
}

fs_initcall(fscache_init);

/*
 * clean up on module removal
 */
static void __exit fscache_exit(void)
{
	_enter("");

	kobject_put(fscache_root);
	kmem_cache_destroy(fscache_cookie_jar);
	fscache_proc_cleanup();
	slow_work_unregister_user(THIS_MODULE);
	printk(KERN_NOTICE "FS-Cache: Unloaded\n");
}

module_exit(fscache_exit);

/*
 * wait_on_bit() sleep function for uninterruptible waiting
 */
int fscache_wait_bit(void *flags)
{
	schedule();
	return 0;
}
EXPORT_SYMBOL(fscache_wait_bit);

/*
 * wait_on_bit() sleep function for interruptible waiting
 */
int fscache_wait_bit_interruptible(void *flags)
{
	schedule();
	return signal_pending(current);
}
EXPORT_SYMBOL(fscache_wait_bit_interruptible);
