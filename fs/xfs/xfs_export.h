// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_EXPORT_H__
#define __XFS_EXPORT_H__

/*
 * Common defines for code related to exporting XFS filesystems over NFS.
 *
 * The NFS fileid goes out on the wire as an array of
 * 32bit unsigned ints in host order.  There are 5 possible
 * formats.
 *
 * (1)	fileid_type=0x00
 *	(no fileid data; handled by the generic code)
 *
 * (2)	fileid_type=0x01
 *	inode-num
 *	generation
 *
 * (3)	fileid_type=0x02
 *	inode-num
 *	generation
 *	parent-inode-num
 *	parent-generation
 *
 * (4)	fileid_type=0x81
 *	inode-num-lo32
 *	inode-num-hi32
 *	generation
 *
 * (5)	fileid_type=0x82
 *	inode-num-lo32
 *	inode-num-hi32
 *	generation
 *	parent-inode-num-lo32
 *	parent-inode-num-hi32
 *	parent-generation
 *
 * Note, the NFS filehandle also includes an fsid portion which
 * may have an inode number in it.  That number is hardcoded to
 * 32bits and there is no way for XFS to intercept it.  In
 * practice this means when exporting an XFS filesystem with 64bit
 * inodes you should either export the mountpoint (rather than
 * a subdirectory) or use the "fsid" export option.
 */

struct xfs_fid64 {
	u64 ino;
	u32 gen;
	u64 parent_ino;
	u32 parent_gen;
} __attribute__((packed));

/* This flag goes on the wire.  Don't play with it. */
#define XFS_FILEID_TYPE_64FLAG	0x80	/* NFS fileid has 64bit inodes */

#endif	/* __XFS_EXPORT_H__ */
