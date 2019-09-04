// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#include "xattr.h"

#include <trace/events/erofs.h>

/* no locking */
static int read_inode(struct inode *inode, void *data)
{
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_inode_compact *dic = data;
	struct erofs_inode_extended *die;

	const unsigned int ifmt = le16_to_cpu(dic->i_format);
	struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);
	erofs_blk_t nblks = 0;

	vi->datalayout = erofs_inode_datalayout(ifmt);

	if (vi->datalayout >= EROFS_INODE_DATALAYOUT_MAX) {
		errln("unsupported datalayout %u of nid %llu",
		      vi->datalayout, vi->nid);
		DBG_BUGON(1);
		return -EOPNOTSUPP;
	}

	switch (erofs_inode_version(ifmt)) {
	case EROFS_INODE_LAYOUT_EXTENDED:
		die = data;

		vi->inode_isize = sizeof(struct erofs_inode_extended);
		vi->xattr_isize = erofs_xattr_ibody_size(die->i_xattr_icount);

		inode->i_mode = le16_to_cpu(die->i_mode);
		switch (inode->i_mode & S_IFMT) {
		case S_IFREG:
		case S_IFDIR:
		case S_IFLNK:
			vi->raw_blkaddr = le32_to_cpu(die->i_u.raw_blkaddr);
			break;
		case S_IFCHR:
		case S_IFBLK:
			inode->i_rdev =
				new_decode_dev(le32_to_cpu(die->i_u.rdev));
			break;
		case S_IFIFO:
		case S_IFSOCK:
			inode->i_rdev = 0;
			break;
		default:
			goto bogusimode;
		}
		i_uid_write(inode, le32_to_cpu(die->i_uid));
		i_gid_write(inode, le32_to_cpu(die->i_gid));
		set_nlink(inode, le32_to_cpu(die->i_nlink));

		/* ns timestamp */
		inode->i_mtime.tv_sec = inode->i_ctime.tv_sec =
			le64_to_cpu(die->i_ctime);
		inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec =
			le32_to_cpu(die->i_ctime_nsec);

		inode->i_size = le64_to_cpu(die->i_size);

		/* total blocks for compressed files */
		if (erofs_inode_is_data_compressed(vi->datalayout))
			nblks = le32_to_cpu(die->i_u.compressed_blocks);
		break;
	case EROFS_INODE_LAYOUT_COMPACT:
		vi->inode_isize = sizeof(struct erofs_inode_compact);
		vi->xattr_isize = erofs_xattr_ibody_size(dic->i_xattr_icount);

		inode->i_mode = le16_to_cpu(dic->i_mode);
		switch (inode->i_mode & S_IFMT) {
		case S_IFREG:
		case S_IFDIR:
		case S_IFLNK:
			vi->raw_blkaddr = le32_to_cpu(dic->i_u.raw_blkaddr);
			break;
		case S_IFCHR:
		case S_IFBLK:
			inode->i_rdev =
				new_decode_dev(le32_to_cpu(dic->i_u.rdev));
			break;
		case S_IFIFO:
		case S_IFSOCK:
			inode->i_rdev = 0;
			break;
		default:
			goto bogusimode;
		}
		i_uid_write(inode, le16_to_cpu(dic->i_uid));
		i_gid_write(inode, le16_to_cpu(dic->i_gid));
		set_nlink(inode, le16_to_cpu(dic->i_nlink));

		/* use build time to derive all file time */
		inode->i_mtime.tv_sec = inode->i_ctime.tv_sec =
			sbi->build_time;
		inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec =
			sbi->build_time_nsec;

		inode->i_size = le32_to_cpu(dic->i_size);
		if (erofs_inode_is_data_compressed(vi->datalayout))
			nblks = le32_to_cpu(dic->i_u.compressed_blocks);
		break;
	default:
		errln("unsupported on-disk inode version %u of nid %llu",
		      erofs_inode_version(ifmt), vi->nid);
		DBG_BUGON(1);
		return -EOPNOTSUPP;
	}

	if (!nblks)
		/* measure inode.i_blocks as generic filesystems */
		inode->i_blocks = roundup(inode->i_size, EROFS_BLKSIZ) >> 9;
	else
		inode->i_blocks = nblks << LOG_SECTORS_PER_BLOCK;
	return 0;

bogusimode:
	errln("bogus i_mode (%o) @ nid %llu", inode->i_mode, vi->nid);
	DBG_BUGON(1);
	return -EFSCORRUPTED;
}

/*
 * try_lock can be required since locking order is:
 *   file data(fs_inode)
 *        meta(bd_inode)
 * but the majority of the callers is "iget",
 * in that case we are pretty sure no deadlock since
 * no data operations exist. However I tend to
 * try_lock since it takes no much overhead and
 * will success immediately.
 */
static int fill_inline_data(struct inode *inode, void *data,
			    unsigned int m_pofs)
{
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_sb_info *sbi = EROFS_I_SB(inode);

	/* should be tail-packing data inline */
	if (vi->datalayout != EROFS_INODE_FLAT_INLINE)
		return 0;

	/* fast symlink */
	if (S_ISLNK(inode->i_mode) && inode->i_size < PAGE_SIZE) {
		char *lnk = erofs_kmalloc(sbi, inode->i_size + 1, GFP_KERNEL);

		if (!lnk)
			return -ENOMEM;

		m_pofs += vi->inode_isize + vi->xattr_isize;

		/* inline symlink data shouldn't cross page boundary as well */
		if (m_pofs + inode->i_size > PAGE_SIZE) {
			kfree(lnk);
			errln("inline data cross block boundary @ nid %llu",
			      vi->nid);
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}

		memcpy(lnk, data + m_pofs, inode->i_size);
		lnk[inode->i_size] = '\0';

		inode->i_link = lnk;
		set_inode_fast_symlink(inode);
	}
	return 0;
}

