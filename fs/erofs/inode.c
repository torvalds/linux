// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "xattr.h"
#include <trace/events/erofs.h>

static int erofs_fill_symlink(struct inode *inode, void *kaddr,
			      unsigned int m_pofs)
{
	struct erofs_inode *vi = EROFS_I(inode);
	loff_t off;

	m_pofs += vi->xattr_isize;
	/* check if it cannot be handled with fast symlink scheme */
	if (vi->datalayout != EROFS_INODE_FLAT_INLINE ||
	    check_add_overflow(m_pofs, inode->i_size, &off) ||
	    off > i_blocksize(inode))
		return 0;

	inode->i_link = kmemdup_nul(kaddr + m_pofs, inode->i_size, GFP_KERNEL);
	return inode->i_link ? 0 : -ENOMEM;
}

static int erofs_read_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	erofs_blk_t blkaddr = erofs_blknr(sb, erofs_iloc(inode));
	unsigned int ofs = erofs_blkoff(sb, erofs_iloc(inode));
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	erofs_blk_t addrmask = BIT_ULL(48) - 1;
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_inode_extended *die, copied;
	struct erofs_inode_compact *dic;
	unsigned int ifmt;
	void *ptr;
	int err = 0;

	ptr = erofs_read_metabuf(&buf, sb, erofs_pos(sb, blkaddr), true);
	if (IS_ERR(ptr)) {
		err = PTR_ERR(ptr);
		erofs_err(sb, "failed to get inode (nid: %llu) page, err %d",
			  vi->nid, err);
		goto err_out;
	}

	dic = ptr + ofs;
	ifmt = le16_to_cpu(dic->i_format);
	if (ifmt & ~EROFS_I_ALL) {
		erofs_err(sb, "unsupported i_format %u of nid %llu",
			  ifmt, vi->nid);
		err = -EOPNOTSUPP;
		goto err_out;
	}

	vi->datalayout = erofs_inode_datalayout(ifmt);
	if (vi->datalayout >= EROFS_INODE_DATALAYOUT_MAX) {
		erofs_err(sb, "unsupported datalayout %u of nid %llu",
			  vi->datalayout, vi->nid);
		err = -EOPNOTSUPP;
		goto err_out;
	}

	switch (erofs_inode_version(ifmt)) {
	case EROFS_INODE_LAYOUT_EXTENDED:
		vi->inode_isize = sizeof(struct erofs_inode_extended);
		/* check if the extended inode acrosses block boundary */
		if (ofs + vi->inode_isize <= sb->s_blocksize) {
			ofs += vi->inode_isize;
			die = (struct erofs_inode_extended *)dic;
			copied.i_u = die->i_u;
			copied.i_nb = die->i_nb;
		} else {
			const unsigned int gotten = sb->s_blocksize - ofs;

			memcpy(&copied, dic, gotten);
			ptr = erofs_read_metabuf(&buf, sb,
					erofs_pos(sb, blkaddr + 1), true);
			if (IS_ERR(ptr)) {
				err = PTR_ERR(ptr);
				erofs_err(sb, "failed to get inode payload block (nid: %llu), err %d",
					  vi->nid, err);
				goto err_out;
			}
			ofs = vi->inode_isize - gotten;
			memcpy((u8 *)&copied + gotten, ptr, ofs);
			die = &copied;
		}
		vi->xattr_isize = erofs_xattr_ibody_size(die->i_xattr_icount);

		inode->i_mode = le16_to_cpu(die->i_mode);
		i_uid_write(inode, le32_to_cpu(die->i_uid));
		i_gid_write(inode, le32_to_cpu(die->i_gid));
		set_nlink(inode, le32_to_cpu(die->i_nlink));
		inode_set_mtime(inode, le64_to_cpu(die->i_mtime),
				le32_to_cpu(die->i_mtime_nsec));

		inode->i_size = le64_to_cpu(die->i_size);
		break;
	case EROFS_INODE_LAYOUT_COMPACT:
		vi->inode_isize = sizeof(struct erofs_inode_compact);
		ofs += vi->inode_isize;
		vi->xattr_isize = erofs_xattr_ibody_size(dic->i_xattr_icount);

		inode->i_mode = le16_to_cpu(dic->i_mode);
		copied.i_u = dic->i_u;
		i_uid_write(inode, le16_to_cpu(dic->i_uid));
		i_gid_write(inode, le16_to_cpu(dic->i_gid));
		if (!S_ISDIR(inode->i_mode) &&
		    ((ifmt >> EROFS_I_NLINK_1_BIT) & 1)) {
			set_nlink(inode, 1);
			copied.i_nb = dic->i_nb;
		} else {
			set_nlink(inode, le16_to_cpu(dic->i_nb.nlink));
			copied.i_nb.startblk_hi = 0;
			addrmask = BIT_ULL(32) - 1;
		}
		inode_set_mtime(inode, sbi->epoch + le32_to_cpu(dic->i_mtime),
				sbi->fixed_nsec);

		inode->i_size = le32_to_cpu(dic->i_size);
		break;
	default:
		erofs_err(sb, "unsupported on-disk inode version %u of nid %llu",
			  erofs_inode_version(ifmt), vi->nid);
		err = -EOPNOTSUPP;
		goto err_out;
	}

	if (unlikely(inode->i_size < 0)) {
		erofs_err(sb, "negative i_size @ nid %llu", vi->nid);
		err = -EFSCORRUPTED;
		goto err_out;
	}
	switch (inode->i_mode & S_IFMT) {
	case S_IFDIR:
		vi->dot_omitted = (ifmt >> EROFS_I_DOT_OMITTED_BIT) & 1;
		fallthrough;
	case S_IFREG:
	case S_IFLNK:
		vi->startblk = le32_to_cpu(copied.i_u.startblk_lo) |
			((u64)le16_to_cpu(copied.i_nb.startblk_hi) << 32);
		if (vi->datalayout == EROFS_INODE_FLAT_PLAIN &&
		    !((vi->startblk ^ EROFS_NULL_ADDR) & addrmask))
			vi->startblk = EROFS_NULL_ADDR;

		if(S_ISLNK(inode->i_mode)) {
			err = erofs_fill_symlink(inode, ptr, ofs);
			if (err)
				goto err_out;
		}
		break;
	case S_IFCHR:
	case S_IFBLK:
		inode->i_rdev = new_decode_dev(le32_to_cpu(copied.i_u.rdev));
		break;
	case S_IFIFO:
	case S_IFSOCK:
		inode->i_rdev = 0;
		break;
	default:
		erofs_err(sb, "bogus i_mode (%o) @ nid %llu", inode->i_mode,
			  vi->nid);
		err = -EFSCORRUPTED;
		goto err_out;
	}

	if (erofs_inode_is_data_compressed(vi->datalayout))
		inode->i_blocks = le32_to_cpu(copied.i_u.blocks_lo) <<
					(sb->s_blocksize_bits - 9);
	else
		inode->i_blocks = round_up(inode->i_size, sb->s_blocksize) >> 9;

	if (vi->datalayout == EROFS_INODE_CHUNK_BASED) {
		/* fill chunked inode summary info */
		vi->chunkformat = le16_to_cpu(copied.i_u.c.format);
		if (vi->chunkformat & ~EROFS_CHUNK_FORMAT_ALL) {
			erofs_err(sb, "unsupported chunk format %x of nid %llu",
				  vi->chunkformat, vi->nid);
			err = -EOPNOTSUPP;
			goto err_out;
		}
		vi->chunkbits = sb->s_blocksize_bits +
			(vi->chunkformat & EROFS_CHUNK_FORMAT_BLKBITS_MASK);
	}
	inode_set_atime_to_ts(inode,
			      inode_set_ctime_to_ts(inode, inode_get_mtime(inode)));

	inode->i_flags &= ~S_DAX;
	if (test_opt(&sbi->opt, DAX_ALWAYS) && S_ISREG(inode->i_mode) &&
	    (vi->datalayout == EROFS_INODE_FLAT_PLAIN ||
	     vi->datalayout == EROFS_INODE_CHUNK_BASED))
		inode->i_flags |= S_DAX;
