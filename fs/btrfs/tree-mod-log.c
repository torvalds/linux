// SPDX-License-Identifier: GPL-2.0

#include "messages.h"
#include "tree-mod-log.h"
#include "disk-io.h"
#include "fs.h"
#include "accessors.h"
#include "tree-checker.h"

struct tree_mod_root {
	u64 logical;
	u8 level;
};

struct tree_mod_elem {
	struct rb_node node;
	u64 logical;
	u64 seq;
	enum btrfs_mod_log_op op;

	/*
	 * This is used for BTRFS_MOD_LOG_KEY_* and BTRFS_MOD_LOG_MOVE_KEYS
	 * operations.
	 */
	int slot;

	/* This is used for BTRFS_MOD_LOG_KEY* and BTRFS_MOD_LOG_ROOT_REPLACE. */
	u64 generation;

	/* Those are used for op == BTRFS_MOD_LOG_KEY_{REPLACE,REMOVE}. */
	struct btrfs_disk_key key;
	u64 blockptr;

	/* This is used for op == BTRFS_MOD_LOG_MOVE_KEYS. */
	struct {
		int dst_slot;
		int nr_items;
	} move;

	/* This is used for op == BTRFS_MOD_LOG_ROOT_REPLACE. */
	struct tree_mod_root old_root;
};

/*
 * Pull a new tree mod seq number for our operation.
 */
static u64 btrfs_inc_tree_mod_seq(struct btrfs_fs_info *fs_info)
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
			   struct btrfs_seq_list *elem)
{
	write_lock(&fs_info->tree_mod_log_lock);
	if (!elem->seq) {
		elem->seq = btrfs_inc_tree_mod_seq(fs_info);
		list_add_tail(&elem->list, &fs_info->tree_mod_seq_list);
		set_bit(BTRFS_FS_TREE_MOD_LOG_USERS, &fs_info->flags);
	}
	write_unlock(&fs_info->tree_mod_log_lock);

	return elem->seq;
}

void btrfs_put_tree_mod_seq(struct btrfs_fs_info *fs_info,
			    struct btrfs_seq_list *elem)
{
	struct rb_root *tm_root;
	struct rb_node *node;
	struct rb_node *next;
	struct tree_mod_elem *tm;
	u64 min_seq = BTRFS_SEQ_LAST;
	u64 seq_putting = elem->seq;

	if (!seq_putting)
		return;

	write_lock(&fs_info->tree_mod_log_lock);
	list_del(&elem->list);
	elem->seq = 0;

	if (list_empty(&fs_info->tree_mod_seq_list)) {
		clear_bit(BTRFS_FS_TREE_MOD_LOG_USERS, &fs_info->flags);
	} else {
		struct btrfs_seq_list *first;

		first = list_first_entry(&fs_info->tree_mod_seq_list,
					 struct btrfs_seq_list, list);
		if (seq_putting > first->seq) {
			/*
			 * Blocker with lower sequence number exists, we cannot
			 * remove anything from the log.
			 */
			write_unlock(&fs_info->tree_mod_log_lock);
			return;
		}
		min_seq = first->seq;
	}

	/*
	 * Anything that's lower than the lowest existing (read: blocked)
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
 * Key order of the log:
 *       node/leaf start address -> sequence
 *
 * The 'start address' is the logical address of the *new* root node for root
 * replace operations, or the logical address of the affected block for all
 * other operations.
 */
static noinline int tree_mod_log_insert(struct btrfs_fs_info *fs_info,
					struct tree_mod_elem *tm)
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
 * Determines if logging can be omitted. Returns true if it can. Otherwise, it
 * returns false with the tree_mod_log_lock acquired. The caller must hold
 * this until all tree mod log insertions are recorded in the rb tree and then
 * write unlock fs_info::tree_mod_log_lock.
 */
static bool tree_mod_dont_log(struct btrfs_fs_info *fs_info, struct extent_buffer *eb)
{
	if (!test_bit(BTRFS_FS_TREE_MOD_LOG_USERS, &fs_info->flags))
		return true;
	if (eb && btrfs_header_level(eb) == 0)
		return true;

	write_lock(&fs_info->tree_mod_log_lock);
	if (list_empty(&(fs_info)->tree_mod_seq_list)) {
		write_unlock(&fs_info->tree_mod_log_lock);
		return true;
	}

	return false;
}

/* Similar to tree_mod_dont_log, but doesn't acquire any locks. */
static bool tree_mod_need_log(const struct btrfs_fs_info *fs_info,
				    struct extent_buffer *eb)
{
	if (!test_bit(BTRFS_FS_TREE_MOD_LOG_USERS, &fs_info->flags))
		return false;
	if (eb && btrfs_header_level(eb) == 0)
		return false;

