// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include <trace/events/btrfs.h>
#include "ctree.h"
#include "extent-io-tree.h"
#include "btrfs_inode.h"

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
