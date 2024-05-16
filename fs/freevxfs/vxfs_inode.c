// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * Copyright (c) 2016 Krzysztof Blaszkowski
 */

/*
 * Veritas filesystem driver - inode routines.
 */
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/namei.h>

#include "vxfs.h"
#include "vxfs_inode.h"
#include "vxfs_extern.h"


#ifdef DIAGNOSTIC
/*
 * Dump inode contents (partially).
 */
void
vxfs_dumpi(struct vxfs_inode_info *vip, ino_t ino)
{
	printk(KERN_DEBUG "\n\n");
	if (ino)
		printk(KERN_DEBUG "dumping vxfs inode %ld\n", ino);
	else
		printk(KERN_DEBUG "dumping unknown vxfs inode\n");

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
 * vxfs_transmod - mode for a VxFS inode
 * @vip:	VxFS inode
 *
 * Description:
 *  vxfs_transmod returns a Linux mode_t for a given
 *  VxFS inode structure.
 */
static __inline__ umode_t
vxfs_transmod(struct vxfs_inode_info *vip)
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
		struct vxfs_inode_info *vip, struct vxfs_dinode *dip)
{
	struct inode *inode = &vip->vfs_inode;

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

	inode->i_mode = vxfs_transmod(vip);
	i_uid_write(inode, (uid_t)vip->vii_uid);
	i_gid_write(inode, (gid_t)vip->vii_gid);

	set_nlink(inode, vip->vii_nlink);
	inode->i_size = vip->vii_size;

	inode->i_atime.tv_sec = vip->vii_atime;
	inode_set_ctime(inode, vip->vii_ctime, 0);
	inode->i_mtime.tv_sec = vip->vii_mtime;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;

	inode->i_blocks = vip->vii_blocks;
	inode->i_generation = vip->vii_gen;
}

/**
 * vxfs_blkiget - find inode based on extent #
 * @sbp:	superblock of the filesystem we search in
 * @extent:	number of the extent to search
 * @ino:	inode number to search
 *
 * Description:
 *  vxfs_blkiget searches inode @ino in the filesystem described by
 *  @sbp in the extent @extent.
 *  Returns the matching VxFS inode on success, else a NULL pointer.
 *
 * NOTE:
 *  While __vxfs_iget uses the pagecache vxfs_blkiget uses the
 *  buffercache.  This function should not be used outside the
 *  read_super() method, otherwise the data may be incoherent.
 */
struct inode *
vxfs_blkiget(struct super_block *sbp, u_long extent, ino_t ino)
{
	struct buffer_head		*bp;
	struct inode			*inode;
	u_long				block, offset;

	inode = new_inode(sbp);
	if (!inode)
		return NULL;
	inode->i_ino = get_next_ino();

	block = extent + ((ino * VXFS_ISIZE) / sbp->s_blocksize);
	offset = ((ino % (sbp->s_blocksize / VXFS_ISIZE)) * VXFS_ISIZE);
	bp = sb_bread(sbp, block);

	if (bp && buffer_mapped(bp)) {
		struct vxfs_inode_info	*vip = VXFS_INO(inode);
		struct vxfs_dinode	*dip;

		dip = (struct vxfs_dinode *)(bp->b_data + offset);
		dip2vip_cpy(VXFS_SBI(sbp), vip, dip);
		vip->vfs_inode.i_mapping->a_ops = &vxfs_aops;
#ifdef DIAGNOSTIC
		vxfs_dumpi(vip, ino);
#endif
		brelse(bp);
		return inode;
	}

	printk(KERN_WARNING "vxfs: unable to read block %ld\n", block);
	brelse(bp);
	iput(inode);
	return NULL;
}

/**
 * __vxfs_iget - generic find inode facility
 * @ilistp:		inode list
 * @vip:		VxFS inode to fill in
 * @ino:		inode number
 *
 * Description:
 *  Search the for inode number @ino in the filesystem
 *  described by @sbp.  Use the specified inode table (@ilistp).
 *  Returns the matching inode on success, else an error code.
 */
