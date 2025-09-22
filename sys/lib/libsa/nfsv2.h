/*	$OpenBSD: nfsv2.h,v 1.6 2014/07/13 15:31:20 mpi Exp $	*/
/*	$NetBSD: nfsv2.h,v 1.2 1996/02/26 23:05:23 gwr Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)nfsv2.h	8.1 (Berkeley) 6/10/93
 */

/*
 * nfs definitions as per the version 2 specs
 */

/*
 * Constants as defined in the Sun NFS Version 2 spec.
 * "NFS: Network File System Protocol Specification" RFC1094
 */

#define NFS_PORT	2049
#define	NFS_PROG	100003
#define NFS_VER2	2
#define	NFS_MAXDGRAMDATA 8192
#define	NFS_MAXDATA	32768
#define	NFS_MAXPATHLEN	1024
#define	NFS_MAXNAMLEN	255
#define	NFS_FHSIZE	32
#define	NFS_MAXPKTHDR	404
#define NFS_MAXPACKET	(NFS_MAXPKTHDR+NFS_MAXDATA)
#define	NFS_MINPACKET	20
#define	NFS_FABLKSIZE	512	/* Size in bytes of a block wrt fa_blocks */

/* Stat numbers for rpc returns */
#define	NFS_OK		0
#define	NFSERR_PERM	1
#define	NFSERR_NOENT	2
#define	NFSERR_IO	5
#define	NFSERR_NXIO	6
#define	NFSERR_ACCES	13
#define	NFSERR_EXIST	17
#define	NFSERR_NODEV	19
#define	NFSERR_NOTDIR	20
#define	NFSERR_ISDIR	21
#define	NFSERR_FBIG	27
#define	NFSERR_NOSPC	28
#define	NFSERR_ROFS	30
#define	NFSERR_NAMETOL	63
#define	NFSERR_NOTEMPTY	66
#define	NFSERR_DQUOT	69
#define	NFSERR_STALE	70
#define	NFSERR_WFLUSH	99

/* Sizes in bytes of various nfs rpc components */
#define	NFSX_FH		32
#define	NFSX_UNSIGNED	4
#define	NFSX_FATTR	68
#define	NFSX_SATTR	32
#define NFSX_STATFS	20
#define	NFSX_COOKIE	4

/* nfs rpc procedure numbers */
#define	NFSPROC_NULL		0
#define	NFSPROC_GETATTR		1
#define	NFSPROC_SETATTR		2
#define	NFSPROC_NOOP		3
#define	NFSPROC_ROOT		NFSPROC_NOOP	/* Obsolete */
#define	NFSPROC_LOOKUP		4
#define	NFSPROC_READLINK	5
#define	NFSPROC_READ		6
#define	NFSPROC_WRITECACHE	NFSPROC_NOOP	/* Obsolete */
#define	NFSPROC_WRITE		8
#define	NFSPROC_CREATE		9
#define	NFSPROC_REMOVE		10
#define	NFSPROC_RENAME		11
#define	NFSPROC_LINK		12
#define	NFSPROC_SYMLINK		13
#define	NFSPROC_MKDIR		14
#define	NFSPROC_RMDIR		15
#define	NFSPROC_READDIR		16
#define	NFSPROC_STATFS		17

#define	NFS_NPROCS		18


/* File types */
typedef enum {
	NFNON=0,
	NFREG=1,
	NFDIR=2,
	NFBLK=3,
	NFCHR=4,
	NFLNK=5
} nfstype;

/* Structs for common parts of the rpc's */
struct nfsv2_time {
	u_int32_t	nfs_sec;
	u_int32_t	nfs_usec;
};

/*
 * File attributes and setable attributes.
 */
struct nfsv2_fattr {
	u_int32_t	fa_type;
	u_int32_t	fa_mode;
	u_int32_t	fa_nlink;
	u_int32_t	fa_uid;
	u_int32_t	fa_gid;
	u_int32_t	fa_size;
	u_int32_t	fa_blocksize;
	u_int32_t	fa_rdev;
	u_int32_t	fa_blocks;
	u_int32_t	fa_fsid;
	u_int32_t	fa_fileid;
	struct nfsv2_time fa_atime;
	struct nfsv2_time fa_mtime;
	struct nfsv2_time fa_ctime;
};

struct nfsv2_sattr {
	u_int32_t	sa_mode;
	u_int32_t	sa_uid;
	u_int32_t	sa_gid;
	u_int32_t	sa_size;
	struct nfsv2_time sa_atime;
	struct nfsv2_time sa_mtime;
};

struct nfsv2_statfs {
	u_int32_t	sf_tsize;
	u_int32_t	sf_bsize;
	u_int32_t	sf_blocks;
	u_int32_t	sf_bfree;
	u_int32_t	sf_bavail;
};
