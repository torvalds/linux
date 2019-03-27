/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)nfsproto.h  8.2 (Berkeley) 3/30/95
 * $FreeBSD$
 */

#ifndef _NFS_NFSPROTO_H_
#define _NFS_NFSPROTO_H_

/*
 * nfs definitions as per the Version 2 and 3 specs
 */

/*
 * Constants as defined in the Sun NFS Version 2 and 3 specs.
 * "NFS: Network File System Protocol Specification" RFC1094
 * and in the "NFS: Network File System Version 3 Protocol
 * Specification"
 */

#define NFS_PORT	2049
#define	NFS_PROG	100003
#define NFS_VER2	2
#define	NFS_VER3	3
#define NFS_VER4	4

#define NFS_V2MAXDATA	8192
#define	NFS_MAXDGRAMDATA 16384
#define	NFS_MAXDATA	32768
#define	NFS_MAXPATHLEN	1024
#define	NFS_MAXNAMLEN	255
#define	NFS_MAXPKTHDR	404	/* XXXv4 this needs to be adjust for v4 */
#define NFS_MAXPACKET	(NFS_MAXPKTHDR + NFS_MAXDATA)
#define	NFS_MINPACKET	20
#define	NFS_FABLKSIZE	512	/* Size in bytes of a block wrt fa_blocks */

/* Stat numbers for rpc returns (version 2, 3 and 4) */
#define	NFS_OK			0
#define	NFSERR_PERM		1
#define	NFSERR_NOENT		2
#define	NFSERR_IO		5
#define	NFSERR_NXIO		6
#define	NFSERR_ACCES		13
#define	NFSERR_EXIST		17
#define	NFSERR_XDEV		18	/* Version 3 only */
#define	NFSERR_NODEV		19
#define	NFSERR_NOTDIR		20
#define	NFSERR_ISDIR		21
#define	NFSERR_INVAL		22	/* Version 3 only */
#define	NFSERR_FBIG		27
#define	NFSERR_NOSPC		28
#define	NFSERR_ROFS		30
#define	NFSERR_MLINK		31	/* Version 3 only */
#define	NFSERR_NAMETOL		63
#define	NFSERR_NOTEMPTY		66
#define	NFSERR_DQUOT		69
#define	NFSERR_STALE		70
#define	NFSERR_REMOTE		71	/* Version 3 only */
#define	NFSERR_WFLUSH		99	/* Version 2 only */
#define	NFSERR_BADHANDLE	10001	/* The rest Version 3, 4 only */
#define	NFSERR_NOT_SYNC		10002
#define	NFSERR_BAD_COOKIE	10003
#define	NFSERR_NOTSUPP		10004
#define	NFSERR_TOOSMALL		10005
#define	NFSERR_SERVERFAULT	10006
#define	NFSERR_BADTYPE		10007
#define	NFSERR_JUKEBOX		10008
#define NFSERR_TRYLATER		NFSERR_JUKEBOX
#define	NFSERR_SAME		10009   /* The rest Version 4 only */
#define	NFSERR_DENIED		10010
#define	NFSERR_EXPIRED		10011
#define	NFSERR_LOCKED		10012
#define	NFSERR_GRACE		10013
#define	NFSERR_FHEXPIRED	10014
#define	NFSERR_SHARDE_DENIED	10015
#define	NFSERR_WRONGSEC		10016
#define	NFSERR_CLID_INUSE	10017
#define	NFSERR_RESOURCE		10018
#define	NFSERR_MOVED		10019
#define	NFSERR_NOFILEHANDLE	10020
#define	NFSERR_MINOR_VERS_MISMATCH 10021
#define	NFSERR_STALE_CLIENTID	10022
#define	NFSERR_STALE_STATEID	10023
#define	NFSERR_OLD_STATEID	10024
#define	NFSERR_BAD_STATEID	10025
#define	NFSERR_BAD_SEQID	10026
#define	NFSERR_NOT_SAME		10027
#define	NFSERR_LOCK_RANGE	10028
#define	NFSERR_SYMLINK		10029
#define	NFSERR_READDIR_NOSPC	10030
#define	NFSERR_LEASE_MOVED	10031
#define	NFSERR_ATTRNOTSUPP	10032
#define	NFSERR_NO_GRACE		10033
#define	NFSERR_RECLAIM_BAD	10034
#define	NFSERR_RECLAIM_CONFLICT	10035
#define	NFSERR_BADXDR		10036
#define	NFSERR_LOCKS_HELD	10037
#define	NFSERR_OPENMODE		10038
#define	NFSERR_BADOWNER		10039
#define	NFSERR_BADCHAR		10040
#define	NFSERR_BADNAME		10041
#define	NFSERR_BAD_RANGE	10042
#define	NFSERR_LOCK_NOTSUPP	10043
#define	NFSERR_OP_ILLEGAL	10044
#define	NFSERR_DEADLOCK		10045
#define	NFSERR_FILE_OPEN	10046
#define	NFSERR_STALEWRITEVERF	30001	/* Fake return for nfs_commit() */



