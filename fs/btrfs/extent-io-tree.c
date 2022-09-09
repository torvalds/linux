// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include <trace/events/btrfs.h>
#include "ctree.h"
#include "extent-io-tree.h"

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
