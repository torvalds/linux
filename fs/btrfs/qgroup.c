// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/btrfs.h>
#include <linux/sched/mm.h>

#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "locking.h"
#include "ulist.h"
#include "backref.h"
#include "extent_io.h"
#include "qgroup.h"
#include "block-group.h"
#include "sysfs.h"
#include "tree-mod-log.h"
#include "fs.h"
#include "accessors.h"
#include "extent-tree.h"
#include "root-tree.h"
#include "tree-checker.h"

enum btrfs_qgroup_mode btrfs_qgroup_mode(struct btrfs_fs_info *fs_info)
{
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		return BTRFS_QGROUP_MODE_DISABLED;
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_SIMPLE_MODE)
		return BTRFS_QGROUP_MODE_SIMPLE;
	return BTRFS_QGROUP_MODE_FULL;
}

bool btrfs_qgroup_enabled(struct btrfs_fs_info *fs_info)
{
	return btrfs_qgroup_mode(fs_info) != BTRFS_QGROUP_MODE_DISABLED;
}

bool btrfs_qgroup_full_accounting(struct btrfs_fs_info *fs_info)
{
	return btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_FULL;
}

/*
 * Helpers to access qgroup reservation
 *
 * Callers should ensure the lock context and type are valid
 */

static u64 qgroup_rsv_total(const struct btrfs_qgroup *qgroup)
{
	u64 ret = 0;
	int i;

	for (i = 0; i < BTRFS_QGROUP_RSV_LAST; i++)
		ret += qgroup->rsv.values[i];

	return ret;
}

#ifdef CONFIG_BTRFS_DEBUG
static const char *qgroup_rsv_type_str(enum btrfs_qgroup_rsv_type type)
{
	if (type == BTRFS_QGROUP_RSV_DATA)
		return "data";
	if (type == BTRFS_QGROUP_RSV_META_PERTRANS)
		return "meta_pertrans";
	if (type == BTRFS_QGROUP_RSV_META_PREALLOC)
		return "meta_prealloc";
	return NULL;
}
#endif

static void qgroup_rsv_add(struct btrfs_fs_info *fs_info,
			   struct btrfs_qgroup *qgroup, u64 num_bytes,
			   enum btrfs_qgroup_rsv_type type)
{
	trace_qgroup_update_reserve(fs_info, qgroup, num_bytes, type);
	qgroup->rsv.values[type] += num_bytes;
}

static void qgroup_rsv_release(struct btrfs_fs_info *fs_info,
			       struct btrfs_qgroup *qgroup, u64 num_bytes,
			       enum btrfs_qgroup_rsv_type type)
{
	trace_qgroup_update_reserve(fs_info, qgroup, -(s64)num_bytes, type);
	if (qgroup->rsv.values[type] >= num_bytes) {
		qgroup->rsv.values[type] -= num_bytes;
		return;
	}
#ifdef CONFIG_BTRFS_DEBUG
	WARN_RATELIMIT(1,
		"qgroup %llu %s reserved space underflow, have %llu to free %llu",
		qgroup->qgroupid, qgroup_rsv_type_str(type),
		qgroup->rsv.values[type], num_bytes);
#endif
	qgroup->rsv.values[type] = 0;
}

static void qgroup_rsv_add_by_qgroup(struct btrfs_fs_info *fs_info,
				     struct btrfs_qgroup *dest,
				     struct btrfs_qgroup *src)
{
	int i;

	for (i = 0; i < BTRFS_QGROUP_RSV_LAST; i++)
		qgroup_rsv_add(fs_info, dest, src->rsv.values[i], i);
}

static void qgroup_rsv_release_by_qgroup(struct btrfs_fs_info *fs_info,
					 struct btrfs_qgroup *dest,
					  struct btrfs_qgroup *src)
{
	int i;

	for (i = 0; i < BTRFS_QGROUP_RSV_LAST; i++)
		qgroup_rsv_release(fs_info, dest, src->rsv.values[i], i);
}

static void btrfs_qgroup_update_old_refcnt(struct btrfs_qgroup *qg, u64 seq,
					   int mod)
{
	if (qg->old_refcnt < seq)
		qg->old_refcnt = seq;
	qg->old_refcnt += mod;
}

static void btrfs_qgroup_update_new_refcnt(struct btrfs_qgroup *qg, u64 seq,
					   int mod)
{
	if (qg->new_refcnt < seq)
		qg->new_refcnt = seq;
	qg->new_refcnt += mod;
}

static inline u64 btrfs_qgroup_get_old_refcnt(struct btrfs_qgroup *qg, u64 seq)
{
	if (qg->old_refcnt < seq)
		return 0;
	return qg->old_refcnt - seq;
}

static inline u64 btrfs_qgroup_get_new_refcnt(struct btrfs_qgroup *qg, u64 seq)
{
	if (qg->new_refcnt < seq)
		return 0;
	return qg->new_refcnt - seq;
}

/*
 * glue structure to represent the relations between qgroups.
 */
struct btrfs_qgroup_list {
	struct list_head next_group;
	struct list_head next_member;
	struct btrfs_qgroup *group;
	struct btrfs_qgroup *member;
};

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

/*
 * Add qgroup to the filesystem's qgroup tree.
 *
 * Must be called with qgroup_lock held and @prealloc preallocated.
 *
 * The control on the lifespan of @prealloc would be transferred to this
 * function, thus caller should no longer touch @prealloc.
 */
static struct btrfs_qgroup *add_qgroup_rb(struct btrfs_fs_info *fs_info,
					  struct btrfs_qgroup *prealloc,
					  u64 qgroupid)
{
	struct rb_node **p = &fs_info->qgroup_tree.rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_qgroup *qgroup;

	/* Caller must have pre-allocated @prealloc. */
	ASSERT(prealloc);

	while (*p) {
		parent = *p;
		qgroup = rb_entry(parent, struct btrfs_qgroup, node);

		if (qgroup->qgroupid < qgroupid) {
			p = &(*p)->rb_left;
		} else if (qgroup->qgroupid > qgroupid) {
			p = &(*p)->rb_right;
		} else {
			kfree(prealloc);
			return qgroup;
		}
	}

	qgroup = prealloc;
	qgroup->qgroupid = qgroupid;
	INIT_LIST_HEAD(&qgroup->groups);
	INIT_LIST_HEAD(&qgroup->members);
	INIT_LIST_HEAD(&qgroup->dirty);
	INIT_LIST_HEAD(&qgroup->iterator);
	INIT_LIST_HEAD(&qgroup->nested_iterator);

	rb_link_node(&qgroup->node, parent, p);
	rb_insert_color(&qgroup->node, &fs_info->qgroup_tree);

	return qgroup;
}

static void __del_qgroup_rb(struct btrfs_fs_info *fs_info,
			    struct btrfs_qgroup *qgroup)
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
}

/* must be called with qgroup_lock held */
static int del_qgroup_rb(struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	struct btrfs_qgroup *qgroup = find_qgroup_rb(fs_info, qgroupid);

	if (!qgroup)
		return -ENOENT;

	rb_erase(&qgroup->node, &fs_info->qgroup_tree);
	__del_qgroup_rb(fs_info, qgroup);
	return 0;
}

/*
 * Add relation specified by two qgroups.
 *
 * Must be called with qgroup_lock held, the ownership of @prealloc is
 * transferred to this function and caller should not touch it anymore.
 *
 * Return: 0        on success
 *         -ENOENT  if one of the qgroups is NULL
 *         <0       other errors
 */
static int __add_relation_rb(struct btrfs_qgroup_list *prealloc,
			     struct btrfs_qgroup *member,
			     struct btrfs_qgroup *parent)
{
	if (!member || !parent) {
		kfree(prealloc);
		return -ENOENT;
	}

	prealloc->group = parent;
	prealloc->member = member;
	list_add_tail(&prealloc->next_group, &member->groups);
	list_add_tail(&prealloc->next_member, &parent->members);

	return 0;
}

/*
 * Add relation specified by two qgroup ids.
 *
 * Must be called with qgroup_lock held.
 *
 * Return: 0        on success
 *         -ENOENT  if one of the ids does not exist
 *         <0       other errors
 */
static int add_relation_rb(struct btrfs_fs_info *fs_info,
			   struct btrfs_qgroup_list *prealloc,
			   u64 memberid, u64 parentid)
{
	struct btrfs_qgroup *member;
	struct btrfs_qgroup *parent;

	member = find_qgroup_rb(fs_info, memberid);
	parent = find_qgroup_rb(fs_info, parentid);

	return __add_relation_rb(prealloc, member, parent);
}

/* Must be called with qgroup_lock held */
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

static void qgroup_mark_inconsistent(struct btrfs_fs_info *fs_info)
{
	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_SIMPLE)
		return;
	fs_info->qgroup_flags |= (BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT |
				  BTRFS_QGROUP_RUNTIME_FLAG_CANCEL_RESCAN |
				  BTRFS_QGROUP_RUNTIME_FLAG_NO_ACCOUNTING);
}

static void qgroup_read_enable_gen(struct btrfs_fs_info *fs_info,
				   struct extent_buffer *leaf, int slot,
				   struct btrfs_qgroup_status_item *ptr)
{
	ASSERT(btrfs_fs_incompat(fs_info, SIMPLE_QUOTA));
	ASSERT(btrfs_item_size(leaf, slot) >= sizeof(*ptr));
	fs_info->qgroup_enable_gen = btrfs_qgroup_status_enable_gen(leaf, ptr);
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
	u64 rescan_progress = 0;

	if (!fs_info->quota_root)
		return 0;

	fs_info->qgroup_ulist = ulist_alloc(GFP_KERNEL);
	if (!fs_info->qgroup_ulist) {
		ret = -ENOMEM;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	ret = btrfs_sysfs_add_qgroups(fs_info);
	if (ret < 0)
		goto out;
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
			fs_info->qgroup_flags = btrfs_qgroup_status_flags(l, ptr);
			if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_SIMPLE_MODE) {
				qgroup_read_enable_gen(fs_info, l, slot, ptr);
			} else if (btrfs_qgroup_status_generation(l, ptr) != fs_info->generation) {
				qgroup_mark_inconsistent(fs_info);
				btrfs_err(fs_info,
					"qgroup generation mismatch, marked as inconsistent");
			}
			rescan_progress = btrfs_qgroup_status_rescan(l, ptr);
			goto next1;
		}

		if (found_key.type != BTRFS_QGROUP_INFO_KEY &&
		    found_key.type != BTRFS_QGROUP_LIMIT_KEY)
			goto next1;

		qgroup = find_qgroup_rb(fs_info, found_key.offset);
		if ((qgroup && found_key.type == BTRFS_QGROUP_INFO_KEY) ||
		    (!qgroup && found_key.type == BTRFS_QGROUP_LIMIT_KEY)) {
			btrfs_err(fs_info, "inconsistent qgroup config");
			qgroup_mark_inconsistent(fs_info);
		}
		if (!qgroup) {
			struct btrfs_qgroup *prealloc;

			prealloc = kzalloc(sizeof(*prealloc), GFP_KERNEL);
			if (!prealloc) {
				ret = -ENOMEM;
				goto out;
			}
			qgroup = add_qgroup_rb(fs_info, prealloc, found_key.offset);
		}
		ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
		if (ret < 0)
			goto out;

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
		struct btrfs_qgroup_list *list = NULL;

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

		list = kzalloc(sizeof(*list), GFP_KERNEL);
		if (!list) {
			ret = -ENOMEM;
			goto out;
		}
		ret = add_relation_rb(fs_info, list, found_key.objectid,
				      found_key.offset);
		list = NULL;
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
	btrfs_free_path(path);
	fs_info->qgroup_flags |= flags;
	if (ret >= 0) {
		if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON)
			set_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
		if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN)
			ret = qgroup_rescan_init(fs_info, rescan_progress, 0);
	} else {
		ulist_free(fs_info->qgroup_ulist);
		fs_info->qgroup_ulist = NULL;
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		btrfs_sysfs_del_qgroups(fs_info);
	}

	return ret < 0 ? ret : 0;
}

/*
 * Called in close_ctree() when quota is still enabled.  This verifies we don't
 * leak some reserved space.
 *
 * Return false if no reserved space is left.
 * Return true if some reserved space is leaked.
 */
bool btrfs_check_quota_leak(struct btrfs_fs_info *fs_info)
{
	struct rb_node *node;
	bool ret = false;

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_DISABLED)
		return ret;
	/*
	 * Since we're unmounting, there is no race and no need to grab qgroup
	 * lock.  And here we don't go post-order to provide a more user
	 * friendly sorted result.
	 */
	for (node = rb_first(&fs_info->qgroup_tree); node; node = rb_next(node)) {
		struct btrfs_qgroup *qgroup;
		int i;

		qgroup = rb_entry(node, struct btrfs_qgroup, node);
		for (i = 0; i < BTRFS_QGROUP_RSV_LAST; i++) {
			if (qgroup->rsv.values[i]) {
				ret = true;
				btrfs_warn(fs_info,
		"qgroup %hu/%llu has unreleased space, type %d rsv %llu",
				   btrfs_qgroup_level(qgroup->qgroupid),
				   btrfs_qgroup_subvolid(qgroup->qgroupid),
				   i, qgroup->rsv.values[i]);
			}
		}
	}
	return ret;
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
		__del_qgroup_rb(fs_info, qgroup);
		btrfs_sysfs_del_one_qgroup(fs_info, qgroup);
		kfree(qgroup);
	}
	/*
	 * We call btrfs_free_qgroup_config() when unmounting
	 * filesystem and disabling quota, so we set qgroup_ulist
	 * to be null here to avoid double free.
	 */
	ulist_free(fs_info->qgroup_ulist);
	fs_info->qgroup_ulist = NULL;
	btrfs_sysfs_del_qgroups(fs_info);
}

static int add_qgroup_relation_item(struct btrfs_trans_handle *trans, u64 src,
				    u64 dst)
{
	int ret;
	struct btrfs_root *quota_root = trans->fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = src;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = dst;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key, 0);

	btrfs_mark_buffer_dirty(trans, path->nodes[0]);

	btrfs_free_path(path);
	return ret;
}

