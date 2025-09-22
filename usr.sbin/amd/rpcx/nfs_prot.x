/*	$OpenBSD: nfs_prot.x,v 1.2 2022/12/28 21:30:15 jmc Exp $	*/

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef RPC_HDR
#endif

const NFS_PORT		= 2049;
const NFS_MAXDATA	= 8192;
const NFS_MAXPATHLEN	= 1024;
const NFS_MAXNAMLEN	= 255;
const NFS_FHSIZE	= 32;
const NFSX_V2FH		= 32;
const NFSX_V3FHMAX	= 64;
const NFS_COOKIESIZE	= 4;
const NFS_FIFO_DEV	= -1;	/* size kludge for named pipes */

/*
 * File types
 */
const NFSMODE_FMT  = 0170000;	/* type of file */
const NFSMODE_DIR  = 0040000;	/* directory */
const NFSMODE_CHR  = 0020000;	/* character special */
const NFSMODE_BLK  = 0060000;	/* block special */
const NFSMODE_REG  = 0100000;	/* regular */
const NFSMODE_LNK  = 0120000;	/* symbolic link */
const NFSMODE_SOCK = 0140000;	/* socket */
const NFSMODE_FIFO = 0010000;	/* fifo */

/*
 * Error status
 */
#ifdef RPC_HDR
%typedef int nfsstat;
%#define xdr_nfsstat xdr_int
#define nfsstat int
%enum {
%	NFS_OK= 0,		/* no error */
%	NFSERR_PERM=1,		/* Not owner */
%	NFSERR_NOENT=2,		/* No such file or directory */
%	NFSERR_IO=5,		/* I/O error */
%	NFSERR_NXIO=6,		/* No such device or address */
%	NFSERR_ACCES=13,	/* Permission denied */
%	NFSERR_EXIST=17,	/* File exists */
%	NFSERR_NODEV=19,	/* No such device */
%	NFSERR_NOTDIR=20,	/* Not a directory*/
%	NFSERR_ISDIR=21,	/* Is a directory */
%	NFSERR_FBIG=27,		/* File too large */
%	NFSERR_NOSPC=28,	/* No space left on device */
%	NFSERR_ROFS=30,		/* Read-only file system */
%	NFSERR_NAMETOOLONG=63,	/* File name too long */
%	NFSERR_NOTEMPTY=66,	/* Directory not empty */
%	NFSERR_DQUOT=69,	/* Disc quota exceeded */
%	NFSERR_STALE=70,	/* Stale NFS file handle */
%	NFSERR_WFLUSH=99	/* write cache flushed */
%};
#elif RPC_XDR
#define nfsstat int
#else
typedef int nfsstat;
#endif

/*
 * File types
 */
#ifdef RPC_HDR
%typedef int ftype;
#define ftype int
%enum {
%	NFNON = 0,	/* non-file */
%	NFREG = 1,	/* regular file */
%	NFDIR = 2,	/* directory */
%	NFBLK = 3,	/* block special */
%	NFCHR = 4,	/* character special */
%	NFLNK = 5,	/* symbolic link */
%	NFSOCK = 6,	/* unix domain sockets */
%	NFBAD = 7,	/* unused */
%	NFFIFO = 8	/* named pipe */
%};
#elif RPC_XDR
#define ftype int
#else
typedef int ftype;
#endif

/*
 * File access handle
 */
struct nfs_fh {
	opaque data[NFS_FHSIZE];
};

/*
 * Timeval
 */
struct nfstime {
	unsigned seconds;
	unsigned useconds;
};


/*
 * File attributes
 */
struct fattr {
	ftype type;		/* file type */
	unsigned mode;		/* protection mode bits */
	unsigned nlink;		/* # hard links */
	unsigned uid;		/* owner user id */
	unsigned gid;		/* owner group id */
	unsigned size;		/* file size in bytes */
	unsigned blocksize;	/* preferred block size */
	unsigned rdev;		/* special device # */
	unsigned blocks;	/* Kb of disk used by file */
	unsigned fsid;		/* device # */
	unsigned fileid;	/* inode # */
	nfstime	atime;		/* time of last access */
	nfstime	mtime;		/* time of last modification */
	nfstime	ctime;		/* time of last change */
};

/*
 * File attributes which can be set
 */
struct sattr {
	unsigned mode;	/* protection mode bits */
	unsigned uid;	/* owner user id */
	unsigned gid;	/* owner group id */
	unsigned size;	/* file size in bytes */
	nfstime	atime;	/* time of last access */
	nfstime	mtime;	/* time of last modification */
};


