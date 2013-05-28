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
	u64 tag;
	u64 refcnt;
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

struct qgroup_rescan {
	struct btrfs_work	work;
	struct btrfs_fs_info	*fs_info;
};

static void qgroup_rescan_start(struct btrfs_fs_info *fs_info,
				struct qgroup_rescan *qscan);

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

/* must be called with qgroup_lock held */
static int del_qgroup_rb(struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	struct btrfs_qgroup *qgroup = find_qgroup_rb(fs_info, qgroupid);
	struct btrfs_qgroup_list *list;

	if (!qgroup)
		return -ENOENT;

	rb_erase(&qgroup->node, &fs_info->qgroup_tree);
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
				printk(KERN_ERR
				 "btrfs: old qgroup version, quota disabled\n");
				goto out;
			}
			if (btrfs_qgroup_status_generation(l, ptr) !=
			    fs_info->generation) {
				flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
				printk(KERN_ERR
					"btrfs: qgroup generation mismatch, "
					"marked as inconsistent\n");
			}
			fs_info->qgroup_flags = btrfs_qgroup_status_flags(l,
									  ptr);
			fs_info->qgroup_rescan_progress.objectid =
					btrfs_qgroup_status_rescan(l, ptr);
			if (fs_info->qgroup_flags &
			    BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
				struct qgroup_rescan *qscan =
					kmalloc(sizeof(*qscan), GFP_NOFS);
				if (!qscan) {
					ret = -ENOMEM;
					goto out;
				}
				fs_info->qgroup_rescan_progress.type = 0;
				fs_info->qgroup_rescan_progress.offset = 0;
				qgroup_rescan_start(fs_info, qscan);
			}
			goto next1;
		}

		if (found_key.type != BTRFS_QGROUP_INFO_KEY &&
		    found_key.type != BTRFS_QGROUP_LIMIT_KEY)
			goto next1;

		qgroup = find_qgroup_rb(fs_info, found_key.offset);
		if ((qgroup && found_key.type == BTRFS_QGROUP_INFO_KEY) ||
		    (!qgroup && found_key.type == BTRFS_QGROUP_LIMIT_KEY)) {
			printk(KERN_ERR "btrfs: inconsitent qgroup config\n");
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
			printk(KERN_WARNING
				"btrfs: orphan qgroup relation 0x%llx->0x%llx\n",
				(unsigned long long)found_key.objectid,
				(unsigned long long)found_key.offset);
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
	}
	btrfs_free_path(path);

	if (ret < 0)
		ulist_free(fs_info->qgroup_ulist);

	return ret < 0 ? ret : 0;
}

/*
 * This is only called from close_ctree() or open_ctree(), both in single-
 * treaded paths. Clean up the in-memory structures. No locking needed.
 */
void btrfs_free_qgroup_config(struct btrfs_fs_info *fs_info)
{
	struct rb_node *n;
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup_list *list;

	while ((n = rb_first(&fs_info->qgroup_tree))) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		rb_erase(n, &fs_info->qgroup_tree);

		while (!list_empty(&qgroup->groups)) {
			list = list_first_entry(&qgroup->groups,
						struct btrfs_qgroup_list,
						next_group);
			list_del(&list->next_group);
			list_del(&list->next_member);
			kfree(list);
		}

		while (!list_empty(&qgroup->members)) {
			list = list_first_entry(&qgroup->members,
						struct btrfs_qgroup_list,
						next_member);
			list_del(&list->next_group);
			list_del(&list->next_member);
			kfree(list);
		}
		kfree(qgroup);
	}
	ulist_free(fs_info->qgroup_ulist);
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
	qgroup_limit = btrfs_item_ptr(l, path->slots[0],
				      struct btrfs_qgroup_limit_item);
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
	qgroup_info = btrfs_item_ptr(l, path->slots[0],
				 struct btrfs_qgroup_info_item);
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
	if (ret)
		ulist_free(fs_info->qgroup_ulist);
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
	btrfs_free_qgroup_config(fs_info);
	spin_unlock(&fs_info->qgroup_lock);

	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

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
		printk(KERN_INFO "unable to update quota limit for %llu\n",
		       (unsigned long long)qgroupid);
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

