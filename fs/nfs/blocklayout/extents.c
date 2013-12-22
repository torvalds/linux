/*
 *  linux/fs/nfs/blocklayout/blocklayout.h
 *
 *  Module for the NFSv4.1 pNFS block layout driver.
 *
 *  Copyright (c) 2006 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@citi.umich.edu>
 *  Fred Isaman <iisaman@umich.edu>
 *
 * permission is granted to use, copy, create derivative works and
 * redistribute this software and such derivative works for any purpose,
 * so long as the name of the university of michigan is not used in
 * any advertising or publicity pertaining to the use or distribution
 * of this software without specific, written prior authorization.  if
 * the above copyright notice or any other identification of the
 * university of michigan is included in any copy of any portion of
 * this software, then the disclaimer below must also be included.
 *
 * this software is provided as is, without representation from the
 * university of michigan as to its fitness for any purpose, and without
 * warranty by the university of michigan of any kind, either express
 * or implied, including without limitation the implied warranties of
 * merchantability and fitness for a particular purpose.  the regents
 * of the university of michigan shall not be liable for any damages,
 * including special, indirect, incidental, or consequential damages,
 * with respect to any claim arising out or in connection with the use
 * of the software, even if it has been or is hereafter advised of the
 * possibility of such damages.
 */

#include "blocklayout.h"
#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

/* Bit numbers */
#define EXTENT_INITIALIZED 0
#define EXTENT_WRITTEN     1
#define EXTENT_IN_COMMIT   2
#define INTERNAL_EXISTS    MY_MAX_TAGS
#define INTERNAL_MASK      ((1 << INTERNAL_EXISTS) - 1)

/* Returns largest t<=s s.t. t%base==0 */
static inline sector_t normalize(sector_t s, int base)
{
	sector_t tmp = s; /* Since do_div modifies its argument */
	return s - sector_div(tmp, base);
}

static inline sector_t normalize_up(sector_t s, int base)
{
	return normalize(s + base - 1, base);
}

/* Complete stub using list while determine API wanted */

/* Returns tags, or negative */
static int32_t _find_entry(struct my_tree *tree, u64 s)
{
	struct pnfs_inval_tracking *pos;

	dprintk("%s(%llu) enter\n", __func__, s);
	list_for_each_entry_reverse(pos, &tree->mtt_stub, it_link) {
		if (pos->it_sector > s)
			continue;
		else if (pos->it_sector == s)
			return pos->it_tags & INTERNAL_MASK;
		else
			break;
	}
	return -ENOENT;
}

static inline
int _has_tag(struct my_tree *tree, u64 s, int32_t tag)
{
	int32_t tags;

	dprintk("%s(%llu, %i) enter\n", __func__, s, tag);
	s = normalize(s, tree->mtt_step_size);
	tags = _find_entry(tree, s);
	if ((tags < 0) || !(tags & (1 << tag)))
		return 0;
	else
		return 1;
}

/* Creates entry with tag, or if entry already exists, unions tag to it.
 * If storage is not NULL, newly created entry will use it.
 * Returns number of entries added, or negative on error.
 */
static int _add_entry(struct my_tree *tree, u64 s, int32_t tag,
		      struct pnfs_inval_tracking *storage)
{
	int found = 0;
	struct pnfs_inval_tracking *pos;

	dprintk("%s(%llu, %i, %p) enter\n", __func__, s, tag, storage);
	list_for_each_entry_reverse(pos, &tree->mtt_stub, it_link) {
		if (pos->it_sector > s)
			continue;
		else if (pos->it_sector == s) {
			found = 1;
			break;
		} else
			break;
	}
	if (found) {
		pos->it_tags |= (1 << tag);
		return 0;
	} else {
		struct pnfs_inval_tracking *new;
		new = storage;
		new->it_sector = s;
		new->it_tags = (1 << tag);
		list_add(&new->it_link, &pos->it_link);
		return 1;
	}
}

