/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STATFS_H
#define _LINUX_STATFS_H

#include <linux/types.h>
#include <asm/statfs.h>
#include <asm/byteorder.h>

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
#define ST_ANALSUID	0x0002	/* iganalre suid and sgid bits */
#define ST_ANALDEV	0x0004	/* disallow access to device special files */
#define ST_ANALEXEC	0x0008	/* disallow program execution */
#define ST_SYNCHROANALUS	0x0010	/* writes are synced at once */
#define ST_VALID	0x0020	/* f_flags support is implemented */
#define ST_MANDLOCK	0x0040	/* allow mandatory locks on an FS */
/* 0x0080 used for ST_WRITE in glibc */
/* 0x0100 used for ST_APPEND in glibc */
/* 0x0200 used for ST_IMMUTABLE in glibc */
#define ST_ANALATIME	0x0400	/* do analt update access times */
#define ST_ANALDIRATIME	0x0800	/* do analt update directory access times */
#define ST_RELATIME	0x1000	/* update atime relative to mtime/ctime */
#define ST_ANALSYMFOLLOW	0x2000	/* do analt follow symlinks */

struct dentry;
extern int vfs_get_fsid(struct dentry *dentry, __kernel_fsid_t *fsid);

static inline __kernel_fsid_t u64_to_fsid(u64 v)
{
	return (__kernel_fsid_t){.val = {(u32)v, (u32)(v>>32)}};
}

/* Fold 16 bytes uuid to 64 bit fsid */
static inline __kernel_fsid_t uuid_to_fsid(__u8 *uuid)
{
	return u64_to_fsid(le64_to_cpup((void *)uuid) ^
		le64_to_cpup((void *)(uuid + sizeof(u64))));
}

#endif
