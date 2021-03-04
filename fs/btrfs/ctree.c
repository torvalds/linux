// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007,2008 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/mm.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "locking.h"
#include "volumes.h"
#include "qgroup.h"

static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level);
static int split_leaf(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      const struct btrfs_key *ins_key, struct btrfs_path *path,
		      int data_size, int extend);
static int push_node_left(struct btrfs_trans_handle *trans,
			  struct extent_buffer *dst,
			  struct extent_buffer *src, int empty);
static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct extent_buffer *dst_buf,
			      struct extent_buffer *src_buf);
static void del_ptr(struct btrfs_root *root, struct btrfs_path *path,
		    int level, int slot);

static const struct btrfs_csums {
	u16		size;
	const char	name[10];
	const char	driver[12];
} btrfs_csums[] = {
	[BTRFS_CSUM_TYPE_CRC32] = { .size = 4, .name = "crc32c" },
	[BTRFS_CSUM_TYPE_XXHASH] = { .size = 8, .name = "xxhash64" },
	[BTRFS_CSUM_TYPE_SHA256] = { .size = 32, .name = "sha256" },
	[BTRFS_CSUM_TYPE_BLAKE2] = { .size = 32, .name = "blake2b",
				     .driver = "blake2b-256" },
};

int btrfs_super_csum_size(const struct btrfs_super_block *s)
{
	u16 t = btrfs_super_csum_type(s);
	/*
	 * csum type is validated at mount time
	 */
	return btrfs_csums[t].size;
}

const char *btrfs_super_csum_name(u16 csum_type)
{
	/* csum type is validated at mount time */
	return btrfs_csums[csum_type].name;
}

/*
 * Return driver name if defined, otherwise the name that's also a valid driver
 * name
 */
const char *btrfs_super_csum_driver(u16 csum_type)
{
	/* csum type is validated at mount time */
	return btrfs_csums[csum_type].driver[0] ?
		btrfs_csums[csum_type].driver :
		btrfs_csums[csum_type].name;
}

size_t __attribute_const__ btrfs_get_num_csums(void)
{
	return ARRAY_SIZE(btrfs_csums);
}

struct btrfs_path *btrfs_alloc_path(void)
{
	return kmem_cache_zalloc(btrfs_path_cachep, GFP_NOFS);
}

/* this also releases the path */
void btrfs_free_path(struct btrfs_path *p)
{
	if (!p)
		return;
	btrfs_release_path(p);
	kmem_cache_free(btrfs_path_cachep, p);
}

/*
 * path release drops references on the extent buffers in the path
 * and it drops any locks held by this path
 *
 * It is safe to call this on paths that no locks or extent buffers held.
 */
noinline void btrfs_release_path(struct btrfs_path *p)
{
	int i;

	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		p->slots[i] = 0;
		if (!p->nodes[i])
			continue;
		if (p->locks[i]) {
			btrfs_tree_unlock_rw(p->nodes[i], p->locks[i]);
			p->locks[i] = 0;
		}
		free_extent_buffer(p->nodes[i]);
		p->nodes[i] = NULL;
	}
}

/*
 * safely gets a reference on the root node of a tree.  A lock
 * is not taken, so a concurrent writer may put a different node
 * at the root of the tree.  See btrfs_lock_root_node for the
 * looping required.
 *
 * The extent buffer returned by this has a reference taken, so
 * it won't disappear.  It may stop being the root of the tree
 * at any time because there are no locks held.
 */
struct extent_buffer *btrfs_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		rcu_read_lock();
		eb = rcu_dereference(root->node);

		/*
		 * RCU really hurts here, we could free up the root node because
		 * it was COWed but we may not get the new root node yet so do
		 * the inc_not_zero dance and if it doesn't work then
		 * synchronize_rcu and try again.
		 */
		if (atomic_inc_not_zero(&eb->refs)) {
			rcu_read_unlock();
			break;
		}
		rcu_read_unlock();
		synchronize_rcu();
	}
	return eb;
}

/*
 * Cowonly root (not-shareable trees, everything not subvolume or reloc roots),
 * just get put onto a simple dirty list.  Transaction walks this list to make
 * sure they get properly updated on disk.
 */
static void add_root_to_dirty_list(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (test_bit(BTRFS_ROOT_DIRTY, &root->state) ||
	    !test_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state))
		return;

	spin_lock(&fs_info->trans_lock);
	if (!test_and_set_bit(BTRFS_ROOT_DIRTY, &root->state)) {
		/* Want the extent tree to be the last on the list */
		if (root->root_key.objectid == BTRFS_EXTENT_TREE_OBJECTID)
			list_move_tail(&root->dirty_list,
				       &fs_info->dirty_cowonly_roots);
		else
			list_move(&root->dirty_list,
				  &fs_info->dirty_cowonly_roots);
	}
	spin_unlock(&fs_info->trans_lock);
}

/*
 * used by snapshot creation to make a copy of a root for a tree with
 * a given objectid.  The buffer with the new root node is returned in
 * cow_ret, and this func returns zero on success or a negative error code.
 */
int btrfs_copy_root(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      struct extent_buffer *buf,
		      struct extent_buffer **cow_ret, u64 new_root_objectid)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *cow;
	int ret = 0;
	int level;
	struct btrfs_disk_key disk_key;

	WARN_ON(test_bit(BTRFS_ROOT_SHAREABLE, &root->state) &&
		trans->transid != fs_info->running_transaction->transid);
	WARN_ON(test_bit(BTRFS_ROOT_SHAREABLE, &root->state) &&
		trans->transid != root->last_trans);

	level = btrfs_header_level(buf);
	if (level == 0)
		btrfs_item_key(buf, &disk_key, 0);
	else
		btrfs_node_key(buf, &disk_key, 0);

	cow = btrfs_alloc_tree_block(trans, root, 0, new_root_objectid,
				     &disk_key, level, buf->start, 0,
				     BTRFS_NESTING_NEW_ROOT);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	copy_extent_buffer_full(cow, buf);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_backref_rev(cow, BTRFS_MIXED_BACKREF_REV);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN |
				     BTRFS_HEADER_FLAG_RELOC);
	if (new_root_objectid == BTRFS_TREE_RELOC_OBJECTID)
		btrfs_set_header_flag(cow, BTRFS_HEADER_FLAG_RELOC);
	else
		btrfs_set_header_owner(cow, new_root_objectid);

	write_extent_buffer_fsid(cow, fs_info->fs_devices->metadata_uuid);

	WARN_ON(btrfs_header_generation(buf) > trans->transid);
	if (new_root_objectid == BTRFS_TREE_RELOC_OBJECTID)
		ret = btrfs_inc_ref(trans, root, cow, 1);
	else
		ret = btrfs_inc_ref(trans, root, cow, 0);
	if (ret) {
		btrfs_tree_unlock(cow);
		free_extent_buffer(cow);
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

enum mod_log_op {
	MOD_LOG_KEY_REPLACE,
	MOD_LOG_KEY_ADD,
	MOD_LOG_KEY_REMOVE,
	MOD_LOG_KEY_REMOVE_WHILE_FREEING,
	MOD_LOG_KEY_REMOVE_WHILE_MOVING,
	MOD_LOG_MOVE_KEYS,
	MOD_LOG_ROOT_REPLACE,
};

struct tree_mod_root {
	u64 logical;
	u8 level;
};

struct tree_mod_elem {
	struct rb_node node;
	u64 logical;
	u64 seq;
	enum mod_log_op op;

	/* this is used for MOD_LOG_KEY_* and MOD_LOG_MOVE_KEYS operations */
	int slot;

	/* this is used for MOD_LOG_KEY* and MOD_LOG_ROOT_REPLACE */
	u64 generation;

	/* those are used for op == MOD_LOG_KEY_{REPLACE,REMOVE} */
	struct btrfs_disk_key key;
	u64 blockptr;

	/* this is used for op == MOD_LOG_MOVE_KEYS */
	struct {
		int dst_slot;
		int nr_items;
	} move;

	/* this is used for op == MOD_LOG_ROOT_REPLACE */
	struct tree_mod_root old_root;
};

/*
 * Pull a new tree mod seq number for our operation.
 */
static inline u64 btrfs_inc_tree_mod_seq(struct btrfs_fs_info *fs_info)
{
	return atomic64_inc_return(&fs_info->tree_mod_seq);
}

/*
 * This adds a new blocker to the tree mod log's blocker list if the @elem
 * passed does not already have a sequence number set. So when a caller expects
 * to record tree modifications, it should ensure to set elem->seq to zero
 * before calling btrfs_get_tree_mod_seq.
 * Returns a fresh, unused tree log modification sequence number, even if no new
 * blocker was added.
 */
u64 btrfs_get_tree_mod_seq(struct btrfs_fs_info *fs_info,
			   struct seq_list *elem)
{
	write_lock(&fs_info->tree_mod_log_lock);
	if (!elem->seq) {
		elem->seq = btrfs_inc_tree_mod_seq(fs_info);
		list_add_tail(&elem->list, &fs_info->tree_mod_seq_list);
	}
	write_unlock(&fs_info->tree_mod_log_lock);

	return elem->seq;
}

void btrfs_put_tree_mod_seq(struct btrfs_fs_info *fs_info,
			    struct seq_list *elem)
{
	struct rb_root *tm_root;
	struct rb_node *node;
	struct rb_node *next;
	struct tree_mod_elem *tm;
	u64 min_seq = (u64)-1;
	u64 seq_putting = elem->seq;

	if (!seq_putting)
		return;

	write_lock(&fs_info->tree_mod_log_lock);
	list_del(&elem->list);
	elem->seq = 0;

	if (!list_empty(&fs_info->tree_mod_seq_list)) {
		struct seq_list *first;

		first = list_first_entry(&fs_info->tree_mod_seq_list,
					 struct seq_list, list);
		if (seq_putting > first->seq) {
			/*
			 * Blocker with lower sequence number exists, we
			 * cannot remove anything from the log.
			 */
			write_unlock(&fs_info->tree_mod_log_lock);
			return;
		}
		min_seq = first->seq;
	}

	/*
	 * anything that's lower than the lowest existing (read: blocked)
	 * sequence number can be removed from the tree.
	 */
	tm_root = &fs_info->tree_mod_log;
	for (node = rb_first(tm_root); node; node = next) {
		next = rb_next(node);
		tm = rb_entry(node, struct tree_mod_elem, node);
		if (tm->seq >= min_seq)
			continue;
		rb_erase(node, tm_root);
		kfree(tm);
	}
	write_unlock(&fs_info->tree_mod_log_lock);
}

/*
 * key order of the log:
 *       node/leaf start address -> sequence
 *
 * The 'start address' is the logical address of the *new* root node
 * for root replace operations, or the logical address of the affected
 * block for all other operations.
 */
static noinline int
__tree_mod_log_insert(struct btrfs_fs_info *fs_info, struct tree_mod_elem *tm)
{
	struct rb_root *tm_root;
	struct rb_node **new;
	struct rb_node *parent = NULL;
	struct tree_mod_elem *cur;

	lockdep_assert_held_write(&fs_info->tree_mod_log_lock);

	tm->seq = btrfs_inc_tree_mod_seq(fs_info);

	tm_root = &fs_info->tree_mod_log;
	new = &tm_root->rb_node;
	while (*new) {
		cur = rb_entry(*new, struct tree_mod_elem, node);
		parent = *new;
		if (cur->logical < tm->logical)
			new = &((*new)->rb_left);
		else if (cur->logical > tm->logical)
			new = &((*new)->rb_right);
		else if (cur->seq < tm->seq)
			new = &((*new)->rb_left);
		else if (cur->seq > tm->seq)
			new = &((*new)->rb_right);
		else
			return -EEXIST;
	}

	rb_link_node(&tm->node, parent, new);
	rb_insert_color(&tm->node, tm_root);
	return 0;
}

/*
 * Determines if logging can be omitted. Returns 1 if it can. Otherwise, it
 * returns zero with the tree_mod_log_lock acquired. The caller must hold
 * this until all tree mod log insertions are recorded in the rb tree and then
 * write unlock fs_info::tree_mod_log_lock.
 */
static inline int tree_mod_dont_log(struct btrfs_fs_info *fs_info,
				    struct extent_buffer *eb) {
	smp_mb();
	if (list_empty(&(fs_info)->tree_mod_seq_list))
		return 1;
	if (eb && btrfs_header_level(eb) == 0)
		return 1;

	write_lock(&fs_info->tree_mod_log_lock);
	if (list_empty(&(fs_info)->tree_mod_seq_list)) {
		write_unlock(&fs_info->tree_mod_log_lock);
		return 1;
	}

	return 0;
}

/* Similar to tree_mod_dont_log, but doesn't acquire any locks. */
static inline int tree_mod_need_log(const struct btrfs_fs_info *fs_info,
				    struct extent_buffer *eb)
{
	smp_mb();
	if (list_empty(&(fs_info)->tree_mod_seq_list))
		return 0;
	if (eb && btrfs_header_level(eb) == 0)
		return 0;

	return 1;
}

static struct tree_mod_elem *
alloc_tree_mod_elem(struct extent_buffer *eb, int slot,
		    enum mod_log_op op, gfp_t flags)
{
	struct tree_mod_elem *tm;

	tm = kzalloc(sizeof(*tm), flags);
	if (!tm)
		return NULL;

	tm->logical = eb->start;
	if (op != MOD_LOG_KEY_ADD) {
		btrfs_node_key(eb, &tm->key, slot);
		tm->blockptr = btrfs_node_blockptr(eb, slot);
	}
	tm->op = op;
	tm->slot = slot;
	tm->generation = btrfs_node_ptr_generation(eb, slot);
	RB_CLEAR_NODE(&tm->node);

	return tm;
}

static noinline int tree_mod_log_insert_key(struct extent_buffer *eb, int slot,
		enum mod_log_op op, gfp_t flags)
{
	struct tree_mod_elem *tm;
	int ret;

	if (!tree_mod_need_log(eb->fs_info, eb))
		return 0;

	tm = alloc_tree_mod_elem(eb, slot, op, flags);
	if (!tm)
		return -ENOMEM;

	if (tree_mod_dont_log(eb->fs_info, eb)) {
		kfree(tm);
		return 0;
	}

	ret = __tree_mod_log_insert(eb->fs_info, tm);
	write_unlock(&eb->fs_info->tree_mod_log_lock);
	if (ret)
		kfree(tm);

	return ret;
}

static noinline int tree_mod_log_insert_move(struct extent_buffer *eb,
		int dst_slot, int src_slot, int nr_items)
{
	struct tree_mod_elem *tm = NULL;
	struct tree_mod_elem **tm_list = NULL;
	int ret = 0;
	int i;
	int locked = 0;

	if (!tree_mod_need_log(eb->fs_info, eb))
		return 0;

	tm_list = kcalloc(nr_items, sizeof(struct tree_mod_elem *), GFP_NOFS);
	if (!tm_list)
		return -ENOMEM;

	tm = kzalloc(sizeof(*tm), GFP_NOFS);
	if (!tm) {
		ret = -ENOMEM;
		goto free_tms;
	}

	tm->logical = eb->start;
	tm->slot = src_slot;
	tm->move.dst_slot = dst_slot;
	tm->move.nr_items = nr_items;
	tm->op = MOD_LOG_MOVE_KEYS;

	for (i = 0; i + dst_slot < src_slot && i < nr_items; i++) {
		tm_list[i] = alloc_tree_mod_elem(eb, i + dst_slot,
		    MOD_LOG_KEY_REMOVE_WHILE_MOVING, GFP_NOFS);
		if (!tm_list[i]) {
			ret = -ENOMEM;
			goto free_tms;
		}
	}

	if (tree_mod_dont_log(eb->fs_info, eb))
		goto free_tms;
	locked = 1;

	/*
	 * When we override something during the move, we log these removals.
	 * This can only happen when we move towards the beginning of the
	 * buffer, i.e. dst_slot < src_slot.
	 */
	for (i = 0; i + dst_slot < src_slot && i < nr_items; i++) {
		ret = __tree_mod_log_insert(eb->fs_info, tm_list[i]);
		if (ret)
			goto free_tms;
	}

	ret = __tree_mod_log_insert(eb->fs_info, tm);
	if (ret)
		goto free_tms;
	write_unlock(&eb->fs_info->tree_mod_log_lock);
	kfree(tm_list);

	return 0;
free_tms:
	for (i = 0; i < nr_items; i++) {
		if (tm_list[i] && !RB_EMPTY_NODE(&tm_list[i]->node))
			rb_erase(&tm_list[i]->node, &eb->fs_info->tree_mod_log);
		kfree(tm_list[i]);
	}
	if (locked)
		write_unlock(&eb->fs_info->tree_mod_log_lock);
	kfree(tm_list);
	kfree(tm);

	return ret;
}

static inline int
__tree_mod_log_free_eb(struct btrfs_fs_info *fs_info,
		       struct tree_mod_elem **tm_list,
		       int nritems)
{
	int i, j;
	int ret;

	for (i = nritems - 1; i >= 0; i--) {
		ret = __tree_mod_log_insert(fs_info, tm_list[i]);
		if (ret) {
			for (j = nritems - 1; j > i; j--)
				rb_erase(&tm_list[j]->node,
					 &fs_info->tree_mod_log);
			return ret;
		}
	}

	return 0;
}

static noinline int tree_mod_log_insert_root(struct extent_buffer *old_root,
			 struct extent_buffer *new_root, int log_removal)
{
	struct btrfs_fs_info *fs_info = old_root->fs_info;
	struct tree_mod_elem *tm = NULL;
	struct tree_mod_elem **tm_list = NULL;
	int nritems = 0;
	int ret = 0;
	int i;

	if (!tree_mod_need_log(fs_info, NULL))
		return 0;

	if (log_removal && btrfs_header_level(old_root) > 0) {
		nritems = btrfs_header_nritems(old_root);
		tm_list = kcalloc(nritems, sizeof(struct tree_mod_elem *),
				  GFP_NOFS);
		if (!tm_list) {
			ret = -ENOMEM;
			goto free_tms;
		}
		for (i = 0; i < nritems; i++) {
			tm_list[i] = alloc_tree_mod_elem(old_root, i,
			    MOD_LOG_KEY_REMOVE_WHILE_FREEING, GFP_NOFS);
			if (!tm_list[i]) {
				ret = -ENOMEM;
				goto free_tms;
			}
		}
	}

	tm = kzalloc(sizeof(*tm), GFP_NOFS);
	if (!tm) {
		ret = -ENOMEM;
		goto free_tms;
	}

	tm->logical = new_root->start;
	tm->old_root.logical = old_root->start;
	tm->old_root.level = btrfs_header_level(old_root);
	tm->generation = btrfs_header_generation(old_root);
	tm->op = MOD_LOG_ROOT_REPLACE;

	if (tree_mod_dont_log(fs_info, NULL))
		goto free_tms;

	if (tm_list)
		ret = __tree_mod_log_free_eb(fs_info, tm_list, nritems);
	if (!ret)
		ret = __tree_mod_log_insert(fs_info, tm);

	write_unlock(&fs_info->tree_mod_log_lock);
	if (ret)
		goto free_tms;
	kfree(tm_list);

	return ret;

free_tms:
	if (tm_list) {
		for (i = 0; i < nritems; i++)
			kfree(tm_list[i]);
		kfree(tm_list);
	}
	kfree(tm);

	return ret;
}

static struct tree_mod_elem *
__tree_mod_log_search(struct btrfs_fs_info *fs_info, u64 start, u64 min_seq,
		      int smallest)
{
	struct rb_root *tm_root;
	struct rb_node *node;
	struct tree_mod_elem *cur = NULL;
	struct tree_mod_elem *found = NULL;

	read_lock(&fs_info->tree_mod_log_lock);
	tm_root = &fs_info->tree_mod_log;
	node = tm_root->rb_node;
	while (node) {
		cur = rb_entry(node, struct tree_mod_elem, node);
		if (cur->logical < start) {
			node = node->rb_left;
		} else if (cur->logical > start) {
			node = node->rb_right;
		} else if (cur->seq < min_seq) {
			node = node->rb_left;
		} else if (!smallest) {
			/* we want the node with the highest seq */
			if (found)
				BUG_ON(found->seq > cur->seq);
			found = cur;
			node = node->rb_left;
		} else if (cur->seq > min_seq) {
			/* we want the node with the smallest seq */
			if (found)
				BUG_ON(found->seq < cur->seq);
			found = cur;
			node = node->rb_right;
		} else {
			found = cur;
			break;
		}
	}
	read_unlock(&fs_info->tree_mod_log_lock);

	return found;
}

/*
 * this returns the element from the log with the smallest time sequence
 * value that's in the log (the oldest log item). any element with a time
 * sequence lower than min_seq will be ignored.
 */
static struct tree_mod_elem *
tree_mod_log_search_oldest(struct btrfs_fs_info *fs_info, u64 start,
			   u64 min_seq)
{
	return __tree_mod_log_search(fs_info, start, min_seq, 1);
}

/*
 * this returns the element from the log with the largest time sequence
 * value that's in the log (the most recent log item). any element with
 * a time sequence lower than min_seq will be ignored.
 */
static struct tree_mod_elem *
tree_mod_log_search(struct btrfs_fs_info *fs_info, u64 start, u64 min_seq)
{
	return __tree_mod_log_search(fs_info, start, min_seq, 0);
}

static noinline int tree_mod_log_eb_copy(struct extent_buffer *dst,
		     struct extent_buffer *src, unsigned long dst_offset,
		     unsigned long src_offset, int nr_items)
{
	struct btrfs_fs_info *fs_info = dst->fs_info;
	int ret = 0;
	struct tree_mod_elem **tm_list = NULL;
	struct tree_mod_elem **tm_list_add, **tm_list_rem;
	int i;
	int locked = 0;

	if (!tree_mod_need_log(fs_info, NULL))
		return 0;

	if (btrfs_header_level(dst) == 0 && btrfs_header_level(src) == 0)
		return 0;

	tm_list = kcalloc(nr_items * 2, sizeof(struct tree_mod_elem *),
			  GFP_NOFS);
	if (!tm_list)
		return -ENOMEM;

	tm_list_add = tm_list;
	tm_list_rem = tm_list + nr_items;
	for (i = 0; i < nr_items; i++) {
		tm_list_rem[i] = alloc_tree_mod_elem(src, i + src_offset,
		    MOD_LOG_KEY_REMOVE, GFP_NOFS);
		if (!tm_list_rem[i]) {
			ret = -ENOMEM;
			goto free_tms;
		}

		tm_list_add[i] = alloc_tree_mod_elem(dst, i + dst_offset,
		    MOD_LOG_KEY_ADD, GFP_NOFS);
		if (!tm_list_add[i]) {
			ret = -ENOMEM;
			goto free_tms;
		}
	}

	if (tree_mod_dont_log(fs_info, NULL))
		goto free_tms;
	locked = 1;

	for (i = 0; i < nr_items; i++) {
		ret = __tree_mod_log_insert(fs_info, tm_list_rem[i]);
		if (ret)
			goto free_tms;
		ret = __tree_mod_log_insert(fs_info, tm_list_add[i]);
		if (ret)
			goto free_tms;
	}

	write_unlock(&fs_info->tree_mod_log_lock);
	kfree(tm_list);

	return 0;

free_tms:
	for (i = 0; i < nr_items * 2; i++) {
		if (tm_list[i] && !RB_EMPTY_NODE(&tm_list[i]->node))
			rb_erase(&tm_list[i]->node, &fs_info->tree_mod_log);
		kfree(tm_list[i]);
	}
	if (locked)
		write_unlock(&fs_info->tree_mod_log_lock);
	kfree(tm_list);

	return ret;
}

static noinline int tree_mod_log_free_eb(struct extent_buffer *eb)
{
	struct tree_mod_elem **tm_list = NULL;
	int nritems = 0;
	int i;
	int ret = 0;

	if (btrfs_header_level(eb) == 0)
		return 0;

	if (!tree_mod_need_log(eb->fs_info, NULL))
		return 0;

	nritems = btrfs_header_nritems(eb);
	tm_list = kcalloc(nritems, sizeof(struct tree_mod_elem *), GFP_NOFS);
	if (!tm_list)
		return -ENOMEM;

	for (i = 0; i < nritems; i++) {
		tm_list[i] = alloc_tree_mod_elem(eb, i,
		    MOD_LOG_KEY_REMOVE_WHILE_FREEING, GFP_NOFS);
		if (!tm_list[i]) {
			ret = -ENOMEM;
			goto free_tms;
		}
	}

	if (tree_mod_dont_log(eb->fs_info, eb))
		goto free_tms;

	ret = __tree_mod_log_free_eb(eb->fs_info, tm_list, nritems);
	write_unlock(&eb->fs_info->tree_mod_log_lock);
	if (ret)
		goto free_tms;
	kfree(tm_list);

	return 0;

free_tms:
	for (i = 0; i < nritems; i++)
		kfree(tm_list[i]);
	kfree(tm_list);

	return ret;
}

/*
 * check if the tree block can be shared by multiple trees
 */
int btrfs_block_can_be_shared(struct btrfs_root *root,
			      struct extent_buffer *buf)
{
	/*
	 * Tree blocks not in shareable trees and tree roots are never shared.
	 * If a block was allocated after the last snapshot and the block was
	 * not allocated by tree relocation, we know the block is not shared.
	 */
	if (test_bit(BTRFS_ROOT_SHAREABLE, &root->state) &&
	    buf != root->node && buf != root->commit_root &&
	    (btrfs_header_generation(buf) <=
	     btrfs_root_last_snapshot(&root->root_item) ||
	     btrfs_header_flag(buf, BTRFS_HEADER_FLAG_RELOC)))
		return 1;

	return 0;
}

static noinline int update_ref_for_cow(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct extent_buffer *buf,
				       struct extent_buffer *cow,
				       int *last_ref)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 refs;
	u64 owner;
	u64 flags;
	u64 new_flags = 0;
	int ret;

	/*
	 * Backrefs update rules:
	 *
	 * Always use full backrefs for extent pointers in tree block
	 * allocated by tree relocation.
	 *
	 * If a shared tree block is no longer referenced by its owner
	 * tree (btrfs_header_owner(buf) == root->root_key.objectid),
	 * use full backrefs for extent pointers in tree block.
	 *
	 * If a tree block is been relocating
	 * (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID),
	 * use full backrefs for extent pointers in tree block.
	 * The reason for this is some operations (such as drop tree)
	 * are only allowed for blocks use full backrefs.
	 */

	if (btrfs_block_can_be_shared(root, buf)) {
		ret = btrfs_lookup_extent_info(trans, fs_info, buf->start,
					       btrfs_header_level(buf), 1,
					       &refs, &flags);
		if (ret)
			return ret;
		if (refs == 0) {
			ret = -EROFS;
			btrfs_handle_fs_error(fs_info, ret, NULL);
			return ret;
		}
	} else {
		refs = 1;
		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID ||
		    btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
			flags = BTRFS_BLOCK_FLAG_FULL_BACKREF;
		else
			flags = 0;
	}

	owner = btrfs_header_owner(buf);
	BUG_ON(owner == BTRFS_TREE_RELOC_OBJECTID &&
	       !(flags & BTRFS_BLOCK_FLAG_FULL_BACKREF));

	if (refs > 1) {
		if ((owner == root->root_key.objectid ||
		     root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) &&
		    !(flags & BTRFS_BLOCK_FLAG_FULL_BACKREF)) {
			ret = btrfs_inc_ref(trans, root, buf, 1);
			if (ret)
				return ret;

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID) {
				ret = btrfs_dec_ref(trans, root, buf, 0);
				if (ret)
					return ret;
				ret = btrfs_inc_ref(trans, root, cow, 1);
				if (ret)
					return ret;
			}
			new_flags |= BTRFS_BLOCK_FLAG_FULL_BACKREF;
		} else {

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID)
				ret = btrfs_inc_ref(trans, root, cow, 1);
			else
				ret = btrfs_inc_ref(trans, root, cow, 0);
			if (ret)
				return ret;
		}
		if (new_flags != 0) {
			int level = btrfs_header_level(buf);

			ret = btrfs_set_disk_extent_flags(trans, buf,
							  new_flags, level, 0);
			if (ret)
				return ret;
		}
	} else {
		if (flags & BTRFS_BLOCK_FLAG_FULL_BACKREF) {
			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID)
				ret = btrfs_inc_ref(trans, root, cow, 1);
			else
				ret = btrfs_inc_ref(trans, root, cow, 0);
			if (ret)
				return ret;
			ret = btrfs_dec_ref(trans, root, buf, 1);
			if (ret)
				return ret;
		}
		btrfs_clean_tree_block(buf);
		*last_ref = 1;
	}
	return 0;
}

