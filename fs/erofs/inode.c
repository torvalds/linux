// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#include "xattr.h"

#include <trace/events/erofs.h>

/* yes locking */
static int erofs_read_iyesde(struct iyesde *iyesde, void *data)
{
	struct erofs_iyesde *vi = EROFS_I(iyesde);
	struct erofs_iyesde_compact *dic = data;
	struct erofs_iyesde_extended *die;

	const unsigned int ifmt = le16_to_cpu(dic->i_format);
	struct erofs_sb_info *sbi = EROFS_SB(iyesde->i_sb);
	erofs_blk_t nblks = 0;

	vi->datalayout = erofs_iyesde_datalayout(ifmt);

	if (vi->datalayout >= EROFS_INODE_DATALAYOUT_MAX) {
		erofs_err(iyesde->i_sb, "unsupported datalayout %u of nid %llu",
			  vi->datalayout, vi->nid);
		DBG_BUGON(1);
		return -EOPNOTSUPP;
	}

	switch (erofs_iyesde_version(ifmt)) {
	case EROFS_INODE_LAYOUT_EXTENDED:
		die = data;

		vi->iyesde_isize = sizeof(struct erofs_iyesde_extended);
		vi->xattr_isize = erofs_xattr_ibody_size(die->i_xattr_icount);

		iyesde->i_mode = le16_to_cpu(die->i_mode);
		switch (iyesde->i_mode & S_IFMT) {
		case S_IFREG:
		case S_IFDIR:
		case S_IFLNK:
			vi->raw_blkaddr = le32_to_cpu(die->i_u.raw_blkaddr);
			break;
		case S_IFCHR:
		case S_IFBLK:
			iyesde->i_rdev =
				new_decode_dev(le32_to_cpu(die->i_u.rdev));
			break;
		case S_IFIFO:
		case S_IFSOCK:
			iyesde->i_rdev = 0;
			break;
		default:
			goto bogusimode;
		}
		i_uid_write(iyesde, le32_to_cpu(die->i_uid));
		i_gid_write(iyesde, le32_to_cpu(die->i_gid));
		set_nlink(iyesde, le32_to_cpu(die->i_nlink));

		/* ns timestamp */
		iyesde->i_mtime.tv_sec = iyesde->i_ctime.tv_sec =
			le64_to_cpu(die->i_ctime);
		iyesde->i_mtime.tv_nsec = iyesde->i_ctime.tv_nsec =
			le32_to_cpu(die->i_ctime_nsec);

		iyesde->i_size = le64_to_cpu(die->i_size);

		/* total blocks for compressed files */
		if (erofs_iyesde_is_data_compressed(vi->datalayout))
			nblks = le32_to_cpu(die->i_u.compressed_blocks);
		break;
	case EROFS_INODE_LAYOUT_COMPACT:
		vi->iyesde_isize = sizeof(struct erofs_iyesde_compact);
		vi->xattr_isize = erofs_xattr_ibody_size(dic->i_xattr_icount);

		iyesde->i_mode = le16_to_cpu(dic->i_mode);
		switch (iyesde->i_mode & S_IFMT) {
		case S_IFREG:
		case S_IFDIR:
		case S_IFLNK:
			vi->raw_blkaddr = le32_to_cpu(dic->i_u.raw_blkaddr);
			break;
		case S_IFCHR:
		case S_IFBLK:
			iyesde->i_rdev =
				new_decode_dev(le32_to_cpu(dic->i_u.rdev));
			break;
		case S_IFIFO:
		case S_IFSOCK:
			iyesde->i_rdev = 0;
			break;
		default:
			goto bogusimode;
		}
		i_uid_write(iyesde, le16_to_cpu(dic->i_uid));
		i_gid_write(iyesde, le16_to_cpu(dic->i_gid));
		set_nlink(iyesde, le16_to_cpu(dic->i_nlink));

		/* use build time to derive all file time */
		iyesde->i_mtime.tv_sec = iyesde->i_ctime.tv_sec =
			sbi->build_time;
		iyesde->i_mtime.tv_nsec = iyesde->i_ctime.tv_nsec =
			sbi->build_time_nsec;

		iyesde->i_size = le32_to_cpu(dic->i_size);
		if (erofs_iyesde_is_data_compressed(vi->datalayout))
			nblks = le32_to_cpu(dic->i_u.compressed_blocks);
		break;
	default:
		erofs_err(iyesde->i_sb,
			  "unsupported on-disk iyesde version %u of nid %llu",
			  erofs_iyesde_version(ifmt), vi->nid);
		DBG_BUGON(1);
		return -EOPNOTSUPP;
	}

	if (!nblks)
		/* measure iyesde.i_blocks as generic filesystems */
		iyesde->i_blocks = roundup(iyesde->i_size, EROFS_BLKSIZ) >> 9;
	else
		iyesde->i_blocks = nblks << LOG_SECTORS_PER_BLOCK;
	return 0;

bogusimode:
	erofs_err(iyesde->i_sb, "bogus i_mode (%o) @ nid %llu",
		  iyesde->i_mode, vi->nid);
	DBG_BUGON(1);
	return -EFSCORRUPTED;
}