/* XXXX Really want option to not create */
/* Over range, unions tag with existing entries, else creates entry with tag */
static int _set_range(struct my_tree *tree, int32_t tag, u64 s, u64 length)
{
	u64 i;

	dprintk("%s(%i, %llu, %llu) enter\n", __func__, tag, s, length);
	for (i = normalize(s, tree->mtt_step_size); i < s + length;
	     i += tree->mtt_step_size)
		if (_add_entry(tree, i, tag, NULL))
			return -ENOMEM;
	return 0;
}

/* Ensure that future operations on given range of tree will not malloc */
static int _preload_range(struct pnfs_inval_markings *marks,
		u64 offset, u64 length)
{
	u64 start, end, s;
	int count, i, used = 0, status = -ENOMEM;
	struct pnfs_inval_tracking **storage;
	struct my_tree  *tree = &marks->im_tree;

	dprintk("%s(%llu, %llu) enter\n", __func__, offset, length);
	start = normalize(offset, tree->mtt_step_size);
	end = normalize_up(offset + length, tree->mtt_step_size);
	count = (int)(end - start) / (int)tree->mtt_step_size;

	/* Pre-malloc what memory we might need */
	storage = kcalloc(count, sizeof(*storage), GFP_NOFS);
	if (!storage)
		return -ENOMEM;
	for (i = 0; i < count; i++) {
		storage[i] = kmalloc(sizeof(struct pnfs_inval_tracking),
				     GFP_NOFS);
		if (!storage[i])
			goto out_cleanup;
	}

	spin_lock_bh(&marks->im_lock);
	for (s = start; s < end; s += tree->mtt_step_size)
		used += _add_entry(tree, s, INTERNAL_EXISTS, storage[used]);
	spin_unlock_bh(&marks->im_lock);

	status = 0;

 out_cleanup:
	for (i = used; i < count; i++) {
		if (!storage[i])
			break;
		kfree(storage[i]);
	}
	kfree(storage);
	return status;
}

/* We are relying on page lock to serialize this */
int bl_is_sector_init(struct pnfs_inval_markings *marks, sector_t isect)
{
	int rv;

	spin_lock_bh(&marks->im_lock);
	rv = _has_tag(&marks->im_tree, isect, EXTENT_INITIALIZED);
	spin_unlock_bh(&marks->im_lock);
	return rv;
}

/* Assume start, end already sector aligned */
static int
_range_has_tag(struct my_tree *tree, u64 start, u64 end, int32_t tag)
{
	struct pnfs_inval_tracking *pos;
	u64 expect = 0;

	dprintk("%s(%llu, %llu, %i) enter\n", __func__, start, end, tag);
	list_for_each_entry_reverse(pos, &tree->mtt_stub, it_link) {
		if (pos->it_sector >= end)
			continue;
		if (!expect) {
			if ((pos->it_sector == end - tree->mtt_step_size) &&
			    (pos->it_tags & (1 << tag))) {
				expect = pos->it_sector - tree->mtt_step_size;
				if (pos->it_sector < tree->mtt_step_size || expect < start)
					return 1;
				continue;
			} else {
				return 0;
			}
		}
		if (pos->it_sector != expect || !(pos->it_tags & (1 << tag)))
			return 0;
		expect -= tree->mtt_step_size;
		if (expect < start)
			return 1;
	}
	return 0;
}

static int is_range_written(struct pnfs_inval_markings *marks,
			    sector_t start, sector_t end)
{
	int rv;

	spin_lock_bh(&marks->im_lock);
	rv = _range_has_tag(&marks->im_tree, start, end, EXTENT_WRITTEN);
	spin_unlock_bh(&marks->im_lock);
	return rv;
}

/* Marks sectors in [offest, offset_length) as having been initialized.
 * All lengths are step-aligned, where step is min(pagesize, blocksize).
 * Currently assumes offset is page-aligned
 */
