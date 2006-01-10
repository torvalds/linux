/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * uptodate.c
 *
 * Tracking the up-to-date-ness of a local buffer_head with respect to
 * the cluster.
 *
 * Copyright (C) 2002, 2004, 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
 *
 * Standard buffer head caching flags (uptodate, etc) are insufficient
 * in a clustered environment - a buffer may be marked up to date on
 * our local node but could have been modified by another cluster
 * member. As a result an additional (and performant) caching scheme
 * is required. A further requirement is that we consume as little
 * memory as possible - we never pin buffer_head structures in order
 * to cache them.
 *
 * We track the existence of up to date buffers on the inodes which
 * are associated with them. Because we don't want to pin
 * buffer_heads, this is only a (strong) hint and several other checks
 * are made in the I/O path to ensure that we don't use a stale or
 * invalid buffer without going to disk:
 *	- buffer_jbd is used liberally - if a bh is in the journal on
 *	  this node then it *must* be up to date.
 *	- the standard buffer_uptodate() macro is used to detect buffers
 *	  which may be invalid (even if we have an up to date tracking
 * 	  item for them)
 *
 * For a full understanding of how this code works together, one
 * should read the callers in dlmglue.c, the I/O functions in
 * buffer_head_io.c and ocfs2_journal_access in journal.c
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/buffer_head.h>
#include <linux/rbtree.h>
#include <linux/jbd.h>

#define MLOG_MASK_PREFIX ML_UPTODATE

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "inode.h"
#include "uptodate.h"

struct ocfs2_meta_cache_item {
	struct rb_node	c_node;
	sector_t	c_block;
};

static kmem_cache_t *ocfs2_uptodate_cachep = NULL;

void ocfs2_metadata_cache_init(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_caching_info *ci = &oi->ip_metadata_cache;

	oi->ip_flags |= OCFS2_INODE_CACHE_INLINE;
	ci->ci_num_cached = 0;
}

/* No lock taken here as 'root' is not expected to be visible to other
 * processes. */
static unsigned int ocfs2_purge_copied_metadata_tree(struct rb_root *root)
{
	unsigned int purged = 0;
	struct rb_node *node;
	struct ocfs2_meta_cache_item *item;

	while ((node = rb_last(root)) != NULL) {
		item = rb_entry(node, struct ocfs2_meta_cache_item, c_node);

		mlog(0, "Purge item %llu\n",
		     (unsigned long long) item->c_block);

		rb_erase(&item->c_node, root);
		kmem_cache_free(ocfs2_uptodate_cachep, item);

		purged++;
	}
	return purged;
}

/* Called from locking and called from ocfs2_clear_inode. Dump the
 * cache for a given inode.
 *
 * This function is a few more lines longer than necessary due to some
 * accounting done here, but I think it's worth tracking down those
 * bugs sooner -- Mark */
void ocfs2_metadata_cache_purge(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	unsigned int tree, to_purge, purged;
	struct ocfs2_caching_info *ci = &oi->ip_metadata_cache;
	struct rb_root root = RB_ROOT;

	spin_lock(&oi->ip_lock);
	tree = !(oi->ip_flags & OCFS2_INODE_CACHE_INLINE);
	to_purge = ci->ci_num_cached;

	mlog(0, "Purge %u %s items from Inode %"MLFu64"\n", to_purge,
	     tree ? "array" : "tree", oi->ip_blkno);

	/* If we're a tree, save off the root so that we can safely
	 * initialize the cache. We do the work to free tree members
	 * without the spinlock. */
	if (tree)
		root = ci->ci_cache.ci_tree;

	ocfs2_metadata_cache_init(inode);
	spin_unlock(&oi->ip_lock);

	purged = ocfs2_purge_copied_metadata_tree(&root);
	/* If possible, track the number wiped so that we can more
	 * easily detect counting errors. Unfortunately, this is only
	 * meaningful for trees. */
	if (tree && purged != to_purge)
		mlog(ML_ERROR, "Inode %"MLFu64", count = %u, purged = %u\n",
		     oi->ip_blkno, to_purge, purged);
}

/* Returns the index in the cache array, -1 if not found.
 * Requires ip_lock. */
static int ocfs2_search_cache_array(struct ocfs2_caching_info *ci,
				    sector_t item)
{
	int i;

	for (i = 0; i < ci->ci_num_cached; i++) {
		if (item == ci->ci_cache.ci_array[i])
			return i;
	}

	return -1;
}

/* Returns the cache item if found, otherwise NULL.
 * Requires ip_lock. */
