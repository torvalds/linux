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
	cFYI(1,("ioctl file %p  cmd %u  arg %lu",filep,command,arg));
	switch(command) {
		case EXT2_IOC_GETFLAGS:
			cFYI(1,("get flags not implemented yet"));
			return -EOPNOTSUPP;
		case EXT2_IOC_SETFLAGS:
			cFYI(1,("set flags not implemented yet"));
			return -EOPNOTSUPP;
		default:
			cFYI(1,("unsupported ioctl"));
			return rc;
	}
#endif /* CONFIG_CIFS_POSIX */
	return rc;
} 