int bl_mark_sectors_init(struct pnfs_inval_markings *marks,
			     sector_t offset, sector_t length)
{
	sector_t start, end;

	dprintk("%s(offset=%llu,len=%llu) enter\n",
		__func__, (u64)offset, (u64)length);

	start = normalize(offset, marks->im_block_size);
	end = normalize_up(offset + length, marks->im_block_size);
	if (_preload_range(marks, start, end - start))
		goto outerr;

	spin_lock_bh(&marks->im_lock);
	if (_set_range(&marks->im_tree, EXTENT_INITIALIZED, offset, length))
		goto out_unlock;
	spin_unlock_bh(&marks->im_lock);

	return 0;

out_unlock:
	spin_unlock_bh(&marks->im_lock);
outerr:
	return -ENOMEM;
}

/* Marks sectors in [offest, offset+length) as having been written to disk.
 * All lengths should be block aligned.
 */
static int mark_written_sectors(struct pnfs_inval_markings *marks,
				sector_t offset, sector_t length)
{
	int status;

	dprintk("%s(offset=%llu,len=%llu) enter\n", __func__,
		(u64)offset, (u64)length);
	spin_lock_bh(&marks->im_lock);
	status = _set_range(&marks->im_tree, EXTENT_WRITTEN, offset, length);
	spin_unlock_bh(&marks->im_lock);
	return status;
}

static void print_short_extent(struct pnfs_block_short_extent *be)
{
	dprintk("PRINT SHORT EXTENT extent %p\n", be);
	if (be) {
		dprintk("        be_f_offset %llu\n", (u64)be->bse_f_offset);
		dprintk("        be_length   %llu\n", (u64)be->bse_length);
	}
}

static void print_clist(struct list_head *list, unsigned int count)
{
	struct pnfs_block_short_extent *be;
	unsigned int i = 0;

	ifdebug(FACILITY) {
		printk(KERN_DEBUG "****************\n");
		printk(KERN_DEBUG "Extent list looks like:\n");
		list_for_each_entry(be, list, bse_node) {
			i++;
			print_short_extent(be);
		}
		if (i != count)
			printk(KERN_DEBUG "\n\nExpected %u entries\n\n\n", count);
		printk(KERN_DEBUG "****************\n");
	}
}

/* Note: In theory, we should do more checking that devid's match between
 * old and new, but if they don't, the lists are too corrupt to salvage anyway.
 */
/* Note this is very similar to bl_add_merge_extent */
static void add_to_commitlist(struct pnfs_block_layout *bl,
			      struct pnfs_block_short_extent *new)
{
	struct list_head *clist = &bl->bl_commit;
	struct pnfs_block_short_extent *old, *save;
	sector_t end = new->bse_f_offset + new->bse_length;

	dprintk("%s enter\n", __func__);
	print_short_extent(new);
	print_clist(clist, bl->bl_count);
	bl->bl_count++;
	/* Scan for proper place to insert, extending new to the left
	 * as much as possible.
	 */
	list_for_each_entry_safe(old, save, clist, bse_node) {
		if (new->bse_f_offset < old->bse_f_offset)
			break;
		if (end <= old->bse_f_offset + old->bse_length) {
			/* Range is already in list */
			bl->bl_count--;
			kfree(new);
			return;
		} else if (new->bse_f_offset <=
				old->bse_f_offset + old->bse_length) {
			/* new overlaps or abuts existing be */
			if (new->bse_mdev == old->bse_mdev) {
				/* extend new to fully replace old */
				new->bse_length += new->bse_f_offset -
						old->bse_f_offset;
				new->bse_f_offset = old->bse_f_offset;
				list_del(&old->bse_node);
				bl->bl_count--;
				kfree(old);
			}
		}
	}
	/* Note that if we never hit the above break, old will not point to a
	 * valid extent.  However, in that case &old->bse_node==list.
	 */
	list_add_tail(&new->bse_node, &old->bse_node);
	/* Scan forward for overlaps.  If we find any, extend new and
	 * remove the overlapped extent.
	 */
	old = list_prepare_entry(new, clist, bse_node);
	list_for_each_entry_safe_continue(old, save, clist, bse_node) {
		if (end < old->bse_f_offset)
			break;
		/* new overlaps or abuts old */
		if (new->bse_mdev == old->bse_mdev) {
			if (end < old->bse_f_offset + old->bse_length) {
				/* extend new to fully cover old */
				end = old->bse_f_offset + old->bse_length;
				new->bse_length = end - new->bse_f_offset;
			}
			list_del(&old->bse_node);
			bl->bl_count--;
			kfree(old);
		}
	}
	dprintk("%s: after merging\n", __func__);
	print_clist(clist, bl->bl_count);
}