static struct ocfs2_meta_cache_item *
ocfs2_search_cache_tree(struct ocfs2_caching_info *ci,
			sector_t block)
{
	struct rb_node * n = ci->ci_cache.ci_tree.rb_node;
	struct ocfs2_meta_cache_item *item = NULL;

	while (n) {
		item = rb_entry(n, struct ocfs2_meta_cache_item, c_node);

		if (block < item->c_block)
			n = n->rb_left;
		else if (block > item->c_block)
			n = n->rb_right;
		else
			return item;
	}

	return NULL;
}

static int ocfs2_buffer_cached(struct ocfs2_inode_info *oi,
			       struct buffer_head *bh)
{
	int index = -1;
	struct ocfs2_meta_cache_item *item = NULL;

	spin_lock(&oi->ip_lock);

	mlog(0, "Inode %"MLFu64", query block %llu (inline = %u)\n",
	     oi->ip_blkno, (unsigned long long) bh->b_blocknr,
	     !!(oi->ip_flags & OCFS2_INODE_CACHE_INLINE));

	if (oi->ip_flags & OCFS2_INODE_CACHE_INLINE)
		index = ocfs2_search_cache_array(&oi->ip_metadata_cache,
						 bh->b_blocknr);
	else
		item = ocfs2_search_cache_tree(&oi->ip_metadata_cache,
					       bh->b_blocknr);

	spin_unlock(&oi->ip_lock);

	mlog(0, "index = %d, item = %p\n", index, item);

	return (index != -1) || (item != NULL);
}

/* Warning: even if it returns true, this does *not* guarantee that
 * the block is stored in our inode metadata cache. */
int ocfs2_buffer_uptodate(struct inode *inode,
			  struct buffer_head *bh)
{
	/* Doesn't matter if the bh is in our cache or not -- if it's
	 * not marked uptodate then we know it can't have correct
	 * data. */
	if (!buffer_uptodate(bh))
		return 0;

	/* OCFS2 does not allow multiple nodes to be changing the same
	 * block at the same time. */
	if (buffer_jbd(bh))
		return 1;

	/* Ok, locally the buffer is marked as up to date, now search
	 * our cache to see if we can trust that. */
	return ocfs2_buffer_cached(OCFS2_I(inode), bh);
}

/* Requires ip_lock */
static void ocfs2_append_cache_array(struct ocfs2_caching_info *ci,
				     sector_t block)
{
	BUG_ON(ci->ci_num_cached >= OCFS2_INODE_MAX_CACHE_ARRAY);

	mlog(0, "block %llu takes position %u\n", (unsigned long long) block,
	     ci->ci_num_cached);

	ci->ci_cache.ci_array[ci->ci_num_cached] = block;
	ci->ci_num_cached++;
}

/* By now the caller should have checked that the item does *not*
 * exist in the tree.
 * Requires ip_lock. */
static void __ocfs2_insert_cache_tree(struct ocfs2_caching_info *ci,
				      struct ocfs2_meta_cache_item *new)
{
	sector_t block = new->c_block;
	struct rb_node *parent = NULL;
	struct rb_node **p = &ci->ci_cache.ci_tree.rb_node;
	struct ocfs2_meta_cache_item *tmp;

	mlog(0, "Insert block %llu num = %u\n", (unsigned long long) block,
	     ci->ci_num_cached);

	while(*p) {
		parent = *p;

		tmp = rb_entry(parent, struct ocfs2_meta_cache_item, c_node);

		if (block < tmp->c_block)
			p = &(*p)->rb_left;
		else if (block > tmp->c_block)
			p = &(*p)->rb_right;
		else {
			/* This should never happen! */
			mlog(ML_ERROR, "Duplicate block %llu cached!\n",
			     (unsigned long long) block);
			BUG();
		}
	}

	rb_link_node(&new->c_node, parent, p);
	rb_insert_color(&new->c_node, &ci->ci_cache.ci_tree);
	ci->ci_num_cached++;
}

static inline int ocfs2_insert_can_use_array(struct ocfs2_inode_info *oi,
					     struct ocfs2_caching_info *ci)
{
	assert_spin_locked(&oi->ip_lock);

	return (oi->ip_flags & OCFS2_INODE_CACHE_INLINE) &&
		(ci->ci_num_cached < OCFS2_INODE_MAX_CACHE_ARRAY);
}

/* tree should be exactly OCFS2_INODE_MAX_CACHE_ARRAY wide. NULL the
 * pointers in tree after we use them - this allows caller to detect
 * when to free in case of error. */
static void ocfs2_expand_cache(struct ocfs2_inode_info *oi,
			       struct ocfs2_meta_cache_item **tree)
{
	int i;
	struct ocfs2_caching_info *ci = &oi->ip_metadata_cache;

