/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
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
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/btrfs.h>

#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "locking.h"
#include "ulist.h"
#include "backref.h"
#include "extent_io.h"
#include "qgroup.h"

/* TODO XXX FIXME
 *  - subvol delete -> delete when ref goes to 0? delete limits also?
 *  - reorganize keys
 *  - compressed
 *  - sync
 *  - copy also limits on subvol creation
 *  - limit
 *  - caches fuer ulists
 *  - performance benchmarks
 *  - check all ioctl parameters
 */

/*
 * one struct for each qgroup, organized in fs_info->qgroup_tree.
 */
struct btrfs_qgroup {
	u64 qgroupid;

	/*
	 * state
	 */
	u64 rfer;	/* referenced */
	u64 rfer_cmpr;	/* referenced compressed */
	u64 excl;	/* exclusive */
	u64 excl_cmpr;	/* exclusive compressed */

	/*
	 * limits
	 */
	u64 lim_flags;	/* which limits are set */
	u64 max_rfer;
	u64 max_excl;
	u64 rsv_rfer;
	u64 rsv_excl;

	/*
	 * reservation tracking
	 */
	u64 reserved;

	/*
	 * lists
	 */
	struct list_head groups;  /* groups this group is member of */
	struct list_head members; /* groups that are members of this group */
	struct list_head dirty;   /* dirty groups */
	struct rb_node node;	  /* tree of qgroups */

	/*
	 * temp variables for accounting operations
	 */
	u64 old_refcnt;
	u64 new_refcnt;
};

/*
 * glue structure to represent the relations between qgroups.
 */
struct btrfs_qgroup_list {
	struct list_head next_group;
	struct list_head next_member;
	struct btrfs_qgroup *group;
	struct btrfs_qgroup *member;
};

#define ptr_to_u64(x) ((u64)(uintptr_t)x)
#define u64_to_ptr(x) ((struct btrfs_qgroup *)(uintptr_t)x)

static int
qgroup_rescan_init(struct btrfs_fs_info *fs_info, u64 progress_objectid,
		   int init_flags);
static void qgroup_rescan_zero_tracking(struct btrfs_fs_info *fs_info);

/* must be called with qgroup_ioctl_lock held */
static struct btrfs_qgroup *find_qgroup_rb(struct btrfs_fs_info *fs_info,
					   u64 qgroupid)
{
	struct rb_node *n = fs_info->qgroup_tree.rb_node;
	struct btrfs_qgroup *qgroup;

	while (n) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		if (qgroup->qgroupid < qgroupid)
			n = n->rb_left;
		else if (qgroup->qgroupid > qgroupid)
			n = n->rb_right;
		else
			return qgroup;
	}
	return NULL;
}

/* must be called with qgroup_lock held */
static struct btrfs_qgroup *add_qgroup_rb(struct btrfs_fs_info *fs_info,
					  u64 qgroupid)
{
	struct rb_node **p = &fs_info->qgroup_tree.rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_qgroup *qgroup;

	while (*p) {
		parent = *p;
		qgroup = rb_entry(parent, struct btrfs_qgroup, node);

		if (qgroup->qgroupid < qgroupid)
			p = &(*p)->rb_left;
		else if (qgroup->qgroupid > qgroupid)
			p = &(*p)->rb_right;
		else
			return qgroup;
	}

	qgroup = kzalloc(sizeof(*qgroup), GFP_ATOMIC);
	if (!qgroup)
		return ERR_PTR(-ENOMEM);

	qgroup->qgroupid = qgroupid;
	INIT_LIST_HEAD(&qgroup->groups);
	INIT_LIST_HEAD(&qgroup->members);
	INIT_LIST_HEAD(&qgroup->dirty);

	rb_link_node(&qgroup->node, parent, p);
	rb_insert_color(&qgroup->node, &fs_info->qgroup_tree);

	return qgroup;
}

static void __del_qgroup_rb(struct btrfs_qgroup *qgroup)
{
	struct btrfs_qgroup_list *list;

	list_del(&qgroup->dirty);
	while (!list_empty(&qgroup->groups)) {
		list = list_first_entry(&qgroup->groups,
					struct btrfs_qgroup_list, next_group);
		list_del(&list->next_group);
		list_del(&list->next_member);
		kfree(list);
	}

	while (!list_empty(&qgroup->members)) {
		list = list_first_entry(&qgroup->members,
					struct btrfs_qgroup_list, next_member);
		list_del(&list->next_group);
		list_del(&list->next_member);
		kfree(list);
	}
	kfree(qgroup);
}

/* must be called with qgroup_lock held */
static int del_qgroup_rb(struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	struct btrfs_qgroup *qgroup = find_qgroup_rb(fs_info, qgroupid);

	if (!qgroup)
		return -ENOENT;

	rb_erase(&qgroup->node, &fs_info->qgroup_tree);
	__del_qgroup_rb(qgroup);
	return 0;
}

/* must be called with qgroup_lock held */
static int add_relation_rb(struct btrfs_fs_info *fs_info,
			   u64 memberid, u64 parentid)
{
	struct btrfs_qgroup *member;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup_list *list;

	member = find_qgroup_rb(fs_info, memberid);
	parent = find_qgroup_rb(fs_info, parentid);
	if (!member || !parent)
		return -ENOENT;

	list = kzalloc(sizeof(*list), GFP_ATOMIC);
	if (!list)
		return -ENOMEM;

	list->group = parent;
	list->member = member;
	list_add_tail(&list->next_group, &member->groups);
	list_add_tail(&list->next_member, &parent->members);

	return 0;
}

/* must be called with qgroup_lock held */
static int del_relation_rb(struct btrfs_fs_info *fs_info,
			   u64 memberid, u64 parentid)
{
	struct btrfs_qgroup *member;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup_list *list;

	member = find_qgroup_rb(fs_info, memberid);
	parent = find_qgroup_rb(fs_info, parentid);
	if (!member || !parent)
		return -ENOENT;

	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent) {
			list_del(&list->next_group);
			list_del(&list->next_member);
			kfree(list);
			return 0;
		}
	}
	return -ENOENT;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
int btrfs_verify_qgroup_counts(struct btrfs_fs_info *fs_info, u64 qgroupid,
			       u64 rfer, u64 excl)
{
	struct btrfs_qgroup *qgroup;

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup)
		return -EINVAL;
	if (qgroup->rfer != rfer || qgroup->excl != excl)
		return -EINVAL;
	return 0;
}
#endif

/*
 * The full config is read in one go, only called from open_ctree()
 * It doesn't use any locking, as at this point we're still single-threaded
 */
int btrfs_read_qgroup_config(struct btrfs_fs_info *fs_info)
{
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_path *path = NULL;
	struct extent_buffer *l;
	int slot;
	int ret = 0;
	u64 flags = 0;
	u64 rescan_progress = 0;

	if (!fs_info->quota_enabled)
		return 0;

	fs_info->qgroup_ulist = ulist_alloc(GFP_NOFS);
	if (!fs_info->qgroup_ulist) {
		ret = -ENOMEM;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/* default this to quota off, in case no status key is found */
	fs_info->qgroup_flags = 0;

	/*
	 * pass 1: read status, all qgroup infos and limits
	 */
	key.objectid = 0;
	key.type = 0;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(quota_root, &key, path, 1, 1);
	if (ret)
		goto out;

	while (1) {
		struct btrfs_qgroup *qgroup;

		slot = path->slots[0];
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);

		if (found_key.type == BTRFS_QGROUP_STATUS_KEY) {
			struct btrfs_qgroup_status_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_status_item);

			if (btrfs_qgroup_status_version(l, ptr) !=
			    BTRFS_QGROUP_STATUS_VERSION) {
				btrfs_err(fs_info,
				 "old qgroup version, quota disabled");
				goto out;
			}
			if (btrfs_qgroup_status_generation(l, ptr) !=
			    fs_info->generation) {
				flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
				btrfs_err(fs_info,
					"qgroup generation mismatch, "
					"marked as inconsistent");
			}
			fs_info->qgroup_flags = btrfs_qgroup_status_flags(l,
									  ptr);
			rescan_progress = btrfs_qgroup_status_rescan(l, ptr);
			goto next1;
		}

		if (found_key.type != BTRFS_QGROUP_INFO_KEY &&
		    found_key.type != BTRFS_QGROUP_LIMIT_KEY)
			goto next1;

		qgroup = find_qgroup_rb(fs_info, found_key.offset);
		if ((qgroup && found_key.type == BTRFS_QGROUP_INFO_KEY) ||
		    (!qgroup && found_key.type == BTRFS_QGROUP_LIMIT_KEY)) {
			btrfs_err(fs_info, "inconsitent qgroup config");
			flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		}
		if (!qgroup) {
			qgroup = add_qgroup_rb(fs_info, found_key.offset);
			if (IS_ERR(qgroup)) {
				ret = PTR_ERR(qgroup);
				goto out;
			}
		}
		switch (found_key.type) {
		case BTRFS_QGROUP_INFO_KEY: {
			struct btrfs_qgroup_info_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_info_item);
			qgroup->rfer = btrfs_qgroup_info_rfer(l, ptr);
			qgroup->rfer_cmpr = btrfs_qgroup_info_rfer_cmpr(l, ptr);
			qgroup->excl = btrfs_qgroup_info_excl(l, ptr);
			qgroup->excl_cmpr = btrfs_qgroup_info_excl_cmpr(l, ptr);
			/* generation currently unused */
			break;
		}
		case BTRFS_QGROUP_LIMIT_KEY: {
			struct btrfs_qgroup_limit_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_limit_item);
			qgroup->lim_flags = btrfs_qgroup_limit_flags(l, ptr);
			qgroup->max_rfer = btrfs_qgroup_limit_max_rfer(l, ptr);
			qgroup->max_excl = btrfs_qgroup_limit_max_excl(l, ptr);
			qgroup->rsv_rfer = btrfs_qgroup_limit_rsv_rfer(l, ptr);
			qgroup->rsv_excl = btrfs_qgroup_limit_rsv_excl(l, ptr);
			break;
		}
		}
