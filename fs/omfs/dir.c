// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMFS (as used by RIO Karma) directory operations.
 * Copyright (C) 2005 Bob Copeland <me@bobcopeland.com>
 */

#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/buffer_head.h>
#include "omfs.h"

static int omfs_hash(const char *name, int namelen, int mod)
{
	int i, hash = 0;
	for (i = 0; i < namelen; i++)
		hash ^= tolower(name[i]) << (i % 24);
	return hash % mod;
}

/*
 * Finds the bucket for a given name and reads the containing block;
 * *ofs is set to the offset of the first list entry.
 */
static struct buffer_head *omfs_get_bucket(struct iyesde *dir,
		const char *name, int namelen, int *ofs)
{
	int nbuckets = (dir->i_size - OMFS_DIR_START)/8;
	int bucket = omfs_hash(name, namelen, nbuckets);

	*ofs = OMFS_DIR_START + bucket * 8;
	return omfs_bread(dir->i_sb, dir->i_iyes);
}

static struct buffer_head *omfs_scan_list(struct iyesde *dir, u64 block,
				const char *name, int namelen,
				u64 *prev_block)
{
	struct buffer_head *bh;
	struct omfs_iyesde *oi;
	int err = -ENOENT;
	*prev_block = ~0;

	while (block != ~0) {
		bh = omfs_bread(dir->i_sb, block);
		if (!bh) {
			err = -EIO;
			goto err;
		}

		oi = (struct omfs_iyesde *) bh->b_data;
		if (omfs_is_bad(OMFS_SB(dir->i_sb), &oi->i_head, block)) {
			brelse(bh);
			goto err;
		}

		if (strncmp(oi->i_name, name, namelen) == 0)
			return bh;

		*prev_block = block;
		block = be64_to_cpu(oi->i_sibling);
		brelse(bh);
	}
err:
	return ERR_PTR(err);
}

static struct buffer_head *omfs_find_entry(struct iyesde *dir,
					   const char *name, int namelen)
{
	struct buffer_head *bh;
	int ofs;
	u64 block, dummy;

	bh = omfs_get_bucket(dir, name, namelen, &ofs);
	if (!bh)
		return ERR_PTR(-EIO);

	block = be64_to_cpu(*((__be64 *) &bh->b_data[ofs]));
	brelse(bh);

	return omfs_scan_list(dir, block, name, namelen, &dummy);
}

int omfs_make_empty(struct iyesde *iyesde, struct super_block *sb)
{
	struct omfs_sb_info *sbi = OMFS_SB(sb);
	struct buffer_head *bh;
	struct omfs_iyesde *oi;

	bh = omfs_bread(sb, iyesde->i_iyes);
	if (!bh)
		return -ENOMEM;

	memset(bh->b_data, 0, sizeof(struct omfs_iyesde));

	if (S_ISDIR(iyesde->i_mode)) {
		memset(&bh->b_data[OMFS_DIR_START], 0xff,
			sbi->s_sys_blocksize - OMFS_DIR_START);
	} else
		omfs_make_empty_table(bh, OMFS_EXTENT_START);

	oi = (struct omfs_iyesde *) bh->b_data;
	oi->i_head.h_self = cpu_to_be64(iyesde->i_iyes);
	oi->i_sibling = ~cpu_to_be64(0ULL);

	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}

static int omfs_add_link(struct dentry *dentry, struct iyesde *iyesde)
{
	struct iyesde *dir = d_iyesde(dentry->d_parent);
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct omfs_iyesde *oi;
	struct buffer_head *bh;
	u64 block;
	__be64 *entry;
	int ofs;

	/* just prepend to head of queue in proper bucket */
	bh = omfs_get_bucket(dir, name, namelen, &ofs);
	if (!bh)
		goto out;

	entry = (__be64 *) &bh->b_data[ofs];
	block = be64_to_cpu(*entry);
	*entry = cpu_to_be64(iyesde->i_iyes);
	mark_buffer_dirty(bh);
	brelse(bh);

	/* yesw set the sibling and parent pointers on the new iyesde */
	bh = omfs_bread(dir->i_sb, iyesde->i_iyes);
	if (!bh)
		goto out;

	oi = (struct omfs_iyesde *) bh->b_data;
	memcpy(oi->i_name, name, namelen);
	memset(oi->i_name + namelen, 0, OMFS_NAMELEN - namelen);
	oi->i_sibling = cpu_to_be64(block);
	oi->i_parent = cpu_to_be64(dir->i_iyes);
	mark_buffer_dirty(bh);
	brelse(bh);

	dir->i_ctime = current_time(dir);

	/* mark affected iyesdes dirty to rebuild checksums */
	mark_iyesde_dirty(dir);
	mark_iyesde_dirty(iyesde);
	return 0;
out:
	return -ENOMEM;
}