	return true;
}

static struct tree_mod_elem *alloc_tree_mod_elem(struct extent_buffer *eb,
						 int slot,
						 enum btrfs_mod_log_op op)
{
	struct tree_mod_elem *tm;

	tm = kzalloc(sizeof(*tm), GFP_NOFS);
	if (!tm)
		return NULL;

	tm->logical = eb->start;
	if (op != BTRFS_MOD_LOG_KEY_ADD) {
		btrfs_node_key(eb, &tm->key, slot);
		tm->blockptr = btrfs_node_blockptr(eb, slot);
	}
	tm->op = op;
	tm->slot = slot;
	tm->generation = btrfs_node_ptr_generation(eb, slot);
	RB_CLEAR_NODE(&tm->node);

	return tm;
}

int btrfs_tree_mod_log_insert_key(struct extent_buffer *eb, int slot,
				  enum btrfs_mod_log_op op)
{
	struct tree_mod_elem *tm;
	int ret = 0;

	if (!tree_mod_need_log(eb->fs_info, eb))
		return 0;

	tm = alloc_tree_mod_elem(eb, slot, op);
	if (!tm)
		ret = -ENOMEM;

	if (tree_mod_dont_log(eb->fs_info, eb)) {
		kfree(tm);
		/*
		 * Don't error if we failed to allocate memory because we don't
		 * need to log.
		 */
		return 0;
	} else if (ret != 0) {
		/*
		 * We previously failed to allocate memory and we need to log,
		 * so we have to fail.
		 */
		goto out_unlock;
	}

	ret = tree_mod_log_insert(eb->fs_info, tm);
out_unlock:
	write_unlock(&eb->fs_info->tree_mod_log_lock);
	if (ret)
		kfree(tm);

