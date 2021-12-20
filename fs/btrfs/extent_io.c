// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/prefetch.h>
#include <linux/cleancache.h>
#include <linux/fsverity.h>
#include "misc.h"
#include "extent_io.h"
#include "extent-io-tree.h"
#include "extent_map.h"
#include "ctree.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "check-integrity.h"
#include "locking.h"
#include "rcu-string.h"
#include "backref.h"
#include "disk-io.h"
#include "subpage.h"
#include "zoned.h"
#include "block-group.h"

static struct kmem_cache *extent_state_cache;
static struct kmem_cache *extent_buffer_cache;
static struct bio_set btrfs_bioset;

static inline bool extent_state_in_tree(const struct extent_state *state)
{
	return !RB_EMPTY_NODE(&state->rb_node);
}

#ifdef CONFIG_BTRFS_DEBUG
static LIST_HEAD(states);
static DEFINE_SPINLOCK(leak_lock);

static inline void btrfs_leak_debug_add(spinlock_t *lock,
					struct list_head *new,
					struct list_head *head)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	list_add(new, head);
	spin_unlock_irqrestore(lock, flags);
}

static inline void btrfs_leak_debug_del(spinlock_t *lock,
					struct list_head *entry)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	list_del(entry);
	spin_unlock_irqrestore(lock, flags);
}

void btrfs_extent_buffer_leak_debug_check(struct btrfs_fs_info *fs_info)
{
	struct extent_buffer *eb;
	unsigned long flags;

	/*
	 * If we didn't get into open_ctree our allocated_ebs will not be
	 * initialized, so just skip this.
	 */
	if (!fs_info->allocated_ebs.next)
		return;

	spin_lock_irqsave(&fs_info->eb_leak_lock, flags);
	while (!list_empty(&fs_info->allocated_ebs)) {
		eb = list_first_entry(&fs_info->allocated_ebs,
				      struct extent_buffer, leak_list);
		pr_err(
	"BTRFS: buffer leak start %llu len %lu refs %d bflags %lu owner %llu\n",
		       eb->start, eb->len, atomic_read(&eb->refs), eb->bflags,
		       btrfs_header_owner(eb));
		list_del(&eb->leak_list);
		kmem_cache_free(extent_buffer_cache, eb);
	}
	spin_unlock_irqrestore(&fs_info->eb_leak_lock, flags);
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

#define btrfs_debug_check_extent_io_range(tree, start, end)		\
	__btrfs_debug_check_extent_io_range(__func__, (tree), (start), (end))
static inline void __btrfs_debug_check_extent_io_range(const char *caller,
		struct extent_io_tree *tree, u64 start, u64 end)
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
#define btrfs_leak_debug_add(lock, new, head)	do {} while (0)
#define btrfs_leak_debug_del(lock, entry)	do {} while (0)
#define btrfs_extent_state_leak_debug_check()	do {} while (0)
#define btrfs_debug_check_extent_io_range(c, s, e)	do {} while (0)
#endif

struct tree_entry {
	u64 start;
	u64 end;
	struct rb_node rb_node;
};

struct extent_page_data {
	struct btrfs_bio_ctrl bio_ctrl;
	/* tells writepage not to lock the state bits for this range
	 * it still does the unlocking
	 */
	unsigned int extent_locked:1;

	/* tells the submit_bio code to use REQ_SYNC */
	unsigned int sync_io:1;
};

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

int __must_check submit_one_bio(struct bio *bio, int mirror_num,
				unsigned long bio_flags)
{
	blk_status_t ret = 0;
	struct extent_io_tree *tree = bio->bi_private;

	bio->bi_private = NULL;

	/* Caller should ensure the bio has at least some range added */
	ASSERT(bio->bi_iter.bi_size);
	if (is_data_inode(tree->private_data))
		ret = btrfs_submit_data_bio(tree->private_data, bio, mirror_num,
					    bio_flags);
	else
		ret = btrfs_submit_metadata_bio(tree->private_data, bio,
						mirror_num, bio_flags);

	return blk_status_to_errno(ret);
}

/* Cleanup unsubmitted bios */
static void end_write_bio(struct extent_page_data *epd, int ret)
{
	struct bio *bio = epd->bio_ctrl.bio;

	if (bio) {
		bio->bi_status = errno_to_blk_status(ret);
		bio_endio(bio);
		epd->bio_ctrl.bio = NULL;
	}
}

/*
 * Submit bio from extent page data via submit_one_bio
 *
 * Return 0 if everything is OK.
 * Return <0 for error.
 */
static int __must_check flush_write_bio(struct extent_page_data *epd)
{
	int ret = 0;
	struct bio *bio = epd->bio_ctrl.bio;

	if (bio) {
		ret = submit_one_bio(bio, 0, 0);
		/*
		 * Clean up of epd->bio is handled by its endio function.
		 * And endio is either triggered by successful bio execution
		 * or the error handler of submit bio hook.
		 * So at this point, no matter what happened, we don't need
		 * to clean up epd->bio.
		 */
		epd->bio_ctrl.bio = NULL;
	}
	return ret;
}

int __init extent_state_cache_init(void)
{
	extent_state_cache = kmem_cache_create("btrfs_extent_state",
			sizeof(struct extent_state), 0,
			SLAB_MEM_SPREAD, NULL);
	if (!extent_state_cache)
		return -ENOMEM;
	return 0;
}

int __init extent_io_init(void)
{
	extent_buffer_cache = kmem_cache_create("btrfs_extent_buffer",
			sizeof(struct extent_buffer), 0,
			SLAB_MEM_SPREAD, NULL);
	if (!extent_buffer_cache)
		return -ENOMEM;

	if (bioset_init(&btrfs_bioset, BIO_POOL_SIZE,
			offsetof(struct btrfs_bio, bio),
			BIOSET_NEED_BVECS))
		goto free_buffer_cache;

	if (bioset_integrity_create(&btrfs_bioset, BIO_POOL_SIZE))
		goto free_bioset;

	return 0;

free_bioset:
	bioset_exit(&btrfs_bioset);

free_buffer_cache:
	kmem_cache_destroy(extent_buffer_cache);
	extent_buffer_cache = NULL;
	return -ENOMEM;
}

void __cold extent_state_cache_exit(void)
{
	btrfs_extent_state_leak_debug_check();
	kmem_cache_destroy(extent_state_cache);
}

void __cold extent_io_exit(void)
{
	/*
	 * Make sure all delayed rcu free are flushed before we
	 * destroy caches.
	 */
	rcu_barrier();
	kmem_cache_destroy(extent_buffer_cache);
	bioset_exit(&btrfs_bioset);
}

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
	state->failrec = NULL;
	RB_CLEAR_NODE(&state->rb_node);
	btrfs_leak_debug_add(&leak_lock, &state->leak_list, &states);
	refcount_set(&state->refs, 1);
	init_waitqueue_head(&state->wq);
	trace_alloc_extent_state(state, mask, _RET_IP_);
	return state;
}

void free_extent_state(struct extent_state *state)
{
	if (!state)
		return;
	if (refcount_dec_and_test(&state->refs)) {
		WARN_ON(extent_state_in_tree(state));
		btrfs_leak_debug_del(&leak_lock, &state->leak_list);
		trace_free_extent_state(state, _RET_IP_);
		kmem_cache_free(extent_state_cache, state);
	}
}

static struct rb_node *tree_insert(struct rb_root *root,
				   struct rb_node *search_start,
				   u64 offset,
				   struct rb_node *node,
				   struct rb_node ***p_in,
				   struct rb_node **parent_in)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct tree_entry *entry;

	if (p_in && parent_in) {
		p = *p_in;
		parent = *parent_in;
		goto do_insert;
	}

	p = search_start ? &search_start : &root->rb_node;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct tree_entry, rb_node);

		if (offset < entry->start)
			p = &(*p)->rb_left;
		else if (offset > entry->end)
			p = &(*p)->rb_right;
		else
			return parent;
	}

do_insert:
	rb_link_node(node, parent, p);
	rb_insert_color(node, root);
	return NULL;
}

/**
 * Search @tree for an entry that contains @offset. Such entry would have
 * entry->start <= offset && entry->end >= offset.
 *
 * @tree:       the tree to search
 * @offset:     offset that should fall within an entry in @tree
 * @next_ret:   pointer to the first entry whose range ends after @offset
 * @prev_ret:   pointer to the first entry whose range begins before @offset
 * @p_ret:      pointer where new node should be anchored (used when inserting an
 *	        entry in the tree)
 * @parent_ret: points to entry which would have been the parent of the entry,
 *               containing @offset
 *
 * This function returns a pointer to the entry that contains @offset byte
 * address. If no such entry exists, then NULL is returned and the other
 * pointer arguments to the function are filled, otherwise the found entry is
 * returned and other pointers are left untouched.
 */
static struct rb_node *__etree_search(struct extent_io_tree *tree, u64 offset,
				      struct rb_node **next_ret,
				      struct rb_node **prev_ret,
				      struct rb_node ***p_ret,
				      struct rb_node **parent_ret)
{
	struct rb_root *root = &tree->state;
	struct rb_node **n = &root->rb_node;
	struct rb_node *prev = NULL;
	struct rb_node *orig_prev = NULL;
	struct tree_entry *entry;
	struct tree_entry *prev_entry = NULL;

	while (*n) {
		prev = *n;
		entry = rb_entry(prev, struct tree_entry, rb_node);
		prev_entry = entry;

		if (offset < entry->start)
			n = &(*n)->rb_left;
		else if (offset > entry->end)
			n = &(*n)->rb_right;
		else
			return *n;
	}

	if (p_ret)
		*p_ret = n;
	if (parent_ret)
		*parent_ret = prev;

	if (next_ret) {
		orig_prev = prev;
		while (prev && offset > prev_entry->end) {
			prev = rb_next(prev);
			prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		}
		*next_ret = prev;
		prev = orig_prev;
	}

	if (prev_ret) {
		prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		while (prev && offset < prev_entry->start) {
			prev = rb_prev(prev);
			prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		}
		*prev_ret = prev;
	}
	return NULL;
}

static inline struct rb_node *
tree_search_for_insert(struct extent_io_tree *tree,
		       u64 offset,
		       struct rb_node ***p_ret,
		       struct rb_node **parent_ret)
{
	struct rb_node *next= NULL;
	struct rb_node *ret;

	ret = __etree_search(tree, offset, &next, NULL, p_ret, parent_ret);
	if (!ret)
		return next;
	return ret;
}

static inline struct rb_node *tree_search(struct extent_io_tree *tree,
					  u64 offset)
{
	return tree_search_for_insert(tree, offset, NULL, NULL);
}

/*
 * utility function to look for merge candidates inside a given range.
 * Any extents with matching state are merged together into a single
 * extent in the tree.  Extents with EXTENT_IO in their state field
 * are not merged because the end_io handlers need to be able to do
 * operations on them without sleeping (or doing allocations/splits).
 *
 * This should be called with the tree lock held.
 */
static void merge_state(struct extent_io_tree *tree,
		        struct extent_state *state)
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

static void set_state_bits(struct extent_io_tree *tree,
			   struct extent_state *state, u32 *bits,
			   struct extent_changeset *changeset);

/*
 * insert an extent_state struct into the tree.  'bits' are set on the
 * struct before it is inserted.
 *
 * This may return -EEXIST if the extent is already there, in which case the
 * state struct is freed.
 *
 * The tree lock is not taken internally.  This is a utility function and
 * probably isn't what you want to call (see set/clear_extent_bit).
 */
static int insert_state(struct extent_io_tree *tree,
			struct extent_state *state, u64 start, u64 end,
			struct rb_node ***p,
			struct rb_node **parent,
			u32 *bits, struct extent_changeset *changeset)
{
	struct rb_node *node;

	if (end < start) {
		btrfs_err(tree->fs_info,
			"insert state: end < start %llu %llu", end, start);
		WARN_ON(1);
	}
	state->start = start;
	state->end = end;

	set_state_bits(tree, state, bits, changeset);

	node = tree_insert(&tree->state, NULL, end, &state->rb_node, p, parent);
	if (node) {
		struct extent_state *found;
		found = rb_entry(node, struct extent_state, rb_node);
		btrfs_err(tree->fs_info,
		       "found node %llu %llu on insert of %llu %llu",
		       found->start, found->end, start, end);
		return -EEXIST;
	}
	merge_state(tree, state);
	return 0;
}

/*
 * split a given extent state struct in two, inserting the preallocated
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
	struct rb_node *node;

	if (tree->private_data && is_data_inode(tree->private_data))
		btrfs_split_delalloc_extent(tree->private_data, orig, split);

	prealloc->start = orig->start;
	prealloc->end = split - 1;
	prealloc->state = orig->state;
	orig->start = split;

	node = tree_insert(&tree->state, &orig->rb_node, prealloc->end,
			   &prealloc->rb_node, NULL, NULL);
	if (node) {
		free_extent_state(prealloc);
		return -EEXIST;
	}
	return 0;
}

static struct extent_state *next_state(struct extent_state *state)
{
	struct rb_node *next = rb_next(&state->rb_node);
	if (next)
		return rb_entry(next, struct extent_state, rb_node);
	else
		return NULL;
}

/*
 * utility function to clear some bits in an extent state struct.
 * it will optionally wake up anyone waiting on this state (wake == 1).
 *
 * If no bits are set on the state struct after clearing things, the
 * struct is freed and removed from the tree
 */
static struct extent_state *clear_state_bit(struct extent_io_tree *tree,
					    struct extent_state *state,
					    u32 *bits, int wake,
					    struct extent_changeset *changeset)
{
	struct extent_state *next;
	u32 bits_to_clear = *bits & ~EXTENT_CTLBITS;
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

static struct extent_state *
alloc_extent_state_atomic(struct extent_state *prealloc)
{
	if (!prealloc)
		prealloc = alloc_extent_state(GFP_ATOMIC);

	return prealloc;
}

static void extent_io_tree_panic(struct extent_io_tree *tree, int err)
{
	btrfs_panic(tree->fs_info, err,
	"locking error: extent tree was modified by another thread while locked");
}

/*
 * clear some bits on a range in the tree.  This may require splitting
 * or inserting elements in the tree, so the gfp mask is used to
 * indicate which allocations or sleeping are allowed.
 *
 * pass 'wake' == 1 to kick any sleepers, and 'delete' == 1 to remove
 * the given range from the tree regardless of state (ie for truncate).
 *
 * the range [start, end] is inclusive.
 *
 * This takes the tree lock, and returns 0 on success and < 0 on error.
 */
int __clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       u32 bits, int wake, int delete,
		       struct extent_state **cached_state,
		       gfp_t mask, struct extent_changeset *changeset)
{
	struct extent_state *state;
	struct extent_state *cached;
	struct extent_state *prealloc = NULL;
	struct rb_node *node;
	u64 last_end;
	int err;
	int clear = 0;

	btrfs_debug_check_extent_io_range(tree, start, end);
	trace_btrfs_clear_extent_bit(tree, start, end - start + 1, bits);

	if (bits & EXTENT_DELALLOC)
		bits |= EXTENT_NORESERVE;

	if (delete)
		bits |= ~EXTENT_CTLBITS;

	if (bits & (EXTENT_LOCKED | EXTENT_BOUNDARY))
		clear = 1;
again:
	if (!prealloc && gfpflags_allow_blocking(mask)) {
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
	/*
	 * this search will find the extents that end after
	 * our range starts
	 */
	node = tree_search(tree, start);
	if (!node)
		goto out;
	state = rb_entry(node, struct extent_state, rb_node);
hit_next:
	if (state->start > end)
		goto out;
	WARN_ON(state->end < start);
	last_end = state->end;

	/* the state doesn't have the wanted bits, go ahead */
	if (!(state->state & bits)) {
		state = next_state(state);
		goto next;
	}

	/*
	 *     | ---- desired range ---- |
	 *  | state | or
	 *  | ------------- state -------------- |
	 *
	 * We need to split the extent we found, and may flip
	 * bits on second half.
	 *
	 * If the extent we found extends past our range, we
	 * just split and search again.  It'll get split again
	 * the next time though.
	 *
	 * If the extent we found is inside our range, we clear
	 * the desired bit on it.
	 */

	if (state->start < start) {
		prealloc = alloc_extent_state_atomic(prealloc);
		BUG_ON(!prealloc);
		err = split_state(tree, state, prealloc, start);
		if (err)
			extent_io_tree_panic(tree, err);

		prealloc = NULL;
		if (err)
			goto out;
		if (state->end <= end) {
			state = clear_state_bit(tree, state, &bits, wake,
						changeset);
			goto next;
		}
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *                        | state |
	 * We need to split the extent, and clear the bit
	 * on the first half
	 */
	if (state->start <= end && state->end > end) {
		prealloc = alloc_extent_state_atomic(prealloc);
		BUG_ON(!prealloc);
		err = split_state(tree, state, prealloc, end + 1);
		if (err)
			extent_io_tree_panic(tree, err);

		if (wake)
			wake_up(&state->wq);

		clear_state_bit(tree, prealloc, &bits, wake, changeset);

		prealloc = NULL;
		goto out;
	}

	state = clear_state_bit(tree, state, &bits, wake, changeset);
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

static void wait_on_state(struct extent_io_tree *tree,
			  struct extent_state *state)
		__releases(tree->lock)
		__acquires(tree->lock)
{
	DEFINE_WAIT(wait);
	prepare_to_wait(&state->wq, &wait, TASK_UNINTERRUPTIBLE);
	spin_unlock(&tree->lock);
	schedule();
	spin_lock(&tree->lock);
	finish_wait(&state->wq, &wait);
}

/*
 * waits for one or more bits to clear on a range in the state tree.
 * The range [start, end] is inclusive.
 * The tree lock is taken by this function
 */
static void wait_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
			    u32 bits)
{
	struct extent_state *state;
	struct rb_node *node;

	btrfs_debug_check_extent_io_range(tree, start, end);

	spin_lock(&tree->lock);
again:
	while (1) {
		/*
		 * this search will find all the extents that end after
		 * our range starts
		 */
		node = tree_search(tree, start);
process_node:
		if (!node)
			break;

		state = rb_entry(node, struct extent_state, rb_node);

		if (state->start > end)
			goto out;

		if (state->state & bits) {
			start = state->start;
			refcount_inc(&state->refs);
			wait_on_state(tree, state);
			free_extent_state(state);
			goto again;
		}
		start = state->end + 1;

		if (start > end)
			break;

		if (!cond_resched_lock(&tree->lock)) {
			node = rb_next(node);
			goto process_node;
		}
	}
out:
	spin_unlock(&tree->lock);
}

static void set_state_bits(struct extent_io_tree *tree,
			   struct extent_state *state,
			   u32 *bits, struct extent_changeset *changeset)
{
	u32 bits_to_set = *bits & ~EXTENT_CTLBITS;
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
	return cache_state_if_flags(state, cached_ptr,
				    EXTENT_LOCKED | EXTENT_BOUNDARY);
}

/*
 * set some bits on a range in the tree.  This may require allocations or
 * sleeping, so the gfp mask is used to indicate what is allowed.
 *
 * If any of the exclusive bits are set, this will fail with -EEXIST if some
 * part of the range already has the desired bits set.  The start of the
 * existing range is returned in failed_start in this case.
 *
 * [start, end] is inclusive This takes the tree lock.
 */
int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end, u32 bits,
		   u32 exclusive_bits, u64 *failed_start,
		   struct extent_state **cached_state, gfp_t mask,
		   struct extent_changeset *changeset)
{
	struct extent_state *state;
	struct extent_state *prealloc = NULL;
	struct rb_node *node;
	struct rb_node **p;
	struct rb_node *parent;
	int err = 0;
	u64 last_start;
	u64 last_end;

	btrfs_debug_check_extent_io_range(tree, start, end);
	trace_btrfs_set_extent_bit(tree, start, end - start + 1, bits);

	if (exclusive_bits)
		ASSERT(failed_start);
	else
		ASSERT(failed_start == NULL);
again:
	if (!prealloc && gfpflags_allow_blocking(mask)) {
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
	if (cached_state && *cached_state) {
		state = *cached_state;
		if (state->start <= start && state->end > start &&
		    extent_state_in_tree(state)) {
			node = &state->rb_node;
			goto hit_next;
		}
	}
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search_for_insert(tree, start, &p, &parent);
	if (!node) {
		prealloc = alloc_extent_state_atomic(prealloc);
		BUG_ON(!prealloc);
		err = insert_state(tree, prealloc, start, end,
				   &p, &parent, &bits, changeset);
		if (err)
			extent_io_tree_panic(tree, err);

		cache_state(prealloc, cached_state);
		prealloc = NULL;
		goto out;
	}
	state = rb_entry(node, struct extent_state, rb_node);
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
			err = -EEXIST;
			goto out;
		}

		set_state_bits(tree, state, &bits, changeset);
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
	 * We need to split the extent we found, and may flip bits on
	 * second half.
	 *
	 * If the extent we found extends past our
	 * range, we just split and search again.  It'll get split
	 * again the next time though.
	 *
	 * If the extent we found is inside our range, we set the
	 * desired bit on it.
	 */
	if (state->start < start) {
		if (state->state & exclusive_bits) {
			*failed_start = start;
			err = -EEXIST;
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
		BUG_ON(!prealloc);
		err = split_state(tree, state, prealloc, start);
		if (err)
			extent_io_tree_panic(tree, err);

		prealloc = NULL;
		if (err)
			goto out;
		if (state->end <= end) {
			set_state_bits(tree, state, &bits, changeset);
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
	 * There's a hole, we need to insert something in it and
	 * ignore the extent we found.
	 */
	if (state->start > start) {
		u64 this_end;
		if (end < last_start)
			this_end = end;
		else
			this_end = last_start - 1;

		prealloc = alloc_extent_state_atomic(prealloc);
		BUG_ON(!prealloc);

		/*
		 * Avoid to free 'prealloc' if it can be merged with
		 * the later extent.
		 */
		err = insert_state(tree, prealloc, start, this_end,
				   NULL, NULL, &bits, changeset);
		if (err)
			extent_io_tree_panic(tree, err);

		cache_state(prealloc, cached_state);
		prealloc = NULL;
		start = this_end + 1;
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *                        | state |
	 * We need to split the extent, and set the bit
	 * on the first half
	 */
	if (state->start <= end && state->end > end) {
		if (state->state & exclusive_bits) {
			*failed_start = start;
			err = -EEXIST;
			goto out;
		}

		prealloc = alloc_extent_state_atomic(prealloc);
		BUG_ON(!prealloc);
		err = split_state(tree, state, prealloc, end + 1);
		if (err)
			extent_io_tree_panic(tree, err);

		set_state_bits(tree, prealloc, &bits, changeset);
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

	return err;

}

/**
 * convert_extent_bit - convert all bits in a given range from one bit to
 * 			another
 * @tree:	the io tree to search
 * @start:	the start offset in bytes
 * @end:	the end offset in bytes (inclusive)
 * @bits:	the bits to set in this range
 * @clear_bits:	the bits to clear in this range
 * @cached_state:	state that we're going to cache
 *
 * This will go through and set bits for the given range.  If any states exist
 * already in this range they are set with the given bit and cleared of the
 * clear_bits.  This is only meant to be used by things that are mergeable, ie
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
	struct rb_node *node;
	struct rb_node **p;
	struct rb_node *parent;
	int err = 0;
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
		    extent_state_in_tree(state)) {
			node = &state->rb_node;
			goto hit_next;
		}
	}

	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search_for_insert(tree, start, &p, &parent);
	if (!node) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc) {
			err = -ENOMEM;
			goto out;
		}
		err = insert_state(tree, prealloc, start, end,
				   &p, &parent, &bits, NULL);
		if (err)
			extent_io_tree_panic(tree, err);
		cache_state(prealloc, cached_state);
		prealloc = NULL;
		goto out;
	}
	state = rb_entry(node, struct extent_state, rb_node);
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
		set_state_bits(tree, state, &bits, NULL);
		cache_state(state, cached_state);
		state = clear_state_bit(tree, state, &clear_bits, 0, NULL);
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
	 * We need to split the extent we found, and may flip bits on
	 * second half.
	 *
	 * If the extent we found extends past our
	 * range, we just split and search again.  It'll get split
	 * again the next time though.
	 *
	 * If the extent we found is inside our range, we set the
	 * desired bit on it.
	 */
	if (state->start < start) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc) {
			err = -ENOMEM;
			goto out;
		}
		err = split_state(tree, state, prealloc, start);
		if (err)
			extent_io_tree_panic(tree, err);
		prealloc = NULL;
		if (err)
			goto out;
		if (state->end <= end) {
			set_state_bits(tree, state, &bits, NULL);
			cache_state(state, cached_state);
			state = clear_state_bit(tree, state, &clear_bits, 0,
						NULL);
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
	 * There's a hole, we need to insert something in it and
	 * ignore the extent we found.
	 */
	if (state->start > start) {
		u64 this_end;
		if (end < last_start)
			this_end = end;
		else
			this_end = last_start - 1;

		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc) {
			err = -ENOMEM;
			goto out;
		}

		/*
		 * Avoid to free 'prealloc' if it can be merged with
		 * the later extent.
		 */
		err = insert_state(tree, prealloc, start, this_end,
				   NULL, NULL, &bits, NULL);
		if (err)
			extent_io_tree_panic(tree, err);
		cache_state(prealloc, cached_state);
		prealloc = NULL;
		start = this_end + 1;
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *                        | state |
	 * We need to split the extent, and set the bit
	 * on the first half
	 */
	if (state->start <= end && state->end > end) {
		prealloc = alloc_extent_state_atomic(prealloc);
		if (!prealloc) {
			err = -ENOMEM;
			goto out;
		}

		err = split_state(tree, state, prealloc, end + 1);
		if (err)
			extent_io_tree_panic(tree, err);

		set_state_bits(tree, prealloc, &bits, NULL);
		cache_state(prealloc, cached_state);
		clear_state_bit(tree, prealloc, &clear_bits, 0, NULL);
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

	return err;
}

/* wrappers around set/clear extent bit */
int set_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			   u32 bits, struct extent_changeset *changeset)
{
	/*
	 * We don't support EXTENT_LOCKED yet, as current changeset will
	 * record any bits changed, so for EXTENT_LOCKED case, it will
	 * either fail with -EEXIST or changeset will record the whole
	 * range.
	 */
	BUG_ON(bits & EXTENT_LOCKED);

	return set_extent_bit(tree, start, end, bits, 0, NULL, NULL, GFP_NOFS,
			      changeset);
}

int set_extent_bits_nowait(struct extent_io_tree *tree, u64 start, u64 end,
			   u32 bits)
{
	return set_extent_bit(tree, start, end, bits, 0, NULL, NULL,
			      GFP_NOWAIT, NULL);
}

int clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     u32 bits, int wake, int delete,
		     struct extent_state **cached)
{
	return __clear_extent_bit(tree, start, end, bits, wake, delete,
				  cached, GFP_NOFS, NULL);
}

int clear_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		u32 bits, struct extent_changeset *changeset)
{
	/*
	 * Don't support EXTENT_LOCKED case, same reason as
	 * set_record_extent_bits().
	 */
	BUG_ON(bits & EXTENT_LOCKED);

	return __clear_extent_bit(tree, start, end, bits, 0, 0, NULL, GFP_NOFS,
				  changeset);
}

/*
 * either insert or lock state struct between start and end use mask to tell
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

void extent_range_clear_dirty_for_io(struct inode *inode, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(inode->i_mapping, index);
		BUG_ON(!page); /* Pages should be in the extent_io_tree */
		clear_page_dirty_for_io(page);
		put_page(page);
		index++;
	}
}

void extent_range_redirty_for_io(struct inode *inode, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(inode->i_mapping, index);
		BUG_ON(!page); /* Pages should be in the extent_io_tree */
		__set_page_dirty_nobuffers(page);
		account_page_redirty(page);
		put_page(page);
		index++;
	}
}