static int del_qgroup_relation_item(struct btrfs_trans_handle *trans, u64 src,
				    u64 dst)
{
	int ret;
	struct btrfs_root *quota_root = trans->fs_info->quota_root;
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

	if (btrfs_is_testing(quota_root->fs_info))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroupid;

	/*
	 * Avoid a transaction abort by catching -EEXIST here. In that
	 * case, we proceed by re-initializing the existing structure
	 * on disk.
	 */

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*qgroup_info));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	qgroup_info = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_info_item);
	btrfs_set_qgroup_info_generation(leaf, qgroup_info, trans->transid);
	btrfs_set_qgroup_info_rfer(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_rfer_cmpr(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_excl(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_excl_cmpr(leaf, qgroup_info, 0);

	btrfs_mark_buffer_dirty(trans, leaf);

	btrfs_release_path(path);

	key.type = BTRFS_QGROUP_LIMIT_KEY;
	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*qgroup_limit));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	qgroup_limit = btrfs_item_ptr(leaf, path->slots[0],
				  struct btrfs_qgroup_limit_item);
	btrfs_set_qgroup_limit_flags(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_max_rfer(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_max_excl(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_rsv_rfer(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_rsv_excl(leaf, qgroup_limit, 0);

	btrfs_mark_buffer_dirty(trans, leaf);

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int del_qgroup_item(struct btrfs_trans_handle *trans, u64 qgroupid)
{
	int ret;
	struct btrfs_root *quota_root = trans->fs_info->quota_root;
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
				    struct btrfs_qgroup *qgroup)
{
	struct btrfs_root *quota_root = trans->fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_limit_item *qgroup_limit;
	int ret;
	int slot;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_LIMIT_KEY;
	key.offset = qgroup->qgroupid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, quota_root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	qgroup_limit = btrfs_item_ptr(l, slot, struct btrfs_qgroup_limit_item);
	btrfs_set_qgroup_limit_flags(l, qgroup_limit, qgroup->lim_flags);
	btrfs_set_qgroup_limit_max_rfer(l, qgroup_limit, qgroup->max_rfer);
	btrfs_set_qgroup_limit_max_excl(l, qgroup_limit, qgroup->max_excl);
	btrfs_set_qgroup_limit_rsv_rfer(l, qgroup_limit, qgroup->rsv_rfer);
	btrfs_set_qgroup_limit_rsv_excl(l, qgroup_limit, qgroup->rsv_excl);

	btrfs_mark_buffer_dirty(trans, l);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_info_item(struct btrfs_trans_handle *trans,
				   struct btrfs_qgroup *qgroup)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_info_item *qgroup_info;
	int ret;
	int slot;

	if (btrfs_is_testing(fs_info))
		return 0;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroup->qgroupid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, quota_root, &key, path, 0, 1);
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

	btrfs_mark_buffer_dirty(trans, l);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_status_item(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root = fs_info->quota_root;
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

	ret = btrfs_search_slot(trans, quota_root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr(l, slot, struct btrfs_qgroup_status_item);
	btrfs_set_qgroup_status_flags(l, ptr, fs_info->qgroup_flags &
				      BTRFS_QGROUP_STATUS_FLAGS_MASK);
	btrfs_set_qgroup_status_generation(l, ptr, trans->transid);
	btrfs_set_qgroup_status_rescan(l, ptr,
				fs_info->qgroup_rescan_progress.objectid);

	btrfs_mark_buffer_dirty(trans, l);

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
	btrfs_free_path(path);
	return ret;
}

int btrfs_quota_enable(struct btrfs_fs_info *fs_info,
		       struct btrfs_ioctl_quota_ctl_args *quota_ctl_args)
{
	struct btrfs_root *quota_root;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_path *path = NULL;
	struct btrfs_qgroup_status_item *ptr;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_qgroup *qgroup = NULL;
	struct btrfs_qgroup *prealloc = NULL;
	struct btrfs_trans_handle *trans = NULL;
	struct ulist *ulist = NULL;
	const bool simple = (quota_ctl_args->cmd == BTRFS_QUOTA_CTL_ENABLE_SIMPLE_QUOTA);
	int ret = 0;
	int slot;

	/*
	 * We need to have subvol_sem write locked, to prevent races between
	 * concurrent tasks trying to enable quotas, because we will unlock
	 * and relock qgroup_ioctl_lock before setting fs_info->quota_root
	 * and before setting BTRFS_FS_QUOTA_ENABLED.
	 */
	lockdep_assert_held_write(&fs_info->subvol_sem);

	if (btrfs_fs_incompat(fs_info, EXTENT_TREE_V2)) {
		btrfs_err(fs_info,
			  "qgroups are currently unsupported in extent tree v2");
		return -EINVAL;
	}

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (fs_info->quota_root)
		goto out;

	ulist = ulist_alloc(GFP_KERNEL);
	if (!ulist) {
		ret = -ENOMEM;
		goto out;
	}

	ret = btrfs_sysfs_add_qgroups(fs_info);
	if (ret < 0)
		goto out;

	/*
	 * Unlock qgroup_ioctl_lock before starting the transaction. This is to
	 * avoid lock acquisition inversion problems (reported by lockdep) between
	 * qgroup_ioctl_lock and the vfs freeze semaphores, acquired when we
	 * start a transaction.
	 * After we started the transaction lock qgroup_ioctl_lock again and
	 * check if someone else created the quota root in the meanwhile. If so,
	 * just return success and release the transaction handle.
	 *
	 * Also we don't need to worry about someone else calling
	 * btrfs_sysfs_add_qgroups() after we unlock and getting an error because
	 * that function returns 0 (success) when the sysfs entries already exist.
	 */
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	/*
	 * 1 for quota root item
	 * 1 for BTRFS_QGROUP_STATUS item
	 *
	 * Yet we also need 2*n items for a QGROUP_INFO/QGROUP_LIMIT items
	 * per subvolume. However those are not currently reserved since it
	 * would be a lot of overkill.
	 */
	trans = btrfs_start_transaction(tree_root, 2);

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	if (fs_info->quota_root)
		goto out;

	fs_info->qgroup_ulist = ulist;
	ulist = NULL;

	/*
	 * initially create the quota tree
	 */
	quota_root = btrfs_create_tree(trans, BTRFS_QUOTA_TREE_OBJECTID);
	if (IS_ERR(quota_root)) {
		ret =  PTR_ERR(quota_root);
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		btrfs_abort_transaction(trans, ret);
		goto out_free_root;
	}

	key.objectid = 0;
	key.type = BTRFS_QGROUP_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*ptr));
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	leaf = path->nodes[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_status_item);
	btrfs_set_qgroup_status_generation(leaf, ptr, trans->transid);
	btrfs_set_qgroup_status_version(leaf, ptr, BTRFS_QGROUP_STATUS_VERSION);
	fs_info->qgroup_flags = BTRFS_QGROUP_STATUS_FLAG_ON;
	if (simple) {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_SIMPLE_MODE;
		btrfs_set_qgroup_status_enable_gen(leaf, ptr, trans->transid);
	} else {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	}
	btrfs_set_qgroup_status_flags(leaf, ptr, fs_info->qgroup_flags &
				      BTRFS_QGROUP_STATUS_FLAGS_MASK);
	btrfs_set_qgroup_status_rescan(leaf, ptr, 0);

	btrfs_mark_buffer_dirty(trans, leaf);

	key.objectid = 0;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = 0;

	btrfs_release_path(path);
	ret = btrfs_search_slot_for_read(tree_root, &key, path, 1, 0);
	if (ret > 0)
		goto out_add_root;
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.type == BTRFS_ROOT_REF_KEY) {

			/* Release locks on tree_root before we access quota_root */
			btrfs_release_path(path);

			/* We should not have a stray @prealloc pointer. */
			ASSERT(prealloc == NULL);
			prealloc = kzalloc(sizeof(*prealloc), GFP_NOFS);
			if (!prealloc) {
				ret = -ENOMEM;
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}

			ret = add_qgroup_item(trans, quota_root,
					      found_key.offset);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}

			qgroup = add_qgroup_rb(fs_info, prealloc, found_key.offset);
			prealloc = NULL;
			if (IS_ERR(qgroup)) {
				ret = PTR_ERR(qgroup);
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}
			ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
			if (ret < 0) {
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}
			ret = btrfs_search_slot_for_read(tree_root, &found_key,
							 path, 1, 0);
			if (ret < 0) {
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}
			if (ret > 0) {
				/*
				 * Shouldn't happen, but in case it does we
				 * don't need to do the btrfs_next_item, just
				 * continue.
				 */
				continue;
			}
		}
		ret = btrfs_next_item(tree_root, path);
		if (ret < 0) {
			btrfs_abort_transaction(trans, ret);
			goto out_free_path;
		}
		if (ret)
			break;
	}

out_add_root:
	btrfs_release_path(path);
	ret = add_qgroup_item(trans, quota_root, BTRFS_FS_TREE_OBJECTID);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	ASSERT(prealloc == NULL);
	prealloc = kzalloc(sizeof(*prealloc), GFP_NOFS);
	if (!prealloc) {
		ret = -ENOMEM;
		goto out_free_path;
	}
	qgroup = add_qgroup_rb(fs_info, prealloc, BTRFS_FS_TREE_OBJECTID);
	prealloc = NULL;
	ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	fs_info->qgroup_enable_gen = trans->transid;

	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	/*
	 * Commit the transaction while not holding qgroup_ioctl_lock, to avoid
	 * a deadlock with tasks concurrently doing other qgroup operations, such
	 * adding/removing qgroups or adding/deleting qgroup relations for example,
	 * because all qgroup operations first start or join a transaction and then
	 * lock the qgroup_ioctl_lock mutex.
	 * We are safe from a concurrent task trying to enable quotas, by calling
	 * this function, since we are serialized by fs_info->subvol_sem.
	 */
	ret = btrfs_commit_transaction(trans);
	trans = NULL;
	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (ret)
		goto out_free_path;

	/*
	 * Set quota enabled flag after committing the transaction, to avoid
	 * deadlocks on fs_info->qgroup_ioctl_lock with concurrent snapshot
	 * creation.
	 */
	spin_lock(&fs_info->qgroup_lock);
	fs_info->quota_root = quota_root;
	set_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
	if (simple)
		btrfs_set_fs_incompat(fs_info, SIMPLE_QUOTA);
	spin_unlock(&fs_info->qgroup_lock);

	/* Skip rescan for simple qgroups. */
	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_SIMPLE)
		goto out_free_path;

	ret = qgroup_rescan_init(fs_info, 0, 1);
	if (!ret) {
	        qgroup_rescan_zero_tracking(fs_info);
		fs_info->qgroup_rescan_running = true;
	        btrfs_queue_work(fs_info->qgroup_rescan_workers,
	                         &fs_info->qgroup_rescan_work);
	} else {
		/*
		 * We have set both BTRFS_FS_QUOTA_ENABLED and
		 * BTRFS_QGROUP_STATUS_FLAG_ON, so we can only fail with
		 * -EINPROGRESS. That can happen because someone started the
		 * rescan worker by calling quota rescan ioctl before we
		 * attempted to initialize the rescan worker. Failure due to
		 * quotas disabled in the meanwhile is not possible, because
		 * we are holding a write lock on fs_info->subvol_sem, which
		 * is also acquired when disabling quotas.
		 * Ignore such error, and any other error would need to undo
		 * everything we did in the transaction we just committed.
		 */
		ASSERT(ret == -EINPROGRESS);
		ret = 0;
	}

out_free_path:
	btrfs_free_path(path);
out_free_root:
	if (ret)
		btrfs_put_root(quota_root);
out:
	if (ret) {
		ulist_free(fs_info->qgroup_ulist);
		fs_info->qgroup_ulist = NULL;
		btrfs_sysfs_del_qgroups(fs_info);
	}
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	if (ret && trans)
		btrfs_end_transaction(trans);
	else if (trans)
		ret = btrfs_end_transaction(trans);
	ulist_free(ulist);
	kfree(prealloc);
	return ret;
}

/*
 * It is possible to have outstanding ordered extents which reserved bytes
 * before we disabled. We need to fully flush delalloc, ordered extents, and a
 * commit to ensure that we don't leak such reservations, only to have them
 * come back if we re-enable.
 *
 * - enable simple quotas
 * - reserve space
 * - release it, store rsv_bytes in OE
 * - disable quotas
 * - enable simple quotas (qgroup rsv are all 0)
 * - OE finishes
 * - run delayed refs
 * - free rsv_bytes, resulting in miscounting or even underflow
 */
static int flush_reservations(struct btrfs_fs_info *fs_info)
{
	struct btrfs_trans_handle *trans;
	int ret;

	ret = btrfs_start_delalloc_roots(fs_info, LONG_MAX, false);
	if (ret)
		return ret;
	btrfs_wait_ordered_roots(fs_info, U64_MAX, 0, (u64)-1);
	trans = btrfs_join_transaction(fs_info->tree_root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);
	ret = btrfs_commit_transaction(trans);

	return ret;
}

int btrfs_quota_disable(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *quota_root;
	struct btrfs_trans_handle *trans = NULL;
	int ret = 0;

	/*
	 * We need to have subvol_sem write locked to prevent races with
	 * snapshot creation.
	 */
	lockdep_assert_held_write(&fs_info->subvol_sem);

	/*
	 * Relocation will mess with backrefs, so make sure we have the
	 * cleaner_mutex held to protect us from relocate.
	 */
	lockdep_assert_held(&fs_info->cleaner_mutex);

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root)
		goto out;

	/*
	 * Unlock the qgroup_ioctl_lock mutex before waiting for the rescan worker to
	 * complete. Otherwise we can deadlock because btrfs_remove_qgroup() needs
	 * to lock that mutex while holding a transaction handle and the rescan
	 * worker needs to commit a transaction.
	 */
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	/*
	 * Request qgroup rescan worker to complete and wait for it. This wait
	 * must be done before transaction start for quota disable since it may
	 * deadlock with transaction by the qgroup rescan worker.
	 */
	clear_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
	btrfs_qgroup_wait_for_completion(fs_info, false);

	/*
	 * We have nothing held here and no trans handle, just return the error
	 * if there is one.
	 */
	ret = flush_reservations(fs_info);
	if (ret)
		return ret;

	/*
	 * 1 For the root item
	 *
	 * We should also reserve enough items for the quota tree deletion in
	 * btrfs_clean_quota_tree but this is not done.
	 *
	 * Also, we must always start a transaction without holding the mutex
	 * qgroup_ioctl_lock, see btrfs_quota_enable().
	 */
	trans = btrfs_start_transaction(fs_info->tree_root, 1);

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		set_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
		goto out;
	}

	if (!fs_info->quota_root)
		goto out;

	spin_lock(&fs_info->qgroup_lock);
	quota_root = fs_info->quota_root;
	fs_info->quota_root = NULL;
	fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_ON;
	fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_SIMPLE_MODE;
	fs_info->qgroup_drop_subtree_thres = BTRFS_MAX_LEVEL;
	spin_unlock(&fs_info->qgroup_lock);

	btrfs_free_qgroup_config(fs_info);

	ret = btrfs_clean_quota_tree(trans, quota_root);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = btrfs_del_root(trans, &quota_root->root_key);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	spin_lock(&fs_info->trans_lock);
	list_del(&quota_root->dirty_list);
	spin_unlock(&fs_info->trans_lock);

	btrfs_tree_lock(quota_root->node);
	btrfs_clear_buffer_dirty(trans, quota_root->node);
	btrfs_tree_unlock(quota_root->node);
	btrfs_free_tree_block(trans, btrfs_root_id(quota_root),
			      quota_root->node, 0, 1);

	btrfs_put_root(quota_root);

out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	if (ret && trans)
		btrfs_end_transaction(trans);
	else if (trans)
		ret = btrfs_commit_transaction(trans);
	return ret;
}

static void qgroup_dirty(struct btrfs_fs_info *fs_info,
			 struct btrfs_qgroup *qgroup)
{
	if (list_empty(&qgroup->dirty))
		list_add(&qgroup->dirty, &fs_info->dirty_qgroups);
}

static void qgroup_iterator_add(struct list_head *head, struct btrfs_qgroup *qgroup)
{
	if (!list_empty(&qgroup->iterator))
		return;

	list_add_tail(&qgroup->iterator, head);
}

static void qgroup_iterator_clean(struct list_head *head)
{
	while (!list_empty(head)) {
		struct btrfs_qgroup *qgroup;

		qgroup = list_first_entry(head, struct btrfs_qgroup, iterator);
		list_del_init(&qgroup->iterator);
	}
}

/*
 * The easy accounting, we're updating qgroup relationship whose child qgroup
 * only has exclusive extents.
 *
 * In this case, all exclusive extents will also be exclusive for parent, so
 * excl/rfer just get added/removed.
 *
 * So is qgroup reservation space, which should also be added/removed to
 * parent.
 * Or when child tries to release reservation space, parent will underflow its
 * reservation (for relationship adding case).
 *
 * Caller should hold fs_info->qgroup_lock.
 */
static int __qgroup_excl_accounting(struct btrfs_fs_info *fs_info, u64 ref_root,
				    struct btrfs_qgroup *src, int sign)
{
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup *cur;
	LIST_HEAD(qgroup_list);
	u64 num_bytes = src->excl;
	int ret = 0;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	qgroup_iterator_add(&qgroup_list, qgroup);
	list_for_each_entry(cur, &qgroup_list, iterator) {
		struct btrfs_qgroup_list *glist;

		qgroup->rfer += sign * num_bytes;
		qgroup->rfer_cmpr += sign * num_bytes;

		WARN_ON(sign < 0 && qgroup->excl < num_bytes);
		qgroup->excl += sign * num_bytes;
		qgroup->excl_cmpr += sign * num_bytes;

		if (sign > 0)
			qgroup_rsv_add_by_qgroup(fs_info, qgroup, src);
		else
			qgroup_rsv_release_by_qgroup(fs_info, qgroup, src);
		qgroup_dirty(fs_info, qgroup);

		/* Append parent qgroups to @qgroup_list. */
		list_for_each_entry(glist, &qgroup->groups, next_group)
			qgroup_iterator_add(&qgroup_list, glist->group);
	}
	ret = 0;
out:
	qgroup_iterator_clean(&qgroup_list);
	return ret;
}


/*
 * Quick path for updating qgroup with only excl refs.
 *
 * In that case, just update all parent will be enough.
 * Or we needs to do a full rescan.
 * Caller should also hold fs_info->qgroup_lock.
 *
 * Return 0 for quick update, return >0 for need to full rescan
 * and mark INCONSISTENT flag.
 * Return < 0 for other error.
 */
static int quick_update_accounting(struct btrfs_fs_info *fs_info,
				   u64 src, u64 dst, int sign)
{
	struct btrfs_qgroup *qgroup;
	int ret = 1;

	qgroup = find_qgroup_rb(fs_info, src);
	if (!qgroup)
		goto out;
	if (qgroup->excl == qgroup->rfer) {
		ret = __qgroup_excl_accounting(fs_info, dst, qgroup, sign);
		if (ret < 0)
			goto out;
		ret = 0;
	}
out:
	if (ret)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	return ret;
}