#define NFSERR_RETVOID		0x20000000 /* Return void, not error */
#define NFSERR_AUTHERR		0x40000000 /* Mark an authentication error */
#define NFSERR_RETERR		0x80000000 /* Mark an error return for V3 */

/* Sizes in bytes of various nfs rpc components */
#define	NFSX_UNSIGNED	4

/* specific to NFS Version 2 */
#define	NFSX_V2FH	32
#define	NFSX_V2FATTR	68
#define	NFSX_V2SATTR	32
#define	NFSX_V2COOKIE	4
#define NFSX_V2STATFS	20

/* specific to NFS Version 3 */
#define NFSX_V3FH		(sizeof (fhandle_t)) /* size this server uses */
#define	NFSX_V3FHMAX		64	/* max. allowed by protocol */
#define NFSX_V3FATTR		84
#define NFSX_V3SATTR		60	/* max. all fields filled in */
#define NFSX_V3SRVSATTR		(sizeof (struct nfsv3_sattr))
#define NFSX_V3POSTOPATTR	(NFSX_V3FATTR + NFSX_UNSIGNED)
#define NFSX_V3WCCDATA		(NFSX_V3POSTOPATTR + 8 * NFSX_UNSIGNED)
#define NFSX_V3COOKIEVERF 	8
#define NFSX_V3WRITEVERF 	8
#define NFSX_V3CREATEVERF	8
#define NFSX_V3STATFS		52
#define NFSX_V3FSINFO		48
#define NFSX_V3PATHCONF		24

/* specific to NFS Version 4 */
#define NFSX_V4VERF		8
#define NFSX_V4FH		128
#define NFSX_V4STATEID		16

/* variants for both versions */
#define NFSX_FH(v3)		((v3) ? (NFSX_V3FHMAX + NFSX_UNSIGNED) : \
					NFSX_V2FH)
#define NFSX_SRVFH(v3)		((v3) ? NFSX_V3FH : NFSX_V2FH)
#define	NFSX_FATTR(v3)		((v3) ? NFSX_V3FATTR : NFSX_V2FATTR)
#define NFSX_PREOPATTR(v3)	((v3) ? (7 * NFSX_UNSIGNED) : 0)
#define NFSX_POSTOPATTR(v3)	((v3) ? (NFSX_V3FATTR + NFSX_UNSIGNED) : 0)
#define NFSX_POSTOPORFATTR(v3)	((v3) ? (NFSX_V3FATTR + NFSX_UNSIGNED) : \
					NFSX_V2FATTR)
#define NFSX_WCCDATA(v3)	((v3) ? NFSX_V3WCCDATA : 0)
#define NFSX_WCCORFATTR(v3)	((v3) ? NFSX_V3WCCDATA : NFSX_V2FATTR)
#define	NFSX_SATTR(v3)		((v3) ? NFSX_V3SATTR : NFSX_V2SATTR)
#define	NFSX_COOKIEVERF(v3)	((v3) ? NFSX_V3COOKIEVERF : 0)
#define	NFSX_WRITEVERF(v3)	((v3) ? NFSX_V3WRITEVERF : 0)
#define NFSX_READDIR(v3)	((v3) ? (5 * NFSX_UNSIGNED) : \
					(2 * NFSX_UNSIGNED))
#define	NFSX_STATFS(v3)		((v3) ? NFSX_V3STATFS : NFSX_V2STATFS)