	mlog_bug_on_msg(ci->ci_num_cached != OCFS2_INODE_MAX_CACHE_ARRAY,
			"Inode %"MLFu64", num cached = %u, should be %u\n",
			oi->ip_blkno, ci->ci_num_cached,
			OCFS2_INODE_MAX_CACHE_ARRAY);
	mlog_bug_on_msg(!(oi->ip_flags & OCFS2_INODE_CACHE_INLINE),
			"Inode %"MLFu64" not marked as inline anymore!\n",
			oi->ip_blkno);
	assert_spin_locked(&oi->ip_lock);

	/* Be careful to initialize the tree members *first* because
	 * once the ci_tree is used, the array is junk... */
	for(i = 0; i < OCFS2_INODE_MAX_CACHE_ARRAY; i++)
		tree[i]->c_block = ci->ci_cache.ci_array[i];

	oi->ip_flags &= ~OCFS2_INODE_CACHE_INLINE;
	ci->ci_cache.ci_tree = RB_ROOT;
	/* this will be set again by __ocfs2_insert_cache_tree */
	ci->ci_num_cached = 0;

	for(i = 0; i < OCFS2_INODE_MAX_CACHE_ARRAY; i++) {
		__ocfs2_insert_cache_tree(ci, tree[i]);
		tree[i] = NULL;
	}

	mlog(0, "Expanded %"MLFu64" to a tree cache: flags 0x%x, num = %u\n",
	     oi->ip_blkno, oi->ip_flags, ci->ci_num_cached);
}

/* Slow path function - memory allocation is necessary. See the
 * comment above ocfs2_set_buffer_uptodate for more information. */
static void __ocfs2_set_buffer_uptodate(struct ocfs2_inode_info *oi,
					sector_t block,
					int expand_tree)
{
	int i;
	struct ocfs2_caching_info *ci = &oi->ip_metadata_cache;
	struct ocfs2_meta_cache_item *new = NULL;
	struct ocfs2_meta_cache_item *tree[OCFS2_INODE_MAX_CACHE_ARRAY] =
		{ NULL, };

	mlog(0, "Inode %"MLFu64", block %llu, expand = %d\n",
	     oi->ip_blkno, (unsigned long long) block, expand_tree);

	new = kmem_cache_alloc(ocfs2_uptodate_cachep, GFP_KERNEL);
	if (!new) {
		mlog_errno(-ENOMEM);
		return;
	}
	new->c_block = block;

	if (expand_tree) {
		/* Do *not* allocate an array here - the removal code
		 * has no way of tracking that. */
		for(i = 0; i < OCFS2_INODE_MAX_CACHE_ARRAY; i++) {
			tree[i] = kmem_cache_alloc(ocfs2_uptodate_cachep,
						   GFP_KERNEL);
			if (!tree[i]) {
				mlog_errno(-ENOMEM);
				goto out_free;
			}

			/* These are initialized in ocfs2_expand_cache! */
		}
	}

	spin_lock(&oi->ip_lock);
	if (ocfs2_insert_can_use_array(oi, ci)) {
		mlog(0, "Someone cleared the tree underneath us\n");
		/* Ok, items were removed from the cache in between
		 * locks. Detect this and revert back to the fast path */
		ocfs2_append_cache_array(ci, block);
		spin_unlock(&oi->ip_lock);
		goto out_free;
	}

	if (expand_tree)
		ocfs2_expand_cache(oi, tree);

	__ocfs2_insert_cache_tree(ci, new);
	spin_unlock(&oi->ip_lock);

	new = NULL;
out_free:
	if (new)
		kmem_cache_free(ocfs2_uptodate_cachep, new);

	/* If these were used, then ocfs2_expand_cache re-set them to
	 * NULL for us. */
	if (tree[0]) {
		for(i = 0; i < OCFS2_INODE_MAX_CACHE_ARRAY; i++)
			if (tree[i])
				kmem_cache_free(ocfs2_uptodate_cachep,
						tree[i]);
	}
}

/* Item insertion is guarded by ip_io_mutex, so the insertion path takes
 * advantage of this by not rechecking for a duplicate insert during
 * the slow case. Additionally, if the cache needs to be bumped up to
 * a tree, the code will not recheck after acquiring the lock --
 * multiple paths cannot be expanding to a tree at the same time.
 *
 * The slow path takes into account that items can be removed
 * (including the whole tree wiped and reset) when this process it out
 * allocating memory. In those cases, it reverts back to the fast
 * path.
 *
 * Note that this function may actually fail to insert the block if
 * memory cannot be allocated. This is not fatal however (but may
 * result in a performance penalty) */
void ocfs2_set_buffer_uptodate(struct inode *inode,
			       struct buffer_head *bh)
{
	int expand;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_caching_info *ci = &oi->ip_metadata_cache;

