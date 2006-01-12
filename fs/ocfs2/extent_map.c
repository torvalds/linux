/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extent_map.c
 *
 * In-memory extent map for OCFS2.  Man, this code was prettier in
 * the library.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/rbtree.h>

#define MLOG_MASK_PREFIX ML_EXTENT_MAP
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "extent_map.h"
#include "inode.h"
#include "super.h"

#include "buffer_head_io.h"


/*
 * SUCK SUCK SUCK
 * Our headers are so bad that struct ocfs2_extent_map is in ocfs.h
 */

struct ocfs2_extent_map_entry {
	struct rb_node e_node;
	int e_tree_depth;
	struct ocfs2_extent_rec e_rec;
};

struct ocfs2_em_insert_context {
	int need_left;
	int need_right;
	struct ocfs2_extent_map_entry *new_ent;
	struct ocfs2_extent_map_entry *old_ent;
	struct ocfs2_extent_map_entry *left_ent;
	struct ocfs2_extent_map_entry *right_ent;
};

static kmem_cache_t *ocfs2_em_ent_cachep = NULL;


static struct ocfs2_extent_map_entry *
ocfs2_extent_map_lookup(struct ocfs2_extent_map *em,
			u32 cpos, u32 clusters,
			struct rb_node ***ret_p,
			struct rb_node **ret_parent);
static int ocfs2_extent_map_insert(struct inode *inode,
				   struct ocfs2_extent_rec *rec,
				   int tree_depth);
static int ocfs2_extent_map_insert_entry(struct ocfs2_extent_map *em,
					 struct ocfs2_extent_map_entry *ent);
static int ocfs2_extent_map_find_leaf(struct inode *inode,
				      u32 cpos, u32 clusters,
				      struct ocfs2_extent_list *el);
static int ocfs2_extent_map_lookup_read(struct inode *inode,
					u32 cpos, u32 clusters,
					struct ocfs2_extent_map_entry **ret_ent);
static int ocfs2_extent_map_try_insert(struct inode *inode,
				       struct ocfs2_extent_rec *rec,
				       int tree_depth,
				       struct ocfs2_em_insert_context *ctxt);

/* returns 1 only if the rec contains all the given clusters -- that is that
 * rec's cpos is <= the cluster cpos and that the rec endpoint (cpos +
 * clusters) is >= the argument's endpoint */
static int ocfs2_extent_rec_contains_clusters(struct ocfs2_extent_rec *rec,
					      u32 cpos, u32 clusters)
{
	if (le32_to_cpu(rec->e_cpos) > cpos)
		return 0;
	if (cpos + clusters > le32_to_cpu(rec->e_cpos) + 
			      le32_to_cpu(rec->e_clusters))
		return 0;
	return 1;
}


/*
 * Find an entry in the tree that intersects the region passed in.
 * Note that this will find straddled intervals, it is up to the
 * callers to enforce any boundary conditions.
 *
 * Callers must hold ip_lock.  This lookup is not guaranteed to return
 * a tree_depth 0 match, and as such can race inserts if the lock
 * were not held.
 *
 * The rb_node garbage lets insertion share the search.  Trivial
 * callers pass NULL.
 */
static struct ocfs2_extent_map_entry *
ocfs2_extent_map_lookup(struct ocfs2_extent_map *em,
			u32 cpos, u32 clusters,
			struct rb_node ***ret_p,
			struct rb_node **ret_parent)
{
	struct rb_node **p = &em->em_extents.rb_node;
	struct rb_node *parent = NULL;
	struct ocfs2_extent_map_entry *ent = NULL;

	while (*p)
	{
		parent = *p;
		ent = rb_entry(parent, struct ocfs2_extent_map_entry,
			       e_node);
		if ((cpos + clusters) <= le32_to_cpu(ent->e_rec.e_cpos)) {
			p = &(*p)->rb_left;
			ent = NULL;
		} else if (cpos >= (le32_to_cpu(ent->e_rec.e_cpos) +
				    le32_to_cpu(ent->e_rec.e_clusters))) {
			p = &(*p)->rb_right;
			ent = NULL;
		} else
			break;
	}

	if (ret_p != NULL)
		*ret_p = p;
	if (ret_parent != NULL)
		*ret_parent = parent;
	return ent;
}

