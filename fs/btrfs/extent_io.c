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
#include "extent_io.h"
#include "extent_map.h"
#include "ctree.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "check-integrity.h"
#include "locking.h"
#include "rcu-string.h"
#include "backref.h"

static struct kmem_cache *extent_state_cache;
static struct kmem_cache *extent_buffer_cache;
static struct bio_set *btrfs_bioset;

static inline bool extent_state_in_tree(const struct extent_state *state)
{
	return !RB_EMPTY_NODE(&state->rb_node);
}

#ifdef CONFIG_BTRFS_DEBUG
static LIST_HEAD(buffers);
static LIST_HEAD(states);

static DEFINE_SPINLOCK(leak_lock);

static inline
void btrfs_leak_debug_add(struct list_head *new, struct list_head *head)
{
	unsigned long flags;

	spin_lock_irqsave(&leak_lock, flags);
	list_add(new, head);
	spin_unlock_irqrestore(&leak_lock, flags);
}

static inline
void btrfs_leak_debug_del(struct list_head *entry)
{
	unsigned long flags;

	spin_lock_irqsave(&leak_lock, flags);
	list_del(entry);
	spin_unlock_irqrestore(&leak_lock, flags);
}

static inline
void btrfs_leak_debug_check(void)
{
	struct extent_state *state;
	struct extent_buffer *eb;

	while (!list_empty(&states)) {
		state = list_entry(states.next, struct extent_state, leak_list);
		pr_err("BTRFS: state leak: start %llu end %llu state %u in tree %d refs %d\n",
		       state->start, state->end, state->state,
		       extent_state_in_tree(state),
		       atomic_read(&state->refs));
		list_del(&state->leak_list);
		kmem_cache_free(extent_state_cache, state);
	}

	while (!list_empty(&buffers)) {
		eb = list_entry(buffers.next, struct extent_buffer, leak_list);
		printk(KERN_ERR "BTRFS: buffer leak start %llu len %lu "
		       "refs %d\n",
		       eb->start, eb->len, atomic_read(&eb->refs));
		list_del(&eb->leak_list);
		kmem_cache_free(extent_buffer_cache, eb);
	}
}

#define btrfs_debug_check_extent_io_range(tree, start, end)		\
	__btrfs_debug_check_extent_io_range(__func__, (tree), (start), (end))
static inline void __btrfs_debug_check_extent_io_range(const char *caller,
		struct extent_io_tree *tree, u64 start, u64 end)
{
	struct inode *inode;
	u64 isize;

	if (!tree->mapping)
		return;

	inode = tree->mapping->host;
	isize = i_size_read(inode);
	if (end >= PAGE_SIZE && (end % 2) == 0 && end != isize - 1) {
		btrfs_debug_rl(BTRFS_I(inode)->root->fs_info,
		    "%s: ino %llu isize %llu odd range [%llu,%llu]",
				caller, btrfs_ino(inode), isize, start, end);
	}
}
#else
#define btrfs_leak_debug_add(new, head)	do {} while (0)
#define btrfs_leak_debug_del(entry)	do {} while (0)
#define btrfs_leak_debug_check()	do {} while (0)
#define btrfs_debug_check_extent_io_range(c, s, e)	do {} while (0)
#endif

#define BUFFER_LRU_MAX 64

struct tree_entry {
	u64 start;
	u64 end;
	struct rb_node rb_node;
};

struct extent_page_data {
	struct bio *bio;
	struct extent_io_tree *tree;
	get_extent_t *get_extent;
	unsigned long bio_flags;

	/* tells writepage not to lock the state bits for this range
	 * it still does the unlocking
	 */
	unsigned int extent_locked:1;

	/* tells the submit_bio code to use a WRITE_SYNC */
	unsigned int sync_io:1;
};

static void add_extent_changeset(struct extent_state *state, unsigned bits,
				 struct extent_changeset *changeset,
				 int set)
{
	int ret;

	if (!changeset)
		return;
	if (set && (state->state & bits) == bits)
		return;
	if (!set && (state->state & bits) == 0)
		return;
	changeset->bytes_changed += state->end - state->start + 1;
	ret = ulist_add(changeset->range_changed, state->start, state->end,
			GFP_ATOMIC);
	/* ENOMEM */
	BUG_ON(ret < 0);
}

static noinline void flush_write_bio(void *data);
static inline struct btrfs_fs_info *
tree_fs_info(struct extent_io_tree *tree)
{
	if (!tree->mapping)
		return NULL;
	return btrfs_sb(tree->mapping->host->i_sb);
}

int __init extent_io_init(void)
{
	extent_state_cache = kmem_cache_create("btrfs_extent_state",
			sizeof(struct extent_state), 0,
			SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, NULL);
	if (!extent_state_cache)
		return -ENOMEM;

	extent_buffer_cache = kmem_cache_create("btrfs_extent_buffer",
			sizeof(struct extent_buffer), 0,
			SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, NULL);
	if (!extent_buffer_cache)
		goto free_state_cache;

	btrfs_bioset = bioset_create(BIO_POOL_SIZE,
				     offsetof(struct btrfs_io_bio, bio));
	if (!btrfs_bioset)
		goto free_buffer_cache;

	if (bioset_integrity_create(btrfs_bioset, BIO_POOL_SIZE))
		goto free_bioset;

	return 0;

free_bioset:
	bioset_free(btrfs_bioset);
	btrfs_bioset = NULL;

free_buffer_cache:
	kmem_cache_destroy(extent_buffer_cache);
	extent_buffer_cache = NULL;

free_state_cache:
	kmem_cache_destroy(extent_state_cache);
	extent_state_cache = NULL;
	return -ENOMEM;
}

void extent_io_exit(void)
{
	btrfs_leak_debug_check();

	/*
	 * Make sure all delayed rcu free are flushed before we
	 * destroy caches.
	 */
	rcu_barrier();
	if (extent_state_cache)
		kmem_cache_destroy(extent_state_cache);
	if (extent_buffer_cache)
		kmem_cache_destroy(extent_buffer_cache);
	if (btrfs_bioset)
		bioset_free(btrfs_bioset);
}

void extent_io_tree_init(struct extent_io_tree *tree,
			 struct address_space *mapping)
{
	tree->state = RB_ROOT;
	tree->ops = NULL;
	tree->dirty_bytes = 0;
	spin_lock_init(&tree->lock);
	tree->mapping = mapping;
}

static struct extent_state *alloc_extent_state(gfp_t mask)
{
	struct extent_state *state;

	state = kmem_cache_alloc(extent_state_cache, mask);
	if (!state)
		return state;
	state->state = 0;
	state->private = 0;
	RB_CLEAR_NODE(&state->rb_node);
	btrfs_leak_debug_add(&state->leak_list, &states);
	atomic_set(&state->refs, 1);
	init_waitqueue_head(&state->wq);
	trace_alloc_extent_state(state, mask, _RET_IP_);
	return state;
}

void free_extent_state(struct extent_state *state)
{
	if (!state)
		return;
	if (atomic_dec_and_test(&state->refs)) {
		WARN_ON(extent_state_in_tree(state));
		btrfs_leak_debug_del(&state->leak_list);
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

static struct rb_node *__etree_search(struct extent_io_tree *tree, u64 offset,
				      struct rb_node **prev_ret,
				      struct rb_node **next_ret,
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

	if (prev_ret) {
		orig_prev = prev;
		while (prev && offset > prev_entry->end) {
			prev = rb_next(prev);
			prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		}
		*prev_ret = prev;
		prev = orig_prev;
	}

	if (next_ret) {
		prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		while (prev && offset < prev_entry->start) {
			prev = rb_prev(prev);
			prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		}
		*next_ret = prev;
	}
	return NULL;
}

static inline struct rb_node *
tree_search_for_insert(struct extent_io_tree *tree,
		       u64 offset,
		       struct rb_node ***p_ret,
		       struct rb_node **parent_ret)
{
	struct rb_node *prev = NULL;
	struct rb_node *ret;

	ret = __etree_search(tree, offset, &prev, NULL, p_ret, parent_ret);
	if (!ret)
		return prev;
	return ret;
}

static inline struct rb_node *tree_search(struct extent_io_tree *tree,
					  u64 offset)
{
	return tree_search_for_insert(tree, offset, NULL, NULL);
}

static void merge_cb(struct extent_io_tree *tree, struct extent_state *new,
		     struct extent_state *other)
{
	if (tree->ops && tree->ops->merge_extent_hook)
		tree->ops->merge_extent_hook(tree->mapping->host, new,
					     other);
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

	if (state->state & (EXTENT_IOBITS | EXTENT_BOUNDARY))
		return;

	other_node = rb_prev(&state->rb_node);
	if (other_node) {
		other = rb_entry(other_node, struct extent_state, rb_node);
		if (other->end == state->start - 1 &&
		    other->state == state->state) {
			merge_cb(tree, state, other);
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
			merge_cb(tree, state, other);
			state->end = other->end;
			rb_erase(&other->rb_node, &tree->state);
			RB_CLEAR_NODE(&other->rb_node);
			free_extent_state(other);
		}
	}
}

static void set_state_cb(struct extent_io_tree *tree,
			 struct extent_state *state, unsigned *bits)
{
	if (tree->ops && tree->ops->set_bit_hook)
		tree->ops->set_bit_hook(tree->mapping->host, state, bits);
}

static void clear_state_cb(struct extent_io_tree *tree,
			   struct extent_state *state, unsigned *bits)
{
	if (tree->ops && tree->ops->clear_bit_hook)
		tree->ops->clear_bit_hook(tree->mapping->host, state, bits);
}

static void set_state_bits(struct extent_io_tree *tree,
			   struct extent_state *state, unsigned *bits,
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
			unsigned *bits, struct extent_changeset *changeset)
{
	struct rb_node *node;

	if (end < start)
		WARN(1, KERN_ERR "BTRFS: end < start %llu %llu\n",
		       end, start);
	state->start = start;
	state->end = end;

	set_state_bits(tree, state, bits, changeset);

	node = tree_insert(&tree->state, NULL, end, &state->rb_node, p, parent);
	if (node) {
		struct extent_state *found;
		found = rb_entry(node, struct extent_state, rb_node);
		printk(KERN_ERR "BTRFS: found node %llu %llu on insert of "
		       "%llu %llu\n",
		       found->start, found->end, start, end);
		return -EEXIST;
	}
	merge_state(tree, state);
	return 0;
}

static void split_cb(struct extent_io_tree *tree, struct extent_state *orig,
		     u64 split)
{
	if (tree->ops && tree->ops->split_extent_hook)
		tree->ops->split_extent_hook(tree->mapping->host, orig, split);
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

	split_cb(tree, orig, split);

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
 * it will optionally wake up any one waiting on this state (wake == 1).
 *
 * If no bits are set on the state struct after clearing things, the
 * struct is freed and removed from the tree
 */
static struct extent_state *clear_state_bit(struct extent_io_tree *tree,
					    struct extent_state *state,
					    unsigned *bits, int wake,
					    struct extent_changeset *changeset)
{
	struct extent_state *next;
	unsigned bits_to_clear = *bits & ~EXTENT_CTLBITS;

	if ((bits_to_clear & EXTENT_DIRTY) && (state->state & EXTENT_DIRTY)) {
		u64 range = state->end - state->start + 1;
		WARN_ON(range > tree->dirty_bytes);
		tree->dirty_bytes -= range;
	}
	clear_state_cb(tree, state, bits);
	add_extent_changeset(state, bits_to_clear, changeset, 0);
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
	btrfs_panic(tree_fs_info(tree), err, "Locking error: "
		    "Extent tree was modified by another "
		    "thread while locked.");
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
static int __clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
			      unsigned bits, int wake, int delete,
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

	if (bits & EXTENT_DELALLOC)
		bits |= EXTENT_NORESERVE;

	if (delete)
		bits |= ~EXTENT_CTLBITS;
	bits |= EXTENT_FIRST_DELALLOC;

	if (bits & (EXTENT_IOBITS | EXTENT_BOUNDARY))
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
				atomic_dec(&cached->refs);
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
	goto search_again;

out:
	spin_unlock(&tree->lock);
	if (prealloc)
		free_extent_state(prealloc);

	return 0;

search_again:
	if (start > end)
		goto out;
	spin_unlock(&tree->lock);
	if (gfpflags_allow_blocking(mask))
		cond_resched();
	goto again;
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
			    unsigned long bits)
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
			atomic_inc(&state->refs);
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
			   unsigned *bits, struct extent_changeset *changeset)
{
	unsigned bits_to_set = *bits & ~EXTENT_CTLBITS;

	set_state_cb(tree, state, bits);
	if ((bits_to_set & EXTENT_DIRTY) && !(state->state & EXTENT_DIRTY)) {
		u64 range = state->end - state->start + 1;
		tree->dirty_bytes += range;
	}
	add_extent_changeset(state, bits_to_set, changeset, 1);
	state->state |= bits_to_set;
}

static void cache_state_if_flags(struct extent_state *state,
				 struct extent_state **cached_ptr,
				 unsigned flags)
{
	if (cached_ptr && !(*cached_ptr)) {
		if (!flags || (state->state & flags)) {
			*cached_ptr = state;
			atomic_inc(&state->refs);
		}
	}
}

static void cache_state(struct extent_state *state,
			struct extent_state **cached_ptr)
{
	return cache_state_if_flags(state, cached_ptr,
				    EXTENT_IOBITS | EXTENT_BOUNDARY);
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

static int __must_check
__set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		 unsigned bits, unsigned exclusive_bits,
		 u64 *failed_start, struct extent_state **cached_state,
		 gfp_t mask, struct extent_changeset *changeset)
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

	bits |= EXTENT_FIRST_DELALLOC;
again:
	if (!prealloc && gfpflags_allow_blocking(mask)) {
		prealloc = alloc_extent_state(mask);
		BUG_ON(!prealloc);
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

	goto search_again;

out:
	spin_unlock(&tree->lock);
	if (prealloc)
		free_extent_state(prealloc);

	return err;

search_again:
	if (start > end)
		goto out;
	spin_unlock(&tree->lock);
	if (gfpflags_allow_blocking(mask))
		cond_resched();
	goto again;
}

int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   unsigned bits, u64 * failed_start,
		   struct extent_state **cached_state, gfp_t mask)
{
	return __set_extent_bit(tree, start, end, bits, 0, failed_start,
				cached_state, mask, NULL);
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
 * @mask:	the allocation mask
 *
 * This will go through and set bits for the given range.  If any states exist
 * already in this range they are set with the given bit and cleared of the
 * clear_bits.  This is only meant to be used by things that are mergeable, ie
 * converting from say DELALLOC to DIRTY.  This is not meant to be used with
 * boundary bits like LOCK.
 */
int convert_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       unsigned bits, unsigned clear_bits,
		       struct extent_state **cached_state, gfp_t mask)
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

again:
	if (!prealloc && gfpflags_allow_blocking(mask)) {
		/*
		 * Best effort, don't worry if extent state allocation fails
		 * here for the first iteration. We might have a cached state
		 * that matches exactly the target range, in which case no
		 * extent state allocations are needed. We'll only know this
		 * after locking the tree.
		 */
		prealloc = alloc_extent_state(mask);
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

	goto search_again;

out:
	spin_unlock(&tree->lock);
	if (prealloc)
		free_extent_state(prealloc);

	return err;

search_again:
	if (start > end)
		goto out;
	spin_unlock(&tree->lock);
	if (gfpflags_allow_blocking(mask))
		cond_resched();
	first_iteration = false;
	goto again;
}

/* wrappers around set/clear extent bit */
int set_extent_dirty(struct extent_io_tree *tree, u64 start, u64 end,
		     gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_DIRTY, NULL,
			      NULL, mask);
}

