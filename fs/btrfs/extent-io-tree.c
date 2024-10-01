// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include <trace/events/btrfs.h>
#include "messages.h"
#include "ctree.h"
#include "extent_io.h"
#include "extent-io-tree.h"
#include "btrfs_inode.h"

static struct kmem_cache *extent_state_cache;

static inline bool extent_state_in_tree(const struct extent_state *state)
{
	return !RB_EMPTY_NODE(&state->rb_node);
}

#ifdef CONFIG_BTRFS_DEBUG
static LIST_HEAD(states);
static DEFINE_SPINLOCK(leak_lock);

static inline void btrfs_leak_debug_add_state(struct extent_state *state)
{
	unsigned long flags;

	spin_lock_irqsave(&leak_lock, flags);
	list_add(&state->leak_list, &states);
	spin_unlock_irqrestore(&leak_lock, flags);
}

static inline void btrfs_leak_debug_del_state(struct extent_state *state)
{
	unsigned long flags;

	spin_lock_irqsave(&leak_lock, flags);
	list_del(&state->leak_list);
	spin_unlock_irqrestore(&leak_lock, flags);
}

static inline void btrfs_extent_state_leak_debug_check(void)
{
	struct extent_state *state;

	while (!list_empty(&states)) {
		state = list_entry(states.next, struct extent_state, leak_list);
		pr_err("BTRFS: state leak: start %llu end %llu state %u in tree %d refs %d\n",
		       state->start, state->end, state->state,
		       extent_state_in_tree(state),
		       refcount_read(&state->refs));
		list_del(&state->leak_list);
		WARN_ON_ONCE(1);
		kmem_cache_free(extent_state_cache, state);
	}
}

#define btrfs_debug_check_extent_io_range(tree, start, end)		\
	__btrfs_debug_check_extent_io_range(__func__, (tree), (start), (end))
static inline void __btrfs_debug_check_extent_io_range(const char *caller,
						       struct extent_io_tree *tree,
						       u64 start, u64 end)
{
	const struct btrfs_inode *inode;
	u64 isize;

	if (tree->owner != IO_TREE_INODE_IO)
		return;

	inode = extent_io_tree_to_inode_const(tree);
	isize = i_size_read(&inode->vfs_inode);
	if (end >= PAGE_SIZE && (end % 2) == 0 && end != isize - 1) {
		btrfs_debug_rl(inode->root->fs_info,
		    "%s: ino %llu isize %llu odd range [%llu,%llu]",
			caller, btrfs_ino(inode), isize, start, end);
	}
}
#else
#define btrfs_leak_debug_add_state(state)		do {} while (0)
#define btrfs_leak_debug_del_state(state)		do {} while (0)
#define btrfs_extent_state_leak_debug_check()		do {} while (0)
#define btrfs_debug_check_extent_io_range(c, s, e)	do {} while (0)
#endif


/*
 * The only tree allowed to set the inode is IO_TREE_INODE_IO.
 */
static bool is_inode_io_tree(const struct extent_io_tree *tree)
{
	return tree->owner == IO_TREE_INODE_IO;
}

/* Return the inode if it's valid for the given tree, otherwise NULL. */
struct btrfs_inode *extent_io_tree_to_inode(struct extent_io_tree *tree)
{
	if (tree->owner == IO_TREE_INODE_IO)
		return tree->inode;
	return NULL;
}

/* Read-only access to the inode. */
const struct btrfs_inode *extent_io_tree_to_inode_const(const struct extent_io_tree *tree)
{
	if (tree->owner == IO_TREE_INODE_IO)
		return tree->inode;
	return NULL;
}

/* For read-only access to fs_info. */
const struct btrfs_fs_info *extent_io_tree_to_fs_info(const struct extent_io_tree *tree)
{
	if (tree->owner == IO_TREE_INODE_IO)
		return tree->inode->root->fs_info;
	return tree->fs_info;
}

void extent_io_tree_init(struct btrfs_fs_info *fs_info,
			 struct extent_io_tree *tree, unsigned int owner)
{
	tree->state = RB_ROOT;
	spin_lock_init(&tree->lock);
	tree->fs_info = fs_info;
	tree->owner = owner;
}

/*
 * Empty an io tree, removing and freeing every extent state record from the
 * tree. This should be called once we are sure no other task can access the
 * tree anymore, so no tree updates happen after we empty the tree and there
 * aren't any waiters on any extent state record (EXTENT_LOCK_BITS are never
 * set on any extent state when calling this function).
 */
void extent_io_tree_release(struct extent_io_tree *tree)
{
	struct rb_root root;
	struct extent_state *state;
	struct extent_state *tmp;

	spin_lock(&tree->lock);
	root = tree->state;
	tree->state = RB_ROOT;
	rbtree_postorder_for_each_entry_safe(state, tmp, &root, rb_node) {
		/* Clear node to keep free_extent_state() happy. */
		RB_CLEAR_NODE(&state->rb_node);
		ASSERT(!(state->state & EXTENT_LOCK_BITS));
		/*
		 * No need for a memory barrier here, as we are holding the tree
		 * lock and we only change the waitqueue while holding that lock
		 * (see wait_extent_bit()).
		 */
		ASSERT(!waitqueue_active(&state->wq));
		free_extent_state(state);
		cond_resched_lock(&tree->lock);
	}
	/*
	 * Should still be empty even after a reschedule, no other task should
	 * be accessing the tree anymore.
	 */
	ASSERT(RB_EMPTY_ROOT(&tree->state));
	spin_unlock(&tree->lock);
}

static struct extent_state *alloc_extent_state(gfp_t mask)
{
	struct extent_state *state;

	/*
	 * The given mask might be not appropriate for the slab allocator,
	 * drop the unsupported bits
	 */
	mask &= ~(__GFP_DMA32|__GFP_HIGHMEM);
	state = kmem_cache_alloc(extent_state_cache, mask);
	if (!state)
		return state;
	state->state = 0;
	RB_CLEAR_NODE(&state->rb_node);
	btrfs_leak_debug_add_state(state);
	refcount_set(&state->refs, 1);
	init_waitqueue_head(&state->wq);
	trace_alloc_extent_state(state, mask, _RET_IP_);
	return state;
}

static struct extent_state *alloc_extent_state_atomic(struct extent_state *prealloc)
{
	if (!prealloc)
		prealloc = alloc_extent_state(GFP_ATOMIC);

	return prealloc;
}

void free_extent_state(struct extent_state *state)
{
	if (!state)
		return;
	if (refcount_dec_and_test(&state->refs)) {
		WARN_ON(extent_state_in_tree(state));
		btrfs_leak_debug_del_state(state);
		trace_free_extent_state(state, _RET_IP_);
		kmem_cache_free(extent_state_cache, state);
	}
}