/* Note the range described by offset, length is guaranteed to be contained
 * within be.
 * new will be freed, either by this function or add_to_commitlist if they
 * decide not to use it, or after LAYOUTCOMMIT uses it in the commitlist.
 */
int bl_mark_for_commit(struct pnfs_block_extent *be,
		    sector_t offset, sector_t length,
		    struct pnfs_block_short_extent *new)
{
	sector_t new_end, end = offset + length;
	struct pnfs_block_layout *bl = container_of(be->be_inval,
						    struct pnfs_block_layout,
						    bl_inval);

	mark_written_sectors(be->be_inval, offset, length);
	/* We want to add the range to commit list, but it must be
	 * block-normalized, and verified that the normalized range has
	 * been entirely written to disk.
	 */
	new->bse_f_offset = offset;
	offset = normalize(offset, bl->bl_blocksize);
	if (offset < new->bse_f_offset) {
		if (is_range_written(be->be_inval, offset, new->bse_f_offset))
			new->bse_f_offset = offset;
		else
			new->bse_f_offset = offset + bl->bl_blocksize;
	}
	new_end = normalize_up(end, bl->bl_blocksize);
	if (end < new_end) {
		if (is_range_written(be->be_inval, end, new_end))
			end = new_end;
		else
			end = new_end - bl->bl_blocksize;
	}
	if (end <= new->bse_f_offset) {
		kfree(new);
		return 0;
	}
	new->bse_length = end - new->bse_f_offset;
	new->bse_devid = be->be_devid;
	new->bse_mdev = be->be_mdev;

	spin_lock(&bl->bl_ext_lock);
	add_to_commitlist(bl, new);
	spin_unlock(&bl->bl_ext_lock);
	return 0;
}

static void print_bl_extent(struct pnfs_block_extent *be)
{
	dprintk("PRINT EXTENT extent %p\n", be);
	if (be) {
		dprintk("        be_f_offset %llu\n", (u64)be->be_f_offset);
		dprintk("        be_length   %llu\n", (u64)be->be_length);
		dprintk("        be_v_offset %llu\n", (u64)be->be_v_offset);
		dprintk("        be_state    %d\n", be->be_state);
	}
}

static void
destroy_extent(struct kref *kref)
{
	struct pnfs_block_extent *be;

	be = container_of(kref, struct pnfs_block_extent, be_refcnt);
	dprintk("%s be=%p\n", __func__, be);
	kfree(be);
}

void
bl_put_extent(struct pnfs_block_extent *be)
{
	if (be) {
		dprintk("%s enter %p (%i)\n", __func__, be,
			atomic_read(&be->be_refcnt.refcount));
		kref_put(&be->be_refcnt, destroy_extent);
	}
}

struct pnfs_block_extent *bl_alloc_extent(void)
{
	struct pnfs_block_extent *be;

	be = kmalloc(sizeof(struct pnfs_block_extent), GFP_NOFS);
	if (!be)
		return NULL;
	INIT_LIST_HEAD(&be->be_node);
	kref_init(&be->be_refcnt);
	be->be_inval = NULL;
	return be;
}