err_out:
	erofs_put_metabuf(&buf);
	return err;
}

static int erofs_fill_inode(struct inode *inode)
{
	struct erofs_inode *vi = EROFS_I(inode);
	int err;

	trace_erofs_fill_inode(inode);
	err = erofs_read_inode(inode);
	if (err)
		return err;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &erofs_generic_iops;
		if (erofs_inode_is_data_compressed(vi->datalayout))
			inode->i_fop = &generic_ro_fops;
		else
			inode->i_fop = &erofs_file_fops;
		break;
	case S_IFDIR:
		inode->i_op = &erofs_dir_iops;
		inode->i_fop = &erofs_dir_fops;
		inode_nohighmem(inode);
		break;
	case S_IFLNK:
		if (inode->i_link)
			inode->i_op = &erofs_fast_symlink_iops;
		else
			inode->i_op = &erofs_symlink_iops;
		inode_nohighmem(inode);
		break;
	default:
		inode->i_op = &erofs_generic_iops;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		return 0;
	}

	mapping_set_large_folios(inode->i_mapping);
	if (erofs_inode_is_data_compressed(vi->datalayout)) {
#ifdef CONFIG_EROFS_FS_ZIP
		DO_ONCE_LITE_IF(inode->i_blkbits != PAGE_SHIFT,
			  erofs_info, inode->i_sb,
			  "EXPERIMENTAL EROFS subpage compressed block support in use. Use at your own risk!");
		inode->i_mapping->a_ops = &z_erofs_aops;
#else
		err = -EOPNOTSUPP;
#endif
	} else {
		inode->i_mapping->a_ops = &erofs_aops;
#ifdef CONFIG_EROFS_FS_ONDEMAND
		if (erofs_is_fscache_mode(inode->i_sb))
			inode->i_mapping->a_ops = &erofs_fscache_access_aops;
#endif
#ifdef CONFIG_EROFS_FS_BACKED_BY_FILE
		if (erofs_is_fileio_mode(EROFS_SB(inode->i_sb)))
			inode->i_mapping->a_ops = &erofs_fileio_aops;
#endif
	}

	return err;
}

