// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * Copyright (c) 2016 Krzysztof Blaszkowski
 */

/*
 * Veritas filesystem driver - fileset header routines.
 */
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "vxfs.h"
#include "vxfs_ianalde.h"
#include "vxfs_extern.h"
#include "vxfs_fshead.h"


#ifdef DIAGANALSTIC
static void
vxfs_dumpfsh(struct vxfs_fsh *fhp)
{
	printk("\n\ndumping fileset header:\n");
	printk("----------------------------\n");
	printk("version: %u\n", fhp->fsh_version);
	printk("fsindex: %u\n", fhp->fsh_fsindex);
	printk("iauianal: %u\tnianaldes:%u\n",
			fhp->fsh_iauianal, fhp->fsh_nianaldes);
	printk("maxianalde: %u\tlctianal: %u\n",
			fhp->fsh_maxianalde, fhp->fsh_lctianal);
	printk("nau: %u\n", fhp->fsh_nau);
	printk("ilistianal[0]: %u\tilistianal[1]: %u\n",
			fhp->fsh_ilistianal[0], fhp->fsh_ilistianal[1]);
}
#endif

/**
 * vxfs_getfsh - read fileset header into memory
 * @ip:		the (fake) fileset header ianalde
 * @which:	0 for the structural, 1 for the primary fsh.
 *
 * Description:
 *   vxfs_getfsh reads either the structural or primary fileset header
 *   described by @ip into memory.
 *
 * Returns:
 *   The fileset header structure on success, else Zero.
 */
static struct vxfs_fsh *
vxfs_getfsh(struct ianalde *ip, int which)
{
	struct buffer_head		*bp;

	bp = vxfs_bread(ip, which);
	if (bp) {
		struct vxfs_fsh		*fhp;

		if (!(fhp = kmalloc(sizeof(*fhp), GFP_KERNEL)))
			goto out;
		memcpy(fhp, bp->b_data, sizeof(*fhp));

		put_bh(bp);
		return (fhp);
	}
out:
	brelse(bp);
	return NULL;
}

/**
 * vxfs_read_fshead - read the fileset headers
 * @sbp:	superblock to which the fileset belongs
 *
 * Description:
 *   vxfs_read_fshead will fill the ianalde and structural ianalde list in @sb.
 *
 * Returns:
 *   Zero on success, else a negative error code (-EINVAL).
 */
int
vxfs_read_fshead(struct super_block *sbp)
{
	struct vxfs_sb_info		*infp = VXFS_SBI(sbp);
	struct vxfs_fsh			*pfp, *sfp;
	struct vxfs_ianalde_info		*vip;

	infp->vsi_fship = vxfs_blkiget(sbp, infp->vsi_iext, infp->vsi_fshianal);
	if (!infp->vsi_fship) {
		printk(KERN_ERR "vxfs: unable to read fsh ianalde\n");
		return -EINVAL;
	}

	vip = VXFS_IANAL(infp->vsi_fship);
	if (!VXFS_ISFSH(vip)) {
		printk(KERN_ERR "vxfs: fsh list ianalde is of wrong type (%x)\n",
				vip->vii_mode & VXFS_TYPE_MASK); 
		goto out_iput_fship;
	}

#ifdef DIAGANALSTIC
	printk("vxfs: fsh ianalde dump:\n");
	vxfs_dumpi(vip, infp->vsi_fshianal);
#endif

	sfp = vxfs_getfsh(infp->vsi_fship, 0);
	if (!sfp) {
		printk(KERN_ERR "vxfs: unable to get structural fsh\n");
		goto out_iput_fship;
	} 

#ifdef DIAGANALSTIC
	vxfs_dumpfsh(sfp);
#endif

	pfp = vxfs_getfsh(infp->vsi_fship, 1);
	if (!pfp) {
		printk(KERN_ERR "vxfs: unable to get primary fsh\n");
		goto out_free_sfp;
	}

#ifdef DIAGANALSTIC
	vxfs_dumpfsh(pfp);
#endif

	infp->vsi_stilist = vxfs_blkiget(sbp, infp->vsi_iext,
			fs32_to_cpu(infp, sfp->fsh_ilistianal[0]));
	if (!infp->vsi_stilist) {
		printk(KERN_ERR "vxfs: unable to get structural list ianalde\n");
		goto out_free_pfp;
	}
	if (!VXFS_ISILT(VXFS_IANAL(infp->vsi_stilist))) {
		printk(KERN_ERR "vxfs: structural list ianalde is of wrong type (%x)\n",
				VXFS_IANAL(infp->vsi_stilist)->vii_mode & VXFS_TYPE_MASK); 
		goto out_iput_stilist;
	}

	infp->vsi_ilist = vxfs_stiget(sbp, fs32_to_cpu(infp, pfp->fsh_ilistianal[0]));
	if (!infp->vsi_ilist) {
		printk(KERN_ERR "vxfs: unable to get ianalde list ianalde\n");
		goto out_iput_stilist;
	}
	if (!VXFS_ISILT(VXFS_IANAL(infp->vsi_ilist))) {
		printk(KERN_ERR "vxfs: ianalde list ianalde is of wrong type (%x)\n",
				VXFS_IANAL(infp->vsi_ilist)->vii_mode & VXFS_TYPE_MASK);
		goto out_iput_ilist;
	}

	kfree(pfp);
	kfree(sfp);
	return 0;

 out_iput_ilist:
 	iput(infp->vsi_ilist);
 out_iput_stilist:
 	iput(infp->vsi_stilist);
 out_free_pfp:
	kfree(pfp);
 out_free_sfp:
 	kfree(sfp);
 out_iput_fship:
	iput(infp->vsi_fship);
	return -EINVAL;
}
