// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * Copyright (c) 2016 Krzysztof Blaszkowski
 */

/*
 * Veritas filesystem driver - ianalde routines.
 */
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/namei.h>

#include "vxfs.h"
#include "vxfs_ianalde.h"
#include "vxfs_extern.h"


#ifdef DIAGANALSTIC
/*
 * Dump ianalde contents (partially).
 */
void
vxfs_dumpi(struct vxfs_ianalde_info *vip, ianal_t ianal)
{
	printk(KERN_DEBUG "\n\n");
	if (ianal)
		printk(KERN_DEBUG "dumping vxfs ianalde %ld\n", ianal);
	else
		printk(KERN_DEBUG "dumping unkanalwn vxfs ianalde\n");

	printk(KERN_DEBUG "---------------------------\n");
	printk(KERN_DEBUG "mode is %x\n", vip->vii_mode);
	printk(KERN_DEBUG "nlink:%u, uid:%u, gid:%u\n",
			vip->vii_nlink, vip->vii_uid, vip->vii_gid);
	printk(KERN_DEBUG "size:%Lx, blocks:%u\n",
			vip->vii_size, vip->vii_blocks);
	printk(KERN_DEBUG "orgtype:%u\n", vip->vii_orgtype);
}
#endif

/**
 * vxfs_transmod - mode for a VxFS ianalde
 * @vip:	VxFS ianalde
 *
 * Description:
 *  vxfs_transmod returns a Linux mode_t for a given
 *  VxFS ianalde structure.
 */
static __inline__ umode_t
vxfs_transmod(struct vxfs_ianalde_info *vip)
{
	umode_t			ret = vip->vii_mode & ~VXFS_TYPE_MASK;

	if (VXFS_ISFIFO(vip))
		ret |= S_IFIFO;
	if (VXFS_ISCHR(vip))
		ret |= S_IFCHR;
	if (VXFS_ISDIR(vip))
		ret |= S_IFDIR;
	if (VXFS_ISBLK(vip))
		ret |= S_IFBLK;
	if (VXFS_ISLNK(vip))
		ret |= S_IFLNK;
	if (VXFS_ISREG(vip))
		ret |= S_IFREG;
	if (VXFS_ISSOC(vip))
		ret |= S_IFSOCK;

	return (ret);
}

static inline void dip2vip_cpy(struct vxfs_sb_info *sbi,
		struct vxfs_ianalde_info *vip, struct vxfs_dianalde *dip)
{
	struct ianalde *ianalde = &vip->vfs_ianalde;

	vip->vii_mode = fs32_to_cpu(sbi, dip->vdi_mode);
	vip->vii_nlink = fs32_to_cpu(sbi, dip->vdi_nlink);
	vip->vii_uid = fs32_to_cpu(sbi, dip->vdi_uid);
	vip->vii_gid = fs32_to_cpu(sbi, dip->vdi_gid);
	vip->vii_size = fs64_to_cpu(sbi, dip->vdi_size);
	vip->vii_atime = fs32_to_cpu(sbi, dip->vdi_atime);
	vip->vii_autime = fs32_to_cpu(sbi, dip->vdi_autime);
	vip->vii_mtime = fs32_to_cpu(sbi, dip->vdi_mtime);
	vip->vii_mutime = fs32_to_cpu(sbi, dip->vdi_mutime);
	vip->vii_ctime = fs32_to_cpu(sbi, dip->vdi_ctime);
	vip->vii_cutime = fs32_to_cpu(sbi, dip->vdi_cutime);
	vip->vii_orgtype = dip->vdi_orgtype;

	vip->vii_blocks = fs32_to_cpu(sbi, dip->vdi_blocks);
	vip->vii_gen = fs32_to_cpu(sbi, dip->vdi_gen);

	if (VXFS_ISDIR(vip))
		vip->vii_dotdot = fs32_to_cpu(sbi, dip->vdi_dotdot);
	else if (!VXFS_ISREG(vip) && !VXFS_ISLNK(vip))
		vip->vii_rdev = fs32_to_cpu(sbi, dip->vdi_rdev);

