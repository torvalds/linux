/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
    This file defines the kernel interface of FUSE
    Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    This -- and only this -- header file may also be distributed under
    the terms of the BSD Licence as follows:

    Copyright (C) 2001-2007 Miklos Szeredi. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
    SUCH DAMAGE.
*/

/*
 * This file defines the kernel interface of FUSE
 *
 * Protocol changelog:
 *
 * 7.1:
 *  - add the following messages:
 *      FUSE_SETATTR, FUSE_SYMLINK, FUSE_MKNOD, FUSE_MKDIR, FUSE_UNLINK,
 *      FUSE_RMDIR, FUSE_RENAME, FUSE_LINK, FUSE_OPEN, FUSE_READ, FUSE_WRITE,
 *      FUSE_RELEASE, FUSE_FSYNC, FUSE_FLUSH, FUSE_SETXATTR, FUSE_GETXATTR,
 *      FUSE_LISTXATTR, FUSE_REMOVEXATTR, FUSE_OPENDIR, FUSE_READDIR,
 *      FUSE_RELEASEDIR
 *  - add padding to messages to accommodate 32-bit servers on 64-bit kernels
 *
 * 7.2:
 *  - add FOPEN_DIRECT_IO and FOPEN_KEEP_CACHE flags
 *  - add FUSE_FSYNCDIR message
 *
 * 7.3:
 *  - add FUSE_ACCESS message
 *  - add FUSE_CREATE message
 *  - add filehandle to fuse_setattr_in
 *
 * 7.4:
 *  - add frsize to fuse_kstatfs
 *  - clean up request size limit checking
 *
 * 7.5:
 *  - add flags and max_write to fuse_init_out
 *
 * 7.6:
 *  - add max_readahead to fuse_init_in and fuse_init_out
 *
 * 7.7:
 *  - add FUSE_INTERRUPT message
 *  - add POSIX file lock support
 *
 * 7.8:
 *  - add lock_owner and flags fields to fuse_release_in
 *  - add FUSE_BMAP message
 *  - add FUSE_DESTROY message
 *
 * 7.9:
 *  - new fuse_getattr_in input argument of GETATTR
 *  - add lk_flags in fuse_lk_in
 *  - add lock_owner field to fuse_setattr_in, fuse_read_in and fuse_write_in
 *  - add blksize field to fuse_attr
 *  - add file flags field to fuse_read_in and fuse_write_in
 *  - Add ATIME_NOW and MTIME_NOW flags to fuse_setattr_in
 *
 * 7.10
 *  - add nonseekable open flag
 *
 * 7.11
 *  - add IOCTL message
 *  - add unsolicited notification support
 *  - add POLL message and NOTIFY_POLL notification
 *
 * 7.12
 *  - add umask flag to input argument of create, mknod and mkdir
 *  - add notification messages for invalidation of inodes and
 *    directory entries
 *
 * 7.13
 *  - make max number of background requests and congestion threshold
 *    tunables
 *
 * 7.14
 *  - add splice support to fuse device
 *
 * 7.15
 *  - add store notify
 *  - add retrieve notify
 *
 * 7.16
 *  - add BATCH_FORGET request
 *  - FUSE_IOCTL_UNRESTRICTED shall now return with array of 'struct
 *    fuse_ioctl_iovec' instead of ambiguous 'struct iovec'
 *  - add FUSE_IOCTL_32BIT flag
 *
 * 7.17
 *  - add FUSE_FLOCK_LOCKS and FUSE_RELEASE_FLOCK_UNLOCK
 *
 * 7.18
 *  - add FUSE_IOCTL_DIR flag
 *  - add FUSE_NOTIFY_DELETE
 *
 * 7.19
 *  - add FUSE_FALLOCATE
 *
 * 7.20
 *  - add FUSE_AUTO_INVAL_DATA
 *
 * 7.21
 *  - add FUSE_READDIRPLUS
 *  - send the requested events in POLL request
 *
 * 7.22
 *  - add FUSE_ASYNC_DIO
 *
 * 7.23
 *  - add FUSE_WRITEBACK_CACHE
 *  - add time_gran to fuse_init_out
 *  - add reserved space to fuse_init_out
 *  - add FATTR_CTIME
 *  - add ctime and ctimensec to fuse_setattr_in
 *  - add FUSE_RENAME2 request
 *  - add FUSE_NO_OPEN_SUPPORT flag
 *
 *  7.24
 *  - add FUSE_LSEEK for SEEK_HOLE and SEEK_DATA support
 *
 *  7.25
 *  - add FUSE_PARALLEL_DIROPS
 *
 *  7.26
 *  - add FUSE_HANDLE_KILLPRIV
 *  - add FUSE_POSIX_ACL
 *
 *  7.27
 *  - add FUSE_ABORT_ERROR
 *
 *  7.28
 *  - add FUSE_COPY_FILE_RANGE
 *  - add FOPEN_CACHE_DIR
 *  - add FUSE_MAX_PAGES, add max_pages to init_out
 *  - add FUSE_CACHE_SYMLINKS
 *
 *  7.29
 *  - add FUSE_NO_OPENDIR_SUPPORT flag
 *
 *  7.30
 *  - add FUSE_EXPLICIT_INVAL_DATA
 *  - add FUSE_IOCTL_COMPAT_X32
 *
 *  7.31
 *  - add FUSE_WRITE_KILL_PRIV flag
 *  - add FUSE_SETUPMAPPING and FUSE_REMOVEMAPPING
 *  - add map_alignment to fuse_init_out, add FUSE_MAP_ALIGNMENT flag
 *
 *  7.32
 *  - add flags to fuse_attr, add FUSE_ATTR_SUBMOUNT, add FUSE_SUBMOUNTS
 *
 *  7.33
 *  - add FUSE_HANDLE_KILLPRIV_V2, FUSE_WRITE_KILL_SUIDGID, FATTR_KILL_SUIDGID
 *  - add FUSE_OPEN_KILL_SUIDGID
 *  - extend fuse_setxattr_in, add FUSE_SETXATTR_EXT
 *  - add FUSE_SETXATTR_ACL_KILL_SGID
 *
 *  7.34
 *  - add FUSE_SYNCFS
 *
 *  7.35
 *  - add FOPEN_NOFLUSH
 *
 *  7.36
 *  - extend fuse_init_in with reserved fields, add FUSE_INIT_EXT init flag
 *  - add flags2 to fuse_init_in and fuse_init_out
 *  - add FUSE_SECURITY_CTX init flag
 *  - add security context to create, mkdir, symlink, and mknod requests
 *  - add FUSE_HAS_INODE_DAX, FUSE_ATTR_DAX
 *
 *  7.37
 *  - add FUSE_TMPFILE
 *
 *  7.38
 *  - add FUSE_EXPIRE_ONLY flag to fuse_notify_inval_entry
 *  - add FOPEN_PARALLEL_DIRECT_WRITES
 *  - add total_extlen to fuse_in_header
 *  - add FUSE_MAX_NR_SECCTX
 *  - add extension header
 *  - add FUSE_EXT_GROUPS
 *  - add FUSE_CREATE_SUPP_GROUP
 *  - add FUSE_HAS_EXPIRE_ONLY
 *
 *  7.39
 *  - add FUSE_DIRECT_IO_ALLOW_MMAP
 *  - add FUSE_STATX and related structures
 *
 *  7.40
 *  - add max_stack_depth to fuse_init_out, add FUSE_PASSTHROUGH init flag
 *  - add backing_id to fuse_open_out, add FOPEN_PASSTHROUGH open flag
 *  - add FUSE_NO_EXPORT_SUPPORT init flag
 *  - add FUSE_NOTIFY_RESEND, add FUSE_HAS_RESEND init flag
 *
 *  7.41
 *  - add FUSE_ALLOW_IDMAP
 *  7.42
 *  - Add FUSE_OVER_IO_URING and all other io-uring related flags and data
 *    structures:
 *    - struct fuse_uring_ent_in_out
 *    - struct fuse_uring_req_header
 *    - struct fuse_uring_cmd_req
 *    - FUSE_URING_IN_OUT_HEADER_SZ
 *    - FUSE_URING_OP_IN_OUT_SZ
 *    - enum fuse_uring_cmd
 */

