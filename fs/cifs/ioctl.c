/*
 *   fs/cifs/ioctl.c
 *
 *   vfs operations that deal with io control
 *
 *   Copyright (C) International Business Machines  Corp., 2005,2013
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifsfs.h"
#include "cifs_ioctl.h"
#include <linux/btrfs.h>

static int cifs_file_clone_range(unsigned int xid, struct file *src_file,
			  struct file *dst_file)
{
	struct inode *src_inode = file_inode(src_file);
	struct inode *target_inode = file_inode(dst_file);
	struct cifsFileInfo *smb_file_src;
	struct cifsFileInfo *smb_file_target;
	struct cifs_tcon *src_tcon;
	struct cifs_tcon *target_tcon;
	int rc;

	cifs_dbg(FYI, "ioctl clone range\n");

	if (!src_file->private_data || !dst_file->private_data) {
		rc = -EBADF;
		cifs_dbg(VFS, "missing cifsFileInfo on copy range src file\n");
		goto out;
	}

	rc = -EXDEV;
	smb_file_target = dst_file->private_data;
	smb_file_src = src_file->private_data;
	src_tcon = tlink_tcon(smb_file_src->tlink);
	target_tcon = tlink_tcon(smb_file_target->tlink);

	if (src_tcon->ses != target_tcon->ses) {
		cifs_dbg(VFS, "source and target of copy not on same server\n");
		goto out;
	}

	/*
	 * Note: cifs case is easier than btrfs since server responsible for
	 * checks for proper open modes and file type and if it wants
	 * server could even support copy of range where source = target
	 */
	lock_two_nondirectories(target_inode, src_inode);

	cifs_dbg(FYI, "about to flush pages\n");
	/* should we flush first and last page first */
	truncate_inode_pages(&target_inode->i_data, 0);

	if (target_tcon->ses->server->ops->clone_range)
		rc = target_tcon->ses->server->ops->clone_range(xid,
			smb_file_src, smb_file_target, 0, src_inode->i_size, 0);
	else
		rc = -EOPNOTSUPP;

	/* force revalidate of size and timestamps of target file now
	   that target is updated on the server */
	CIFS_I(target_inode)->time = 0;
	/* although unlocking in the reverse order from locking is not
	   strictly necessary here it is a little cleaner to be consistent */
	unlock_two_nondirectories(src_inode, target_inode);
out:
	return rc;
}

static long cifs_ioctl_clone(unsigned int xid, struct file *dst_file,
			unsigned long srcfd)
{
	int rc;
	struct fd src_file;
	struct inode *src_inode;

	cifs_dbg(FYI, "ioctl clone range\n");
	/* the destination must be opened for writing */
	if (!(dst_file->f_mode & FMODE_WRITE)) {
		cifs_dbg(FYI, "file target not open for write\n");
		return -EINVAL;
	}

	/* check if target volume is readonly and take reference */
	rc = mnt_want_write_file(dst_file);
	if (rc) {
		cifs_dbg(FYI, "mnt_want_write failed with rc %d\n", rc);
		return rc;
	}

	src_file = fdget(srcfd);
	if (!src_file.file) {
		rc = -EBADF;
		goto out_drop_write;
	}

	if (src_file.file->f_op->unlocked_ioctl != cifs_ioctl) {
		rc = -EBADF;
		cifs_dbg(VFS, "src file seems to be from a different filesystem type\n");
		goto out_fput;
	}

	src_inode = file_inode(src_file.file);
	rc = -EINVAL;
	if (S_ISDIR(src_inode->i_mode))
		goto out_fput;

	rc = cifs_file_clone_range(xid, src_file.file, dst_file);

out_fput:
	fdput(src_file);
out_drop_write:
	mnt_drop_write_file(dst_file);
	return rc;
}

static long smb_mnt_get_fsinfo(unsigned int xid, struct cifs_tcon *tcon,
				void __user *arg)
{
	int rc = 0;
	struct smb_mnt_fs_info *fsinf;

	fsinf = kzalloc(sizeof(struct smb_mnt_fs_info), GFP_KERNEL);
	if (fsinf == NULL)
		return -ENOMEM;

	fsinf->version = 1;
	fsinf->protocol_id = tcon->ses->server->vals->protocol_id;
	fsinf->device_characteristics =
			le32_to_cpu(tcon->fsDevInfo.DeviceCharacteristics);
	fsinf->device_type = le32_to_cpu(tcon->fsDevInfo.DeviceType);
	fsinf->fs_attributes = le32_to_cpu(tcon->fsAttrInfo.Attributes);
	fsinf->max_path_component =
		le32_to_cpu(tcon->fsAttrInfo.MaxPathNameComponentLength);
#ifdef CONFIG_CIFS_SMB2
	fsinf->vol_serial_number = tcon->vol_serial_number;
	fsinf->vol_create_time = le64_to_cpu(tcon->vol_create_time);
	fsinf->share_flags = tcon->share_flags;
	fsinf->share_caps = le32_to_cpu(tcon->capabilities);
	fsinf->sector_flags = tcon->ss_flags;
	fsinf->optimal_sector_size = tcon->perf_sector_size;
	fsinf->max_bytes_chunk = tcon->max_bytes_chunk;
	fsinf->maximal_access = tcon->maximal_access;
#endif /* SMB2 */
	fsinf->cifs_posix_caps = le64_to_cpu(tcon->fsUnixInfo.Capability);

	if (copy_to_user(arg, fsinf, sizeof(struct smb_mnt_fs_info)))
		rc = -EFAULT;

	kfree(fsinf);
	return rc;
}

