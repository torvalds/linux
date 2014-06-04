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


static efs_ino_t efs_find_entry(struct inode *inode, const char *name, int len) {
	struct buffer_head *bh;

	int			slot, namelen;
	char			*nameptr;
	struct efs_dir		*dirblock;
	struct efs_dentry	*dirslot;
	efs_ino_t		inodenum;
	efs_block_t		block;
 
	if (inode->i_size & (EFS_DIRBSIZE-1))
		pr_warn("%s(): directory size not a multiple of EFS_DIRBSIZE\n",
			__func__);

	for(block = 0; block < inode->i_blocks; block++) {

		bh = sb_bread(inode->i_sb, efs_bmap(inode, block));
		if (!bh) {
			pr_err("%s(): failed to read dir block %d\n",
			       __func__, block);
			return 0;
		}
    
		dirblock = (struct efs_dir *) bh->b_data;

		if (be16_to_cpu(dirblock->magic) != EFS_DIRBLK_MAGIC) {
			pr_err("%s(): invalid directory block\n", __func__);
			brelse(bh);
			return(0);
		}

		for(slot = 0; slot < dirblock->slots; slot++) {
			dirslot  = (struct efs_dentry *) (((char *) bh->b_data) + EFS_SLOTAT(dirblock, slot));

			namelen  = dirslot->namelen;
			nameptr  = dirslot->name;

			if ((namelen == len) && (!memcmp(name, nameptr, len))) {
				inodenum = be32_to_cpu(dirslot->inode);
				brelse(bh);
				return(inodenum);
			}
		}
		brelse(bh);
	}
	return(0);
}

struct dentry *efs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	efs_ino_t inodenum;
	struct inode *inode = NULL;

	inodenum = efs_find_entry(dir, dentry->d_name.name, dentry->d_name.len);
	if (inodenum)
		inode = efs_iget(dir->i_sb, inodenum);

	return d_splice_alias(inode, dentry);
}

static struct inode *efs_nfs_get_inode(struct super_block *sb, u64 ino,
		u32 generation)
{
	struct inode *inode;

	if (ino == 0)
		return ERR_PTR(-ESTALE);
	inode = efs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	if (generation && inode->i_generation != generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	return inode;
}

struct dentry *efs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    efs_nfs_get_inode);
}

struct dentry *efs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    efs_nfs_get_inode);
}

struct dentry *efs_get_parent(struct dentry *child)
{
	struct dentry *parent = ERR_PTR(-ENOENT);
	efs_ino_t ino;

	ino = efs_find_entry(child->d_inode, "..", 2);
	if (ino)
		parent = d_obtain_alias(efs_iget(child->d_inode->i_sb, ino));

	return parent;
}
