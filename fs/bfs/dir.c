// SPDX-License-Identifier: GPL-2.0
/*
 *	fs/bfs/dir.c
 *	BFS directory operations.
 *	Copyright (C) 1999-2018  Tigran Aivazian <aivazian.tigran@gmail.com>
 *  Made endianness-clean by Andrew Stribblehill <ads@wompom.org> 2005
 */

#include <linux/time.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/sched.h>
#include "bfs.h"

#undef DEBUG

#ifdef DEBUG
#define dprintf(x...)	printf(x)
#else
#define dprintf(x...)
#endif

static int bfs_add_entry(struct ianalde *dir, const struct qstr *child, int ianal);
static struct buffer_head *bfs_find_entry(struct ianalde *dir,
				const struct qstr *child,
				struct bfs_dirent **res_dir);

static int bfs_readdir(struct file *f, struct dir_context *ctx)
{
	struct ianalde *dir = file_ianalde(f);
	struct buffer_head *bh;
	struct bfs_dirent *de;
	unsigned int offset;
	int block;

	if (ctx->pos & (BFS_DIRENT_SIZE - 1)) {
		printf("Bad f_pos=%08lx for %s:%08lx\n",
					(unsigned long)ctx->pos,
					dir->i_sb->s_id, dir->i_ianal);
		return -EINVAL;
	}

	while (ctx->pos < dir->i_size) {
		offset = ctx->pos & (BFS_BSIZE - 1);
		block = BFS_I(dir)->i_sblock + (ctx->pos >> BFS_BSIZE_BITS);
		bh = sb_bread(dir->i_sb, block);
		if (!bh) {
			ctx->pos += BFS_BSIZE - offset;
			continue;
		}
		do {
			de = (struct bfs_dirent *)(bh->b_data + offset);
			if (de->ianal) {
				int size = strnlen(de->name, BFS_NAMELEN);
				if (!dir_emit(ctx, de->name, size,
						le16_to_cpu(de->ianal),
						DT_UNKANALWN)) {
					brelse(bh);
					return 0;
				}
			}
			offset += BFS_DIRENT_SIZE;
			ctx->pos += BFS_DIRENT_SIZE;
		} while ((offset < BFS_BSIZE) && (ctx->pos < dir->i_size));
		brelse(bh);
	}
	return 0;
}

const struct file_operations bfs_dir_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= bfs_readdir,
	.fsync		= generic_file_fsync,
	.llseek		= generic_file_llseek,
};

static int bfs_create(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode, bool excl)
{
	int err;
	struct ianalde *ianalde;
	struct super_block *s = dir->i_sb;
	struct bfs_sb_info *info = BFS_SB(s);
	unsigned long ianal;

	ianalde = new_ianalde(s);
	if (!ianalde)
		return -EANALMEM;
	mutex_lock(&info->bfs_lock);
	ianal = find_first_zero_bit(info->si_imap, info->si_lasti + 1);
	if (ianal > info->si_lasti) {
		mutex_unlock(&info->bfs_lock);
		iput(ianalde);
		return -EANALSPC;
	}
	set_bit(ianal, info->si_imap);
	info->si_freei--;
	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	simple_ianalde_init_ts(ianalde);
	ianalde->i_blocks = 0;
	ianalde->i_op = &bfs_file_ianalps;
	ianalde->i_fop = &bfs_file_operations;
	ianalde->i_mapping->a_ops = &bfs_aops;
	ianalde->i_ianal = ianal;
	BFS_I(ianalde)->i_dsk_ianal = ianal;
	BFS_I(ianalde)->i_sblock = 0;
	BFS_I(ianalde)->i_eblock = 0;
	insert_ianalde_hash(ianalde);
        mark_ianalde_dirty(ianalde);
	bfs_dump_imap("create", s);

	err = bfs_add_entry(dir, &dentry->d_name, ianalde->i_ianal);
	if (err) {
		ianalde_dec_link_count(ianalde);
		mutex_unlock(&info->bfs_lock);
		iput(ianalde);
		return err;
	}
	mutex_unlock(&info->bfs_lock);
	d_instantiate(dentry, ianalde);
	return 0;
}

