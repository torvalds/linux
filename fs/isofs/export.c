// SPDX-License-Identifier: GPL-2.0
/*
 * fs/isofs/export.c
 *
 *  (C) 2004  Paul Serice - The new inode scheme requires switching
 *                          from iget() to iget5_locked() which means
 *                          the NFS export operations have to be hand
 *                          coded because the default routines rely on
 *                          iget().
 *
 * The following files are helpful:
 *
 *     Documentation/filesystems/nfs/exporting.rst
 *     fs/exportfs/expfs.c.
 */

#include "isofs.h"

static struct dentry *
isofs_export_iget(struct super_block *sb,
		  unsigned long block,
		  unsigned long offset,
		  __u32 generation)
{
	struct inode *inode;

	if (block == 0)
		return ERR_PTR(-ESTALE);
	inode = isofs_iget(sb, block, offset);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (generation && inode->i_generation != generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return d_obtain_alias(inode);
}

/* This function is surprisingly simple.  The trick is understanding
 * that "child" is always a directory. So, to find its parent, you
 * simply need to find its ".." entry, normalize its block and offset,
 * and return the underlying inode.  See the comments for
 * isofs_normalize_block_and_offset(). */
static struct dentry *isofs_export_get_parent(struct dentry *child)
{
	unsigned long parent_block = 0;
	unsigned long parent_offset = 0;
	struct inode *child_inode = d_inode(child);
	struct iso_inode_info *e_child_inode = ISOFS_I(child_inode);
	struct iso_directory_record *de = NULL;
	struct buffer_head * bh = NULL;
	struct dentry *rv = NULL;

	/* "child" must always be a directory. */
	if (!S_ISDIR(child_inode->i_mode)) {
		printk(KERN_ERR "isofs: isofs_export_get_parent(): "
		       "child is not a directory!\n");
		rv = ERR_PTR(-EACCES);
		goto out;
	}

	/* It is an invariant that the directory offset is zero.  If
	 * it is not zero, it means the directory failed to be
	 * normalized for some reason. */
	if (e_child_inode->i_iget5_offset != 0) {
		printk(KERN_ERR "isofs: isofs_export_get_parent(): "
		       "child directory not normalized!\n");
		rv = ERR_PTR(-EACCES);
		goto out;
	}

	/* The child inode has been normalized such that its
	 * i_iget5_block value points to the "." entry.  Fortunately,
	 * the ".." entry is located in the same block. */
	parent_block = e_child_inode->i_iget5_block;

	/* Get the block in question. */
	bh = sb_bread(child_inode->i_sb, parent_block);
	if (bh == NULL) {
		rv = ERR_PTR(-EACCES);
		goto out;
	}

	/* This is the "." entry. */
	de = (struct iso_directory_record*)bh->b_data;

	/* The ".." entry is always the second entry. */
	parent_offset = (unsigned long)isonum_711(de->length);
	de = (struct iso_directory_record*)(bh->b_data + parent_offset);

	/* Verify it is in fact the ".." entry. */
	if ((isonum_711(de->name_len) != 1) || (de->name[0] != 1)) {
		printk(KERN_ERR "isofs: Unable to find the \"..\" "
		       "directory for NFS.\n");
		rv = ERR_PTR(-EACCES);
		goto out;
	}

	/* Normalize */
	isofs_normalize_block_and_offset(de, &parent_block, &parent_offset);

	rv = d_obtain_alias(isofs_iget(child_inode->i_sb, parent_block,
				     parent_offset));
 out:
	if (bh)
		brelse(bh);
	return rv;
}

static int
isofs_export_encode_fh(struct inode *inode,
		       __u32 *fh32,
		       int *max_len,
		       struct inode *parent)
{
	struct iso_inode_info * ei = ISOFS_I(inode);
	int len = *max_len;
	int type = 1;
	__u16 *fh16 = (__u16*)fh32;

	/*
	 * WARNING: max_len is 5 for NFSv2.  Because of this
	 * limitation, we use the lower 16 bits of fh32[1] to hold the
	 * offset of the inode and the upper 16 bits of fh32[1] to
	 * hold the offset of the parent.
	 */
	if (parent && (len < 5)) {
		*max_len = 5;
		return FILEID_INVALID;
	} else if (len < 3) {
		*max_len = 3;
		return FILEID_INVALID;
	}

	len = 3;
	fh32[0] = ei->i_iget5_block;
 	fh16[2] = (__u16)ei->i_iget5_offset;  /* fh16 [sic] */
	fh16[3] = 0;  /* avoid leaking uninitialized data */
	fh32[2] = inode->i_generation;
	if (parent) {
		struct iso_inode_info *eparent;
		eparent = ISOFS_I(parent);
		fh32[3] = eparent->i_iget5_block;
		fh16[3] = (__u16)eparent->i_iget5_offset;  /* fh16 [sic] */
		fh32[4] = parent->i_generation;
		len = 5;
		type = 2;
	}
	*max_len = len;
	return type;
}

struct isofs_fid {
	u32 block;
	u16 offset;
	u16 parent_offset;
	u32 generation;
	u32 parent_block;
	u32 parent_generation;
};

static struct dentry *isofs_fh_to_dentry(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type)
{
	struct isofs_fid *ifid = (struct isofs_fid *)fid;

	if (fh_len < 3 || fh_type > 2)
		return NULL;

	return isofs_export_iget(sb, ifid->block, ifid->offset,
			ifid->generation);
}

static struct dentry *isofs_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct isofs_fid *ifid = (struct isofs_fid *)fid;

	if (fh_len < 2 || fh_type != 2)
		return NULL;

	return isofs_export_iget(sb,
			fh_len > 3 ? ifid->parent_block : 0,
			ifid->parent_offset,
			fh_len > 4 ? ifid->parent_generation : 0);
}

const struct export_operations isofs_export_ops = {
	.encode_fh	= isofs_export_encode_fh,
	.fh_to_dentry	= isofs_fh_to_dentry,
	.fh_to_parent	= isofs_fh_to_parent,
	.get_parent     = isofs_export_get_parent,
};