static int erofs_fill_symlink(struct iyesde *iyesde, void *data,
			      unsigned int m_pofs)
{
	struct erofs_iyesde *vi = EROFS_I(iyesde);
	char *lnk;

	/* if it canyest be handled with fast symlink scheme */
	if (vi->datalayout != EROFS_INODE_FLAT_INLINE ||
	    iyesde->i_size >= PAGE_SIZE) {
		iyesde->i_op = &erofs_symlink_iops;
		return 0;
	}

	lnk = kmalloc(iyesde->i_size + 1, GFP_KERNEL);
	if (!lnk)
		return -ENOMEM;

	m_pofs += vi->iyesde_isize + vi->xattr_isize;
	/* inline symlink data shouldn't cross page boundary as well */
	if (m_pofs + iyesde->i_size > PAGE_SIZE) {
		kfree(lnk);
		erofs_err(iyesde->i_sb,
			  "inline data cross block boundary @ nid %llu",
			  vi->nid);
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}

	memcpy(lnk, data + m_pofs, iyesde->i_size);
	lnk[iyesde->i_size] = '\0';

	iyesde->i_link = lnk;
	iyesde->i_op = &erofs_fast_symlink_iops;
	return 0;
}

static int erofs_fill_iyesde(struct iyesde *iyesde, int isdir)
{
	struct super_block *sb = iyesde->i_sb;
	struct erofs_iyesde *vi = EROFS_I(iyesde);
	struct page *page;
	void *data;
	int err;
	erofs_blk_t blkaddr;
	unsigned int ofs;
	erofs_off_t iyesde_loc;

	trace_erofs_fill_iyesde(iyesde, isdir);
	iyesde_loc = iloc(EROFS_SB(sb), vi->nid);
	blkaddr = erofs_blknr(iyesde_loc);
	ofs = erofs_blkoff(iyesde_loc);

	erofs_dbg("%s, reading iyesde nid %llu at %u of blkaddr %u",
		  __func__, vi->nid, ofs, blkaddr);

	page = erofs_get_meta_page(sb, blkaddr);

	if (IS_ERR(page)) {
		erofs_err(sb, "failed to get iyesde (nid: %llu) page, err %ld",
			  vi->nid, PTR_ERR(page));
		return PTR_ERR(page);
	}

	DBG_BUGON(!PageUptodate(page));
	data = page_address(page);

	err = erofs_read_iyesde(iyesde, data + ofs);
	if (err)
		goto out_unlock;

	/* setup the new iyesde */
	switch (iyesde->i_mode & S_IFMT) {
	case S_IFREG:
		iyesde->i_op = &erofs_generic_iops;
		iyesde->i_fop = &generic_ro_fops;
		break;
	case S_IFDIR:
		iyesde->i_op = &erofs_dir_iops;
		iyesde->i_fop = &erofs_dir_fops;
		break;
	case S_IFLNK:
		err = erofs_fill_symlink(iyesde, data, ofs);
		if (err)
			goto out_unlock;
		iyesde_yeshighmem(iyesde);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		iyesde->i_op = &erofs_generic_iops;
		init_special_iyesde(iyesde, iyesde->i_mode, iyesde->i_rdev);
		goto out_unlock;
	default:
		err = -EFSCORRUPTED;
		goto out_unlock;
	}

	if (erofs_iyesde_is_data_compressed(vi->datalayout)) {
		err = z_erofs_fill_iyesde(iyesde);
		goto out_unlock;
	}
	iyesde->i_mapping->a_ops = &erofs_raw_access_aops;

out_unlock:
	unlock_page(page);
	put_page(page);
	return err;
}

