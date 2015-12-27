/*
 * Copyright (C) 2008 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: integrity_iint.c
 *	- implements the integrity hooks: integrity_inode_alloc,
 *	  integrity_inode_free
 *	- cache integrity information associated with an inode
 *	  using a rbtree tree.
 */
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include "integrity.h"

static struct rb_root integrity_iint_tree = RB_ROOT;
static DEFINE_RWLOCK(integrity_iint_lock);
static struct kmem_cache *iint_cache __read_mostly;

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
	iint->ima_file_status = INTEGRITY_UNKNOWN;
	iint->ima_mmap_status = INTEGRITY_UNKNOWN;
	iint->ima_bprm_status = INTEGRITY_UNKNOWN;
	iint->ima_module_status = INTEGRITY_UNKNOWN;
	iint->evm_status = INTEGRITY_UNKNOWN;
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
	struct integrity_iint_cache *iint = foo;

	memset(iint, 0, sizeof(*iint));
	iint->version = 0;
	iint->flags = 0UL;
	iint->ima_file_status = INTEGRITY_UNKNOWN;
	iint->ima_mmap_status = INTEGRITY_UNKNOWN;
	iint->ima_bprm_status = INTEGRITY_UNKNOWN;
	iint->ima_module_status = INTEGRITY_UNKNOWN;
	iint->evm_status = INTEGRITY_UNKNOWN;
}

static int __init integrity_iintcache_init(void)
{
	iint_cache =
	    kmem_cache_create("iint_cache", sizeof(struct integrity_iint_cache),
			      0, SLAB_PANIC, init_once);
	return 0;
}
security_initcall(integrity_iintcache_init);


/*
 * integrity_kernel_read - read data from the file
 *
 * This is a function for reading file content instead of kernel_read().
 * It does not perform locking checks to ensure it cannot be blocked.
 * It does not perform security checks because it is irrelevant for IMA.
 *
 */
int integrity_kernel_read(struct file *file, loff_t offset,
			  char *addr, unsigned long count)
{
	mm_segment_t old_fs;
	char __user *buf = (char __user *)addr;
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;

	old_fs = get_fs();
	set_fs(get_ds());
	ret = __vfs_read(file, buf, count, &offset);
	set_fs(old_fs);

	return ret;
}

/*
 * integrity_read_file - read entire file content into the buffer
 *
 * This is function opens a file, allocates the buffer of required
 * size, read entire file content to the buffer and closes the file
 *
 * It is used only by init code.
 *
 */
int __init integrity_read_file(const char *path, char **data)
{
	struct file *file;
	loff_t size;
	char *buf;
	int rc = -EINVAL;

	if (!path || !*path)
		return -EINVAL;

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		pr_err("Unable to open file: %s (%d)", path, rc);
		return rc;
	}

	size = i_size_read(file_inode(file));
	if (size <= 0)
		goto out;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto out;
	}

	rc = integrity_kernel_read(file, 0, buf, size);
	if (rc < 0)
		kfree(buf);
	else if (rc != size)
		rc = -EIO;
	else
		*data = buf;
out:
	fput(file);
	return rc;
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
}