static void print_elist(struct list_head *list)
{
	struct pnfs_block_extent *be;
	dprintk("****************\n");
	dprintk("Extent list looks like:\n");
	list_for_each_entry(be, list, be_node) {
		print_bl_extent(be);
	}
	dprintk("****************\n");
}

static inline int
extents_consistent(struct pnfs_block_extent *old, struct pnfs_block_extent *new)
{
	/* Note this assumes new->be_f_offset >= old->be_f_offset */
	return (new->be_state == old->be_state) &&
		((new->be_state == PNFS_BLOCK_NONE_DATA) ||
		 ((new->be_v_offset - old->be_v_offset ==
		   new->be_f_offset - old->be_f_offset) &&
		  new->be_mdev == old->be_mdev));
}

/* Adds new to appropriate list in bl, modifying new and removing existing
 * extents as appropriate to deal with overlaps.
 *
 * See bl_find_get_extent for list constraints.
 *
 * Refcount on new is already set.  If end up not using it, or error out,
 * need to put the reference.
 *
 * bl->bl_ext_lock is held by caller.
 */
int
bl_add_merge_extent(struct pnfs_block_layout *bl,
		     struct pnfs_block_extent *new)
{
	struct pnfs_block_extent *be, *tmp;
	sector_t end = new->be_f_offset + new->be_length;
	struct list_head *list;

	dprintk("%s enter with be=%p\n", __func__, new);
	print_bl_extent(new);
	list = &bl->bl_extents[bl_choose_list(new->be_state)];
	print_elist(list);

	/* Scan for proper place to insert, extending new to the left
	 * as much as possible.
	 */
	list_for_each_entry_safe_reverse(be, tmp, list, be_node) {
		if (new->be_f_offset >= be->be_f_offset + be->be_length)
			break;
		if (new->be_f_offset >= be->be_f_offset) {
			if (end <= be->be_f_offset + be->be_length) {
				/* new is a subset of existing be*/
				if (extents_consistent(be, new)) {
					dprintk("%s: new is subset, ignoring\n",
						__func__);
					bl_put_extent(new);
					return 0;
				} else {
					goto out_err;
				}
			} else {
				/* |<--   be   -->|
				 *          |<--   new   -->| */
				if (extents_consistent(be, new)) {
					/* extend new to fully replace be */
					new->be_length += new->be_f_offset -
						be->be_f_offset;
					new->be_f_offset = be->be_f_offset;
					new->be_v_offset = be->be_v_offset;
					dprintk("%s: removing %p\n", __func__, be);
					list_del(&be->be_node);
					bl_put_extent(be);
				} else {
					goto out_err;
				}
			}
		} else if (end >= be->be_f_offset + be->be_length) {
			/* new extent overlap existing be */
			if (extents_consistent(be, new)) {
				/* extend new to fully replace be */
				dprintk("%s: removing %p\n", __func__, be);
				list_del(&be->be_node);
				bl_put_extent(be);
			} else {
				goto out_err;
			}
		} else if (end > be->be_f_offset) {
			/*           |<--   be   -->|
			 *|<--   new   -->| */
			if (extents_consistent(new, be)) {
				/* extend new to fully replace be */
				new->be_length += be->be_f_offset + be->be_length -
					new->be_f_offset - new->be_length;
				dprintk("%s: removing %p\n", __func__, be);
				list_del(&be->be_node);
				bl_put_extent(be);
			} else {
				goto out_err;
			}
		}
	}
	/* Note that if we never hit the above break, be will not point to a
	 * valid extent.  However, in that case &be->be_node==list.
	 */
	list_add(&new->be_node, &be->be_node);
	dprintk("%s: inserting new\n", __func__);
	print_elist(list);
	/* FIXME - The per-list consistency checks have all been done,
	 * should now check cross-list consistency.
	 */
	return 0;

 out_err:
	bl_put_extent(new);
	return -EIO;
}

/* Returns extent, or NULL.  If a second READ extent exists, it is returned
 * in cow_read, if given.
 *
 * The extents are kept in two seperate ordered lists, one for READ and NONE,
 * one for READWRITE and INVALID.  Within each list, we assume:
 * 1. Extents are ordered by file offset.
 * 2. For any given isect, there is at most one extents that matches.
 */
