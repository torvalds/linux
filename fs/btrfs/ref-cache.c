/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include "ctree.h"
#include "ref-cache.h"
#include "transaction.h"

static struct rb_node *tree_insert(struct rb_root *root, u64 bytenr,
				   struct rb_node *node)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_leaf_ref *entry;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct btrfs_leaf_ref, rb_node);

		if (bytenr < entry->bytenr)
			p = &(*p)->rb_left;
		else if (bytenr > entry->bytenr)
			p = &(*p)->rb_right;
		else
			return parent;
	}

	entry = rb_entry(node, struct btrfs_leaf_ref, rb_node);
	rb_link_node(node, parent, p);
	rb_insert_color(node, root);
	return NULL;
}

static struct rb_node *tree_search(struct rb_root *root, u64 bytenr)
{
	struct rb_node *n = root->rb_node;
	struct btrfs_leaf_ref *entry;

	while (n) {
		entry = rb_entry(n, struct btrfs_leaf_ref, rb_node);
		WARN_ON(!entry->in_tree);

		if (bytenr < entry->bytenr)
			n = n->rb_left;
		else if (bytenr > entry->bytenr)
			n = n->rb_right;
		else
			return n;
	}
	return NULL;
}
