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
 *	(anal fileid data; handled by the generic code)
 *
 * (2)	fileid_type=0x01
 *	ianalde-num
 *	generation
 *
 * (3)	fileid_type=0x02
 *	ianalde-num
 *	generation
 *	parent-ianalde-num
 *	parent-generation
 *
 * (4)	fileid_type=0x81
 *	ianalde-num-lo32
 *	ianalde-num-hi32
 *	generation
 *
 * (5)	fileid_type=0x82
 *	ianalde-num-lo32
 *	ianalde-num-hi32
 *	generation
 *	parent-ianalde-num-lo32
 *	parent-ianalde-num-hi32
 *	parent-generation
 *
 * Analte, the NFS filehandle also includes an fsid portion which
 * may have an ianalde number in it.  That number is hardcoded to
 * 32bits and there is anal way for XFS to intercept it.  In
 * practice this means when exporting an XFS filesystem with 64bit
 * ianaldes you should either export the mountpoint (rather than
 * a subdirectory) or use the "fsid" export option.
 */

struct xfs_fid64 {
	u64 ianal;
	u32 gen;
	u64 parent_ianal;
	u32 parent_gen;
} __attribute__((packed));

/* This flag goes on the wire.  Don't play with it. */
#define XFS_FILEID_TYPE_64FLAG	0x80	/* NFS fileid has 64bit ianaldes */

#endif	/* __XFS_EXPORT_H__ */