next1:
		ret = btrfs_next_item(quota_root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;
	}
	btrfs_release_path(path);

	/*
	 * pass 2: read all qgroup relations
	 */
	key.objectid = 0;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(quota_root, &key, path, 1, 0);
	if (ret)
		goto out;
	while (1) {
		slot = path->slots[0];
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);

		if (found_key.type != BTRFS_QGROUP_RELATION_KEY)
			goto next2;

		if (found_key.objectid > found_key.offset) {
			/* parent <- member, not needed to build config */
			/* FIXME should we omit the key completely? */
			goto next2;
		}

		ret = add_relation_rb(fs_info, found_key.objectid,
				      found_key.offset);
		if (ret == -ENOENT) {
			btrfs_warn(fs_info,
				"orphan qgroup relation 0x%llx->0x%llx",
				found_key.objectid, found_key.offset);
			ret = 0;	/* ignore the error */
		}
		if (ret)
			goto out;
next2:
		ret = btrfs_next_item(quota_root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;
	}
out:
	fs_info->qgroup_flags |= flags;
	if (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON)) {
		fs_info->quota_enabled = 0;
		fs_info->pending_quota_state = 0;
	} else if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN &&
		   ret >= 0) {
		ret = qgroup_rescan_init(fs_info, rescan_progress, 0);
	}
	btrfs_free_path(path);

	if (ret < 0) {
		ulist_free(fs_info->qgroup_ulist);
		fs_info->qgroup_ulist = NULL;
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	}

	return ret < 0 ? ret : 0;
}

/*
 * This is called from close_ctree() or open_ctree() or btrfs_quota_disable(),
 * first two are in single-threaded paths.And for the third one, we have set
 * quota_root to be null with qgroup_lock held before, so it is safe to clean
 * up the in-memory structures without qgroup_lock held.
 */
void btrfs_free_qgroup_config(struct btrfs_fs_info *fs_info)
{
	struct rb_node *n;
	struct btrfs_qgroup *qgroup;

	while ((n = rb_first(&fs_info->qgroup_tree))) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		rb_erase(n, &fs_info->qgroup_tree);
		__del_qgroup_rb(qgroup);
	}
	/*
	 * we call btrfs_free_qgroup_config() when umounting
	 * filesystem and disabling quota, so we set qgroup_ulit
	 * to be null here to avoid double free.
	 */
	ulist_free(fs_info->qgroup_ulist);
	fs_info->qgroup_ulist = NULL;
}

static int add_qgroup_relation_item(struct btrfs_trans_handle *trans,
				    struct btrfs_root *quota_root,
				    u64 src, u64 dst)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = src;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = dst;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key, 0);

	btrfs_mark_buffer_dirty(path->nodes[0]);

	btrfs_free_path(path);
	return ret;
}

static int del_qgroup_relation_item(struct btrfs_trans_handle *trans,
				    struct btrfs_root *quota_root,
				    u64 src, u64 dst)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = src;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = dst;

	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);
out:
	btrfs_free_path(path);
	return ret;
}

static int add_qgroup_item(struct btrfs_trans_handle *trans,
			   struct btrfs_root *quota_root, u64 qgroupid)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_qgroup_info_item *qgroup_info;
	struct btrfs_qgroup_limit_item *qgroup_limit;
	struct extent_buffer *leaf;
	struct btrfs_key key;

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
	if (unlikely(test_bit(BTRFS_ROOT_DUMMY_ROOT, &quota_root->state)))
		return 0;
