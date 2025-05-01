/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_STAT_H
#define _UAPI_LINUX_STAT_H

#include <linux/types.h>

#if defined(__KERNEL__) || !defined(__GLIBC__) || (__GLIBC__ < 2)

#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100

#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010

#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#endif

/*
 * Timestamp structure for the timestamps in struct statx.
 *
 * tv_sec holds the number of seconds before (negative) or after (positive)
 * 00:00:00 1st January 1970 UTC.
 *
 * tv_nsec holds a number of nanoseconds (0..999,999,999) after the tv_sec time.
 *
 * __reserved is held in case we need a yet finer resolution.
 */
struct statx_timestamp {
	__s64	tv_sec;
	__u32	tv_nsec;
	__s32	__reserved;
};

/*
 * Structures for the extended file attribute retrieval system call
 * (statx()).
 *
 * The caller passes a mask of what they're specifically interested in as a
 * parameter to statx().  What statx() actually got will be indicated in
 * st_mask upon return.
 *
 * For each bit in the mask argument:
 *
 * - if the datum is not supported:
 *
 *   - the bit will be cleared, and
 *
 *   - the datum will be set to an appropriate fabricated value if one is
 *     available (eg. CIFS can take a default uid and gid), otherwise
 *
 *   - the field will be cleared;
 *
 * - otherwise, if explicitly requested:
 *
 *   - the datum will be synchronised to the server if AT_STATX_FORCE_SYNC is
 *     set or if the datum is considered out of date, and
 *
 *   - the field will be filled in and the bit will be set;
 *
 * - otherwise, if not requested, but available in approximate form without any
 *   effort, it will be filled in anyway, and the bit will be set upon return
 *   (it might not be up to date, however, and no attempt will be made to
 *   synchronise the internal state first);
 *
 * - otherwise the field and the bit will be cleared before returning.
 *
 * Items in STATX_BASIC_STATS may be marked unavailable on return, but they
 * will have values installed for compatibility purposes so that stat() and
 * co. can be emulated in userspace.
 */
struct statx {
	/* 0x00 */
	/* What results were written [uncond] */
	__u32	stx_mask;

	/* Preferred general I/O size [uncond] */
	__u32	stx_blksize;

	/* Flags conveying information about the file [uncond] */
	__u64	stx_attributes;

	/* 0x10 */
	/* Number of hard links */
	__u32	stx_nlink;

	/* User ID of owner */
	__u32	stx_uid;

	/* Group ID of owner */
	__u32	stx_gid;

	/* File mode */
	__u16	stx_mode;
	__u16	__spare0[1];

	/* 0x20 */
	/* Inode number */
	__u64	stx_ino;

	/* File size */
	__u64	stx_size;

	/* Number of 512-byte blocks allocated */
	__u64	stx_blocks;

	/* Mask to show what's supported in stx_attributes */
	__u64	stx_attributes_mask;

	/* 0x40 */
	/* Last access time */
	struct statx_timestamp	stx_atime;

	/* File creation time */
	struct statx_timestamp	stx_btime;

	/* Last attribute change time */
	struct statx_timestamp	stx_ctime;

	/* Last data modification time */
	struct statx_timestamp	stx_mtime;

	/* 0x80 */
	/* Device ID of special file [if bdev/cdev] */
	__u32	stx_rdev_major;
	__u32	stx_rdev_minor;

	/* ID of device containing file [uncond] */
	__u32	stx_dev_major;
	__u32	stx_dev_minor;

	/* 0x90 */
	__u64	stx_mnt_id;

	/* Memory buffer alignment for direct I/O */
	__u32	stx_dio_mem_align;

	/* File offset alignment for direct I/O */
	__u32	stx_dio_offset_align;

	/* 0xa0 */
	/* Subvolume identifier */
	__u64	stx_subvol;

	/* Min atomic write unit in bytes */
	__u32	stx_atomic_write_unit_min;

	/* Max atomic write unit in bytes */
	__u32	stx_atomic_write_unit_max;

	/* 0xb0 */
	/* Max atomic write segment count */
	__u32   stx_atomic_write_segments_max;

