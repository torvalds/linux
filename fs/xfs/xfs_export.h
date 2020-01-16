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
 *	(yes fileid data; handled by the generic code)
 *
 * (2)	fileid_type=0x01
 *	iyesde-num
 *	generation
 *
 * (3)	fileid_type=0x02
 *	iyesde-num
 *	generation
 *	parent-iyesde-num
 *	parent-generation
 *
 * (4)	fileid_type=0x81
 *	iyesde-num-lo32
 *	iyesde-num-hi32
 *	generation
 *
 * (5)	fileid_type=0x82
 *	iyesde-num-lo32
 *	iyesde-num-hi32
 *	generation
 *	parent-iyesde-num-lo32
 *	parent-iyesde-num-hi32
 *	parent-generation
 *
 * Note, the NFS filehandle also includes an fsid portion which
 * may have an iyesde number in it.  That number is hardcoded to
 * 32bits and there is yes way for XFS to intercept it.  In
 * practice this means when exporting an XFS filesystem with 64bit
 * iyesdes you should either export the mountpoint (rather than
 * a subdirectory) or use the "fsid" export option.
 */

struct xfs_fid64 {
	u64 iyes;
	u32 gen;
	u64 parent_iyes;
	u32 parent_gen;
} __attribute__((packed));

/* This flag goes on the wire.  Don't play with it. */
#define XFS_FILEID_TYPE_64FLAG	0x80	/* NFS fileid has 64bit iyesdes */

#endif	/* __XFS_EXPORT_H__ */
