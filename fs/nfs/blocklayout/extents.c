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
