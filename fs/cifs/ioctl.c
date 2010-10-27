/*
 *   fs/cifs/ioctl.c
 *
 *   vfs operations that deal with io control
 *
 *   Copyright (C) International Business Machines  Corp., 2005,2007
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
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifsfs.h"

#define CIFS_IOC_CHECKUMOUNT _IO(0xCF, 2)

long cifs_ioctl(struct file *filep, unsigned int command, unsigned long arg)
{
	struct inode *inode = filep->f_dentry->d_inode;
	int rc = -ENOTTY; /* strange error - but the precedent */
	int xid;
	struct cifs_sb_info *cifs_sb;
#ifdef CONFIG_CIFS_POSIX
	__u64	ExtAttrBits = 0;
	__u64	ExtAttrMask = 0;
	__u64   caps;
	struct cifsTconInfo *tcon;
	struct cifsFileInfo *pSMBFile = filep->private_data;
#endif /* CONFIG_CIFS_POSIX */

	xid = GetXid();

	cFYI(1, "ioctl file %p  cmd %u  arg %lu", filep, command, arg);

	cifs_sb = CIFS_SB(inode->i_sb);

#ifdef CONFIG_CIFS_POSIX
	tcon = cifs_sb->tcon;
	if (tcon)
		caps = le64_to_cpu(tcon->fsUnixInfo.Capability);
	else {
		rc = -EIO;
		FreeXid(xid);
		return -EIO;
	}
#endif /* CONFIG_CIFS_POSIX */

	switch (command) {
		case CIFS_IOC_CHECKUMOUNT:
			cFYI(1, "User unmount attempted");
			if (cifs_sb->mnt_uid == current_uid())
				rc = 0;
			else {
				rc = -EACCES;
				cFYI(1, "uids do not match");
			}
			break;
#ifdef CONFIG_CIFS_POSIX
		case FS_IOC_GETFLAGS:
			if (CIFS_UNIX_EXTATTR_CAP & caps) {
				if (pSMBFile == NULL)
					break;
				rc = CIFSGetExtAttr(xid, tcon, pSMBFile->netfid,
					&ExtAttrBits, &ExtAttrMask);
				if (rc == 0)
					rc = put_user(ExtAttrBits &
						FS_FL_USER_VISIBLE,
						(int __user *)arg);
			}
			break;

		case FS_IOC_SETFLAGS:
			if (CIFS_UNIX_EXTATTR_CAP & caps) {
				if (get_user(ExtAttrBits, (int __user *)arg)) {
					rc = -EFAULT;
					break;
				}
				if (pSMBFile == NULL)
					break;
				/* rc= CIFSGetExtAttr(xid,tcon,pSMBFile->netfid,
					extAttrBits, &ExtAttrMask);*/
			}
			cFYI(1, "set flags not implemented yet");
			break;
#endif /* CONFIG_CIFS_POSIX */
		default:
			cFYI(1, "unsupported ioctl");
			break;
	}

	FreeXid(xid);
	return rc;
}
