#ifndef _GENERIC_STATFS_H
#define _GENERIC_STATFS_H

#include <linux/types.h>

#ifdef __KERNEL__
typedef __kernel_fsid_t	fsid_t;
#endif

/*
 * Most 64-bit platforms use 'long', while most 32-bit platforms use '__u32'.
 * Yes, they differ in signedness as well as size.
 * Special cases can override it for themselves -- except for S390x, which
 * is just a little too special for us. And MIPS, which I'm not touching
 * with a 10' pole.
 */
#ifndef __statfs_word
#if BITS_PER_LONG == 64
#define __statfs_word long
#else
#define __statfs_word __u32
#endif
#endif

struct statfs {
	__statfs_word f_type;
	__statfs_word f_bsize;
	__statfs_word f_blocks;
	__statfs_word f_bfree;
	__statfs_word f_bavail;
	__statfs_word f_files;
	__statfs_word f_ffree;
	__kernel_fsid_t f_fsid;
	__statfs_word f_namelen;
	__statfs_word f_frsize;
	__statfs_word f_flags;
	__statfs_word f_spare[4];
};

/*
 * ARM needs to avoid the 32-bit padding at the end, for consistency
 * between EABI and OABI 
 */
#ifndef ARCH_PACK_STATFS64
#define ARCH_PACK_STATFS64
#endif

struct statfs64 {
	__statfs_word f_type;
	__statfs_word f_bsize;
	__u64 f_blocks;
	__u64 f_bfree;
	__u64 f_bavail;
	__u64 f_files;
	__u64 f_ffree;
	__kernel_fsid_t f_fsid;
	__statfs_word f_namelen;
	__statfs_word f_frsize;
	__statfs_word f_flags;
	__statfs_word f_spare[4];
} ARCH_PACK_STATFS64;

/* 
 * IA64 and x86_64 need to avoid the 32-bit padding at the end,
 * to be compatible with the i386 ABI
 */
#ifndef ARCH_PACK_COMPAT_STATFS64
#define ARCH_PACK_COMPAT_STATFS64
#endif

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
	__u32 f_flags;
	__u32 f_spare[4];
} ARCH_PACK_COMPAT_STATFS64;

#endif