static struct extent_buffer *alloc_tree_block_no_bg_flush(
					  struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  u64 parent_start,
					  const struct btrfs_disk_key *disk_key,
					  int level,
					  u64 hint,
					  u64 empty_size,
					  enum btrfs_lock_nesting nest)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *ret;

	/*
	 * If we are COWing a node/leaf from the extent, chunk, device or free
	 * space trees, make sure that we do not finish block group creation of
	 * pending block groups. We do this to avoid a deadlock.
	 * COWing can result in allocation of a new chunk, and flushing pending
	 * block groups (btrfs_create_pending_block_groups()) can be triggered
	 * when finishing allocation of a new chunk. Creation of a pending block
	 * group modifies the extent, chunk, device and free space trees,
	 * therefore we could deadlock with ourselves since we are holding a
	 * lock on an extent buffer that btrfs_create_pending_block_groups() may
	 * try to COW later.
	 * For similar reasons, we also need to delay flushing pending block
	 * groups when splitting a leaf or node, from one of those trees, since
	 * we are holding a write lock on it and its parent or when inserting a
	 * new root node for one of those trees.
	 */
	if (root == fs_info->extent_root ||
	    root == fs_info->chunk_root ||
	    root == fs_info->dev_root ||
	    root == fs_info->free_space_root)
		trans->can_flush_pending_bgs = false;

	ret = btrfs_alloc_tree_block(trans, root, parent_start,
				     root->root_key.objectid, disk_key, level,
				     hint, empty_size, nest);
	trans->can_flush_pending_bgs = true;

	return ret;
}

/*
 * does the dirty work in cow of a single block.  The parent block (if
 * supplied) is updated to point to the new cow copy.  The new buffer is marked
 * dirty and returned locked.  If you modify the block it needs to be marked
 * dirty again.
 *
 * search_start -- an allocation hint for the new block
 *
 * empty_size -- a hint that you plan on doing more cow.  This is the size in
 * bytes the allocator should try to find free next to the block it returns.
 * This is just a hint and may be ignored by the allocator.
 */
static noinline int __btrfs_cow_block(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct extent_buffer *buf,
			     struct extent_buffer *parent, int parent_slot,
			     struct extent_buffer **cow_ret,
			     u64 search_start, u64 empty_size,
			     enum btrfs_lock_nesting nest)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_disk_key disk_key;
	struct extent_buffer *cow;
	int level, ret;
	int last_ref = 0;
	int unlock_orig = 0;
	u64 parent_start = 0;

	if (*cow_ret == buf)
		unlock_orig = 1;

	btrfs_assert_tree_locked(buf);

	WARN_ON(test_bit(BTRFS_ROOT_SHAREABLE, &root->state) &&
		trans->transid != fs_info->running_transaction->transid);
	WARN_ON(test_bit(BTRFS_ROOT_SHAREABLE, &root->state) &&
		trans->transid != root->last_trans);

	level = btrfs_header_level(buf);

	if (level == 0)
		btrfs_item_key(buf, &disk_key, 0);
	else
		btrfs_node_key(buf, &disk_key, 0);

	if ((root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) && parent)
		parent_start = parent->start;

	cow = alloc_tree_block_no_bg_flush(trans, root, parent_start, &disk_key,
					   level, search_start, empty_size, nest);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	/* cow is set to blocking by btrfs_init_new_buffer */

	copy_extent_buffer_full(cow, buf);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_backref_rev(cow, BTRFS_MIXED_BACKREF_REV);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN |
				     BTRFS_HEADER_FLAG_RELOC);
	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
		btrfs_set_header_flag(cow, BTRFS_HEADER_FLAG_RELOC);
	else
		btrfs_set_header_owner(cow, root->root_key.objectid);

	write_extent_buffer_fsid(cow, fs_info->fs_devices->metadata_uuid);

	ret = update_ref_for_cow(trans, root, buf, cow, &last_ref);
	if (ret) {
		btrfs_tree_unlock(cow);
		free_extent_buffer(cow);
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	if (test_bit(BTRFS_ROOT_SHAREABLE, &root->state)) {
		ret = btrfs_reloc_cow_block(trans, root, buf, cow);
		if (ret) {
			btrfs_tree_unlock(cow);
			free_extent_buffer(cow);
			btrfs_abort_transaction(trans, ret);
			return ret;
		}
	}

	if (buf == root->node) {
		WARN_ON(parent && parent != buf);
		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID ||
		    btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
			parent_start = buf->start;

		atomic_inc(&cow->refs);
		ret = tree_mod_log_insert_root(root->node, cow, 1);
		BUG_ON(ret < 0);
		rcu_assign_pointer(root->node, cow);

		btrfs_free_tree_block(trans, root, buf, parent_start,
				      last_ref);
		free_extent_buffer(buf);
		add_root_to_dirty_list(root);
	} else {
		WARN_ON(trans->transid != btrfs_header_generation(parent));
		tree_mod_log_insert_key(parent, parent_slot,
					MOD_LOG_KEY_REPLACE, GFP_NOFS);
		btrfs_set_node_blockptr(parent, parent_slot,
					cow->start);
		btrfs_set_node_ptr_generation(parent, parent_slot,
					      trans->transid);
		btrfs_mark_buffer_dirty(parent);
		if (last_ref) {
			ret = tree_mod_log_free_eb(buf);
			if (ret) {
				btrfs_tree_unlock(cow);
				free_extent_buffer(cow);
				btrfs_abort_transaction(trans, ret);
				return ret;
			}
		}
		btrfs_free_tree_block(trans, root, buf, parent_start,
				      last_ref);
	}
	if (unlock_orig)
		btrfs_tree_unlock(buf);
	free_extent_buffer_stale(buf);
	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

/*
 * returns the logical address of the oldest predecessor of the given root.
 * entries older than time_seq are ignored.
 */
static struct tree_mod_elem *__tree_mod_log_oldest_root(
		struct extent_buffer *eb_root, u64 time_seq)
{
	struct tree_mod_elem *tm;
	struct tree_mod_elem *found = NULL;
	u64 root_logical = eb_root->start;
	int looped = 0;

	if (!time_seq)
		return NULL;

	/*
	 * the very last operation that's logged for a root is the
	 * replacement operation (if it is replaced at all). this has
	 * the logical address of the *new* root, making it the very
	 * first operation that's logged for this root.
	 */
	while (1) {
		tm = tree_mod_log_search_oldest(eb_root->fs_info, root_logical,
						time_seq);
		if (!looped && !tm)
			return NULL;
		/*
		 * if there are no tree operation for the oldest root, we simply
		 * return it. this should only happen if that (old) root is at
		 * level 0.
		 */
		if (!tm)
			break;

		/*
		 * if there's an operation that's not a root replacement, we
		 * found the oldest version of our root. normally, we'll find a
		 * MOD_LOG_KEY_REMOVE_WHILE_FREEING operation here.
		 */
		if (tm->op != MOD_LOG_ROOT_REPLACE)
			break;

		found = tm;
		root_logical = tm->old_root.logical;
		looped = 1;
	}

	/* if there's no old root to return, return what we found instead */
	if (!found)
		found = tm;

	return found;
}

/*
 * tm is a pointer to the first operation to rewind within eb. then, all
 * previous operations will be rewound (until we reach something older than
 * time_seq).
 */
static void
__tree_mod_log_rewind(struct btrfs_fs_info *fs_info, struct extent_buffer *eb,
		      u64 time_seq, struct tree_mod_elem *first_tm)
{
	u32 n;
	struct rb_node *next;
	struct tree_mod_elem *tm = first_tm;
	unsigned long o_dst;
	unsigned long o_src;
	unsigned long p_size = sizeof(struct btrfs_key_ptr);

	n = btrfs_header_nritems(eb);
	read_lock(&fs_info->tree_mod_log_lock);
	while (tm && tm->seq >= time_seq) {
		/*
		 * all the operations are recorded with the operator used for
		 * the modification. as we're going backwards, we do the
		 * opposite of each operation here.
		 */
		switch (tm->op) {
		case MOD_LOG_KEY_REMOVE_WHILE_FREEING:
			BUG_ON(tm->slot < n);
			fallthrough;
		case MOD_LOG_KEY_REMOVE_WHILE_MOVING:
		case MOD_LOG_KEY_REMOVE:
			btrfs_set_node_key(eb, &tm->key, tm->slot);
			btrfs_set_node_blockptr(eb, tm->slot, tm->blockptr);
			btrfs_set_node_ptr_generation(eb, tm->slot,
						      tm->generation);
			n++;
			break;
		case MOD_LOG_KEY_REPLACE:
			BUG_ON(tm->slot >= n);
			btrfs_set_node_key(eb, &tm->key, tm->slot);
			btrfs_set_node_blockptr(eb, tm->slot, tm->blockptr);
			btrfs_set_node_ptr_generation(eb, tm->slot,
						      tm->generation);
			break;
		case MOD_LOG_KEY_ADD:
			/* if a move operation is needed it's in the log */
			n--;
			break;
		case MOD_LOG_MOVE_KEYS:
			o_dst = btrfs_node_key_ptr_offset(tm->slot);
			o_src = btrfs_node_key_ptr_offset(tm->move.dst_slot);
			memmove_extent_buffer(eb, o_dst, o_src,
					      tm->move.nr_items * p_size);
			break;
		case MOD_LOG_ROOT_REPLACE:
			/*
			 * this operation is special. for roots, this must be
			 * handled explicitly before rewinding.
			 * for non-roots, this operation may exist if the node
			 * was a root: root A -> child B; then A gets empty and
			 * B is promoted to the new root. in the mod log, we'll
			 * have a root-replace operation for B, a tree block
			 * that is no root. we simply ignore that operation.
			 */
			break;
		}
		next = rb_next(&tm->node);
		if (!next)
			break;
		tm = rb_entry(next, struct tree_mod_elem, node);
		if (tm->logical != first_tm->logical)
			break;
	}
	read_unlock(&fs_info->tree_mod_log_lock);
	btrfs_set_header_nritems(eb, n);
}

/*
 * Called with eb read locked. If the buffer cannot be rewound, the same buffer
 * is returned. If rewind operations happen, a fresh buffer is returned. The
 * returned buffer is always read-locked. If the returned buffer is not the
 * input buffer, the lock on the input buffer is released and the input buffer
 * is freed (its refcount is decremented).
 */
static struct extent_buffer *
tree_mod_log_rewind(struct btrfs_fs_info *fs_info, struct btrfs_path *path,
		    struct extent_buffer *eb, u64 time_seq)
{
	struct extent_buffer *eb_rewin;
	struct tree_mod_elem *tm;

	if (!time_seq)
		return eb;

	if (btrfs_header_level(eb) == 0)
		return eb;

	tm = tree_mod_log_search(fs_info, eb->start, time_seq);
	if (!tm)
		return eb;

	btrfs_set_path_blocking(path);
	btrfs_set_lock_blocking_read(eb);

	if (tm->op == MOD_LOG_KEY_REMOVE_WHILE_FREEING) {
		BUG_ON(tm->slot != 0);
		eb_rewin = alloc_dummy_extent_buffer(fs_info, eb->start);
		if (!eb_rewin) {
			btrfs_tree_read_unlock_blocking(eb);
			free_extent_buffer(eb);
			return NULL;
		}
		btrfs_set_header_bytenr(eb_rewin, eb->start);
		btrfs_set_header_backref_rev(eb_rewin,
					     btrfs_header_backref_rev(eb));
		btrfs_set_header_owner(eb_rewin, btrfs_header_owner(eb));
		btrfs_set_header_level(eb_rewin, btrfs_header_level(eb));
	} else {
		eb_rewin = btrfs_clone_extent_buffer(eb);
		if (!eb_rewin) {
			btrfs_tree_read_unlock_blocking(eb);
			free_extent_buffer(eb);
			return NULL;
		}
	}

	btrfs_tree_read_unlock_blocking(eb);
	free_extent_buffer(eb);

	btrfs_set_buffer_lockdep_class(btrfs_header_owner(eb_rewin),
				       eb_rewin, btrfs_header_level(eb_rewin));
	btrfs_tree_read_lock(eb_rewin);
	__tree_mod_log_rewind(fs_info, eb_rewin, time_seq, tm);
	WARN_ON(btrfs_header_nritems(eb_rewin) >
		BTRFS_NODEPTRS_PER_BLOCK(fs_info));

	return eb_rewin;
}

/*
 * get_old_root() rewinds the state of @root's root node to the given @time_seq
 * value. If there are no changes, the current root->root_node is returned. If
 * anything changed in between, there's a fresh buffer allocated on which the
 * rewind operations are done. In any case, the returned buffer is read locked.
 * Returns NULL on error (with no locks held).
 */
static inline struct extent_buffer *
get_old_root(struct btrfs_root *root, u64 time_seq)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct tree_mod_elem *tm;
	struct extent_buffer *eb = NULL;
	struct extent_buffer *eb_root;
	u64 eb_root_owner = 0;
	struct extent_buffer *old;
	struct tree_mod_root *old_root = NULL;
	u64 old_generation = 0;
	u64 logical;
	int level;

	eb_root = btrfs_read_lock_root_node(root);
	tm = __tree_mod_log_oldest_root(eb_root, time_seq);
	if (!tm)
		return eb_root;

	if (tm->op == MOD_LOG_ROOT_REPLACE) {
		old_root = &tm->old_root;
		old_generation = tm->generation;
		logical = old_root->logical;
		level = old_root->level;
	} else {
		logical = eb_root->start;
		level = btrfs_header_level(eb_root);
	}

	tm = tree_mod_log_search(fs_info, logical, time_seq);
	if (old_root && tm && tm->op != MOD_LOG_KEY_REMOVE_WHILE_FREEING) {
		btrfs_tree_read_unlock(eb_root);
		free_extent_buffer(eb_root);
		old = read_tree_block(fs_info, logical, 0, level, NULL);
		if (WARN_ON(IS_ERR(old) || !extent_buffer_uptodate(old))) {
			if (!IS_ERR(old))
				free_extent_buffer(old);
			btrfs_warn(fs_info,
				   "failed to read tree block %llu from get_old_root",
				   logical);
		} else {
			eb = btrfs_clone_extent_buffer(old);
			free_extent_buffer(old);
		}
	} else if (old_root) {
		eb_root_owner = btrfs_header_owner(eb_root);
		btrfs_tree_read_unlock(eb_root);
		free_extent_buffer(eb_root);
		eb = alloc_dummy_extent_buffer(fs_info, logical);
	} else {
		btrfs_set_lock_blocking_read(eb_root);
		eb = btrfs_clone_extent_buffer(eb_root);
		btrfs_tree_read_unlock_blocking(eb_root);
		free_extent_buffer(eb_root);
	}

	if (!eb)
		return NULL;
	if (old_root) {
		btrfs_set_header_bytenr(eb, eb->start);
		btrfs_set_header_backref_rev(eb, BTRFS_MIXED_BACKREF_REV);
		btrfs_set_header_owner(eb, eb_root_owner);
		btrfs_set_header_level(eb, old_root->level);
		btrfs_set_header_generation(eb, old_generation);
	}
	btrfs_set_buffer_lockdep_class(btrfs_header_owner(eb), eb,
				       btrfs_header_level(eb));
	btrfs_tree_read_lock(eb);
	if (tm)
		__tree_mod_log_rewind(fs_info, eb, time_seq, tm);
	else
		WARN_ON(btrfs_header_level(eb) != 0);
	WARN_ON(btrfs_header_nritems(eb) > BTRFS_NODEPTRS_PER_BLOCK(fs_info));

	return eb;
}

