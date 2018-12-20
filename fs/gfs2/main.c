/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfs2_ondisk.h>
#include <linux/rcupdate.h>
#include <linux/rculist_bl.h>
#include <linux/atomic.h>
#include <linux/mempool.h>

#include "gfs2.h"
#include "incore.h"
#include "super.h"
#include "sys.h"
#include "util.h"
#include "glock.h"
#include "quota.h"
#include "recovery.h"
#include "dir.h"
#include "glops.h"

struct workqueue_struct *gfs2_control_wq;

static void gfs2_init_inode_once(void *foo)
{
	struct gfs2_inode *ip = foo;

	inode_init_once(&ip->i_inode);
	atomic_set(&ip->i_sizehint, 0);
	init_rwsem(&ip->i_rw_mutex);
	INIT_LIST_HEAD(&ip->i_trunc_list);
	ip->i_qadata = NULL;
	gfs2_holder_mark_uninitialized(&ip->i_rgd_gh);
	memset(&ip->i_res, 0, sizeof(ip->i_res));
	RB_CLEAR_NODE(&ip->i_res.rs_node);
	ip->i_hash_cache = NULL;
	gfs2_holder_mark_uninitialized(&ip->i_iopen_gh);
}

static void gfs2_init_glock_once(void *foo)
{
	struct gfs2_glock *gl = foo;

	spin_lock_init(&gl->gl_lockref.lock);
	INIT_LIST_HEAD(&gl->gl_holders);
	INIT_LIST_HEAD(&gl->gl_lru);
	INIT_LIST_HEAD(&gl->gl_ail_list);
	atomic_set(&gl->gl_ail_count, 0);
	atomic_set(&gl->gl_revokes, 0);
}

static void gfs2_init_gl_aspace_once(void *foo)
{
	struct gfs2_glock *gl = foo;
	struct address_space *mapping = (struct address_space *)(gl + 1);

	gfs2_init_glock_once(gl);
	address_space_init_once(mapping);
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
	gfs2_quota_hash_init();

	error = gfs2_sys_init();
	if (error)
		return error;

	error = list_lru_init(&gfs2_qd_lru);
	if (error)
		goto fail_lru;

	error = gfs2_glock_init();
	if (error)
		goto fail_glock;

	error = -ENOMEM;
	gfs2_glock_cachep = kmem_cache_create("gfs2_glock",
					      sizeof(struct gfs2_glock),
					      0, 0,
					      gfs2_init_glock_once);
	if (!gfs2_glock_cachep)
		goto fail_cachep1;

	gfs2_glock_aspace_cachep = kmem_cache_create("gfs2_glock(aspace)",
					sizeof(struct gfs2_glock) +
					sizeof(struct address_space),
					0, 0, gfs2_init_gl_aspace_once);

	if (!gfs2_glock_aspace_cachep)
		goto fail_cachep2;

	gfs2_inode_cachep = kmem_cache_create("gfs2_inode",
					      sizeof(struct gfs2_inode),
					      0,  SLAB_RECLAIM_ACCOUNT|
						  SLAB_MEM_SPREAD|
						  SLAB_ACCOUNT,
					      gfs2_init_inode_once);
	if (!gfs2_inode_cachep)
		goto fail_cachep3;

	gfs2_bufdata_cachep = kmem_cache_create("gfs2_bufdata",
						sizeof(struct gfs2_bufdata),
					        0, 0, NULL);
	if (!gfs2_bufdata_cachep)
		goto fail_cachep4;

	gfs2_rgrpd_cachep = kmem_cache_create("gfs2_rgrpd",
					      sizeof(struct gfs2_rgrpd),
					      0, 0, NULL);
	if (!gfs2_rgrpd_cachep)
		goto fail_cachep5;

	gfs2_quotad_cachep = kmem_cache_create("gfs2_quotad",
					       sizeof(struct gfs2_quota_data),
					       0, 0, NULL);
	if (!gfs2_quotad_cachep)
		goto fail_cachep6;

	gfs2_qadata_cachep = kmem_cache_create("gfs2_qadata",
					       sizeof(struct gfs2_qadata),
					       0, 0, NULL);
	if (!gfs2_qadata_cachep)
		goto fail_cachep7;

	error = register_shrinker(&gfs2_qd_shrinker);
	if (error)
		goto fail_shrinker;

	error = register_filesystem(&gfs2_fs_type);
	if (error)
		goto fail_fs1;

	error = register_filesystem(&gfs2meta_fs_type);
	if (error)
		goto fail_fs2;

	error = -ENOMEM;
	gfs_recovery_wq = alloc_workqueue("gfs_recovery",
					  WQ_MEM_RECLAIM | WQ_FREEZABLE, 0);
	if (!gfs_recovery_wq)
		goto fail_wq1;

	gfs2_control_wq = alloc_workqueue("gfs2_control",
					  WQ_UNBOUND | WQ_FREEZABLE, 0);
	if (!gfs2_control_wq)
		goto fail_wq2;

	gfs2_freeze_wq = alloc_workqueue("freeze_workqueue", 0, 0);

	if (!gfs2_freeze_wq)
		goto fail_wq3;

	gfs2_page_pool = mempool_create_page_pool(64, 0);
	if (!gfs2_page_pool)
		goto fail_mempool;

	error = gfs2_register_debugfs();
	if (error)
		goto fail_debugfs;

	pr_info("GFS2 installed\n");

	return 0;

fail_debugfs:
	mempool_destroy(gfs2_page_pool);
fail_mempool:
	destroy_workqueue(gfs2_freeze_wq);
fail_wq3:
	destroy_workqueue(gfs2_control_wq);
fail_wq2:
	destroy_workqueue(gfs_recovery_wq);
fail_wq1:
	unregister_filesystem(&gfs2meta_fs_type);
fail_fs2:
	unregister_filesystem(&gfs2_fs_type);
fail_fs1:
	unregister_shrinker(&gfs2_qd_shrinker);
fail_shrinker:
	kmem_cache_destroy(gfs2_qadata_cachep);
fail_cachep7:
	kmem_cache_destroy(gfs2_quotad_cachep);
fail_cachep6:
	kmem_cache_destroy(gfs2_rgrpd_cachep);
fail_cachep5:
	kmem_cache_destroy(gfs2_bufdata_cachep);
fail_cachep4:
	kmem_cache_destroy(gfs2_inode_cachep);
fail_cachep3:
	kmem_cache_destroy(gfs2_glock_aspace_cachep);
fail_cachep2:
	kmem_cache_destroy(gfs2_glock_cachep);
fail_cachep1:
	gfs2_glock_exit();
fail_glock:
	list_lru_destroy(&gfs2_qd_lru);
fail_lru:
	gfs2_sys_uninit();
	return error;
}

/**
 * exit_gfs2_fs - Unregister the file system
 *
 */

static void __exit exit_gfs2_fs(void)
{
	unregister_shrinker(&gfs2_qd_shrinker);
	gfs2_glock_exit();
	gfs2_unregister_debugfs();
	unregister_filesystem(&gfs2_fs_type);
	unregister_filesystem(&gfs2meta_fs_type);
	destroy_workqueue(gfs_recovery_wq);
	destroy_workqueue(gfs2_control_wq);
	destroy_workqueue(gfs2_freeze_wq);
	list_lru_destroy(&gfs2_qd_lru);

	rcu_barrier();

	mempool_destroy(gfs2_page_pool);
	kmem_cache_destroy(gfs2_qadata_cachep);
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

