/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STATFS_H
#define _LINUX_STATFS_H

#include <linux/types.h>
#include <asm/statfs.h>

struct kstatfs {
	long f_type;
	long f_bsize;
	u64 f_blocks;
	u64 f_bfree;
	u64 f_bavail;
	u64 f_files;
	u64 f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

/*
 * Definitions for the flag in f_flag.
 *
 * Generally these flags are equivalent to the MS_ flags used in the mount
 * ABI.  The exception is ST_VALID which has the same value as MS_REMOUNT
 * which doesn't make any sense for statfs.
 */
#define ST_RDONLY	0x0001	/* mount read-only */
#define ST_NOSUID	0x0002	/* ignore suid and sgid bits */
#define ST_NODEV	0x0004	/* disallow access to device special files */
#define ST_NOEXEC	0x0008	/* disallow program execution */
#define ST_SYNCHRONOUS	0x0010	/* writes are synced at once */
#define ST_VALID	0x0020	/* f_flags support is implemented */
#define ST_MANDLOCK	0x0040	/* allow mandatory locks on an FS */
/* 0x0080 used for ST_WRITE in glibc */
/* 0x0100 used for ST_APPEND in glibc */
/* 0x0200 used for ST_IMMUTABLE in glibc */
#define ST_NOATIME	0x0400	/* do not update access times */
#define ST_NODIRATIME	0x0800	/* do not update directory access times */
#define ST_RELATIME	0x1000	/* update atime relative to mtime/ctime */
#define ST_NOSYMFOLLOW	0x2000	/* do not follow symlinks */

struct dentry;
extern int vfs_get_fsid(struct dentry *dentry, __kernel_fsid_t *fsid);

#endif