/* find the first state struct with 'bits' set after 'start', and
 * return it.  tree->lock must be held.  NULL will returned if
 * nothing was found after 'start'
 */
static struct extent_state *
find_first_extent_bit_state(struct extent_io_tree *tree, u64 start, u32 bits)
{
	struct rb_node *node;
	struct extent_state *state;

	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, start);
	if (!node)
		goto out;

	while (1) {
		state = rb_entry(node, struct extent_state, rb_node);
		if (state->end >= start && (state->state & bits))
			return state;

		node = rb_next(node);
		if (!node)
			break;
	}
out:
	return NULL;
}

/*
 * Find the first offset in the io tree with one or more @bits set.
 *
 * Note: If there are multiple bits set in @bits, any of them will match.
 *
 * Return 0 if we find something, and update @start_ret and @end_ret.
 * Return 1 if we found nothing.
 */
int find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			  u64 *start_ret, u64 *end_ret, u32 bits,
			  struct extent_state **cached_state)
{
	struct extent_state *state;
	int ret = 1;

	spin_lock(&tree->lock);
	if (cached_state && *cached_state) {
		state = *cached_state;
		if (state->end == start - 1 && extent_state_in_tree(state)) {
			while ((state = next_state(state)) != NULL) {
				if (state->state & bits)
					goto got_it;
			}
			free_extent_state(*cached_state);
			*cached_state = NULL;
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
		ret = 0;
	}
out:
	spin_unlock(&tree->lock);
	return ret;
}

/**
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

/**
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
		node = __etree_search(tree, start, &next, &prev, NULL, NULL);
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
				 * have the bits set, so take its start as
				 * the beginning of the desired range
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

/*
 * find a contiguous range of bytes in the file marked as delalloc, not
 * more than 'max_bytes'.  start and end are used to return the range,
 *
 * true is returned if we find something, false if nothing was in the tree
 */
bool btrfs_find_delalloc_range(struct extent_io_tree *tree, u64 *start,
			       u64 *end, u64 max_bytes,
			       struct extent_state **cached_state)
{
	struct rb_node *node;
	struct extent_state *state;
	u64 cur_start = *start;
	bool found = false;
	u64 total_bytes = 0;

	spin_lock(&tree->lock);

	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, cur_start);
	if (!node) {
		*end = (u64)-1;
		goto out;
	}

	while (1) {
		state = rb_entry(node, struct extent_state, rb_node);
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
		node = rb_next(node);
		total_bytes += state->end - state->start + 1;
		if (total_bytes >= max_bytes)
			break;
		if (!node)
			break;
	}
out:
	spin_unlock(&tree->lock);
	return found;
}

/*
 * Process one page for __process_pages_contig().
 *
 * Return >0 if we hit @page == @locked_page.
 * Return 0 if we updated the page status.
 * Return -EGAIN if the we need to try again.
 * (For PAGE_LOCK case but got dirty page or page not belong to mapping)
 */
static int process_one_page(struct btrfs_fs_info *fs_info,
			    struct address_space *mapping,
			    struct page *page, struct page *locked_page,
			    unsigned long page_ops, u64 start, u64 end)
{
	u32 len;

	ASSERT(end + 1 - start != 0 && end + 1 - start < U32_MAX);
	len = end + 1 - start;

	if (page_ops & PAGE_SET_ORDERED)
		btrfs_page_clamp_set_ordered(fs_info, page, start, len);
	if (page_ops & PAGE_SET_ERROR)
		btrfs_page_clamp_set_error(fs_info, page, start, len);
	if (page_ops & PAGE_START_WRITEBACK) {
		btrfs_page_clamp_clear_dirty(fs_info, page, start, len);
		btrfs_page_clamp_set_writeback(fs_info, page, start, len);
	}
	if (page_ops & PAGE_END_WRITEBACK)
		btrfs_page_clamp_clear_writeback(fs_info, page, start, len);

	if (page == locked_page)
		return 1;

	if (page_ops & PAGE_LOCK) {
		int ret;

		ret = btrfs_page_start_writer_lock(fs_info, page, start, len);
		if (ret)
			return ret;
		if (!PageDirty(page) || page->mapping != mapping) {
			btrfs_page_end_writer_lock(fs_info, page, start, len);
			return -EAGAIN;
		}
	}
	if (page_ops & PAGE_UNLOCK)
		btrfs_page_end_writer_lock(fs_info, page, start, len);
	return 0;
}

static int __process_pages_contig(struct address_space *mapping,
				  struct page *locked_page,
				  u64 start, u64 end, unsigned long page_ops,
				  u64 *processed_end)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(mapping->host->i_sb);
	pgoff_t start_index = start >> PAGE_SHIFT;
	pgoff_t end_index = end >> PAGE_SHIFT;
	pgoff_t index = start_index;
	unsigned long nr_pages = end_index - start_index + 1;
	unsigned long pages_processed = 0;
	struct page *pages[16];
	int err = 0;
	int i;

	if (page_ops & PAGE_LOCK) {
		ASSERT(page_ops == PAGE_LOCK);
		ASSERT(processed_end && *processed_end == start);
	}

	if ((page_ops & PAGE_SET_ERROR) && nr_pages > 0)
		mapping_set_error(mapping, -EIO);

	while (nr_pages > 0) {
		int found_pages;

		found_pages = find_get_pages_contig(mapping, index,
				     min_t(unsigned long,
				     nr_pages, ARRAY_SIZE(pages)), pages);
		if (found_pages == 0) {
			/*
			 * Only if we're going to lock these pages, we can find
			 * nothing at @index.
			 */
			ASSERT(page_ops & PAGE_LOCK);
			err = -EAGAIN;
			goto out;
		}

		for (i = 0; i < found_pages; i++) {
			int process_ret;

			process_ret = process_one_page(fs_info, mapping,
					pages[i], locked_page, page_ops,
					start, end);
			if (process_ret < 0) {
				for (; i < found_pages; i++)
					put_page(pages[i]);
				err = -EAGAIN;
				goto out;
			}
			put_page(pages[i]);
			pages_processed++;
		}
		nr_pages -= found_pages;
		index += found_pages;
		cond_resched();
	}
out:
	if (err && processed_end) {
		/*
		 * Update @processed_end. I know this is awful since it has
		 * two different return value patterns (inclusive vs exclusive).
		 *
		 * But the exclusive pattern is necessary if @start is 0, or we
		 * underflow and check against processed_end won't work as
		 * expected.
		 */
		if (pages_processed)
			*processed_end = min(end,
			((u64)(start_index + pages_processed) << PAGE_SHIFT) - 1);
		else
			*processed_end = start;
	}
	return err;
}

static noinline void __unlock_for_delalloc(struct inode *inode,
					   struct page *locked_page,
					   u64 start, u64 end)
{
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;

	ASSERT(locked_page);
	if (index == locked_page->index && end_index == index)
		return;

	__process_pages_contig(inode->i_mapping, locked_page, start, end,
			       PAGE_UNLOCK, NULL);
}

static noinline int lock_delalloc_pages(struct inode *inode,
					struct page *locked_page,
					u64 delalloc_start,
					u64 delalloc_end)
{
	unsigned long index = delalloc_start >> PAGE_SHIFT;
	unsigned long end_index = delalloc_end >> PAGE_SHIFT;
	u64 processed_end = delalloc_start;
	int ret;

	ASSERT(locked_page);
	if (index == locked_page->index && index == end_index)
		return 0;

	ret = __process_pages_contig(inode->i_mapping, locked_page, delalloc_start,
				     delalloc_end, PAGE_LOCK, &processed_end);
	if (ret == -EAGAIN && processed_end > delalloc_start)
		__unlock_for_delalloc(inode, locked_page, delalloc_start,
				      processed_end);
	return ret;
}

/*
 * Find and lock a contiguous range of bytes in the file marked as delalloc, no
 * more than @max_bytes.
 *
 * @start:	The original start bytenr to search.
 *		Will store the extent range start bytenr.
 * @end:	The original end bytenr of the search range
 *		Will store the extent range end bytenr.
 *
 * Return true if we find a delalloc range which starts inside the original
 * range, and @start/@end will store the delalloc range start/end.
 *
 * Return false if we can't find any delalloc range which starts inside the
 * original range, and @start/@end will be the non-delalloc range start/end.
 */
EXPORT_FOR_TESTS
noinline_for_stack bool find_lock_delalloc_range(struct inode *inode,
				    struct page *locked_page, u64 *start,
				    u64 *end)
{
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;
	const u64 orig_start = *start;
	const u64 orig_end = *end;
	u64 max_bytes = BTRFS_MAX_EXTENT_SIZE;
	u64 delalloc_start;
	u64 delalloc_end;
	bool found;
	struct extent_state *cached_state = NULL;
	int ret;
	int loops = 0;

	/* Caller should pass a valid @end to indicate the search range end */
	ASSERT(orig_end > orig_start);

	/* The range should at least cover part of the page */
	ASSERT(!(orig_start >= page_offset(locked_page) + PAGE_SIZE ||
		 orig_end <= page_offset(locked_page)));
again:
	/* step one, find a bunch of delalloc bytes starting at start */
	delalloc_start = *start;
	delalloc_end = 0;
	found = btrfs_find_delalloc_range(tree, &delalloc_start, &delalloc_end,
					  max_bytes, &cached_state);
	if (!found || delalloc_end <= *start || delalloc_start > orig_end) {
		*start = delalloc_start;

		/* @delalloc_end can be -1, never go beyond @orig_end */
		*end = min(delalloc_end, orig_end);
		free_extent_state(cached_state);
		return false;
	}

	/*
	 * start comes from the offset of locked_page.  We have to lock
	 * pages in order, so we can't process delalloc bytes before
	 * locked_page
	 */
	if (delalloc_start < *start)
		delalloc_start = *start;

	/*
	 * make sure to limit the number of pages we try to lock down
	 */
	if (delalloc_end + 1 - delalloc_start > max_bytes)
		delalloc_end = delalloc_start + max_bytes - 1;

	/* step two, lock all the pages after the page that has start */
	ret = lock_delalloc_pages(inode, locked_page,
				  delalloc_start, delalloc_end);
	ASSERT(!ret || ret == -EAGAIN);
	if (ret == -EAGAIN) {
		/* some of the pages are gone, lets avoid looping by
		 * shortening the size of the delalloc range we're searching
		 */
		free_extent_state(cached_state);
		cached_state = NULL;
		if (!loops) {
			max_bytes = PAGE_SIZE;
			loops = 1;
			goto again;
		} else {
			found = false;
			goto out_failed;
		}
	}

	/* step three, lock the state bits for the whole range */
	lock_extent_bits(tree, delalloc_start, delalloc_end, &cached_state);

	/* then test to make sure it is all still delalloc */
	ret = test_range_bit(tree, delalloc_start, delalloc_end,
			     EXTENT_DELALLOC, 1, cached_state);
	if (!ret) {
		unlock_extent_cached(tree, delalloc_start, delalloc_end,
				     &cached_state);
		__unlock_for_delalloc(inode, locked_page,
			      delalloc_start, delalloc_end);
		cond_resched();
		goto again;
	}
	free_extent_state(cached_state);
	*start = delalloc_start;
	*end = delalloc_end;
out_failed:
	return found;
}

void extent_clear_unlock_delalloc(struct btrfs_inode *inode, u64 start, u64 end,
				  struct page *locked_page,
				  u32 clear_bits, unsigned long page_ops)
{
	clear_extent_bit(&inode->io_tree, start, end, clear_bits, 1, 0, NULL);

	__process_pages_contig(inode->vfs_inode.i_mapping, locked_page,
			       start, end, page_ops, NULL);
}

/*
 * count the number of bytes in the tree that have a given bit(s)
 * set.  This can be fairly slow, except for EXTENT_DIRTY which is
 * cached.  The total number found is returned.
 */
u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end, u64 max_bytes,
		     u32 bits, int contig)
{
	struct rb_node *node;
	struct extent_state *state;
	u64 cur_start = *start;
	u64 total_bytes = 0;
	u64 last = 0;
	int found = 0;

	if (WARN_ON(search_end <= cur_start))
		return 0;

	spin_lock(&tree->lock);
	if (cur_start == 0 && bits == EXTENT_DIRTY) {
		total_bytes = tree->dirty_bytes;
		goto out;
	}
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, cur_start);
	if (!node)
		goto out;

	while (1) {
		state = rb_entry(node, struct extent_state, rb_node);
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
		node = rb_next(node);
		if (!node)
			break;
	}
out:
	spin_unlock(&tree->lock);
	return total_bytes;
}

/*
 * set the private field for a given byte offset in the tree.  If there isn't
 * an extent_state there already, this does nothing.
 */
int set_state_failrec(struct extent_io_tree *tree, u64 start,
		      struct io_failure_record *failrec)
{
	struct rb_node *node;
	struct extent_state *state;
	int ret = 0;

	spin_lock(&tree->lock);
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, start);
	if (!node) {
		ret = -ENOENT;
		goto out;
	}
	state = rb_entry(node, struct extent_state, rb_node);
	if (state->start != start) {
		ret = -ENOENT;
		goto out;
	}
	state->failrec = failrec;
out:
	spin_unlock(&tree->lock);
	return ret;
}

struct io_failure_record *get_state_failrec(struct extent_io_tree *tree, u64 start)
{
	struct rb_node *node;
	struct extent_state *state;
	struct io_failure_record *failrec;

	spin_lock(&tree->lock);
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, start);
	if (!node) {
		failrec = ERR_PTR(-ENOENT);
		goto out;
	}
	state = rb_entry(node, struct extent_state, rb_node);
	if (state->start != start) {
		failrec = ERR_PTR(-ENOENT);
		goto out;
	}

	failrec = state->failrec;
out:
	spin_unlock(&tree->lock);
	return failrec;
}

/*
 * searches a range in the state tree for a given mask.
 * If 'filled' == 1, this returns 1 only if every extent in the tree
 * has the bits set.  Otherwise, 1 is returned if any bit in the
 * range is found set.
 */
int test_range_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   u32 bits, int filled, struct extent_state *cached)
{
	struct extent_state *state = NULL;
	struct rb_node *node;
	int bitset = 0;

	spin_lock(&tree->lock);
	if (cached && extent_state_in_tree(cached) && cached->start <= start &&
	    cached->end > start)
		node = &cached->rb_node;
	else
		node = tree_search(tree, start);
	while (node && start <= end) {
		state = rb_entry(node, struct extent_state, rb_node);

		if (filled && state->start > start) {
			bitset = 0;
			break;
		}

		if (state->start > end)
			break;

		if (state->state & bits) {
			bitset = 1;
			if (!filled)
				break;
		} else if (filled) {
			bitset = 0;
			break;
		}

		if (state->end == (u64)-1)
			break;

		start = state->end + 1;
		if (start > end)
			break;
		node = rb_next(node);
		if (!node) {
			if (filled)
				bitset = 0;
			break;
		}
	}
	spin_unlock(&tree->lock);
	return bitset;
}

int free_io_failure(struct extent_io_tree *failure_tree,
		    struct extent_io_tree *io_tree,
		    struct io_failure_record *rec)
{
	int ret;
	int err = 0;

	set_state_failrec(failure_tree, rec->start, NULL);
	ret = clear_extent_bits(failure_tree, rec->start,
				rec->start + rec->len - 1,
				EXTENT_LOCKED | EXTENT_DIRTY);
	if (ret)
		err = ret;

	ret = clear_extent_bits(io_tree, rec->start,
				rec->start + rec->len - 1,
				EXTENT_DAMAGED);
	if (ret && !err)
		err = ret;

	kfree(rec);
	return err;
}

/*
 * this bypasses the standard btrfs submit functions deliberately, as
 * the standard behavior is to write all copies in a raid setup. here we only
 * want to write the one bad copy. so we do the mapping for ourselves and issue
 * submit_bio directly.
 * to avoid any synchronization issues, wait for the data after writing, which
 * actually prevents the read that triggered the error from finishing.
 * currently, there can be no more than two copies of every data bit. thus,
 * exactly one rewrite is required.
 */
static int repair_io_failure(struct btrfs_fs_info *fs_info, u64 ino, u64 start,
			     u64 length, u64 logical, struct page *page,
			     unsigned int pg_offset, int mirror_num)
{
	struct bio *bio;
	struct btrfs_device *dev;
	u64 map_length = 0;
	u64 sector;
	struct btrfs_io_context *bioc = NULL;
	int ret;

	ASSERT(!(fs_info->sb->s_flags & SB_RDONLY));
	BUG_ON(!mirror_num);

	if (btrfs_is_zoned(fs_info))
		return btrfs_repair_one_zone(fs_info, logical);

	bio = btrfs_bio_alloc(1);
	bio->bi_iter.bi_size = 0;
	map_length = length;

	/*
	 * Avoid races with device replace and make sure our bioc has devices
	 * associated to its stripes that don't go away while we are doing the
	 * read repair operation.
	 */
	btrfs_bio_counter_inc_blocked(fs_info);
	if (btrfs_is_parity_mirror(fs_info, logical, length)) {
		/*
		 * Note that we don't use BTRFS_MAP_WRITE because it's supposed
		 * to update all raid stripes, but here we just want to correct
		 * bad stripe, thus BTRFS_MAP_READ is abused to only get the bad
		 * stripe's dev and sector.
		 */
		ret = btrfs_map_block(fs_info, BTRFS_MAP_READ, logical,
				      &map_length, &bioc, 0);
		if (ret) {
			btrfs_bio_counter_dec(fs_info);
			bio_put(bio);
			return -EIO;
		}
		ASSERT(bioc->mirror_num == 1);
	} else {
		ret = btrfs_map_block(fs_info, BTRFS_MAP_WRITE, logical,
				      &map_length, &bioc, mirror_num);
		if (ret) {
			btrfs_bio_counter_dec(fs_info);
			bio_put(bio);
			return -EIO;
		}
		BUG_ON(mirror_num != bioc->mirror_num);
	}

	sector = bioc->stripes[bioc->mirror_num - 1].physical >> 9;
	bio->bi_iter.bi_sector = sector;
	dev = bioc->stripes[bioc->mirror_num - 1].dev;
	btrfs_put_bioc(bioc);
	if (!dev || !dev->bdev ||
	    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state)) {
		btrfs_bio_counter_dec(fs_info);
		bio_put(bio);
		return -EIO;
	}
	bio_set_dev(bio, dev->bdev);
	bio->bi_opf = REQ_OP_WRITE | REQ_SYNC;
	bio_add_page(bio, page, length, pg_offset);

	if (btrfsic_submit_bio_wait(bio)) {
		/* try to remap that extent elsewhere? */
		btrfs_bio_counter_dec(fs_info);
		bio_put(bio);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_WRITE_ERRS);
		return -EIO;
	}

	btrfs_info_rl_in_rcu(fs_info,
		"read error corrected: ino %llu off %llu (dev %s sector %llu)",
				  ino, start,
				  rcu_str_deref(dev->name), sector);
	btrfs_bio_counter_dec(fs_info);
	bio_put(bio);
	return 0;
}

int btrfs_repair_eb_io_failure(const struct extent_buffer *eb, int mirror_num)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	u64 start = eb->start;
	int i, num_pages = num_extent_pages(eb);
	int ret = 0;

	if (sb_rdonly(fs_info->sb))
		return -EROFS;

	for (i = 0; i < num_pages; i++) {
		struct page *p = eb->pages[i];

		ret = repair_io_failure(fs_info, 0, start, PAGE_SIZE, start, p,
					start - page_offset(p), mirror_num);
		if (ret)
			break;
		start += PAGE_SIZE;
	}

	return ret;
}

/*
 * each time an IO finishes, we do a fast check in the IO failure tree
 * to see if we need to process or clean up an io_failure_record
 */
int clean_io_failure(struct btrfs_fs_info *fs_info,
		     struct extent_io_tree *failure_tree,
		     struct extent_io_tree *io_tree, u64 start,
		     struct page *page, u64 ino, unsigned int pg_offset)
{
	u64 private;
	struct io_failure_record *failrec;
	struct extent_state *state;
	int num_copies;
	int ret;

	private = 0;
	ret = count_range_bits(failure_tree, &private, (u64)-1, 1,
			       EXTENT_DIRTY, 0);
	if (!ret)
		return 0;

	failrec = get_state_failrec(failure_tree, start);
	if (IS_ERR(failrec))
		return 0;

	BUG_ON(!failrec->this_mirror);

	if (sb_rdonly(fs_info->sb))
		goto out;

	spin_lock(&io_tree->lock);
	state = find_first_extent_bit_state(io_tree,
					    failrec->start,
					    EXTENT_LOCKED);
	spin_unlock(&io_tree->lock);

	if (state && state->start <= failrec->start &&
	    state->end >= failrec->start + failrec->len - 1) {
		num_copies = btrfs_num_copies(fs_info, failrec->logical,
					      failrec->len);
		if (num_copies > 1)  {
			repair_io_failure(fs_info, ino, start, failrec->len,
					  failrec->logical, page, pg_offset,
					  failrec->failed_mirror);
		}
	}

out:
	free_io_failure(failure_tree, io_tree, failrec);

	return 0;
}

/*
 * Can be called when
 * - hold extent lock
 * - under ordered extent
 * - the inode is freeing
 */
void btrfs_free_io_failure_record(struct btrfs_inode *inode, u64 start, u64 end)
{
	struct extent_io_tree *failure_tree = &inode->io_failure_tree;
	struct io_failure_record *failrec;
	struct extent_state *state, *next;

	if (RB_EMPTY_ROOT(&failure_tree->state))
		return;

	spin_lock(&failure_tree->lock);
	state = find_first_extent_bit_state(failure_tree, start, EXTENT_DIRTY);
	while (state) {
		if (state->start > end)
			break;

		ASSERT(state->end <= end);

		next = next_state(state);

		failrec = state->failrec;
		free_extent_state(state);
		kfree(failrec);

		state = next;
	}
	spin_unlock(&failure_tree->lock);
}

