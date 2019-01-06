/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_FS_H
#define _UAPI_LINUX_FS_H

/*
 * This file has definitions for some important file table structures
 * and constants and structures used by various generic file system
 * ioctl's.  Please do not make any changes in this file before
 * sending patches for review to linux-fsdevel@vger.kernel.org and
 * linux-api@vger.kernel.org.
 */

#include <linux/limits.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/* Use of MS_* flags within the kernel is restricted to core mount(2) code. */
#if !defined(__KERNEL__)
#include <linux/mount.h>
#endif

/*
 * It's silly to have NR_OPEN bigger than NR_FILE, but you can change
 * the file limit at runtime and only root can increase the per-process
 * nr_file rlimit, so it's safe to set up a ridiculously high absolute
 * upper limit on files-per-process.
 *
 * Some programs (notably those using select()) may have to be 
 * recompiled to take full advantage of the new limits..  
 */

/* Fixed constants first: */
#undef NR_OPEN
#define INR_OPEN_CUR 1024	/* Initial setting for nfile rlimits */
#define INR_OPEN_MAX 4096	/* Hard limit for nfile rlimits */

#define BLOCK_SIZE_BITS 10
#define BLOCK_SIZE (1<<BLOCK_SIZE_BITS)

#define SEEK_SET	0	/* seek relative to beginning of file */
#define SEEK_CUR	1	/* seek relative to current file position */
#define SEEK_END	2	/* seek relative to end of file */
#define SEEK_DATA	3	/* seek to the next data */
#define SEEK_HOLE	4	/* seek to the next hole */
#define SEEK_MAX	SEEK_HOLE

#define RENAME_NOREPLACE	(1 << 0)	/* Don't overwrite target */
#define RENAME_EXCHANGE		(1 << 1)	/* Exchange source and dest */
#define RENAME_WHITEOUT		(1 << 2)	/* Whiteout source */

struct file_clone_range {
	__s64 src_fd;
	__u64 src_offset;
	__u64 src_length;
	__u64 dest_offset;
};

struct fstrim_range {
	__u64 start;
	__u64 len;
	__u64 minlen;
};

/* extent-same (dedupe) ioctls; these MUST match the btrfs ioctl definitions */
#define FILE_DEDUPE_RANGE_SAME		0
#define FILE_DEDUPE_RANGE_DIFFERS	1

/* from struct btrfs_ioctl_file_extent_same_info */
struct file_dedupe_range_info {
	__s64 dest_fd;		/* in - destination file */
	__u64 dest_offset;	/* in - start of extent in destination */
	__u64 bytes_deduped;	/* out - total # of bytes we were able
				 * to dedupe from this file. */
	/* status of this dedupe operation:
	 * < 0 for error
	 * == FILE_DEDUPE_RANGE_SAME if dedupe succeeds
	 * == FILE_DEDUPE_RANGE_DIFFERS if data differs
	 */
	__s32 status;		/* out - see above description */
	__u32 reserved;		/* must be zero */
};

/* from struct btrfs_ioctl_file_extent_same_args */
struct file_dedupe_range {
	__u64 src_offset;	/* in - start of extent in source */
	__u64 src_length;	/* in - length of extent */
	__u16 dest_count;	/* in - total elements in info array */
	__u16 reserved1;	/* must be zero */
	__u32 reserved2;	/* must be zero */
	struct file_dedupe_range_info info[0];
};

/* And dynamically-tunable limits and defaults: */
struct files_stat_struct {
	unsigned long nr_files;		/* read only */
	unsigned long nr_free_files;	/* read only */
	unsigned long max_files;		/* tunable */
};

struct inodes_stat_t {
	long nr_inodes;
	long nr_unused;
	long dummy[5];		/* padding for sysctl ABI compatibility */
};


#define NR_FILE  8192	/* this can well be larger on a larger system */

/*
 * Structure for FS_IOC_FSGETXATTR[A] and FS_IOC_FSSETXATTR.
 */
struct fsxattr {
	__u32		fsx_xflags;	/* xflags field value (get/set) */
	__u32		fsx_extsize;	/* extsize field value (get/set)*/
	__u32		fsx_nextents;	/* nextents field value (get)	*/
	__u32		fsx_projid;	/* project identifier (get/set) */
	__u32		fsx_cowextsize;	/* CoW extsize field value (get/set)*/
	unsigned char	fsx_pad[8];
};

/*
 * Flags for the fsx_xflags field
 */