#endif
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroupid;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*qgroup_info));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	qgroup_info = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_info_item);
	btrfs_set_qgroup_info_generation(leaf, qgroup_info, trans->transid);
	btrfs_set_qgroup_info_rfer(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_rfer_cmpr(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_excl(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_excl_cmpr(leaf, qgroup_info, 0);

	btrfs_mark_buffer_dirty(leaf);

	btrfs_release_path(path);

	key.type = BTRFS_QGROUP_LIMIT_KEY;
	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*qgroup_limit));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	qgroup_limit = btrfs_item_ptr(leaf, path->slots[0],
				  struct btrfs_qgroup_limit_item);
	btrfs_set_qgroup_limit_flags(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_max_rfer(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_max_excl(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_rsv_rfer(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_rsv_excl(leaf, qgroup_limit, 0);

	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int del_qgroup_item(struct btrfs_trans_handle *trans,
			   struct btrfs_root *quota_root, u64 qgroupid)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroupid;
	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);
	if (ret)
		goto out;

	btrfs_release_path(path);

	key.type = BTRFS_QGROUP_LIMIT_KEY;
	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_limit_item(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root, u64 qgroupid,
				    u64 flags, u64 max_rfer, u64 max_excl,
				    u64 rsv_rfer, u64 rsv_excl)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_limit_item *qgroup_limit;
	int ret;
	int slot;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_LIMIT_KEY;
	key.offset = qgroupid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	qgroup_limit = btrfs_item_ptr(l, slot, struct btrfs_qgroup_limit_item);
	btrfs_set_qgroup_limit_flags(l, qgroup_limit, flags);
	btrfs_set_qgroup_limit_max_rfer(l, qgroup_limit, max_rfer);
	btrfs_set_qgroup_limit_max_excl(l, qgroup_limit, max_excl);
	btrfs_set_qgroup_limit_rsv_rfer(l, qgroup_limit, rsv_rfer);
	btrfs_set_qgroup_limit_rsv_excl(l, qgroup_limit, rsv_excl);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_info_item(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_qgroup *qgroup)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_info_item *qgroup_info;
	int ret;
	int slot;

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
	if (unlikely(test_bit(BTRFS_ROOT_DUMMY_ROOT, &root->state)))
		return 0;
#endif
	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroup->qgroupid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	qgroup_info = btrfs_item_ptr(l, slot, struct btrfs_qgroup_info_item);
	btrfs_set_qgroup_info_generation(l, qgroup_info, trans->transid);
	btrfs_set_qgroup_info_rfer(l, qgroup_info, qgroup->rfer);
	btrfs_set_qgroup_info_rfer_cmpr(l, qgroup_info, qgroup->rfer_cmpr);
	btrfs_set_qgroup_info_excl(l, qgroup_info, qgroup->excl);
	btrfs_set_qgroup_info_excl_cmpr(l, qgroup_info, qgroup->excl_cmpr);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_status_item(struct btrfs_trans_handle *trans,
				     struct btrfs_fs_info *fs_info,
				    struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_status_item *ptr;
	int ret;
	int slot;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_STATUS_KEY;
	key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr(l, slot, struct btrfs_qgroup_status_item);
	btrfs_set_qgroup_status_flags(l, ptr, fs_info->qgroup_flags);
	btrfs_set_qgroup_status_generation(l, ptr, trans->transid);
	btrfs_set_qgroup_status_rescan(l, ptr,
				fs_info->qgroup_rescan_progress.objectid);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * called with qgroup_lock held
 */
static int btrfs_clean_quota_tree(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf = NULL;
	int ret;
	int nr = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;

	key.objectid = 0;
	key.offset = 0;
	key.type = 0;

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;
		leaf = path->nodes[0];
		nr = btrfs_header_nritems(leaf);
		if (!nr)
			break;
		/*
		 * delete the leaf one by one
		 * since the whole tree is going
		 * to be deleted.
		 */
		path->slots[0] = 0;
		ret = btrfs_del_items(trans, root, path, 0, nr);
		if (ret)
			goto out;

		btrfs_release_path(path);
	}
	ret = 0;
out:
	root->fs_info->pending_quota_state = 0;
	btrfs_free_path(path);
	return ret;
}

int btrfs_quota_enable(struct btrfs_trans_handle *trans,
		       struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *quota_root;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_path *path = NULL;
	struct btrfs_qgroup_status_item *ptr;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_qgroup *qgroup = NULL;
	int ret = 0;
	int slot;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (fs_info->quota_root) {
		fs_info->pending_quota_state = 1;
		goto out;
	}

	fs_info->qgroup_ulist = ulist_alloc(GFP_NOFS);
	if (!fs_info->qgroup_ulist) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * initially create the quota tree
	 */
	quota_root = btrfs_create_tree(trans, fs_info,
				       BTRFS_QUOTA_TREE_OBJECTID);
	if (IS_ERR(quota_root)) {
		ret =  PTR_ERR(quota_root);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out_free_root;
	}

	key.objectid = 0;
	key.type = BTRFS_QGROUP_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*ptr));
	if (ret)
		goto out_free_path;

	leaf = path->nodes[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_status_item);
	btrfs_set_qgroup_status_generation(leaf, ptr, trans->transid);
	btrfs_set_qgroup_status_version(leaf, ptr, BTRFS_QGROUP_STATUS_VERSION);
	fs_info->qgroup_flags = BTRFS_QGROUP_STATUS_FLAG_ON |
				BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	btrfs_set_qgroup_status_flags(leaf, ptr, fs_info->qgroup_flags);
	btrfs_set_qgroup_status_rescan(leaf, ptr, 0);

	btrfs_mark_buffer_dirty(leaf);

	key.objectid = 0;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = 0;

	btrfs_release_path(path);
	ret = btrfs_search_slot_for_read(tree_root, &key, path, 1, 0);
	if (ret > 0)
		goto out_add_root;
	if (ret < 0)
		goto out_free_path;


	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.type == BTRFS_ROOT_REF_KEY) {
			ret = add_qgroup_item(trans, quota_root,
					      found_key.offset);
			if (ret)
				goto out_free_path;

			qgroup = add_qgroup_rb(fs_info, found_key.offset);
			if (IS_ERR(qgroup)) {
				ret = PTR_ERR(qgroup);
				goto out_free_path;
			}
		}
		ret = btrfs_next_item(tree_root, path);
		if (ret < 0)
			goto out_free_path;
		if (ret)
			break;
	}

out_add_root:
	btrfs_release_path(path);
	ret = add_qgroup_item(trans, quota_root, BTRFS_FS_TREE_OBJECTID);
	if (ret)
		goto out_free_path;

	qgroup = add_qgroup_rb(fs_info, BTRFS_FS_TREE_OBJECTID);
	if (IS_ERR(qgroup)) {
		ret = PTR_ERR(qgroup);
		goto out_free_path;
	}
	spin_lock(&fs_info->qgroup_lock);
	fs_info->quota_root = quota_root;
	fs_info->pending_quota_state = 1;
	spin_unlock(&fs_info->qgroup_lock);
out_free_path:
	btrfs_free_path(path);
out_free_root:
	if (ret) {
		free_extent_buffer(quota_root->node);
		free_extent_buffer(quota_root->commit_root);
		kfree(quota_root);
	}
out:
	if (ret) {
		ulist_free(fs_info->qgroup_ulist);
		fs_info->qgroup_ulist = NULL;
	}
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_quota_disable(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *quota_root;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root)
		goto out;
	spin_lock(&fs_info->qgroup_lock);
	fs_info->quota_enabled = 0;
	fs_info->pending_quota_state = 0;
	quota_root = fs_info->quota_root;
	fs_info->quota_root = NULL;
	spin_unlock(&fs_info->qgroup_lock);

	btrfs_free_qgroup_config(fs_info);

	ret = btrfs_clean_quota_tree(trans, quota_root);
	if (ret)
		goto out;

	ret = btrfs_del_root(trans, tree_root, &quota_root->root_key);
	if (ret)
		goto out;

	list_del(&quota_root->dirty_list);

	btrfs_tree_lock(quota_root->node);
	clean_tree_block(trans, tree_root, quota_root->node);
	btrfs_tree_unlock(quota_root->node);
	btrfs_free_tree_block(trans, quota_root, quota_root->node, 0, 1);

	free_extent_buffer(quota_root->node);
	free_extent_buffer(quota_root->commit_root);
	kfree(quota_root);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

static void qgroup_dirty(struct btrfs_fs_info *fs_info,
			 struct btrfs_qgroup *qgroup)
{
	if (list_empty(&qgroup->dirty))
		list_add(&qgroup->dirty, &fs_info->dirty_qgroups);
}

int btrfs_add_qgroup_relation(struct btrfs_trans_handle *trans,
			      struct btrfs_fs_info *fs_info, u64 src, u64 dst)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup *member;
	struct btrfs_qgroup_list *list;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}
	member = find_qgroup_rb(fs_info, src);
	parent = find_qgroup_rb(fs_info, dst);
	if (!member || !parent) {
		ret = -EINVAL;
		goto out;
	}

	/* check if such qgroup relation exist firstly */
	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent) {
			ret = -EEXIST;
			goto out;
		}
	}

	ret = add_qgroup_relation_item(trans, quota_root, src, dst);
	if (ret)
		goto out;

	ret = add_qgroup_relation_item(trans, quota_root, dst, src);
	if (ret) {
		del_qgroup_relation_item(trans, quota_root, src, dst);
		goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	ret = add_relation_rb(quota_root->fs_info, src, dst);
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_del_qgroup_relation(struct btrfs_trans_handle *trans,
			      struct btrfs_fs_info *fs_info, u64 src, u64 dst)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup *member;
	struct btrfs_qgroup_list *list;
	int ret = 0;
	int err;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	member = find_qgroup_rb(fs_info, src);
	parent = find_qgroup_rb(fs_info, dst);
	if (!member || !parent) {
		ret = -EINVAL;
		goto out;
	}

	/* check if such qgroup relation exist firstly */
	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent)
			goto exist;
	}
	ret = -ENOENT;
	goto out;
exist:
	ret = del_qgroup_relation_item(trans, quota_root, src, dst);
	err = del_qgroup_relation_item(trans, quota_root, dst, src);
	if (err && !ret)
		ret = err;

	spin_lock(&fs_info->qgroup_lock);
	del_relation_rb(fs_info, src, dst);
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_create_qgroup(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info, u64 qgroupid, char *name)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}
	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (qgroup) {
		ret = -EEXIST;
		goto out;
	}

	ret = add_qgroup_item(trans, quota_root, qgroupid);
	if (ret)
		goto out;

	spin_lock(&fs_info->qgroup_lock);
	qgroup = add_qgroup_rb(fs_info, qgroupid);
	spin_unlock(&fs_info->qgroup_lock);

	if (IS_ERR(qgroup))
		ret = PTR_ERR(qgroup);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_remove_qgroup(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	} else {
		/* check if there are no relations to this qgroup */
		if (!list_empty(&qgroup->groups) ||
		    !list_empty(&qgroup->members)) {
			ret = -EBUSY;
			goto out;
		}
	}
	ret = del_qgroup_item(trans, quota_root, qgroupid);

	spin_lock(&fs_info->qgroup_lock);
	del_qgroup_rb(quota_root->fs_info, qgroupid);
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_limit_qgroup(struct btrfs_trans_handle *trans,
		       struct btrfs_fs_info *fs_info, u64 qgroupid,
		       struct btrfs_qgroup_limit *limit)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	}
	ret = update_qgroup_limit_item(trans, quota_root, qgroupid,
				       limit->flags, limit->max_rfer,
				       limit->max_excl, limit->rsv_rfer,
				       limit->rsv_excl);
	if (ret) {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		btrfs_info(fs_info, "unable to update quota limit for %llu",
		       qgroupid);
	}

	spin_lock(&fs_info->qgroup_lock);
	qgroup->lim_flags = limit->flags;
	qgroup->max_rfer = limit->max_rfer;
	qgroup->max_excl = limit->max_excl;
	qgroup->rsv_rfer = limit->rsv_rfer;
	qgroup->rsv_excl = limit->rsv_excl;
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}
static int comp_oper(struct btrfs_qgroup_operation *oper1,
		     struct btrfs_qgroup_operation *oper2)
{
	if (oper1->bytenr < oper2->bytenr)
		return -1;
	if (oper1->bytenr > oper2->bytenr)
		return 1;
	if (oper1->seq < oper2->seq)
		return -1;
	if (oper1->seq > oper2->seq)
		return -1;
	if (oper1->ref_root < oper2->ref_root)
		return -1;
	if (oper1->ref_root > oper2->ref_root)
		return 1;
	if (oper1->type < oper2->type)
		return -1;
	if (oper1->type > oper2->type)
		return 1;
	return 0;
}

