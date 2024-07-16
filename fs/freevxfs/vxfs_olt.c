// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 */

/* 
 * Veritas filesystem driver - object location table support.
 */
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>

#include "vxfs.h"
#include "vxfs_olt.h"
#include "vxfs_extern.h"


static inline void
vxfs_get_fshead(struct vxfs_oltfshead *fshp, struct vxfs_sb_info *infp)
{
	BUG_ON(infp->vsi_fshino);
	infp->vsi_fshino = fs32_to_cpu(infp, fshp->olt_fsino[0]);
}

static inline void
vxfs_get_ilist(struct vxfs_oltilist *ilistp, struct vxfs_sb_info *infp)
{
	BUG_ON(infp->vsi_iext);
	infp->vsi_iext = fs32_to_cpu(infp, ilistp->olt_iext[0]);
}

static inline u_long
vxfs_oblock(struct super_block *sbp, daddr_t block, u_long bsize)
{
	BUG_ON(sbp->s_blocksize % bsize);
	return (block * (sbp->s_blocksize / bsize));
}


/**
 * vxfs_read_olt - read olt
 * @sbp:	superblock of the filesystem
 * @bsize:	blocksize of the filesystem
 *
 * Description:
 *   vxfs_read_olt reads the olt of the filesystem described by @sbp
 *   into main memory and does some basic setup.
 *
 * Returns:
 *   Zero on success, else a negative error code.
 */
int
vxfs_read_olt(struct super_block *sbp, u_long bsize)
{
	struct vxfs_sb_info	*infp = VXFS_SBI(sbp);
	struct buffer_head	*bp;
	struct vxfs_olt		*op;
	char			*oaddr, *eaddr;

	bp = sb_bread(sbp, vxfs_oblock(sbp, infp->vsi_oltext, bsize));
	if (!bp || !bp->b_data)
		goto fail;

	op = (struct vxfs_olt *)bp->b_data;
	if (fs32_to_cpu(infp, op->olt_magic) != VXFS_OLT_MAGIC) {
		printk(KERN_NOTICE "vxfs: ivalid olt magic number\n");
		goto fail;
	}

	/*
	 * It is in theory possible that vsi_oltsize is > 1.
	 * I've not seen any such filesystem yet and I'm lazy..  --hch
	 */
	if (infp->vsi_oltsize > 1) {
		printk(KERN_NOTICE "vxfs: oltsize > 1 detected.\n");
		printk(KERN_NOTICE "vxfs: please notify hch@infradead.org\n");
		goto fail;
	}

	oaddr = bp->b_data + fs32_to_cpu(infp, op->olt_size);
	eaddr = bp->b_data + (infp->vsi_oltsize * sbp->s_blocksize);

	while (oaddr < eaddr) {
		struct vxfs_oltcommon	*ocp =
			(struct vxfs_oltcommon *)oaddr;
		
		switch (fs32_to_cpu(infp, ocp->olt_type)) {
		case VXFS_OLT_FSHEAD:
			vxfs_get_fshead((struct vxfs_oltfshead *)oaddr, infp);
			break;
		case VXFS_OLT_ILIST:
			vxfs_get_ilist((struct vxfs_oltilist *)oaddr, infp);
			break;
		}

		oaddr += fs32_to_cpu(infp, ocp->olt_size);
	}

	brelse(bp);
	return (infp->vsi_fshino && infp->vsi_iext) ? 0 : -EINVAL;

fail:
	brelse(bp);
	return -EINVAL;
}