struct pnfs_block_extent *
bl_find_get_extent(struct pnfs_block_layout *bl, sector_t isect,
	    struct pnfs_block_extent **cow_read)
{
	struct pnfs_block_extent *be, *cow, *ret;
	int i;

	dprintk("%s enter with isect %llu\n", __func__, (u64)isect);
	cow = ret = NULL;
	spin_lock(&bl->bl_ext_lock);
	for (i = 0; i < EXTENT_LISTS; i++) {
		list_for_each_entry_reverse(be, &bl->bl_extents[i], be_node) {
			if (isect >= be->be_f_offset + be->be_length)
				break;
			if (isect >= be->be_f_offset) {
				/* We have found an extent */
				dprintk("%s Get %p (%i)\n", __func__, be,
					atomic_read(&be->be_refcnt.refcount));
				kref_get(&be->be_refcnt);
				if (!ret)
					ret = be;
				else if (be->be_state != PNFS_BLOCK_READ_DATA)
					bl_put_extent(be);
				else
					cow = be;
				break;
			}
		}
		if (ret &&
		    (!cow_read || ret->be_state != PNFS_BLOCK_INVALID_DATA))
			break;
	}
	spin_unlock(&bl->bl_ext_lock);
	if (cow_read)
		*cow_read = cow;
	print_bl_extent(ret);
	return ret;
}

/* Similar to bl_find_get_extent, but called with lock held, and ignores cow */
static struct pnfs_block_extent *
bl_find_get_extent_locked(struct pnfs_block_layout *bl, sector_t isect)
{
	struct pnfs_block_extent *be, *ret = NULL;
	int i;

	dprintk("%s enter with isect %llu\n", __func__, (u64)isect);
	for (i = 0; i < EXTENT_LISTS; i++) {
		if (ret)
			break;
		list_for_each_entry_reverse(be, &bl->bl_extents[i], be_node) {
			if (isect >= be->be_f_offset + be->be_length)
				break;
			if (isect >= be->be_f_offset) {
				/* We have found an extent */
				dprintk("%s Get %p (%i)\n", __func__, be,
					atomic_read(&be->be_refcnt.refcount));
				kref_get(&be->be_refcnt);
				ret = be;
				break;
			}
		}
	}
	print_bl_extent(ret);
	return ret;
}

int
encode_pnfs_block_layoutupdate(struct pnfs_block_layout *bl,
			       struct xdr_stream *xdr,
			       const struct nfs4_layoutcommit_args *arg)
{
	struct pnfs_block_short_extent *lce, *save;
	unsigned int count = 0;
	__be32 *p, *xdr_start;

	dprintk("%s enter\n", __func__);
	/* BUG - creation of bl_commit is buggy - need to wait for
	 * entire block to be marked WRITTEN before it can be added.
	 */
	spin_lock(&bl->bl_ext_lock);
	/* Want to adjust for possible truncate */
	/* We now want to adjust argument range */

	/* XDR encode the ranges found */
	xdr_start = xdr_reserve_space(xdr, 8);
	if (!xdr_start)
		goto out;
	list_for_each_entry_safe(lce, save, &bl->bl_commit, bse_node) {
		p = xdr_reserve_space(xdr, 7 * 4 + sizeof(lce->bse_devid.data));
		if (!p)
			break;
		p = xdr_encode_opaque_fixed(p, lce->bse_devid.data, NFS4_DEVICEID4_SIZE);
		p = xdr_encode_hyper(p, lce->bse_f_offset << SECTOR_SHIFT);
		p = xdr_encode_hyper(p, lce->bse_length << SECTOR_SHIFT);
		p = xdr_encode_hyper(p, 0LL);
		*p++ = cpu_to_be32(PNFS_BLOCK_READWRITE_DATA);
		list_del(&lce->bse_node);
		list_add_tail(&lce->bse_node, &bl->bl_committing);
		bl->bl_count--;
		count++;
	}
	xdr_start[0] = cpu_to_be32((xdr->p - xdr_start - 1) * 4);
	xdr_start[1] = cpu_to_be32(count);
out:
	spin_unlock(&bl->bl_ext_lock);
	dprintk("%s found %i ranges\n", __func__, count);
	return 0;
}