#define FS_XFLAG_REALTIME	0x00000001	/* data in realtime volume */
#define FS_XFLAG_PREALLOC	0x00000002	/* preallocated file extents */
#define FS_XFLAG_IMMUTABLE	0x00000008	/* file cannot be modified */
#define FS_XFLAG_APPEND		0x00000010	/* all writes append */
#define FS_XFLAG_SYNC		0x00000020	/* all writes synchronous */
#define FS_XFLAG_NOATIME	0x00000040	/* do not update access time */
#define FS_XFLAG_NODUMP		0x00000080	/* do not include in backups */
#define FS_XFLAG_RTINHERIT	0x00000100	/* create with rt bit set */
#define FS_XFLAG_PROJINHERIT	0x00000200	/* create with parents projid */
#define FS_XFLAG_NOSYMLINKS	0x00000400	/* disallow symlink creation */
#define FS_XFLAG_EXTSIZE	0x00000800	/* extent size allocator hint */
#define FS_XFLAG_EXTSZINHERIT	0x00001000	/* inherit inode extent size */
#define FS_XFLAG_NODEFRAG	0x00002000	/* do not defragment */
#define FS_XFLAG_FILESTREAM	0x00004000	/* use filestream allocator */
#define FS_XFLAG_DAX		0x00008000	/* use DAX for IO */
#define FS_XFLAG_COWEXTSIZE	0x00010000	/* CoW extent size allocator hint */
#define FS_XFLAG_HASATTR	0x80000000	/* no DIFLAG for this	*/

/* the read-only stuff doesn't really belong here, but any other place is
   probably as bad and I don't want to create yet another include file. */

#define BLKROSET   _IO(0x12,93)	/* set device read-only (0 = read-write) */
#define BLKROGET   _IO(0x12,94)	/* get read-only status (0 = read_write) */
#define BLKRRPART  _IO(0x12,95)	/* re-read partition table */
#define BLKGETSIZE _IO(0x12,96)	/* return device size /512 (long *arg) */
#define BLKFLSBUF  _IO(0x12,97)	/* flush buffer cache */
#define BLKRASET   _IO(0x12,98)	/* set read ahead for block device */
#define BLKRAGET   _IO(0x12,99)	/* get current read ahead setting */
#define BLKFRASET  _IO(0x12,100)/* set filesystem (mm/filemap.c) read-ahead */
#define BLKFRAGET  _IO(0x12,101)/* get filesystem (mm/filemap.c) read-ahead */
#define BLKSECTSET _IO(0x12,102)/* set max sectors per request (ll_rw_blk.c) */
#define BLKSECTGET _IO(0x12,103)/* get max sectors per request (ll_rw_blk.c) */
#define BLKSSZGET  _IO(0x12,104)/* get block device sector size */
#if 0
#define BLKPG      _IO(0x12,105)/* See blkpg.h */

/* Some people are morons.  Do not use sizeof! */

#define BLKELVGET  _IOR(0x12,106,size_t)/* elevator get */
#define BLKELVSET  _IOW(0x12,107,size_t)/* elevator set */
/* This was here just to show that the number is taken -
   probably all these _IO(0x12,*) ioctls should be moved to blkpg.h. */
#endif
/* A jump here: 108-111 have been used for various private purposes. */
#define BLKBSZGET  _IOR(0x12,112,size_t)
#define BLKBSZSET  _IOW(0x12,113,size_t)
#define BLKGETSIZE64 _IOR(0x12,114,size_t)	/* return device size in bytes (u64 *arg) */
#define BLKTRACESETUP _IOWR(0x12,115,struct blk_user_trace_setup)
#define BLKTRACESTART _IO(0x12,116)
#define BLKTRACESTOP _IO(0x12,117)
#define BLKTRACETEARDOWN _IO(0x12,118)
#define BLKDISCARD _IO(0x12,119)
#define BLKIOMIN _IO(0x12,120)
#define BLKIOOPT _IO(0x12,121)
#define BLKALIGNOFF _IO(0x12,122)
#define BLKPBSZGET _IO(0x12,123)
#define BLKDISCARDZEROES _IO(0x12,124)
#define BLKSECDISCARD _IO(0x12,125)
#define BLKROTATIONAL _IO(0x12,126)
#define BLKZEROOUT _IO(0x12,127)
/*
 * A jump here: 130-131 are reserved for zoned block devices
 * (see uapi/linux/blkzoned.h)
 */

