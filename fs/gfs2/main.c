/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>
#include <asm/atomic.h>

#include "gfs2.h"
#include "incore.h"
#include "ops_fstype.h"
#include "sys.h"
#include "util.h"
#include "glock.h"

static void gfs2_init_inode_once(void *foo, kmem_cache_t *cachep, unsigned long flags)
{
	struct gfs2_inode *ip = foo;
	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		inode_init_once(&ip->i_inode);
		spin_lock_init(&ip->i_spin);
		init_rwsem(&ip->i_rw_mutex);
		memset(ip->i_cache, 0, sizeof(ip->i_cache));
	}
}

static void gfs2_init_glock_once(void *foo, kmem_cache_t *cachep, unsigned long flags)
{
	struct gfs2_glock *gl = foo;
	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		INIT_HLIST_NODE(&gl->gl_list);
		spin_lock_init(&gl->gl_spin);
		INIT_LIST_HEAD(&gl->gl_holders);
		INIT_LIST_HEAD(&gl->gl_waiters1);
		INIT_LIST_HEAD(&gl->gl_waiters2);
		INIT_LIST_HEAD(&gl->gl_waiters3);
		gl->gl_lvb = NULL;
		atomic_set(&gl->gl_lvb_count, 0);
		INIT_LIST_HEAD(&gl->gl_reclaim);
		INIT_LIST_HEAD(&gl->gl_ail_list);
		atomic_set(&gl->gl_ail_count, 0);
	}
}

/**
 * init_gfs2_fs - Register GFS2 as a filesystem
 *
 * Returns: 0 on success, error code on failure
 */

static int __init init_gfs2_fs(void)
{
	int error;

	error = gfs2_sys_init();
	if (error)
		return error;

	error = gfs2_glock_init();
	if (error)
		goto fail;

	error = -ENOMEM;
	gfs2_glock_cachep = kmem_cache_create("gfs2_glock",
					      sizeof(struct gfs2_glock),
					      0, 0,
					      gfs2_init_glock_once, NULL);
	if (!gfs2_glock_cachep)
		goto fail;

	gfs2_inode_cachep = kmem_cache_create("gfs2_inode",
					      sizeof(struct gfs2_inode),
					      0,  SLAB_RECLAIM_ACCOUNT|
					          SLAB_MEM_SPREAD,
					      gfs2_init_inode_once, NULL);
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

	error = register_filesystem(&gfs2meta_fs_type);
	if (error)
		goto fail_unregister;

	printk("GFS2 (built %s %s) installed\n", __DATE__, __TIME__);

	return 0;

fail_unregister:
	unregister_filesystem(&gfs2_fs_type);
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
	unregister_filesystem(&gfs2meta_fs_type);

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

