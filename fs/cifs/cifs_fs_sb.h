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
#define CIFS_MOUNT_SET_UID      2 /* set current's euid in create etc. */
#define CIFS_MOUNT_SERVER_INUM  4 /* inode numbers from uniqueid from server  */
#define CIFS_MOUNT_DIRECT_IO    8 /* do not write nor read through page cache */
#define CIFS_MOUNT_NO_XATTR     0x10  /* if set - disable xattr support       */
#define CIFS_MOUNT_MAP_SPECIAL_CHR 0x20 /* remap illegal chars in filenames   */
#define CIFS_MOUNT_POSIX_PATHS  0x40  /* Negotiate posix pathnames if possible*/
#define CIFS_MOUNT_UNX_EMUL     0x80  /* Network compat with SFUnix emulation */
#define CIFS_MOUNT_NO_BRL       0x100 /* No sending byte range locks to srv   */
#define CIFS_MOUNT_CIFS_ACL     0x200 /* send ACL requests to non-POSIX srv   */
#define CIFS_MOUNT_OVERR_UID    0x400 /* override uid returned from server    */
#define CIFS_MOUNT_OVERR_GID    0x800 /* override gid returned from server    */
#define CIFS_MOUNT_DYNPERM      0x1000 /* allow in-memory only mode setting   */
#define CIFS_MOUNT_NOPOSIXBRL   0x2000 /* mandatory not posix byte range lock */
#define CIFS_MOUNT_NOSSYNC      0x4000 /* don't do slow SMBflush on every sync*/

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
	int	prepathlen;
	char   *prepath; /* relative path under the share to mount to */
#ifdef CONFIG_CIFS_DFS_UPCALL
	char   *mountdata; /* mount options received at mount time */
#endif
};
#endif				/* _CIFS_FS_SB_H */