/*
 * erofs nid is 64bits, but i_iyes is 'unsigned long', therefore
 * we should do more for 32-bit platform to find the right iyesde.
 */
static int erofs_ilookup_test_actor(struct iyesde *iyesde, void *opaque)
{
	const erofs_nid_t nid = *(erofs_nid_t *)opaque;

	return EROFS_I(iyesde)->nid == nid;
}

static int erofs_iget_set_actor(struct iyesde *iyesde, void *opaque)
{
	const erofs_nid_t nid = *(erofs_nid_t *)opaque;

	iyesde->i_iyes = erofs_iyesde_hash(nid);
	return 0;
}

static inline struct iyesde *erofs_iget_locked(struct super_block *sb,
					      erofs_nid_t nid)
{
	const unsigned long hashval = erofs_iyesde_hash(nid);

	return iget5_locked(sb, hashval, erofs_ilookup_test_actor,
		erofs_iget_set_actor, &nid);
}

struct iyesde *erofs_iget(struct super_block *sb,
			 erofs_nid_t nid,
			 bool isdir)
{
	struct iyesde *iyesde = erofs_iget_locked(sb, nid);

	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	if (iyesde->i_state & I_NEW) {
		int err;
		struct erofs_iyesde *vi = EROFS_I(iyesde);

		vi->nid = nid;

		err = erofs_fill_iyesde(iyesde, isdir);
		if (!err)
			unlock_new_iyesde(iyesde);
		else {
			iget_failed(iyesde);
			iyesde = ERR_PTR(err);
		}
	}
	return iyesde;
}

int erofs_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int query_flags)
{
	struct iyesde *const iyesde = d_iyesde(path->dentry);

	if (erofs_iyesde_is_data_compressed(EROFS_I(iyesde)->datalayout))
		stat->attributes |= STATX_ATTR_COMPRESSED;

	stat->attributes |= STATX_ATTR_IMMUTABLE;
	stat->attributes_mask |= (STATX_ATTR_COMPRESSED |
				  STATX_ATTR_IMMUTABLE);

	generic_fillattr(iyesde, stat);
	return 0;
}

const struct iyesde_operations erofs_generic_iops = {
	.getattr = erofs_getattr,
#ifdef CONFIG_EROFS_FS_XATTR
	.listxattr = erofs_listxattr,
#endif
	.get_acl = erofs_get_acl,
};

const struct iyesde_operations erofs_symlink_iops = {
	.get_link = page_get_link,
	.getattr = erofs_getattr,
#ifdef CONFIG_EROFS_FS_XATTR
	.listxattr = erofs_listxattr,
#endif
	.get_acl = erofs_get_acl,
};

const struct iyesde_operations erofs_fast_symlink_iops = {
	.get_link = simple_get_link,
	.getattr = erofs_getattr,
#ifdef CONFIG_EROFS_FS_XATTR
	.listxattr = erofs_listxattr,
#endif
	.get_acl = erofs_get_acl,
};

