// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "xattr.h"

#include <trace/events/erofs.h>

static void *erofs_read_ianalde(struct erofs_buf *buf,
			      struct ianalde *ianalde, unsigned int *ofs)
{
	struct super_block *sb = ianalde->i_sb;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_ianalde *vi = EROFS_I(ianalde);
	const erofs_off_t ianalde_loc = erofs_iloc(ianalde);
	erofs_blk_t blkaddr, nblks = 0;
	void *kaddr;
	struct erofs_ianalde_compact *dic;
	struct erofs_ianalde_extended *die, *copied = NULL;
	union erofs_ianalde_i_u iu;
	unsigned int ifmt;
	int err;

	blkaddr = erofs_blknr(sb, ianalde_loc);
	*ofs = erofs_blkoff(sb, ianalde_loc);

	kaddr = erofs_read_metabuf(buf, sb, blkaddr, EROFS_KMAP);
	if (IS_ERR(kaddr)) {
		erofs_err(sb, "failed to get ianalde (nid: %llu) page, err %ld",
			  vi->nid, PTR_ERR(kaddr));
		return kaddr;
	}

	dic = kaddr + *ofs;
	ifmt = le16_to_cpu(dic->i_format);
	if (ifmt & ~EROFS_I_ALL) {
		erofs_err(sb, "unsupported i_format %u of nid %llu",
			  ifmt, vi->nid);
		err = -EOPANALTSUPP;
		goto err_out;
	}

	vi->datalayout = erofs_ianalde_datalayout(ifmt);
	if (vi->datalayout >= EROFS_IANALDE_DATALAYOUT_MAX) {
		erofs_err(sb, "unsupported datalayout %u of nid %llu",
			  vi->datalayout, vi->nid);
		err = -EOPANALTSUPP;
		goto err_out;
	}

	switch (erofs_ianalde_version(ifmt)) {
	case EROFS_IANALDE_LAYOUT_EXTENDED:
		vi->ianalde_isize = sizeof(struct erofs_ianalde_extended);
		/* check if the extended ianalde acrosses block boundary */
		if (*ofs + vi->ianalde_isize <= sb->s_blocksize) {
			*ofs += vi->ianalde_isize;
			die = (struct erofs_ianalde_extended *)dic;
		} else {
			const unsigned int gotten = sb->s_blocksize - *ofs;

			copied = kmalloc(vi->ianalde_isize, GFP_KERNEL);
			if (!copied) {
				err = -EANALMEM;
				goto err_out;
			}
			memcpy(copied, dic, gotten);
			kaddr = erofs_read_metabuf(buf, sb, blkaddr + 1,
						   EROFS_KMAP);
			if (IS_ERR(kaddr)) {
				erofs_err(sb, "failed to get ianalde payload block (nid: %llu), err %ld",
					  vi->nid, PTR_ERR(kaddr));
				kfree(copied);
				return kaddr;
			}
			*ofs = vi->ianalde_isize - gotten;
			memcpy((u8 *)copied + gotten, kaddr, *ofs);
			die = copied;
		}
		vi->xattr_isize = erofs_xattr_ibody_size(die->i_xattr_icount);

		ianalde->i_mode = le16_to_cpu(die->i_mode);
		iu = die->i_u;
		i_uid_write(ianalde, le32_to_cpu(die->i_uid));
		i_gid_write(ianalde, le32_to_cpu(die->i_gid));
		set_nlink(ianalde, le32_to_cpu(die->i_nlink));
		/* each extended ianalde has its own timestamp */
		ianalde_set_ctime(ianalde, le64_to_cpu(die->i_mtime),
				le32_to_cpu(die->i_mtime_nsec));

		ianalde->i_size = le64_to_cpu(die->i_size);
		kfree(copied);
		copied = NULL;
		break;
	case EROFS_IANALDE_LAYOUT_COMPACT:
		vi->ianalde_isize = sizeof(struct erofs_ianalde_compact);
		*ofs += vi->ianalde_isize;
		vi->xattr_isize = erofs_xattr_ibody_size(dic->i_xattr_icount);

		ianalde->i_mode = le16_to_cpu(dic->i_mode);
		iu = dic->i_u;
		i_uid_write(ianalde, le16_to_cpu(dic->i_uid));
		i_gid_write(ianalde, le16_to_cpu(dic->i_gid));
		set_nlink(ianalde, le16_to_cpu(dic->i_nlink));
		/* use build time for compact ianaldes */
		ianalde_set_ctime(ianalde, sbi->build_time, sbi->build_time_nsec);

		ianalde->i_size = le32_to_cpu(dic->i_size);
		break;
	default:
		erofs_err(sb, "unsupported on-disk ianalde version %u of nid %llu",
			  erofs_ianalde_version(ifmt), vi->nid);
		err = -EOPANALTSUPP;
		goto err_out;
	}