#ifndef _LINUX_FUSE_H
#define _LINUX_FUSE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/*
 * Version negotiation:
 *
 * Both the kernel and userspace send the version they support in the
 * INIT request and reply respectively.
 *
 * If the major versions match then both shall use the smallest
 * of the two minor versions for communication.
 *
 * If the kernel supports a larger major version, then userspace shall
 * reply with the major version it supports, ignore the rest of the
 * INIT message and expect a new INIT message from the kernel with a
 * matching major version.
 *
 * If the library supports a larger major version, then it shall fall
 * back to the major protocol version sent by the kernel for
 * communication and reply with that major version (and an arbitrary
 * supported minor version).
 */

/** Version number of this interface */
#define FUSE_KERNEL_VERSION 7

/** Minor version number of this interface */
#define FUSE_KERNEL_MINOR_VERSION 42

/** The node ID of the root inode */
#define FUSE_ROOT_ID 1

/* Make sure all structures are padded to 64bit boundary, so 32bit
   userspace works under 64bit kernels */

struct fuse_attr {
	uint64_t	ino;
	uint64_t	size;
	uint64_t	blocks;
	uint64_t	atime;
	uint64_t	mtime;
	uint64_t	ctime;
	uint32_t	atimensec;
	uint32_t	mtimensec;
	uint32_t	ctimensec;
	uint32_t	mode;
	uint32_t	nlink;
	uint32_t	uid;
	uint32_t	gid;
	uint32_t	rdev;
	uint32_t	blksize;
	uint32_t	flags;
};

/*
 * The following structures are bit-for-bit compatible with the statx(2) ABI in
 * Linux.
 */
struct fuse_sx_time {
	int64_t		tv_sec;
	uint32_t	tv_nsec;
	int32_t		__reserved;
};

struct fuse_statx {
	uint32_t	mask;
	uint32_t	blksize;
	uint64_t	attributes;
	uint32_t	nlink;
	uint32_t	uid;
	uint32_t	gid;
	uint16_t	mode;
	uint16_t	__spare0[1];
	uint64_t	ino;
	uint64_t	size;
	uint64_t	blocks;
	uint64_t	attributes_mask;
	struct fuse_sx_time	atime;
	struct fuse_sx_time	btime;
	struct fuse_sx_time	ctime;
	struct fuse_sx_time	mtime;
	uint32_t	rdev_major;
	uint32_t	rdev_minor;
	uint32_t	dev_major;
	uint32_t	dev_minor;
	uint64_t	__spare2[14];
};

struct fuse_kstatfs {
	uint64_t	blocks;
	uint64_t	bfree;
	uint64_t	bavail;
	uint64_t	files;
	uint64_t	ffree;
	uint32_t	bsize;
	uint32_t	namelen;
	uint32_t	frsize;
	uint32_t	padding;
	uint32_t	spare[6];
};