int set_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		    unsigned bits, gfp_t mask)
{
	return set_extent_bit(tree, start, end, bits, NULL,
			      NULL, mask);
}

int set_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			   unsigned bits, gfp_t mask,
			   struct extent_changeset *changeset)
{
	/*
	 * We don't support EXTENT_LOCKED yet, as current changeset will
	 * record any bits changed, so for EXTENT_LOCKED case, it will
	 * either fail with -EEXIST or changeset will record the whole
	 * range.
	 */
	BUG_ON(bits & EXTENT_LOCKED);

	return __set_extent_bit(tree, start, end, bits, 0, NULL, NULL, mask,
				changeset);
}

int clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     unsigned bits, int wake, int delete,
		     struct extent_state **cached, gfp_t mask)
{
	return __clear_extent_bit(tree, start, end, bits, wake, delete,
				  cached, mask, NULL);
}

int clear_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		      unsigned bits, gfp_t mask)
{
	int wake = 0;

	if (bits & EXTENT_LOCKED)
		wake = 1;

	return clear_extent_bit(tree, start, end, bits, wake, 0, NULL, mask);
}

int clear_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			     unsigned bits, gfp_t mask,
			     struct extent_changeset *changeset)
{
	/*
	 * Don't support EXTENT_LOCKED case, same reason as
	 * set_record_extent_bits().
	 */
	BUG_ON(bits & EXTENT_LOCKED);

	return __clear_extent_bit(tree, start, end, bits, 0, 0, NULL, mask,
				  changeset);
}

int set_extent_delalloc(struct extent_io_tree *tree, u64 start, u64 end,
			struct extent_state **cached_state, gfp_t mask)
{
	return set_extent_bit(tree, start, end,
			      EXTENT_DELALLOC | EXTENT_UPTODATE,
			      NULL, cached_state, mask);
}

int set_extent_defrag(struct extent_io_tree *tree, u64 start, u64 end,
		      struct extent_state **cached_state, gfp_t mask)
{
	return set_extent_bit(tree, start, end,
			      EXTENT_DELALLOC | EXTENT_UPTODATE | EXTENT_DEFRAG,
			      NULL, cached_state, mask);
}

int clear_extent_dirty(struct extent_io_tree *tree, u64 start, u64 end,
		       gfp_t mask)
{
	return clear_extent_bit(tree, start, end,
				EXTENT_DIRTY | EXTENT_DELALLOC |
				EXTENT_DO_ACCOUNTING, 0, 0, NULL, mask);
}

int set_extent_new(struct extent_io_tree *tree, u64 start, u64 end,
		     gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_NEW, NULL,
			      NULL, mask);
}

int set_extent_uptodate(struct extent_io_tree *tree, u64 start, u64 end,
			struct extent_state **cached_state, gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_UPTODATE, NULL,
			      cached_state, mask);
}

int clear_extent_uptodate(struct extent_io_tree *tree, u64 start, u64 end,
			  struct extent_state **cached_state, gfp_t mask)
{
	return clear_extent_bit(tree, start, end, EXTENT_UPTODATE, 0, 0,
				cached_state, mask);
}

/*
 * either insert or lock state struct between start and end use mask to tell
 * us if waiting is desired.
 */
int lock_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		     unsigned bits, struct extent_state **cached_state)
{
	int err;
	u64 failed_start;

	while (1) {
		err = __set_extent_bit(tree, start, end, EXTENT_LOCKED | bits,
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

int lock_extent(struct extent_io_tree *tree, u64 start, u64 end)
{
	return lock_extent_bits(tree, start, end, 0, NULL);
}

int try_lock_extent(struct extent_io_tree *tree, u64 start, u64 end)
{
	int err;
	u64 failed_start;

	err = __set_extent_bit(tree, start, end, EXTENT_LOCKED, EXTENT_LOCKED,
			       &failed_start, NULL, GFP_NOFS, NULL);
	if (err == -EEXIST) {
		if (failed_start > start)
			clear_extent_bit(tree, start, failed_start - 1,
					 EXTENT_LOCKED, 1, 0, NULL, GFP_NOFS);
		return 0;
	}
	return 1;
}

int unlock_extent_cached(struct extent_io_tree *tree, u64 start, u64 end,
			 struct extent_state **cached, gfp_t mask)
{
	return clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, cached,
				mask);
}

int unlock_extent(struct extent_io_tree *tree, u64 start, u64 end)
{
	return clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, NULL,
				GFP_NOFS);
}

int extent_range_clear_dirty_for_io(struct inode *inode, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(inode->i_mapping, index);
		BUG_ON(!page); /* Pages should be in the extent_io_tree */
		clear_page_dirty_for_io(page);
		page_cache_release(page);
		index++;
	}
	return 0;
}

int extent_range_redirty_for_io(struct inode *inode, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(inode->i_mapping, index);
		BUG_ON(!page); /* Pages should be in the extent_io_tree */
		__set_page_dirty_nobuffers(page);
		account_page_redirty(page);
		page_cache_release(page);
		index++;
	}
	return 0;
}

/*
 * helper function to set both pages and extents in the tree writeback
 */
static int set_range_writeback(struct extent_io_tree *tree, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(tree->mapping, index);
		BUG_ON(!page); /* Pages should be in the extent_io_tree */
		set_page_writeback(page);
		page_cache_release(page);
		index++;
	}
	return 0;
}

/* find the first state struct with 'bits' set after 'start', and
 * return it.  tree->lock must be held.  NULL will returned if
 * nothing was found after 'start'
 */
static struct extent_state *
find_first_extent_bit_state(struct extent_io_tree *tree,
			    u64 start, unsigned bits)
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
 * find the first offset in the io tree with 'bits' set. zero is
 * returned if we find something, and *start_ret and *end_ret are
 * set to reflect the state struct that was found.
 *
 * If nothing was found, 1 is returned. If found something, return 0.
 */
int find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			  u64 *start_ret, u64 *end_ret, unsigned bits,
			  struct extent_state **cached_state)
{
	struct extent_state *state;
	struct rb_node *n;
	int ret = 1;

