// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * File: integrity_iint.c
 *	- implements the integrity hooks: integrity_inode_alloc,
 *	  integrity_inode_free
 *	- cache integrity information associated with an inode
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
 * __integrity_iint_find - return the iint associated with an inode
 */
static struct integrity_iint_cache *__integrity_iint_find(struct inode *inode)
{
	struct integrity_iint_cache *iint;
	struct rb_node *n = integrity_iint_tree.rb_node;

	while (n) {
		iint = rb_entry(n, struct integrity_iint_cache, rb_node);

		if (inode < iint->inode)
			n = n->rb_left;
		else if (inode > iint->inode)
			n = n->rb_right;
		else
			break;
	}
	if (!n)
		return NULL;

	return iint;
}

/*
 * integrity_iint_find - return the iint associated with an inode
 */
struct integrity_iint_cache *integrity_iint_find(struct inode *inode)
{
	struct integrity_iint_cache *iint;

	if (!IS_IMA(inode))
		return NULL;

	read_lock(&integrity_iint_lock);
	iint = __integrity_iint_find(inode);
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
 * integrity_inode_get - find or allocate an iint associated with an inode
 * @inode: pointer to the inode
 * @return: allocated iint
 *
 * Caller must lock i_mutex
 */
struct integrity_iint_cache *integrity_inode_get(struct inode *inode)
{
	struct rb_node **p;
	struct rb_node *node, *parent = NULL;
	struct integrity_iint_cache *iint, *test_iint;

	/*
	 * The integrity's "iint_cache" is initialized at security_init(),
	 * unless it is not included in the ordered list of LSMs enabled
	 * on the boot command line.
	 */
	if (!iint_cache)
		panic("%s: lsm=integrity required.\n", __func__);

	iint = integrity_iint_find(inode);
	if (iint)
		return iint;

	iint = kmem_cache_alloc(iint_cache, GFP_NOFS);
	if (!iint)
		return NULL;

	write_lock(&integrity_iint_lock);

	p = &integrity_iint_tree.rb_node;
	while (*p) {
		parent = *p;
		test_iint = rb_entry(parent, struct integrity_iint_cache,
				     rb_node);
		if (inode < test_iint->inode)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	iint->inode = inode;
	node = &iint->rb_node;
	inode->i_flags |= S_IMA;
	rb_link_node(node, parent, p);
	rb_insert_color(node, &integrity_iint_tree);

	write_unlock(&integrity_iint_lock);
	return iint;
}

/**
 * integrity_inode_free - called on security_inode_free
 * @inode: pointer to the inode
 *
 * Free the integrity information(iint) associated with an inode.
 */
void integrity_inode_free(struct inode *inode)
{
	struct integrity_iint_cache *iint;

	if (!IS_IMA(inode))
		return;

	write_lock(&integrity_iint_lock);
	iint = __integrity_iint_find(inode);
	rb_erase(&iint->rb_node, &integrity_iint_tree);
	write_unlock(&integrity_iint_lock);

	iint_free(iint);
}

static void init_once(void *foo)
{
	struct integrity_iint_cache *iint = (struct integrity_iint_cache *) foo;

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
 * It does not perform locking checks to ensure it cannot be blocked.
 * It does not perform security checks because it is irrelevant for IMA.
 *
 */
int integrity_kernel_read(struct file *file, loff_t offset,
			  void *addr, unsigned long count)
{
	return __kernel_read(file, addr, count, &offset);
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

	if (!IS_ENABLED(CONFIG_IMA_LOAD_X509))
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