static struct io_failure_record *btrfs_get_io_failure_record(struct inode *inode,
							     u64 start)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct io_failure_record *failrec;
	struct extent_map *em;
	struct extent_io_tree *failure_tree = &BTRFS_I(inode)->io_failure_tree;
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	const u32 sectorsize = fs_info->sectorsize;
	int ret;
	u64 logical;

	failrec = get_state_failrec(failure_tree, start);
	if (!IS_ERR(failrec)) {
		btrfs_debug(fs_info,
	"Get IO Failure Record: (found) logical=%llu, start=%llu, len=%llu",
			failrec->logical, failrec->start, failrec->len);
		/*
		 * when data can be on disk more than twice, add to failrec here
		 * (e.g. with a list for failed_mirror) to make
		 * clean_io_failure() clean all those errors at once.
		 */

		return failrec;
	}

	failrec = kzalloc(sizeof(*failrec), GFP_NOFS);
	if (!failrec)
		return ERR_PTR(-ENOMEM);

	failrec->start = start;
	failrec->len = sectorsize;
	failrec->this_mirror = 0;
	failrec->bio_flags = 0;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, failrec->len);
	if (!em) {
		read_unlock(&em_tree->lock);
		kfree(failrec);
		return ERR_PTR(-EIO);
	}

	if (em->start > start || em->start + em->len <= start) {
		free_extent_map(em);
		em = NULL;
	}
	read_unlock(&em_tree->lock);
	if (!em) {
		kfree(failrec);
		return ERR_PTR(-EIO);
	}

	logical = start - em->start;
	logical = em->block_start + logical;
	if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
		logical = em->block_start;
		failrec->bio_flags = EXTENT_BIO_COMPRESSED;
		extent_set_compress_type(&failrec->bio_flags, em->compress_type);
	}

	btrfs_debug(fs_info,
		    "Get IO Failure Record: (new) logical=%llu, start=%llu, len=%llu",
		    logical, start, failrec->len);

	failrec->logical = logical;
	free_extent_map(em);

	/* Set the bits in the private failure tree */
	ret = set_extent_bits(failure_tree, start, start + sectorsize - 1,
			      EXTENT_LOCKED | EXTENT_DIRTY);
	if (ret >= 0) {
		ret = set_state_failrec(failure_tree, start, failrec);
		/* Set the bits in the inode's tree */
		ret = set_extent_bits(tree, start, start + sectorsize - 1,
				      EXTENT_DAMAGED);
	} else if (ret < 0) {
		kfree(failrec);
		return ERR_PTR(ret);
	}

	return failrec;
}

static bool btrfs_check_repairable(struct inode *inode,
				   struct io_failure_record *failrec,
				   int failed_mirror)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int num_copies;

	num_copies = btrfs_num_copies(fs_info, failrec->logical, failrec->len);
	if (num_copies == 1) {
		/*
		 * we only have a single copy of the data, so don't bother with
		 * all the retry and error correction code that follows. no
		 * matter what the error is, it is very likely to persist.
		 */
		btrfs_debug(fs_info,
			"Check Repairable: cannot repair, num_copies=%d, next_mirror %d, failed_mirror %d",
			num_copies, failrec->this_mirror, failed_mirror);
		return false;
	}

	/* The failure record should only contain one sector */
	ASSERT(failrec->len == fs_info->sectorsize);

	/*
	 * There are two premises:
	 * a) deliver good data to the caller
	 * b) correct the bad sectors on disk
	 *
	 * Since we're only doing repair for one sector, we only need to get
	 * a good copy of the failed sector and if we succeed, we have setup
	 * everything for repair_io_failure to do the rest for us.
	 */
	failrec->failed_mirror = failed_mirror;
	failrec->this_mirror++;
	if (failrec->this_mirror == failed_mirror)
		failrec->this_mirror++;

	if (failrec->this_mirror > num_copies) {
		btrfs_debug(fs_info,
			"Check Repairable: (fail) num_copies=%d, next_mirror %d, failed_mirror %d",
			num_copies, failrec->this_mirror, failed_mirror);
		return false;
	}

	return true;
}

int btrfs_repair_one_sector(struct inode *inode,
			    struct bio *failed_bio, u32 bio_offset,
			    struct page *page, unsigned int pgoff,
			    u64 start, int failed_mirror,
			    submit_bio_hook_t *submit_bio_hook)
{
	struct io_failure_record *failrec;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;
	struct extent_io_tree *failure_tree = &BTRFS_I(inode)->io_failure_tree;
	struct btrfs_bio *failed_bbio = btrfs_bio(failed_bio);
	const int icsum = bio_offset >> fs_info->sectorsize_bits;
	struct bio *repair_bio;
	struct btrfs_bio *repair_bbio;
	blk_status_t status;

	btrfs_debug(fs_info,
		   "repair read error: read error at %llu", start);

	BUG_ON(bio_op(failed_bio) == REQ_OP_WRITE);

	failrec = btrfs_get_io_failure_record(inode, start);
	if (IS_ERR(failrec))
		return PTR_ERR(failrec);


	if (!btrfs_check_repairable(inode, failrec, failed_mirror)) {
		free_io_failure(failure_tree, tree, failrec);
		return -EIO;
	}

	repair_bio = btrfs_bio_alloc(1);
	repair_bbio = btrfs_bio(repair_bio);
	repair_bio->bi_opf = REQ_OP_READ;
	repair_bio->bi_end_io = failed_bio->bi_end_io;
	repair_bio->bi_iter.bi_sector = failrec->logical >> 9;
	repair_bio->bi_private = failed_bio->bi_private;

	if (failed_bbio->csum) {
		const u32 csum_size = fs_info->csum_size;

		repair_bbio->csum = repair_bbio->csum_inline;
		memcpy(repair_bbio->csum,
		       failed_bbio->csum + csum_size * icsum, csum_size);
	}

	bio_add_page(repair_bio, page, failrec->len, pgoff);
	repair_bbio->iter = repair_bio->bi_iter;

	btrfs_debug(btrfs_sb(inode->i_sb),
		    "repair read error: submitting new read to mirror %d",
		    failrec->this_mirror);

	status = submit_bio_hook(inode, repair_bio, failrec->this_mirror,
				 failrec->bio_flags);
	if (status) {
		free_io_failure(failure_tree, tree, failrec);
		bio_put(repair_bio);
	}
	return blk_status_to_errno(status);
}

static void end_page_read(struct page *page, bool uptodate, u64 start, u32 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(page->mapping->host->i_sb);

	ASSERT(page_offset(page) <= start &&
	       start + len <= page_offset(page) + PAGE_SIZE);

	if (uptodate) {
		if (fsverity_active(page->mapping->host) &&
		    !PageError(page) &&
		    !PageUptodate(page) &&
		    start < i_size_read(page->mapping->host) &&
		    !fsverity_verify_page(page)) {
			btrfs_page_set_error(fs_info, page, start, len);
		} else {
			btrfs_page_set_uptodate(fs_info, page, start, len);
		}
	} else {
		btrfs_page_clear_uptodate(fs_info, page, start, len);
		btrfs_page_set_error(fs_info, page, start, len);
	}

	if (fs_info->sectorsize == PAGE_SIZE)
		unlock_page(page);
	else
		btrfs_subpage_end_reader(fs_info, page, start, len);
}

static blk_status_t submit_read_repair(struct inode *inode,
				      struct bio *failed_bio, u32 bio_offset,
				      struct page *page, unsigned int pgoff,
				      u64 start, u64 end, int failed_mirror,
				      unsigned int error_bitmap,
				      submit_bio_hook_t *submit_bio_hook)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	const u32 sectorsize = fs_info->sectorsize;
	const int nr_bits = (end + 1 - start) >> fs_info->sectorsize_bits;
	int error = 0;
	int i;

	BUG_ON(bio_op(failed_bio) == REQ_OP_WRITE);

	/* We're here because we had some read errors or csum mismatch */
	ASSERT(error_bitmap);

	/*
	 * We only get called on buffered IO, thus page must be mapped and bio
	 * must not be cloned.
	 */
	ASSERT(page->mapping && !bio_flagged(failed_bio, BIO_CLONED));

	/* Iterate through all the sectors in the range */
	for (i = 0; i < nr_bits; i++) {
		const unsigned int offset = i * sectorsize;
		struct extent_state *cached = NULL;
		bool uptodate = false;
		int ret;

		if (!(error_bitmap & (1U << i))) {
			/*
			 * This sector has no error, just end the page read
			 * and unlock the range.
			 */
			uptodate = true;
			goto next;
		}

		ret = btrfs_repair_one_sector(inode, failed_bio,
				bio_offset + offset,
				page, pgoff + offset, start + offset,
				failed_mirror, submit_bio_hook);
		if (!ret) {
			/*
			 * We have submitted the read repair, the page release
			 * will be handled by the endio function of the
			 * submitted repair bio.
			 * Thus we don't need to do any thing here.
			 */
			continue;
		}
		/*
		 * Repair failed, just record the error but still continue.
		 * Or the remaining sectors will not be properly unlocked.
		 */
		if (!error)
			error = ret;
next:
		end_page_read(page, uptodate, start + offset, sectorsize);
		if (uptodate)
			set_extent_uptodate(&BTRFS_I(inode)->io_tree,
					start + offset,
					start + offset + sectorsize - 1,
					&cached, GFP_ATOMIC);
		unlock_extent_cached_atomic(&BTRFS_I(inode)->io_tree,
				start + offset,
				start + offset + sectorsize - 1,
				&cached);
	}
	return errno_to_blk_status(error);
}

/* lots and lots of room for performance fixes in the end_bio funcs */

void end_extent_writepage(struct page *page, int err, u64 start, u64 end)
{
	struct btrfs_inode *inode;
	const bool uptodate = (err == 0);
	int ret = 0;

	ASSERT(page && page->mapping);
	inode = BTRFS_I(page->mapping->host);
	btrfs_writepage_endio_finish_ordered(inode, page, start, end, uptodate);

	if (!uptodate) {
		const struct btrfs_fs_info *fs_info = inode->root->fs_info;
		u32 len;

		ASSERT(end + 1 - start <= U32_MAX);
		len = end + 1 - start;

		btrfs_page_clear_uptodate(fs_info, page, start, len);
		btrfs_page_set_error(fs_info, page, start, len);
		ret = err < 0 ? err : -EIO;
		mapping_set_error(page->mapping, ret);
	}
}

/*
 * after a writepage IO is done, we need to:
 * clear the uptodate bits on error
 * clear the writeback bits in the extent tree for this IO
 * end_page_writeback if the page has no more pending IO
 *
 * Scheduling is not allowed, so the extent state tree is expected
 * to have one and only one object corresponding to this IO.
 */
static void end_bio_extent_writepage(struct bio *bio)
{
	int error = blk_status_to_errno(bio->bi_status);
	struct bio_vec *bvec;
	u64 start;
	u64 end;
	struct bvec_iter_all iter_all;
	bool first_bvec = true;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *page = bvec->bv_page;
		struct inode *inode = page->mapping->host;
		struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
		const u32 sectorsize = fs_info->sectorsize;

		/* Our read/write should always be sector aligned. */
		if (!IS_ALIGNED(bvec->bv_offset, sectorsize))
			btrfs_err(fs_info,
		"partial page write in btrfs with offset %u and length %u",
				  bvec->bv_offset, bvec->bv_len);
		else if (!IS_ALIGNED(bvec->bv_len, sectorsize))
			btrfs_info(fs_info,
		"incomplete page write with offset %u and length %u",
				   bvec->bv_offset, bvec->bv_len);

		start = page_offset(page) + bvec->bv_offset;
		end = start + bvec->bv_len - 1;

		if (first_bvec) {
			btrfs_record_physical_zoned(inode, start, bio);
			first_bvec = false;
		}

		end_extent_writepage(page, error, start, end);

		btrfs_page_clear_writeback(fs_info, page, start, bvec->bv_len);
	}

	bio_put(bio);
}

/*
 * Record previously processed extent range
 *
 * For endio_readpage_release_extent() to handle a full extent range, reducing
 * the extent io operations.
 */
struct processed_extent {
	struct btrfs_inode *inode;
	/* Start of the range in @inode */
	u64 start;
	/* End of the range in @inode */
	u64 end;
	bool uptodate;
};

/*
 * Try to release processed extent range
 *
 * May not release the extent range right now if the current range is
 * contiguous to processed extent.
 *
 * Will release processed extent when any of @inode, @uptodate, the range is
 * no longer contiguous to the processed range.
 *
 * Passing @inode == NULL will force processed extent to be released.
 */
static void endio_readpage_release_extent(struct processed_extent *processed,
			      struct btrfs_inode *inode, u64 start, u64 end,
			      bool uptodate)
{
	struct extent_state *cached = NULL;
	struct extent_io_tree *tree;

	/* The first extent, initialize @processed */
	if (!processed->inode)
		goto update;

	/*
	 * Contiguous to processed extent, just uptodate the end.
	 *
	 * Several things to notice:
	 *
	 * - bio can be merged as long as on-disk bytenr is contiguous
	 *   This means we can have page belonging to other inodes, thus need to
	 *   check if the inode still matches.
	 * - bvec can contain range beyond current page for multi-page bvec
	 *   Thus we need to do processed->end + 1 >= start check
	 */
	if (processed->inode == inode && processed->uptodate == uptodate &&
	    processed->end + 1 >= start && end >= processed->end) {
		processed->end = end;
		return;
	}

	tree = &processed->inode->io_tree;
	/*
	 * Now we don't have range contiguous to the processed range, release
	 * the processed range now.
	 */
	if (processed->uptodate && tree->track_uptodate)
		set_extent_uptodate(tree, processed->start, processed->end,
				    &cached, GFP_ATOMIC);
	unlock_extent_cached_atomic(tree, processed->start, processed->end,
				    &cached);

update:
	/* Update processed to current range */
	processed->inode = inode;
	processed->start = start;
	processed->end = end;
	processed->uptodate = uptodate;
}

static void begin_page_read(struct btrfs_fs_info *fs_info, struct page *page)
{
	ASSERT(PageLocked(page));
	if (fs_info->sectorsize == PAGE_SIZE)
		return;

	ASSERT(PagePrivate(page));
	btrfs_subpage_start_reader(fs_info, page, page_offset(page), PAGE_SIZE);
}

/*
 * Find extent buffer for a givne bytenr.
 *
 * This is for end_bio_extent_readpage(), thus we can't do any unsafe locking
 * in endio context.
 */
static struct extent_buffer *find_extent_buffer_readpage(
		struct btrfs_fs_info *fs_info, struct page *page, u64 bytenr)
{
	struct extent_buffer *eb;

	/*
	 * For regular sectorsize, we can use page->private to grab extent
	 * buffer
	 */
	if (fs_info->sectorsize == PAGE_SIZE) {
		ASSERT(PagePrivate(page) && page->private);
		return (struct extent_buffer *)page->private;
	}

	/* For subpage case, we need to lookup buffer radix tree */
	rcu_read_lock();
	eb = radix_tree_lookup(&fs_info->buffer_radix,
			       bytenr >> fs_info->sectorsize_bits);
	rcu_read_unlock();
	ASSERT(eb);
	return eb;
}

/*
 * after a readpage IO is done, we need to:
 * clear the uptodate bits on error
 * set the uptodate bits if things worked
 * set the page up to date if all extents in the tree are uptodate
 * clear the lock bit in the extent tree
 * unlock the page if there are no other extents locked for it
 *
 * Scheduling is not allowed, so the extent state tree is expected
 * to have one and only one object corresponding to this IO.
 */
static void end_bio_extent_readpage(struct bio *bio)
{
	struct bio_vec *bvec;
	struct btrfs_bio *bbio = btrfs_bio(bio);
	struct extent_io_tree *tree, *failure_tree;
	struct processed_extent processed = { 0 };
	/*
	 * The offset to the beginning of a bio, since one bio can never be
	 * larger than UINT_MAX, u32 here is enough.
	 */
	u32 bio_offset = 0;
	int mirror;
	int ret;
	struct bvec_iter_all iter_all;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_segment_all(bvec, bio, iter_all) {
		bool uptodate = !bio->bi_status;
		struct page *page = bvec->bv_page;
		struct inode *inode = page->mapping->host;
		struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
		const u32 sectorsize = fs_info->sectorsize;
		unsigned int error_bitmap = (unsigned int)-1;
		u64 start;
		u64 end;
		u32 len;

		btrfs_debug(fs_info,
			"end_bio_extent_readpage: bi_sector=%llu, err=%d, mirror=%u",
			bio->bi_iter.bi_sector, bio->bi_status,
			bbio->mirror_num);
		tree = &BTRFS_I(inode)->io_tree;
		failure_tree = &BTRFS_I(inode)->io_failure_tree;

		/*
		 * We always issue full-sector reads, but if some block in a
		 * page fails to read, blk_update_request() will advance
		 * bv_offset and adjust bv_len to compensate.  Print a warning
		 * for unaligned offsets, and an error if they don't add up to
		 * a full sector.
		 */
		if (!IS_ALIGNED(bvec->bv_offset, sectorsize))
			btrfs_err(fs_info,
		"partial page read in btrfs with offset %u and length %u",
				  bvec->bv_offset, bvec->bv_len);
		else if (!IS_ALIGNED(bvec->bv_offset + bvec->bv_len,
				     sectorsize))
			btrfs_info(fs_info,
		"incomplete page read with offset %u and length %u",
				   bvec->bv_offset, bvec->bv_len);

		start = page_offset(page) + bvec->bv_offset;
		end = start + bvec->bv_len - 1;
		len = bvec->bv_len;

		mirror = bbio->mirror_num;
		if (likely(uptodate)) {
			if (is_data_inode(inode)) {
				error_bitmap = btrfs_verify_data_csum(bbio,
						bio_offset, page, start, end);
				ret = error_bitmap;
			} else {
				ret = btrfs_validate_metadata_buffer(bbio,
					page, start, end, mirror);
			}
			if (ret)
				uptodate = false;
			else
				clean_io_failure(BTRFS_I(inode)->root->fs_info,
						 failure_tree, tree, start,
						 page,
						 btrfs_ino(BTRFS_I(inode)), 0);
		}

		if (likely(uptodate))
			goto readpage_ok;

		if (is_data_inode(inode)) {
			/*
			 * btrfs_submit_read_repair() will handle all the good
			 * and bad sectors, we just continue to the next bvec.
			 */
			submit_read_repair(inode, bio, bio_offset, page,
					   start - page_offset(page), start,
					   end, mirror, error_bitmap,
					   btrfs_submit_data_bio);

			ASSERT(bio_offset + len > bio_offset);
			bio_offset += len;
			continue;
		} else {
			struct extent_buffer *eb;

			eb = find_extent_buffer_readpage(fs_info, page, start);
			set_bit(EXTENT_BUFFER_READ_ERR, &eb->bflags);
			eb->read_mirror = mirror;
			atomic_dec(&eb->io_pages);
			if (test_and_clear_bit(EXTENT_BUFFER_READAHEAD,
					       &eb->bflags))
				btree_readahead_hook(eb, -EIO);
		}
readpage_ok:
		if (likely(uptodate)) {
			loff_t i_size = i_size_read(inode);
			pgoff_t end_index = i_size >> PAGE_SHIFT;

			/*
			 * Zero out the remaining part if this range straddles
			 * i_size.
			 *
			 * Here we should only zero the range inside the bvec,
			 * not touch anything else.
			 *
			 * NOTE: i_size is exclusive while end is inclusive.
			 */
			if (page->index == end_index && i_size <= end) {
				u32 zero_start = max(offset_in_page(i_size),
						     offset_in_page(start));

				zero_user_segment(page, zero_start,
						  offset_in_page(end) + 1);
			}
		}
		ASSERT(bio_offset + len > bio_offset);
		bio_offset += len;

		/* Update page status and unlock */
		end_page_read(page, uptodate, start, len);
		endio_readpage_release_extent(&processed, BTRFS_I(inode),
					      start, end, PageUptodate(page));
	}
	/* Release the last extent */
	endio_readpage_release_extent(&processed, NULL, 0, 0, false);
	btrfs_bio_free_csum(bbio);
	bio_put(bio);
}

/*
 * Initialize the members up to but not including 'bio'. Use after allocating a
 * new bio by bio_alloc_bioset as it does not initialize the bytes outside of
 * 'bio' because use of __GFP_ZERO is not supported.
 */
static inline void btrfs_bio_init(struct btrfs_bio *bbio)
{
	memset(bbio, 0, offsetof(struct btrfs_bio, bio));
}

/*
 * Allocate a btrfs_io_bio, with @nr_iovecs as maximum number of iovecs.
 *
 * The bio allocation is backed by bioset and does not fail.
 */
struct bio *btrfs_bio_alloc(unsigned int nr_iovecs)
{
	struct bio *bio;

	ASSERT(0 < nr_iovecs && nr_iovecs <= BIO_MAX_VECS);
	bio = bio_alloc_bioset(GFP_NOFS, nr_iovecs, &btrfs_bioset);
	btrfs_bio_init(btrfs_bio(bio));
	return bio;
}

struct bio *btrfs_bio_clone(struct bio *bio)
{
	struct btrfs_bio *bbio;
	struct bio *new;

	/* Bio allocation backed by a bioset does not fail */
	new = bio_clone_fast(bio, GFP_NOFS, &btrfs_bioset);
	bbio = btrfs_bio(new);
	btrfs_bio_init(bbio);
	bbio->iter = bio->bi_iter;
	return new;
}

struct bio *btrfs_bio_clone_partial(struct bio *orig, u64 offset, u64 size)
{
	struct bio *bio;
	struct btrfs_bio *bbio;

	ASSERT(offset <= UINT_MAX && size <= UINT_MAX);

	/* this will never fail when it's backed by a bioset */
	bio = bio_clone_fast(orig, GFP_NOFS, &btrfs_bioset);
	ASSERT(bio);

	bbio = btrfs_bio(bio);
	btrfs_bio_init(bbio);

	bio_trim(bio, offset >> 9, size >> 9);
	bbio->iter = bio->bi_iter;
	return bio;
}

/**
 * Attempt to add a page to bio
 *
 * @bio:	destination bio
 * @page:	page to add to the bio
 * @disk_bytenr:  offset of the new bio or to check whether we are adding
 *                a contiguous page to the previous one
 * @pg_offset:	starting offset in the page
 * @size:	portion of page that we want to write
 * @prev_bio_flags:  flags of previous bio to see if we can merge the current one
 * @bio_flags:	flags of the current bio to see if we can merge them
 *
 * Attempt to add a page to bio considering stripe alignment etc.
 *
 * Return >= 0 for the number of bytes added to the bio.
 * Can return 0 if the current bio is already at stripe/zone boundary.
 * Return <0 for error.
 */
static int btrfs_bio_add_page(struct btrfs_bio_ctrl *bio_ctrl,
			      struct page *page,
			      u64 disk_bytenr, unsigned int size,
			      unsigned int pg_offset,
			      unsigned long bio_flags)
{
	struct bio *bio = bio_ctrl->bio;
	u32 bio_size = bio->bi_iter.bi_size;
	u32 real_size;
	const sector_t sector = disk_bytenr >> SECTOR_SHIFT;
	bool contig;
	int ret;

	ASSERT(bio);
	/* The limit should be calculated when bio_ctrl->bio is allocated */
	ASSERT(bio_ctrl->len_to_oe_boundary && bio_ctrl->len_to_stripe_boundary);
	if (bio_ctrl->bio_flags != bio_flags)
		return 0;

	if (bio_ctrl->bio_flags & EXTENT_BIO_COMPRESSED)
		contig = bio->bi_iter.bi_sector == sector;
	else
		contig = bio_end_sector(bio) == sector;
	if (!contig)
		return 0;

	real_size = min(bio_ctrl->len_to_oe_boundary,
			bio_ctrl->len_to_stripe_boundary) - bio_size;
	real_size = min(real_size, size);

	/*
	 * If real_size is 0, never call bio_add_*_page(), as even size is 0,
	 * bio will still execute its endio function on the page!
	 */
	if (real_size == 0)
		return 0;

	if (bio_op(bio) == REQ_OP_ZONE_APPEND)
		ret = bio_add_zone_append_page(bio, page, real_size, pg_offset);
	else
		ret = bio_add_page(bio, page, real_size, pg_offset);

	return ret;
}

static int calc_bio_boundaries(struct btrfs_bio_ctrl *bio_ctrl,
			       struct btrfs_inode *inode, u64 file_offset)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_io_geometry geom;
	struct btrfs_ordered_extent *ordered;
	struct extent_map *em;
	u64 logical = (bio_ctrl->bio->bi_iter.bi_sector << SECTOR_SHIFT);
	int ret;

	/*
	 * Pages for compressed extent are never submitted to disk directly,
	 * thus it has no real boundary, just set them to U32_MAX.
	 *
	 * The split happens for real compressed bio, which happens in
	 * btrfs_submit_compressed_read/write().
	 */
	if (bio_ctrl->bio_flags & EXTENT_BIO_COMPRESSED) {
		bio_ctrl->len_to_oe_boundary = U32_MAX;
		bio_ctrl->len_to_stripe_boundary = U32_MAX;
		return 0;
	}
	em = btrfs_get_chunk_map(fs_info, logical, fs_info->sectorsize);
	if (IS_ERR(em))
		return PTR_ERR(em);
	ret = btrfs_get_io_geometry(fs_info, em, btrfs_op(bio_ctrl->bio),
				    logical, &geom);
	free_extent_map(em);
	if (ret < 0) {
		return ret;
	}
	if (geom.len > U32_MAX)
		bio_ctrl->len_to_stripe_boundary = U32_MAX;
	else
		bio_ctrl->len_to_stripe_boundary = (u32)geom.len;

	if (!btrfs_is_zoned(fs_info) ||
	    bio_op(bio_ctrl->bio) != REQ_OP_ZONE_APPEND) {
		bio_ctrl->len_to_oe_boundary = U32_MAX;
		return 0;
	}

	/* Ordered extent not yet created, so we're good */
	ordered = btrfs_lookup_ordered_extent(inode, file_offset);
	if (!ordered) {
		bio_ctrl->len_to_oe_boundary = U32_MAX;
		return 0;
	}

	bio_ctrl->len_to_oe_boundary = min_t(u32, U32_MAX,
		ordered->disk_bytenr + ordered->disk_num_bytes - logical);
	btrfs_put_ordered_extent(ordered);
	return 0;
}