	spin_lock(&tree->lock);
	if (cached_state && *cached_state) {
		state = *cached_state;
		if (state->end == start - 1 && extent_state_in_tree(state)) {
			n = rb_next(&state->rb_node);
			while (n) {
				state = rb_entry(n, struct extent_state,
						 rb_node);
				if (state->state & bits)
					goto got_it;
				n = rb_next(n);
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

/*
 * find a contiguous range of bytes in the file marked as delalloc, not
 * more than 'max_bytes'.  start and end are used to return the range,
 *
 * 1 is returned if we find something, 0 if nothing was in the tree
 */
static noinline u64 find_delalloc_range(struct extent_io_tree *tree,
					u64 *start, u64 *end, u64 max_bytes,
					struct extent_state **cached_state)
{
	struct rb_node *node;
	struct extent_state *state;
	u64 cur_start = *start;
	u64 found = 0;
	u64 total_bytes = 0;

	spin_lock(&tree->lock);

	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, cur_start);
	if (!node) {
		if (!found)
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
			atomic_inc(&state->refs);
		}
		found++;
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

static noinline void __unlock_for_delalloc(struct inode *inode,
					   struct page *locked_page,
					   u64 start, u64 end)
{
	int ret;
	struct page *pages[16];
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	unsigned long nr_pages = end_index - index + 1;
	int i;

	if (index == locked_page->index && end_index == index)
		return;

	while (nr_pages > 0) {
		ret = find_get_pages_contig(inode->i_mapping, index,
				     min_t(unsigned long, nr_pages,
				     ARRAY_SIZE(pages)), pages);
		for (i = 0; i < ret; i++) {
			if (pages[i] != locked_page)
				unlock_page(pages[i]);
			page_cache_release(pages[i]);
		}
		nr_pages -= ret;
		index += ret;
		cond_resched();
	}
}

static noinline int lock_delalloc_pages(struct inode *inode,
					struct page *locked_page,
					u64 delalloc_start,
					u64 delalloc_end)
{
	unsigned long index = delalloc_start >> PAGE_CACHE_SHIFT;
	unsigned long start_index = index;
	unsigned long end_index = delalloc_end >> PAGE_CACHE_SHIFT;
	unsigned long pages_locked = 0;
	struct page *pages[16];
	unsigned long nrpages;
	int ret;
	int i;

	/* the caller is responsible for locking the start index */
	if (index == locked_page->index && index == end_index)
		return 0;

	/* skip the page at the start index */
	nrpages = end_index - index + 1;
	while (nrpages > 0) {
		ret = find_get_pages_contig(inode->i_mapping, index,
				     min_t(unsigned long,
				     nrpages, ARRAY_SIZE(pages)), pages);
		if (ret == 0) {
			ret = -EAGAIN;
			goto done;
		}
		/* now we have an array of pages, lock them all */
		for (i = 0; i < ret; i++) {
			/*
			 * the caller is taking responsibility for
			 * locked_page
			 */
			if (pages[i] != locked_page) {
				lock_page(pages[i]);
				if (!PageDirty(pages[i]) ||
				    pages[i]->mapping != inode->i_mapping) {
					ret = -EAGAIN;
					unlock_page(pages[i]);
					page_cache_release(pages[i]);
					goto done;
				}
			}
			page_cache_release(pages[i]);
			pages_locked++;
		}
		nrpages -= ret;
		index += ret;
		cond_resched();
	}
	ret = 0;
done:
	if (ret && pages_locked) {
		__unlock_for_delalloc(inode, locked_page,
			      delalloc_start,
			      ((u64)(start_index + pages_locked - 1)) <<
			      PAGE_CACHE_SHIFT);
	}
	return ret;
}

/*
 * find a contiguous range of bytes in the file marked as delalloc, not
 * more than 'max_bytes'.  start and end are used to return the range,
 *
 * 1 is returned if we find something, 0 if nothing was in the tree
 */
STATIC u64 find_lock_delalloc_range(struct inode *inode,
				    struct extent_io_tree *tree,
				    struct page *locked_page, u64 *start,
				    u64 *end, u64 max_bytes)
{
	u64 delalloc_start;
	u64 delalloc_end;
	u64 found;
	struct extent_state *cached_state = NULL;
	int ret;
	int loops = 0;

again:
	/* step one, find a bunch of delalloc bytes starting at start */
	delalloc_start = *start;
	delalloc_end = 0;
	found = find_delalloc_range(tree, &delalloc_start, &delalloc_end,
				    max_bytes, &cached_state);
	if (!found || delalloc_end <= *start) {
		*start = delalloc_start;
		*end = delalloc_end;
		free_extent_state(cached_state);
		return 0;
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
	if (ret == -EAGAIN) {
		/* some of the pages are gone, lets avoid looping by
		 * shortening the size of the delalloc range we're searching
		 */
		free_extent_state(cached_state);
		cached_state = NULL;
		if (!loops) {
			max_bytes = PAGE_CACHE_SIZE;
			loops = 1;
			goto again;
		} else {
			found = 0;
			goto out_failed;
		}
	}
	BUG_ON(ret); /* Only valid values are 0 and -EAGAIN */

	/* step three, lock the state bits for the whole range */
	lock_extent_bits(tree, delalloc_start, delalloc_end, 0, &cached_state);

	/* then test to make sure it is all still delalloc */
	ret = test_range_bit(tree, delalloc_start, delalloc_end,
			     EXTENT_DELALLOC, 1, cached_state);
	if (!ret) {
		unlock_extent_cached(tree, delalloc_start, delalloc_end,
				     &cached_state, GFP_NOFS);
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

int extent_clear_unlock_delalloc(struct inode *inode, u64 start, u64 end,
				 struct page *locked_page,
				 unsigned clear_bits,
				 unsigned long page_ops)
{
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;
	int ret;
	struct page *pages[16];
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	unsigned long nr_pages = end_index - index + 1;
	int i;

	clear_extent_bit(tree, start, end, clear_bits, 1, 0, NULL, GFP_NOFS);
	if (page_ops == 0)
		return 0;

	if ((page_ops & PAGE_SET_ERROR) && nr_pages > 0)
		mapping_set_error(inode->i_mapping, -EIO);

	while (nr_pages > 0) {
		ret = find_get_pages_contig(inode->i_mapping, index,
				     min_t(unsigned long,
				     nr_pages, ARRAY_SIZE(pages)), pages);
		for (i = 0; i < ret; i++) {

			if (page_ops & PAGE_SET_PRIVATE2)
				SetPagePrivate2(pages[i]);

			if (pages[i] == locked_page) {
				page_cache_release(pages[i]);
				continue;
			}
			if (page_ops & PAGE_CLEAR_DIRTY)
				clear_page_dirty_for_io(pages[i]);
			if (page_ops & PAGE_SET_WRITEBACK)
				set_page_writeback(pages[i]);
			if (page_ops & PAGE_SET_ERROR)
				SetPageError(pages[i]);
			if (page_ops & PAGE_END_WRITEBACK)
				end_page_writeback(pages[i]);
			if (page_ops & PAGE_UNLOCK)
				unlock_page(pages[i]);
			page_cache_release(pages[i]);
		}
		nr_pages -= ret;
		index += ret;
		cond_resched();
	}
	return 0;
}

/*
 * count the number of bytes in the tree that have a given bit(s)
 * set.  This can be fairly slow, except for EXTENT_DIRTY which is
 * cached.  The total number found is returned.
 */
u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end, u64 max_bytes,
		     unsigned bits, int contig)
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
static int set_state_private(struct extent_io_tree *tree, u64 start, u64 private)
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
	state->private = private;
out:
	spin_unlock(&tree->lock);
	return ret;
}

int get_state_private(struct extent_io_tree *tree, u64 start, u64 *private)
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
	*private = state->private;
out:
	spin_unlock(&tree->lock);
	return ret;
}

/*
 * searches a range in the state tree for a given mask.
 * If 'filled' == 1, this returns 1 only if every extent in the tree
 * has the bits set.  Otherwise, 1 is returned if any bit in the
 * range is found set.
 */
int test_range_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   unsigned bits, int filled, struct extent_state *cached)
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

/*
 * helper function to set a given page up to date if all the
 * extents in the tree for that page are up to date
 */
static void check_page_uptodate(struct extent_io_tree *tree, struct page *page)
{
	u64 start = page_offset(page);
	u64 end = start + PAGE_CACHE_SIZE - 1;
	if (test_range_bit(tree, start, end, EXTENT_UPTODATE, 1, NULL))
		SetPageUptodate(page);
}

int free_io_failure(struct inode *inode, struct io_failure_record *rec)
{
	int ret;
	int err = 0;
	struct extent_io_tree *failure_tree = &BTRFS_I(inode)->io_failure_tree;

	set_state_private(failure_tree, rec->start, 0);
	ret = clear_extent_bits(failure_tree, rec->start,
				rec->start + rec->len - 1,
				EXTENT_LOCKED | EXTENT_DIRTY, GFP_NOFS);
	if (ret)
		err = ret;

	ret = clear_extent_bits(&BTRFS_I(inode)->io_tree, rec->start,
				rec->start + rec->len - 1,
				EXTENT_DAMAGED, GFP_NOFS);
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
int repair_io_failure(struct inode *inode, u64 start, u64 length, u64 logical,
		      struct page *page, unsigned int pg_offset, int mirror_num)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct bio *bio;
	struct btrfs_device *dev;
	u64 map_length = 0;
	u64 sector;
	struct btrfs_bio *bbio = NULL;
	struct btrfs_mapping_tree *map_tree = &fs_info->mapping_tree;
	int ret;

	ASSERT(!(fs_info->sb->s_flags & MS_RDONLY));
	BUG_ON(!mirror_num);

	/* we can't repair anything in raid56 yet */
	if (btrfs_is_parity_mirror(map_tree, logical, length, mirror_num))
		return 0;

	bio = btrfs_io_bio_alloc(GFP_NOFS, 1);
	if (!bio)
		return -EIO;
	bio->bi_iter.bi_size = 0;
	map_length = length;

	ret = btrfs_map_block(fs_info, WRITE, logical,
			      &map_length, &bbio, mirror_num);
	if (ret) {
		bio_put(bio);
		return -EIO;
	}
	BUG_ON(mirror_num != bbio->mirror_num);
	sector = bbio->stripes[mirror_num-1].physical >> 9;
	bio->bi_iter.bi_sector = sector;
	dev = bbio->stripes[mirror_num-1].dev;
	btrfs_put_bbio(bbio);
	if (!dev || !dev->bdev || !dev->writeable) {
		bio_put(bio);
		return -EIO;
	}
	bio->bi_bdev = dev->bdev;
	bio_add_page(bio, page, length, pg_offset);

	if (btrfsic_submit_bio_wait(WRITE_SYNC, bio)) {
		/* try to remap that extent elsewhere? */
		bio_put(bio);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_WRITE_ERRS);
		return -EIO;
	}

	btrfs_info_rl_in_rcu(fs_info,
		"read error corrected: ino %llu off %llu (dev %s sector %llu)",
				  btrfs_ino(inode), start,
				  rcu_str_deref(dev->name), sector);
	bio_put(bio);
	return 0;
}

int repair_eb_io_failure(struct btrfs_root *root, struct extent_buffer *eb,
			 int mirror_num)
{
	u64 start = eb->start;
	unsigned long i, num_pages = num_extent_pages(eb->start, eb->len);
	int ret = 0;

	if (root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	for (i = 0; i < num_pages; i++) {
		struct page *p = eb->pages[i];

		ret = repair_io_failure(root->fs_info->btree_inode, start,
					PAGE_CACHE_SIZE, start, p,
					start - page_offset(p), mirror_num);
		if (ret)
			break;
		start += PAGE_CACHE_SIZE;
	}

	return ret;
}

/*
 * each time an IO finishes, we do a fast check in the IO failure tree
 * to see if we need to process or clean up an io_failure_record
 */
int clean_io_failure(struct inode *inode, u64 start, struct page *page,
		     unsigned int pg_offset)
{
	u64 private;
	u64 private_failure;
	struct io_failure_record *failrec;
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct extent_state *state;
	int num_copies;
	int ret;

	private = 0;
	ret = count_range_bits(&BTRFS_I(inode)->io_failure_tree, &private,
				(u64)-1, 1, EXTENT_DIRTY, 0);
	if (!ret)
		return 0;

	ret = get_state_private(&BTRFS_I(inode)->io_failure_tree, start,
				&private_failure);
	if (ret)
		return 0;

	failrec = (struct io_failure_record *)(unsigned long) private_failure;
	BUG_ON(!failrec->this_mirror);

	if (failrec->in_validation) {
		/* there was no real error, just free the record */
		pr_debug("clean_io_failure: freeing dummy error at %llu\n",
			 failrec->start);
		goto out;
	}
	if (fs_info->sb->s_flags & MS_RDONLY)
		goto out;

	spin_lock(&BTRFS_I(inode)->io_tree.lock);
	state = find_first_extent_bit_state(&BTRFS_I(inode)->io_tree,
					    failrec->start,
					    EXTENT_LOCKED);
	spin_unlock(&BTRFS_I(inode)->io_tree.lock);

	if (state && state->start <= failrec->start &&
	    state->end >= failrec->start + failrec->len - 1) {
		num_copies = btrfs_num_copies(fs_info, failrec->logical,
					      failrec->len);
		if (num_copies > 1)  {
			repair_io_failure(inode, start, failrec->len,
					  failrec->logical, page,
					  pg_offset, failrec->failed_mirror);
		}
	}

out:
	free_io_failure(inode, failrec);

	return 0;
}

/*
 * Can be called when
 * - hold extent lock
 * - under ordered extent
 * - the inode is freeing
 */
void btrfs_free_io_failure_record(struct inode *inode, u64 start, u64 end)
{
	struct extent_io_tree *failure_tree = &BTRFS_I(inode)->io_failure_tree;
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

		failrec = (struct io_failure_record *)(unsigned long)state->private;
		free_extent_state(state);
		kfree(failrec);

		state = next;
	}
	spin_unlock(&failure_tree->lock);
}

int btrfs_get_io_failure_record(struct inode *inode, u64 start, u64 end,
				struct io_failure_record **failrec_ret)
{
	struct io_failure_record *failrec;
	u64 private;
	struct extent_map *em;
	struct extent_io_tree *failure_tree = &BTRFS_I(inode)->io_failure_tree;
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	int ret;
	u64 logical;

	ret = get_state_private(failure_tree, start, &private);
	if (ret) {
		failrec = kzalloc(sizeof(*failrec), GFP_NOFS);
		if (!failrec)
			return -ENOMEM;

		failrec->start = start;
		failrec->len = end - start + 1;
		failrec->this_mirror = 0;
		failrec->bio_flags = 0;
		failrec->in_validation = 0;

		read_lock(&em_tree->lock);
		em = lookup_extent_mapping(em_tree, start, failrec->len);
		if (!em) {
			read_unlock(&em_tree->lock);
			kfree(failrec);
			return -EIO;
		}

		if (em->start > start || em->start + em->len <= start) {
			free_extent_map(em);
			em = NULL;
		}
		read_unlock(&em_tree->lock);
		if (!em) {
			kfree(failrec);
			return -EIO;
		}

		logical = start - em->start;
		logical = em->block_start + logical;
		if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
			logical = em->block_start;
			failrec->bio_flags = EXTENT_BIO_COMPRESSED;
			extent_set_compress_type(&failrec->bio_flags,
						 em->compress_type);
		}

		pr_debug("Get IO Failure Record: (new) logical=%llu, start=%llu, len=%llu\n",
			 logical, start, failrec->len);

		failrec->logical = logical;
		free_extent_map(em);

		/* set the bits in the private failure tree */
		ret = set_extent_bits(failure_tree, start, end,
					EXTENT_LOCKED | EXTENT_DIRTY, GFP_NOFS);
		if (ret >= 0)
			ret = set_state_private(failure_tree, start,
						(u64)(unsigned long)failrec);
		/* set the bits in the inode's tree */
		if (ret >= 0)
			ret = set_extent_bits(tree, start, end, EXTENT_DAMAGED,
						GFP_NOFS);
		if (ret < 0) {
			kfree(failrec);
			return ret;
		}
	} else {
		failrec = (struct io_failure_record *)(unsigned long)private;
		pr_debug("Get IO Failure Record: (found) logical=%llu, start=%llu, len=%llu, validation=%d\n",
			 failrec->logical, failrec->start, failrec->len,
			 failrec->in_validation);
		/*
		 * when data can be on disk more than twice, add to failrec here
		 * (e.g. with a list for failed_mirror) to make
		 * clean_io_failure() clean all those errors at once.
		 */
	}

	*failrec_ret = failrec;

	return 0;
}

int btrfs_check_repairable(struct inode *inode, struct bio *failed_bio,
			   struct io_failure_record *failrec, int failed_mirror)
{
	int num_copies;

	num_copies = btrfs_num_copies(BTRFS_I(inode)->root->fs_info,
				      failrec->logical, failrec->len);
	if (num_copies == 1) {
		/*
		 * we only have a single copy of the data, so don't bother with
		 * all the retry and error correction code that follows. no
		 * matter what the error is, it is very likely to persist.
		 */
		pr_debug("Check Repairable: cannot repair, num_copies=%d, next_mirror %d, failed_mirror %d\n",
			 num_copies, failrec->this_mirror, failed_mirror);
		return 0;
	}

	/*
	 * there are two premises:
	 *	a) deliver good data to the caller
	 *	b) correct the bad sectors on disk
	 */
	if (failed_bio->bi_vcnt > 1) {
		/*
		 * to fulfill b), we need to know the exact failing sectors, as
		 * we don't want to rewrite any more than the failed ones. thus,
		 * we need separate read requests for the failed bio
		 *
		 * if the following BUG_ON triggers, our validation request got
		 * merged. we need separate requests for our algorithm to work.
		 */
		BUG_ON(failrec->in_validation);
		failrec->in_validation = 1;
		failrec->this_mirror = failed_mirror;
	} else {
		/*
		 * we're ready to fulfill a) and b) alongside. get a good copy
		 * of the failed sector and if we succeed, we have setup
		 * everything for repair_io_failure to do the rest for us.
		 */
		if (failrec->in_validation) {
			BUG_ON(failrec->this_mirror != failed_mirror);
			failrec->in_validation = 0;
			failrec->this_mirror = 0;
		}
		failrec->failed_mirror = failed_mirror;
		failrec->this_mirror++;
		if (failrec->this_mirror == failed_mirror)
			failrec->this_mirror++;
	}

	if (failrec->this_mirror > num_copies) {
		pr_debug("Check Repairable: (fail) num_copies=%d, next_mirror %d, failed_mirror %d\n",
			 num_copies, failrec->this_mirror, failed_mirror);
		return 0;
	}

	return 1;
}


struct bio *btrfs_create_repair_bio(struct inode *inode, struct bio *failed_bio,
				    struct io_failure_record *failrec,
				    struct page *page, int pg_offset, int icsum,
				    bio_end_io_t *endio_func, void *data)
{
	struct bio *bio;
	struct btrfs_io_bio *btrfs_failed_bio;
	struct btrfs_io_bio *btrfs_bio;

	bio = btrfs_io_bio_alloc(GFP_NOFS, 1);
	if (!bio)
		return NULL;

	bio->bi_end_io = endio_func;
	bio->bi_iter.bi_sector = failrec->logical >> 9;
	bio->bi_bdev = BTRFS_I(inode)->root->fs_info->fs_devices->latest_bdev;
	bio->bi_iter.bi_size = 0;
	bio->bi_private = data;

	btrfs_failed_bio = btrfs_io_bio(failed_bio);
	if (btrfs_failed_bio->csum) {
		struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
		u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);

		btrfs_bio = btrfs_io_bio(bio);
		btrfs_bio->csum = btrfs_bio->csum_inline;
		icsum *= csum_size;
		memcpy(btrfs_bio->csum, btrfs_failed_bio->csum + icsum,
		       csum_size);
	}

	bio_add_page(bio, page, failrec->len, pg_offset);

	return bio;
}

/*
 * this is a generic handler for readpage errors (default
 * readpage_io_failed_hook). if other copies exist, read those and write back
 * good data to the failed position. does not investigate in remapping the
 * failed extent elsewhere, hoping the device will be smart enough to do this as
 * needed
 */

static int bio_readpage_error(struct bio *failed_bio, u64 phy_offset,
			      struct page *page, u64 start, u64 end,
			      int failed_mirror)
{
	struct io_failure_record *failrec;
	struct inode *inode = page->mapping->host;
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;
	struct bio *bio;
	int read_mode;
	int ret;

	BUG_ON(failed_bio->bi_rw & REQ_WRITE);

	ret = btrfs_get_io_failure_record(inode, start, end, &failrec);
	if (ret)
		return ret;

	ret = btrfs_check_repairable(inode, failed_bio, failrec, failed_mirror);
	if (!ret) {
		free_io_failure(inode, failrec);
		return -EIO;
	}

	if (failed_bio->bi_vcnt > 1)
		read_mode = READ_SYNC | REQ_FAILFAST_DEV;
	else
		read_mode = READ_SYNC;

	phy_offset >>= inode->i_sb->s_blocksize_bits;
	bio = btrfs_create_repair_bio(inode, failed_bio, failrec, page,
				      start - page_offset(page),
				      (int)phy_offset, failed_bio->bi_end_io,
				      NULL);
	if (!bio) {
		free_io_failure(inode, failrec);
		return -EIO;
	}

	pr_debug("Repair Read Error: submitting new read[%#x] to this_mirror=%d, in_validation=%d\n",
		 read_mode, failrec->this_mirror, failrec->in_validation);

	ret = tree->ops->submit_bio_hook(inode, read_mode, bio,
					 failrec->this_mirror,
					 failrec->bio_flags, 0);
	if (ret) {
		free_io_failure(inode, failrec);
		bio_put(bio);
	}

	return ret;
}

/* lots and lots of room for performance fixes in the end_bio funcs */

int end_extent_writepage(struct page *page, int err, u64 start, u64 end)
{
	int uptodate = (err == 0);
	struct extent_io_tree *tree;
	int ret = 0;

	tree = &BTRFS_I(page->mapping->host)->io_tree;

	if (tree->ops && tree->ops->writepage_end_io_hook) {
		ret = tree->ops->writepage_end_io_hook(page, start,
					       end, NULL, uptodate);
		if (ret)
			uptodate = 0;
	}

	if (!uptodate) {
		ClearPageUptodate(page);
		SetPageError(page);
		ret = ret < 0 ? ret : -EIO;
		mapping_set_error(page->mapping, ret);
	}
	return 0;
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
	struct bio_vec *bvec;
	u64 start;
	u64 end;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		/* We always issue full-page reads, but if some block
		 * in a page fails to read, blk_update_request() will
		 * advance bv_offset and adjust bv_len to compensate.
		 * Print a warning for nonzero offsets, and an error
		 * if they don't add up to a full page.  */
		if (bvec->bv_offset || bvec->bv_len != PAGE_CACHE_SIZE) {
			if (bvec->bv_offset + bvec->bv_len != PAGE_CACHE_SIZE)
				btrfs_err(BTRFS_I(page->mapping->host)->root->fs_info,
				   "partial page write in btrfs with offset %u and length %u",
					bvec->bv_offset, bvec->bv_len);
			else
				btrfs_info(BTRFS_I(page->mapping->host)->root->fs_info,
				   "incomplete page write in btrfs with offset %u and "
				   "length %u",
					bvec->bv_offset, bvec->bv_len);
		}

		start = page_offset(page);
		end = start + bvec->bv_offset + bvec->bv_len - 1;

		if (end_extent_writepage(page, bio->bi_error, start, end))
			continue;

		end_page_writeback(page);
	}

	bio_put(bio);
}