/*
 * btrfs_qgroup_record_ref is called when the ref is added or deleted. it puts
 * the modification into a list that's later used by btrfs_end_transaction to
 * pass the recorded modifications on to btrfs_qgroup_account_ref.
 */
int btrfs_qgroup_record_ref(struct btrfs_trans_handle *trans,
			    struct btrfs_delayed_ref_node *node,
			    struct btrfs_delayed_extent_op *extent_op)
{
	struct qgroup_update *u;

	BUG_ON(!trans->delayed_ref_elem.seq);
	u = kmalloc(sizeof(*u), GFP_NOFS);
	if (!u)
		return -ENOMEM;

	u->node = node;
	u->extent_op = extent_op;
	list_add_tail(&u->list, &trans->qgroup_ref_list);

	return 0;
}

static int qgroup_account_ref_step1(struct btrfs_fs_info *fs_info,
				    struct ulist *roots, struct ulist *tmp,
				    u64 seq)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct ulist_node *tmp_unode;
	struct ulist_iterator tmp_uiter;
	struct btrfs_qgroup *qg;
	int ret;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(roots, &uiter))) {
		qg = find_qgroup_rb(fs_info, unode->val);
		if (!qg)
			continue;

		ulist_reinit(tmp);
						/* XXX id not needed */
		ret = ulist_add(tmp, qg->qgroupid,
				(u64)(uintptr_t)qg, GFP_ATOMIC);
		if (ret < 0)
			return ret;
		ULIST_ITER_INIT(&tmp_uiter);
		while ((tmp_unode = ulist_next(tmp, &tmp_uiter))) {
			struct btrfs_qgroup_list *glist;

			qg = (struct btrfs_qgroup *)(uintptr_t)tmp_unode->aux;
			if (qg->refcnt < seq)
				qg->refcnt = seq + 1;
			else
				++qg->refcnt;

			list_for_each_entry(glist, &qg->groups, next_group) {
				ret = ulist_add(tmp, glist->group->qgroupid,
						(u64)(uintptr_t)glist->group,
						GFP_ATOMIC);
				if (ret < 0)
					return ret;
			}
		}
	}

	return 0;
}

static int qgroup_account_ref_step2(struct btrfs_fs_info *fs_info,
				    struct ulist *roots, struct ulist *tmp,
				    u64 seq, int sgn, u64 num_bytes,
				    struct btrfs_qgroup *qgroup)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_qgroup *qg;
	struct btrfs_qgroup_list *glist;
	int ret;

	ulist_reinit(tmp);
	ret = ulist_add(tmp, qgroup->qgroupid, (uintptr_t)qgroup, GFP_ATOMIC);
	if (ret < 0)
		return ret;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(tmp, &uiter))) {
		qg = (struct btrfs_qgroup *)(uintptr_t)unode->aux;
		if (qg->refcnt < seq) {
			/* not visited by step 1 */
			qg->rfer += sgn * num_bytes;
			qg->rfer_cmpr += sgn * num_bytes;
			if (roots->nnodes == 0) {
				qg->excl += sgn * num_bytes;
				qg->excl_cmpr += sgn * num_bytes;
			}
			qgroup_dirty(fs_info, qg);
		}
		WARN_ON(qg->tag >= seq);
		qg->tag = seq;

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(tmp, glist->group->qgroupid,
					(uintptr_t)glist->group, GFP_ATOMIC);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int qgroup_account_ref_step3(struct btrfs_fs_info *fs_info,
				    struct ulist *roots, struct ulist *tmp,
				    u64 seq, int sgn, u64 num_bytes)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_qgroup *qg;
	struct ulist_node *tmp_unode;
	struct ulist_iterator tmp_uiter;
	int ret;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(roots, &uiter))) {
		qg = find_qgroup_rb(fs_info, unode->val);
		if (!qg)
			continue;

		ulist_reinit(tmp);
		ret = ulist_add(tmp, qg->qgroupid, (uintptr_t)qg, GFP_ATOMIC);
		if (ret < 0)
			return ret;

		ULIST_ITER_INIT(&tmp_uiter);
		while ((tmp_unode = ulist_next(tmp, &tmp_uiter))) {
			struct btrfs_qgroup_list *glist;

			qg = (struct btrfs_qgroup *)(uintptr_t)tmp_unode->aux;
			if (qg->tag == seq)
				continue;

			if (qg->refcnt - seq == roots->nnodes) {
				qg->excl -= sgn * num_bytes;
				qg->excl_cmpr -= sgn * num_bytes;
				qgroup_dirty(fs_info, qg);
			}

			list_for_each_entry(glist, &qg->groups, next_group) {
				ret = ulist_add(tmp, glist->group->qgroupid,
						(uintptr_t)glist->group,
						GFP_ATOMIC);
				if (ret < 0)
					return ret;
			}
		}
	}

	return 0;
}