/*
 * Find the leaf containing the interval we want.  While we're on our
 * way down the tree, fill in every record we see at any depth, because
 * we might want it later.
 *
 * Note that this code is run without ip_lock.  That's because it
 * sleeps while reading.  If someone is also filling the extent list at
 * the same time we are, we might have to restart.
 */
static int ocfs2_extent_map_find_leaf(struct inode *inode,
				      u32 cpos, u32 clusters,
				      struct ocfs2_extent_list *el)
{
	int i, ret;
	struct buffer_head *eb_bh = NULL;
	u64 blkno;
	u32 rec_end;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;

	/*
	 * The bh data containing the el cannot change here, because
	 * we hold alloc_sem.  So we can do this without other
	 * locks.
	 */
	while (el->l_tree_depth)
	{
		blkno = 0;
		for (i = 0; i < le16_to_cpu(el->l_next_free_rec); i++) {
			rec = &el->l_recs[i];
			rec_end = (le32_to_cpu(rec->e_cpos) +
				   le32_to_cpu(rec->e_clusters));

			ret = -EBADR;
			if (rec_end > OCFS2_I(inode)->ip_clusters) {
				mlog_errno(ret);
				goto out_free;
			}

			if (rec_end <= cpos) {
				ret = ocfs2_extent_map_insert(inode, rec,
						le16_to_cpu(el->l_tree_depth));
				if (ret && (ret != -EEXIST)) {
					mlog_errno(ret);
					goto out_free;
				}
				continue;
			}
			if ((cpos + clusters) <= le32_to_cpu(rec->e_cpos)) {
				ret = ocfs2_extent_map_insert(inode, rec,
						le16_to_cpu(el->l_tree_depth));
				if (ret && (ret != -EEXIST)) {
					mlog_errno(ret);
					goto out_free;
				}
				continue;
			}

			/*
			 * We've found a record that matches our
			 * interval.  We don't insert it because we're
			 * about to traverse it.
			 */

			/* Check to see if we're stradling */
			ret = -ESRCH;
			if (!ocfs2_extent_rec_contains_clusters(rec,
							        cpos,
								clusters)) {
				mlog_errno(ret);
				goto out_free;
			}

			/*
			 * If we've already found a record, the el has
			 * two records covering the same interval.
			 * EEEK!
			 */
			ret = -EBADR;
			if (blkno) {
				mlog_errno(ret);
				goto out_free;
			}

			blkno = le64_to_cpu(rec->e_blkno);
		}

		/*
		 * We don't support holes, and we're still up
		 * in the branches, so we'd better have found someone
		 */
		ret = -EBADR;
		if (!blkno) {
			mlog_errno(ret);
			goto out_free;
		}

		if (eb_bh) {
			brelse(eb_bh);
			eb_bh = NULL;
		}
		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				       blkno, &eb_bh, OCFS2_BH_CACHED,
				       inode);
		if (ret) {
			mlog_errno(ret);
			goto out_free;
		}
		eb = (struct ocfs2_extent_block *)eb_bh->b_data;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			ret = -EIO;
			goto out_free;
		}
		el = &eb->h_list;
	}

	if (el->l_tree_depth)
		BUG();

	for (i = 0; i < le16_to_cpu(el->l_next_free_rec); i++) {
		rec = &el->l_recs[i];
		ret = ocfs2_extent_map_insert(inode, rec,
					      le16_to_cpu(el->l_tree_depth));
		if (ret) {
			mlog_errno(ret);
			goto out_free;
		}
	}

	ret = 0;

out_free:
	if (eb_bh)
		brelse(eb_bh);

	return ret;
}

/*
 * This lookup actually will read from disk.  It has one invariant:
 * It will never re-traverse blocks.  This means that all inserts should
 * be new regions or more granular regions (both allowed by insert).
 */
static int ocfs2_extent_map_lookup_read(struct inode *inode,
					u32 cpos,
					u32 clusters,
					struct ocfs2_extent_map_entry **ret_ent)
{
	int ret;
	u64 blkno;
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;
	struct ocfs2_extent_map_entry *ent;
	struct buffer_head *bh = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_list *el;

