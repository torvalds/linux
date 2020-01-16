// SPDX-License-Identifier: GPL-2.0-only
/*
 * iyesde.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang,
 *              and from work (c) 1998 Mike Shaver.
 */

#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/fs.h>
#include "efs.h"
#include <linux/efs_fs_sb.h>

static int efs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,efs_get_block);
}
static sector_t _efs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,efs_get_block);
}
static const struct address_space_operations efs_aops = {
	.readpage = efs_readpage,
	.bmap = _efs_bmap
};

static inline void extent_copy(efs_extent *src, efs_extent *dst) {
	/*
	 * this is slightly evil. it doesn't just copy
	 * efs_extent from src to dst, it also mangles
	 * the bits so that dst ends up in cpu byte-order.
	 */

	dst->cooked.ex_magic  =  (unsigned int) src->raw[0];
	dst->cooked.ex_bn     = ((unsigned int) src->raw[1] << 16) |
				((unsigned int) src->raw[2] <<  8) |
				((unsigned int) src->raw[3] <<  0);
	dst->cooked.ex_length =  (unsigned int) src->raw[4];
	dst->cooked.ex_offset = ((unsigned int) src->raw[5] << 16) |
				((unsigned int) src->raw[6] <<  8) |
				((unsigned int) src->raw[7] <<  0);
	return;
}

struct iyesde *efs_iget(struct super_block *super, unsigned long iyes)
{
	int i, iyesde_index;
	dev_t device;
	u32 rdev;
	struct buffer_head *bh;
	struct efs_sb_info    *sb = SUPER_INFO(super);
	struct efs_iyesde_info *in;
	efs_block_t block, offset;
	struct efs_diyesde *efs_iyesde;
	struct iyesde *iyesde;