/* nfs rpc procedure numbers (before version mapping) */
#define	NFSPROC_NULL		0
#define	NFSPROC_GETATTR		1
#define	NFSPROC_SETATTR		2
#define	NFSPROC_LOOKUP		3
#define	NFSPROC_ACCESS		4
#define	NFSPROC_READLINK	5
#define	NFSPROC_READ		6
#define	NFSPROC_WRITE		7
#define	NFSPROC_CREATE		8
#define	NFSPROC_MKDIR		9
#define	NFSPROC_SYMLINK		10
#define	NFSPROC_MKNOD		11
#define	NFSPROC_REMOVE		12
#define	NFSPROC_RMDIR		13
#define	NFSPROC_RENAME		14
#define	NFSPROC_LINK		15
#define	NFSPROC_READDIR		16
#define	NFSPROC_READDIRPLUS	17
#define	NFSPROC_FSSTAT		18
#define	NFSPROC_FSINFO		19
#define	NFSPROC_PATHCONF	20
#define	NFSPROC_COMMIT		21
#define NFSPROC_NOOP		22
#define	NFS_NPROCS		23

/* Actual Version 2 procedure numbers */
#define	NFSV2PROC_NULL		0
#define	NFSV2PROC_GETATTR	1
#define	NFSV2PROC_SETATTR	2
#define	NFSV2PROC_NOOP		3
#define	NFSV2PROC_ROOT		NFSV2PROC_NOOP	/* Obsolete */
#define	NFSV2PROC_LOOKUP	4
#define	NFSV2PROC_READLINK	5
#define	NFSV2PROC_READ		6
#define	NFSV2PROC_WRITECACHE	NFSV2PROC_NOOP	/* Obsolete */
#define	NFSV2PROC_WRITE		8
#define	NFSV2PROC_CREATE	9
#define	NFSV2PROC_REMOVE	10
#define	NFSV2PROC_RENAME	11
#define	NFSV2PROC_LINK		12
#define	NFSV2PROC_SYMLINK	13
#define	NFSV2PROC_MKDIR		14
#define	NFSV2PROC_RMDIR		15
#define	NFSV2PROC_READDIR	16
#define	NFSV2PROC_STATFS	17

/* Version 4 procedure numbers */
#define NFSV4PROC_NULL         0
#define NFSV4PROC_COMPOUND     1

/* Version 4 operation numbers */
#define NFSV4OP_ACCESS		3
#define NFSV4OP_CLOSE		4
#define NFSV4OP_COMMIT		5
#define NFSV4OP_CREATE		6
#define NFSV4OP_DELEGPURGE	7
#define NFSV4OP_DELEGRETURN	8
#define NFSV4OP_GETATTR		9
#define NFSV4OP_GETFH		10
#define NFSV4OP_LINK		11
#define NFSV4OP_LOCK		12
#define NFSV4OP_LOCKT		13
#define NFSV4OP_LOCKU		14
#define NFSV4OP_LOOKUP		15
#define NFSV4OP_LOOKUPP		16
#define NFSV4OP_NVERIFY		17
#define NFSV4OP_OPEN		18
#define NFSV4OP_OPENATTR	19
#define NFSV4OP_OPEN_CONFIRM	20
#define NFSV4OP_OPEN_DOWNGRADE	21
#define NFSV4OP_PUTFH		22
#define NFSV4OP_PUTPUBFH	23
#define NFSV4OP_PUTROOTFH	24
#define NFSV4OP_READ		25
#define NFSV4OP_READDIR		26
#define NFSV4OP_READLINK	27
#define NFSV4OP_REMOVE		28
#define NFSV4OP_RENAME		29
#define NFSV4OP_RENEW		30
#define NFSV4OP_RESTOREFH	31
#define NFSV4OP_SAVEFH		32
#define NFSV4OP_SECINFO		33
#define NFSV4OP_SETATTR		34
#define NFSV4OP_SETCLIENTID	35
#define NFSV4OP_SETCLIENTID_CONFIRM 36
#define NFSV4OP_VERIFY		37
#define NFSV4OP_WRITE		38

/*
 * Constants used by the Version 3 protocol for various RPCs
 */
#define NFSV3SATTRTIME_DONTCHANGE	0
#define NFSV3SATTRTIME_TOSERVER		1
#define NFSV3SATTRTIME_TOCLIENT		2

