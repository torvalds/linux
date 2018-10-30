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
#include <linux/rbtree.h>

#ifndef _CIFS_FS_SB_H
#define _CIFS_FS_SB_H

#include <linux/backing-dev.h>

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
#define CIFS_MOUNT_FSCACHE	0x8000 /* local caching enabled */
#define CIFS_MOUNT_MF_SYMLINKS	0x10000 /* Minshall+French Symlinks enabled */
#define CIFS_MOUNT_MULTIUSER	0x20000 /* multiuser mount */
#define CIFS_MOUNT_STRICT_IO	0x40000 /* strict cache mode */
#define CIFS_MOUNT_RWPIDFORWARD	0x80000 /* use pid forwarding for rw */
#define CIFS_MOUNT_POSIXACL	0x100000 /* mirror of SB_POSIXACL in mnt_cifs_flags */
#define CIFS_MOUNT_CIFS_BACKUPUID 0x200000 /* backup intent bit for a user */
#define CIFS_MOUNT_CIFS_BACKUPGID 0x400000 /* backup intent bit for a group */
#define CIFS_MOUNT_MAP_SFM_CHR	0x800000 /* SFM/MAC mapping for illegal chars */
#define CIFS_MOUNT_USE_PREFIX_PATH 0x1000000 /* make subpath with unaccessible
					      * root mountable
					      */
#define CIFS_MOUNT_UID_FROM_ACL 0x2000000 /* try to get UID via special SID */
#define CIFS_MOUNT_NO_HANDLE_CACHE 0x4000000 /* disable caching dir handles */
#define CIFS_MOUNT_NO_DFS 0x8000000 /* disable DFS resolving */

struct cifs_sb_info {
	struct rb_root tlink_tree;
	spinlock_t tlink_tree_lock;
	struct tcon_link *master_tlink;
	struct nls_table *local_nls;
	unsigned int rsize;
	unsigned int wsize;
	unsigned long actimeo; /* attribute cache timeout (jiffies) */
	atomic_t active;
	kuid_t	mnt_uid;
	kgid_t	mnt_gid;
	kuid_t	mnt_backupuid;
	kgid_t	mnt_backupgid;
	umode_t	mnt_file_mode;
	umode_t	mnt_dir_mode;
	unsigned int mnt_cifs_flags;
	char   *mountdata; /* options received at mount time or via DFS refs */
	struct delayed_work prune_tlinks;
	struct rcu_head rcu;
	char *prepath;
};
#endif				/* _CIFS_FS_SB_H */