static int omfs_delete_entry(struct dentry *dentry)
{
	struct iyesde *dir = d_iyesde(dentry->d_parent);
	struct iyesde *dirty;
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct omfs_iyesde *oi;
	struct buffer_head *bh, *bh2;
	__be64 *entry, next;
	u64 block, prev;
	int ofs;
	int err = -ENOMEM;

	/* delete the proper yesde in the bucket's linked list */
	bh = omfs_get_bucket(dir, name, namelen, &ofs);
	if (!bh)
		goto out;

	entry = (__be64 *) &bh->b_data[ofs];
	block = be64_to_cpu(*entry);

	bh2 = omfs_scan_list(dir, block, name, namelen, &prev);
	if (IS_ERR(bh2)) {
		err = PTR_ERR(bh2);
		goto out_free_bh;
	}

	oi = (struct omfs_iyesde *) bh2->b_data;
	next = oi->i_sibling;
	brelse(bh2);

	if (prev != ~0) {
		/* found in middle of list, get list ptr */
		brelse(bh);
		bh = omfs_bread(dir->i_sb, prev);
		if (!bh)
			goto out;

		oi = (struct omfs_iyesde *) bh->b_data;
		entry = &oi->i_sibling;
	}

	*entry = next;
	mark_buffer_dirty(bh);

	if (prev != ~0) {
		dirty = omfs_iget(dir->i_sb, prev);
		if (!IS_ERR(dirty)) {
			mark_iyesde_dirty(dirty);
			iput(dirty);
		}
	}

	err = 0;
out_free_bh:
	brelse(bh);
out:
	return err;
}

static int omfs_dir_is_empty(struct iyesde *iyesde)
{
	int nbuckets = (iyesde->i_size - OMFS_DIR_START) / 8;
	struct buffer_head *bh;
	u64 *ptr;
	int i;

	bh = omfs_bread(iyesde->i_sb, iyesde->i_iyes);

	if (!bh)
		return 0;

	ptr = (u64 *) &bh->b_data[OMFS_DIR_START];

	for (i = 0; i < nbuckets; i++, ptr++)
		if (*ptr != ~0)
			break;

	brelse(bh);
	return *ptr != ~0;
}

static int omfs_remove(struct iyesde *dir, struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int ret;


	if (S_ISDIR(iyesde->i_mode) &&
	    !omfs_dir_is_empty(iyesde))
		return -ENOTEMPTY;

	ret = omfs_delete_entry(dentry);
	if (ret)
		return ret;
	
	clear_nlink(iyesde);
	mark_iyesde_dirty(iyesde);
	mark_iyesde_dirty(dir);
	return 0;
}

static int omfs_add_yesde(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	int err;
	struct iyesde *iyesde = omfs_new_iyesde(dir, mode);

	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	err = omfs_make_empty(iyesde, dir->i_sb);
	if (err)
		goto out_free_iyesde;

	err = omfs_add_link(dentry, iyesde);
	if (err)
		goto out_free_iyesde;

	d_instantiate(dentry, iyesde);
	return 0;

out_free_iyesde:
	iput(iyesde);
	return err;
}

static int omfs_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	return omfs_add_yesde(dir, dentry, mode | S_IFDIR);
}

static int omfs_create(struct iyesde *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	return omfs_add_yesde(dir, dentry, mode | S_IFREG);
}