#define NFSV3ACCESS_READ		0x01
#define NFSV3ACCESS_LOOKUP		0x02
#define NFSV3ACCESS_MODIFY		0x04
#define NFSV3ACCESS_EXTEND		0x08
#define NFSV3ACCESS_DELETE		0x10
#define NFSV3ACCESS_EXECUTE		0x20

#define NFSV3WRITE_UNSTABLE		0
#define NFSV3WRITE_DATASYNC		1
#define NFSV3WRITE_FILESYNC		2

#define NFSV3CREATE_UNCHECKED		0
#define NFSV3CREATE_GUARDED		1
#define NFSV3CREATE_EXCLUSIVE		2

#define NFSV3FSINFO_LINK		0x01
#define NFSV3FSINFO_SYMLINK		0x02
#define NFSV3FSINFO_HOMOGENEOUS		0x08
#define NFSV3FSINFO_CANSETTIME		0x10

/*
 * Constants used by the Version 4 protocol for various RPCs
 */

#define NFSV4ACCESS_READ		0x01
#define NFSV4ACCESS_LOOKUP		0x02
#define NFSV4ACCESS_MODIFY		0x04
#define NFSV4ACCESS_EXTEND		0x08
#define NFSV4ACCESS_DELETE		0x10
#define NFSV4ACCESS_EXECUTE		0x20

#define NFSV4OPENRES_MLOCK		0x01
#define NFSV4OPENRES_CONFIRM		0x02

#define NFSV4OPENSHARE_ACCESS_READ	0x01
#define NFSV4OPENSHARE_ACCESS_WRITE	0x02
#define NFSV4OPENSHARE_ACCESS_BOTH	0x03
#define NFSV4OPENSHARE_DENY_NONE	0x00
#define NFSV4OPENSHARE_DENY_READ	0x01
#define NFSV4OPENSHARE_DENY_WRITE	0x02
#define NFSV4OPENSHARE_DENY_BOTH	0x03

/* File types */
typedef enum {
	NFNON=0,
	NFREG=1,
	NFDIR=2,
	NFBLK=3,
	NFCHR=4,
	NFLNK=5,
	NFSOCK=6,
	NFFIFO=7,
	NFATTRDIR = 8,
	NFNAMEDATTR = 9,
	NFBAD = 10,
} nfstype;	

/* NFSv4 claim type */
typedef enum {
	NCLNULL = 0,
	NCLPREV = 1,
	NCLDELEGCUR = 2,
	NCLDELEGPREV = 3,
} nfsv4cltype;

/* Other NFSv4 types */
typedef enum {
	NSHUNSTABLE = 0,
	NSHDATASYNC = 1,
	NSHFILESYNC = 2,
} nfsv4stablehow;

typedef enum { OTNOCREATE = 0, OTCREATE = 1 } nfsv4opentype;
typedef enum { CMUNCHECKED = 0, CMGUARDED = 1, CMEXCLUSIVE = 2 } nfsv4createmode;
typedef enum { THSERVERTIME = 0, THCLIENTTIME = 1 } nfsv4timehow;
typedef enum { ODNONE = 0, ODREAD = 1, ODWRITE = 2 } nfsv4opendelegtype;

/* Structs for common parts of the rpc's */

/*
 * File Handle (32 bytes for version 2), variable up to 64 for version 3.
 * File Handles of up to NFS_SMALLFH in size are stored directly in the
 * nfs node, whereas larger ones are malloc'd. (This never happens when
 * NFS_SMALLFH is set to 64.)
 * NFS_SMALLFH should be in the range of 32 to 64 and be divisible by 4.
 */
#ifndef NFS_SMALLFH
#define NFS_SMALLFH	128
#endif
union nfsfh {
	fhandle_t	fh_generic;
	u_char		fh_bytes[NFS_SMALLFH];
};
typedef union nfsfh nfsfh_t;

struct nfsv2_time {
	u_int32_t	nfsv2_sec;
	u_int32_t	nfsv2_usec;
};
typedef struct nfsv2_time	nfstime2;

struct nfsv3_time {
	u_int32_t	nfsv3_sec;
	u_int32_t	nfsv3_nsec;
};
typedef struct nfsv3_time	nfstime3;

/*
 * Quads are defined as arrays of 2 longs to ensure dense packing for the
 * protocol and to facilitate xdr conversion.
 */