static int fill_inode(struct inode *inode, int isdir)
{
	struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);
	struct erofs_inode *vi = EROFS_I(inode);
	struct page *page;
	void *data;
	int err;
	erofs_blk_t blkaddr;
	unsigned int ofs;
	erofs_off_t inode_loc;

	trace_erofs_fill_inode(inode, isdir);
	inode_loc = iloc(sbi, vi->nid);
	blkaddr = erofs_blknr(inode_loc);
	ofs = erofs_blkoff(inode_loc);

	debugln("%s, reading inode nid %llu at %u of blkaddr %u",
		__func__, vi->nid, ofs, blkaddr);

	page = erofs_get_meta_page(inode->i_sb, blkaddr, isdir);

	if (IS_ERR(page)) {
		errln("failed to get inode (nid: %llu) page, err %ld",
		      vi->nid, PTR_ERR(page));
		return PTR_ERR(page);
	}

	DBG_BUGON(!PageUptodate(page));
	data = page_address(page);

	err = read_inode(inode, data + ofs);
	if (!err) {
		/* setup the new inode */
		switch (inode->i_mode & S_IFMT) {
		case S_IFREG:
			inode->i_op = &erofs_generic_iops;
			inode->i_fop = &generic_ro_fops;
			break;
		case S_IFDIR:
			inode->i_op = &erofs_dir_iops;
			inode->i_fop = &erofs_dir_fops;
			break;
		case S_IFLNK:
			/* by default, page_get_link is used for symlink */
			inode->i_op = &erofs_symlink_iops;
			inode_nohighmem(inode);
			break;
		case S_IFCHR:
		case S_IFBLK:
		case S_IFIFO:
		case S_IFSOCK:
			inode->i_op = &erofs_generic_iops;
			init_special_inode(inode, inode->i_mode, inode->i_rdev);
			goto out_unlock;
		default:
			err = -EFSCORRUPTED;
			goto out_unlock;
		}

		if (erofs_inode_is_data_compressed(vi->datalayout)) {
			err = z_erofs_fill_inode(inode);
			goto out_unlock;
		}

		inode->i_mapping->a_ops = &erofs_raw_access_aops;

		/* fill last page if inline data is available */
		err = fill_inline_data(inode, data, ofs);
	}

out_unlock:
	unlock_page(page);
	put_page(page);
	return err;
}

/*
 * erofs nid is 64bits, but i_ino is 'unsigned long', therefore
 * we should do more for 32-bit platform to find the right inode.
 */
#if BITS_PER_LONG == 32
static int erofs_ilookup_test_actor(struct inode *inode, void *opaque)
{
	const erofs_nid_t nid = *(erofs_nid_t *)opaque;

	return EROFS_I(inode)->nid == nid;
}

static int erofs_iget_set_actor(struct inode *inode, void *opaque)
{
	const erofs_nid_t nid = *(erofs_nid_t *)opaque;

	inode->i_ino = erofs_inode_hash(nid);
	return 0;
}
#endif

static inline struct inode *erofs_iget_locked(struct super_block *sb,
					      erofs_nid_t nid)
{
	const unsigned long hashval = erofs_inode_hash(nid);

#if BITS_PER_LONG >= 64
	/* it is safe to use iget_locked for >= 64-bit platform */
	return iget_locked(sb, hashval);
#else
	return iget5_locked(sb, hashval, erofs_ilookup_test_actor,
		erofs_iget_set_actor, &nid);
#endif
}

struct inode *erofs_iget(struct super_block *sb,
			 erofs_nid_t nid,
			 bool isdir)
{
	struct inode *inode = erofs_iget_locked(sb, nid);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		int err;
		struct erofs_inode *vi = EROFS_I(inode);

		vi->nid = nid;

		err = fill_inode(inode, isdir);
		if (!err)
			unlock_new_inode(inode);
		else {
			iget_failed(inode);
			inode = ERR_PTR(err);
		}
	}
	return inode;
}

int erofs_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int query_flags)
{
	struct inode *const inode = d_inode(path->dentry);

	if (erofs_inode_is_data_compressed(EROFS_I(inode)->datalayout))
		stat->attributes |= STATX_ATTR_COMPRESSED;

	stat->attributes |= STATX_ATTR_IMMUTABLE;
	stat->attributes_mask |= (STATX_ATTR_COMPRESSED |
				  STATX_ATTR_IMMUTABLE);

	generic_fillattr(inode, stat);
	return 0;
}

const struct inode_operations erofs_generic_iops = {
	.getattr = erofs_getattr,
#ifdef CONFIG_EROFS_FS_XATTR
	.listxattr = erofs_listxattr,
#endif
	.get_acl = erofs_get_acl,
};

const struct inode_operations erofs_symlink_iops = {
	.get_link = page_get_link,
	.getattr = erofs_getattr,
#ifdef CONFIG_EROFS_FS_XATTR
	.listxattr = erofs_listxattr,
#endif
	.get_acl = erofs_get_acl,
};

const struct inode_operations erofs_fast_symlink_iops = {
	.get_link = simple_get_link,
	.getattr = erofs_getattr,
#ifdef CONFIG_EROFS_FS_XATTR
	.listxattr = erofs_listxattr,
#endif
	.get_acl = erofs_get_acl,
};

