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
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifsfs.h"

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

	cifs_dbg(FYI, "ioctl file %p  cmd %u  arg %lu\n", filep, command, arg);

	cifs_sb = CIFS_SB(inode->i_sb);

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
		default:
			cifs_dbg(FYI, "unsupported ioctl\n");
			break;
	}

	free_xid(xid);
	return rc;
}
