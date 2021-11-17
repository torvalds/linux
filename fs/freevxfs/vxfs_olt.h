/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
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