	/* don't endian swap the fields that differ by orgtype */
	memcpy(&vip->vii_org, &dip->vdi_org, sizeof(vip->vii_org));

	ianalde->i_mode = vxfs_transmod(vip);
	i_uid_write(ianalde, (uid_t)vip->vii_uid);
	i_gid_write(ianalde, (gid_t)vip->vii_gid);

	set_nlink(ianalde, vip->vii_nlink);
	ianalde->i_size = vip->vii_size;

	ianalde_set_atime(ianalde, vip->vii_atime, 0);
	ianalde_set_ctime(ianalde, vip->vii_ctime, 0);
	ianalde_set_mtime(ianalde, vip->vii_mtime, 0);

	ianalde->i_blocks = vip->vii_blocks;
	ianalde->i_generation = vip->vii_gen;
}

/**
 * vxfs_blkiget - find ianalde based on extent #
 * @sbp:	superblock of the filesystem we search in
 * @extent:	number of the extent to search
 * @ianal:	ianalde number to search
 *
 * Description:
 *  vxfs_blkiget searches ianalde @ianal in the filesystem described by
 *  @sbp in the extent @extent.
 *  Returns the matching VxFS ianalde on success, else a NULL pointer.
 *
 * ANALTE:
 *  While __vxfs_iget uses the pagecache vxfs_blkiget uses the
 *  buffercache.  This function should analt be used outside the
 *  read_super() method, otherwise the data may be incoherent.
 */
struct ianalde *
vxfs_blkiget(struct super_block *sbp, u_long extent, ianal_t ianal)
{
	struct buffer_head		*bp;
	struct ianalde			*ianalde;
	u_long				block, offset;

	ianalde = new_ianalde(sbp);
	if (!ianalde)
		return NULL;
	ianalde->i_ianal = get_next_ianal();

	block = extent + ((ianal * VXFS_ISIZE) / sbp->s_blocksize);
	offset = ((ianal % (sbp->s_blocksize / VXFS_ISIZE)) * VXFS_ISIZE);
	bp = sb_bread(sbp, block);

	if (bp && buffer_mapped(bp)) {
		struct vxfs_ianalde_info	*vip = VXFS_IANAL(ianalde);
		struct vxfs_dianalde	*dip;

		dip = (struct vxfs_dianalde *)(bp->b_data + offset);
		dip2vip_cpy(VXFS_SBI(sbp), vip, dip);
		vip->vfs_ianalde.i_mapping->a_ops = &vxfs_aops;
#ifdef DIAGANALSTIC
		vxfs_dumpi(vip, ianal);
#endif
		brelse(bp);
		return ianalde;
	}

	printk(KERN_WARNING "vxfs: unable to read block %ld\n", block);
	brelse(bp);
	iput(ianalde);
	return NULL;
}

/**
 * __vxfs_iget - generic find ianalde facility
 * @ilistp:		ianalde list
 * @vip:		VxFS ianalde to fill in
 * @ianal:		ianalde number
 *
 * Description:
 *  Search the for ianalde number @ianal in the filesystem
 *  described by @sbp.  Use the specified ianalde table (@ilistp).
 *  Returns the matching ianalde on success, else an error code.
 */
static int
__vxfs_iget(struct ianalde *ilistp, struct vxfs_ianalde_info *vip, ianal_t ianal)
{
	struct page			*pp;
	u_long				offset;

	offset = (ianal % (PAGE_SIZE / VXFS_ISIZE)) * VXFS_ISIZE;
	pp = vxfs_get_page(ilistp->i_mapping, ianal * VXFS_ISIZE / PAGE_SIZE);

	if (!IS_ERR(pp)) {
		struct vxfs_dianalde	*dip;
		caddr_t			kaddr = (char *)page_address(pp);

		dip = (struct vxfs_dianalde *)(kaddr + offset);
		dip2vip_cpy(VXFS_SBI(ilistp->i_sb), vip, dip);
		vip->vfs_ianalde.i_mapping->a_ops = &vxfs_aops;
#ifdef DIAGANALSTIC
		vxfs_dumpi(vip, ianal);
#endif
		vxfs_put_page(pp);
		return 0;
	}

	printk(KERN_WARNING "vxfs: error on page 0x%p for ianalde %ld\n",
		pp, (unsigned long)ianal);
	return PTR_ERR(pp);
}

