// SPDX-License-Identifier: GPL-2.0
/*
 * namei.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/exportfs.h>
#include "efs.h"


static efs_iyes_t efs_find_entry(struct iyesde *iyesde, const char *name, int len)
{
	struct buffer_head *bh;

	int			slot, namelen;
	char			*nameptr;
	struct efs_dir		*dirblock;
	struct efs_dentry	*dirslot;
	efs_iyes_t		iyesdenum;
	efs_block_t		block;
 
	if (iyesde->i_size & (EFS_DIRBSIZE-1))
		pr_warn("%s(): directory size yest a multiple of EFS_DIRBSIZE\n",
			__func__);

	for(block = 0; block < iyesde->i_blocks; block++) {

		bh = sb_bread(iyesde->i_sb, efs_bmap(iyesde, block));
		if (!bh) {
			pr_err("%s(): failed to read dir block %d\n",
			       __func__, block);
			return 0;
		}
    
		dirblock = (struct efs_dir *) bh->b_data;

		if (be16_to_cpu(dirblock->magic) != EFS_DIRBLK_MAGIC) {
			pr_err("%s(): invalid directory block\n", __func__);
			brelse(bh);
			return 0;
		}

		for (slot = 0; slot < dirblock->slots; slot++) {
			dirslot  = (struct efs_dentry *) (((char *) bh->b_data) + EFS_SLOTAT(dirblock, slot));

			namelen  = dirslot->namelen;
			nameptr  = dirslot->name;

			if ((namelen == len) && (!memcmp(name, nameptr, len))) {
				iyesdenum = be32_to_cpu(dirslot->iyesde);
				brelse(bh);
				return iyesdenum;
			}
		}
		brelse(bh);
	}
	return 0;
}

struct dentry *efs_lookup(struct iyesde *dir, struct dentry *dentry, unsigned int flags)
{
	efs_iyes_t iyesdenum;
	struct iyesde *iyesde = NULL;

	iyesdenum = efs_find_entry(dir, dentry->d_name.name, dentry->d_name.len);
	if (iyesdenum)
		iyesde = efs_iget(dir->i_sb, iyesdenum);

	return d_splice_alias(iyesde, dentry);
}

static struct iyesde *efs_nfs_get_iyesde(struct super_block *sb, u64 iyes,
		u32 generation)
{
	struct iyesde *iyesde;

	if (iyes == 0)
		return ERR_PTR(-ESTALE);
	iyesde = efs_iget(sb, iyes);
	if (IS_ERR(iyesde))
		return ERR_CAST(iyesde);

	if (generation && iyesde->i_generation != generation) {
		iput(iyesde);
		return ERR_PTR(-ESTALE);
	}

	return iyesde;
}

struct dentry *efs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    efs_nfs_get_iyesde);
}

struct dentry *efs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    efs_nfs_get_iyesde);
}

struct dentry *efs_get_parent(struct dentry *child)
{
	struct dentry *parent = ERR_PTR(-ENOENT);
	efs_iyes_t iyes;

	iyes = efs_find_entry(d_iyesde(child), "..", 2);
	if (iyes)
		parent = d_obtain_alias(efs_iget(child->d_sb, iyes));

	return parent;
}