static void
endio_readpage_release_extent(struct extent_io_tree *tree, u64 start, u64 len,
			      int uptodate)
{
	struct extent_state *cached = NULL;
	u64 end = start + len - 1;

	if (uptodate && tree->track_uptodate)
		set_extent_uptodate(tree, start, end, &cached, GFP_ATOMIC);
	unlock_extent_cached(tree, start, end, &cached, GFP_ATOMIC);
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
	int uptodate = !bio->bi_error;
	struct btrfs_io_bio *io_bio = btrfs_io_bio(bio);
	struct extent_io_tree *tree;
	u64 offset = 0;
	u64 start;
	u64 end;
	u64 len;
	u64 extent_start = 0;
	u64 extent_len = 0;
	int mirror;
	int ret;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;
		struct inode *inode = page->mapping->host;

		pr_debug("end_bio_extent_readpage: bi_sector=%llu, err=%d, "
			 "mirror=%u\n", (u64)bio->bi_iter.bi_sector,
			 bio->bi_error, io_bio->mirror_num);
		tree = &BTRFS_I(inode)->io_tree;

		/* We always issue full-page reads, but if some block
		 * in a page fails to read, blk_update_request() will
		 * advance bv_offset and adjust bv_len to compensate.
		 * Print a warning for nonzero offsets, and an error
		 * if they don't add up to a full page.  */
		if (bvec->bv_offset || bvec->bv_len != PAGE_CACHE_SIZE) {
			if (bvec->bv_offset + bvec->bv_len != PAGE_CACHE_SIZE)
				btrfs_err(BTRFS_I(page->mapping->host)->root->fs_info,
				   "partial page read in btrfs with offset %u and length %u",
					bvec->bv_offset, bvec->bv_len);
			else
				btrfs_info(BTRFS_I(page->mapping->host)->root->fs_info,
				   "incomplete page read in btrfs with offset %u and "
				   "length %u",
					bvec->bv_offset, bvec->bv_len);
		}

		start = page_offset(page);
		end = start + bvec->bv_offset + bvec->bv_len - 1;
		len = bvec->bv_len;

		mirror = io_bio->mirror_num;
		if (likely(uptodate && tree->ops &&
			   tree->ops->readpage_end_io_hook)) {
			ret = tree->ops->readpage_end_io_hook(io_bio, offset,
							      page, start, end,
							      mirror);
			if (ret)
				uptodate = 0;
			else
				clean_io_failure(inode, start, page, 0);
		}

		if (likely(uptodate))
			goto readpage_ok;

		if (tree->ops && tree->ops->readpage_io_failed_hook) {
			ret = tree->ops->readpage_io_failed_hook(page, mirror);
			if (!ret && !bio->bi_error)
				uptodate = 1;
		} else {
			/*
			 * The generic bio_readpage_error handles errors the
			 * following way: If possible, new read requests are
			 * created and submitted and will end up in
			 * end_bio_extent_readpage as well (if we're lucky, not
			 * in the !uptodate case). In that case it returns 0 and
			 * we just go on with the next page in our bio. If it
			 * can't handle the error it will return -EIO and we
			 * remain responsible for that page.
			 */
			ret = bio_readpage_error(bio, offset, page, start, end,
						 mirror);
			if (ret == 0) {
				uptodate = !bio->bi_error;
				offset += len;
				continue;
			}
		}
readpage_ok:
		if (likely(uptodate)) {
			loff_t i_size = i_size_read(inode);
			pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
			unsigned off;

			/* Zero out the end if this page straddles i_size */
			off = i_size & (PAGE_CACHE_SIZE-1);
			if (page->index == end_index && off)
				zero_user_segment(page, off, PAGE_CACHE_SIZE);
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
		offset += len;

		if (unlikely(!uptodate)) {
			if (extent_len) {
				endio_readpage_release_extent(tree,
							      extent_start,
							      extent_len, 1);
				extent_start = 0;
				extent_len = 0;
			}
			endio_readpage_release_extent(tree, start,
						      end - start + 1, 0);
		} else if (!extent_len) {
			extent_start = start;
			extent_len = end + 1 - start;
		} else if (extent_start + extent_len == start) {
			extent_len += end + 1 - start;
		} else {
			endio_readpage_release_extent(tree, extent_start,
						      extent_len, uptodate);
			extent_start = start;
			extent_len = end + 1 - start;
		}
	}

	if (extent_len)
		endio_readpage_release_extent(tree, extent_start, extent_len,
					      uptodate);
	if (io_bio->end_io)
		io_bio->end_io(io_bio, bio->bi_error);
	bio_put(bio);
}

/*
 * this allocates from the btrfs_bioset.  We're returning a bio right now
 * but you can call btrfs_io_bio for the appropriate container_of magic
 */
struct bio *
btrfs_bio_alloc(struct block_device *bdev, u64 first_sector, int nr_vecs,
		gfp_t gfp_flags)
{
	struct btrfs_io_bio *btrfs_bio;
	struct bio *bio;

	bio = bio_alloc_bioset(gfp_flags, nr_vecs, btrfs_bioset);

	if (bio == NULL && (current->flags & PF_MEMALLOC)) {
		while (!bio && (nr_vecs /= 2)) {
			bio = bio_alloc_bioset(gfp_flags,
					       nr_vecs, btrfs_bioset);
		}
	}

	if (bio) {
		bio->bi_bdev = bdev;
		bio->bi_iter.bi_sector = first_sector;
		btrfs_bio = btrfs_io_bio(bio);
		btrfs_bio->csum = NULL;
		btrfs_bio->csum_allocated = NULL;
		btrfs_bio->end_io = NULL;
	}
	return bio;
}

struct bio *btrfs_bio_clone(struct bio *bio, gfp_t gfp_mask)
{
	struct btrfs_io_bio *btrfs_bio;
	struct bio *new;

	new = bio_clone_bioset(bio, gfp_mask, btrfs_bioset);
	if (new) {
		btrfs_bio = btrfs_io_bio(new);
		btrfs_bio->csum = NULL;
		btrfs_bio->csum_allocated = NULL;
		btrfs_bio->end_io = NULL;
	}
	return new;
}

/* this also allocates from the btrfs_bioset */
struct bio *btrfs_io_bio_alloc(gfp_t gfp_mask, unsigned int nr_iovecs)
{
	struct btrfs_io_bio *btrfs_bio;
	struct bio *bio;

	bio = bio_alloc_bioset(gfp_mask, nr_iovecs, btrfs_bioset);
	if (bio) {
		btrfs_bio = btrfs_io_bio(bio);
		btrfs_bio->csum = NULL;
		btrfs_bio->csum_allocated = NULL;
		btrfs_bio->end_io = NULL;
	}
	return bio;
}


static int __must_check submit_one_bio(int rw, struct bio *bio,
				       int mirror_num, unsigned long bio_flags)
{
	int ret = 0;
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct page *page = bvec->bv_page;
	struct extent_io_tree *tree = bio->bi_private;
	u64 start;

	start = page_offset(page) + bvec->bv_offset;

	bio->bi_private = NULL;

	bio_get(bio);

	if (tree->ops && tree->ops->submit_bio_hook)
		ret = tree->ops->submit_bio_hook(page->mapping->host, rw, bio,
					   mirror_num, bio_flags, start);
	else
		btrfsic_submit_bio(rw, bio);

	bio_put(bio);
	return ret;
}

static int merge_bio(int rw, struct extent_io_tree *tree, struct page *page,
		     unsigned long offset, size_t size, struct bio *bio,
		     unsigned long bio_flags)
{
	int ret = 0;
	if (tree->ops && tree->ops->merge_bio_hook)
		ret = tree->ops->merge_bio_hook(rw, page, offset, size, bio,
						bio_flags);
	BUG_ON(ret < 0);
	return ret;

}

static int submit_extent_page(int rw, struct extent_io_tree *tree,
			      struct writeback_control *wbc,
			      struct page *page, sector_t sector,
			      size_t size, unsigned long offset,
			      struct block_device *bdev,
			      struct bio **bio_ret,
			      unsigned long max_pages,
			      bio_end_io_t end_io_func,
			      int mirror_num,
			      unsigned long prev_bio_flags,
			      unsigned long bio_flags,
			      bool force_bio_submit)
{
	int ret = 0;
	struct bio *bio;
	int contig = 0;
	int old_compressed = prev_bio_flags & EXTENT_BIO_COMPRESSED;
	size_t page_size = min_t(size_t, size, PAGE_CACHE_SIZE);

	if (bio_ret && *bio_ret) {
		bio = *bio_ret;
		if (old_compressed)
			contig = bio->bi_iter.bi_sector == sector;
		else
			contig = bio_end_sector(bio) == sector;

		if (prev_bio_flags != bio_flags || !contig ||
		    force_bio_submit ||
		    merge_bio(rw, tree, page, offset, page_size, bio, bio_flags) ||
		    bio_add_page(bio, page, page_size, offset) < page_size) {
			ret = submit_one_bio(rw, bio, mirror_num,
					     prev_bio_flags);
			if (ret < 0) {
				*bio_ret = NULL;
				return ret;
			}
			bio = NULL;
		} else {
			if (wbc)
				wbc_account_io(wbc, page, page_size);
			return 0;
		}
	}

	bio = btrfs_bio_alloc(bdev, sector, BIO_MAX_PAGES,
			GFP_NOFS | __GFP_HIGH);
	if (!bio)
		return -ENOMEM;

	bio_add_page(bio, page, page_size, offset);
	bio->bi_end_io = end_io_func;
	bio->bi_private = tree;
	if (wbc) {
		wbc_init_bio(wbc, bio);
		wbc_account_io(wbc, page, page_size);
	}

	if (bio_ret)
		*bio_ret = bio;
	else
		ret = submit_one_bio(rw, bio, mirror_num, bio_flags);

	return ret;
}

static void attach_extent_buffer_page(struct extent_buffer *eb,
				      struct page *page)
{
	if (!PagePrivate(page)) {
		SetPagePrivate(page);
		page_cache_get(page);
		set_page_private(page, (unsigned long)eb);
	} else {
		WARN_ON(page->private != (unsigned long)eb);
	}
}

void set_page_extent_mapped(struct page *page)
{
	if (!PagePrivate(page)) {
		SetPagePrivate(page);
		page_cache_get(page);
		set_page_private(page, EXTENT_PAGE_PRIVATE);
	}
}

static struct extent_map *
__get_extent_map(struct inode *inode, struct page *page, size_t pg_offset,
		 u64 start, u64 len, get_extent_t *get_extent,
		 struct extent_map **em_cached)
{
	struct extent_map *em;

	if (em_cached && *em_cached) {
		em = *em_cached;
		if (extent_map_in_tree(em) && start >= em->start &&
		    start < extent_map_end(em)) {
			atomic_inc(&em->refs);
			return em;
		}

		free_extent_map(em);
		*em_cached = NULL;
	}

	em = get_extent(inode, page, pg_offset, start, len, 0);
	if (em_cached && !IS_ERR_OR_NULL(em)) {
		BUG_ON(*em_cached);
		atomic_inc(&em->refs);
		*em_cached = em;
	}
	return em;
}
/*
 * basic readpage implementation.  Locked extent state structs are inserted
 * into the tree that are removed when the IO is done (by the end_io
 * handlers)
 * XXX JDM: This needs looking at to ensure proper page locking
 */
