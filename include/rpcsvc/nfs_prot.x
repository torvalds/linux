/*-
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
%#ifndef lint
%/*static char sccsid[] = "from: @(#)nfs_prot.x 1.2 87/10/12 Copyr 1987 Sun Micro";*/
%/*static char sccsid[] = "from: @(#)nfs_prot.x	2.1 88/08/01 4.0 RPCSRC";*/
%#endif /* not lint */
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

const NFS_PORT          = 2049;
const NFS_MAXDATA       = 8192;
const NFS_MAXPATHLEN    = 1024;
const NFS_MAXNAMLEN	= 255;
const NFS_FHSIZE	= 32;
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
enum nfsstat {
	NFS_OK= 0,		/* no error */
	NFSERR_PERM=1,		/* Not owner */
	NFSERR_NOENT=2,		/* No such file or directory */
	NFSERR_IO=5,		/* I/O error */
	NFSERR_NXIO=6,		/* No such device or address */
	NFSERR_ACCES=13,	/* Permission denied */
	NFSERR_EXIST=17,	/* File exists */
	NFSERR_NODEV=19,	/* No such device */
	NFSERR_NOTDIR=20,	/* Not a directory*/
	NFSERR_ISDIR=21,	/* Is a directory */
	NFSERR_FBIG=27,		/* File too large */
	NFSERR_NOSPC=28,	/* No space left on device */
	NFSERR_ROFS=30,		/* Read-only file system */
	NFSERR_NAMETOOLONG=63,	/* File name too long */
	NFSERR_NOTEMPTY=66,	/* Directory not empty */
	NFSERR_DQUOT=69,	/* Disc quota exceeded */
	NFSERR_STALE=70,	/* Stale NFS file handle */
	NFSERR_WFLUSH=99	/* write cache flushed */
};

/*
 * File types
 */
enum ftype {
	NFNON = 0,	/* non-file */
	NFREG = 1,	/* regular file */
	NFDIR = 2,	/* directory */
	NFBLK = 3,	/* block special */
	NFCHR = 4,	/* character special */
	NFLNK = 5,	/* symbolic link */
	NFSOCK = 6,	/* unix domain sockets */
	NFBAD = 7,	/* unused */
	NFFIFO = 8 	/* named pipe */
};

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

#ifdef WANT_NFS3

/*
 * NFSv3 constants and types
 */
const NFS3_FHSIZE	= 64;	/* maximum size in bytes of a file handle */
const NFS3_COOKIEVERFSIZE = 8;	/* size of a cookie verifier for READDIR */
const NFS3_CREATEVERFSIZE = 8;	/* size of the verifier used for CREATE */
const NFS3_WRITEVERFSIZE = 8;	/* size of the verifier used for WRITE */

typedef unsigned hyper uint64;
typedef hyper int64;
typedef unsigned long uint32;
typedef long int32;
typedef string filename3<>;
typedef string nfspath3<>;
typedef uint64 fileid3;
typedef uint64 cookie3;
typedef opaque cookieverf3[NFS3_COOKIEVERFSIZE];
typedef opaque createverf3[NFS3_CREATEVERFSIZE];
typedef opaque writeverf3[NFS3_WRITEVERFSIZE];
typedef uint32 uid3;
typedef uint32 gid3;
typedef uint64 size3;
typedef uint64 offset3;
typedef uint32 mode3;
typedef uint32 count3;

/*
 * Error status (v3)
 */
