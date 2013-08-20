/*
 * Copyright (C) 2007,2008 Oracle.  All rights reserved.
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
#include <linux/rbtree.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "locking.h"

static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level);
static int split_leaf(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *ins_key,
		      struct btrfs_path *path, int data_size, int extend);
static int push_node_left(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *dst,
			  struct extent_buffer *src, int empty);
static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct extent_buffer *dst_buf,
			      struct extent_buffer *src_buf);
static void del_ptr(struct btrfs_root *root, struct btrfs_path *path,
		    int level, int slot);
static void tree_mod_log_free_eb(struct btrfs_fs_info *fs_info,
				 struct extent_buffer *eb);
static int btrfs_prev_leaf(struct btrfs_root *root, struct btrfs_path *path);

struct btrfs_path *btrfs_alloc_path(void)
{
	struct btrfs_path *path;
	path = kmem_cache_zalloc(btrfs_path_cachep, GFP_NOFS);
	return path;
}

/*
 * set all locked nodes in the path to blocking locks.  This should
 * be done before scheduling
 */
noinline void btrfs_set_path_blocking(struct btrfs_path *p)
{
	int i;
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (!p->nodes[i] || !p->locks[i])
			continue;
		btrfs_set_lock_blocking_rw(p->nodes[i], p->locks[i]);
		if (p->locks[i] == BTRFS_READ_LOCK)
			p->locks[i] = BTRFS_READ_LOCK_BLOCKING;
		else if (p->locks[i] == BTRFS_WRITE_LOCK)
			p->locks[i] = BTRFS_WRITE_LOCK_BLOCKING;
	}
}

/*
 * reset all the locked nodes in the patch to spinning locks.
 *
 * held is used to keep lockdep happy, when lockdep is enabled
 * we set held to a blocking lock before we go around and
 * retake all the spinlocks in the path.  You can safely use NULL
 * for held
 */
noinline void btrfs_clear_path_blocking(struct btrfs_path *p,
					struct extent_buffer *held, int held_rw)
{
	int i;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/* lockdep really cares that we take all of these spinlocks
	 * in the right order.  If any of the locks in the path are not
	 * currently blocking, it is going to complain.  So, make really
	 * really sure by forcing the path to blocking before we clear
	 * the path blocking.
	 */
	if (held) {
		btrfs_set_lock_blocking_rw(held, held_rw);
		if (held_rw == BTRFS_WRITE_LOCK)
			held_rw = BTRFS_WRITE_LOCK_BLOCKING;
		else if (held_rw == BTRFS_READ_LOCK)
			held_rw = BTRFS_READ_LOCK_BLOCKING;
	}
	btrfs_set_path_blocking(p);
#endif

	for (i = BTRFS_MAX_LEVEL - 1; i >= 0; i--) {
		if (p->nodes[i] && p->locks[i]) {
			btrfs_clear_lock_blocking_rw(p->nodes[i], p->locks[i]);
			if (p->locks[i] == BTRFS_WRITE_LOCK_BLOCKING)
				p->locks[i] = BTRFS_WRITE_LOCK;
			else if (p->locks[i] == BTRFS_READ_LOCK_BLOCKING)
				p->locks[i] = BTRFS_READ_LOCK;
		}
	}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (held)
		btrfs_clear_lock_blocking_rw(held, held_rw);
#endif
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
		 * it was cow'ed but we may not get the new root node yet so do
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

/* loop around taking references on and locking the root node of the
 * tree until you end up with a lock on the root.  A locked buffer
 * is returned, with a reference held.
 */
struct extent_buffer *btrfs_lock_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);
		btrfs_tree_lock(eb);
		if (eb == root->node)
			break;
		btrfs_tree_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

/* loop around taking references on and locking the root node of the
 * tree until you end up with a lock on the root.  A locked buffer
 * is returned, with a reference held.
 */
static struct extent_buffer *btrfs_read_lock_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);
		btrfs_tree_read_lock(eb);
		if (eb == root->node)
			break;
		btrfs_tree_read_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

/* cowonly root (everything not a reference counted cow subvolume), just get
 * put onto a simple dirty list.  transaction.c walks this to make sure they
 * get properly updated on disk.
 */
static void add_root_to_dirty_list(struct btrfs_root *root)
{
	spin_lock(&root->fs_info->trans_lock);
	if (root->track_dirty && list_empty(&root->dirty_list)) {
		list_add(&root->dirty_list,
			 &root->fs_info->dirty_cowonly_roots);
	}
	spin_unlock(&root->fs_info->trans_lock);
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
	struct extent_buffer *cow;
	int ret = 0;
	int level;
	struct btrfs_disk_key disk_key;

	WARN_ON(root->ref_cows && trans->transid !=
		root->fs_info->running_transaction->transid);
	WARN_ON(root->ref_cows && trans->transid != root->last_trans);

	level = btrfs_header_level(buf);
	if (level == 0)
		btrfs_item_key(buf, &disk_key, 0);
	else
		btrfs_node_key(buf, &disk_key, 0);

	cow = btrfs_alloc_free_block(trans, root, buf->len, 0,
				     new_root_objectid, &disk_key, level,
				     buf->start, 0);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	copy_extent_buffer(cow, buf, 0, 0, cow->len);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_backref_rev(cow, BTRFS_MIXED_BACKREF_REV);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN |
				     BTRFS_HEADER_FLAG_RELOC);
	if (new_root_objectid == BTRFS_TREE_RELOC_OBJECTID)
		btrfs_set_header_flag(cow, BTRFS_HEADER_FLAG_RELOC);
	else
		btrfs_set_header_owner(cow, new_root_objectid);

	write_extent_buffer(cow, root->fs_info->fsid, btrfs_header_fsid(cow),
			    BTRFS_FSID_SIZE);

	WARN_ON(btrfs_header_generation(buf) > trans->transid);
	if (new_root_objectid == BTRFS_TREE_RELOC_OBJECTID)
		ret = btrfs_inc_ref(trans, root, cow, 1, 1);
	else
		ret = btrfs_inc_ref(trans, root, cow, 0, 1);

	if (ret)
		return ret;

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

struct tree_mod_move {
	int dst_slot;
	int nr_items;
};

struct tree_mod_root {
	u64 logical;
	u8 level;
};

struct tree_mod_elem {
	struct rb_node node;
	u64 index;		/* shifted logical */
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
	struct tree_mod_move move;

	/* this is used for op == MOD_LOG_ROOT_REPLACE */
	struct tree_mod_root old_root;
};

static inline void tree_mod_log_read_lock(struct btrfs_fs_info *fs_info)
{
	read_lock(&fs_info->tree_mod_log_lock);
}

static inline void tree_mod_log_read_unlock(struct btrfs_fs_info *fs_info)
{
	read_unlock(&fs_info->tree_mod_log_lock);
}

static inline void tree_mod_log_write_lock(struct btrfs_fs_info *fs_info)
{
	write_lock(&fs_info->tree_mod_log_lock);
}

static inline void tree_mod_log_write_unlock(struct btrfs_fs_info *fs_info)
{
	write_unlock(&fs_info->tree_mod_log_lock);
}

/*
 * Increment the upper half of tree_mod_seq, set lower half zero.
 *
 * Must be called with fs_info->tree_mod_seq_lock held.
 */
static inline u64 btrfs_inc_tree_mod_seq_major(struct btrfs_fs_info *fs_info)
{
	u64 seq = atomic64_read(&fs_info->tree_mod_seq);
	seq &= 0xffffffff00000000ull;
	seq += 1ull << 32;
	atomic64_set(&fs_info->tree_mod_seq, seq);
	return seq;
}

/*
 * Increment the lower half of tree_mod_seq.
 *
 * Must be called with fs_info->tree_mod_seq_lock held. The way major numbers
 * are generated should not technically require a spin lock here. (Rationale:
 * incrementing the minor while incrementing the major seq number is between its
 * atomic64_read and atomic64_set calls doesn't duplicate sequence numbers, it
 * just returns a unique sequence number as usual.) We have decided to leave
 * that requirement in here and rethink it once we notice it really imposes a
 * problem on some workload.
 */
static inline u64 btrfs_inc_tree_mod_seq_minor(struct btrfs_fs_info *fs_info)
{
	return atomic64_inc_return(&fs_info->tree_mod_seq);
}

/*
 * return the last minor in the previous major tree_mod_seq number
 */
u64 btrfs_tree_mod_seq_prev(u64 seq)
{
	return (seq & 0xffffffff00000000ull) - 1ull;
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
	u64 seq;

	tree_mod_log_write_lock(fs_info);
	spin_lock(&fs_info->tree_mod_seq_lock);
	if (!elem->seq) {
		elem->seq = btrfs_inc_tree_mod_seq_major(fs_info);
		list_add_tail(&elem->list, &fs_info->tree_mod_seq_list);
	}
	seq = btrfs_inc_tree_mod_seq_minor(fs_info);
	spin_unlock(&fs_info->tree_mod_seq_lock);
	tree_mod_log_write_unlock(fs_info);

	return seq;
}

void btrfs_put_tree_mod_seq(struct btrfs_fs_info *fs_info,
			    struct seq_list *elem)
{
	struct rb_root *tm_root;
	struct rb_node *node;
	struct rb_node *next;
	struct seq_list *cur_elem;
	struct tree_mod_elem *tm;
	u64 min_seq = (u64)-1;
	u64 seq_putting = elem->seq;

	if (!seq_putting)
		return;

	spin_lock(&fs_info->tree_mod_seq_lock);
	list_del(&elem->list);
	elem->seq = 0;

	list_for_each_entry(cur_elem, &fs_info->tree_mod_seq_list, list) {
		if (cur_elem->seq < min_seq) {
			if (seq_putting > cur_elem->seq) {
				/*
				 * blocker with lower sequence number exists, we
				 * cannot remove anything from the log
				 */
				spin_unlock(&fs_info->tree_mod_seq_lock);
				return;
			}
			min_seq = cur_elem->seq;
		}
	}
	spin_unlock(&fs_info->tree_mod_seq_lock);

	/*
	 * anything that's lower than the lowest existing (read: blocked)
	 * sequence number can be removed from the tree.
	 */
	tree_mod_log_write_lock(fs_info);
	tm_root = &fs_info->tree_mod_log;
	for (node = rb_first(tm_root); node; node = next) {
		next = rb_next(node);
		tm = container_of(node, struct tree_mod_elem, node);
		if (tm->seq > min_seq)
			continue;
		rb_erase(node, tm_root);
		kfree(tm);
	}
	tree_mod_log_write_unlock(fs_info);
}

/*
 * key order of the log:
 *       index -> sequence
 *
 * the index is the shifted logical of the *new* root node for root replace
 * operations, or the shifted logical of the affected block for all other
 * operations.
 */
static noinline int
__tree_mod_log_insert(struct btrfs_fs_info *fs_info, struct tree_mod_elem *tm)
{
	struct rb_root *tm_root;
	struct rb_node **new;
	struct rb_node *parent = NULL;
	struct tree_mod_elem *cur;
	int ret = 0;

	BUG_ON(!tm);

	tree_mod_log_write_lock(fs_info);
	if (list_empty(&fs_info->tree_mod_seq_list)) {
		tree_mod_log_write_unlock(fs_info);
		/*
		 * Ok we no longer care about logging modifications, free up tm
		 * and return 0.  Any callers shouldn't be using tm after
		 * calling tree_mod_log_insert, but if they do we can just
		 * change this to return a special error code to let the callers
		 * do their own thing.
		 */
		kfree(tm);
		return 0;
	}

	spin_lock(&fs_info->tree_mod_seq_lock);
	tm->seq = btrfs_inc_tree_mod_seq_minor(fs_info);
	spin_unlock(&fs_info->tree_mod_seq_lock);

	tm_root = &fs_info->tree_mod_log;
	new = &tm_root->rb_node;
	while (*new) {
		cur = container_of(*new, struct tree_mod_elem, node);
		parent = *new;
		if (cur->index < tm->index)
			new = &((*new)->rb_left);
		else if (cur->index > tm->index)
			new = &((*new)->rb_right);
		else if (cur->seq < tm->seq)
			new = &((*new)->rb_left);
		else if (cur->seq > tm->seq)
			new = &((*new)->rb_right);
		else {
			ret = -EEXIST;
			kfree(tm);
			goto out;
		}
	}

	rb_link_node(&tm->node, parent, new);
	rb_insert_color(&tm->node, tm_root);
out:
	tree_mod_log_write_unlock(fs_info);
	return ret;
}

/*
 * Determines if logging can be omitted. Returns 1 if it can. Otherwise, it
 * returns zero with the tree_mod_log_lock acquired. The caller must hold
 * this until all tree mod log insertions are recorded in the rb tree and then
 * call tree_mod_log_write_unlock() to release.
 */