int btrfs_add_qgroup_relation(struct btrfs_trans_handle *trans, u64 src, u64 dst)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup *member;
	struct btrfs_qgroup_list *list;
	struct btrfs_qgroup_list *prealloc = NULL;
	int ret = 0;

	/* Check the level of src and dst first */
	if (btrfs_qgroup_level(src) >= btrfs_qgroup_level(dst))
		return -EINVAL;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
		ret = -ENOTCONN;
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

	prealloc = kzalloc(sizeof(*list), GFP_NOFS);
	if (!prealloc) {
		ret = -ENOMEM;
		goto out;
	}
	ret = add_qgroup_relation_item(trans, src, dst);
	if (ret)
		goto out;

	ret = add_qgroup_relation_item(trans, dst, src);
	if (ret) {
		del_qgroup_relation_item(trans, src, dst);
		goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	ret = __add_relation_rb(prealloc, member, parent);
	prealloc = NULL;
	if (ret < 0) {
		spin_unlock(&fs_info->qgroup_lock);
		goto out;
	}
	ret = quick_update_accounting(fs_info, src, dst, 1);
	spin_unlock(&fs_info->qgroup_lock);
out:
	kfree(prealloc);
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

static int __del_qgroup_relation(struct btrfs_trans_handle *trans, u64 src,
				 u64 dst)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup *member;
	struct btrfs_qgroup_list *list;
	bool found = false;
	int ret = 0;
	int ret2;

	if (!fs_info->quota_root) {
		ret = -ENOTCONN;
		goto out;
	}

	member = find_qgroup_rb(fs_info, src);
	parent = find_qgroup_rb(fs_info, dst);
	/*
	 * The parent/member pair doesn't exist, then try to delete the dead
	 * relation items only.
	 */
	if (!member || !parent)
		goto delete_item;

	/* check if such qgroup relation exist firstly */
	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent) {
			found = true;
			break;
		}
	}

delete_item:
	ret = del_qgroup_relation_item(trans, src, dst);
	if (ret < 0 && ret != -ENOENT)
		goto out;
	ret2 = del_qgroup_relation_item(trans, dst, src);
	if (ret2 < 0 && ret2 != -ENOENT)
		goto out;

	/* At least one deletion succeeded, return 0 */
	if (!ret || !ret2)
		ret = 0;

	if (found) {
		spin_lock(&fs_info->qgroup_lock);
		del_relation_rb(fs_info, src, dst);
		ret = quick_update_accounting(fs_info, src, dst, -1);
		spin_unlock(&fs_info->qgroup_lock);
	}
out:
	return ret;
}

int btrfs_del_qgroup_relation(struct btrfs_trans_handle *trans, u64 src,
			      u64 dst)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	ret = __del_qgroup_relation(trans, src, dst);
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	return ret;
}

int btrfs_create_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup *prealloc = NULL;
	int ret = 0;

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_DISABLED)
		return 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
		ret = -ENOTCONN;
		goto out;
	}
	quota_root = fs_info->quota_root;
	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (qgroup) {
		ret = -EEXIST;
		goto out;
	}

	prealloc = kzalloc(sizeof(*prealloc), GFP_NOFS);
	if (!prealloc) {
		ret = -ENOMEM;
		goto out;
	}

	ret = add_qgroup_item(trans, quota_root, qgroupid);
	if (ret)
		goto out;

	spin_lock(&fs_info->qgroup_lock);
	qgroup = add_qgroup_rb(fs_info, prealloc, qgroupid);
	spin_unlock(&fs_info->qgroup_lock);
	prealloc = NULL;

	ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	kfree(prealloc);
	return ret;
}

static bool qgroup_has_usage(struct btrfs_qgroup *qgroup)
{
	return (qgroup->rfer > 0 || qgroup->rfer_cmpr > 0 ||
		qgroup->excl > 0 || qgroup->excl_cmpr > 0 ||
		qgroup->rsv.values[BTRFS_QGROUP_RSV_DATA] > 0 ||
		qgroup->rsv.values[BTRFS_QGROUP_RSV_META_PREALLOC] > 0 ||
		qgroup->rsv.values[BTRFS_QGROUP_RSV_META_PERTRANS] > 0);
}

int btrfs_remove_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup_list *list;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
		ret = -ENOTCONN;
		goto out;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	}

	if (is_fstree(qgroupid) && qgroup_has_usage(qgroup)) {
		ret = -EBUSY;
		goto out;
	}

	/* Check if there are no children of this qgroup */
	if (!list_empty(&qgroup->members)) {
		ret = -EBUSY;
		goto out;
	}

	ret = del_qgroup_item(trans, qgroupid);
	if (ret && ret != -ENOENT)
		goto out;

	while (!list_empty(&qgroup->groups)) {
		list = list_first_entry(&qgroup->groups,
					struct btrfs_qgroup_list, next_group);
		ret = __del_qgroup_relation(trans, qgroupid,
					    list->group->qgroupid);
		if (ret)
			goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	del_qgroup_rb(fs_info, qgroupid);
	spin_unlock(&fs_info->qgroup_lock);

	/*
	 * Remove the qgroup from sysfs now without holding the qgroup_lock
	 * spinlock, since the sysfs_remove_group() function needs to take
	 * the mutex kernfs_mutex through kernfs_remove_by_name_ns().
	 */
	btrfs_sysfs_del_one_qgroup(fs_info, qgroup);
	kfree(qgroup);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_limit_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid,
		       struct btrfs_qgroup_limit *limit)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup *qgroup;
	int ret = 0;
	/* Sometimes we would want to clear the limit on this qgroup.
	 * To meet this requirement, we treat the -1 as a special value
	 * which tell kernel to clear the limit on this qgroup.
	 */
	const u64 CLEAR_VALUE = -1;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
		ret = -ENOTCONN;
		goto out;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	if (limit->flags & BTRFS_QGROUP_LIMIT_MAX_RFER) {
		if (limit->max_rfer == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_MAX_RFER;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_MAX_RFER;
			qgroup->max_rfer = 0;
		} else {
			qgroup->max_rfer = limit->max_rfer;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_MAX_EXCL) {
		if (limit->max_excl == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_MAX_EXCL;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_MAX_EXCL;
			qgroup->max_excl = 0;
		} else {
			qgroup->max_excl = limit->max_excl;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_RSV_RFER) {
		if (limit->rsv_rfer == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_RSV_RFER;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_RSV_RFER;
			qgroup->rsv_rfer = 0;
		} else {
			qgroup->rsv_rfer = limit->rsv_rfer;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_RSV_EXCL) {
		if (limit->rsv_excl == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_RSV_EXCL;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_RSV_EXCL;
			qgroup->rsv_excl = 0;
		} else {
			qgroup->rsv_excl = limit->rsv_excl;
		}
	}
	qgroup->lim_flags |= limit->flags;

	spin_unlock(&fs_info->qgroup_lock);

	ret = update_qgroup_limit_item(trans, qgroup);
	if (ret) {
		qgroup_mark_inconsistent(fs_info);
		btrfs_info(fs_info, "unable to update quota limit for %llu",
		       qgroupid);
	}

out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

/*
 * Inform qgroup to trace one dirty extent, its info is recorded in @record.
 * So qgroup can account it at transaction committing time.
 *
 * No lock version, caller must acquire delayed ref lock and allocated memory,
 * then call btrfs_qgroup_trace_extent_post() after exiting lock context.
 *
 * Return 0 for success insert
 * Return >0 for existing record, caller can free @record safely.
 * Error is not possible
 */
int btrfs_qgroup_trace_extent_nolock(struct btrfs_fs_info *fs_info,
				struct btrfs_delayed_ref_root *delayed_refs,
				struct btrfs_qgroup_extent_record *record)
{
	struct rb_node **p = &delayed_refs->dirty_extent_root.rb_node;
	struct rb_node *parent_node = NULL;
	struct btrfs_qgroup_extent_record *entry;
	u64 bytenr = record->bytenr;

	if (!btrfs_qgroup_full_accounting(fs_info))
		return 1;

	lockdep_assert_held(&delayed_refs->lock);
	trace_btrfs_qgroup_trace_extent(fs_info, record);

	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct btrfs_qgroup_extent_record,
				 node);
		if (bytenr < entry->bytenr) {
			p = &(*p)->rb_left;
		} else if (bytenr > entry->bytenr) {
			p = &(*p)->rb_right;
		} else {
			if (record->data_rsv && !entry->data_rsv) {
				entry->data_rsv = record->data_rsv;
				entry->data_rsv_refroot =
					record->data_rsv_refroot;
			}
			return 1;
		}
	}

	rb_link_node(&record->node, parent_node, p);
	rb_insert_color(&record->node, &delayed_refs->dirty_extent_root);
	return 0;
}

/*
 * Post handler after qgroup_trace_extent_nolock().
 *
 * NOTE: Current qgroup does the expensive backref walk at transaction
 * committing time with TRANS_STATE_COMMIT_DOING, this blocks incoming
 * new transaction.
 * This is designed to allow btrfs_find_all_roots() to get correct new_roots
 * result.
 *
 * However for old_roots there is no need to do backref walk at that time,
 * since we search commit roots to walk backref and result will always be
 * correct.
 *
 * Due to the nature of no lock version, we can't do backref there.
 * So we must call btrfs_qgroup_trace_extent_post() after exiting
 * spinlock context.
 *
 * TODO: If we can fix and prove btrfs_find_all_roots() can get correct result
 * using current root, then we can move all expensive backref walk out of
 * transaction committing, but not now as qgroup accounting will be wrong again.
 */
int btrfs_qgroup_trace_extent_post(struct btrfs_trans_handle *trans,
				   struct btrfs_qgroup_extent_record *qrecord)
{
	struct btrfs_backref_walk_ctx ctx = { 0 };
	int ret;

	if (!btrfs_qgroup_full_accounting(trans->fs_info))
		return 0;
	/*
	 * We are always called in a context where we are already holding a
	 * transaction handle. Often we are called when adding a data delayed
	 * reference from btrfs_truncate_inode_items() (truncating or unlinking),
	 * in which case we will be holding a write lock on extent buffer from a
	 * subvolume tree. In this case we can't allow btrfs_find_all_roots() to
	 * acquire fs_info->commit_root_sem, because that is a higher level lock
	 * that must be acquired before locking any extent buffers.
	 *
	 * So we want btrfs_find_all_roots() to not acquire the commit_root_sem
	 * but we can't pass it a non-NULL transaction handle, because otherwise
	 * it would not use commit roots and would lock extent buffers, causing
	 * a deadlock if it ends up trying to read lock the same extent buffer
	 * that was previously write locked at btrfs_truncate_inode_items().
	 *
	 * So pass a NULL transaction handle to btrfs_find_all_roots() and
	 * explicitly tell it to not acquire the commit_root_sem - if we are
	 * holding a transaction handle we don't need its protection.
	 */
	ASSERT(trans != NULL);

	if (trans->fs_info->qgroup_flags & BTRFS_QGROUP_RUNTIME_FLAG_NO_ACCOUNTING)
		return 0;

	ctx.bytenr = qrecord->bytenr;
	ctx.fs_info = trans->fs_info;

	ret = btrfs_find_all_roots(&ctx, true);
	if (ret < 0) {
		qgroup_mark_inconsistent(trans->fs_info);
		btrfs_warn(trans->fs_info,
"error accounting new delayed refs extent (err code: %d), quota inconsistent",
			ret);
		return 0;
	}

	/*
	 * Here we don't need to get the lock of
	 * trans->transaction->delayed_refs, since inserted qrecord won't
	 * be deleted, only qrecord->node may be modified (new qrecord insert)
	 *
	 * So modifying qrecord->old_roots is safe here
	 */
	qrecord->old_roots = ctx.roots;
	return 0;
}

/*
 * Inform qgroup to trace one dirty extent, specified by @bytenr and
 * @num_bytes.
 * So qgroup can account it at commit trans time.
 *
 * Better encapsulated version, with memory allocation and backref walk for
 * commit roots.
 * So this can sleep.
 *
 * Return 0 if the operation is done.
 * Return <0 for error, like memory allocation failure or invalid parameter
 * (NULL trans)
 */
int btrfs_qgroup_trace_extent(struct btrfs_trans_handle *trans, u64 bytenr,
			      u64 num_bytes)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup_extent_record *record;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	if (!btrfs_qgroup_full_accounting(fs_info) || bytenr == 0 || num_bytes == 0)
		return 0;
	record = kzalloc(sizeof(*record), GFP_NOFS);
	if (!record)
		return -ENOMEM;

	delayed_refs = &trans->transaction->delayed_refs;
	record->bytenr = bytenr;
	record->num_bytes = num_bytes;
	record->old_roots = NULL;

	spin_lock(&delayed_refs->lock);
	ret = btrfs_qgroup_trace_extent_nolock(fs_info, delayed_refs, record);
	spin_unlock(&delayed_refs->lock);
	if (ret > 0) {
		kfree(record);
		return 0;
	}
	return btrfs_qgroup_trace_extent_post(trans, record);
}

/*
 * Inform qgroup to trace all leaf items of data
 *
 * Return 0 for success
 * Return <0 for error(ENOMEM)
 */
int btrfs_qgroup_trace_leaf_items(struct btrfs_trans_handle *trans,
				  struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int nr = btrfs_header_nritems(eb);
	int i, extent_type, ret;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	u64 bytenr, num_bytes;

	/* We can be called directly from walk_up_proc() */
	if (!btrfs_qgroup_full_accounting(fs_info))
		return 0;

	for (i = 0; i < nr; i++) {
		btrfs_item_key_to_cpu(eb, &key, i);

		if (key.type != BTRFS_EXTENT_DATA_KEY)
			continue;

		fi = btrfs_item_ptr(eb, i, struct btrfs_file_extent_item);
		/* filter out non qgroup-accountable extents  */
		extent_type = btrfs_file_extent_type(eb, fi);

		if (extent_type == BTRFS_FILE_EXTENT_INLINE)
			continue;

		bytenr = btrfs_file_extent_disk_bytenr(eb, fi);
		if (!bytenr)
			continue;

		num_bytes = btrfs_file_extent_disk_num_bytes(eb, fi);

		ret = btrfs_qgroup_trace_extent(trans, bytenr, num_bytes);
		if (ret)
			return ret;
	}
	cond_resched();
	return 0;
}

/*
 * Walk up the tree from the bottom, freeing leaves and any interior
 * nodes which have had all slots visited. If a node (leaf or
 * interior) is freed, the node above it will have it's slot
 * incremented. The root node will never be freed.
 *
 * At the end of this function, we should have a path which has all
 * slots incremented to the next position for a search. If we need to
 * read a new node it will be NULL and the node above it will have the
 * correct slot selected for a later read.
 *
 * If we increment the root nodes slot counter past the number of
 * elements, 1 is returned to signal completion of the search.
 */
static int adjust_slots_upwards(struct btrfs_path *path, int root_level)
{
	int level = 0;
	int nr, slot;
	struct extent_buffer *eb;

	if (root_level == 0)
		return 1;

	while (level <= root_level) {
		eb = path->nodes[level];
		nr = btrfs_header_nritems(eb);
		path->slots[level]++;
		slot = path->slots[level];
		if (slot >= nr || level == 0) {
			/*
			 * Don't free the root -  we will detect this
			 * condition after our loop and return a
			 * positive value for caller to stop walking the tree.
			 */
			if (level != root_level) {
				btrfs_tree_unlock_rw(eb, path->locks[level]);
				path->locks[level] = 0;

				free_extent_buffer(eb);
				path->nodes[level] = NULL;
				path->slots[level] = 0;
			}
		} else {
			/*
			 * We have a valid slot to walk back down
			 * from. Stop here so caller can process these
			 * new nodes.
			 */
			break;
		}

		level++;
	}

	eb = path->nodes[root_level];
	if (path->slots[root_level] >= btrfs_header_nritems(eb))
		return 1;

	return 0;
}