enum nfsstat3 {
	NFS3_OK	= 0,
	NFS3ERR_PERM		= 1,
	NFS3ERR_NOENT		= 2,
	NFS3ERR_IO		= 5,
	NFS3ERR_NXIO		= 6,
	NFS3ERR_ACCES		= 13,
	NFS3ERR_EXIST		= 17,
	NFS3ERR_XDEV		= 18,
	NFS3ERR_NODEV		= 19,
	NFS3ERR_NOTDIR		= 20,
	NFS3ERR_ISDIR		= 21,
	NFS3ERR_INVAL		= 22,
	NFS3ERR_FBIG		= 27,
	NFS3ERR_NOSPC		= 28,
	NFS3ERR_ROFS		= 30,
	NFS3ERR_MLINK		= 31,
	NFS3ERR_NAMETOOLONG	= 63,
	NFS3ERR_NOTEMPTY	= 66,
	NFS3ERR_DQUOT		= 69,
	NFS3ERR_STALE		= 70,
	NFS3ERR_REMOTE		= 71,
	NFS3ERR_BADHANDLE	= 10001,
	NFS3ERR_NOT_SYNC	= 10002,
	NFS3ERR_BAD_COOKIE	= 10003,
	NFS3ERR_NOTSUPP		= 10004,
	NFS3ERR_TOOSMALL	= 10005,
	NFS3ERR_SERVERFAULT	= 10006,
	NFS3ERR_BADTYPE		= 10007,
	NFS3ERR_JUKEBOX		= 10008
};

/*
 * File types (v3)
 */
enum ftype3 {
	NF3REG	= 1,		/* regular file */
	NF3DIR	= 2,		/* directory */
	NF3BLK	= 3,		/* block special */
	NF3CHR	= 4,		/* character special */
	NF3LNK	= 5,		/* symbolic link */
	NF3SOCK	= 6,		/* unix domain sockets */
	NF3FIFO	= 7		/* named pipe */
};

struct specdata3 {
	uint32	specdata1;
	uint32	specdata2;
};

/*
 * File access handle (v3)
 */
struct nfs_fh3 {
	opaque data<NFS3_FHSIZE>;
};

/* 
 * Timeval (v3)
 */
struct nfstime3 {
	uint32	seconds;
	uint32	nseconds;
};


/*
 * File attributes (v3)
 */
struct fattr3 {
	ftype3	type;		/* file type */
	mode3	mode;		/* protection mode bits */
	uint32	nlink;		/* # hard links */
	uid3	uid;		/* owner user id */
	gid3	gid;		/* owner group id */
	size3	size;		/* file size in bytes */
	size3	used;		/* preferred block size */
	specdata3 rdev;		/* special device # */
	uint64 fsid;		/* device # */
	fileid3	fileid;		/* inode # */
	nfstime3 atime;		/* time of last access */
	nfstime3 mtime;		/* time of last modification */
	nfstime3 ctime;		/* time of last change */
};

union post_op_attr switch (bool attributes_follow) {
case TRUE:
	fattr3	attributes;
case FALSE:
	void;
};

struct wcc_attr {
	size3	size;
	nfstime3 mtime;
	nfstime3 ctime;
};

union pre_op_attr switch (bool attributes_follow) {
case TRUE:
	wcc_attr attributes;
case FALSE:
	void;
};

struct wcc_data {
	pre_op_attr before;
	post_op_attr after;
};

union post_op_fh3 switch (bool handle_follows) {
case TRUE:
	nfs_fh3	handle;
case FALSE:
	void;
};

/*
 * File attributes which can be set (v3)
 */
enum time_how {
	DONT_CHANGE		= 0,
	SET_TO_SERVER_TIME	= 1,
	SET_TO_CLIENT_TIME	= 2
};

union set_mode3 switch (bool set_it) {
case TRUE:
	mode3	mode;
default:
	void;
};

union set_uid3 switch (bool set_it) {
case TRUE:
	uid3	uid;
default:
	void;
};

union set_gid3 switch (bool set_it) {
case TRUE:
	gid3	gid;
default:
	void;
};

union set_size3 switch (bool set_it) {
case TRUE:
	size3	size;
default:
	void;
};

union set_atime switch (time_how set_it) {
case SET_TO_CLIENT_TIME:
	nfstime3	atime;
default:
	void;
};

union set_mtime switch (time_how set_it) {
case SET_TO_CLIENT_TIME:
	nfstime3	mtime;
default:
	void;
};

