/*
 *   fs/cifs/ioctl.c
 *
 *   vfs operations that deal with io control
 *
 *   Copyright (C) International Business Machines  Corp., 2005
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
#include <linux/ext2_fs.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

int cifs_ioctl (struct inode * inode, struct file * filep, 
		unsigned int command, unsigned long arg)
{
	int rc = -ENOTTY; /* strange error - but the precedent */
#ifdef CONFIG_CIFS_POSIX
	__u64	ExtAttrBits = 0;
	__u64	ExtAttrMask = 0;
	__u64   caps;
#endif /* CONFIG_CIFS_POSIX */
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *tcon;
	struct cifsFileInfo *pSMBFile =
		(struct cifsFileInfo *)filep->private_data;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	tcon = cifs_sb->tcon;
	if (pSMBFile == NULL)
		goto cifs_ioctl_out;

#ifdef CONFIG_CIFS_POSIX
	if(tcon)
		caps = le64_to_cpu(tcon->fsUnixInfo.Capability);
	else {
		rc = -EIO;
		goto cifs_ioctl_out;
	}

	cFYI(1,("ioctl file %p  cmd %u  arg %lu",filep,command,arg));
	switch(command) {
		case EXT2_IOC_GETFLAGS:
			if(CIFS_UNIX_EXTATTR_CAP & caps) {
				rc = CIFSGetExtAttr(xid, tcon, pSMBFile->netfid,
					&ExtAttrBits, &ExtAttrMask);
				if(rc == 0)
					rc = put_user(ExtAttrBits &
						EXT2_FL_USER_VISIBLE,
						(int __user *)arg);
			}
			break;

		case EXT2_IOC_SETFLAGS:
			if(CIFS_UNIX_EXTATTR_CAP & caps) {
				if(get_user(ExtAttrBits,(int __user *)arg)) {
					rc = -EFAULT;
					goto cifs_ioctl_out;
				}
			  /* rc = CIFSGetExtAttr(xid, tcon, pSMBFile->netfid,
					extAttrBits, &ExtAttrMask);*/
				
			}
			cFYI(1,("set flags not implemented yet"));
			break;
		default:
			cFYI(1,("unsupported ioctl"));
			return rc;
	}
#endif /* CONFIG_CIFS_POSIX */

cifs_ioctl_out:
	FreeXid(xid);
	return rc;
} 