	spin_lock(&OCFS2_I(inode)->ip_lock);
	ent = ocfs2_extent_map_lookup(em, cpos, clusters, NULL, NULL);
	if (ent) {
		if (!ent->e_tree_depth) {
			spin_unlock(&OCFS2_I(inode)->ip_lock);
			*ret_ent = ent;
			return 0;
		}
		blkno = le64_to_cpu(ent->e_rec.e_blkno);
		spin_unlock(&OCFS2_I(inode)->ip_lock);

		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb), blkno, &bh,
				       OCFS2_BH_CACHED, inode);
		if (ret) {
			mlog_errno(ret);
			if (bh)
				brelse(bh);
			return ret;
		}
		eb = (struct ocfs2_extent_block *)bh->b_data;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			brelse(bh);
			return -EIO;
		}
		el = &eb->h_list;
	} else {
		spin_unlock(&OCFS2_I(inode)->ip_lock);

		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				       OCFS2_I(inode)->ip_blkno, &bh,
				       OCFS2_BH_CACHED, inode);
		if (ret) {
			mlog_errno(ret);
			if (bh)
				brelse(bh);
			return ret;
		}
		di = (struct ocfs2_dinode *)bh->b_data;
		if (!OCFS2_IS_VALID_DINODE(di)) {
			brelse(bh);
			OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, di);
			return -EIO;
		}
		el = &di->id2.i_list;
	}

	ret = ocfs2_extent_map_find_leaf(inode, cpos, clusters, el);
	brelse(bh);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	ent = ocfs2_extent_map_lookup(em, cpos, clusters, NULL, NULL);
	if (!ent) {
		ret = -ESRCH;
		mlog_errno(ret);
		return ret;
	}

	if (ent->e_tree_depth)
		BUG();  /* FIXME: Make sure this isn't a corruption */

	*ret_ent = ent;

	return 0;
}

/*
 * Callers must hold ip_lock.  This can insert pieces of the tree,
 * thus racing lookup if the lock weren't held.
 */
static int ocfs2_extent_map_insert_entry(struct ocfs2_extent_map *em,
					 struct ocfs2_extent_map_entry *ent)
{
	struct rb_node **p, *parent;
	struct ocfs2_extent_map_entry *old_ent;

	old_ent = ocfs2_extent_map_lookup(em, le32_to_cpu(ent->e_rec.e_cpos),
					  le32_to_cpu(ent->e_rec.e_clusters),
					  &p, &parent);
	if (old_ent)
		return -EEXIST;

	rb_link_node(&ent->e_node, parent, p);
	rb_insert_color(&ent->e_node, &em->em_extents);

	return 0;
}


/*
 * Simple rule: on any return code other than -EAGAIN, anything left
 * in the insert_context will be freed.
 */
static int ocfs2_extent_map_try_insert(struct inode *inode,
				       struct ocfs2_extent_rec *rec,
				       int tree_depth,
				       struct ocfs2_em_insert_context *ctxt)
{
	int ret;
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;
	struct ocfs2_extent_map_entry *old_ent;

	ctxt->need_left = 0;
	ctxt->need_right = 0;
	ctxt->old_ent = NULL;

	spin_lock(&OCFS2_I(inode)->ip_lock);
	ret = ocfs2_extent_map_insert_entry(em, ctxt->new_ent);
	if (!ret) {
		ctxt->new_ent = NULL;
		goto out_unlock;
	}

	old_ent = ocfs2_extent_map_lookup(em, le32_to_cpu(rec->e_cpos),
					  le32_to_cpu(rec->e_clusters), NULL,
					  NULL);

	if (!old_ent)
		BUG();

	ret = -EEXIST;
	if (old_ent->e_tree_depth < tree_depth)
		goto out_unlock;

	if (old_ent->e_tree_depth == tree_depth) {
		if (!memcmp(rec, &old_ent->e_rec,
			    sizeof(struct ocfs2_extent_rec)))
			ret = 0;

		/* FIXME: Should this be ESRCH/EBADR??? */
		goto out_unlock;
	}