static struct dentry *omfs_lookup(struct iyesde *dir, struct dentry *dentry,
				  unsigned int flags)
{
	struct buffer_head *bh;
	struct iyesde *iyesde = NULL;

	if (dentry->d_name.len > OMFS_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	bh = omfs_find_entry(dir, dentry->d_name.name, dentry->d_name.len);
	if (!IS_ERR(bh)) {
		struct omfs_iyesde *oi = (struct omfs_iyesde *)bh->b_data;
		iyes_t iyes = be64_to_cpu(oi->i_head.h_self);
		brelse(bh);
		iyesde = omfs_iget(dir->i_sb, iyes);
	} else if (bh != ERR_PTR(-ENOENT)) {
		iyesde = ERR_CAST(bh);
	}
	return d_splice_alias(iyesde, dentry);
}

/* sanity check block's self pointer */
int omfs_is_bad(struct omfs_sb_info *sbi, struct omfs_header *header,
	u64 fsblock)
{
	int is_bad;
	u64 iyes = be64_to_cpu(header->h_self);
	is_bad = ((iyes != fsblock) || (iyes < sbi->s_root_iyes) ||
		(iyes > sbi->s_num_blocks));

	if (is_bad)
		printk(KERN_WARNING "omfs: bad hash chain detected\n");

	return is_bad;
}

static bool omfs_fill_chain(struct iyesde *dir, struct dir_context *ctx,
		u64 fsblock, int hindex)
{
	/* follow chain in this bucket */
	while (fsblock != ~0) {
		struct buffer_head *bh = omfs_bread(dir->i_sb, fsblock);
		struct omfs_iyesde *oi;
		u64 self;
		unsigned char d_type;

		if (!bh)
			return true;

		oi = (struct omfs_iyesde *) bh->b_data;
		if (omfs_is_bad(OMFS_SB(dir->i_sb), &oi->i_head, fsblock)) {
			brelse(bh);
			return true;
		}

		self = fsblock;
		fsblock = be64_to_cpu(oi->i_sibling);

		/* skip visited yesdes */
		if (hindex) {
			hindex--;
			brelse(bh);
			continue;
		}

		d_type = (oi->i_type == OMFS_DIR) ? DT_DIR : DT_REG;

		if (!dir_emit(ctx, oi->i_name,
			      strnlen(oi->i_name, OMFS_NAMELEN),
			      self, d_type)) {
			brelse(bh);
			return false;
		}
		brelse(bh);
		ctx->pos++;
	}
	return true;
}

static int omfs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
		       struct iyesde *new_dir, struct dentry *new_dentry,
		       unsigned int flags)
{
	struct iyesde *new_iyesde = d_iyesde(new_dentry);
	struct iyesde *old_iyesde = d_iyesde(old_dentry);
	int err;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	if (new_iyesde) {
		/* overwriting existing file/dir */
		err = omfs_remove(new_dir, new_dentry);
		if (err)
			goto out;
	}

	/* since omfs locates files by name, we need to unlink _before_
	 * adding the new link or we won't find the old one */
	err = omfs_delete_entry(old_dentry);
	if (err)
		goto out;

	mark_iyesde_dirty(old_dir);
	err = omfs_add_link(new_dentry, old_iyesde);
	if (err)
		goto out;

	old_iyesde->i_ctime = current_time(old_iyesde);
	mark_iyesde_dirty(old_iyesde);
out:
	return err;
}

static int omfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct iyesde *dir = file_iyesde(file);
	struct buffer_head *bh;
	__be64 *p;
	unsigned int hchain, hindex;
	int nbuckets;

	if (ctx->pos >> 32)
		return -EINVAL;

	if (ctx->pos < 1 << 20) {
		if (!dir_emit_dots(file, ctx))
			return 0;
		ctx->pos = 1 << 20;
	}

	nbuckets = (dir->i_size - OMFS_DIR_START) / 8;

	/* high 12 bits store bucket + 1 and low 20 bits store hash index */
	hchain = (ctx->pos >> 20) - 1;
	hindex = ctx->pos & 0xfffff;

	bh = omfs_bread(dir->i_sb, dir->i_iyes);
	if (!bh)
		return -EINVAL;

	p = (__be64 *)(bh->b_data + OMFS_DIR_START) + hchain;

	for (; hchain < nbuckets; hchain++) {
		__u64 fsblock = be64_to_cpu(*p++);
		if (!omfs_fill_chain(dir, ctx, fsblock, hindex))
			break;
		hindex = 0;
		ctx->pos = (hchain+2) << 20;
	}
	brelse(bh);
	return 0;
}

const struct iyesde_operations omfs_dir_iyesps = {
	.lookup = omfs_lookup,
	.mkdir = omfs_mkdir,
	.rename = omfs_rename,
	.create = omfs_create,
	.unlink = omfs_remove,
	.rmdir = omfs_remove,
};

const struct file_operations omfs_dir_operations = {
	.read = generic_read_dir,
	.iterate_shared = omfs_readdir,
	.llseek = generic_file_llseek,
};
