/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * Copyright (c) 2016 Krzysztof Blaszkowski
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    yestice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may yest be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Veritas filesystem driver - iyesde routines.
 */
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/namei.h>

#include "vxfs.h"
#include "vxfs_iyesde.h"
#include "vxfs_extern.h"


#ifdef DIAGNOSTIC
/*
 * Dump iyesde contents (partially).
 */
void
vxfs_dumpi(struct vxfs_iyesde_info *vip, iyes_t iyes)
{
	printk(KERN_DEBUG "\n\n");
	if (iyes)
		printk(KERN_DEBUG "dumping vxfs iyesde %ld\n", iyes);
	else
		printk(KERN_DEBUG "dumping unkyeswn vxfs iyesde\n");

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
 * vxfs_transmod - mode for a VxFS iyesde
 * @vip:	VxFS iyesde
 *
 * Description:
 *  vxfs_transmod returns a Linux mode_t for a given
 *  VxFS iyesde structure.
 */
static __inline__ umode_t
vxfs_transmod(struct vxfs_iyesde_info *vip)
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
		struct vxfs_iyesde_info *vip, struct vxfs_diyesde *dip)
{
	struct iyesde *iyesde = &vip->vfs_iyesde;

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

	iyesde->i_mode = vxfs_transmod(vip);
	i_uid_write(iyesde, (uid_t)vip->vii_uid);
	i_gid_write(iyesde, (gid_t)vip->vii_gid);

	set_nlink(iyesde, vip->vii_nlink);
	iyesde->i_size = vip->vii_size;

	iyesde->i_atime.tv_sec = vip->vii_atime;
	iyesde->i_ctime.tv_sec = vip->vii_ctime;
	iyesde->i_mtime.tv_sec = vip->vii_mtime;
	iyesde->i_atime.tv_nsec = 0;
	iyesde->i_ctime.tv_nsec = 0;
	iyesde->i_mtime.tv_nsec = 0;

	iyesde->i_blocks = vip->vii_blocks;
	iyesde->i_generation = vip->vii_gen;
}

/**
 * vxfs_blkiget - find iyesde based on extent #
 * @sbp:	superblock of the filesystem we search in
 * @extent:	number of the extent to search
 * @iyes:	iyesde number to search
 *
 * Description:
 *  vxfs_blkiget searches iyesde @iyes in the filesystem described by
 *  @sbp in the extent @extent.
 *  Returns the matching VxFS iyesde on success, else a NULL pointer.
 *
 * NOTE:
 *  While __vxfs_iget uses the pagecache vxfs_blkiget uses the
 *  buffercache.  This function should yest be used outside the
 *  read_super() method, otherwise the data may be incoherent.
 */
struct iyesde *
vxfs_blkiget(struct super_block *sbp, u_long extent, iyes_t iyes)
{
	struct buffer_head		*bp;
	struct iyesde			*iyesde;
	u_long				block, offset;

	iyesde = new_iyesde(sbp);
	if (!iyesde)
		return NULL;
	iyesde->i_iyes = get_next_iyes();

	block = extent + ((iyes * VXFS_ISIZE) / sbp->s_blocksize);
	offset = ((iyes % (sbp->s_blocksize / VXFS_ISIZE)) * VXFS_ISIZE);
	bp = sb_bread(sbp, block);

	if (bp && buffer_mapped(bp)) {
		struct vxfs_iyesde_info	*vip = VXFS_INO(iyesde);
		struct vxfs_diyesde	*dip;

		dip = (struct vxfs_diyesde *)(bp->b_data + offset);
		dip2vip_cpy(VXFS_SBI(sbp), vip, dip);
		vip->vfs_iyesde.i_mapping->a_ops = &vxfs_aops;
#ifdef DIAGNOSTIC
		vxfs_dumpi(vip, iyes);
#endif
		brelse(bp);
		return iyesde;
	}

	printk(KERN_WARNING "vxfs: unable to read block %ld\n", block);
	brelse(bp);
	iput(iyesde);
	return NULL;
}

/**
 * __vxfs_iget - generic find iyesde facility
 * @ilistp:		iyesde list
 * @vip:		VxFS iyesde to fill in
 * @iyes:		iyesde number
 *
 * Description:
 *  Search the for iyesde number @iyes in the filesystem
 *  described by @sbp.  Use the specified iyesde table (@ilistp).
 *  Returns the matching iyesde on success, else an error code.
 */