#define BMAP_IOCTL 1		/* obsolete - kept for compatibility */
#define FIBMAP	   _IO(0x00,1)	/* bmap access */
#define FIGETBSZ   _IO(0x00,2)	/* get the block size used for bmap */
#define FIFREEZE	_IOWR('X', 119, int)	/* Freeze */
#define FITHAW		_IOWR('X', 120, int)	/* Thaw */
#define FITRIM		_IOWR('X', 121, struct fstrim_range)	/* Trim */
#define FICLONE		_IOW(0x94, 9, int)
#define FICLONERANGE	_IOW(0x94, 13, struct file_clone_range)
#define FIDEDUPERANGE	_IOWR(0x94, 54, struct file_dedupe_range)

#define FSLABEL_MAX 256	/* Max chars for the interface; each fs may differ */

#define	FS_IOC_GETFLAGS			_IOR('f', 1, long)
#define	FS_IOC_SETFLAGS			_IOW('f', 2, long)
#define	FS_IOC_GETVERSION		_IOR('v', 1, long)
#define	FS_IOC_SETVERSION		_IOW('v', 2, long)
#define FS_IOC_FIEMAP			_IOWR('f', 11, struct fiemap)
#define FS_IOC32_GETFLAGS		_IOR('f', 1, int)
#define FS_IOC32_SETFLAGS		_IOW('f', 2, int)
#define FS_IOC32_GETVERSION		_IOR('v', 1, int)
#define FS_IOC32_SETVERSION		_IOW('v', 2, int)
#define FS_IOC_FSGETXATTR		_IOR('X', 31, struct fsxattr)
#define FS_IOC_FSSETXATTR		_IOW('X', 32, struct fsxattr)
#define FS_IOC_GETFSLABEL		_IOR(0x94, 49, char[FSLABEL_MAX])
#define FS_IOC_SETFSLABEL		_IOW(0x94, 50, char[FSLABEL_MAX])

/*
 * File system encryption support
 */
/* Policy provided via an ioctl on the topmost directory */
#define FS_KEY_DESCRIPTOR_SIZE	8

#define FS_POLICY_FLAGS_PAD_4		0x00
#define FS_POLICY_FLAGS_PAD_8		0x01
#define FS_POLICY_FLAGS_PAD_16		0x02
#define FS_POLICY_FLAGS_PAD_32		0x03
#define FS_POLICY_FLAGS_PAD_MASK	0x03
#define FS_POLICY_FLAG_DIRECT_KEY	0x04	/* use master key directly */
#define FS_POLICY_FLAGS_VALID		0x07

/* Encryption algorithms */
#define FS_ENCRYPTION_MODE_INVALID		0
#define FS_ENCRYPTION_MODE_AES_256_XTS		1
#define FS_ENCRYPTION_MODE_AES_256_GCM		2
#define FS_ENCRYPTION_MODE_AES_256_CBC		3
#define FS_ENCRYPTION_MODE_AES_256_CTS		4
#define FS_ENCRYPTION_MODE_AES_128_CBC		5
#define FS_ENCRYPTION_MODE_AES_128_CTS		6
#define FS_ENCRYPTION_MODE_SPECK128_256_XTS	7 /* Removed, do not use. */
#define FS_ENCRYPTION_MODE_SPECK128_256_CTS	8 /* Removed, do not use. */
#define FS_ENCRYPTION_MODE_ADIANTUM		9

struct fscrypt_policy {
	__u8 version;
	__u8 contents_encryption_mode;
	__u8 filenames_encryption_mode;
	__u8 flags;
	__u8 master_key_descriptor[FS_KEY_DESCRIPTOR_SIZE];
};

#define FS_IOC_SET_ENCRYPTION_POLICY	_IOR('f', 19, struct fscrypt_policy)
#define FS_IOC_GET_ENCRYPTION_PWSALT	_IOW('f', 20, __u8[16])
#define FS_IOC_GET_ENCRYPTION_POLICY	_IOW('f', 21, struct fscrypt_policy)

/* Parameters for passing an encryption key into the kernel keyring */
#define FS_KEY_DESC_PREFIX		"fscrypt:"
#define FS_KEY_DESC_PREFIX_SIZE		8

/* Structure that userspace passes to the kernel keyring */
#define FS_MAX_KEY_SIZE			64

struct fscrypt_key {
	__u32 mode;
	__u8 raw[FS_MAX_KEY_SIZE];
	__u32 size;
};

