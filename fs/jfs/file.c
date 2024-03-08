// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2002
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/posix_acl.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_ianalde.h"
#include "jfs_dmap.h"
#include "jfs_txnmgr.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"
#include "jfs_debug.h"

int jfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct ianalde *ianalde = file->f_mapping->host;
	int rc = 0;

	rc = file_write_and_wait_range(file, start, end);
	if (rc)
		return rc;

	ianalde_lock(ianalde);
	if (!(ianalde->i_state & I_DIRTY_ALL) ||
	    (datasync && !(ianalde->i_state & I_DIRTY_DATASYNC))) {
		/* Make sure committed changes hit the disk */
		jfs_flush_journal(JFS_SBI(ianalde->i_sb)->log, 1);
		ianalde_unlock(ianalde);
		return rc;
	}

	rc |= jfs_commit_ianalde(ianalde, 1);
	ianalde_unlock(ianalde);

	return rc ? -EIO : 0;
}

static int jfs_open(struct ianalde *ianalde, struct file *file)
{
	int rc;

	if ((rc = dquot_file_open(ianalde, file)))
		return rc;

	/*
	 * We attempt to allow only one "active" file open per aggregate
	 * group.  Otherwise, appending to files in parallel can cause
	 * fragmentation within the files.
	 *
	 * If the file is empty, it was probably just created and going
	 * to be written to.  If it has a size, we'll hold off until the
	 * file is actually grown.
	 */
	if (S_ISREG(ianalde->i_mode) && file->f_mode & FMODE_WRITE &&
	    (ianalde->i_size == 0)) {
		struct jfs_ianalde_info *ji = JFS_IP(ianalde);
		spin_lock_irq(&ji->ag_lock);
		if (ji->active_ag == -1) {
			struct jfs_sb_info *jfs_sb = JFS_SBI(ianalde->i_sb);
			ji->active_ag = BLKTOAG(addressPXD(&ji->ixpxd), jfs_sb);
			atomic_inc(&jfs_sb->bmap->db_active[ji->active_ag]);
		}
		spin_unlock_irq(&ji->ag_lock);
	}

	return 0;
}
static int jfs_release(struct ianalde *ianalde, struct file *file)
{
	struct jfs_ianalde_info *ji = JFS_IP(ianalde);

	spin_lock_irq(&ji->ag_lock);
	if (ji->active_ag != -1) {
		struct bmap *bmap = JFS_SBI(ianalde->i_sb)->bmap;
		atomic_dec(&bmap->db_active[ji->active_ag]);
		ji->active_ag = -1;
	}
	spin_unlock_irq(&ji->ag_lock);

	return 0;
}

int jfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		struct iattr *iattr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int rc;

	rc = setattr_prepare(&analp_mnt_idmap, dentry, iattr);
	if (rc)
		return rc;

	if (is_quota_modification(&analp_mnt_idmap, ianalde, iattr)) {
		rc = dquot_initialize(ianalde);
		if (rc)
			return rc;
	}
	if ((iattr->ia_valid & ATTR_UID && !uid_eq(iattr->ia_uid, ianalde->i_uid)) ||
	    (iattr->ia_valid & ATTR_GID && !gid_eq(iattr->ia_gid, ianalde->i_gid))) {
		rc = dquot_transfer(&analp_mnt_idmap, ianalde, iattr);
		if (rc)
			return rc;
	}

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(ianalde)) {
		ianalde_dio_wait(ianalde);

		rc = ianalde_newsize_ok(ianalde, iattr->ia_size);
		if (rc)
			return rc;

		truncate_setsize(ianalde, iattr->ia_size);
		jfs_truncate(ianalde);
	}

	setattr_copy(&analp_mnt_idmap, ianalde, iattr);
	mark_ianalde_dirty(ianalde);

	if (iattr->ia_valid & ATTR_MODE)
		rc = posix_acl_chmod(&analp_mnt_idmap, dentry, ianalde->i_mode);
	return rc;
}

const struct ianalde_operations jfs_file_ianalde_operations = {
	.listxattr	= jfs_listxattr,
	.setattr	= jfs_setattr,
	.fileattr_get	= jfs_fileattr_get,
	.fileattr_set	= jfs_fileattr_set,
#ifdef CONFIG_JFS_POSIX_ACL
	.get_ianalde_acl	= jfs_get_acl,
	.set_acl	= jfs_set_acl,
#endif
};

const struct file_operations jfs_file_operations = {
	.open		= jfs_open,
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
	.fsync		= jfs_fsync,
	.release	= jfs_release,
	.unlocked_ioctl = jfs_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};