struct fuse_file_lock {
	uint64_t	start;
	uint64_t	end;
	uint32_t	type;
	uint32_t	pid; /* tgid */
};

/**
 * Bitmasks for fuse_setattr_in.valid
 */
#define FATTR_MODE	(1 << 0)
#define FATTR_UID	(1 << 1)
#define FATTR_GID	(1 << 2)
#define FATTR_SIZE	(1 << 3)
#define FATTR_ATIME	(1 << 4)
#define FATTR_MTIME	(1 << 5)
#define FATTR_FH	(1 << 6)
#define FATTR_ATIME_NOW	(1 << 7)
#define FATTR_MTIME_NOW	(1 << 8)
#define FATTR_LOCKOWNER	(1 << 9)
#define FATTR_CTIME	(1 << 10)
#define FATTR_KILL_SUIDGID	(1 << 11)

/**
 * Flags returned by the OPEN request
 *
 * FOPEN_DIRECT_IO: bypass page cache for this open file
 * FOPEN_KEEP_CACHE: don't invalidate the data cache on open
 * FOPEN_NONSEEKABLE: the file is not seekable
 * FOPEN_CACHE_DIR: allow caching this directory
 * FOPEN_STREAM: the file is stream-like (no file position at all)
 * FOPEN_NOFLUSH: don't flush data cache on close (unless FUSE_WRITEBACK_CACHE)
 * FOPEN_PARALLEL_DIRECT_WRITES: Allow concurrent direct writes on the same inode
 * FOPEN_PASSTHROUGH: passthrough read/write io for this open file
 */
#define FOPEN_DIRECT_IO		(1 << 0)
#define FOPEN_KEEP_CACHE	(1 << 1)
#define FOPEN_NONSEEKABLE	(1 << 2)
#define FOPEN_CACHE_DIR		(1 << 3)
#define FOPEN_STREAM		(1 << 4)
#define FOPEN_NOFLUSH		(1 << 5)
#define FOPEN_PARALLEL_DIRECT_WRITES	(1 << 6)
#define FOPEN_PASSTHROUGH	(1 << 7)

/**
 * INIT request/reply flags
 *
 * FUSE_ASYNC_READ: asynchronous read requests
 * FUSE_POSIX_LOCKS: remote locking for POSIX file locks
 * FUSE_FILE_OPS: kernel sends file handle for fstat, etc... (not yet supported)
 * FUSE_ATOMIC_O_TRUNC: handles the O_TRUNC open flag in the filesystem
 * FUSE_EXPORT_SUPPORT: filesystem handles lookups of "." and ".."
 * FUSE_BIG_WRITES: filesystem can handle write size larger than 4kB
 * FUSE_DONT_MASK: don't apply umask to file mode on create operations
 * FUSE_SPLICE_WRITE: kernel supports splice write on the device
 * FUSE_SPLICE_MOVE: kernel supports splice move on the device
 * FUSE_SPLICE_READ: kernel supports splice read on the device
 * FUSE_FLOCK_LOCKS: remote locking for BSD style file locks
 * FUSE_HAS_IOCTL_DIR: kernel supports ioctl on directories
 * FUSE_AUTO_INVAL_DATA: automatically invalidate cached pages
 * FUSE_DO_READDIRPLUS: do READDIRPLUS (READDIR+LOOKUP in one)
 * FUSE_READDIRPLUS_AUTO: adaptive readdirplus
 * FUSE_ASYNC_DIO: asynchronous direct I/O submission
 * FUSE_WRITEBACK_CACHE: use writeback cache for buffered writes
 * FUSE_NO_OPEN_SUPPORT: kernel supports zero-message opens
 * FUSE_PARALLEL_DIROPS: allow parallel lookups and readdir
 * FUSE_HANDLE_KILLPRIV: fs handles killing suid/sgid/cap on write/chown/trunc
 * FUSE_POSIX_ACL: filesystem supports posix acls
 * FUSE_ABORT_ERROR: reading the device after abort returns ECONNABORTED
 * FUSE_MAX_PAGES: init_out.max_pages contains the max number of req pages
 * FUSE_CACHE_SYMLINKS: cache READLINK responses
 * FUSE_NO_OPENDIR_SUPPORT: kernel supports zero-message opendir
 * FUSE_EXPLICIT_INVAL_DATA: only invalidate cached pages on explicit request
 * FUSE_MAP_ALIGNMENT: init_out.map_alignment contains log2(byte alignment) for
 *		       foffset and moffset fields in struct
 *		       fuse_setupmapping_out and fuse_removemapping_one.
 * FUSE_SUBMOUNTS: kernel supports auto-mounting directory submounts
 * FUSE_HANDLE_KILLPRIV_V2: fs kills suid/sgid/cap on write/chown/trunc.
 *			Upon write/truncate suid/sgid is only killed if caller
 *			does not have CAP_FSETID. Additionally upon
 *			write/truncate sgid is killed only if file has group
 *			execute permission. (Same as Linux VFS behavior).
 * FUSE_SETXATTR_EXT:	Server supports extended struct fuse_setxattr_in
 * FUSE_INIT_EXT: extended fuse_init_in request
 * FUSE_INIT_RESERVED: reserved, do not use
 * FUSE_SECURITY_CTX:	add security context to create, mkdir, symlink, and
 *			mknod
 * FUSE_HAS_INODE_DAX:  use per inode DAX
 * FUSE_CREATE_SUPP_GROUP: add supplementary group info to create, mkdir,
 *			symlink and mknod (single group that matches parent)
 * FUSE_HAS_EXPIRE_ONLY: kernel supports expiry-only entry invalidation
 * FUSE_DIRECT_IO_ALLOW_MMAP: allow shared mmap in FOPEN_DIRECT_IO mode.
 * FUSE_NO_EXPORT_SUPPORT: explicitly disable export support
 * FUSE_HAS_RESEND: kernel supports resending pending requests, and the high bit
 *		    of the request ID indicates resend requests
 * FUSE_ALLOW_IDMAP: allow creation of idmapped mounts
 * FUSE_OVER_IO_URING: Indicate that client supports io-uring
 */