int btrfs_old_root_level(struct btrfs_root *root, u64 time_seq)
{
	struct tree_mod_elem *tm;
	int level;
	struct extent_buffer *eb_root = btrfs_root_node(root);

	tm = __tree_mod_log_oldest_root(eb_root, time_seq);
	if (tm && tm->op == MOD_LOG_ROOT_REPLACE) {
		level = tm->old_root.level;
	} else {
		level = btrfs_header_level(eb_root);
	}
	free_extent_buffer(eb_root);

	return level;
}

static inline int should_cow_block(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct extent_buffer *buf)
{
	if (btrfs_is_testing(root->fs_info))
		return 0;

	/* Ensure we can see the FORCE_COW bit */
	smp_mb__before_atomic();

	/*
	 * We do not need to cow a block if
	 * 1) this block is not created or changed in this transaction;
	 * 2) this block does not belong to TREE_RELOC tree;
	 * 3) the root is not forced COW.
	 *
	 * What is forced COW:
	 *    when we create snapshot during committing the transaction,
	 *    after we've finished copying src root, we must COW the shared
	 *    block to ensure the metadata consistency.
	 */
	if (btrfs_header_generation(buf) == trans->transid &&
	    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN) &&
	    !(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID &&
	      btrfs_header_flag(buf, BTRFS_HEADER_FLAG_RELOC)) &&
	    !test_bit(BTRFS_ROOT_FORCE_COW, &root->state))
		return 0;
	return 1;
}

/*
 * cows a single block, see __btrfs_cow_block for the real work.
 * This version of it has extra checks so that a block isn't COWed more than
 * once per transaction, as long as it hasn't been written yet
 */
noinline int btrfs_cow_block(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct extent_buffer *buf,
		    struct extent_buffer *parent, int parent_slot,
		    struct extent_buffer **cow_ret,
		    enum btrfs_lock_nesting nest)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 search_start;
	int ret;

	if (test_bit(BTRFS_ROOT_DELETING, &root->state))
		btrfs_err(fs_info,
			"COW'ing blocks on a fs root that's being dropped");

	if (trans->transaction != fs_info->running_transaction)
		WARN(1, KERN_CRIT "trans %llu running %llu\n",
		       trans->transid,
		       fs_info->running_transaction->transid);

	if (trans->transid != fs_info->generation)
		WARN(1, KERN_CRIT "trans %llu running %llu\n",
		       trans->transid, fs_info->generation);

	if (!should_cow_block(trans, root, buf)) {
		trans->dirty = true;
		*cow_ret = buf;
		return 0;
	}

	search_start = buf->start & ~((u64)SZ_1G - 1);

	if (parent)
		btrfs_set_lock_blocking_write(parent);
	btrfs_set_lock_blocking_write(buf);

	/*
	 * Before CoWing this block for later modification, check if it's
	 * the subtree root and do the delayed subtree trace if needed.
	 *
	 * Also We don't care about the error, as it's handled internally.
	 */
	btrfs_qgroup_trace_subtree_after_cow(trans, root, buf);
	ret = __btrfs_cow_block(trans, root, buf, parent,
				 parent_slot, cow_ret, search_start, 0, nest);

	trace_btrfs_cow_block(root, buf, *cow_ret);

	return ret;
}

/*
 * helper function for defrag to decide if two blocks pointed to by a
 * node are actually close by
 */
static int close_blocks(u64 blocknr, u64 other, u32 blocksize)
{
	if (blocknr < other && other - (blocknr + blocksize) < 32768)
		return 1;
	if (blocknr > other && blocknr - (other + blocksize) < 32768)
		return 1;
	return 0;
}

#ifdef __LITTLE_ENDIAN

/*
 * Compare two keys, on little-endian the disk order is same as CPU order and
 * we can avoid the conversion.
 */
static int comp_keys(const struct btrfs_disk_key *disk_key,
		     const struct btrfs_key *k2)
{
	const struct btrfs_key *k1 = (const struct btrfs_key *)disk_key;

	return btrfs_comp_cpu_keys(k1, k2);
}

#else

/*
 * compare two keys in a memcmp fashion
 */
static int comp_keys(const struct btrfs_disk_key *disk,
		     const struct btrfs_key *k2)
{
	struct btrfs_key k1;

	btrfs_disk_key_to_cpu(&k1, disk);

	return btrfs_comp_cpu_keys(&k1, k2);
}
#endif

/*
 * same as comp_keys only with two btrfs_key's
 */
int __pure btrfs_comp_cpu_keys(const struct btrfs_key *k1, const struct btrfs_key *k2)
{
	if (k1->objectid > k2->objectid)
		return 1;
	if (k1->objectid < k2->objectid)
		return -1;
	if (k1->type > k2->type)
		return 1;
	if (k1->type < k2->type)
		return -1;
	if (k1->offset > k2->offset)
		return 1;
	if (k1->offset < k2->offset)
		return -1;
	return 0;
}

/*
 * this is used by the defrag code to go through all the
 * leaves pointed to by a node and reallocate them so that
 * disk order is close to key order
 */
int btrfs_realloc_node(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct extent_buffer *parent,
		       int start_slot, u64 *last_ret,
		       struct btrfs_key *progress)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *cur;
	u64 blocknr;
	u64 gen;
	u64 search_start = *last_ret;
	u64 last_block = 0;
	u64 other;
	u32 parent_nritems;
	int end_slot;
	int i;
	int err = 0;
	int parent_level;
	int uptodate;
	u32 blocksize;
	int progress_passed = 0;
	struct btrfs_disk_key disk_key;

	parent_level = btrfs_header_level(parent);

	WARN_ON(trans->transaction != fs_info->running_transaction);
	WARN_ON(trans->transid != fs_info->generation);

	parent_nritems = btrfs_header_nritems(parent);
	blocksize = fs_info->nodesize;
	end_slot = parent_nritems - 1;

	if (parent_nritems <= 1)
		return 0;

	btrfs_set_lock_blocking_write(parent);

	for (i = start_slot; i <= end_slot; i++) {
		struct btrfs_key first_key;
		int close = 1;

		btrfs_node_key(parent, &disk_key, i);
		if (!progress_passed && comp_keys(&disk_key, progress) < 0)
			continue;

		progress_passed = 1;
		blocknr = btrfs_node_blockptr(parent, i);
		gen = btrfs_node_ptr_generation(parent, i);
		btrfs_node_key_to_cpu(parent, &first_key, i);
		if (last_block == 0)
			last_block = blocknr;

		if (i > 0) {
			other = btrfs_node_blockptr(parent, i - 1);
			close = close_blocks(blocknr, other, blocksize);
		}
		if (!close && i < end_slot) {
			other = btrfs_node_blockptr(parent, i + 1);
			close = close_blocks(blocknr, other, blocksize);
		}
		if (close) {
			last_block = blocknr;
			continue;
		}

		cur = find_extent_buffer(fs_info, blocknr);
		if (cur)
			uptodate = btrfs_buffer_uptodate(cur, gen, 0);
		else
			uptodate = 0;
		if (!cur || !uptodate) {
			if (!cur) {
				cur = read_tree_block(fs_info, blocknr, gen,
						      parent_level - 1,
						      &first_key);
				if (IS_ERR(cur)) {
					return PTR_ERR(cur);
				} else if (!extent_buffer_uptodate(cur)) {
					free_extent_buffer(cur);
					return -EIO;
				}
			} else if (!uptodate) {
				err = btrfs_read_buffer(cur, gen,
						parent_level - 1,&first_key);
				if (err) {
					free_extent_buffer(cur);
					return err;
				}
			}
		}
		if (search_start == 0)
			search_start = last_block;

		btrfs_tree_lock(cur);
		btrfs_set_lock_blocking_write(cur);
		err = __btrfs_cow_block(trans, root, cur, parent, i,
					&cur, search_start,
					min(16 * blocksize,
					    (end_slot - i) * blocksize),
					BTRFS_NESTING_COW);
		if (err) {
			btrfs_tree_unlock(cur);
			free_extent_buffer(cur);
			break;
		}
		search_start = cur->start;
		last_block = cur->start;
		*last_ret = search_start;
		btrfs_tree_unlock(cur);
		free_extent_buffer(cur);
	}
	return err;
}

/*
 * search for key in the extent_buffer.  The items start at offset p,
 * and they are item_size apart.  There are 'max' items in p.
 *
 * the slot in the array is returned via slot, and it points to
 * the place where you would insert key if it is not found in
 * the array.
 *
 * slot may point to max if the key is bigger than all of the keys
 */
static noinline int generic_bin_search(struct extent_buffer *eb,
				       unsigned long p, int item_size,
				       const struct btrfs_key *key,
				       int max, int *slot)
{
	int low = 0;
	int high = max;
	int ret;
	const int key_size = sizeof(struct btrfs_disk_key);

	if (low > high) {
		btrfs_err(eb->fs_info,
		 "%s: low (%d) > high (%d) eb %llu owner %llu level %d",
			  __func__, low, high, eb->start,
			  btrfs_header_owner(eb), btrfs_header_level(eb));
		return -EINVAL;
	}

	while (low < high) {
		unsigned long oip;
		unsigned long offset;
		struct btrfs_disk_key *tmp;
		struct btrfs_disk_key unaligned;
		int mid;

		mid = (low + high) / 2;
		offset = p + mid * item_size;
		oip = offset_in_page(offset);

		if (oip + key_size <= PAGE_SIZE) {
			const unsigned long idx = offset >> PAGE_SHIFT;
			char *kaddr = page_address(eb->pages[idx]);

			tmp = (struct btrfs_disk_key *)(kaddr + oip);
		} else {
			read_extent_buffer(eb, &unaligned, offset, key_size);
			tmp = &unaligned;
		}

		ret = comp_keys(tmp, key);

		if (ret < 0)
			low = mid + 1;
		else if (ret > 0)
			high = mid;
		else {
			*slot = mid;
			return 0;
		}
	}
	*slot = low;
	return 1;
}

/*
 * simple bin_search frontend that does the right thing for
 * leaves vs nodes
 */
int btrfs_bin_search(struct extent_buffer *eb, const struct btrfs_key *key,
		     int *slot)
{
	if (btrfs_header_level(eb) == 0)
		return generic_bin_search(eb,
					  offsetof(struct btrfs_leaf, items),
					  sizeof(struct btrfs_item),
					  key, btrfs_header_nritems(eb),
					  slot);
	else
		return generic_bin_search(eb,
					  offsetof(struct btrfs_node, ptrs),
					  sizeof(struct btrfs_key_ptr),
					  key, btrfs_header_nritems(eb),
					  slot);
}

static void root_add_used(struct btrfs_root *root, u32 size)
{
	spin_lock(&root->accounting_lock);
	btrfs_set_root_used(&root->root_item,
			    btrfs_root_used(&root->root_item) + size);
	spin_unlock(&root->accounting_lock);
}

static void root_sub_used(struct btrfs_root *root, u32 size)
{
	spin_lock(&root->accounting_lock);
	btrfs_set_root_used(&root->root_item,
			    btrfs_root_used(&root->root_item) - size);
	spin_unlock(&root->accounting_lock);
}

/* given a node and slot number, this reads the blocks it points to.  The
 * extent buffer is returned with a reference taken (but unlocked).
 */
struct extent_buffer *btrfs_read_node_slot(struct extent_buffer *parent,
					   int slot)
{
	int level = btrfs_header_level(parent);
	struct extent_buffer *eb;
	struct btrfs_key first_key;

	if (slot < 0 || slot >= btrfs_header_nritems(parent))
		return ERR_PTR(-ENOENT);

	BUG_ON(level == 0);

	btrfs_node_key_to_cpu(parent, &first_key, slot);
	eb = read_tree_block(parent->fs_info, btrfs_node_blockptr(parent, slot),
			     btrfs_node_ptr_generation(parent, slot),
			     level - 1, &first_key);
	if (!IS_ERR(eb) && !extent_buffer_uptodate(eb)) {
		free_extent_buffer(eb);
		eb = ERR_PTR(-EIO);
	}

	return eb;
}

/*
 * node level balancing, used to make sure nodes are in proper order for
 * item deletion.  We balance from the top down, so we have to make sure
 * that a deletion won't leave an node completely empty later on.
 */
static noinline int balance_level(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct btrfs_path *path, int level)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	u64 orig_ptr;

	ASSERT(level > 0);

	mid = path->nodes[level];

	WARN_ON(path->locks[level] != BTRFS_WRITE_LOCK &&
		path->locks[level] != BTRFS_WRITE_LOCK_BLOCKING);
	WARN_ON(btrfs_header_generation(mid) != trans->transid);

	orig_ptr = btrfs_node_blockptr(mid, orig_slot);

	if (level < BTRFS_MAX_LEVEL - 1) {
		parent = path->nodes[level + 1];
		pslot = path->slots[level + 1];
	}

	/*
	 * deal with the case where there is only one pointer in the root
	 * by promoting the node below to a root
	 */
	if (!parent) {
		struct extent_buffer *child;

		if (btrfs_header_nritems(mid) != 1)
			return 0;

		/* promote the child to a root */
		child = btrfs_read_node_slot(mid, 0);
		if (IS_ERR(child)) {
			ret = PTR_ERR(child);
			btrfs_handle_fs_error(fs_info, ret, NULL);
			goto enospc;
		}

		btrfs_tree_lock(child);
		btrfs_set_lock_blocking_write(child);
		ret = btrfs_cow_block(trans, root, child, mid, 0, &child,
				      BTRFS_NESTING_COW);
		if (ret) {
			btrfs_tree_unlock(child);
			free_extent_buffer(child);
			goto enospc;
		}

		ret = tree_mod_log_insert_root(root->node, child, 1);
		BUG_ON(ret < 0);
		rcu_assign_pointer(root->node, child);

		add_root_to_dirty_list(root);
		btrfs_tree_unlock(child);

		path->locks[level] = 0;
		path->nodes[level] = NULL;
		btrfs_clean_tree_block(mid);
		btrfs_tree_unlock(mid);
		/* once for the path */
		free_extent_buffer(mid);

		root_sub_used(root, mid->len);
		btrfs_free_tree_block(trans, root, mid, 0, 1);
		/* once for the root ptr */
		free_extent_buffer_stale(mid);
		return 0;
	}
	if (btrfs_header_nritems(mid) >
	    BTRFS_NODEPTRS_PER_BLOCK(fs_info) / 4)
		return 0;

	left = btrfs_read_node_slot(parent, pslot - 1);
	if (IS_ERR(left))
		left = NULL;

	if (left) {
		__btrfs_tree_lock(left, BTRFS_NESTING_LEFT);
		btrfs_set_lock_blocking_write(left);
		wret = btrfs_cow_block(trans, root, left,
				       parent, pslot - 1, &left,
				       BTRFS_NESTING_LEFT_COW);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}

	right = btrfs_read_node_slot(parent, pslot + 1);
	if (IS_ERR(right))
		right = NULL;

	if (right) {
		__btrfs_tree_lock(right, BTRFS_NESTING_RIGHT);
		btrfs_set_lock_blocking_write(right);
		wret = btrfs_cow_block(trans, root, right,
				       parent, pslot + 1, &right,
				       BTRFS_NESTING_RIGHT_COW);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}

	/* first, try to make some room in the middle buffer */
	if (left) {
		orig_slot += btrfs_header_nritems(left);
		wret = push_node_left(trans, left, mid, 1);
		if (wret < 0)
			ret = wret;
	}

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right) {
		wret = push_node_left(trans, mid, right, 1);
		if (wret < 0 && wret != -ENOSPC)
			ret = wret;
		if (btrfs_header_nritems(right) == 0) {
			btrfs_clean_tree_block(right);
			btrfs_tree_unlock(right);
			del_ptr(root, path, level + 1, pslot + 1);
			root_sub_used(root, right->len);
			btrfs_free_tree_block(trans, root, right, 0, 1);
			free_extent_buffer_stale(right);
			right = NULL;
		} else {
			struct btrfs_disk_key right_key;
			btrfs_node_key(right, &right_key, 0);
			ret = tree_mod_log_insert_key(parent, pslot + 1,
					MOD_LOG_KEY_REPLACE, GFP_NOFS);
			BUG_ON(ret < 0);
			btrfs_set_node_key(parent, &right_key, pslot + 1);
			btrfs_mark_buffer_dirty(parent);
		}
	}
	if (btrfs_header_nritems(mid) == 1) {
		/*
		 * we're not allowed to leave a node with one item in the
		 * tree during a delete.  A deletion from lower in the tree
		 * could try to delete the only pointer in this node.
		 * So, pull some keys from the left.
		 * There has to be a left pointer at this point because
		 * otherwise we would have pulled some pointers from the
		 * right
		 */
		if (!left) {
			ret = -EROFS;
			btrfs_handle_fs_error(fs_info, ret, NULL);
			goto enospc;
		}
		wret = balance_node_right(trans, mid, left);
		if (wret < 0) {
			ret = wret;
			goto enospc;
		}
		if (wret == 1) {
			wret = push_node_left(trans, left, mid, 1);
			if (wret < 0)
				ret = wret;
		}
		BUG_ON(wret == 1);
	}
	if (btrfs_header_nritems(mid) == 0) {
		btrfs_clean_tree_block(mid);
		btrfs_tree_unlock(mid);
		del_ptr(root, path, level + 1, pslot);
		root_sub_used(root, mid->len);
		btrfs_free_tree_block(trans, root, mid, 0, 1);
		free_extent_buffer_stale(mid);
		mid = NULL;
	} else {
		/* update the parent key to reflect our changes */
		struct btrfs_disk_key mid_key;
		btrfs_node_key(mid, &mid_key, 0);
		ret = tree_mod_log_insert_key(parent, pslot,
				MOD_LOG_KEY_REPLACE, GFP_NOFS);
		BUG_ON(ret < 0);
		btrfs_set_node_key(parent, &mid_key, pslot);
		btrfs_mark_buffer_dirty(parent);
	}

	/* update the path */
	if (left) {
		if (btrfs_header_nritems(left) > orig_slot) {
			atomic_inc(&left->refs);
			/* left was locked after cow */
			path->nodes[level] = left;
			path->slots[level + 1] -= 1;
			path->slots[level] = orig_slot;
			if (mid) {
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			}
		} else {
			orig_slot -= btrfs_header_nritems(left);
			path->slots[level] = orig_slot;
		}
	}
	/* double check we haven't messed things up */
	if (orig_ptr !=
	    btrfs_node_blockptr(path->nodes[level], path->slots[level]))
		BUG();
