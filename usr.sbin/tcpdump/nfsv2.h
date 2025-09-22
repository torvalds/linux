/*	$OpenBSD: nfsv2.h,v 1.7 2003/06/02 23:36:54 millert Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996
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
 *	@(#)nfsv2.h	7.11 (Berkeley) 9/30/92
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
#define	NFSX_NFSFATTR	68
#define	NFSX_NQFATTR	92
#define	NFSX_NFSSATTR	32
#define	NFSX_NQSATTR	44
#define	NFSX_COOKIE	4
#define NFSX_NFSSTATFS	20
#define	NFSX_NQSTATFS	28
#define	NFSX_FATTR(isnq)	((isnq) ? NFSX_NQFATTR : NFSX_NFSFATTR)
#define	NFSX_SATTR(isnq)	((isnq) ? NFSX_NQSATTR : NFSX_NFSSATTR)
#define	NFSX_STATFS(isnq)	((isnq) ? NFSX_NQSTATFS : NFSX_NFSSTATFS)

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

/* NQ nfs numbers */
#define	NQNFSPROC_READDIRLOOK	18
#define	NQNFSPROC_GETLEASE	19
#define	NQNFSPROC_VACATED	20
#define	NQNFSPROC_EVICTED	21
#define	NQNFSPROC_ACCESS	22

#define	NFS_NPROCS		23
/* Conversion macros */
extern int		vttoif_tab[];
#define	vtonfs_mode(t,m) \
		txdr_unsigned(((t) == VFIFO) ? MAKEIMODE(VCHR, (m)) : \
				MAKEIMODE((t), (m)))
#define	nfstov_mode(a)	(fxdr_unsigned(u_short, (a))&07777)
#define	vtonfs_type(a)	txdr_unsigned(nfs_type[((int32_t)(a))])
#define	nfstov_type(a)	ntov_type[fxdr_unsigned(u_int32_t,(a))&0x7]

/* File types */
typedef enum {
    NFNON=0, NFREG=1, NFDIR=2, NFBLK=3, NFCHR=4, NFLNK=5
} tcpdump_nfstype;

/* Structs for common parts of the rpc's */
struct nfsv2_time {
	u_int32_t nfs_sec;
	u_int32_t nfs_usec;
};

struct nqnfs_time {
	u_int32_t nq_sec;
	u_int32_t nq_nsec;
};

/*
 * File attributes and setable attributes. These structures cover both
 * NFS version 2 and the NQNFS protocol. Note that the union is only
 * used to that one pointer can refer to both variants. These structures
 * go out on the wire and must be densely packed, so no quad data types
 * are used. (all fields are int32_t or u_int32_t's or structures of same)
 * NB: You can't do sizeof(struct nfsv2_fattr), you must use the
 *     NFSX_FATTR(isnq) macro.
 */
struct nfsv2_fattr {
	u_int32_t fa_type;
	u_int32_t fa_mode;
	u_int32_t fa_nlink;
	u_int32_t fa_uid;
	u_int32_t fa_gid;
	union {
		struct {
			u_int32_t nfsfa_size;
			u_int32_t nfsfa_blocksize;
			u_int32_t nfsfa_rdev;
			u_int32_t nfsfa_blocks;
			u_int32_t nfsfa_fsid;
			u_int32_t nfsfa_fileid;
			struct nfsv2_time nfsfa_atime;
			struct nfsv2_time nfsfa_mtime;
			struct nfsv2_time nfsfa_ctime;
		} fa_nfsv2;
		struct {
			struct {
				u_int32_t nqfa_qsize[2];
			} nqfa_size;
			u_int32_t nqfa_blocksize;
			u_int32_t nqfa_rdev;
			struct {
				u_int32_t	nqfa_qbytes[2];
			} nqfa_bytes;
			u_int32_t nqfa_fsid;
			u_int32_t nqfa_fileid;
			struct nqnfs_time nqfa_atime;
			struct nqnfs_time nqfa_mtime;
			struct nqnfs_time nqfa_ctime;
			u_int32_t nqfa_flags;
			u_int32_t nqfa_gen;
			struct {
				u_int32_t nqfa_qfilerev[2];
			} nqfa_filerev;
		} fa_nqnfs;
	} fa_un;
};