static int
__vxfs_iget(struct inode *ilistp, struct vxfs_inode_info *vip, ino_t ino)
{
	struct page			*pp;
	u_long				offset;

	offset = (ino % (PAGE_SIZE / VXFS_ISIZE)) * VXFS_ISIZE;
	pp = vxfs_get_page(ilistp->i_mapping, ino * VXFS_ISIZE / PAGE_SIZE);

	if (!IS_ERR(pp)) {
		struct vxfs_dinode	*dip;
		caddr_t			kaddr = (char *)page_address(pp);

		dip = (struct vxfs_dinode *)(kaddr + offset);
		dip2vip_cpy(VXFS_SBI(ilistp->i_sb), vip, dip);
		vip->vfs_inode.i_mapping->a_ops = &vxfs_aops;
#ifdef DIAGNOSTIC
		vxfs_dumpi(vip, ino);
#endif
		vxfs_put_page(pp);
		return 0;
	}

	printk(KERN_WARNING "vxfs: error on page 0x%p for inode %ld\n",
		pp, (unsigned long)ino);
	return PTR_ERR(pp);
}

/**
 * vxfs_stiget - find inode using the structural inode list
 * @sbp:	VFS superblock
 * @ino:	inode #
 *
 * Description:
 *  Find inode @ino in the filesystem described by @sbp using
 *  the structural inode list.
 *  Returns the matching inode on success, else a NULL pointer.
 */
struct inode *
vxfs_stiget(struct super_block *sbp, ino_t ino)
{
	struct inode *inode;
	int error;

	inode = new_inode(sbp);
	if (!inode)
		return NULL;
	inode->i_ino = get_next_ino();

	error = __vxfs_iget(VXFS_SBI(sbp)->vsi_stilist, VXFS_INO(inode), ino);
	if (error) {
		iput(inode);
		return NULL;
	}

	return inode;
}

/**
 * vxfs_iget - get an inode
 * @sbp:	the superblock to get the inode for
 * @ino:	the number of the inode to get
 *
 * Description:
 *  vxfs_read_inode creates an inode, reads the disk inode for @ino and fills
 *  in all relevant fields in the new inode.
 */
struct inode *
vxfs_iget(struct super_block *sbp, ino_t ino)
{
	struct vxfs_inode_info		*vip;
	const struct address_space_operations	*aops;
	struct inode *ip;
	int error;

	ip = iget_locked(sbp, ino);
	if (!ip)
		return ERR_PTR(-ENOMEM);
	if (!(ip->i_state & I_NEW))
		return ip;

	vip = VXFS_INO(ip);
	error = __vxfs_iget(VXFS_SBI(sbp)->vsi_ilist, vip, ino);
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
		ip->i_op = &vxfs_dir_inode_ops;
		ip->i_fop = &vxfs_dir_operations;
		ip->i_mapping->a_ops = aops;
	} else if (S_ISLNK(ip->i_mode)) {
		if (!VXFS_ISIMMED(vip)) {
			ip->i_op = &page_symlink_inode_operations;
			inode_nohighmem(ip);
			ip->i_mapping->a_ops = &vxfs_aops;
		} else {
			ip->i_op = &simple_symlink_inode_operations;
			ip->i_link = vip->vii_immed.vi_immed;
			nd_terminate_link(ip->i_link, ip->i_size,
					  sizeof(vip->vii_immed.vi_immed) - 1);
		}
	} else
		init_special_inode(ip, ip->i_mode, old_decode_dev(vip->vii_rdev));

	unlock_new_inode(ip);
	return ip;
}

/**
 * vxfs_evict_inode - remove inode from main memory
 * @ip:		inode to discard.
 *
 * Description:
 *  vxfs_evict_inode() is called on the final iput and frees the private
 *  inode area.
 */
void
vxfs_evict_inode(struct inode *ip)
{
	truncate_inode_pages_final(&ip->i_data);
	clear_inode(ip);
}
