// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include <trace/events/btrfs.h>
#include "ctree.h"
#include "extent-io-tree.h"
#include "btrfs_inode.h"
#include "misc.h"

static struct kmem_cache *extent_state_cache;

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
		kmem_cache_free(extent_state_cache, state);
	}
}

void __btrfs_debug_check_extent_io_range(const char *caller,
					 struct extent_io_tree *tree, u64 start,
					 u64 end)
{
	struct inode *inode = tree->private_data;
	u64 isize;

	if (!inode || !is_data_inode(inode))
		return;

	isize = i_size_read(inode);
	if (end >= PAGE_SIZE && (end % 2) == 0 && end != isize - 1) {
		btrfs_debug_rl(BTRFS_I(inode)->root->fs_info,
		    "%s: ino %llu isize %llu odd range [%llu,%llu]",
			caller, btrfs_ino(BTRFS_I(inode)), isize, start, end);
	}
}
#else
#define btrfs_leak_debug_add_state(state)		do {} while (0)
#define btrfs_leak_debug_del_state(state)		do {} while (0)
#define btrfs_extent_state_leak_debug_check()		do {} while (0)
#endif

/*
 * For the file_extent_tree, we want to hold the inode lock when we lookup and
 * update the disk_i_size, but lockdep will complain because our io_tree we hold
 * the tree lock and get the inode lock when setting delalloc.  These two things
 * are unrelated, so make a class for the file_extent_tree so we don't get the
 * two locking patterns mixed up.
 */
static struct lock_class_key file_extent_tree_class;

void extent_io_tree_init(struct btrfs_fs_info *fs_info,
			 struct extent_io_tree *tree, unsigned int owner,
			 void *private_data)
{
	tree->fs_info = fs_info;
	tree->state = RB_ROOT;
	tree->dirty_bytes = 0;
	spin_lock_init(&tree->lock);
	tree->private_data = private_data;
	tree->owner = owner;
	if (owner == IO_TREE_INODE_FILE_EXTENT)
		lockdep_set_class(&tree->lock, &file_extent_tree_class);
}

void extent_io_tree_release(struct extent_io_tree *tree)
{
	spin_lock(&tree->lock);
	/*
	 * Do a single barrier for the waitqueue_active check here, the state
	 * of the waitqueue should not change once extent_io_tree_release is
	 * called.
	 */
	smp_mb();
	while (!RB_EMPTY_ROOT(&tree->state)) {
		struct rb_node *node;
		struct extent_state *state;

		node = rb_first(&tree->state);
		state = rb_entry(node, struct extent_state, rb_node);
		rb_erase(&state->rb_node, &tree->state);
		RB_CLEAR_NODE(&state->rb_node);
		/*
		 * btree io trees aren't supposed to have tasks waiting for
		 * changes in the flags of extent states ever.
		 */
		ASSERT(!waitqueue_active(&state->wq));
		free_extent_state(state);

		cond_resched_lock(&tree->lock);
	}
	spin_unlock(&tree->lock);
}

struct extent_state *alloc_extent_state(gfp_t mask)
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

struct extent_state *alloc_extent_state_atomic(struct extent_state *prealloc)
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
struct rb_node *tree_search_for_insert(struct extent_io_tree *tree, u64 offset,
				       struct rb_node ***node_ret,
				       struct rb_node **parent_ret)
{
	struct rb_root *root = &tree->state;
	struct rb_node **node = &root->rb_node;
	struct rb_node *prev = NULL;
	struct tree_entry *entry;

	while (*node) {
		prev = *node;
		entry = rb_entry(prev, struct tree_entry, rb_node);

		if (offset < entry->start)
			node = &(*node)->rb_left;
		else if (offset > entry->end)
			node = &(*node)->rb_right;
		else
			return *node;
	}

	if (node_ret)
		*node_ret = node;
	if (parent_ret)
		*parent_ret = prev;

	/* Search neighbors until we find the first one past the end */
	while (prev && offset > entry->end) {
		prev = rb_next(prev);
		entry = rb_entry(prev, struct tree_entry, rb_node);
	}

	return prev;
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
struct rb_node *tree_search_prev_next(struct extent_io_tree *tree, u64 offset,
				      struct rb_node **prev_ret,
				      struct rb_node **next_ret)
{
	struct rb_root *root = &tree->state;
	struct rb_node **node = &root->rb_node;
	struct rb_node *prev = NULL;
	struct rb_node *orig_prev = NULL;
	struct tree_entry *entry;

	ASSERT(prev_ret);
	ASSERT(next_ret);

	while (*node) {
		prev = *node;
		entry = rb_entry(prev, struct tree_entry, rb_node);

		if (offset < entry->start)
			node = &(*node)->rb_left;
		else if (offset > entry->end)
			node = &(*node)->rb_right;
		else
			return *node;
	}