/*
 * btrfs_qgroup_account_ref is called for every ref that is added to or deleted
 * from the fs. First, all roots referencing the extent are searched, and
 * then the space is accounted accordingly to the different roots. The
 * accounting algorithm works in 3 steps documented inline.
 */
int btrfs_qgroup_account_ref(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info,
			     struct btrfs_delayed_ref_node *node,
			     struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_key ins;
	struct btrfs_root *quota_root;
	u64 ref_root;
	struct btrfs_qgroup *qgroup;
	struct ulist *roots = NULL;
	u64 seq;
	int ret = 0;
	int sgn;

	if (!fs_info->quota_enabled)
		return 0;

	BUG_ON(!fs_info->quota_root);

	ins.objectid = node->bytenr;
	ins.offset = node->num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;

	if (node->type == BTRFS_TREE_BLOCK_REF_KEY ||
	    node->type == BTRFS_SHARED_BLOCK_REF_KEY) {
		struct btrfs_delayed_tree_ref *ref;
		ref = btrfs_delayed_node_to_tree_ref(node);
		ref_root = ref->root;
	} else if (node->type == BTRFS_EXTENT_DATA_REF_KEY ||
		   node->type == BTRFS_SHARED_DATA_REF_KEY) {
		struct btrfs_delayed_data_ref *ref;
		ref = btrfs_delayed_node_to_data_ref(node);
		ref_root = ref->root;
	} else {
		BUG();
	}

	if (!is_fstree(ref_root)) {
		/*
		 * non-fs-trees are not being accounted
		 */
		return 0;
	}

	switch (node->action) {
	case BTRFS_ADD_DELAYED_REF:
	case BTRFS_ADD_DELAYED_EXTENT:
		sgn = 1;
		seq = btrfs_tree_mod_seq_prev(node->seq);
		break;
	case BTRFS_DROP_DELAYED_REF:
		sgn = -1;
		seq = node->seq;
		break;
	case BTRFS_UPDATE_DELAYED_HEAD:
		return 0;
	default:
		BUG();
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		if (fs_info->qgroup_rescan_progress.objectid <= node->bytenr) {
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			return 0;
		}
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	/*
	 * the delayed ref sequence number we pass depends on the direction of
	 * the operation. for add operations, we pass
	 * tree_mod_log_prev_seq(node->seq) to skip
	 * the delayed ref's current sequence number, because we need the state
	 * of the tree before the add operation. for delete operations, we pass
	 * (node->seq) to include the delayed ref's current sequence number,
	 * because we need the state of the tree after the delete operation.
	 */
	ret = btrfs_find_all_roots(trans, fs_info, node->bytenr, seq, &roots);
	if (ret < 0)
		return ret;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	spin_lock(&fs_info->qgroup_lock);
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		if (fs_info->qgroup_rescan_progress.objectid <= node->bytenr) {
			ret = 0;
			goto unlock;
		}
	}

	quota_root = fs_info->quota_root;
	if (!quota_root)
		goto unlock;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto unlock;

	/*
	 * step 1: for each old ref, visit all nodes once and inc refcnt
	 */
	ulist_reinit(fs_info->qgroup_ulist);
	seq = fs_info->qgroup_seq;
	fs_info->qgroup_seq += roots->nnodes + 1; /* max refcnt */

	ret = qgroup_account_ref_step1(fs_info, roots, fs_info->qgroup_ulist,
				       seq);
	if (ret)
		goto unlock;

	/*
	 * step 2: walk from the new root
	 */
	ret = qgroup_account_ref_step2(fs_info, roots, fs_info->qgroup_ulist,
				       seq, sgn, node->num_bytes, qgroup);
	if (ret)
		goto unlock;

	/*
	 * step 3: walk again from old refs
	 */
	ret = qgroup_account_ref_step3(fs_info, roots, fs_info->qgroup_ulist,
				       seq, sgn, node->num_bytes);
	if (ret)
		goto unlock;

unlock:
	spin_unlock(&fs_info->qgroup_lock);
	mutex_unlock(&fs_info->qgroup_rescan_lock);
	ulist_free(roots);

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
		ret = btrfs_qgroup_rescan(fs_info);
		if (ret)
			pr_err("btrfs: start rescan quota failed: %d\n", ret);
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
		dstgroup->rfer = srcgroup->rfer - level_size;
		dstgroup->rfer_cmpr = srcgroup->rfer_cmpr - level_size;
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

		qg = (struct btrfs_qgroup *)(uintptr_t)unode->aux;

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

		qg = (struct btrfs_qgroup *)(uintptr_t)unode->aux;

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

		qg = (struct btrfs_qgroup *)(uintptr_t)unode->aux;

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
	pr_err("btrfs: qgroups not uptodate in trans handle %p: list is%s empty, seq is %#x.%x\n",
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
qgroup_rescan_leaf(struct qgroup_rescan *qscan, struct btrfs_path *path,
		   struct btrfs_trans_handle *trans, struct ulist *tmp,
		   struct extent_buffer *scratch_leaf)
{
	struct btrfs_key found;
	struct btrfs_fs_info *fs_info = qscan->fs_info;
	struct ulist *roots = NULL;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct seq_list tree_mod_seq_elem = {};
	u64 seq;
	int slot;
	int ret;

	path->leave_spinning = 1;
	mutex_lock(&fs_info->qgroup_rescan_lock);
	ret = btrfs_search_slot_for_read(fs_info->extent_root,
					 &fs_info->qgroup_rescan_progress,
					 path, 1, 0);

	pr_debug("current progress key (%llu %u %llu), search_slot ret %d\n",
		 (unsigned long long)fs_info->qgroup_rescan_progress.objectid,
		 fs_info->qgroup_rescan_progress.type,
		 (unsigned long long)fs_info->qgroup_rescan_progress.offset,
		 ret);

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
		if (found.type != BTRFS_EXTENT_ITEM_KEY)
			continue;
		ret = btrfs_find_all_roots(trans, fs_info, found.objectid,
					   tree_mod_seq_elem.seq, &roots);
		if (ret < 0)
			goto out;
		spin_lock(&fs_info->qgroup_lock);
		seq = fs_info->qgroup_seq;
		fs_info->qgroup_seq += roots->nnodes + 1; /* max refcnt */

		ret = qgroup_account_ref_step1(fs_info, roots, tmp, seq);
		if (ret) {
			spin_unlock(&fs_info->qgroup_lock);
			ulist_free(roots);
			goto out;
		}

		/*
		 * step2 of btrfs_qgroup_account_ref works from a single root,
		 * we're doing all at once here.
		 */
		ulist_reinit(tmp);
		ULIST_ITER_INIT(&uiter);
		while ((unode = ulist_next(roots, &uiter))) {
			struct btrfs_qgroup *qg;

			qg = find_qgroup_rb(fs_info, unode->val);
			if (!qg)
				continue;

			ret = ulist_add(tmp, qg->qgroupid, (uintptr_t)qg,
					GFP_ATOMIC);
			if (ret < 0) {
				spin_unlock(&fs_info->qgroup_lock);
				ulist_free(roots);
				goto out;
			}
		}

		/* this loop is similar to step 2 of btrfs_qgroup_account_ref */
		ULIST_ITER_INIT(&uiter);
		while ((unode = ulist_next(tmp, &uiter))) {
			struct btrfs_qgroup *qg;
			struct btrfs_qgroup_list *glist;

			qg = (struct btrfs_qgroup *)(uintptr_t) unode->aux;
			qg->rfer += found.offset;
			qg->rfer_cmpr += found.offset;
			WARN_ON(qg->tag >= seq);
			if (qg->refcnt - seq == roots->nnodes) {
				qg->excl += found.offset;
				qg->excl_cmpr += found.offset;
			}
			qgroup_dirty(fs_info, qg);

			list_for_each_entry(glist, &qg->groups, next_group) {
				ret = ulist_add(tmp, glist->group->qgroupid,
						(uintptr_t)glist->group,
						GFP_ATOMIC);
				if (ret < 0) {
					spin_unlock(&fs_info->qgroup_lock);
					ulist_free(roots);
					goto out;
				}
			}
		}

		spin_unlock(&fs_info->qgroup_lock);
		ulist_free(roots);
		ret = 0;
	}

out:
	btrfs_put_tree_mod_seq(fs_info, &tree_mod_seq_elem);

	return ret;
}

static void btrfs_qgroup_rescan_worker(struct btrfs_work *work)
{
	struct qgroup_rescan *qscan = container_of(work, struct qgroup_rescan,
						   work);
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_fs_info *fs_info = qscan->fs_info;
	struct ulist *tmp = NULL;
	struct extent_buffer *scratch_leaf = NULL;
	int err = -ENOMEM;

	path = btrfs_alloc_path();
	if (!path)
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
			err = qgroup_rescan_leaf(qscan, path, trans,
						 tmp, scratch_leaf);
		}
		if (err > 0)
			btrfs_commit_transaction(trans, fs_info->fs_root);
		else
			btrfs_end_transaction(trans, fs_info->fs_root);
	}

