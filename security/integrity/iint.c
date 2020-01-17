// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * File: integrity_iint.c
 *	- implements the integrity hooks: integrity_iyesde_alloc,
 *	  integrity_iyesde_free
 *	- cache integrity information associated with an iyesde
 *	  using a rbtree tree.
 */
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/security.h>
#include <linux/lsm_hooks.h>
#include "integrity.h"

static struct rb_root integrity_iint_tree = RB_ROOT;
static DEFINE_RWLOCK(integrity_iint_lock);
static struct kmem_cache *iint_cache __read_mostly;

struct dentry *integrity_dir;

/*
 * __integrity_iint_find - return the iint associated with an iyesde
 */
static struct integrity_iint_cache *__integrity_iint_find(struct iyesde *iyesde)
{
	struct integrity_iint_cache *iint;
	struct rb_yesde *n = integrity_iint_tree.rb_yesde;

	while (n) {
		iint = rb_entry(n, struct integrity_iint_cache, rb_yesde);

		if (iyesde < iint->iyesde)
			n = n->rb_left;
		else if (iyesde > iint->iyesde)
			n = n->rb_right;
		else
			break;
	}
	if (!n)
		return NULL;

	return iint;
}

/*
 * integrity_iint_find - return the iint associated with an iyesde
 */
struct integrity_iint_cache *integrity_iint_find(struct iyesde *iyesde)
{
	struct integrity_iint_cache *iint;

	if (!IS_IMA(iyesde))
		return NULL;

	read_lock(&integrity_iint_lock);
	iint = __integrity_iint_find(iyesde);
	read_unlock(&integrity_iint_lock);

	return iint;
}

static void iint_free(struct integrity_iint_cache *iint)
{
	kfree(iint->ima_hash);
	iint->ima_hash = NULL;
	iint->version = 0;
	iint->flags = 0UL;
	iint->atomic_flags = 0UL;
	iint->ima_file_status = INTEGRITY_UNKNOWN;
	iint->ima_mmap_status = INTEGRITY_UNKNOWN;
	iint->ima_bprm_status = INTEGRITY_UNKNOWN;
	iint->ima_read_status = INTEGRITY_UNKNOWN;
	iint->ima_creds_status = INTEGRITY_UNKNOWN;
	iint->evm_status = INTEGRITY_UNKNOWN;
	iint->measured_pcrs = 0;
	kmem_cache_free(iint_cache, iint);
}

/**
 * integrity_iyesde_get - find or allocate an iint associated with an iyesde
 * @iyesde: pointer to the iyesde
 * @return: allocated iint
 *
 * Caller must lock i_mutex
 */
struct integrity_iint_cache *integrity_iyesde_get(struct iyesde *iyesde)
{
	struct rb_yesde **p;
	struct rb_yesde *yesde, *parent = NULL;
	struct integrity_iint_cache *iint, *test_iint;

	iint = integrity_iint_find(iyesde);
	if (iint)
		return iint;

	iint = kmem_cache_alloc(iint_cache, GFP_NOFS);
	if (!iint)
		return NULL;

	write_lock(&integrity_iint_lock);

	p = &integrity_iint_tree.rb_yesde;
	while (*p) {
		parent = *p;
		test_iint = rb_entry(parent, struct integrity_iint_cache,
				     rb_yesde);
		if (iyesde < test_iint->iyesde)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	iint->iyesde = iyesde;
	yesde = &iint->rb_yesde;
	iyesde->i_flags |= S_IMA;
	rb_link_yesde(yesde, parent, p);
	rb_insert_color(yesde, &integrity_iint_tree);

	write_unlock(&integrity_iint_lock);
	return iint;
}

/**
 * integrity_iyesde_free - called on security_iyesde_free
 * @iyesde: pointer to the iyesde
 *
 * Free the integrity information(iint) associated with an iyesde.
 */
void integrity_iyesde_free(struct iyesde *iyesde)
{
	struct integrity_iint_cache *iint;

	if (!IS_IMA(iyesde))
		return;

	write_lock(&integrity_iint_lock);
	iint = __integrity_iint_find(iyesde);
	rb_erase(&iint->rb_yesde, &integrity_iint_tree);
	write_unlock(&integrity_iint_lock);

	iint_free(iint);
}

static void init_once(void *foo)
{
	struct integrity_iint_cache *iint = foo;

	memset(iint, 0, sizeof(*iint));
	iint->ima_file_status = INTEGRITY_UNKNOWN;
	iint->ima_mmap_status = INTEGRITY_UNKNOWN;
	iint->ima_bprm_status = INTEGRITY_UNKNOWN;
	iint->ima_read_status = INTEGRITY_UNKNOWN;
	iint->ima_creds_status = INTEGRITY_UNKNOWN;
	iint->evm_status = INTEGRITY_UNKNOWN;
	mutex_init(&iint->mutex);
}

static int __init integrity_iintcache_init(void)
{
	iint_cache =
	    kmem_cache_create("iint_cache", sizeof(struct integrity_iint_cache),
			      0, SLAB_PANIC, init_once);
	return 0;
}
DEFINE_LSM(integrity) = {
	.name = "integrity",
	.init = integrity_iintcache_init,
};


/*
 * integrity_kernel_read - read data from the file
 *
 * This is a function for reading file content instead of kernel_read().
 * It does yest perform locking checks to ensure it canyest be blocked.
 * It does yest perform security checks because it is irrelevant for IMA.
 *
 */
int integrity_kernel_read(struct file *file, loff_t offset,
			  void *addr, unsigned long count)
{
	mm_segment_t old_fs;
	char __user *buf = (char __user *)addr;
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = __vfs_read(file, buf, count, &offset);
	set_fs(old_fs);

	return ret;
}

/*
 * integrity_load_keys - load integrity keys hook
 *
 * Hooks is called from init/main.c:kernel_init_freeable()
 * when rootfs is ready
 */
void __init integrity_load_keys(void)
{
	ima_load_x509();
	evm_load_x509();
}

static int __init integrity_fs_init(void)
{
	integrity_dir = securityfs_create_dir("integrity", NULL);
	if (IS_ERR(integrity_dir)) {
		int ret = PTR_ERR(integrity_dir);

		if (ret != -ENODEV)
			pr_err("Unable to create integrity sysfs dir: %d\n",
			       ret);
		integrity_dir = NULL;
		return ret;
	}

	return 0;
}

late_initcall(integrity_fs_init)