#define FUSE_ASYNC_READ		(1 << 0)
#define FUSE_POSIX_LOCKS	(1 << 1)
#define FUSE_FILE_OPS		(1 << 2)
#define FUSE_ATOMIC_O_TRUNC	(1 << 3)
#define FUSE_EXPORT_SUPPORT	(1 << 4)
#define FUSE_BIG_WRITES		(1 << 5)
#define FUSE_DONT_MASK		(1 << 6)
#define FUSE_SPLICE_WRITE	(1 << 7)
#define FUSE_SPLICE_MOVE	(1 << 8)
#define FUSE_SPLICE_READ	(1 << 9)
#define FUSE_FLOCK_LOCKS	(1 << 10)
#define FUSE_HAS_IOCTL_DIR	(1 << 11)
#define FUSE_AUTO_INVAL_DATA	(1 << 12)
#define FUSE_DO_READDIRPLUS	(1 << 13)
#define FUSE_READDIRPLUS_AUTO	(1 << 14)
#define FUSE_ASYNC_DIO		(1 << 15)
#define FUSE_WRITEBACK_CACHE	(1 << 16)
#define FUSE_NO_OPEN_SUPPORT	(1 << 17)
#define FUSE_PARALLEL_DIROPS    (1 << 18)
#define FUSE_HANDLE_KILLPRIV	(1 << 19)
#define FUSE_POSIX_ACL		(1 << 20)
#define FUSE_ABORT_ERROR	(1 << 21)
#define FUSE_MAX_PAGES		(1 << 22)
#define FUSE_CACHE_SYMLINKS	(1 << 23)
#define FUSE_NO_OPENDIR_SUPPORT (1 << 24)
#define FUSE_EXPLICIT_INVAL_DATA (1 << 25)
#define FUSE_MAP_ALIGNMENT	(1 << 26)
#define FUSE_SUBMOUNTS		(1 << 27)
#define FUSE_HANDLE_KILLPRIV_V2	(1 << 28)
#define FUSE_SETXATTR_EXT	(1 << 29)
#define FUSE_INIT_EXT		(1 << 30)
#define FUSE_INIT_RESERVED	(1 << 31)
/* bits 32..63 get shifted down 32 bits into the flags2 field */
#define FUSE_SECURITY_CTX	(1ULL << 32)
#define FUSE_HAS_INODE_DAX	(1ULL << 33)
#define FUSE_CREATE_SUPP_GROUP	(1ULL << 34)
#define FUSE_HAS_EXPIRE_ONLY	(1ULL << 35)
#define FUSE_DIRECT_IO_ALLOW_MMAP (1ULL << 36)
#define FUSE_PASSTHROUGH	(1ULL << 37)
#define FUSE_NO_EXPORT_SUPPORT	(1ULL << 38)
#define FUSE_HAS_RESEND		(1ULL << 39)

/* Obsolete alias for FUSE_DIRECT_IO_ALLOW_MMAP */
#define FUSE_DIRECT_IO_RELAX	FUSE_DIRECT_IO_ALLOW_MMAP
#define FUSE_ALLOW_IDMAP	(1ULL << 40)
#define FUSE_OVER_IO_URING	(1ULL << 41)

/**
 * CUSE INIT request/reply flags
 *
 * CUSE_UNRESTRICTED_IOCTL:  use unrestricted ioctl
 */
#define CUSE_UNRESTRICTED_IOCTL	(1 << 0)

/**
 * Release flags
 */
#define FUSE_RELEASE_FLUSH	(1 << 0)
#define FUSE_RELEASE_FLOCK_UNLOCK	(1 << 1)

/**
 * Getattr flags
 */
#define FUSE_GETATTR_FH		(1 << 0)

/**
 * Lock flags
 */
#define FUSE_LK_FLOCK		(1 << 0)

/**
 * WRITE flags
 *
 * FUSE_WRITE_CACHE: delayed write from page cache, file handle is guessed
 * FUSE_WRITE_LOCKOWNER: lock_owner field is valid
 * FUSE_WRITE_KILL_SUIDGID: kill suid and sgid bits
 */
#define FUSE_WRITE_CACHE	(1 << 0)
#define FUSE_WRITE_LOCKOWNER	(1 << 1)
#define FUSE_WRITE_KILL_SUIDGID (1 << 2)

/* Obsolete alias; this flag implies killing suid/sgid only. */
#define FUSE_WRITE_KILL_PRIV	FUSE_WRITE_KILL_SUIDGID

/**
 * Read flags
 */
#define FUSE_READ_LOCKOWNER	(1 << 1)