static int __do_readpage(struct extent_io_tree *tree,
			 struct page *page,
			 get_extent_t *get_extent,
			 struct extent_map **em_cached,
			 struct bio **bio, int mirror_num,
			 unsigned long *bio_flags, int rw,
			 u64 *prev_em_start)
{
	struct inode *inode = page->mapping->host;
	u64 start = page_offset(page);
	u64 page_end = start + PAGE_CACHE_SIZE - 1;
	u64 end;
	u64 cur = start;
	u64 extent_offset;
	u64 last_byte = i_size_read(inode);
	u64 block_start;
	u64 cur_end;
	sector_t sector;
	struct extent_map *em;
	struct block_device *bdev;
	int ret;
	int nr = 0;
	int parent_locked = *bio_flags & EXTENT_BIO_PARENT_LOCKED;
	size_t pg_offset = 0;
	size_t iosize;
	size_t disk_io_size;
	size_t blocksize = inode->i_sb->s_blocksize;
	unsigned long this_bio_flag = *bio_flags & EXTENT_BIO_PARENT_LOCKED;

	set_page_extent_mapped(page);

	end = page_end;
	if (!PageUptodate(page)) {
		if (cleancache_get_page(page) == 0) {
			BUG_ON(blocksize != PAGE_SIZE);
			unlock_extent(tree, start, end);
			goto out;
		}
	}

	if (page->index == last_byte >> PAGE_CACHE_SHIFT) {
		char *userpage;
		size_t zero_offset = last_byte & (PAGE_CACHE_SIZE - 1);

		if (zero_offset) {
			iosize = PAGE_CACHE_SIZE - zero_offset;
			userpage = kmap_atomic(page);
			memset(userpage + zero_offset, 0, iosize);
			flush_dcache_page(page);
			kunmap_atomic(userpage);
		}
	}
	while (cur <= end) {
		unsigned long pnr = (last_byte >> PAGE_CACHE_SHIFT) + 1;
		bool force_bio_submit = false;

		if (cur >= last_byte) {
			char *userpage;
			struct extent_state *cached = NULL;

			iosize = PAGE_CACHE_SIZE - pg_offset;
			userpage = kmap_atomic(page);
			memset(userpage + pg_offset, 0, iosize);
			flush_dcache_page(page);
			kunmap_atomic(userpage);
			set_extent_uptodate(tree, cur, cur + iosize - 1,
					    &cached, GFP_NOFS);
			if (!parent_locked)
				unlock_extent_cached(tree, cur,
						     cur + iosize - 1,
						     &cached, GFP_NOFS);
			break;
		}
		em = __get_extent_map(inode, page, pg_offset, cur,
				      end - cur + 1, get_extent, em_cached);
		if (IS_ERR_OR_NULL(em)) {
			SetPageError(page);
			if (!parent_locked)
				unlock_extent(tree, cur, end);
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
		if (this_bio_flag & EXTENT_BIO_COMPRESSED) {
			disk_io_size = em->block_len;
			sector = em->block_start >> 9;
		} else {
			sector = (em->block_start + extent_offset) >> 9;
			disk_io_size = iosize;
		}
		bdev = em->bdev;
		block_start = em->block_start;
		if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
			block_start = EXTENT_MAP_HOLE;

		/*
		 * If we have a file range that points to a compressed extent
		 * and it's followed by a consecutive file range that points to
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
		    *prev_em_start != em->orig_start)
			force_bio_submit = true;

		if (prev_em_start)
			*prev_em_start = em->orig_start;

		free_extent_map(em);
		em = NULL;

		/* we've found a hole, just zero and go on */
		if (block_start == EXTENT_MAP_HOLE) {
			char *userpage;
			struct extent_state *cached = NULL;

			userpage = kmap_atomic(page);
			memset(userpage + pg_offset, 0, iosize);
			flush_dcache_page(page);
			kunmap_atomic(userpage);

			set_extent_uptodate(tree, cur, cur + iosize - 1,
					    &cached, GFP_NOFS);
			if (parent_locked)
				free_extent_state(cached);
			else
				unlock_extent_cached(tree, cur,
						     cur + iosize - 1,
						     &cached, GFP_NOFS);
			cur = cur + iosize;
			pg_offset += iosize;
			continue;
		}
		/* the get_extent function already copied into the page */
		if (test_range_bit(tree, cur, cur_end,
				   EXTENT_UPTODATE, 1, NULL)) {
			check_page_uptodate(tree, page);
			if (!parent_locked)
				unlock_extent(tree, cur, cur + iosize - 1);
			cur = cur + iosize;
			pg_offset += iosize;
			continue;
		}
		/* we have an inline extent but it didn't get marked up
		 * to date.  Error out
		 */
		if (block_start == EXTENT_MAP_INLINE) {
			SetPageError(page);
			if (!parent_locked)
				unlock_extent(tree, cur, cur + iosize - 1);
			cur = cur + iosize;
			pg_offset += iosize;
			continue;
		}

		pnr -= page->index;
		ret = submit_extent_page(rw, tree, NULL, page,
					 sector, disk_io_size, pg_offset,
					 bdev, bio, pnr,
					 end_bio_extent_readpage, mirror_num,
					 *bio_flags,
					 this_bio_flag,
					 force_bio_submit);
		if (!ret) {
			nr++;
			*bio_flags = this_bio_flag;
		} else {
			SetPageError(page);
			if (!parent_locked)
				unlock_extent(tree, cur, cur + iosize - 1);
		}
		cur = cur + iosize;
		pg_offset += iosize;
	}
out:
	if (!nr) {
		if (!PageError(page))
			SetPageUptodate(page);
		unlock_page(page);
	}
	return 0;
}

static inline void __do_contiguous_readpages(struct extent_io_tree *tree,
					     struct page *pages[], int nr_pages,
					     u64 start, u64 end,
					     get_extent_t *get_extent,
					     struct extent_map **em_cached,
					     struct bio **bio, int mirror_num,
					     unsigned long *bio_flags, int rw,
					     u64 *prev_em_start)
{
	struct inode *inode;
	struct btrfs_ordered_extent *ordered;
	int index;

	inode = pages[0]->mapping->host;
	while (1) {
		lock_extent(tree, start, end);
		ordered = btrfs_lookup_ordered_range(inode, start,
						     end - start + 1);
		if (!ordered)
			break;
		unlock_extent(tree, start, end);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
	}

	for (index = 0; index < nr_pages; index++) {
		__do_readpage(tree, pages[index], get_extent, em_cached, bio,
			      mirror_num, bio_flags, rw, prev_em_start);
		page_cache_release(pages[index]);
	}
}

static void __extent_readpages(struct extent_io_tree *tree,
			       struct page *pages[],
			       int nr_pages, get_extent_t *get_extent,
			       struct extent_map **em_cached,
			       struct bio **bio, int mirror_num,
			       unsigned long *bio_flags, int rw,
			       u64 *prev_em_start)
{
	u64 start = 0;
	u64 end = 0;
	u64 page_start;
	int index;
	int first_index = 0;

	for (index = 0; index < nr_pages; index++) {
		page_start = page_offset(pages[index]);
		if (!end) {
			start = page_start;
			end = start + PAGE_CACHE_SIZE - 1;
			first_index = index;
		} else if (end + 1 == page_start) {
			end += PAGE_CACHE_SIZE;
		} else {
			__do_contiguous_readpages(tree, &pages[first_index],
						  index - first_index, start,
						  end, get_extent, em_cached,
						  bio, mirror_num, bio_flags,
						  rw, prev_em_start);
			start = page_start;
			end = start + PAGE_CACHE_SIZE - 1;
			first_index = index;
		}
	}

	if (end)
		__do_contiguous_readpages(tree, &pages[first_index],
					  index - first_index, start,
					  end, get_extent, em_cached, bio,
					  mirror_num, bio_flags, rw,
					  prev_em_start);
}

static int __extent_read_full_page(struct extent_io_tree *tree,
				   struct page *page,
				   get_extent_t *get_extent,
				   struct bio **bio, int mirror_num,
				   unsigned long *bio_flags, int rw)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_ordered_extent *ordered;
	u64 start = page_offset(page);
	u64 end = start + PAGE_CACHE_SIZE - 1;
	int ret;

	while (1) {
		lock_extent(tree, start, end);
		ordered = btrfs_lookup_ordered_extent(inode, start);
		if (!ordered)
			break;
		unlock_extent(tree, start, end);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
	}

	ret = __do_readpage(tree, page, get_extent, NULL, bio, mirror_num,
			    bio_flags, rw, NULL);
	return ret;
}

int extent_read_full_page(struct extent_io_tree *tree, struct page *page,
			    get_extent_t *get_extent, int mirror_num)
{
	struct bio *bio = NULL;
	unsigned long bio_flags = 0;
	int ret;

	ret = __extent_read_full_page(tree, page, get_extent, &bio, mirror_num,
				      &bio_flags, READ);
	if (bio)
		ret = submit_one_bio(READ, bio, mirror_num, bio_flags);
	return ret;
}

int extent_read_full_page_nolock(struct extent_io_tree *tree, struct page *page,
				 get_extent_t *get_extent, int mirror_num)
{
	struct bio *bio = NULL;
	unsigned long bio_flags = EXTENT_BIO_PARENT_LOCKED;
	int ret;

	ret = __do_readpage(tree, page, get_extent, NULL, &bio, mirror_num,
			    &bio_flags, READ, NULL);
	if (bio)
		ret = submit_one_bio(READ, bio, mirror_num, bio_flags);
	return ret;
}

static noinline void update_nr_written(struct page *page,
				      struct writeback_control *wbc,
				      unsigned long nr_written)
{
	wbc->nr_to_write -= nr_written;
	if (wbc->range_cyclic || (wbc->nr_to_write > 0 &&
	    wbc->range_start == 0 && wbc->range_end == LLONG_MAX))
		page->mapping->writeback_index = page->index + nr_written;
}

/*
 * helper for __extent_writepage, doing all of the delayed allocation setup.
 *
 * This returns 1 if our fill_delalloc function did all the work required
 * to write the page (copy into inline extent).  In this case the IO has
 * been started and the page is already unlocked.
 *
 * This returns 0 if all went well (page still locked)
 * This returns < 0 if there were errors (page still locked)
 */
static noinline_for_stack int writepage_delalloc(struct inode *inode,
			      struct page *page, struct writeback_control *wbc,
			      struct extent_page_data *epd,
			      u64 delalloc_start,
			      unsigned long *nr_written)
{
	struct extent_io_tree *tree = epd->tree;
	u64 page_end = delalloc_start + PAGE_CACHE_SIZE - 1;
	u64 nr_delalloc;
	u64 delalloc_to_write = 0;
	u64 delalloc_end = 0;
	int ret;
	int page_started = 0;

	if (epd->extent_locked || !tree->ops || !tree->ops->fill_delalloc)
		return 0;

	while (delalloc_end < page_end) {
		nr_delalloc = find_lock_delalloc_range(inode, tree,
					       page,
					       &delalloc_start,
					       &delalloc_end,
					       BTRFS_MAX_EXTENT_SIZE);
		if (nr_delalloc == 0) {
			delalloc_start = delalloc_end + 1;
			continue;
		}
		ret = tree->ops->fill_delalloc(inode, page,
					       delalloc_start,
					       delalloc_end,
					       &page_started,
					       nr_written);
		/* File system has been set read-only */
		if (ret) {
			SetPageError(page);
			/* fill_delalloc should be return < 0 for error
			 * but just in case, we use > 0 here meaning the
			 * IO is started, so we don't want to return > 0
			 * unless things are going well.
			 */
			ret = ret < 0 ? ret : -EIO;
			goto done;
		}
		/*
		 * delalloc_end is already one less than the total
		 * length, so we don't subtract one from
		 * PAGE_CACHE_SIZE
		 */
		delalloc_to_write += (delalloc_end - delalloc_start +
				      PAGE_CACHE_SIZE) >>
				      PAGE_CACHE_SHIFT;
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

	ret = 0;

done:
	return ret;
}

/*
 * helper for __extent_writepage.  This calls the writepage start hooks,
 * and does the loop to map the page into extents and bios.
 *
 * We return 1 if the IO is started and the page is unlocked,
 * 0 if all went well (page still locked)
 * < 0 if there were errors (page still locked)
 */
static noinline_for_stack int __extent_writepage_io(struct inode *inode,
				 struct page *page,
				 struct writeback_control *wbc,
				 struct extent_page_data *epd,
				 loff_t i_size,
				 unsigned long nr_written,
				 int write_flags, int *nr_ret)
{
	struct extent_io_tree *tree = epd->tree;
	u64 start = page_offset(page);
	u64 page_end = start + PAGE_CACHE_SIZE - 1;
	u64 end;
	u64 cur = start;
	u64 extent_offset;
	u64 block_start;
	u64 iosize;
	sector_t sector;
	struct extent_state *cached_state = NULL;
	struct extent_map *em;
	struct block_device *bdev;
	size_t pg_offset = 0;
	size_t blocksize;
	int ret = 0;
	int nr = 0;
	bool compressed;

	if (tree->ops && tree->ops->writepage_start_hook) {
		ret = tree->ops->writepage_start_hook(page, start,
						      page_end);
		if (ret) {
			/* Fixup worker will requeue */
			if (ret == -EBUSY)
				wbc->pages_skipped++;
			else
				redirty_page_for_writepage(wbc, page);

			update_nr_written(page, wbc, nr_written);
			unlock_page(page);
			ret = 1;
			goto done_unlocked;
		}
	}

	/*
	 * we don't want to touch the inode after unlocking the page,
	 * so we update the mapping writeback index now
	 */
	update_nr_written(page, wbc, nr_written + 1);

	end = page_end;
	if (i_size <= start) {
		if (tree->ops && tree->ops->writepage_end_io_hook)
			tree->ops->writepage_end_io_hook(page, start,
							 page_end, NULL, 1);
		goto done;
	}

	blocksize = inode->i_sb->s_blocksize;

	while (cur <= end) {
		u64 em_end;
		if (cur >= i_size) {
			if (tree->ops && tree->ops->writepage_end_io_hook)
				tree->ops->writepage_end_io_hook(page, cur,
							 page_end, NULL, 1);
			break;
		}
		em = epd->get_extent(inode, page, pg_offset, cur,
				     end - cur + 1, 1);
		if (IS_ERR_OR_NULL(em)) {
			SetPageError(page);
			ret = PTR_ERR_OR_ZERO(em);
			break;
		}

		extent_offset = cur - em->start;
		em_end = extent_map_end(em);
		BUG_ON(em_end <= cur);
		BUG_ON(end < cur);
		iosize = min(em_end - cur, end - cur + 1);
		iosize = ALIGN(iosize, blocksize);
		sector = (em->block_start + extent_offset) >> 9;
		bdev = em->bdev;
		block_start = em->block_start;
		compressed = test_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
		free_extent_map(em);
		em = NULL;

		/*
		 * compressed and inline extents are written through other
		 * paths in the FS
		 */
		if (compressed || block_start == EXTENT_MAP_HOLE ||
		    block_start == EXTENT_MAP_INLINE) {
			/*
			 * end_io notification does not happen here for
			 * compressed extents
			 */
			if (!compressed && tree->ops &&
			    tree->ops->writepage_end_io_hook)
				tree->ops->writepage_end_io_hook(page, cur,
							 cur + iosize - 1,
							 NULL, 1);
			else if (compressed) {
				/* we don't want to end_page_writeback on
				 * a compressed extent.  this happens
				 * elsewhere
				 */
				nr++;
			}

			cur += iosize;
			pg_offset += iosize;
			continue;
		}

		if (tree->ops && tree->ops->writepage_io_hook) {
			ret = tree->ops->writepage_io_hook(page, cur,
						cur + iosize - 1);
		} else {
			ret = 0;
		}
		if (ret) {
			SetPageError(page);
		} else {
			unsigned long max_nr = (i_size >> PAGE_CACHE_SHIFT) + 1;

			set_range_writeback(tree, cur, cur + iosize - 1);
			if (!PageWriteback(page)) {
				btrfs_err(BTRFS_I(inode)->root->fs_info,
					   "page %lu not writeback, cur %llu end %llu",
				       page->index, cur, end);
			}

			ret = submit_extent_page(write_flags, tree, wbc, page,
						 sector, iosize, pg_offset,
						 bdev, &epd->bio, max_nr,
						 end_bio_extent_writepage,
						 0, 0, 0, false);
			if (ret)
				SetPageError(page);
		}
		cur = cur + iosize;
		pg_offset += iosize;
		nr++;
	}
done:
	*nr_ret = nr;

done_unlocked:

	/* drop our reference on any cached states */
	free_extent_state(cached_state);
	return ret;
}

/*
 * the writepage semantics are similar to regular writepage.  extent
 * records are inserted to lock ranges in the tree, and as dirty areas
 * are found, they are marked writeback.  Then the lock bits are removed
 * and the end_io handler clears the writeback ranges
 */
static int __extent_writepage(struct page *page, struct writeback_control *wbc,
			      void *data)
{
	struct inode *inode = page->mapping->host;
	struct extent_page_data *epd = data;
	u64 start = page_offset(page);
	u64 page_end = start + PAGE_CACHE_SIZE - 1;
	int ret;
	int nr = 0;
	size_t pg_offset = 0;
	loff_t i_size = i_size_read(inode);
	unsigned long end_index = i_size >> PAGE_CACHE_SHIFT;
	int write_flags;
	unsigned long nr_written = 0;

	if (wbc->sync_mode == WB_SYNC_ALL)
		write_flags = WRITE_SYNC;
	else
		write_flags = WRITE;

	trace___extent_writepage(page, inode, wbc);

	WARN_ON(!PageLocked(page));

	ClearPageError(page);

	pg_offset = i_size & (PAGE_CACHE_SIZE - 1);
	if (page->index > end_index ||
	   (page->index == end_index && !pg_offset)) {
		page->mapping->a_ops->invalidatepage(page, 0, PAGE_CACHE_SIZE);
		unlock_page(page);
		return 0;
	}

	if (page->index == end_index) {
		char *userpage;

		userpage = kmap_atomic(page);
		memset(userpage + pg_offset, 0,
		       PAGE_CACHE_SIZE - pg_offset);
		kunmap_atomic(userpage);
		flush_dcache_page(page);
	}

	pg_offset = 0;

	set_page_extent_mapped(page);

	ret = writepage_delalloc(inode, page, wbc, epd, start, &nr_written);
	if (ret == 1)
		goto done_unlocked;
	if (ret)
		goto done;

	ret = __extent_writepage_io(inode, page, wbc, epd,
				    i_size, nr_written, write_flags, &nr);
	if (ret == 1)
		goto done_unlocked;

done:
	if (nr == 0) {
		/* make sure the mapping tag for page dirty gets cleared */
		set_page_writeback(page);
		end_page_writeback(page);
	}
	if (PageError(page)) {
		ret = ret < 0 ? ret : -EIO;
		end_extent_writepage(page, ret, start, page_end);
	}
	unlock_page(page);
	return ret;

done_unlocked:
	return 0;
}

void wait_on_extent_buffer_writeback(struct extent_buffer *eb)
{
	wait_on_bit_io(&eb->bflags, EXTENT_BUFFER_WRITEBACK,
		       TASK_UNINTERRUPTIBLE);
}

static noinline_for_stack int
lock_extent_buffer_for_io(struct extent_buffer *eb,
			  struct btrfs_fs_info *fs_info,
			  struct extent_page_data *epd)
{
	unsigned long i, num_pages;
	int flush = 0;
	int ret = 0;

	if (!btrfs_try_tree_write_lock(eb)) {
		flush = 1;
		flush_write_bio(epd);
		btrfs_tree_lock(eb);
	}

	if (test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags)) {
		btrfs_tree_unlock(eb);
		if (!epd->sync_io)
			return 0;
		if (!flush) {
			flush_write_bio(epd);
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
		__percpu_counter_add(&fs_info->dirty_metadata_bytes,
				     -eb->len,
				     fs_info->dirty_metadata_batch);
		ret = 1;
	} else {
		spin_unlock(&eb->refs_lock);
	}

	btrfs_tree_unlock(eb);

	if (!ret)
		return ret;

	num_pages = num_extent_pages(eb->start, eb->len);
	for (i = 0; i < num_pages; i++) {
		struct page *p = eb->pages[i];

		if (!trylock_page(p)) {
			if (!flush) {
				flush_write_bio(epd);
				flush = 1;
			}
			lock_page(p);
		}
	}

	return ret;
}

static void end_extent_buffer_writeback(struct extent_buffer *eb)
{
	clear_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags);
	smp_mb__after_atomic();
	wake_up_bit(&eb->bflags, EXTENT_BUFFER_WRITEBACK);
}

