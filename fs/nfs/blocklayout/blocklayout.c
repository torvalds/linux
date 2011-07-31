/*
 *  linux/fs/nfs/blocklayout/blocklayout.c
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
#include <linux/module.h>
#include <linux/init.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY	NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Adamson <andros@citi.umich.edu>");
MODULE_DESCRIPTION("The NFSv4.1 pNFS Block layout driver");

static enum pnfs_try_status
bl_read_pagelist(struct nfs_read_data *rdata)
{
	return PNFS_NOT_ATTEMPTED;
}

static enum pnfs_try_status
bl_write_pagelist(struct nfs_write_data *wdata,
		  int sync)
{
	return PNFS_NOT_ATTEMPTED;
}

/* STUB */
static void
release_extents(struct pnfs_block_layout *bl,
		struct pnfs_layout_range *range)
{
	return;
}

/* STUB */
static void
release_inval_marks(struct pnfs_inval_markings *marks)
{
	return;
}

static void bl_free_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct pnfs_block_layout *bl = BLK_LO2EXT(lo);

	dprintk("%s enter\n", __func__);
	release_extents(bl, NULL);
	release_inval_marks(&bl->bl_inval);
	kfree(bl);
}

static struct pnfs_layout_hdr *bl_alloc_layout_hdr(struct inode *inode,
						   gfp_t gfp_flags)
{
	struct pnfs_block_layout *bl;

	dprintk("%s enter\n", __func__);
	bl = kzalloc(sizeof(*bl), gfp_flags);
	if (!bl)
		return NULL;
	spin_lock_init(&bl->bl_ext_lock);
	INIT_LIST_HEAD(&bl->bl_extents[0]);
	INIT_LIST_HEAD(&bl->bl_extents[1]);
	INIT_LIST_HEAD(&bl->bl_commit);
	INIT_LIST_HEAD(&bl->bl_committing);
	bl->bl_count = 0;
	bl->bl_blocksize = NFS_SERVER(inode)->pnfs_blksize >> SECTOR_SHIFT;
	BL_INIT_INVAL_MARKS(&bl->bl_inval, bl->bl_blocksize);
	return &bl->bl_layout;
}

static void
bl_free_lseg(struct pnfs_layout_segment *lseg)
{
}

static struct pnfs_layout_segment *
bl_alloc_lseg(struct pnfs_layout_hdr *lo,
	      struct nfs4_layoutget_res *lgr, gfp_t gfp_flags)
{
	return NULL;
}

static void
bl_encode_layoutcommit(struct pnfs_layout_hdr *lo, struct xdr_stream *xdr,
		       const struct nfs4_layoutcommit_args *arg)
{
}

static void
bl_cleanup_layoutcommit(struct nfs4_layoutcommit_data *lcdata)
{
}

static int
bl_set_layoutdriver(struct nfs_server *server, const struct nfs_fh *fh)
{
	dprintk("%s enter\n", __func__);
	return 0;
}

static int
bl_clear_layoutdriver(struct nfs_server *server)
{
	dprintk("%s enter\n", __func__);
	return 0;
}

static struct pnfs_layoutdriver_type blocklayout_type = {
	.id				= LAYOUT_BLOCK_VOLUME,
	.name				= "LAYOUT_BLOCK_VOLUME",
	.read_pagelist			= bl_read_pagelist,
	.write_pagelist			= bl_write_pagelist,
	.alloc_layout_hdr		= bl_alloc_layout_hdr,
	.free_layout_hdr		= bl_free_layout_hdr,
	.alloc_lseg			= bl_alloc_lseg,
	.free_lseg			= bl_free_lseg,
	.encode_layoutcommit		= bl_encode_layoutcommit,
	.cleanup_layoutcommit		= bl_cleanup_layoutcommit,
	.set_layoutdriver		= bl_set_layoutdriver,
	.clear_layoutdriver		= bl_clear_layoutdriver,
};

static int __init nfs4blocklayout_init(void)
{
	int ret;

	dprintk("%s: NFSv4 Block Layout Driver Registering...\n", __func__);

	ret = pnfs_register_layoutdriver(&blocklayout_type);
	return ret;
}

static void __exit nfs4blocklayout_exit(void)
{
	dprintk("%s: NFSv4 Block Layout Driver Unregistering...\n",
	       __func__);

	pnfs_unregister_layoutdriver(&blocklayout_type);
}

MODULE_ALIAS("nfs-layouttype4-3");

module_init(nfs4blocklayout_init);
module_exit(nfs4blocklayout_exit);