static int alloc_new_bio(struct btrfs_inode *inode,
			 struct btrfs_bio_ctrl *bio_ctrl,
			 struct writeback_control *wbc,
			 unsigned int opf,
			 bio_end_io_t end_io_func,
			 u64 disk_bytenr, u32 offset, u64 file_offset,
			 unsigned long bio_flags)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct bio *bio;
	int ret;

	bio = btrfs_bio_alloc(BIO_MAX_VECS);
	/*
	 * For compressed page range, its disk_bytenr is always @disk_bytenr
	 * passed in, no matter if we have added any range into previous bio.
	 */
	if (bio_flags & EXTENT_BIO_COMPRESSED)
		bio->bi_iter.bi_sector = disk_bytenr >> SECTOR_SHIFT;
	else
		bio->bi_iter.bi_sector = (disk_bytenr + offset) >> SECTOR_SHIFT;
	bio_ctrl->bio = bio;
	bio_ctrl->bio_flags = bio_flags;
	bio->bi_end_io = end_io_func;
	bio->bi_private = &inode->io_tree;
	bio->bi_write_hint = inode->vfs_inode.i_write_hint;
	bio->bi_opf = opf;
	ret = calc_bio_boundaries(bio_ctrl, inode, file_offset);
	if (ret < 0)
		goto error;
	if (wbc) {
		struct block_device *bdev;

		bdev = fs_info->fs_devices->latest_dev->bdev;
		bio_set_dev(bio, bdev);
		wbc_init_bio(wbc, bio);
	}
	if (btrfs_is_zoned(fs_info) && bio_op(bio) == REQ_OP_ZONE_APPEND) {
		struct btrfs_device *device;

		device = btrfs_zoned_get_device(fs_info, disk_bytenr,
						fs_info->sectorsize);
		if (IS_ERR(device)) {
			ret = PTR_ERR(device);
			goto error;
		}

		btrfs_bio(bio)->device = device;
	}
	return 0;
error:
	bio_ctrl->bio = NULL;
	bio->bi_status = errno_to_blk_status(ret);
	bio_endio(bio);
	return ret;
}

/*
 * @opf:	bio REQ_OP_* and REQ_* flags as one value
 * @wbc:	optional writeback control for io accounting
 * @page:	page to add to the bio
 * @disk_bytenr: logical bytenr where the write will be
 * @size:	portion of page that we want to write to
 * @pg_offset:	offset of the new bio or to check whether we are adding
 *              a contiguous page to the previous one
 * @bio_ret:	must be valid pointer, newly allocated bio will be stored there
 * @end_io_func:     end_io callback for new bio
 * @mirror_num:	     desired mirror to read/write
 * @prev_bio_flags:  flags of previous bio to see if we can merge the current one
 * @bio_flags:	flags of the current bio to see if we can merge them
 */
static int submit_extent_page(unsigned int opf,
			      struct writeback_control *wbc,
			      struct btrfs_bio_ctrl *bio_ctrl,
			      struct page *page, u64 disk_bytenr,
			      size_t size, unsigned long pg_offset,
			      bio_end_io_t end_io_func,
			      int mirror_num,
			      unsigned long bio_flags,
			      bool force_bio_submit)
{
	int ret = 0;
	struct btrfs_inode *inode = BTRFS_I(page->mapping->host);
	unsigned int cur = pg_offset;

	ASSERT(bio_ctrl);

	ASSERT(pg_offset < PAGE_SIZE && size <= PAGE_SIZE &&
	       pg_offset + size <= PAGE_SIZE);
	if (force_bio_submit && bio_ctrl->bio) {
		ret = submit_one_bio(bio_ctrl->bio, mirror_num, bio_ctrl->bio_flags);
		bio_ctrl->bio = NULL;
		if (ret < 0)
			return ret;
	}

	while (cur < pg_offset + size) {
		u32 offset = cur - pg_offset;
		int added;

		/* Allocate new bio if needed */
		if (!bio_ctrl->bio) {
			ret = alloc_new_bio(inode, bio_ctrl, wbc, opf,
					    end_io_func, disk_bytenr, offset,
					    page_offset(page) + cur,
					    bio_flags);
			if (ret < 0)
				return ret;
		}
		/*
		 * We must go through btrfs_bio_add_page() to ensure each
		 * page range won't cross various boundaries.
		 */
		if (bio_flags & EXTENT_BIO_COMPRESSED)
			added = btrfs_bio_add_page(bio_ctrl, page, disk_bytenr,
					size - offset, pg_offset + offset,
					bio_flags);
		else
			added = btrfs_bio_add_page(bio_ctrl, page,
					disk_bytenr + offset, size - offset,
					pg_offset + offset, bio_flags);

		/* Metadata page range should never be split */
		if (!is_data_inode(&inode->vfs_inode))
			ASSERT(added == 0 || added == size - offset);

		/* At least we added some page, update the account */
		if (wbc && added)
			wbc_account_cgroup_owner(wbc, page, added);

		/* We have reached boundary, submit right now */
		if (added < size - offset) {
			/* The bio should contain some page(s) */
			ASSERT(bio_ctrl->bio->bi_iter.bi_size);
			ret = submit_one_bio(bio_ctrl->bio, mirror_num,
					bio_ctrl->bio_flags);
			bio_ctrl->bio = NULL;
			if (ret < 0)
				return ret;
		}
		cur += added;
	}
	return 0;
}

static int attach_extent_buffer_page(struct extent_buffer *eb,
				     struct page *page,
				     struct btrfs_subpage *prealloc)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	int ret = 0;

	/*
	 * If the page is mapped to btree inode, we should hold the private
	 * lock to prevent race.
	 * For cloned or dummy extent buffers, their pages are not mapped and
	 * will not race with any other ebs.
	 */
	if (page->mapping)
		lockdep_assert_held(&page->mapping->private_lock);

	if (fs_info->sectorsize == PAGE_SIZE) {
		if (!PagePrivate(page))
			attach_page_private(page, eb);
		else
			WARN_ON(page->private != (unsigned long)eb);
		return 0;
	}

	/* Already mapped, just free prealloc */
	if (PagePrivate(page)) {
		btrfs_free_subpage(prealloc);
		return 0;
	}

	if (prealloc)
		/* Has preallocated memory for subpage */
		attach_page_private(page, prealloc);
	else
		/* Do new allocation to attach subpage */
		ret = btrfs_attach_subpage(fs_info, page,
					   BTRFS_SUBPAGE_METADATA);
	return ret;
}

int set_page_extent_mapped(struct page *page)
{
	struct btrfs_fs_info *fs_info;

	ASSERT(page->mapping);

	if (PagePrivate(page))
		return 0;

	fs_info = btrfs_sb(page->mapping->host->i_sb);

	if (fs_info->sectorsize < PAGE_SIZE)
		return btrfs_attach_subpage(fs_info, page, BTRFS_SUBPAGE_DATA);

	attach_page_private(page, (void *)EXTENT_PAGE_PRIVATE);
	return 0;
}

void clear_page_extent_mapped(struct page *page)
{
	struct btrfs_fs_info *fs_info;

	ASSERT(page->mapping);

	if (!PagePrivate(page))
		return;

	fs_info = btrfs_sb(page->mapping->host->i_sb);
	if (fs_info->sectorsize < PAGE_SIZE)
		return btrfs_detach_subpage(fs_info, page);

	detach_page_private(page);
}

static struct extent_map *
__get_extent_map(struct inode *inode, struct page *page, size_t pg_offset,
		 u64 start, u64 len, struct extent_map **em_cached)
{
	struct extent_map *em;

	if (em_cached && *em_cached) {
		em = *em_cached;
		if (extent_map_in_tree(em) && start >= em->start &&
		    start < extent_map_end(em)) {
			refcount_inc(&em->refs);
			return em;
		}

		free_extent_map(em);
		*em_cached = NULL;
	}

	em = btrfs_get_extent(BTRFS_I(inode), page, pg_offset, start, len);
	if (em_cached && !IS_ERR_OR_NULL(em)) {
		BUG_ON(*em_cached);
		refcount_inc(&em->refs);
		*em_cached = em;
	}
	return em;
}
/*
 * basic readpage implementation.  Locked extent state structs are inserted
 * into the tree that are removed when the IO is done (by the end_io
 * handlers)
 * XXX JDM: This needs looking at to ensure proper page locking
 * return 0 on success, otherwise return error
 */
int btrfs_do_readpage(struct page *page, struct extent_map **em_cached,
		      struct btrfs_bio_ctrl *bio_ctrl,
		      unsigned int read_flags, u64 *prev_em_start)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 start = page_offset(page);
	const u64 end = start + PAGE_SIZE - 1;
	u64 cur = start;
	u64 extent_offset;
	u64 last_byte = i_size_read(inode);
	u64 block_start;
	u64 cur_end;
	struct extent_map *em;
	int ret = 0;
	int nr = 0;
	size_t pg_offset = 0;
	size_t iosize;
	size_t blocksize = inode->i_sb->s_blocksize;
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;

	ret = set_page_extent_mapped(page);
	if (ret < 0) {
		unlock_extent(tree, start, end);
		btrfs_page_set_error(fs_info, page, start, PAGE_SIZE);
		unlock_page(page);
		goto out;
	}

	if (!PageUptodate(page)) {
		if (cleancache_get_page(page) == 0) {
			BUG_ON(blocksize != PAGE_SIZE);
			unlock_extent(tree, start, end);
			unlock_page(page);
			goto out;
		}
	}

	if (page->index == last_byte >> PAGE_SHIFT) {
		size_t zero_offset = offset_in_page(last_byte);

		if (zero_offset) {
			iosize = PAGE_SIZE - zero_offset;
			memzero_page(page, zero_offset, iosize);
			flush_dcache_page(page);
		}
	}
	begin_page_read(fs_info, page);
	while (cur <= end) {
		unsigned long this_bio_flag = 0;
		bool force_bio_submit = false;
		u64 disk_bytenr;

		ASSERT(IS_ALIGNED(cur, fs_info->sectorsize));
		if (cur >= last_byte) {
			struct extent_state *cached = NULL;

			iosize = PAGE_SIZE - pg_offset;
			memzero_page(page, pg_offset, iosize);
			flush_dcache_page(page);
			set_extent_uptodate(tree, cur, cur + iosize - 1,
					    &cached, GFP_NOFS);
			unlock_extent_cached(tree, cur,
					     cur + iosize - 1, &cached);
			end_page_read(page, true, cur, iosize);
			break;
		}
		em = __get_extent_map(inode, page, pg_offset, cur,
				      end - cur + 1, em_cached);
		if (IS_ERR_OR_NULL(em)) {
			unlock_extent(tree, cur, end);
			end_page_read(page, false, cur, end + 1 - cur);
			break;
		}
		extent_offset = cur - em->start;
		BUG_ON(extent_map_end(em) <= cur);
		BUG_ON(end < cur);

		if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
			this_bio_flag |= EXTENT_BIO_COMPRESSED;
			extent_set_compress_type(&this_bio_flag,
						 em->compress_type);
		}

		iosize = min(extent_map_end(em) - cur, end - cur + 1);
		cur_end = min(extent_map_end(em) - 1, end);
		iosize = ALIGN(iosize, blocksize);
		if (this_bio_flag & EXTENT_BIO_COMPRESSED)
			disk_bytenr = em->block_start;
		else
			disk_bytenr = em->block_start + extent_offset;
		block_start = em->block_start;
		if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
			block_start = EXTENT_MAP_HOLE;

		/*
		 * If we have a file range that points to a compressed extent
		 * and it's followed by a consecutive file range that points
		 * to the same compressed extent (possibly with a different
		 * offset and/or length, so it either points to the whole extent
		 * or only part of it), we must make sure we do not submit a
		 * single bio to populate the pages for the 2 ranges because
		 * this makes the compressed extent read zero out the pages
		 * belonging to the 2nd range. Imagine the following scenario:
		 *
		 *  File layout
		 *  [0 - 8K]                     [8K - 24K]
		 *    |                               |
		 *    |                               |
		 * points to extent X,         points to extent X,
		 * offset 4K, length of 8K     offset 0, length 16K
		 *
		 * [extent X, compressed length = 4K uncompressed length = 16K]
		 *
		 * If the bio to read the compressed extent covers both ranges,
		 * it will decompress extent X into the pages belonging to the
		 * first range and then it will stop, zeroing out the remaining
		 * pages that belong to the other range that points to extent X.
		 * So here we make sure we submit 2 bios, one for the first
		 * range and another one for the third range. Both will target
		 * the same physical extent from disk, but we can't currently
		 * make the compressed bio endio callback populate the pages
		 * for both ranges because each compressed bio is tightly
		 * coupled with a single extent map, and each range can have
		 * an extent map with a different offset value relative to the
		 * uncompressed data of our extent and different lengths. This
		 * is a corner case so we prioritize correctness over
		 * non-optimal behavior (submitting 2 bios for the same extent).
		 */
		if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags) &&
		    prev_em_start && *prev_em_start != (u64)-1 &&
		    *prev_em_start != em->start)
			force_bio_submit = true;

		if (prev_em_start)
			*prev_em_start = em->start;

		free_extent_map(em);
		em = NULL;

		/* we've found a hole, just zero and go on */
		if (block_start == EXTENT_MAP_HOLE) {
			struct extent_state *cached = NULL;

			memzero_page(page, pg_offset, iosize);
			flush_dcache_page(page);

			set_extent_uptodate(tree, cur, cur + iosize - 1,
					    &cached, GFP_NOFS);
			unlock_extent_cached(tree, cur,
					     cur + iosize - 1, &cached);
			end_page_read(page, true, cur, iosize);
			cur = cur + iosize;
			pg_offset += iosize;
			continue;
		}
		/* the get_extent function already copied into the page */
		if (test_range_bit(tree, cur, cur_end,
				   EXTENT_UPTODATE, 1, NULL)) {
			unlock_extent(tree, cur, cur + iosize - 1);
			end_page_read(page, true, cur, iosize);
			cur = cur + iosize;
			pg_offset += iosize;
			continue;
		}
		/* we have an inline extent but it didn't get marked up
		 * to date.  Error out
		 */
		if (block_start == EXTENT_MAP_INLINE) {
			unlock_extent(tree, cur, cur + iosize - 1);
			end_page_read(page, false, cur, iosize);
			cur = cur + iosize;
			pg_offset += iosize;
			continue;
		}

		ret = submit_extent_page(REQ_OP_READ | read_flags, NULL,
					 bio_ctrl, page, disk_bytenr, iosize,
					 pg_offset,
					 end_bio_extent_readpage, 0,
					 this_bio_flag,
					 force_bio_submit);
		if (!ret) {
			nr++;
		} else {
			unlock_extent(tree, cur, cur + iosize - 1);
			end_page_read(page, false, cur, iosize);
			goto out;
		}
		cur = cur + iosize;
		pg_offset += iosize;
	}
out:
	return ret;
}

static inline void contiguous_readpages(struct page *pages[], int nr_pages,
					u64 start, u64 end,
					struct extent_map **em_cached,
					struct btrfs_bio_ctrl *bio_ctrl,
					u64 *prev_em_start)
{
	struct btrfs_inode *inode = BTRFS_I(pages[0]->mapping->host);
	int index;

	btrfs_lock_and_flush_ordered_range(inode, start, end, NULL);

	for (index = 0; index < nr_pages; index++) {
		btrfs_do_readpage(pages[index], em_cached, bio_ctrl,
				  REQ_RAHEAD, prev_em_start);
		put_page(pages[index]);
	}
}

static void update_nr_written(struct writeback_control *wbc,
			      unsigned long nr_written)
{
	wbc->nr_to_write -= nr_written;
}

/*
 * helper for __extent_writepage, doing all of the delayed allocation setup.
 *
 * This returns 1 if btrfs_run_delalloc_range function did all the work required
 * to write the page (copy into inline extent).  In this case the IO has
 * been started and the page is already unlocked.
 *
 * This returns 0 if all went well (page still locked)
 * This returns < 0 if there were errors (page still locked)
 */
static noinline_for_stack int writepage_delalloc(struct btrfs_inode *inode,
		struct page *page, struct writeback_control *wbc,
		unsigned long *nr_written)
{
	const u64 page_end = page_offset(page) + PAGE_SIZE - 1;
	u64 delalloc_start = page_offset(page);
	u64 delalloc_to_write = 0;
	int ret;
	int page_started = 0;

	while (delalloc_start < page_end) {
		u64 delalloc_end = page_end;
		bool found;

		found = find_lock_delalloc_range(&inode->vfs_inode, page,
					       &delalloc_start,
					       &delalloc_end);
		if (!found) {
			delalloc_start = delalloc_end + 1;
			continue;
		}
		ret = btrfs_run_delalloc_range(inode, page, delalloc_start,
				delalloc_end, &page_started, nr_written, wbc);
		if (ret) {
			btrfs_page_set_error(inode->root->fs_info, page,
					     page_offset(page), PAGE_SIZE);
			return ret;
		}
		/*
		 * delalloc_end is already one less than the total length, so
		 * we don't subtract one from PAGE_SIZE
		 */
		delalloc_to_write += (delalloc_end - delalloc_start +
				      PAGE_SIZE) >> PAGE_SHIFT;
		delalloc_start = delalloc_end + 1;
	}
	if (wbc->nr_to_write < delalloc_to_write) {
		int thresh = 8192;

		if (delalloc_to_write < thresh * 2)
			thresh = delalloc_to_write;
		wbc->nr_to_write = min_t(u64, delalloc_to_write,
					 thresh);
	}

	/* did the fill delalloc function already unlock and start
	 * the IO?
	 */
	if (page_started) {
		/*
		 * we've unlocked the page, so we can't update
		 * the mapping's writeback index, just update
		 * nr_to_write.
		 */
		wbc->nr_to_write -= *nr_written;
		return 1;
	}

	return 0;
}

/*
 * Find the first byte we need to write.
 *
 * For subpage, one page can contain several sectors, and
 * __extent_writepage_io() will just grab all extent maps in the page
 * range and try to submit all non-inline/non-compressed extents.
 *
 * This is a big problem for subpage, we shouldn't re-submit already written
 * data at all.
 * This function will lookup subpage dirty bit to find which range we really
 * need to submit.
 *
 * Return the next dirty range in [@start, @end).
 * If no dirty range is found, @start will be page_offset(page) + PAGE_SIZE.
 */
static void find_next_dirty_byte(struct btrfs_fs_info *fs_info,
				 struct page *page, u64 *start, u64 *end)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	struct btrfs_subpage_info *spi = fs_info->subpage_info;
	u64 orig_start = *start;
	/* Declare as unsigned long so we can use bitmap ops */
	unsigned long flags;
	int range_start_bit;
	int range_end_bit;

	/*
	 * For regular sector size == page size case, since one page only
	 * contains one sector, we return the page offset directly.
	 */
	if (fs_info->sectorsize == PAGE_SIZE) {
		*start = page_offset(page);
		*end = page_offset(page) + PAGE_SIZE;
		return;
	}

	range_start_bit = spi->dirty_offset +
			  (offset_in_page(orig_start) >> fs_info->sectorsize_bits);

	/* We should have the page locked, but just in case */
	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_next_set_region(subpage->bitmaps, &range_start_bit, &range_end_bit,
			       spi->dirty_offset + spi->bitmap_nr_bits);
	spin_unlock_irqrestore(&subpage->lock, flags);

	range_start_bit -= spi->dirty_offset;
	range_end_bit -= spi->dirty_offset;

	*start = page_offset(page) + range_start_bit * fs_info->sectorsize;
	*end = page_offset(page) + range_end_bit * fs_info->sectorsize;
}

/*
 * helper for __extent_writepage.  This calls the writepage start hooks,
 * and does the loop to map the page into extents and bios.
 *
 * We return 1 if the IO is started and the page is unlocked,
 * 0 if all went well (page still locked)
 * < 0 if there were errors (page still locked)
 */
static noinline_for_stack int __extent_writepage_io(struct btrfs_inode *inode,
				 struct page *page,
				 struct writeback_control *wbc,
				 struct extent_page_data *epd,
				 loff_t i_size,
				 unsigned long nr_written,
				 int *nr_ret)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u64 cur = page_offset(page);
	u64 end = cur + PAGE_SIZE - 1;
	u64 extent_offset;
	u64 block_start;
	struct extent_map *em;
	int ret = 0;
	int nr = 0;
	u32 opf = REQ_OP_WRITE;
	const unsigned int write_flags = wbc_to_write_flags(wbc);
	bool compressed;

	ret = btrfs_writepage_cow_fixup(page);
	if (ret) {
		/* Fixup worker will requeue */
		redirty_page_for_writepage(wbc, page);
		update_nr_written(wbc, nr_written);
		unlock_page(page);
		return 1;
	}

	/*
	 * we don't want to touch the inode after unlocking the page,
	 * so we update the mapping writeback index now
	 */
	update_nr_written(wbc, nr_written + 1);

	while (cur <= end) {
		u64 disk_bytenr;
		u64 em_end;
		u64 dirty_range_start = cur;
		u64 dirty_range_end;
		u32 iosize;

		if (cur >= i_size) {
			btrfs_writepage_endio_finish_ordered(inode, page, cur,
							     end, true);
			/*
			 * This range is beyond i_size, thus we don't need to
			 * bother writing back.
			 * But we still need to clear the dirty subpage bit, or
			 * the next time the page gets dirtied, we will try to
			 * writeback the sectors with subpage dirty bits,
			 * causing writeback without ordered extent.
			 */
			btrfs_page_clear_dirty(fs_info, page, cur, end + 1 - cur);
			break;
		}

		find_next_dirty_byte(fs_info, page, &dirty_range_start,
				     &dirty_range_end);
		if (cur < dirty_range_start) {
			cur = dirty_range_start;
			continue;
		}

		em = btrfs_get_extent(inode, NULL, 0, cur, end - cur + 1);
		if (IS_ERR_OR_NULL(em)) {
			btrfs_page_set_error(fs_info, page, cur, end - cur + 1);
			ret = PTR_ERR_OR_ZERO(em);
			break;
		}

		extent_offset = cur - em->start;
		em_end = extent_map_end(em);
		ASSERT(cur <= em_end);
		ASSERT(cur < end);
		ASSERT(IS_ALIGNED(em->start, fs_info->sectorsize));
		ASSERT(IS_ALIGNED(em->len, fs_info->sectorsize));
		block_start = em->block_start;
		compressed = test_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
		disk_bytenr = em->block_start + extent_offset;

		/*
		 * Note that em_end from extent_map_end() and dirty_range_end from
		 * find_next_dirty_byte() are all exclusive
		 */
		iosize = min(min(em_end, end + 1), dirty_range_end) - cur;

		if (btrfs_use_zone_append(inode, em->block_start))
			opf = REQ_OP_ZONE_APPEND;

		free_extent_map(em);
		em = NULL;

		/*
		 * compressed and inline extents are written through other
		 * paths in the FS
		 */
		if (compressed || block_start == EXTENT_MAP_HOLE ||
		    block_start == EXTENT_MAP_INLINE) {
			if (compressed)
				nr++;
			else
				btrfs_writepage_endio_finish_ordered(inode,
						page, cur, cur + iosize - 1, true);
			btrfs_page_clear_dirty(fs_info, page, cur, iosize);
			cur += iosize;
			continue;
		}

		btrfs_set_range_writeback(inode, cur, cur + iosize - 1);
		if (!PageWriteback(page)) {
			btrfs_err(inode->root->fs_info,
				   "page %lu not writeback, cur %llu end %llu",
			       page->index, cur, end);
		}

		/*
		 * Although the PageDirty bit is cleared before entering this
		 * function, subpage dirty bit is not cleared.
		 * So clear subpage dirty bit here so next time we won't submit
		 * page for range already written to disk.
		 */
		btrfs_page_clear_dirty(fs_info, page, cur, iosize);

		ret = submit_extent_page(opf | write_flags, wbc,
					 &epd->bio_ctrl, page,
					 disk_bytenr, iosize,
					 cur - page_offset(page),
					 end_bio_extent_writepage,
					 0, 0, false);
		if (ret) {
			btrfs_page_set_error(fs_info, page, cur, iosize);
			if (PageWriteback(page))
				btrfs_page_clear_writeback(fs_info, page, cur,
							   iosize);
		}

		cur += iosize;
		nr++;
	}
	/*
	 * If we finish without problem, we should not only clear page dirty,
	 * but also empty subpage dirty bits
	 */
	if (!ret)
		btrfs_page_assert_not_dirty(fs_info, page);
	*nr_ret = nr;
	return ret;
}

