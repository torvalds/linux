/*
 *   fs/cifs/cifs_fs_sb.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2004
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
 */
#ifndef _CIFS_FS_SB_H
#define _CIFS_FS_SB_H

#define CIFS_MOUNT_NO_PERM      1 /* do not do client vfs_perm check */
#define CIFS_MOUNT_SET_UID      2 /* set current->euid in create etc. */
#define CIFS_MOUNT_SERVER_INUM  4 /* inode numbers from uniqueid from server */
#define CIFS_MOUNT_DIRECT_IO    8 /* do not write nor read through page cache */
#define CIFS_MOUNT_NO_XATTR  0x10 /* if set - disable xattr support */
#define CIFS_MOUNT_MAP_SPECIAL_CHR 0x20 /* remap illegal chars in filenames */

struct cifs_sb_info {
	struct cifsTconInfo *tcon;	/* primary mount */
	struct list_head nested_tcon_q;
	struct nls_table *local_nls;
	unsigned int rsize;
	unsigned int wsize;
	uid_t	mnt_uid;
	gid_t	mnt_gid;
	mode_t	mnt_file_mode;
	mode_t	mnt_dir_mode;
	int     mnt_cifs_flags;
};
#endif				/* _CIFS_FS_SB_H */