struct sattr3 {
	set_mode3	mode;
	set_uid3	uid;
	set_gid3	gid;
	set_size3	size;
	set_atime	atime;
	set_mtime	mtime;
};

/*
 * Arguments for directory operations (v3)
 */
struct diropargs3 {
	nfs_fh3	dir;		/* directory file handle */
	filename3 name;		/* name (up to NFS_MAXNAMLEN bytes) */
};

/*
 * Arguments to getattr (v3).
 */
struct GETATTR3args {
	nfs_fh3		object;
};

struct GETATTR3resok {
	fattr3		obj_attributes;
};

union GETATTR3res switch (nfsstat3 status) {
case NFS3_OK:
	GETATTR3resok	resok;
default:
	void;
};

/*
 * Arguments to setattr (v3).
 */
union sattrguard3 switch (bool check) {
case TRUE:
	nfstime3	obj_ctime;
case FALSE:
	void;
};

struct SETATTR3args {
	nfs_fh3		object;
	sattr3		new_attributes;
	sattrguard3	guard;
};

struct SETATTR3resok {
	wcc_data	obj_wcc;
};

struct SETATTR3resfail {
	wcc_data	obj_wcc;
};

union SETATTR3res switch (nfsstat3 status) {
case NFS3_OK:
	SETATTR3resok	resok;
default:
	SETATTR3resfail	resfail;
};

/*
 * Arguments to lookup (v3).
 */
struct LOOKUP3args {
	diropargs3	what;
};

struct LOOKUP3resok {
	nfs_fh3		object;
	post_op_attr	obj_attributes;
	post_op_attr	dir_attributes;
};

struct LOOKUP3resfail {
	post_op_attr	dir_attributes;
};

union LOOKUP3res switch (nfsstat3 status) {
case NFS3_OK:
	LOOKUP3resok	resok;
default:
	LOOKUP3resfail	resfail;
};

/*
 * Arguments to access (v3).
 */
const ACCESS3_READ	= 0x0001;
const ACCESS3_LOOKUP	= 0x0002;
const ACCESS3_MODIFY	= 0x0004;
const ACCESS3_EXTEND	= 0x0008;
const ACCESS3_DELETE	= 0x0010;
const ACCESS3_EXECUTE	= 0x0020;

struct ACCESS3args {
	nfs_fh3		object;
	uint32		access;
};

struct ACCESS3resok {
	post_op_attr	obj_attributes;
	uint32		access;
};

struct ACCESS3resfail {
	post_op_attr	obj_attributes;
};

union ACCESS3res switch (nfsstat3 status) {
case NFS3_OK:
	ACCESS3resok	resok;
default:
	ACCESS3resfail	resfail;
};

/*
 * Arguments to readlink (v3).
 */
struct READLINK3args {
	nfs_fh3		symlink;
};

struct READLINK3resok {
	post_op_attr	symlink_attributes;
	nfspath3	data;
};

struct READLINK3resfail {
	post_op_attr	symlink_attributes;
};

union READLINK3res switch (nfsstat3 status) {
case NFS3_OK:
	READLINK3resok	resok;
default:
	READLINK3resfail resfail;
};

/*
 * Arguments to read (v3).
 */
struct READ3args {
	nfs_fh3		file;
	offset3		offset;
	count3		count;
};

struct READ3resok {
	post_op_attr	file_attributes;
	count3		count;
	bool		eof;
	opaque		data<>;
};

struct READ3resfail {
	post_op_attr	file_attributes;
};

/* XXX: solaris 2.6 uses ``nfsstat'' here */
union READ3res switch (nfsstat3 status) {
case NFS3_OK:
	READ3resok	resok;
default:
	READ3resfail	resfail;
};

/*
 * Arguments to write (v3).
 */
enum stable_how {
	UNSTABLE	= 0,
	DATA_SYNC	= 1,
	FILE_SYNC	= 2
};

struct WRITE3args {
	nfs_fh3		file;
	offset3		offset;
	count3		count;
	stable_how	stable;
	opaque		data<>;
};