	return ret;
}

static struct tree_mod_elem *tree_mod_log_alloc_move(struct extent_buffer *eb,
						     int dst_slot, int src_slot,
						     int nr_items)
{
	struct tree_mod_elem *tm;

	tm = kzalloc(sizeof(*tm), GFP_NOFS);
	if (!tm)
		return ERR_PTR(-ENOMEM);

	tm->logical = eb->start;
	tm->slot = src_slot;
	tm->move.dst_slot = dst_slot;
	tm->move.nr_items = nr_items;
	tm->op = BTRFS_MOD_LOG_MOVE_KEYS;
	RB_CLEAR_NODE(&tm->node);

	return tm;
}

int btrfs_tree_mod_log_insert_move(struct extent_buffer *eb,
				   int dst_slot, int src_slot,
				   int nr_items)
{
	struct tree_mod_elem *tm = NULL;
	struct tree_mod_elem **tm_list = NULL;
	int ret = 0;
	int i;
	bool locked = false;

	if (!tree_mod_need_log(eb->fs_info, eb))
		return 0;

	tm_list = kcalloc(nr_items, sizeof(struct tree_mod_elem *), GFP_NOFS);
	if (!tm_list) {
		ret = -ENOMEM;
		goto lock;
	}

	tm = tree_mod_log_alloc_move(eb, dst_slot, src_slot, nr_items);
	if (IS_ERR(tm)) {
		ret = PTR_ERR(tm);
		tm = NULL;
		goto lock;
	}

	for (i = 0; i + dst_slot < src_slot && i < nr_items; i++) {
		tm_list[i] = alloc_tree_mod_elem(eb, i + dst_slot,
				BTRFS_MOD_LOG_KEY_REMOVE_WHILE_MOVING);
		if (!tm_list[i]) {
			ret = -ENOMEM;
			goto lock;
		}
	}

lock:
	if (tree_mod_dont_log(eb->fs_info, eb)) {
		/*
		 * Don't error if we failed to allocate memory because we don't
		 * need to log.
		 */
		ret = 0;
		goto free_tms;
	}
	locked = true;

	/*
	 * We previously failed to allocate memory and we need to log, so we
	 * have to fail.
	 */
	if (ret != 0)
		goto free_tms;

	/*
	 * When we override something during the move, we log these removals.
	 * This can only happen when we move towards the beginning of the
	 * buffer, i.e. dst_slot < src_slot.
	 */
	for (i = 0; i + dst_slot < src_slot && i < nr_items; i++) {
		ret = tree_mod_log_insert(eb->fs_info, tm_list[i]);
		if (ret)
			goto free_tms;
	}

	ret = tree_mod_log_insert(eb->fs_info, tm);
	if (ret)
		goto free_tms;
	write_unlock(&eb->fs_info->tree_mod_log_lock);
	kfree(tm_list);

	return 0;

free_tms:
	if (tm_list) {
		for (i = 0; i < nr_items; i++) {
			if (tm_list[i] && !RB_EMPTY_NODE(&tm_list[i]->node))
				rb_erase(&tm_list[i]->node, &eb->fs_info->tree_mod_log);
			kfree(tm_list[i]);
		}
	}
	if (locked)
		write_unlock(&eb->fs_info->tree_mod_log_lock);
	kfree(tm_list);
	kfree(tm);

	return ret;
}

static int tree_mod_log_free_eb(struct btrfs_fs_info *fs_info,
				struct tree_mod_elem **tm_list,
				int nritems)
{
	int i, j;
	int ret;

	for (i = nritems - 1; i >= 0; i--) {
		ret = tree_mod_log_insert(fs_info, tm_list[i]);
		if (ret) {
			for (j = nritems - 1; j > i; j--)
				rb_erase(&tm_list[j]->node,
					 &fs_info->tree_mod_log);
			return ret;
		}
	}

	return 0;
}

int btrfs_tree_mod_log_insert_root(struct extent_buffer *old_root,
				   struct extent_buffer *new_root,
				   bool log_removal)
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
			goto lock;
		}
		for (i = 0; i < nritems; i++) {
			tm_list[i] = alloc_tree_mod_elem(old_root, i,
			    BTRFS_MOD_LOG_KEY_REMOVE_WHILE_FREEING);
			if (!tm_list[i]) {
				ret = -ENOMEM;
				goto lock;
			}
		}
	}

	tm = kzalloc(sizeof(*tm), GFP_NOFS);
	if (!tm) {
		ret = -ENOMEM;
		goto lock;
	}

	tm->logical = new_root->start;
	tm->old_root.logical = old_root->start;
	tm->old_root.level = btrfs_header_level(old_root);
	tm->generation = btrfs_header_generation(old_root);
	tm->op = BTRFS_MOD_LOG_ROOT_REPLACE;

lock:
	if (tree_mod_dont_log(fs_info, NULL)) {
		/*
		 * Don't error if we failed to allocate memory because we don't
		 * need to log.
		 */
		ret = 0;
		goto free_tms;
	} else if (ret != 0) {
		/*
		 * We previously failed to allocate memory and we need to log,
		 * so we have to fail.
		 */
		goto out_unlock;
	}

	if (tm_list)
		ret = tree_mod_log_free_eb(fs_info, tm_list, nritems);
	if (!ret)
		ret = tree_mod_log_insert(fs_info, tm);

out_unlock:
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

