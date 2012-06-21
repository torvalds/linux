/*
 * Copyright (C) 2011 Fujitsu.  All rights reserved.
 * Written by Miao Xie <miaox@cn.fujitsu.com>
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

#ifndef __DELAYED_TREE_OPERATION_H
#define __DELAYED_TREE_OPERATION_H

#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/atomic.h>

#include "ctree.h"

/* types of the delayed item */
#define BTRFS_DELAYED_INSERTION_ITEM	1
#define BTRFS_DELAYED_DELETION_ITEM	2

struct btrfs_delayed_root {
	spinlock_t lock;
	struct list_head node_list;
	/*
	 * Used for delayed nodes which is waiting to be dealt with by the
	 * worker. If the delayed node is inserted into the work queue, we
	 * drop it from this list.
	 */
	struct list_head prepare_list;
	atomic_t items;		/* for delayed items */
	int nodes;		/* for delayed nodes */
	wait_queue_head_t wait;
};

struct btrfs_delayed_node {
	u64 inode_id;
	u64 bytes_reserved;
	struct btrfs_root *root;
	/* Used to add the node into the delayed root's node list. */
	struct list_head n_list;
	/*
	 * Used to add the node into the prepare list, the nodes in this list
	 * is waiting to be dealt with by the async worker.
	 */
	struct list_head p_list;
	struct rb_root ins_root;
	struct rb_root del_root;
	struct mutex mutex;
	struct btrfs_inode_item inode_item;
	atomic_t refs;
	u64 index_cnt;
	bool in_list;
	bool inode_dirty;
	int count;
};

struct btrfs_delayed_item {
	struct rb_node rb_node;
	struct btrfs_key key;
	struct list_head tree_list;	/* used for batch insert/delete items */
	struct list_head readdir_list;	/* used for readdir items */
	u64 bytes_reserved;
	struct btrfs_delayed_node *delayed_node;
	atomic_t refs;
	int ins_or_del;
	u32 data_len;
	char data[0];
};

static inline void btrfs_init_delayed_root(
				struct btrfs_delayed_root *delayed_root)
{
	atomic_set(&delayed_root->items, 0);
	delayed_root->nodes = 0;
	spin_lock_init(&delayed_root->lock);
	init_waitqueue_head(&delayed_root->wait);
	INIT_LIST_HEAD(&delayed_root->node_list);
	INIT_LIST_HEAD(&delayed_root->prepare_list);
}

int btrfs_insert_delayed_dir_index(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root, const char *name,
				   int name_len, struct inode *dir,
				   struct btrfs_disk_key *disk_key, u8 type,
				   u64 index);

int btrfs_delete_delayed_dir_index(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root, struct inode *dir,
				   u64 index);

int btrfs_inode_delayed_dir_index_count(struct inode *inode);

int btrfs_run_delayed_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root);
int btrfs_run_delayed_items_nr(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root, int nr);

void btrfs_balance_delayed_items(struct btrfs_root *root);

int btrfs_commit_inode_delayed_items(struct btrfs_trans_handle *trans,
				     struct inode *inode);
/* Used for evicting the inode. */
void btrfs_remove_delayed_node(struct inode *inode);
void btrfs_kill_delayed_inode_items(struct inode *inode);


int btrfs_delayed_update_inode(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root, struct inode *inode);
int btrfs_fill_inode(struct inode *inode, u32 *rdev);

/* Used for drop dead root */
void btrfs_kill_all_delayed_nodes(struct btrfs_root *root);

/* Used for clean the transaction */
void btrfs_destroy_delayed_inodes(struct btrfs_root *root);

/* Used for readdir() */
void btrfs_get_delayed_items(struct inode *inode, struct list_head *ins_list,
			     struct list_head *del_list);
void btrfs_put_delayed_items(struct list_head *ins_list,
			     struct list_head *del_list);
int btrfs_should_delete_dir_index(struct list_head *del_list,
				  u64 index);
int btrfs_readdir_delayed_dir_index(struct file *filp, void *dirent,
				    filldir_t filldir,
				    struct list_head *ins_list);

/* for init */
int __init btrfs_delayed_inode_init(void);
void btrfs_delayed_inode_exit(void);

/* for debugging */
void btrfs_assert_delayed_root_empty(struct btrfs_root *root);

#endif
