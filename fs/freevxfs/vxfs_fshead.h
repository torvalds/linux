/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * Copyright (c) 2016 Krzysztof Blaszkowski
 */
#ifndef _VXFS_FSHEAD_H_
#define _VXFS_FSHEAD_H_

/*
 * Veritas filesystem driver - fileset header structures.
 *
 * This file contains the physical structure of the VxFS
 * fileset header.
 */


/*
 * Fileset header 
 */
struct vxfs_fsh {
	__fs32		fsh_version;		/* fileset header version */
	__fs32		fsh_fsindex;		/* fileset index */
	__fs32		fsh_time;		/* modification time - sec */
	__fs32		fsh_utime;		/* modification time - usec */
	__fs32		fsh_extop;		/* extop flags */
	__fs32		fsh_ninodes;		/* allocated inodes */
	__fs32		fsh_nau;		/* number of IAUs */
	__fs32		fsh_old_ilesize;	/* old size of ilist */
	__fs32		fsh_dflags;		/* flags */
	__fs32		fsh_quota;		/* quota limit */
	__fs32		fsh_maxinode;		/* maximum inode number */
	__fs32		fsh_iauino;		/* IAU inode */
	__fs32		fsh_ilistino[2];	/* ilist inodes */
	__fs32		fsh_lctino;		/* link count table inode */

	/*
	 * Slightly more fields follow, but they
	 *  a) are not of any interest for us, and
	 *  b) differ a lot in different vxfs versions/ports
	 */
};

#endif /* _VXFS_FSHEAD_H_ */