static int insert_qgroup_oper(struct btrfs_fs_info *fs_info,
			      struct btrfs_qgroup_operation *oper)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct btrfs_qgroup_operation *cur;
	int cmp;

	spin_lock(&fs_info->qgroup_op_lock);
	p = &fs_info->qgroup_op_tree.rb_node;
	while (*p) {
		parent = *p;
		cur = rb_entry(parent, struct btrfs_qgroup_operation, n);
		cmp = comp_oper(cur, oper);
		if (cmp < 0) {
			p = &(*p)->rb_right;
		} else if (cmp) {
			p = &(*p)->rb_left;
		} else {
			spin_unlock(&fs_info->qgroup_op_lock);
			return -EEXIST;
		}
	}
	rb_link_node(&oper->n, parent, p);
	rb_insert_color(&oper->n, &fs_info->qgroup_op_tree);
	spin_unlock(&fs_info->qgroup_op_lock);
	return 0;
}

/*
 * Record a quota operation for processing later on.
 * @trans: the transaction we are adding the delayed op to.
 * @fs_info: the fs_info for this fs.
 * @ref_root: the root of the reference we are acting on,
 * @bytenr: the bytenr we are acting on.
 * @num_bytes: the number of bytes in the reference.
 * @type: the type of operation this is.
 * @mod_seq: do we need to get a sequence number for looking up roots.
 *
 * We just add it to our trans qgroup_ref_list and carry on and process these
 * operations in order at some later point.  If the reference root isn't a fs
 * root then we don't bother with doing anything.
 *
 * MUST BE HOLDING THE REF LOCK.
 */
int btrfs_qgroup_record_ref(struct btrfs_trans_handle *trans,
			    struct btrfs_fs_info *fs_info, u64 ref_root,
			    u64 bytenr, u64 num_bytes,
			    enum btrfs_qgroup_operation_type type, int mod_seq)
{
	struct btrfs_qgroup_operation *oper;
	int ret;

	if (!is_fstree(ref_root) || !fs_info->quota_enabled)
		return 0;

	oper = kmalloc(sizeof(*oper), GFP_NOFS);
	if (!oper)
		return -ENOMEM;

	oper->ref_root = ref_root;
	oper->bytenr = bytenr;
	oper->num_bytes = num_bytes;
	oper->type = type;
	oper->seq = atomic_inc_return(&fs_info->qgroup_op_seq);
	INIT_LIST_HEAD(&oper->elem.list);
	oper->elem.seq = 0;
	ret = insert_qgroup_oper(fs_info, oper);
	if (ret) {
		/* Shouldn't happen so have an assert for developers */
		ASSERT(0);
		kfree(oper);
		return ret;
	}
	list_add_tail(&oper->list, &trans->qgroup_ref_list);

	if (mod_seq)
		btrfs_get_tree_mod_seq(fs_info, &oper->elem);

	return 0;
}

/*
 * The easy accounting, if we are adding/removing the only ref for an extent
 * then this qgroup and all of the parent qgroups get their refrence and
 * exclusive counts adjusted.
 */
static int qgroup_excl_accounting(struct btrfs_fs_info *fs_info,
				  struct btrfs_qgroup_operation *oper)
{
	struct btrfs_qgroup *qgroup;
	struct ulist *tmp;
	struct btrfs_qgroup_list *glist;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	int sign = 0;
	int ret = 0;

	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp)
		return -ENOMEM;

	spin_lock(&fs_info->qgroup_lock);
	if (!fs_info->quota_root)
		goto out;
	qgroup = find_qgroup_rb(fs_info, oper->ref_root);
	if (!qgroup)
		goto out;
	switch (oper->type) {
	case BTRFS_QGROUP_OPER_ADD_EXCL:
		sign = 1;
		break;
	case BTRFS_QGROUP_OPER_SUB_EXCL:
		sign = -1;
		break;
	default:
		ASSERT(0);
	}
	qgroup->rfer += sign * oper->num_bytes;
	qgroup->rfer_cmpr += sign * oper->num_bytes;

	WARN_ON(sign < 0 && qgroup->excl < oper->num_bytes);
	qgroup->excl += sign * oper->num_bytes;
	qgroup->excl_cmpr += sign * oper->num_bytes;

	qgroup_dirty(fs_info, qgroup);

	/* Get all of the parent groups that contain this qgroup */
	list_for_each_entry(glist, &qgroup->groups, next_group) {
		ret = ulist_add(tmp, glist->group->qgroupid,
				ptr_to_u64(glist->group), GFP_ATOMIC);
		if (ret < 0)
			goto out;
	}

	/* Iterate all of the parents and adjust their reference counts */
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(tmp, &uiter))) {
		qgroup = u64_to_ptr(unode->aux);
		qgroup->rfer += sign * oper->num_bytes;
		qgroup->rfer_cmpr += sign * oper->num_bytes;
		qgroup->excl += sign * oper->num_bytes;
		if (sign < 0)
			WARN_ON(qgroup->excl < oper->num_bytes);
		qgroup->excl_cmpr += sign * oper->num_bytes;
		qgroup_dirty(fs_info, qgroup);

		/* Add any parents of the parents */
		list_for_each_entry(glist, &qgroup->groups, next_group) {
			ret = ulist_add(tmp, glist->group->qgroupid,
					ptr_to_u64(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;
out:
	spin_unlock(&fs_info->qgroup_lock);
	ulist_free(tmp);
	return ret;
}

/*
 * Walk all of the roots that pointed to our bytenr and adjust their refcnts as
 * properly.
 */
static int qgroup_calc_old_refcnt(struct btrfs_fs_info *fs_info,
				  u64 root_to_skip, struct ulist *tmp,
				  struct ulist *roots, struct ulist *qgroups,
				  u64 seq, int *old_roots, int rescan)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct ulist_node *tmp_unode;
	struct ulist_iterator tmp_uiter;
	struct btrfs_qgroup *qg;
	int ret;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(roots, &uiter))) {
		/* We don't count our current root here */
		if (unode->val == root_to_skip)
			continue;
		qg = find_qgroup_rb(fs_info, unode->val);
		if (!qg)
			continue;
		/*
		 * We could have a pending removal of this same ref so we may
		 * not have actually found our ref root when doing
		 * btrfs_find_all_roots, so we need to keep track of how many
		 * old roots we find in case we removed ours and added a
		 * different one at the same time.  I don't think this could
		 * happen in practice but that sort of thinking leads to pain
		 * and suffering and to the dark side.
		 */
		(*old_roots)++;

		ulist_reinit(tmp);
		ret = ulist_add(qgroups, qg->qgroupid, ptr_to_u64(qg),
				GFP_ATOMIC);
		if (ret < 0)
			return ret;
		ret = ulist_add(tmp, qg->qgroupid, ptr_to_u64(qg), GFP_ATOMIC);
		if (ret < 0)
			return ret;
		ULIST_ITER_INIT(&tmp_uiter);
		while ((tmp_unode = ulist_next(tmp, &tmp_uiter))) {
			struct btrfs_qgroup_list *glist;

			qg = u64_to_ptr(tmp_unode->aux);
			/*
			 * We use this sequence number to keep from having to
			 * run the whole list and 0 out the refcnt every time.
			 * We basically use sequnce as the known 0 count and
			 * then add 1 everytime we see a qgroup.  This is how we
			 * get how many of the roots actually point up to the
			 * upper level qgroups in order to determine exclusive
			 * counts.
			 *
			 * For rescan we want to set old_refcnt to seq so our
			 * exclusive calculations end up correct.
			 */
			if (rescan)
				qg->old_refcnt = seq;
			else if (qg->old_refcnt < seq)
				qg->old_refcnt = seq + 1;
			else
				qg->old_refcnt++;

			if (qg->new_refcnt < seq)
				qg->new_refcnt = seq + 1;
			else
				qg->new_refcnt++;
			list_for_each_entry(glist, &qg->groups, next_group) {
				ret = ulist_add(qgroups, glist->group->qgroupid,
						ptr_to_u64(glist->group),
						GFP_ATOMIC);
				if (ret < 0)
					return ret;
				ret = ulist_add(tmp, glist->group->qgroupid,
						ptr_to_u64(glist->group),
						GFP_ATOMIC);
				if (ret < 0)
					return ret;
			}
		}
	}
	return 0;
}

