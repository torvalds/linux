// SPDX-License-Identifier: GPL-2.0-or-later
/* Network filesystem caching backend to use cache files on a premounted
 * filesystem
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/statfs.h>
#include <linux/sysctl.h>
#include <linux/miscdevice.h>
#define CREATE_TRACE_POINTS
#include "internal.h"

unsigned cachefiles_debug;
module_param_named(debug, cachefiles_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(cachefiles_debug, "CacheFiles debugging mask");

MODULE_DESCRIPTION("Mounted-filesystem based cache");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

struct kmem_cache *cachefiles_object_jar;

static struct miscdevice cachefiles_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "cachefiles",
	.fops	= &cachefiles_daemon_fops,
};

static void cachefiles_object_init_once(void *_object)
{
	struct cachefiles_object *object = _object;

	memset(object, 0, sizeof(*object));
	spin_lock_init(&object->work_lock);
}

/*
 * initialise the fs caching module
 */
static int __init cachefiles_init(void)
{
	int ret;

	ret = misc_register(&cachefiles_dev);
	if (ret < 0)
		goto error_dev;

	/* create an object jar */
	ret = -ENOMEM;
	cachefiles_object_jar =
		kmem_cache_create("cachefiles_object_jar",
				  sizeof(struct cachefiles_object),
				  0,
				  SLAB_HWCACHE_ALIGN,
				  cachefiles_object_init_once);
	if (!cachefiles_object_jar) {
		pr_notice("Failed to allocate an object jar\n");
		goto error_object_jar;
	}

	pr_info("Loaded\n");
	return 0;

error_object_jar:
	misc_deregister(&cachefiles_dev);
error_dev:
	pr_err("failed to register: %d\n", ret);
	return ret;
}

fs_initcall(cachefiles_init);

/*
 * clean up on module removal
 */
static void __exit cachefiles_exit(void)
{
	pr_info("Unloading\n");

	kmem_cache_destroy(cachefiles_object_jar);
	misc_deregister(&cachefiles_dev);
}

module_exit(cachefiles_exit);
