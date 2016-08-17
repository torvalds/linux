/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 *	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *		All Rights Reserved
 */

#ifndef _SYS_MNTENT_H
#define	_SYS_MNTENT_H

#define	MNTTYPE_ZFS	"zfs"		/* ZFS file system */

#define	MOUNT_SUCCESS	0x00		/* Success */
#define	MOUNT_USAGE	0x01		/* Invalid invocation or permissions */
#define	MOUNT_SYSERR	0x02		/* System error (ENOMEM, etc) */
#define	MOUNT_SOFTWARE	0x04		/* Internal mount bug */
#define	MOUNT_USER	0x08		/* Interrupted by user (EINTR) */
#define	MOUNT_FILEIO	0x10		/* Error updating/locking /etc/mtab */
#define	MOUNT_FAIL	0x20		/* Mount failed */
#define	MOUNT_SOMEOK	0x40		/* At least on mount succeeded */
#define	MOUNT_BUSY	0x80		/* Mount failed due to EBUSY */

#define	MNTOPT_ASYNC	"async"		/* all I/O is asynchronous */
#define	MNTOPT_ATIME	"atime"		/* update atime for files */
#define	MNTOPT_NOATIME	"noatime"	/* do not update atime for files */
#define	MNTOPT_AUTO	"auto"		/* automount */
#define	MNTOPT_NOAUTO	"noauto"	/* do not automount */
#define	MNTOPT_CONTEXT	"context"	/* selinux context */
#define	MNTOPT_FSCONTEXT "fscontext"	/* selinux fscontext */
#define	MNTOPT_DEFCONTEXT "defcontext"	/* selinux defcontext */
#define	MNTOPT_ROOTCONTEXT "rootcontext" /* selinux rootcontext */
#define	MNTOPT_DEFAULTS	"defaults"	/* defaults */
#define	MNTOPT_DEVICES	"dev"		/* device-special allowed */
#define	MNTOPT_NODEVICES "nodev"	/* device-special disallowed */
#define	MNTOPT_DIRATIME	"diratime"	/* update atime for dirs */
#define	MNTOPT_NODIRATIME "nodiratime"	/* do not update atime for dirs */
#define	MNTOPT_DIRSYNC	"dirsync"	/* do dir updates synchronously */
#define	MNTOPT_EXEC	"exec"		/* enable executables */
#define	MNTOPT_NOEXEC	"noexec"	/* disable executables */
#define	MNTOPT_GROUP	"group"		/* allow group mount */
#define	MNTOPT_NOGROUP	"nogroup"	/* do not allow group mount */
#define	MNTOPT_IVERSION	"iversion"	/* update inode version */
#define	MNTOPT_NOIVERSION "noiversion"	/* do not update inode version */
#define	MNTOPT_NBMAND	"mand"		/* allow non-blocking mandatory locks */
#define	MNTOPT_NONBMAND	"nomand"	/* deny non-blocking mandatory locks */
#define	MNTOPT_NETDEV	"_netdev"	/* network device */
#define	MNTOPT_NOFAIL	"nofail"	/* no failure */
#define	MNTOPT_RELATIME	"relatime"	/* allow relative time updates */
#define	MNTOPT_NORELATIME "norelatime"	/* do not allow relative time updates */
#define	MNTOPT_STRICTATIME "strictatime" /* strict access time updates */
#define	MNTOPT_NOSTRICTATIME "nostrictatime" /* No strict access time updates */
#define	MNTOPT_LAZYTIME "lazytime"	/* Defer access time writing */
#define	MNTOPT_SETUID	"suid"		/* Both setuid and devices allowed */
#define	MNTOPT_NOSETUID	"nosuid"	/* Neither setuid nor devices allowed */
#define	MNTOPT_OWNER	"owner"		/* allow owner mount */
#define	MNTOPT_NOOWNER	"noowner"	/* do not allow owner mount */
#define	MNTOPT_REMOUNT	"remount"	/* change mount options */
#define	MNTOPT_RO	"ro"		/* read only */
#define	MNTOPT_RW	"rw"		/* read/write */
#define	MNTOPT_SYNC	"sync"		/* all I/O is synchronous */
#define	MNTOPT_USER	"user"		/* allow user mount */
#define	MNTOPT_NOUSER	"nouser"	/* do not allow user mount */
#define	MNTOPT_USERS	"users"		/* allow user mount */
#define	MNTOPT_NOUSERS	"nousers"	/* do not allow user mount */
#define	MNTOPT_SUB	"sub"		/* allow mounts on subdirs */
#define	MNTOPT_NOSUB	"nosub"		/* do not allow mounts on subdirs */
#define	MNTOPT_QUIET	"quiet"		/* quiet mount */
#define	MNTOPT_LOUD	"loud"		/* verbose mount */
#define	MNTOPT_BIND	"bind"		/* remount part of a tree */
#define	MNTOPT_RBIND	"rbind"		/* include subtrees */
#define	MNTOPT_DIRXATTR	"dirxattr"	/* enable directory xattrs */
#define	MNTOPT_SAXATTR	"saxattr"	/* enable system-attribute xattrs */
#define	MNTOPT_XATTR	"xattr"		/* enable extended attributes */
#define	MNTOPT_NOXATTR	"noxattr"	/* disable extended attributes */
#define	MNTOPT_COMMENT	"comment"	/* comment */
#define	MNTOPT_ZFSUTIL	"zfsutil"	/* called by zfs utility */
#define	MNTOPT_ACL	"acl"		/* passed by util-linux-2.24 mount */
#define	MNTOPT_NOACL	"noacl"		/* likewise */
#define	MNTOPT_POSIXACL	"posixacl"	/* likewise */
#define	MNTOPT_MNTPOINT	"mntpoint"	/* mount point hint */

#endif	/* _SYS_MNTENT_H */
