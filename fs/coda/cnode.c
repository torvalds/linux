// SPDX-License-Identifier: GPL-2.0
/* cnode related routines for the coda kernel code
   (C) 1996 Peter Braam
   */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/time.h>

#include <linux/coda.h>
#include <linux/pagemap.h>
#include "coda_psdev.h"
#include "coda_linux.h"

static inline int coda_fideq(struct CodaFid *fid1, struct CodaFid *fid2)
{
	return memcmp(fid1, fid2, sizeof(*fid1)) == 0;
}

static const struct inode_operations coda_symlink_inode_operations = {
	.get_link	= page_get_link,
	.setattr	= coda_setattr,
};

/* cnode.c */
static void coda_fill_inode(struct inode *inode, struct coda_vattr *attr)
{
        coda_vattr_to_iattr(inode, attr);

        if (S_ISREG(inode->i_mode)) {
                inode->i_op = &coda_file_inode_operations;
                inode->i_fop = &coda_file_operations;
        } else if (S_ISDIR(inode->i_mode)) {
                inode->i_op = &coda_dir_inode_operations;
                inode->i_fop = &coda_dir_operations;
        } else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &coda_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_data.a_ops = &coda_symlink_aops;
		inode->i_mapping = &inode->i_data;
	} else
                init_special_inode(inode, inode->i_mode, huge_decode_dev(attr->va_rdev));
}

static int coda_test_inode(struct inode *inode, void *data)
{
	struct CodaFid *fid = (struct CodaFid *)data;
	struct coda_inode_info *cii = ITOC(inode);
	return coda_fideq(&cii->c_fid, fid);
}

static int coda_set_inode(struct inode *inode, void *data)
{
	struct CodaFid *fid = (struct CodaFid *)data;
	struct coda_inode_info *cii = ITOC(inode);
	cii->c_fid = *fid;
	return 0;
}

struct inode * coda_iget(struct super_block * sb, struct CodaFid * fid,
			 struct coda_vattr * attr)
{
	struct inode *inode;
	struct coda_inode_info *cii;
	unsigned long hash = coda_f2i(fid);
	umode_t inode_type = coda_inode_type(attr);

retry:
	inode = iget5_locked(sb, hash, coda_test_inode, coda_set_inode, fid);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		cii = ITOC(inode);
		/* we still need to set i_ino for things like stat(2) */
		inode->i_ino = hash;
		/* inode is locked and unique, no need to grab cii->c_lock */
		cii->c_mapcount = 0;
		coda_fill_inode(inode, attr);
		unlock_new_inode(inode);
	} else if ((inode->i_mode & S_IFMT) != inode_type) {
		/* Inode has changed type, mark bad and grab a new one */
		remove_inode_hash(inode);
		coda_flag_inode(inode, C_PURGE);
		iput(inode);
		goto retry;
	}
	return inode;
}

/* this is effectively coda_iget:
   - get attributes (might be cached)
   - get the inode for the fid using vfs iget
   - link the two up if this is needed
   - fill in the attributes
*/
struct inode *coda_cnode_make(struct CodaFid *fid, struct super_block *sb)
{
        struct coda_vattr attr;
	struct inode *inode;
        int error;
        
	/* We get inode numbers from Venus -- see venus source */
	error = venus_getattr(sb, fid, &attr);
	if (error)
		return ERR_PTR(error);

	inode = coda_iget(sb, fid, &attr);
	if (IS_ERR(inode))
		pr_warn("%s: coda_iget failed\n", __func__);
	return inode;
}


/* Although we treat Coda file identifiers as immutable, there is one
 * special case for files created during a disconnection where they may
 * not be globally unique. When an identifier collision is detected we
 * first try to flush the cached inode from the kernel and finally
 * resort to renaming/rehashing in-place. Userspace remembers both old
 * and new values of the identifier to handle any in-flight upcalls.
 * The real solution is to use globally unique UUIDs as identifiers, but
 * retrofitting the existing userspace code for this is non-trivial. */
void coda_replace_fid(struct inode *inode, struct CodaFid *oldfid, 
		      struct CodaFid *newfid)
{
	struct coda_inode_info *cii = ITOC(inode);
	unsigned long hash = coda_f2i(newfid);
	
	BUG_ON(!coda_fideq(&cii->c_fid, oldfid));

	/* replace fid and rehash inode */
	/* XXX we probably need to hold some lock here! */
	remove_inode_hash(inode);
	cii->c_fid = *newfid;
	inode->i_ino = hash;
	__insert_inode_hash(inode, hash);
}

/* convert a fid to an inode. */
struct inode *coda_fid_to_inode(struct CodaFid *fid, struct super_block *sb) 
{
	struct inode *inode;
	unsigned long hash = coda_f2i(fid);

	inode = ilookup5(sb, hash, coda_test_inode, fid);
	if ( !inode )
		return NULL;

	/* we should never see newly created inodes because we intentionally
	 * fail in the initialization callback */
	BUG_ON(inode->i_state & I_NEW);

	return inode;
}

struct coda_file_info *coda_ftoc(struct file *file)
{
	struct coda_file_info *cfi = file->private_data;

	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);

	return cfi;

}

/* the CONTROL inode is made without asking attributes from Venus */
struct inode *coda_cnode_makectl(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	if (inode) {
		inode->i_ino = CTL_INO;
		inode->i_op = &coda_ioctl_inode_operations;
		inode->i_fop = &coda_ioctl_operations;
		inode->i_mode = 0444;
		return inode;
	}
	return ERR_PTR(-ENOMEM);
}