static int add_extent_changeset(struct extent_state *state, u32 bits,
				 struct extent_changeset *changeset,
				 int set)
{
	int ret;

	if (!changeset)
		return 0;
	if (set && (state->state & bits) == bits)
		return 0;
	if (!set && (state->state & bits) == 0)
		return 0;
	changeset->bytes_changed += state->end - state->start + 1;
	ret = ulist_add(&changeset->range_changed, state->start, state->end,
			GFP_ATOMIC);
	return ret;
}

static inline struct extent_state *next_state(struct extent_state *state)
{
	struct rb_node *next = rb_next(&state->rb_node);

	if (next)
		return rb_entry(next, struct extent_state, rb_node);
	else
		return NULL;
}

static inline struct extent_state *prev_state(struct extent_state *state)
{
	struct rb_node *next = rb_prev(&state->rb_node);

	if (next)
		return rb_entry(next, struct extent_state, rb_node);
	else
		return NULL;
}

/*
 * Search @tree for an entry that contains @offset. Such entry would have
 * entry->start <= offset && entry->end >= offset.
 *
 * @tree:       the tree to search
 * @offset:     offset that should fall within an entry in @tree
 * @node_ret:   pointer where new node should be anchored (used when inserting an
 *	        entry in the tree)
 * @parent_ret: points to entry which would have been the parent of the entry,
 *               containing @offset
 *
 * Return a pointer to the entry that contains @offset byte address and don't change
 * @node_ret and @parent_ret.
 *
 * If no such entry exists, return pointer to entry that ends before @offset
 * and fill parameters @node_ret and @parent_ret, ie. does not return NULL.
 */
static inline struct extent_state *tree_search_for_insert(struct extent_io_tree *tree,
							  u64 offset,
							  struct rb_node ***node_ret,
							  struct rb_node **parent_ret)
{
	struct rb_root *root = &tree->state;
	struct rb_node **node = &root->rb_node;
	struct rb_node *prev = NULL;
	struct extent_state *entry = NULL;

	while (*node) {
		prev = *node;
		entry = rb_entry(prev, struct extent_state, rb_node);

		if (offset < entry->start)
			node = &(*node)->rb_left;
		else if (offset > entry->end)
			node = &(*node)->rb_right;
		else
			return entry;
	}

	if (node_ret)
		*node_ret = node;
	if (parent_ret)
		*parent_ret = prev;

	/* Search neighbors until we find the first one past the end */
	while (entry && offset > entry->end)
		entry = next_state(entry);

	return entry;
}

/*
 * Search offset in the tree or fill neighbor rbtree node pointers.
 *
 * @tree:      the tree to search
 * @offset:    offset that should fall within an entry in @tree
 * @next_ret:  pointer to the first entry whose range ends after @offset
 * @prev_ret:  pointer to the first entry whose range begins before @offset
 *
 * Return a pointer to the entry that contains @offset byte address. If no
 * such entry exists, then return NULL and fill @prev_ret and @next_ret.
 * Otherwise return the found entry and other pointers are left untouched.
 */
static struct extent_state *tree_search_prev_next(struct extent_io_tree *tree,
						  u64 offset,
						  struct extent_state **prev_ret,
						  struct extent_state **next_ret)
{
	struct rb_root *root = &tree->state;
	struct rb_node **node = &root->rb_node;
	struct extent_state *orig_prev;
	struct extent_state *entry = NULL;

	ASSERT(prev_ret);
	ASSERT(next_ret);

	while (*node) {
		entry = rb_entry(*node, struct extent_state, rb_node);

		if (offset < entry->start)
			node = &(*node)->rb_left;
		else if (offset > entry->end)
			node = &(*node)->rb_right;
		else
			return entry;
	}

	orig_prev = entry;
	while (entry && offset > entry->end)
		entry = next_state(entry);
	*next_ret = entry;
	entry = orig_prev;

	while (entry && offset < entry->start)
		entry = prev_state(entry);
	*prev_ret = entry;

	return NULL;
}

/*
 * Inexact rb-tree search, return the next entry if @offset is not found
 */
static inline struct extent_state *tree_search(struct extent_io_tree *tree, u64 offset)
{
	return tree_search_for_insert(tree, offset, NULL, NULL);
}

static void extent_io_tree_panic(const struct extent_io_tree *tree,
				 const struct extent_state *state,
				 const char *opname,
				 int err)
{
	btrfs_panic(extent_io_tree_to_fs_info(tree), err,
		    "extent io tree error on %s state start %llu end %llu",
		    opname, state->start, state->end);
}

static void merge_prev_state(struct extent_io_tree *tree, struct extent_state *state)
{
	struct extent_state *prev;

	prev = prev_state(state);
	if (prev && prev->end == state->start - 1 && prev->state == state->state) {
		if (is_inode_io_tree(tree))
			btrfs_merge_delalloc_extent(extent_io_tree_to_inode(tree),
						    state, prev);
		state->start = prev->start;
		rb_erase(&prev->rb_node, &tree->state);
		RB_CLEAR_NODE(&prev->rb_node);
		free_extent_state(prev);
	}
}

static void merge_next_state(struct extent_io_tree *tree, struct extent_state *state)
{
	struct extent_state *next;

	next = next_state(state);
	if (next && next->start == state->end + 1 && next->state == state->state) {
		if (is_inode_io_tree(tree))
			btrfs_merge_delalloc_extent(extent_io_tree_to_inode(tree),
						    state, next);
		state->end = next->end;
		rb_erase(&next->rb_node, &tree->state);
		RB_CLEAR_NODE(&next->rb_node);
		free_extent_state(next);
	}
}

/*
 * Utility function to look for merge candidates inside a given range.  Any
 * extents with matching state are merged together into a single extent in the
 * tree.  Extents with EXTENT_IO in their state field are not merged because
 * the end_io handlers need to be able to do operations on them without
 * sleeping (or doing allocations/splits).
 *
 * This should be called with the tree lock held.
 */
static void merge_state(struct extent_io_tree *tree, struct extent_state *state)
{
	if (state->state & (EXTENT_LOCK_BITS | EXTENT_BOUNDARY))
		return;

	merge_prev_state(tree, state);
	merge_next_state(tree, state);
}

static void set_state_bits(struct extent_io_tree *tree,
			   struct extent_state *state,
			   u32 bits, struct extent_changeset *changeset)
{
	u32 bits_to_set = bits & ~EXTENT_CTLBITS;
	int ret;

	if (is_inode_io_tree(tree))
		btrfs_set_delalloc_extent(extent_io_tree_to_inode(tree), state, bits);

	ret = add_extent_changeset(state, bits_to_set, changeset, 1);
	BUG_ON(ret < 0);
	state->state |= bits_to_set;
}