/*
 * We need to walk forward in our operation tree and account for any roots that
 * were deleted after we made this operation.
 */
static int qgroup_account_deleted_refs(struct btrfs_fs_info *fs_info,
				       struct btrfs_qgroup_operation *oper,
				       struct ulist *tmp,
				       struct ulist *qgroups, u64 seq,
				       int *old_roots)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_qgroup *qg;
	struct btrfs_qgroup_operation *tmp_oper;
	struct rb_node *n;
	int ret;

	ulist_reinit(tmp);

	/*
	 * We only walk forward in the tree since we're only interested in
	 * removals that happened _after_  our operation.
	 */
	spin_lock(&fs_info->qgroup_op_lock);
	n = rb_next(&oper->n);
	spin_unlock(&fs_info->qgroup_op_lock);
	if (!n)
		return 0;
	tmp_oper = rb_entry(n, struct btrfs_qgroup_operation, n);
	while (tmp_oper->bytenr == oper->bytenr) {
		/*
		 * If it's not a removal we don't care, additions work out
		 * properly with our refcnt tracking.
		 */
		if (tmp_oper->type != BTRFS_QGROUP_OPER_SUB_SHARED &&
		    tmp_oper->type != BTRFS_QGROUP_OPER_SUB_EXCL)
			goto next;
		qg = find_qgroup_rb(fs_info, tmp_oper->ref_root);
		if (!qg)
			goto next;
		ret = ulist_add(qgroups, qg->qgroupid, ptr_to_u64(qg),
				GFP_ATOMIC);
		if (ret) {
			if (ret < 0)
				return ret;
			/*
			 * We only want to increase old_roots if this qgroup is
			 * not already in the list of qgroups.  If it is already
			 * there then that means it must have been re-added or
			 * the delete will be discarded because we had an
			 * existing ref that we haven't looked up yet.  In this
			 * case we don't want to increase old_roots.  So if ret
			 * == 1 then we know that this is the first time we've
			 * seen this qgroup and we can bump the old_roots.
			 */
			(*old_roots)++;
			ret = ulist_add(tmp, qg->qgroupid, ptr_to_u64(qg),
					GFP_ATOMIC);
			if (ret < 0)
				return ret;
		}
next:
		spin_lock(&fs_info->qgroup_op_lock);
		n = rb_next(&tmp_oper->n);
		spin_unlock(&fs_info->qgroup_op_lock);
		if (!n)
			break;
		tmp_oper = rb_entry(n, struct btrfs_qgroup_operation, n);
	}

	/* Ok now process the qgroups we found */
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(tmp, &uiter))) {
		struct btrfs_qgroup_list *glist;

		qg = u64_to_ptr(unode->aux);
		if (qg->old_refcnt < seq)
			qg->old_refcnt = seq + 1;
		else
			qg->old_refcnt++;
		if (qg->new_refcnt < seq)
			qg->new_refcnt = seq + 1;
		else
			qg->new_refcnt++;
		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(qgroups, glist->group->qgroupid,
					ptr_to_u64(glist->group), GFP_ATOMIC);
			if (ret < 0)
				return ret;
			ret = ulist_add(tmp, glist->group->qgroupid,
					ptr_to_u64(glist->group), GFP_ATOMIC);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}

/* Add refcnt for the newly added reference. */
static int qgroup_calc_new_refcnt(struct btrfs_fs_info *fs_info,
				  struct btrfs_qgroup_operation *oper,
				  struct btrfs_qgroup *qgroup,
				  struct ulist *tmp, struct ulist *qgroups,
				  u64 seq)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_qgroup *qg;
	int ret;

	ulist_reinit(tmp);
	ret = ulist_add(qgroups, qgroup->qgroupid, ptr_to_u64(qgroup),
			GFP_ATOMIC);
	if (ret < 0)
		return ret;
	ret = ulist_add(tmp, qgroup->qgroupid, ptr_to_u64(qgroup),
			GFP_ATOMIC);
	if (ret < 0)
		return ret;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(tmp, &uiter))) {
		struct btrfs_qgroup_list *glist;

		qg = u64_to_ptr(unode->aux);
		if (oper->type == BTRFS_QGROUP_OPER_ADD_SHARED) {
			if (qg->new_refcnt < seq)
				qg->new_refcnt = seq + 1;
			else
				qg->new_refcnt++;
		} else {
			if (qg->old_refcnt < seq)
				qg->old_refcnt = seq + 1;
			else
				qg->old_refcnt++;
		}
		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(tmp, glist->group->qgroupid,
					ptr_to_u64(glist->group), GFP_ATOMIC);
			if (ret < 0)
				return ret;
			ret = ulist_add(qgroups, glist->group->qgroupid,
					ptr_to_u64(glist->group), GFP_ATOMIC);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}

/*
 * This adjusts the counters for all referenced qgroups if need be.
 */
static int qgroup_adjust_counters(struct btrfs_fs_info *fs_info,
				  u64 root_to_skip, u64 num_bytes,
				  struct ulist *qgroups, u64 seq,
				  int old_roots, int new_roots, int rescan)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_qgroup *qg;
	u64 cur_new_count, cur_old_count;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(qgroups, &uiter))) {
		bool dirty = false;

		qg = u64_to_ptr(unode->aux);
		/*
		 * Wasn't referenced before but is now, add to the reference
		 * counters.
		 */
		if (qg->old_refcnt <= seq && qg->new_refcnt > seq) {
			qg->rfer += num_bytes;
			qg->rfer_cmpr += num_bytes;
			dirty = true;
		}

		/*
		 * Was referenced before but isn't now, subtract from the
		 * reference counters.
		 */
		if (qg->old_refcnt > seq && qg->new_refcnt <= seq) {
			qg->rfer -= num_bytes;
			qg->rfer_cmpr -= num_bytes;
			dirty = true;
		}

		if (qg->old_refcnt < seq)
			cur_old_count = 0;
		else
			cur_old_count = qg->old_refcnt - seq;
		if (qg->new_refcnt < seq)
			cur_new_count = 0;
		else
			cur_new_count = qg->new_refcnt - seq;

		/*
		 * If our refcount was the same as the roots previously but our
		 * new count isn't the same as the number of roots now then we
		 * went from having a exclusive reference on this range to not.
		 */
		if (old_roots && cur_old_count == old_roots &&
		    (cur_new_count != new_roots || new_roots == 0)) {
			WARN_ON(cur_new_count != new_roots && new_roots == 0);
			qg->excl -= num_bytes;
			qg->excl_cmpr -= num_bytes;
			dirty = true;
		}

		/*
		 * If we didn't reference all the roots before but now we do we
		 * have an exclusive reference to this range.
		 */
		if ((!old_roots || (old_roots && cur_old_count != old_roots))
		    && cur_new_count == new_roots) {
			qg->excl += num_bytes;
			qg->excl_cmpr += num_bytes;
			dirty = true;
		}

		if (dirty)
			qgroup_dirty(fs_info, qg);
	}
	return 0;
}

/*
 * If we removed a data extent and there were other references for that bytenr
 * then we need to lookup all referenced roots to make sure we still don't
 * reference this bytenr.  If we do then we can just discard this operation.
 */
static int check_existing_refs(struct btrfs_trans_handle *trans,
			       struct btrfs_fs_info *fs_info,
			       struct btrfs_qgroup_operation *oper)
{
	struct ulist *roots = NULL;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	int ret = 0;

	ret = btrfs_find_all_roots(trans, fs_info, oper->bytenr,
				   oper->elem.seq, &roots);
	if (ret < 0)
		return ret;
	ret = 0;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(roots, &uiter))) {
		if (unode->val == oper->ref_root) {
			ret = 1;
			break;
		}
	}
	ulist_free(roots);
	btrfs_put_tree_mod_seq(fs_info, &oper->elem);

	return ret;
}