	/* File offset alignment for direct I/O reads */
	__u32	stx_dio_read_offset_align;

	/* 0xb8 */
	__u64	__spare3[9];	/* Spare space for future expansion */

	/* 0x100 */
};

/*
 * Flags to be stx_mask
 *
 * Query request/result mask for statx() and struct statx::stx_mask.
 *
 * These bits should be set in the mask argument of statx() to request
 * particular items when calling statx().
 */
#define STATX_TYPE		0x00000001U	/* Want/got stx_mode & S_IFMT */
#define STATX_MODE		0x00000002U	/* Want/got stx_mode & ~S_IFMT */
#define STATX_NLINK		0x00000004U	/* Want/got stx_nlink */
#define STATX_UID		0x00000008U	/* Want/got stx_uid */
#define STATX_GID		0x00000010U	/* Want/got stx_gid */
#define STATX_ATIME		0x00000020U	/* Want/got stx_atime */
#define STATX_MTIME		0x00000040U	/* Want/got stx_mtime */
#define STATX_CTIME		0x00000080U	/* Want/got stx_ctime */
#define STATX_INO		0x00000100U	/* Want/got stx_ino */
#define STATX_SIZE		0x00000200U	/* Want/got stx_size */
#define STATX_BLOCKS		0x00000400U	/* Want/got stx_blocks */
#define STATX_BASIC_STATS	0x000007ffU	/* The stuff in the normal stat struct */
#define STATX_BTIME		0x00000800U	/* Want/got stx_btime */
#define STATX_MNT_ID		0x00001000U	/* Got stx_mnt_id */
#define STATX_DIOALIGN		0x00002000U	/* Want/got direct I/O alignment info */
#define STATX_MNT_ID_UNIQUE	0x00004000U	/* Want/got extended stx_mount_id */
#define STATX_SUBVOL		0x00008000U	/* Want/got stx_subvol */
#define STATX_WRITE_ATOMIC	0x00010000U	/* Want/got atomic_write_* fields */
#define STATX_DIO_READ_ALIGN	0x00020000U	/* Want/got dio read alignment info */

#define STATX__RESERVED		0x80000000U	/* Reserved for future struct statx expansion */

#ifndef __KERNEL__
/*
 * This is deprecated, and shall remain the same value in the future.  To avoid
 * confusion please use the equivalent (STATX_BASIC_STATS | STATX_BTIME)
 * instead.
 */
#define STATX_ALL		0x00000fffU
#endif

/*
 * Attributes to be found in stx_attributes and masked in stx_attributes_mask.
 *
 * These give information about the features or the state of a file that might
 * be of use to ordinary userspace programs such as GUIs or ls rather than
 * specialised tools.
 *
 * Note that the flags marked [I] correspond to the FS_IOC_SETFLAGS flags
 * semantically.  Where possible, the numerical value is picked to correspond
 * also.  Note that the DAX attribute indicates that the file is in the CPU
 * direct access state.  It does not correspond to the per-inode flag that
 * some filesystems support.
 *
 */
#define STATX_ATTR_COMPRESSED		0x00000004 /* [I] File is compressed by the fs */
#define STATX_ATTR_IMMUTABLE		0x00000010 /* [I] File is marked immutable */
#define STATX_ATTR_APPEND		0x00000020 /* [I] File is append-only */
#define STATX_ATTR_NODUMP		0x00000040 /* [I] File is not to be dumped */
#define STATX_ATTR_ENCRYPTED		0x00000800 /* [I] File requires key to decrypt in fs */
#define STATX_ATTR_AUTOMOUNT		0x00001000 /* Dir: Automount trigger */
#define STATX_ATTR_MOUNT_ROOT		0x00002000 /* Root of a mount */
#define STATX_ATTR_VERITY		0x00100000 /* [I] Verity protected file */
#define STATX_ATTR_DAX			0x00200000 /* File is currently in DAX state */
#define STATX_ATTR_WRITE_ATOMIC		0x00400000 /* File supports atomic write operations */


#endif /* _UAPI_LINUX_STAT_H */