struct nfs_uquad {
	u_int32_t	nfsuquad[2];
};
typedef	struct nfs_uquad	nfsuint64;

/*
 * Used to convert between two u_longs and a u_quad_t.
 */
union nfs_quadconvert {
	u_int32_t	lval[2];
	u_quad_t	qval;
};
typedef union nfs_quadconvert	nfsquad_t;

/*
 * NFS Version 3 special file number.
 */
struct nfsv3_spec {
	u_int32_t	specdata1;
	u_int32_t	specdata2;
};
typedef	struct nfsv3_spec	nfsv3spec;

/*
 * NFS Version 4 bitmap.
 */
struct nfsv4_bitmap {
	uint32_t	bmlen;
	uint32_t	*bmval;
};
typedef struct nfsv4_bitmap nfsv4bitmap;

struct nfsv4_changeinfo {
	u_int		ciatomic;
	uint64_t	cibefore;
	uint64_t	ciafter;
};
typedef struct nfsv4_changeinfo nfsv4changeinfo;

/*
 * File attributes and setable attributes. These structures cover both
 * NFS version 2 and the version 3 protocol. Note that the union is only
 * used so that one pointer can refer to both variants. These structures
 * go out on the wire and must be densely packed, so no quad data types
 * are used. (all fields are longs or u_longs or structures of same)
 * NB: You can't do sizeof(struct nfs_fattr), you must use the
 *     NFSX_FATTR(v3) macro.
 */
struct nfs_fattr {
	u_int32_t	fa_type;
	u_int32_t	fa_mode;
	u_int32_t	fa_nlink;
	u_int32_t	fa_uid;
	u_int32_t	fa_gid;
	union {
		struct {
			u_int32_t	nfsv2fa_size;
			u_int32_t	nfsv2fa_blocksize;
			u_int32_t	nfsv2fa_rdev;
			u_int32_t	nfsv2fa_blocks;
			u_int32_t	nfsv2fa_fsid;
			u_int32_t	nfsv2fa_fileid;
			nfstime2	nfsv2fa_atime;
			nfstime2	nfsv2fa_mtime;
			nfstime2	nfsv2fa_ctime;
		} fa_nfsv2;
		struct {
			nfsuint64	nfsv3fa_size;
			nfsuint64	nfsv3fa_used;
			nfsv3spec	nfsv3fa_rdev;
			nfsuint64	nfsv3fa_fsid;
			nfsuint64	nfsv3fa_fileid;
			nfstime3	nfsv3fa_atime;
			nfstime3	nfsv3fa_mtime;
			nfstime3	nfsv3fa_ctime;
		} fa_nfsv3;
	} fa_un;
};

/* and some ugly defines for accessing union components */
#define	fa2_size		fa_un.fa_nfsv2.nfsv2fa_size
#define	fa2_blocksize		fa_un.fa_nfsv2.nfsv2fa_blocksize
#define	fa2_rdev		fa_un.fa_nfsv2.nfsv2fa_rdev
#define	fa2_blocks		fa_un.fa_nfsv2.nfsv2fa_blocks
#define	fa2_fsid		fa_un.fa_nfsv2.nfsv2fa_fsid
#define	fa2_fileid		fa_un.fa_nfsv2.nfsv2fa_fileid
#define	fa2_atime		fa_un.fa_nfsv2.nfsv2fa_atime
#define	fa2_mtime		fa_un.fa_nfsv2.nfsv2fa_mtime
#define	fa2_ctime		fa_un.fa_nfsv2.nfsv2fa_ctime
#define	fa3_size		fa_un.fa_nfsv3.nfsv3fa_size
#define	fa3_used		fa_un.fa_nfsv3.nfsv3fa_used
#define	fa3_rdev		fa_un.fa_nfsv3.nfsv3fa_rdev
#define	fa3_fsid		fa_un.fa_nfsv3.nfsv3fa_fsid
#define	fa3_fileid		fa_un.fa_nfsv3.nfsv3fa_fileid
#define	fa3_atime		fa_un.fa_nfsv3.nfsv3fa_atime
#define	fa3_mtime		fa_un.fa_nfsv3.nfsv3fa_mtime
#define	fa3_ctime		fa_un.fa_nfsv3.nfsv3fa_ctime