/*
 * Insert an extent_state struct into the tree.  'bits' are set on the
 * struct before it is inserted.
 *
 * Returns a pointer to the struct extent_state record containing the range
 * requested for insertion, which may be the same as the given struct or it
 * may be an existing record in the tree that was expanded to accommodate the
 * requested range. In case of an extent_state different from the one that was
 * given, the later can be freed or reused by the caller.
 *
 * On error it returns an error pointer.
 *
 * The tree lock is not taken internally.  This is a utility function and
 * probably isn't what you want to call (see set/clear_extent_bit).
 */
static struct extent_state *insert_state(struct extent_io_tree *tree,
					 struct extent_state *state,
					 u32 bits,
					 struct extent_changeset *changeset)
{
	struct rb_node **node;
	struct rb_node *parent = NULL;
	const u64 start = state->start - 1;
	const u64 end = state->end + 1;
	const bool try_merge = !(bits & (EXTENT_LOCK_BITS | EXTENT_BOUNDARY));

	set_state_bits(tree, state, bits, changeset);

	node = &tree->state.rb_node;
	while (*node) {
		struct extent_state *entry;

		parent = *node;
		entry = rb_entry(parent, struct extent_state, rb_node);

		if (state->end < entry->start) {
			if (try_merge && end == entry->start &&
			    state->state == entry->state) {
				if (is_inode_io_tree(tree))
					btrfs_merge_delalloc_extent(
							extent_io_tree_to_inode(tree),
							state, entry);
				entry->start = state->start;
				merge_prev_state(tree, entry);
				state->state = 0;
				return entry;
			}
			node = &(*node)->rb_left;
		} else if (state->end > entry->end) {
			if (try_merge && entry->end == start &&
			    state->state == entry->state) {
				if (is_inode_io_tree(tree))
					btrfs_merge_delalloc_extent(
							extent_io_tree_to_inode(tree),
							state, entry);
				entry->end = state->end;
				merge_next_state(tree, entry);
				state->state = 0;
				return entry;
			}
			node = &(*node)->rb_right;
		} else {
			return ERR_PTR(-EEXIST);
		}
	}

	rb_link_node(&state->rb_node, parent, node);
	rb_insert_color(&state->rb_node, &tree->state);

	return state;
}

/*
 * Insert state to @tree to the location given by @node and @parent.
 */
static void insert_state_fast(struct extent_io_tree *tree,
			      struct extent_state *state, struct rb_node **node,
			      struct rb_node *parent, unsigned bits,
			      struct extent_changeset *changeset)
{
	set_state_bits(tree, state, bits, changeset);
	rb_link_node(&state->rb_node, parent, node);
	rb_insert_color(&state->rb_node, &tree->state);
	merge_state(tree, state);
}

/*
 * Split a given extent state struct in two, inserting the preallocated
 * struct 'prealloc' as the newly created second half.  'split' indicates an
 * offset inside 'orig' where it should be split.
 *
 * Before calling,
 * the tree has 'orig' at [orig->start, orig->end].  After calling, there
 * are two extent state structs in the tree:
 * prealloc: [orig->start, split - 1]
 * orig: [ split, orig->end ]
 *
 * The tree locks are not taken by this function. They need to be held
 * by the caller.
 */
static int split_state(struct extent_io_tree *tree, struct extent_state *orig,
		       struct extent_state *prealloc, u64 split)
{
	struct rb_node *parent = NULL;
	struct rb_node **node;

	if (is_inode_io_tree(tree))
		btrfs_split_delalloc_extent(extent_io_tree_to_inode(tree), orig,
					    split);

	prealloc->start = orig->start;
	prealloc->end = split - 1;
	prealloc->state = orig->state;
	orig->start = split;

	parent = &orig->rb_node;
	node = &parent;
	while (*node) {
		struct extent_state *entry;

		parent = *node;
		entry = rb_entry(parent, struct extent_state, rb_node);

		if (prealloc->end < entry->start) {
			node = &(*node)->rb_left;
		} else if (prealloc->end > entry->end) {
			node = &(*node)->rb_right;
		} else {
			free_extent_state(prealloc);
			return -EEXIST;
		}
	}

	rb_link_node(&prealloc->rb_node, parent, node);
	rb_insert_color(&prealloc->rb_node, &tree->state);

	return 0;
}

/*
 * Utility function to clear some bits in an extent state struct.  It will
 * optionally wake up anyone waiting on this state (wake == 1).
 *
 * If no bits are set on the state struct after clearing things, the
 * struct is freed and removed from the tree
 */
static struct extent_state *clear_state_bit(struct extent_io_tree *tree,
					    struct extent_state *state,
					    u32 bits, int wake,
					    struct extent_changeset *changeset)
{
	struct extent_state *next;
	u32 bits_to_clear = bits & ~EXTENT_CTLBITS;
	int ret;

	if (is_inode_io_tree(tree))
		btrfs_clear_delalloc_extent(extent_io_tree_to_inode(tree), state,
					    bits);

	ret = add_extent_changeset(state, bits_to_clear, changeset, 0);
	BUG_ON(ret < 0);
	state->state &= ~bits_to_clear;
	if (wake)
		wake_up(&state->wq);
	if (state->state == 0) {
		next = next_state(state);
		if (extent_state_in_tree(state)) {
			rb_erase(&state->rb_node, &tree->state);
			RB_CLEAR_NODE(&state->rb_node);
			free_extent_state(state);
		} else {
			WARN_ON(1);
		}
	} else {
		merge_state(tree, state);
		next = next_state(state);
	}
	return next;
}

/*
 * Detect if extent bits request NOWAIT semantics and set the gfp mask accordingly,
 * unset the EXTENT_NOWAIT bit.
 */
static void set_gfp_mask_from_bits(u32 *bits, gfp_t *mask)
{
	*mask = (*bits & EXTENT_NOWAIT ? GFP_NOWAIT : GFP_NOFS);
	*bits &= EXTENT_NOWAIT - 1;
}

/*
 * Clear some bits on a range in the tree.  This may require splitting or
 * inserting elements in the tree, so the gfp mask is used to indicate which
 * allocations or sleeping are allowed.
 *
 * The range [start, end] is inclusive.
 *
 * This takes the tree lock, and returns 0 on success and < 0 on error.
 */
int __clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       u32 bits, struct extent_state **cached_state,
		       struct extent_changeset *changeset)
{
	struct extent_state *state;
	struct extent_state *cached;
	struct extent_state *prealloc = NULL;
	u64 last_end;
	int err;
	int clear = 0;
	int wake;
	int delete = (bits & EXTENT_CLEAR_ALL_BITS);
	gfp_t mask;