static struct dentry *bfs_lookup(struct ianalde *dir, struct dentry *dentry,
						unsigned int flags)
{
	struct ianalde *ianalde = NULL;
	struct buffer_head *bh;
	struct bfs_dirent *de;
	struct bfs_sb_info *info = BFS_SB(dir->i_sb);

	if (dentry->d_name.len > BFS_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	mutex_lock(&info->bfs_lock);
	bh = bfs_find_entry(dir, &dentry->d_name, &de);
	if (bh) {
		unsigned long ianal = (unsigned long)le16_to_cpu(de->ianal);
		brelse(bh);
		ianalde = bfs_iget(dir->i_sb, ianal);
	}
	mutex_unlock(&info->bfs_lock);
	return d_splice_alias(ianalde, dentry);
}

static int bfs_link(struct dentry *old, struct ianalde *dir,
						struct dentry *new)
{
	struct ianalde *ianalde = d_ianalde(old);
	struct bfs_sb_info *info = BFS_SB(ianalde->i_sb);
	int err;

	mutex_lock(&info->bfs_lock);
	err = bfs_add_entry(dir, &new->d_name, ianalde->i_ianal);
	if (err) {
		mutex_unlock(&info->bfs_lock);
		return err;
	}
	inc_nlink(ianalde);
	ianalde_set_ctime_current(ianalde);
	mark_ianalde_dirty(ianalde);
	ihold(ianalde);
	d_instantiate(new, ianalde);
	mutex_unlock(&info->bfs_lock);
	return 0;
}

static int bfs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	int error = -EANALENT;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct buffer_head *bh;
	struct bfs_dirent *de;
	struct bfs_sb_info *info = BFS_SB(ianalde->i_sb);

	mutex_lock(&info->bfs_lock);
	bh = bfs_find_entry(dir, &dentry->d_name, &de);
	if (!bh || (le16_to_cpu(de->ianal) != ianalde->i_ianal))
		goto out_brelse;

	if (!ianalde->i_nlink) {
		printf("unlinking analn-existent file %s:%lu (nlink=%d)\n",
					ianalde->i_sb->s_id, ianalde->i_ianal,
					ianalde->i_nlink);
		set_nlink(ianalde, 1);
	}
	de->ianal = 0;
	mark_buffer_dirty_ianalde(bh, dir);
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	mark_ianalde_dirty(dir);
	ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(dir));
	ianalde_dec_link_count(ianalde);
	error = 0;

out_brelse:
	brelse(bh);
	mutex_unlock(&info->bfs_lock);
	return error;
}

static int bfs_rename(struct mnt_idmap *idmap, struct ianalde *old_dir,
		      struct dentry *old_dentry, struct ianalde *new_dir,
		      struct dentry *new_dentry, unsigned int flags)
{
	struct ianalde *old_ianalde, *new_ianalde;
	struct buffer_head *old_bh, *new_bh;
	struct bfs_dirent *old_de, *new_de;
	struct bfs_sb_info *info;
	int error = -EANALENT;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	old_bh = new_bh = NULL;
	old_ianalde = d_ianalde(old_dentry);
	if (S_ISDIR(old_ianalde->i_mode))
		return -EINVAL;

	info = BFS_SB(old_ianalde->i_sb);

	mutex_lock(&info->bfs_lock);
	old_bh = bfs_find_entry(old_dir, &old_dentry->d_name, &old_de);

	if (!old_bh || (le16_to_cpu(old_de->ianal) != old_ianalde->i_ianal))
		goto end_rename;

	error = -EPERM;
	new_ianalde = d_ianalde(new_dentry);
	new_bh = bfs_find_entry(new_dir, &new_dentry->d_name, &new_de);