/*
 * If we share a reference across multiple roots then we may need to adjust
 * various qgroups referenced and exclusive counters.  The basic premise is this
 *
 * 1) We have seq to represent a 0 count.  Instead of looping through all of the
 * qgroups and resetting their refcount to 0 we just constantly bump this
 * sequence number to act as the base reference count.  This means that if
 * anybody is equal to or below this sequence they were never referenced.  We
 * jack this sequence up by the number of roots we found each time in order to
 * make sure we don't have any overlap.
 *
 * 2) We first search all the roots that reference the area _except_ the root
 * we're acting on currently.  This makes up the old_refcnt of all the qgroups
 * before.
 *
 * 3) We walk all of the qgroups referenced by the root we are currently acting
 * on, and will either adjust old_refcnt in the case of a removal or the
 * new_refcnt in the case of an addition.
 *
 * 4) Finally we walk all the qgroups that are referenced by this range
 * including the root we are acting on currently.  We will adjust the counters
 * based on the number of roots we had and will have after this operation.
 *
 * Take this example as an illustration
 *
 *			[qgroup 1/0]
 *		     /         |          \
 *		[qg 0/0]   [qg 0/1]	[qg 0/2]
 *		   \          |            /
 *		  [	   extent	    ]
 *
 * Say we are adding a reference that is covered by qg 0/0.  The first step
 * would give a refcnt of 1 to qg 0/1 and 0/2 and a refcnt of 2 to qg 1/0 with
 * old_roots being 2.  Because it is adding new_roots will be 1.  We then go
 * through qg 0/0 which will get the new_refcnt set to 1 and add 1 to qg 1/0's
 * new_refcnt, bringing it to 3.  We then walk through all of the qgroups, we
 * notice that the old refcnt for qg 0/0 < the new refcnt, so we added a
 * reference and thus must add the size to the referenced bytes.  Everything
 * else is the same so nothing else changes.
 */
static int qgroup_shared_accounting(struct btrfs_trans_handle *trans,
				    struct btrfs_fs_info *fs_info,
				    struct btrfs_qgroup_operation *oper)
{
	struct ulist *roots = NULL;
	struct ulist *qgroups, *tmp;
	struct btrfs_qgroup *qgroup;
	struct seq_list elem = {};
	u64 seq;
	int old_roots = 0;
	int new_roots = 0;
	int ret = 0;

	if (oper->elem.seq) {
		ret = check_existing_refs(trans, fs_info, oper);
		if (ret < 0)
			return ret;
		if (ret)
			return 0;
	}

	qgroups = ulist_alloc(GFP_NOFS);
	if (!qgroups)
		return -ENOMEM;

	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp) {
		ulist_free(qgroups);
		return -ENOMEM;
	}

	btrfs_get_tree_mod_seq(fs_info, &elem);
	ret = btrfs_find_all_roots(trans, fs_info, oper->bytenr, elem.seq,
				   &roots);
	btrfs_put_tree_mod_seq(fs_info, &elem);
	if (ret < 0) {
		ulist_free(qgroups);
		ulist_free(tmp);
		return ret;
	}
	spin_lock(&fs_info->qgroup_lock);
	qgroup = find_qgroup_rb(fs_info, oper->ref_root);
	if (!qgroup)
		goto out;
	seq = fs_info->qgroup_seq;

	/*
	 * So roots is the list of all the roots currently pointing at the
	 * bytenr, including the ref we are adding if we are adding, or not if
	 * we are removing a ref.  So we pass in the ref_root to skip that root
	 * in our calculations.  We set old_refnct and new_refcnt cause who the
	 * hell knows what everything looked like before, and it doesn't matter
	 * except...
	 */
	ret = qgroup_calc_old_refcnt(fs_info, oper->ref_root, tmp, roots, qgroups,
				     seq, &old_roots, 0);
	if (ret < 0)
		goto out;

	/*
	 * Now adjust the refcounts of the qgroups that care about this
	 * reference, either the old_count in the case of removal or new_count
	 * in the case of an addition.
	 */
	ret = qgroup_calc_new_refcnt(fs_info, oper, qgroup, tmp, qgroups,
				     seq);
	if (ret < 0)
		goto out;

	/*
	 * ...in the case of removals.  If we had a removal before we got around
	 * to processing this operation then we need to find that guy and count
	 * his references as if they really existed so we don't end up screwing
	 * up the exclusive counts.  Then whenever we go to process the delete
	 * everything will be grand and we can account for whatever exclusive
	 * changes need to be made there.  We also have to pass in old_roots so
	 * we have an accurate count of the roots as it pertains to this
	 * operations view of the world.
	 */
	ret = qgroup_account_deleted_refs(fs_info, oper, tmp, qgroups, seq,
					  &old_roots);
	if (ret < 0)
		goto out;

	/*
	 * We are adding our root, need to adjust up the number of roots,
	 * otherwise old_roots is the number of roots we want.
	 */
	if (oper->type == BTRFS_QGROUP_OPER_ADD_SHARED) {
		new_roots = old_roots + 1;
	} else {
		new_roots = old_roots;
		old_roots++;
	}
	fs_info->qgroup_seq += old_roots + 1;


	/*
	 * And now the magic happens, bless Arne for having a pretty elegant
	 * solution for this.
	 */
	qgroup_adjust_counters(fs_info, oper->ref_root, oper->num_bytes,
			       qgroups, seq, old_roots, new_roots, 0);
out:
	spin_unlock(&fs_info->qgroup_lock);
	ulist_free(qgroups);
	ulist_free(roots);
	ulist_free(tmp);
	return ret;
}

/*
 * btrfs_qgroup_account_ref is called for every ref that is added to or deleted
 * from the fs. First, all roots referencing the extent are searched, and
 * then the space is accounted accordingly to the different roots. The
 * accounting algorithm works in 3 steps documented inline.
 */
static int btrfs_qgroup_account(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info,
				struct btrfs_qgroup_operation *oper)
{
	int ret = 0;

	if (!fs_info->quota_enabled)
		return 0;

	BUG_ON(!fs_info->quota_root);

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		if (fs_info->qgroup_rescan_progress.objectid <= oper->bytenr) {
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			return 0;
		}
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	ASSERT(is_fstree(oper->ref_root));

	switch (oper->type) {
	case BTRFS_QGROUP_OPER_ADD_EXCL:
	case BTRFS_QGROUP_OPER_SUB_EXCL:
		ret = qgroup_excl_accounting(fs_info, oper);
		break;
	case BTRFS_QGROUP_OPER_ADD_SHARED:
	case BTRFS_QGROUP_OPER_SUB_SHARED:
		ret = qgroup_shared_accounting(trans, fs_info, oper);
		break;
	default:
		ASSERT(0);
	}
	return ret;
}

/*
 * Needs to be called everytime we run delayed refs, even if there is an error
 * in order to cleanup outstanding operations.
 */
int btrfs_delayed_qgroup_accounting(struct btrfs_trans_handle *trans,
				    struct btrfs_fs_info *fs_info)
{
	struct btrfs_qgroup_operation *oper;
	int ret = 0;

	while (!list_empty(&trans->qgroup_ref_list)) {
		oper = list_first_entry(&trans->qgroup_ref_list,
					struct btrfs_qgroup_operation, list);
		list_del_init(&oper->list);
		if (!ret || !trans->aborted)
			ret = btrfs_qgroup_account(trans, fs_info, oper);
		spin_lock(&fs_info->qgroup_op_lock);
		rb_erase(&oper->n, &fs_info->qgroup_op_tree);
		spin_unlock(&fs_info->qgroup_op_lock);
		btrfs_put_tree_mod_seq(fs_info, &oper->elem);
		kfree(oper);
	}
	return ret;
}

/*
 * called from commit_transaction. Writes all changed qgroups to disk.
 */
int btrfs_run_qgroups(struct btrfs_trans_handle *trans,
		      struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *quota_root = fs_info->quota_root;
	int ret = 0;
	int start_rescan_worker = 0;

	if (!quota_root)
		goto out;

	if (!fs_info->quota_enabled && fs_info->pending_quota_state)
		start_rescan_worker = 1;

	fs_info->quota_enabled = fs_info->pending_quota_state;

	spin_lock(&fs_info->qgroup_lock);
	while (!list_empty(&fs_info->dirty_qgroups)) {
		struct btrfs_qgroup *qgroup;
		qgroup = list_first_entry(&fs_info->dirty_qgroups,
					  struct btrfs_qgroup, dirty);
		list_del_init(&qgroup->dirty);
		spin_unlock(&fs_info->qgroup_lock);
		ret = update_qgroup_info_item(trans, quota_root, qgroup);
		if (ret)
			fs_info->qgroup_flags |=
					BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		spin_lock(&fs_info->qgroup_lock);
	}
	if (fs_info->quota_enabled)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_ON;
	else
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_ON;
	spin_unlock(&fs_info->qgroup_lock);

	ret = update_qgroup_status_item(trans, fs_info, quota_root);
	if (ret)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;

	if (!ret && start_rescan_worker) {
		ret = qgroup_rescan_init(fs_info, 0, 1);
		if (!ret) {
			qgroup_rescan_zero_tracking(fs_info);
			btrfs_queue_work(fs_info->qgroup_rescan_workers,
					 &fs_info->qgroup_rescan_work);
		}
		ret = 0;
	}

out:

	return ret;
}

/*
 * copy the acounting information between qgroups. This is necessary when a
 * snapshot or a subvolume is created
 */