	set_gfp_mask_from_bits(&bits, &mask);
	btrfs_debug_check_extent_io_range(tree, start, end);
	trace_btrfs_clear_extent_bit(tree, start, end - start + 1, bits);

	if (delete)
		bits |= ~EXTENT_CTLBITS;

	if (bits & EXTENT_DELALLOC)
		bits |= EXTENT_NORESERVE;

	wake = ((bits & EXTENT_LOCK_BITS) ? 1 : 0);
	if (bits & (EXTENT_LOCK_BITS | EXTENT_BOUNDARY))
		clear = 1;
again:
	if (!prealloc) {
		/*
		 * Don't care for allocation failure here because we might end
		 * up not needing the pre-allocated extent state at all, which
		 * is the case if we only have in the tree extent states that
		 * cover our input range and don't cover too any other range.
		 * If we end up needing a new extent state we allocate it later.
		 */
		prealloc = alloc_extent_state(mask);
	}

	spin_lock(&tree->lock);
	if (cached_state) {
		cached = *cached_state;

		if (clear) {
			*cached_state = NULL;
			cached_state = NULL;
		}

		if (cached && extent_state_in_tree(cached) &&
		    cached->start <= start && cached->end > start) {
			if (clear)
				refcount_dec(&cached->refs);
			state = cached;
			goto hit_next;
		}
		if (clear)
			free_extent_state(cached);
	}

	/* This search will find the extents that end after our range starts. */
	state = tree_search(tree, start);
	if (!state)
		goto out;
hit_next:
	if (state->start > end)
		goto out;
	WARN_ON(state->end < start);
	last_end = state->end;

	/* The state doesn't have the wanted bits, go ahead. */
	if (!(state->state & bits)) {
		state = next_state(state);
		goto next;
	}

	/*
	 *     | ---- desired range ---- |
	 *  | state | or
	 *  | ------------- state -------------- |
	 *
	 * We need to split the extent we found, and may flip bits on second
	 * half.
	 *
	 * If the extent we found extends past our range, we just split and
	 * search again.  It'll get split again the next time though.
	 *
	 * If the extent we found is inside our range, we clear the desired bit
	 * on it.
	 */

	if (state->start < start) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc)
			goto search_again;
		err = split_state(tree, state, prealloc, start);
		if (err)
			extent_io_tree_panic(tree, state, "split", err);

		prealloc = NULL;
		if (err)
			goto out;
		if (state->end <= end) {
			state = clear_state_bit(tree, state, bits, wake, changeset);
			goto next;
		}
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *                        | state |
	 * We need to split the extent, and clear the bit on the first half.
	 */
	if (state->start <= end && state->end > end) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc)
			goto search_again;
		err = split_state(tree, state, prealloc, end + 1);
		if (err)
			extent_io_tree_panic(tree, state, "split", err);

		if (wake)
			wake_up(&state->wq);

		clear_state_bit(tree, prealloc, bits, wake, changeset);

		prealloc = NULL;
		goto out;
	}

	state = clear_state_bit(tree, state, bits, wake, changeset);
next:
	if (last_end == (u64)-1)
		goto out;
	start = last_end + 1;
	if (start <= end && state && !need_resched())
		goto hit_next;

search_again:
	if (start > end)
		goto out;
	spin_unlock(&tree->lock);
	if (gfpflags_allow_blocking(mask))
		cond_resched();
	goto again;

out:
	spin_unlock(&tree->lock);
	if (prealloc)
		free_extent_state(prealloc);

	return 0;

}

/*
 * Wait for one or more bits to clear on a range in the state tree.
 * The range [start, end] is inclusive.
 * The tree lock is taken by this function
 */
static void wait_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
			    u32 bits, struct extent_state **cached_state)
{
	struct extent_state *state;

	btrfs_debug_check_extent_io_range(tree, start, end);

	spin_lock(&tree->lock);
again:
	/*
	 * Maintain cached_state, as we may not remove it from the tree if there
	 * are more bits than the bits we're waiting on set on this state.
	 */
	if (cached_state && *cached_state) {
		state = *cached_state;
		if (extent_state_in_tree(state) &&
		    state->start <= start && start < state->end)
			goto process_node;
	}
	while (1) {
		/*
		 * This search will find all the extents that end after our
		 * range starts.
		 */
		state = tree_search(tree, start);
process_node:
		if (!state)
			break;
		if (state->start > end)
			goto out;

		if (state->state & bits) {
			DEFINE_WAIT(wait);

			start = state->start;
			refcount_inc(&state->refs);
			prepare_to_wait(&state->wq, &wait, TASK_UNINTERRUPTIBLE);
			spin_unlock(&tree->lock);
			schedule();
			spin_lock(&tree->lock);
			finish_wait(&state->wq, &wait);
			free_extent_state(state);
			goto again;
		}
		start = state->end + 1;

		if (start > end)
			break;

		if (!cond_resched_lock(&tree->lock)) {
			state = next_state(state);
			goto process_node;
		}
	}
out:
	/* This state is no longer useful, clear it and free it up. */
	if (cached_state && *cached_state) {
		state = *cached_state;
		*cached_state = NULL;
		free_extent_state(state);
	}
	spin_unlock(&tree->lock);
}

static void cache_state_if_flags(struct extent_state *state,
				 struct extent_state **cached_ptr,
				 unsigned flags)
{
	if (cached_ptr && !(*cached_ptr)) {
		if (!flags || (state->state & flags)) {
			*cached_ptr = state;
			refcount_inc(&state->refs);
		}
	}
}

static void cache_state(struct extent_state *state,
			struct extent_state **cached_ptr)
{
	return cache_state_if_flags(state, cached_ptr, EXTENT_LOCK_BITS | EXTENT_BOUNDARY);
}

/*
 * Find the first state struct with 'bits' set after 'start', and return it.
 * tree->lock must be held.  NULL will returned if nothing was found after
 * 'start'.
 */
static struct extent_state *find_first_extent_bit_state(struct extent_io_tree *tree,
							u64 start, u32 bits)
{
	struct extent_state *state;

	/*
	 * This search will find all the extents that end after our range
	 * starts.
	 */
	state = tree_search(tree, start);
	while (state) {
		if (state->end >= start && (state->state & bits))
			return state;
		state = next_state(state);
	}
	return NULL;
}

/*
 * Find the first offset in the io tree with one or more @bits set.
 *
 * Note: If there are multiple bits set in @bits, any of them will match.
 *
 * Return true if we find something, and update @start_ret and @end_ret.
 * Return false if we found nothing.
 */