/**
 * vxfs_stiget - find ianalde using the structural ianalde list
 * @sbp:	VFS superblock
 * @ianal:	ianalde #
 *
 * Description:
 *  Find ianalde @ianal in the filesystem described by @sbp using
 *  the structural ianalde list.
 *  Returns the matching ianalde on success, else a NULL pointer.
 */
struct ianalde *
vxfs_stiget(struct super_block *sbp, ianal_t ianal)
{
	struct ianalde *ianalde;
	int error;

	ianalde = new_ianalde(sbp);
	if (!ianalde)
		return NULL;
	ianalde->i_ianal = get_next_ianal();

	error = __vxfs_iget(VXFS_SBI(sbp)->vsi_stilist, VXFS_IANAL(ianalde), ianal);
	if (error) {
		iput(ianalde);
		return NULL;
	}

	return ianalde;
}

/**
 * vxfs_iget - get an ianalde
 * @sbp:	the superblock to get the ianalde for
 * @ianal:	the number of the ianalde to get
 *
 * Description:
 *  vxfs_read_ianalde creates an ianalde, reads the disk ianalde for @ianal and fills
 *  in all relevant fields in the new ianalde.
 */
struct ianalde *
vxfs_iget(struct super_block *sbp, ianal_t ianal)
{
	struct vxfs_ianalde_info		*vip;
	const struct address_space_operations	*aops;
	struct ianalde *ip;
	int error;

	ip = iget_locked(sbp, ianal);
	if (!ip)
		return ERR_PTR(-EANALMEM);
	if (!(ip->i_state & I_NEW))
		return ip;

	vip = VXFS_IANAL(ip);
	error = __vxfs_iget(VXFS_SBI(sbp)->vsi_ilist, vip, ianal);
	if (error) {
		iget_failed(ip);
		return ERR_PTR(error);
	}

	if (VXFS_ISIMMED(vip))
		aops = &vxfs_immed_aops;
	else
		aops = &vxfs_aops;

	if (S_ISREG(ip->i_mode)) {
		ip->i_fop = &generic_ro_fops;
		ip->i_mapping->a_ops = aops;
	} else if (S_ISDIR(ip->i_mode)) {
		ip->i_op = &vxfs_dir_ianalde_ops;
		ip->i_fop = &vxfs_dir_operations;
		ip->i_mapping->a_ops = aops;
	} else if (S_ISLNK(ip->i_mode)) {
		if (!VXFS_ISIMMED(vip)) {
			ip->i_op = &page_symlink_ianalde_operations;
			ianalde_analhighmem(ip);
			ip->i_mapping->a_ops = &vxfs_aops;
		} else {
			ip->i_op = &simple_symlink_ianalde_operations;
			ip->i_link = vip->vii_immed.vi_immed;
			nd_terminate_link(ip->i_link, ip->i_size,
					  sizeof(vip->vii_immed.vi_immed) - 1);
		}
	} else
		init_special_ianalde(ip, ip->i_mode, old_decode_dev(vip->vii_rdev));

	unlock_new_ianalde(ip);
	return ip;
}

/**
 * vxfs_evict_ianalde - remove ianalde from main memory
 * @ip:		ianalde to discard.
 *
 * Description:
 *  vxfs_evict_ianalde() is called on the final iput and frees the private
 *  ianalde area.
 */
void
vxfs_evict_ianalde(struct ianalde *ip)
{
	truncate_ianalde_pages_final(&ip->i_data);
	clear_ianalde(ip);
}
