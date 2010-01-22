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
 * File: ima_iint.c
 * 	- implements the IMA hooks: ima_inode_alloc, ima_inode_free
 *	- cache integrity information associated with an inode
 *	  using a radix tree.
 */
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/radix-tree.h>
#include "ima.h"

RADIX_TREE(ima_iint_store, GFP_ATOMIC);
DEFINE_SPINLOCK(ima_iint_lock);

static struct kmem_cache *iint_cache __read_mostly;

/* ima_iint_find_get - return the iint associated with an inode
 *
 * ima_iint_find_get gets a reference to the iint. Caller must
 * remember to put the iint reference.
 */
struct ima_iint_cache *ima_iint_find_get(struct inode *inode)
{
	struct ima_iint_cache *iint;

	rcu_read_lock();
	iint = radix_tree_lookup(&ima_iint_store, (unsigned long)inode);
	if (!iint)
		goto out;
	kref_get(&iint->refcount);
out:
	rcu_read_unlock();
	return iint;
}

/**
 * ima_inode_alloc - allocate an iint associated with an inode
 * @inode: pointer to the inode
 */
int ima_inode_alloc(struct inode *inode)
{
	struct ima_iint_cache *iint = NULL;
	int rc = 0;

	if (!ima_initialized)
		return 0;

	iint = kmem_cache_alloc(iint_cache, GFP_NOFS);
	if (!iint)
		return -ENOMEM;

	rc = radix_tree_preload(GFP_NOFS);
	if (rc < 0)
		goto out;

	spin_lock(&ima_iint_lock);
	rc = radix_tree_insert(&ima_iint_store, (unsigned long)inode, iint);
	spin_unlock(&ima_iint_lock);
out:
	if (rc < 0)
		kmem_cache_free(iint_cache, iint);

	radix_tree_preload_end();

	return rc;
}

/* iint_free - called when the iint refcount goes to zero */
void iint_free(struct kref *kref)
{
	struct ima_iint_cache *iint = container_of(kref, struct ima_iint_cache,
						   refcount);
	iint->version = 0;
	iint->flags = 0UL;
	if (iint->readcount != 0) {
		printk(KERN_INFO "%s: readcount: %ld\n", __FUNCTION__,
		       iint->readcount);
		iint->readcount = 0;
	}
	if (iint->writecount != 0) {
		printk(KERN_INFO "%s: writecount: %ld\n", __FUNCTION__,
		       iint->writecount);
		iint->writecount = 0;
	}
	if (iint->opencount != 0) {
		printk(KERN_INFO "%s: opencount: %ld\n", __FUNCTION__,
		       iint->opencount);
		iint->opencount = 0;
	}
	kref_set(&iint->refcount, 1);
	kmem_cache_free(iint_cache, iint);
}

void iint_rcu_free(struct rcu_head *rcu_head)
{
	struct ima_iint_cache *iint = container_of(rcu_head,
						   struct ima_iint_cache, rcu);
	kref_put(&iint->refcount, iint_free);
}

/**
 * ima_inode_free - called on security_inode_free
 * @inode: pointer to the inode
 *
 * Free the integrity information(iint) associated with an inode.
 */
void ima_inode_free(struct inode *inode)
{
	struct ima_iint_cache *iint;

	if (!ima_initialized)
		return;
	spin_lock(&ima_iint_lock);
	iint = radix_tree_delete(&ima_iint_store, (unsigned long)inode);
	spin_unlock(&ima_iint_lock);
	if (iint)
		call_rcu(&iint->rcu, iint_rcu_free);
}

static void init_once(void *foo)
{
	struct ima_iint_cache *iint = foo;

	memset(iint, 0, sizeof *iint);
	iint->version = 0;
	iint->flags = 0UL;
	mutex_init(&iint->mutex);
	iint->readcount = 0;
	iint->writecount = 0;
	iint->opencount = 0;
	kref_set(&iint->refcount, 1);
}

void __init ima_iintcache_init(void)
{
	iint_cache =
	    kmem_cache_create("iint_cache", sizeof(struct ima_iint_cache), 0,
			      SLAB_PANIC, init_once);
}