/*
 * Inode flags (FS_IOC_GETFLAGS / FS_IOC_SETFLAGS)
 *
 * Note: for historical reasons, these flags were originally used and
 * defined for use by ext2/ext3, and then other file systems started
 * using these flags so they wouldn't need to write their own version
 * of chattr/lsattr (which was shipped as part of e2fsprogs).  You
 * should think twice before trying to use these flags in new
 * contexts, or trying to assign these flags, since they are used both
 * as the UAPI and the on-disk encoding for ext2/3/4.  Also, we are
 * almost out of 32-bit flags.  :-)
 *
 * We have recently hoisted FS_IOC_FSGETXATTR / FS_IOC_FSSETXATTR from
 * XFS to the generic FS level interface.  This uses a structure that
 * has padding and hence has more room to grow, so it may be more
 * appropriate for many new use cases.
 *
 * Please do not change these flags or interfaces before checking with
 * linux-fsdevel@vger.kernel.org and linux-api@vger.kernel.org.
 */
#define	FS_SECRM_FL			0x00000001 /* Secure deletion */
#define	FS_UNRM_FL			0x00000002 /* Undelete */
#define	FS_COMPR_FL			0x00000004 /* Compress file */
#define FS_SYNC_FL			0x00000008 /* Synchronous updates */
#define FS_IMMUTABLE_FL			0x00000010 /* Immutable file */
#define FS_APPEND_FL			0x00000020 /* writes to file may only append */
#define FS_NODUMP_FL			0x00000040 /* do not dump file */
#define FS_NOATIME_FL			0x00000080 /* do not update atime */
/* Reserved for compression usage... */
#define FS_DIRTY_FL			0x00000100
#define FS_COMPRBLK_FL			0x00000200 /* One or more compressed clusters */
#define FS_NOCOMP_FL			0x00000400 /* Don't compress */
/* End compression flags --- maybe not all used */
#define FS_ENCRYPT_FL			0x00000800 /* Encrypted file */
#define FS_BTREE_FL			0x00001000 /* btree format dir */
#define FS_INDEX_FL			0x00001000 /* hash-indexed directory */
#define FS_IMAGIC_FL			0x00002000 /* AFS directory */
#define FS_JOURNAL_DATA_FL		0x00004000 /* Reserved for ext3 */
#define FS_NOTAIL_FL			0x00008000 /* file tail should not be merged */
#define FS_DIRSYNC_FL			0x00010000 /* dirsync behaviour (directories only) */
#define FS_TOPDIR_FL			0x00020000 /* Top of directory hierarchies*/
#define FS_HUGE_FILE_FL			0x00040000 /* Reserved for ext4 */
#define FS_EXTENT_FL			0x00080000 /* Extents */
#define FS_EA_INODE_FL			0x00200000 /* Inode used for large EA */
#define FS_EOFBLOCKS_FL			0x00400000 /* Reserved for ext4 */
#define FS_NOCOW_FL			0x00800000 /* Do not cow file */
#define FS_INLINE_DATA_FL		0x10000000 /* Reserved for ext4 */
#define FS_PROJINHERIT_FL		0x20000000 /* Create with parents projid */
#define FS_RESERVED_FL			0x80000000 /* reserved for ext2 lib */

#define FS_FL_USER_VISIBLE		0x0003DFFF /* User visible flags */
#define FS_FL_USER_MODIFIABLE		0x000380FF /* User modifiable flags */


#define SYNC_FILE_RANGE_WAIT_BEFORE	1
#define SYNC_FILE_RANGE_WRITE		2
#define SYNC_FILE_RANGE_WAIT_AFTER	4

/*
 * Flags for preadv2/pwritev2:
 */

typedef int __bitwise __kernel_rwf_t;

/* high priority request, poll if possible */
#define RWF_HIPRI	((__force __kernel_rwf_t)0x00000001)

/* per-IO O_DSYNC */
#define RWF_DSYNC	((__force __kernel_rwf_t)0x00000002)

/* per-IO O_SYNC */
#define RWF_SYNC	((__force __kernel_rwf_t)0x00000004)

/* per-IO, return -EAGAIN if operation would block */
#define RWF_NOWAIT	((__force __kernel_rwf_t)0x00000008)

/* per-IO O_APPEND */
#define RWF_APPEND	((__force __kernel_rwf_t)0x00000010)

/* mask of flags supported by the kernel */
#define RWF_SUPPORTED	(RWF_HIPRI | RWF_DSYNC | RWF_SYNC | RWF_NOWAIT |\
			 RWF_APPEND)

#endif /* _UAPI_LINUX_FS_H */
