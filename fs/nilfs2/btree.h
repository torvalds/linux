/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NILFS B-tree.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#ifndef _NILFS_BTREE_H
#define _NILFS_BTREE_H

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/list.h>
#include <linux/nilfs2_ondisk.h>	/* nilfs_btree_node */
#include "btnode.h"
#include "bmap.h"

/**
 * struct nilfs_btree_path - A path on which B-tree operations are executed
 * @bp_bh: buffer head of node block
 * @bp_sib_bh: buffer head of sibling node block
 * @bp_index: index of child node
 * @bp_oldreq: ptr end request for old ptr
 * @bp_newreq: ptr alloc request for new ptr
 * @bp_op: rebalance operation
 */
struct nilfs_btree_path {
	struct buffer_head *bp_bh;
	struct buffer_head *bp_sib_bh;
	int bp_index;
	union nilfs_bmap_ptr_req bp_oldreq;
	union nilfs_bmap_ptr_req bp_newreq;
	struct nilfs_btnode_chkey_ctxt bp_ctxt;
	void (*bp_op)(struct nilfs_bmap *, struct nilfs_btree_path *,
		      int, __u64 *, __u64 *);
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

extern struct kmem_cache *nilfs_btree_path_cache;

int nilfs_btree_init(struct nilfs_bmap *);
int nilfs_btree_convert_and_insert(struct nilfs_bmap *, __u64, __u64,
				   const __u64 *, const __u64 *, int);
void nilfs_btree_init_gc(struct nilfs_bmap *);

int nilfs_btree_broken_node_block(struct buffer_head *bh);

#endif	/* _NILFS_BTREE_H */