bool find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			   u64 *start_ret, u64 *end_ret, u32 bits,
			   struct extent_state **cached_state)
{
	struct extent_state *state;
	bool ret = false;

	spin_lock(&tree->lock);
	if (cached_state && *cached_state) {
		state = *cached_state;
		if (state->end == start - 1 && extent_state_in_tree(state)) {
			while ((state = next_state(state)) != NULL) {
				if (state->state & bits)
					break;
			}
			/*
			 * If we found the next extent state, clear cached_state
			 * so that we can cache the next extent state below and
			 * avoid future calls going over the same extent state
			 * again. If we haven't found any, clear as well since
			 * it's now useless.
			 */
			free_extent_state(*cached_state);
			*cached_state = NULL;
			if (state)
				goto got_it;
			goto out;
		}
		free_extent_state(*cached_state);
		*cached_state = NULL;
	}

	state = find_first_extent_bit_state(tree, start, bits);
got_it:
	if (state) {
		cache_state_if_flags(state, cached_state, 0);
		*start_ret = state->start;
		*end_ret = state->end;
		ret = true;
	}
out:
	spin_unlock(&tree->lock);
	return ret;
}

/*
 * Find a contiguous area of bits
 *
 * @tree:      io tree to check
 * @start:     offset to start the search from
 * @start_ret: the first offset we found with the bits set
 * @end_ret:   the final contiguous range of the bits that were set
 * @bits:      bits to look for
 *
 * set_extent_bit and clear_extent_bit can temporarily split contiguous ranges
 * to set bits appropriately, and then merge them again.  During this time it
 * will drop the tree->lock, so use this helper if you want to find the actual
 * contiguous area for given bits.  We will search to the first bit we find, and
 * then walk down the tree until we find a non-contiguous area.  The area
 * returned will be the full contiguous area with the bits set.
 */
int find_contiguous_extent_bit(struct extent_io_tree *tree, u64 start,
			       u64 *start_ret, u64 *end_ret, u32 bits)
{
	struct extent_state *state;
	int ret = 1;

	ASSERT(!btrfs_fs_incompat(extent_io_tree_to_fs_info(tree), NO_HOLES));

	spin_lock(&tree->lock);
	state = find_first_extent_bit_state(tree, start, bits);
	if (state) {
		*start_ret = state->start;
		*end_ret = state->end;
		while ((state = next_state(state)) != NULL) {
			if (state->start > (*end_ret + 1))
				break;
			*end_ret = state->end;
		}
		ret = 0;
	}
	spin_unlock(&tree->lock);
	return ret;
}

/*
 * Find a contiguous range of bytes in the file marked as delalloc, not more
 * than 'max_bytes'.  start and end are used to return the range,
 *
 * True is returned if we find something, false if nothing was in the tree.
 */
bool btrfs_find_delalloc_range(struct extent_io_tree *tree, u64 *start,
			       u64 *end, u64 max_bytes,
			       struct extent_state **cached_state)
{
	struct extent_state *state;
	u64 cur_start = *start;
	bool found = false;
	u64 total_bytes = 0;

	spin_lock(&tree->lock);

	/*
	 * This search will find all the extents that end after our range
	 * starts.
	 */
	state = tree_search(tree, cur_start);
	if (!state) {
		*end = (u64)-1;
		goto out;
	}

	while (state) {
		if (found && (state->start != cur_start ||
			      (state->state & EXTENT_BOUNDARY))) {
			goto out;
		}
		if (!(state->state & EXTENT_DELALLOC)) {
			if (!found)
				*end = state->end;
			goto out;
		}
		if (!found) {
			*start = state->start;
			*cached_state = state;
			refcount_inc(&state->refs);
		}
		found = true;
		*end = state->end;
		cur_start = state->end + 1;
		total_bytes += state->end - state->start + 1;
		if (total_bytes >= max_bytes)
			break;
		state = next_state(state);
	}
out:
	spin_unlock(&tree->lock);
	return found;
}

/*
 * Set some bits on a range in the tree.  This may require allocations or
 * sleeping. By default all allocations use GFP_NOFS, use EXTENT_NOWAIT for
 * GFP_NOWAIT.
 *
 * If any of the exclusive bits are set, this will fail with -EEXIST if some
 * part of the range already has the desired bits set.  The extent_state of the
 * existing range is returned in failed_state in this case, and the start of the
 * existing range is returned in failed_start.  failed_state is used as an
 * optimization for wait_extent_bit, failed_start must be used as the source of
 * truth as failed_state may have changed since we returned.
 *
 * [start, end] is inclusive This takes the tree lock.
 */
static int __set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
			    u32 bits, u64 *failed_start,
			    struct extent_state **failed_state,
			    struct extent_state **cached_state,
			    struct extent_changeset *changeset)
{
	struct extent_state *state;
	struct extent_state *prealloc = NULL;
	struct rb_node **p = NULL;
	struct rb_node *parent = NULL;
	int ret = 0;
	u64 last_start;
	u64 last_end;
	u32 exclusive_bits = (bits & EXTENT_LOCK_BITS);
	gfp_t mask;

	set_gfp_mask_from_bits(&bits, &mask);
	btrfs_debug_check_extent_io_range(tree, start, end);
	trace_btrfs_set_extent_bit(tree, start, end - start + 1, bits);

	if (exclusive_bits)
		ASSERT(failed_start);
	else
		ASSERT(failed_start == NULL && failed_state == NULL);
again:
	if (!prealloc) {
		/*
		 * Don't care for allocation failure here because we might end
		 * up not needing the pre-allocated extent state at all, which
		 * is the case if we only have in the tree extent states that
		 * cover our input range and don't cover too any other range.
		 * If we end up needing a new extent state we allocate it later.
		 */
		prealloc = alloc_extent_state(mask);
	}
	/* Optimistically preallocate the extent changeset ulist node. */
	if (changeset)
		extent_changeset_prealloc(changeset, mask);

	spin_lock(&tree->lock);
	if (cached_state && *cached_state) {
		state = *cached_state;
		if (state->start <= start && state->end > start &&
		    extent_state_in_tree(state))
			goto hit_next;
	}
	/*
	 * This search will find all the extents that end after our range
	 * starts.
	 */
	state = tree_search_for_insert(tree, start, &p, &parent);
	if (!state) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc)
			goto search_again;
		prealloc->start = start;
		prealloc->end = end;
		insert_state_fast(tree, prealloc, p, parent, bits, changeset);
		cache_state(prealloc, cached_state);
		prealloc = NULL;
		goto out;
	}