static void set_btree_ioerr(struct page *page)
{
	struct extent_buffer *eb = (struct extent_buffer *)page->private;
	struct btrfs_inode *btree_ino = BTRFS_I(eb->fs_info->btree_inode);

	SetPageError(page);
	if (test_and_set_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags))
		return;

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
		set_bit(BTRFS_INODE_BTREE_ERR, &btree_ino->runtime_flags);
		break;
	case 0:
		set_bit(BTRFS_INODE_BTREE_LOG1_ERR, &btree_ino->runtime_flags);
		break;
	case 1:
		set_bit(BTRFS_INODE_BTREE_LOG2_ERR, &btree_ino->runtime_flags);
		break;
	default:
		BUG(); /* unexpected, logic error */
	}
}

static void end_bio_extent_buffer_writepage(struct bio *bio)
{
	struct bio_vec *bvec;
	struct extent_buffer *eb;
	int i, done;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		eb = (struct extent_buffer *)page->private;
		BUG_ON(!eb);
		done = atomic_dec_and_test(&eb->io_pages);

		if (bio->bi_error ||
		    test_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags)) {
			ClearPageUptodate(page);
			set_btree_ioerr(page);
		}

		end_page_writeback(page);

		if (!done)
			continue;

		end_extent_buffer_writeback(eb);
	}

	bio_put(bio);
}

static noinline_for_stack int write_one_eb(struct extent_buffer *eb,
			struct btrfs_fs_info *fs_info,
			struct writeback_control *wbc,
			struct extent_page_data *epd)
{
	struct block_device *bdev = fs_info->fs_devices->latest_bdev;
	struct extent_io_tree *tree = &BTRFS_I(fs_info->btree_inode)->io_tree;
	u64 offset = eb->start;
	unsigned long i, num_pages;
	unsigned long bio_flags = 0;
	int rw = (epd->sync_io ? WRITE_SYNC : WRITE) | REQ_META;
	int ret = 0;

	clear_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags);
	num_pages = num_extent_pages(eb->start, eb->len);
	atomic_set(&eb->io_pages, num_pages);
	if (btrfs_header_owner(eb) == BTRFS_TREE_LOG_OBJECTID)
		bio_flags = EXTENT_BIO_TREE_LOG;

	for (i = 0; i < num_pages; i++) {
		struct page *p = eb->pages[i];

		clear_page_dirty_for_io(p);
		set_page_writeback(p);
		ret = submit_extent_page(rw, tree, wbc, p, offset >> 9,
					 PAGE_CACHE_SIZE, 0, bdev, &epd->bio,
					 -1, end_bio_extent_buffer_writepage,
					 0, epd->bio_flags, bio_flags, false);
		epd->bio_flags = bio_flags;
		if (ret) {
			set_btree_ioerr(p);
			end_page_writeback(p);
			if (atomic_sub_and_test(num_pages - i, &eb->io_pages))
				end_extent_buffer_writeback(eb);
			ret = -EIO;
			break;
		}
		offset += PAGE_CACHE_SIZE;
		update_nr_written(p, wbc, 1);
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

int btree_write_cache_pages(struct address_space *mapping,
				   struct writeback_control *wbc)
{
	struct extent_io_tree *tree = &BTRFS_I(mapping->host)->io_tree;
	struct btrfs_fs_info *fs_info = BTRFS_I(mapping->host)->root->fs_info;
	struct extent_buffer *eb, *prev_eb = NULL;
	struct extent_page_data epd = {
		.bio = NULL,
		.tree = tree,
		.extent_locked = 0,
		.sync_io = wbc->sync_mode == WB_SYNC_ALL,
		.bio_flags = 0,
	};
	int ret = 0;
	int done = 0;
	int nr_to_write_done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	int scanned = 0;
	int tag;

	pagevec_init(&pvec, 0);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		scanned = 1;
	}
	if (wbc->sync_mode == WB_SYNC_ALL)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL)
		tag_pages_for_writeback(mapping, index, end);
	while (!done && !nr_to_write_done && (index <= end) &&
	       (nr_pages = pagevec_lookup_tag(&pvec, mapping, &index, tag,
			min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1))) {
		unsigned i;

		scanned = 1;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			if (!PagePrivate(page))
				continue;

			if (!wbc->range_cyclic && page->index > end) {
				done = 1;
				break;
			}

			spin_lock(&mapping->private_lock);
			if (!PagePrivate(page)) {
				spin_unlock(&mapping->private_lock);
				continue;
			}

			eb = (struct extent_buffer *)page->private;

			/*
			 * Shouldn't happen and normally this would be a BUG_ON
			 * but no sense in crashing the users box for something
			 * we can survive anyway.
			 */
			if (WARN_ON(!eb)) {
				spin_unlock(&mapping->private_lock);
				continue;
			}

			if (eb == prev_eb) {
				spin_unlock(&mapping->private_lock);
				continue;
			}

			ret = atomic_inc_not_zero(&eb->refs);
			spin_unlock(&mapping->private_lock);
			if (!ret)
				continue;

			prev_eb = eb;
			ret = lock_extent_buffer_for_io(eb, fs_info, &epd);
			if (!ret) {
				free_extent_buffer(eb);
				continue;
			}

			ret = write_one_eb(eb, fs_info, wbc, &epd);
			if (ret) {
				done = 1;
				free_extent_buffer(eb);
				break;
			}
			free_extent_buffer(eb);

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
	flush_write_bio(&epd);
	return ret;
}

/**
 * write_cache_pages - walk the list of dirty pages of the given address space and write all of them.
 * @mapping: address space structure to write
 * @wbc: subtract the number of written pages from *@wbc->nr_to_write
 * @writepage: function called for each page
 * @data: data passed to writepage function
 *
 * If a page is already under I/O, write_cache_pages() skips it, even
 * if it's dirty.  This is desirable behaviour for memory-cleaning writeback,
 * but it is INCORRECT for data-integrity system calls such as fsync().  fsync()
 * and msync() need to guarantee that all the data which was dirty at the time
 * the call was made get new I/O started against them.  If wbc->sync_mode is
 * WB_SYNC_ALL then we were called for data integrity and we must wait for
 * existing IO to complete.
 */
static int extent_write_cache_pages(struct extent_io_tree *tree,
			     struct address_space *mapping,
			     struct writeback_control *wbc,
			     writepage_t writepage, void *data,
			     void (*flush_fn)(void *))
{
	struct inode *inode = mapping->host;
	int ret = 0;
	int done = 0;
	int err = 0;
	int nr_to_write_done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	int scanned = 0;
	int tag;

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

	pagevec_init(&pvec, 0);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		scanned = 1;
	}
	if (wbc->sync_mode == WB_SYNC_ALL)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL)
		tag_pages_for_writeback(mapping, index, end);
	while (!done && !nr_to_write_done && (index <= end) &&
	       (nr_pages = pagevec_lookup_tag(&pvec, mapping, &index, tag,
			min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1))) {
		unsigned i;

		scanned = 1;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			/*
			 * At this point we hold neither mapping->tree_lock nor
			 * lock on the page itself: the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or even
			 * swizzled back from swapper_space to tmpfs file
			 * mapping
			 */
			if (!trylock_page(page)) {
				flush_fn(data);
				lock_page(page);
			}

			if (unlikely(page->mapping != mapping)) {
				unlock_page(page);
				continue;
			}

			if (!wbc->range_cyclic && page->index > end) {
				done = 1;
				unlock_page(page);
				continue;
			}

			if (wbc->sync_mode != WB_SYNC_NONE) {
				if (PageWriteback(page))
					flush_fn(data);
				wait_on_page_writeback(page);
			}

			if (PageWriteback(page) ||
			    !clear_page_dirty_for_io(page)) {
				unlock_page(page);
				continue;
			}

			ret = (*writepage)(page, wbc, data);

			if (unlikely(ret == AOP_WRITEPAGE_ACTIVATE)) {
				unlock_page(page);
				ret = 0;
			}
			if (!err && ret < 0)
				err = ret;

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
	if (!scanned && !done && !err) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;
		goto retry;
	}
	btrfs_add_delayed_iput(inode);
	return err;
}

static void flush_epd_write_bio(struct extent_page_data *epd)
{
	if (epd->bio) {
		int rw = WRITE;
		int ret;

		if (epd->sync_io)
			rw = WRITE_SYNC;

		ret = submit_one_bio(rw, epd->bio, 0, epd->bio_flags);
		BUG_ON(ret < 0); /* -ENOMEM */
		epd->bio = NULL;
	}
}

static noinline void flush_write_bio(void *data)
{
	struct extent_page_data *epd = data;
	flush_epd_write_bio(epd);
}

int extent_write_full_page(struct extent_io_tree *tree, struct page *page,
			  get_extent_t *get_extent,
			  struct writeback_control *wbc)
{
	int ret;
	struct extent_page_data epd = {
		.bio = NULL,
		.tree = tree,
		.get_extent = get_extent,
		.extent_locked = 0,
		.sync_io = wbc->sync_mode == WB_SYNC_ALL,
		.bio_flags = 0,
	};

	ret = __extent_writepage(page, wbc, &epd);

	flush_epd_write_bio(&epd);
	return ret;
}

int extent_write_locked_range(struct extent_io_tree *tree, struct inode *inode,
			      u64 start, u64 end, get_extent_t *get_extent,
			      int mode)
{
	int ret = 0;
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	unsigned long nr_pages = (end - start + PAGE_CACHE_SIZE) >>
		PAGE_CACHE_SHIFT;

	struct extent_page_data epd = {
		.bio = NULL,
		.tree = tree,
		.get_extent = get_extent,
		.extent_locked = 1,
		.sync_io = mode == WB_SYNC_ALL,
		.bio_flags = 0,
	};
	struct writeback_control wbc_writepages = {
		.sync_mode	= mode,
		.nr_to_write	= nr_pages * 2,
		.range_start	= start,
		.range_end	= end + 1,
	};

	while (start <= end) {
		page = find_get_page(mapping, start >> PAGE_CACHE_SHIFT);
		if (clear_page_dirty_for_io(page))
			ret = __extent_writepage(page, &wbc_writepages, &epd);
		else {
			if (tree->ops && tree->ops->writepage_end_io_hook)
				tree->ops->writepage_end_io_hook(page, start,
						 start + PAGE_CACHE_SIZE - 1,
						 NULL, 1);
			unlock_page(page);
		}
		page_cache_release(page);
		start += PAGE_CACHE_SIZE;
	}

	flush_epd_write_bio(&epd);
	return ret;
}

int extent_writepages(struct extent_io_tree *tree,
		      struct address_space *mapping,
		      get_extent_t *get_extent,
		      struct writeback_control *wbc)
{
	int ret = 0;
	struct extent_page_data epd = {
		.bio = NULL,
		.tree = tree,
		.get_extent = get_extent,
		.extent_locked = 0,
		.sync_io = wbc->sync_mode == WB_SYNC_ALL,
		.bio_flags = 0,
	};

	ret = extent_write_cache_pages(tree, mapping, wbc,
				       __extent_writepage, &epd,
				       flush_write_bio);
	flush_epd_write_bio(&epd);
	return ret;
}