	/* The block may very well exist in our cache already, so avoid
	 * doing any more work in that case. */
	if (ocfs2_buffer_cached(oi, bh))
		return;

	mlog(0, "Inode %"MLFu64", inserting block %llu\n", oi->ip_blkno,
	     (unsigned long long) bh->b_blocknr);

	/* No need to recheck under spinlock - insertion is guarded by
	 * ip_io_mutex */
	spin_lock(&oi->ip_lock);
	if (ocfs2_insert_can_use_array(oi, ci)) {
		/* Fast case - it's an array and there's a free
		 * spot. */
		ocfs2_append_cache_array(ci, bh->b_blocknr);
		spin_unlock(&oi->ip_lock);
		return;
	}

	expand = 0;
	if (oi->ip_flags & OCFS2_INODE_CACHE_INLINE) {
		/* We need to bump things up to a tree. */
		expand = 1;
	}
	spin_unlock(&oi->ip_lock);

	__ocfs2_set_buffer_uptodate(oi, bh->b_blocknr, expand);
}

/* Called against a newly allocated buffer. Most likely nobody should
 * be able to read this sort of metadata while it's still being
 * allocated, but this is careful to take ip_io_mutex anyway. */
void ocfs2_set_new_buffer_uptodate(struct inode *inode,
				   struct buffer_head *bh)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	/* This should definitely *not* exist in our cache */
	BUG_ON(ocfs2_buffer_cached(oi, bh));

	set_buffer_uptodate(bh);

	mutex_lock(&oi->ip_io_mutex);
	ocfs2_set_buffer_uptodate(inode, bh);
	mutex_unlock(&oi->ip_io_mutex);
}

/* Requires ip_lock. */
static void ocfs2_remove_metadata_array(struct ocfs2_caching_info *ci,
					int index)
{
	sector_t *array = ci->ci_cache.ci_array;
	int bytes;

	BUG_ON(index < 0 || index >= OCFS2_INODE_MAX_CACHE_ARRAY);
	BUG_ON(index >= ci->ci_num_cached);
	BUG_ON(!ci->ci_num_cached);

	mlog(0, "remove index %d (num_cached = %u\n", index,
	     ci->ci_num_cached);

	ci->ci_num_cached--;

	/* don't need to copy if the array is now empty, or if we
	 * removed at the tail */
	if (ci->ci_num_cached && index < ci->ci_num_cached) {
		bytes = sizeof(sector_t) * (ci->ci_num_cached - index);
		memmove(&array[index], &array[index + 1], bytes);
	}
}

/* Requires ip_lock. */
static void ocfs2_remove_metadata_tree(struct ocfs2_caching_info *ci,
				       struct ocfs2_meta_cache_item *item)
{
	mlog(0, "remove block %llu from tree\n",
	     (unsigned long long) item->c_block);

	rb_erase(&item->c_node, &ci->ci_cache.ci_tree);
	ci->ci_num_cached--;
}

/* Called when we remove a chunk of metadata from an inode. We don't
 * bother reverting things to an inlined array in the case of a remove
 * which moves us back under the limit. */
void ocfs2_remove_from_cache(struct inode *inode,
			     struct buffer_head *bh)
{
	int index;
	sector_t block = bh->b_blocknr;
	struct ocfs2_meta_cache_item *item = NULL;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_caching_info *ci = &oi->ip_metadata_cache;

	spin_lock(&oi->ip_lock);
	mlog(0, "Inode %"MLFu64", remove %llu, items = %u, array = %u\n",
	     oi->ip_blkno, (unsigned long long) block, ci->ci_num_cached,
	     oi->ip_flags & OCFS2_INODE_CACHE_INLINE);

	if (oi->ip_flags & OCFS2_INODE_CACHE_INLINE) {
		index = ocfs2_search_cache_array(ci, block);
		if (index != -1)
			ocfs2_remove_metadata_array(ci, index);
	} else {
		item = ocfs2_search_cache_tree(ci, block);
		if (item)
			ocfs2_remove_metadata_tree(ci, item);
	}
	spin_unlock(&oi->ip_lock);

	if (item)
		kmem_cache_free(ocfs2_uptodate_cachep, item);
}

int __init init_ocfs2_uptodate_cache(void)
{
	ocfs2_uptodate_cachep = kmem_cache_create("ocfs2_uptodate",
				  sizeof(struct ocfs2_meta_cache_item),
				  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!ocfs2_uptodate_cachep)
		return -ENOMEM;

	mlog(0, "%u inlined cache items per inode.\n",
	     OCFS2_INODE_MAX_CACHE_ARRAY);

	return 0;
}

void exit_ocfs2_uptodate_cache(void)
{
	if (ocfs2_uptodate_cachep)
		kmem_cache_destroy(ocfs2_uptodate_cachep);
}