/*
 * Helper function to trace a subtree tree block swap.
 *
 * The swap will happen in highest tree block, but there may be a lot of
 * tree blocks involved.
 *
 * For example:
 *  OO = Old tree blocks
 *  NN = New tree blocks allocated during balance
 *
 *           File tree (257)                  Reloc tree for 257
 * L2              OO                                NN
 *               /    \                            /    \
 * L1          OO      OO (a)                    OO      NN (a)
 *            / \     / \                       / \     / \
 * L0       OO   OO OO   OO                   OO   OO NN   NN
 *                  (b)  (c)                          (b)  (c)
 *
 * When calling qgroup_trace_extent_swap(), we will pass:
 * @src_eb = OO(a)
 * @dst_path = [ nodes[1] = NN(a), nodes[0] = NN(c) ]
 * @dst_level = 0
 * @root_level = 1
 *
 * In that case, qgroup_trace_extent_swap() will search from OO(a) to
 * reach OO(c), then mark both OO(c) and NN(c) as qgroup dirty.
 *
 * The main work of qgroup_trace_extent_swap() can be split into 3 parts:
 *
 * 1) Tree search from @src_eb
 *    It should acts as a simplified btrfs_search_slot().
 *    The key for search can be extracted from @dst_path->nodes[dst_level]
 *    (first key).
 *
 * 2) Mark the final tree blocks in @src_path and @dst_path qgroup dirty
 *    NOTE: In above case, OO(a) and NN(a) won't be marked qgroup dirty.
 *    They should be marked during previous (@dst_level = 1) iteration.
 *
 * 3) Mark file extents in leaves dirty
 *    We don't have good way to pick out new file extents only.
 *    So we still follow the old method by scanning all file extents in
 *    the leave.
 *
 * This function can free us from keeping two paths, thus later we only need
 * to care about how to iterate all new tree blocks in reloc tree.
 */
static int qgroup_trace_extent_swap(struct btrfs_trans_handle* trans,
				    struct extent_buffer *src_eb,
				    struct btrfs_path *dst_path,
				    int dst_level, int root_level,
				    bool trace_leaf)
{
	struct btrfs_key key;
	struct btrfs_path *src_path;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	u32 nodesize = fs_info->nodesize;
	int cur_level = root_level;
	int ret;

	BUG_ON(dst_level > root_level);
	/* Level mismatch */
	if (btrfs_header_level(src_eb) != root_level)
		return -EINVAL;

	src_path = btrfs_alloc_path();
	if (!src_path) {
		ret = -ENOMEM;
		goto out;
	}

	if (dst_level)
		btrfs_node_key_to_cpu(dst_path->nodes[dst_level], &key, 0);
	else
		btrfs_item_key_to_cpu(dst_path->nodes[dst_level], &key, 0);

	/* For src_path */
	atomic_inc(&src_eb->refs);
	src_path->nodes[root_level] = src_eb;
	src_path->slots[root_level] = dst_path->slots[root_level];
	src_path->locks[root_level] = 0;

	/* A simplified version of btrfs_search_slot() */
	while (cur_level >= dst_level) {
		struct btrfs_key src_key;
		struct btrfs_key dst_key;

		if (src_path->nodes[cur_level] == NULL) {
			struct extent_buffer *eb;
			int parent_slot;

			eb = src_path->nodes[cur_level + 1];
			parent_slot = src_path->slots[cur_level + 1];

			eb = btrfs_read_node_slot(eb, parent_slot);
			if (IS_ERR(eb)) {
				ret = PTR_ERR(eb);
				goto out;
			}

			src_path->nodes[cur_level] = eb;

			btrfs_tree_read_lock(eb);
			src_path->locks[cur_level] = BTRFS_READ_LOCK;
		}

		src_path->slots[cur_level] = dst_path->slots[cur_level];
		if (cur_level) {
			btrfs_node_key_to_cpu(dst_path->nodes[cur_level],
					&dst_key, dst_path->slots[cur_level]);
			btrfs_node_key_to_cpu(src_path->nodes[cur_level],
					&src_key, src_path->slots[cur_level]);
		} else {
			btrfs_item_key_to_cpu(dst_path->nodes[cur_level],
					&dst_key, dst_path->slots[cur_level]);
			btrfs_item_key_to_cpu(src_path->nodes[cur_level],
					&src_key, src_path->slots[cur_level]);
		}
		/* Content mismatch, something went wrong */
		if (btrfs_comp_cpu_keys(&dst_key, &src_key)) {
			ret = -ENOENT;
			goto out;
		}
		cur_level--;
	}

	/*
	 * Now both @dst_path and @src_path have been populated, record the tree
	 * blocks for qgroup accounting.
	 */
	ret = btrfs_qgroup_trace_extent(trans, src_path->nodes[dst_level]->start,
					nodesize);
	if (ret < 0)
		goto out;
	ret = btrfs_qgroup_trace_extent(trans, dst_path->nodes[dst_level]->start,
					nodesize);
	if (ret < 0)
		goto out;

	/* Record leaf file extents */
	if (dst_level == 0 && trace_leaf) {
		ret = btrfs_qgroup_trace_leaf_items(trans, src_path->nodes[0]);
		if (ret < 0)
			goto out;
		ret = btrfs_qgroup_trace_leaf_items(trans, dst_path->nodes[0]);
	}
out:
	btrfs_free_path(src_path);
	return ret;
}

/*
 * Helper function to do recursive generation-aware depth-first search, to
 * locate all new tree blocks in a subtree of reloc tree.
 *
 * E.g. (OO = Old tree blocks, NN = New tree blocks, whose gen == last_snapshot)
 *         reloc tree
 * L2         NN (a)
 *          /    \
 * L1    OO        NN (b)
 *      /  \      /  \
 * L0  OO  OO    OO  NN
 *               (c) (d)
 * If we pass:
 * @dst_path = [ nodes[1] = NN(b), nodes[0] = NULL ],
 * @cur_level = 1
 * @root_level = 1
 *
 * We will iterate through tree blocks NN(b), NN(d) and info qgroup to trace
 * above tree blocks along with their counter parts in file tree.
 * While during search, old tree blocks OO(c) will be skipped as tree block swap
 * won't affect OO(c).
 */
static int qgroup_trace_new_subtree_blocks(struct btrfs_trans_handle* trans,
					   struct extent_buffer *src_eb,
					   struct btrfs_path *dst_path,
					   int cur_level, int root_level,
					   u64 last_snapshot, bool trace_leaf)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct extent_buffer *eb;
	bool need_cleanup = false;
	int ret = 0;
	int i;

	/* Level sanity check */
	if (cur_level < 0 || cur_level >= BTRFS_MAX_LEVEL - 1 ||
	    root_level < 0 || root_level >= BTRFS_MAX_LEVEL - 1 ||
	    root_level < cur_level) {
		btrfs_err_rl(fs_info,
			"%s: bad levels, cur_level=%d root_level=%d",
			__func__, cur_level, root_level);
		return -EUCLEAN;
	}

	/* Read the tree block if needed */
	if (dst_path->nodes[cur_level] == NULL) {
		int parent_slot;
		u64 child_gen;

		/*
		 * dst_path->nodes[root_level] must be initialized before
		 * calling this function.
		 */
		if (cur_level == root_level) {
			btrfs_err_rl(fs_info,
	"%s: dst_path->nodes[%d] not initialized, root_level=%d cur_level=%d",
				__func__, root_level, root_level, cur_level);
			return -EUCLEAN;
		}

		/*
		 * We need to get child blockptr/gen from parent before we can
		 * read it.
		  */
		eb = dst_path->nodes[cur_level + 1];
		parent_slot = dst_path->slots[cur_level + 1];
		child_gen = btrfs_node_ptr_generation(eb, parent_slot);

		/* This node is old, no need to trace */
		if (child_gen < last_snapshot)
			goto out;

		eb = btrfs_read_node_slot(eb, parent_slot);
		if (IS_ERR(eb)) {
			ret = PTR_ERR(eb);
			goto out;
		}

		dst_path->nodes[cur_level] = eb;
		dst_path->slots[cur_level] = 0;

		btrfs_tree_read_lock(eb);
		dst_path->locks[cur_level] = BTRFS_READ_LOCK;
		need_cleanup = true;
	}

	/* Now record this tree block and its counter part for qgroups */
	ret = qgroup_trace_extent_swap(trans, src_eb, dst_path, cur_level,
				       root_level, trace_leaf);
	if (ret < 0)
		goto cleanup;

	eb = dst_path->nodes[cur_level];

	if (cur_level > 0) {
		/* Iterate all child tree blocks */
		for (i = 0; i < btrfs_header_nritems(eb); i++) {
			/* Skip old tree blocks as they won't be swapped */
			if (btrfs_node_ptr_generation(eb, i) < last_snapshot)
				continue;
			dst_path->slots[cur_level] = i;

			/* Recursive call (at most 7 times) */
			ret = qgroup_trace_new_subtree_blocks(trans, src_eb,
					dst_path, cur_level - 1, root_level,
					last_snapshot, trace_leaf);
			if (ret < 0)
				goto cleanup;
		}
	}

cleanup:
	if (need_cleanup) {
		/* Clean up */
		btrfs_tree_unlock_rw(dst_path->nodes[cur_level],
				     dst_path->locks[cur_level]);
		free_extent_buffer(dst_path->nodes[cur_level]);
		dst_path->nodes[cur_level] = NULL;
		dst_path->slots[cur_level] = 0;
		dst_path->locks[cur_level] = 0;
	}
out:
	return ret;
}

static int qgroup_trace_subtree_swap(struct btrfs_trans_handle *trans,
				struct extent_buffer *src_eb,
				struct extent_buffer *dst_eb,
				u64 last_snapshot, bool trace_leaf)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_path *dst_path = NULL;
	int level;
	int ret;

	if (!btrfs_qgroup_full_accounting(fs_info))
		return 0;

	/* Wrong parameter order */
	if (btrfs_header_generation(src_eb) > btrfs_header_generation(dst_eb)) {
		btrfs_err_rl(fs_info,
		"%s: bad parameter order, src_gen=%llu dst_gen=%llu", __func__,
			     btrfs_header_generation(src_eb),
			     btrfs_header_generation(dst_eb));
		return -EUCLEAN;
	}

	if (!extent_buffer_uptodate(src_eb) || !extent_buffer_uptodate(dst_eb)) {
		ret = -EIO;
		goto out;
	}

	level = btrfs_header_level(dst_eb);
	dst_path = btrfs_alloc_path();
	if (!dst_path) {
		ret = -ENOMEM;
		goto out;
	}
	/* For dst_path */
	atomic_inc(&dst_eb->refs);
	dst_path->nodes[level] = dst_eb;
	dst_path->slots[level] = 0;
	dst_path->locks[level] = 0;

	/* Do the generation aware breadth-first search */
	ret = qgroup_trace_new_subtree_blocks(trans, src_eb, dst_path, level,
					      level, last_snapshot, trace_leaf);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	btrfs_free_path(dst_path);
	if (ret < 0)
		qgroup_mark_inconsistent(fs_info);
	return ret;
}

/*
 * Inform qgroup to trace a whole subtree, including all its child tree
 * blocks and data.
 * The root tree block is specified by @root_eb.
 *
 * Normally used by relocation(tree block swap) and subvolume deletion.
 *
 * Return 0 for success
 * Return <0 for error(ENOMEM or tree search error)
 */
int btrfs_qgroup_trace_subtree(struct btrfs_trans_handle *trans,
			       struct extent_buffer *root_eb,
			       u64 root_gen, int root_level)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int ret = 0;
	int level;
	u8 drop_subptree_thres;
	struct extent_buffer *eb = root_eb;
	struct btrfs_path *path = NULL;

	ASSERT(0 <= root_level && root_level < BTRFS_MAX_LEVEL);
	ASSERT(root_eb != NULL);

	if (!btrfs_qgroup_full_accounting(fs_info))
		return 0;

	spin_lock(&fs_info->qgroup_lock);
	drop_subptree_thres = fs_info->qgroup_drop_subtree_thres;
	spin_unlock(&fs_info->qgroup_lock);

	/*
	 * This function only gets called for snapshot drop, if we hit a high
	 * node here, it means we are going to change ownership for quite a lot
	 * of extents, which will greatly slow down btrfs_commit_transaction().
	 *
	 * So here if we find a high tree here, we just skip the accounting and
	 * mark qgroup inconsistent.
	 */
	if (root_level >= drop_subptree_thres) {
		qgroup_mark_inconsistent(fs_info);
		return 0;
	}

	if (!extent_buffer_uptodate(root_eb)) {
		struct btrfs_tree_parent_check check = {
			.has_first_key = false,
			.transid = root_gen,
			.level = root_level
		};

		ret = btrfs_read_extent_buffer(root_eb, &check);
		if (ret)
			goto out;
	}

	if (root_level == 0) {
		ret = btrfs_qgroup_trace_leaf_items(trans, root_eb);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * Walk down the tree.  Missing extent blocks are filled in as
	 * we go. Metadata is accounted every time we read a new
	 * extent block.
	 *
	 * When we reach a leaf, we account for file extent items in it,
	 * walk back up the tree (adjusting slot pointers as we go)
	 * and restart the search process.
	 */
	atomic_inc(&root_eb->refs);	/* For path */
	path->nodes[root_level] = root_eb;
	path->slots[root_level] = 0;
	path->locks[root_level] = 0; /* so release_path doesn't try to unlock */
walk_down:
	level = root_level;
	while (level >= 0) {
		if (path->nodes[level] == NULL) {
			int parent_slot;
			u64 child_bytenr;

			/*
			 * We need to get child blockptr from parent before we
			 * can read it.
			  */
			eb = path->nodes[level + 1];
			parent_slot = path->slots[level + 1];
			child_bytenr = btrfs_node_blockptr(eb, parent_slot);

			eb = btrfs_read_node_slot(eb, parent_slot);
			if (IS_ERR(eb)) {
				ret = PTR_ERR(eb);
				goto out;
			}

			path->nodes[level] = eb;
			path->slots[level] = 0;

			btrfs_tree_read_lock(eb);
			path->locks[level] = BTRFS_READ_LOCK;

			ret = btrfs_qgroup_trace_extent(trans, child_bytenr,
							fs_info->nodesize);
			if (ret)
				goto out;
		}

		if (level == 0) {
			ret = btrfs_qgroup_trace_leaf_items(trans,
							    path->nodes[level]);
			if (ret)
				goto out;

			/* Nonzero return here means we completed our search */
			ret = adjust_slots_upwards(path, root_level);
			if (ret)
				break;

			/* Restart search with new slots */
			goto walk_down;
		}

		level--;
	}

	ret = 0;
out:
	btrfs_free_path(path);

	return ret;
}

static void qgroup_iterator_nested_add(struct list_head *head, struct btrfs_qgroup *qgroup)
{
	if (!list_empty(&qgroup->nested_iterator))
		return;

	list_add_tail(&qgroup->nested_iterator, head);
}

static void qgroup_iterator_nested_clean(struct list_head *head)
{
	while (!list_empty(head)) {
		struct btrfs_qgroup *qgroup;

		qgroup = list_first_entry(head, struct btrfs_qgroup, nested_iterator);
		list_del_init(&qgroup->nested_iterator);
	}
}

#define UPDATE_NEW	0
#define UPDATE_OLD	1
/*
 * Walk all of the roots that points to the bytenr and adjust their refcnts.
 */
static void qgroup_update_refcnt(struct btrfs_fs_info *fs_info,
				 struct ulist *roots, struct list_head *qgroups,
				 u64 seq, int update_old)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_qgroup *qg;

	if (!roots)
		return;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(roots, &uiter))) {
		LIST_HEAD(tmp);

		qg = find_qgroup_rb(fs_info, unode->val);
		if (!qg)
			continue;

		qgroup_iterator_nested_add(qgroups, qg);
		qgroup_iterator_add(&tmp, qg);
		list_for_each_entry(qg, &tmp, iterator) {
			struct btrfs_qgroup_list *glist;

			if (update_old)
				btrfs_qgroup_update_old_refcnt(qg, seq, 1);
			else
				btrfs_qgroup_update_new_refcnt(qg, seq, 1);

			list_for_each_entry(glist, &qg->groups, next_group) {
				qgroup_iterator_nested_add(qgroups, glist->group);
				qgroup_iterator_add(&tmp, glist->group);
			}
		}
		qgroup_iterator_clean(&tmp);
	}
}

/*
 * Update qgroup rfer/excl counters.
 * Rfer update is easy, codes can explain themselves.
 *
 * Excl update is tricky, the update is split into 2 parts.
 * Part 1: Possible exclusive <-> sharing detect:
 *	|	A	|	!A	|
 *  -------------------------------------
 *  B	|	*	|	-	|
 *  -------------------------------------
 *  !B	|	+	|	**	|
 *  -------------------------------------
 *
 * Conditions:
 * A:	cur_old_roots < nr_old_roots	(not exclusive before)
 * !A:	cur_old_roots == nr_old_roots	(possible exclusive before)
 * B:	cur_new_roots < nr_new_roots	(not exclusive now)
 * !B:	cur_new_roots == nr_new_roots	(possible exclusive now)
 *
 * Results:
 * +: Possible sharing -> exclusive	-: Possible exclusive -> sharing
 * *: Definitely not changed.		**: Possible unchanged.
 *
 * For !A and !B condition, the exception is cur_old/new_roots == 0 case.
 *
 * To make the logic clear, we first use condition A and B to split
 * combination into 4 results.
 *
 * Then, for result "+" and "-", check old/new_roots == 0 case, as in them
 * only on variant maybe 0.
 *
 * Lastly, check result **, since there are 2 variants maybe 0, split them
 * again(2x2).
 * But this time we don't need to consider other things, the codes and logic
 * is easy to understand now.
 */