/*
 * the writepage semantics are similar to regular writepage.  extent
 * records are inserted to lock ranges in the tree, and as dirty areas
 * are found, they are marked writeback.  Then the lock bits are removed
 * and the end_io handler clears the writeback ranges
 *
 * Return 0 if everything goes well.
 * Return <0 for error.
 */
static int __extent_writepage(struct page *page, struct writeback_control *wbc,
			      struct extent_page_data *epd)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	const u64 page_start = page_offset(page);
	const u64 page_end = page_start + PAGE_SIZE - 1;
	int ret;
	int nr = 0;
	size_t pg_offset;
	loff_t i_size = i_size_read(inode);
	unsigned long end_index = i_size >> PAGE_SHIFT;
	unsigned long nr_written = 0;

	trace___extent_writepage(page, inode, wbc);

	WARN_ON(!PageLocked(page));

	btrfs_page_clear_error(btrfs_sb(inode->i_sb), page,
			       page_offset(page), PAGE_SIZE);

	pg_offset = offset_in_page(i_size);
	if (page->index > end_index ||
	   (page->index == end_index && !pg_offset)) {
		page->mapping->a_ops->invalidatepage(page, 0, PAGE_SIZE);
		unlock_page(page);
		return 0;
	}

	if (page->index == end_index) {
		memzero_page(page, pg_offset, PAGE_SIZE - pg_offset);
		flush_dcache_page(page);
	}

	ret = set_page_extent_mapped(page);
	if (ret < 0) {
		SetPageError(page);
		goto done;
	}

	if (!epd->extent_locked) {
		ret = writepage_delalloc(BTRFS_I(inode), page, wbc, &nr_written);
		if (ret == 1)
			return 0;
		if (ret)
			goto done;
	}

	ret = __extent_writepage_io(BTRFS_I(inode), page, wbc, epd, i_size,
				    nr_written, &nr);
	if (ret == 1)
		return 0;

done:
	if (nr == 0) {
		/* make sure the mapping tag for page dirty gets cleared */
		set_page_writeback(page);
		end_page_writeback(page);
	}
	/*
	 * Here we used to have a check for PageError() and then set @ret and
	 * call end_extent_writepage().
	 *
	 * But in fact setting @ret here will cause different error paths
	 * between subpage and regular sectorsize.
	 *
	 * For regular page size, we never submit current page, but only add
	 * current page to current bio.
	 * The bio submission can only happen in next page.
	 * Thus if we hit the PageError() branch, @ret is already set to
	 * non-zero value and will not get updated for regular sectorsize.
	 *
	 * But for subpage case, it's possible we submit part of current page,
	 * thus can get PageError() set by submitted bio of the same page,
	 * while our @ret is still 0.
	 *
	 * So here we unify the behavior and don't set @ret.
	 * Error can still be properly passed to higher layer as page will
	 * be set error, here we just don't handle the IO failure.
	 *
	 * NOTE: This is just a hotfix for subpage.
	 * The root fix will be properly ending ordered extent when we hit
	 * an error during writeback.
	 *
	 * But that needs a bigger refactoring, as we not only need to grab the
	 * submitted OE, but also need to know exactly at which bytenr we hit
	 * the error.
	 * Currently the full page based __extent_writepage_io() is not
	 * capable of that.
	 */
	if (PageError(page))
		end_extent_writepage(page, ret, page_start, page_end);
	if (epd->extent_locked) {
		/*
		 * If epd->extent_locked, it's from extent_write_locked_range(),
		 * the page can either be locked by lock_page() or
		 * process_one_page().
		 * Let btrfs_page_unlock_writer() handle both cases.
		 */
		ASSERT(wbc);
		btrfs_page_unlock_writer(fs_info, page, wbc->range_start,
					 wbc->range_end + 1 - wbc->range_start);
	} else {
		unlock_page(page);
	}
	ASSERT(ret <= 0);
	return ret;
}

void wait_on_extent_buffer_writeback(struct extent_buffer *eb)
{
	wait_on_bit_io(&eb->bflags, EXTENT_BUFFER_WRITEBACK,
		       TASK_UNINTERRUPTIBLE);
}

static void end_extent_buffer_writeback(struct extent_buffer *eb)
{
	if (test_bit(EXTENT_BUFFER_ZONE_FINISH, &eb->bflags))
		btrfs_zone_finish_endio(eb->fs_info, eb->start, eb->len);

	clear_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags);
	smp_mb__after_atomic();
	wake_up_bit(&eb->bflags, EXTENT_BUFFER_WRITEBACK);
}

/*
 * Lock extent buffer status and pages for writeback.
 *
 * May try to flush write bio if we can't get the lock.
 *
 * Return  0 if the extent buffer doesn't need to be submitted.
 *           (E.g. the extent buffer is not dirty)
 * Return >0 is the extent buffer is submitted to bio.
 * Return <0 if something went wrong, no page is locked.
 */
static noinline_for_stack int lock_extent_buffer_for_io(struct extent_buffer *eb,
			  struct extent_page_data *epd)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	int i, num_pages, failed_page_nr;
	int flush = 0;
	int ret = 0;

	if (!btrfs_try_tree_write_lock(eb)) {
		ret = flush_write_bio(epd);
		if (ret < 0)
			return ret;
		flush = 1;
		btrfs_tree_lock(eb);
	}

	if (test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags)) {
		btrfs_tree_unlock(eb);
		if (!epd->sync_io)
			return 0;
		if (!flush) {
			ret = flush_write_bio(epd);
			if (ret < 0)
				return ret;
			flush = 1;
		}
		while (1) {
			wait_on_extent_buffer_writeback(eb);
			btrfs_tree_lock(eb);
			if (!test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags))
				break;
			btrfs_tree_unlock(eb);
		}
	}

	/*
	 * We need to do this to prevent races in people who check if the eb is
	 * under IO since we can end up having no IO bits set for a short period
	 * of time.
	 */
	spin_lock(&eb->refs_lock);
	if (test_and_clear_bit(EXTENT_BUFFER_DIRTY, &eb->bflags)) {
		set_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags);
		spin_unlock(&eb->refs_lock);
		btrfs_set_header_flag(eb, BTRFS_HEADER_FLAG_WRITTEN);
		percpu_counter_add_batch(&fs_info->dirty_metadata_bytes,
					 -eb->len,
					 fs_info->dirty_metadata_batch);
		ret = 1;
	} else {
		spin_unlock(&eb->refs_lock);
	}

	btrfs_tree_unlock(eb);

	/*
	 * Either we don't need to submit any tree block, or we're submitting
	 * subpage eb.
	 * Subpage metadata doesn't use page locking at all, so we can skip
	 * the page locking.
	 */
	if (!ret || fs_info->sectorsize < PAGE_SIZE)
		return ret;

	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++) {
		struct page *p = eb->pages[i];

		if (!trylock_page(p)) {
			if (!flush) {
				int err;

				err = flush_write_bio(epd);
				if (err < 0) {
					ret = err;
					failed_page_nr = i;
					goto err_unlock;
				}
				flush = 1;
			}
			lock_page(p);
		}
	}

	return ret;
err_unlock:
	/* Unlock already locked pages */
	for (i = 0; i < failed_page_nr; i++)
		unlock_page(eb->pages[i]);
	/*
	 * Clear EXTENT_BUFFER_WRITEBACK and wake up anyone waiting on it.
	 * Also set back EXTENT_BUFFER_DIRTY so future attempts to this eb can
	 * be made and undo everything done before.
	 */
	btrfs_tree_lock(eb);
	spin_lock(&eb->refs_lock);
	set_bit(EXTENT_BUFFER_DIRTY, &eb->bflags);
	end_extent_buffer_writeback(eb);
	spin_unlock(&eb->refs_lock);
	percpu_counter_add_batch(&fs_info->dirty_metadata_bytes, eb->len,
				 fs_info->dirty_metadata_batch);
	btrfs_clear_header_flag(eb, BTRFS_HEADER_FLAG_WRITTEN);
	btrfs_tree_unlock(eb);
	return ret;
}

static void set_btree_ioerr(struct page *page, struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;

	btrfs_page_set_error(fs_info, page, eb->start, eb->len);
	if (test_and_set_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags))
		return;

	/*
	 * A read may stumble upon this buffer later, make sure that it gets an
	 * error and knows there was an error.
	 */
	clear_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);

	/*
	 * We need to set the mapping with the io error as well because a write
	 * error will flip the file system readonly, and then syncfs() will
	 * return a 0 because we are readonly if we don't modify the err seq for
	 * the superblock.
	 */
	mapping_set_error(page->mapping, -EIO);

	/*
	 * If we error out, we should add back the dirty_metadata_bytes
	 * to make it consistent.
	 */
	percpu_counter_add_batch(&fs_info->dirty_metadata_bytes,
				 eb->len, fs_info->dirty_metadata_batch);

	/*
	 * If writeback for a btree extent that doesn't belong to a log tree
	 * failed, increment the counter transaction->eb_write_errors.
	 * We do this because while the transaction is running and before it's
	 * committing (when we call filemap_fdata[write|wait]_range against
	 * the btree inode), we might have
	 * btree_inode->i_mapping->a_ops->writepages() called by the VM - if it
	 * returns an error or an error happens during writeback, when we're
	 * committing the transaction we wouldn't know about it, since the pages
	 * can be no longer dirty nor marked anymore for writeback (if a
	 * subsequent modification to the extent buffer didn't happen before the
	 * transaction commit), which makes filemap_fdata[write|wait]_range not
	 * able to find the pages tagged with SetPageError at transaction
	 * commit time. So if this happens we must abort the transaction,
	 * otherwise we commit a super block with btree roots that point to
	 * btree nodes/leafs whose content on disk is invalid - either garbage
	 * or the content of some node/leaf from a past generation that got
	 * cowed or deleted and is no longer valid.
	 *
	 * Note: setting AS_EIO/AS_ENOSPC in the btree inode's i_mapping would
	 * not be enough - we need to distinguish between log tree extents vs
	 * non-log tree extents, and the next filemap_fdatawait_range() call
	 * will catch and clear such errors in the mapping - and that call might
	 * be from a log sync and not from a transaction commit. Also, checking
	 * for the eb flag EXTENT_BUFFER_WRITE_ERR at transaction commit time is
	 * not done and would not be reliable - the eb might have been released
	 * from memory and reading it back again means that flag would not be
	 * set (since it's a runtime flag, not persisted on disk).
	 *
	 * Using the flags below in the btree inode also makes us achieve the
	 * goal of AS_EIO/AS_ENOSPC when writepages() returns success, started
	 * writeback for all dirty pages and before filemap_fdatawait_range()
	 * is called, the writeback for all dirty pages had already finished
	 * with errors - because we were not using AS_EIO/AS_ENOSPC,
	 * filemap_fdatawait_range() would return success, as it could not know
	 * that writeback errors happened (the pages were no longer tagged for
	 * writeback).
	 */
	switch (eb->log_index) {
	case -1:
		set_bit(BTRFS_FS_BTREE_ERR, &fs_info->flags);
		break;
	case 0:
		set_bit(BTRFS_FS_LOG1_ERR, &fs_info->flags);
		break;
	case 1:
		set_bit(BTRFS_FS_LOG2_ERR, &fs_info->flags);
		break;
	default:
		BUG(); /* unexpected, logic error */
	}
}

/*
 * The endio specific version which won't touch any unsafe spinlock in endio
 * context.
 */
static struct extent_buffer *find_extent_buffer_nolock(
		struct btrfs_fs_info *fs_info, u64 start)
{
	struct extent_buffer *eb;

	rcu_read_lock();
	eb = radix_tree_lookup(&fs_info->buffer_radix,
			       start >> fs_info->sectorsize_bits);
	if (eb && atomic_inc_not_zero(&eb->refs)) {
		rcu_read_unlock();
		return eb;
	}
	rcu_read_unlock();
	return NULL;
}

/*
 * The endio function for subpage extent buffer write.
 *
 * Unlike end_bio_extent_buffer_writepage(), we only call end_page_writeback()
 * after all extent buffers in the page has finished their writeback.
 */
static void end_bio_subpage_eb_writepage(struct bio *bio)
{
	struct btrfs_fs_info *fs_info;
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	fs_info = btrfs_sb(bio_first_page_all(bio)->mapping->host->i_sb);
	ASSERT(fs_info->sectorsize < PAGE_SIZE);

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *page = bvec->bv_page;
		u64 bvec_start = page_offset(page) + bvec->bv_offset;
		u64 bvec_end = bvec_start + bvec->bv_len - 1;
		u64 cur_bytenr = bvec_start;

		ASSERT(IS_ALIGNED(bvec->bv_len, fs_info->nodesize));

		/* Iterate through all extent buffers in the range */
		while (cur_bytenr <= bvec_end) {
			struct extent_buffer *eb;
			int done;

			/*
			 * Here we can't use find_extent_buffer(), as it may
			 * try to lock eb->refs_lock, which is not safe in endio
			 * context.
			 */
			eb = find_extent_buffer_nolock(fs_info, cur_bytenr);
			ASSERT(eb);

			cur_bytenr = eb->start + eb->len;

			ASSERT(test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags));
			done = atomic_dec_and_test(&eb->io_pages);
			ASSERT(done);

			if (bio->bi_status ||
			    test_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags)) {
				ClearPageUptodate(page);
				set_btree_ioerr(page, eb);
			}

			btrfs_subpage_clear_writeback(fs_info, page, eb->start,
						      eb->len);
			end_extent_buffer_writeback(eb);
			/*
			 * free_extent_buffer() will grab spinlock which is not
			 * safe in endio context. Thus here we manually dec
			 * the ref.
			 */
			atomic_dec(&eb->refs);
		}
	}
	bio_put(bio);
}

static void end_bio_extent_buffer_writepage(struct bio *bio)
{
	struct bio_vec *bvec;
	struct extent_buffer *eb;
	int done;
	struct bvec_iter_all iter_all;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *page = bvec->bv_page;

		eb = (struct extent_buffer *)page->private;
		BUG_ON(!eb);
		done = atomic_dec_and_test(&eb->io_pages);

		if (bio->bi_status ||
		    test_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags)) {
			ClearPageUptodate(page);
			set_btree_ioerr(page, eb);
		}

		end_page_writeback(page);

		if (!done)
			continue;

		end_extent_buffer_writeback(eb);
	}

	bio_put(bio);
}

static void prepare_eb_write(struct extent_buffer *eb)
{
	u32 nritems;
	unsigned long start;
	unsigned long end;

	clear_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags);
	atomic_set(&eb->io_pages, num_extent_pages(eb));

	/* Set btree blocks beyond nritems with 0 to avoid stale content */
	nritems = btrfs_header_nritems(eb);
	if (btrfs_header_level(eb) > 0) {
		end = btrfs_node_key_ptr_offset(nritems);
		memzero_extent_buffer(eb, end, eb->len - end);
	} else {
		/*
		 * Leaf:
		 * header 0 1 2 .. N ... data_N .. data_2 data_1 data_0
		 */
		start = btrfs_item_nr_offset(nritems);
		end = BTRFS_LEAF_DATA_OFFSET + leaf_data_end(eb);
		memzero_extent_buffer(eb, start, end - start);
	}
}

/*
 * Unlike the work in write_one_eb(), we rely completely on extent locking.
 * Page locking is only utilized at minimum to keep the VMM code happy.
 */
static int write_one_subpage_eb(struct extent_buffer *eb,
				struct writeback_control *wbc,
				struct extent_page_data *epd)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct page *page = eb->pages[0];
	unsigned int write_flags = wbc_to_write_flags(wbc) | REQ_META;
	bool no_dirty_ebs = false;
	int ret;

	prepare_eb_write(eb);

	/* clear_page_dirty_for_io() in subpage helper needs page locked */
	lock_page(page);
	btrfs_subpage_set_writeback(fs_info, page, eb->start, eb->len);

	/* Check if this is the last dirty bit to update nr_written */
	no_dirty_ebs = btrfs_subpage_clear_and_test_dirty(fs_info, page,
							  eb->start, eb->len);
	if (no_dirty_ebs)
		clear_page_dirty_for_io(page);

	ret = submit_extent_page(REQ_OP_WRITE | write_flags, wbc,
			&epd->bio_ctrl, page, eb->start, eb->len,
			eb->start - page_offset(page),
			end_bio_subpage_eb_writepage, 0, 0, false);
	if (ret) {
		btrfs_subpage_clear_writeback(fs_info, page, eb->start, eb->len);
		set_btree_ioerr(page, eb);
		unlock_page(page);

		if (atomic_dec_and_test(&eb->io_pages))
			end_extent_buffer_writeback(eb);
		return -EIO;
	}
	unlock_page(page);
	/*
	 * Submission finished without problem, if no range of the page is
	 * dirty anymore, we have submitted a page.  Update nr_written in wbc.
	 */
	if (no_dirty_ebs)
		update_nr_written(wbc, 1);
	return ret;
}

static noinline_for_stack int write_one_eb(struct extent_buffer *eb,
			struct writeback_control *wbc,
			struct extent_page_data *epd)
{
	u64 disk_bytenr = eb->start;
	int i, num_pages;
	unsigned int write_flags = wbc_to_write_flags(wbc) | REQ_META;
	int ret = 0;

	prepare_eb_write(eb);

	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++) {
		struct page *p = eb->pages[i];

		clear_page_dirty_for_io(p);
		set_page_writeback(p);
		ret = submit_extent_page(REQ_OP_WRITE | write_flags, wbc,
					 &epd->bio_ctrl, p, disk_bytenr,
					 PAGE_SIZE, 0,
					 end_bio_extent_buffer_writepage,
					 0, 0, false);
		if (ret) {
			set_btree_ioerr(p, eb);
			if (PageWriteback(p))
				end_page_writeback(p);
			if (atomic_sub_and_test(num_pages - i, &eb->io_pages))
				end_extent_buffer_writeback(eb);
			ret = -EIO;
			break;
		}
		disk_bytenr += PAGE_SIZE;
		update_nr_written(wbc, 1);
		unlock_page(p);
	}

	if (unlikely(ret)) {
		for (; i < num_pages; i++) {
			struct page *p = eb->pages[i];
			clear_page_dirty_for_io(p);
			unlock_page(p);
		}
	}

	return ret;
}

/*
 * Submit one subpage btree page.
 *
 * The main difference to submit_eb_page() is:
 * - Page locking
 *   For subpage, we don't rely on page locking at all.
 *
 * - Flush write bio
 *   We only flush bio if we may be unable to fit current extent buffers into
 *   current bio.
 *
 * Return >=0 for the number of submitted extent buffers.
 * Return <0 for fatal error.
 */
static int submit_eb_subpage(struct page *page,
			     struct writeback_control *wbc,
			     struct extent_page_data *epd)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(page->mapping->host->i_sb);
	int submitted = 0;
	u64 page_start = page_offset(page);
	int bit_start = 0;
	int sectors_per_node = fs_info->nodesize >> fs_info->sectorsize_bits;
	int ret;

	/* Lock and write each dirty extent buffers in the range */
	while (bit_start < fs_info->subpage_info->bitmap_nr_bits) {
		struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
		struct extent_buffer *eb;
		unsigned long flags;
		u64 start;

		/*
		 * Take private lock to ensure the subpage won't be detached
		 * in the meantime.
		 */
		spin_lock(&page->mapping->private_lock);
		if (!PagePrivate(page)) {
			spin_unlock(&page->mapping->private_lock);
			break;
		}
		spin_lock_irqsave(&subpage->lock, flags);
		if (!test_bit(bit_start + fs_info->subpage_info->dirty_offset,
			      subpage->bitmaps)) {
			spin_unlock_irqrestore(&subpage->lock, flags);
			spin_unlock(&page->mapping->private_lock);
			bit_start++;
			continue;
		}

		start = page_start + bit_start * fs_info->sectorsize;
		bit_start += sectors_per_node;

		/*
		 * Here we just want to grab the eb without touching extra
		 * spin locks, so call find_extent_buffer_nolock().
		 */
		eb = find_extent_buffer_nolock(fs_info, start);
		spin_unlock_irqrestore(&subpage->lock, flags);
		spin_unlock(&page->mapping->private_lock);

		/*
		 * The eb has already reached 0 refs thus find_extent_buffer()
		 * doesn't return it. We don't need to write back such eb
		 * anyway.
		 */
		if (!eb)
			continue;

		ret = lock_extent_buffer_for_io(eb, epd);
		if (ret == 0) {
			free_extent_buffer(eb);
			continue;
		}
		if (ret < 0) {
			free_extent_buffer(eb);
			goto cleanup;
		}
		ret = write_one_subpage_eb(eb, wbc, epd);
		free_extent_buffer(eb);
		if (ret < 0)
			goto cleanup;
		submitted++;
	}
	return submitted;

cleanup:
	/* We hit error, end bio for the submitted extent buffers */
	end_write_bio(epd, ret);
	return ret;
}

/*
 * Submit all page(s) of one extent buffer.
 *
 * @page:	the page of one extent buffer
 * @eb_context:	to determine if we need to submit this page, if current page
 *		belongs to this eb, we don't need to submit
 *
 * The caller should pass each page in their bytenr order, and here we use
 * @eb_context to determine if we have submitted pages of one extent buffer.
 *
 * If we have, we just skip until we hit a new page that doesn't belong to
 * current @eb_context.
 *
 * If not, we submit all the page(s) of the extent buffer.
 *
 * Return >0 if we have submitted the extent buffer successfully.
 * Return 0 if we don't need to submit the page, as it's already submitted by
 * previous call.
 * Return <0 for fatal error.
 */
static int submit_eb_page(struct page *page, struct writeback_control *wbc,
			  struct extent_page_data *epd,
			  struct extent_buffer **eb_context)
{
	struct address_space *mapping = page->mapping;
	struct btrfs_block_group *cache = NULL;
	struct extent_buffer *eb;
	int ret;

	if (!PagePrivate(page))
		return 0;

	if (btrfs_sb(page->mapping->host->i_sb)->sectorsize < PAGE_SIZE)
		return submit_eb_subpage(page, wbc, epd);

	spin_lock(&mapping->private_lock);
	if (!PagePrivate(page)) {
		spin_unlock(&mapping->private_lock);
		return 0;
	}

	eb = (struct extent_buffer *)page->private;

	/*
	 * Shouldn't happen and normally this would be a BUG_ON but no point
	 * crashing the machine for something we can survive anyway.
	 */
	if (WARN_ON(!eb)) {
		spin_unlock(&mapping->private_lock);
		return 0;
	}

	if (eb == *eb_context) {
		spin_unlock(&mapping->private_lock);
		return 0;
	}
	ret = atomic_inc_not_zero(&eb->refs);
	spin_unlock(&mapping->private_lock);
	if (!ret)
		return 0;

	if (!btrfs_check_meta_write_pointer(eb->fs_info, eb, &cache)) {
		/*
		 * If for_sync, this hole will be filled with
		 * trasnsaction commit.
		 */
		if (wbc->sync_mode == WB_SYNC_ALL && !wbc->for_sync)
			ret = -EAGAIN;
		else
			ret = 0;
		free_extent_buffer(eb);
		return ret;
	}

	*eb_context = eb;

	ret = lock_extent_buffer_for_io(eb, epd);
	if (ret <= 0) {
		btrfs_revert_meta_write_pointer(cache, eb);
		if (cache)
			btrfs_put_block_group(cache);
		free_extent_buffer(eb);
		return ret;
	}
	if (cache) {
		/* Impiles write in zoned mode */
		btrfs_put_block_group(cache);
		/* Mark the last eb in a block group */
		if (cache->seq_zone && eb->start + eb->len == cache->zone_capacity)
			set_bit(EXTENT_BUFFER_ZONE_FINISH, &eb->bflags);
	}
	ret = write_one_eb(eb, wbc, epd);
	free_extent_buffer(eb);
	if (ret < 0)
		return ret;
	return 1;
}

int btree_write_cache_pages(struct address_space *mapping,
				   struct writeback_control *wbc)
{
	struct extent_buffer *eb_context = NULL;
	struct extent_page_data epd = {
		.bio_ctrl = { 0 },
		.extent_locked = 0,
		.sync_io = wbc->sync_mode == WB_SYNC_ALL,
	};
	struct btrfs_fs_info *fs_info = BTRFS_I(mapping->host)->root->fs_info;
	int ret = 0;
	int done = 0;
	int nr_to_write_done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	int scanned = 0;
	xa_mark_t tag;

	pagevec_init(&pvec);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
		/*
		 * Start from the beginning does not need to cycle over the
		 * range, mark it as scanned.
		 */
		scanned = (index == 0);
	} else {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		scanned = 1;
	}
	if (wbc->sync_mode == WB_SYNC_ALL)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
	btrfs_zoned_meta_io_lock(fs_info);