enospc:
	if (right) {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	if (left) {
		if (path->nodes[level] != left)
			btrfs_tree_unlock(left);
		free_extent_buffer(left);
	}
	return ret;
}

/* Node balancing for insertion.  Here we only split or push nodes around
 * when they are completely full.  This is also done top down, so we
 * have to be pessimistic.
 */
static noinline int push_nodes_for_insert(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path, int level)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];

	if (level == 0)
		return 1;

	mid = path->nodes[level];
	WARN_ON(btrfs_header_generation(mid) != trans->transid);

	if (level < BTRFS_MAX_LEVEL - 1) {
		parent = path->nodes[level + 1];
		pslot = path->slots[level + 1];
	}

	if (!parent)
		return 1;

	left = btrfs_read_node_slot(parent, pslot - 1);
	if (IS_ERR(left))
		left = NULL;

	/* first, try to make some room in the middle buffer */
	if (left) {
		u32 left_nr;

		__btrfs_tree_lock(left, BTRFS_NESTING_LEFT);
		btrfs_set_lock_blocking_write(left);

		left_nr = btrfs_header_nritems(left);
		if (left_nr >= BTRFS_NODEPTRS_PER_BLOCK(fs_info) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, left, parent,
					      pslot - 1, &left,
					      BTRFS_NESTING_LEFT_COW);
			if (ret)
				wret = 1;
			else {
				wret = push_node_left(trans, left, mid, 0);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;
			orig_slot += left_nr;
			btrfs_node_key(mid, &disk_key, 0);
			ret = tree_mod_log_insert_key(parent, pslot,
					MOD_LOG_KEY_REPLACE, GFP_NOFS);
			BUG_ON(ret < 0);
			btrfs_set_node_key(parent, &disk_key, pslot);
			btrfs_mark_buffer_dirty(parent);
			if (btrfs_header_nritems(left) > orig_slot) {
				path->nodes[level] = left;
				path->slots[level + 1] -= 1;
				path->slots[level] = orig_slot;
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			} else {
				orig_slot -=
					btrfs_header_nritems(left);
				path->slots[level] = orig_slot;
				btrfs_tree_unlock(left);
				free_extent_buffer(left);
			}
			return 0;
		}
		btrfs_tree_unlock(left);
		free_extent_buffer(left);
	}
	right = btrfs_read_node_slot(parent, pslot + 1);
	if (IS_ERR(right))
		right = NULL;

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right) {
		u32 right_nr;

		__btrfs_tree_lock(right, BTRFS_NESTING_RIGHT);
		btrfs_set_lock_blocking_write(right);

		right_nr = btrfs_header_nritems(right);
		if (right_nr >= BTRFS_NODEPTRS_PER_BLOCK(fs_info) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, right,
					      parent, pslot + 1,
					      &right, BTRFS_NESTING_RIGHT_COW);
			if (ret)
				wret = 1;
			else {
				wret = balance_node_right(trans, right, mid);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_node_key(right, &disk_key, 0);
			ret = tree_mod_log_insert_key(parent, pslot + 1,
					MOD_LOG_KEY_REPLACE, GFP_NOFS);
			BUG_ON(ret < 0);
			btrfs_set_node_key(parent, &disk_key, pslot + 1);
			btrfs_mark_buffer_dirty(parent);

			if (btrfs_header_nritems(mid) <= orig_slot) {
				path->nodes[level] = right;
				path->slots[level + 1] += 1;
				path->slots[level] = orig_slot -
					btrfs_header_nritems(mid);
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			} else {
				btrfs_tree_unlock(right);
				free_extent_buffer(right);
			}
			return 0;
		}
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	return 1;
}

/*
 * readahead one full node of leaves, finding things that are close
 * to the block in 'slot', and triggering ra on them.
 */
static void reada_for_search(struct btrfs_fs_info *fs_info,
			     struct btrfs_path *path,
			     int level, int slot, u64 objectid)
{
	struct extent_buffer *node;
	struct btrfs_disk_key disk_key;
	u32 nritems;
	u64 search;
	u64 target;
	u64 nread = 0;
	struct extent_buffer *eb;
	u32 nr;
	u32 blocksize;
	u32 nscan = 0;

	if (level != 1)
		return;

	if (!path->nodes[level])
		return;

	node = path->nodes[level];

	search = btrfs_node_blockptr(node, slot);
	blocksize = fs_info->nodesize;
	eb = find_extent_buffer(fs_info, search);
	if (eb) {
		free_extent_buffer(eb);
		return;
	}

	target = search;

	nritems = btrfs_header_nritems(node);
	nr = slot;

	while (1) {
		if (path->reada == READA_BACK) {
			if (nr == 0)
				break;
			nr--;
		} else if (path->reada == READA_FORWARD) {
			nr++;
			if (nr >= nritems)
				break;
		}
		if (path->reada == READA_BACK && objectid) {
			btrfs_node_key(node, &disk_key, nr);
			if (btrfs_disk_key_objectid(&disk_key) != objectid)
				break;
		}
		search = btrfs_node_blockptr(node, nr);
		if ((search <= target && target - search <= 65536) ||
		    (search > target && search - target <= 65536)) {
			readahead_tree_block(fs_info, search);
			nread += blocksize;
		}
		nscan++;
		if ((nread > 65536 || nscan > 32))
			break;
	}
}

static noinline void reada_for_balance(struct btrfs_fs_info *fs_info,
				       struct btrfs_path *path, int level)
{
	int slot;
	int nritems;
	struct extent_buffer *parent;
	struct extent_buffer *eb;
	u64 gen;
	u64 block1 = 0;
	u64 block2 = 0;

	parent = path->nodes[level + 1];
	if (!parent)
		return;

	nritems = btrfs_header_nritems(parent);
	slot = path->slots[level + 1];

	if (slot > 0) {
		block1 = btrfs_node_blockptr(parent, slot - 1);
		gen = btrfs_node_ptr_generation(parent, slot - 1);
		eb = find_extent_buffer(fs_info, block1);
		/*
		 * if we get -eagain from btrfs_buffer_uptodate, we
		 * don't want to return eagain here.  That will loop
		 * forever
		 */
		if (eb && btrfs_buffer_uptodate(eb, gen, 1) != 0)
			block1 = 0;
		free_extent_buffer(eb);
	}
	if (slot + 1 < nritems) {
		block2 = btrfs_node_blockptr(parent, slot + 1);
		gen = btrfs_node_ptr_generation(parent, slot + 1);
		eb = find_extent_buffer(fs_info, block2);
		if (eb && btrfs_buffer_uptodate(eb, gen, 1) != 0)
			block2 = 0;
		free_extent_buffer(eb);
	}

	if (block1)
		readahead_tree_block(fs_info, block1);
	if (block2)
		readahead_tree_block(fs_info, block2);
}


/*
 * when we walk down the tree, it is usually safe to unlock the higher layers
 * in the tree.  The exceptions are when our path goes through slot 0, because
 * operations on the tree might require changing key pointers higher up in the
 * tree.
 *
 * callers might also have set path->keep_locks, which tells this code to keep
 * the lock if the path points to the last slot in the block.  This is part of
 * walking through the tree, and selecting the next slot in the higher block.
 *
 * lowest_unlock sets the lowest level in the tree we're allowed to unlock.  so
 * if lowest_unlock is 1, level 0 won't be unlocked
 */
static noinline void unlock_up(struct btrfs_path *path, int level,
			       int lowest_unlock, int min_write_lock_level,
			       int *write_lock_level)
{
	int i;
	int skip_level = level;
	int no_skips = 0;
	struct extent_buffer *t;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		if (!path->nodes[i])
			break;
		if (!path->locks[i])
			break;
		if (!no_skips && path->slots[i] == 0) {
			skip_level = i + 1;
			continue;
		}
		if (!no_skips && path->keep_locks) {
			u32 nritems;
			t = path->nodes[i];
			nritems = btrfs_header_nritems(t);
			if (nritems < 1 || path->slots[i] >= nritems - 1) {
				skip_level = i + 1;
				continue;
			}
		}
		if (skip_level < i && i >= lowest_unlock)
			no_skips = 1;

		t = path->nodes[i];
		if (i >= lowest_unlock && i > skip_level) {
			btrfs_tree_unlock_rw(t, path->locks[i]);
			path->locks[i] = 0;
			if (write_lock_level &&
			    i > min_write_lock_level &&
			    i <= *write_lock_level) {
				*write_lock_level = i - 1;
			}
		}
	}
}

/*
 * helper function for btrfs_search_slot.  The goal is to find a block
 * in cache without setting the path to blocking.  If we find the block
 * we return zero and the path is unchanged.
 *
 * If we can't find the block, we set the path blocking and do some
 * reada.  -EAGAIN is returned and the search must be repeated.
 */
static int
read_block_for_search(struct btrfs_root *root, struct btrfs_path *p,
		      struct extent_buffer **eb_ret, int level, int slot,
		      const struct btrfs_key *key)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 blocknr;
	u64 gen;
	struct extent_buffer *tmp;
	struct btrfs_key first_key;
	int ret;
	int parent_level;

	blocknr = btrfs_node_blockptr(*eb_ret, slot);
	gen = btrfs_node_ptr_generation(*eb_ret, slot);
	parent_level = btrfs_header_level(*eb_ret);
	btrfs_node_key_to_cpu(*eb_ret, &first_key, slot);

	tmp = find_extent_buffer(fs_info, blocknr);
	if (tmp) {
		/* first we do an atomic uptodate check */
		if (btrfs_buffer_uptodate(tmp, gen, 1) > 0) {
			/*
			 * Do extra check for first_key, eb can be stale due to
			 * being cached, read from scrub, or have multiple
			 * parents (shared tree blocks).
			 */
			if (btrfs_verify_level_key(tmp,
					parent_level - 1, &first_key, gen)) {
				free_extent_buffer(tmp);
				return -EUCLEAN;
			}
			*eb_ret = tmp;
			return 0;
		}

		/* the pages were up to date, but we failed
		 * the generation number check.  Do a full
		 * read for the generation number that is correct.
		 * We must do this without dropping locks so
		 * we can trust our generation number
		 */
		btrfs_set_path_blocking(p);

		/* now we're allowed to do a blocking uptodate check */
		ret = btrfs_read_buffer(tmp, gen, parent_level - 1, &first_key);
		if (!ret) {
			*eb_ret = tmp;
			return 0;
		}
		free_extent_buffer(tmp);
		btrfs_release_path(p);
		return -EIO;
	}

	/*
	 * reduce lock contention at high levels
	 * of the btree by dropping locks before
	 * we read.  Don't release the lock on the current
	 * level because we need to walk this node to figure
	 * out which blocks to read.
	 */
	btrfs_unlock_up_safe(p, level + 1);
	btrfs_set_path_blocking(p);

	if (p->reada != READA_NONE)
		reada_for_search(fs_info, p, level, slot, key->objectid);

	ret = -EAGAIN;
	tmp = read_tree_block(fs_info, blocknr, gen, parent_level - 1,
			      &first_key);
	if (!IS_ERR(tmp)) {
		/*
		 * If the read above didn't mark this buffer up to date,
		 * it will never end up being up to date.  Set ret to EIO now
		 * and give up so that our caller doesn't loop forever
		 * on our EAGAINs.
		 */
		if (!extent_buffer_uptodate(tmp))
			ret = -EIO;
		free_extent_buffer(tmp);
	} else {
		ret = PTR_ERR(tmp);
	}

	btrfs_release_path(p);
	return ret;
}

/*
 * helper function for btrfs_search_slot.  This does all of the checks
 * for node-level blocks and does any balancing required based on
 * the ins_len.
 *
 * If no extra work was required, zero is returned.  If we had to
 * drop the path, -EAGAIN is returned and btrfs_search_slot must
 * start over
 */
static int
setup_nodes_for_search(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *p,
		       struct extent_buffer *b, int level, int ins_len,
		       int *write_lock_level)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	if ((p->search_for_split || ins_len > 0) && btrfs_header_nritems(b) >=
	    BTRFS_NODEPTRS_PER_BLOCK(fs_info) - 3) {
		int sret;

		if (*write_lock_level < level + 1) {
			*write_lock_level = level + 1;
			btrfs_release_path(p);
			goto again;
		}

		btrfs_set_path_blocking(p);
		reada_for_balance(fs_info, p, level);
		sret = split_node(trans, root, p, level);

		BUG_ON(sret > 0);
		if (sret) {
			ret = sret;
			goto done;
		}
		b = p->nodes[level];
	} else if (ins_len < 0 && btrfs_header_nritems(b) <
		   BTRFS_NODEPTRS_PER_BLOCK(fs_info) / 2) {
		int sret;

		if (*write_lock_level < level + 1) {
			*write_lock_level = level + 1;
			btrfs_release_path(p);
			goto again;
		}

		btrfs_set_path_blocking(p);
		reada_for_balance(fs_info, p, level);
		sret = balance_level(trans, root, p, level);

		if (sret) {
			ret = sret;
			goto done;
		}
		b = p->nodes[level];
		if (!b) {
			btrfs_release_path(p);
			goto again;
		}
		BUG_ON(btrfs_header_nritems(b) == 1);
	}
	return 0;

again:
	ret = -EAGAIN;
done:
	return ret;
}

int btrfs_find_item(struct btrfs_root *fs_root, struct btrfs_path *path,
		u64 iobjectid, u64 ioff, u8 key_type,
		struct btrfs_key *found_key)
{
	int ret;
	struct btrfs_key key;
	struct extent_buffer *eb;

	ASSERT(path);
	ASSERT(found_key);

	key.type = key_type;
	key.objectid = iobjectid;
	key.offset = ioff;

	ret = btrfs_search_slot(NULL, fs_root, &key, path, 0, 0);
	if (ret < 0)
		return ret;

	eb = path->nodes[0];
	if (ret && path->slots[0] >= btrfs_header_nritems(eb)) {
		ret = btrfs_next_leaf(fs_root, path);
		if (ret)
			return ret;
		eb = path->nodes[0];
	}

	btrfs_item_key_to_cpu(eb, found_key, path->slots[0]);
	if (found_key->type != key.type ||
			found_key->objectid != key.objectid)
		return 1;

	return 0;
}

static struct extent_buffer *btrfs_search_slot_get_root(struct btrfs_root *root,
							struct btrfs_path *p,
							int write_lock_level)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *b;
	int root_lock;
	int level = 0;

	/* We try very hard to do read locks on the root */
	root_lock = BTRFS_READ_LOCK;

	if (p->search_commit_root) {
		/*
		 * The commit roots are read only so we always do read locks,
		 * and we always must hold the commit_root_sem when doing
		 * searches on them, the only exception is send where we don't
		 * want to block transaction commits for a long time, so
		 * we need to clone the commit root in order to avoid races
		 * with transaction commits that create a snapshot of one of
		 * the roots used by a send operation.
		 */
		if (p->need_commit_sem) {
			down_read(&fs_info->commit_root_sem);
			b = btrfs_clone_extent_buffer(root->commit_root);
			up_read(&fs_info->commit_root_sem);
			if (!b)
				return ERR_PTR(-ENOMEM);

		} else {
			b = root->commit_root;
			atomic_inc(&b->refs);
		}
		level = btrfs_header_level(b);
		/*
		 * Ensure that all callers have set skip_locking when
		 * p->search_commit_root = 1.
		 */
		ASSERT(p->skip_locking == 1);

		goto out;
	}

	if (p->skip_locking) {
		b = btrfs_root_node(root);
		level = btrfs_header_level(b);
		goto out;
	}

	/*
	 * If the level is set to maximum, we can skip trying to get the read
	 * lock.
	 */
	if (write_lock_level < BTRFS_MAX_LEVEL) {
		/*
		 * We don't know the level of the root node until we actually
		 * have it read locked
		 */
		b = __btrfs_read_lock_root_node(root, p->recurse);
		level = btrfs_header_level(b);
		if (level > write_lock_level)
			goto out;

		/* Whoops, must trade for write lock */
		btrfs_tree_read_unlock(b);
		free_extent_buffer(b);
	}

	b = btrfs_lock_root_node(root);
	root_lock = BTRFS_WRITE_LOCK;

	/* The level might have changed, check again */
	level = btrfs_header_level(b);

out:
	p->nodes[level] = b;
	if (!p->skip_locking)
		p->locks[level] = root_lock;
	/*
	 * Callers are responsible for dropping b's references.
	 */
	return b;
}


/*
 * btrfs_search_slot - look for a key in a tree and perform necessary
 * modifications to preserve tree invariants.
 *
 * @trans:	Handle of transaction, used when modifying the tree
 * @p:		Holds all btree nodes along the search path
 * @root:	The root node of the tree
 * @key:	The key we are looking for
 * @ins_len:	Indicates purpose of search, for inserts it is 1, for
 *		deletions it's -1. 0 for plain searches
 * @cow:	boolean should CoW operations be performed. Must always be 1
 *		when modifying the tree.
 *
 * If @ins_len > 0, nodes and leaves will be split as we walk down the tree.
 * If @ins_len < 0, nodes will be merged as we walk down the tree (if possible)
 *
 * If @key is found, 0 is returned and you can find the item in the leaf level
 * of the path (level 0)
 *
 * If @key isn't found, 1 is returned and the leaf level of the path (level 0)
 * points to the slot where it should be inserted
 *
 * If an error is encountered while searching the tree a negative error number
 * is returned
 */