static void qgroup_update_counters(struct btrfs_fs_info *fs_info,
				   struct list_head *qgroups, u64 nr_old_roots,
				   u64 nr_new_roots, u64 num_bytes, u64 seq)
{
	struct btrfs_qgroup *qg;

	list_for_each_entry(qg, qgroups, nested_iterator) {
		u64 cur_new_count, cur_old_count;
		bool dirty = false;

		cur_old_count = btrfs_qgroup_get_old_refcnt(qg, seq);
		cur_new_count = btrfs_qgroup_get_new_refcnt(qg, seq);

		trace_qgroup_update_counters(fs_info, qg, cur_old_count,
					     cur_new_count);

		/* Rfer update part */
		if (cur_old_count == 0 && cur_new_count > 0) {
			qg->rfer += num_bytes;
			qg->rfer_cmpr += num_bytes;
			dirty = true;
		}
		if (cur_old_count > 0 && cur_new_count == 0) {
			qg->rfer -= num_bytes;
			qg->rfer_cmpr -= num_bytes;
			dirty = true;
		}

		/* Excl update part */
		/* Exclusive/none -> shared case */
		if (cur_old_count == nr_old_roots &&
		    cur_new_count < nr_new_roots) {
			/* Exclusive -> shared */
			if (cur_old_count != 0) {
				qg->excl -= num_bytes;
				qg->excl_cmpr -= num_bytes;
				dirty = true;
			}
		}

		/* Shared -> exclusive/none case */
		if (cur_old_count < nr_old_roots &&
		    cur_new_count == nr_new_roots) {
			/* Shared->exclusive */
			if (cur_new_count != 0) {
				qg->excl += num_bytes;
				qg->excl_cmpr += num_bytes;
				dirty = true;
			}
		}

		/* Exclusive/none -> exclusive/none case */
		if (cur_old_count == nr_old_roots &&
		    cur_new_count == nr_new_roots) {
			if (cur_old_count == 0) {
				/* None -> exclusive/none */

				if (cur_new_count != 0) {
					/* None -> exclusive */
					qg->excl += num_bytes;
					qg->excl_cmpr += num_bytes;
					dirty = true;
				}
				/* None -> none, nothing changed */
			} else {
				/* Exclusive -> exclusive/none */

				if (cur_new_count == 0) {
					/* Exclusive -> none */
					qg->excl -= num_bytes;
					qg->excl_cmpr -= num_bytes;
					dirty = true;
				}
				/* Exclusive -> exclusive, nothing changed */
			}
		}

		if (dirty)
			qgroup_dirty(fs_info, qg);
	}
}

/*
 * Check if the @roots potentially is a list of fs tree roots
 *
 * Return 0 for definitely not a fs/subvol tree roots ulist
 * Return 1 for possible fs/subvol tree roots in the list (considering an empty
 *          one as well)
 */
static int maybe_fs_roots(struct ulist *roots)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;

	/* Empty one, still possible for fs roots */
	if (!roots || roots->nnodes == 0)
		return 1;

	ULIST_ITER_INIT(&uiter);
	unode = ulist_next(roots, &uiter);
	if (!unode)
		return 1;

	/*
	 * If it contains fs tree roots, then it must belong to fs/subvol
	 * trees.
	 * If it contains a non-fs tree, it won't be shared with fs/subvol trees.
	 */
	return is_fstree(unode->val);
}

int btrfs_qgroup_account_extent(struct btrfs_trans_handle *trans, u64 bytenr,
				u64 num_bytes, struct ulist *old_roots,
				struct ulist *new_roots)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	LIST_HEAD(qgroups);
	u64 seq;
	u64 nr_new_roots = 0;
	u64 nr_old_roots = 0;
	int ret = 0;

	/*
	 * If quotas get disabled meanwhile, the resources need to be freed and
	 * we can't just exit here.
	 */
	if (!btrfs_qgroup_full_accounting(fs_info) ||
	    fs_info->qgroup_flags & BTRFS_QGROUP_RUNTIME_FLAG_NO_ACCOUNTING)
		goto out_free;

	if (new_roots) {
		if (!maybe_fs_roots(new_roots))
			goto out_free;
		nr_new_roots = new_roots->nnodes;
	}
	if (old_roots) {
		if (!maybe_fs_roots(old_roots))
			goto out_free;
		nr_old_roots = old_roots->nnodes;
	}

	/* Quick exit, either not fs tree roots, or won't affect any qgroup */
	if (nr_old_roots == 0 && nr_new_roots == 0)
		goto out_free;

	trace_btrfs_qgroup_account_extent(fs_info, trans->transid, bytenr,
					num_bytes, nr_old_roots, nr_new_roots);

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		if (fs_info->qgroup_rescan_progress.objectid <= bytenr) {
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			ret = 0;
			goto out_free;
		}
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	spin_lock(&fs_info->qgroup_lock);
	seq = fs_info->qgroup_seq;

	/* Update old refcnts using old_roots */
	qgroup_update_refcnt(fs_info, old_roots, &qgroups, seq, UPDATE_OLD);

	/* Update new refcnts using new_roots */
	qgroup_update_refcnt(fs_info, new_roots, &qgroups, seq, UPDATE_NEW);

	qgroup_update_counters(fs_info, &qgroups, nr_old_roots, nr_new_roots,
			       num_bytes, seq);

	/*
	 * We're done using the iterator, release all its qgroups while holding
	 * fs_info->qgroup_lock so that we don't race with btrfs_remove_qgroup()
	 * and trigger use-after-free accesses to qgroups.
	 */
	qgroup_iterator_nested_clean(&qgroups);

	/*
	 * Bump qgroup_seq to avoid seq overlap
	 */
	fs_info->qgroup_seq += max(nr_old_roots, nr_new_roots) + 1;
	spin_unlock(&fs_info->qgroup_lock);
out_free:
	ulist_free(old_roots);
	ulist_free(new_roots);
	return ret;
}

int btrfs_qgroup_account_extents(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup_extent_record *record;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct ulist *new_roots = NULL;
	struct rb_node *node;
	u64 num_dirty_extents = 0;
	u64 qgroup_to_skip;
	int ret = 0;

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_SIMPLE)
		return 0;

	delayed_refs = &trans->transaction->delayed_refs;
	qgroup_to_skip = delayed_refs->qgroup_to_skip;
	while ((node = rb_first(&delayed_refs->dirty_extent_root))) {
		record = rb_entry(node, struct btrfs_qgroup_extent_record,
				  node);

		num_dirty_extents++;
		trace_btrfs_qgroup_account_extents(fs_info, record);

		if (!ret && !(fs_info->qgroup_flags &
			      BTRFS_QGROUP_RUNTIME_FLAG_NO_ACCOUNTING)) {
			struct btrfs_backref_walk_ctx ctx = { 0 };

			ctx.bytenr = record->bytenr;
			ctx.fs_info = fs_info;

			/*
			 * Old roots should be searched when inserting qgroup
			 * extent record.
			 *
			 * But for INCONSISTENT (NO_ACCOUNTING) -> rescan case,
			 * we may have some record inserted during
			 * NO_ACCOUNTING (thus no old_roots populated), but
			 * later we start rescan, which clears NO_ACCOUNTING,
			 * leaving some inserted records without old_roots
			 * populated.
			 *
			 * Those cases are rare and should not cause too much
			 * time spent during commit_transaction().
			 */
			if (!record->old_roots) {
				/* Search commit root to find old_roots */
				ret = btrfs_find_all_roots(&ctx, false);
				if (ret < 0)
					goto cleanup;
				record->old_roots = ctx.roots;
				ctx.roots = NULL;
			}

			/*
			 * Use BTRFS_SEQ_LAST as time_seq to do special search,
			 * which doesn't lock tree or delayed_refs and search
			 * current root. It's safe inside commit_transaction().
			 */
			ctx.trans = trans;
			ctx.time_seq = BTRFS_SEQ_LAST;
			ret = btrfs_find_all_roots(&ctx, false);
			if (ret < 0)
				goto cleanup;
			new_roots = ctx.roots;
			if (qgroup_to_skip) {
				ulist_del(new_roots, qgroup_to_skip, 0);
				ulist_del(record->old_roots, qgroup_to_skip,
					  0);
			}
			ret = btrfs_qgroup_account_extent(trans, record->bytenr,
							  record->num_bytes,
							  record->old_roots,
							  new_roots);
			record->old_roots = NULL;
			new_roots = NULL;
		}
		/* Free the reserved data space */
		btrfs_qgroup_free_refroot(fs_info,
				record->data_rsv_refroot,
				record->data_rsv,
				BTRFS_QGROUP_RSV_DATA);
cleanup:
		ulist_free(record->old_roots);
		ulist_free(new_roots);
		new_roots = NULL;
		rb_erase(node, &delayed_refs->dirty_extent_root);
		kfree(record);

	}
	trace_qgroup_num_dirty_extents(fs_info, trans->transid,
				       num_dirty_extents);
	return ret;
}

/*
 * Writes all changed qgroups to disk.
 * Called by the transaction commit path and the qgroup assign ioctl.
 */
int btrfs_run_qgroups(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int ret = 0;

	/*
	 * In case we are called from the qgroup assign ioctl, assert that we
	 * are holding the qgroup_ioctl_lock, otherwise we can race with a quota
	 * disable operation (ioctl) and access a freed quota root.
	 */
	if (trans->transaction->state != TRANS_STATE_COMMIT_DOING)
		lockdep_assert_held(&fs_info->qgroup_ioctl_lock);

	if (!fs_info->quota_root)
		return ret;

	spin_lock(&fs_info->qgroup_lock);
	while (!list_empty(&fs_info->dirty_qgroups)) {
		struct btrfs_qgroup *qgroup;
		qgroup = list_first_entry(&fs_info->dirty_qgroups,
					  struct btrfs_qgroup, dirty);
		list_del_init(&qgroup->dirty);
		spin_unlock(&fs_info->qgroup_lock);
		ret = update_qgroup_info_item(trans, qgroup);
		if (ret)
			qgroup_mark_inconsistent(fs_info);
		ret = update_qgroup_limit_item(trans, qgroup);
		if (ret)
			qgroup_mark_inconsistent(fs_info);
		spin_lock(&fs_info->qgroup_lock);
	}
	if (btrfs_qgroup_enabled(fs_info))
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_ON;
	else
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_ON;
	spin_unlock(&fs_info->qgroup_lock);

	ret = update_qgroup_status_item(trans);
	if (ret)
		qgroup_mark_inconsistent(fs_info);

	return ret;
}

int btrfs_qgroup_check_inherit(struct btrfs_fs_info *fs_info,
			       struct btrfs_qgroup_inherit *inherit,
			       size_t size)
{
	if (!btrfs_qgroup_enabled(fs_info))
		return 0;
	if (inherit->flags & ~BTRFS_QGROUP_INHERIT_FLAGS_SUPP)
		return -EOPNOTSUPP;
	if (size < sizeof(*inherit) || size > PAGE_SIZE)
		return -EINVAL;

	/*
	 * In the past we allowed btrfs_qgroup_inherit to specify to copy
	 * rfer/excl numbers directly from other qgroups.  This behavior has
	 * been disabled in userspace for a very long time, but here we should
	 * also disable it in kernel, as this behavior is known to mark qgroup
	 * inconsistent, and a rescan would wipe out the changes anyway.
	 *
	 * Reject any btrfs_qgroup_inherit with num_ref_copies or num_excl_copies.
	 */
	if (inherit->num_ref_copies > 0 || inherit->num_excl_copies > 0)
		return -EINVAL;

	if (size != struct_size(inherit, qgroups, inherit->num_qgroups))
		return -EINVAL;

	/*
	 * Now check all the remaining qgroups, they should all:
	 *
	 * - Exist
	 * - Be higher level qgroups.
	 */
	for (int i = 0; i < inherit->num_qgroups; i++) {
		struct btrfs_qgroup *qgroup;
		u64 qgroupid = inherit->qgroups[i];

		if (btrfs_qgroup_level(qgroupid) == 0)
			return -EINVAL;

		spin_lock(&fs_info->qgroup_lock);
		qgroup = find_qgroup_rb(fs_info, qgroupid);
		if (!qgroup) {
			spin_unlock(&fs_info->qgroup_lock);
			return -ENOENT;
		}
		spin_unlock(&fs_info->qgroup_lock);
	}
	return 0;
}

static int qgroup_auto_inherit(struct btrfs_fs_info *fs_info,
			       u64 inode_rootid,
			       struct btrfs_qgroup_inherit **inherit)
{
	int i = 0;
	u64 num_qgroups = 0;
	struct btrfs_qgroup *inode_qg;
	struct btrfs_qgroup_list *qg_list;
	struct btrfs_qgroup_inherit *res;
	size_t struct_sz;
	u64 *qgids;

	if (*inherit)
		return -EEXIST;

	inode_qg = find_qgroup_rb(fs_info, inode_rootid);
	if (!inode_qg)
		return -ENOENT;

	num_qgroups = list_count_nodes(&inode_qg->groups);

	if (!num_qgroups)
		return 0;

	struct_sz = struct_size(res, qgroups, num_qgroups);
	if (struct_sz == SIZE_MAX)
		return -ERANGE;

	res = kzalloc(struct_sz, GFP_NOFS);
	if (!res)
		return -ENOMEM;
	res->num_qgroups = num_qgroups;
	qgids = res->qgroups;

	list_for_each_entry(qg_list, &inode_qg->groups, next_group)
		qgids[i++] = qg_list->group->qgroupid;

	*inherit = res;
	return 0;
}

/*
 * Check if we can skip rescan when inheriting qgroups.  If @src has a single
 * @parent, and that @parent is owning all its bytes exclusively, we can skip
 * the full rescan, by just adding nodesize to the @parent's excl/rfer.
 *
 * Return <0 for fatal errors (like srcid/parentid has no qgroup).
 * Return 0 if a quick inherit is done.
 * Return >0 if a quick inherit is not possible, and a full rescan is needed.
 */
static int qgroup_snapshot_quick_inherit(struct btrfs_fs_info *fs_info,
					 u64 srcid, u64 parentid)
{
	struct btrfs_qgroup *src;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup_list *list;
	int nr_parents = 0;

	src = find_qgroup_rb(fs_info, srcid);
	if (!src)
		return -ENOENT;
	parent = find_qgroup_rb(fs_info, parentid);
	if (!parent)
		return -ENOENT;

	/*
	 * Source has no parent qgroup, but our new qgroup would have one.
	 * Qgroup numbers would become inconsistent.
	 */
	if (list_empty(&src->groups))
		return 1;

	list_for_each_entry(list, &src->groups, next_group) {
		/* The parent is not the same, quick update is not possible. */
		if (list->group->qgroupid != parentid)
			return 1;
		nr_parents++;
		/*
		 * More than one parent qgroup, we can't be sure about accounting
		 * consistency.
		 */
		if (nr_parents > 1)
			return 1;
	}

	/*
	 * The parent is not exclusively owning all its bytes.  We're not sure
	 * if the source has any bytes not fully owned by the parent.
	 */
	if (parent->excl != parent->rfer)
		return 1;

	parent->excl += fs_info->nodesize;
	parent->rfer += fs_info->nodesize;
	return 0;
}

/*
 * Copy the accounting information between qgroups. This is necessary
 * when a snapshot or a subvolume is created. Throwing an error will
 * cause a transaction abort so we take extra care here to only error
 * when a readonly fs is a reasonable outcome.
 */
int btrfs_qgroup_inherit(struct btrfs_trans_handle *trans, u64 srcid,
			 u64 objectid, u64 inode_rootid,
			 struct btrfs_qgroup_inherit *inherit)
{
	int ret = 0;
	int i;
	u64 *i_qgroups;
	bool committing = false;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *srcgroup;
	struct btrfs_qgroup *dstgroup;
	struct btrfs_qgroup *prealloc;
	struct btrfs_qgroup_list **qlist_prealloc = NULL;
	bool free_inherit = false;
	bool need_rescan = false;
	u32 level_size = 0;
	u64 nums;

	prealloc = kzalloc(sizeof(*prealloc), GFP_NOFS);
	if (!prealloc)
		return -ENOMEM;