retry:
	if (wbc->sync_mode == WB_SYNC_ALL)
		tag_pages_for_writeback(mapping, index, end);
	while (!done && !nr_to_write_done && (index <= end) &&
	       (nr_pages = pagevec_lookup_range_tag(&pvec, mapping, &index, end,
			tag))) {
		unsigned i;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			ret = submit_eb_page(page, wbc, &epd, &eb_context);
			if (ret == 0)
				continue;
			if (ret < 0) {
				done = 1;
				break;
			}

			/*
			 * the filesystem may choose to bump up nr_to_write.
			 * We have to make sure to honor the new nr_to_write
			 * at any time
			 */
			nr_to_write_done = wbc->nr_to_write <= 0;
		}
		pagevec_release(&pvec);
		cond_resched();
	}
	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;
		goto retry;
	}
	if (ret < 0) {
		end_write_bio(&epd, ret);
		goto out;
	}
	/*
	 * If something went wrong, don't allow any metadata write bio to be
	 * submitted.
	 *
	 * This would prevent use-after-free if we had dirty pages not
	 * cleaned up, which can still happen by fuzzed images.
	 *
	 * - Bad extent tree
	 *   Allowing existing tree block to be allocated for other trees.
	 *
	 * - Log tree operations
	 *   Exiting tree blocks get allocated to log tree, bumps its
	 *   generation, then get cleaned in tree re-balance.
	 *   Such tree block will not be written back, since it's clean,
	 *   thus no WRITTEN flag set.
	 *   And after log writes back, this tree block is not traced by
	 *   any dirty extent_io_tree.
	 *
	 * - Offending tree block gets re-dirtied from its original owner
	 *   Since it has bumped generation, no WRITTEN flag, it can be
	 *   reused without COWing. This tree block will not be traced
	 *   by btrfs_transaction::dirty_pages.
	 *
	 *   Now such dirty tree block will not be cleaned by any dirty
	 *   extent io tree. Thus we don't want to submit such wild eb
	 *   if the fs already has error.
	 */
	if (!BTRFS_FS_ERROR(fs_info)) {
		ret = flush_write_bio(&epd);
	} else {
		ret = -EROFS;
		end_write_bio(&epd, ret);
	}
out:
	btrfs_zoned_meta_io_unlock(fs_info);
	return ret;
}

/**
 * Walk the list of dirty pages of the given address space and write all of them.
 *
 * @mapping: address space structure to write
 * @wbc:     subtract the number of written pages from *@wbc->nr_to_write
 * @epd:     holds context for the write, namely the bio
 *
 * If a page is already under I/O, write_cache_pages() skips it, even
 * if it's dirty.  This is desirable behaviour for memory-cleaning writeback,
 * but it is INCORRECT for data-integrity system calls such as fsync().  fsync()
 * and msync() need to guarantee that all the data which was dirty at the time
 * the call was made get new I/O started against them.  If wbc->sync_mode is
 * WB_SYNC_ALL then we were called for data integrity and we must wait for
 * existing IO to complete.
 */
static int extent_write_cache_pages(struct address_space *mapping,
			     struct writeback_control *wbc,
			     struct extent_page_data *epd)
{
	struct inode *inode = mapping->host;
	int ret = 0;
	int done = 0;
	int nr_to_write_done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index;
	int range_whole = 0;
	int scanned = 0;
	xa_mark_t tag;

	/*
	 * We have to hold onto the inode so that ordered extents can do their
	 * work when the IO finishes.  The alternative to this is failing to add
	 * an ordered extent if the igrab() fails there and that is a huge pain
	 * to deal with, so instead just hold onto the inode throughout the
	 * writepages operation.  If it fails here we are freeing up the inode
	 * anyway and we'd rather not waste our time writing out stuff that is
	 * going to be truncated anyway.
	 */
	if (!igrab(inode))
		return 0;

	pagevec_init(&pvec);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
		/*
		 * Start from the beginning does not need to cycle over the
		 * range, mark it as scanned.
		 */
		scanned = (index == 0);
	} else {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		scanned = 1;
	}

	/*
	 * We do the tagged writepage as long as the snapshot flush bit is set
	 * and we are the first one who do the filemap_flush() on this inode.
	 *
	 * The nr_to_write == LONG_MAX is needed to make sure other flushers do
	 * not race in and drop the bit.
	 */
	if (range_whole && wbc->nr_to_write == LONG_MAX &&
	    test_and_clear_bit(BTRFS_INODE_SNAPSHOT_FLUSH,
			       &BTRFS_I(inode)->runtime_flags))
		wbc->tagged_writepages = 1;

	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);
	done_index = index;
	while (!done && !nr_to_write_done && (index <= end) &&
			(nr_pages = pagevec_lookup_range_tag(&pvec, mapping,
						&index, end, tag))) {
		unsigned i;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			done_index = page->index + 1;
			/*
			 * At this point we hold neither the i_pages lock nor
			 * the page lock: the page may be truncated or
			 * invalidated (changing page->mapping to NULL),
			 * or even swizzled back from swapper_space to
			 * tmpfs file mapping
			 */
			if (!trylock_page(page)) {
				ret = flush_write_bio(epd);
				BUG_ON(ret < 0);
				lock_page(page);
			}

			if (unlikely(page->mapping != mapping)) {
				unlock_page(page);
				continue;
			}

			if (wbc->sync_mode != WB_SYNC_NONE) {
				if (PageWriteback(page)) {
					ret = flush_write_bio(epd);
					BUG_ON(ret < 0);
				}
				wait_on_page_writeback(page);
			}

			if (PageWriteback(page) ||
			    !clear_page_dirty_for_io(page)) {
				unlock_page(page);
				continue;
			}

			ret = __extent_writepage(page, wbc, epd);
			if (ret < 0) {
				done = 1;
				break;
			}

			/*
			 * the filesystem may choose to bump up nr_to_write.
			 * We have to make sure to honor the new nr_to_write
			 * at any time
			 */
			nr_to_write_done = wbc->nr_to_write <= 0;
		}
		pagevec_release(&pvec);
		cond_resched();
	}
	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;

		/*
		 * If we're looping we could run into a page that is locked by a
		 * writer and that writer could be waiting on writeback for a
		 * page in our current bio, and thus deadlock, so flush the
		 * write bio here.
		 */
		ret = flush_write_bio(epd);
		if (!ret)
			goto retry;
	}

	if (wbc->range_cyclic || (wbc->nr_to_write > 0 && range_whole))
		mapping->writeback_index = done_index;

	btrfs_add_delayed_iput(inode);
	return ret;
}

int extent_write_full_page(struct page *page, struct writeback_control *wbc)
{
	int ret;
	struct extent_page_data epd = {
		.bio_ctrl = { 0 },
		.extent_locked = 0,
		.sync_io = wbc->sync_mode == WB_SYNC_ALL,
	};

	ret = __extent_writepage(page, wbc, &epd);
	ASSERT(ret <= 0);
	if (ret < 0) {
		end_write_bio(&epd, ret);
		return ret;
	}

	ret = flush_write_bio(&epd);
	ASSERT(ret <= 0);
	return ret;
}

/*
 * Submit the pages in the range to bio for call sites which delalloc range has
 * already been ran (aka, ordered extent inserted) and all pages are still
 * locked.
 */
int extent_write_locked_range(struct inode *inode, u64 start, u64 end)
{
	bool found_error = false;
	int first_error = 0;
	int ret = 0;
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	u64 cur = start;
	unsigned long nr_pages;
	const u32 sectorsize = btrfs_sb(inode->i_sb)->sectorsize;
	struct extent_page_data epd = {
		.bio_ctrl = { 0 },
		.extent_locked = 1,
		.sync_io = 1,
	};
	struct writeback_control wbc_writepages = {
		.sync_mode	= WB_SYNC_ALL,
		.range_start	= start,
		.range_end	= end + 1,
		/* We're called from an async helper function */
		.punt_to_cgroup	= 1,
		.no_cgroup_owner = 1,
	};

	ASSERT(IS_ALIGNED(start, sectorsize) && IS_ALIGNED(end + 1, sectorsize));
	nr_pages = (round_up(end, PAGE_SIZE) - round_down(start, PAGE_SIZE)) >>
		   PAGE_SHIFT;
	wbc_writepages.nr_to_write = nr_pages * 2;

	wbc_attach_fdatawrite_inode(&wbc_writepages, inode);
	while (cur <= end) {
		u64 cur_end = min(round_down(cur, PAGE_SIZE) + PAGE_SIZE - 1, end);

		page = find_get_page(mapping, cur >> PAGE_SHIFT);
		/*
		 * All pages in the range are locked since
		 * btrfs_run_delalloc_range(), thus there is no way to clear
		 * the page dirty flag.
		 */
		ASSERT(PageLocked(page));
		ASSERT(PageDirty(page));
		clear_page_dirty_for_io(page);
		ret = __extent_writepage(page, &wbc_writepages, &epd);
		ASSERT(ret <= 0);
		if (ret < 0) {
			found_error = true;
			first_error = ret;
		}
		put_page(page);
		cur = cur_end + 1;
	}

	if (!found_error)
		ret = flush_write_bio(&epd);
	else
		end_write_bio(&epd, ret);

	wbc_detach_inode(&wbc_writepages);
	if (found_error)
		return first_error;
	return ret;
}

int extent_writepages(struct address_space *mapping,
		      struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	const bool data_reloc = btrfs_is_data_reloc_root(BTRFS_I(inode)->root);
	const bool zoned = btrfs_is_zoned(BTRFS_I(inode)->root->fs_info);
	int ret = 0;
	struct extent_page_data epd = {
		.bio_ctrl = { 0 },
		.extent_locked = 0,
		.sync_io = wbc->sync_mode == WB_SYNC_ALL,
	};

	/*
	 * Allow only a single thread to do the reloc work in zoned mode to
	 * protect the write pointer updates.
	 */
	if (data_reloc && zoned)
		btrfs_inode_lock(inode, 0);
	ret = extent_write_cache_pages(mapping, wbc, &epd);
	if (data_reloc && zoned)
		btrfs_inode_unlock(inode, 0);
	ASSERT(ret <= 0);
	if (ret < 0) {
		end_write_bio(&epd, ret);
		return ret;
	}
	ret = flush_write_bio(&epd);
	return ret;
}

void extent_readahead(struct readahead_control *rac)
{
	struct btrfs_bio_ctrl bio_ctrl = { 0 };
	struct page *pagepool[16];
	struct extent_map *em_cached = NULL;
	u64 prev_em_start = (u64)-1;
	int nr;

	while ((nr = readahead_page_batch(rac, pagepool))) {
		u64 contig_start = readahead_pos(rac);
		u64 contig_end = contig_start + readahead_batch_length(rac) - 1;

		contiguous_readpages(pagepool, nr, contig_start, contig_end,
				&em_cached, &bio_ctrl, &prev_em_start);
	}

	if (em_cached)
		free_extent_map(em_cached);

	if (bio_ctrl.bio) {
		if (submit_one_bio(bio_ctrl.bio, 0, bio_ctrl.bio_flags))
			return;
	}
}

/*
 * basic invalidatepage code, this waits on any locked or writeback
 * ranges corresponding to the page, and then deletes any extent state
 * records from the tree
 */
int extent_invalidatepage(struct extent_io_tree *tree,
			  struct page *page, unsigned long offset)
{
	struct extent_state *cached_state = NULL;
	u64 start = page_offset(page);
	u64 end = start + PAGE_SIZE - 1;
	size_t blocksize = page->mapping->host->i_sb->s_blocksize;

	/* This function is only called for the btree inode */
	ASSERT(tree->owner == IO_TREE_BTREE_INODE_IO);

	start += ALIGN(offset, blocksize);
	if (start > end)
		return 0;

	lock_extent_bits(tree, start, end, &cached_state);
	wait_on_page_writeback(page);

	/*
	 * Currently for btree io tree, only EXTENT_LOCKED is utilized,
	 * so here we only need to unlock the extent range to free any
	 * existing extent state.
	 */
	unlock_extent_cached(tree, start, end, &cached_state);
	return 0;
}

/*
 * a helper for releasepage, this tests for areas of the page that
 * are locked or under IO and drops the related state bits if it is safe
 * to drop the page.
 */
static int try_release_extent_state(struct extent_io_tree *tree,
				    struct page *page, gfp_t mask)
{
	u64 start = page_offset(page);
	u64 end = start + PAGE_SIZE - 1;
	int ret = 1;

	if (test_range_bit(tree, start, end, EXTENT_LOCKED, 0, NULL)) {
		ret = 0;
	} else {
		/*
		 * At this point we can safely clear everything except the
		 * locked bit, the nodatasum bit and the delalloc new bit.
		 * The delalloc new bit will be cleared by ordered extent
		 * completion.
		 */
		ret = __clear_extent_bit(tree, start, end,
			 ~(EXTENT_LOCKED | EXTENT_NODATASUM | EXTENT_DELALLOC_NEW),
			 0, 0, NULL, mask, NULL);

		/* if clear_extent_bit failed for enomem reasons,
		 * we can't allow the release to continue.
		 */
		if (ret < 0)
			ret = 0;
		else
			ret = 1;
	}
	return ret;
}

/*
 * a helper for releasepage.  As long as there are no locked extents
 * in the range corresponding to the page, both state records and extent
 * map records are removed
 */
int try_release_extent_mapping(struct page *page, gfp_t mask)
{
	struct extent_map *em;
	u64 start = page_offset(page);
	u64 end = start + PAGE_SIZE - 1;
	struct btrfs_inode *btrfs_inode = BTRFS_I(page->mapping->host);
	struct extent_io_tree *tree = &btrfs_inode->io_tree;
	struct extent_map_tree *map = &btrfs_inode->extent_tree;

	if (gfpflags_allow_blocking(mask) &&
	    page->mapping->host->i_size > SZ_16M) {
		u64 len;
		while (start <= end) {
			struct btrfs_fs_info *fs_info;
			u64 cur_gen;

			len = end - start + 1;
			write_lock(&map->lock);
			em = lookup_extent_mapping(map, start, len);
			if (!em) {
				write_unlock(&map->lock);
				break;
			}
			if (test_bit(EXTENT_FLAG_PINNED, &em->flags) ||
			    em->start != start) {
				write_unlock(&map->lock);
				free_extent_map(em);
				break;
			}
			if (test_range_bit(tree, em->start,
					   extent_map_end(em) - 1,
					   EXTENT_LOCKED, 0, NULL))
				goto next;
			/*
			 * If it's not in the list of modified extents, used
			 * by a fast fsync, we can remove it. If it's being
			 * logged we can safely remove it since fsync took an
			 * extra reference on the em.
			 */
			if (list_empty(&em->list) ||
			    test_bit(EXTENT_FLAG_LOGGING, &em->flags))
				goto remove_em;
			/*
			 * If it's in the list of modified extents, remove it
			 * only if its generation is older then the current one,
			 * in which case we don't need it for a fast fsync.
			 * Otherwise don't remove it, we could be racing with an
			 * ongoing fast fsync that could miss the new extent.
			 */
			fs_info = btrfs_inode->root->fs_info;
			spin_lock(&fs_info->trans_lock);
			cur_gen = fs_info->generation;
			spin_unlock(&fs_info->trans_lock);
			if (em->generation >= cur_gen)
				goto next;
remove_em:
			/*
			 * We only remove extent maps that are not in the list of
			 * modified extents or that are in the list but with a
			 * generation lower then the current generation, so there
			 * is no need to set the full fsync flag on the inode (it
			 * hurts the fsync performance for workloads with a data
			 * size that exceeds or is close to the system's memory).
			 */
			remove_extent_mapping(map, em);
			/* once for the rb tree */
			free_extent_map(em);
next:
			start = extent_map_end(em);
			write_unlock(&map->lock);

			/* once for us */
			free_extent_map(em);

			cond_resched(); /* Allow large-extent preemption. */
		}
	}
	return try_release_extent_state(tree, page, mask);
}

/*
 * helper function for fiemap, which doesn't want to see any holes.
 * This maps until we find something past 'last'
 */
static struct extent_map *get_extent_skip_holes(struct btrfs_inode *inode,
						u64 offset, u64 last)
{
	u64 sectorsize = btrfs_inode_sectorsize(inode);
	struct extent_map *em;
	u64 len;

	if (offset >= last)
		return NULL;

	while (1) {
		len = last - offset;
		if (len == 0)
			break;
		len = ALIGN(len, sectorsize);
		em = btrfs_get_extent_fiemap(inode, offset, len);
		if (IS_ERR_OR_NULL(em))
			return em;

		/* if this isn't a hole return it */
		if (em->block_start != EXTENT_MAP_HOLE)
			return em;

		/* this is a hole, advance to the next extent */
		offset = extent_map_end(em);
		free_extent_map(em);
		if (offset >= last)
			break;
	}
	return NULL;
}

/*
 * To cache previous fiemap extent
 *
 * Will be used for merging fiemap extent
 */
struct fiemap_cache {
	u64 offset;
	u64 phys;
	u64 len;
	u32 flags;
	bool cached;
};

/*
 * Helper to submit fiemap extent.
 *
 * Will try to merge current fiemap extent specified by @offset, @phys,
 * @len and @flags with cached one.
 * And only when we fails to merge, cached one will be submitted as
 * fiemap extent.
 *
 * Return value is the same as fiemap_fill_next_extent().
 */
static int emit_fiemap_extent(struct fiemap_extent_info *fieinfo,
				struct fiemap_cache *cache,
				u64 offset, u64 phys, u64 len, u32 flags)
{
	int ret = 0;

	if (!cache->cached)
		goto assign;

	/*
	 * Sanity check, extent_fiemap() should have ensured that new
	 * fiemap extent won't overlap with cached one.
	 * Not recoverable.
	 *
	 * NOTE: Physical address can overlap, due to compression
	 */
	if (cache->offset + cache->len > offset) {
		WARN_ON(1);
		return -EINVAL;
	}

	/*
	 * Only merges fiemap extents if
	 * 1) Their logical addresses are continuous
	 *
	 * 2) Their physical addresses are continuous
	 *    So truly compressed (physical size smaller than logical size)
	 *    extents won't get merged with each other
	 *
	 * 3) Share same flags except FIEMAP_EXTENT_LAST
	 *    So regular extent won't get merged with prealloc extent
	 */
	if (cache->offset + cache->len  == offset &&
	    cache->phys + cache->len == phys  &&
	    (cache->flags & ~FIEMAP_EXTENT_LAST) ==
			(flags & ~FIEMAP_EXTENT_LAST)) {
		cache->len += len;
		cache->flags |= flags;
		goto try_submit_last;
	}

	/* Not mergeable, need to submit cached one */
	ret = fiemap_fill_next_extent(fieinfo, cache->offset, cache->phys,
				      cache->len, cache->flags);
	cache->cached = false;
	if (ret)
		return ret;
assign:
	cache->cached = true;
	cache->offset = offset;
	cache->phys = phys;
	cache->len = len;
	cache->flags = flags;
try_submit_last:
	if (cache->flags & FIEMAP_EXTENT_LAST) {
		ret = fiemap_fill_next_extent(fieinfo, cache->offset,
				cache->phys, cache->len, cache->flags);
		cache->cached = false;
	}
	return ret;
}

/*
 * Emit last fiemap cache
 *
 * The last fiemap cache may still be cached in the following case:
 * 0		      4k		    8k
 * |<- Fiemap range ->|
 * |<------------  First extent ----------->|
 *
 * In this case, the first extent range will be cached but not emitted.
 * So we must emit it before ending extent_fiemap().
 */
static int emit_last_fiemap_cache(struct fiemap_extent_info *fieinfo,
				  struct fiemap_cache *cache)
{
	int ret;

	if (!cache->cached)
		return 0;

	ret = fiemap_fill_next_extent(fieinfo, cache->offset, cache->phys,
				      cache->len, cache->flags);
	cache->cached = false;
	if (ret > 0)
		ret = 0;
	return ret;
}

int extent_fiemap(struct btrfs_inode *inode, struct fiemap_extent_info *fieinfo,
		  u64 start, u64 len)
{
	int ret = 0;
	u64 off;
	u64 max = start + len;
	u32 flags = 0;
	u32 found_type;
	u64 last;
	u64 last_for_get_extent = 0;
	u64 disko = 0;
	u64 isize = i_size_read(&inode->vfs_inode);
	struct btrfs_key found_key;
	struct extent_map *em = NULL;
	struct extent_state *cached_state = NULL;
	struct btrfs_path *path;
	struct btrfs_root *root = inode->root;
	struct fiemap_cache cache = { 0 };
	struct ulist *roots;
	struct ulist *tmp_ulist;
	int end = 0;
	u64 em_start = 0;
	u64 em_len = 0;
	u64 em_end = 0;

	if (len == 0)
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	roots = ulist_alloc(GFP_KERNEL);
	tmp_ulist = ulist_alloc(GFP_KERNEL);
	if (!roots || !tmp_ulist) {
		ret = -ENOMEM;
		goto out_free_ulist;
	}

	/*
	 * We can't initialize that to 'start' as this could miss extents due
	 * to extent item merging
	 */
	off = 0;
	start = round_down(start, btrfs_inode_sectorsize(inode));
	len = round_up(max, btrfs_inode_sectorsize(inode)) - start;

	/*
	 * lookup the last file extent.  We're not using i_size here
	 * because there might be preallocation past i_size
	 */
	ret = btrfs_lookup_file_extent(NULL, root, path, btrfs_ino(inode), -1,
				       0);
	if (ret < 0) {
		goto out_free_ulist;
	} else {
		WARN_ON(!ret);
		if (ret == 1)
			ret = 0;
	}

	path->slots[0]--;
	btrfs_item_key_to_cpu(path->nodes[0], &found_key, path->slots[0]);
	found_type = found_key.type;

	/* No extents, but there might be delalloc bits */
	if (found_key.objectid != btrfs_ino(inode) ||
	    found_type != BTRFS_EXTENT_DATA_KEY) {
		/* have to trust i_size as the end */
		last = (u64)-1;
		last_for_get_extent = isize;
	} else {
		/*
		 * remember the start of the last extent.  There are a
		 * bunch of different factors that go into the length of the
		 * extent, so its much less complex to remember where it started
		 */
		last = found_key.offset;
		last_for_get_extent = last + 1;
	}
	btrfs_release_path(path);

	/*
	 * we might have some extents allocated but more delalloc past those
	 * extents.  so, we trust isize unless the start of the last extent is
	 * beyond isize
	 */
	if (last < isize) {
		last = (u64)-1;
		last_for_get_extent = isize;
	}

	lock_extent_bits(&inode->io_tree, start, start + len - 1,
			 &cached_state);

	em = get_extent_skip_holes(inode, start, last_for_get_extent);
	if (!em)
		goto out;
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto out;
	}

	while (!end) {
		u64 offset_in_extent = 0;

		/* break if the extent we found is outside the range */
		if (em->start >= max || extent_map_end(em) < off)
			break;

		/*
		 * get_extent may return an extent that starts before our
		 * requested range.  We have to make sure the ranges
		 * we return to fiemap always move forward and don't
		 * overlap, so adjust the offsets here
		 */
		em_start = max(em->start, off);

		/*
		 * record the offset from the start of the extent
		 * for adjusting the disk offset below.  Only do this if the
		 * extent isn't compressed since our in ram offset may be past
		 * what we have actually allocated on disk.
		 */
		if (!test_bit(EXTENT_FLAG_COMPRESSED, &em->flags))
			offset_in_extent = em_start - em->start;
		em_end = extent_map_end(em);
		em_len = em_end - em_start;
		flags = 0;
		if (em->block_start < EXTENT_MAP_LAST_BYTE)
			disko = em->block_start + offset_in_extent;
		else
			disko = 0;

		/*
		 * bump off for our next call to get_extent
		 */
		off = extent_map_end(em);
		if (off >= max)
			end = 1;

		if (em->block_start == EXTENT_MAP_LAST_BYTE) {
			end = 1;
			flags |= FIEMAP_EXTENT_LAST;
		} else if (em->block_start == EXTENT_MAP_INLINE) {
			flags |= (FIEMAP_EXTENT_DATA_INLINE |
				  FIEMAP_EXTENT_NOT_ALIGNED);
		} else if (em->block_start == EXTENT_MAP_DELALLOC) {
			flags |= (FIEMAP_EXTENT_DELALLOC |
				  FIEMAP_EXTENT_UNKNOWN);
		} else if (fieinfo->fi_extents_max) {
			u64 bytenr = em->block_start -
				(em->start - em->orig_start);

			/*
			 * As btrfs supports shared space, this information
			 * can be exported to userspace tools via
			 * flag FIEMAP_EXTENT_SHARED.  If fi_extents_max == 0
			 * then we're just getting a count and we can skip the
			 * lookup stuff.
			 */
			ret = btrfs_check_shared(root, btrfs_ino(inode),
						 bytenr, roots, tmp_ulist);
			if (ret < 0)
				goto out_free;
			if (ret)
				flags |= FIEMAP_EXTENT_SHARED;
			ret = 0;
		}
		if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags))
			flags |= FIEMAP_EXTENT_ENCODED;
		if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
			flags |= FIEMAP_EXTENT_UNWRITTEN;

		free_extent_map(em);
		em = NULL;
		if ((em_start >= last) || em_len == (u64)-1 ||
		   (last == (u64)-1 && isize <= em_end)) {
			flags |= FIEMAP_EXTENT_LAST;
			end = 1;
		}

		/* now scan forward to see if this is really the last extent. */
		em = get_extent_skip_holes(inode, off, last_for_get_extent);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			goto out;
		}
		if (!em) {
			flags |= FIEMAP_EXTENT_LAST;
			end = 1;
		}
		ret = emit_fiemap_extent(fieinfo, &cache, em_start, disko,
					   em_len, flags);
		if (ret) {
			if (ret == 1)
				ret = 0;
			goto out_free;
		}
	}