hit_next:
	last_start = state->start;
	last_end = state->end;

	/*
	 * | ---- desired range ---- |
	 * | state |
	 *
	 * Just lock what we found and keep going
	 */
	if (state->start == start && state->end <= end) {
		if (state->state & exclusive_bits) {
			*failed_start = state->start;
			cache_state(state, failed_state);
			ret = -EEXIST;
			goto out;
		}

		set_state_bits(tree, state, bits, changeset);
		cache_state(state, cached_state);
		merge_state(tree, state);
		if (last_end == (u64)-1)
			goto out;
		start = last_end + 1;
		state = next_state(state);
		if (start < end && state && state->start == start &&
		    !need_resched())
			goto hit_next;
		goto search_again;
	}

	/*
	 *     | ---- desired range ---- |
	 * | state |
	 *   or
	 * | ------------- state -------------- |
	 *
	 * We need to split the extent we found, and may flip bits on second
	 * half.
	 *
	 * If the extent we found extends past our range, we just split and
	 * search again.  It'll get split again the next time though.
	 *
	 * If the extent we found is inside our range, we set the desired bit
	 * on it.
	 */
	if (state->start < start) {
		if (state->state & exclusive_bits) {
			*failed_start = start;
			cache_state(state, failed_state);
			ret = -EEXIST;
			goto out;
		}

		/*
		 * If this extent already has all the bits we want set, then
		 * skip it, not necessary to split it or do anything with it.
		 */
		if ((state->state & bits) == bits) {
			start = state->end + 1;
			cache_state(state, cached_state);
			goto search_again;
		}

		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc)
			goto search_again;
		ret = split_state(tree, state, prealloc, start);
		if (ret)
			extent_io_tree_panic(tree, state, "split", ret);

		prealloc = NULL;
		if (ret)
			goto out;
		if (state->end <= end) {
			set_state_bits(tree, state, bits, changeset);
			cache_state(state, cached_state);
			merge_state(tree, state);
			if (last_end == (u64)-1)
				goto out;
			start = last_end + 1;
			state = next_state(state);
			if (start < end && state && state->start == start &&
			    !need_resched())
				goto hit_next;
		}
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *     | state | or               | state |
	 *
	 * There's a hole, we need to insert something in it and ignore the
	 * extent we found.
	 */
	if (state->start > start) {
		u64 this_end;
		struct extent_state *inserted_state;

		if (end < last_start)
			this_end = end;
		else
			this_end = last_start - 1;

		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc)
			goto search_again;

		/*
		 * Avoid to free 'prealloc' if it can be merged with the later
		 * extent.
		 */
		prealloc->start = start;
		prealloc->end = this_end;
		inserted_state = insert_state(tree, prealloc, bits, changeset);
		if (IS_ERR(inserted_state)) {
			ret = PTR_ERR(inserted_state);
			extent_io_tree_panic(tree, prealloc, "insert", ret);
		}

		cache_state(inserted_state, cached_state);
		if (inserted_state == prealloc)
			prealloc = NULL;
		start = this_end + 1;
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *                        | state |
	 *
	 * We need to split the extent, and set the bit on the first half
	 */
	if (state->start <= end && state->end > end) {
		if (state->state & exclusive_bits) {
			*failed_start = start;
			cache_state(state, failed_state);
			ret = -EEXIST;
			goto out;
		}

		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc)
			goto search_again;
		ret = split_state(tree, state, prealloc, end + 1);
		if (ret)
			extent_io_tree_panic(tree, state, "split", ret);

		set_state_bits(tree, prealloc, bits, changeset);
		cache_state(prealloc, cached_state);
		merge_state(tree, prealloc);
		prealloc = NULL;
		goto out;
	}

search_again:
	if (start > end)
		goto out;
	spin_unlock(&tree->lock);
	if (gfpflags_allow_blocking(mask))
		cond_resched();
	goto again;

out:
	spin_unlock(&tree->lock);
	if (prealloc)
		free_extent_state(prealloc);

	return ret;

}

int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   u32 bits, struct extent_state **cached_state)
{
	return __set_extent_bit(tree, start, end, bits, NULL, NULL,
				cached_state, NULL);
}

/*
 * Convert all bits in a given range from one bit to another
 *
 * @tree:	the io tree to search
 * @start:	the start offset in bytes
 * @end:	the end offset in bytes (inclusive)
 * @bits:	the bits to set in this range
 * @clear_bits:	the bits to clear in this range
 * @cached_state:	state that we're going to cache
 *
 * This will go through and set bits for the given range.  If any states exist
 * already in this range they are set with the given bit and cleared of the
 * clear_bits.  This is only meant to be used by things that are mergeable, ie.
 * converting from say DELALLOC to DIRTY.  This is not meant to be used with
 * boundary bits like LOCK.
 *
 * All allocations are done with GFP_NOFS.
 */
int convert_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       u32 bits, u32 clear_bits,
		       struct extent_state **cached_state)
{
	struct extent_state *state;
	struct extent_state *prealloc = NULL;
	struct rb_node **p = NULL;
	struct rb_node *parent = NULL;
	int ret = 0;
	u64 last_start;
	u64 last_end;
	bool first_iteration = true;

	btrfs_debug_check_extent_io_range(tree, start, end);
	trace_btrfs_convert_extent_bit(tree, start, end - start + 1, bits,
				       clear_bits);

again:
	if (!prealloc) {
		/*
		 * Best effort, don't worry if extent state allocation fails
		 * here for the first iteration. We might have a cached state
		 * that matches exactly the target range, in which case no
		 * extent state allocations are needed. We'll only know this
		 * after locking the tree.
		 */
		prealloc = alloc_extent_state(GFP_NOFS);
		if (!prealloc && !first_iteration)
			return -ENOMEM;
	}

	spin_lock(&tree->lock);
	if (cached_state && *cached_state) {
		state = *cached_state;
		if (state->start <= start && state->end > start &&
		    extent_state_in_tree(state))
			goto hit_next;
	}

	/*
	 * This search will find all the extents that end after our range
	 * starts.
	 */
	state = tree_search_for_insert(tree, start, &p, &parent);
	if (!state) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc) {
			ret = -ENOMEM;
			goto out;
		}
		prealloc->start = start;
		prealloc->end = end;
		insert_state_fast(tree, prealloc, p, parent, bits, NULL);
		cache_state(prealloc, cached_state);
		prealloc = NULL;
		goto out;
	}