/* Helper function to set_to_rw that initialize a new extent */
static void
_prep_new_extent(struct pnfs_block_extent *new,
		 struct pnfs_block_extent *orig,
		 sector_t offset, sector_t length, int state)
{
	kref_init(&new->be_refcnt);
	/* don't need to INIT_LIST_HEAD(&new->be_node) */
	memcpy(&new->be_devid, &orig->be_devid, sizeof(struct nfs4_deviceid));
	new->be_mdev = orig->be_mdev;
	new->be_f_offset = offset;
	new->be_length = length;
	new->be_v_offset = orig->be_v_offset - orig->be_f_offset + offset;
	new->be_state = state;
	new->be_inval = orig->be_inval;
}

/* Tries to merge be with extent in front of it in list.
 * Frees storage if not used.
 */
static struct pnfs_block_extent *
_front_merge(struct pnfs_block_extent *be, struct list_head *head,
	     struct pnfs_block_extent *storage)
{
	struct pnfs_block_extent *prev;

	if (!storage)
		goto no_merge;
	if (&be->be_node == head || be->be_node.prev == head)
		goto no_merge;
	prev = list_entry(be->be_node.prev, struct pnfs_block_extent, be_node);
	if ((prev->be_f_offset + prev->be_length != be->be_f_offset) ||
	    !extents_consistent(prev, be))
		goto no_merge;
	_prep_new_extent(storage, prev, prev->be_f_offset,
			 prev->be_length + be->be_length, prev->be_state);
	list_replace(&prev->be_node, &storage->be_node);
	bl_put_extent(prev);
	list_del(&be->be_node);
	bl_put_extent(be);
	return storage;

 no_merge:
	kfree(storage);
	return be;
}

static u64
set_to_rw(struct pnfs_block_layout *bl, u64 offset, u64 length)
{
	u64 rv = offset + length;
	struct pnfs_block_extent *be, *e1, *e2, *e3, *new, *old;
	struct pnfs_block_extent *children[3];
	struct pnfs_block_extent *merge1 = NULL, *merge2 = NULL;
	int i = 0, j;

	dprintk("%s(%llu, %llu)\n", __func__, offset, length);
	/* Create storage for up to three new extents e1, e2, e3 */
	e1 = kmalloc(sizeof(*e1), GFP_ATOMIC);
	e2 = kmalloc(sizeof(*e2), GFP_ATOMIC);
	e3 = kmalloc(sizeof(*e3), GFP_ATOMIC);
	/* BUG - we are ignoring any failure */
	if (!e1 || !e2 || !e3)
		goto out_nosplit;

	spin_lock(&bl->bl_ext_lock);
	be = bl_find_get_extent_locked(bl, offset);
	rv = be->be_f_offset + be->be_length;
	if (be->be_state != PNFS_BLOCK_INVALID_DATA) {
		spin_unlock(&bl->bl_ext_lock);
		goto out_nosplit;
	}
	/* Add e* to children, bumping e*'s krefs */
	if (be->be_f_offset != offset) {
		_prep_new_extent(e1, be, be->be_f_offset,
				 offset - be->be_f_offset,
				 PNFS_BLOCK_INVALID_DATA);
		children[i++] = e1;
		print_bl_extent(e1);
	} else
		merge1 = e1;
	_prep_new_extent(e2, be, offset,
			 min(length, be->be_f_offset + be->be_length - offset),
			 PNFS_BLOCK_READWRITE_DATA);
	children[i++] = e2;
	print_bl_extent(e2);
	if (offset + length < be->be_f_offset + be->be_length) {
		_prep_new_extent(e3, be, e2->be_f_offset + e2->be_length,
				 be->be_f_offset + be->be_length -
				 offset - length,
				 PNFS_BLOCK_INVALID_DATA);
		children[i++] = e3;
		print_bl_extent(e3);
	} else
		merge2 = e3;

	/* Remove be from list, and insert the e* */
	/* We don't get refs on e*, since this list is the base reference
	 * set when init'ed.
	 */
	if (i < 3)
		children[i] = NULL;
	new = children[0];
	list_replace(&be->be_node, &new->be_node);
	bl_put_extent(be);
	new = _front_merge(new, &bl->bl_extents[RW_EXTENT], merge1);
	for (j = 1; j < i; j++) {
		old = new;
		new = children[j];
		list_add(&new->be_node, &old->be_node);
	}
	if (merge2) {
		/* This is a HACK, should just create a _back_merge function */
		new = list_entry(new->be_node.next,
				 struct pnfs_block_extent, be_node);
		new = _front_merge(new, &bl->bl_extents[RW_EXTENT], merge2);
	}
	spin_unlock(&bl->bl_ext_lock);

	/* Since we removed the base reference above, be is now scheduled for
	 * destruction.
	 */
	bl_put_extent(be);
	dprintk("%s returns %llu after split\n", __func__, rv);
	return rv;

 out_nosplit:
	kfree(e1);
	kfree(e2);
	kfree(e3);
	dprintk("%s returns %llu without splitting\n", __func__, rv);
	return rv;
}

