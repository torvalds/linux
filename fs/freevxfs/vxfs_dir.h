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
 * It is used for free space managment and additionally includes
 * a hash for speeding up directory search (lookup).
 *
 * The hash may be empty and in fact we do not use it all in the
 * Linux driver for now.
 */
struct vxfs_dirblk {
	u_int16_t	d_free;		/* free space in dirblock */
	u_int16_t	d_nhash;	/* no of hash chains */
	u_int16_t	d_hash[1];	/* hash chain */
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
	vx_ino_t	d_ino;			/* inode number */
	u_int16_t	d_reclen;		/* record length */
	u_int16_t	d_namelen;		/* d_name length */
	u_int16_t	d_hashnext;		/* next hash entry */
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
#define VXFS_NAMEMIN		((int)((struct vxfs_direct *)0)->d_name)
#define VXFS_DIRROUND(len)	((VXFS_DIRPAD + (len) - 1) & ~(VXFS_DIRPAD -1))
#define VXFS_DIRLEN(len)	(VXFS_DIRROUND(VXFS_NAMEMIN + (len)))

/*
 * VXFS_DIRBLKOV is the overhead of a specific dirblock.
 */
#define VXFS_DIRBLKOV(dbp)	((sizeof(short) * dbp->d_nhash) + 4)

#endif /* _VXFS_DIR_H_ */