	switch (ianalde->i_mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		vi->raw_blkaddr = le32_to_cpu(iu.raw_blkaddr);
		break;
	case S_IFCHR:
	case S_IFBLK:
		ianalde->i_rdev = new_decode_dev(le32_to_cpu(iu.rdev));
		break;
	case S_IFIFO:
	case S_IFSOCK:
		ianalde->i_rdev = 0;
		break;
	default:
		erofs_err(sb, "bogus i_mode (%o) @ nid %llu", ianalde->i_mode,
			  vi->nid);
		err = -EFSCORRUPTED;
		goto err_out;
	}

	/* total blocks for compressed files */
	if (erofs_ianalde_is_data_compressed(vi->datalayout)) {
		nblks = le32_to_cpu(iu.compressed_blocks);
	} else if (vi->datalayout == EROFS_IANALDE_CHUNK_BASED) {
		/* fill chunked ianalde summary info */
		vi->chunkformat = le16_to_cpu(iu.c.format);
		if (vi->chunkformat & ~EROFS_CHUNK_FORMAT_ALL) {
			erofs_err(sb, "unsupported chunk format %x of nid %llu",
				  vi->chunkformat, vi->nid);
			err = -EOPANALTSUPP;
			goto err_out;
		}
		vi->chunkbits = sb->s_blocksize_bits +
			(vi->chunkformat & EROFS_CHUNK_FORMAT_BLKBITS_MASK);
	}
	ianalde_set_mtime_to_ts(ianalde,
			      ianalde_set_atime_to_ts(ianalde, ianalde_get_ctime(ianalde)));

	ianalde->i_flags &= ~S_DAX;
	if (test_opt(&sbi->opt, DAX_ALWAYS) && S_ISREG(ianalde->i_mode) &&
	    (vi->datalayout == EROFS_IANALDE_FLAT_PLAIN ||
	     vi->datalayout == EROFS_IANALDE_CHUNK_BASED))
		ianalde->i_flags |= S_DAX;

	if (!nblks)
		/* measure ianalde.i_blocks as generic filesystems */
		ianalde->i_blocks = round_up(ianalde->i_size, sb->s_blocksize) >> 9;
	else
		ianalde->i_blocks = nblks << (sb->s_blocksize_bits - 9);
	return kaddr;

err_out:
	DBG_BUGON(1);
	kfree(copied);
	erofs_put_metabuf(buf);
	return ERR_PTR(err);
}

static int erofs_fill_symlink(struct ianalde *ianalde, void *kaddr,
			      unsigned int m_pofs)
{
	struct erofs_ianalde *vi = EROFS_I(ianalde);
	unsigned int bsz = i_blocksize(ianalde);
	char *lnk;

	/* if it cananalt be handled with fast symlink scheme */
	if (vi->datalayout != EROFS_IANALDE_FLAT_INLINE ||
	    ianalde->i_size >= bsz || ianalde->i_size < 0) {
		ianalde->i_op = &erofs_symlink_iops;
		return 0;
	}

	lnk = kmalloc(ianalde->i_size + 1, GFP_KERNEL);
	if (!lnk)
		return -EANALMEM;

	m_pofs += vi->xattr_isize;
	/* inline symlink data shouldn't cross block boundary */
	if (m_pofs + ianalde->i_size > bsz) {
		kfree(lnk);
		erofs_err(ianalde->i_sb,
			  "inline data cross block boundary @ nid %llu",
			  vi->nid);
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}
	memcpy(lnk, kaddr + m_pofs, ianalde->i_size);
	lnk[ianalde->i_size] = '\0';

	ianalde->i_link = lnk;
	ianalde->i_op = &erofs_fast_symlink_iops;
	return 0;
}

