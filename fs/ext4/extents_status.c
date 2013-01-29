/*
 *  fs/ext4/extents_status.c
 *
 * Written by Yongqiang Yang <xiaoqiangnk@gmail.com>
 * Modified by
 *	Allison Henderson <achender@linux.vnet.ibm.com>
 *	Hugh Dickins <hughd@google.com>
 *	Zheng Liu <wenqing.lz@taobao.com>
 *
 * Ext4 extents status tree core functions.
 */
#include <linux/rbtree.h>
#include "ext4.h"
#include "extents_status.h"
#include "ext4_extents.h"

#include <trace/events/ext4.h>

/*
 * According to previous discussion in Ext4 Developer Workshop, we
 * will introduce a new structure called io tree to track all extent
 * status in order to solve some problems that we have met
 * (e.g. Reservation space warning), and provide extent-level locking.
 * Delay extent tree is the first step to achieve this goal.  It is
 * original built by Yongqiang Yang.  At that time it is called delay
 * extent tree, whose goal is only track delay extent in memory to
 * simplify the implementation of fiemap and bigalloc, and introduce
 * lseek SEEK_DATA/SEEK_HOLE support.  That is why it is still called
 * delay extent tree at the following comment.  But for better
 * understand what it does, it has been rename to extent status tree.
 *
 * Currently the first step has been done.  All delay extents are
 * tracked in the tree.  It maintains the delay extent when a delay
 * allocation is issued, and the delay extent is written out or
 * invalidated.  Therefore the implementation of fiemap and bigalloc
 * are simplified, and SEEK_DATA/SEEK_HOLE are introduced.
 *
 * The following comment describes the implemenmtation of extent
 * status tree and future works.
 */

/*
 * extents status tree implementation for ext4.
 *
 *
 * ==========================================================================
 * Extents status encompass delayed extents and extent locks
 *
 * 1. Why delayed extent implementation ?
 *
 * Without delayed extent, ext4 identifies a delayed extent by looking
 * up page cache, this has several deficiencies - complicated, buggy,
 * and inefficient code.
 *
 * FIEMAP, SEEK_HOLE/DATA, bigalloc, punch hole and writeout all need
 * to know if a block or a range of blocks are belonged to a delayed
 * extent.
 *
 * Let us have a look at how they do without delayed extents implementation.
 *   --	FIEMAP
 *	FIEMAP looks up page cache to identify delayed allocations from holes.
 *
 *   --	SEEK_HOLE/DATA
 *	SEEK_HOLE/DATA has the same problem as FIEMAP.
 *
 *   --	bigalloc
 *	bigalloc looks up page cache to figure out if a block is
 *	already under delayed allocation or not to determine whether
 *	quota reserving is needed for the cluster.
 *
 *   -- punch hole
 *	punch hole looks up page cache to identify a delayed extent.
 *
 *   --	writeout
 *	Writeout looks up whole page cache to see if a buffer is
 *	mapped, If there are not very many delayed buffers, then it is
 *	time comsuming.
 *
 * With delayed extents implementation, FIEMAP, SEEK_HOLE/DATA,
 * bigalloc and writeout can figure out if a block or a range of
 * blocks is under delayed allocation(belonged to a delayed extent) or
 * not by searching the delayed extent tree.
 *
 *
 * ==========================================================================
 * 2. ext4 delayed extents impelmentation
 *
 *   --	delayed extent
 *	A delayed extent is a range of blocks which are contiguous
 *	logically and under delayed allocation.  Unlike extent in
 *	ext4, delayed extent in ext4 is a in-memory struct, there is
 *	no corresponding on-disk data.  There is no limit on length of
 *	delayed extent, so a delayed extent can contain as many blocks
 *	as they are contiguous logically.
 *
 *   --	delayed extent tree
 *	Every inode has a delayed extent tree and all under delayed
 *	allocation blocks are added to the tree as delayed extents.
 *	Delayed extents in the tree are ordered by logical block no.
 *
 *   --	operations on a delayed extent tree
 *	There are three operations on a delayed extent tree: find next
 *	delayed extent, adding a space(a range of blocks) and removing
 *	a space.
 *
 *   --	race on a delayed extent tree
 *	Delayed extent tree is protected inode->i_es_lock.
 *
 *
 * ==========================================================================
 * 3. performance analysis
 *   --	overhead
 *	1. There is a cache extent for write access, so if writes are
 *	not very random, adding space operaions are in O(1) time.
 *
 *   --	gain
 *	2. Code is much simpler, more readable, more maintainable and
 *	more efficient.
 *
 *
 * ==========================================================================
 * 4. TODO list
 *   -- Track all extent status
 *
 *   -- Improve get block process
 *
 *   -- Extent-level locking
 */

