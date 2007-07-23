/*
 * namei.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/efs_fs.h>
#include <linux/smp_lock.h>

static efs_ino_t efs_find_entry(struct inode *inode, const char *name, int len) {
	struct buffer_head *bh;

	int			slot, namelen;
	char			*nameptr;
	struct efs_dir		*dirblock;
	struct efs_dentry	*dirslot;
	efs_ino_t		inodenum;
	efs_block_t		block;
 
	if (inode->i_size & (EFS_DIRBSIZE-1))
		printk(KERN_WARNING "EFS: WARNING: find_entry(): directory size not a multiple of EFS_DIRBSIZE\n");

	for(block = 0; block < inode->i_blocks; block++) {

		bh = sb_bread(inode->i_sb, efs_bmap(inode, block));
		if (!bh) {
			printk(KERN_ERR "EFS: find_entry(): failed to read dir block %d\n", block);
			return 0;
		}
    
		dirblock = (struct efs_dir *) bh->b_data;

		if (be16_to_cpu(dirblock->magic) != EFS_DIRBLK_MAGIC) {
			printk(KERN_ERR "EFS: find_entry(): invalid directory block\n");
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

struct dentry *efs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd) {
	efs_ino_t inodenum;
	struct inode * inode = NULL;

	lock_kernel();
	inodenum = efs_find_entry(dir, dentry->d_name.name, dentry->d_name.len);
	if (inodenum) {
		if (!(inode = iget(dir->i_sb, inodenum))) {
			unlock_kernel();
			return ERR_PTR(-EACCES);
		}
	}
	unlock_kernel();

	d_add(dentry, inode);
	return NULL;
}

struct dentry *efs_get_dentry(struct super_block *sb, void *vobjp)
{
	__u32 *objp = vobjp;
	unsigned long ino = objp[0];
	__u32 generation = objp[1];
	struct inode *inode;
	struct dentry *result;

	if (ino == 0)
		return ERR_PTR(-ESTALE);
	inode = iget(sb, ino);
	if (inode == NULL)
		return ERR_PTR(-ENOMEM);

	if (is_bad_inode(inode) ||
	    (generation && inode->i_generation != generation)) {
	    	result = ERR_PTR(-ESTALE);
		goto out_iput;
	}

	result = d_alloc_anon(inode);
	if (!result) {
		result = ERR_PTR(-ENOMEM);
		goto out_iput;
	}
	return result;

 out_iput:
	iput(inode);
	return result;
}

struct dentry *efs_get_parent(struct dentry *child)
{
	struct dentry *parent;
	struct inode *inode;
	efs_ino_t ino;
	int error;

	lock_kernel();

	error = -ENOENT;
	ino = efs_find_entry(child->d_inode, "..", 2);
	if (!ino)
		goto fail;

	error = -EACCES;
	inode = iget(child->d_inode->i_sb, ino);
	if (!inode)
		goto fail;

	error = -ENOMEM;
	parent = d_alloc_anon(inode);
	if (!parent)
		goto fail_iput;

	unlock_kernel();
	return parent;

 fail_iput:
	iput(inode);
 fail:
	unlock_kernel();
	return ERR_PTR(error);
}