	orig_prev = prev;
	while (prev && offset > entry->end) {
		prev = rb_next(prev);
		entry = rb_entry(prev, struct tree_entry, rb_node);
	}
	*next_ret = prev;
	prev = orig_prev;

	entry = rb_entry(prev, struct tree_entry, rb_node);
	while (prev && offset < entry->start) {
		prev = rb_prev(prev);
		entry = rb_entry(prev, struct tree_entry, rb_node);
	}
	*prev_ret = prev;

	return NULL;
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
void merge_state(struct extent_io_tree *tree, struct extent_state *state)
{
	struct extent_state *other;
	struct rb_node *other_node;

	if (state->state & (EXTENT_LOCKED | EXTENT_BOUNDARY))
		return;

	other_node = rb_prev(&state->rb_node);
	if (other_node) {
		other = rb_entry(other_node, struct extent_state, rb_node);
		if (other->end == state->start - 1 &&
		    other->state == state->state) {
			if (tree->private_data &&
			    is_data_inode(tree->private_data))
				btrfs_merge_delalloc_extent(tree->private_data,
							    state, other);
			state->start = other->start;
			rb_erase(&other->rb_node, &tree->state);
			RB_CLEAR_NODE(&other->rb_node);
			free_extent_state(other);
		}
	}
	other_node = rb_next(&state->rb_node);
	if (other_node) {
		other = rb_entry(other_node, struct extent_state, rb_node);
		if (other->start == state->end + 1 &&
		    other->state == state->state) {
			if (tree->private_data &&
			    is_data_inode(tree->private_data))
				btrfs_merge_delalloc_extent(tree->private_data,
							    state, other);
			state->end = other->end;
			rb_erase(&other->rb_node, &tree->state);
			RB_CLEAR_NODE(&other->rb_node);
			free_extent_state(other);
		}
	}
}

void set_state_bits(struct extent_io_tree *tree, struct extent_state *state,
		    u32 bits, struct extent_changeset *changeset)
{
	u32 bits_to_set = bits & ~EXTENT_CTLBITS;
	int ret;

	if (tree->private_data && is_data_inode(tree->private_data))
		btrfs_set_delalloc_extent(tree->private_data, state, bits);

	if ((bits_to_set & EXTENT_DIRTY) && !(state->state & EXTENT_DIRTY)) {
		u64 range = state->end - state->start + 1;
		tree->dirty_bytes += range;
	}
	ret = add_extent_changeset(state, bits_to_set, changeset, 1);
	BUG_ON(ret < 0);
	state->state |= bits_to_set;
}

/*
 * Insert an extent_state struct into the tree.  'bits' are set on the
 * struct before it is inserted.
 *
 * This may return -EEXIST if the extent is already there, in which case the
 * state struct is freed.
 *
 * The tree lock is not taken internally.  This is a utility function and
 * probably isn't what you want to call (see set/clear_extent_bit).
 */
int insert_state(struct extent_io_tree *tree, struct extent_state *state,
		 u32 bits, struct extent_changeset *changeset)
{
	struct rb_node **node;
	struct rb_node *parent;
	const u64 end = state->end;

	set_state_bits(tree, state, bits, changeset);

	node = &tree->state.rb_node;
	while (*node) {
		struct tree_entry *entry;

		parent = *node;
		entry = rb_entry(parent, struct tree_entry, rb_node);

		if (end < entry->start) {
			node = &(*node)->rb_left;
		} else if (end > entry->end) {
			node = &(*node)->rb_right;
		} else {
			btrfs_err(tree->fs_info,
			       "found node %llu %llu on insert of %llu %llu",
			       entry->start, entry->end, state->start, end);
			return -EEXIST;
		}
	}

	rb_link_node(&state->rb_node, parent, node);
	rb_insert_color(&state->rb_node, &tree->state);

	merge_state(tree, state);
	return 0;
}

/*
 * Insert state to @tree to the location given by @node and @parent.
 */
void insert_state_fast(struct extent_io_tree *tree, struct extent_state *state,
		       struct rb_node **node, struct rb_node *parent,
		       unsigned bits, struct extent_changeset *changeset)
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
int split_state(struct extent_io_tree *tree, struct extent_state *orig,
		struct extent_state *prealloc, u64 split)
{
	struct rb_node *parent = NULL;
	struct rb_node **node;

	if (tree->private_data && is_data_inode(tree->private_data))
		btrfs_split_delalloc_extent(tree->private_data, orig, split);

	prealloc->start = orig->start;
	prealloc->end = split - 1;
	prealloc->state = orig->state;
	orig->start = split;