struct nfsv4_fattr {
	u_int		fa4_valid;
	nfstype		fa4_type;
	off_t		fa4_size;
	uint64_t	fa4_fsid_major;
	uint64_t	fa4_fsid_minor;
	uint64_t	fa4_fileid;
	mode_t		fa4_mode;
	nlink_t		fa4_nlink;
	uid_t		fa4_uid;
	gid_t		fa4_gid;
	uint32_t	fa4_rdev_major;
	uint32_t	fa4_rdev_minor;
	struct timespec	fa4_atime;
	struct timespec	fa4_btime;
	struct timespec	fa4_ctime;
	struct timespec	fa4_mtime;
	uint64_t	fa4_maxread;
	uint64_t	fa4_maxwrite;
	uint64_t	fa4_ffree;
	uint64_t	fa4_ftotal;
	uint32_t	fa4_maxname;
	uint64_t	fa4_savail;
	uint64_t	fa4_sfree;
	uint64_t	fa4_stotal;
	uint64_t	fa4_changeid;
	uint32_t	fa4_lease_time;
	uint64_t	fa4_maxfilesize;
};

/* Flags for fa4_valid */
#define FA4V_SIZE	0x00000001
#define FA4V_FSID	0x00000002
#define FA4V_FILEID	0x00000004
#define FA4V_MODE	0x00000008
#define FA4V_NLINK	0x00000010
#define FA4V_UID	0x00000020
#define FA4V_GID	0x00000040
#define FA4V_RDEV	0x00000080
#define FA4V_ATIME	0x00000100
#define FA4V_BTIME	0x00000200
#define FA4V_CTIME	0x00000400
#define FA4V_MTIME	0x00000800
#define FA4V_MAXREAD	0x00001000
#define FA4V_MAXWRITE	0x00002000
#define FA4V_TYPE	0x00004000
#define FA4V_FFREE	0x00008000
#define FA4V_FTOTAL	0x00010000
#define FA4V_MAXNAME	0x00020000
#define FA4V_SAVAIL	0x00040000
#define FA4V_SFREE	0x00080000
#define FA4V_STOTAL	0x00100000
#define FA4V_CHANGEID	0x00200000
#define FA4V_LEASE_TIME	0x00400000
#define FA4V_MAXFILESIZE 0x00800000
#define FA4V_ACL	0x01000000

/* Offsets into bitmask */
#define FA4_SUPPORTED_ATTRS	0
#define FA4_TYPE		1
#define FA4_FH_EXPIRE_TYPE	2
#define FA4_CHANGE		3
#define FA4_SIZE		4
#define FA4_LINK_SUPPORT	5
#define FA4_SYMLINK_SUPPORT	6
#define FA4_NAMED_ATTR		7
#define FA4_FSID		8
#define FA4_UNIQUE_HANDLES	9
#define FA4_LEASE_TIME		10
#define FA4_RDATTR_ERROR	11
#define FA4_ACL			12
#define FA4_ACLSUPPORT		13
#define FA4_ARCHIVE		14
#define FA4_CANSETTIME		15
#define FA4_CASE_INSENSITIVE	16
#define FA4_CASE_PRESERVING	17 
#define FA4_CHOWN_RESTRICTED	18
#define FA4_FILEHANDLE		19
#define FA4_FILEID		20
#define FA4_FILES_AVAIL		21
#define FA4_FILES_FREE		22
#define FA4_FILES_TOTAL		23
#define FA4_FS_LOCATIONS	24
#define FA4_HIDDEN		25
#define FA4_HOMOGENEOUS		26
#define FA4_MAXFILESIZE		27
#define FA4_MAXLINK		28
#define FA4_MAXNAME		29
#define FA4_MAXREAD		30
#define FA4_MAXWRITE		31
#define FA4_MIMETYPE		32
#define FA4_MODE		33
#define FA4_NO_TRUNC		34
#define FA4_NUMLINKS		35
#define FA4_OWNER		36
#define FA4_OWNER_GROUP		37
#define FA4_QUOTA_HARD		38
#define FA4_QUOTA_SOFT		39
#define FA4_QUOTA_USED		40
#define FA4_RAWDEV		41
#define FA4_SPACE_AVAIL		42
#define FA4_SPACE_FREE		43
#define FA4_SPACE_TOTAL		44
#define FA4_SPACE_USED		45
#define FA4_SYSTEM		46
#define FA4_TIME_ACCESS		47
#define FA4_TIME_ACCESS_SET	48
#define FA4_TIME_BACKUP		49
#define FA4_TIME_CREATE		50
#define FA4_TIME_DELTA		51
#define FA4_TIME_METADATA	52
#define FA4_TIME_MODIFY		53
#define FA4_TIME_MODIFY_SET	54
#define FA4_ATTR_MAX		55