/* and some ugly defines for accessing union components */
#define	fa_nfssize		fa_un.fa_nfsv2.nfsfa_size
#define	fa_nfsblocksize		fa_un.fa_nfsv2.nfsfa_blocksize
#define	fa_nfsrdev		fa_un.fa_nfsv2.nfsfa_rdev
#define	fa_nfsblocks		fa_un.fa_nfsv2.nfsfa_blocks
#define	fa_nfsfsid		fa_un.fa_nfsv2.nfsfa_fsid
#define	fa_nfsfileid		fa_un.fa_nfsv2.nfsfa_fileid
#define	fa_nfsatime		fa_un.fa_nfsv2.nfsfa_atime
#define	fa_nfsmtime		fa_un.fa_nfsv2.nfsfa_mtime
#define	fa_nfsctime		fa_un.fa_nfsv2.nfsfa_ctime
#define	fa_nqsize		fa_un.fa_nqnfs.nqfa_size
#define	fa_nqblocksize		fa_un.fa_nqnfs.nqfa_blocksize
#define	fa_nqrdev		fa_un.fa_nqnfs.nqfa_rdev
#define	fa_nqbytes		fa_un.fa_nqnfs.nqfa_bytes
#define	fa_nqfsid		fa_un.fa_nqnfs.nqfa_fsid
#define	fa_nqfileid		fa_un.fa_nqnfs.nqfa_fileid
#define	fa_nqatime		fa_un.fa_nqnfs.nqfa_atime
#define	fa_nqmtime		fa_un.fa_nqnfs.nqfa_mtime
#define	fa_nqctime		fa_un.fa_nqnfs.nqfa_ctime
#define	fa_nqflags		fa_un.fa_nqnfs.nqfa_flags
#define	fa_nqgen		fa_un.fa_nqnfs.nqfa_gen
#define	fa_nqfilerev		fa_un.fa_nqnfs.nqfa_filerev

struct nfsv2_sattr {
	u_int32_t sa_mode;
	u_int32_t sa_uid;
	u_int32_t sa_gid;
	union {
		struct {
			u_int32_t nfssa_size;
			struct nfsv2_time nfssa_atime;
			struct nfsv2_time nfssa_mtime;
		} sa_nfsv2;
		struct {
			struct {
				u_int32_t nqsa_qsize[2];
			} nqsa_size;
			struct nqnfs_time nqsa_atime;
			struct nqnfs_time nqsa_mtime;
			u_int32_t nqsa_flags;
			u_int32_t nqsa_rdev;
		} sa_nqnfs;
	} sa_un;
};

/* and some ugly defines for accessing the unions */
#define	sa_nfssize		sa_un.sa_nfsv2.nfssa_size
#define	sa_nfsatime		sa_un.sa_nfsv2.nfssa_atime
#define	sa_nfsmtime		sa_un.sa_nfsv2.nfssa_mtime
#define	sa_nqsize		sa_un.sa_nqnfs.nqsa_size
#define	sa_nqatime		sa_un.sa_nqnfs.nqsa_atime
#define	sa_nqmtime		sa_un.sa_nqnfs.nqsa_mtime
#define	sa_nqflags		sa_un.sa_nqnfs.nqsa_flags
#define	sa_nqrdev		sa_un.sa_nqnfs.nqsa_rdev

struct nfsv2_statfs {
	u_int32_t sf_tsize;
	u_int32_t sf_bsize;
	u_int32_t sf_blocks;
	u_int32_t sf_bfree;
	u_int32_t sf_bavail;
	u_int32_t sf_files;	/* Nqnfs only */
	u_int32_t sf_ffree;	/* ditto      */
};