	/*
	 * There are only two callers of this function.
	 *
	 * One in create_subvol() in the ioctl context, which needs to hold
	 * the qgroup_ioctl_lock.
	 *
	 * The other one in create_pending_snapshot() where no other qgroup
	 * code can modify the fs as they all need to either start a new trans
	 * or hold a trans handler, thus we don't need to hold
	 * qgroup_ioctl_lock.
	 * This would avoid long and complex lock chain and make lockdep happy.
	 */
	spin_lock(&fs_info->trans_lock);
	if (trans->transaction->state == TRANS_STATE_COMMIT_DOING)
		committing = true;
	spin_unlock(&fs_info->trans_lock);

	if (!committing)
		mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!btrfs_qgroup_enabled(fs_info))
		goto out;

	quota_root = fs_info->quota_root;
	if (!quota_root) {
		ret = -EINVAL;
		goto out;
	}

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_SIMPLE && !inherit) {
		ret = qgroup_auto_inherit(fs_info, inode_rootid, &inherit);
		if (ret)
			goto out;
		free_inherit = true;
	}

	if (inherit) {
		i_qgroups = (u64 *)(inherit + 1);
		nums = inherit->num_qgroups + 2 * inherit->num_ref_copies +
		       2 * inherit->num_excl_copies;
		for (i = 0; i < nums; ++i) {
			srcgroup = find_qgroup_rb(fs_info, *i_qgroups);

			/*
			 * Zero out invalid groups so we can ignore
			 * them later.
			 */
			if (!srcgroup ||
			    ((srcgroup->qgroupid >> 48) <= (objectid >> 48)))
				*i_qgroups = 0ULL;

			++i_qgroups;
		}
	}

	/*
	 * create a tracking group for the subvol itself
	 */
	ret = add_qgroup_item(trans, quota_root, objectid);
	if (ret)
		goto out;

	/*
	 * add qgroup to all inherited groups
	 */
	if (inherit) {
		i_qgroups = (u64 *)(inherit + 1);
		for (i = 0; i < inherit->num_qgroups; ++i, ++i_qgroups) {
			if (*i_qgroups == 0)
				continue;
			ret = add_qgroup_relation_item(trans, objectid,
						       *i_qgroups);
			if (ret && ret != -EEXIST)
				goto out;
			ret = add_qgroup_relation_item(trans, *i_qgroups,
						       objectid);
			if (ret && ret != -EEXIST)
				goto out;
		}
		ret = 0;

		qlist_prealloc = kcalloc(inherit->num_qgroups,
					 sizeof(struct btrfs_qgroup_list *),
					 GFP_NOFS);
		if (!qlist_prealloc) {
			ret = -ENOMEM;
			goto out;
		}
		for (int i = 0; i < inherit->num_qgroups; i++) {
			qlist_prealloc[i] = kzalloc(sizeof(struct btrfs_qgroup_list),
						    GFP_NOFS);
			if (!qlist_prealloc[i]) {
				ret = -ENOMEM;
				goto out;
			}
		}
	}

	spin_lock(&fs_info->qgroup_lock);

	dstgroup = add_qgroup_rb(fs_info, prealloc, objectid);
	prealloc = NULL;

	if (inherit && inherit->flags & BTRFS_QGROUP_INHERIT_SET_LIMITS) {
		dstgroup->lim_flags = inherit->lim.flags;
		dstgroup->max_rfer = inherit->lim.max_rfer;
		dstgroup->max_excl = inherit->lim.max_excl;
		dstgroup->rsv_rfer = inherit->lim.rsv_rfer;
		dstgroup->rsv_excl = inherit->lim.rsv_excl;

		qgroup_dirty(fs_info, dstgroup);
	}

	if (srcid && btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_FULL) {
		srcgroup = find_qgroup_rb(fs_info, srcid);
		if (!srcgroup)
			goto unlock;

		/*
		 * We call inherit after we clone the root in order to make sure
		 * our counts don't go crazy, so at this point the only
		 * difference between the two roots should be the root node.
		 */
		level_size = fs_info->nodesize;
		dstgroup->rfer = srcgroup->rfer;
		dstgroup->rfer_cmpr = srcgroup->rfer_cmpr;
		dstgroup->excl = level_size;
		dstgroup->excl_cmpr = level_size;
		srcgroup->excl = level_size;
		srcgroup->excl_cmpr = level_size;

		/* inherit the limit info */
		dstgroup->lim_flags = srcgroup->lim_flags;
		dstgroup->max_rfer = srcgroup->max_rfer;
		dstgroup->max_excl = srcgroup->max_excl;
		dstgroup->rsv_rfer = srcgroup->rsv_rfer;
		dstgroup->rsv_excl = srcgroup->rsv_excl;

		qgroup_dirty(fs_info, dstgroup);
		qgroup_dirty(fs_info, srcgroup);

		/*
		 * If the source qgroup has parent but the new one doesn't,
		 * we need a full rescan.
		 */
		if (!inherit && !list_empty(&srcgroup->groups))
			need_rescan = true;
	}

	if (!inherit)
		goto unlock;

	i_qgroups = (u64 *)(inherit + 1);
	for (i = 0; i < inherit->num_qgroups; ++i) {
		if (*i_qgroups) {
			ret = add_relation_rb(fs_info, qlist_prealloc[i], objectid,
					      *i_qgroups);
			qlist_prealloc[i] = NULL;
			if (ret)
				goto unlock;
		}
		if (srcid) {
			/* Check if we can do a quick inherit. */
			ret = qgroup_snapshot_quick_inherit(fs_info, srcid, *i_qgroups);
			if (ret < 0)
				goto unlock;
			if (ret > 0)
				need_rescan = true;
			ret = 0;
		}
		++i_qgroups;
	}

	for (i = 0; i <  inherit->num_ref_copies; ++i, i_qgroups += 2) {
		struct btrfs_qgroup *src;
		struct btrfs_qgroup *dst;

		if (!i_qgroups[0] || !i_qgroups[1])
			continue;

		src = find_qgroup_rb(fs_info, i_qgroups[0]);
		dst = find_qgroup_rb(fs_info, i_qgroups[1]);

		if (!src || !dst) {
			ret = -EINVAL;
			goto unlock;
		}

		dst->rfer = src->rfer - level_size;
		dst->rfer_cmpr = src->rfer_cmpr - level_size;

		/* Manually tweaking numbers certainly needs a rescan */
		need_rescan = true;
	}
	for (i = 0; i <  inherit->num_excl_copies; ++i, i_qgroups += 2) {
		struct btrfs_qgroup *src;
		struct btrfs_qgroup *dst;

		if (!i_qgroups[0] || !i_qgroups[1])
			continue;

		src = find_qgroup_rb(fs_info, i_qgroups[0]);
		dst = find_qgroup_rb(fs_info, i_qgroups[1]);

		if (!src || !dst) {
			ret = -EINVAL;
			goto unlock;
		}

		dst->excl = src->excl + level_size;
		dst->excl_cmpr = src->excl_cmpr + level_size;
		need_rescan = true;
	}

unlock:
	spin_unlock(&fs_info->qgroup_lock);
	if (!ret)
		ret = btrfs_sysfs_add_one_qgroup(fs_info, dstgroup);
out:
	if (!committing)
		mutex_unlock(&fs_info->qgroup_ioctl_lock);
	if (need_rescan)
		qgroup_mark_inconsistent(fs_info);
	if (qlist_prealloc) {
		for (int i = 0; i < inherit->num_qgroups; i++)
			kfree(qlist_prealloc[i]);
		kfree(qlist_prealloc);
	}
	if (free_inherit)
		kfree(inherit);
	kfree(prealloc);
	return ret;
}

static bool qgroup_check_limits(const struct btrfs_qgroup *qg, u64 num_bytes)
{
	if ((qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_RFER) &&
	    qgroup_rsv_total(qg) + (s64)qg->rfer + num_bytes > qg->max_rfer)
		return false;

	if ((qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_EXCL) &&
	    qgroup_rsv_total(qg) + (s64)qg->excl + num_bytes > qg->max_excl)
		return false;

	return true;
}

static int qgroup_reserve(struct btrfs_root *root, u64 num_bytes, bool enforce,
			  enum btrfs_qgroup_rsv_type type)
{
	struct btrfs_qgroup *qgroup;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 ref_root = btrfs_root_id(root);
	int ret = 0;
	LIST_HEAD(qgroup_list);

	if (!is_fstree(ref_root))
		return 0;

	if (num_bytes == 0)
		return 0;

	if (test_bit(BTRFS_FS_QUOTA_OVERRIDE, &fs_info->flags) &&
	    capable(CAP_SYS_RESOURCE))
		enforce = false;

	spin_lock(&fs_info->qgroup_lock);
	if (!fs_info->quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	qgroup_iterator_add(&qgroup_list, qgroup);
	list_for_each_entry(qgroup, &qgroup_list, iterator) {
		struct btrfs_qgroup_list *glist;

		if (enforce && !qgroup_check_limits(qgroup, num_bytes)) {
			ret = -EDQUOT;
			goto out;
		}

		list_for_each_entry(glist, &qgroup->groups, next_group)
			qgroup_iterator_add(&qgroup_list, glist->group);
	}

	ret = 0;
	/*
	 * no limits exceeded, now record the reservation into all qgroups
	 */
	list_for_each_entry(qgroup, &qgroup_list, iterator)
		qgroup_rsv_add(fs_info, qgroup, num_bytes, type);

out:
	qgroup_iterator_clean(&qgroup_list);
	spin_unlock(&fs_info->qgroup_lock);
	return ret;
}

/*
 * Free @num_bytes of reserved space with @type for qgroup.  (Normally level 0
 * qgroup).
 *
 * Will handle all higher level qgroup too.
 *
 * NOTE: If @num_bytes is (u64)-1, this means to free all bytes of this qgroup.
 * This special case is only used for META_PERTRANS type.
 */
void btrfs_qgroup_free_refroot(struct btrfs_fs_info *fs_info,
			       u64 ref_root, u64 num_bytes,
			       enum btrfs_qgroup_rsv_type type)
{
	struct btrfs_qgroup *qgroup;
	LIST_HEAD(qgroup_list);

	if (!is_fstree(ref_root))
		return;

	if (num_bytes == 0)
		return;

	if (num_bytes == (u64)-1 && type != BTRFS_QGROUP_RSV_META_PERTRANS) {
		WARN(1, "%s: Invalid type to free", __func__);
		return;
	}
	spin_lock(&fs_info->qgroup_lock);

	if (!fs_info->quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	if (num_bytes == (u64)-1)
		/*
		 * We're freeing all pertrans rsv, get reserved value from
		 * level 0 qgroup as real num_bytes to free.
		 */
		num_bytes = qgroup->rsv.values[type];

	qgroup_iterator_add(&qgroup_list, qgroup);
	list_for_each_entry(qgroup, &qgroup_list, iterator) {
		struct btrfs_qgroup_list *glist;

		qgroup_rsv_release(fs_info, qgroup, num_bytes, type);
		list_for_each_entry(glist, &qgroup->groups, next_group) {
			qgroup_iterator_add(&qgroup_list, glist->group);
		}
	}
out:
	qgroup_iterator_clean(&qgroup_list);
	spin_unlock(&fs_info->qgroup_lock);
}

/*
 * Check if the leaf is the last leaf. Which means all node pointers
 * are at their last position.
 */
static bool is_last_leaf(struct btrfs_path *path)
{
	int i;

	for (i = 1; i < BTRFS_MAX_LEVEL && path->nodes[i]; i++) {
		if (path->slots[i] != btrfs_header_nritems(path->nodes[i]) - 1)
			return false;
	}
	return true;
}

/*
 * returns < 0 on error, 0 when more leafs are to be scanned.
 * returns 1 when done.
 */
static int qgroup_rescan_leaf(struct btrfs_trans_handle *trans,
			      struct btrfs_path *path)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *extent_root;
	struct btrfs_key found;
	struct extent_buffer *scratch_leaf = NULL;
	u64 num_bytes;
	bool done;
	int slot;
	int ret;

	if (!btrfs_qgroup_full_accounting(fs_info))
		return 1;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	extent_root = btrfs_extent_root(fs_info,
				fs_info->qgroup_rescan_progress.objectid);
	ret = btrfs_search_slot_for_read(extent_root,
					 &fs_info->qgroup_rescan_progress,
					 path, 1, 0);

	btrfs_debug(fs_info,
		"current progress key (%llu %u %llu), search_slot ret %d",
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
	done = is_last_leaf(path);

	btrfs_item_key_to_cpu(path->nodes[0], &found,
			      btrfs_header_nritems(path->nodes[0]) - 1);
	fs_info->qgroup_rescan_progress.objectid = found.objectid + 1;

	scratch_leaf = btrfs_clone_extent_buffer(path->nodes[0]);
	if (!scratch_leaf) {
		ret = -ENOMEM;
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		goto out;
	}
	slot = path->slots[0];
	btrfs_release_path(path);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	for (; slot < btrfs_header_nritems(scratch_leaf); ++slot) {
		struct btrfs_backref_walk_ctx ctx = { 0 };

		btrfs_item_key_to_cpu(scratch_leaf, &found, slot);
		if (found.type != BTRFS_EXTENT_ITEM_KEY &&
		    found.type != BTRFS_METADATA_ITEM_KEY)
			continue;
		if (found.type == BTRFS_METADATA_ITEM_KEY)
			num_bytes = fs_info->nodesize;
		else
			num_bytes = found.offset;

		ctx.bytenr = found.objectid;
		ctx.fs_info = fs_info;

		ret = btrfs_find_all_roots(&ctx, false);
		if (ret < 0)
			goto out;
		/* For rescan, just pass old_roots as NULL */
		ret = btrfs_qgroup_account_extent(trans, found.objectid,
						  num_bytes, NULL, ctx.roots);
		if (ret < 0)
			goto out;
	}
out:
	if (scratch_leaf)
		free_extent_buffer(scratch_leaf);

	if (done && !ret) {
		ret = 1;
		fs_info->qgroup_rescan_progress.objectid = (u64)-1;
	}
	return ret;
}

static bool rescan_should_stop(struct btrfs_fs_info *fs_info)
{
	if (btrfs_fs_closing(fs_info))
		return true;
	if (test_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state))
		return true;
	if (!btrfs_qgroup_enabled(fs_info))
		return true;
	if (fs_info->qgroup_flags & BTRFS_QGROUP_RUNTIME_FLAG_CANCEL_RESCAN)
		return true;
	return false;
}

static void btrfs_qgroup_rescan_worker(struct btrfs_work *work)
{
	struct btrfs_fs_info *fs_info = container_of(work, struct btrfs_fs_info,
						     qgroup_rescan_work);
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans = NULL;
	int ret = 0;
	bool stopped = false;
	bool did_leaf_rescans = false;

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_SIMPLE)
		return;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
	/*
	 * Rescan should only search for commit root, and any later difference
	 * should be recorded by qgroup
	 */
	path->search_commit_root = 1;
	path->skip_locking = 1;

	while (!ret && !(stopped = rescan_should_stop(fs_info))) {
		trans = btrfs_start_transaction(fs_info->fs_root, 0);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}

		ret = qgroup_rescan_leaf(trans, path);
		did_leaf_rescans = true;

		if (ret > 0)
			btrfs_commit_transaction(trans);
		else
			btrfs_end_transaction(trans);
	}

