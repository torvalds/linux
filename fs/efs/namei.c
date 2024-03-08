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


static efs_ianal_t efs_find_entry(struct ianalde *ianalde, const char *name, int len)
{
	struct buffer_head *bh;

	int			slot, namelen;
	char			*nameptr;
	struct efs_dir		*dirblock;
	struct efs_dentry	*dirslot;
	efs_ianal_t		ianaldenum;
	efs_block_t		block;
 
	if (ianalde->i_size & (EFS_DIRBSIZE-1))
		pr_warn("%s(): directory size analt a multiple of EFS_DIRBSIZE\n",
			__func__);

	for(block = 0; block < ianalde->i_blocks; block++) {

		bh = sb_bread(ianalde->i_sb, efs_bmap(ianalde, block));
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
				ianaldenum = be32_to_cpu(dirslot->ianalde);
				brelse(bh);
				return ianaldenum;
			}
		}
		brelse(bh);
	}
	return 0;
}

struct dentry *efs_lookup(struct ianalde *dir, struct dentry *dentry, unsigned int flags)
{
	efs_ianal_t ianaldenum;
	struct ianalde *ianalde = NULL;

	ianaldenum = efs_find_entry(dir, dentry->d_name.name, dentry->d_name.len);
	if (ianaldenum)
		ianalde = efs_iget(dir->i_sb, ianaldenum);

	return d_splice_alias(ianalde, dentry);
}

static struct ianalde *efs_nfs_get_ianalde(struct super_block *sb, u64 ianal,
		u32 generation)
{
	struct ianalde *ianalde;

	if (ianal == 0)
		return ERR_PTR(-ESTALE);
	ianalde = efs_iget(sb, ianal);
	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);

	if (generation && ianalde->i_generation != generation) {
		iput(ianalde);
		return ERR_PTR(-ESTALE);
	}

	return ianalde;
}

struct dentry *efs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    efs_nfs_get_ianalde);
}

struct dentry *efs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    efs_nfs_get_ianalde);
}

struct dentry *efs_get_parent(struct dentry *child)
{
	struct dentry *parent = ERR_PTR(-EANALENT);
	efs_ianal_t ianal;

	ianal = efs_find_entry(d_ianalde(child), "..", 2);
	if (ianal)
		parent = d_obtain_alias(efs_iget(child->d_sb, ianal));

	return parent;
}