hit_next:
	last_start = state->start;
	last_end = state->end;

	/*
	 * | ---- desired range ---- |
	 * | state |
	 *
	 * Just lock what we found and keep going.
	 */
	if (state->start == start && state->end <= end) {
		set_state_bits(tree, state, bits, NULL);
		cache_state(state, cached_state);
		state = clear_state_bit(tree, state, clear_bits, 0, NULL);
		if (last_end == (u64)-1)
			goto out;
		start = last_end + 1;
		if (start < end && state && state->start == start &&
		    !need_resched())
			goto hit_next;
		goto search_again;
	}

	/*
	 *     | ---- desired range ---- |
	 * | state |
	 *   or
	 * | ------------- state -------------- |
	 *
	 * We need to split the extent we found, and may flip bits on second
	 * half.
	 *
	 * If the extent we found extends past our range, we just split and
	 * search again.  It'll get split again the next time though.
	 *
	 * If the extent we found is inside our range, we set the desired bit
	 * on it.
	 */
	if (state->start < start) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc) {
			ret = -ENOMEM;
			goto out;
		}
		ret = split_state(tree, state, prealloc, start);
		if (ret)
			extent_io_tree_panic(tree, state, "split", ret);
		prealloc = NULL;
		if (ret)
			goto out;
		if (state->end <= end) {
			set_state_bits(tree, state, bits, NULL);
			cache_state(state, cached_state);
			state = clear_state_bit(tree, state, clear_bits, 0, NULL);
			if (last_end == (u64)-1)
				goto out;
			start = last_end + 1;
			if (start < end && state && state->start == start &&
			    !need_resched())
				goto hit_next;
		}
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *     | state | or               | state |
	 *
	 * There's a hole, we need to insert something in it and ignore the
	 * extent we found.
	 */
	if (state->start > start) {
		u64 this_end;
		struct extent_state *inserted_state;

		if (end < last_start)
			this_end = end;
		else
			this_end = last_start - 1;

		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc) {
			ret = -ENOMEM;
			goto out;
		}

		/*
		 * Avoid to free 'prealloc' if it can be merged with the later
		 * extent.
		 */
		prealloc->start = start;
		prealloc->end = this_end;
		inserted_state = insert_state(tree, prealloc, bits, NULL);
		if (IS_ERR(inserted_state)) {
			ret = PTR_ERR(inserted_state);
			extent_io_tree_panic(tree, prealloc, "insert", ret);
		}
		cache_state(inserted_state, cached_state);
		if (inserted_state == prealloc)
			prealloc = NULL;
		start = this_end + 1;
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *                        | state |
	 *
	 * We need to split the extent, and set the bit on the first half.
	 */
	if (state->start <= end && state->end > end) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc) {
			ret = -ENOMEM;
			goto out;
		}

		ret = split_state(tree, state, prealloc, end + 1);
		if (ret)
			extent_io_tree_panic(tree, state, "split", ret);

		set_state_bits(tree, prealloc, bits, NULL);
		cache_state(prealloc, cached_state);
		clear_state_bit(tree, prealloc, clear_bits, 0, NULL);
		prealloc = NULL;
		goto out;
	}

search_again:
	if (start > end)
		goto out;
	spin_unlock(&tree->lock);
	cond_resched();
	first_iteration = false;
	goto again;

out:
	spin_unlock(&tree->lock);
	if (prealloc)
		free_extent_state(prealloc);

	return ret;
}

/*
 * Find the first range that has @bits not set. This range could start before
 * @start.
 *
 * @tree:      the tree to search
 * @start:     offset at/after which the found extent should start
 * @start_ret: records the beginning of the range
 * @end_ret:   records the end of the range (inclusive)
 * @bits:      the set of bits which must be unset
 *
 * Since unallocated range is also considered one which doesn't have the bits
 * set it's possible that @end_ret contains -1, this happens in case the range
 * spans (last_range_end, end of device]. In this case it's up to the caller to
 * trim @end_ret to the appropriate size.
 */
void find_first_clear_extent_bit(struct extent_io_tree *tree, u64 start,
				 u64 *start_ret, u64 *end_ret, u32 bits)
{
	struct extent_state *state;
	struct extent_state *prev = NULL, *next = NULL;

	spin_lock(&tree->lock);

	/* Find first extent with bits cleared */
	while (1) {
		state = tree_search_prev_next(tree, start, &prev, &next);
		if (!state && !next && !prev) {
			/*
			 * Tree is completely empty, send full range and let
			 * caller deal with it
			 */
			*start_ret = 0;
			*end_ret = -1;
			goto out;
		} else if (!state && !next) {
			/*
			 * We are past the last allocated chunk, set start at
			 * the end of the last extent.
			 */
			*start_ret = prev->end + 1;
			*end_ret = -1;
			goto out;
		} else if (!state) {
			state = next;
		}

		/*
		 * At this point 'state' either contains 'start' or start is
		 * before 'state'
		 */
		if (in_range(start, state->start, state->end - state->start + 1)) {
			if (state->state & bits) {
				/*
				 * |--range with bits sets--|
				 *    |
				 *    start
				 */
				start = state->end + 1;
			} else {
				/*
				 * 'start' falls within a range that doesn't
				 * have the bits set, so take its start as the
				 * beginning of the desired range
				 *
				 * |--range with bits cleared----|
				 *      |
				 *      start
				 */
				*start_ret = state->start;
				break;
			}
		} else {
			/*
			 * |---prev range---|---hole/unset---|---node range---|
			 *                          |
			 *                        start
			 *
			 *                        or
			 *
			 * |---hole/unset--||--first node--|
			 * 0   |
			 *    start
			 */
			if (prev)
				*start_ret = prev->end + 1;
			else
				*start_ret = 0;
			break;
		}
	}

	/*
	 * Find the longest stretch from start until an entry which has the
	 * bits set
	 */
	while (state) {
		if (state->end >= start && !(state->state & bits)) {
			*end_ret = state->end;
		} else {
			*end_ret = state->start - 1;
			break;
		}
		state = next_state(state);
	}
out:
	spin_unlock(&tree->lock);
}

/*
 * Count the number of bytes in the tree that have a given bit(s) set for a
 * given range.
 *
 * @tree:         The io tree to search.
 * @start:        The start offset of the range. This value is updated to the
 *                offset of the first byte found with the given bit(s), so it
 *                can end up being bigger than the initial value.
 * @search_end:   The end offset (inclusive value) of the search range.
 * @max_bytes:    The maximum byte count we are interested. The search stops
 *                once it reaches this count.
 * @bits:         The bits the range must have in order to be accounted for.
 *                If multiple bits are set, then only subranges that have all
 *                the bits set are accounted for.
 * @contig:       Indicate if we should ignore holes in the range or not. If
 *                this is true, then stop once we find a hole.
 * @cached_state: A cached state to be used across multiple calls to this
 *                function in order to speedup searches. Use NULL if this is
 *                called only once or if each call does not start where the
 *                previous one ended.
 *
 * Returns the total number of bytes found within the given range that have
 * all given bits set. If the returned number of bytes is greater than zero
 * then @start is updated with the offset of the first byte with the bits set.
 */