out_free:
	if (!ret)
		ret = emit_last_fiemap_cache(fieinfo, &cache);
	free_extent_map(em);
out:
	unlock_extent_cached(&inode->io_tree, start, start + len - 1,
			     &cached_state);

out_free_ulist:
	btrfs_free_path(path);
	ulist_free(roots);
	ulist_free(tmp_ulist);
	return ret;
}

static void __free_extent_buffer(struct extent_buffer *eb)
{
	kmem_cache_free(extent_buffer_cache, eb);
}

int extent_buffer_under_io(const struct extent_buffer *eb)
{
	return (atomic_read(&eb->io_pages) ||
		test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags) ||
		test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
}

static bool page_range_has_eb(struct btrfs_fs_info *fs_info, struct page *page)
{
	struct btrfs_subpage *subpage;

	lockdep_assert_held(&page->mapping->private_lock);

	if (PagePrivate(page)) {
		subpage = (struct btrfs_subpage *)page->private;
		if (atomic_read(&subpage->eb_refs))
			return true;
		/*
		 * Even there is no eb refs here, we may still have
		 * end_page_read() call relying on page::private.
		 */
		if (atomic_read(&subpage->readers))
			return true;
	}
	return false;
}

static void detach_extent_buffer_page(struct extent_buffer *eb, struct page *page)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	const bool mapped = !test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags);

	/*
	 * For mapped eb, we're going to change the page private, which should
	 * be done under the private_lock.
	 */
	if (mapped)
		spin_lock(&page->mapping->private_lock);

	if (!PagePrivate(page)) {
		if (mapped)
			spin_unlock(&page->mapping->private_lock);
		return;
	}

	if (fs_info->sectorsize == PAGE_SIZE) {
		/*
		 * We do this since we'll remove the pages after we've
		 * removed the eb from the radix tree, so we could race
		 * and have this page now attached to the new eb.  So
		 * only clear page_private if it's still connected to
		 * this eb.
		 */
		if (PagePrivate(page) &&
		    page->private == (unsigned long)eb) {
			BUG_ON(test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
			BUG_ON(PageDirty(page));
			BUG_ON(PageWriteback(page));
			/*
			 * We need to make sure we haven't be attached
			 * to a new eb.
			 */
			detach_page_private(page);
		}
		if (mapped)
			spin_unlock(&page->mapping->private_lock);
		return;
	}

	/*
	 * For subpage, we can have dummy eb with page private.  In this case,
	 * we can directly detach the private as such page is only attached to
	 * one dummy eb, no sharing.
	 */
	if (!mapped) {
		btrfs_detach_subpage(fs_info, page);
		return;
	}

	btrfs_page_dec_eb_refs(fs_info, page);

	/*
	 * We can only detach the page private if there are no other ebs in the
	 * page range and no unfinished IO.
	 */
	if (!page_range_has_eb(fs_info, page))
		btrfs_detach_subpage(fs_info, page);

	spin_unlock(&page->mapping->private_lock);
}

/* Release all pages attached to the extent buffer */
static void btrfs_release_extent_buffer_pages(struct extent_buffer *eb)
{
	int i;
	int num_pages;

	ASSERT(!extent_buffer_under_io(eb));

	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++) {
		struct page *page = eb->pages[i];

		if (!page)
			continue;

		detach_extent_buffer_page(eb, page);

		/* One for when we allocated the page */
		put_page(page);
	}
}

/*
 * Helper for releasing the extent buffer.
 */
static inline void btrfs_release_extent_buffer(struct extent_buffer *eb)
{
	btrfs_release_extent_buffer_pages(eb);
	btrfs_leak_debug_del(&eb->fs_info->eb_leak_lock, &eb->leak_list);
	__free_extent_buffer(eb);
}

static struct extent_buffer *
__alloc_extent_buffer(struct btrfs_fs_info *fs_info, u64 start,
		      unsigned long len)
{
	struct extent_buffer *eb = NULL;

	eb = kmem_cache_zalloc(extent_buffer_cache, GFP_NOFS|__GFP_NOFAIL);
	eb->start = start;
	eb->len = len;
	eb->fs_info = fs_info;
	eb->bflags = 0;
	init_rwsem(&eb->lock);

	btrfs_leak_debug_add(&fs_info->eb_leak_lock, &eb->leak_list,
			     &fs_info->allocated_ebs);
	INIT_LIST_HEAD(&eb->release_list);

	spin_lock_init(&eb->refs_lock);
	atomic_set(&eb->refs, 1);
	atomic_set(&eb->io_pages, 0);

	ASSERT(len <= BTRFS_MAX_METADATA_BLOCKSIZE);

	return eb;
}

struct extent_buffer *btrfs_clone_extent_buffer(const struct extent_buffer *src)
{
	int i;
	struct page *p;
	struct extent_buffer *new;
	int num_pages = num_extent_pages(src);

	new = __alloc_extent_buffer(src->fs_info, src->start, src->len);
	if (new == NULL)
		return NULL;

	/*
	 * Set UNMAPPED before calling btrfs_release_extent_buffer(), as
	 * btrfs_release_extent_buffer() have different behavior for
	 * UNMAPPED subpage extent buffer.
	 */
	set_bit(EXTENT_BUFFER_UNMAPPED, &new->bflags);

	for (i = 0; i < num_pages; i++) {
		int ret;

		p = alloc_page(GFP_NOFS);
		if (!p) {
			btrfs_release_extent_buffer(new);
			return NULL;
		}
		ret = attach_extent_buffer_page(new, p, NULL);
		if (ret < 0) {
			put_page(p);
			btrfs_release_extent_buffer(new);
			return NULL;
		}
		WARN_ON(PageDirty(p));
		new->pages[i] = p;
		copy_page(page_address(p), page_address(src->pages[i]));
	}
	set_extent_buffer_uptodate(new);

	return new;
}

struct extent_buffer *__alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						  u64 start, unsigned long len)
{
	struct extent_buffer *eb;
	int num_pages;
	int i;

	eb = __alloc_extent_buffer(fs_info, start, len);
	if (!eb)
		return NULL;

	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++) {
		int ret;

		eb->pages[i] = alloc_page(GFP_NOFS);
		if (!eb->pages[i])
			goto err;
		ret = attach_extent_buffer_page(eb, eb->pages[i], NULL);
		if (ret < 0)
			goto err;
	}
	set_extent_buffer_uptodate(eb);
	btrfs_set_header_nritems(eb, 0);
	set_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags);

	return eb;
err:
	for (; i > 0; i--) {
		detach_extent_buffer_page(eb, eb->pages[i - 1]);
		__free_page(eb->pages[i - 1]);
	}
	__free_extent_buffer(eb);
	return NULL;
}

struct extent_buffer *alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						u64 start)
{
	return __alloc_dummy_extent_buffer(fs_info, start, fs_info->nodesize);
}

static void check_buffer_tree_ref(struct extent_buffer *eb)
{
	int refs;
	/*
	 * The TREE_REF bit is first set when the extent_buffer is added
	 * to the radix tree. It is also reset, if unset, when a new reference
	 * is created by find_extent_buffer.
	 *
	 * It is only cleared in two cases: freeing the last non-tree
	 * reference to the extent_buffer when its STALE bit is set or
	 * calling releasepage when the tree reference is the only reference.
	 *
	 * In both cases, care is taken to ensure that the extent_buffer's
	 * pages are not under io. However, releasepage can be concurrently
	 * called with creating new references, which is prone to race
	 * conditions between the calls to check_buffer_tree_ref in those
	 * codepaths and clearing TREE_REF in try_release_extent_buffer.
	 *
	 * The actual lifetime of the extent_buffer in the radix tree is
	 * adequately protected by the refcount, but the TREE_REF bit and
	 * its corresponding reference are not. To protect against this
	 * class of races, we call check_buffer_tree_ref from the codepaths
	 * which trigger io after they set eb->io_pages. Note that once io is
	 * initiated, TREE_REF can no longer be cleared, so that is the
	 * moment at which any such race is best fixed.
	 */
	refs = atomic_read(&eb->refs);
	if (refs >= 2 && test_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags))
		return;

	spin_lock(&eb->refs_lock);
	if (!test_and_set_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags))
		atomic_inc(&eb->refs);
	spin_unlock(&eb->refs_lock);
}

static void mark_extent_buffer_accessed(struct extent_buffer *eb,
		struct page *accessed)
{
	int num_pages, i;

	check_buffer_tree_ref(eb);

	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++) {
		struct page *p = eb->pages[i];

		if (p != accessed)
			mark_page_accessed(p);
	}
}

struct extent_buffer *find_extent_buffer(struct btrfs_fs_info *fs_info,
					 u64 start)
{
	struct extent_buffer *eb;

	eb = find_extent_buffer_nolock(fs_info, start);
	if (!eb)
		return NULL;
	/*
	 * Lock our eb's refs_lock to avoid races with free_extent_buffer().
	 * When we get our eb it might be flagged with EXTENT_BUFFER_STALE and
	 * another task running free_extent_buffer() might have seen that flag
	 * set, eb->refs == 2, that the buffer isn't under IO (dirty and
	 * writeback flags not set) and it's still in the tree (flag
	 * EXTENT_BUFFER_TREE_REF set), therefore being in the process of
	 * decrementing the extent buffer's reference count twice.  So here we
	 * could race and increment the eb's reference count, clear its stale
	 * flag, mark it as dirty and drop our reference before the other task
	 * finishes executing free_extent_buffer, which would later result in
	 * an attempt to free an extent buffer that is dirty.
	 */
	if (test_bit(EXTENT_BUFFER_STALE, &eb->bflags)) {
		spin_lock(&eb->refs_lock);
		spin_unlock(&eb->refs_lock);
	}
	mark_extent_buffer_accessed(eb, NULL);
	return eb;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
struct extent_buffer *alloc_test_extent_buffer(struct btrfs_fs_info *fs_info,
					u64 start)
{
	struct extent_buffer *eb, *exists = NULL;
	int ret;

	eb = find_extent_buffer(fs_info, start);
	if (eb)
		return eb;
	eb = alloc_dummy_extent_buffer(fs_info, start);
	if (!eb)
		return ERR_PTR(-ENOMEM);
	eb->fs_info = fs_info;
again:
	ret = radix_tree_preload(GFP_NOFS);
	if (ret) {
		exists = ERR_PTR(ret);
		goto free_eb;
	}
	spin_lock(&fs_info->buffer_lock);
	ret = radix_tree_insert(&fs_info->buffer_radix,
				start >> fs_info->sectorsize_bits, eb);
	spin_unlock(&fs_info->buffer_lock);
	radix_tree_preload_end();
	if (ret == -EEXIST) {
		exists = find_extent_buffer(fs_info, start);
		if (exists)
			goto free_eb;
		else
			goto again;
	}
	check_buffer_tree_ref(eb);
	set_bit(EXTENT_BUFFER_IN_TREE, &eb->bflags);

	return eb;
free_eb:
	btrfs_release_extent_buffer(eb);
	return exists;
}
#endif

static struct extent_buffer *grab_extent_buffer(
		struct btrfs_fs_info *fs_info, struct page *page)
{
	struct extent_buffer *exists;

	/*
	 * For subpage case, we completely rely on radix tree to ensure we
	 * don't try to insert two ebs for the same bytenr.  So here we always
	 * return NULL and just continue.
	 */
	if (fs_info->sectorsize < PAGE_SIZE)
		return NULL;

	/* Page not yet attached to an extent buffer */
	if (!PagePrivate(page))
		return NULL;

	/*
	 * We could have already allocated an eb for this page and attached one
	 * so lets see if we can get a ref on the existing eb, and if we can we
	 * know it's good and we can just return that one, else we know we can
	 * just overwrite page->private.
	 */
	exists = (struct extent_buffer *)page->private;
	if (atomic_inc_not_zero(&exists->refs))
		return exists;

	WARN_ON(PageDirty(page));
	detach_page_private(page);
	return NULL;
}

struct extent_buffer *alloc_extent_buffer(struct btrfs_fs_info *fs_info,
					  u64 start, u64 owner_root, int level)
{
	unsigned long len = fs_info->nodesize;
	int num_pages;
	int i;
	unsigned long index = start >> PAGE_SHIFT;
	struct extent_buffer *eb;
	struct extent_buffer *exists = NULL;
	struct page *p;
	struct address_space *mapping = fs_info->btree_inode->i_mapping;
	int uptodate = 1;
	int ret;

	if (!IS_ALIGNED(start, fs_info->sectorsize)) {
		btrfs_err(fs_info, "bad tree block start %llu", start);
		return ERR_PTR(-EINVAL);
	}

#if BITS_PER_LONG == 32
	if (start >= MAX_LFS_FILESIZE) {
		btrfs_err_rl(fs_info,
		"extent buffer %llu is beyond 32bit page cache limit", start);
		btrfs_err_32bit_limit(fs_info);
		return ERR_PTR(-EOVERFLOW);
	}
	if (start >= BTRFS_32BIT_EARLY_WARN_THRESHOLD)
		btrfs_warn_32bit_limit(fs_info);
#endif

	if (fs_info->sectorsize < PAGE_SIZE &&
	    offset_in_page(start) + len > PAGE_SIZE) {
		btrfs_err(fs_info,
		"tree block crosses page boundary, start %llu nodesize %lu",
			  start, len);
		return ERR_PTR(-EINVAL);
	}

	eb = find_extent_buffer(fs_info, start);
	if (eb)
		return eb;

	eb = __alloc_extent_buffer(fs_info, start, len);
	if (!eb)
		return ERR_PTR(-ENOMEM);
	btrfs_set_buffer_lockdep_class(owner_root, eb, level);

	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++, index++) {
		struct btrfs_subpage *prealloc = NULL;

		p = find_or_create_page(mapping, index, GFP_NOFS|__GFP_NOFAIL);
		if (!p) {
			exists = ERR_PTR(-ENOMEM);
			goto free_eb;
		}

		/*
		 * Preallocate page->private for subpage case, so that we won't
		 * allocate memory with private_lock hold.  The memory will be
		 * freed by attach_extent_buffer_page() or freed manually if
		 * we exit earlier.
		 *
		 * Although we have ensured one subpage eb can only have one
		 * page, but it may change in the future for 16K page size
		 * support, so we still preallocate the memory in the loop.
		 */
		if (fs_info->sectorsize < PAGE_SIZE) {
			prealloc = btrfs_alloc_subpage(fs_info, BTRFS_SUBPAGE_METADATA);
			if (IS_ERR(prealloc)) {
				ret = PTR_ERR(prealloc);
				unlock_page(p);
				put_page(p);
				exists = ERR_PTR(ret);
				goto free_eb;
			}
		}

		spin_lock(&mapping->private_lock);
		exists = grab_extent_buffer(fs_info, p);
		if (exists) {
			spin_unlock(&mapping->private_lock);
			unlock_page(p);
			put_page(p);
			mark_extent_buffer_accessed(exists, p);
			btrfs_free_subpage(prealloc);
			goto free_eb;
		}
		/* Should not fail, as we have preallocated the memory */
		ret = attach_extent_buffer_page(eb, p, prealloc);
		ASSERT(!ret);
		/*
		 * To inform we have extra eb under allocation, so that
		 * detach_extent_buffer_page() won't release the page private
		 * when the eb hasn't yet been inserted into radix tree.
		 *
		 * The ref will be decreased when the eb released the page, in
		 * detach_extent_buffer_page().
		 * Thus needs no special handling in error path.
		 */
		btrfs_page_inc_eb_refs(fs_info, p);
		spin_unlock(&mapping->private_lock);

		WARN_ON(btrfs_page_test_dirty(fs_info, p, eb->start, eb->len));
		eb->pages[i] = p;
		if (!PageUptodate(p))
			uptodate = 0;

		/*
		 * We can't unlock the pages just yet since the extent buffer
		 * hasn't been properly inserted in the radix tree, this
		 * opens a race with btree_releasepage which can free a page
		 * while we are still filling in all pages for the buffer and
		 * we could crash.
		 */
	}
	if (uptodate)
		set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
again:
	ret = radix_tree_preload(GFP_NOFS);
	if (ret) {
		exists = ERR_PTR(ret);
		goto free_eb;
	}

	spin_lock(&fs_info->buffer_lock);
	ret = radix_tree_insert(&fs_info->buffer_radix,
				start >> fs_info->sectorsize_bits, eb);
	spin_unlock(&fs_info->buffer_lock);
	radix_tree_preload_end();
	if (ret == -EEXIST) {
		exists = find_extent_buffer(fs_info, start);
		if (exists)
			goto free_eb;
		else
			goto again;
	}
	/* add one reference for the tree */
	check_buffer_tree_ref(eb);
	set_bit(EXTENT_BUFFER_IN_TREE, &eb->bflags);

	/*
	 * Now it's safe to unlock the pages because any calls to
	 * btree_releasepage will correctly detect that a page belongs to a
	 * live buffer and won't free them prematurely.
	 */
	for (i = 0; i < num_pages; i++)
		unlock_page(eb->pages[i]);
	return eb;

free_eb:
	WARN_ON(!atomic_dec_and_test(&eb->refs));
	for (i = 0; i < num_pages; i++) {
		if (eb->pages[i])
			unlock_page(eb->pages[i]);
	}

	btrfs_release_extent_buffer(eb);
	return exists;
}

static inline void btrfs_release_extent_buffer_rcu(struct rcu_head *head)
{
	struct extent_buffer *eb =
			container_of(head, struct extent_buffer, rcu_head);

	__free_extent_buffer(eb);
}

static int release_extent_buffer(struct extent_buffer *eb)
	__releases(&eb->refs_lock)
{
	lockdep_assert_held(&eb->refs_lock);

	WARN_ON(atomic_read(&eb->refs) == 0);
	if (atomic_dec_and_test(&eb->refs)) {
		if (test_and_clear_bit(EXTENT_BUFFER_IN_TREE, &eb->bflags)) {
			struct btrfs_fs_info *fs_info = eb->fs_info;

			spin_unlock(&eb->refs_lock);

			spin_lock(&fs_info->buffer_lock);
			radix_tree_delete(&fs_info->buffer_radix,
					  eb->start >> fs_info->sectorsize_bits);
			spin_unlock(&fs_info->buffer_lock);
		} else {
			spin_unlock(&eb->refs_lock);
		}

		btrfs_leak_debug_del(&eb->fs_info->eb_leak_lock, &eb->leak_list);
		/* Should be safe to release our pages at this point */
		btrfs_release_extent_buffer_pages(eb);
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
		if (unlikely(test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags))) {
			__free_extent_buffer(eb);
			return 1;
		}
#endif
		call_rcu(&eb->rcu_head, btrfs_release_extent_buffer_rcu);
		return 1;
	}
	spin_unlock(&eb->refs_lock);

	return 0;
}

void free_extent_buffer(struct extent_buffer *eb)
{
	int refs;
	int old;
	if (!eb)
		return;

	while (1) {
		refs = atomic_read(&eb->refs);
		if ((!test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags) && refs <= 3)
		    || (test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags) &&
			refs == 1))
			break;
		old = atomic_cmpxchg(&eb->refs, refs, refs - 1);
		if (old == refs)
			return;
	}

	spin_lock(&eb->refs_lock);
	if (atomic_read(&eb->refs) == 2 &&
	    test_bit(EXTENT_BUFFER_STALE, &eb->bflags) &&
	    !extent_buffer_under_io(eb) &&
	    test_and_clear_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags))
		atomic_dec(&eb->refs);

	/*
	 * I know this is terrible, but it's temporary until we stop tracking
	 * the uptodate bits and such for the extent buffers.
	 */
	release_extent_buffer(eb);
}

void free_extent_buffer_stale(struct extent_buffer *eb)
{
	if (!eb)
		return;

	spin_lock(&eb->refs_lock);
	set_bit(EXTENT_BUFFER_STALE, &eb->bflags);

	if (atomic_read(&eb->refs) == 2 && !extent_buffer_under_io(eb) &&
	    test_and_clear_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags))
		atomic_dec(&eb->refs);
	release_extent_buffer(eb);
}

static void btree_clear_page_dirty(struct page *page)
{
	ASSERT(PageDirty(page));
	ASSERT(PageLocked(page));
	clear_page_dirty_for_io(page);
	xa_lock_irq(&page->mapping->i_pages);
	if (!PageDirty(page))
		__xa_clear_mark(&page->mapping->i_pages,
				page_index(page), PAGECACHE_TAG_DIRTY);
	xa_unlock_irq(&page->mapping->i_pages);
}

static void clear_subpage_extent_buffer_dirty(const struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct page *page = eb->pages[0];
	bool last;

	/* btree_clear_page_dirty() needs page locked */
	lock_page(page);
	last = btrfs_subpage_clear_and_test_dirty(fs_info, page, eb->start,
						  eb->len);
	if (last)
		btree_clear_page_dirty(page);
	unlock_page(page);
	WARN_ON(atomic_read(&eb->refs) == 0);
}

void clear_extent_buffer_dirty(const struct extent_buffer *eb)
{
	int i;
	int num_pages;
	struct page *page;

	if (eb->fs_info->sectorsize < PAGE_SIZE)
		return clear_subpage_extent_buffer_dirty(eb);

	num_pages = num_extent_pages(eb);

	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		if (!PageDirty(page))
			continue;
		lock_page(page);
		btree_clear_page_dirty(page);
		ClearPageError(page);
		unlock_page(page);
	}
	WARN_ON(atomic_read(&eb->refs) == 0);
}

bool set_extent_buffer_dirty(struct extent_buffer *eb)
{
	int i;
	int num_pages;
	bool was_dirty;

	check_buffer_tree_ref(eb);

	was_dirty = test_and_set_bit(EXTENT_BUFFER_DIRTY, &eb->bflags);

	num_pages = num_extent_pages(eb);
	WARN_ON(atomic_read(&eb->refs) == 0);
	WARN_ON(!test_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags));

	if (!was_dirty) {
		bool subpage = eb->fs_info->sectorsize < PAGE_SIZE;

		/*
		 * For subpage case, we can have other extent buffers in the
		 * same page, and in clear_subpage_extent_buffer_dirty() we
		 * have to clear page dirty without subpage lock held.
		 * This can cause race where our page gets dirty cleared after
		 * we just set it.
		 *
		 * Thankfully, clear_subpage_extent_buffer_dirty() has locked
		 * its page for other reasons, we can use page lock to prevent
		 * the above race.
		 */
		if (subpage)
			lock_page(eb->pages[0]);
		for (i = 0; i < num_pages; i++)
			btrfs_page_set_dirty(eb->fs_info, eb->pages[i],
					     eb->start, eb->len);
		if (subpage)
			unlock_page(eb->pages[0]);
	}
#ifdef CONFIG_BTRFS_DEBUG
	for (i = 0; i < num_pages; i++)
		ASSERT(PageDirty(eb->pages[i]));
#endif

	return was_dirty;
}

void clear_extent_buffer_uptodate(struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct page *page;
	int num_pages;
	int i;

	clear_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		if (page)
			btrfs_page_clear_uptodate(fs_info, page,
						  eb->start, eb->len);
	}
}

void set_extent_buffer_uptodate(struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct page *page;
	int num_pages;
	int i;

	set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		btrfs_page_set_uptodate(fs_info, page, eb->start, eb->len);
	}
}

static int read_extent_buffer_subpage(struct extent_buffer *eb, int wait,
				      int mirror_num)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct extent_io_tree *io_tree;
	struct page *page = eb->pages[0];
	struct btrfs_bio_ctrl bio_ctrl = { 0 };
	int ret = 0;

	ASSERT(!test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags));
	ASSERT(PagePrivate(page));
	io_tree = &BTRFS_I(fs_info->btree_inode)->io_tree;

	if (wait == WAIT_NONE) {
		if (!try_lock_extent(io_tree, eb->start, eb->start + eb->len - 1))
			return -EAGAIN;
	} else {
		ret = lock_extent(io_tree, eb->start, eb->start + eb->len - 1);
		if (ret < 0)
			return ret;
	}

	ret = 0;
	if (test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags) ||
	    PageUptodate(page) ||
	    btrfs_subpage_test_uptodate(fs_info, page, eb->start, eb->len)) {
		set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
		unlock_extent(io_tree, eb->start, eb->start + eb->len - 1);
		return ret;
	}

	clear_bit(EXTENT_BUFFER_READ_ERR, &eb->bflags);
	eb->read_mirror = 0;
	atomic_set(&eb->io_pages, 1);
	check_buffer_tree_ref(eb);
	btrfs_subpage_clear_error(fs_info, page, eb->start, eb->len);

	btrfs_subpage_start_reader(fs_info, page, eb->start, eb->len);
	ret = submit_extent_page(REQ_OP_READ | REQ_META, NULL, &bio_ctrl,
				 page, eb->start, eb->len,
				 eb->start - page_offset(page),
				 end_bio_extent_readpage, mirror_num, 0,
				 true);
	if (ret) {
		/*
		 * In the endio function, if we hit something wrong we will
		 * increase the io_pages, so here we need to decrease it for
		 * error path.
		 */
		atomic_dec(&eb->io_pages);
	}
	if (bio_ctrl.bio) {
		int tmp;

		tmp = submit_one_bio(bio_ctrl.bio, mirror_num, 0);
		bio_ctrl.bio = NULL;
		if (tmp < 0)
			return tmp;
	}
	if (ret || wait != WAIT_COMPLETE)
		return ret;

	wait_extent_bit(io_tree, eb->start, eb->start + eb->len - 1, EXTENT_LOCKED);
	if (!test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags))
		ret = -EIO;
	return ret;
}

