// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2022, Alibaba Cloud
 */
#include "internal.h"

static int erofs_fill_dentries(struct inode *dir, struct dir_context *ctx,
			       void *dentry_blk, struct erofs_dirent *de,
			       unsigned int nameoff0, unsigned int maxsize)
{
	const struct erofs_dirent *end = dentry_blk + nameoff0;

	while (de < end) {
		unsigned char d_type = fs_ftype_to_dtype(de->file_type);
		unsigned int nameoff = le16_to_cpu(de->nameoff);
		const char *de_name = (char *)dentry_blk + nameoff;
		unsigned int de_namelen;

		/* the last dirent in the block? */
		if (de + 1 >= end)
			de_namelen = strnlen(de_name, maxsize - nameoff);
		else
			de_namelen = le16_to_cpu(de[1].nameoff) - nameoff;

		/* a corrupted entry is found */
		if (nameoff + de_namelen > maxsize ||
		    de_namelen > EROFS_NAME_LEN) {
			erofs_err(dir->i_sb, "bogus dirent @ nid %llu",
				  EROFS_I(dir)->nid);
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}

		if (!dir_emit(ctx, de_name, de_namelen,
			      erofs_nid_to_ino64(EROFS_SB(dir->i_sb),
						 le64_to_cpu(de->nid)), d_type))
			return 1;
		++de;
		ctx->pos += sizeof(struct erofs_dirent);
	}
	return 0;
}

static int erofs_readdir(struct file *f, struct dir_context *ctx)
{
	struct inode *dir = file_inode(f);
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	struct super_block *sb = dir->i_sb;
	struct file_ra_state *ra = &f->f_ra;
	unsigned long bsz = sb->s_blocksize;
	unsigned int ofs = erofs_blkoff(sb, ctx->pos);
	pgoff_t ra_pages = DIV_ROUND_UP_POW2(
			EROFS_I_SB(dir)->dir_ra_bytes, PAGE_SIZE);
	pgoff_t nr_pages = DIV_ROUND_UP_POW2(dir->i_size, PAGE_SIZE);
	int err = 0;
	bool initial = true;

	buf.mapping = dir->i_mapping;
	while (ctx->pos < dir->i_size) {
		erofs_off_t dbstart = ctx->pos - ofs;
		struct erofs_dirent *de;
		unsigned int nameoff, maxsize;

		if (fatal_signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}

		/* readahead blocks to enhance performance for large directories */
		if (ra_pages) {
			pgoff_t idx = DIV_ROUND_UP_POW2(ctx->pos, PAGE_SIZE);
			pgoff_t pages = min(nr_pages - idx, ra_pages);

			if (pages > 1 && !ra_has_index(ra, idx))
				page_cache_sync_readahead(dir->i_mapping, ra,
							  f, idx, pages);
		}

		de = erofs_bread(&buf, dbstart, true);
		if (IS_ERR(de)) {
			erofs_err(sb, "failed to readdir of logical block %llu of nid %llu",
				  erofs_blknr(sb, dbstart), EROFS_I(dir)->nid);
			err = PTR_ERR(de);
			break;
		}

		nameoff = le16_to_cpu(de->nameoff);
		if (nameoff < sizeof(struct erofs_dirent) || nameoff >= bsz) {
			erofs_err(sb, "invalid de[0].nameoff %u @ nid %llu",
				  nameoff, EROFS_I(dir)->nid);
			err = -EFSCORRUPTED;
			break;
		}

		maxsize = min_t(unsigned int, dir->i_size - dbstart, bsz);
		/* search dirents at the arbitrary position */
		if (initial) {
			initial = false;
			ofs = roundup(ofs, sizeof(struct erofs_dirent));
			ctx->pos = dbstart + ofs;
		}

		err = erofs_fill_dentries(dir, ctx, de, (void *)de + ofs,
					  nameoff, maxsize);
		if (err)
			break;
		ctx->pos = dbstart + maxsize;
		ofs = 0;
		cond_resched();
	}
	erofs_put_metabuf(&buf);
	if (EROFS_I(dir)->dot_omitted && ctx->pos == dir->i_size) {
		if (!dir_emit_dot(f, ctx))
			return 0;
		++ctx->pos;
	}
	return err < 0 ? err : 0;
}

const struct file_operations erofs_dir_fops = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= erofs_readdir,
	.unlocked_ioctl = erofs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = erofs_compat_ioctl,
#endif
};
