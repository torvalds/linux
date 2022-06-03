/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 */
#ifndef _VXFS_OLT_H_
#define _VXFS_OLT_H_

/*
 * Veritas filesystem driver - Object Location Table data structures.
 *
 * This file contains definitions for the Object Location Table used
 * by the Veritas Filesystem version 2 and newer.
 */


/*
 * OLT magic number (vxfs_olt->olt_magic).
 */
#define VXFS_OLT_MAGIC		0xa504FCF5

/*
 * VxFS OLT entry types.
 */
enum {
	VXFS_OLT_FREE	= 1,
	VXFS_OLT_FSHEAD	= 2,
	VXFS_OLT_CUT	= 3,
	VXFS_OLT_ILIST	= 4,
	VXFS_OLT_DEV	= 5,
	VXFS_OLT_SB	= 6
};

/*
 * VxFS OLT header.
 *
 * The Object Location Table header is placed at the beginning of each
 * OLT extent.  It is used to fing certain filesystem-wide metadata, e.g.
 * the initial inode list, the fileset header or the device configuration.
 */
struct vxfs_olt {
	__fs32		olt_magic;	/* magic number			*/
	__fs32		olt_size;	/* size of this entry		*/
	__fs32		olt_checksum;	/* checksum of extent		*/
	__u32		__unused1;	/* ???				*/
	__fs32		olt_mtime;	/* time of last mod. (sec)	*/
	__fs32		olt_mutime;	/* time of last mod. (usec)	*/
	__fs32		olt_totfree;	/* free space in OLT extent	*/
	__fs32		olt_extents[2];	/* addr of this extent, replica	*/
	__fs32		olt_esize;	/* size of this extent		*/
	__fs32		olt_next[2];    /* addr of next extent, replica	*/
	__fs32		olt_nsize;	/* size of next extent		*/
	__u32		__unused2;	/* align to 8 byte boundary	*/
};

/*
 * VxFS common OLT entry (on disk).
 */
struct vxfs_oltcommon {
	__fs32		olt_type;	/* type of this record		*/
	__fs32		olt_size;	/* size of this record		*/
};

/*
 * VxFS free OLT entry (on disk).
 */
struct vxfs_oltfree {
	__fs32		olt_type;	/* type of this record		*/
	__fs32		olt_fsize;	/* size of this free record	*/
};

/*
 * VxFS initial-inode list (on disk).
 */
struct vxfs_oltilist {
	__fs32	olt_type;	/* type of this record		*/
	__fs32	olt_size;	/* size of this record		*/
	__fs32		olt_iext[2];	/* initial inode list, replica	*/
};

/*
 * Current Usage Table 
 */
struct vxfs_oltcut {
	__fs32		olt_type;	/* type of this record		*/
	__fs32		olt_size;	/* size of this record		*/
	__fs32		olt_cutino;	/* inode of current usage table	*/
	__u8		__pad;		/* unused, 8 byte align		*/
};

/*
 * Inodes containing Superblock, Intent log and OLTs 
 */
struct vxfs_oltsb {
	__fs32		olt_type;	/* type of this record		*/
	__fs32		olt_size;	/* size of this record		*/
	__fs32		olt_sbino;	/* inode of superblock file	*/
	__u32		__unused1;	/* ???				*/
	__fs32		olt_logino[2];	/* inode of log file,replica	*/
	__fs32		olt_oltino[2];	/* inode of OLT, replica	*/
};

/*
 * Inode containing device configuration + it's replica 
 */
struct vxfs_oltdev {
	__fs32		olt_type;	/* type of this record		*/
	__fs32		olt_size;	/* size of this record		*/
	__fs32		olt_devino[2];	/* inode of device config files	*/
};

/*
 * Fileset header 
 */
struct vxfs_oltfshead {
	__fs32		olt_type;	/* type number			*/
	__fs32		olt_size;	/* size of this record		*/
	__fs32		olt_fsino[2];   /* inodes of fileset header	*/
};

#endif /* _VXFS_OLT_H_ */