static inline int tree_mod_dont_log(struct btrfs_fs_info *fs_info,
				    struct extent_buffer *eb) {
	smp_mb();
	if (list_empty(&(fs_info)->tree_mod_seq_list))
		return 1;
	if (eb && btrfs_header_level(eb) == 0)
		return 1;
	return 0;
}

static inline int
__tree_mod_log_insert_key(struct btrfs_fs_info *fs_info,
			  struct extent_buffer *eb, int slot,
			  enum mod_log_op op, gfp_t flags)
{
	struct tree_mod_elem *tm;

	tm = kzalloc(sizeof(*tm), flags);
	if (!tm)
		return -ENOMEM;

	tm->index = eb->start >> PAGE_CACHE_SHIFT;
	if (op != MOD_LOG_KEY_ADD) {
		btrfs_node_key(eb, &tm->key, slot);
		tm->blockptr = btrfs_node_blockptr(eb, slot);
	}
	tm->op = op;
	tm->slot = slot;
	tm->generation = btrfs_node_ptr_generation(eb, slot);

	return __tree_mod_log_insert(fs_info, tm);
}

static noinline int
tree_mod_log_insert_key(struct btrfs_fs_info *fs_info,
			struct extent_buffer *eb, int slot,
			enum mod_log_op op, gfp_t flags)
{
	if (tree_mod_dont_log(fs_info, eb))
		return 0;

	return __tree_mod_log_insert_key(fs_info, eb, slot, op, flags);
}

static noinline int
tree_mod_log_insert_move(struct btrfs_fs_info *fs_info,
			 struct extent_buffer *eb, int dst_slot, int src_slot,
			 int nr_items, gfp_t flags)
{
	struct tree_mod_elem *tm;
	int ret;
	int i;

	if (tree_mod_dont_log(fs_info, eb))
		return 0;

	/*
	 * When we override something during the move, we log these removals.
	 * This can only happen when we move towards the beginning of the
	 * buffer, i.e. dst_slot < src_slot.
	 */
	for (i = 0; i + dst_slot < src_slot && i < nr_items; i++) {
		ret = __tree_mod_log_insert_key(fs_info, eb, i + dst_slot,
				MOD_LOG_KEY_REMOVE_WHILE_MOVING, GFP_NOFS);
		BUG_ON(ret < 0);
	}

	tm = kzalloc(sizeof(*tm), flags);
	if (!tm)
		return -ENOMEM;

	tm->index = eb->start >> PAGE_CACHE_SHIFT;
	tm->slot = src_slot;
	tm->move.dst_slot = dst_slot;
	tm->move.nr_items = nr_items;
	tm->op = MOD_LOG_MOVE_KEYS;

	return __tree_mod_log_insert(fs_info, tm);
}

static inline void
__tree_mod_log_free_eb(struct btrfs_fs_info *fs_info, struct extent_buffer *eb)
{
	int i;
	u32 nritems;
	int ret;

	if (btrfs_header_level(eb) == 0)
		return;

	nritems = btrfs_header_nritems(eb);
	for (i = nritems - 1; i >= 0; i--) {
		ret = __tree_mod_log_insert_key(fs_info, eb, i,
				MOD_LOG_KEY_REMOVE_WHILE_FREEING, GFP_NOFS);
		BUG_ON(ret < 0);
	}
}

static noinline int
tree_mod_log_insert_root(struct btrfs_fs_info *fs_info,
			 struct extent_buffer *old_root,
			 struct extent_buffer *new_root, gfp_t flags,
			 int log_removal)
{
	struct tree_mod_elem *tm;

	if (tree_mod_dont_log(fs_info, NULL))
		return 0;

	if (log_removal)
		__tree_mod_log_free_eb(fs_info, old_root);

	tm = kzalloc(sizeof(*tm), flags);
	if (!tm)
		return -ENOMEM;

	tm->index = new_root->start >> PAGE_CACHE_SHIFT;
	tm->old_root.logical = old_root->start;
	tm->old_root.level = btrfs_header_level(old_root);
	tm->generation = btrfs_header_generation(old_root);
	tm->op = MOD_LOG_ROOT_REPLACE;

	return __tree_mod_log_insert(fs_info, tm);
}

