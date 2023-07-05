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

static int bfs_add_entry(struct inode *dir, const struct qstr *child, int ino);
static struct buffer_head *bfs_find_entry(struct inode *dir,
				const struct qstr *child,
				struct bfs_dirent **res_dir);

static int bfs_readdir(struct file *f, struct dir_context *ctx)
{
	struct inode *dir = file_inode(f);
	struct buffer_head *bh;
	struct bfs_dirent *de;
	unsigned int offset;
	int block;

	if (ctx->pos & (BFS_DIRENT_SIZE - 1)) {
		printf("Bad f_pos=%08lx for %s:%08lx\n",
					(unsigned long)ctx->pos,
					dir->i_sb->s_id, dir->i_ino);
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
			if (de->ino) {
				int size = strnlen(de->name, BFS_NAMELEN);
				if (!dir_emit(ctx, de->name, size,
						le16_to_cpu(de->ino),
						DT_UNKNOWN)) {
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

static int bfs_create(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, umode_t mode, bool excl)
{
	int err;
	struct inode *inode;
	struct super_block *s = dir->i_sb;
	struct bfs_sb_info *info = BFS_SB(s);
	unsigned long ino;

	inode = new_inode(s);
	if (!inode)
		return -ENOMEM;
	mutex_lock(&info->bfs_lock);
	ino = find_first_zero_bit(info->si_imap, info->si_lasti + 1);
	if (ino > info->si_lasti) {
		mutex_unlock(&info->bfs_lock);
		iput(inode);
		return -ENOSPC;
	}
	set_bit(ino, info->si_imap);
	info->si_freei--;
	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_blocks = 0;
	inode->i_op = &bfs_file_inops;
	inode->i_fop = &bfs_file_operations;
	inode->i_mapping->a_ops = &bfs_aops;
	inode->i_ino = ino;
	BFS_I(inode)->i_dsk_ino = ino;
	BFS_I(inode)->i_sblock = 0;
	BFS_I(inode)->i_eblock = 0;
	insert_inode_hash(inode);
        mark_inode_dirty(inode);
	bfs_dump_imap("create", s);

	err = bfs_add_entry(dir, &dentry->d_name, inode->i_ino);
	if (err) {
		inode_dec_link_count(inode);
		mutex_unlock(&info->bfs_lock);
		iput(inode);
		return err;
	}
	mutex_unlock(&info->bfs_lock);
	d_instantiate(dentry, inode);
	return 0;
}

static struct dentry *bfs_lookup(struct inode *dir, struct dentry *dentry,
						unsigned int flags)
{
	struct inode *inode = NULL;
	struct buffer_head *bh;
	struct bfs_dirent *de;
	struct bfs_sb_info *info = BFS_SB(dir->i_sb);

	if (dentry->d_name.len > BFS_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	mutex_lock(&info->bfs_lock);
	bh = bfs_find_entry(dir, &dentry->d_name, &de);
	if (bh) {
		unsigned long ino = (unsigned long)le16_to_cpu(de->ino);
		brelse(bh);
		inode = bfs_iget(dir->i_sb, ino);
	}
	mutex_unlock(&info->bfs_lock);
	return d_splice_alias(inode, dentry);
}

static int bfs_link(struct dentry *old, struct inode *dir,
						struct dentry *new)
{
	struct inode *inode = d_inode(old);
	struct bfs_sb_info *info = BFS_SB(inode->i_sb);
	int err;

	mutex_lock(&info->bfs_lock);
	err = bfs_add_entry(dir, &new->d_name, inode->i_ino);
	if (err) {
		mutex_unlock(&info->bfs_lock);
		return err;
	}
	inc_nlink(inode);
	inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);
	ihold(inode);
	d_instantiate(new, inode);
	mutex_unlock(&info->bfs_lock);
	return 0;
}

static int bfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error = -ENOENT;
	struct inode *inode = d_inode(dentry);
	struct buffer_head *bh;
	struct bfs_dirent *de;
	struct bfs_sb_info *info = BFS_SB(inode->i_sb);

	mutex_lock(&info->bfs_lock);
	bh = bfs_find_entry(dir, &dentry->d_name, &de);
	if (!bh || (le16_to_cpu(de->ino) != inode->i_ino))
		goto out_brelse;

	if (!inode->i_nlink) {
		printf("unlinking non-existent file %s:%lu (nlink=%d)\n",
					inode->i_sb->s_id, inode->i_ino,
					inode->i_nlink);
		set_nlink(inode, 1);
	}
	de->ino = 0;
	mark_buffer_dirty_inode(bh, dir);
	dir->i_ctime = dir->i_mtime = current_time(dir);
	mark_inode_dirty(dir);
	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);
	error = 0;

out_brelse:
	brelse(bh);
	mutex_unlock(&info->bfs_lock);
	return error;
}

static int bfs_rename(struct mnt_idmap *idmap, struct inode *old_dir,
		      struct dentry *old_dentry, struct inode *new_dir,
		      struct dentry *new_dentry, unsigned int flags)
{
	struct inode *old_inode, *new_inode;
	struct buffer_head *old_bh, *new_bh;
	struct bfs_dirent *old_de, *new_de;
	struct bfs_sb_info *info;
	int error = -ENOENT;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	old_bh = new_bh = NULL;
	old_inode = d_inode(old_dentry);
	if (S_ISDIR(old_inode->i_mode))
		return -EINVAL;

	info = BFS_SB(old_inode->i_sb);

	mutex_lock(&info->bfs_lock);
	old_bh = bfs_find_entry(old_dir, &old_dentry->d_name, &old_de);

	if (!old_bh || (le16_to_cpu(old_de->ino) != old_inode->i_ino))
		goto end_rename;

	error = -EPERM;
	new_inode = d_inode(new_dentry);
	new_bh = bfs_find_entry(new_dir, &new_dentry->d_name, &new_de);

	if (new_bh && !new_inode) {
		brelse(new_bh);
		new_bh = NULL;
	}
	if (!new_bh) {
		error = bfs_add_entry(new_dir, &new_dentry->d_name,
					old_inode->i_ino);
		if (error)
			goto end_rename;
	}
	old_de->ino = 0;
	old_dir->i_ctime = old_dir->i_mtime = current_time(old_dir);
	mark_inode_dirty(old_dir);
	if (new_inode) {
		new_inode->i_ctime = current_time(new_inode);
		inode_dec_link_count(new_inode);
	}
	mark_buffer_dirty_inode(old_bh, old_dir);
	error = 0;

end_rename:
	mutex_unlock(&info->bfs_lock);
	brelse(old_bh);
	brelse(new_bh);
	return error;
}

const struct inode_operations bfs_dir_inops = {
	.create			= bfs_create,
	.lookup			= bfs_lookup,
	.link			= bfs_link,
	.unlink			= bfs_unlink,
	.rename			= bfs_rename,
};

static int bfs_add_entry(struct inode *dir, const struct qstr *child, int ino)
{
	const unsigned char *name = child->name;
	int namelen = child->len;
	struct buffer_head *bh;
	struct bfs_dirent *de;
	int block, sblock, eblock, off, pos;
	int i;

	dprintf("name=%s, namelen=%d\n", name, namelen);

	if (!namelen)
		return -ENOENT;
	if (namelen > BFS_NAMELEN)
		return -ENAMETOOLONG;

	sblock = BFS_I(dir)->i_sblock;
	eblock = BFS_I(dir)->i_eblock;
	for (block = sblock; block <= eblock; block++) {
		bh = sb_bread(dir->i_sb, block);
		if (!bh)
			return -EIO;
		for (off = 0; off < BFS_BSIZE; off += BFS_DIRENT_SIZE) {
			de = (struct bfs_dirent *)(bh->b_data + off);
			if (!de->ino) {
				pos = (block - sblock) * BFS_BSIZE + off;
				if (pos >= dir->i_size) {
					dir->i_size += BFS_DIRENT_SIZE;
					dir->i_ctime = current_time(dir);
				}
				dir->i_mtime = dir->i_ctime = current_time(dir);
				mark_inode_dirty(dir);
				de->ino = cpu_to_le16((u16)ino);
				for (i = 0; i < BFS_NAMELEN; i++)
					de->name[i] =
						(i < namelen) ? name[i] : 0;
				mark_buffer_dirty_inode(bh, dir);
				brelse(bh);
				return 0;
			}
		}
		brelse(bh);
	}
	return -ENOSPC;
}

static inline int bfs_namecmp(int len, const unsigned char *name,
							const char *buffer)
{
	if ((len < BFS_NAMELEN) && buffer[len])
		return 0;
	return !memcmp(name, buffer, len);
}

static struct buffer_head *bfs_find_entry(struct inode *dir,
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
		if (le16_to_cpu(de->ino) &&
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