int read_extent_buffer_pages(struct extent_buffer *eb, int wait, int mirror_num)
{
	int i;
	struct page *page;
	int err;
	int ret = 0;
	int locked_pages = 0;
	int all_uptodate = 1;
	int num_pages;
	unsigned long num_reads = 0;
	struct btrfs_bio_ctrl bio_ctrl = { 0 };

	if (test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags))
		return 0;

	/*
	 * We could have had EXTENT_BUFFER_UPTODATE cleared by the write
	 * operation, which could potentially still be in flight.  In this case
	 * we simply want to return an error.
	 */
	if (unlikely(test_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags)))
		return -EIO;

	if (eb->fs_info->sectorsize < PAGE_SIZE)
		return read_extent_buffer_subpage(eb, wait, mirror_num);

	num_pages = num_extent_pages(eb);
	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		if (wait == WAIT_NONE) {
			/*
			 * WAIT_NONE is only utilized by readahead. If we can't
			 * acquire the lock atomically it means either the eb
			 * is being read out or under modification.
			 * Either way the eb will be or has been cached,
			 * readahead can exit safely.
			 */
			if (!trylock_page(page))
				goto unlock_exit;
		} else {
			lock_page(page);
		}
		locked_pages++;
	}
	/*
	 * We need to firstly lock all pages to make sure that
	 * the uptodate bit of our pages won't be affected by
	 * clear_extent_buffer_uptodate().
	 */
	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		if (!PageUptodate(page)) {
			num_reads++;
			all_uptodate = 0;
		}
	}

	if (all_uptodate) {
		set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
		goto unlock_exit;
	}

	clear_bit(EXTENT_BUFFER_READ_ERR, &eb->bflags);
	eb->read_mirror = 0;
	atomic_set(&eb->io_pages, num_reads);
	/*
	 * It is possible for releasepage to clear the TREE_REF bit before we
	 * set io_pages. See check_buffer_tree_ref for a more detailed comment.
	 */
	check_buffer_tree_ref(eb);
	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];

		if (!PageUptodate(page)) {
			if (ret) {
				atomic_dec(&eb->io_pages);
				unlock_page(page);
				continue;
			}

			ClearPageError(page);
			err = submit_extent_page(REQ_OP_READ | REQ_META, NULL,
					 &bio_ctrl, page, page_offset(page),
					 PAGE_SIZE, 0, end_bio_extent_readpage,
					 mirror_num, 0, false);
			if (err) {
				/*
				 * We failed to submit the bio so it's the
				 * caller's responsibility to perform cleanup
				 * i.e unlock page/set error bit.
				 */
				ret = err;
				SetPageError(page);
				unlock_page(page);
				atomic_dec(&eb->io_pages);
			}
		} else {
			unlock_page(page);
		}
	}

	if (bio_ctrl.bio) {
		err = submit_one_bio(bio_ctrl.bio, mirror_num, bio_ctrl.bio_flags);
		bio_ctrl.bio = NULL;
		if (err)
			return err;
	}

	if (ret || wait != WAIT_COMPLETE)
		return ret;

	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		wait_on_page_locked(page);
		if (!PageUptodate(page))
			ret = -EIO;
	}

	return ret;

unlock_exit:
	while (locked_pages > 0) {
		locked_pages--;
		page = eb->pages[locked_pages];
		unlock_page(page);
	}
	return ret;
}

static bool report_eb_range(const struct extent_buffer *eb, unsigned long start,
			    unsigned long len)
{
	btrfs_warn(eb->fs_info,
		"access to eb bytenr %llu len %lu out of range start %lu len %lu",
		eb->start, eb->len, start, len);
	WARN_ON(IS_ENABLED(CONFIG_BTRFS_DEBUG));

	return true;
}

/*
 * Check if the [start, start + len) range is valid before reading/writing
 * the eb.
 * NOTE: @start and @len are offset inside the eb, not logical address.
 *
 * Caller should not touch the dst/src memory if this function returns error.
 */
static inline int check_eb_range(const struct extent_buffer *eb,
				 unsigned long start, unsigned long len)
{
	unsigned long offset;

	/* start, start + len should not go beyond eb->len nor overflow */
	if (unlikely(check_add_overflow(start, len, &offset) || offset > eb->len))
		return report_eb_range(eb, start, len);

	return false;
}

void read_extent_buffer(const struct extent_buffer *eb, void *dstv,
			unsigned long start, unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *dst = (char *)dstv;
	unsigned long i = get_eb_page_index(start);

	if (check_eb_range(eb, start, len))
		return;

	offset = get_eb_offset_in_page(eb, start);

	while (len > 0) {
		page = eb->pages[i];

		cur = min(len, (PAGE_SIZE - offset));
		kaddr = page_address(page);
		memcpy(dst, kaddr + offset, cur);

		dst += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}

int read_extent_buffer_to_user_nofault(const struct extent_buffer *eb,
				       void __user *dstv,
				       unsigned long start, unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char __user *dst = (char __user *)dstv;
	unsigned long i = get_eb_page_index(start);
	int ret = 0;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = get_eb_offset_in_page(eb, start);

	while (len > 0) {
		page = eb->pages[i];

		cur = min(len, (PAGE_SIZE - offset));
		kaddr = page_address(page);
		if (copy_to_user_nofault(dst, kaddr + offset, cur)) {
			ret = -EFAULT;
			break;
		}

		dst += cur;
		len -= cur;
		offset = 0;
		i++;
	}

	return ret;
}

int memcmp_extent_buffer(const struct extent_buffer *eb, const void *ptrv,
			 unsigned long start, unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *ptr = (char *)ptrv;
	unsigned long i = get_eb_page_index(start);
	int ret = 0;

	if (check_eb_range(eb, start, len))
		return -EINVAL;

	offset = get_eb_offset_in_page(eb, start);

	while (len > 0) {
		page = eb->pages[i];

		cur = min(len, (PAGE_SIZE - offset));

		kaddr = page_address(page);
		ret = memcmp(ptr, kaddr + offset, cur);
		if (ret)
			break;

		ptr += cur;
		len -= cur;
		offset = 0;
		i++;
	}
	return ret;
}

/*
 * Check that the extent buffer is uptodate.
 *
 * For regular sector size == PAGE_SIZE case, check if @page is uptodate.
 * For subpage case, check if the range covered by the eb has EXTENT_UPTODATE.
 */
static void assert_eb_page_uptodate(const struct extent_buffer *eb,
				    struct page *page)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;

	if (fs_info->sectorsize < PAGE_SIZE) {
		bool uptodate;

		uptodate = btrfs_subpage_test_uptodate(fs_info, page,
						       eb->start, eb->len);
		WARN_ON(!uptodate);
	} else {
		WARN_ON(!PageUptodate(page));
	}
}

void write_extent_buffer_chunk_tree_uuid(const struct extent_buffer *eb,
		const void *srcv)
{
	char *kaddr;

	assert_eb_page_uptodate(eb, eb->pages[0]);
	kaddr = page_address(eb->pages[0]) +
		get_eb_offset_in_page(eb, offsetof(struct btrfs_header,
						   chunk_tree_uuid));
	memcpy(kaddr, srcv, BTRFS_FSID_SIZE);
}

void write_extent_buffer_fsid(const struct extent_buffer *eb, const void *srcv)
{
	char *kaddr;

	assert_eb_page_uptodate(eb, eb->pages[0]);
	kaddr = page_address(eb->pages[0]) +
		get_eb_offset_in_page(eb, offsetof(struct btrfs_header, fsid));
	memcpy(kaddr, srcv, BTRFS_FSID_SIZE);
}

void write_extent_buffer(const struct extent_buffer *eb, const void *srcv,
			 unsigned long start, unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *src = (char *)srcv;
	unsigned long i = get_eb_page_index(start);

	WARN_ON(test_bit(EXTENT_BUFFER_NO_CHECK, &eb->bflags));

	if (check_eb_range(eb, start, len))
		return;

	offset = get_eb_offset_in_page(eb, start);

	while (len > 0) {
		page = eb->pages[i];
		assert_eb_page_uptodate(eb, page);

		cur = min(len, PAGE_SIZE - offset);
		kaddr = page_address(page);
		memcpy(kaddr + offset, src, cur);

		src += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}

void memzero_extent_buffer(const struct extent_buffer *eb, unsigned long start,
		unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	unsigned long i = get_eb_page_index(start);

	if (check_eb_range(eb, start, len))
		return;

	offset = get_eb_offset_in_page(eb, start);

	while (len > 0) {
		page = eb->pages[i];
		assert_eb_page_uptodate(eb, page);

		cur = min(len, PAGE_SIZE - offset);
		kaddr = page_address(page);
		memset(kaddr + offset, 0, cur);

		len -= cur;
		offset = 0;
		i++;
	}
}

void copy_extent_buffer_full(const struct extent_buffer *dst,
			     const struct extent_buffer *src)
{
	int i;
	int num_pages;

	ASSERT(dst->len == src->len);

	if (dst->fs_info->sectorsize == PAGE_SIZE) {
		num_pages = num_extent_pages(dst);
		for (i = 0; i < num_pages; i++)
			copy_page(page_address(dst->pages[i]),
				  page_address(src->pages[i]));
	} else {
		size_t src_offset = get_eb_offset_in_page(src, 0);
		size_t dst_offset = get_eb_offset_in_page(dst, 0);

		ASSERT(src->fs_info->sectorsize < PAGE_SIZE);
		memcpy(page_address(dst->pages[0]) + dst_offset,
		       page_address(src->pages[0]) + src_offset,
		       src->len);
	}
}

void copy_extent_buffer(const struct extent_buffer *dst,
			const struct extent_buffer *src,
			unsigned long dst_offset, unsigned long src_offset,
			unsigned long len)
{
	u64 dst_len = dst->len;
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	unsigned long i = get_eb_page_index(dst_offset);

	if (check_eb_range(dst, dst_offset, len) ||
	    check_eb_range(src, src_offset, len))
		return;

	WARN_ON(src->len != dst_len);

	offset = get_eb_offset_in_page(dst, dst_offset);

	while (len > 0) {
		page = dst->pages[i];
		assert_eb_page_uptodate(dst, page);

		cur = min(len, (unsigned long)(PAGE_SIZE - offset));

		kaddr = page_address(page);
		read_extent_buffer(src, kaddr + offset, src_offset, cur);

		src_offset += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}

/*
 * eb_bitmap_offset() - calculate the page and offset of the byte containing the
 * given bit number
 * @eb: the extent buffer
 * @start: offset of the bitmap item in the extent buffer
 * @nr: bit number
 * @page_index: return index of the page in the extent buffer that contains the
 * given bit number
 * @page_offset: return offset into the page given by page_index
 *
 * This helper hides the ugliness of finding the byte in an extent buffer which
 * contains a given bit.
 */
static inline void eb_bitmap_offset(const struct extent_buffer *eb,
				    unsigned long start, unsigned long nr,
				    unsigned long *page_index,
				    size_t *page_offset)
{
	size_t byte_offset = BIT_BYTE(nr);
	size_t offset;

	/*
	 * The byte we want is the offset of the extent buffer + the offset of
	 * the bitmap item in the extent buffer + the offset of the byte in the
	 * bitmap item.
	 */
	offset = start + offset_in_page(eb->start) + byte_offset;

	*page_index = offset >> PAGE_SHIFT;
	*page_offset = offset_in_page(offset);
}

/**
 * extent_buffer_test_bit - determine whether a bit in a bitmap item is set
 * @eb: the extent buffer
 * @start: offset of the bitmap item in the extent buffer
 * @nr: bit number to test
 */
int extent_buffer_test_bit(const struct extent_buffer *eb, unsigned long start,
			   unsigned long nr)
{
	u8 *kaddr;
	struct page *page;
	unsigned long i;
	size_t offset;

	eb_bitmap_offset(eb, start, nr, &i, &offset);
	page = eb->pages[i];
	assert_eb_page_uptodate(eb, page);
	kaddr = page_address(page);
	return 1U & (kaddr[offset] >> (nr & (BITS_PER_BYTE - 1)));
}

/**
 * extent_buffer_bitmap_set - set an area of a bitmap
 * @eb: the extent buffer
 * @start: offset of the bitmap item in the extent buffer
 * @pos: bit number of the first bit
 * @len: number of bits to set
 */
void extent_buffer_bitmap_set(const struct extent_buffer *eb, unsigned long start,
			      unsigned long pos, unsigned long len)
{
	u8 *kaddr;
	struct page *page;
	unsigned long i;
	size_t offset;
	const unsigned int size = pos + len;
	int bits_to_set = BITS_PER_BYTE - (pos % BITS_PER_BYTE);
	u8 mask_to_set = BITMAP_FIRST_BYTE_MASK(pos);

	eb_bitmap_offset(eb, start, pos, &i, &offset);
	page = eb->pages[i];
	assert_eb_page_uptodate(eb, page);
	kaddr = page_address(page);

	while (len >= bits_to_set) {
		kaddr[offset] |= mask_to_set;
		len -= bits_to_set;
		bits_to_set = BITS_PER_BYTE;
		mask_to_set = ~0;
		if (++offset >= PAGE_SIZE && len > 0) {
			offset = 0;
			page = eb->pages[++i];
			assert_eb_page_uptodate(eb, page);
			kaddr = page_address(page);
		}
	}
	if (len) {
		mask_to_set &= BITMAP_LAST_BYTE_MASK(size);
		kaddr[offset] |= mask_to_set;
	}
}


/**
 * extent_buffer_bitmap_clear - clear an area of a bitmap
 * @eb: the extent buffer
 * @start: offset of the bitmap item in the extent buffer
 * @pos: bit number of the first bit
 * @len: number of bits to clear
 */
void extent_buffer_bitmap_clear(const struct extent_buffer *eb,
				unsigned long start, unsigned long pos,
				unsigned long len)
{
	u8 *kaddr;
	struct page *page;
	unsigned long i;
	size_t offset;
	const unsigned int size = pos + len;
	int bits_to_clear = BITS_PER_BYTE - (pos % BITS_PER_BYTE);
	u8 mask_to_clear = BITMAP_FIRST_BYTE_MASK(pos);

	eb_bitmap_offset(eb, start, pos, &i, &offset);
	page = eb->pages[i];
	assert_eb_page_uptodate(eb, page);
	kaddr = page_address(page);

	while (len >= bits_to_clear) {
		kaddr[offset] &= ~mask_to_clear;
		len -= bits_to_clear;
		bits_to_clear = BITS_PER_BYTE;
		mask_to_clear = ~0;
		if (++offset >= PAGE_SIZE && len > 0) {
			offset = 0;
			page = eb->pages[++i];
			assert_eb_page_uptodate(eb, page);
			kaddr = page_address(page);
		}
	}
	if (len) {
		mask_to_clear &= BITMAP_LAST_BYTE_MASK(size);
		kaddr[offset] &= ~mask_to_clear;
	}
}

static inline bool areas_overlap(unsigned long src, unsigned long dst, unsigned long len)
{
	unsigned long distance = (src > dst) ? src - dst : dst - src;
	return distance < len;
}

static void copy_pages(struct page *dst_page, struct page *src_page,
		       unsigned long dst_off, unsigned long src_off,
		       unsigned long len)
{
	char *dst_kaddr = page_address(dst_page);
	char *src_kaddr;
	int must_memmove = 0;

	if (dst_page != src_page) {
		src_kaddr = page_address(src_page);
	} else {
		src_kaddr = dst_kaddr;
		if (areas_overlap(src_off, dst_off, len))
			must_memmove = 1;
	}

	if (must_memmove)
		memmove(dst_kaddr + dst_off, src_kaddr + src_off, len);
	else
		memcpy(dst_kaddr + dst_off, src_kaddr + src_off, len);
}

void memcpy_extent_buffer(const struct extent_buffer *dst,
			  unsigned long dst_offset, unsigned long src_offset,
			  unsigned long len)
{
	size_t cur;
	size_t dst_off_in_page;
	size_t src_off_in_page;
	unsigned long dst_i;
	unsigned long src_i;

	if (check_eb_range(dst, dst_offset, len) ||
	    check_eb_range(dst, src_offset, len))
		return;

	while (len > 0) {
		dst_off_in_page = get_eb_offset_in_page(dst, dst_offset);
		src_off_in_page = get_eb_offset_in_page(dst, src_offset);

		dst_i = get_eb_page_index(dst_offset);
		src_i = get_eb_page_index(src_offset);

		cur = min(len, (unsigned long)(PAGE_SIZE -
					       src_off_in_page));
		cur = min_t(unsigned long, cur,
			(unsigned long)(PAGE_SIZE - dst_off_in_page));

		copy_pages(dst->pages[dst_i], dst->pages[src_i],
			   dst_off_in_page, src_off_in_page, cur);

		src_offset += cur;
		dst_offset += cur;
		len -= cur;
	}
}

void memmove_extent_buffer(const struct extent_buffer *dst,
			   unsigned long dst_offset, unsigned long src_offset,
			   unsigned long len)
{
	size_t cur;
	size_t dst_off_in_page;
	size_t src_off_in_page;
	unsigned long dst_end = dst_offset + len - 1;
	unsigned long src_end = src_offset + len - 1;
	unsigned long dst_i;
	unsigned long src_i;

	if (check_eb_range(dst, dst_offset, len) ||
	    check_eb_range(dst, src_offset, len))
		return;
	if (dst_offset < src_offset) {
		memcpy_extent_buffer(dst, dst_offset, src_offset, len);
		return;
	}
	while (len > 0) {
		dst_i = get_eb_page_index(dst_end);
		src_i = get_eb_page_index(src_end);

		dst_off_in_page = get_eb_offset_in_page(dst, dst_end);
		src_off_in_page = get_eb_offset_in_page(dst, src_end);

		cur = min_t(unsigned long, len, src_off_in_page + 1);
		cur = min(cur, dst_off_in_page + 1);
		copy_pages(dst->pages[dst_i], dst->pages[src_i],
			   dst_off_in_page - cur + 1,
			   src_off_in_page - cur + 1, cur);

		dst_end -= cur;
		src_end -= cur;
		len -= cur;
	}
}

#define GANG_LOOKUP_SIZE	16
static struct extent_buffer *get_next_extent_buffer(
		struct btrfs_fs_info *fs_info, struct page *page, u64 bytenr)
{
	struct extent_buffer *gang[GANG_LOOKUP_SIZE];
	struct extent_buffer *found = NULL;
	u64 page_start = page_offset(page);
	u64 cur = page_start;

	ASSERT(in_range(bytenr, page_start, PAGE_SIZE));
	lockdep_assert_held(&fs_info->buffer_lock);

	while (cur < page_start + PAGE_SIZE) {
		int ret;
		int i;

		ret = radix_tree_gang_lookup(&fs_info->buffer_radix,
				(void **)gang, cur >> fs_info->sectorsize_bits,
				min_t(unsigned int, GANG_LOOKUP_SIZE,
				      PAGE_SIZE / fs_info->nodesize));
		if (ret == 0)
			goto out;
		for (i = 0; i < ret; i++) {
			/* Already beyond page end */
			if (gang[i]->start >= page_start + PAGE_SIZE)
				goto out;
			/* Found one */
			if (gang[i]->start >= bytenr) {
				found = gang[i];
				goto out;
			}
		}
		cur = gang[ret - 1]->start + gang[ret - 1]->len;
	}
out:
	return found;
}

static int try_release_subpage_extent_buffer(struct page *page)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(page->mapping->host->i_sb);
	u64 cur = page_offset(page);
	const u64 end = page_offset(page) + PAGE_SIZE;
	int ret;

	while (cur < end) {
		struct extent_buffer *eb = NULL;

		/*
		 * Unlike try_release_extent_buffer() which uses page->private
		 * to grab buffer, for subpage case we rely on radix tree, thus
		 * we need to ensure radix tree consistency.
		 *
		 * We also want an atomic snapshot of the radix tree, thus go
		 * with spinlock rather than RCU.
		 */
		spin_lock(&fs_info->buffer_lock);
		eb = get_next_extent_buffer(fs_info, page, cur);
		if (!eb) {
			/* No more eb in the page range after or at cur */
			spin_unlock(&fs_info->buffer_lock);
			break;
		}
		cur = eb->start + eb->len;

		/*
		 * The same as try_release_extent_buffer(), to ensure the eb
		 * won't disappear out from under us.
		 */
		spin_lock(&eb->refs_lock);
		if (atomic_read(&eb->refs) != 1 || extent_buffer_under_io(eb)) {
			spin_unlock(&eb->refs_lock);
			spin_unlock(&fs_info->buffer_lock);
			break;
		}
		spin_unlock(&fs_info->buffer_lock);

		/*
		 * If tree ref isn't set then we know the ref on this eb is a
		 * real ref, so just return, this eb will likely be freed soon
		 * anyway.
		 */
		if (!test_and_clear_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags)) {
			spin_unlock(&eb->refs_lock);
			break;
		}

		/*
		 * Here we don't care about the return value, we will always
		 * check the page private at the end.  And
		 * release_extent_buffer() will release the refs_lock.
		 */
		release_extent_buffer(eb);
	}
	/*
	 * Finally to check if we have cleared page private, as if we have
	 * released all ebs in the page, the page private should be cleared now.
	 */
	spin_lock(&page->mapping->private_lock);
	if (!PagePrivate(page))
		ret = 1;
	else
		ret = 0;
	spin_unlock(&page->mapping->private_lock);
	return ret;

}

int try_release_extent_buffer(struct page *page)
{
	struct extent_buffer *eb;

	if (btrfs_sb(page->mapping->host->i_sb)->sectorsize < PAGE_SIZE)
		return try_release_subpage_extent_buffer(page);

	/*
	 * We need to make sure nobody is changing page->private, as we rely on
	 * page->private as the pointer to extent buffer.
	 */
	spin_lock(&page->mapping->private_lock);
	if (!PagePrivate(page)) {
		spin_unlock(&page->mapping->private_lock);
		return 1;
	}

	eb = (struct extent_buffer *)page->private;
	BUG_ON(!eb);

	/*
	 * This is a little awful but should be ok, we need to make sure that
	 * the eb doesn't disappear out from under us while we're looking at
	 * this page.
	 */
	spin_lock(&eb->refs_lock);
	if (atomic_read(&eb->refs) != 1 || extent_buffer_under_io(eb)) {
		spin_unlock(&eb->refs_lock);
		spin_unlock(&page->mapping->private_lock);
		return 0;
	}
	spin_unlock(&page->mapping->private_lock);

	/*
	 * If tree ref isn't set then we know the ref on this eb is a real ref,
	 * so just return, this page will likely be freed soon anyway.
	 */
	if (!test_and_clear_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags)) {
		spin_unlock(&eb->refs_lock);
		return 0;
	}

	return release_extent_buffer(eb);
}

/*
 * btrfs_readahead_tree_block - attempt to readahead a child block
 * @fs_info:	the fs_info
 * @bytenr:	bytenr to read
 * @owner_root: objectid of the root that owns this eb
 * @gen:	generation for the uptodate check, can be 0
 * @level:	level for the eb
 *
 * Attempt to readahead a tree block at @bytenr.  If @gen is 0 then we do a
 * normal uptodate check of the eb, without checking the generation.  If we have
 * to read the block we will not block on anything.
 */
void btrfs_readahead_tree_block(struct btrfs_fs_info *fs_info,
				u64 bytenr, u64 owner_root, u64 gen, int level)
{
	struct extent_buffer *eb;
	int ret;

	eb = btrfs_find_create_tree_block(fs_info, bytenr, owner_root, level);
	if (IS_ERR(eb))
		return;

	if (btrfs_buffer_uptodate(eb, gen, 1)) {
		free_extent_buffer(eb);
		return;
	}

	ret = read_extent_buffer_pages(eb, WAIT_NONE, 0);
	if (ret < 0)
		free_extent_buffer_stale(eb);
	else
		free_extent_buffer(eb);
}

/*
 * btrfs_readahead_node_child - readahead a node's child block
 * @node:	parent node we're reading from
 * @slot:	slot in the parent node for the child we want to read
 *
 * A helper for btrfs_readahead_tree_block, we simply read the bytenr pointed at
 * the slot in the node provided.
 */
void btrfs_readahead_node_child(struct extent_buffer *node, int slot)
{
	btrfs_readahead_tree_block(node->fs_info,
				   btrfs_node_blockptr(node, slot),
				   btrfs_header_owner(node),
				   btrfs_node_ptr_generation(node, slot),
				   btrfs_header_level(node) - 1);
}
