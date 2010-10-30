/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfs2_ondisk.h>
#include <asm/atomic.h>

#include "gfs2.h"
#include "incore.h"
#include "super.h"
#include "sys.h"
#include "util.h"
#include "glock.h"
#include "quota.h"
#include "recovery.h"
#include "dir.h"

static struct shrinker qd_shrinker = {
	.shrink = gfs2_shrink_qd_memory,
	.seeks = DEFAULT_SEEKS,
};

static void gfs2_init_inode_once(void *foo)
{
	struct gfs2_inode *ip = foo;

	inode_init_once(&ip->i_inode);
	init_rwsem(&ip->i_rw_mutex);
	INIT_LIST_HEAD(&ip->i_trunc_list);
	ip->i_alloc = NULL;
}

static void gfs2_init_glock_once(void *foo)
{
	struct gfs2_glock *gl = foo;

	INIT_HLIST_NODE(&gl->gl_list);
	spin_lock_init(&gl->gl_spin);
	INIT_LIST_HEAD(&gl->gl_holders);
	INIT_LIST_HEAD(&gl->gl_lru);
	INIT_LIST_HEAD(&gl->gl_ail_list);
	atomic_set(&gl->gl_ail_count, 0);
}

static void gfs2_init_gl_aspace_once(void *foo)
{
	struct gfs2_glock *gl = foo;
	struct address_space *mapping = (struct address_space *)(gl + 1);

	gfs2_init_glock_once(gl);
	memset(mapping, 0, sizeof(*mapping));
	INIT_RADIX_TREE(&mapping->page_tree, GFP_ATOMIC);
	spin_lock_init(&mapping->tree_lock);
	spin_lock_init(&mapping->i_mmap_lock);
	INIT_LIST_HEAD(&mapping->private_list);
	spin_lock_init(&mapping->private_lock);
	INIT_RAW_PRIO_TREE_ROOT(&mapping->i_mmap);
	INIT_LIST_HEAD(&mapping->i_mmap_nonlinear);
}

/**
 * init_gfs2_fs - Register GFS2 as a filesystem
 *
 * Returns: 0 on success, error code on failure
 */

static int __init init_gfs2_fs(void)
{
	int error;

	gfs2_str2qstr(&gfs2_qdot, ".");
	gfs2_str2qstr(&gfs2_qdotdot, "..");

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
					      gfs2_init_glock_once);
	if (!gfs2_glock_cachep)
		goto fail;

	gfs2_glock_aspace_cachep = kmem_cache_create("gfs2_glock(aspace)",
					sizeof(struct gfs2_glock) +
					sizeof(struct address_space),
					0, 0, gfs2_init_gl_aspace_once);

	if (!gfs2_glock_aspace_cachep)
		goto fail;

	gfs2_inode_cachep = kmem_cache_create("gfs2_inode",
					      sizeof(struct gfs2_inode),
					      0,  SLAB_RECLAIM_ACCOUNT|
					          SLAB_MEM_SPREAD,
					      gfs2_init_inode_once);
	if (!gfs2_inode_cachep)
		goto fail;

	gfs2_bufdata_cachep = kmem_cache_create("gfs2_bufdata",
						sizeof(struct gfs2_bufdata),
					        0, 0, NULL);
	if (!gfs2_bufdata_cachep)
		goto fail;

	gfs2_rgrpd_cachep = kmem_cache_create("gfs2_rgrpd",
					      sizeof(struct gfs2_rgrpd),
					      0, 0, NULL);
	if (!gfs2_rgrpd_cachep)
		goto fail;

	gfs2_quotad_cachep = kmem_cache_create("gfs2_quotad",
					       sizeof(struct gfs2_quota_data),
					       0, 0, NULL);
	if (!gfs2_quotad_cachep)
		goto fail;

	register_shrinker(&qd_shrinker);

	error = register_filesystem(&gfs2_fs_type);
	if (error)
		goto fail;

	error = register_filesystem(&gfs2meta_fs_type);
	if (error)
		goto fail_unregister;

	error = -ENOMEM;
	gfs_recovery_wq = alloc_workqueue("gfs_recovery",
					  WQ_MEM_RECLAIM | WQ_FREEZEABLE, 0);
	if (!gfs_recovery_wq)
		goto fail_wq;

	gfs2_register_debugfs();

	printk("GFS2 (built %s %s) installed\n", __DATE__, __TIME__);

	return 0;

fail_wq:
	unregister_filesystem(&gfs2meta_fs_type);
fail_unregister:
	unregister_filesystem(&gfs2_fs_type);
fail:
	unregister_shrinker(&qd_shrinker);
	gfs2_glock_exit();

	if (gfs2_quotad_cachep)
		kmem_cache_destroy(gfs2_quotad_cachep);

	if (gfs2_rgrpd_cachep)
		kmem_cache_destroy(gfs2_rgrpd_cachep);

	if (gfs2_bufdata_cachep)
		kmem_cache_destroy(gfs2_bufdata_cachep);

	if (gfs2_inode_cachep)
		kmem_cache_destroy(gfs2_inode_cachep);

	if (gfs2_glock_aspace_cachep)
		kmem_cache_destroy(gfs2_glock_aspace_cachep);

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
	unregister_shrinker(&qd_shrinker);
	gfs2_glock_exit();
	gfs2_unregister_debugfs();
	unregister_filesystem(&gfs2_fs_type);
	unregister_filesystem(&gfs2meta_fs_type);
	destroy_workqueue(gfs_recovery_wq);

	kmem_cache_destroy(gfs2_quotad_cachep);
	kmem_cache_destroy(gfs2_rgrpd_cachep);
	kmem_cache_destroy(gfs2_bufdata_cachep);
	kmem_cache_destroy(gfs2_inode_cachep);
	kmem_cache_destroy(gfs2_glock_aspace_cachep);
	kmem_cache_destroy(gfs2_glock_cachep);

	gfs2_sys_uninit();
}

MODULE_DESCRIPTION("Global File System");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

module_init(init_gfs2_fs);
module_exit(exit_gfs2_fs);