struct WRITE3resok {
	wcc_data	file_wcc;
	count3		count;
	stable_how	committed;
	writeverf3	verf;
};

struct WRITE3resfail {
	wcc_data	file_wcc;
};

union WRITE3res switch (nfsstat3 status) {
case NFS3_OK:
	WRITE3resok	resok;
default:
	WRITE3resfail	resfail;
};

/*
 * Arguments to create (v3).
 */
enum createmode3 {
	UNCHECKED	= 0,
	GUARDED		= 1,
	EXCLUSIVE	= 2
};

union createhow3 switch (createmode3 mode) {
case UNCHECKED:
case GUARDED:
	sattr3		obj_attributes;
case EXCLUSIVE:
	createverf3	verf;
};

struct CREATE3args {
	diropargs3	where;
	createhow3	how;
};

struct CREATE3resok {
	post_op_fh3	obj;
	post_op_attr	obj_attributes;
	wcc_data	dir_wcc;
};

struct CREATE3resfail {
	wcc_data	dir_wcc;
};

union CREATE3res switch (nfsstat3 status) {
case NFS3_OK:
	CREATE3resok	resok;
default:
	CREATE3resfail	resfail;
};

/*
 * Arguments to mkdir (v3).
 */
struct MKDIR3args {
	diropargs3	where;
	sattr3		attributes;
};

struct MKDIR3resok {
	post_op_fh3	obj;
	post_op_attr	obj_attributes;
	wcc_data	dir_wcc;
};

struct MKDIR3resfail {
	wcc_data	dir_wcc;
};

union MKDIR3res switch (nfsstat3 status) {
case NFS3_OK:
	MKDIR3resok	resok;
default:
	MKDIR3resfail	resfail;
};

/*
 * Arguments to symlink (v3).
 */
struct symlinkdata3 {
	sattr3		symlink_attributes;
	nfspath3	symlink_data;
};

struct SYMLINK3args {
	diropargs3	where;
	symlinkdata3	symlink;
};

struct SYMLINK3resok {
	post_op_fh3	obj;
	post_op_attr	obj_attributes;
	wcc_data	dir_wcc;
};

struct SYMLINK3resfail {
	wcc_data	dir_wcc;
};

union SYMLINK3res switch (nfsstat3 status) {
case NFS3_OK:
	SYMLINK3resok	resok;
default:
	SYMLINK3resfail	resfail;
};

/*
 * Arguments to mknod (v3).
 */
struct devicedata3 {
	sattr3		dev_attributes;
	specdata3	spec;
};

union mknoddata3 switch (ftype3 type) {
case NF3CHR:
case NF3BLK:
	devicedata3	device;
case NF3SOCK:
case NF3FIFO:
	sattr3		pipe_attributes;
default:
	void;
};

struct MKNOD3args {
	diropargs3	where;
	mknoddata3	what;
};

struct MKNOD3resok {
	post_op_fh3	obj;
	post_op_attr	obj_attributes;
	wcc_data	dir_wcc;
};

struct MKNOD3resfail {
	wcc_data	dir_wcc;
};

union MKNOD3res switch (nfsstat3 status) {
case NFS3_OK:
	MKNOD3resok	resok;
default:
	MKNOD3resfail	resfail;
};

/*
 * Arguments to remove (v3).
 */
struct REMOVE3args {
	diropargs3	object;
};

struct REMOVE3resok {
	wcc_data	dir_wcc;
};

struct REMOVE3resfail {
	wcc_data	dir_wcc;
};

union REMOVE3res switch (nfsstat3 status) {
case NFS3_OK:
	REMOVE3resok	resok;
default:
	REMOVE3resfail	resfail;
};

/*
 * Arguments to rmdir (v3).
 */
struct RMDIR3args {
	diropargs3	object;
};

struct RMDIR3resok {
	wcc_data	dir_wcc;
};

struct RMDIR3resfail {
	wcc_data	dir_wcc;
};