void
clean_pnfs_block_layoutupdate(struct pnfs_block_layout *bl,
			      const struct nfs4_layoutcommit_args *arg,
			      int status)
{
	struct pnfs_block_short_extent *lce, *save;

	dprintk("%s status %d\n", __func__, status);
	list_for_each_entry_safe(lce, save, &bl->bl_committing, bse_node) {
		if (likely(!status)) {
			u64 offset = lce->bse_f_offset;
			u64 end = offset + lce->bse_length;

			do {
				offset = set_to_rw(bl, offset, end - offset);
			} while (offset < end);
			list_del(&lce->bse_node);

			kfree(lce);
		} else {
			list_del(&lce->bse_node);
			spin_lock(&bl->bl_ext_lock);
			add_to_commitlist(bl, lce);
			spin_unlock(&bl->bl_ext_lock);
		}
	}
}

int bl_push_one_short_extent(struct pnfs_inval_markings *marks)
{
	struct pnfs_block_short_extent *new;

	new = kmalloc(sizeof(*new), GFP_NOFS);
	if (unlikely(!new))
		return -ENOMEM;

	spin_lock_bh(&marks->im_lock);
	list_add(&new->bse_node, &marks->im_extents);
	spin_unlock_bh(&marks->im_lock);

	return 0;
}

struct pnfs_block_short_extent *
bl_pop_one_short_extent(struct pnfs_inval_markings *marks)
{
	struct pnfs_block_short_extent *rv = NULL;

	spin_lock_bh(&marks->im_lock);
	if (!list_empty(&marks->im_extents)) {
		rv = list_entry((&marks->im_extents)->next,
				struct pnfs_block_short_extent, bse_node);
		list_del_init(&rv->bse_node);
	}
	spin_unlock_bh(&marks->im_lock);

	return rv;
}

void bl_free_short_extents(struct pnfs_inval_markings *marks, int num_to_free)
{
	struct pnfs_block_short_extent *se = NULL, *tmp;

	if (num_to_free <= 0)
		return;

	spin_lock(&marks->im_lock);
	list_for_each_entry_safe(se, tmp, &marks->im_extents, bse_node) {
		list_del(&se->bse_node);
		kfree(se);
		if (--num_to_free == 0)
			break;
	}
	spin_unlock(&marks->im_lock);

	BUG_ON(num_to_free > 0);
}