out:
	kfree(scratch_leaf);
	ulist_free(tmp);
	btrfs_free_path(path);
	kfree(qscan);

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
		pr_info("btrfs: qgroup scan completed%s\n",
			err == 2 ? " (inconsistency flag cleared)" : "");
	} else {
		pr_err("btrfs: qgroup scan failed with %d\n", err);
	}

	complete_all(&fs_info->qgroup_rescan_completion);
}

static void
qgroup_rescan_start(struct btrfs_fs_info *fs_info, struct qgroup_rescan *qscan)
{
	memset(&qscan->work, 0, sizeof(qscan->work));
	qscan->work.func = btrfs_qgroup_rescan_worker;
	qscan->fs_info = fs_info;

	pr_info("btrfs: qgroup scan started\n");
	btrfs_queue_worker(&fs_info->qgroup_rescan_workers, &qscan->work);
}

int
btrfs_qgroup_rescan(struct btrfs_fs_info *fs_info)
{
	int ret = 0;
	struct rb_node *n;
	struct btrfs_qgroup *qgroup;
	struct qgroup_rescan *qscan = kmalloc(sizeof(*qscan), GFP_NOFS);

	if (!qscan)
		return -ENOMEM;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	spin_lock(&fs_info->qgroup_lock);
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN)
		ret = -EINPROGRESS;
	else if (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON))
		ret = -EINVAL;
	if (ret) {
		spin_unlock(&fs_info->qgroup_lock);
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		kfree(qscan);
		return ret;
	}

	fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	memset(&fs_info->qgroup_rescan_progress, 0,
		sizeof(fs_info->qgroup_rescan_progress));
	init_completion(&fs_info->qgroup_rescan_completion);

	/* clear all current qgroup tracking information */
	for (n = rb_first(&fs_info->qgroup_tree); n; n = rb_next(n)) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		qgroup->rfer = 0;
		qgroup->rfer_cmpr = 0;
		qgroup->excl = 0;
		qgroup->excl_cmpr = 0;
	}
	spin_unlock(&fs_info->qgroup_lock);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	qgroup_rescan_start(fs_info, qscan);

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