union RMDIR3res switch (nfsstat3 status) {
case NFS3_OK:
	RMDIR3resok	resok;
default:
	RMDIR3resfail	resfail;
};

/*
 * Arguments to rename (v3).
 */
struct RENAME3args {
	diropargs3	from;
	diropargs3	to;
};

struct RENAME3resok {
	wcc_data	fromdir_wcc;
	wcc_data	todir_wcc;
};

struct RENAME3resfail {
	wcc_data	fromdir_wcc;
	wcc_data	todir_wcc;
};

union RENAME3res switch (nfsstat3 status) {
case NFS3_OK:
	RENAME3resok	resok;
default:
	RENAME3resfail	resfail;
};

/*
 * Arguments to link (v3).
 */
struct LINK3args {
	nfs_fh3		file;
	diropargs3	link;
};

struct LINK3resok {
	post_op_attr	file_attributes;
	wcc_data	linkdir_wcc;
};

struct LINK3resfail {
	post_op_attr	file_attributes;
	wcc_data	linkdir_wcc;
};

union LINK3res switch (nfsstat3 status) {
case NFS3_OK:
	LINK3resok	resok;
default:
	LINK3resfail	resfail;
};

/*
 * Arguments to readdir (v3).
 */
struct READDIR3args {
	nfs_fh3		dir;
	cookie3		cookie;
	cookieverf3	cookieverf;
	count3		count;
};

struct entry3 {
	fileid3		fileid;
	filename3	name;
	cookie3		cookie;
	entry3		*nextentry;
};

struct dirlist3 {
	entry3		*entries;
	bool		eof;
};

struct READDIR3resok {
	post_op_attr	dir_attributes;
	cookieverf3	cookieverf;
	dirlist3	reply;
};

struct READDIR3resfail {
	post_op_attr	dir_attributes;
};

union READDIR3res switch (nfsstat3 status) {
case NFS3_OK:
	READDIR3resok	resok;
default:
	READDIR3resfail	resfail;
};

/*
 * Arguments to readdirplus (v3).
 */
struct READDIRPLUS3args {
	nfs_fh3		dir;
	cookie3		cookie;
	cookieverf3	cookieverf;
	count3		dircount;
	count3		maxcount;
};

struct entryplus3 {
	fileid3		fileid;
	filename3	name;
	cookie3		cookie;
	post_op_attr	name_attributes;
	post_op_fh3	name_handle;
	entryplus3	*nextentry;
};

struct dirlistplus3 {
	entryplus3	*entries;
	bool		eof;
};

struct READDIRPLUS3resok {
	post_op_attr	dir_attributes;
	cookieverf3	cookieverf;
	dirlistplus3	reply;
};

struct READDIRPLUS3resfail {
	post_op_attr	dir_attributes;
};

union READDIRPLUS3res switch (nfsstat3 status) {
case NFS3_OK:
	READDIRPLUS3resok	resok;
default:
	READDIRPLUS3resfail	resfail;
};

/*
 * Arguments to fsstat (v3).
 */
struct FSSTAT3args {
	nfs_fh3		fsroot;
};

struct FSSTAT3resok {
	post_op_attr	obj_attributes;
	size3		tbytes;
	size3		fbytes;
	size3		abytes;
	size3		tfiles;
	size3		ffiles;
	size3		afiles;
	uint32		invarsec;
};

struct FSSTAT3resfail {
	post_op_attr	obj_attributes;
};

union FSSTAT3res switch (nfsstat3 status) {
case NFS3_OK:
	FSSTAT3resok	resok;
default:
	FSSTAT3resfail	resfail;
};

/*
 * Arguments to fsinfo (v3).
 */
const FSF3_LINK		= 0x0001;
const FSF3_SYMLINK	= 0x0002;
const FSF3_HOMOGENEOUS	= 0x0008;
const FSF3_CANSETTIME	= 0x0010;

struct FSINFO3args {
	nfs_fh3		fsroot;
};