u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end, u64 max_bytes,
		     u32 bits, int contig,
		     struct extent_state **cached_state)
{
	struct extent_state *state = NULL;
	struct extent_state *cached;
	u64 cur_start = *start;
	u64 total_bytes = 0;
	u64 last = 0;
	int found = 0;

	if (WARN_ON(search_end < cur_start))
		return 0;

	spin_lock(&tree->lock);

	if (!cached_state || !*cached_state)
		goto search;

	cached = *cached_state;

	if (!extent_state_in_tree(cached))
		goto search;

	if (cached->start <= cur_start && cur_start <= cached->end) {
		state = cached;
	} else if (cached->start > cur_start) {
		struct extent_state *prev;

		/*
		 * The cached state starts after our search range's start. Check
		 * if the previous state record starts at or before the range we
		 * are looking for, and if so, use it - this is a common case
		 * when there are holes between records in the tree. If there is
		 * no previous state record, we can start from our cached state.
		 */
		prev = prev_state(cached);
		if (!prev)
			state = cached;
		else if (prev->start <= cur_start && cur_start <= prev->end)
			state = prev;
	}

	/*
	 * This search will find all the extents that end after our range
	 * starts.
	 */
search:
	if (!state)
		state = tree_search(tree, cur_start);

	while (state) {
		if (state->start > search_end)
			break;
		if (contig && found && state->start > last + 1)
			break;
		if (state->end >= cur_start && (state->state & bits) == bits) {
			total_bytes += min(search_end, state->end) + 1 -
				       max(cur_start, state->start);
			if (total_bytes >= max_bytes)
				break;
			if (!found) {
				*start = max(cur_start, state->start);
				found = 1;
			}
			last = state->end;
		} else if (contig && found) {
			break;
		}
		state = next_state(state);
	}

	if (cached_state) {
		free_extent_state(*cached_state);
		*cached_state = state;
		if (state)
			refcount_inc(&state->refs);
	}

	spin_unlock(&tree->lock);

	return total_bytes;
}

/*
 * Check if the single @bit exists in the given range.
 */
bool test_range_bit_exists(struct extent_io_tree *tree, u64 start, u64 end, u32 bit)
{
	struct extent_state *state = NULL;
	bool bitset = false;

	ASSERT(is_power_of_2(bit));

	spin_lock(&tree->lock);
	state = tree_search(tree, start);
	while (state && start <= end) {
		if (state->start > end)
			break;

		if (state->state & bit) {
			bitset = true;
			break;
		}

		/* If state->end is (u64)-1, start will overflow to 0 */
		start = state->end + 1;
		if (start > end || start == 0)
			break;
		state = next_state(state);
	}
	spin_unlock(&tree->lock);
	return bitset;
}

/*
 * Check if the whole range [@start,@end) contains the single @bit set.
 */
bool test_range_bit(struct extent_io_tree *tree, u64 start, u64 end, u32 bit,
		    struct extent_state *cached)
{
	struct extent_state *state = NULL;
	bool bitset = true;

	ASSERT(is_power_of_2(bit));

	spin_lock(&tree->lock);
	if (cached && extent_state_in_tree(cached) && cached->start <= start &&
	    cached->end > start)
		state = cached;
	else
		state = tree_search(tree, start);
	while (state && start <= end) {
		if (state->start > start) {
			bitset = false;
			break;
		}

		if (state->start > end)
			break;

		if ((state->state & bit) == 0) {
			bitset = false;
			break;
		}

		if (state->end == (u64)-1)
			break;

		/*
		 * Last entry (if state->end is (u64)-1 and overflow happens),
		 * or next entry starts after the range.
		 */
		start = state->end + 1;
		if (start > end || start == 0)
			break;
		state = next_state(state);
	}

	/* We ran out of states and were still inside of our range. */
	if (!state)
		bitset = false;
	spin_unlock(&tree->lock);
	return bitset;
}

/* Wrappers around set/clear extent bit */
int set_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			   u32 bits, struct extent_changeset *changeset)
{
	/*
	 * We don't support EXTENT_LOCK_BITS yet, as current changeset will
	 * record any bits changed, so for EXTENT_LOCK_BITS case, it will either
	 * fail with -EEXIST or changeset will record the whole range.
	 */
	ASSERT(!(bits & EXTENT_LOCK_BITS));

	return __set_extent_bit(tree, start, end, bits, NULL, NULL, NULL, changeset);
}

int clear_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			     u32 bits, struct extent_changeset *changeset)
{
	/*
	 * Don't support EXTENT_LOCK_BITS case, same reason as
	 * set_record_extent_bits().
	 */
	ASSERT(!(bits & EXTENT_LOCK_BITS));

	return __clear_extent_bit(tree, start, end, bits, NULL, changeset);
}

bool __try_lock_extent(struct extent_io_tree *tree, u64 start, u64 end, u32 bits,
		       struct extent_state **cached)
{
	int err;
	u64 failed_start;

	err = __set_extent_bit(tree, start, end, bits, &failed_start,
			       NULL, cached, NULL);
	if (err == -EEXIST) {
		if (failed_start > start)
			clear_extent_bit(tree, start, failed_start - 1, bits, cached);
		return 0;
	}
	return 1;
}

/*
 * Either insert or lock state struct between start and end use mask to tell
 * us if waiting is desired.
 */
int __lock_extent(struct extent_io_tree *tree, u64 start, u64 end, u32 bits,
		  struct extent_state **cached_state)
{
	struct extent_state *failed_state = NULL;
	int err;
	u64 failed_start;

	err = __set_extent_bit(tree, start, end, bits, &failed_start,
			       &failed_state, cached_state, NULL);
	while (err == -EEXIST) {
		if (failed_start != start)
			clear_extent_bit(tree, start, failed_start - 1,
					 bits, cached_state);

		wait_extent_bit(tree, failed_start, end, bits, &failed_state);
		err = __set_extent_bit(tree, start, end, bits,
				       &failed_start, &failed_state,
				       cached_state, NULL);
	}
	return err;
}

void __cold extent_state_free_cachep(void)
{
	btrfs_extent_state_leak_debug_check();
	kmem_cache_destroy(extent_state_cache);
}

int __init extent_state_init_cachep(void)
{
	extent_state_cache = kmem_cache_create("btrfs_extent_state",
					       sizeof(struct extent_state), 0, 0,
					       NULL);
	if (!extent_state_cache)
		return -ENOMEM;

	return 0;
}