/**
 * Ioctl flags
 *
 * FUSE_IOCTL_COMPAT: 32bit compat ioctl on 64bit machine
 * FUSE_IOCTL_UNRESTRICTED: not restricted to well-formed ioctls, retry allowed
 * FUSE_IOCTL_RETRY: retry with new iovecs
 * FUSE_IOCTL_32BIT: 32bit ioctl
 * FUSE_IOCTL_DIR: is a directory
 * FUSE_IOCTL_COMPAT_X32: x32 compat ioctl on 64bit machine (64bit time_t)
 *
 * FUSE_IOCTL_MAX_IOV: maximum of in_iovecs + out_iovecs
 */
#define FUSE_IOCTL_COMPAT	(1 << 0)
#define FUSE_IOCTL_UNRESTRICTED	(1 << 1)
#define FUSE_IOCTL_RETRY	(1 << 2)
#define FUSE_IOCTL_32BIT	(1 << 3)
#define FUSE_IOCTL_DIR		(1 << 4)
#define FUSE_IOCTL_COMPAT_X32	(1 << 5)

#define FUSE_IOCTL_MAX_IOV	256

/**
 * Poll flags
 *
 * FUSE_POLL_SCHEDULE_NOTIFY: request poll notify
 */
#define FUSE_POLL_SCHEDULE_NOTIFY (1 << 0)

/**
 * Fsync flags
 *
 * FUSE_FSYNC_FDATASYNC: Sync data only, not metadata
 */
#define FUSE_FSYNC_FDATASYNC	(1 << 0)

/**
 * fuse_attr flags
 *
 * FUSE_ATTR_SUBMOUNT: Object is a submount root
 * FUSE_ATTR_DAX: Enable DAX for this file in per inode DAX mode
 */
#define FUSE_ATTR_SUBMOUNT      (1 << 0)
#define FUSE_ATTR_DAX		(1 << 1)

/**
 * Open flags
 * FUSE_OPEN_KILL_SUIDGID: Kill suid and sgid if executable
 */
#define FUSE_OPEN_KILL_SUIDGID	(1 << 0)

/**
 * setxattr flags
 * FUSE_SETXATTR_ACL_KILL_SGID: Clear SGID when system.posix_acl_access is set
 */
#define FUSE_SETXATTR_ACL_KILL_SGID	(1 << 0)

/**
 * notify_inval_entry flags
 * FUSE_EXPIRE_ONLY
 */
#define FUSE_EXPIRE_ONLY		(1 << 0)

/**
 * extension type
 * FUSE_MAX_NR_SECCTX: maximum value of &fuse_secctx_header.nr_secctx
 * FUSE_EXT_GROUPS: &fuse_supp_groups extension
 */
enum fuse_ext_type {
	/* Types 0..31 are reserved for fuse_secctx_header */
	FUSE_MAX_NR_SECCTX	= 31,
	FUSE_EXT_GROUPS		= 32,
};

enum fuse_opcode {
	FUSE_LOOKUP		= 1,
	FUSE_FORGET		= 2,  /* no reply */
	FUSE_GETATTR		= 3,
	FUSE_SETATTR		= 4,
	FUSE_READLINK		= 5,
	FUSE_SYMLINK		= 6,
	FUSE_MKNOD		= 8,
	FUSE_MKDIR		= 9,
	FUSE_UNLINK		= 10,
	FUSE_RMDIR		= 11,
	FUSE_RENAME		= 12,
	FUSE_LINK		= 13,
	FUSE_OPEN		= 14,
	FUSE_READ		= 15,
	FUSE_WRITE		= 16,
	FUSE_STATFS		= 17,
	FUSE_RELEASE		= 18,
	FUSE_FSYNC		= 20,
	FUSE_SETXATTR		= 21,
	FUSE_GETXATTR		= 22,
	FUSE_LISTXATTR		= 23,
	FUSE_REMOVEXATTR	= 24,
	FUSE_FLUSH		= 25,
	FUSE_INIT		= 26,
	FUSE_OPENDIR		= 27,
	FUSE_READDIR		= 28,
	FUSE_RELEASEDIR		= 29,
	FUSE_FSYNCDIR		= 30,
	FUSE_GETLK		= 31,
	FUSE_SETLK		= 32,
	FUSE_SETLKW		= 33,
	FUSE_ACCESS		= 34,
	FUSE_CREATE		= 35,
	FUSE_INTERRUPT		= 36,
	FUSE_BMAP		= 37,
	FUSE_DESTROY		= 38,
	FUSE_IOCTL		= 39,
	FUSE_POLL		= 40,
	FUSE_NOTIFY_REPLY	= 41,
	FUSE_BATCH_FORGET	= 42,
	FUSE_FALLOCATE		= 43,
	FUSE_READDIRPLUS	= 44,
	FUSE_RENAME2		= 45,
	FUSE_LSEEK		= 46,
	FUSE_COPY_FILE_RANGE	= 47,
	FUSE_SETUPMAPPING	= 48,
	FUSE_REMOVEMAPPING	= 49,
	FUSE_SYNCFS		= 50,
	FUSE_TMPFILE		= 51,
	FUSE_STATX		= 52,

	/* CUSE specific operations */
	CUSE_INIT		= 4096,

	/* Reserved opcodes: helpful to detect structure endian-ness */
	CUSE_INIT_BSWAP_RESERVED	= 1048576,	/* CUSE_INIT << 8 */
	FUSE_INIT_BSWAP_RESERVED	= 436207616,	/* FUSE_INIT << 24 */
};