int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      const struct btrfs_key *key, struct btrfs_path *p,
		      int ins_len, int cow)
{
	struct extent_buffer *b;
	int slot;
	int ret;
	int err;
	int level;
	int lowest_unlock = 1;
	/* everything at write_lock_level or lower must be write locked */
	int write_lock_level = 0;
	u8 lowest_level = 0;
	int min_write_lock_level;
	int prev_cmp;

	lowest_level = p->lowest_level;
	WARN_ON(lowest_level && ins_len > 0);
	WARN_ON(p->nodes[0] != NULL);
	BUG_ON(!cow && ins_len);

	if (ins_len < 0) {
		lowest_unlock = 2;

		/* when we are removing items, we might have to go up to level
		 * two as we update tree pointers  Make sure we keep write
		 * for those levels as well
		 */
		write_lock_level = 2;
	} else if (ins_len > 0) {
		/*
		 * for inserting items, make sure we have a write lock on
		 * level 1 so we can update keys
		 */
		write_lock_level = 1;
	}

	if (!cow)
		write_lock_level = -1;

	if (cow && (p->keep_locks || p->lowest_level))
		write_lock_level = BTRFS_MAX_LEVEL;

	min_write_lock_level = write_lock_level;

again:
	prev_cmp = -1;
	b = btrfs_search_slot_get_root(root, p, write_lock_level);
	if (IS_ERR(b)) {
		ret = PTR_ERR(b);
		goto done;
	}

	while (b) {
		int dec = 0;

		level = btrfs_header_level(b);

		if (cow) {
			bool last_level = (level == (BTRFS_MAX_LEVEL - 1));

			/*
			 * if we don't really need to cow this block
			 * then we don't want to set the path blocking,
			 * so we test it here
			 */
			if (!should_cow_block(trans, root, b)) {
				trans->dirty = true;
				goto cow_done;
			}

			/*
			 * must have write locks on this node and the
			 * parent
			 */
			if (level > write_lock_level ||
			    (level + 1 > write_lock_level &&
			    level + 1 < BTRFS_MAX_LEVEL &&
			    p->nodes[level + 1])) {
				write_lock_level = level + 1;
				btrfs_release_path(p);
				goto again;
			}

			btrfs_set_path_blocking(p);
			if (last_level)
				err = btrfs_cow_block(trans, root, b, NULL, 0,
						      &b,
						      BTRFS_NESTING_COW);
			else
				err = btrfs_cow_block(trans, root, b,
						      p->nodes[level + 1],
						      p->slots[level + 1], &b,
						      BTRFS_NESTING_COW);
			if (err) {
				ret = err;
				goto done;
			}
		}
cow_done:
		p->nodes[level] = b;
		/*
		 * Leave path with blocking locks to avoid massive
		 * lock context switch, this is made on purpose.
		 */

		/*
		 * we have a lock on b and as long as we aren't changing
		 * the tree, there is no way to for the items in b to change.
		 * It is safe to drop the lock on our parent before we
		 * go through the expensive btree search on b.
		 *
		 * If we're inserting or deleting (ins_len != 0), then we might
		 * be changing slot zero, which may require changing the parent.
		 * So, we can't drop the lock until after we know which slot
		 * we're operating on.
		 */
		if (!ins_len && !p->keep_locks) {
			int u = level + 1;

			if (u < BTRFS_MAX_LEVEL && p->locks[u]) {
				btrfs_tree_unlock_rw(p->nodes[u], p->locks[u]);
				p->locks[u] = 0;
			}
		}

		/*
		 * If btrfs_bin_search returns an exact match (prev_cmp == 0)
		 * we can safely assume the target key will always be in slot 0
		 * on lower levels due to the invariants BTRFS' btree provides,
		 * namely that a btrfs_key_ptr entry always points to the
		 * lowest key in the child node, thus we can skip searching
		 * lower levels
		 */
		if (prev_cmp == 0) {
			slot = 0;
			ret = 0;
		} else {
			ret = btrfs_bin_search(b, key, &slot);
			prev_cmp = ret;
			if (ret < 0)
				goto done;
		}

		if (level == 0) {
			p->slots[level] = slot;
			if (ins_len > 0 &&
			    btrfs_leaf_free_space(b) < ins_len) {
				if (write_lock_level < 1) {
					write_lock_level = 1;
					btrfs_release_path(p);
					goto again;
				}

				btrfs_set_path_blocking(p);
				err = split_leaf(trans, root, key,
						 p, ins_len, ret == 0);

				BUG_ON(err > 0);
				if (err) {
					ret = err;
					goto done;
				}
			}
			if (!p->search_for_split)
				unlock_up(p, level, lowest_unlock,
					  min_write_lock_level, NULL);
			goto done;
		}
		if (ret && slot > 0) {
			dec = 1;
			slot--;
		}
		p->slots[level] = slot;
		err = setup_nodes_for_search(trans, root, p, b, level, ins_len,
					     &write_lock_level);
		if (err == -EAGAIN)
			goto again;
		if (err) {
			ret = err;
			goto done;
		}
		b = p->nodes[level];
		slot = p->slots[level];

		/*
		 * Slot 0 is special, if we change the key we have to update
		 * the parent pointer which means we must have a write lock on
		 * the parent
		 */
		if (slot == 0 && ins_len && write_lock_level < level + 1) {
			write_lock_level = level + 1;
			btrfs_release_path(p);
			goto again;
		}

		unlock_up(p, level, lowest_unlock, min_write_lock_level,
			  &write_lock_level);

		if (level == lowest_level) {
			if (dec)
				p->slots[level]++;
			goto done;
		}

		err = read_block_for_search(root, p, &b, level, slot, key);
		if (err == -EAGAIN)
			goto again;
		if (err) {
			ret = err;
			goto done;
		}

		if (!p->skip_locking) {
			level = btrfs_header_level(b);
			if (level <= write_lock_level) {
				if (!btrfs_try_tree_write_lock(b)) {
					btrfs_set_path_blocking(p);
					btrfs_tree_lock(b);
				}
				p->locks[level] = BTRFS_WRITE_LOCK;
			} else {
				if (!btrfs_tree_read_lock_atomic(b)) {
					btrfs_set_path_blocking(p);
					__btrfs_tree_read_lock(b, BTRFS_NESTING_NORMAL,
							       p->recurse);
				}
				p->locks[level] = BTRFS_READ_LOCK;
			}
			p->nodes[level] = b;
		}
	}
	ret = 1;
done:
	/*
	 * we don't really know what they plan on doing with the path
	 * from here on, so for now just mark it as blocking
	 */
	if (!p->leave_spinning)
		btrfs_set_path_blocking(p);
	if (ret < 0 && !p->skip_release_on_error)
		btrfs_release_path(p);
	return ret;
}

/*
 * Like btrfs_search_slot, this looks for a key in the given tree. It uses the
 * current state of the tree together with the operations recorded in the tree
 * modification log to search for the key in a previous version of this tree, as
 * denoted by the time_seq parameter.
 *
 * Naturally, there is no support for insert, delete or cow operations.
 *
 * The resulting path and return value will be set up as if we called
 * btrfs_search_slot at that point in time with ins_len and cow both set to 0.
 */
int btrfs_search_old_slot(struct btrfs_root *root, const struct btrfs_key *key,
			  struct btrfs_path *p, u64 time_seq)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *b;
	int slot;
	int ret;
	int err;
	int level;
	int lowest_unlock = 1;
	u8 lowest_level = 0;

	lowest_level = p->lowest_level;
	WARN_ON(p->nodes[0] != NULL);

	if (p->search_commit_root) {
		BUG_ON(time_seq);
		return btrfs_search_slot(NULL, root, key, p, 0, 0);
	}

again:
	b = get_old_root(root, time_seq);
	if (!b) {
		ret = -EIO;
		goto done;
	}
	level = btrfs_header_level(b);
	p->locks[level] = BTRFS_READ_LOCK;

	while (b) {
		int dec = 0;

		level = btrfs_header_level(b);
		p->nodes[level] = b;

		/*
		 * we have a lock on b and as long as we aren't changing
		 * the tree, there is no way to for the items in b to change.
		 * It is safe to drop the lock on our parent before we
		 * go through the expensive btree search on b.
		 */
		btrfs_unlock_up_safe(p, level + 1);

		ret = btrfs_bin_search(b, key, &slot);
		if (ret < 0)
			goto done;

		if (level == 0) {
			p->slots[level] = slot;
			unlock_up(p, level, lowest_unlock, 0, NULL);
			goto done;
		}

		if (ret && slot > 0) {
			dec = 1;
			slot--;
		}
		p->slots[level] = slot;
		unlock_up(p, level, lowest_unlock, 0, NULL);

		if (level == lowest_level) {
			if (dec)
				p->slots[level]++;
			goto done;
		}

		err = read_block_for_search(root, p, &b, level, slot, key);
		if (err == -EAGAIN)
			goto again;
		if (err) {
			ret = err;
			goto done;
		}

		level = btrfs_header_level(b);
		if (!btrfs_tree_read_lock_atomic(b)) {
			btrfs_set_path_blocking(p);
			btrfs_tree_read_lock(b);
		}
		b = tree_mod_log_rewind(fs_info, p, b, time_seq);
		if (!b) {
			ret = -ENOMEM;
			goto done;
		}
		p->locks[level] = BTRFS_READ_LOCK;
		p->nodes[level] = b;
	}
	ret = 1;
done:
	if (!p->leave_spinning)
		btrfs_set_path_blocking(p);
	if (ret < 0)
		btrfs_release_path(p);

	return ret;
}

/*
 * helper to use instead of search slot if no exact match is needed but
 * instead the next or previous item should be returned.
 * When find_higher is true, the next higher item is returned, the next lower
 * otherwise.
 * When return_any and find_higher are both true, and no higher item is found,
 * return the next lower instead.
 * When return_any is true and find_higher is false, and no lower item is found,
 * return the next higher instead.
 * It returns 0 if any item is found, 1 if none is found (tree empty), and
 * < 0 on error
 */
int btrfs_search_slot_for_read(struct btrfs_root *root,
			       const struct btrfs_key *key,
			       struct btrfs_path *p, int find_higher,
			       int return_any)
{
	int ret;
	struct extent_buffer *leaf;

again:
	ret = btrfs_search_slot(NULL, root, key, p, 0, 0);
	if (ret <= 0)
		return ret;
	/*
	 * a return value of 1 means the path is at the position where the
	 * item should be inserted. Normally this is the next bigger item,
	 * but in case the previous item is the last in a leaf, path points
	 * to the first free slot in the previous leaf, i.e. at an invalid
	 * item.
	 */
	leaf = p->nodes[0];

	if (find_higher) {
		if (p->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, p);
			if (ret <= 0)
				return ret;
			if (!return_any)
				return 1;
			/*
			 * no higher item found, return the next
			 * lower instead
			 */
			return_any = 0;
			find_higher = 0;
			btrfs_release_path(p);
			goto again;
		}
	} else {
		if (p->slots[0] == 0) {
			ret = btrfs_prev_leaf(root, p);
			if (ret < 0)
				return ret;
			if (!ret) {
				leaf = p->nodes[0];
				if (p->slots[0] == btrfs_header_nritems(leaf))
					p->slots[0]--;
				return 0;
			}
			if (!return_any)
				return 1;
			/*
			 * no lower item found, return the next
			 * higher instead
			 */
			return_any = 0;
			find_higher = 1;
			btrfs_release_path(p);
			goto again;
		} else {
			--p->slots[0];
		}
	}
	return 0;
}

/*
 * adjust the pointers going up the tree, starting at level
 * making sure the right key of each node is points to 'key'.
 * This is used after shifting pointers to the left, so it stops
 * fixing up pointers when a given leaf/node is not in slot 0 of the
 * higher levels
 *
 */
static void fixup_low_keys(struct btrfs_path *path,
			   struct btrfs_disk_key *key, int level)
{
	int i;
	struct extent_buffer *t;
	int ret;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		int tslot = path->slots[i];

		if (!path->nodes[i])
			break;
		t = path->nodes[i];
		ret = tree_mod_log_insert_key(t, tslot, MOD_LOG_KEY_REPLACE,
				GFP_ATOMIC);
		BUG_ON(ret < 0);
		btrfs_set_node_key(t, key, tslot);
		btrfs_mark_buffer_dirty(path->nodes[i]);
		if (tslot != 0)
			break;
	}
}

/*
 * update item key.
 *
 * This function isn't completely safe. It's the caller's responsibility
 * that the new key won't break the order
 */
void btrfs_set_item_key_safe(struct btrfs_fs_info *fs_info,
			     struct btrfs_path *path,
			     const struct btrfs_key *new_key)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *eb;
	int slot;

	eb = path->nodes[0];
	slot = path->slots[0];
	if (slot > 0) {
		btrfs_item_key(eb, &disk_key, slot - 1);
		if (unlikely(comp_keys(&disk_key, new_key) >= 0)) {
			btrfs_crit(fs_info,
		"slot %u key (%llu %u %llu) new key (%llu %u %llu)",
				   slot, btrfs_disk_key_objectid(&disk_key),
				   btrfs_disk_key_type(&disk_key),
				   btrfs_disk_key_offset(&disk_key),
				   new_key->objectid, new_key->type,
				   new_key->offset);
			btrfs_print_leaf(eb);
			BUG();
		}
	}
	if (slot < btrfs_header_nritems(eb) - 1) {
		btrfs_item_key(eb, &disk_key, slot + 1);
		if (unlikely(comp_keys(&disk_key, new_key) <= 0)) {
			btrfs_crit(fs_info,
		"slot %u key (%llu %u %llu) new key (%llu %u %llu)",
				   slot, btrfs_disk_key_objectid(&disk_key),
				   btrfs_disk_key_type(&disk_key),
				   btrfs_disk_key_offset(&disk_key),
				   new_key->objectid, new_key->type,
				   new_key->offset);
			btrfs_print_leaf(eb);
			BUG();
		}
	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(eb, &disk_key, slot);
	btrfs_mark_buffer_dirty(eb);
	if (slot == 0)
		fixup_low_keys(path, &disk_key, 1);
}

/*
 * Check key order of two sibling extent buffers.
 *
 * Return true if something is wrong.
 * Return false if everything is fine.
 *
 * Tree-checker only works inside one tree block, thus the following
 * corruption can not be detected by tree-checker:
 *
 * Leaf @left			| Leaf @right
 * --------------------------------------------------------------
 * | 1 | 2 | 3 | 4 | 5 | f6 |   | 7 | 8 |
 *
 * Key f6 in leaf @left itself is valid, but not valid when the next
 * key in leaf @right is 7.
 * This can only be checked at tree block merge time.
 * And since tree checker has ensured all key order in each tree block
 * is correct, we only need to bother the last key of @left and the first
 * key of @right.
 */
static bool check_sibling_keys(struct extent_buffer *left,
			       struct extent_buffer *right)
{
	struct btrfs_key left_last;
	struct btrfs_key right_first;
	int level = btrfs_header_level(left);
	int nr_left = btrfs_header_nritems(left);
	int nr_right = btrfs_header_nritems(right);

	/* No key to check in one of the tree blocks */
	if (!nr_left || !nr_right)
		return false;

	if (level) {
		btrfs_node_key_to_cpu(left, &left_last, nr_left - 1);
		btrfs_node_key_to_cpu(right, &right_first, 0);
	} else {
		btrfs_item_key_to_cpu(left, &left_last, nr_left - 1);
		btrfs_item_key_to_cpu(right, &right_first, 0);
	}

	if (btrfs_comp_cpu_keys(&left_last, &right_first) >= 0) {
		btrfs_crit(left->fs_info,
"bad key order, sibling blocks, left last (%llu %u %llu) right first (%llu %u %llu)",
			   left_last.objectid, left_last.type,
			   left_last.offset, right_first.objectid,
			   right_first.type, right_first.offset);
		return true;
	}
	return false;
}

/*
 * try to push data from one node into the next node left in the
 * tree.
 *
 * returns 0 if some ptrs were pushed left, < 0 if there was some horrible
 * error, and > 0 if there was no room in the left hand block.
 */
static int push_node_left(struct btrfs_trans_handle *trans,
			  struct extent_buffer *dst,
			  struct extent_buffer *src, int empty)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int push_items = 0;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(fs_info) - dst_nritems;
	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	if (!empty && src_nritems <= 8)
		return 1;

	if (push_items <= 0)
		return 1;

	if (empty) {
		push_items = min(src_nritems, push_items);
		if (push_items < src_nritems) {
			/* leave at least 8 pointers in the node if
			 * we aren't going to empty it
			 */
			if (src_nritems - push_items < 8) {
				if (push_items <= 8)
					return 1;
				push_items -= 8;
			}
		}
	} else
		push_items = min(src_nritems - 8, push_items);

	/* dst is the left eb, src is the middle eb */
	if (check_sibling_keys(dst, src)) {
		ret = -EUCLEAN;
		btrfs_abort_transaction(trans, ret);
		return ret;
	}
	ret = tree_mod_log_eb_copy(dst, src, dst_nritems, 0, push_items);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}
	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(dst_nritems),
			   btrfs_node_key_ptr_offset(0),
			   push_items * sizeof(struct btrfs_key_ptr));

	if (push_items < src_nritems) {
		/*
		 * Don't call tree_mod_log_insert_move here, key removal was
		 * already fully logged by tree_mod_log_eb_copy above.
		 */
		memmove_extent_buffer(src, btrfs_node_key_ptr_offset(0),
				      btrfs_node_key_ptr_offset(push_items),
				      (src_nritems - push_items) *
				      sizeof(struct btrfs_key_ptr));
	}
	btrfs_set_header_nritems(src, src_nritems - push_items);
	btrfs_set_header_nritems(dst, dst_nritems + push_items);
	btrfs_mark_buffer_dirty(src);
	btrfs_mark_buffer_dirty(dst);

	return ret;
}

/*
 * try to push data from one node into the next node right in the
 * tree.
 *
 * returns 0 if some ptrs were pushed, < 0 if there was some horrible
 * error, and > 0 if there was no room in the right hand block.
 *
 * this will  only push up to 1/2 the contents of the left node over
 */
static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct extent_buffer *dst,
			      struct extent_buffer *src)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int push_items = 0;
	int max_push;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(fs_info) - dst_nritems;
	if (push_items <= 0)
		return 1;

	if (src_nritems < 4)
		return 1;

	max_push = src_nritems / 2 + 1;
	/* don't try to empty the node */
	if (max_push >= src_nritems)
		return 1;

	if (max_push < push_items)
		push_items = max_push;

	/* dst is the right eb, src is the middle eb */
	if (check_sibling_keys(src, dst)) {
		ret = -EUCLEAN;
		btrfs_abort_transaction(trans, ret);
		return ret;
	}
	ret = tree_mod_log_insert_move(dst, push_items, 0, dst_nritems);
	BUG_ON(ret < 0);
	memmove_extent_buffer(dst, btrfs_node_key_ptr_offset(push_items),
				      btrfs_node_key_ptr_offset(0),
				      (dst_nritems) *
				      sizeof(struct btrfs_key_ptr));

	ret = tree_mod_log_eb_copy(dst, src, 0, src_nritems - push_items,
				   push_items);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}
	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(src_nritems - push_items),
			   push_items * sizeof(struct btrfs_key_ptr));

	btrfs_set_header_nritems(src, src_nritems - push_items);
	btrfs_set_header_nritems(dst, dst_nritems + push_items);

	btrfs_mark_buffer_dirty(src);
	btrfs_mark_buffer_dirty(dst);

	return ret;
}

/*
 * helper function to insert a new root level in the tree.
 * A new node is allocated, and a single item is inserted to
 * point to the existing root
 *
 * returns zero on success or < 0 on failure.
 */
static noinline int insert_new_root(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_path *path, int level)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 lower_gen;
	struct extent_buffer *lower;
	struct extent_buffer *c;
	struct extent_buffer *old;
	struct btrfs_disk_key lower_key;
	int ret;

	BUG_ON(path->nodes[level]);
	BUG_ON(path->nodes[level-1] != root->node);

	lower = path->nodes[level-1];
	if (level == 1)
		btrfs_item_key(lower, &lower_key, 0);
	else
		btrfs_node_key(lower, &lower_key, 0);

	c = alloc_tree_block_no_bg_flush(trans, root, 0, &lower_key, level,
					 root->node->start, 0,
					 BTRFS_NESTING_NEW_ROOT);
	if (IS_ERR(c))
		return PTR_ERR(c);

	root_add_used(root, fs_info->nodesize);

	btrfs_set_header_nritems(c, 1);
	btrfs_set_node_key(c, &lower_key, 0);
	btrfs_set_node_blockptr(c, 0, lower->start);
	lower_gen = btrfs_header_generation(lower);
	WARN_ON(lower_gen != trans->transid);

	btrfs_set_node_ptr_generation(c, 0, lower_gen);

	btrfs_mark_buffer_dirty(c);

	old = root->node;
	ret = tree_mod_log_insert_root(root->node, c, 0);
	BUG_ON(ret < 0);
	rcu_assign_pointer(root->node, c);

	/* the super has an extra ref to root->node */
	free_extent_buffer(old);

	add_root_to_dirty_list(root);
	atomic_inc(&c->refs);
	path->nodes[level] = c;
	path->locks[level] = BTRFS_WRITE_LOCK_BLOCKING;
	path->slots[level] = 0;
	return 0;
}

/*
 * worker function to insert a single pointer in a node.
 * the node should have enough room for the pointer already
 *
 * slot and level indicate where you want the key to go, and
 * blocknr is the block the key points to.
 */
