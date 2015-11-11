/*
 *   Copyright (C) International Business Machines Corp., 2000-2002
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/posix_acl.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_inode.h"
#include "jfs_dmap.h"
#include "jfs_txnmgr.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"
#include "jfs_debug.h"

int jfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file->f_mapping->host;
	int rc = 0;

	rc = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (rc)
		return rc;

	mutex_lock(&inode->i_mutex);
	if (!(inode->i_state & I_DIRTY_ALL) ||
	    (datasync && !(inode->i_state & I_DIRTY_DATASYNC))) {
		/* Make sure committed changes hit the disk */
		jfs_flush_journal(JFS_SBI(inode->i_sb)->log, 1);
		mutex_unlock(&inode->i_mutex);
		return rc;
	}

	rc |= jfs_commit_inode(inode, 1);
	mutex_unlock(&inode->i_mutex);

	return rc ? -EIO : 0;
}

static int jfs_open(struct inode *inode, struct file *file)
{
	int rc;

	if ((rc = dquot_file_open(inode, file)))
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
	if (S_ISREG(inode->i_mode) && file->f_mode & FMODE_WRITE &&
	    (inode->i_size == 0)) {
		struct jfs_inode_info *ji = JFS_IP(inode);
		spin_lock_irq(&ji->ag_lock);
		if (ji->active_ag == -1) {
			struct jfs_sb_info *jfs_sb = JFS_SBI(inode->i_sb);
			ji->active_ag = BLKTOAG(addressPXD(&ji->ixpxd), jfs_sb);
			atomic_inc(&jfs_sb->bmap->db_active[ji->active_ag]);
		}
		spin_unlock_irq(&ji->ag_lock);
	}

	return 0;
}
static int jfs_release(struct inode *inode, struct file *file)
{
	struct jfs_inode_info *ji = JFS_IP(inode);

	spin_lock_irq(&ji->ag_lock);
	if (ji->active_ag != -1) {
		struct bmap *bmap = JFS_SBI(inode->i_sb)->bmap;
		atomic_dec(&bmap->db_active[ji->active_ag]);
		ji->active_ag = -1;
	}
	spin_unlock_irq(&ji->ag_lock);

	return 0;
}

int jfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	int rc;

	rc = inode_change_ok(inode, iattr);
	if (rc)
		return rc;

	if (is_quota_modification(inode, iattr)) {
		rc = dquot_initialize(inode);
		if (rc)
			return rc;
	}
	if ((iattr->ia_valid & ATTR_UID && !uid_eq(iattr->ia_uid, inode->i_uid)) ||
	    (iattr->ia_valid & ATTR_GID && !gid_eq(iattr->ia_gid, inode->i_gid))) {
		rc = dquot_transfer(inode, iattr);
		if (rc)
			return rc;
	}

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(inode)) {
		inode_dio_wait(inode);

		rc = inode_newsize_ok(inode, iattr->ia_size);
		if (rc)
			return rc;

		truncate_setsize(inode, iattr->ia_size);
		jfs_truncate(inode);
	}

	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);

	if (iattr->ia_valid & ATTR_MODE)
		rc = posix_acl_chmod(inode, inode->i_mode);
	return rc;
}

const struct inode_operations jfs_file_inode_operations = {
	.setxattr	= jfs_setxattr,
	.getxattr	= jfs_getxattr,
	.listxattr	= jfs_listxattr,
	.removexattr	= jfs_removexattr,
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
