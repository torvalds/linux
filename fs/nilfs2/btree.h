/*
 * btree.h - NILFS B-tree.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 */

#ifndef _NILFS_BTREE_H
#define _NILFS_BTREE_H

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/list.h>
#include <linux/nilfs2_fs.h>
#include "btnode.h"
#include "bmap.h"

struct nilfs_btree;
struct nilfs_btree_path;

/**
 * struct nilfs_btree_operations - B-tree operation table
 */
struct nilfs_btree_operations {
	__u64 (*btop_find_target)(const struct nilfs_btree *,
				  const struct nilfs_btree_path *, __u64);
	void (*btop_set_target)(struct nilfs_btree *, __u64, __u64);

	struct the_nilfs *(*btop_get_nilfs)(struct nilfs_btree *);

	int (*btop_propagate)(struct nilfs_btree *,
			      struct nilfs_btree_path *,
			      int,
			      struct buffer_head *);
	int (*btop_assign)(struct nilfs_btree *,
			   struct nilfs_btree_path *,
			   int,
			   struct buffer_head **,
			   sector_t,
			   union nilfs_binfo *);
};

/**
 * struct nilfs_btree_node - B-tree node
 * @bn_flags: flags
 * @bn_level: level
 * @bn_nchildren: number of children
 * @bn_pad: padding
 */
struct nilfs_btree_node {
	__u8 bn_flags;
	__u8 bn_level;
	__le16 bn_nchildren;
	__le32 bn_pad;
};

/* flags */
#define NILFS_BTREE_NODE_ROOT	0x01

/* level */
#define NILFS_BTREE_LEVEL_DATA		0
#define NILFS_BTREE_LEVEL_NODE_MIN	(NILFS_BTREE_LEVEL_DATA + 1)
#define NILFS_BTREE_LEVEL_MAX		14

/**
 * struct nilfs_btree - B-tree structure
 * @bt_bmap: bmap base structure
 * @bt_ops: B-tree operation table
 */
struct nilfs_btree {
	struct nilfs_bmap bt_bmap;

	/* B-tree-specific members */
	const struct nilfs_btree_operations *bt_ops;
};


#define NILFS_BTREE_ROOT_SIZE		NILFS_BMAP_SIZE
#define NILFS_BTREE_ROOT_NCHILDREN_MAX					\
	((NILFS_BTREE_ROOT_SIZE - sizeof(struct nilfs_btree_node)) /	\
	 (sizeof(__le64 /* dkey */) + sizeof(__le64 /* dptr */)))
#define NILFS_BTREE_ROOT_NCHILDREN_MIN	0
#define NILFS_BTREE_NODE_EXTRA_PAD_SIZE	(sizeof(__le64))
#define NILFS_BTREE_NODE_NCHILDREN_MAX(nodesize)			\
	(((nodesize) - sizeof(struct nilfs_btree_node) -		\
		NILFS_BTREE_NODE_EXTRA_PAD_SIZE) /			\
	 (sizeof(__le64 /* dkey */) + sizeof(__le64 /* dptr */)))
#define NILFS_BTREE_NODE_NCHILDREN_MIN(nodesize)			\
	((NILFS_BTREE_NODE_NCHILDREN_MAX(nodesize) - 1) / 2 + 1)
#define NILFS_BTREE_KEY_MIN	((__u64)0)
#define NILFS_BTREE_KEY_MAX	(~(__u64)0)


int nilfs_btree_path_cache_init(void);
void nilfs_btree_path_cache_destroy(void);
int nilfs_btree_init(struct nilfs_bmap *, __u64, __u64);
int nilfs_btree_convert_and_insert(struct nilfs_bmap *, __u64, __u64,
				   const __u64 *, const __u64 *,
				   int, __u64, __u64);
void nilfs_btree_init_gc(struct nilfs_bmap *);

#endif	/* _NILFS_BTREE_H */