static void insert_ptr(struct btrfs_trans_handle *trans,
		       struct btrfs_path *path,
		       struct btrfs_disk_key *key, u64 bytenr,
		       int slot, int level)
{
	struct extent_buffer *lower;
	int nritems;
	int ret;

	BUG_ON(!path->nodes[level]);
	btrfs_assert_tree_locked(path->nodes[level]);
	lower = path->nodes[level];
	nritems = btrfs_header_nritems(lower);
	BUG_ON(slot > nritems);
	BUG_ON(nritems == BTRFS_NODEPTRS_PER_BLOCK(trans->fs_info));
	if (slot != nritems) {
		if (level) {
			ret = tree_mod_log_insert_move(lower, slot + 1, slot,
					nritems - slot);
			BUG_ON(ret < 0);
		}
		memmove_extent_buffer(lower,
			      btrfs_node_key_ptr_offset(slot + 1),
			      btrfs_node_key_ptr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_key_ptr));
	}
	if (level) {
		ret = tree_mod_log_insert_key(lower, slot, MOD_LOG_KEY_ADD,
				GFP_NOFS);
		BUG_ON(ret < 0);
	}
	btrfs_set_node_key(lower, key, slot);
	btrfs_set_node_blockptr(lower, slot, bytenr);
	WARN_ON(trans->transid == 0);
	btrfs_set_node_ptr_generation(lower, slot, trans->transid);
	btrfs_set_header_nritems(lower, nritems + 1);
	btrfs_mark_buffer_dirty(lower);
}

/*
 * split the node at the specified level in path in two.
 * The path is corrected to point to the appropriate node after the split
 *
 * Before splitting this tries to make some room in the node by pushing
 * left and right, if either one works, it returns right away.
 *
 * returns 0 on success and < 0 on failure
 */
static noinline int split_node(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_path *path, int level)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *c;
	struct extent_buffer *split;
	struct btrfs_disk_key disk_key;
	int mid;
	int ret;
	u32 c_nritems;

	c = path->nodes[level];
	WARN_ON(btrfs_header_generation(c) != trans->transid);
	if (c == root->node) {
		/*
		 * trying to split the root, lets make a new one
		 *
		 * tree mod log: We don't log_removal old root in
		 * insert_new_root, because that root buffer will be kept as a
		 * normal node. We are going to log removal of half of the
		 * elements below with tree_mod_log_eb_copy. We're holding a
		 * tree lock on the buffer, which is why we cannot race with
		 * other tree_mod_log users.
		 */
		ret = insert_new_root(trans, root, path, level + 1);
		if (ret)
			return ret;
	} else {
		ret = push_nodes_for_insert(trans, root, path, level);
		c = path->nodes[level];
		if (!ret && btrfs_header_nritems(c) <
		    BTRFS_NODEPTRS_PER_BLOCK(fs_info) - 3)
			return 0;
		if (ret < 0)
			return ret;
	}

	c_nritems = btrfs_header_nritems(c);
	mid = (c_nritems + 1) / 2;
	btrfs_node_key(c, &disk_key, mid);

	split = alloc_tree_block_no_bg_flush(trans, root, 0, &disk_key, level,
					     c->start, 0, BTRFS_NESTING_SPLIT);
	if (IS_ERR(split))
		return PTR_ERR(split);

	root_add_used(root, fs_info->nodesize);
	ASSERT(btrfs_header_level(c) == level);

	ret = tree_mod_log_eb_copy(split, c, 0, mid, c_nritems - mid);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}
	copy_extent_buffer(split, c,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(mid),
			   (c_nritems - mid) * sizeof(struct btrfs_key_ptr));
	btrfs_set_header_nritems(split, c_nritems - mid);
	btrfs_set_header_nritems(c, mid);
	ret = 0;

	btrfs_mark_buffer_dirty(c);
	btrfs_mark_buffer_dirty(split);

	insert_ptr(trans, path, &disk_key, split->start,
		   path->slots[level + 1] + 1, level + 1);

	if (path->slots[level] >= mid) {
		path->slots[level] -= mid;
		btrfs_tree_unlock(c);
		free_extent_buffer(c);
		path->nodes[level] = split;
		path->slots[level + 1] += 1;
	} else {
		btrfs_tree_unlock(split);
		free_extent_buffer(split);
	}
	return ret;
}

/*
 * how many bytes are required to store the items in a leaf.  start
 * and nr indicate which items in the leaf to check.  This totals up the
 * space used both by the item structs and the item data
 */
static int leaf_space_used(struct extent_buffer *l, int start, int nr)
{
	struct btrfs_item *start_item;
	struct btrfs_item *end_item;
	int data_len;
	int nritems = btrfs_header_nritems(l);
	int end = min(nritems, start + nr) - 1;

	if (!nr)
		return 0;
	start_item = btrfs_item_nr(start);
	end_item = btrfs_item_nr(end);
	data_len = btrfs_item_offset(l, start_item) +
		   btrfs_item_size(l, start_item);
	data_len = data_len - btrfs_item_offset(l, end_item);
	data_len += sizeof(struct btrfs_item) * nr;
	WARN_ON(data_len < 0);
	return data_len;
}

/*
 * The space between the end of the leaf items and
 * the start of the leaf data.  IOW, how much room
 * the leaf has left for both items and data
 */
noinline int btrfs_leaf_free_space(struct extent_buffer *leaf)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	int nritems = btrfs_header_nritems(leaf);
	int ret;

	ret = BTRFS_LEAF_DATA_SIZE(fs_info) - leaf_space_used(leaf, 0, nritems);
	if (ret < 0) {
		btrfs_crit(fs_info,
			   "leaf free space ret %d, leaf data size %lu, used %d nritems %d",
			   ret,
			   (unsigned long) BTRFS_LEAF_DATA_SIZE(fs_info),
			   leaf_space_used(leaf, 0, nritems), nritems);
	}
	return ret;
}

/*
 * min slot controls the lowest index we're willing to push to the
 * right.  We'll push up to and including min_slot, but no lower
 */
static noinline int __push_leaf_right(struct btrfs_path *path,
				      int data_size, int empty,
				      struct extent_buffer *right,
				      int free_space, u32 left_nritems,
				      u32 min_slot)
{
	struct btrfs_fs_info *fs_info = right->fs_info;
	struct extent_buffer *left = path->nodes[0];
	struct extent_buffer *upper = path->nodes[1];
	struct btrfs_map_token token;
	struct btrfs_disk_key disk_key;
	int slot;
	u32 i;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 nr;
	u32 right_nritems;
	u32 data_end;
	u32 this_item_size;

	if (empty)
		nr = 0;
	else
		nr = max_t(u32, 1, min_slot);

	if (path->slots[0] >= left_nritems)
		push_space += data_size;

	slot = path->slots[1];
	i = left_nritems - 1;
	while (i >= nr) {
		item = btrfs_item_nr(i);

		if (!empty && push_items > 0) {
			if (path->slots[0] > i)
				break;
			if (path->slots[0] == i) {
				int space = btrfs_leaf_free_space(left);

				if (space + push_space * 2 > free_space)
					break;
			}
		}

		if (path->slots[0] == i)
			push_space += data_size;

		this_item_size = btrfs_item_size(left, item);
		if (this_item_size + sizeof(*item) + push_space > free_space)
			break;

		push_items++;
		push_space += this_item_size + sizeof(*item);
		if (i == 0)
			break;
		i--;
	}

	if (push_items == 0)
		goto out_unlock;

	WARN_ON(!empty && push_items == left_nritems);

	/* push left to right */
	right_nritems = btrfs_header_nritems(right);

	push_space = btrfs_item_end_nr(left, left_nritems - push_items);
	push_space -= leaf_data_end(left);

	/* make room in the right data area */
	data_end = leaf_data_end(right);
	memmove_extent_buffer(right,
			      BTRFS_LEAF_DATA_OFFSET + data_end - push_space,
			      BTRFS_LEAF_DATA_OFFSET + data_end,
			      BTRFS_LEAF_DATA_SIZE(fs_info) - data_end);

	/* copy from the left data area */
	copy_extent_buffer(right, left, BTRFS_LEAF_DATA_OFFSET +
		     BTRFS_LEAF_DATA_SIZE(fs_info) - push_space,
		     BTRFS_LEAF_DATA_OFFSET + leaf_data_end(left),
		     push_space);

	memmove_extent_buffer(right, btrfs_item_nr_offset(push_items),
			      btrfs_item_nr_offset(0),
			      right_nritems * sizeof(struct btrfs_item));

	/* copy the items from left to right */
	copy_extent_buffer(right, left, btrfs_item_nr_offset(0),
		   btrfs_item_nr_offset(left_nritems - push_items),
		   push_items * sizeof(struct btrfs_item));

	/* update the item pointers */
	btrfs_init_map_token(&token, right);
	right_nritems += push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(fs_info);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(i);
		push_space -= btrfs_token_item_size(&token, item);
		btrfs_set_token_item_offset(&token, item, push_space);
	}

	left_nritems -= push_items;
	btrfs_set_header_nritems(left, left_nritems);

	if (left_nritems)
		btrfs_mark_buffer_dirty(left);
	else
		btrfs_clean_tree_block(left);

	btrfs_mark_buffer_dirty(right);

	btrfs_item_key(right, &disk_key, 0);
	btrfs_set_node_key(upper, &disk_key, slot + 1);
	btrfs_mark_buffer_dirty(upper);

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] >= left_nritems) {
		path->slots[0] -= left_nritems;
		if (btrfs_header_nritems(path->nodes[0]) == 0)
			btrfs_clean_tree_block(path->nodes[0]);
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = right;
		path->slots[1] += 1;
	} else {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	return 0;

out_unlock:
	btrfs_tree_unlock(right);
	free_extent_buffer(right);
	return 1;
}

/*
 * push some data in the path leaf to the right, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 *
 * returns 1 if the push failed because the other node didn't have enough
 * room, 0 if everything worked out and < 0 if there were major errors.
 *
 * this will push starting from min_slot to the end of the leaf.  It won't
 * push any slot lower than min_slot
 */
static int push_leaf_right(struct btrfs_trans_handle *trans, struct btrfs_root
			   *root, struct btrfs_path *path,
			   int min_data_size, int data_size,
			   int empty, u32 min_slot)
{
	struct extent_buffer *left = path->nodes[0];
	struct extent_buffer *right;
	struct extent_buffer *upper;
	int slot;
	int free_space;
	u32 left_nritems;
	int ret;

	if (!path->nodes[1])
		return 1;

	slot = path->slots[1];
	upper = path->nodes[1];
	if (slot >= btrfs_header_nritems(upper) - 1)
		return 1;

	btrfs_assert_tree_locked(path->nodes[1]);

	right = btrfs_read_node_slot(upper, slot + 1);
	/*
	 * slot + 1 is not valid or we fail to read the right node,
	 * no big deal, just return.
	 */
	if (IS_ERR(right))
		return 1;

	__btrfs_tree_lock(right, BTRFS_NESTING_RIGHT);
	btrfs_set_lock_blocking_write(right);

	free_space = btrfs_leaf_free_space(right);
	if (free_space < data_size)
		goto out_unlock;

	/* cow and double check */
	ret = btrfs_cow_block(trans, root, right, upper,
			      slot + 1, &right, BTRFS_NESTING_RIGHT_COW);
	if (ret)
		goto out_unlock;

	free_space = btrfs_leaf_free_space(right);
	if (free_space < data_size)
		goto out_unlock;

	left_nritems = btrfs_header_nritems(left);
	if (left_nritems == 0)
		goto out_unlock;

	if (check_sibling_keys(left, right)) {
		ret = -EUCLEAN;
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
		return ret;
	}
	if (path->slots[0] == left_nritems && !empty) {
		/* Key greater than all keys in the leaf, right neighbor has
		 * enough room for it and we're not emptying our leaf to delete
		 * it, therefore use right neighbor to insert the new item and
		 * no need to touch/dirty our left leaf. */
		btrfs_tree_unlock(left);
		free_extent_buffer(left);
		path->nodes[0] = right;
		path->slots[0] = 0;
		path->slots[1]++;
		return 0;
	}

	return __push_leaf_right(path, min_data_size, empty,
				right, free_space, left_nritems, min_slot);
out_unlock:
	btrfs_tree_unlock(right);
	free_extent_buffer(right);
	return 1;
}

/*
 * push some data in the path leaf to the left, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 *
 * max_slot can put a limit on how far into the leaf we'll push items.  The
 * item at 'max_slot' won't be touched.  Use (u32)-1 to make us do all the
 * items
 */
static noinline int __push_leaf_left(struct btrfs_path *path, int data_size,
				     int empty, struct extent_buffer *left,
				     int free_space, u32 right_nritems,
				     u32 max_slot)
{
	struct btrfs_fs_info *fs_info = left->fs_info;
	struct btrfs_disk_key disk_key;
	struct extent_buffer *right = path->nodes[0];
	int i;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 old_left_nritems;
	u32 nr;
	int ret = 0;
	u32 this_item_size;
	u32 old_left_item_size;
	struct btrfs_map_token token;

	if (empty)
		nr = min(right_nritems, max_slot);
	else
		nr = min(right_nritems - 1, max_slot);

	for (i = 0; i < nr; i++) {
		item = btrfs_item_nr(i);

		if (!empty && push_items > 0) {
			if (path->slots[0] < i)
				break;
			if (path->slots[0] == i) {
				int space = btrfs_leaf_free_space(right);

				if (space + push_space * 2 > free_space)
					break;
			}
		}

		if (path->slots[0] == i)
			push_space += data_size;

		this_item_size = btrfs_item_size(right, item);
		if (this_item_size + sizeof(*item) + push_space > free_space)
			break;

		push_items++;
		push_space += this_item_size + sizeof(*item);
	}

	if (push_items == 0) {
		ret = 1;
		goto out;
	}
	WARN_ON(!empty && push_items == btrfs_header_nritems(right));

	/* push data from right to left */
	copy_extent_buffer(left, right,
			   btrfs_item_nr_offset(btrfs_header_nritems(left)),
			   btrfs_item_nr_offset(0),
			   push_items * sizeof(struct btrfs_item));

	push_space = BTRFS_LEAF_DATA_SIZE(fs_info) -
		     btrfs_item_offset_nr(right, push_items - 1);

	copy_extent_buffer(left, right, BTRFS_LEAF_DATA_OFFSET +
		     leaf_data_end(left) - push_space,
		     BTRFS_LEAF_DATA_OFFSET +
		     btrfs_item_offset_nr(right, push_items - 1),
		     push_space);
	old_left_nritems = btrfs_header_nritems(left);
	BUG_ON(old_left_nritems <= 0);

	btrfs_init_map_token(&token, left);
	old_left_item_size = btrfs_item_offset_nr(left, old_left_nritems - 1);
	for (i = old_left_nritems; i < old_left_nritems + push_items; i++) {
		u32 ioff;

		item = btrfs_item_nr(i);

		ioff = btrfs_token_item_offset(&token, item);
		btrfs_set_token_item_offset(&token, item,
		      ioff - (BTRFS_LEAF_DATA_SIZE(fs_info) - old_left_item_size));
	}
	btrfs_set_header_nritems(left, old_left_nritems + push_items);

	/* fixup right node */
	if (push_items > right_nritems)
		WARN(1, KERN_CRIT "push items %d nr %u\n", push_items,
		       right_nritems);

	if (push_items < right_nritems) {
		push_space = btrfs_item_offset_nr(right, push_items - 1) -
						  leaf_data_end(right);
		memmove_extent_buffer(right, BTRFS_LEAF_DATA_OFFSET +
				      BTRFS_LEAF_DATA_SIZE(fs_info) - push_space,
				      BTRFS_LEAF_DATA_OFFSET +
				      leaf_data_end(right), push_space);

		memmove_extent_buffer(right, btrfs_item_nr_offset(0),
			      btrfs_item_nr_offset(push_items),
			     (btrfs_header_nritems(right) - push_items) *
			     sizeof(struct btrfs_item));
	}

	btrfs_init_map_token(&token, right);
	right_nritems -= push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(fs_info);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(i);

		push_space = push_space - btrfs_token_item_size(&token, item);
		btrfs_set_token_item_offset(&token, item, push_space);
	}

	btrfs_mark_buffer_dirty(left);
	if (right_nritems)
		btrfs_mark_buffer_dirty(right);
	else
		btrfs_clean_tree_block(right);

	btrfs_item_key(right, &disk_key, 0);
	fixup_low_keys(path, &disk_key, 1);

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] < push_items) {
		path->slots[0] += old_left_nritems;
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = left;
		path->slots[1] -= 1;
	} else {
		btrfs_tree_unlock(left);
		free_extent_buffer(left);
		path->slots[0] -= push_items;
	}
	BUG_ON(path->slots[0] < 0);
	return ret;
out:
	btrfs_tree_unlock(left);
	free_extent_buffer(left);
	return ret;
}

/*
 * push some data in the path leaf to the left, trying to free up at
 * least data_size bytes.  returns zero if the push worked, nonzero otherwise
 *
 * max_slot can put a limit on how far into the leaf we'll push items.  The
 * item at 'max_slot' won't be touched.  Use (u32)-1 to make us push all the
 * items
 */
static int push_leaf_left(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, int min_data_size,
			  int data_size, int empty, u32 max_slot)
{
	struct extent_buffer *right = path->nodes[0];
	struct extent_buffer *left;
	int slot;
	int free_space;
	u32 right_nritems;
	int ret = 0;

	slot = path->slots[1];
	if (slot == 0)
		return 1;
	if (!path->nodes[1])
		return 1;

	right_nritems = btrfs_header_nritems(right);
	if (right_nritems == 0)
		return 1;

	btrfs_assert_tree_locked(path->nodes[1]);

	left = btrfs_read_node_slot(path->nodes[1], slot - 1);
	/*
	 * slot - 1 is not valid or we fail to read the left node,
	 * no big deal, just return.
	 */
	if (IS_ERR(left))
		return 1;

	__btrfs_tree_lock(left, BTRFS_NESTING_LEFT);
	btrfs_set_lock_blocking_write(left);

	free_space = btrfs_leaf_free_space(left);
	if (free_space < data_size) {
		ret = 1;
		goto out;
	}

	/* cow and double check */
	ret = btrfs_cow_block(trans, root, left,
			      path->nodes[1], slot - 1, &left,
			      BTRFS_NESTING_LEFT_COW);
	if (ret) {
		/* we hit -ENOSPC, but it isn't fatal here */
		if (ret == -ENOSPC)
			ret = 1;
		goto out;
	}

	free_space = btrfs_leaf_free_space(left);
	if (free_space < data_size) {
		ret = 1;
		goto out;
	}

	if (check_sibling_keys(left, right)) {
		ret = -EUCLEAN;
		goto out;
	}
	return __push_leaf_left(path, min_data_size,
			       empty, left, free_space, right_nritems,
			       max_slot);
out:
	btrfs_tree_unlock(left);
	free_extent_buffer(left);
	return ret;
}

/*
 * split the path's leaf in two, making sure there is at least data_size
 * available for the resulting leaf level of the path.
 */
static noinline void copy_for_split(struct btrfs_trans_handle *trans,
				    struct btrfs_path *path,
				    struct extent_buffer *l,
				    struct extent_buffer *right,
				    int slot, int mid, int nritems)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int data_copy_size;
	int rt_data_off;
	int i;
	struct btrfs_disk_key disk_key;
	struct btrfs_map_token token;

	nritems = nritems - mid;
	btrfs_set_header_nritems(right, nritems);
	data_copy_size = btrfs_item_end_nr(l, mid) - leaf_data_end(l);

	copy_extent_buffer(right, l, btrfs_item_nr_offset(0),
			   btrfs_item_nr_offset(mid),
			   nritems * sizeof(struct btrfs_item));

	copy_extent_buffer(right, l,
		     BTRFS_LEAF_DATA_OFFSET + BTRFS_LEAF_DATA_SIZE(fs_info) -
		     data_copy_size, BTRFS_LEAF_DATA_OFFSET +
		     leaf_data_end(l), data_copy_size);

	rt_data_off = BTRFS_LEAF_DATA_SIZE(fs_info) - btrfs_item_end_nr(l, mid);

	btrfs_init_map_token(&token, right);
	for (i = 0; i < nritems; i++) {
		struct btrfs_item *item = btrfs_item_nr(i);
		u32 ioff;

		ioff = btrfs_token_item_offset(&token, item);
		btrfs_set_token_item_offset(&token, item, ioff + rt_data_off);
	}

	btrfs_set_header_nritems(l, mid);
	btrfs_item_key(right, &disk_key, 0);
	insert_ptr(trans, path, &disk_key, right->start, path->slots[1] + 1, 1);

	btrfs_mark_buffer_dirty(right);
	btrfs_mark_buffer_dirty(l);
	BUG_ON(path->slots[0] != slot);

	if (mid <= slot) {
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = right;
		path->slots[0] -= mid;
		path->slots[1] += 1;
	} else {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}

	BUG_ON(path->slots[0] < 0);
}

