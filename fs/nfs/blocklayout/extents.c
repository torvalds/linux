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
	list_for_each_entry_safe(be, tmp, list, be_node) {
		if (new->be_f_offset < be->be_f_offset)
			break;
		if (end <= be->be_f_offset + be->be_length) {
			/* new is a subset of existing be*/
			if (extents_consistent(be, new)) {
				dprintk("%s: new is subset, ignoring\n",
					__func__);
				bl_put_extent(new);
				return 0;
			} else
				goto out_err;
		} else if (new->be_f_offset <=
				be->be_f_offset + be->be_length) {
			/* new overlaps or abuts existing be */
			if (extents_consistent(be, new)) {
				/* extend new to fully replace be */
				new->be_length += new->be_f_offset -
						  be->be_f_offset;
				new->be_f_offset = be->be_f_offset;
				new->be_v_offset = be->be_v_offset;
				dprintk("%s: removing %p\n", __func__, be);
				list_del(&be->be_node);
				bl_put_extent(be);
			} else if (new->be_f_offset !=
				   be->be_f_offset + be->be_length)
				goto out_err;
		}
	}
	/* Note that if we never hit the above break, be will not point to a
	 * valid extent.  However, in that case &be->be_node==list.
	 */
	list_add_tail(&new->be_node, &be->be_node);
	dprintk("%s: inserting new\n", __func__);
	print_elist(list);
	/* Scan forward for overlaps.  If we find any, extend new and
	 * remove the overlapped extent.
	 */
	be = list_prepare_entry(new, list, be_node);
	list_for_each_entry_safe_continue(be, tmp, list, be_node) {
		if (end < be->be_f_offset)
			break;
		/* new overlaps or abuts existing be */
		if (extents_consistent(be, new)) {
			if (end < be->be_f_offset + be->be_length) {
				/* extend new to fully cover be */
				end = be->be_f_offset + be->be_length;
				new->be_length = end - new->be_f_offset;
			}
			dprintk("%s: removing %p\n", __func__, be);
			list_del(&be->be_node);
			bl_put_extent(be);
		} else if (end != be->be_f_offset) {
			list_del(&new->be_node);
			goto out_err;
		}
	}
	dprintk("%s: after merging\n", __func__);
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
