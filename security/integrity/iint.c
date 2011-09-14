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
#include "integrity.h"

static struct rb_root integrity_iint_tree = RB_ROOT;
static DEFINE_SPINLOCK(integrity_iint_lock);
static struct kmem_cache *iint_cache __read_mostly;

int iint_initialized;

/*
 * __integrity_iint_find - return the iint associated with an inode
 */
static struct integrity_iint_cache *__integrity_iint_find(struct inode *inode)
{
	struct integrity_iint_cache *iint;
	struct rb_node *n = integrity_iint_tree.rb_node;

	assert_spin_locked(&integrity_iint_lock);

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

	spin_lock(&integrity_iint_lock);
	iint = __integrity_iint_find(inode);
	spin_unlock(&integrity_iint_lock);

	return iint;
}

static void iint_free(struct integrity_iint_cache *iint)
{
	iint->version = 0;
	iint->flags = 0UL;
	iint->evm_status = INTEGRITY_UNKNOWN;
	kmem_cache_free(iint_cache, iint);
}

/**
 * integrity_inode_alloc - allocate an iint associated with an inode
 * @inode: pointer to the inode
 */
int integrity_inode_alloc(struct inode *inode)
{
	struct rb_node **p;
	struct rb_node *new_node, *parent = NULL;
	struct integrity_iint_cache *new_iint, *test_iint;
	int rc;

	new_iint = kmem_cache_alloc(iint_cache, GFP_NOFS);
	if (!new_iint)
		return -ENOMEM;

	new_iint->inode = inode;
	new_node = &new_iint->rb_node;

	mutex_lock(&inode->i_mutex);	/* i_flags */
	spin_lock(&integrity_iint_lock);

	p = &integrity_iint_tree.rb_node;
	while (*p) {
		parent = *p;
		test_iint = rb_entry(parent, struct integrity_iint_cache,
				     rb_node);
		rc = -EEXIST;
		if (inode < test_iint->inode)
			p = &(*p)->rb_left;
		else if (inode > test_iint->inode)
			p = &(*p)->rb_right;
		else
			goto out_err;
	}

	inode->i_flags |= S_IMA;
	rb_link_node(new_node, parent, p);
	rb_insert_color(new_node, &integrity_iint_tree);

	spin_unlock(&integrity_iint_lock);
	mutex_unlock(&inode->i_mutex);	/* i_flags */

	return 0;
out_err:
	spin_unlock(&integrity_iint_lock);
	mutex_unlock(&inode->i_mutex);	/* i_flags */
	iint_free(new_iint);

	return rc;
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

	spin_lock(&integrity_iint_lock);
	iint = __integrity_iint_find(inode);
	rb_erase(&iint->rb_node, &integrity_iint_tree);
	spin_unlock(&integrity_iint_lock);

	iint_free(iint);
}

static void init_once(void *foo)
{
	struct integrity_iint_cache *iint = foo;

	memset(iint, 0, sizeof *iint);
	iint->version = 0;
	iint->flags = 0UL;
	mutex_init(&iint->mutex);
	iint->evm_status = INTEGRITY_UNKNOWN;
}

static int __init integrity_iintcache_init(void)
{
	iint_cache =
	    kmem_cache_create("iint_cache", sizeof(struct integrity_iint_cache),
			      0, SLAB_PANIC, init_once);
	iint_initialized = 1;
	return 0;
}
security_initcall(integrity_iintcache_init);