/*
 * double splits happen when we need to insert a big item in the middle
 * of a leaf.  A double split can leave us with 3 mostly empty leaves:
 * leaf: [ slots 0 - N] [ our target ] [ N + 1 - total in leaf ]
 *          A                 B                 C
 *
 * We avoid this by trying to push the items on either side of our target
 * into the adjacent leaves.  If all goes well we can avoid the double split
 * completely.
 */
static noinline int push_for_double_split(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  int data_size)
{
	int ret;
	int progress = 0;
	int slot;
	u32 nritems;
	int space_needed = data_size;

	slot = path->slots[0];
	if (slot < btrfs_header_nritems(path->nodes[0]))
		space_needed -= btrfs_leaf_free_space(path->nodes[0]);

	/*
	 * try to push all the items after our slot into the
	 * right leaf
	 */
	ret = push_leaf_right(trans, root, path, 1, space_needed, 0, slot);
	if (ret < 0)
		return ret;

	if (ret == 0)
		progress++;

	nritems = btrfs_header_nritems(path->nodes[0]);
	/*
	 * our goal is to get our slot at the start or end of a leaf.  If
	 * we've done so we're done
	 */
	if (path->slots[0] == 0 || path->slots[0] == nritems)
		return 0;

	if (btrfs_leaf_free_space(path->nodes[0]) >= data_size)
		return 0;

	/* try to push all the items before our slot into the next leaf */
	slot = path->slots[0];
	space_needed = data_size;
	if (slot > 0)
		space_needed -= btrfs_leaf_free_space(path->nodes[0]);
	ret = push_leaf_left(trans, root, path, 1, space_needed, 0, slot);
	if (ret < 0)
		return ret;

	if (ret == 0)
		progress++;

	if (progress)
		return 0;
	return 1;
}

/*
 * split the path's leaf in two, making sure there is at least data_size
 * available for the resulting leaf level of the path.
 *
 * returns 0 if all went well and < 0 on failure.
 */
static noinline int split_leaf(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       const struct btrfs_key *ins_key,
			       struct btrfs_path *path, int data_size,
			       int extend)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *l;
	u32 nritems;
	int mid;
	int slot;
	struct extent_buffer *right;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret = 0;
	int wret;
	int split;
	int num_doubles = 0;
	int tried_avoid_double = 0;

	l = path->nodes[0];
	slot = path->slots[0];
	if (extend && data_size + btrfs_item_size_nr(l, slot) +
	    sizeof(struct btrfs_item) > BTRFS_LEAF_DATA_SIZE(fs_info))
		return -EOVERFLOW;

	/* first try to make some room by pushing left and right */
	if (data_size && path->nodes[1]) {
		int space_needed = data_size;

		if (slot < btrfs_header_nritems(l))
			space_needed -= btrfs_leaf_free_space(l);

		wret = push_leaf_right(trans, root, path, space_needed,
				       space_needed, 0, 0);
		if (wret < 0)
			return wret;
		if (wret) {
			space_needed = data_size;
			if (slot > 0)
				space_needed -= btrfs_leaf_free_space(l);
			wret = push_leaf_left(trans, root, path, space_needed,
					      space_needed, 0, (u32)-1);
			if (wret < 0)
				return wret;
		}
		l = path->nodes[0];

		/* did the pushes work? */
		if (btrfs_leaf_free_space(l) >= data_size)
			return 0;
	}

	if (!path->nodes[1]) {
		ret = insert_new_root(trans, root, path, 1);
		if (ret)
			return ret;
	}
again:
	split = 1;
	l = path->nodes[0];
	slot = path->slots[0];
	nritems = btrfs_header_nritems(l);
	mid = (nritems + 1) / 2;

	if (mid <= slot) {
		if (nritems == 1 ||
		    leaf_space_used(l, mid, nritems - mid) + data_size >
			BTRFS_LEAF_DATA_SIZE(fs_info)) {
			if (slot >= nritems) {
				split = 0;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size > BTRFS_LEAF_DATA_SIZE(fs_info)) {
					if (data_size && !tried_avoid_double)
						goto push_for_double;
					split = 2;
				}
			}
		}
	} else {
		if (leaf_space_used(l, 0, mid) + data_size >
			BTRFS_LEAF_DATA_SIZE(fs_info)) {
			if (!extend && data_size && slot == 0) {
				split = 0;
			} else if ((extend || !data_size) && slot == 0) {
				mid = 1;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size > BTRFS_LEAF_DATA_SIZE(fs_info)) {
					if (data_size && !tried_avoid_double)
						goto push_for_double;
					split = 2;
				}
			}
		}
	}

	if (split == 0)
		btrfs_cpu_key_to_disk(&disk_key, ins_key);
	else
		btrfs_item_key(l, &disk_key, mid);

	/*
	 * We have to about BTRFS_NESTING_NEW_ROOT here if we've done a double
	 * split, because we're only allowed to have MAX_LOCKDEP_SUBCLASSES
	 * subclasses, which is 8 at the time of this patch, and we've maxed it
	 * out.  In the future we could add a
	 * BTRFS_NESTING_SPLIT_THE_SPLITTENING if we need to, but for now just
	 * use BTRFS_NESTING_NEW_ROOT.
	 */
	right = alloc_tree_block_no_bg_flush(trans, root, 0, &disk_key, 0,
					     l->start, 0, num_doubles ?
					     BTRFS_NESTING_NEW_ROOT :
					     BTRFS_NESTING_SPLIT);
	if (IS_ERR(right))
		return PTR_ERR(right);

	root_add_used(root, fs_info->nodesize);

	if (split == 0) {
		if (mid <= slot) {
			btrfs_set_header_nritems(right, 0);
			insert_ptr(trans, path, &disk_key,
				   right->start, path->slots[1] + 1, 1);
			btrfs_tree_unlock(path->nodes[0]);
			free_extent_buffer(path->nodes[0]);
			path->nodes[0] = right;
			path->slots[0] = 0;
			path->slots[1] += 1;
		} else {
			btrfs_set_header_nritems(right, 0);
			insert_ptr(trans, path, &disk_key,
				   right->start, path->slots[1], 1);
			btrfs_tree_unlock(path->nodes[0]);
			free_extent_buffer(path->nodes[0]);
			path->nodes[0] = right;
			path->slots[0] = 0;
			if (path->slots[1] == 0)
				fixup_low_keys(path, &disk_key, 1);
		}
		/*
		 * We create a new leaf 'right' for the required ins_len and
		 * we'll do btrfs_mark_buffer_dirty() on this leaf after copying
		 * the content of ins_len to 'right'.
		 */
		return ret;
	}

	copy_for_split(trans, path, l, right, slot, mid, nritems);

	if (split == 2) {
		BUG_ON(num_doubles != 0);
		num_doubles++;
		goto again;
	}

	return 0;

push_for_double:
	push_for_double_split(trans, root, path, data_size);
	tried_avoid_double = 1;
	if (btrfs_leaf_free_space(path->nodes[0]) >= data_size)
		return 0;
	goto again;
}

static noinline int setup_leaf_for_split(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root,
					 struct btrfs_path *path, int ins_len)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	u64 extent_len = 0;
	u32 item_size;
	int ret;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	BUG_ON(key.type != BTRFS_EXTENT_DATA_KEY &&
	       key.type != BTRFS_EXTENT_CSUM_KEY);

	if (btrfs_leaf_free_space(leaf) >= ins_len)
		return 0;

	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	if (key.type == BTRFS_EXTENT_DATA_KEY) {
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_len = btrfs_file_extent_num_bytes(leaf, fi);
	}
	btrfs_release_path(path);

	path->keep_locks = 1;
	path->search_for_split = 1;
	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	path->search_for_split = 0;
	if (ret > 0)
		ret = -EAGAIN;
	if (ret < 0)
		goto err;

	ret = -EAGAIN;
	leaf = path->nodes[0];
	/* if our item isn't there, return now */
	if (item_size != btrfs_item_size_nr(leaf, path->slots[0]))
		goto err;

	/* the leaf has  changed, it now has room.  return now */
	if (btrfs_leaf_free_space(path->nodes[0]) >= ins_len)
		goto err;

	if (key.type == BTRFS_EXTENT_DATA_KEY) {
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		if (extent_len != btrfs_file_extent_num_bytes(leaf, fi))
			goto err;
	}

	btrfs_set_path_blocking(path);
	ret = split_leaf(trans, root, &key, path, ins_len, 1);
	if (ret)
		goto err;

	path->keep_locks = 0;
	btrfs_unlock_up_safe(path, 1);
	return 0;
err:
	path->keep_locks = 0;
	return ret;
}

static noinline int split_item(struct btrfs_path *path,
			       const struct btrfs_key *new_key,
			       unsigned long split_offset)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	struct btrfs_item *new_item;
	int slot;
	char *buf;
	u32 nritems;
	u32 item_size;
	u32 orig_offset;
	struct btrfs_disk_key disk_key;

	leaf = path->nodes[0];
	BUG_ON(btrfs_leaf_free_space(leaf) < sizeof(struct btrfs_item));

	btrfs_set_path_blocking(path);

	item = btrfs_item_nr(path->slots[0]);
	orig_offset = btrfs_item_offset(leaf, item);
	item_size = btrfs_item_size(leaf, item);

	buf = kmalloc(item_size, GFP_NOFS);
	if (!buf)
		return -ENOMEM;

	read_extent_buffer(leaf, buf, btrfs_item_ptr_offset(leaf,
			    path->slots[0]), item_size);

	slot = path->slots[0] + 1;
	nritems = btrfs_header_nritems(leaf);
	if (slot != nritems) {
		/* shift the items */
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + 1),
				btrfs_item_nr_offset(slot),
				(nritems - slot) * sizeof(struct btrfs_item));
	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(leaf, &disk_key, slot);

	new_item = btrfs_item_nr(slot);

	btrfs_set_item_offset(leaf, new_item, orig_offset);
	btrfs_set_item_size(leaf, new_item, item_size - split_offset);

	btrfs_set_item_offset(leaf, item,
			      orig_offset + item_size - split_offset);
	btrfs_set_item_size(leaf, item, split_offset);

	btrfs_set_header_nritems(leaf, nritems + 1);

	/* write the data for the start of the original item */
	write_extent_buffer(leaf, buf,
			    btrfs_item_ptr_offset(leaf, path->slots[0]),
			    split_offset);

	/* write the data for the new item */
	write_extent_buffer(leaf, buf + split_offset,
			    btrfs_item_ptr_offset(leaf, slot),
			    item_size - split_offset);
	btrfs_mark_buffer_dirty(leaf);

	BUG_ON(btrfs_leaf_free_space(leaf) < 0);
	kfree(buf);
	return 0;
}

/*
 * This function splits a single item into two items,
 * giving 'new_key' to the new item and splitting the
 * old one at split_offset (from the start of the item).
 *
 * The path may be released by this operation.  After
 * the split, the path is pointing to the old item.  The
 * new item is going to be in the same node as the old one.
 *
 * Note, the item being split must be smaller enough to live alone on
 * a tree block with room for one extra struct btrfs_item
 *
 * This allows us to split the item in place, keeping a lock on the
 * leaf the entire time.
 */
int btrfs_split_item(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_path *path,
		     const struct btrfs_key *new_key,
		     unsigned long split_offset)
{
	int ret;
	ret = setup_leaf_for_split(trans, root, path,
				   sizeof(struct btrfs_item));
	if (ret)
		return ret;

	ret = split_item(path, new_key, split_offset);
	return ret;
}

/*
 * This function duplicate a item, giving 'new_key' to the new item.
 * It guarantees both items live in the same tree leaf and the new item
 * is contiguous with the original item.
 *
 * This allows us to split file extent in place, keeping a lock on the
 * leaf the entire time.
 */
int btrfs_duplicate_item(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct btrfs_path *path,
			 const struct btrfs_key *new_key)
{
	struct extent_buffer *leaf;
	int ret;
	u32 item_size;

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	ret = setup_leaf_for_split(trans, root, path,
				   item_size + sizeof(struct btrfs_item));
	if (ret)
		return ret;

	path->slots[0]++;
	setup_items_for_insert(root, path, new_key, &item_size, 1);
	leaf = path->nodes[0];
	memcpy_extent_buffer(leaf,
			     btrfs_item_ptr_offset(leaf, path->slots[0]),
			     btrfs_item_ptr_offset(leaf, path->slots[0] - 1),
			     item_size);
	return 0;
}

/*
 * make the item pointed to by the path smaller.  new_size indicates
 * how small to make it, and from_end tells us if we just chop bytes
 * off the end of the item or if we shift the item to chop bytes off
 * the front.
 */
void btrfs_truncate_item(struct btrfs_path *path, u32 new_size, int from_end)
{
	int slot;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data_start;
	unsigned int old_size;
	unsigned int size_diff;
	int i;
	struct btrfs_map_token token;

	leaf = path->nodes[0];
	slot = path->slots[0];

	old_size = btrfs_item_size_nr(leaf, slot);
	if (old_size == new_size)
		return;

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(leaf);

	old_data_start = btrfs_item_offset_nr(leaf, slot);

	size_diff = old_size - new_size;

	BUG_ON(slot < 0);
	BUG_ON(slot >= nritems);

	/*
	 * item0..itemN ... dataN.offset..dataN.size .. data0.size
	 */
	/* first correct the data pointers */
	btrfs_init_map_token(&token, leaf);
	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(i);

		ioff = btrfs_token_item_offset(&token, item);
		btrfs_set_token_item_offset(&token, item, ioff + size_diff);
	}

	/* shift the data */
	if (from_end) {
		memmove_extent_buffer(leaf, BTRFS_LEAF_DATA_OFFSET +
			      data_end + size_diff, BTRFS_LEAF_DATA_OFFSET +
			      data_end, old_data_start + new_size - data_end);
	} else {
		struct btrfs_disk_key disk_key;
		u64 offset;

		btrfs_item_key(leaf, &disk_key, slot);

		if (btrfs_disk_key_type(&disk_key) == BTRFS_EXTENT_DATA_KEY) {
			unsigned long ptr;
			struct btrfs_file_extent_item *fi;

			fi = btrfs_item_ptr(leaf, slot,
					    struct btrfs_file_extent_item);
			fi = (struct btrfs_file_extent_item *)(
			     (unsigned long)fi - size_diff);

			if (btrfs_file_extent_type(leaf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE) {
				ptr = btrfs_item_ptr_offset(leaf, slot);
				memmove_extent_buffer(leaf, ptr,
				      (unsigned long)fi,
				      BTRFS_FILE_EXTENT_INLINE_DATA_START);
			}
		}

		memmove_extent_buffer(leaf, BTRFS_LEAF_DATA_OFFSET +
			      data_end + size_diff, BTRFS_LEAF_DATA_OFFSET +
			      data_end, old_data_start - data_end);

		offset = btrfs_disk_key_offset(&disk_key);
		btrfs_set_disk_key_offset(&disk_key, offset + size_diff);
		btrfs_set_item_key(leaf, &disk_key, slot);
		if (slot == 0)
			fixup_low_keys(path, &disk_key, 1);
	}

	item = btrfs_item_nr(slot);
	btrfs_set_item_size(leaf, item, new_size);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(leaf) < 0) {
		btrfs_print_leaf(leaf);
		BUG();
	}
}

/*
 * make the item pointed to by the path bigger, data_size is the added size.
 */
void btrfs_extend_item(struct btrfs_path *path, u32 data_size)
{
	int slot;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data;
	unsigned int old_size;
	int i;
	struct btrfs_map_token token;

	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(leaf);

	if (btrfs_leaf_free_space(leaf) < data_size) {
		btrfs_print_leaf(leaf);
		BUG();
	}
	slot = path->slots[0];
	old_data = btrfs_item_end_nr(leaf, slot);

	BUG_ON(slot < 0);
	if (slot >= nritems) {
		btrfs_print_leaf(leaf);
		btrfs_crit(leaf->fs_info, "slot %d too large, nritems %d",
			   slot, nritems);
		BUG();
	}

	/*
	 * item0..itemN ... dataN.offset..dataN.size .. data0.size
	 */
	/* first correct the data pointers */
	btrfs_init_map_token(&token, leaf);
	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(i);

		ioff = btrfs_token_item_offset(&token, item);
		btrfs_set_token_item_offset(&token, item, ioff - data_size);
	}

	/* shift the data */
	memmove_extent_buffer(leaf, BTRFS_LEAF_DATA_OFFSET +
		      data_end - data_size, BTRFS_LEAF_DATA_OFFSET +
		      data_end, old_data - data_end);

	data_end = old_data;
	old_size = btrfs_item_size_nr(leaf, slot);
	item = btrfs_item_nr(slot);
	btrfs_set_item_size(leaf, item, old_size + data_size);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(leaf) < 0) {
		btrfs_print_leaf(leaf);
		BUG();
	}
}

/**
 * setup_items_for_insert - Helper called before inserting one or more items
 * to a leaf. Main purpose is to save stack depth by doing the bulk of the work
 * in a function that doesn't call btrfs_search_slot
 *
 * @root:	root we are inserting items to
 * @path:	points to the leaf/slot where we are going to insert new items
 * @cpu_key:	array of keys for items to be inserted
 * @data_size:	size of the body of each item we are going to insert
 * @nr:		size of @cpu_key/@data_size arrays
 */
void setup_items_for_insert(struct btrfs_root *root, struct btrfs_path *path,
			    const struct btrfs_key *cpu_key, u32 *data_size,
			    int nr)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_item *item;
	int i;
	u32 nritems;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;
	struct extent_buffer *leaf;
	int slot;
	struct btrfs_map_token token;
	u32 total_size;
	u32 total_data = 0;

	for (i = 0; i < nr; i++)
		total_data += data_size[i];
	total_size = total_data + (nr * sizeof(struct btrfs_item));

	if (path->slots[0] == 0) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key);
		fixup_low_keys(path, &disk_key, 1);
	}
	btrfs_unlock_up_safe(path, 1);

	leaf = path->nodes[0];
	slot = path->slots[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(leaf);

	if (btrfs_leaf_free_space(leaf) < total_size) {
		btrfs_print_leaf(leaf);
		btrfs_crit(fs_info, "not enough freespace need %u have %d",
			   total_size, btrfs_leaf_free_space(leaf));
		BUG();
	}

	btrfs_init_map_token(&token, leaf);
	if (slot != nritems) {
		unsigned int old_data = btrfs_item_end_nr(leaf, slot);

		if (old_data < data_end) {
			btrfs_print_leaf(leaf);
			btrfs_crit(fs_info,
		"item at slot %d with data offset %u beyond data end of leaf %u",
				   slot, old_data, data_end);
			BUG();
		}
		/*
		 * item0..itemN ... dataN.offset..dataN.size .. data0.size
		 */
		/* first correct the data pointers */
		for (i = slot; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(i);
			ioff = btrfs_token_item_offset(&token, item);
			btrfs_set_token_item_offset(&token, item,
						    ioff - total_data);
		}
		/* shift the items */
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + nr),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

		/* shift the data */
		memmove_extent_buffer(leaf, BTRFS_LEAF_DATA_OFFSET +
			      data_end - total_data, BTRFS_LEAF_DATA_OFFSET +
			      data_end, old_data - data_end);
		data_end = old_data;
	}

	/* setup the item for the new data */
	for (i = 0; i < nr; i++) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key + i);
		btrfs_set_item_key(leaf, &disk_key, slot + i);
		item = btrfs_item_nr(slot + i);
		data_end -= data_size[i];
		btrfs_set_token_item_offset(&token, item, data_end);
		btrfs_set_token_item_size(&token, item, data_size[i]);
	}

	btrfs_set_header_nritems(leaf, nritems + nr);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(leaf) < 0) {
		btrfs_print_leaf(leaf);
		BUG();
	}
}

