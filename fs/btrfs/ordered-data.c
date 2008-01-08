/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/gfp.h>
#include <linux/slab.h>
#include "ctree.h"
#include "transaction.h"
#include "btrfs_inode.h"

struct tree_entry {
	u64 root_objectid;
	u64 objectid;
	struct rb_node rb_node;
};

/*
 * returns > 0 if entry passed (root, objectid) is > entry,
 * < 0 if (root, objectid) < entry and zero if they are equal
 */
static int comp_entry(struct tree_entry *entry, u64 root_objectid,
		      u64 objectid)
{
	if (root_objectid < entry->root_objectid)
		return -1;
	if (root_objectid > entry->root_objectid)
		return 1;
	if (objectid < entry->objectid)
		return -1;
	if (objectid > entry->objectid)
		return 1;
	return 0;
}

static struct rb_node *tree_insert(struct rb_root *root, u64 root_objectid,
				   u64 objectid, struct rb_node *node)
{
	struct rb_node ** p = &root->rb_node;
	struct rb_node * parent = NULL;
	struct tree_entry *entry;
	int comp;

	while(*p) {
		parent = *p;
		entry = rb_entry(parent, struct tree_entry, rb_node);

		comp = comp_entry(entry, root_objectid, objectid);
		if (comp < 0)
			p = &(*p)->rb_left;
		else if (comp > 0)
			p = &(*p)->rb_right;
		else
			return parent;
	}

	rb_link_node(node, parent, p);
	rb_insert_color(node, root);
	return NULL;
}

static struct rb_node *__tree_search(struct rb_root *root, u64 root_objectid,
				     u64 objectid, struct rb_node **prev_ret)
{
	struct rb_node * n = root->rb_node;
	struct rb_node *prev = NULL;
	struct tree_entry *entry;
	struct tree_entry *prev_entry = NULL;
	int comp;

	while(n) {
		entry = rb_entry(n, struct tree_entry, rb_node);
		prev = n;
		prev_entry = entry;
		comp = comp_entry(entry, root_objectid, objectid);

		if (comp < 0)
			n = n->rb_left;
		else if (comp > 0)
			n = n->rb_right;
		else
			return n;
	}
	if (!prev_ret)
		return NULL;

	while(prev && comp_entry(prev_entry, root_objectid, objectid) >= 0) {
		prev = rb_next(prev);
		prev_entry = rb_entry(prev, struct tree_entry, rb_node);
	}
	*prev_ret = prev;
	return NULL;
}

static inline struct rb_node *tree_search(struct rb_root *root,
					  u64 root_objectid, u64 objectid)
{
	struct rb_node *prev;
	struct rb_node *ret;
	ret = __tree_search(root, root_objectid, objectid, &prev);
	if (!ret)
		return prev;
	return ret;
}

int btrfs_add_ordered_inode(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 root_objectid = root->root_key.objectid;
	u64 transid = root->fs_info->running_transaction->transid;
	struct tree_entry *entry;
	struct rb_node *node;
	struct btrfs_ordered_inode_tree *tree;

	if (transid <= BTRFS_I(inode)->ordered_trans)
		return 0;

	tree = &root->fs_info->running_transaction->ordered_inode_tree;

	read_lock(&tree->lock);
	node = __tree_search(&tree->tree, root_objectid, inode->i_ino, NULL);
	read_unlock(&tree->lock);
	if (node) {
		return 0;
	}

	entry = kmalloc(sizeof(*entry), GFP_NOFS);
	if (!entry)
		return -ENOMEM;

	write_lock(&tree->lock);
	entry->objectid = inode->i_ino;
	entry->root_objectid = root_objectid;

	node = tree_insert(&tree->tree, root_objectid,
			   inode->i_ino, &entry->rb_node);

	BTRFS_I(inode)->ordered_trans = transid;

	write_unlock(&tree->lock);
	if (node)
		kfree(entry);
	return 0;
}

int btrfs_find_first_ordered_inode(struct btrfs_ordered_inode_tree *tree,
				       u64 *root_objectid, u64 *objectid)
{
	struct tree_entry *entry;
	struct rb_node *node;

	write_lock(&tree->lock);
	node = tree_search(&tree->tree, *root_objectid, *objectid);
	if (!node) {
		write_unlock(&tree->lock);
		return 0;
	}
	entry = rb_entry(node, struct tree_entry, rb_node);

	while(comp_entry(entry, *root_objectid, *objectid) >= 0) {
		node = rb_next(node);
		if (!node)
			break;
		entry = rb_entry(node, struct tree_entry, rb_node);
	}
	if (!node) {
		write_unlock(&tree->lock);
		return 0;
	}

	*root_objectid = entry->root_objectid;
	*objectid = entry->objectid;
	write_unlock(&tree->lock);
	return 1;
}

int btrfs_find_del_first_ordered_inode(struct btrfs_ordered_inode_tree *tree,
				       u64 *root_objectid, u64 *objectid)
{
	struct tree_entry *entry;
	struct rb_node *node;

	write_lock(&tree->lock);
	node = tree_search(&tree->tree, *root_objectid, *objectid);
	if (!node) {
		write_unlock(&tree->lock);
		return 0;
	}

	entry = rb_entry(node, struct tree_entry, rb_node);
	while(comp_entry(entry, *root_objectid, *objectid) >= 0) {
		node = rb_next(node);
		if (!node)
			break;
		entry = rb_entry(node, struct tree_entry, rb_node);
	}
	if (!node) {
		write_unlock(&tree->lock);
		return 0;
	}

	*root_objectid = entry->root_objectid;
	*objectid = entry->objectid;
	rb_erase(node, &tree->tree);
	write_unlock(&tree->lock);
	kfree(entry);
	return 1;
}