static struct tree_mod_elem *
__tree_mod_log_search(struct btrfs_fs_info *fs_info, u64 start, u64 min_seq,
		      int smallest)
{
	struct rb_root *tm_root;
	struct rb_node *node;
	struct tree_mod_elem *cur = NULL;
	struct tree_mod_elem *found = NULL;
	u64 index = start >> PAGE_CACHE_SHIFT;

	tree_mod_log_read_lock(fs_info);
	tm_root = &fs_info->tree_mod_log;
	node = tm_root->rb_node;
	while (node) {
		cur = container_of(node, struct tree_mod_elem, node);
		if (cur->index < index) {
			node = node->rb_left;
		} else if (cur->index > index) {
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
	tree_mod_log_read_unlock(fs_info);

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

static noinline void
tree_mod_log_eb_copy(struct btrfs_fs_info *fs_info, struct extent_buffer *dst,
		     struct extent_buffer *src, unsigned long dst_offset,
		     unsigned long src_offset, int nr_items)
{
	int ret;
	int i;

	if (tree_mod_dont_log(fs_info, NULL))
		return;

	if (btrfs_header_level(dst) == 0 && btrfs_header_level(src) == 0)
		return;

	for (i = 0; i < nr_items; i++) {
		ret = __tree_mod_log_insert_key(fs_info, src,
						i + src_offset,
						MOD_LOG_KEY_REMOVE, GFP_NOFS);
		BUG_ON(ret < 0);
		ret = __tree_mod_log_insert_key(fs_info, dst,
						     i + dst_offset,
						     MOD_LOG_KEY_ADD,
						     GFP_NOFS);
		BUG_ON(ret < 0);
	}
}

static inline void
tree_mod_log_eb_move(struct btrfs_fs_info *fs_info, struct extent_buffer *dst,
		     int dst_offset, int src_offset, int nr_items)
{
	int ret;
	ret = tree_mod_log_insert_move(fs_info, dst, dst_offset, src_offset,
				       nr_items, GFP_NOFS);
	BUG_ON(ret < 0);
}

static noinline void
tree_mod_log_set_node_key(struct btrfs_fs_info *fs_info,
			  struct extent_buffer *eb, int slot, int atomic)
{
	int ret;

	ret = __tree_mod_log_insert_key(fs_info, eb, slot,
					MOD_LOG_KEY_REPLACE,
					atomic ? GFP_ATOMIC : GFP_NOFS);
	BUG_ON(ret < 0);
}

static noinline void
tree_mod_log_free_eb(struct btrfs_fs_info *fs_info, struct extent_buffer *eb)
{
	if (tree_mod_dont_log(fs_info, eb))
		return;
	__tree_mod_log_free_eb(fs_info, eb);
}

static noinline void
tree_mod_log_set_root_pointer(struct btrfs_root *root,
			      struct extent_buffer *new_root_node,
			      int log_removal)
{
	int ret;
	ret = tree_mod_log_insert_root(root->fs_info, root->node,
				       new_root_node, GFP_NOFS, log_removal);
	BUG_ON(ret < 0);
}

/*
 * check if the tree block can be shared by multiple trees
 */
int btrfs_block_can_be_shared(struct btrfs_root *root,
			      struct extent_buffer *buf)
{
	/*
	 * Tree blocks not in refernece counted trees and tree roots
	 * are never shared. If a block was allocated after the last
	 * snapshot and the block was not allocated by tree relocation,
	 * we know the block is not shared.
	 */
	if (root->ref_cows &&
	    buf != root->node && buf != root->commit_root &&
	    (btrfs_header_generation(buf) <=
	     btrfs_root_last_snapshot(&root->root_item) ||
	     btrfs_header_flag(buf, BTRFS_HEADER_FLAG_RELOC)))
		return 1;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (root->ref_cows &&
	    btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
		return 1;
#endif
	return 0;
}

static noinline int update_ref_for_cow(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct extent_buffer *buf,
				       struct extent_buffer *cow,
				       int *last_ref)
{
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
		ret = btrfs_lookup_extent_info(trans, root, buf->start,
					       btrfs_header_level(buf), 1,
					       &refs, &flags);
		if (ret)
			return ret;
		if (refs == 0) {
			ret = -EROFS;
			btrfs_std_error(root->fs_info, ret);
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
			ret = btrfs_inc_ref(trans, root, buf, 1, 1);
			BUG_ON(ret); /* -ENOMEM */

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID) {
				ret = btrfs_dec_ref(trans, root, buf, 0, 1);
				BUG_ON(ret); /* -ENOMEM */
				ret = btrfs_inc_ref(trans, root, cow, 1, 1);
				BUG_ON(ret); /* -ENOMEM */
			}
			new_flags |= BTRFS_BLOCK_FLAG_FULL_BACKREF;
		} else {

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID)
				ret = btrfs_inc_ref(trans, root, cow, 1, 1);
			else
				ret = btrfs_inc_ref(trans, root, cow, 0, 1);
			BUG_ON(ret); /* -ENOMEM */
		}
		if (new_flags != 0) {
			int level = btrfs_header_level(buf);

			ret = btrfs_set_disk_extent_flags(trans, root,
							  buf->start,
							  buf->len,
							  new_flags, level, 0);
			if (ret)
				return ret;
		}
	} else {
		if (flags & BTRFS_BLOCK_FLAG_FULL_BACKREF) {
			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID)
				ret = btrfs_inc_ref(trans, root, cow, 1, 1);
			else
				ret = btrfs_inc_ref(trans, root, cow, 0, 1);
			BUG_ON(ret); /* -ENOMEM */
			ret = btrfs_dec_ref(trans, root, buf, 1, 1);
			BUG_ON(ret); /* -ENOMEM */
		}
		clean_tree_block(trans, root, buf);
		*last_ref = 1;
	}
	return 0;
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
			     u64 search_start, u64 empty_size)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *cow;
	int level, ret;
	int last_ref = 0;
	int unlock_orig = 0;
	u64 parent_start;

	if (*cow_ret == buf)
		unlock_orig = 1;

	btrfs_assert_tree_locked(buf);

	WARN_ON(root->ref_cows && trans->transid !=
		root->fs_info->running_transaction->transid);
	WARN_ON(root->ref_cows && trans->transid != root->last_trans);

	level = btrfs_header_level(buf);

	if (level == 0)
		btrfs_item_key(buf, &disk_key, 0);
	else
		btrfs_node_key(buf, &disk_key, 0);

	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) {
		if (parent)
			parent_start = parent->start;
		else
			parent_start = 0;
	} else
		parent_start = 0;

	cow = btrfs_alloc_free_block(trans, root, buf->len, parent_start,
				     root->root_key.objectid, &disk_key,
				     level, search_start, empty_size);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	/* cow is set to blocking by btrfs_init_new_buffer */

	copy_extent_buffer(cow, buf, 0, 0, cow->len);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_backref_rev(cow, BTRFS_MIXED_BACKREF_REV);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN |
				     BTRFS_HEADER_FLAG_RELOC);
	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
		btrfs_set_header_flag(cow, BTRFS_HEADER_FLAG_RELOC);
	else
		btrfs_set_header_owner(cow, root->root_key.objectid);

	write_extent_buffer(cow, root->fs_info->fsid, btrfs_header_fsid(cow),
			    BTRFS_FSID_SIZE);

	ret = update_ref_for_cow(trans, root, buf, cow, &last_ref);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		return ret;
	}

	if (root->ref_cows)
		btrfs_reloc_cow_block(trans, root, buf, cow);

	if (buf == root->node) {
		WARN_ON(parent && parent != buf);
		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID ||
		    btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
			parent_start = buf->start;
		else
			parent_start = 0;

		extent_buffer_get(cow);
		tree_mod_log_set_root_pointer(root, cow, 1);
		rcu_assign_pointer(root->node, cow);

		btrfs_free_tree_block(trans, root, buf, parent_start,
				      last_ref);
		free_extent_buffer(buf);
		add_root_to_dirty_list(root);
	} else {
		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
			parent_start = parent->start;
		else
			parent_start = 0;

		WARN_ON(trans->transid != btrfs_header_generation(parent));
		tree_mod_log_insert_key(root->fs_info, parent, parent_slot,
					MOD_LOG_KEY_REPLACE, GFP_NOFS);
		btrfs_set_node_blockptr(parent, parent_slot,
					cow->start);
		btrfs_set_node_ptr_generation(parent, parent_slot,
					      trans->transid);
		btrfs_mark_buffer_dirty(parent);
		if (last_ref)
			tree_mod_log_free_eb(root->fs_info, buf);
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
static struct tree_mod_elem *
__tree_mod_log_oldest_root(struct btrfs_fs_info *fs_info,
			   struct extent_buffer *eb_root, u64 time_seq)
{
	struct tree_mod_elem *tm;
	struct tree_mod_elem *found = NULL;
	u64 root_logical = eb_root->start;
	int looped = 0;

	if (!time_seq)
		return NULL;

	/*
	 * the very last operation that's logged for a root is the replacement
	 * operation (if it is replaced at all). this has the index of the *new*
	 * root, making it the very first operation that's logged for this root.
	 */
	while (1) {
		tm = tree_mod_log_search_oldest(fs_info, root_logical,
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
 * previous operations will be rewinded (until we reach something older than
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
	tree_mod_log_read_lock(fs_info);
	while (tm && tm->seq >= time_seq) {
		/*
		 * all the operations are recorded with the operator used for
		 * the modification. as we're going backwards, we do the
		 * opposite of each operation here.
		 */
		switch (tm->op) {
		case MOD_LOG_KEY_REMOVE_WHILE_FREEING:
			BUG_ON(tm->slot < n);
			/* Fallthrough */
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
		tm = container_of(next, struct tree_mod_elem, node);
		if (tm->index != first_tm->index)
			break;
	}
	tree_mod_log_read_unlock(fs_info);
	btrfs_set_header_nritems(eb, n);
}

/*
 * Called with eb read locked. If the buffer cannot be rewinded, the same buffer
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
	btrfs_set_lock_blocking_rw(eb, BTRFS_READ_LOCK);

	if (tm->op == MOD_LOG_KEY_REMOVE_WHILE_FREEING) {
		BUG_ON(tm->slot != 0);
		eb_rewin = alloc_dummy_extent_buffer(eb->start,
						fs_info->tree_root->nodesize);
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

	btrfs_clear_path_blocking(path, NULL, BTRFS_READ_LOCK);
	btrfs_tree_read_unlock_blocking(eb);
	free_extent_buffer(eb);

	extent_buffer_get(eb_rewin);
	btrfs_tree_read_lock(eb_rewin);
	__tree_mod_log_rewind(fs_info, eb_rewin, time_seq, tm);
	WARN_ON(btrfs_header_nritems(eb_rewin) >
		BTRFS_NODEPTRS_PER_BLOCK(fs_info->tree_root));

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
	struct tree_mod_elem *tm;
	struct extent_buffer *eb = NULL;
	struct extent_buffer *eb_root;
	struct extent_buffer *old;
	struct tree_mod_root *old_root = NULL;
	u64 old_generation = 0;
	u64 logical;
	u32 blocksize;

	eb_root = btrfs_read_lock_root_node(root);
	tm = __tree_mod_log_oldest_root(root->fs_info, eb_root, time_seq);
	if (!tm)
		return eb_root;

	if (tm->op == MOD_LOG_ROOT_REPLACE) {
		old_root = &tm->old_root;
		old_generation = tm->generation;
		logical = old_root->logical;
	} else {
		logical = eb_root->start;
	}

	tm = tree_mod_log_search(root->fs_info, logical, time_seq);
	if (old_root && tm && tm->op != MOD_LOG_KEY_REMOVE_WHILE_FREEING) {
		btrfs_tree_read_unlock(eb_root);
		free_extent_buffer(eb_root);
		blocksize = btrfs_level_size(root, old_root->level);
		old = read_tree_block(root, logical, blocksize, 0);
		if (!old || !extent_buffer_uptodate(old)) {
			free_extent_buffer(old);
			pr_warn("btrfs: failed to read tree block %llu from get_old_root\n",
				logical);
			WARN_ON(1);
		} else {
			eb = btrfs_clone_extent_buffer(old);
			free_extent_buffer(old);
		}
	} else if (old_root) {
		btrfs_tree_read_unlock(eb_root);
		free_extent_buffer(eb_root);
		eb = alloc_dummy_extent_buffer(logical, root->nodesize);
	} else {
		btrfs_set_lock_blocking_rw(eb_root, BTRFS_READ_LOCK);
		eb = btrfs_clone_extent_buffer(eb_root);
		btrfs_tree_read_unlock_blocking(eb_root);
		free_extent_buffer(eb_root);
	}

	if (!eb)
		return NULL;
	extent_buffer_get(eb);
	btrfs_tree_read_lock(eb);
	if (old_root) {
		btrfs_set_header_bytenr(eb, eb->start);
		btrfs_set_header_backref_rev(eb, BTRFS_MIXED_BACKREF_REV);
		btrfs_set_header_owner(eb, btrfs_header_owner(eb_root));
		btrfs_set_header_level(eb, old_root->level);
		btrfs_set_header_generation(eb, old_generation);
	}
	if (tm)
		__tree_mod_log_rewind(root->fs_info, eb, time_seq, tm);
	else
		WARN_ON(btrfs_header_level(eb) != 0);
	WARN_ON(btrfs_header_nritems(eb) > BTRFS_NODEPTRS_PER_BLOCK(root));

	return eb;
}

int btrfs_old_root_level(struct btrfs_root *root, u64 time_seq)
{
	struct tree_mod_elem *tm;
	int level;
	struct extent_buffer *eb_root = btrfs_root_node(root);

	tm = __tree_mod_log_oldest_root(root->fs_info, eb_root, time_seq);
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
	/* ensure we can see the force_cow */
	smp_rmb();

	/*
	 * We do not need to cow a block if
	 * 1) this block is not created or changed in this transaction;
	 * 2) this block does not belong to TREE_RELOC tree;
	 * 3) the root is not forced COW.
	 *
	 * What is forced COW:
	 *    when we create snapshot during commiting the transaction,
	 *    after we've finished coping src root, we must COW the shared
	 *    block to ensure the metadata consistency.
	 */
	if (btrfs_header_generation(buf) == trans->transid &&
	    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN) &&
	    !(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID &&
	      btrfs_header_flag(buf, BTRFS_HEADER_FLAG_RELOC)) &&
	    !root->force_cow)
		return 0;
	return 1;
}

/*
 * cows a single block, see __btrfs_cow_block for the real work.
 * This version of it has extra checks so that a block isn't cow'd more than
 * once per transaction, as long as it hasn't been written yet
 */
noinline int btrfs_cow_block(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct extent_buffer *buf,
		    struct extent_buffer *parent, int parent_slot,
		    struct extent_buffer **cow_ret)
{
	u64 search_start;
	int ret;

	if (trans->transaction != root->fs_info->running_transaction)
		WARN(1, KERN_CRIT "trans %llu running %llu\n",
		       trans->transid,
		       root->fs_info->running_transaction->transid);

	if (trans->transid != root->fs_info->generation)
		WARN(1, KERN_CRIT "trans %llu running %llu\n",
		       trans->transid, root->fs_info->generation);

	if (!should_cow_block(trans, root, buf)) {
		*cow_ret = buf;
		return 0;
	}

	search_start = buf->start & ~((u64)(1024 * 1024 * 1024) - 1);

	if (parent)
		btrfs_set_lock_blocking(parent);
	btrfs_set_lock_blocking(buf);

	ret = __btrfs_cow_block(trans, root, buf, parent,
				 parent_slot, cow_ret, search_start, 0);

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

/*
 * compare two keys in a memcmp fashion
 */
static int comp_keys(struct btrfs_disk_key *disk, struct btrfs_key *k2)
{
	struct btrfs_key k1;

	btrfs_disk_key_to_cpu(&k1, disk);

	return btrfs_comp_cpu_keys(&k1, k2);
}

/*
 * same as comp_keys only with two btrfs_key's
 */
int btrfs_comp_cpu_keys(struct btrfs_key *k1, struct btrfs_key *k2)
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

	WARN_ON(trans->transaction != root->fs_info->running_transaction);
	WARN_ON(trans->transid != root->fs_info->generation);

	parent_nritems = btrfs_header_nritems(parent);
	blocksize = btrfs_level_size(root, parent_level - 1);
	end_slot = parent_nritems;

	if (parent_nritems == 1)
		return 0;

	btrfs_set_lock_blocking(parent);

	for (i = start_slot; i < end_slot; i++) {
		int close = 1;

		btrfs_node_key(parent, &disk_key, i);
		if (!progress_passed && comp_keys(&disk_key, progress) < 0)
			continue;

		progress_passed = 1;
		blocknr = btrfs_node_blockptr(parent, i);
		gen = btrfs_node_ptr_generation(parent, i);
		if (last_block == 0)
			last_block = blocknr;

		if (i > 0) {
			other = btrfs_node_blockptr(parent, i - 1);
			close = close_blocks(blocknr, other, blocksize);
		}
		if (!close && i < end_slot - 2) {
			other = btrfs_node_blockptr(parent, i + 1);
			close = close_blocks(blocknr, other, blocksize);
		}
		if (close) {
			last_block = blocknr;
			continue;
		}

		cur = btrfs_find_tree_block(root, blocknr, blocksize);
		if (cur)
			uptodate = btrfs_buffer_uptodate(cur, gen, 0);
		else
			uptodate = 0;
		if (!cur || !uptodate) {
			if (!cur) {
				cur = read_tree_block(root, blocknr,
							 blocksize, gen);
				if (!cur || !extent_buffer_uptodate(cur)) {
					free_extent_buffer(cur);
					return -EIO;
				}
			} else if (!uptodate) {
				err = btrfs_read_buffer(cur, gen);
				if (err) {
					free_extent_buffer(cur);
					return err;
				}
			}
		}
		if (search_start == 0)
			search_start = last_block;

		btrfs_tree_lock(cur);
		btrfs_set_lock_blocking(cur);
		err = __btrfs_cow_block(trans, root, cur, parent, i,
					&cur, search_start,
					min(16 * blocksize,
					    (end_slot - i) * blocksize));
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
 * The leaf data grows from end-to-front in the node.
 * this returns the address of the start of the last item,
 * which is the stop of the leaf data stack
 */
static inline unsigned int leaf_data_end(struct btrfs_root *root,
					 struct extent_buffer *leaf)
{
	u32 nr = btrfs_header_nritems(leaf);
	if (nr == 0)
		return BTRFS_LEAF_DATA_SIZE(root);
	return btrfs_item_offset_nr(leaf, nr - 1);
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
				       unsigned long p,
				       int item_size, struct btrfs_key *key,
				       int max, int *slot)
{
	int low = 0;
	int high = max;
	int mid;
	int ret;
	struct btrfs_disk_key *tmp = NULL;
	struct btrfs_disk_key unaligned;
	unsigned long offset;
	char *kaddr = NULL;
	unsigned long map_start = 0;
	unsigned long map_len = 0;
	int err;

	while (low < high) {
		mid = (low + high) / 2;
		offset = p + mid * item_size;

		if (!kaddr || offset < map_start ||
		    (offset + sizeof(struct btrfs_disk_key)) >
		    map_start + map_len) {

			err = map_private_extent_buffer(eb, offset,
						sizeof(struct btrfs_disk_key),
						&kaddr, &map_start, &map_len);

			if (!err) {
				tmp = (struct btrfs_disk_key *)(kaddr + offset -
							map_start);
			} else {
				read_extent_buffer(eb, &unaligned,
						   offset, sizeof(unaligned));
				tmp = &unaligned;
			}

		} else {
			tmp = (struct btrfs_disk_key *)(kaddr + offset -
							map_start);
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
static int bin_search(struct extent_buffer *eb, struct btrfs_key *key,
		      int level, int *slot)
{
	if (level == 0)
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

int btrfs_bin_search(struct extent_buffer *eb, struct btrfs_key *key,
		     int level, int *slot)
{
	return bin_search(eb, key, level, slot);
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
 * NULL is returned on error.
 */
static noinline struct extent_buffer *read_node_slot(struct btrfs_root *root,
				   struct extent_buffer *parent, int slot)
{
	int level = btrfs_header_level(parent);
	struct extent_buffer *eb;

	if (slot < 0)
		return NULL;
	if (slot >= btrfs_header_nritems(parent))
		return NULL;

	BUG_ON(level == 0);

	eb = read_tree_block(root, btrfs_node_blockptr(parent, slot),
			     btrfs_level_size(root, level - 1),
			     btrfs_node_ptr_generation(parent, slot));
	if (eb && !extent_buffer_uptodate(eb)) {
		free_extent_buffer(eb);
		eb = NULL;
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
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	u64 orig_ptr;

	if (level == 0)
		return 0;

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
		child = read_node_slot(root, mid, 0);
		if (!child) {
			ret = -EROFS;
			btrfs_std_error(root->fs_info, ret);
			goto enospc;
		}

		btrfs_tree_lock(child);
		btrfs_set_lock_blocking(child);
		ret = btrfs_cow_block(trans, root, child, mid, 0, &child);
		if (ret) {
			btrfs_tree_unlock(child);
			free_extent_buffer(child);
			goto enospc;
		}

		tree_mod_log_set_root_pointer(root, child, 1);
		rcu_assign_pointer(root->node, child);

		add_root_to_dirty_list(root);
		btrfs_tree_unlock(child);

		path->locks[level] = 0;
		path->nodes[level] = NULL;
		clean_tree_block(trans, root, mid);
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
	    BTRFS_NODEPTRS_PER_BLOCK(root) / 4)
		return 0;

	left = read_node_slot(root, parent, pslot - 1);
	if (left) {
		btrfs_tree_lock(left);
		btrfs_set_lock_blocking(left);
		wret = btrfs_cow_block(trans, root, left,
				       parent, pslot - 1, &left);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}
	right = read_node_slot(root, parent, pslot + 1);
	if (right) {
		btrfs_tree_lock(right);
		btrfs_set_lock_blocking(right);
		wret = btrfs_cow_block(trans, root, right,
				       parent, pslot + 1, &right);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}

	/* first, try to make some room in the middle buffer */
	if (left) {
		orig_slot += btrfs_header_nritems(left);
		wret = push_node_left(trans, root, left, mid, 1);
		if (wret < 0)
			ret = wret;
	}

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right) {
		wret = push_node_left(trans, root, mid, right, 1);
		if (wret < 0 && wret != -ENOSPC)
			ret = wret;
		if (btrfs_header_nritems(right) == 0) {
			clean_tree_block(trans, root, right);
			btrfs_tree_unlock(right);
			del_ptr(root, path, level + 1, pslot + 1);
			root_sub_used(root, right->len);
			btrfs_free_tree_block(trans, root, right, 0, 1);
			free_extent_buffer_stale(right);
			right = NULL;
		} else {
			struct btrfs_disk_key right_key;
			btrfs_node_key(right, &right_key, 0);
			tree_mod_log_set_node_key(root->fs_info, parent,
						  pslot + 1, 0);
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
			btrfs_std_error(root->fs_info, ret);
			goto enospc;
		}
		wret = balance_node_right(trans, root, mid, left);
		if (wret < 0) {
			ret = wret;
			goto enospc;
		}
		if (wret == 1) {
			wret = push_node_left(trans, root, left, mid, 1);
			if (wret < 0)
				ret = wret;
		}
		BUG_ON(wret == 1);
	}
	if (btrfs_header_nritems(mid) == 0) {
		clean_tree_block(trans, root, mid);
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
		tree_mod_log_set_node_key(root->fs_info, parent,
					  pslot, 0);
		btrfs_set_node_key(parent, &mid_key, pslot);
		btrfs_mark_buffer_dirty(parent);
	}

	/* update the path */
	if (left) {
		if (btrfs_header_nritems(left) > orig_slot) {
			extent_buffer_get(left);
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

	left = read_node_slot(root, parent, pslot - 1);

	/* first, try to make some room in the middle buffer */
	if (left) {
		u32 left_nr;

		btrfs_tree_lock(left);
		btrfs_set_lock_blocking(left);

		left_nr = btrfs_header_nritems(left);
		if (left_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, left, parent,
					      pslot - 1, &left);
			if (ret)
				wret = 1;
			else {
				wret = push_node_left(trans, root,
						      left, mid, 0);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;
			orig_slot += left_nr;
			btrfs_node_key(mid, &disk_key, 0);
			tree_mod_log_set_node_key(root->fs_info, parent,
						  pslot, 0);
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
	right = read_node_slot(root, parent, pslot + 1);

	/*
	 * then try to empty the right most buffer into the middle
	 */
	if (right) {
		u32 right_nr;

		btrfs_tree_lock(right);
		btrfs_set_lock_blocking(right);

		right_nr = btrfs_header_nritems(right);
		if (right_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, right,
					      parent, pslot + 1,
					      &right);
			if (ret)
				wret = 1;
			else {
				wret = balance_node_right(trans, root,
							  right, mid);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_node_key(right, &disk_key, 0);
			tree_mod_log_set_node_key(root->fs_info, parent,
						  pslot + 1, 0);
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
static void reada_for_search(struct btrfs_root *root,
			     struct btrfs_path *path,
			     int level, int slot, u64 objectid)
{
	struct extent_buffer *node;
	struct btrfs_disk_key disk_key;
	u32 nritems;
	u64 search;
	u64 target;
	u64 nread = 0;
	u64 gen;
	int direction = path->reada;
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
	blocksize = btrfs_level_size(root, level - 1);
	eb = btrfs_find_tree_block(root, search, blocksize);
	if (eb) {
		free_extent_buffer(eb);
		return;
	}

	target = search;

	nritems = btrfs_header_nritems(node);
	nr = slot;

	while (1) {
		if (direction < 0) {
			if (nr == 0)
				break;
			nr--;
		} else if (direction > 0) {
			nr++;
			if (nr >= nritems)
				break;
		}
		if (path->reada < 0 && objectid) {
			btrfs_node_key(node, &disk_key, nr);
			if (btrfs_disk_key_objectid(&disk_key) != objectid)
				break;
		}
		search = btrfs_node_blockptr(node, nr);
		if ((search <= target && target - search <= 65536) ||
		    (search > target && search - target <= 65536)) {
			gen = btrfs_node_ptr_generation(node, nr);
			readahead_tree_block(root, search, blocksize, gen);
			nread += blocksize;
		}
		nscan++;
		if ((nread > 65536 || nscan > 32))
			break;
	}
}

static noinline void reada_for_balance(struct btrfs_root *root,
				       struct btrfs_path *path, int level)
{
	int slot;
	int nritems;
	struct extent_buffer *parent;
	struct extent_buffer *eb;
	u64 gen;
	u64 block1 = 0;
	u64 block2 = 0;
	int blocksize;

	parent = path->nodes[level + 1];
	if (!parent)
		return;

	nritems = btrfs_header_nritems(parent);
	slot = path->slots[level + 1];
	blocksize = btrfs_level_size(root, level);

	if (slot > 0) {
		block1 = btrfs_node_blockptr(parent, slot - 1);
		gen = btrfs_node_ptr_generation(parent, slot - 1);
		eb = btrfs_find_tree_block(root, block1, blocksize);
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
		eb = btrfs_find_tree_block(root, block2, blocksize);
		if (eb && btrfs_buffer_uptodate(eb, gen, 1) != 0)
			block2 = 0;
		free_extent_buffer(eb);
	}

	if (block1)
		readahead_tree_block(root, block1, blocksize, 0);
	if (block2)
		readahead_tree_block(root, block2, blocksize, 0);
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
		if (i >= lowest_unlock && i > skip_level && path->locks[i]) {
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
 * This releases any locks held in the path starting at level and
 * going all the way up to the root.
 *
 * btrfs_search_slot will keep the lock held on higher nodes in a few
 * corner cases, such as COW of the block at slot zero in the node.  This
 * ignores those rules, and it should only be called when there are no
 * more updates to be done higher up in the tree.
 */
noinline void btrfs_unlock_up_safe(struct btrfs_path *path, int level)
{
	int i;

	if (path->keep_locks)
		return;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		if (!path->nodes[i])
			continue;
		if (!path->locks[i])
			continue;
		btrfs_tree_unlock_rw(path->nodes[i], path->locks[i]);
		path->locks[i] = 0;
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
read_block_for_search(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *p,
		       struct extent_buffer **eb_ret, int level, int slot,
		       struct btrfs_key *key, u64 time_seq)
{
	u64 blocknr;
	u64 gen;
	u32 blocksize;
	struct extent_buffer *b = *eb_ret;
	struct extent_buffer *tmp;
	int ret;

	blocknr = btrfs_node_blockptr(b, slot);
	gen = btrfs_node_ptr_generation(b, slot);
	blocksize = btrfs_level_size(root, level - 1);

	tmp = btrfs_find_tree_block(root, blocknr, blocksize);
	if (tmp) {
		/* first we do an atomic uptodate check */
		if (btrfs_buffer_uptodate(tmp, gen, 1) > 0) {
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
		ret = btrfs_read_buffer(tmp, gen);
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

	free_extent_buffer(tmp);
	if (p->reada)
		reada_for_search(root, p, level, slot, key->objectid);

	btrfs_release_path(p);

	ret = -EAGAIN;
	tmp = read_tree_block(root, blocknr, blocksize, 0);
	if (tmp) {
		/*
		 * If the read above didn't mark this buffer up to date,
		 * it will never end up being up to date.  Set ret to EIO now
		 * and give up so that our caller doesn't loop forever
		 * on our EAGAINs.
		 */
		if (!btrfs_buffer_uptodate(tmp, 0, 0))
			ret = -EIO;
		free_extent_buffer(tmp);
	}
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
	int ret;
	if ((p->search_for_split || ins_len > 0) && btrfs_header_nritems(b) >=
	    BTRFS_NODEPTRS_PER_BLOCK(root) - 3) {
		int sret;

		if (*write_lock_level < level + 1) {
			*write_lock_level = level + 1;
			btrfs_release_path(p);
			goto again;
		}

		btrfs_set_path_blocking(p);
		reada_for_balance(root, p, level);
		sret = split_node(trans, root, p, level);
		btrfs_clear_path_blocking(p, NULL, 0);

		BUG_ON(sret > 0);
		if (sret) {
			ret = sret;
			goto done;
		}
		b = p->nodes[level];
	} else if (ins_len < 0 && btrfs_header_nritems(b) <
		   BTRFS_NODEPTRS_PER_BLOCK(root) / 2) {
		int sret;

		if (*write_lock_level < level + 1) {
			*write_lock_level = level + 1;
			btrfs_release_path(p);
			goto again;
		}

		btrfs_set_path_blocking(p);
		reada_for_balance(root, p, level);
		sret = balance_level(trans, root, p, level);
		btrfs_clear_path_blocking(p, NULL, 0);

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

/*
 * look for key in the tree.  path is filled in with nodes along the way
 * if key is found, we return zero and you can find the item in the leaf
 * level of the path (level 0)
 *
 * If the key isn't found, the path points to the slot where it should
 * be inserted, and 1 is returned.  If there are other errors during the
 * search a negative error number is returned.
 *
 * if ins_len > 0, nodes and leaves will be split as we walk down the
 * tree.  if ins_len < 0, nodes will be merged as we walk down the tree (if
 * possible)
 */
int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_path *p, int
		      ins_len, int cow)
{
	struct extent_buffer *b;
	int slot;
	int ret;
	int err;
	int level;
	int lowest_unlock = 1;
	int root_lock;
	/* everything at write_lock_level or lower must be write locked */
	int write_lock_level = 0;
	u8 lowest_level = 0;
	int min_write_lock_level;

	lowest_level = p->lowest_level;
	WARN_ON(lowest_level && ins_len > 0);
	WARN_ON(p->nodes[0] != NULL);

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
	/*
	 * we try very hard to do read locks on the root
	 */
	root_lock = BTRFS_READ_LOCK;
	level = 0;
	if (p->search_commit_root) {
		/*
		 * the commit roots are read only
		 * so we always do read locks
		 */
		b = root->commit_root;
		extent_buffer_get(b);
		level = btrfs_header_level(b);
		if (!p->skip_locking)
			btrfs_tree_read_lock(b);
	} else {
		if (p->skip_locking) {
			b = btrfs_root_node(root);
			level = btrfs_header_level(b);
		} else {
			/* we don't know the level of the root node
			 * until we actually have it read locked
			 */
			b = btrfs_read_lock_root_node(root);
			level = btrfs_header_level(b);
			if (level <= write_lock_level) {
				/* whoops, must trade for write lock */
				btrfs_tree_read_unlock(b);
				free_extent_buffer(b);
				b = btrfs_lock_root_node(root);
				root_lock = BTRFS_WRITE_LOCK;

				/* the level might have changed, check again */
				level = btrfs_header_level(b);
			}
		}
	}
	p->nodes[level] = b;
	if (!p->skip_locking)
		p->locks[level] = root_lock;

	while (b) {
		level = btrfs_header_level(b);

		/*
		 * setup the path here so we can release it under lock
		 * contention with the cow code
		 */
		if (cow) {
			/*
			 * if we don't really need to cow this block
			 * then we don't want to set the path blocking,
			 * so we test it here
			 */
			if (!should_cow_block(trans, root, b))
				goto cow_done;

			btrfs_set_path_blocking(p);

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

			err = btrfs_cow_block(trans, root, b,
					      p->nodes[level + 1],
					      p->slots[level + 1], &b);
			if (err) {
				ret = err;
				goto done;
			}
		}
cow_done:
		BUG_ON(!cow && ins_len);

		p->nodes[level] = b;
		btrfs_clear_path_blocking(p, NULL, 0);

		/*
		 * we have a lock on b and as long as we aren't changing
		 * the tree, there is no way to for the items in b to change.
		 * It is safe to drop the lock on our parent before we
		 * go through the expensive btree search on b.
		 *
		 * If cow is true, then we might be changing slot zero,
		 * which may require changing the parent.  So, we can't
		 * drop the lock until after we know which slot we're
		 * operating on.
		 */
		if (!cow)
			btrfs_unlock_up_safe(p, level + 1);

		ret = bin_search(b, key, level, &slot);

		if (level != 0) {
			int dec = 0;
			if (ret && slot > 0) {
				dec = 1;
				slot -= 1;
			}
			p->slots[level] = slot;
			err = setup_nodes_for_search(trans, root, p, b, level,
					     ins_len, &write_lock_level);
			if (err == -EAGAIN)
				goto again;
			if (err) {
				ret = err;
				goto done;
			}
			b = p->nodes[level];
			slot = p->slots[level];

			/*
			 * slot 0 is special, if we change the key
			 * we have to update the parent pointer
			 * which means we must have a write lock
			 * on the parent
			 */
			if (slot == 0 && cow &&
			    write_lock_level < level + 1) {
				write_lock_level = level + 1;
				btrfs_release_path(p);
				goto again;
			}

			unlock_up(p, level, lowest_unlock,
				  min_write_lock_level, &write_lock_level);

			if (level == lowest_level) {
				if (dec)
					p->slots[level]++;
				goto done;
			}

			err = read_block_for_search(trans, root, p,
						    &b, level, slot, key, 0);
			if (err == -EAGAIN)
				goto again;
			if (err) {
				ret = err;
				goto done;
			}

			if (!p->skip_locking) {
				level = btrfs_header_level(b);
				if (level <= write_lock_level) {
					err = btrfs_try_tree_write_lock(b);
					if (!err) {
						btrfs_set_path_blocking(p);
						btrfs_tree_lock(b);
						btrfs_clear_path_blocking(p, b,
								  BTRFS_WRITE_LOCK);
					}
					p->locks[level] = BTRFS_WRITE_LOCK;
				} else {
					err = btrfs_try_tree_read_lock(b);
					if (!err) {
						btrfs_set_path_blocking(p);
						btrfs_tree_read_lock(b);
						btrfs_clear_path_blocking(p, b,
								  BTRFS_READ_LOCK);
					}
					p->locks[level] = BTRFS_READ_LOCK;
				}
				p->nodes[level] = b;
			}
		} else {
			p->slots[level] = slot;
			if (ins_len > 0 &&
			    btrfs_leaf_free_space(root, b) < ins_len) {
				if (write_lock_level < 1) {
					write_lock_level = 1;
					btrfs_release_path(p);
					goto again;
				}

				btrfs_set_path_blocking(p);
				err = split_leaf(trans, root, key,
						 p, ins_len, ret == 0);
				btrfs_clear_path_blocking(p, NULL, 0);

				BUG_ON(err > 0);
				if (err) {
					ret = err;
					goto done;
				}
			}
			if (!p->search_for_split)
				unlock_up(p, level, lowest_unlock,
					  min_write_lock_level, &write_lock_level);
			goto done;
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
	if (ret < 0)
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
int btrfs_search_old_slot(struct btrfs_root *root, struct btrfs_key *key,
			  struct btrfs_path *p, u64 time_seq)
{
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
	level = btrfs_header_level(b);
	p->locks[level] = BTRFS_READ_LOCK;

	while (b) {
		level = btrfs_header_level(b);
		p->nodes[level] = b;
		btrfs_clear_path_blocking(p, NULL, 0);

		/*
		 * we have a lock on b and as long as we aren't changing
		 * the tree, there is no way to for the items in b to change.
		 * It is safe to drop the lock on our parent before we
		 * go through the expensive btree search on b.
		 */
		btrfs_unlock_up_safe(p, level + 1);

		ret = bin_search(b, key, level, &slot);

		if (level != 0) {
			int dec = 0;
			if (ret && slot > 0) {
				dec = 1;
				slot -= 1;
			}
			p->slots[level] = slot;
			unlock_up(p, level, lowest_unlock, 0, NULL);

			if (level == lowest_level) {
				if (dec)
					p->slots[level]++;
				goto done;
			}

			err = read_block_for_search(NULL, root, p, &b, level,
						    slot, key, time_seq);
			if (err == -EAGAIN)
				goto again;
			if (err) {
				ret = err;
				goto done;
			}

			level = btrfs_header_level(b);
			err = btrfs_try_tree_read_lock(b);
			if (!err) {
				btrfs_set_path_blocking(p);
				btrfs_tree_read_lock(b);
				btrfs_clear_path_blocking(p, b,
							  BTRFS_READ_LOCK);
			}
			b = tree_mod_log_rewind(root->fs_info, p, b, time_seq);
			if (!b) {
				ret = -ENOMEM;
				goto done;
			}
			p->locks[level] = BTRFS_READ_LOCK;
			p->nodes[level] = b;
		} else {
			p->slots[level] = slot;
			unlock_up(p, level, lowest_unlock, 0, NULL);
			goto done;
		}
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
			       struct btrfs_key *key, struct btrfs_path *p,
			       int find_higher, int return_any)
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
				p->slots[0] = btrfs_header_nritems(leaf) - 1;
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
static void fixup_low_keys(struct btrfs_root *root, struct btrfs_path *path,
			   struct btrfs_disk_key *key, int level)
{
	int i;
	struct extent_buffer *t;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		int tslot = path->slots[i];
		if (!path->nodes[i])
			break;
		t = path->nodes[i];
		tree_mod_log_set_node_key(root->fs_info, t, tslot, 1);
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
void btrfs_set_item_key_safe(struct btrfs_root *root, struct btrfs_path *path,
			     struct btrfs_key *new_key)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *eb;
	int slot;

	eb = path->nodes[0];
	slot = path->slots[0];
	if (slot > 0) {
		btrfs_item_key(eb, &disk_key, slot - 1);
		BUG_ON(comp_keys(&disk_key, new_key) >= 0);
	}
	if (slot < btrfs_header_nritems(eb) - 1) {
		btrfs_item_key(eb, &disk_key, slot + 1);
		BUG_ON(comp_keys(&disk_key, new_key) <= 0);
	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(eb, &disk_key, slot);
	btrfs_mark_buffer_dirty(eb);
	if (slot == 0)
		fixup_low_keys(root, path, &disk_key, 1);
}

/*
 * try to push data from one node into the next node left in the
 * tree.
 *
 * returns 0 if some ptrs were pushed left, < 0 if there was some horrible
 * error, and > 0 if there was no room in the left hand block.
 */
static int push_node_left(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *dst,
			  struct extent_buffer *src, int empty)
{
	int push_items = 0;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
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

	tree_mod_log_eb_copy(root->fs_info, dst, src, dst_nritems, 0,
			     push_items);
	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(dst_nritems),
			   btrfs_node_key_ptr_offset(0),
			   push_items * sizeof(struct btrfs_key_ptr));

	if (push_items < src_nritems) {
		/*
		 * don't call tree_mod_log_eb_move here, key removal was already
		 * fully logged by tree_mod_log_eb_copy above.
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
			      struct btrfs_root *root,
			      struct extent_buffer *dst,
			      struct extent_buffer *src)
{
	int push_items = 0;
	int max_push;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
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

	tree_mod_log_eb_move(root->fs_info, dst, push_items, 0, dst_nritems);
	memmove_extent_buffer(dst, btrfs_node_key_ptr_offset(push_items),
				      btrfs_node_key_ptr_offset(0),
				      (dst_nritems) *
				      sizeof(struct btrfs_key_ptr));

	tree_mod_log_eb_copy(root->fs_info, dst, src, 0,
			     src_nritems - push_items, push_items);
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
	u64 lower_gen;
	struct extent_buffer *lower;
	struct extent_buffer *c;
	struct extent_buffer *old;
	struct btrfs_disk_key lower_key;

	BUG_ON(path->nodes[level]);
	BUG_ON(path->nodes[level-1] != root->node);

	lower = path->nodes[level-1];
	if (level == 1)
		btrfs_item_key(lower, &lower_key, 0);
	else
		btrfs_node_key(lower, &lower_key, 0);

	c = btrfs_alloc_free_block(trans, root, root->nodesize, 0,
				   root->root_key.objectid, &lower_key,
				   level, root->node->start, 0);
	if (IS_ERR(c))
		return PTR_ERR(c);

	root_add_used(root, root->nodesize);

	memset_extent_buffer(c, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_nritems(c, 1);
	btrfs_set_header_level(c, level);
	btrfs_set_header_bytenr(c, c->start);
	btrfs_set_header_generation(c, trans->transid);
	btrfs_set_header_backref_rev(c, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(c, root->root_key.objectid);

	write_extent_buffer(c, root->fs_info->fsid, btrfs_header_fsid(c),
			    BTRFS_FSID_SIZE);

	write_extent_buffer(c, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(c),
			    BTRFS_UUID_SIZE);

	btrfs_set_node_key(c, &lower_key, 0);
	btrfs_set_node_blockptr(c, 0, lower->start);
	lower_gen = btrfs_header_generation(lower);
	WARN_ON(lower_gen != trans->transid);

	btrfs_set_node_ptr_generation(c, 0, lower_gen);

	btrfs_mark_buffer_dirty(c);

	old = root->node;
	tree_mod_log_set_root_pointer(root, c, 0);
	rcu_assign_pointer(root->node, c);

	/* the super has an extra ref to root->node */
	free_extent_buffer(old);

	add_root_to_dirty_list(root);
	extent_buffer_get(c);
	path->nodes[level] = c;
	path->locks[level] = BTRFS_WRITE_LOCK;
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
		       struct btrfs_root *root, struct btrfs_path *path,
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
	BUG_ON(nritems == BTRFS_NODEPTRS_PER_BLOCK(root));
	if (slot != nritems) {
		if (level)
			tree_mod_log_eb_move(root->fs_info, lower, slot + 1,
					     slot, nritems - slot);
		memmove_extent_buffer(lower,
			      btrfs_node_key_ptr_offset(slot + 1),
			      btrfs_node_key_ptr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_key_ptr));
	}
	if (level) {
		ret = tree_mod_log_insert_key(root->fs_info, lower, slot,
					      MOD_LOG_KEY_ADD, GFP_NOFS);
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
		    BTRFS_NODEPTRS_PER_BLOCK(root) - 3)
			return 0;
		if (ret < 0)
			return ret;
	}

	c_nritems = btrfs_header_nritems(c);
	mid = (c_nritems + 1) / 2;
	btrfs_node_key(c, &disk_key, mid);

	split = btrfs_alloc_free_block(trans, root, root->nodesize, 0,
					root->root_key.objectid,
					&disk_key, level, c->start, 0);
	if (IS_ERR(split))
		return PTR_ERR(split);

	root_add_used(root, root->nodesize);

	memset_extent_buffer(split, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_level(split, btrfs_header_level(c));
	btrfs_set_header_bytenr(split, split->start);
	btrfs_set_header_generation(split, trans->transid);
	btrfs_set_header_backref_rev(split, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(split, root->root_key.objectid);
	write_extent_buffer(split, root->fs_info->fsid,
			    btrfs_header_fsid(split), BTRFS_FSID_SIZE);
	write_extent_buffer(split, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(split),
			    BTRFS_UUID_SIZE);

	tree_mod_log_eb_copy(root->fs_info, split, c, 0, mid, c_nritems - mid);
	copy_extent_buffer(split, c,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(mid),
			   (c_nritems - mid) * sizeof(struct btrfs_key_ptr));
	btrfs_set_header_nritems(split, c_nritems - mid);
	btrfs_set_header_nritems(c, mid);
	ret = 0;

	btrfs_mark_buffer_dirty(c);
	btrfs_mark_buffer_dirty(split);

	insert_ptr(trans, root, path, &disk_key, split->start,
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
	struct btrfs_map_token token;
	int data_len;
	int nritems = btrfs_header_nritems(l);
	int end = min(nritems, start + nr) - 1;

	if (!nr)
		return 0;
	btrfs_init_map_token(&token);
	start_item = btrfs_item_nr(l, start);
	end_item = btrfs_item_nr(l, end);
	data_len = btrfs_token_item_offset(l, start_item, &token) +
		btrfs_token_item_size(l, start_item, &token);
	data_len = data_len - btrfs_token_item_offset(l, end_item, &token);
	data_len += sizeof(struct btrfs_item) * nr;
	WARN_ON(data_len < 0);
	return data_len;
}

/*
 * The space between the end of the leaf items and
 * the start of the leaf data.  IOW, how much room
 * the leaf has left for both items and data
 */
noinline int btrfs_leaf_free_space(struct btrfs_root *root,
				   struct extent_buffer *leaf)
{
	int nritems = btrfs_header_nritems(leaf);
	int ret;
	ret = BTRFS_LEAF_DATA_SIZE(root) - leaf_space_used(leaf, 0, nritems);
	if (ret < 0) {
		printk(KERN_CRIT "leaf free space ret %d, leaf data size %lu, "
		       "used %d nritems %d\n",
		       ret, (unsigned long) BTRFS_LEAF_DATA_SIZE(root),
		       leaf_space_used(leaf, 0, nritems), nritems);
	}
	return ret;
}

/*
 * min slot controls the lowest index we're willing to push to the
 * right.  We'll push up to and including min_slot, but no lower
 */
static noinline int __push_leaf_right(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      int data_size, int empty,
				      struct extent_buffer *right,
				      int free_space, u32 left_nritems,
				      u32 min_slot)
{
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

	btrfs_init_map_token(&token);

	if (empty)
		nr = 0;
	else
		nr = max_t(u32, 1, min_slot);

	if (path->slots[0] >= left_nritems)
		push_space += data_size;

	slot = path->slots[1];
	i = left_nritems - 1;
	while (i >= nr) {
		item = btrfs_item_nr(left, i);

		if (!empty && push_items > 0) {
			if (path->slots[0] > i)
				break;
			if (path->slots[0] == i) {
				int space = btrfs_leaf_free_space(root, left);
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
	push_space -= leaf_data_end(root, left);

	/* make room in the right data area */
	data_end = leaf_data_end(root, right);
	memmove_extent_buffer(right,
			      btrfs_leaf_data(right) + data_end - push_space,
			      btrfs_leaf_data(right) + data_end,
			      BTRFS_LEAF_DATA_SIZE(root) - data_end);

	/* copy from the left data area */
	copy_extent_buffer(right, left, btrfs_leaf_data(right) +
		     BTRFS_LEAF_DATA_SIZE(root) - push_space,
		     btrfs_leaf_data(left) + leaf_data_end(root, left),
		     push_space);

	memmove_extent_buffer(right, btrfs_item_nr_offset(push_items),
			      btrfs_item_nr_offset(0),
			      right_nritems * sizeof(struct btrfs_item));

	/* copy the items from left to right */
	copy_extent_buffer(right, left, btrfs_item_nr_offset(0),
		   btrfs_item_nr_offset(left_nritems - push_items),
		   push_items * sizeof(struct btrfs_item));

	/* update the item pointers */
	right_nritems += push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(right, i);
		push_space -= btrfs_token_item_size(right, item, &token);
		btrfs_set_token_item_offset(right, item, push_space, &token);
	}

	left_nritems -= push_items;
	btrfs_set_header_nritems(left, left_nritems);

	if (left_nritems)
		btrfs_mark_buffer_dirty(left);
	else
		clean_tree_block(trans, root, left);

	btrfs_mark_buffer_dirty(right);

	btrfs_item_key(right, &disk_key, 0);
	btrfs_set_node_key(upper, &disk_key, slot + 1);
	btrfs_mark_buffer_dirty(upper);

	/* then fixup the leaf pointer in the path */
	if (path->slots[0] >= left_nritems) {
		path->slots[0] -= left_nritems;
		if (btrfs_header_nritems(path->nodes[0]) == 0)
			clean_tree_block(trans, root, path->nodes[0]);
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

	right = read_node_slot(root, upper, slot + 1);
	if (right == NULL)
		return 1;

	btrfs_tree_lock(right);
	btrfs_set_lock_blocking(right);

	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size)
		goto out_unlock;

	/* cow and double check */
	ret = btrfs_cow_block(trans, root, right, upper,
			      slot + 1, &right);
	if (ret)
		goto out_unlock;

	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size)
		goto out_unlock;

	left_nritems = btrfs_header_nritems(left);
	if (left_nritems == 0)
		goto out_unlock;

	return __push_leaf_right(trans, root, path, min_data_size, empty,
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
static noinline int __push_leaf_left(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct btrfs_path *path, int data_size,
				     int empty, struct extent_buffer *left,
				     int free_space, u32 right_nritems,
				     u32 max_slot)
{
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

	btrfs_init_map_token(&token);

	if (empty)
		nr = min(right_nritems, max_slot);
	else
		nr = min(right_nritems - 1, max_slot);

	for (i = 0; i < nr; i++) {
		item = btrfs_item_nr(right, i);

		if (!empty && push_items > 0) {
			if (path->slots[0] < i)
				break;
			if (path->slots[0] == i) {
				int space = btrfs_leaf_free_space(root, right);
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
	if (!empty && push_items == btrfs_header_nritems(right))
		WARN_ON(1);

	/* push data from right to left */
	copy_extent_buffer(left, right,
			   btrfs_item_nr_offset(btrfs_header_nritems(left)),
			   btrfs_item_nr_offset(0),
			   push_items * sizeof(struct btrfs_item));

	push_space = BTRFS_LEAF_DATA_SIZE(root) -
		     btrfs_item_offset_nr(right, push_items - 1);

	copy_extent_buffer(left, right, btrfs_leaf_data(left) +
		     leaf_data_end(root, left) - push_space,
		     btrfs_leaf_data(right) +
		     btrfs_item_offset_nr(right, push_items - 1),
		     push_space);
	old_left_nritems = btrfs_header_nritems(left);
	BUG_ON(old_left_nritems <= 0);

	old_left_item_size = btrfs_item_offset_nr(left, old_left_nritems - 1);
	for (i = old_left_nritems; i < old_left_nritems + push_items; i++) {
		u32 ioff;

		item = btrfs_item_nr(left, i);

		ioff = btrfs_token_item_offset(left, item, &token);
		btrfs_set_token_item_offset(left, item,
		      ioff - (BTRFS_LEAF_DATA_SIZE(root) - old_left_item_size),
		      &token);
	}
	btrfs_set_header_nritems(left, old_left_nritems + push_items);

	/* fixup right node */
	if (push_items > right_nritems)
		WARN(1, KERN_CRIT "push items %d nr %u\n", push_items,
		       right_nritems);

	if (push_items < right_nritems) {
		push_space = btrfs_item_offset_nr(right, push_items - 1) -
						  leaf_data_end(root, right);
		memmove_extent_buffer(right, btrfs_leaf_data(right) +
				      BTRFS_LEAF_DATA_SIZE(root) - push_space,
				      btrfs_leaf_data(right) +
				      leaf_data_end(root, right), push_space);

		memmove_extent_buffer(right, btrfs_item_nr_offset(0),
			      btrfs_item_nr_offset(push_items),
			     (btrfs_header_nritems(right) - push_items) *
			     sizeof(struct btrfs_item));
	}
	right_nritems -= push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(right, i);

		push_space = push_space - btrfs_token_item_size(right,
								item, &token);
		btrfs_set_token_item_offset(right, item, push_space, &token);
	}

	btrfs_mark_buffer_dirty(left);
	if (right_nritems)
		btrfs_mark_buffer_dirty(right);
	else
		clean_tree_block(trans, root, right);

	btrfs_item_key(right, &disk_key, 0);
	fixup_low_keys(root, path, &disk_key, 1);

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

	left = read_node_slot(root, path->nodes[1], slot - 1);
	if (left == NULL)
		return 1;

	btrfs_tree_lock(left);
	btrfs_set_lock_blocking(left);

	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size) {
		ret = 1;
		goto out;
	}

	/* cow and double check */
	ret = btrfs_cow_block(trans, root, left,
			      path->nodes[1], slot - 1, &left);
	if (ret) {
		/* we hit -ENOSPC, but it isn't fatal here */
		if (ret == -ENOSPC)
			ret = 1;
		goto out;
	}

	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size) {
		ret = 1;
		goto out;
	}

	return __push_leaf_left(trans, root, path, min_data_size,
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
				    struct btrfs_root *root,
				    struct btrfs_path *path,
				    struct extent_buffer *l,
				    struct extent_buffer *right,
				    int slot, int mid, int nritems)
{
	int data_copy_size;
	int rt_data_off;
	int i;
	struct btrfs_disk_key disk_key;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	nritems = nritems - mid;
	btrfs_set_header_nritems(right, nritems);
	data_copy_size = btrfs_item_end_nr(l, mid) - leaf_data_end(root, l);

	copy_extent_buffer(right, l, btrfs_item_nr_offset(0),
			   btrfs_item_nr_offset(mid),
			   nritems * sizeof(struct btrfs_item));

	copy_extent_buffer(right, l,
		     btrfs_leaf_data(right) + BTRFS_LEAF_DATA_SIZE(root) -
		     data_copy_size, btrfs_leaf_data(l) +
		     leaf_data_end(root, l), data_copy_size);

	rt_data_off = BTRFS_LEAF_DATA_SIZE(root) -
		      btrfs_item_end_nr(l, mid);

	for (i = 0; i < nritems; i++) {
		struct btrfs_item *item = btrfs_item_nr(right, i);
		u32 ioff;

		ioff = btrfs_token_item_offset(right, item, &token);
		btrfs_set_token_item_offset(right, item,
					    ioff + rt_data_off, &token);
	}

	btrfs_set_header_nritems(l, mid);
	btrfs_item_key(right, &disk_key, 0);
	insert_ptr(trans, root, path, &disk_key, right->start,
		   path->slots[1] + 1, 1);

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

	slot = path->slots[0];

	/*
	 * try to push all the items after our slot into the
	 * right leaf
	 */
	ret = push_leaf_right(trans, root, path, 1, data_size, 0, slot);
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

	if (btrfs_leaf_free_space(root, path->nodes[0]) >= data_size)
		return 0;

	/* try to push all the items before our slot into the next leaf */
	slot = path->slots[0];
	ret = push_leaf_left(trans, root, path, 1, data_size, 0, slot);
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
			       struct btrfs_key *ins_key,
			       struct btrfs_path *path, int data_size,
			       int extend)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *l;
	u32 nritems;
	int mid;
	int slot;
	struct extent_buffer *right;
	int ret = 0;
	int wret;
	int split;
	int num_doubles = 0;
	int tried_avoid_double = 0;

	l = path->nodes[0];
	slot = path->slots[0];
	if (extend && data_size + btrfs_item_size_nr(l, slot) +
	    sizeof(struct btrfs_item) > BTRFS_LEAF_DATA_SIZE(root))
		return -EOVERFLOW;

	/* first try to make some room by pushing left and right */
	if (data_size && path->nodes[1]) {
		wret = push_leaf_right(trans, root, path, data_size,
				       data_size, 0, 0);
		if (wret < 0)
			return wret;
		if (wret) {
			wret = push_leaf_left(trans, root, path, data_size,
					      data_size, 0, (u32)-1);
			if (wret < 0)
				return wret;
		}
		l = path->nodes[0];

		/* did the pushes work? */
		if (btrfs_leaf_free_space(root, l) >= data_size)
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
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (slot >= nritems) {
				split = 0;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size > BTRFS_LEAF_DATA_SIZE(root)) {
					if (data_size && !tried_avoid_double)
						goto push_for_double;
					split = 2;
				}
			}
		}
	} else {
		if (leaf_space_used(l, 0, mid) + data_size >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (!extend && data_size && slot == 0) {
				split = 0;
			} else if ((extend || !data_size) && slot == 0) {
				mid = 1;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size > BTRFS_LEAF_DATA_SIZE(root)) {
					if (data_size && !tried_avoid_double)
						goto push_for_double;
					split = 2 ;
				}
			}
		}
	}

	if (split == 0)
		btrfs_cpu_key_to_disk(&disk_key, ins_key);
	else
		btrfs_item_key(l, &disk_key, mid);

	right = btrfs_alloc_free_block(trans, root, root->leafsize, 0,
					root->root_key.objectid,
					&disk_key, 0, l->start, 0);
	if (IS_ERR(right))
		return PTR_ERR(right);

	root_add_used(root, root->leafsize);

	memset_extent_buffer(right, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(right, right->start);
	btrfs_set_header_generation(right, trans->transid);
	btrfs_set_header_backref_rev(right, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(right, root->root_key.objectid);
	btrfs_set_header_level(right, 0);
	write_extent_buffer(right, root->fs_info->fsid,
			    btrfs_header_fsid(right), BTRFS_FSID_SIZE);

	write_extent_buffer(right, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(right),
			    BTRFS_UUID_SIZE);

	if (split == 0) {
		if (mid <= slot) {
			btrfs_set_header_nritems(right, 0);
			insert_ptr(trans, root, path, &disk_key, right->start,
				   path->slots[1] + 1, 1);
			btrfs_tree_unlock(path->nodes[0]);
			free_extent_buffer(path->nodes[0]);
			path->nodes[0] = right;
			path->slots[0] = 0;
			path->slots[1] += 1;
		} else {
			btrfs_set_header_nritems(right, 0);
			insert_ptr(trans, root, path, &disk_key, right->start,
					  path->slots[1], 1);
			btrfs_tree_unlock(path->nodes[0]);
			free_extent_buffer(path->nodes[0]);
			path->nodes[0] = right;
			path->slots[0] = 0;
			if (path->slots[1] == 0)
				fixup_low_keys(root, path, &disk_key, 1);
		}
		btrfs_mark_buffer_dirty(right);
		return ret;
	}

	copy_for_split(trans, root, path, l, right, slot, mid, nritems);

	if (split == 2) {
		BUG_ON(num_doubles != 0);
		num_doubles++;
		goto again;
	}

	return 0;

push_for_double:
	push_for_double_split(trans, root, path, data_size);
	tried_avoid_double = 1;
	if (btrfs_leaf_free_space(root, path->nodes[0]) >= data_size)
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

	if (btrfs_leaf_free_space(root, leaf) >= ins_len)
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
	if (ret < 0)
		goto err;

	ret = -EAGAIN;
	leaf = path->nodes[0];
	/* if our item isn't there or got smaller, return now */
	if (ret > 0 || item_size != btrfs_item_size_nr(leaf, path->slots[0]))
		goto err;

	/* the leaf has  changed, it now has room.  return now */
	if (btrfs_leaf_free_space(root, path->nodes[0]) >= ins_len)
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

static noinline int split_item(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_path *path,
			       struct btrfs_key *new_key,
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
	BUG_ON(btrfs_leaf_free_space(root, leaf) < sizeof(struct btrfs_item));

	btrfs_set_path_blocking(path);

	item = btrfs_item_nr(leaf, path->slots[0]);
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

	new_item = btrfs_item_nr(leaf, slot);

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

	BUG_ON(btrfs_leaf_free_space(root, leaf) < 0);
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
		     struct btrfs_key *new_key,
		     unsigned long split_offset)
{
	int ret;
	ret = setup_leaf_for_split(trans, root, path,
				   sizeof(struct btrfs_item));
	if (ret)
		return ret;

	ret = split_item(trans, root, path, new_key, split_offset);
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
			 struct btrfs_key *new_key)
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
	setup_items_for_insert(root, path, new_key, &item_size,
			       item_size, item_size +
			       sizeof(struct btrfs_item), 1);
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
void btrfs_truncate_item(struct btrfs_root *root, struct btrfs_path *path,
			 u32 new_size, int from_end)
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

	btrfs_init_map_token(&token);

	leaf = path->nodes[0];
	slot = path->slots[0];

	old_size = btrfs_item_size_nr(leaf, slot);
	if (old_size == new_size)
		return;

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	old_data_start = btrfs_item_offset_nr(leaf, slot);

	size_diff = old_size - new_size;

	BUG_ON(slot < 0);
	BUG_ON(slot >= nritems);

	/*
	 * item0..itemN ... dataN.offset..dataN.size .. data0.size
	 */
	/* first correct the data pointers */
	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(leaf, i);

		ioff = btrfs_token_item_offset(leaf, item, &token);
		btrfs_set_token_item_offset(leaf, item,
					    ioff + size_diff, &token);
	}

	/* shift the data */
	if (from_end) {
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + size_diff, btrfs_leaf_data(leaf) +
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
				      offsetof(struct btrfs_file_extent_item,
						 disk_bytenr));
			}
		}

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + size_diff, btrfs_leaf_data(leaf) +
			      data_end, old_data_start - data_end);

		offset = btrfs_disk_key_offset(&disk_key);
		btrfs_set_disk_key_offset(&disk_key, offset + size_diff);
		btrfs_set_item_key(leaf, &disk_key, slot);
		if (slot == 0)
			fixup_low_keys(root, path, &disk_key, 1);
	}

	item = btrfs_item_nr(leaf, slot);
	btrfs_set_item_size(leaf, item, new_size);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
}

/*
 * make the item pointed to by the path bigger, data_size is the added size.
 */
void btrfs_extend_item(struct btrfs_root *root, struct btrfs_path *path,
		       u32 data_size)
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

	btrfs_init_map_token(&token);

	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < data_size) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
	slot = path->slots[0];
	old_data = btrfs_item_end_nr(leaf, slot);

	BUG_ON(slot < 0);
	if (slot >= nritems) {
		btrfs_print_leaf(root, leaf);
		printk(KERN_CRIT "slot %d too large, nritems %d\n",
		       slot, nritems);
		BUG_ON(1);
	}

	/*
	 * item0..itemN ... dataN.offset..dataN.size .. data0.size
	 */
	/* first correct the data pointers */
	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(leaf, i);

		ioff = btrfs_token_item_offset(leaf, item, &token);
		btrfs_set_token_item_offset(leaf, item,
					    ioff - data_size, &token);
	}

	/* shift the data */
	memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
		      data_end - data_size, btrfs_leaf_data(leaf) +
		      data_end, old_data - data_end);

	data_end = old_data;
	old_size = btrfs_item_size_nr(leaf, slot);
	item = btrfs_item_nr(leaf, slot);
	btrfs_set_item_size(leaf, item, old_size + data_size);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
}

/*
 * this is a helper for btrfs_insert_empty_items, the main goal here is
 * to save stack depth by doing the bulk of the work in a function
 * that doesn't call btrfs_search_slot
 */
void setup_items_for_insert(struct btrfs_root *root, struct btrfs_path *path,
			    struct btrfs_key *cpu_key, u32 *data_size,
			    u32 total_data, u32 total_size, int nr)
{
	struct btrfs_item *item;
	int i;
	u32 nritems;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;
	struct extent_buffer *leaf;
	int slot;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	leaf = path->nodes[0];
	slot = path->slots[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < total_size) {
		btrfs_print_leaf(root, leaf);
		printk(KERN_CRIT "not enough freespace need %u have %d\n",
		       total_size, btrfs_leaf_free_space(root, leaf));
		BUG();
	}

	if (slot != nritems) {
		unsigned int old_data = btrfs_item_end_nr(leaf, slot);

		if (old_data < data_end) {
			btrfs_print_leaf(root, leaf);
			printk(KERN_CRIT "slot %d old_data %d data_end %d\n",
			       slot, old_data, data_end);
			BUG_ON(1);
		}
		/*
		 * item0..itemN ... dataN.offset..dataN.size .. data0.size
		 */
		/* first correct the data pointers */
		for (i = slot; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(leaf, i);
			ioff = btrfs_token_item_offset(leaf, item, &token);
			btrfs_set_token_item_offset(leaf, item,
						    ioff - total_data, &token);
		}
		/* shift the items */
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + nr),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

		/* shift the data */
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end - total_data, btrfs_leaf_data(leaf) +
			      data_end, old_data - data_end);
		data_end = old_data;
	}

	/* setup the item for the new data */
	for (i = 0; i < nr; i++) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key + i);
		btrfs_set_item_key(leaf, &disk_key, slot + i);
		item = btrfs_item_nr(leaf, slot + i);
		btrfs_set_token_item_offset(leaf, item,
					    data_end - data_size[i], &token);
		data_end -= data_size[i];
		btrfs_set_token_item_size(leaf, item, data_size[i], &token);
	}

	btrfs_set_header_nritems(leaf, nritems + nr);

	if (slot == 0) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key);
		fixup_low_keys(root, path, &disk_key, 1);
	}
	btrfs_unlock_up_safe(path, 1);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
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
			    struct btrfs_key *cpu_key, u32 *data_size,
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

	setup_items_for_insert(root, path, cpu_key, data_size,
			       total_data, total_size, nr);
	return 0;
}

/*
 * Given a key and some data, insert an item into the tree.
 * This does all the path init required, making room in the tree if needed.
 */
int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *cpu_key, void *data, u32
		      data_size)
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
		if (level)
			tree_mod_log_eb_move(root->fs_info, parent, slot,
					     slot + 1, nritems - slot - 1);
		memmove_extent_buffer(parent,
			      btrfs_node_key_ptr_offset(slot),
			      btrfs_node_key_ptr_offset(slot + 1),
			      sizeof(struct btrfs_key_ptr) *
			      (nritems - slot - 1));
	} else if (level) {
		ret = tree_mod_log_insert_key(root->fs_info, parent, slot,
					      MOD_LOG_KEY_REMOVE, GFP_NOFS);
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
		fixup_low_keys(root, path, &disk_key, level + 1);
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

	extent_buffer_get(leaf);
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
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	int last_off;
	int dsize = 0;
	int ret = 0;
	int wret;
	int i;
	u32 nritems;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	leaf = path->nodes[0];
	last_off = btrfs_item_offset_nr(leaf, slot + nr - 1);

	for (i = 0; i < nr; i++)
		dsize += btrfs_item_size_nr(leaf, slot + i);

	nritems = btrfs_header_nritems(leaf);

	if (slot + nr != nritems) {
		int data_end = leaf_data_end(root, leaf);

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + dsize,
			      btrfs_leaf_data(leaf) + data_end,
			      last_off - data_end);

		for (i = slot + nr; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(leaf, i);
			ioff = btrfs_token_item_offset(leaf, item, &token);
			btrfs_set_token_item_offset(leaf, item,
						    ioff + dsize, &token);
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
			clean_tree_block(trans, root, leaf);
			btrfs_del_leaf(trans, root, path, leaf);
		}
	} else {
		int used = leaf_space_used(leaf, 0, nritems);
		if (slot == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_item_key(leaf, &disk_key, 0);
			fixup_low_keys(root, path, &disk_key, 1);
		}

		/* delete the leaf if it is mostly empty */
		if (used < BTRFS_LEAF_DATA_SIZE(root) / 3) {
			/* push_leaf_left fixes the path.
			 * make sure the path still points to our leaf
			 * for possible call to del_ptr below
			 */
			slot = path->slots[1];
			extent_buffer_get(leaf);

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
static int btrfs_prev_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	struct btrfs_key key;
	struct btrfs_disk_key found_key;
	int ret;

	btrfs_item_key_to_cpu(path->nodes[0], &key, 0);

	if (key.offset > 0)
		key.offset--;
	else if (key.type > 0)
		key.type--;
	else if (key.objectid > 0)
		key.objectid--;
	else
		return 1;

	btrfs_release_path(path);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ret;
	btrfs_item_key(path->nodes[0], &found_key, 0);
	ret = comp_keys(&found_key, &key);
	if (ret < 0)
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
 * This does lock as it descends, and path->keep_locks should be set
 * to 1 by the caller.
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
			 struct btrfs_key *max_key,
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

	WARN_ON(!path->keep_locks);
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
		sret = bin_search(cur, min_key, level, &slot);

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
		 * If it is too old, old, skip to the next one.
		 */
		while (slot < nritems) {
			u64 blockptr;
			u64 gen;

			blockptr = btrfs_node_blockptr(cur, slot);
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
			unlock_up(path, level, 1, 0, NULL);
			goto out;
		}
		btrfs_set_path_blocking(path);
		cur = read_node_slot(root, cur, slot);
		BUG_ON(!cur); /* -ENOMEM */

		btrfs_tree_read_lock(cur);

		path->locks[level - 1] = BTRFS_READ_LOCK;
		path->nodes[level - 1] = cur;
		unlock_up(path, level, 1, 0, NULL);
		btrfs_clear_path_blocking(path, NULL, 0);
	}
out:
	if (ret == 0)
		memcpy(min_key, &found_key, sizeof(found_key));
	btrfs_set_path_blocking(path);
	return ret;
}

static void tree_move_down(struct btrfs_root *root,
			   struct btrfs_path *path,
			   int *level, int root_level)
{
	BUG_ON(*level == 0);
	path->nodes[*level - 1] = read_node_slot(root, path->nodes[*level],
					path->slots[*level]);
	path->slots[*level - 1] = 0;
	(*level)--;
}

static int tree_move_next_or_upnext(struct btrfs_root *root,
				    struct btrfs_path *path,
				    int *level, int root_level)
{
	int ret = 0;
	int nritems;
	nritems = btrfs_header_nritems(path->nodes[*level]);

	path->slots[*level]++;

	while (path->slots[*level] >= nritems) {
		if (*level == root_level)
			return -1;

		/* move upnext */
		path->slots[*level] = 0;
		free_extent_buffer(path->nodes[*level]);
		path->nodes[*level] = NULL;
		(*level)++;
		path->slots[*level]++;

		nritems = btrfs_header_nritems(path->nodes[*level]);
		ret = 1;
	}
	return ret;
}

/*
 * Returns 1 if it had to move up and next. 0 is returned if it moved only next
 * or down.
 */
static int tree_advance(struct btrfs_root *root,
			struct btrfs_path *path,
			int *level, int root_level,
			int allow_down,
			struct btrfs_key *key)
{
	int ret;

	if (*level == 0 || !allow_down) {
		ret = tree_move_next_or_upnext(root, path, level, root_level);
	} else {
		tree_move_down(root, path, level, root_level);
		ret = 0;
	}
	if (ret >= 0) {
		if (*level == 0)
			btrfs_item_key_to_cpu(path->nodes[*level], key,
					path->slots[*level]);
		else
			btrfs_node_key_to_cpu(path->nodes[*level], key,
					path->slots[*level]);
	}
	return ret;
}

static int tree_compare_item(struct btrfs_root *left_root,
			     struct btrfs_path *left_path,
			     struct btrfs_path *right_path,
			     char *tmp_buf)
{
	int cmp;
	int len1, len2;
	unsigned long off1, off2;

	len1 = btrfs_item_size_nr(left_path->nodes[0], left_path->slots[0]);
	len2 = btrfs_item_size_nr(right_path->nodes[0], right_path->slots[0]);
	if (len1 != len2)
		return 1;

	off1 = btrfs_item_ptr_offset(left_path->nodes[0], left_path->slots[0]);
	off2 = btrfs_item_ptr_offset(right_path->nodes[0],
				right_path->slots[0]);

	read_extent_buffer(left_path->nodes[0], tmp_buf, off1, len1);

	cmp = memcmp_extent_buffer(right_path->nodes[0], tmp_buf, off2, len1);
	if (cmp)
		return 1;
	return 0;
}

#define ADVANCE 1
#define ADVANCE_ONLY_NEXT -1

/*
 * This function compares two trees and calls the provided callback for
 * every changed/new/deleted item it finds.
 * If shared tree blocks are encountered, whole subtrees are skipped, making
 * the compare pretty fast on snapshotted subvolumes.
 *
 * This currently works on commit roots only. As commit roots are read only,
 * we don't do any locking. The commit roots are protected with transactions.
 * Transactions are ended and rejoined when a commit is tried in between.
 *
 * This function checks for modifications done to the trees while comparing.
 * If it detects a change, it aborts immediately.
 */
int btrfs_compare_trees(struct btrfs_root *left_root,
			struct btrfs_root *right_root,
			btrfs_changed_cb_t changed_cb, void *ctx)
{
	int ret;
	int cmp;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *left_path = NULL;
	struct btrfs_path *right_path = NULL;
	struct btrfs_key left_key;
	struct btrfs_key right_key;
	char *tmp_buf = NULL;
	int left_root_level;
	int right_root_level;
	int left_level;
	int right_level;
	int left_end_reached;
	int right_end_reached;
	int advance_left;
	int advance_right;
	u64 left_blockptr;
	u64 right_blockptr;
	u64 left_start_ctransid;
	u64 right_start_ctransid;
	u64 ctransid;

	left_path = btrfs_alloc_path();
	if (!left_path) {
		ret = -ENOMEM;
		goto out;
	}
	right_path = btrfs_alloc_path();
	if (!right_path) {
		ret = -ENOMEM;
		goto out;
	}

	tmp_buf = kmalloc(left_root->leafsize, GFP_NOFS);
	if (!tmp_buf) {
		ret = -ENOMEM;
		goto out;
	}

	left_path->search_commit_root = 1;
	left_path->skip_locking = 1;
	right_path->search_commit_root = 1;
	right_path->skip_locking = 1;

	spin_lock(&left_root->root_item_lock);
	left_start_ctransid = btrfs_root_ctransid(&left_root->root_item);
	spin_unlock(&left_root->root_item_lock);

	spin_lock(&right_root->root_item_lock);
	right_start_ctransid = btrfs_root_ctransid(&right_root->root_item);
	spin_unlock(&right_root->root_item_lock);

	trans = btrfs_join_transaction(left_root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	/*
	 * Strategy: Go to the first items of both trees. Then do
	 *
	 * If both trees are at level 0
	 *   Compare keys of current items
	 *     If left < right treat left item as new, advance left tree
	 *       and repeat
	 *     If left > right treat right item as deleted, advance right tree
	 *       and repeat
	 *     If left == right do deep compare of items, treat as changed if
	 *       needed, advance both trees and repeat
	 * If both trees are at the same level but not at level 0
	 *   Compare keys of current nodes/leafs
	 *     If left < right advance left tree and repeat
	 *     If left > right advance right tree and repeat
	 *     If left == right compare blockptrs of the next nodes/leafs
	 *       If they match advance both trees but stay at the same level
	 *         and repeat
	 *       If they don't match advance both trees while allowing to go
	 *         deeper and repeat
	 * If tree levels are different
	 *   Advance the tree that needs it and repeat
	 *
	 * Advancing a tree means:
	 *   If we are at level 0, try to go to the next slot. If that's not
	 *   possible, go one level up and repeat. Stop when we found a level
	 *   where we could go to the next slot. We may at this point be on a
	 *   node or a leaf.
	 *
	 *   If we are not at level 0 and not on shared tree blocks, go one
	 *   level deeper.
	 *
	 *   If we are not at level 0 and on shared tree blocks, go one slot to
	 *   the right if possible or go up and right.
	 */

	left_level = btrfs_header_level(left_root->commit_root);
	left_root_level = left_level;
	left_path->nodes[left_level] = left_root->commit_root;
	extent_buffer_get(left_path->nodes[left_level]);

	right_level = btrfs_header_level(right_root->commit_root);
	right_root_level = right_level;
	right_path->nodes[right_level] = right_root->commit_root;
	extent_buffer_get(right_path->nodes[right_level]);

	if (left_level == 0)
		btrfs_item_key_to_cpu(left_path->nodes[left_level],
				&left_key, left_path->slots[left_level]);
	else
		btrfs_node_key_to_cpu(left_path->nodes[left_level],
				&left_key, left_path->slots[left_level]);
	if (right_level == 0)
		btrfs_item_key_to_cpu(right_path->nodes[right_level],
				&right_key, right_path->slots[right_level]);
	else
		btrfs_node_key_to_cpu(right_path->nodes[right_level],
				&right_key, right_path->slots[right_level]);

	left_end_reached = right_end_reached = 0;
	advance_left = advance_right = 0;

	while (1) {
		/*
		 * We need to make sure the transaction does not get committed
		 * while we do anything on commit roots. This means, we need to
		 * join and leave transactions for every item that we process.
		 */
		if (trans && btrfs_should_end_transaction(trans, left_root)) {
			btrfs_release_path(left_path);
			btrfs_release_path(right_path);

			ret = btrfs_end_transaction(trans, left_root);
			trans = NULL;
			if (ret < 0)
				goto out;
		}
		/* now rejoin the transaction */
		if (!trans) {
			trans = btrfs_join_transaction(left_root);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				trans = NULL;
				goto out;
			}

			spin_lock(&left_root->root_item_lock);
			ctransid = btrfs_root_ctransid(&left_root->root_item);
			spin_unlock(&left_root->root_item_lock);
			if (ctransid != left_start_ctransid)
				left_start_ctransid = 0;

			spin_lock(&right_root->root_item_lock);
			ctransid = btrfs_root_ctransid(&right_root->root_item);
			spin_unlock(&right_root->root_item_lock);
			if (ctransid != right_start_ctransid)
				right_start_ctransid = 0;

			if (!left_start_ctransid || !right_start_ctransid) {
				WARN(1, KERN_WARNING
					"btrfs: btrfs_compare_tree detected "
					"a change in one of the trees while "
					"iterating. This is probably a "
					"bug.\n");
				ret = -EIO;
				goto out;
			}

			/*
			 * the commit root may have changed, so start again
			 * where we stopped
			 */
			left_path->lowest_level = left_level;
			right_path->lowest_level = right_level;
			ret = btrfs_search_slot(NULL, left_root,
					&left_key, left_path, 0, 0);
			if (ret < 0)
				goto out;
			ret = btrfs_search_slot(NULL, right_root,
					&right_key, right_path, 0, 0);
			if (ret < 0)
				goto out;
		}

		if (advance_left && !left_end_reached) {
			ret = tree_advance(left_root, left_path, &left_level,
					left_root_level,
					advance_left != ADVANCE_ONLY_NEXT,
					&left_key);
			if (ret < 0)
				left_end_reached = ADVANCE;
			advance_left = 0;
		}
		if (advance_right && !right_end_reached) {
			ret = tree_advance(right_root, right_path, &right_level,
					right_root_level,
					advance_right != ADVANCE_ONLY_NEXT,
					&right_key);
			if (ret < 0)
				right_end_reached = ADVANCE;
			advance_right = 0;
		}

		if (left_end_reached && right_end_reached) {
			ret = 0;
			goto out;
		} else if (left_end_reached) {
			if (right_level == 0) {
				ret = changed_cb(left_root, right_root,
						left_path, right_path,
						&right_key,
						BTRFS_COMPARE_TREE_DELETED,
						ctx);
				if (ret < 0)
					goto out;
			}
			advance_right = ADVANCE;
			continue;
		} else if (right_end_reached) {
			if (left_level == 0) {
				ret = changed_cb(left_root, right_root,
						left_path, right_path,
						&left_key,
						BTRFS_COMPARE_TREE_NEW,
						ctx);
				if (ret < 0)
					goto out;
			}
			advance_left = ADVANCE;
			continue;
		}

		if (left_level == 0 && right_level == 0) {
			cmp = btrfs_comp_cpu_keys(&left_key, &right_key);
			if (cmp < 0) {
				ret = changed_cb(left_root, right_root,
						left_path, right_path,
						&left_key,
						BTRFS_COMPARE_TREE_NEW,
						ctx);
				if (ret < 0)
					goto out;
				advance_left = ADVANCE;
			} else if (cmp > 0) {
				ret = changed_cb(left_root, right_root,
						left_path, right_path,
						&right_key,
						BTRFS_COMPARE_TREE_DELETED,
						ctx);
				if (ret < 0)
					goto out;
				advance_right = ADVANCE;
			} else {
				enum btrfs_compare_tree_result cmp;

				WARN_ON(!extent_buffer_uptodate(left_path->nodes[0]));
				ret = tree_compare_item(left_root, left_path,
						right_path, tmp_buf);
				if (ret)
					cmp = BTRFS_COMPARE_TREE_CHANGED;
				else
					cmp = BTRFS_COMPARE_TREE_SAME;
				ret = changed_cb(left_root, right_root,
						 left_path, right_path,
						 &left_key, cmp, ctx);
				if (ret < 0)
					goto out;
				advance_left = ADVANCE;
				advance_right = ADVANCE;
			}
		} else if (left_level == right_level) {
			cmp = btrfs_comp_cpu_keys(&left_key, &right_key);
			if (cmp < 0) {
				advance_left = ADVANCE;
			} else if (cmp > 0) {
				advance_right = ADVANCE;
			} else {
				left_blockptr = btrfs_node_blockptr(
						left_path->nodes[left_level],
						left_path->slots[left_level]);
				right_blockptr = btrfs_node_blockptr(
						right_path->nodes[right_level],
						right_path->slots[right_level]);
				if (left_blockptr == right_blockptr) {
					/*
					 * As we're on a shared block, don't
					 * allow to go deeper.
					 */
					advance_left = ADVANCE_ONLY_NEXT;
					advance_right = ADVANCE_ONLY_NEXT;
				} else {
					advance_left = ADVANCE;
					advance_right = ADVANCE;
				}
			}
		} else if (left_level < right_level) {
			advance_right = ADVANCE;
		} else {
			advance_left = ADVANCE;
		}
	}

out:
	btrfs_free_path(left_path);
	btrfs_free_path(right_path);
	kfree(tmp_buf);

	if (trans) {
		if (!ret)
			ret = btrfs_end_transaction(trans, left_root);
		else
			btrfs_end_transaction(trans, left_root);
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

	WARN_ON(!path->keep_locks);
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

			if (path->locks[level + 1]) {
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
		ret = read_block_for_search(NULL, root, path, &next, level,
					    slot, &key, 0);
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
				btrfs_tree_read_lock(next);
				btrfs_clear_path_blocking(path, next,
							  BTRFS_READ_LOCK);
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

		ret = read_block_for_search(NULL, root, path, &next, level,
					    0, &key, 0);
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
				btrfs_tree_read_lock(next);
				btrfs_clear_path_blocking(path, next,
							  BTRFS_READ_LOCK);
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