/*
 * Given a key and some data, insert items into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
int btrfs_insert_empty_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path,
			    const struct btrfs_key *cpu_key, u32 *data_size,
			    int nr)
{
	int ret = 0;
	int slot;
	int i;
	u32 total_size = 0;
	u32 total_data = 0;

	for (i = 0; i < nr; i++)
		total_data += data_size[i];

	total_size = total_data + (nr * sizeof(struct btrfs_item));
	ret = btrfs_search_slot(trans, root, cpu_key, path, total_size, 1);
	if (ret == 0)
		return -EEXIST;
	if (ret < 0)
		return ret;

	slot = path->slots[0];
	BUG_ON(slot < 0);

	setup_items_for_insert(root, path, cpu_key, data_size, nr);
	return 0;
}

/*
 * Given a key and some data, insert an item into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      const struct btrfs_key *cpu_key, void *data,
		      u32 data_size)
{
	int ret = 0;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	unsigned long ptr;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	ret = btrfs_insert_empty_item(trans, root, path, cpu_key, data_size);
	if (!ret) {
		leaf = path->nodes[0];
		ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
		write_extent_buffer(leaf, data, ptr, data_size);
		btrfs_mark_buffer_dirty(leaf);
	}
	btrfs_free_path(path);
	return ret;
}

/*
 * delete the pointer from a given node.
 *
 * the tree should have been previously balanced so the deletion does not
 * empty a node.
 */
static void del_ptr(struct btrfs_root *root, struct btrfs_path *path,
		    int level, int slot)
{
	struct extent_buffer *parent = path->nodes[level];
	u32 nritems;
	int ret;

	nritems = btrfs_header_nritems(parent);
	if (slot != nritems - 1) {
		if (level) {
			ret = tree_mod_log_insert_move(parent, slot, slot + 1,
					nritems - slot - 1);
			BUG_ON(ret < 0);
		}
		memmove_extent_buffer(parent,
			      btrfs_node_key_ptr_offset(slot),
			      btrfs_node_key_ptr_offset(slot + 1),
			      sizeof(struct btrfs_key_ptr) *
			      (nritems - slot - 1));
	} else if (level) {
		ret = tree_mod_log_insert_key(parent, slot, MOD_LOG_KEY_REMOVE,
				GFP_NOFS);
		BUG_ON(ret < 0);
	}

	nritems--;
	btrfs_set_header_nritems(parent, nritems);
	if (nritems == 0 && parent == root->node) {
		BUG_ON(btrfs_header_level(root->node) != 1);
		/* just turn the root into a leaf and break */
		btrfs_set_header_level(root->node, 0);
	} else if (slot == 0) {
		struct btrfs_disk_key disk_key;

		btrfs_node_key(parent, &disk_key, 0);
		fixup_low_keys(path, &disk_key, level + 1);
	}
	btrfs_mark_buffer_dirty(parent);
}

/*
 * a helper function to delete the leaf pointed to by path->slots[1] and
 * path->nodes[1].
 *
 * This deletes the pointer in path->nodes[1] and frees the leaf
 * block extent.  zero is returned if it all worked out, < 0 otherwise.
 *
 * The path must have already been setup for deleting the leaf, including
 * all the proper balancing.  path->nodes[1] must be locked.
 */
static noinline void btrfs_del_leaf(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    struct btrfs_path *path,
				    struct extent_buffer *leaf)
{
	WARN_ON(btrfs_header_generation(leaf) != trans->transid);
	del_ptr(root, path, 1, path->slots[1]);

	/*
	 * btrfs_free_extent is expensive, we want to make sure we
	 * aren't holding any locks when we call it
	 */
	btrfs_unlock_up_safe(path, 0);

	root_sub_used(root, leaf->len);

	atomic_inc(&leaf->refs);
	btrfs_free_tree_block(trans, root, leaf, 0, 1);
	free_extent_buffer_stale(leaf);
}
/*
 * delete the item at the leaf level in path.  If that empties
 * the leaf, remove it from the tree
 */
int btrfs_del_items(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		    struct btrfs_path *path, int slot, int nr)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 last_off;
	u32 dsize = 0;
	int ret = 0;
	int wret;
	int i;
	u32 nritems;

	leaf = path->nodes[0];
	last_off = btrfs_item_offset_nr(leaf, slot + nr - 1);

	for (i = 0; i < nr; i++)
		dsize += btrfs_item_size_nr(leaf, slot + i);

	nritems = btrfs_header_nritems(leaf);

	if (slot + nr != nritems) {
		int data_end = leaf_data_end(leaf);
		struct btrfs_map_token token;

		memmove_extent_buffer(leaf, BTRFS_LEAF_DATA_OFFSET +
			      data_end + dsize,
			      BTRFS_LEAF_DATA_OFFSET + data_end,
			      last_off - data_end);

		btrfs_init_map_token(&token, leaf);
		for (i = slot + nr; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(i);
			ioff = btrfs_token_item_offset(&token, item);
			btrfs_set_token_item_offset(&token, item, ioff + dsize);
		}

		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot),
			      btrfs_item_nr_offset(slot + nr),
			      sizeof(struct btrfs_item) *
			      (nritems - slot - nr));
	}
	btrfs_set_header_nritems(leaf, nritems - nr);
	nritems -= nr;

	/* delete the leaf if we've emptied it */
	if (nritems == 0) {
		if (leaf == root->node) {
			btrfs_set_header_level(leaf, 0);
		} else {
			btrfs_set_path_blocking(path);
			btrfs_clean_tree_block(leaf);
			btrfs_del_leaf(trans, root, path, leaf);
		}
	} else {
		int used = leaf_space_used(leaf, 0, nritems);
		if (slot == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_item_key(leaf, &disk_key, 0);
			fixup_low_keys(path, &disk_key, 1);
		}

		/* delete the leaf if it is mostly empty */
		if (used < BTRFS_LEAF_DATA_SIZE(fs_info) / 3) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			atomic_inc(&leaf->refs);

			btrfs_set_path_blocking(path);
			wret = push_leaf_left(trans, root, path, 1, 1,
					      1, (u32)-1);
			if (wret < 0 && wret != -ENOSPC)
				ret = wret;

			if (path->nodes[0] == leaf &&
			    btrfs_header_nritems(leaf)) {
				wret = push_leaf_right(trans, root, path, 1,
						       1, 1, 0);
				if (wret < 0 && wret != -ENOSPC)
					ret = wret;
			}

			if (btrfs_header_nritems(leaf) == 0) {
				path->slots[1] = slot;
				btrfs_del_leaf(trans, root, path, leaf);
				free_extent_buffer(leaf);
				ret = 0;
			} else {
				/* if we're still in the path, make sure
				 * we're dirty.  Otherwise, one of the
				 * push_leaf functions must have already
				 * dirtied this buffer
				 */
				if (path->nodes[0] == leaf)
					btrfs_mark_buffer_dirty(leaf);
				free_extent_buffer(leaf);
			}
		} else {
			btrfs_mark_buffer_dirty(leaf);
		}
	}
	return ret;
}

/*
 * search the tree again to find a leaf with lesser keys
 * returns 0 if it found something or 1 if there are no lesser leaves.
 * returns < 0 on io errors.
 *
 * This may release the path, and so you may lose any locks held at the
 * time you call it.
 */
int btrfs_prev_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	struct btrfs_key key;
	struct btrfs_disk_key found_key;
	int ret;

	btrfs_item_key_to_cpu(path->nodes[0], &key, 0);

	if (key.offset > 0) {
		key.offset--;
	} else if (key.type > 0) {
		key.type--;
		key.offset = (u64)-1;
	} else if (key.objectid > 0) {
		key.objectid--;
		key.type = (u8)-1;
		key.offset = (u64)-1;
	} else {
		return 1;
	}

	btrfs_release_path(path);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ret;
	btrfs_item_key(path->nodes[0], &found_key, 0);
	ret = comp_keys(&found_key, &key);
	/*
	 * We might have had an item with the previous key in the tree right
	 * before we released our path. And after we released our path, that
	 * item might have been pushed to the first slot (0) of the leaf we
	 * were holding due to a tree balance. Alternatively, an item with the
	 * previous key can exist as the only element of a leaf (big fat item).
	 * Therefore account for these 2 cases, so that our callers (like
	 * btrfs_previous_item) don't miss an existing item with a key matching
	 * the previous key we computed above.
	 */
	if (ret <= 0)
		return 0;
	return 1;
}

/*
 * A helper function to walk down the tree starting at min_key, and looking
 * for nodes or leaves that are have a minimum transaction id.
 * This is used by the btree defrag code, and tree logging
 *
 * This does not cow, but it does stuff the starting key it finds back
 * into min_key, so you can call btrfs_search_slot with cow=1 on the
 * key and get a writable path.
 *
 * This honors path->lowest_level to prevent descent past a given level
 * of the tree.
 *
 * min_trans indicates the oldest transaction that you are interested
 * in walking through.  Any nodes or leaves older than min_trans are
 * skipped over (without reading them).
 *
 * returns zero if something useful was found, < 0 on error and 1 if there
 * was nothing in the tree that matched the search criteria.
 */
int btrfs_search_forward(struct btrfs_root *root, struct btrfs_key *min_key,
			 struct btrfs_path *path,
			 u64 min_trans)
{
	struct extent_buffer *cur;
	struct btrfs_key found_key;
	int slot;
	int sret;
	u32 nritems;
	int level;
	int ret = 1;
	int keep_locks = path->keep_locks;

	path->keep_locks = 1;
again:
	cur = btrfs_read_lock_root_node(root);
	level = btrfs_header_level(cur);
	WARN_ON(path->nodes[level]);
	path->nodes[level] = cur;
	path->locks[level] = BTRFS_READ_LOCK;

	if (btrfs_header_generation(cur) < min_trans) {
		ret = 1;
		goto out;
	}
	while (1) {
		nritems = btrfs_header_nritems(cur);
		level = btrfs_header_level(cur);
		sret = btrfs_bin_search(cur, min_key, &slot);
		if (sret < 0) {
			ret = sret;
			goto out;
		}

		/* at the lowest level, we're done, setup the path and exit */
		if (level == path->lowest_level) {
			if (slot >= nritems)
				goto find_next_key;
			ret = 0;
			path->slots[level] = slot;
			btrfs_item_key_to_cpu(cur, &found_key, slot);
			goto out;
		}
		if (sret && slot > 0)
			slot--;
		/*
		 * check this node pointer against the min_trans parameters.
		 * If it is too old, skip to the next one.
		 */
		while (slot < nritems) {
			u64 gen;

			gen = btrfs_node_ptr_generation(cur, slot);
			if (gen < min_trans) {
				slot++;
				continue;
			}
			break;
		}
find_next_key:
		/*
		 * we didn't find a candidate key in this node, walk forward
		 * and find another one
		 */
		if (slot >= nritems) {
			path->slots[level] = slot;
			btrfs_set_path_blocking(path);
			sret = btrfs_find_next_key(root, path, min_key, level,
						  min_trans);
			if (sret == 0) {
				btrfs_release_path(path);
				goto again;
			} else {
				goto out;
			}
		}
		/* save our key for returning back */
		btrfs_node_key_to_cpu(cur, &found_key, slot);
		path->slots[level] = slot;
		if (level == path->lowest_level) {
			ret = 0;
			goto out;
		}
		btrfs_set_path_blocking(path);
		cur = btrfs_read_node_slot(cur, slot);
		if (IS_ERR(cur)) {
			ret = PTR_ERR(cur);
			goto out;
		}

		btrfs_tree_read_lock(cur);

		path->locks[level - 1] = BTRFS_READ_LOCK;
		path->nodes[level - 1] = cur;
		unlock_up(path, level, 1, 0, NULL);
	}
out:
	path->keep_locks = keep_locks;
	if (ret == 0) {
		btrfs_unlock_up_safe(path, path->lowest_level + 1);
		btrfs_set_path_blocking(path);
		memcpy(min_key, &found_key, sizeof(found_key));
	}
	return ret;
}

/*
 * this is similar to btrfs_next_leaf, but does not try to preserve
 * and fixup the path.  It looks for and returns the next key in the
 * tree based on the current path and the min_trans parameters.
 *
 * 0 is returned if another key is found, < 0 if there are any errors
 * and 1 is returned if there are no higher keys in the tree
 *
 * path->keep_locks should be set to 1 on the search made before
 * calling this function.
 */
int btrfs_find_next_key(struct btrfs_root *root, struct btrfs_path *path,
			struct btrfs_key *key, int level, u64 min_trans)
{
	int slot;
	struct extent_buffer *c;

	WARN_ON(!path->keep_locks && !path->skip_locking);
	while (level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;

		slot = path->slots[level] + 1;
		c = path->nodes[level];
next:
		if (slot >= btrfs_header_nritems(c)) {
			int ret;
			int orig_lowest;
			struct btrfs_key cur_key;
			if (level + 1 >= BTRFS_MAX_LEVEL ||
			    !path->nodes[level + 1])
				return 1;

			if (path->locks[level + 1] || path->skip_locking) {
				level++;
				continue;
			}

			slot = btrfs_header_nritems(c) - 1;
			if (level == 0)
				btrfs_item_key_to_cpu(c, &cur_key, slot);
			else
				btrfs_node_key_to_cpu(c, &cur_key, slot);

			orig_lowest = path->lowest_level;
			btrfs_release_path(path);
			path->lowest_level = level;
			ret = btrfs_search_slot(NULL, root, &cur_key, path,
						0, 0);
			path->lowest_level = orig_lowest;
			if (ret < 0)
				return ret;

			c = path->nodes[level];
			slot = path->slots[level];
			if (ret == 0)
				slot++;
			goto next;
		}

		if (level == 0)
			btrfs_item_key_to_cpu(c, key, slot);
		else {
			u64 gen = btrfs_node_ptr_generation(c, slot);

			if (gen < min_trans) {
				slot++;
				goto next;
			}
			btrfs_node_key_to_cpu(c, key, slot);
		}
		return 0;
	}
	return 1;
}

/*
 * search the tree again to find a leaf with greater keys
 * returns 0 if it found something or 1 if there are no greater leaves.
 * returns < 0 on io errors.
 */
int btrfs_next_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	return btrfs_next_old_leaf(root, path, 0);
}

int btrfs_next_old_leaf(struct btrfs_root *root, struct btrfs_path *path,
			u64 time_seq)
{
	int slot;
	int level;
	struct extent_buffer *c;
	struct extent_buffer *next;
	struct btrfs_key key;
	u32 nritems;
	int ret;
	int old_spinning = path->leave_spinning;
	int next_rw_lock = 0;

	nritems = btrfs_header_nritems(path->nodes[0]);
	if (nritems == 0)
		return 1;

	btrfs_item_key_to_cpu(path->nodes[0], &key, nritems - 1);
again:
	level = 1;
	next = NULL;
	next_rw_lock = 0;
	btrfs_release_path(path);

	path->keep_locks = 1;
	path->leave_spinning = 1;

	if (time_seq)
		ret = btrfs_search_old_slot(root, &key, path, time_seq);
	else
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	path->keep_locks = 0;

	if (ret < 0)
		return ret;

	nritems = btrfs_header_nritems(path->nodes[0]);
	/*
	 * by releasing the path above we dropped all our locks.  A balance
	 * could have added more items next to the key that used to be
	 * at the very end of the block.  So, check again here and
	 * advance the path if there are now more items available.
	 */
	if (nritems > 0 && path->slots[0] < nritems - 1) {
		if (ret == 0)
			path->slots[0]++;
		ret = 0;
		goto done;
	}
	/*
	 * So the above check misses one case:
	 * - after releasing the path above, someone has removed the item that
	 *   used to be at the very end of the block, and balance between leafs
	 *   gets another one with bigger key.offset to replace it.
	 *
	 * This one should be returned as well, or we can get leaf corruption
	 * later(esp. in __btrfs_drop_extents()).
	 *
	 * And a bit more explanation about this check,
	 * with ret > 0, the key isn't found, the path points to the slot
	 * where it should be inserted, so the path->slots[0] item must be the
	 * bigger one.
	 */
	if (nritems > 0 && ret > 0 && path->slots[0] == nritems - 1) {
		ret = 0;
		goto done;
	}

	while (level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level]) {
			ret = 1;
			goto done;
		}

		slot = path->slots[level] + 1;
		c = path->nodes[level];
		if (slot >= btrfs_header_nritems(c)) {
			level++;
			if (level == BTRFS_MAX_LEVEL) {
				ret = 1;
				goto done;
			}
			continue;
		}

		if (next) {
			btrfs_tree_unlock_rw(next, next_rw_lock);
			free_extent_buffer(next);
		}

		next = c;
		next_rw_lock = path->locks[level];
		ret = read_block_for_search(root, path, &next, level,
					    slot, &key);
		if (ret == -EAGAIN)
			goto again;

		if (ret < 0) {
			btrfs_release_path(path);
			goto done;
		}

		if (!path->skip_locking) {
			ret = btrfs_try_tree_read_lock(next);
			if (!ret && time_seq) {
				/*
				 * If we don't get the lock, we may be racing
				 * with push_leaf_left, holding that lock while
				 * itself waiting for the leaf we've currently
				 * locked. To solve this situation, we give up
				 * on our lock and cycle.
				 */
				free_extent_buffer(next);
				btrfs_release_path(path);
				cond_resched();
				goto again;
			}
			if (!ret) {
				btrfs_set_path_blocking(path);
				__btrfs_tree_read_lock(next,
						       BTRFS_NESTING_RIGHT,
						       path->recurse);
			}
			next_rw_lock = BTRFS_READ_LOCK;
		}
		break;
	}
	path->slots[level] = slot;
	while (1) {
		level--;
		c = path->nodes[level];
		if (path->locks[level])
			btrfs_tree_unlock_rw(c, path->locks[level]);

		free_extent_buffer(c);
		path->nodes[level] = next;
		path->slots[level] = 0;
		if (!path->skip_locking)
			path->locks[level] = next_rw_lock;
		if (!level)
			break;

		ret = read_block_for_search(root, path, &next, level,
					    0, &key);
		if (ret == -EAGAIN)
			goto again;

		if (ret < 0) {
			btrfs_release_path(path);
			goto done;
		}

		if (!path->skip_locking) {
			ret = btrfs_try_tree_read_lock(next);
			if (!ret) {
				btrfs_set_path_blocking(path);
				__btrfs_tree_read_lock(next,
						       BTRFS_NESTING_RIGHT,
						       path->recurse);
			}
			next_rw_lock = BTRFS_READ_LOCK;
		}
	}
	ret = 0;
done:
	unlock_up(path, 0, 1, 0, NULL);
	path->leave_spinning = old_spinning;
	if (!old_spinning)
		btrfs_set_path_blocking(path);

	return ret;
}

/*
 * this uses btrfs_prev_leaf to walk backwards in the tree, and keeps
 * searching until it gets past min_objectid or finds an item of 'type'
 *
 * returns 0 if something is found, 1 if nothing was found and < 0 on error
 */
int btrfs_previous_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid,
			int type)
{
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;

	while (1) {
		if (path->slots[0] == 0) {
			btrfs_set_path_blocking(path);
			ret = btrfs_prev_leaf(root, path);
			if (ret != 0)
				return ret;
		} else {
			path->slots[0]--;
		}
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (nritems == 0)
			return 1;
		if (path->slots[0] == nritems)
			path->slots[0]--;

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid < min_objectid)
			break;
		if (found_key.type == type)
			return 0;
		if (found_key.objectid == min_objectid &&
		    found_key.type < type)
			break;
	}
	return 1;
}

/*
 * search in extent tree to find a previous Metadata/Data extent item with
 * min objecitd.
 *
 * returns 0 if something is found, 1 if nothing was found and < 0 on error
 */
int btrfs_previous_extent_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid)
{
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;

	while (1) {
		if (path->slots[0] == 0) {
			btrfs_set_path_blocking(path);
			ret = btrfs_prev_leaf(root, path);
			if (ret != 0)
				return ret;
		} else {
			path->slots[0]--;
		}
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (nritems == 0)
			return 1;
		if (path->slots[0] == nritems)
			path->slots[0]--;

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid < min_objectid)
			break;
		if (found_key.type == BTRFS_EXTENT_ITEM_KEY ||
		    found_key.type == BTRFS_METADATA_ITEM_KEY)
			return 0;
		if (found_key.objectid == min_objectid &&
		    found_key.type < BTRFS_EXTENT_ITEM_KEY)
			break;
	}
	return 1;
}
