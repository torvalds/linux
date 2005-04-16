/*
 *  smb_fs_i.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_FS_I
#define _LINUX_SMB_FS_I

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/fs.h>

/*
 * smb fs inode data (in memory only)
 */
struct smb_inode_info {

	/*
	 * file handles are local to a connection. A file is open if
	 * (open == generation).
	 */
        unsigned int open;	/* open generation */
	__u16 fileid;		/* What id to handle a file with? */
	__u16 attr;		/* Attribute fields, DOS value */

	__u16 access;		/* Access mode */
	__u16 flags;
	unsigned long oldmtime;	/* last time refreshed */
	unsigned long closed;	/* timestamp when closed */
	unsigned openers;	/* number of fileid users */

	struct inode vfs_inode;	/* must be at the end */
};

#endif
#endif
