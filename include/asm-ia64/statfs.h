#ifndef _ASM_IA64_STATFS_H
#define _ASM_IA64_STATFS_H

/*
 * Based on <asm-i386/statfs.h>.
 *
 * Modified 1998, 1999, 2003
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */

#ifndef __KERNEL_STRICT_NAMES
# include <linux/types.h>
typedef __kernel_fsid_t	fsid_t;
#endif

/*
 * This is ugly --- we're already 64-bit, so just duplicate the definitions
 */
struct statfs {
	long f_type;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_spare[5];
};


struct statfs64 {
	long f_type;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_spare[5];
};

struct compat_statfs64 {
	__u32 f_type;
	__u32 f_bsize;
	__u64 f_blocks;
	__u64 f_bfree;
	__u64 f_bavail;
	__u64 f_files;
	__u64 f_ffree;
	__kernel_fsid_t f_fsid;
	__u32 f_namelen;
	__u32 f_frsize;
	__u32 f_spare[5];
} __attribute__((packed));

#endif /* _ASM_IA64_STATFS_H */