enum fuse_notify_code {
	FUSE_NOTIFY_POLL   = 1,
	FUSE_NOTIFY_INVAL_INODE = 2,
	FUSE_NOTIFY_INVAL_ENTRY = 3,
	FUSE_NOTIFY_STORE = 4,
	FUSE_NOTIFY_RETRIEVE = 5,
	FUSE_NOTIFY_DELETE = 6,
	FUSE_NOTIFY_RESEND = 7,
	FUSE_NOTIFY_CODE_MAX,
};

/* The read buffer is required to be at least 8k, but may be much larger */
#define FUSE_MIN_READ_BUFFER 8192

#define FUSE_COMPAT_ENTRY_OUT_SIZE 120

struct fuse_entry_out {
	uint64_t	nodeid;		/* Inode ID */
	uint64_t	generation;	/* Inode generation: nodeid:gen must
					   be unique for the fs's lifetime */
	uint64_t	entry_valid;	/* Cache timeout for the name */
	uint64_t	attr_valid;	/* Cache timeout for the attributes */
	uint32_t	entry_valid_nsec;
	uint32_t	attr_valid_nsec;
	struct fuse_attr attr;
};

struct fuse_forget_in {
	uint64_t	nlookup;
};

struct fuse_forget_one {
	uint64_t	nodeid;
	uint64_t	nlookup;
};

struct fuse_batch_forget_in {
	uint32_t	count;
	uint32_t	dummy;
};

struct fuse_getattr_in {
	uint32_t	getattr_flags;
	uint32_t	dummy;
	uint64_t	fh;
};

#define FUSE_COMPAT_ATTR_OUT_SIZE 96

struct fuse_attr_out {
	uint64_t	attr_valid;	/* Cache timeout for the attributes */
	uint32_t	attr_valid_nsec;
	uint32_t	dummy;
	struct fuse_attr attr;
};

struct fuse_statx_in {
	uint32_t	getattr_flags;
	uint32_t	reserved;
	uint64_t	fh;
	uint32_t	sx_flags;
	uint32_t	sx_mask;
};

struct fuse_statx_out {
	uint64_t	attr_valid;	/* Cache timeout for the attributes */
	uint32_t	attr_valid_nsec;
	uint32_t	flags;
	uint64_t	spare[2];
	struct fuse_statx stat;
};

#define FUSE_COMPAT_MKNOD_IN_SIZE 8

struct fuse_mknod_in {
	uint32_t	mode;
	uint32_t	rdev;
	uint32_t	umask;
	uint32_t	padding;
};

struct fuse_mkdir_in {
	uint32_t	mode;
	uint32_t	umask;
};

struct fuse_rename_in {
	uint64_t	newdir;
};

struct fuse_rename2_in {
	uint64_t	newdir;
	uint32_t	flags;
	uint32_t	padding;
};

struct fuse_link_in {
	uint64_t	oldnodeid;
};

struct fuse_setattr_in {
	uint32_t	valid;
	uint32_t	padding;
	uint64_t	fh;
	uint64_t	size;
	uint64_t	lock_owner;
	uint64_t	atime;
	uint64_t	mtime;
	uint64_t	ctime;
	uint32_t	atimensec;
	uint32_t	mtimensec;
	uint32_t	ctimensec;
	uint32_t	mode;
	uint32_t	unused4;
	uint32_t	uid;
	uint32_t	gid;
	uint32_t	unused5;
};

struct fuse_open_in {
	uint32_t	flags;
	uint32_t	open_flags;	/* FUSE_OPEN_... */
};

struct fuse_create_in {
	uint32_t	flags;
	uint32_t	mode;
	uint32_t	umask;
	uint32_t	open_flags;	/* FUSE_OPEN_... */
};

struct fuse_open_out {
	uint64_t	fh;
	uint32_t	open_flags;
	int32_t		backing_id;
};

struct fuse_release_in {
	uint64_t	fh;
	uint32_t	flags;
	uint32_t	release_flags;
	uint64_t	lock_owner;
};

struct fuse_flush_in {
	uint64_t	fh;
	uint32_t	unused;
	uint32_t	padding;
	uint64_t	lock_owner;
};

struct fuse_read_in {
	uint64_t	fh;
	uint64_t	offset;
	uint32_t	size;
	uint32_t	read_flags;
	uint64_t	lock_owner;
	uint32_t	flags;
	uint32_t	padding;
};

#define FUSE_COMPAT_WRITE_IN_SIZE 24

struct fuse_write_in {
	uint64_t	fh;
	uint64_t	offset;
	uint32_t	size;
	uint32_t	write_flags;
	uint64_t	lock_owner;
	uint32_t	flags;
	uint32_t	padding;
};

struct fuse_write_out {
	uint32_t	size;
	uint32_t	padding;
};

#define FUSE_COMPAT_STATFS_SIZE 48

struct fuse_statfs_out {
	struct fuse_kstatfs st;
};

struct fuse_fsync_in {
	uint64_t	fh;
	uint32_t	fsync_flags;
	uint32_t	padding;
};

#define FUSE_COMPAT_SETXATTR_IN_SIZE 8

struct fuse_setxattr_in {
	uint32_t	size;
	uint32_t	flags;
	uint32_t	setxattr_flags;
	uint32_t	padding;
};

struct fuse_getxattr_in {
	uint32_t	size;
	uint32_t	padding;
};

struct fuse_getxattr_out {
	uint32_t	size;
	uint32_t	padding;
};

struct fuse_lk_in {
	uint64_t	fh;
	uint64_t	owner;
	struct fuse_file_lock lk;
	uint32_t	lk_flags;
	uint32_t	padding;
};

