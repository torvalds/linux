/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "ops_fstype.h"
#include "sys.h"

/**
 * init_gfs2_fs - Register GFS2 as a filesystem
 *
 * Returns: 0 on success, error code on failure
 */

static int __init init_gfs2_fs(void)
{
	int error;

	gfs2_init_lmh();

	error = gfs2_sys_init();
	if (error)
		return error;

	error = -ENOMEM;

	gfs2_glock_cachep = kmem_cache_create("gfs2_glock",
					      sizeof(struct gfs2_glock),
					      0, 0, NULL, NULL);
	if (!gfs2_glock_cachep)
		goto fail;

	gfs2_inode_cachep = kmem_cache_create("gfs2_inode",
					      sizeof(struct gfs2_inode),
					      0, 0, NULL, NULL);
	if (!gfs2_inode_cachep)
		goto fail;

	gfs2_bufdata_cachep = kmem_cache_create("gfs2_bufdata",
						sizeof(struct gfs2_bufdata),
					        0, 0, NULL, NULL);
	if (!gfs2_bufdata_cachep)
		goto fail;

	error = register_filesystem(&gfs2_fs_type);
	if (error)
		goto fail;

	printk("GFS2 (built %s %s) installed\n", __DATE__, __TIME__);

	return 0;

 fail:
	if (gfs2_bufdata_cachep)
		kmem_cache_destroy(gfs2_bufdata_cachep);

	if (gfs2_inode_cachep)
		kmem_cache_destroy(gfs2_inode_cachep);

	if (gfs2_glock_cachep)
		kmem_cache_destroy(gfs2_glock_cachep);

	gfs2_sys_uninit();
	return error;
}

/**
 * exit_gfs2_fs - Unregister the file system
 *
 */

static void __exit exit_gfs2_fs(void)
{
	unregister_filesystem(&gfs2_fs_type);

	kmem_cache_destroy(gfs2_bufdata_cachep);
	kmem_cache_destroy(gfs2_inode_cachep);
	kmem_cache_destroy(gfs2_glock_cachep);

	gfs2_sys_uninit();
}

MODULE_DESCRIPTION("Global File System");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

module_init(init_gfs2_fs);
module_exit(exit_gfs2_fs);