	iyesde = iget_locked(super, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	in = INODE_INFO(iyesde);

	/*
	** EFS layout:
	**
	** |   cylinder group    |   cylinder group    |   cylinder group ..etc
	** |iyesdes|data          |iyesdes|data          |iyesdes|data       ..etc
	**
	** work out the iyesde block index, (considering initially that the
	** iyesdes are stored as consecutive blocks). then work out the block
	** number of that iyesde given the above layout, and finally the
	** offset of the iyesde within that block.
	*/

	iyesde_index = iyesde->i_iyes /
		(EFS_BLOCKSIZE / sizeof(struct efs_diyesde));

	block = sb->fs_start + sb->first_block + 
		(sb->group_size * (iyesde_index / sb->iyesde_blocks)) +
		(iyesde_index % sb->iyesde_blocks);

	offset = (iyesde->i_iyes %
			(EFS_BLOCKSIZE / sizeof(struct efs_diyesde))) *
		sizeof(struct efs_diyesde);

	bh = sb_bread(iyesde->i_sb, block);
	if (!bh) {
		pr_warn("%s() failed at block %d\n", __func__, block);
		goto read_iyesde_error;
	}

	efs_iyesde = (struct efs_diyesde *) (bh->b_data + offset);
    
	iyesde->i_mode  = be16_to_cpu(efs_iyesde->di_mode);
	set_nlink(iyesde, be16_to_cpu(efs_iyesde->di_nlink));
	i_uid_write(iyesde, (uid_t)be16_to_cpu(efs_iyesde->di_uid));
	i_gid_write(iyesde, (gid_t)be16_to_cpu(efs_iyesde->di_gid));
	iyesde->i_size  = be32_to_cpu(efs_iyesde->di_size);
	iyesde->i_atime.tv_sec = be32_to_cpu(efs_iyesde->di_atime);
	iyesde->i_mtime.tv_sec = be32_to_cpu(efs_iyesde->di_mtime);
	iyesde->i_ctime.tv_sec = be32_to_cpu(efs_iyesde->di_ctime);
	iyesde->i_atime.tv_nsec = iyesde->i_mtime.tv_nsec = iyesde->i_ctime.tv_nsec = 0;

	/* this is the number of blocks in the file */
	if (iyesde->i_size == 0) {
		iyesde->i_blocks = 0;
	} else {
		iyesde->i_blocks = ((iyesde->i_size - 1) >> EFS_BLOCKSIZE_BITS) + 1;
	}

	rdev = be16_to_cpu(efs_iyesde->di_u.di_dev.odev);
	if (rdev == 0xffff) {
		rdev = be32_to_cpu(efs_iyesde->di_u.di_dev.ndev);
		if (sysv_major(rdev) > 0xfff)
			device = 0;
		else
			device = MKDEV(sysv_major(rdev), sysv_miyesr(rdev));
	} else
		device = old_decode_dev(rdev);

	/* get the number of extents for this object */
	in->numextents = be16_to_cpu(efs_iyesde->di_numextents);
	in->lastextent = 0;

	/* copy the extents contained within the iyesde to memory */
	for(i = 0; i < EFS_DIRECTEXTENTS; i++) {
		extent_copy(&(efs_iyesde->di_u.di_extents[i]), &(in->extents[i]));
		if (i < in->numextents && in->extents[i].cooked.ex_magic != 0) {
			pr_warn("extent %d has bad magic number in iyesde %lu\n",
				i, iyesde->i_iyes);
			brelse(bh);
			goto read_iyesde_error;
		}
	}

	brelse(bh);
	pr_debug("efs_iget(): iyesde %lu, extents %d, mode %o\n",
		 iyesde->i_iyes, in->numextents, iyesde->i_mode);
	switch (iyesde->i_mode & S_IFMT) {
		case S_IFDIR: 
			iyesde->i_op = &efs_dir_iyesde_operations; 
			iyesde->i_fop = &efs_dir_operations; 
			break;
		case S_IFREG:
			iyesde->i_fop = &generic_ro_fops;
			iyesde->i_data.a_ops = &efs_aops;
			break;
		case S_IFLNK:
			iyesde->i_op = &page_symlink_iyesde_operations;
			iyesde_yeshighmem(iyesde);
			iyesde->i_data.a_ops = &efs_symlink_aops;
			break;
		case S_IFCHR:
		case S_IFBLK:
		case S_IFIFO:
			init_special_iyesde(iyesde, iyesde->i_mode, device);
			break;
		default:
			pr_warn("unsupported iyesde mode %o\n", iyesde->i_mode);
			goto read_iyesde_error;
			break;
	}

	unlock_new_iyesde(iyesde);
	return iyesde;
        
read_iyesde_error:
	pr_warn("failed to read iyesde %lu\n", iyesde->i_iyes);
	iget_failed(iyesde);
	return ERR_PTR(-EIO);
}

static inline efs_block_t
efs_extent_check(efs_extent *ptr, efs_block_t block, struct efs_sb_info *sb) {
	efs_block_t start;
	efs_block_t length;
	efs_block_t offset;

	/*
	 * given an extent and a logical block within a file,
	 * can this block be found within this extent ?
	 */
	start  = ptr->cooked.ex_bn;
	length = ptr->cooked.ex_length;
	offset = ptr->cooked.ex_offset;

	if ((block >= offset) && (block < offset+length)) {
		return(sb->fs_start + start + block - offset);
	} else {
		return 0;
	}
}

efs_block_t efs_map_block(struct iyesde *iyesde, efs_block_t block) {
	struct efs_sb_info    *sb = SUPER_INFO(iyesde->i_sb);
	struct efs_iyesde_info *in = INODE_INFO(iyesde);
	struct buffer_head    *bh = NULL;

	int cur, last, first = 1;
	int ibase, ioffset, dirext, direxts, indext, indexts;
	efs_block_t iblock, result = 0, lastblock = 0;
	efs_extent ext, *exts;

	last = in->lastextent;

	if (in->numextents <= EFS_DIRECTEXTENTS) {
		/* first check the last extent we returned */
		if ((result = efs_extent_check(&in->extents[last], block, sb)))
			return result;
    
		/* if we only have one extent then yesthing can be found */
		if (in->numextents == 1) {
			pr_err("%s() failed to map (1 extent)\n", __func__);
			return 0;
		}

		direxts = in->numextents;

		/*
		 * check the stored extents in the iyesde
		 * start with next extent and check forwards
		 */
		for(dirext = 1; dirext < direxts; dirext++) {
			cur = (last + dirext) % in->numextents;
			if ((result = efs_extent_check(&in->extents[cur], block, sb))) {
				in->lastextent = cur;
				return result;
			}
		}

		pr_err("%s() failed to map block %u (dir)\n", __func__, block);
		return 0;
	}

	pr_debug("%s(): indirect search for logical block %u\n",
		 __func__, block);
	direxts = in->extents[0].cooked.ex_offset;
	indexts = in->numextents;

	for(indext = 0; indext < indexts; indext++) {
		cur = (last + indext) % indexts;

		/*
		 * work out which direct extent contains `cur'.
		 *
		 * also compute ibase: i.e. the number of the first
		 * indirect extent contained within direct extent `cur'.
		 *
		 */
		ibase = 0;
		for(dirext = 0; cur < ibase && dirext < direxts; dirext++) {
			ibase += in->extents[dirext].cooked.ex_length *
				(EFS_BLOCKSIZE / sizeof(efs_extent));
		}

		if (dirext == direxts) {
			/* should never happen */
			pr_err("couldn't find direct extent for indirect extent %d (block %u)\n",
			       cur, block);
			if (bh) brelse(bh);
			return 0;
		}
		
		/* work out block number and offset of this indirect extent */
		iblock = sb->fs_start + in->extents[dirext].cooked.ex_bn +
			(cur - ibase) /
			(EFS_BLOCKSIZE / sizeof(efs_extent));
		ioffset = (cur - ibase) %
			(EFS_BLOCKSIZE / sizeof(efs_extent));

		if (first || lastblock != iblock) {
			if (bh) brelse(bh);

			bh = sb_bread(iyesde->i_sb, iblock);
			if (!bh) {
				pr_err("%s() failed at block %d\n",
				       __func__, iblock);
				return 0;
			}
			pr_debug("%s(): read indirect extent block %d\n",
				 __func__, iblock);
			first = 0;
			lastblock = iblock;
		}

		exts = (efs_extent *) bh->b_data;

		extent_copy(&(exts[ioffset]), &ext);

		if (ext.cooked.ex_magic != 0) {
			pr_err("extent %d has bad magic number in block %d\n",
			       cur, iblock);
			if (bh) brelse(bh);
			return 0;
		}

		if ((result = efs_extent_check(&ext, block, sb))) {
			if (bh) brelse(bh);
			in->lastextent = cur;
			return result;
		}
	}
	if (bh) brelse(bh);
	pr_err("%s() failed to map block %u (indir)\n", __func__, block);
	return 0;
}  

MODULE_LICENSE("GPL");