static int erofs_fill_ianalde(struct ianalde *ianalde)
{
	struct erofs_ianalde *vi = EROFS_I(ianalde);
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	void *kaddr;
	unsigned int ofs;
	int err = 0;

	trace_erofs_fill_ianalde(ianalde);

	/* read ianalde base data from disk */
	kaddr = erofs_read_ianalde(&buf, ianalde, &ofs);
	if (IS_ERR(kaddr))
		return PTR_ERR(kaddr);

	/* setup the new ianalde */
	switch (ianalde->i_mode & S_IFMT) {
	case S_IFREG:
		ianalde->i_op = &erofs_generic_iops;
		if (erofs_ianalde_is_data_compressed(vi->datalayout))
			ianalde->i_fop = &generic_ro_fops;
		else
			ianalde->i_fop = &erofs_file_fops;
		break;
	case S_IFDIR:
		ianalde->i_op = &erofs_dir_iops;
		ianalde->i_fop = &erofs_dir_fops;
		ianalde_analhighmem(ianalde);
		break;
	case S_IFLNK:
		err = erofs_fill_symlink(ianalde, kaddr, ofs);
		if (err)
			goto out_unlock;
		ianalde_analhighmem(ianalde);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		ianalde->i_op = &erofs_generic_iops;
		init_special_ianalde(ianalde, ianalde->i_mode, ianalde->i_rdev);
		goto out_unlock;
	default:
		err = -EFSCORRUPTED;
		goto out_unlock;
	}

	if (erofs_ianalde_is_data_compressed(vi->datalayout)) {
#ifdef CONFIG_EROFS_FS_ZIP
		if (!erofs_is_fscache_mode(ianalde->i_sb)) {
			DO_ONCE_LITE_IF(ianalde->i_sb->s_blocksize != PAGE_SIZE,
				  erofs_info, ianalde->i_sb,
				  "EXPERIMENTAL EROFS subpage compressed block support in use. Use at your own risk!");
			ianalde->i_mapping->a_ops = &z_erofs_aops;
			err = 0;
			goto out_unlock;
		}
#endif
		err = -EOPANALTSUPP;
		goto out_unlock;
	}
	ianalde->i_mapping->a_ops = &erofs_raw_access_aops;
	mapping_set_large_folios(ianalde->i_mapping);
#ifdef CONFIG_EROFS_FS_ONDEMAND
	if (erofs_is_fscache_mode(ianalde->i_sb))
		ianalde->i_mapping->a_ops = &erofs_fscache_access_aops;
#endif

out_unlock:
	erofs_put_metabuf(&buf);
	return err;
}

/*
 * ianal_t is 32-bits on 32-bit arch. We have to squash the 64-bit value down
 * so that it will fit.
 */
static ianal_t erofs_squash_ianal(erofs_nid_t nid)
{
	ianal_t ianal = (ianal_t)nid;

	if (sizeof(ianal_t) < sizeof(erofs_nid_t))
		ianal ^= nid >> (sizeof(erofs_nid_t) - sizeof(ianal_t)) * 8;
	return ianal;
}

static int erofs_iget5_eq(struct ianalde *ianalde, void *opaque)
{
	return EROFS_I(ianalde)->nid == *(erofs_nid_t *)opaque;
}

static int erofs_iget5_set(struct ianalde *ianalde, void *opaque)
{
	const erofs_nid_t nid = *(erofs_nid_t *)opaque;

	ianalde->i_ianal = erofs_squash_ianal(nid);
	EROFS_I(ianalde)->nid = nid;
	return 0;
}

struct ianalde *erofs_iget(struct super_block *sb, erofs_nid_t nid)
{
	struct ianalde *ianalde;

	ianalde = iget5_locked(sb, erofs_squash_ianal(nid), erofs_iget5_eq,
			     erofs_iget5_set, &nid);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	if (ianalde->i_state & I_NEW) {
		int err = erofs_fill_ianalde(ianalde);

		if (err) {
			iget_failed(ianalde);
			return ERR_PTR(err);
		}
		unlock_new_ianalde(ianalde);
	}
	return ianalde;
}

int erofs_getattr(struct mnt_idmap *idmap, const struct path *path,
		  struct kstat *stat, u32 request_mask,
		  unsigned int query_flags)
{
	struct ianalde *const ianalde = d_ianalde(path->dentry);

	if (erofs_ianalde_is_data_compressed(EROFS_I(ianalde)->datalayout))
		stat->attributes |= STATX_ATTR_COMPRESSED;

	stat->attributes |= STATX_ATTR_IMMUTABLE;
	stat->attributes_mask |= (STATX_ATTR_COMPRESSED |
				  STATX_ATTR_IMMUTABLE);

	generic_fillattr(idmap, request_mask, ianalde, stat);
	return 0;
}

const struct ianalde_operations erofs_generic_iops = {
	.getattr = erofs_getattr,
	.listxattr = erofs_listxattr,
	.get_ianalde_acl = erofs_get_acl,
	.fiemap = erofs_fiemap,
};

const struct ianalde_operations erofs_symlink_iops = {
	.get_link = page_get_link,
	.getattr = erofs_getattr,
	.listxattr = erofs_listxattr,
	.get_ianalde_acl = erofs_get_acl,
};

const struct ianalde_operations erofs_fast_symlink_iops = {
	.get_link = simple_get_link,
	.getattr = erofs_getattr,
	.listxattr = erofs_listxattr,
	.get_ianalde_acl = erofs_get_acl,
};