out:
	btrfs_free_path(path);

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (ret > 0 &&
	    fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	} else if (ret < 0 || stopped) {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	/*
	 * Only update status, since the previous part has already updated the
	 * qgroup info, and only if we did any actual work. This also prevents
	 * race with a concurrent quota disable, which has already set
	 * fs_info->quota_root to NULL and cleared BTRFS_FS_QUOTA_ENABLED at
	 * btrfs_quota_disable().
	 */
	if (did_leaf_rescans) {
		trans = btrfs_start_transaction(fs_info->quota_root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			btrfs_err(fs_info,
				  "fail to start transaction for status update: %d",
				  ret);
		}
	} else {
		trans = NULL;
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (!stopped ||
	    fs_info->qgroup_flags & BTRFS_QGROUP_RUNTIME_FLAG_CANCEL_RESCAN)
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	if (trans) {
		int ret2 = update_qgroup_status_item(trans);

		if (ret2 < 0) {
			ret = ret2;
			btrfs_err(fs_info, "fail to update qgroup status: %d", ret);
		}
	}
	fs_info->qgroup_rescan_running = false;
	fs_info->qgroup_flags &= ~BTRFS_QGROUP_RUNTIME_FLAG_CANCEL_RESCAN;
	complete_all(&fs_info->qgroup_rescan_completion);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	if (!trans)
		return;

	btrfs_end_transaction(trans);

	if (stopped) {
		btrfs_info(fs_info, "qgroup scan paused");
	} else if (fs_info->qgroup_flags & BTRFS_QGROUP_RUNTIME_FLAG_CANCEL_RESCAN) {
		btrfs_info(fs_info, "qgroup scan cancelled");
	} else if (ret >= 0) {
		btrfs_info(fs_info, "qgroup scan completed%s",
			ret > 0 ? " (inconsistency flag cleared)" : "");
	} else {
		btrfs_err(fs_info, "qgroup scan failed with %d", ret);
	}
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

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_SIMPLE) {
		btrfs_warn(fs_info, "qgroup rescan init failed, running in simple mode");
		return -EINVAL;
	}

	if (!init_flags) {
		/* we're resuming qgroup rescan at mount time */
		if (!(fs_info->qgroup_flags &
		      BTRFS_QGROUP_STATUS_FLAG_RESCAN)) {
			btrfs_warn(fs_info,
			"qgroup rescan init failed, qgroup rescan is not queued");
			ret = -EINVAL;
		} else if (!(fs_info->qgroup_flags &
			     BTRFS_QGROUP_STATUS_FLAG_ON)) {
			btrfs_warn(fs_info,
			"qgroup rescan init failed, qgroup is not enabled");
			ret = -EINVAL;
		}

		if (ret)
			return ret;
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);

	if (init_flags) {
		if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
			btrfs_warn(fs_info,
				   "qgroup rescan is already in progress");
			ret = -EINPROGRESS;
		} else if (!(fs_info->qgroup_flags &
			     BTRFS_QGROUP_STATUS_FLAG_ON)) {
			btrfs_warn(fs_info,
			"qgroup rescan init failed, qgroup is not enabled");
			ret = -EINVAL;
		} else if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_DISABLED) {
			/* Quota disable is in progress */
			ret = -EBUSY;
		}

		if (ret) {
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			return ret;
		}
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	}

	memset(&fs_info->qgroup_rescan_progress, 0,
		sizeof(fs_info->qgroup_rescan_progress));
	fs_info->qgroup_flags &= ~(BTRFS_QGROUP_RUNTIME_FLAG_CANCEL_RESCAN |
				   BTRFS_QGROUP_RUNTIME_FLAG_NO_ACCOUNTING);
	fs_info->qgroup_rescan_progress.objectid = progress_objectid;
	init_completion(&fs_info->qgroup_rescan_completion);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	btrfs_init_work(&fs_info->qgroup_rescan_work,
			btrfs_qgroup_rescan_worker, NULL);
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
		qgroup_dirty(fs_info, qgroup);
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

	trans = btrfs_attach_transaction_barrier(fs_info->fs_root);
	if (IS_ERR(trans) && trans != ERR_PTR(-ENOENT)) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		return PTR_ERR(trans);
	} else if (trans != ERR_PTR(-ENOENT)) {
		ret = btrfs_commit_transaction(trans);
		if (ret) {
			fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
			return ret;
		}
	}

	qgroup_rescan_zero_tracking(fs_info);

	mutex_lock(&fs_info->qgroup_rescan_lock);
	fs_info->qgroup_rescan_running = true;
	btrfs_queue_work(fs_info->qgroup_rescan_workers,
			 &fs_info->qgroup_rescan_work);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	return 0;
}

int btrfs_qgroup_wait_for_completion(struct btrfs_fs_info *fs_info,
				     bool interruptible)
{
	int running;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	running = fs_info->qgroup_rescan_running;
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	if (!running)
		return 0;

	if (interruptible)
		ret = wait_for_completion_interruptible(
					&fs_info->qgroup_rescan_completion);
	else
		wait_for_completion(&fs_info->qgroup_rescan_completion);

	return ret;
}

/*
 * this is only called from open_ctree where we're still single threaded, thus
 * locking is omitted here.
 */
void
btrfs_qgroup_rescan_resume(struct btrfs_fs_info *fs_info)
{
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		mutex_lock(&fs_info->qgroup_rescan_lock);
		fs_info->qgroup_rescan_running = true;
		btrfs_queue_work(fs_info->qgroup_rescan_workers,
				 &fs_info->qgroup_rescan_work);
		mutex_unlock(&fs_info->qgroup_rescan_lock);
	}
}

#define rbtree_iterate_from_safe(node, next, start)				\
       for (node = start; node && ({ next = rb_next(node); 1;}); node = next)

static int qgroup_unreserve_range(struct btrfs_inode *inode,
				  struct extent_changeset *reserved, u64 start,
				  u64 len)
{
	struct rb_node *node;
	struct rb_node *next;
	struct ulist_node *entry;
	int ret = 0;

	node = reserved->range_changed.root.rb_node;
	if (!node)
		return 0;
	while (node) {
		entry = rb_entry(node, struct ulist_node, rb_node);
		if (entry->val < start)
			node = node->rb_right;
		else
			node = node->rb_left;
	}

	if (entry->val > start && rb_prev(&entry->rb_node))
		entry = rb_entry(rb_prev(&entry->rb_node), struct ulist_node,
				 rb_node);

	rbtree_iterate_from_safe(node, next, &entry->rb_node) {
		u64 entry_start;
		u64 entry_end;
		u64 entry_len;
		int clear_ret;

		entry = rb_entry(node, struct ulist_node, rb_node);
		entry_start = entry->val;
		entry_end = entry->aux;
		entry_len = entry_end - entry_start + 1;

		if (entry_start >= start + len)
			break;
		if (entry_start + entry_len <= start)
			continue;
		/*
		 * Now the entry is in [start, start + len), revert the
		 * EXTENT_QGROUP_RESERVED bit.
		 */
		clear_ret = clear_extent_bits(&inode->io_tree, entry_start,
					      entry_end, EXTENT_QGROUP_RESERVED);
		if (!ret && clear_ret < 0)
			ret = clear_ret;

		ulist_del(&reserved->range_changed, entry->val, entry->aux);
		if (likely(reserved->bytes_changed >= entry_len)) {
			reserved->bytes_changed -= entry_len;
		} else {
			WARN_ON(1);
			reserved->bytes_changed = 0;
		}
	}

	return ret;
}

/*
 * Try to free some space for qgroup.
 *
 * For qgroup, there are only 3 ways to free qgroup space:
 * - Flush nodatacow write
 *   Any nodatacow write will free its reserved data space at run_delalloc_range().
 *   In theory, we should only flush nodatacow inodes, but it's not yet
 *   possible, so we need to flush the whole root.
 *
 * - Wait for ordered extents
 *   When ordered extents are finished, their reserved metadata is finally
 *   converted to per_trans status, which can be freed by later commit
 *   transaction.
 *
 * - Commit transaction
 *   This would free the meta_per_trans space.
 *   In theory this shouldn't provide much space, but any more qgroup space
 *   is needed.
 */
static int try_flush_qgroup(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;
	int ret;

	/* Can't hold an open transaction or we run the risk of deadlocking. */
	ASSERT(current->journal_info == NULL);
	if (WARN_ON(current->journal_info))
		return 0;

	/*
	 * We don't want to run flush again and again, so if there is a running
	 * one, we won't try to start a new flush, but exit directly.
	 */
	if (test_and_set_bit(BTRFS_ROOT_QGROUP_FLUSHING, &root->state)) {
		wait_event(root->qgroup_flush_wait,
			!test_bit(BTRFS_ROOT_QGROUP_FLUSHING, &root->state));
		return 0;
	}

	ret = btrfs_start_delalloc_snapshot(root, true);
	if (ret < 0)
		goto out;
	btrfs_wait_ordered_extents(root, U64_MAX, 0, (u64)-1);

	trans = btrfs_attach_transaction_barrier(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		if (ret == -ENOENT)
			ret = 0;
		goto out;
	}

	ret = btrfs_commit_transaction(trans);
out:
	clear_bit(BTRFS_ROOT_QGROUP_FLUSHING, &root->state);
	wake_up(&root->qgroup_flush_wait);
	return ret;
}

static int qgroup_reserve_data(struct btrfs_inode *inode,
			struct extent_changeset **reserved_ret, u64 start,
			u64 len)
{
	struct btrfs_root *root = inode->root;
	struct extent_changeset *reserved;
	bool new_reserved = false;
	u64 orig_reserved;
	u64 to_reserve;
	int ret;

	if (btrfs_qgroup_mode(root->fs_info) == BTRFS_QGROUP_MODE_DISABLED ||
	    !is_fstree(btrfs_root_id(root)) || len == 0)
		return 0;

	/* @reserved parameter is mandatory for qgroup */
	if (WARN_ON(!reserved_ret))
		return -EINVAL;
	if (!*reserved_ret) {
		new_reserved = true;
		*reserved_ret = extent_changeset_alloc();
		if (!*reserved_ret)
			return -ENOMEM;
	}
	reserved = *reserved_ret;
	/* Record already reserved space */
	orig_reserved = reserved->bytes_changed;
	ret = set_record_extent_bits(&inode->io_tree, start,
			start + len -1, EXTENT_QGROUP_RESERVED, reserved);

	/* Newly reserved space */
	to_reserve = reserved->bytes_changed - orig_reserved;
	trace_btrfs_qgroup_reserve_data(&inode->vfs_inode, start, len,
					to_reserve, QGROUP_RESERVE);
	if (ret < 0)
		goto out;
	ret = qgroup_reserve(root, to_reserve, true, BTRFS_QGROUP_RSV_DATA);
	if (ret < 0)
		goto cleanup;

	return ret;

cleanup:
	qgroup_unreserve_range(inode, reserved, start, len);
out:
	if (new_reserved) {
		extent_changeset_free(reserved);
		*reserved_ret = NULL;
	}
	return ret;
}

/*
 * Reserve qgroup space for range [start, start + len).
 *
 * This function will either reserve space from related qgroups or do nothing
 * if the range is already reserved.
 *
 * Return 0 for successful reservation
 * Return <0 for error (including -EQUOT)
 *
 * NOTE: This function may sleep for memory allocation, dirty page flushing and
 *	 commit transaction. So caller should not hold any dirty page locked.
 */
int btrfs_qgroup_reserve_data(struct btrfs_inode *inode,
			struct extent_changeset **reserved_ret, u64 start,
			u64 len)
{
	int ret;

	ret = qgroup_reserve_data(inode, reserved_ret, start, len);
	if (ret <= 0 && ret != -EDQUOT)
		return ret;

	ret = try_flush_qgroup(inode->root);
	if (ret < 0)
		return ret;
	return qgroup_reserve_data(inode, reserved_ret, start, len);
}

/* Free ranges specified by @reserved, normally in error path */
static int qgroup_free_reserved_data(struct btrfs_inode *inode,
				     struct extent_changeset *reserved,
				     u64 start, u64 len, u64 *freed_ret)
{
	struct btrfs_root *root = inode->root;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct extent_changeset changeset;
	u64 freed = 0;
	int ret;

	extent_changeset_init(&changeset);
	len = round_up(start + len, root->fs_info->sectorsize);
	start = round_down(start, root->fs_info->sectorsize);

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(&reserved->range_changed, &uiter))) {
		u64 range_start = unode->val;
		/* unode->aux is the inclusive end */
		u64 range_len = unode->aux - range_start + 1;
		u64 free_start;
		u64 free_len;

		extent_changeset_release(&changeset);

		/* Only free range in range [start, start + len) */
		if (range_start >= start + len ||
		    range_start + range_len <= start)
			continue;
		free_start = max(range_start, start);
		free_len = min(start + len, range_start + range_len) -
			   free_start;
		/*
		 * TODO: To also modify reserved->ranges_reserved to reflect
		 * the modification.
		 *
		 * However as long as we free qgroup reserved according to
		 * EXTENT_QGROUP_RESERVED, we won't double free.
		 * So not need to rush.
		 */
		ret = clear_record_extent_bits(&inode->io_tree, free_start,
				free_start + free_len - 1,
				EXTENT_QGROUP_RESERVED, &changeset);
		if (ret < 0)
			goto out;
		freed += changeset.bytes_changed;
	}
	btrfs_qgroup_free_refroot(root->fs_info, btrfs_root_id(root), freed,
				  BTRFS_QGROUP_RSV_DATA);
	if (freed_ret)
		*freed_ret = freed;
	ret = 0;
out:
	extent_changeset_release(&changeset);
	return ret;
}

static int __btrfs_qgroup_release_data(struct btrfs_inode *inode,
			struct extent_changeset *reserved, u64 start, u64 len,
			u64 *released, int free)
{
	struct extent_changeset changeset;
	int trace_op = QGROUP_RELEASE;
	int ret;

	if (btrfs_qgroup_mode(inode->root->fs_info) == BTRFS_QGROUP_MODE_DISABLED) {
		extent_changeset_init(&changeset);
		return clear_record_extent_bits(&inode->io_tree, start,
						start + len - 1,
						EXTENT_QGROUP_RESERVED, &changeset);
	}

	/* In release case, we shouldn't have @reserved */
	WARN_ON(!free && reserved);
	if (free && reserved)
		return qgroup_free_reserved_data(inode, reserved, start, len, released);
	extent_changeset_init(&changeset);
	ret = clear_record_extent_bits(&inode->io_tree, start, start + len -1,
				       EXTENT_QGROUP_RESERVED, &changeset);
	if (ret < 0)
		goto out;

	if (free)
		trace_op = QGROUP_FREE;
	trace_btrfs_qgroup_release_data(&inode->vfs_inode, start, len,
					changeset.bytes_changed, trace_op);
	if (free)
		btrfs_qgroup_free_refroot(inode->root->fs_info,
				btrfs_root_id(inode->root),
				changeset.bytes_changed, BTRFS_QGROUP_RSV_DATA);
	if (released)
		*released = changeset.bytes_changed;
out:
	extent_changeset_release(&changeset);
	return ret;
}

/*
 * Free a reserved space range from io_tree and related qgroups
 *
 * Should be called when a range of pages get invalidated before reaching disk.
 * Or for error cleanup case.
 * if @reserved is given, only reserved range in [@start, @start + @len) will
 * be freed.
 *
 * For data written to disk, use btrfs_qgroup_release_data().
 *
 * NOTE: This function may sleep for memory allocation.
 */
int btrfs_qgroup_free_data(struct btrfs_inode *inode,
			   struct extent_changeset *reserved,
			   u64 start, u64 len, u64 *freed)
{
	return __btrfs_qgroup_release_data(inode, reserved, start, len, freed, 1);
}

/*
 * Release a reserved space range from io_tree only.
 *
 * Should be called when a range of pages get written to disk and corresponding
 * FILE_EXTENT is inserted into corresponding root.
 *
 * Since new qgroup accounting framework will only update qgroup numbers at
 * commit_transaction() time, its reserved space shouldn't be freed from
 * related qgroups.
 *
 * But we should release the range from io_tree, to allow further write to be
 * COWed.
 *
 * NOTE: This function may sleep for memory allocation.
 */
int btrfs_qgroup_release_data(struct btrfs_inode *inode, u64 start, u64 len, u64 *released)
{
	return __btrfs_qgroup_release_data(inode, NULL, start, len, released, 0);
}

static void add_root_meta_rsv(struct btrfs_root *root, int num_bytes,
			      enum btrfs_qgroup_rsv_type type)
{
	if (type != BTRFS_QGROUP_RSV_META_PREALLOC &&
	    type != BTRFS_QGROUP_RSV_META_PERTRANS)
		return;
	if (num_bytes == 0)
		return;

	spin_lock(&root->qgroup_meta_rsv_lock);
	if (type == BTRFS_QGROUP_RSV_META_PREALLOC)
		root->qgroup_meta_rsv_prealloc += num_bytes;
	else
		root->qgroup_meta_rsv_pertrans += num_bytes;
	spin_unlock(&root->qgroup_meta_rsv_lock);
}

static int sub_root_meta_rsv(struct btrfs_root *root, int num_bytes,
			     enum btrfs_qgroup_rsv_type type)
{
	if (type != BTRFS_QGROUP_RSV_META_PREALLOC &&
	    type != BTRFS_QGROUP_RSV_META_PERTRANS)
		return 0;
	if (num_bytes == 0)
		return 0;

	spin_lock(&root->qgroup_meta_rsv_lock);
	if (type == BTRFS_QGROUP_RSV_META_PREALLOC) {
		num_bytes = min_t(u64, root->qgroup_meta_rsv_prealloc,
				  num_bytes);
		root->qgroup_meta_rsv_prealloc -= num_bytes;
	} else {
		num_bytes = min_t(u64, root->qgroup_meta_rsv_pertrans,
				  num_bytes);
		root->qgroup_meta_rsv_pertrans -= num_bytes;
	}
	spin_unlock(&root->qgroup_meta_rsv_lock);
	return num_bytes;
}

