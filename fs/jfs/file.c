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
#include "jfs_iyesde.h"
#include "jfs_dmap.h"
#include "jfs_txnmgr.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"
#include "jfs_debug.h"

int jfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct iyesde *iyesde = file->f_mapping->host;
	int rc = 0;

	rc = file_write_and_wait_range(file, start, end);
	if (rc)
		return rc;

	iyesde_lock(iyesde);
	if (!(iyesde->i_state & I_DIRTY_ALL) ||
	    (datasync && !(iyesde->i_state & I_DIRTY_DATASYNC))) {
		/* Make sure committed changes hit the disk */
		jfs_flush_journal(JFS_SBI(iyesde->i_sb)->log, 1);
		iyesde_unlock(iyesde);
		return rc;
	}

	rc |= jfs_commit_iyesde(iyesde, 1);
	iyesde_unlock(iyesde);

	return rc ? -EIO : 0;
}

static int jfs_open(struct iyesde *iyesde, struct file *file)
{
	int rc;

	if ((rc = dquot_file_open(iyesde, file)))
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
	if (S_ISREG(iyesde->i_mode) && file->f_mode & FMODE_WRITE &&
	    (iyesde->i_size == 0)) {
		struct jfs_iyesde_info *ji = JFS_IP(iyesde);
		spin_lock_irq(&ji->ag_lock);
		if (ji->active_ag == -1) {
			struct jfs_sb_info *jfs_sb = JFS_SBI(iyesde->i_sb);
			ji->active_ag = BLKTOAG(addressPXD(&ji->ixpxd), jfs_sb);
			atomic_inc(&jfs_sb->bmap->db_active[ji->active_ag]);
		}
		spin_unlock_irq(&ji->ag_lock);
	}

	return 0;
}
static int jfs_release(struct iyesde *iyesde, struct file *file)
{
	struct jfs_iyesde_info *ji = JFS_IP(iyesde);

	spin_lock_irq(&ji->ag_lock);
	if (ji->active_ag != -1) {
		struct bmap *bmap = JFS_SBI(iyesde->i_sb)->bmap;
		atomic_dec(&bmap->db_active[ji->active_ag]);
		ji->active_ag = -1;
	}
	spin_unlock_irq(&ji->ag_lock);

	return 0;
}

int jfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int rc;

	rc = setattr_prepare(dentry, iattr);
	if (rc)
		return rc;

	if (is_quota_modification(iyesde, iattr)) {
		rc = dquot_initialize(iyesde);
		if (rc)
			return rc;
	}
	if ((iattr->ia_valid & ATTR_UID && !uid_eq(iattr->ia_uid, iyesde->i_uid)) ||
	    (iattr->ia_valid & ATTR_GID && !gid_eq(iattr->ia_gid, iyesde->i_gid))) {
		rc = dquot_transfer(iyesde, iattr);
		if (rc)
			return rc;
	}

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(iyesde)) {
		iyesde_dio_wait(iyesde);

		rc = iyesde_newsize_ok(iyesde, iattr->ia_size);
		if (rc)
			return rc;

		truncate_setsize(iyesde, iattr->ia_size);
		jfs_truncate(iyesde);
	}

	setattr_copy(iyesde, iattr);
	mark_iyesde_dirty(iyesde);

	if (iattr->ia_valid & ATTR_MODE)
		rc = posix_acl_chmod(iyesde, iyesde->i_mode);
	return rc;
}

const struct iyesde_operations jfs_file_iyesde_operations = {
	.listxattr	= jfs_listxattr,
	.setattr	= jfs_setattr,
#ifdef CONFIG_JFS_POSIX_ACL
	.get_acl	= jfs_get_acl,
	.set_acl	= jfs_set_acl,
#endif
};

const struct file_operations jfs_file_operations = {
	.open		= jfs_open,
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fsync		= jfs_fsync,
	.release	= jfs_release,
	.unlocked_ioctl = jfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= jfs_compat_ioctl,
#endif
};