int extent_readpages(struct extent_io_tree *tree,
		     struct address_space *mapping,
		     struct list_head *pages, unsigned nr_pages,
		     get_extent_t get_extent)
{
	struct bio *bio = NULL;
	unsigned page_idx;
	unsigned long bio_flags = 0;
	struct page *pagepool[16];
	struct page *page;
	struct extent_map *em_cached = NULL;
	int nr = 0;
	u64 prev_em_start = (u64)-1;

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		page = list_entry(pages->prev, struct page, lru);

		prefetchw(&page->flags);
		list_del(&page->lru);
		if (add_to_page_cache_lru(page, mapping,
					page->index, GFP_NOFS)) {
			page_cache_release(page);
			continue;
		}

		pagepool[nr++] = page;
		if (nr < ARRAY_SIZE(pagepool))
			continue;
		__extent_readpages(tree, pagepool, nr, get_extent, &em_cached,
				   &bio, 0, &bio_flags, READ, &prev_em_start);
		nr = 0;
	}
	if (nr)
		__extent_readpages(tree, pagepool, nr, get_extent, &em_cached,
				   &bio, 0, &bio_flags, READ, &prev_em_start);

	if (em_cached)
		free_extent_map(em_cached);

	BUG_ON(!list_empty(pages));
	if (bio)
		return submit_one_bio(READ, bio, 0, bio_flags);
	return 0;
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
	u64 end = start + PAGE_CACHE_SIZE - 1;
	size_t blocksize = page->mapping->host->i_sb->s_blocksize;

	start += ALIGN(offset, blocksize);
	if (start > end)
		return 0;

	lock_extent_bits(tree, start, end, 0, &cached_state);
	wait_on_page_writeback(page);
	clear_extent_bit(tree, start, end,
			 EXTENT_LOCKED | EXTENT_DIRTY | EXTENT_DELALLOC |
			 EXTENT_DO_ACCOUNTING,
			 1, 1, &cached_state, GFP_NOFS);
	return 0;
}

/*
 * a helper for releasepage, this tests for areas of the page that
 * are locked or under IO and drops the related state bits if it is safe
 * to drop the page.
 */
static int try_release_extent_state(struct extent_map_tree *map,
				    struct extent_io_tree *tree,
				    struct page *page, gfp_t mask)
{
	u64 start = page_offset(page);
	u64 end = start + PAGE_CACHE_SIZE - 1;
	int ret = 1;

	if (test_range_bit(tree, start, end,
			   EXTENT_IOBITS, 0, NULL))
		ret = 0;
	else {
		if ((mask & GFP_NOFS) == GFP_NOFS)
			mask = GFP_NOFS;
		/*
		 * at this point we can safely clear everything except the
		 * locked bit and the nodatasum bit
		 */
		ret = clear_extent_bit(tree, start, end,
				 ~(EXTENT_LOCKED | EXTENT_NODATASUM),
				 0, 0, NULL, mask);

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
int try_release_extent_mapping(struct extent_map_tree *map,
			       struct extent_io_tree *tree, struct page *page,
			       gfp_t mask)
{
	struct extent_map *em;
	u64 start = page_offset(page);
	u64 end = start + PAGE_CACHE_SIZE - 1;

	if (gfpflags_allow_blocking(mask) &&
	    page->mapping->host->i_size > 16 * 1024 * 1024) {
		u64 len;
		while (start <= end) {
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
			if (!test_range_bit(tree, em->start,
					    extent_map_end(em) - 1,
					    EXTENT_LOCKED | EXTENT_WRITEBACK,
					    0, NULL)) {
				remove_extent_mapping(map, em);
				/* once for the rb tree */
				free_extent_map(em);
			}
			start = extent_map_end(em);
			write_unlock(&map->lock);

			/* once for us */
			free_extent_map(em);
		}
	}
	return try_release_extent_state(map, tree, page, mask);
}

/*
 * helper function for fiemap, which doesn't want to see any holes.
 * This maps until we find something past 'last'
 */
static struct extent_map *get_extent_skip_holes(struct inode *inode,
						u64 offset,
						u64 last,
						get_extent_t *get_extent)
{
	u64 sectorsize = BTRFS_I(inode)->root->sectorsize;
	struct extent_map *em;
	u64 len;

	if (offset >= last)
		return NULL;

	while (1) {
		len = last - offset;
		if (len == 0)
			break;
		len = ALIGN(len, sectorsize);
		em = get_extent(inode, NULL, 0, offset, len, 0);
		if (IS_ERR_OR_NULL(em))
			return em;

		/* if this isn't a hole return it */
		if (!test_bit(EXTENT_FLAG_VACANCY, &em->flags) &&
		    em->block_start != EXTENT_MAP_HOLE) {
			return em;
		}

		/* this is a hole, advance to the next extent */
		offset = extent_map_end(em);
		free_extent_map(em);
		if (offset >= last)
			break;
	}
	return NULL;
}

int extent_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len, get_extent_t *get_extent)
{
	int ret = 0;
	u64 off = start;
	u64 max = start + len;
	u32 flags = 0;
	u32 found_type;
	u64 last;
	u64 last_for_get_extent = 0;
	u64 disko = 0;
	u64 isize = i_size_read(inode);
	struct btrfs_key found_key;
	struct extent_map *em = NULL;
	struct extent_state *cached_state = NULL;
	struct btrfs_path *path;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int end = 0;
	u64 em_start = 0;
	u64 em_len = 0;
	u64 em_end = 0;

	if (len == 0)
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->leave_spinning = 1;

	start = round_down(start, BTRFS_I(inode)->root->sectorsize);
	len = round_up(max, BTRFS_I(inode)->root->sectorsize) - start;

	/*
	 * lookup the last file extent.  We're not using i_size here
	 * because there might be preallocation past i_size
	 */
	ret = btrfs_lookup_file_extent(NULL, root, path, btrfs_ino(inode), -1,
				       0);
	if (ret < 0) {
		btrfs_free_path(path);
		return ret;
	}
	WARN_ON(!ret);
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

	lock_extent_bits(&BTRFS_I(inode)->io_tree, start, start + len - 1, 0,
			 &cached_state);

	em = get_extent_skip_holes(inode, start, last_for_get_extent,
				   get_extent);
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
		disko = 0;
		flags = 0;

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

			disko = em->block_start + offset_in_extent;

			/*
			 * As btrfs supports shared space, this information
			 * can be exported to userspace tools via
			 * flag FIEMAP_EXTENT_SHARED.  If fi_extents_max == 0
			 * then we're just getting a count and we can skip the
			 * lookup stuff.
			 */
			ret = btrfs_check_shared(NULL, root->fs_info,
						 root->objectid,
						 btrfs_ino(inode), bytenr);
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
		em = get_extent_skip_holes(inode, off, last_for_get_extent,
					   get_extent);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			goto out;
		}
		if (!em) {
			flags |= FIEMAP_EXTENT_LAST;
			end = 1;
		}
		ret = fiemap_fill_next_extent(fieinfo, em_start, disko,
					      em_len, flags);
		if (ret) {
			if (ret == 1)
				ret = 0;
			goto out_free;
		}
	}
out_free:
	free_extent_map(em);
out:
	btrfs_free_path(path);
	unlock_extent_cached(&BTRFS_I(inode)->io_tree, start, start + len - 1,
			     &cached_state, GFP_NOFS);
	return ret;
}

static void __free_extent_buffer(struct extent_buffer *eb)
{
	btrfs_leak_debug_del(&eb->leak_list);
	kmem_cache_free(extent_buffer_cache, eb);
}

int extent_buffer_under_io(struct extent_buffer *eb)
{
	return (atomic_read(&eb->io_pages) ||
		test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags) ||
		test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
}

/*
 * Helper for releasing extent buffer page.
 */
static void btrfs_release_extent_buffer_page(struct extent_buffer *eb)
{
	unsigned long index;
	struct page *page;
	int mapped = !test_bit(EXTENT_BUFFER_DUMMY, &eb->bflags);

	BUG_ON(extent_buffer_under_io(eb));

	index = num_extent_pages(eb->start, eb->len);
	if (index == 0)
		return;

	do {
		index--;
		page = eb->pages[index];
		if (!page)
			continue;
		if (mapped)
			spin_lock(&page->mapping->private_lock);
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
			ClearPagePrivate(page);
			set_page_private(page, 0);
			/* One for the page private */
			page_cache_release(page);
		}

		if (mapped)
			spin_unlock(&page->mapping->private_lock);

		/* One for when we alloced the page */
		page_cache_release(page);
	} while (index != 0);
}

/*
 * Helper for releasing the extent buffer.
 */
static inline void btrfs_release_extent_buffer(struct extent_buffer *eb)
{
	btrfs_release_extent_buffer_page(eb);
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
	rwlock_init(&eb->lock);
	atomic_set(&eb->write_locks, 0);
	atomic_set(&eb->read_locks, 0);
	atomic_set(&eb->blocking_readers, 0);
	atomic_set(&eb->blocking_writers, 0);
	atomic_set(&eb->spinning_readers, 0);
	atomic_set(&eb->spinning_writers, 0);
	eb->lock_nested = 0;
	init_waitqueue_head(&eb->write_lock_wq);
	init_waitqueue_head(&eb->read_lock_wq);

	btrfs_leak_debug_add(&eb->leak_list, &buffers);

	spin_lock_init(&eb->refs_lock);
	atomic_set(&eb->refs, 1);
	atomic_set(&eb->io_pages, 0);

	/*
	 * Sanity checks, currently the maximum is 64k covered by 16x 4k pages
	 */
	BUILD_BUG_ON(BTRFS_MAX_METADATA_BLOCKSIZE
		> MAX_INLINE_EXTENT_BUFFER_SIZE);
	BUG_ON(len > MAX_INLINE_EXTENT_BUFFER_SIZE);

	return eb;
}

struct extent_buffer *btrfs_clone_extent_buffer(struct extent_buffer *src)
{
	unsigned long i;
	struct page *p;
	struct extent_buffer *new;
	unsigned long num_pages = num_extent_pages(src->start, src->len);

	new = __alloc_extent_buffer(src->fs_info, src->start, src->len);
	if (new == NULL)
		return NULL;

	for (i = 0; i < num_pages; i++) {
		p = alloc_page(GFP_NOFS);
		if (!p) {
			btrfs_release_extent_buffer(new);
			return NULL;
		}
		attach_extent_buffer_page(new, p);
		WARN_ON(PageDirty(p));
		SetPageUptodate(p);
		new->pages[i] = p;
	}

	copy_extent_buffer(new, src, 0, 0, src->len);
	set_bit(EXTENT_BUFFER_UPTODATE, &new->bflags);
	set_bit(EXTENT_BUFFER_DUMMY, &new->bflags);

	return new;
}

struct extent_buffer *alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						u64 start)
{
	struct extent_buffer *eb;
	unsigned long len;
	unsigned long num_pages;
	unsigned long i;

	if (!fs_info) {
		/*
		 * Called only from tests that don't always have a fs_info
		 * available, but we know that nodesize is 4096
		 */
		len = 4096;
	} else {
		len = fs_info->tree_root->nodesize;
	}
	num_pages = num_extent_pages(0, len);

	eb = __alloc_extent_buffer(fs_info, start, len);
	if (!eb)
		return NULL;

	for (i = 0; i < num_pages; i++) {
		eb->pages[i] = alloc_page(GFP_NOFS);
		if (!eb->pages[i])
			goto err;
	}
	set_extent_buffer_uptodate(eb);
	btrfs_set_header_nritems(eb, 0);
	set_bit(EXTENT_BUFFER_DUMMY, &eb->bflags);

	return eb;
err:
	for (; i > 0; i--)
		__free_page(eb->pages[i - 1]);
	__free_extent_buffer(eb);
	return NULL;
}

static void check_buffer_tree_ref(struct extent_buffer *eb)
{
	int refs;
	/* the ref bit is tricky.  We have to make sure it is set
	 * if we have the buffer dirty.   Otherwise the
	 * code to free a buffer can end up dropping a dirty
	 * page
	 *
	 * Once the ref bit is set, it won't go away while the
	 * buffer is dirty or in writeback, and it also won't
	 * go away while we have the reference count on the
	 * eb bumped.
	 *
	 * We can't just set the ref bit without bumping the
	 * ref on the eb because free_extent_buffer might
	 * see the ref bit and try to clear it.  If this happens
	 * free_extent_buffer might end up dropping our original
	 * ref by mistake and freeing the page before we are able
	 * to add one more ref.
	 *
	 * So bump the ref count first, then set the bit.  If someone
	 * beat us to it, drop the ref we added.
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
	unsigned long num_pages, i;

	check_buffer_tree_ref(eb);

	num_pages = num_extent_pages(eb->start, eb->len);
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

	rcu_read_lock();
	eb = radix_tree_lookup(&fs_info->buffer_radix,
			       start >> PAGE_CACHE_SHIFT);
	if (eb && atomic_inc_not_zero(&eb->refs)) {
		rcu_read_unlock();
		/*
		 * Lock our eb's refs_lock to avoid races with
		 * free_extent_buffer. When we get our eb it might be flagged
		 * with EXTENT_BUFFER_STALE and another task running
		 * free_extent_buffer might have seen that flag set,
		 * eb->refs == 2, that the buffer isn't under IO (dirty and
		 * writeback flags not set) and it's still in the tree (flag
		 * EXTENT_BUFFER_TREE_REF set), therefore being in the process
		 * of decrementing the extent buffer's reference count twice.
		 * So here we could race and increment the eb's reference count,
		 * clear its stale flag, mark it as dirty and drop our reference
		 * before the other task finishes executing free_extent_buffer,
		 * which would later result in an attempt to free an extent
		 * buffer that is dirty.
		 */
		if (test_bit(EXTENT_BUFFER_STALE, &eb->bflags)) {
			spin_lock(&eb->refs_lock);
			spin_unlock(&eb->refs_lock);
		}
		mark_extent_buffer_accessed(eb, NULL);
		return eb;
	}
	rcu_read_unlock();

	return NULL;
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
		return NULL;
	eb->fs_info = fs_info;
again:
	ret = radix_tree_preload(GFP_NOFS & ~__GFP_HIGHMEM);
	if (ret)
		goto free_eb;
	spin_lock(&fs_info->buffer_lock);
	ret = radix_tree_insert(&fs_info->buffer_radix,
				start >> PAGE_CACHE_SHIFT, eb);
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

	/*
	 * We will free dummy extent buffer's if they come into
	 * free_extent_buffer with a ref count of 2, but if we are using this we
	 * want the buffers to stay in memory until we're done with them, so
	 * bump the ref count again.
	 */
	atomic_inc(&eb->refs);
	return eb;
free_eb:
	btrfs_release_extent_buffer(eb);
	return exists;
}
#endif