	/*
	 * We do it in this order specifically so that no actual tree
	 * changes occur until we have all the pieces we need.  We
	 * don't want malloc failures to leave an inconsistent tree.
	 * Whenever we drop the lock, another process could be
	 * inserting.  Also note that, if another process just beat us
	 * to an insert, we might not need the same pieces we needed
	 * the first go round.  In the end, the pieces we need will
	 * be used, and the pieces we don't will be freed.
	 */
	ctxt->need_left = !!(le32_to_cpu(rec->e_cpos) >
			     le32_to_cpu(old_ent->e_rec.e_cpos));
	ctxt->need_right = !!((le32_to_cpu(old_ent->e_rec.e_cpos) +
			       le32_to_cpu(old_ent->e_rec.e_clusters)) >
			      (le32_to_cpu(rec->e_cpos) + le32_to_cpu(rec->e_clusters)));
	ret = -EAGAIN;
	if (ctxt->need_left) {
		if (!ctxt->left_ent)
			goto out_unlock;
		*(ctxt->left_ent) = *old_ent;
		ctxt->left_ent->e_rec.e_clusters =
			cpu_to_le32(le32_to_cpu(rec->e_cpos) -
				    le32_to_cpu(ctxt->left_ent->e_rec.e_cpos));
	}
	if (ctxt->need_right) {
		if (!ctxt->right_ent)
			goto out_unlock;
		*(ctxt->right_ent) = *old_ent;
		ctxt->right_ent->e_rec.e_cpos =
			cpu_to_le32(le32_to_cpu(rec->e_cpos) +
				    le32_to_cpu(rec->e_clusters));
		ctxt->right_ent->e_rec.e_clusters =
			cpu_to_le32((le32_to_cpu(old_ent->e_rec.e_cpos) +
				     le32_to_cpu(old_ent->e_rec.e_clusters)) -
				    le32_to_cpu(ctxt->right_ent->e_rec.e_cpos));
	}

	rb_erase(&old_ent->e_node, &em->em_extents);
	/* Now that he's erased, set him up for deletion */
	ctxt->old_ent = old_ent;

	if (ctxt->need_left) {
		ret = ocfs2_extent_map_insert_entry(em,
						    ctxt->left_ent);
		if (ret)
			goto out_unlock;
		ctxt->left_ent = NULL;
	}

	if (ctxt->need_right) {
		ret = ocfs2_extent_map_insert_entry(em,
						    ctxt->right_ent);
		if (ret)
			goto out_unlock;
		ctxt->right_ent = NULL;
	}

	ret = ocfs2_extent_map_insert_entry(em, ctxt->new_ent);

	if (!ret)
		ctxt->new_ent = NULL;

out_unlock:
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	return ret;
}


static int ocfs2_extent_map_insert(struct inode *inode,
				   struct ocfs2_extent_rec *rec,
				   int tree_depth)
{
	int ret;
	struct ocfs2_em_insert_context ctxt = {0, };

	if ((le32_to_cpu(rec->e_cpos) + le32_to_cpu(rec->e_clusters)) >
	    OCFS2_I(inode)->ip_map.em_clusters) {
		ret = -EBADR;
		mlog_errno(ret);
		return ret;
	}

	/* Zero e_clusters means a truncated tail record.  It better be EOF */
	if (!rec->e_clusters) {
		if ((le32_to_cpu(rec->e_cpos) + le32_to_cpu(rec->e_clusters)) !=
		    OCFS2_I(inode)->ip_map.em_clusters) {
			ret = -EBADR;
			mlog_errno(ret);
			return ret;
		}

		/* Ignore the truncated tail */
		return 0;
	}

	ret = -ENOMEM;
	ctxt.new_ent = kmem_cache_alloc(ocfs2_em_ent_cachep,
					GFP_KERNEL);
	if (!ctxt.new_ent) {
		mlog_errno(ret);
		return ret;
	}

	ctxt.new_ent->e_rec = *rec;
	ctxt.new_ent->e_tree_depth = tree_depth;

	do {
		ret = -ENOMEM;
		if (ctxt.need_left && !ctxt.left_ent) {
			ctxt.left_ent =
				kmem_cache_alloc(ocfs2_em_ent_cachep,
						 GFP_KERNEL);
			if (!ctxt.left_ent)
				break;
		}
		if (ctxt.need_right && !ctxt.right_ent) {
			ctxt.right_ent =
				kmem_cache_alloc(ocfs2_em_ent_cachep,
						 GFP_KERNEL);
			if (!ctxt.right_ent)
				break;
		}

		ret = ocfs2_extent_map_try_insert(inode, rec,
						  tree_depth, &ctxt);
	} while (ret == -EAGAIN);

	if (ret < 0)
		mlog_errno(ret);

	if (ctxt.left_ent)
		kmem_cache_free(ocfs2_em_ent_cachep, ctxt.left_ent);
	if (ctxt.right_ent)
		kmem_cache_free(ocfs2_em_ent_cachep, ctxt.right_ent);
	if (ctxt.old_ent)
		kmem_cache_free(ocfs2_em_ent_cachep, ctxt.old_ent);
	if (ctxt.new_ent)
		kmem_cache_free(ocfs2_em_ent_cachep, ctxt.new_ent);

	return ret;
}