/*
 * ino_t is 32-bits on 32-bit arch. We have to squash the 64-bit value down
 * so that it will fit.
 */
static ino_t erofs_squash_ino(erofs_nid_t nid)
{
	ino_t ino = (ino_t)nid;

	if (sizeof(ino_t) < sizeof(erofs_nid_t))
		ino ^= nid >> (sizeof(erofs_nid_t) - sizeof(ino_t)) * 8;
	return ino;
}

static int erofs_iget5_eq(struct inode *inode, void *opaque)
{
	return EROFS_I(inode)->nid == *(erofs_nid_t *)opaque;
}

static int erofs_iget5_set(struct inode *inode, void *opaque)
{
	const erofs_nid_t nid = *(erofs_nid_t *)opaque;

	inode->i_ino = erofs_squash_ino(nid);
	EROFS_I(inode)->nid = nid;
	return 0;
}

struct inode *erofs_iget(struct super_block *sb, erofs_nid_t nid)
{
	struct inode *inode;

	inode = iget5_locked(sb, erofs_squash_ino(nid), erofs_iget5_eq,
			     erofs_iget5_set, &nid);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		int err = erofs_fill_inode(inode);

		if (err) {
			iget_failed(inode);
			return ERR_PTR(err);
		}
		unlock_new_inode(inode);
	}
	return inode;
}

int erofs_getattr(struct mnt_idmap *idmap, const struct path *path,
		  struct kstat *stat, u32 request_mask,
		  unsigned int query_flags)
{
	struct inode *const inode = d_inode(path->dentry);
	struct block_device *bdev = inode->i_sb->s_bdev;
	bool compressed =
		erofs_inode_is_data_compressed(EROFS_I(inode)->datalayout);

	if (compressed)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	stat->attributes |= STATX_ATTR_IMMUTABLE;
	stat->attributes_mask |= (STATX_ATTR_COMPRESSED |
				  STATX_ATTR_IMMUTABLE);

	/*
	 * Return the DIO alignment restrictions if requested.
	 *
	 * In EROFS, STATX_DIOALIGN is only supported in bdev-based mode
	 * and uncompressed inodes, otherwise we report no DIO support.
	 */
	if ((request_mask & STATX_DIOALIGN) && S_ISREG(inode->i_mode)) {
		stat->result_mask |= STATX_DIOALIGN;
		if (bdev && !compressed) {
			stat->dio_mem_align = bdev_dma_alignment(bdev) + 1;
			stat->dio_offset_align = bdev_logical_block_size(bdev);
		}
	}
	generic_fillattr(idmap, request_mask, inode, stat);
	return 0;
}

const struct inode_operations erofs_generic_iops = {
	.getattr = erofs_getattr,
	.listxattr = erofs_listxattr,
	.get_inode_acl = erofs_get_acl,
	.fiemap = erofs_fiemap,
};

const struct inode_operations erofs_symlink_iops = {
	.get_link = page_get_link,
	.getattr = erofs_getattr,
	.listxattr = erofs_listxattr,
	.get_inode_acl = erofs_get_acl,
};

const struct inode_operations erofs_fast_symlink_iops = {
	.get_link = simple_get_link,
	.getattr = erofs_getattr,
	.listxattr = erofs_listxattr,
	.get_inode_acl = erofs_get_acl,
};