static struct tree_mod_elem *__tree_mod_log_search(struct btrfs_fs_info *fs_info,
						   u64 start, u64 min_seq,
						   bool smallest)
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
			/* We want the node with the highest seq */
			if (found)
				BUG_ON(found->seq > cur->seq);
			found = cur;
			node = node->rb_left;
		} else if (cur->seq > min_seq) {
			/* We want the node with the smallest seq */
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
 * This returns the element from the log with the smallest time sequence
 * value that's in the log (the oldest log item). Any element with a time
 * sequence lower than min_seq will be ignored.
 */
static struct tree_mod_elem *tree_mod_log_search_oldest(struct btrfs_fs_info *fs_info,
							u64 start, u64 min_seq)
{
	return __tree_mod_log_search(fs_info, start, min_seq, true);
}

/*
 * This returns the element from the log with the largest time sequence
 * value that's in the log (the most recent log item). Any element with
 * a time sequence lower than min_seq will be ignored.
 */
static struct tree_mod_elem *tree_mod_log_search(struct btrfs_fs_info *fs_info,
						 u64 start, u64 min_seq)
{
	return __tree_mod_log_search(fs_info, start, min_seq, false);
}

int btrfs_tree_mod_log_eb_copy(struct extent_buffer *dst,
			       struct extent_buffer *src,
			       unsigned long dst_offset,
			       unsigned long src_offset,
			       int nr_items)
{
	struct btrfs_fs_info *fs_info = dst->fs_info;
	int ret = 0;
	struct tree_mod_elem **tm_list = NULL;
	struct tree_mod_elem **tm_list_add = NULL;
	struct tree_mod_elem **tm_list_rem = NULL;
	int i;
	bool locked = false;
	struct tree_mod_elem *dst_move_tm = NULL;
	struct tree_mod_elem *src_move_tm = NULL;
	u32 dst_move_nr_items = btrfs_header_nritems(dst) - dst_offset;
	u32 src_move_nr_items = btrfs_header_nritems(src) - (src_offset + nr_items);

	if (!tree_mod_need_log(fs_info, NULL))
		return 0;

	if (btrfs_header_level(dst) == 0 && btrfs_header_level(src) == 0)
		return 0;

	tm_list = kcalloc(nr_items * 2, sizeof(struct tree_mod_elem *),
			  GFP_NOFS);
	if (!tm_list) {
		ret = -ENOMEM;
		goto lock;
	}

	if (dst_move_nr_items) {
		dst_move_tm = tree_mod_log_alloc_move(dst, dst_offset + nr_items,
						      dst_offset, dst_move_nr_items);
		if (IS_ERR(dst_move_tm)) {
			ret = PTR_ERR(dst_move_tm);
			dst_move_tm = NULL;
			goto lock;
		}
	}
	if (src_move_nr_items) {
		src_move_tm = tree_mod_log_alloc_move(src, src_offset,
						      src_offset + nr_items,
						      src_move_nr_items);
		if (IS_ERR(src_move_tm)) {
			ret = PTR_ERR(src_move_tm);
			src_move_tm = NULL;
			goto lock;
		}
	}

	tm_list_add = tm_list;
	tm_list_rem = tm_list + nr_items;
	for (i = 0; i < nr_items; i++) {
		tm_list_rem[i] = alloc_tree_mod_elem(src, i + src_offset,
						     BTRFS_MOD_LOG_KEY_REMOVE);
		if (!tm_list_rem[i]) {
			ret = -ENOMEM;
			goto lock;
		}

		tm_list_add[i] = alloc_tree_mod_elem(dst, i + dst_offset,
						     BTRFS_MOD_LOG_KEY_ADD);
		if (!tm_list_add[i]) {
			ret = -ENOMEM;
			goto lock;
		}
	}

lock:
	if (tree_mod_dont_log(fs_info, NULL)) {
		/*
		 * Don't error if we failed to allocate memory because we don't
		 * need to log.
		 */
		ret = 0;
		goto free_tms;
	}
	locked = true;

	/*
	 * We previously failed to allocate memory and we need to log, so we
	 * have to fail.
	 */
	if (ret != 0)
		goto free_tms;

	if (dst_move_tm) {
		ret = tree_mod_log_insert(fs_info, dst_move_tm);
		if (ret)
			goto free_tms;
	}
	for (i = 0; i < nr_items; i++) {
		ret = tree_mod_log_insert(fs_info, tm_list_rem[i]);
		if (ret)
			goto free_tms;
		ret = tree_mod_log_insert(fs_info, tm_list_add[i]);
		if (ret)
			goto free_tms;
	}
	if (src_move_tm) {
		ret = tree_mod_log_insert(fs_info, src_move_tm);
		if (ret)
			goto free_tms;
	}

	write_unlock(&fs_info->tree_mod_log_lock);
	kfree(tm_list);

	return 0;

free_tms:
	if (dst_move_tm && !RB_EMPTY_NODE(&dst_move_tm->node))
		rb_erase(&dst_move_tm->node, &fs_info->tree_mod_log);
	kfree(dst_move_tm);
	if (src_move_tm && !RB_EMPTY_NODE(&src_move_tm->node))
		rb_erase(&src_move_tm->node, &fs_info->tree_mod_log);
	kfree(src_move_tm);
	if (tm_list) {
		for (i = 0; i < nr_items * 2; i++) {
			if (tm_list[i] && !RB_EMPTY_NODE(&tm_list[i]->node))
				rb_erase(&tm_list[i]->node, &fs_info->tree_mod_log);
			kfree(tm_list[i]);
		}
	}
	if (locked)
		write_unlock(&fs_info->tree_mod_log_lock);
	kfree(tm_list);

	return ret;
}

int btrfs_tree_mod_log_free_eb(struct extent_buffer *eb)
{
	struct tree_mod_elem **tm_list = NULL;
	int nritems = 0;
	int i;
	int ret = 0;

	if (!tree_mod_need_log(eb->fs_info, eb))
		return 0;

	nritems = btrfs_header_nritems(eb);
	tm_list = kcalloc(nritems, sizeof(struct tree_mod_elem *), GFP_NOFS);
	if (!tm_list) {
		ret = -ENOMEM;
		goto lock;
	}

	for (i = 0; i < nritems; i++) {
		tm_list[i] = alloc_tree_mod_elem(eb, i,
				    BTRFS_MOD_LOG_KEY_REMOVE_WHILE_FREEING);
		if (!tm_list[i]) {
			ret = -ENOMEM;
			goto lock;
		}
	}

lock:
	if (tree_mod_dont_log(eb->fs_info, eb)) {
		/*
		 * Don't error if we failed to allocate memory because we don't
		 * need to log.
		 */
		ret = 0;
		goto free_tms;
	} else if (ret != 0) {
		/*
		 * We previously failed to allocate memory and we need to log,
		 * so we have to fail.
		 */
		goto out_unlock;
	}

	ret = tree_mod_log_free_eb(eb->fs_info, tm_list, nritems);
out_unlock:
	write_unlock(&eb->fs_info->tree_mod_log_lock);
	if (ret)
		goto free_tms;
	kfree(tm_list);

	return 0;

free_tms:
	if (tm_list) {
		for (i = 0; i < nritems; i++)
			kfree(tm_list[i]);
		kfree(tm_list);
	}

	return ret;
}

/*
 * Returns the logical address of the oldest predecessor of the given root.
 * Entries older than time_seq are ignored.
 */
static struct tree_mod_elem *tree_mod_log_oldest_root(struct extent_buffer *eb_root,
						      u64 time_seq)
{
	struct tree_mod_elem *tm;
	struct tree_mod_elem *found = NULL;
	u64 root_logical = eb_root->start;
	bool looped = false;

	if (!time_seq)
		return NULL;

	/*
	 * The very last operation that's logged for a root is the replacement
	 * operation (if it is replaced at all). This has the logical address
	 * of the *new* root, making it the very first operation that's logged
	 * for this root.
	 */
	while (1) {
		tm = tree_mod_log_search_oldest(eb_root->fs_info, root_logical,
						time_seq);
		if (!looped && !tm)
			return NULL;
		/*
		 * If there are no tree operation for the oldest root, we simply
		 * return it. This should only happen if that (old) root is at
		 * level 0.
		 */
		if (!tm)
			break;

		/*
		 * If there's an operation that's not a root replacement, we
		 * found the oldest version of our root. Normally, we'll find a
		 * BTRFS_MOD_LOG_KEY_REMOVE_WHILE_FREEING operation here.
		 */
		if (tm->op != BTRFS_MOD_LOG_ROOT_REPLACE)
			break;

		found = tm;
		root_logical = tm->old_root.logical;
		looped = true;
	}

	/* If there's no old root to return, return what we found instead */
	if (!found)
		found = tm;

	return found;
}


/*
 * tm is a pointer to the first operation to rewind within eb. Then, all
 * previous operations will be rewound (until we reach something older than
 * time_seq).
 */
static void tree_mod_log_rewind(struct btrfs_fs_info *fs_info,
				struct extent_buffer *eb,
				u64 time_seq,
				struct tree_mod_elem *first_tm)
{
	u32 n;
	struct rb_node *next;
	struct tree_mod_elem *tm = first_tm;
	unsigned long o_dst;
	unsigned long o_src;
	unsigned long p_size = sizeof(struct btrfs_key_ptr);
	/*
	 * max_slot tracks the maximum valid slot of the rewind eb at every
	 * step of the rewind. This is in contrast with 'n' which eventually
	 * matches the number of items, but can be wrong during moves or if
	 * removes overlap on already valid slots (which is probably separately
	 * a bug). We do this to validate the offsets of memmoves for rewinding
	 * moves and detect invalid memmoves.
	 *
	 * Since a rewind eb can start empty, max_slot is a signed integer with
	 * a special meaning for -1, which is that no slot is valid to move out
	 * of. Any other negative value is invalid.
	 */
	int max_slot;
	int move_src_end_slot;
	int move_dst_end_slot;

	n = btrfs_header_nritems(eb);
	max_slot = n - 1;
	read_lock(&fs_info->tree_mod_log_lock);
	while (tm && tm->seq >= time_seq) {
		ASSERT(max_slot >= -1);
		/*
		 * All the operations are recorded with the operator used for
		 * the modification. As we're going backwards, we do the
		 * opposite of each operation here.
		 */
		switch (tm->op) {
		case BTRFS_MOD_LOG_KEY_REMOVE_WHILE_FREEING:
			BUG_ON(tm->slot < n);
			fallthrough;
		case BTRFS_MOD_LOG_KEY_REMOVE_WHILE_MOVING:
		case BTRFS_MOD_LOG_KEY_REMOVE:
			btrfs_set_node_key(eb, &tm->key, tm->slot);
			btrfs_set_node_blockptr(eb, tm->slot, tm->blockptr);
			btrfs_set_node_ptr_generation(eb, tm->slot,
						      tm->generation);
			n++;
			if (tm->slot > max_slot)
				max_slot = tm->slot;
			break;
		case BTRFS_MOD_LOG_KEY_REPLACE:
			BUG_ON(tm->slot >= n);
			btrfs_set_node_key(eb, &tm->key, tm->slot);
			btrfs_set_node_blockptr(eb, tm->slot, tm->blockptr);
			btrfs_set_node_ptr_generation(eb, tm->slot,
						      tm->generation);
			break;
		case BTRFS_MOD_LOG_KEY_ADD:
			/*
			 * It is possible we could have already removed keys
			 * behind the known max slot, so this will be an
			 * overestimate. In practice, the copy operation
			 * inserts them in increasing order, and overestimating
			 * just means we miss some warnings, so it's OK. It
			 * isn't worth carefully tracking the full array of
			 * valid slots to check against when moving.
			 */
			if (tm->slot == max_slot)
				max_slot--;
			/* if a move operation is needed it's in the log */
			n--;
			break;
		case BTRFS_MOD_LOG_MOVE_KEYS:
			ASSERT(tm->move.nr_items > 0);
			move_src_end_slot = tm->move.dst_slot + tm->move.nr_items - 1;
			move_dst_end_slot = tm->slot + tm->move.nr_items - 1;
			o_dst = btrfs_node_key_ptr_offset(eb, tm->slot);
			o_src = btrfs_node_key_ptr_offset(eb, tm->move.dst_slot);
			if (WARN_ON(move_src_end_slot > max_slot ||
				    tm->move.nr_items <= 0)) {
				btrfs_warn(fs_info,
"move from invalid tree mod log slot eb %llu slot %d dst_slot %d nr_items %d seq %llu n %u max_slot %d",
					   eb->start, tm->slot,
					   tm->move.dst_slot, tm->move.nr_items,
					   tm->seq, n, max_slot);
			}
			memmove_extent_buffer(eb, o_dst, o_src,
					      tm->move.nr_items * p_size);
			max_slot = move_dst_end_slot;
			break;
		case BTRFS_MOD_LOG_ROOT_REPLACE:
			/*
			 * This operation is special. For roots, this must be
			 * handled explicitly before rewinding.
			 * For non-roots, this operation may exist if the node
			 * was a root: root A -> child B; then A gets empty and
			 * B is promoted to the new root. In the mod log, we'll
			 * have a root-replace operation for B, a tree block
			 * that is no root. We simply ignore that operation.
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
struct extent_buffer *btrfs_tree_mod_log_rewind(struct btrfs_fs_info *fs_info,
						struct btrfs_path *path,
						struct extent_buffer *eb,
						u64 time_seq)
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

	if (tm->op == BTRFS_MOD_LOG_KEY_REMOVE_WHILE_FREEING) {
		BUG_ON(tm->slot != 0);
		eb_rewin = alloc_dummy_extent_buffer(fs_info, eb->start);
		if (!eb_rewin) {
			btrfs_tree_read_unlock(eb);
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
			btrfs_tree_read_unlock(eb);
			free_extent_buffer(eb);
			return NULL;
		}
	}

	btrfs_tree_read_unlock(eb);
	free_extent_buffer(eb);

	btrfs_set_buffer_lockdep_class(btrfs_header_owner(eb_rewin),
				       eb_rewin, btrfs_header_level(eb_rewin));
	btrfs_tree_read_lock(eb_rewin);
	tree_mod_log_rewind(fs_info, eb_rewin, time_seq, tm);
	WARN_ON(btrfs_header_nritems(eb_rewin) >
		BTRFS_NODEPTRS_PER_BLOCK(fs_info));

	return eb_rewin;
}

/*
 * Rewind the state of @root's root node to the given @time_seq value.
 * If there are no changes, the current root->root_node is returned. If anything
 * changed in between, there's a fresh buffer allocated on which the rewind
 * operations are done. In any case, the returned buffer is read locked.
 * Returns NULL on error (with no locks held).
 */
struct extent_buffer *btrfs_get_old_root(struct btrfs_root *root, u64 time_seq)
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
	tm = tree_mod_log_oldest_root(eb_root, time_seq);
	if (!tm)
		return eb_root;

	if (tm->op == BTRFS_MOD_LOG_ROOT_REPLACE) {
		old_root = &tm->old_root;
		old_generation = tm->generation;
		logical = old_root->logical;
		level = old_root->level;
	} else {
		logical = eb_root->start;
		level = btrfs_header_level(eb_root);
	}

	tm = tree_mod_log_search(fs_info, logical, time_seq);
	if (old_root && tm && tm->op != BTRFS_MOD_LOG_KEY_REMOVE_WHILE_FREEING) {
		struct btrfs_tree_parent_check check = { 0 };

		btrfs_tree_read_unlock(eb_root);
		free_extent_buffer(eb_root);

		check.level = level;
		check.owner_root = root->root_key.objectid;

		old = read_tree_block(fs_info, logical, &check);
		if (WARN_ON(IS_ERR(old) || !extent_buffer_uptodate(old))) {
			if (!IS_ERR(old))
				free_extent_buffer(old);
			btrfs_warn(fs_info,
				   "failed to read tree block %llu from get_old_root",
				   logical);
		} else {
			struct tree_mod_elem *tm2;

			btrfs_tree_read_lock(old);
			eb = btrfs_clone_extent_buffer(old);
			/*
			 * After the lookup for the most recent tree mod operation
			 * above and before we locked and cloned the extent buffer
			 * 'old', a new tree mod log operation may have been added.
			 * So lookup for a more recent one to make sure the number
			 * of mod log operations we replay is consistent with the
			 * number of items we have in the cloned extent buffer,
			 * otherwise we can hit a BUG_ON when rewinding the extent
			 * buffer.
			 */
			tm2 = tree_mod_log_search(fs_info, logical, time_seq);
			btrfs_tree_read_unlock(old);
			free_extent_buffer(old);
			ASSERT(tm2);
			ASSERT(tm2 == tm || tm2->seq > tm->seq);
			if (!tm2 || tm2->seq < tm->seq) {
				free_extent_buffer(eb);
				return NULL;
			}
			tm = tm2;
		}
	} else if (old_root) {
		eb_root_owner = btrfs_header_owner(eb_root);
		btrfs_tree_read_unlock(eb_root);
		free_extent_buffer(eb_root);
		eb = alloc_dummy_extent_buffer(fs_info, logical);
	} else {
		eb = btrfs_clone_extent_buffer(eb_root);
		btrfs_tree_read_unlock(eb_root);
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
		tree_mod_log_rewind(fs_info, eb, time_seq, tm);
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

	tm = tree_mod_log_oldest_root(eb_root, time_seq);
	if (tm && tm->op == BTRFS_MOD_LOG_ROOT_REPLACE)
		level = tm->old_root.level;
	else
		level = btrfs_header_level(eb_root);

	free_extent_buffer(eb_root);

	return level;
}

/*
 * Return the lowest sequence number in the tree modification log.
 *
 * Return the sequence number of the oldest tree modification log user, which
 * corresponds to the lowest sequence number of all existing users. If there are
 * no users it returns 0.
 */
u64 btrfs_tree_mod_log_lowest_seq(struct btrfs_fs_info *fs_info)
{
	u64 ret = 0;

	read_lock(&fs_info->tree_mod_log_lock);
	if (!list_empty(&fs_info->tree_mod_seq_list)) {
		struct btrfs_seq_list *elem;

		elem = list_first_entry(&fs_info->tree_mod_seq_list,
					struct btrfs_seq_list, list);
		ret = elem->seq;
	}
	read_unlock(&fs_info->tree_mod_log_lock);

	return ret;
}
