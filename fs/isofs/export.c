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
 *     Documentation/filesystems/Exporting
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
	struct dentry *result;
	if (block == 0)
		return ERR_PTR(-ESTALE);
	inode = isofs_iget(sb, block, offset);
	if (inode == NULL)
		return ERR_PTR(-ENOMEM);
	if (is_bad_inode(inode)
	    || (generation && inode->i_generation != generation))
	{
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	result = d_alloc_anon(inode);
	if (!result) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	return result;
}

static struct dentry *
isofs_export_get_dentry(struct super_block *sb, void *vobjp)
{
	__u32 *objp = vobjp;
	unsigned long block = objp[0];
	unsigned long offset = objp[1];
	__u32 generation = objp[2];
	return isofs_export_iget(sb, block, offset, generation);
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
	struct inode *child_inode = child->d_inode;
	struct iso_inode_info *e_child_inode = ISOFS_I(child_inode);
	struct inode *parent_inode = NULL;
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

	/* Get the inode. */
	parent_inode = isofs_iget(child_inode->i_sb,
				  parent_block,
				  parent_offset);
	if (parent_inode == NULL) {
		rv = ERR_PTR(-EACCES);
		goto out;
	}

	/* Allocate the dentry. */
	rv = d_alloc_anon(parent_inode);
	if (rv == NULL) {
		rv = ERR_PTR(-ENOMEM);
		goto out;
	}

 out:
	if (bh) {
		brelse(bh);
	}
	return rv;
}

static int
isofs_export_encode_fh(struct dentry *dentry,
		       __u32 *fh32,
		       int *max_len,
		       int connectable)
{
	struct inode * inode = dentry->d_inode;
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

	if (len < 3 || (connectable && len < 5))
		return 255;

	len = 3;
	fh32[0] = ei->i_iget5_block;
 	fh16[2] = (__u16)ei->i_iget5_offset;  /* fh16 [sic] */
	fh32[2] = inode->i_generation;
	if (connectable && !S_ISDIR(inode->i_mode)) {
		struct inode *parent;
		struct iso_inode_info *eparent;
		spin_lock(&dentry->d_lock);
		parent = dentry->d_parent->d_inode;
		eparent = ISOFS_I(parent);
		fh32[3] = eparent->i_iget5_block;
		fh16[3] = (__u16)eparent->i_iget5_offset;  /* fh16 [sic] */
		fh32[4] = parent->i_generation;
		spin_unlock(&dentry->d_lock);
		len = 5;
		type = 2;
	}
	*max_len = len;
	return type;
}


static struct dentry *
isofs_export_decode_fh(struct super_block *sb,
		       __u32 *fh32,
		       int fh_len,
		       int fileid_type,
		       int (*acceptable)(void *context, struct dentry *de),
		       void *context)
{
	__u16 *fh16 = (__u16*)fh32;
	__u32 child[3];   /* The child is what triggered all this. */
	__u32 parent[3];  /* The parent is just along for the ride. */

	if (fh_len < 3 || fileid_type > 2)
		return NULL;

	child[0] = fh32[0];
	child[1] = fh16[2];  /* fh16 [sic] */
	child[2] = fh32[2];

	parent[0] = 0;
	parent[1] = 0;
	parent[2] = 0;
	if (fileid_type == 2) {
		if (fh_len > 2) parent[0] = fh32[3];
		parent[1] = fh16[3];  /* fh16 [sic] */
		if (fh_len > 4) parent[2] = fh32[4];
	}

	return sb->s_export_op->find_exported_dentry(sb, child, parent,
						     acceptable, context);
}


struct export_operations isofs_export_ops = {
	.decode_fh	= isofs_export_decode_fh,
	.encode_fh	= isofs_export_encode_fh,
	.get_dentry	= isofs_export_get_dentry,
	.get_parent     = isofs_export_get_parent,
};