int btrfs_qgroup_inherit(struct btrfs_trans_handle *trans,
			 struct btrfs_fs_info *fs_info, u64 srcid, u64 objectid,
			 struct btrfs_qgroup_inherit *inherit)
{
	int ret = 0;
	int i;
	u64 *i_qgroups;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_qgroup *srcgroup;
	struct btrfs_qgroup *dstgroup;
	u32 level_size = 0;
	u64 nums;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_enabled)
		goto out;

	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	if (inherit) {
		i_qgroups = (u64 *)(inherit + 1);
		nums = inherit->num_qgroups + 2 * inherit->num_ref_copies +
		       2 * inherit->num_excl_copies;
		for (i = 0; i < nums; ++i) {
			srcgroup = find_qgroup_rb(fs_info, *i_qgroups);
			if (!srcgroup) {
				ret = -EINVAL;
				goto out;
			}
			++i_qgroups;
		}
	}

	/*
	 * create a tracking group for the subvol itself
	 */
	ret = add_qgroup_item(trans, quota_root, objectid);
	if (ret)
		goto out;

	if (inherit && inherit->flags & BTRFS_QGROUP_INHERIT_SET_LIMITS) {
		ret = update_qgroup_limit_item(trans, quota_root, objectid,
					       inherit->lim.flags,
					       inherit->lim.max_rfer,
					       inherit->lim.max_excl,
					       inherit->lim.rsv_rfer,
					       inherit->lim.rsv_excl);
		if (ret)
			goto out;
	}

	if (srcid) {
		struct btrfs_root *srcroot;
		struct btrfs_key srckey;
		int srcroot_level;

		srckey.objectid = srcid;
		srckey.type = BTRFS_ROOT_ITEM_KEY;
		srckey.offset = (u64)-1;
		srcroot = btrfs_read_fs_root_no_name(fs_info, &srckey);
		if (IS_ERR(srcroot)) {
			ret = PTR_ERR(srcroot);
			goto out;
		}

		rcu_read_lock();
		srcroot_level = btrfs_header_level(srcroot->node);
		level_size = btrfs_level_size(srcroot, srcroot_level);
		rcu_read_unlock();
	}

	/*
	 * add qgroup to all inherited groups
	 */
	if (inherit) {
		i_qgroups = (u64 *)(inherit + 1);
		for (i = 0; i < inherit->num_qgroups; ++i) {
			ret = add_qgroup_relation_item(trans, quota_root,
						       objectid, *i_qgroups);
			if (ret)
				goto out;
			ret = add_qgroup_relation_item(trans, quota_root,
						       *i_qgroups, objectid);
			if (ret)
				goto out;
			++i_qgroups;
		}
	}


	spin_lock(&fs_info->qgroup_lock);

	dstgroup = add_qgroup_rb(fs_info, objectid);
	if (IS_ERR(dstgroup)) {
		ret = PTR_ERR(dstgroup);
		goto unlock;
	}

	if (srcid) {
		srcgroup = find_qgroup_rb(fs_info, srcid);
		if (!srcgroup)
			goto unlock;

		/*
		 * We call inherit after we clone the root in order to make sure
		 * our counts don't go crazy, so at this point the only
		 * difference between the two roots should be the root node.
		 */
		dstgroup->rfer = srcgroup->rfer;
		dstgroup->rfer_cmpr = srcgroup->rfer_cmpr;
		dstgroup->excl = level_size;
		dstgroup->excl_cmpr = level_size;
		srcgroup->excl = level_size;
		srcgroup->excl_cmpr = level_size;
		qgroup_dirty(fs_info, dstgroup);
		qgroup_dirty(fs_info, srcgroup);
	}

	if (!inherit)
		goto unlock;

	i_qgroups = (u64 *)(inherit + 1);
	for (i = 0; i < inherit->num_qgroups; ++i) {
		ret = add_relation_rb(quota_root->fs_info, objectid,
				      *i_qgroups);
		if (ret)
			goto unlock;
		++i_qgroups;
	}

	for (i = 0; i <  inherit->num_ref_copies; ++i) {
		struct btrfs_qgroup *src;
		struct btrfs_qgroup *dst;

		src = find_qgroup_rb(fs_info, i_qgroups[0]);
		dst = find_qgroup_rb(fs_info, i_qgroups[1]);

		if (!src || !dst) {
			ret = -EINVAL;
			goto unlock;
		}

		dst->rfer = src->rfer - level_size;
		dst->rfer_cmpr = src->rfer_cmpr - level_size;
		i_qgroups += 2;
	}
	for (i = 0; i <  inherit->num_excl_copies; ++i) {
		struct btrfs_qgroup *src;
		struct btrfs_qgroup *dst;

		src = find_qgroup_rb(fs_info, i_qgroups[0]);
		dst = find_qgroup_rb(fs_info, i_qgroups[1]);

		if (!src || !dst) {
			ret = -EINVAL;
			goto unlock;
		}

		dst->excl = src->excl + level_size;
		dst->excl_cmpr = src->excl_cmpr + level_size;
		i_qgroups += 2;
	}

unlock:
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

/*
 * reserve some space for a qgroup and all its parents. The reservation takes
 * place with start_transaction or dealloc_reserve, similar to ENOSPC
 * accounting. If not enough space is available, EDQUOT is returned.
 * We assume that the requested space is new for all qgroups.
 */
int btrfs_qgroup_reserve(struct btrfs_root *root, u64 num_bytes)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 ref_root = root->root_key.objectid;
	int ret = 0;
	struct ulist_node *unode;
	struct ulist_iterator uiter;

	if (!is_fstree(ref_root))
		return 0;

	if (num_bytes == 0)
		return 0;

	spin_lock(&fs_info->qgroup_lock);
	quota_root = fs_info->quota_root;
	if (!quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	/*
	 * in a first step, we check all affected qgroups if any limits would
	 * be exceeded
	 */
	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			(uintptr_t)qgroup, GFP_ATOMIC);
	if (ret < 0)
		goto out;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = u64_to_ptr(unode->aux);

		if ((qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_RFER) &&
		    qg->reserved + (s64)qg->rfer + num_bytes >
		    qg->max_rfer) {
			ret = -EDQUOT;
			goto out;
		}

		if ((qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_EXCL) &&
		    qg->reserved + (s64)qg->excl + num_bytes >
		    qg->max_excl) {
			ret = -EDQUOT;
			goto out;
		}

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					(uintptr_t)glist->group, GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;
	/*
	 * no limits exceeded, now record the reservation into all qgroups
	 */
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;

		qg = u64_to_ptr(unode->aux);

		qg->reserved += num_bytes;
	}

out:
	spin_unlock(&fs_info->qgroup_lock);
	return ret;
}

void btrfs_qgroup_free(struct btrfs_root *root, u64 num_bytes)
{
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	u64 ref_root = root->root_key.objectid;
	int ret = 0;

	if (!is_fstree(ref_root))
		return;

	if (num_bytes == 0)
		return;

	spin_lock(&fs_info->qgroup_lock);

	quota_root = fs_info->quota_root;
	if (!quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			(uintptr_t)qgroup, GFP_ATOMIC);
	if (ret < 0)
		goto out;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = u64_to_ptr(unode->aux);

		qg->reserved -= num_bytes;

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					(uintptr_t)glist->group, GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}

out:
	spin_unlock(&fs_info->qgroup_lock);
}

void assert_qgroups_uptodate(struct btrfs_trans_handle *trans)
{
	if (list_empty(&trans->qgroup_ref_list) && !trans->delayed_ref_elem.seq)
		return;
	btrfs_err(trans->root->fs_info,
		"qgroups not uptodate in trans handle %p:  list is%s empty, "
		"seq is %#x.%x",
		trans, list_empty(&trans->qgroup_ref_list) ? "" : " not",
		(u32)(trans->delayed_ref_elem.seq >> 32),
		(u32)trans->delayed_ref_elem.seq);
	BUG();
}

/*
 * returns < 0 on error, 0 when more leafs are to be scanned.
 * returns 1 when done, 2 when done and FLAG_INCONSISTENT was cleared.
 */
static int
qgroup_rescan_leaf(struct btrfs_fs_info *fs_info, struct btrfs_path *path,
		   struct btrfs_trans_handle *trans, struct ulist *qgroups,
		   struct ulist *tmp, struct extent_buffer *scratch_leaf)
{
	struct btrfs_key found;
	struct ulist *roots = NULL;
	struct seq_list tree_mod_seq_elem = {};
	u64 num_bytes;
	u64 seq;
	int new_roots;
	int slot;
	int ret;

	path->leave_spinning = 1;
	mutex_lock(&fs_info->qgroup_rescan_lock);
	ret = btrfs_search_slot_for_read(fs_info->extent_root,
					 &fs_info->qgroup_rescan_progress,
					 path, 1, 0);