int btrfs_qgroup_reserve_meta(struct btrfs_root *root, int num_bytes,
			      enum btrfs_qgroup_rsv_type type, bool enforce)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_DISABLED ||
	    !is_fstree(btrfs_root_id(root)) || num_bytes == 0)
		return 0;

	BUG_ON(num_bytes != round_down(num_bytes, fs_info->nodesize));
	trace_qgroup_meta_reserve(root, (s64)num_bytes, type);
	ret = qgroup_reserve(root, num_bytes, enforce, type);
	if (ret < 0)
		return ret;
	/*
	 * Record what we have reserved into root.
	 *
	 * To avoid quota disabled->enabled underflow.
	 * In that case, we may try to free space we haven't reserved
	 * (since quota was disabled), so record what we reserved into root.
	 * And ensure later release won't underflow this number.
	 */
	add_root_meta_rsv(root, num_bytes, type);
	return ret;
}

int __btrfs_qgroup_reserve_meta(struct btrfs_root *root, int num_bytes,
				enum btrfs_qgroup_rsv_type type, bool enforce,
				bool noflush)
{
	int ret;

	ret = btrfs_qgroup_reserve_meta(root, num_bytes, type, enforce);
	if ((ret <= 0 && ret != -EDQUOT) || noflush)
		return ret;

	ret = try_flush_qgroup(root);
	if (ret < 0)
		return ret;
	return btrfs_qgroup_reserve_meta(root, num_bytes, type, enforce);
}

/*
 * Per-transaction meta reservation should be all freed at transaction commit
 * time
 */
void btrfs_qgroup_free_meta_all_pertrans(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_DISABLED ||
	    !is_fstree(btrfs_root_id(root)))
		return;

	/* TODO: Update trace point to handle such free */
	trace_qgroup_meta_free_all_pertrans(root);
	/* Special value -1 means to free all reserved space */
	btrfs_qgroup_free_refroot(fs_info, btrfs_root_id(root), (u64)-1,
				  BTRFS_QGROUP_RSV_META_PERTRANS);
}

void __btrfs_qgroup_free_meta(struct btrfs_root *root, int num_bytes,
			      enum btrfs_qgroup_rsv_type type)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_DISABLED ||
	    !is_fstree(btrfs_root_id(root)))
		return;

	/*
	 * reservation for META_PREALLOC can happen before quota is enabled,
	 * which can lead to underflow.
	 * Here ensure we will only free what we really have reserved.
	 */
	num_bytes = sub_root_meta_rsv(root, num_bytes, type);
	BUG_ON(num_bytes != round_down(num_bytes, fs_info->nodesize));
	trace_qgroup_meta_reserve(root, -(s64)num_bytes, type);
	btrfs_qgroup_free_refroot(fs_info, btrfs_root_id(root), num_bytes, type);
}

static void qgroup_convert_meta(struct btrfs_fs_info *fs_info, u64 ref_root,
				int num_bytes)
{
	struct btrfs_qgroup *qgroup;
	LIST_HEAD(qgroup_list);

	if (num_bytes == 0)
		return;
	if (!fs_info->quota_root)
		return;

	spin_lock(&fs_info->qgroup_lock);
	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	qgroup_iterator_add(&qgroup_list, qgroup);
	list_for_each_entry(qgroup, &qgroup_list, iterator) {
		struct btrfs_qgroup_list *glist;

		qgroup_rsv_release(fs_info, qgroup, num_bytes,
				BTRFS_QGROUP_RSV_META_PREALLOC);
		if (!sb_rdonly(fs_info->sb))
			qgroup_rsv_add(fs_info, qgroup, num_bytes,
				       BTRFS_QGROUP_RSV_META_PERTRANS);

		list_for_each_entry(glist, &qgroup->groups, next_group)
			qgroup_iterator_add(&qgroup_list, glist->group);
	}
out:
	qgroup_iterator_clean(&qgroup_list);
	spin_unlock(&fs_info->qgroup_lock);
}

/*
 * Convert @num_bytes of META_PREALLOCATED reservation to META_PERTRANS.
 *
 * This is called when preallocated meta reservation needs to be used.
 * Normally after btrfs_join_transaction() call.
 */
void btrfs_qgroup_convert_reserved_meta(struct btrfs_root *root, int num_bytes)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (btrfs_qgroup_mode(fs_info) == BTRFS_QGROUP_MODE_DISABLED ||
	    !is_fstree(btrfs_root_id(root)))
		return;
	/* Same as btrfs_qgroup_free_meta_prealloc() */
	num_bytes = sub_root_meta_rsv(root, num_bytes,
				      BTRFS_QGROUP_RSV_META_PREALLOC);
	trace_qgroup_meta_convert(root, num_bytes);
	qgroup_convert_meta(fs_info, btrfs_root_id(root), num_bytes);
	if (!sb_rdonly(fs_info->sb))
		add_root_meta_rsv(root, num_bytes, BTRFS_QGROUP_RSV_META_PERTRANS);
}

/*
 * Check qgroup reserved space leaking, normally at destroy inode
 * time
 */
void btrfs_qgroup_check_reserved_leak(struct btrfs_inode *inode)
{
	struct extent_changeset changeset;
	struct ulist_node *unode;
	struct ulist_iterator iter;
	int ret;

	extent_changeset_init(&changeset);
	ret = clear_record_extent_bits(&inode->io_tree, 0, (u64)-1,
			EXTENT_QGROUP_RESERVED, &changeset);

	WARN_ON(ret < 0);
	if (WARN_ON(changeset.bytes_changed)) {
		ULIST_ITER_INIT(&iter);
		while ((unode = ulist_next(&changeset.range_changed, &iter))) {
			btrfs_warn(inode->root->fs_info,
		"leaking qgroup reserved space, ino: %llu, start: %llu, end: %llu",
				btrfs_ino(inode), unode->val, unode->aux);
		}
		btrfs_qgroup_free_refroot(inode->root->fs_info,
				btrfs_root_id(inode->root),
				changeset.bytes_changed, BTRFS_QGROUP_RSV_DATA);

	}
	extent_changeset_release(&changeset);
}

void btrfs_qgroup_init_swapped_blocks(
	struct btrfs_qgroup_swapped_blocks *swapped_blocks)
{
	int i;

	spin_lock_init(&swapped_blocks->lock);
	for (i = 0; i < BTRFS_MAX_LEVEL; i++)
		swapped_blocks->blocks[i] = RB_ROOT;
	swapped_blocks->swapped = false;
}

/*
 * Delete all swapped blocks record of @root.
 * Every record here means we skipped a full subtree scan for qgroup.
 *
 * Gets called when committing one transaction.
 */
void btrfs_qgroup_clean_swapped_blocks(struct btrfs_root *root)
{
	struct btrfs_qgroup_swapped_blocks *swapped_blocks;
	int i;

	swapped_blocks = &root->swapped_blocks;

	spin_lock(&swapped_blocks->lock);
	if (!swapped_blocks->swapped)
		goto out;
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		struct rb_root *cur_root = &swapped_blocks->blocks[i];
		struct btrfs_qgroup_swapped_block *entry;
		struct btrfs_qgroup_swapped_block *next;

		rbtree_postorder_for_each_entry_safe(entry, next, cur_root,
						     node)
			kfree(entry);
		swapped_blocks->blocks[i] = RB_ROOT;
	}
	swapped_blocks->swapped = false;
out:
	spin_unlock(&swapped_blocks->lock);
}

/*
 * Add subtree roots record into @subvol_root.
 *
 * @subvol_root:	tree root of the subvolume tree get swapped
 * @bg:			block group under balance
 * @subvol_parent/slot:	pointer to the subtree root in subvolume tree
 * @reloc_parent/slot:	pointer to the subtree root in reloc tree
 *			BOTH POINTERS ARE BEFORE TREE SWAP
 * @last_snapshot:	last snapshot generation of the subvolume tree
 */
int btrfs_qgroup_add_swapped_blocks(struct btrfs_trans_handle *trans,
		struct btrfs_root *subvol_root,
		struct btrfs_block_group *bg,
		struct extent_buffer *subvol_parent, int subvol_slot,
		struct extent_buffer *reloc_parent, int reloc_slot,
		u64 last_snapshot)
{
	struct btrfs_fs_info *fs_info = subvol_root->fs_info;
	struct btrfs_qgroup_swapped_blocks *blocks = &subvol_root->swapped_blocks;
	struct btrfs_qgroup_swapped_block *block;
	struct rb_node **cur;
	struct rb_node *parent = NULL;
	int level = btrfs_header_level(subvol_parent) - 1;
	int ret = 0;

	if (!btrfs_qgroup_full_accounting(fs_info))
		return 0;

	if (btrfs_node_ptr_generation(subvol_parent, subvol_slot) >
	    btrfs_node_ptr_generation(reloc_parent, reloc_slot)) {
		btrfs_err_rl(fs_info,
		"%s: bad parameter order, subvol_gen=%llu reloc_gen=%llu",
			__func__,
			btrfs_node_ptr_generation(subvol_parent, subvol_slot),
			btrfs_node_ptr_generation(reloc_parent, reloc_slot));
		return -EUCLEAN;
	}

	block = kmalloc(sizeof(*block), GFP_NOFS);
	if (!block) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * @reloc_parent/slot is still before swap, while @block is going to
	 * record the bytenr after swap, so we do the swap here.
	 */
	block->subvol_bytenr = btrfs_node_blockptr(reloc_parent, reloc_slot);
	block->subvol_generation = btrfs_node_ptr_generation(reloc_parent,
							     reloc_slot);
	block->reloc_bytenr = btrfs_node_blockptr(subvol_parent, subvol_slot);
	block->reloc_generation = btrfs_node_ptr_generation(subvol_parent,
							    subvol_slot);
	block->last_snapshot = last_snapshot;
	block->level = level;

	/*
	 * If we have bg == NULL, we're called from btrfs_recover_relocation(),
	 * no one else can modify tree blocks thus we qgroup will not change
	 * no matter the value of trace_leaf.
	 */
	if (bg && bg->flags & BTRFS_BLOCK_GROUP_DATA)
		block->trace_leaf = true;
	else
		block->trace_leaf = false;
	btrfs_node_key_to_cpu(reloc_parent, &block->first_key, reloc_slot);

	/* Insert @block into @blocks */
	spin_lock(&blocks->lock);
	cur = &blocks->blocks[level].rb_node;
	while (*cur) {
		struct btrfs_qgroup_swapped_block *entry;

		parent = *cur;
		entry = rb_entry(parent, struct btrfs_qgroup_swapped_block,
				 node);

		if (entry->subvol_bytenr < block->subvol_bytenr) {
			cur = &(*cur)->rb_left;
		} else if (entry->subvol_bytenr > block->subvol_bytenr) {
			cur = &(*cur)->rb_right;
		} else {
			if (entry->subvol_generation !=
					block->subvol_generation ||
			    entry->reloc_bytenr != block->reloc_bytenr ||
			    entry->reloc_generation !=
					block->reloc_generation) {
				/*
				 * Duplicated but mismatch entry found.
				 * Shouldn't happen.
				 *
				 * Marking qgroup inconsistent should be enough
				 * for end users.
				 */
				WARN_ON(IS_ENABLED(CONFIG_BTRFS_DEBUG));
				ret = -EEXIST;
			}
			kfree(block);
			goto out_unlock;
		}
	}
	rb_link_node(&block->node, parent, cur);
	rb_insert_color(&block->node, &blocks->blocks[level]);
	blocks->swapped = true;
out_unlock:
	spin_unlock(&blocks->lock);
out:
	if (ret < 0)
		qgroup_mark_inconsistent(fs_info);
	return ret;
}

/*
 * Check if the tree block is a subtree root, and if so do the needed
 * delayed subtree trace for qgroup.
 *
 * This is called during btrfs_cow_block().
 */
int btrfs_qgroup_trace_subtree_after_cow(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root,
					 struct extent_buffer *subvol_eb)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_tree_parent_check check = { 0 };
	struct btrfs_qgroup_swapped_blocks *blocks = &root->swapped_blocks;
	struct btrfs_qgroup_swapped_block *block;
	struct extent_buffer *reloc_eb = NULL;
	struct rb_node *node;
	bool found = false;
	bool swapped = false;
	int level = btrfs_header_level(subvol_eb);
	int ret = 0;
	int i;

	if (!btrfs_qgroup_full_accounting(fs_info))
		return 0;
	if (!is_fstree(btrfs_root_id(root)) || !root->reloc_root)
		return 0;

	spin_lock(&blocks->lock);
	if (!blocks->swapped) {
		spin_unlock(&blocks->lock);
		return 0;
	}
	node = blocks->blocks[level].rb_node;

	while (node) {
		block = rb_entry(node, struct btrfs_qgroup_swapped_block, node);
		if (block->subvol_bytenr < subvol_eb->start) {
			node = node->rb_left;
		} else if (block->subvol_bytenr > subvol_eb->start) {
			node = node->rb_right;
		} else {
			found = true;
			break;
		}
	}
	if (!found) {
		spin_unlock(&blocks->lock);
		goto out;
	}
	/* Found one, remove it from @blocks first and update blocks->swapped */
	rb_erase(&block->node, &blocks->blocks[level]);
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (RB_EMPTY_ROOT(&blocks->blocks[i])) {
			swapped = true;
			break;
		}
	}
	blocks->swapped = swapped;
	spin_unlock(&blocks->lock);

	check.level = block->level;
	check.transid = block->reloc_generation;
	check.has_first_key = true;
	memcpy(&check.first_key, &block->first_key, sizeof(check.first_key));

	/* Read out reloc subtree root */
	reloc_eb = read_tree_block(fs_info, block->reloc_bytenr, &check);
	if (IS_ERR(reloc_eb)) {
		ret = PTR_ERR(reloc_eb);
		reloc_eb = NULL;
		goto free_out;
	}
	if (!extent_buffer_uptodate(reloc_eb)) {
		ret = -EIO;
		goto free_out;
	}

	ret = qgroup_trace_subtree_swap(trans, reloc_eb, subvol_eb,
			block->last_snapshot, block->trace_leaf);
free_out:
	kfree(block);
	free_extent_buffer(reloc_eb);
out:
	if (ret < 0) {
		btrfs_err_rl(fs_info,
			     "failed to account subtree at bytenr %llu: %d",
			     subvol_eb->start, ret);
		qgroup_mark_inconsistent(fs_info);
	}
	return ret;
}

void btrfs_qgroup_destroy_extent_records(struct btrfs_transaction *trans)
{
	struct btrfs_qgroup_extent_record *entry;
	struct btrfs_qgroup_extent_record *next;
	struct rb_root *root;

	root = &trans->delayed_refs.dirty_extent_root;
	rbtree_postorder_for_each_entry_safe(entry, next, root, node) {
		ulist_free(entry->old_roots);
		kfree(entry);
	}
	*root = RB_ROOT;
}

void btrfs_free_squota_rsv(struct btrfs_fs_info *fs_info, u64 root, u64 rsv_bytes)
{
	if (btrfs_qgroup_mode(fs_info) != BTRFS_QGROUP_MODE_SIMPLE)
		return;

	if (!is_fstree(root))
		return;

	btrfs_qgroup_free_refroot(fs_info, root, rsv_bytes, BTRFS_QGROUP_RSV_DATA);
}

int btrfs_record_squota_delta(struct btrfs_fs_info *fs_info,
			      struct btrfs_squota_delta *delta)
{
	int ret;
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup *qg;
	LIST_HEAD(qgroup_list);
	u64 root = delta->root;
	u64 num_bytes = delta->num_bytes;
	const int sign = (delta->is_inc ? 1 : -1);

	if (btrfs_qgroup_mode(fs_info) != BTRFS_QGROUP_MODE_SIMPLE)
		return 0;

	if (!is_fstree(root))
		return 0;

	/* If the extent predates enabling quotas, don't count it. */
	if (delta->generation < fs_info->qgroup_enable_gen)
		return 0;

	spin_lock(&fs_info->qgroup_lock);
	qgroup = find_qgroup_rb(fs_info, root);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	}

	ret = 0;
	qgroup_iterator_add(&qgroup_list, qgroup);
	list_for_each_entry(qg, &qgroup_list, iterator) {
		struct btrfs_qgroup_list *glist;

		qg->excl += num_bytes * sign;
		qg->rfer += num_bytes * sign;
		qgroup_dirty(fs_info, qg);

		list_for_each_entry(glist, &qg->groups, next_group)
			qgroup_iterator_add(&qgroup_list, glist->group);
	}
	qgroup_iterator_clean(&qgroup_list);

out:
	spin_unlock(&fs_info->qgroup_lock);
	return ret;
}