struct extent_buffer *alloc_extent_buffer(struct btrfs_fs_info *fs_info,
					  u64 start)
{
	unsigned long len = fs_info->tree_root->nodesize;
	unsigned long num_pages = num_extent_pages(start, len);
	unsigned long i;
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	struct extent_buffer *eb;
	struct extent_buffer *exists = NULL;
	struct page *p;
	struct address_space *mapping = fs_info->btree_inode->i_mapping;
	int uptodate = 1;
	int ret;

	eb = find_extent_buffer(fs_info, start);
	if (eb)
		return eb;

	eb = __alloc_extent_buffer(fs_info, start, len);
	if (!eb)
		return NULL;

	for (i = 0; i < num_pages; i++, index++) {
		p = find_or_create_page(mapping, index, GFP_NOFS|__GFP_NOFAIL);
		if (!p)
			goto free_eb;

		spin_lock(&mapping->private_lock);
		if (PagePrivate(p)) {
			/*
			 * We could have already allocated an eb for this page
			 * and attached one so lets see if we can get a ref on
			 * the existing eb, and if we can we know it's good and
			 * we can just return that one, else we know we can just
			 * overwrite page->private.
			 */
			exists = (struct extent_buffer *)p->private;
			if (atomic_inc_not_zero(&exists->refs)) {
				spin_unlock(&mapping->private_lock);
				unlock_page(p);
				page_cache_release(p);
				mark_extent_buffer_accessed(exists, p);
				goto free_eb;
			}
			exists = NULL;

			/*
			 * Do this so attach doesn't complain and we need to
			 * drop the ref the old guy had.
			 */
			ClearPagePrivate(p);
			WARN_ON(PageDirty(p));
			page_cache_release(p);
		}
		attach_extent_buffer_page(eb, p);
		spin_unlock(&mapping->private_lock);
		WARN_ON(PageDirty(p));
		eb->pages[i] = p;
		if (!PageUptodate(p))
			uptodate = 0;

		/*
		 * see below about how we avoid a nasty race with release page
		 * and why we unlock later
		 */
	}
	if (uptodate)
		set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
again:
	ret = radix_tree_preload(GFP_NOFS & ~__GFP_HIGHMEM);
	if (ret)
		goto free_eb;

	spin_lock(&fs_info->buffer_lock);
	ret = radix_tree_insert(&fs_info->buffer_radix,
				start >> PAGE_CACHE_SHIFT, eb);
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
	 * there is a race where release page may have
	 * tried to find this extent buffer in the radix
	 * but failed.  It will tell the VM it is safe to
	 * reclaim the, and it will clear the page private bit.
	 * We must make sure to set the page private bit properly
	 * after the extent buffer is in the radix tree so
	 * it doesn't get lost
	 */
	SetPageChecked(eb->pages[0]);
	for (i = 1; i < num_pages; i++) {
		p = eb->pages[i];
		ClearPageChecked(p);
		unlock_page(p);
	}
	unlock_page(eb->pages[0]);
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

/* Expects to have eb->eb_lock already held */
static int release_extent_buffer(struct extent_buffer *eb)
{
	WARN_ON(atomic_read(&eb->refs) == 0);
	if (atomic_dec_and_test(&eb->refs)) {
		if (test_and_clear_bit(EXTENT_BUFFER_IN_TREE, &eb->bflags)) {
			struct btrfs_fs_info *fs_info = eb->fs_info;

			spin_unlock(&eb->refs_lock);

			spin_lock(&fs_info->buffer_lock);
			radix_tree_delete(&fs_info->buffer_radix,
					  eb->start >> PAGE_CACHE_SHIFT);
			spin_unlock(&fs_info->buffer_lock);
		} else {
			spin_unlock(&eb->refs_lock);
		}

		/* Should be safe to release our pages at this point */
		btrfs_release_extent_buffer_page(eb);
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
		if (unlikely(test_bit(EXTENT_BUFFER_DUMMY, &eb->bflags))) {
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
		if (refs <= 3)
			break;
		old = atomic_cmpxchg(&eb->refs, refs, refs - 1);
		if (old == refs)
			return;
	}

	spin_lock(&eb->refs_lock);
	if (atomic_read(&eb->refs) == 2 &&
	    test_bit(EXTENT_BUFFER_DUMMY, &eb->bflags))
		atomic_dec(&eb->refs);

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

void clear_extent_buffer_dirty(struct extent_buffer *eb)
{
	unsigned long i;
	unsigned long num_pages;
	struct page *page;

	num_pages = num_extent_pages(eb->start, eb->len);

	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		if (!PageDirty(page))
			continue;

		lock_page(page);
		WARN_ON(!PagePrivate(page));

		clear_page_dirty_for_io(page);
		spin_lock_irq(&page->mapping->tree_lock);
		if (!PageDirty(page)) {
			radix_tree_tag_clear(&page->mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_DIRTY);
		}
		spin_unlock_irq(&page->mapping->tree_lock);
		ClearPageError(page);
		unlock_page(page);
	}
	WARN_ON(atomic_read(&eb->refs) == 0);
}

int set_extent_buffer_dirty(struct extent_buffer *eb)
{
	unsigned long i;
	unsigned long num_pages;
	int was_dirty = 0;

	check_buffer_tree_ref(eb);

	was_dirty = test_and_set_bit(EXTENT_BUFFER_DIRTY, &eb->bflags);

	num_pages = num_extent_pages(eb->start, eb->len);
	WARN_ON(atomic_read(&eb->refs) == 0);
	WARN_ON(!test_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags));

	for (i = 0; i < num_pages; i++)
		set_page_dirty(eb->pages[i]);
	return was_dirty;
}

int clear_extent_buffer_uptodate(struct extent_buffer *eb)
{
	unsigned long i;
	struct page *page;
	unsigned long num_pages;

	clear_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	num_pages = num_extent_pages(eb->start, eb->len);
	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		if (page)
			ClearPageUptodate(page);
	}
	return 0;
}

int set_extent_buffer_uptodate(struct extent_buffer *eb)
{
	unsigned long i;
	struct page *page;
	unsigned long num_pages;

	set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	num_pages = num_extent_pages(eb->start, eb->len);
	for (i = 0; i < num_pages; i++) {
		page = eb->pages[i];
		SetPageUptodate(page);
	}
	return 0;
}

int extent_buffer_uptodate(struct extent_buffer *eb)
{
	return test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
}

int read_extent_buffer_pages(struct extent_io_tree *tree,
			     struct extent_buffer *eb, u64 start, int wait,
			     get_extent_t *get_extent, int mirror_num)
{
	unsigned long i;
	unsigned long start_i;
	struct page *page;
	int err;
	int ret = 0;
	int locked_pages = 0;
	int all_uptodate = 1;
	unsigned long num_pages;
	unsigned long num_reads = 0;
	struct bio *bio = NULL;
	unsigned long bio_flags = 0;

	if (test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags))
		return 0;

	if (start) {
		WARN_ON(start < eb->start);
		start_i = (start >> PAGE_CACHE_SHIFT) -
			(eb->start >> PAGE_CACHE_SHIFT);
	} else {
		start_i = 0;
	}

	num_pages = num_extent_pages(eb->start, eb->len);
	for (i = start_i; i < num_pages; i++) {
		page = eb->pages[i];
		if (wait == WAIT_NONE) {
			if (!trylock_page(page))
				goto unlock_exit;
		} else {
			lock_page(page);
		}
		locked_pages++;
		if (!PageUptodate(page)) {
			num_reads++;
			all_uptodate = 0;
		}
	}
	if (all_uptodate) {
		if (start_i == 0)
			set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
		goto unlock_exit;
	}

	clear_bit(EXTENT_BUFFER_READ_ERR, &eb->bflags);
	eb->read_mirror = 0;
	atomic_set(&eb->io_pages, num_reads);
	for (i = start_i; i < num_pages; i++) {
		page = eb->pages[i];
		if (!PageUptodate(page)) {
			ClearPageError(page);
			err = __extent_read_full_page(tree, page,
						      get_extent, &bio,
						      mirror_num, &bio_flags,
						      READ | REQ_META);
			if (err)
				ret = err;
		} else {
			unlock_page(page);
		}
	}

	if (bio) {
		err = submit_one_bio(READ | REQ_META, bio, mirror_num,
				     bio_flags);
		if (err)
			return err;
	}

	if (ret || wait != WAIT_COMPLETE)
		return ret;

	for (i = start_i; i < num_pages; i++) {
		page = eb->pages[i];
		wait_on_page_locked(page);
		if (!PageUptodate(page))
			ret = -EIO;
	}

	return ret;

unlock_exit:
	i = start_i;
	while (locked_pages > 0) {
		page = eb->pages[i];
		i++;
		unlock_page(page);
		locked_pages--;
	}
	return ret;
}

void read_extent_buffer(struct extent_buffer *eb, void *dstv,
			unsigned long start,
			unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *dst = (char *)dstv;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & (PAGE_CACHE_SIZE - 1);

	while (len > 0) {
		page = eb->pages[i];

		cur = min(len, (PAGE_CACHE_SIZE - offset));
		kaddr = page_address(page);
		memcpy(dst, kaddr + offset, cur);

		dst += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}

int read_extent_buffer_to_user(struct extent_buffer *eb, void __user *dstv,
			unsigned long start,
			unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char __user *dst = (char __user *)dstv;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;
	int ret = 0;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & (PAGE_CACHE_SIZE - 1);

	while (len > 0) {
		page = eb->pages[i];

		cur = min(len, (PAGE_CACHE_SIZE - offset));
		kaddr = page_address(page);
		if (copy_to_user(dst, kaddr + offset, cur)) {
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

int map_private_extent_buffer(struct extent_buffer *eb, unsigned long start,
			       unsigned long min_len, char **map,
			       unsigned long *map_start,
			       unsigned long *map_len)
{
	size_t offset = start & (PAGE_CACHE_SIZE - 1);
	char *kaddr;
	struct page *p;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;
	unsigned long end_i = (start_offset + start + min_len - 1) >>
		PAGE_CACHE_SHIFT;

	if (i != end_i)
		return -EINVAL;

	if (i == 0) {
		offset = start_offset;
		*map_start = 0;
	} else {
		offset = 0;
		*map_start = ((u64)i << PAGE_CACHE_SHIFT) - start_offset;
	}

	if (start + min_len > eb->len) {
		WARN(1, KERN_ERR "btrfs bad mapping eb start %llu len %lu, "
		       "wanted %lu %lu\n",
		       eb->start, eb->len, start, min_len);
		return -EINVAL;
	}

	p = eb->pages[i];
	kaddr = page_address(p);
	*map = kaddr + offset;
	*map_len = PAGE_CACHE_SIZE - offset;
	return 0;
}

int memcmp_extent_buffer(struct extent_buffer *eb, const void *ptrv,
			  unsigned long start,
			  unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *ptr = (char *)ptrv;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;
	int ret = 0;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & (PAGE_CACHE_SIZE - 1);

	while (len > 0) {
		page = eb->pages[i];

		cur = min(len, (PAGE_CACHE_SIZE - offset));

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

void write_extent_buffer(struct extent_buffer *eb, const void *srcv,
			 unsigned long start, unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *src = (char *)srcv;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & (PAGE_CACHE_SIZE - 1);

	while (len > 0) {
		page = eb->pages[i];
		WARN_ON(!PageUptodate(page));

		cur = min(len, PAGE_CACHE_SIZE - offset);
		kaddr = page_address(page);
		memcpy(kaddr + offset, src, cur);

		src += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}

void memset_extent_buffer(struct extent_buffer *eb, char c,
			  unsigned long start, unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & (PAGE_CACHE_SIZE - 1);

	while (len > 0) {
		page = eb->pages[i];
		WARN_ON(!PageUptodate(page));

		cur = min(len, PAGE_CACHE_SIZE - offset);
		kaddr = page_address(page);
		memset(kaddr + offset, c, cur);

		len -= cur;
		offset = 0;
		i++;
	}
}

void copy_extent_buffer(struct extent_buffer *dst, struct extent_buffer *src,
			unsigned long dst_offset, unsigned long src_offset,
			unsigned long len)
{
	u64 dst_len = dst->len;
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	size_t start_offset = dst->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + dst_offset) >> PAGE_CACHE_SHIFT;

	WARN_ON(src->len != dst_len);

	offset = (start_offset + dst_offset) &
		(PAGE_CACHE_SIZE - 1);

	while (len > 0) {
		page = dst->pages[i];
		WARN_ON(!PageUptodate(page));

		cur = min(len, (unsigned long)(PAGE_CACHE_SIZE - offset));

		kaddr = page_address(page);
		read_extent_buffer(src, kaddr + offset, src_offset, cur);

		src_offset += cur;
		len -= cur;
		offset = 0;
		i++;
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

void memcpy_extent_buffer(struct extent_buffer *dst, unsigned long dst_offset,
			   unsigned long src_offset, unsigned long len)
{
	size_t cur;
	size_t dst_off_in_page;
	size_t src_off_in_page;
	size_t start_offset = dst->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long dst_i;
	unsigned long src_i;

	if (src_offset + len > dst->len) {
		btrfs_err(dst->fs_info,
			"memmove bogus src_offset %lu move "
		       "len %lu dst len %lu", src_offset, len, dst->len);
		BUG_ON(1);
	}
	if (dst_offset + len > dst->len) {
		btrfs_err(dst->fs_info,
			"memmove bogus dst_offset %lu move "
		       "len %lu dst len %lu", dst_offset, len, dst->len);
		BUG_ON(1);
	}

	while (len > 0) {
		dst_off_in_page = (start_offset + dst_offset) &
			(PAGE_CACHE_SIZE - 1);
		src_off_in_page = (start_offset + src_offset) &
			(PAGE_CACHE_SIZE - 1);

		dst_i = (start_offset + dst_offset) >> PAGE_CACHE_SHIFT;
		src_i = (start_offset + src_offset) >> PAGE_CACHE_SHIFT;

		cur = min(len, (unsigned long)(PAGE_CACHE_SIZE -
					       src_off_in_page));
		cur = min_t(unsigned long, cur,
			(unsigned long)(PAGE_CACHE_SIZE - dst_off_in_page));

		copy_pages(dst->pages[dst_i], dst->pages[src_i],
			   dst_off_in_page, src_off_in_page, cur);

		src_offset += cur;
		dst_offset += cur;
		len -= cur;
	}
}

void memmove_extent_buffer(struct extent_buffer *dst, unsigned long dst_offset,
			   unsigned long src_offset, unsigned long len)
{
	size_t cur;
	size_t dst_off_in_page;
	size_t src_off_in_page;
	unsigned long dst_end = dst_offset + len - 1;
	unsigned long src_end = src_offset + len - 1;
	size_t start_offset = dst->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long dst_i;
	unsigned long src_i;

	if (src_offset + len > dst->len) {
		btrfs_err(dst->fs_info, "memmove bogus src_offset %lu move "
		       "len %lu len %lu", src_offset, len, dst->len);
		BUG_ON(1);
	}
	if (dst_offset + len > dst->len) {
		btrfs_err(dst->fs_info, "memmove bogus dst_offset %lu move "
		       "len %lu len %lu", dst_offset, len, dst->len);
		BUG_ON(1);
	}
	if (dst_offset < src_offset) {
		memcpy_extent_buffer(dst, dst_offset, src_offset, len);
		return;
	}
	while (len > 0) {
		dst_i = (start_offset + dst_end) >> PAGE_CACHE_SHIFT;
		src_i = (start_offset + src_end) >> PAGE_CACHE_SHIFT;

		dst_off_in_page = (start_offset + dst_end) &
			(PAGE_CACHE_SIZE - 1);
		src_off_in_page = (start_offset + src_end) &
			(PAGE_CACHE_SIZE - 1);

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

int try_release_extent_buffer(struct page *page)
{
	struct extent_buffer *eb;

	/*
	 * We need to make sure noboody is attaching this page to an eb right
	 * now.
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