	pr_debug("current progress key (%llu %u %llu), search_slot ret %d\n",
		 fs_info->qgroup_rescan_progress.objectid,
		 fs_info->qgroup_rescan_progress.type,
		 fs_info->qgroup_rescan_progress.offset, ret);

	if (ret) {
		/*
		 * The rescan is about to end, we will not be scanning any
		 * further blocks. We cannot unset the RESCAN flag here, because
		 * we want to commit the transaction if everything went well.
		 * To make the live accounting work in this phase, we set our
		 * scan progress pointer such that every real extent objectid
		 * will be smaller.
		 */
		fs_info->qgroup_rescan_progress.objectid = (u64)-1;
		btrfs_release_path(path);
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		return ret;
	}

	btrfs_item_key_to_cpu(path->nodes[0], &found,
			      btrfs_header_nritems(path->nodes[0]) - 1);
	fs_info->qgroup_rescan_progress.objectid = found.objectid + 1;

	btrfs_get_tree_mod_seq(fs_info, &tree_mod_seq_elem);
	memcpy(scratch_leaf, path->nodes[0], sizeof(*scratch_leaf));
	slot = path->slots[0];
	btrfs_release_path(path);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	for (; slot < btrfs_header_nritems(scratch_leaf); ++slot) {
		btrfs_item_key_to_cpu(scratch_leaf, &found, slot);
		if (found.type != BTRFS_EXTENT_ITEM_KEY &&
		    found.type != BTRFS_METADATA_ITEM_KEY)
			continue;
		if (found.type == BTRFS_METADATA_ITEM_KEY)
			num_bytes = fs_info->extent_root->leafsize;
		else
			num_bytes = found.offset;

		ulist_reinit(qgroups);
		ret = btrfs_find_all_roots(NULL, fs_info, found.objectid, 0,
					   &roots);
		if (ret < 0)
			goto out;
		spin_lock(&fs_info->qgroup_lock);
		seq = fs_info->qgroup_seq;
		fs_info->qgroup_seq += roots->nnodes + 1; /* max refcnt */

		new_roots = 0;
		ret = qgroup_calc_old_refcnt(fs_info, 0, tmp, roots, qgroups,
					     seq, &new_roots, 1);
		if (ret < 0) {
			spin_unlock(&fs_info->qgroup_lock);
			ulist_free(roots);
			goto out;
		}

		ret = qgroup_adjust_counters(fs_info, 0, num_bytes, qgroups,
					     seq, 0, new_roots, 1);
		if (ret < 0) {
			spin_unlock(&fs_info->qgroup_lock);
			ulist_free(roots);
			goto out;
		}
		spin_unlock(&fs_info->qgroup_lock);
		ulist_free(roots);
	}
out:
	btrfs_put_tree_mod_seq(fs_info, &tree_mod_seq_elem);

	return ret;
}

static void btrfs_qgroup_rescan_worker(struct btrfs_work *work)
{
	struct btrfs_fs_info *fs_info = container_of(work, struct btrfs_fs_info,
						     qgroup_rescan_work);
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans = NULL;
	struct ulist *tmp = NULL, *qgroups = NULL;
	struct extent_buffer *scratch_leaf = NULL;
	int err = -ENOMEM;

	path = btrfs_alloc_path();
	if (!path)
		goto out;
	qgroups = ulist_alloc(GFP_NOFS);
	if (!qgroups)
		goto out;
	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp)
		goto out;
	scratch_leaf = kmalloc(sizeof(*scratch_leaf), GFP_NOFS);
	if (!scratch_leaf)
		goto out;

	err = 0;
	while (!err) {
		trans = btrfs_start_transaction(fs_info->fs_root, 0);
		if (IS_ERR(trans)) {
			err = PTR_ERR(trans);
			break;
		}
		if (!fs_info->quota_enabled) {
			err = -EINTR;
		} else {
			err = qgroup_rescan_leaf(fs_info, path, trans,
						 qgroups, tmp, scratch_leaf);
		}
		if (err > 0)
			btrfs_commit_transaction(trans, fs_info->fs_root);
		else
			btrfs_end_transaction(trans, fs_info->fs_root);
	}

out:
	kfree(scratch_leaf);
	ulist_free(qgroups);
	ulist_free(tmp);
	btrfs_free_path(path);

	mutex_lock(&fs_info->qgroup_rescan_lock);
	fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;

	if (err == 2 &&
	    fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	} else if (err < 0) {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	if (err >= 0) {
		btrfs_info(fs_info, "qgroup scan completed%s",
			err == 2 ? " (inconsistency flag cleared)" : "");
	} else {
		btrfs_err(fs_info, "qgroup scan failed with %d", err);
	}

	complete_all(&fs_info->qgroup_rescan_completion);
}

/*
 * Checks that (a) no rescan is running and (b) quota is enabled. Allocates all
 * memory required for the rescan context.
 */
static int
qgroup_rescan_init(struct btrfs_fs_info *fs_info, u64 progress_objectid,
		   int init_flags)
{
	int ret = 0;

	if (!init_flags &&
	    (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) ||
	     !(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON))) {
		ret = -EINVAL;
		goto err;
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);
	spin_lock(&fs_info->qgroup_lock);

	if (init_flags) {
		if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN)
			ret = -EINPROGRESS;
		else if (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON))
			ret = -EINVAL;

		if (ret) {
			spin_unlock(&fs_info->qgroup_lock);
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			goto err;
		}

		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	}

	memset(&fs_info->qgroup_rescan_progress, 0,
		sizeof(fs_info->qgroup_rescan_progress));
	fs_info->qgroup_rescan_progress.objectid = progress_objectid;

	spin_unlock(&fs_info->qgroup_lock);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	init_completion(&fs_info->qgroup_rescan_completion);

	memset(&fs_info->qgroup_rescan_work, 0,
	       sizeof(fs_info->qgroup_rescan_work));
	btrfs_init_work(&fs_info->qgroup_rescan_work,
			btrfs_qgroup_rescan_worker, NULL, NULL);

	if (ret) {
err:
		btrfs_info(fs_info, "qgroup_rescan_init failed with %d", ret);
		return ret;
	}

	return 0;
}

static void
qgroup_rescan_zero_tracking(struct btrfs_fs_info *fs_info)
{
	struct rb_node *n;
	struct btrfs_qgroup *qgroup;

	spin_lock(&fs_info->qgroup_lock);
	/* clear all current qgroup tracking information */
	for (n = rb_first(&fs_info->qgroup_tree); n; n = rb_next(n)) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		qgroup->rfer = 0;
		qgroup->rfer_cmpr = 0;
		qgroup->excl = 0;
		qgroup->excl_cmpr = 0;
	}
	spin_unlock(&fs_info->qgroup_lock);
}

int
btrfs_qgroup_rescan(struct btrfs_fs_info *fs_info)
{
	int ret = 0;
	struct btrfs_trans_handle *trans;

	ret = qgroup_rescan_init(fs_info, 0, 1);
	if (ret)
		return ret;

	/*
	 * We have set the rescan_progress to 0, which means no more
	 * delayed refs will be accounted by btrfs_qgroup_account_ref.
	 * However, btrfs_qgroup_account_ref may be right after its call
	 * to btrfs_find_all_roots, in which case it would still do the
	 * accounting.
	 * To solve this, we're committing the transaction, which will
	 * ensure we run all delayed refs and only after that, we are
	 * going to clear all tracking information for a clean start.
	 */

	trans = btrfs_join_transaction(fs_info->fs_root);
	if (IS_ERR(trans)) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		return PTR_ERR(trans);
	}
	ret = btrfs_commit_transaction(trans, fs_info->fs_root);
	if (ret) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		return ret;
	}

	qgroup_rescan_zero_tracking(fs_info);

	btrfs_queue_work(fs_info->qgroup_rescan_workers,
			 &fs_info->qgroup_rescan_work);

	return 0;
}

int btrfs_qgroup_wait_for_completion(struct btrfs_fs_info *fs_info)
{
	int running;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	spin_lock(&fs_info->qgroup_lock);
	running = fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	spin_unlock(&fs_info->qgroup_lock);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	if (running)
		ret = wait_for_completion_interruptible(
					&fs_info->qgroup_rescan_completion);

	return ret;
}

/*
 * this is only called from open_ctree where we're still single threaded, thus
 * locking is omitted here.
 */
void
btrfs_qgroup_rescan_resume(struct btrfs_fs_info *fs_info)
{
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN)
		btrfs_queue_work(fs_info->qgroup_rescan_workers,
				 &fs_info->qgroup_rescan_work);
}