struct FSINFO3resok {
	post_op_attr	obj_attributes;
	uint32		rtmax;
	uint32		rtpref;
	uint32		rtmult;
	uint32		wtmax;
	uint32		wtpref;
	uint32		wtmult;
	uint32		dtpref;
	size3		maxfilesize;
	nfstime3	time_delta;
	uint32		properties;
};

struct FSINFO3resfail {
	post_op_attr	obj_attributes;
};

union FSINFO3res switch (nfsstat3 status) {
case NFS3_OK:
	FSINFO3resok	resok;
default:
	FSINFO3resfail	resfail;
};

/*
 * Arguments to pathconf (v3).
 */
struct PATHCONF3args {
	nfs_fh3		object;
};

struct PATHCONF3resok {
	post_op_attr	obj_attributes;
	uint32		linkmax;
	uint32		name_max;
	bool		no_trunc;
	bool		chown_restricted;
	bool		case_insensitive;
	bool		case_preserving;
};

struct PATHCONF3resfail {
	post_op_attr	obj_attributes;
};

union PATHCONF3res switch (nfsstat3 status) {
case NFS3_OK:
	PATHCONF3resok	resok;
default:
	PATHCONF3resfail	resfail;
};

/*
 * Arguments to commit (v3).
 */
struct COMMIT3args {
	nfs_fh3		file;
	offset3		offset;
	count3		count;
};

struct COMMIT3resok {
	wcc_data	file_wcc;
	writeverf3	verf;
};

struct COMMIT3resfail {
	wcc_data	file_wcc;
};

union COMMIT3res switch (nfsstat3 status) {
case NFS3_OK:
	COMMIT3resok	resok;
default:
	COMMIT3resfail	resfail;
};

#endif /* WANT_NFS3 */

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
#ifdef WANT_NFS3
program NFS3_PROGRAM {
	version NFS_V3 {
		void
		NFSPROC3_NULL(void)			= 0;

		GETATTR3res
		NFSPROC3_GETATTR(GETATTR3args)		= 1;

		SETATTR3res
		NFSPROC3_SETATTR(SETATTR3args)		= 2;

		LOOKUP3res
		NFSPROC3_LOOKUP(LOOKUP3args)		= 3;

		ACCESS3res
		NFSPROC3_ACCESS(ACCESS3args)		= 4;

		READLINK3res
		NFSPROC3_READLINK(READLINK3args)	= 5;

		READ3res
		NFSPROC3_READ(READ3args)		= 6;

		WRITE3res
		NFSPROC3_WRITE(WRITE3args)		= 7;

		CREATE3res
		NFSPROC3_CREATE(CREATE3args)		= 8;

		MKDIR3res
		NFSPROC3_MKDIR(MKDIR3args)		= 9;

		SYMLINK3res
		NFSPROC3_SYMLINK(SYMLINK3args)		= 10;

		MKNOD3res
		NFSPROC3_MKNOD(MKNOD3args)		= 11;

		REMOVE3res
		NFSPROC3_REMOVE(REMOVE3args)		= 12;

		RMDIR3res
		NFSPROC3_RMDIR(RMDIR3args)		= 13;

		RENAME3res
		NFSPROC3_RENAME(RENAME3args)		= 14;

		LINK3res
		NFSPROC3_LINK(LINK3args)		= 15;

		READDIR3res
		NFSPROC3_READDIR(READDIR3args)		= 16;

		READDIRPLUS3res
		NFSPROC3_READDIRPLUS(READDIRPLUS3args)	= 17;

		FSSTAT3res
		NFSPROC3_FSSTAT(FSSTAT3args)		= 18;

		FSINFO3res
		NFSPROC3_FSINFO(FSINFO3args)		= 19;

		PATHCONF3res
		NFSPROC3_PATHCONF(PATHCONF3args)	= 20;

		COMMIT3res
		NFSPROC3_COMMIT(COMMIT3args)		= 21;
	} = 3;
} = 100003;
#endif