/*
 * Append this record to the tail of the extent map.  It must be
 * tree_depth 0.  The record might be an extension of an existing
 * record, and as such that needs to be handled.  eg:
 *
 * Existing record in the extent map:
 *
 *	cpos = 10, len = 10
 * 	|---------|
 *
 * New Record:
 *
 *	cpos = 10, len = 20
 * 	|------------------|
 *
 * The passed record is the new on-disk record.  The new_clusters value
 * is how many clusters were added to the file.  If the append is a
 * contiguous append, the new_clusters has been added to
 * rec->e_clusters.  If the append is an entirely new extent, then
 * rec->e_clusters is == new_clusters.
 */
int ocfs2_extent_map_append(struct inode *inode,
			    struct ocfs2_extent_rec *rec,
			    u32 new_clusters)
{
	int ret;
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;
	struct ocfs2_extent_map_entry *ent;
	struct ocfs2_extent_rec *old;

	BUG_ON(!new_clusters);
	BUG_ON(le32_to_cpu(rec->e_clusters) < new_clusters);

	if (em->em_clusters < OCFS2_I(inode)->ip_clusters) {
		/*
		 * Size changed underneath us on disk.  Drop any
		 * straddling records and update our idea of
		 * i_clusters
		 */
		ocfs2_extent_map_drop(inode, em->em_clusters - 1);
		em->em_clusters = OCFS2_I(inode)->ip_clusters;
	}

	mlog_bug_on_msg((le32_to_cpu(rec->e_cpos) +
			 le32_to_cpu(rec->e_clusters)) !=
			(em->em_clusters + new_clusters),
			"Inode %"MLFu64":\n"
			"rec->e_cpos = %u + rec->e_clusters = %u = %u\n"
			"em->em_clusters = %u + new_clusters = %u = %u\n",
			OCFS2_I(inode)->ip_blkno,
			le32_to_cpu(rec->e_cpos), le32_to_cpu(rec->e_clusters),
			le32_to_cpu(rec->e_cpos) + le32_to_cpu(rec->e_clusters),
			em->em_clusters, new_clusters,
			em->em_clusters + new_clusters);

	em->em_clusters += new_clusters;

	ret = -ENOENT;
	if (le32_to_cpu(rec->e_clusters) > new_clusters) {
		/* This is a contiguous append */
		ent = ocfs2_extent_map_lookup(em, le32_to_cpu(rec->e_cpos), 1,
					      NULL, NULL);
		if (ent) {
			old = &ent->e_rec;
			BUG_ON((le32_to_cpu(rec->e_cpos) +
				le32_to_cpu(rec->e_clusters)) !=
				 (le32_to_cpu(old->e_cpos) +
				  le32_to_cpu(old->e_clusters) +
				  new_clusters));
			if (ent->e_tree_depth == 0) {
				BUG_ON(le32_to_cpu(old->e_cpos) !=
				       le32_to_cpu(rec->e_cpos));
				BUG_ON(le64_to_cpu(old->e_blkno) !=
				       le64_to_cpu(rec->e_blkno));
				ret = 0;
			}
			/*
			 * Let non-leafs fall through as -ENOENT to
			 * force insertion of the new leaf.
			 */
			le32_add_cpu(&old->e_clusters, new_clusters);
		}
	}

	if (ret == -ENOENT)
		ret = ocfs2_extent_map_insert(inode, rec, 0);
	if (ret < 0)
		mlog_errno(ret);
	return ret;
}

#if 0
/* Code here is included but defined out as it completes the extent
 * map api and may be used in the future. */

/*
 * Look up the record containing this cluster offset.  This record is
 * part of the extent map.  Do not free it.  Any changes you make to
 * it will reflect in the extent map.  So, if your last extent
 * is (cpos = 10, clusters = 10) and you truncate the file by 5
 * clusters, you can do:
 *
 * ret = ocfs2_extent_map_get_rec(em, orig_size - 5, &rec);
 * rec->e_clusters -= 5;
 *
 * The lookup does not read from disk.  If the map isn't filled in for
 * an entry, you won't find it.
 *
 * Also note that the returned record is valid until alloc_sem is
 * dropped.  After that, truncate and extend can happen.  Caveat Emptor.
 */
