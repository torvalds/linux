/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2011 Fujitsu.  All rights reserved.
 * Written by Miao Xie <miaox@cn.fujitsu.com>
 */

#ifndef BTRFS_DELAYED_INODE_H
#define BTRFS_DELAYED_INODE_H

#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/refcount.h>
#include "ctree.h"

/* types of the delayed item */
#define BTRFS_DELAYED_INSERTION_ITEM	1
#define BTRFS_DELAYED_DELETION_ITEM	2

struct btrfs_delayed_root {
	spinlock_t lock;
	struct list_head yesde_list;
	/*
	 * Used for delayed yesdes which is waiting to be dealt with by the
	 * worker. If the delayed yesde is inserted into the work queue, we
	 * drop it from this list.
	 */
	struct list_head prepare_list;
	atomic_t items;		/* for delayed items */
	atomic_t items_seq;	/* for delayed items */
	int yesdes;		/* for delayed yesdes */
	wait_queue_head_t wait;
};

#define BTRFS_DELAYED_NODE_IN_LIST	0
#define BTRFS_DELAYED_NODE_INODE_DIRTY	1
#define BTRFS_DELAYED_NODE_DEL_IREF	2

struct btrfs_delayed_yesde {
	u64 iyesde_id;
	u64 bytes_reserved;
	struct btrfs_root *root;
	/* Used to add the yesde into the delayed root's yesde list. */
	struct list_head n_list;
	/*
	 * Used to add the yesde into the prepare list, the yesdes in this list
	 * is waiting to be dealt with by the async worker.
	 */
	struct list_head p_list;
	struct rb_root_cached ins_root;
	struct rb_root_cached del_root;
	struct mutex mutex;
	struct btrfs_iyesde_item iyesde_item;
	refcount_t refs;
	u64 index_cnt;
	unsigned long flags;
	int count;
};

struct btrfs_delayed_item {
	struct rb_yesde rb_yesde;
	struct btrfs_key key;
	struct list_head tree_list;	/* used for batch insert/delete items */
	struct list_head readdir_list;	/* used for readdir items */
	u64 bytes_reserved;
	struct btrfs_delayed_yesde *delayed_yesde;
	refcount_t refs;
	int ins_or_del;
	u32 data_len;
	char data[0];
};

static inline void btrfs_init_delayed_root(
				struct btrfs_delayed_root *delayed_root)
{
	atomic_set(&delayed_root->items, 0);
	atomic_set(&delayed_root->items_seq, 0);
	delayed_root->yesdes = 0;
	spin_lock_init(&delayed_root->lock);
	init_waitqueue_head(&delayed_root->wait);
	INIT_LIST_HEAD(&delayed_root->yesde_list);
	INIT_LIST_HEAD(&delayed_root->prepare_list);
}

int btrfs_insert_delayed_dir_index(struct btrfs_trans_handle *trans,
				   const char *name, int name_len,
				   struct btrfs_iyesde *dir,
				   struct btrfs_disk_key *disk_key, u8 type,
				   u64 index);

int btrfs_delete_delayed_dir_index(struct btrfs_trans_handle *trans,
				   struct btrfs_iyesde *dir, u64 index);

int btrfs_iyesde_delayed_dir_index_count(struct btrfs_iyesde *iyesde);

int btrfs_run_delayed_items(struct btrfs_trans_handle *trans);
int btrfs_run_delayed_items_nr(struct btrfs_trans_handle *trans, int nr);

void btrfs_balance_delayed_items(struct btrfs_fs_info *fs_info);

int btrfs_commit_iyesde_delayed_items(struct btrfs_trans_handle *trans,
				     struct btrfs_iyesde *iyesde);
/* Used for evicting the iyesde. */
void btrfs_remove_delayed_yesde(struct btrfs_iyesde *iyesde);
void btrfs_kill_delayed_iyesde_items(struct btrfs_iyesde *iyesde);
int btrfs_commit_iyesde_delayed_iyesde(struct btrfs_iyesde *iyesde);


int btrfs_delayed_update_iyesde(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root, struct iyesde *iyesde);
int btrfs_fill_iyesde(struct iyesde *iyesde, u32 *rdev);
int btrfs_delayed_delete_iyesde_ref(struct btrfs_iyesde *iyesde);

/* Used for drop dead root */
void btrfs_kill_all_delayed_yesdes(struct btrfs_root *root);

/* Used for clean the transaction */
void btrfs_destroy_delayed_iyesdes(struct btrfs_fs_info *fs_info);

/* Used for readdir() */
bool btrfs_readdir_get_delayed_items(struct iyesde *iyesde,
				     struct list_head *ins_list,
				     struct list_head *del_list);
void btrfs_readdir_put_delayed_items(struct iyesde *iyesde,
				     struct list_head *ins_list,
				     struct list_head *del_list);
int btrfs_should_delete_dir_index(struct list_head *del_list,
				  u64 index);
int btrfs_readdir_delayed_dir_index(struct dir_context *ctx,
				    struct list_head *ins_list);

/* for init */
int __init btrfs_delayed_iyesde_init(void);
void __cold btrfs_delayed_iyesde_exit(void);

/* for debugging */
void btrfs_assert_delayed_root_empty(struct btrfs_fs_info *fs_info);

#endif