typedef string filename<NFS_MAXNAMLEN>;
typedef string nfspath<NFS_MAXPATHLEN>;

/*
 * Reply status with file attributes
 */
union attrstat switch (nfsstat status) {
case NFS_OK:
	fattr attributes;
default:
	void;
};

struct sattrargs {
	nfs_fh file;
	sattr attributes;
};

/*
 * Arguments for directory operations
 */
struct diropargs {
	nfs_fh	dir;	/* directory file handle */
	filename name;		/* name (up to NFS_MAXNAMLEN bytes) */
};

struct diropokres {
	nfs_fh file;
	fattr attributes;
};

/*
 * Results from directory operation
 */
union diropres switch (nfsstat status) {
case NFS_OK:
	diropokres diropres;
default:
	void;
};

union readlinkres switch (nfsstat status) {
case NFS_OK:
	nfspath data;
default:
	void;
};

/*
 * Arguments to remote read
 */
struct readargs {
	nfs_fh file;		/* handle for file */
	unsigned offset;	/* byte offset in file */
	unsigned count;		/* immediate read count */
	unsigned totalcount;	/* total read count (from this offset)*/
};

/*
 * Status OK portion of remote read reply
 */
struct readokres {
	fattr	attributes;	/* attributes, need for pagin*/
	opaque data<NFS_MAXDATA>;
};

union readres switch (nfsstat status) {
case NFS_OK:
	readokres reply;
default:
	void;
};

/*
 * Arguments to remote write
 */
struct writeargs {
	nfs_fh	file;		/* handle for file */
	unsigned beginoffset;	/* beginning byte offset in file */
	unsigned offset;	/* current byte offset in file */
	unsigned totalcount;	/* total write count (to this offset)*/
	opaque data<NFS_MAXDATA>;
};

struct createargs {
	diropargs where;
	sattr attributes;
};

struct renameargs {
	diropargs from;
	diropargs to;
};

struct linkargs {
	nfs_fh from;
	diropargs to;
};

struct symlinkargs {
	diropargs from;
	nfspath to;
	sattr attributes;
};


typedef opaque nfscookie[NFS_COOKIESIZE];

/*
 * Arguments to readdir
 */
struct readdirargs {
	nfs_fh dir;		/* directory handle */
	nfscookie cookie;
	unsigned count;		/* number of directory bytes to read */
};

struct entry {
	unsigned fileid;
	filename name;
	nfscookie cookie;
	entry *nextentry;
};

struct dirlist {
	entry *entries;
	bool eof;
};

union readdirres switch (nfsstat status) {
case NFS_OK:
	dirlist reply;
default:
	void;
};

struct statfsokres {
	unsigned tsize;	/* preferred transfer size in bytes */
	unsigned bsize;	/* fundamental file system block size */
	unsigned blocks;	/* total blocks in file system */
	unsigned bfree;	/* free blocks in fs */
	unsigned bavail;	/* free blocks avail to non-superuser */
};

union statfsres switch (nfsstat status) {
case NFS_OK:
	statfsokres reply;
default:
	void;
};

/*
 * Remote file service routines
 */
program NFS_PROGRAM {
	version NFS_VERSION {
		void
		NFSPROC_NULL(void) = 0;

		attrstat
		NFSPROC_GETATTR(nfs_fh) =	1;

		attrstat
		NFSPROC_SETATTR(sattrargs) = 2;

		void
		NFSPROC_ROOT(void) = 3;

		diropres
		NFSPROC_LOOKUP(diropargs) = 4;

		readlinkres
		NFSPROC_READLINK(nfs_fh) = 5;

		readres
		NFSPROC_READ(readargs) = 6;

		void
		NFSPROC_WRITECACHE(void) = 7;

		attrstat
		NFSPROC_WRITE(writeargs) = 8;

		diropres
		NFSPROC_CREATE(createargs) = 9;

		nfsstat
		NFSPROC_REMOVE(diropargs) = 10;

		nfsstat
		NFSPROC_RENAME(renameargs) = 11;

		nfsstat
		NFSPROC_LINK(linkargs) = 12;

		nfsstat
		NFSPROC_SYMLINK(symlinkargs) = 13;

		diropres
		NFSPROC_MKDIR(createargs) = 14;

		nfsstat
		NFSPROC_RMDIR(diropargs) = 15;

		readdirres
		NFSPROC_READDIR(readdirargs) = 16;

		statfsres
		NFSPROC_STATFS(nfs_fh) = 17;
	} = 2;
} = 100003;