int ocfs2_extent_map_get_rec(struct inode *inode, u32 cpos,
			     struct ocfs2_extent_rec **rec,
			     int *tree_depth)
{
	int ret = -ENOENT;
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;
	struct ocfs2_extent_map_entry *ent;

	*rec = NULL;

	if (cpos >= OCFS2_I(inode)->ip_clusters)
		return -EINVAL;

	if (cpos >= em->em_clusters) {
		/*
		 * Size changed underneath us on disk.  Drop any
		 * straddling records and update our idea of
		 * i_clusters
		 */
		ocfs2_extent_map_drop(inode, em->em_clusters - 1);
		em->em_clusters = OCFS2_I(inode)->ip_clusters ;
	}

	ent = ocfs2_extent_map_lookup(&OCFS2_I(inode)->ip_map, cpos, 1,
				      NULL, NULL);

	if (ent) {
		*rec = &ent->e_rec;
		if (tree_depth)
			*tree_depth = ent->e_tree_depth;
		ret = 0;
	}

	return ret;
}

int ocfs2_extent_map_get_clusters(struct inode *inode,
				  u32 v_cpos, int count,
				  u32 *p_cpos, int *ret_count)
{
	int ret;
	u32 coff, ccount;
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;
	struct ocfs2_extent_map_entry *ent = NULL;

	*p_cpos = ccount = 0;

	if ((v_cpos + count) > OCFS2_I(inode)->ip_clusters)
		return -EINVAL;

	if ((v_cpos + count) > em->em_clusters) {
		/*
		 * Size changed underneath us on disk.  Drop any
		 * straddling records and update our idea of
		 * i_clusters
		 */
		ocfs2_extent_map_drop(inode, em->em_clusters - 1);
		em->em_clusters = OCFS2_I(inode)->ip_clusters;
	}


	ret = ocfs2_extent_map_lookup_read(inode, v_cpos, count, &ent);
	if (ret)
		return ret;

	if (ent) {
		/* We should never find ourselves straddling an interval */
		if (!ocfs2_extent_rec_contains_clusters(&ent->e_rec,
							v_cpos,
							count))
			return -ESRCH;

		coff = v_cpos - le32_to_cpu(ent->e_rec.e_cpos);
		*p_cpos = ocfs2_blocks_to_clusters(inode->i_sb,
				le64_to_cpu(ent->e_rec.e_blkno)) +
			  coff;

		if (ret_count)
			*ret_count = le32_to_cpu(ent->e_rec.e_clusters) - coff;

		return 0;
	}


	return -ENOENT;
}

#endif  /*  0  */

int ocfs2_extent_map_get_blocks(struct inode *inode,
				u64 v_blkno, int count,
				u64 *p_blkno, int *ret_count)
{
	int ret;
	u64 boff;
	u32 cpos, clusters;
	int bpc = ocfs2_clusters_to_blocks(inode->i_sb, 1);
	struct ocfs2_extent_map_entry *ent = NULL;
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;
	struct ocfs2_extent_rec *rec;

	*p_blkno = 0;

	cpos = ocfs2_blocks_to_clusters(inode->i_sb, v_blkno);
	clusters = ocfs2_blocks_to_clusters(inode->i_sb,
					    (u64)count + bpc - 1);
	if ((cpos + clusters) > OCFS2_I(inode)->ip_clusters) {
		ret = -EINVAL;
		mlog_errno(ret);
		return ret;
	}

	if ((cpos + clusters) > em->em_clusters) {
		/*
		 * Size changed underneath us on disk.  Drop any
		 * straddling records and update our idea of
		 * i_clusters
		 */
		ocfs2_extent_map_drop(inode, em->em_clusters - 1);
		em->em_clusters = OCFS2_I(inode)->ip_clusters;
	}

	ret = ocfs2_extent_map_lookup_read(inode, cpos, clusters, &ent);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	if (ent)
	{
		rec = &ent->e_rec;

		/* We should never find ourselves straddling an interval */
		if (!ocfs2_extent_rec_contains_clusters(rec, cpos, clusters)) {
			ret = -ESRCH;
			mlog_errno(ret);
			return ret;
		}

		boff = ocfs2_clusters_to_blocks(inode->i_sb, cpos -
						le32_to_cpu(rec->e_cpos));
		boff += (v_blkno & (u64)(bpc - 1));
		*p_blkno = le64_to_cpu(rec->e_blkno) + boff;

		if (ret_count) {
			*ret_count = ocfs2_clusters_to_blocks(inode->i_sb,
					le32_to_cpu(rec->e_clusters)) - boff;
		}

		return 0;
	}

	return -ENOENT;
}

