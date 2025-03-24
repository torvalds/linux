/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * FS_IOC_FIEMAP ioctl infrastructure.
 *
 * Some portions copyright (C) 2007 Cluster File Systems, Inc
 *
 * Authors: Mark Fasheh <mfasheh@suse.com>
 *          Kalpak Shah <kalpak.shah@sun.com>
 *          Andreas Dilger <adilger@sun.com>
 */

#ifndef _UAPI_LINUX_FIEMAP_H
#define _UAPI_LINUX_FIEMAP_H

#include <linux/types.h>

/**
 * struct fiemap_extent - description of one fiemap extent
 * @fe_logical: byte offset of the extent in the file
 * @fe_physical: byte offset of extent on disk
 * @fe_length: length in bytes for this extent
 * @fe_flags: FIEMAP_EXTENT_* flags for this extent
 */
struct fiemap_extent {
	__u64 fe_logical;
	__u64 fe_physical;
	__u64 fe_length;
	/* private: */
	__u64 fe_reserved64[2];
	/* public: */
	__u32 fe_flags;
	/* private: */
	__u32 fe_reserved[3];
};

/**
 * struct fiemap - file extent mappings
 * @fm_start: byte offset (inclusive) at which to start mapping (in)
 * @fm_length: logical length of mapping which userspace wants (in)
 * @fm_flags: FIEMAP_FLAG_* flags for request (in/out)
 * @fm_mapped_extents: number of extents that were mapped (out)
 * @fm_extent_count: size of fm_extents array (in)
 * @fm_extents: array of mapped extents (out)
 */
struct fiemap {
	__u64 fm_start;
	__u64 fm_length;
	__u32 fm_flags;
	__u32 fm_mapped_extents;
	__u32 fm_extent_count;
	/* private: */
	__u32 fm_reserved;
	/* public: */
	struct fiemap_extent fm_extents[];
};

#define FIEMAP_MAX_OFFSET	(~0ULL)

/* flags used in fm_flags: */
#define FIEMAP_FLAG_SYNC	0x00000001 /* sync file data before map */
#define FIEMAP_FLAG_XATTR	0x00000002 /* map extended attribute tree */
#define FIEMAP_FLAG_CACHE	0x00000004 /* request caching of the extents */

#define FIEMAP_FLAGS_COMPAT	(FIEMAP_FLAG_SYNC | FIEMAP_FLAG_XATTR)

/* flags used in fe_flags: */
#define FIEMAP_EXTENT_LAST		0x00000001 /* Last extent in file. */
#define FIEMAP_EXTENT_UNKNOWN		0x00000002 /* Data location unknown. */
#define FIEMAP_EXTENT_DELALLOC		0x00000004 /* Location still pending.
						    * Sets EXTENT_UNKNOWN. */
#define FIEMAP_EXTENT_ENCODED		0x00000008 /* Data can not be read
						    * while fs is unmounted */
#define FIEMAP_EXTENT_DATA_ENCRYPTED	0x00000080 /* Data is encrypted by fs.
						    * Sets EXTENT_NO_BYPASS. */
#define FIEMAP_EXTENT_NOT_ALIGNED	0x00000100 /* Extent offsets may not be
						    * block aligned. */
#define FIEMAP_EXTENT_DATA_INLINE	0x00000200 /* Data mixed with metadata.
						    * Sets EXTENT_NOT_ALIGNED.*/
#define FIEMAP_EXTENT_DATA_TAIL		0x00000400 /* Multiple files in block.
						    * Sets EXTENT_NOT_ALIGNED.*/
#define FIEMAP_EXTENT_UNWRITTEN		0x00000800 /* Space allocated, but
						    * no data (i.e. zero). */
#define FIEMAP_EXTENT_MERGED		0x00001000 /* File does not natively
						    * support extents. Result
						    * merged for efficiency. */
#define FIEMAP_EXTENT_SHARED		0x00002000 /* Space shared with other
						    * files. */

#endif /* _UAPI_LINUX_FIEMAP_H */
