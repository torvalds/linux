/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_CLNT_H__
#define __XFS_CLNT_H__

/*
 * XFS arguments structure, constructed from the arguments we
 * are passed via the mount system call.
 *
 * NOTE: The mount system call is handled differently between
 * Linux and IRIX.  In IRIX we worked work with a binary data
 * structure coming in across the syscall interface from user
 * space (the mount userspace knows about each filesystem type
 * and the set of valid options for it, and converts the users
 * argument string into a binary structure _before_ making the
 * system call), and the ABI issues that this implies.
 *
 * In Linux, we are passed a comma separated set of options;
 * ie. a NULL terminated string of characters.  Userspace mount
 * code does not have any knowledge of mount options expected by
 * each filesystem type and so each filesystem parses its mount
 * options in kernel space.
 *
 * For the Linux port, we kept this structure pretty much intact
 * and use it internally (because the existing code groks it).
 */
struct xfs_mount_args {
	int	flags;		/* flags -> see XFSMNT_... macros below */
	int	logbufs;	/* Number of log buffers, -1 to default */
	int	logbufsize;	/* Size of log buffers, -1 to default */
	char	fsname[MAXNAMELEN+1];	/* data device name */
	char	rtname[MAXNAMELEN+1];	/* realtime device filename */
	char	logname[MAXNAMELEN+1];	/* journal device filename */
	char	mtpt[MAXNAMELEN+1];	/* filesystem mount point */
	int	sunit;		/* stripe unit (BBs) */
	int	swidth;		/* stripe width (BBs), multiple of sunit */
	uchar_t iosizelog;	/* log2 of the preferred I/O size */
	int	ihashsize;	/* inode hash table size (buckets) */
};

/*
 * XFS mount option flags
 */
#define	XFSMNT_CHKLOG		0x00000001	/* check log */
#define	XFSMNT_WSYNC		0x00000002	/* safe mode nfs mount
						 * compatible */
#define	XFSMNT_INO64		0x00000004	/* move inode numbers up
						 * past 2^32 */
#define XFSMNT_UQUOTA		0x00000008	/* user quota accounting */
#define XFSMNT_PQUOTA		0x00000010	/* IRIX prj quota accounting */
#define XFSMNT_UQUOTAENF	0x00000020	/* user quota limit
						 * enforcement */
#define XFSMNT_PQUOTAENF	0x00000040	/* IRIX project quota limit
						 * enforcement */
#define XFSMNT_NOATIME		0x00000100	/* don't modify access
						 * times on reads */
#define XFSMNT_NOALIGN		0x00000200	/* don't allocate at
						 * stripe boundaries*/
#define XFSMNT_RETERR		0x00000400	/* return error to user */
#define XFSMNT_NORECOVERY	0x00000800	/* no recovery, implies
						 * read-only mount */
#define XFSMNT_SHARED		0x00001000	/* shared XFS mount */
#define XFSMNT_IOSIZE		0x00002000	/* optimize for I/O size */
#define XFSMNT_OSYNCISOSYNC	0x00004000	/* o_sync is REALLY o_sync */
						/* (osyncisdsync is now default) */
#define XFSMNT_32BITINODES	0x00200000	/* restrict inodes to 32
						 * bits of address space */
#define XFSMNT_GQUOTA		0x00400000	/* group quota accounting */
#define XFSMNT_GQUOTAENF	0x00800000	/* group quota limit
						 * enforcement */
#define XFSMNT_NOUUID		0x01000000	/* Ignore fs uuid */
#define XFSMNT_DMAPI		0x02000000	/* enable dmapi/xdsm */
#define XFSMNT_NOLOGFLUSH	0x04000000	/* Don't flush for log blocks */
#define XFSMNT_IDELETE		0x08000000	/* inode cluster delete */
#define XFSMNT_SWALLOC		0x10000000	/* turn on stripe width
						 * allocation */
#define XFSMNT_IHASHSIZE	0x20000000	/* inode hash table size */
#define XFSMNT_DIRSYNC		0x40000000	/* sync creat,link,unlink,rename
						 * symlink,mkdir,rmdir,mknod */

#endif	/* __XFS_CLNT_H__ */