	parent = &orig->rb_node;
	node = &parent;
	while (*node) {
		struct tree_entry *entry;

		parent = *node;
		entry = rb_entry(parent, struct tree_entry, rb_node);

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
struct extent_state *clear_state_bit(struct extent_io_tree *tree,
				     struct extent_state *state, u32 bits,
				     int wake,
				     struct extent_changeset *changeset)
{
	struct extent_state *next;
	u32 bits_to_clear = bits & ~EXTENT_CTLBITS;
	int ret;

	if ((bits_to_clear & EXTENT_DIRTY) && (state->state & EXTENT_DIRTY)) {
		u64 range = state->end - state->start + 1;
		WARN_ON(range > tree->dirty_bytes);
		tree->dirty_bytes -= range;
	}

	if (tree->private_data && is_data_inode(tree->private_data))
		btrfs_clear_delalloc_extent(tree->private_data, state, bits);

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
	struct rb_node *node, *prev = NULL, *next;

	spin_lock(&tree->lock);

	/* Find first extent with bits cleared */
	while (1) {
		node = tree_search_prev_next(tree, start, &prev, &next);
		if (!node && !next && !prev) {
			/*
			 * Tree is completely empty, send full range and let
			 * caller deal with it
			 */
			*start_ret = 0;
			*end_ret = -1;
			goto out;
		} else if (!node && !next) {
			/*
			 * We are past the last allocated chunk, set start at
			 * the end of the last extent.
			 */
			state = rb_entry(prev, struct extent_state, rb_node);
			*start_ret = state->end + 1;
			*end_ret = -1;
			goto out;
		} else if (!node) {
			node = next;
		}
		/*
		 * At this point 'node' either contains 'start' or start is
		 * before 'node'
		 */
		state = rb_entry(node, struct extent_state, rb_node);

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
			if (prev) {
				state = rb_entry(prev, struct extent_state,
						 rb_node);
				*start_ret = state->end + 1;
			} else {
				*start_ret = 0;
			}
			break;
		}
	}

	/*
	 * Find the longest stretch from start until an entry which has the
	 * bits set
	 */
	while (1) {
		state = rb_entry(node, struct extent_state, rb_node);
		if (state->end >= start && !(state->state & bits)) {
			*end_ret = state->end;
		} else {
			*end_ret = state->start - 1;
			break;
		}

		node = rb_next(node);
		if (!node)
			break;
	}
out:
	spin_unlock(&tree->lock);
}

/* Wrappers around set/clear extent bit */
int set_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			   u32 bits, struct extent_changeset *changeset)
{
	/*
	 * We don't support EXTENT_LOCKED yet, as current changeset will
	 * record any bits changed, so for EXTENT_LOCKED case, it will
	 * either fail with -EEXIST or changeset will record the whole
	 * range.
	 */
	ASSERT(!(bits & EXTENT_LOCKED));

	return set_extent_bit(tree, start, end, bits, 0, NULL, NULL, GFP_NOFS,
			      changeset);
}

int clear_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			     u32 bits, struct extent_changeset *changeset)
{
	/*
	 * Don't support EXTENT_LOCKED case, same reason as
	 * set_record_extent_bits().
	 */
	ASSERT(!(bits & EXTENT_LOCKED));

	return __clear_extent_bit(tree, start, end, bits, 0, 0, NULL, GFP_NOFS,
				  changeset);
}

int try_lock_extent(struct extent_io_tree *tree, u64 start, u64 end)
{
	int err;
	u64 failed_start;

	err = set_extent_bit(tree, start, end, EXTENT_LOCKED, EXTENT_LOCKED,
			     &failed_start, NULL, GFP_NOFS, NULL);
	if (err == -EEXIST) {
		if (failed_start > start)
			clear_extent_bit(tree, start, failed_start - 1,
					 EXTENT_LOCKED, 1, 0, NULL);
		return 0;
	}
	return 1;
}

/*
 * Either insert or lock state struct between start and end use mask to tell
 * us if waiting is desired.
 */
int lock_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		     struct extent_state **cached_state)
{
	int err;
	u64 failed_start;

	while (1) {
		err = set_extent_bit(tree, start, end, EXTENT_LOCKED,
				     EXTENT_LOCKED, &failed_start,
				     cached_state, GFP_NOFS, NULL);
		if (err == -EEXIST) {
			wait_extent_bit(tree, failed_start, end, EXTENT_LOCKED);
			start = failed_start;
		} else
			break;
		WARN_ON(start > end);
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
			sizeof(struct extent_state), 0,
			SLAB_MEM_SPREAD, NULL);
	if (!extent_state_cache)
		return -ENOMEM;

	return 0;
}
