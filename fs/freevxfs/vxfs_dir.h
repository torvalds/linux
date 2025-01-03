/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 */
#ifndef _VXFS_DIR_H_
#define _VXFS_DIR_H_

/*
 * Veritas filesystem driver - directory structure.
 *
 * This file contains the definition of the vxfs directory format.
 */


/*
 * VxFS directory block header.
 *
 * This entry is the head of every filesystem block in a directory.
 * It is used for free space management and additionally includes
 * a hash for speeding up directory search (lookup).
 *
 * The hash may be empty and in fact we do not use it all in the
 * Linux driver for now.
 */
struct vxfs_dirblk {
	__fs16		d_free;		/* free space in dirblock */
	__fs16		d_nhash;	/* no of hash chains */
	__fs16		d_hash[];	/* hash chain */
};

/*
 * VXFS_NAMELEN is the maximum length of the d_name field
 *	of an VxFS directory entry.
 */
#define VXFS_NAMELEN	256

/*
 * VxFS directory entry.
 */
struct vxfs_direct {
	__fs32		d_ino;			/* inode number */
	__fs16		d_reclen;		/* record length */
	__fs16		d_namelen;		/* d_name length */
	__fs16		d_hashnext;		/* next hash entry */
	char		d_name[VXFS_NAMELEN];	/* name */
};

/*
 * VXFS_DIRPAD defines the directory entry boundaries, is _must_ be
 *	a multiple of four.
 * VXFS_NAMEMIN is the length of a directory entry with a NULL d_name.
 * VXFS_DIRROUND is an internal macros that rounds a length to a value
 *	usable for directory sizes.
 * VXFS_DIRLEN calculates the directory entry size for an entry with
 *	a d_name with size len.
 */
#define VXFS_DIRPAD		4
#define VXFS_NAMEMIN		offsetof(struct vxfs_direct, d_name)
#define VXFS_DIRROUND(len)	((VXFS_DIRPAD + (len) - 1) & ~(VXFS_DIRPAD -1))
#define VXFS_DIRLEN(len)	(VXFS_DIRROUND(VXFS_NAMEMIN + (len)))

/*
 * VXFS_DIRBLKOV is the overhead of a specific dirblock.
 */
#define VXFS_DIRBLKOV(sbi, dbp)	\
	((sizeof(short) * fs16_to_cpu(sbi, dbp->d_nhash)) + 4)

#endif /* _VXFS_DIR_H_ */