/* Macros for v4 fattr manipulation */
#define FA4_SET(n, p)	((p)[(n)/32] |= (1 << ((n) % 32)))
#define FA4_CLR(n, p)	((p)[(n)/32] &= ~(1 << ((n) % 32)))
#define FA4_ISSET(n, p)	((p)[(n)/32] & (1 << ((n) % 32)))
#define FA4_ZERO(p)	bzero((p), 8)
#define FA4_SKIP(p)	((p) += 2)

struct nfsv2_sattr {
	u_int32_t	sa_mode;
	u_int32_t	sa_uid;
	u_int32_t	sa_gid;
	u_int32_t	sa_size;
	nfstime2	sa_atime;
	nfstime2	sa_mtime;
};

/*
 * NFS Version 3 sattr structure for the new node creation case.
 */
struct nfsv3_sattr {
	u_int32_t	sa_modetrue;
	u_int32_t	sa_mode;
	u_int32_t	sa_uidfalse;
	u_int32_t	sa_gidfalse;
	u_int32_t	sa_sizefalse;
	u_int32_t	sa_atimetype;
	nfstime3	sa_atime;
	u_int32_t	sa_mtimetype;
	nfstime3	sa_mtime;
};

struct nfs_statfs {
	union {
		struct {
			u_int32_t	nfsv2sf_tsize;
			u_int32_t	nfsv2sf_bsize;
			u_int32_t	nfsv2sf_blocks;
			u_int32_t	nfsv2sf_bfree;
			u_int32_t	nfsv2sf_bavail;
		} sf_nfsv2;
		struct {
			nfsuint64	nfsv3sf_tbytes;
			nfsuint64	nfsv3sf_fbytes;
			nfsuint64	nfsv3sf_abytes;
			nfsuint64	nfsv3sf_tfiles;
			nfsuint64	nfsv3sf_ffiles;
			nfsuint64	nfsv3sf_afiles;
			u_int32_t	nfsv3sf_invarsec;
		} sf_nfsv3;
	} sf_un;
};

#define sf_tsize	sf_un.sf_nfsv2.nfsv2sf_tsize
#define sf_bsize	sf_un.sf_nfsv2.nfsv2sf_bsize
#define sf_blocks	sf_un.sf_nfsv2.nfsv2sf_blocks
#define sf_bfree	sf_un.sf_nfsv2.nfsv2sf_bfree
#define sf_bavail	sf_un.sf_nfsv2.nfsv2sf_bavail
#define sf_tbytes	sf_un.sf_nfsv3.nfsv3sf_tbytes
#define sf_fbytes	sf_un.sf_nfsv3.nfsv3sf_fbytes
#define sf_abytes	sf_un.sf_nfsv3.nfsv3sf_abytes
#define sf_tfiles	sf_un.sf_nfsv3.nfsv3sf_tfiles
#define sf_ffiles	sf_un.sf_nfsv3.nfsv3sf_ffiles
#define sf_afiles	sf_un.sf_nfsv3.nfsv3sf_afiles
#define sf_invarsec	sf_un.sf_nfsv3.nfsv3sf_invarsec

struct nfsv3_fsinfo {
	u_int32_t	fs_rtmax;
	u_int32_t	fs_rtpref;
	u_int32_t	fs_rtmult;
	u_int32_t	fs_wtmax;
	u_int32_t	fs_wtpref;
	u_int32_t	fs_wtmult;
	u_int32_t	fs_dtpref;
	nfsuint64	fs_maxfilesize;
	nfstime3	fs_timedelta;
	u_int32_t	fs_properties;
};

struct nfsv3_pathconf {
	u_int32_t	pc_linkmax;
	u_int32_t	pc_namemax;
	u_int32_t	pc_notrunc;
	u_int32_t	pc_chownrestricted;
	u_int32_t	pc_caseinsensitive;
	u_int32_t	pc_casepreserving;
};

#endif