struct fuse_lk_out {
	struct fuse_file_lock lk;
};

struct fuse_access_in {
	uint32_t	mask;
	uint32_t	padding;
};

struct fuse_init_in {
	uint32_t	major;
	uint32_t	minor;
	uint32_t	max_readahead;
	uint32_t	flags;
	uint32_t	flags2;
	uint32_t	unused[11];
};

#define FUSE_COMPAT_INIT_OUT_SIZE 8
#define FUSE_COMPAT_22_INIT_OUT_SIZE 24

struct fuse_init_out {
	uint32_t	major;
	uint32_t	minor;
	uint32_t	max_readahead;
	uint32_t	flags;
	uint16_t	max_background;
	uint16_t	congestion_threshold;
	uint32_t	max_write;
	uint32_t	time_gran;
	uint16_t	max_pages;
	uint16_t	map_alignment;
	uint32_t	flags2;
	uint32_t	max_stack_depth;
	uint32_t	unused[6];
};

#define CUSE_INIT_INFO_MAX 4096

struct cuse_init_in {
	uint32_t	major;
	uint32_t	minor;
	uint32_t	unused;
	uint32_t	flags;
};

struct cuse_init_out {
	uint32_t	major;
	uint32_t	minor;
	uint32_t	unused;
	uint32_t	flags;
	uint32_t	max_read;
	uint32_t	max_write;
	uint32_t	dev_major;		/* chardev major */
	uint32_t	dev_minor;		/* chardev minor */
	uint32_t	spare[10];
};

struct fuse_interrupt_in {
	uint64_t	unique;
};

struct fuse_bmap_in {
	uint64_t	block;
	uint32_t	blocksize;
	uint32_t	padding;
};

struct fuse_bmap_out {
	uint64_t	block;
};

struct fuse_ioctl_in {
	uint64_t	fh;
	uint32_t	flags;
	uint32_t	cmd;
	uint64_t	arg;
	uint32_t	in_size;
	uint32_t	out_size;
};

struct fuse_ioctl_iovec {
	uint64_t	base;
	uint64_t	len;
};

struct fuse_ioctl_out {
	int32_t		result;
	uint32_t	flags;
	uint32_t	in_iovs;
	uint32_t	out_iovs;
};

struct fuse_poll_in {
	uint64_t	fh;
	uint64_t	kh;
	uint32_t	flags;
	uint32_t	events;
};

struct fuse_poll_out {
	uint32_t	revents;
	uint32_t	padding;
};

struct fuse_notify_poll_wakeup_out {
	uint64_t	kh;
};

struct fuse_fallocate_in {
	uint64_t	fh;
	uint64_t	offset;
	uint64_t	length;
	uint32_t	mode;
	uint32_t	padding;
};

/**
 * FUSE request unique ID flag
 *
 * Indicates whether this is a resend request. The receiver should handle this
 * request accordingly.
 */
#define FUSE_UNIQUE_RESEND (1ULL << 63)

/**
 * This value will be set by the kernel to
 * (struct fuse_in_header).{uid,gid} fields in
 * case when:
 * - fuse daemon enabled FUSE_ALLOW_IDMAP
 * - idmapping information is not available and uid/gid
 *   can not be mapped in accordance with an idmapping.
 *
 * Note: an idmapping information always available
 * for inode creation operations like:
 * FUSE_MKNOD, FUSE_SYMLINK, FUSE_MKDIR, FUSE_TMPFILE,
 * FUSE_CREATE and FUSE_RENAME2 (with RENAME_WHITEOUT).
 */
#define FUSE_INVALID_UIDGID ((uint32_t)(-1))

struct fuse_in_header {
	uint32_t	len;
	uint32_t	opcode;
	uint64_t	unique;
	uint64_t	nodeid;
	uint32_t	uid;
	uint32_t	gid;
	uint32_t	pid;
	uint16_t	total_extlen; /* length of extensions in 8byte units */
	uint16_t	padding;
};

struct fuse_out_header {
	uint32_t	len;
	int32_t		error;
	uint64_t	unique;
};

struct fuse_dirent {
	uint64_t	ino;
	uint64_t	off;
	uint32_t	namelen;
	uint32_t	type;
	char name[];
};

/* Align variable length records to 64bit boundary */
#define FUSE_REC_ALIGN(x) \
	(((x) + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) - 1))

#define FUSE_NAME_OFFSET offsetof(struct fuse_dirent, name)
#define FUSE_DIRENT_ALIGN(x) FUSE_REC_ALIGN(x)
#define FUSE_DIRENT_SIZE(d) \
	FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + (d)->namelen)

struct fuse_direntplus {
	struct fuse_entry_out entry_out;
	struct fuse_dirent dirent;
};

#define FUSE_NAME_OFFSET_DIRENTPLUS \
	offsetof(struct fuse_direntplus, dirent.name)
#define FUSE_DIRENTPLUS_SIZE(d) \
	FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET_DIRENTPLUS + (d)->dirent.namelen)

struct fuse_notify_inval_inode_out {
	uint64_t	ino;
	int64_t		off;
	int64_t		len;
};

struct fuse_notify_inval_entry_out {
	uint64_t	parent;
	uint32_t	namelen;
	uint32_t	flags;
};

struct fuse_notify_delete_out {
	uint64_t	parent;
	uint64_t	child;
	uint32_t	namelen;
	uint32_t	padding;
};

struct fuse_notify_store_out {
	uint64_t	nodeid;
	uint64_t	offset;
	uint32_t	size;
	uint32_t	padding;
};

