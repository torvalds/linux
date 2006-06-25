#ifndef __CRAMFS_H
#define __CRAMFS_H

#include <linux/types.h>

#define CRAMFS_MAGIC		0x28cd3d45	/* some random number */
#define CRAMFS_SIGNATURE	"Compressed ROMFS"

/*
 * Width of various bitfields in struct cramfs_inode.
 * Primarily used to generate warnings in mkcramfs.
 */
#define CRAMFS_MODE_WIDTH 16
#define CRAMFS_UID_WIDTH 16
#define CRAMFS_SIZE_WIDTH 24
#define CRAMFS_GID_WIDTH 8
#define CRAMFS_NAMELEN_WIDTH 6
#define CRAMFS_OFFSET_WIDTH 26

/*
 * Since inode.namelen is a unsigned 6-bit number, the maximum cramfs
 * path length is 63 << 2 = 252.
 */
#define CRAMFS_MAXPATHLEN (((1 << CRAMFS_NAMELEN_WIDTH) - 1) << 2)

/*
 * Reasonably terse representation of the inode data.
 */
struct cramfs_inode {
	__u32 mode:CRAMFS_MODE_WIDTH, uid:CRAMFS_UID_WIDTH;
	/* SIZE for device files is i_rdev */
	__u32 size:CRAMFS_SIZE_WIDTH, gid:CRAMFS_GID_WIDTH;
	/* NAMELEN is the length of the file name, divided by 4 and
           rounded up.  (cramfs doesn't support hard links.) */
	/* OFFSET: For symlinks and non-empty regular files, this
	   contains the offset (divided by 4) of the file data in
	   compressed form (starting with an array of block pointers;
	   see README).  For non-empty directories it is the offset
	   (divided by 4) of the inode of the first file in that
	   directory.  For anything else, offset is zero. */
	__u32 namelen:CRAMFS_NAMELEN_WIDTH, offset:CRAMFS_OFFSET_WIDTH;
};

struct cramfs_info {
	__u32 crc;
	__u32 edition;
	__u32 blocks;
	__u32 files;
};

/*
 * Superblock information at the beginning of the FS.
 */
struct cramfs_super {
	__u32 magic;			/* 0x28cd3d45 - random number */
	__u32 size;			/* length in bytes */
	__u32 flags;			/* feature flags */
	__u32 future;			/* reserved for future use */
	__u8 signature[16];		/* "Compressed ROMFS" */
	struct cramfs_info fsid;	/* unique filesystem info */
	__u8 name[16];			/* user-defined name */
	struct cramfs_inode root;	/* root inode data */
};

/*
 * Feature flags
 *
 * 0x00000000 - 0x000000ff: features that work for all past kernels
 * 0x00000100 - 0xffffffff: features that don't work for past kernels
 */
#define CRAMFS_FLAG_FSID_VERSION_2	0x00000001	/* fsid version #2 */
#define CRAMFS_FLAG_SORTED_DIRS		0x00000002	/* sorted dirs */
#define CRAMFS_FLAG_HOLES		0x00000100	/* support for holes */
#define CRAMFS_FLAG_WRONG_SIGNATURE	0x00000200	/* reserved */
#define CRAMFS_FLAG_SHIFTED_ROOT_OFFSET	0x00000400	/* shifted root fs */

/*
 * Valid values in super.flags.  Currently we refuse to mount
 * if (flags & ~CRAMFS_SUPPORTED_FLAGS).  Maybe that should be
 * changed to test super.future instead.
 */
#define CRAMFS_SUPPORTED_FLAGS	( 0x000000ff \
				| CRAMFS_FLAG_HOLES \
				| CRAMFS_FLAG_WRONG_SIGNATURE \
				| CRAMFS_FLAG_SHIFTED_ROOT_OFFSET )

/* Uncompression interfaces to the underlying zlib */
int cramfs_uncompress_block(void *dst, int dstlen, void *src, int srclen);
int cramfs_uncompress_init(void);
int cramfs_uncompress_exit(void);

#endif