static int
__vxfs_iget(struct iyesde *ilistp, struct vxfs_iyesde_info *vip, iyes_t iyes)
{
	struct page			*pp;
	u_long				offset;

	offset = (iyes % (PAGE_SIZE / VXFS_ISIZE)) * VXFS_ISIZE;
	pp = vxfs_get_page(ilistp->i_mapping, iyes * VXFS_ISIZE / PAGE_SIZE);

	if (!IS_ERR(pp)) {
		struct vxfs_diyesde	*dip;
		caddr_t			kaddr = (char *)page_address(pp);

		dip = (struct vxfs_diyesde *)(kaddr + offset);
		dip2vip_cpy(VXFS_SBI(ilistp->i_sb), vip, dip);
		vip->vfs_iyesde.i_mapping->a_ops = &vxfs_aops;
#ifdef DIAGNOSTIC
		vxfs_dumpi(vip, iyes);
#endif
		vxfs_put_page(pp);
		return 0;
	}

	printk(KERN_WARNING "vxfs: error on page 0x%p for iyesde %ld\n",
		pp, (unsigned long)iyes);
	return PTR_ERR(pp);
}

/**
 * vxfs_stiget - find iyesde using the structural iyesde list
 * @sbp:	VFS superblock
 * @iyes:	iyesde #
 *
 * Description:
 *  Find iyesde @iyes in the filesystem described by @sbp using
 *  the structural iyesde list.
 *  Returns the matching iyesde on success, else a NULL pointer.
 */
struct iyesde *
vxfs_stiget(struct super_block *sbp, iyes_t iyes)
{
	struct iyesde *iyesde;
	int error;

	iyesde = new_iyesde(sbp);
	if (!iyesde)
		return NULL;
	iyesde->i_iyes = get_next_iyes();

	error = __vxfs_iget(VXFS_SBI(sbp)->vsi_stilist, VXFS_INO(iyesde), iyes);
	if (error) {
		iput(iyesde);
		return NULL;
	}

	return iyesde;
}

/**
 * vxfs_iget - get an iyesde
 * @sbp:	the superblock to get the iyesde for
 * @iyes:	the number of the iyesde to get
 *
 * Description:
 *  vxfs_read_iyesde creates an iyesde, reads the disk iyesde for @iyes and fills
 *  in all relevant fields in the new iyesde.
 */
struct iyesde *
vxfs_iget(struct super_block *sbp, iyes_t iyes)
{
	struct vxfs_iyesde_info		*vip;
	const struct address_space_operations	*aops;
	struct iyesde *ip;
	int error;

	ip = iget_locked(sbp, iyes);
	if (!ip)
		return ERR_PTR(-ENOMEM);
	if (!(ip->i_state & I_NEW))
		return ip;

	vip = VXFS_INO(ip);
	error = __vxfs_iget(VXFS_SBI(sbp)->vsi_ilist, vip, iyes);
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
		ip->i_op = &vxfs_dir_iyesde_ops;
		ip->i_fop = &vxfs_dir_operations;
		ip->i_mapping->a_ops = aops;
	} else if (S_ISLNK(ip->i_mode)) {
		if (!VXFS_ISIMMED(vip)) {
			ip->i_op = &page_symlink_iyesde_operations;
			iyesde_yeshighmem(ip);
			ip->i_mapping->a_ops = &vxfs_aops;
		} else {
			ip->i_op = &simple_symlink_iyesde_operations;
			ip->i_link = vip->vii_immed.vi_immed;
			nd_terminate_link(ip->i_link, ip->i_size,
					  sizeof(vip->vii_immed.vi_immed) - 1);
		}
	} else
		init_special_iyesde(ip, ip->i_mode, old_decode_dev(vip->vii_rdev));

	unlock_new_iyesde(ip);
	return ip;
}

/**
 * vxfs_evict_iyesde - remove iyesde from main memory
 * @ip:		iyesde to discard.
 *
 * Description:
 *  vxfs_evict_iyesde() is called on the final iput and frees the private
 *  iyesde area.
 */
void
vxfs_evict_iyesde(struct iyesde *ip)
{
	truncate_iyesde_pages_final(&ip->i_data);
	clear_iyesde(ip);
}