long cifs_ioctl(struct file *filep, unsigned int command, unsigned long arg)
{
	struct inode *inode = file_inode(filep);
	int rc = -ENOTTY; /* strange error - but the precedent */
	unsigned int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsFileInfo *pSMBFile = filep->private_data;
	struct cifs_tcon *tcon;
	__u64	ExtAttrBits = 0;
	__u64   caps;

	xid = get_xid();

	cifs_sb = CIFS_SB(inode->i_sb);
	cifs_dbg(FYI, "cifs ioctl 0x%x\n", command);
	switch (command) {
		case FS_IOC_GETFLAGS:
			if (pSMBFile == NULL)
				break;
			tcon = tlink_tcon(pSMBFile->tlink);
			caps = le64_to_cpu(tcon->fsUnixInfo.Capability);
#ifdef CONFIG_CIFS_POSIX
			if (CIFS_UNIX_EXTATTR_CAP & caps) {
				__u64	ExtAttrMask = 0;
				rc = CIFSGetExtAttr(xid, tcon,
						    pSMBFile->fid.netfid,
						    &ExtAttrBits, &ExtAttrMask);
				if (rc == 0)
					rc = put_user(ExtAttrBits &
						FS_FL_USER_VISIBLE,
						(int __user *)arg);
				if (rc != EOPNOTSUPP)
					break;
			}
#endif /* CONFIG_CIFS_POSIX */
			rc = 0;
			if (CIFS_I(inode)->cifsAttrs & ATTR_COMPRESSED) {
				/* add in the compressed bit */
				ExtAttrBits = FS_COMPR_FL;
				rc = put_user(ExtAttrBits & FS_FL_USER_VISIBLE,
					      (int __user *)arg);
			}
			break;
		case FS_IOC_SETFLAGS:
			if (pSMBFile == NULL)
				break;
			tcon = tlink_tcon(pSMBFile->tlink);
			caps = le64_to_cpu(tcon->fsUnixInfo.Capability);

			if (get_user(ExtAttrBits, (int __user *)arg)) {
				rc = -EFAULT;
				break;
			}

			/*
			 * if (CIFS_UNIX_EXTATTR_CAP & caps)
			 *	rc = CIFSSetExtAttr(xid, tcon,
			 *		       pSMBFile->fid.netfid,
			 *		       extAttrBits,
			 *		       &ExtAttrMask);
			 * if (rc != EOPNOTSUPP)
			 *	break;
			 */

			/* Currently only flag we can set is compressed flag */
			if ((ExtAttrBits & FS_COMPR_FL) == 0)
				break;

			/* Try to set compress flag */
			if (tcon->ses->server->ops->set_compression) {
				rc = tcon->ses->server->ops->set_compression(
							xid, tcon, pSMBFile);
				cifs_dbg(FYI, "set compress flag rc %d\n", rc);
			}
			break;
		case CIFS_IOC_COPYCHUNK_FILE:
			rc = cifs_ioctl_clone(xid, filep, arg);
			break;
		case CIFS_IOC_SET_INTEGRITY:
			if (pSMBFile == NULL)
				break;
			tcon = tlink_tcon(pSMBFile->tlink);
			if (tcon->ses->server->ops->set_integrity)
				rc = tcon->ses->server->ops->set_integrity(xid,
						tcon, pSMBFile);
			else
				rc = -EOPNOTSUPP;
			break;
		case CIFS_IOC_GET_MNT_INFO:
			if (pSMBFile == NULL)
				break;
			tcon = tlink_tcon(pSMBFile->tlink);
			rc = smb_mnt_get_fsinfo(xid, tcon, (void __user *)arg);
			break;
		case CIFS_ENUMERATE_SNAPSHOTS:
			if (pSMBFile == NULL)
				break;
			if (arg == 0) {
				rc = -EINVAL;
				goto cifs_ioc_exit;
			}
			tcon = tlink_tcon(pSMBFile->tlink);
			if (tcon->ses->server->ops->enum_snapshots)
				rc = tcon->ses->server->ops->enum_snapshots(xid, tcon,
						pSMBFile, (void __user *)arg);
			else
				rc = -EOPNOTSUPP;
			break;
		default:
			cifs_dbg(FYI, "unsupported ioctl\n");
			break;
	}
cifs_ioc_exit:
	free_xid(xid);
	return rc;
}
