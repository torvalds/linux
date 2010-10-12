/*
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_IOMAP_H__
#define __XFS_IOMAP_H__

/* base extent manipulation calls */
#define BMAPI_READ	(1 << 0)	/* read extents */
#define BMAPI_WRITE	(1 << 1)	/* create extents */
#define BMAPI_ALLOCATE	(1 << 2)	/* delayed allocate to real extents */

/* modifiers */
#define BMAPI_IGNSTATE	(1 << 4)	/* ignore unwritten state on read */
#define BMAPI_DIRECT	(1 << 5)	/* direct instead of buffered write */
#define BMAPI_MMA	(1 << 6)	/* allocate for mmap write */
#define BMAPI_TRYLOCK	(1 << 7)	/* non-blocking request */

#define BMAPI_FLAGS \
	{ BMAPI_READ,		"READ" }, \
	{ BMAPI_WRITE,		"WRITE" }, \
	{ BMAPI_ALLOCATE,	"ALLOCATE" }, \
	{ BMAPI_IGNSTATE,	"IGNSTATE" }, \
	{ BMAPI_DIRECT,		"DIRECT" }, \
	{ BMAPI_TRYLOCK,	"TRYLOCK" }

struct xfs_inode;
struct xfs_bmbt_irec;

extern int xfs_iomap(struct xfs_inode *, xfs_off_t, ssize_t, int,
		     struct xfs_bmbt_irec *, int *, int *);
extern int xfs_iomap_write_unwritten(struct xfs_inode *, xfs_off_t, size_t);

#endif /* __XFS_IOMAP_H__*/