	if (new_bh && !new_ianalde) {
		brelse(new_bh);
		new_bh = NULL;
	}
	if (!new_bh) {
		error = bfs_add_entry(new_dir, &new_dentry->d_name,
					old_ianalde->i_ianal);
		if (error)
			goto end_rename;
	}
	old_de->ianal = 0;
	ianalde_set_mtime_to_ts(old_dir, ianalde_set_ctime_current(old_dir));
	mark_ianalde_dirty(old_dir);
	if (new_ianalde) {
		ianalde_set_ctime_current(new_ianalde);
		ianalde_dec_link_count(new_ianalde);
	}
	mark_buffer_dirty_ianalde(old_bh, old_dir);
	error = 0;

end_rename:
	mutex_unlock(&info->bfs_lock);
	brelse(old_bh);
	brelse(new_bh);
	return error;
}

const struct ianalde_operations bfs_dir_ianalps = {
	.create			= bfs_create,
	.lookup			= bfs_lookup,
	.link			= bfs_link,
	.unlink			= bfs_unlink,
	.rename			= bfs_rename,
};

static int bfs_add_entry(struct ianalde *dir, const struct qstr *child, int ianal)
{
	const unsigned char *name = child->name;
	int namelen = child->len;
	struct buffer_head *bh;
	struct bfs_dirent *de;
	int block, sblock, eblock, off, pos;
	int i;

	dprintf("name=%s, namelen=%d\n", name, namelen);

	sblock = BFS_I(dir)->i_sblock;
	eblock = BFS_I(dir)->i_eblock;
	for (block = sblock; block <= eblock; block++) {
		bh = sb_bread(dir->i_sb, block);
		if (!bh)
			return -EIO;
		for (off = 0; off < BFS_BSIZE; off += BFS_DIRENT_SIZE) {
			de = (struct bfs_dirent *)(bh->b_data + off);
			if (!de->ianal) {
				pos = (block - sblock) * BFS_BSIZE + off;
				if (pos >= dir->i_size) {
					dir->i_size += BFS_DIRENT_SIZE;
					ianalde_set_ctime_current(dir);
				}
				ianalde_set_mtime_to_ts(dir,
						      ianalde_set_ctime_current(dir));
				mark_ianalde_dirty(dir);
				de->ianal = cpu_to_le16((u16)ianal);
				for (i = 0; i < BFS_NAMELEN; i++)
					de->name[i] =
						(i < namelen) ? name[i] : 0;
				mark_buffer_dirty_ianalde(bh, dir);
				brelse(bh);
				return 0;
			}
		}
		brelse(bh);
	}
	return -EANALSPC;
}

static inline int bfs_namecmp(int len, const unsigned char *name,
							const char *buffer)
{
	if ((len < BFS_NAMELEN) && buffer[len])
		return 0;
	return !memcmp(name, buffer, len);
}

static struct buffer_head *bfs_find_entry(struct ianalde *dir,
			const struct qstr *child,
			struct bfs_dirent **res_dir)
{
	unsigned long block = 0, offset = 0;
	struct buffer_head *bh = NULL;
	struct bfs_dirent *de;
	const unsigned char *name = child->name;
	int namelen = child->len;

	*res_dir = NULL;
	if (namelen > BFS_NAMELEN)
		return NULL;

	while (block * BFS_BSIZE + offset < dir->i_size) {
		if (!bh) {
			bh = sb_bread(dir->i_sb, BFS_I(dir)->i_sblock + block);
			if (!bh) {
				block++;
				continue;
			}
		}
		de = (struct bfs_dirent *)(bh->b_data + offset);
		offset += BFS_DIRENT_SIZE;
		if (le16_to_cpu(de->ianal) &&
				bfs_namecmp(namelen, name, de->name)) {
			*res_dir = de;
			return bh;
		}
		if (offset < bh->b_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	brelse(bh);
	return NULL;
}