int ocfs2_extent_map_init(struct inode *inode)
{
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;

	em->em_extents = RB_ROOT;
	em->em_clusters = 0;

	return 0;
}

/* Needs the lock */
static void __ocfs2_extent_map_drop(struct inode *inode,
				    u32 new_clusters,
				    struct rb_node **free_head,
				    struct ocfs2_extent_map_entry **tail_ent)
{
	struct rb_node *node, *next;
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;
	struct ocfs2_extent_map_entry *ent;

	*free_head = NULL;

	ent = NULL;
	node = rb_last(&em->em_extents);
	while (node)
	{
		next = rb_prev(node);

		ent = rb_entry(node, struct ocfs2_extent_map_entry,
			       e_node);
		if (le32_to_cpu(ent->e_rec.e_cpos) < new_clusters)
			break;

		rb_erase(&ent->e_node, &em->em_extents);

		node->rb_right = *free_head;
		*free_head = node;

		ent = NULL;
		node = next;
	}

	/* Do we have an entry straddling new_clusters? */
	if (tail_ent) {
		if (ent &&
		    ((le32_to_cpu(ent->e_rec.e_cpos) +
		      le32_to_cpu(ent->e_rec.e_clusters)) > new_clusters))
			*tail_ent = ent;
		else
			*tail_ent = NULL;
	}
}

static void __ocfs2_extent_map_drop_cleanup(struct rb_node *free_head)
{
	struct rb_node *node;
	struct ocfs2_extent_map_entry *ent;

	while (free_head) {
		node = free_head;
		free_head = node->rb_right;

		ent = rb_entry(node, struct ocfs2_extent_map_entry,
			       e_node);
		kmem_cache_free(ocfs2_em_ent_cachep, ent);
	}
}

/*
 * Remove all entries past new_clusters, inclusive of an entry that
 * contains new_clusters.  This is effectively a cache forget.
 *
 * If you want to also clip the last extent by some number of clusters,
 * you need to call ocfs2_extent_map_trunc().
 * This code does not check or modify ip_clusters.
 */
int ocfs2_extent_map_drop(struct inode *inode, u32 new_clusters)
{
	struct rb_node *free_head = NULL;
	struct ocfs2_extent_map *em = &OCFS2_I(inode)->ip_map;
	struct ocfs2_extent_map_entry *ent;

	spin_lock(&OCFS2_I(inode)->ip_lock);

	__ocfs2_extent_map_drop(inode, new_clusters, &free_head, &ent);

	if (ent) {
		rb_erase(&ent->e_node, &em->em_extents);
		ent->e_node.rb_right = free_head;
		free_head = &ent->e_node;
	}

	spin_unlock(&OCFS2_I(inode)->ip_lock);

	if (free_head)
		__ocfs2_extent_map_drop_cleanup(free_head);

	return 0;
}

/*
 * Remove all entries past new_clusters and also clip any extent
 * straddling new_clusters, if there is one.  This does not check
 * or modify ip_clusters
 */
int ocfs2_extent_map_trunc(struct inode *inode, u32 new_clusters)
{
	struct rb_node *free_head = NULL;
	struct ocfs2_extent_map_entry *ent = NULL;

	spin_lock(&OCFS2_I(inode)->ip_lock);

	__ocfs2_extent_map_drop(inode, new_clusters, &free_head, &ent);

	if (ent)
		ent->e_rec.e_clusters = cpu_to_le32(new_clusters -
					       le32_to_cpu(ent->e_rec.e_cpos));

	OCFS2_I(inode)->ip_map.em_clusters = new_clusters;

	spin_unlock(&OCFS2_I(inode)->ip_lock);

	if (free_head)
		__ocfs2_extent_map_drop_cleanup(free_head);

	return 0;
}

int __init init_ocfs2_extent_maps(void)
{
	ocfs2_em_ent_cachep =
		kmem_cache_create("ocfs2_em_ent",
				  sizeof(struct ocfs2_extent_map_entry),
				  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!ocfs2_em_ent_cachep)
		return -ENOMEM;

	return 0;
}

void __exit exit_ocfs2_extent_maps(void)
{
	kmem_cache_destroy(ocfs2_em_ent_cachep);
}