static struct kmem_cache *ext4_es_cachep;

int __init ext4_init_es(void)
{
	ext4_es_cachep = KMEM_CACHE(extent_status, SLAB_RECLAIM_ACCOUNT);
	if (ext4_es_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void ext4_exit_es(void)
{
	if (ext4_es_cachep)
		kmem_cache_destroy(ext4_es_cachep);
}

void ext4_es_init_tree(struct ext4_es_tree *tree)
{
	tree->root = RB_ROOT;
	tree->cache_es = NULL;
}

#ifdef ES_DEBUG__
static void ext4_es_print_tree(struct inode *inode)
{
	struct ext4_es_tree *tree;
	struct rb_node *node;

	printk(KERN_DEBUG "status extents for inode %lu:", inode->i_ino);
	tree = &EXT4_I(inode)->i_es_tree;
	node = rb_first(&tree->root);
	while (node) {
		struct extent_status *es;
		es = rb_entry(node, struct extent_status, rb_node);
		printk(KERN_DEBUG " [%u/%u)", es->start, es->len);
		node = rb_next(node);
	}
	printk(KERN_DEBUG "\n");
}
#else
#define ext4_es_print_tree(inode)
#endif

static inline ext4_lblk_t extent_status_end(struct extent_status *es)
{
	BUG_ON(es->start + es->len < es->start);
	return es->start + es->len - 1;
}

/*
 * search through the tree for an delayed extent with a given offset.  If
 * it can't be found, try to find next extent.
 */
static struct extent_status *__es_tree_search(struct rb_root *root,
					      ext4_lblk_t offset)
{
	struct rb_node *node = root->rb_node;
	struct extent_status *es = NULL;

	while (node) {
		es = rb_entry(node, struct extent_status, rb_node);
		if (offset < es->start)
			node = node->rb_left;
		else if (offset > extent_status_end(es))
			node = node->rb_right;
		else
			return es;
	}

	if (es && offset < es->start)
		return es;

	if (es && offset > extent_status_end(es)) {
		node = rb_next(&es->rb_node);
		return node ? rb_entry(node, struct extent_status, rb_node) :
			      NULL;
	}

	return NULL;
}

/*
 * ext4_es_find_extent: find the 1st delayed extent covering @es->start
 * if it exists, otherwise, the next extent after @es->start.
 *
 * @inode: the inode which owns delayed extents
 * @es: delayed extent that we found
 *
 * Returns the first block of the next extent after es, otherwise
 * EXT_MAX_BLOCKS if no delay extent is found.
 * Delayed extent is returned via @es.
 */
ext4_lblk_t ext4_es_find_extent(struct inode *inode, struct extent_status *es)
{
	struct ext4_es_tree *tree = NULL;
	struct extent_status *es1 = NULL;
	struct rb_node *node;
	ext4_lblk_t ret = EXT_MAX_BLOCKS;

	trace_ext4_es_find_extent_enter(inode, es->start);

	read_lock(&EXT4_I(inode)->i_es_lock);
	tree = &EXT4_I(inode)->i_es_tree;

	/* find delay extent in cache firstly */
	if (tree->cache_es) {
		es1 = tree->cache_es;
		if (in_range(es->start, es1->start, es1->len)) {
			es_debug("%u cached by [%u/%u)\n",
				 es->start, es1->start, es1->len);
			goto out;
		}
	}

	es->len = 0;
	es1 = __es_tree_search(&tree->root, es->start);

out:
	if (es1) {
		tree->cache_es = es1;
		es->start = es1->start;
		es->len = es1->len;
		node = rb_next(&es1->rb_node);
		if (node) {
			es1 = rb_entry(node, struct extent_status, rb_node);
			ret = es1->start;
		}
	}

	read_unlock(&EXT4_I(inode)->i_es_lock);

	trace_ext4_es_find_extent_exit(inode, es, ret);
	return ret;
}

static struct extent_status *
ext4_es_alloc_extent(ext4_lblk_t start, ext4_lblk_t len)
{
	struct extent_status *es;
	es = kmem_cache_alloc(ext4_es_cachep, GFP_ATOMIC);
	if (es == NULL)
		return NULL;
	es->start = start;
	es->len = len;
	return es;
}

static void ext4_es_free_extent(struct extent_status *es)
{
	kmem_cache_free(ext4_es_cachep, es);
}

static struct extent_status *
ext4_es_try_to_merge_left(struct ext4_es_tree *tree, struct extent_status *es)
{
	struct extent_status *es1;
	struct rb_node *node;

	node = rb_prev(&es->rb_node);
	if (!node)
		return es;

	es1 = rb_entry(node, struct extent_status, rb_node);
	if (es->start == extent_status_end(es1) + 1) {
		es1->len += es->len;
		rb_erase(&es->rb_node, &tree->root);
		ext4_es_free_extent(es);
		es = es1;
	}

	return es;
}

static struct extent_status *
ext4_es_try_to_merge_right(struct ext4_es_tree *tree, struct extent_status *es)
{
	struct extent_status *es1;
	struct rb_node *node;

	node = rb_next(&es->rb_node);
	if (!node)
		return es;

	es1 = rb_entry(node, struct extent_status, rb_node);
	if (es1->start == extent_status_end(es) + 1) {
		es->len += es1->len;
		rb_erase(node, &tree->root);
		ext4_es_free_extent(es1);
	}

	return es;
}

static int __es_insert_extent(struct ext4_es_tree *tree, ext4_lblk_t offset,
			      ext4_lblk_t len)
{
	struct rb_node **p = &tree->root.rb_node;
	struct rb_node *parent = NULL;
	struct extent_status *es;
	ext4_lblk_t end = offset + len - 1;

	BUG_ON(end < offset);
	es = tree->cache_es;
	if (es && offset == (extent_status_end(es) + 1)) {
		es_debug("cached by [%u/%u)\n", es->start, es->len);
		es->len += len;
		es = ext4_es_try_to_merge_right(tree, es);
		goto out;
	} else if (es && es->start == end + 1) {
		es_debug("cached by [%u/%u)\n", es->start, es->len);
		es->start = offset;
		es->len += len;
		es = ext4_es_try_to_merge_left(tree, es);
		goto out;
	} else if (es && es->start <= offset &&
		   end <= extent_status_end(es)) {
		es_debug("cached by [%u/%u)\n", es->start, es->len);
		goto out;
	}

	while (*p) {
		parent = *p;
		es = rb_entry(parent, struct extent_status, rb_node);

		if (offset < es->start) {
			if (es->start == end + 1) {
				es->start = offset;
				es->len += len;
				es = ext4_es_try_to_merge_left(tree, es);
				goto out;
			}
			p = &(*p)->rb_left;
		} else if (offset > extent_status_end(es)) {
			if (offset == extent_status_end(es) + 1) {
				es->len += len;
				es = ext4_es_try_to_merge_right(tree, es);
				goto out;
			}
			p = &(*p)->rb_right;
		} else {
			if (extent_status_end(es) <= end)
				es->len = offset - es->start + len;
			goto out;
		}
	}

	es = ext4_es_alloc_extent(offset, len);
	if (!es)
		return -ENOMEM;
	rb_link_node(&es->rb_node, parent, p);
	rb_insert_color(&es->rb_node, &tree->root);

out:
	tree->cache_es = es;
	return 0;
}

/*
 * ext4_es_insert_extent() adds a space to a delayed extent tree.
 * Caller holds inode->i_es_lock.
 *
 * ext4_es_insert_extent is called by ext4_da_write_begin and
 * ext4_es_remove_extent.
 *
 * Return 0 on success, error code on failure.
 */
int ext4_es_insert_extent(struct inode *inode, ext4_lblk_t offset,
			  ext4_lblk_t len)
{
	struct ext4_es_tree *tree;
	int err = 0;

	trace_ext4_es_insert_extent(inode, offset, len);
	es_debug("add [%u/%u) to extent status tree of inode %lu\n",
		 offset, len, inode->i_ino);

	write_lock(&EXT4_I(inode)->i_es_lock);
	tree = &EXT4_I(inode)->i_es_tree;
	err = __es_insert_extent(tree, offset, len);
	write_unlock(&EXT4_I(inode)->i_es_lock);

	ext4_es_print_tree(inode);

	return err;
}

/*
 * ext4_es_remove_extent() removes a space from a delayed extent tree.
 * Caller holds inode->i_es_lock.
 *
 * Return 0 on success, error code on failure.
 */
int ext4_es_remove_extent(struct inode *inode, ext4_lblk_t offset,
			  ext4_lblk_t len)
{
	struct rb_node *node;
	struct ext4_es_tree *tree;
	struct extent_status *es;
	struct extent_status orig_es;
	ext4_lblk_t len1, len2, end;
	int err = 0;

	trace_ext4_es_remove_extent(inode, offset, len);
	es_debug("remove [%u/%u) from extent status tree of inode %lu\n",
		 offset, len, inode->i_ino);

	end = offset + len - 1;
	BUG_ON(end < offset);
	write_lock(&EXT4_I(inode)->i_es_lock);
	tree = &EXT4_I(inode)->i_es_tree;
	es = __es_tree_search(&tree->root, offset);
	if (!es)
		goto out;
	if (es->start > end)
		goto out;

	/* Simply invalidate cache_es. */
	tree->cache_es = NULL;

	orig_es.start = es->start;
	orig_es.len = es->len;
	len1 = offset > es->start ? offset - es->start : 0;
	len2 = extent_status_end(es) > end ?
	       extent_status_end(es) - end : 0;
	if (len1 > 0)
		es->len = len1;
	if (len2 > 0) {
		if (len1 > 0) {
			err = __es_insert_extent(tree, end + 1, len2);
			if (err) {
				es->start = orig_es.start;
				es->len = orig_es.len;
				goto out;
			}
		} else {
			es->start = end + 1;
			es->len = len2;
		}
		goto out;
	}

	if (len1 > 0) {
		node = rb_next(&es->rb_node);
		if (node)
			es = rb_entry(node, struct extent_status, rb_node);
		else
			es = NULL;
	}

	while (es && extent_status_end(es) <= end) {
		node = rb_next(&es->rb_node);
		rb_erase(&es->rb_node, &tree->root);
		ext4_es_free_extent(es);
		if (!node) {
			es = NULL;
			break;
		}
		es = rb_entry(node, struct extent_status, rb_node);
	}

	if (es && es->start < end + 1) {
		len1 = extent_status_end(es) - end;
		es->start = end + 1;
		es->len = len1;
	}

out:
	write_unlock(&EXT4_I(inode)->i_es_lock);
	ext4_es_print_tree(inode);
	return err;
}