struct fuse_notify_retrieve_out {
	uint64_t	notify_unique;
	uint64_t	nodeid;
	uint64_t	offset;
	uint32_t	size;
	uint32_t	padding;
};

/* Matches the size of fuse_write_in */
struct fuse_notify_retrieve_in {
	uint64_t	dummy1;
	uint64_t	offset;
	uint32_t	size;
	uint32_t	dummy2;
	uint64_t	dummy3;
	uint64_t	dummy4;
};

struct fuse_backing_map {
	int32_t		fd;
	uint32_t	flags;
	uint64_t	padding;
};

/* Device ioctls: */
#define FUSE_DEV_IOC_MAGIC		229
#define FUSE_DEV_IOC_CLONE		_IOR(FUSE_DEV_IOC_MAGIC, 0, uint32_t)
#define FUSE_DEV_IOC_BACKING_OPEN	_IOW(FUSE_DEV_IOC_MAGIC, 1, \
					     struct fuse_backing_map)
#define FUSE_DEV_IOC_BACKING_CLOSE	_IOW(FUSE_DEV_IOC_MAGIC, 2, uint32_t)

struct fuse_lseek_in {
	uint64_t	fh;
	uint64_t	offset;
	uint32_t	whence;
	uint32_t	padding;
};

struct fuse_lseek_out {
	uint64_t	offset;
};

struct fuse_copy_file_range_in {
	uint64_t	fh_in;
	uint64_t	off_in;
	uint64_t	nodeid_out;
	uint64_t	fh_out;
	uint64_t	off_out;
	uint64_t	len;
	uint64_t	flags;
};

#define FUSE_SETUPMAPPING_FLAG_WRITE (1ull << 0)
#define FUSE_SETUPMAPPING_FLAG_READ (1ull << 1)
struct fuse_setupmapping_in {
	/* An already open handle */
	uint64_t	fh;
	/* Offset into the file to start the mapping */
	uint64_t	foffset;
	/* Length of mapping required */
	uint64_t	len;
	/* Flags, FUSE_SETUPMAPPING_FLAG_* */
	uint64_t	flags;
	/* Offset in Memory Window */
	uint64_t	moffset;
};

struct fuse_removemapping_in {
	/* number of fuse_removemapping_one follows */
	uint32_t        count;
};

struct fuse_removemapping_one {
	/* Offset into the dax window start the unmapping */
	uint64_t        moffset;
	/* Length of mapping required */
	uint64_t	len;
};

#define FUSE_REMOVEMAPPING_MAX_ENTRY   \
		(PAGE_SIZE / sizeof(struct fuse_removemapping_one))

struct fuse_syncfs_in {
	uint64_t	padding;
};

/*
 * For each security context, send fuse_secctx with size of security context
 * fuse_secctx will be followed by security context name and this in turn
 * will be followed by actual context label.
 * fuse_secctx, name, context
 */
struct fuse_secctx {
	uint32_t	size;
	uint32_t	padding;
};

/*
 * Contains the information about how many fuse_secctx structures are being
 * sent and what's the total size of all security contexts (including
 * size of fuse_secctx_header).
 *
 */
struct fuse_secctx_header {
	uint32_t	size;
	uint32_t	nr_secctx;
};

/**
 * struct fuse_ext_header - extension header
 * @size: total size of this extension including this header
 * @type: type of extension
 *
 * This is made compatible with fuse_secctx_header by using type values >
 * FUSE_MAX_NR_SECCTX
 */
struct fuse_ext_header {
	uint32_t	size;
	uint32_t	type;
};

/**
 * struct fuse_supp_groups - Supplementary group extension
 * @nr_groups: number of supplementary groups
 * @groups: flexible array of group IDs
 */
struct fuse_supp_groups {
	uint32_t	nr_groups;
	uint32_t	groups[];
};

/**
 * Size of the ring buffer header
 */
#define FUSE_URING_IN_OUT_HEADER_SZ 128
#define FUSE_URING_OP_IN_OUT_SZ 128

/* Used as part of the fuse_uring_req_header */
struct fuse_uring_ent_in_out {
	uint64_t flags;

	/*
	 * commit ID to be used in a reply to a ring request (see also
	 * struct fuse_uring_cmd_req)
	 */
	uint64_t commit_id;

	/* size of user payload buffer */
	uint32_t payload_sz;
	uint32_t padding;

	uint64_t reserved;
};

/**
 * Header for all fuse-io-uring requests
 */
struct fuse_uring_req_header {
	/* struct fuse_in_header / struct fuse_out_header */
	char in_out[FUSE_URING_IN_OUT_HEADER_SZ];

	/* per op code header */
	char op_in[FUSE_URING_OP_IN_OUT_SZ];

	struct fuse_uring_ent_in_out ring_ent_in_out;
};

/**
 * sqe commands to the kernel
 */
enum fuse_uring_cmd {
	FUSE_IO_URING_CMD_INVALID = 0,

	/* register the request buffer and fetch a fuse request */
	FUSE_IO_URING_CMD_REGISTER = 1,

	/* commit fuse request result and fetch next request */
	FUSE_IO_URING_CMD_COMMIT_AND_FETCH = 2,
};

/**
 * In the 80B command area of the SQE.
 */
struct fuse_uring_cmd_req {
	uint64_t flags;

	/* entry identifier for commits */
	uint64_t commit_id;

	/* queue the command is for (queue index) */
	uint16_t qid;
	uint8_t padding[6];
};

#endif /* _LINUX_FUSE_H */
