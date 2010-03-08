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

#define IOMAP_DADDR_NULL ((xfs_daddr_t) (-1LL))


typedef enum {				/* iomap_flags values */
	IOMAP_READ =		0,	/* mapping for a read */
	IOMAP_HOLE =		0x02,	/* mapping covers a hole  */
	IOMAP_DELAY =		0x04,	/* mapping covers delalloc region  */
	IOMAP_REALTIME =	0x10,	/* mapping on the realtime device  */
	IOMAP_UNWRITTEN =	0x20,	/* mapping covers allocated */
					/* but uninitialized file data  */
	IOMAP_NEW =		0x40	/* just allocate */
} iomap_flags_t;

typedef enum {
	/* base extent manipulation calls */
	BMAPI_READ = (1 << 0),		/* read extents */
	BMAPI_WRITE = (1 << 1),		/* create extents */
	BMAPI_ALLOCATE = (1 << 2),	/* delayed allocate to real extents */
	/* modifiers */
	BMAPI_IGNSTATE = (1 << 4),	/* ignore unwritten state on read */
	BMAPI_DIRECT = (1 << 5),	/* direct instead of buffered write */
	BMAPI_MMAP = (1 << 6),		/* allocate for mmap write */
	BMAPI_TRYLOCK = (1 << 7),	/* non-blocking request */
} bmapi_flags_t;

#define BMAPI_FLAGS \
	{ BMAPI_READ,		"READ" }, \
	{ BMAPI_WRITE,		"WRITE" }, \
	{ BMAPI_ALLOCATE,	"ALLOCATE" }, \
	{ BMAPI_IGNSTATE,	"IGNSTATE" }, \
	{ BMAPI_DIRECT,		"DIRECT" }, \
	{ BMAPI_MMAP,		"MMAP" }, \
	{ BMAPI_TRYLOCK,	"TRYLOCK" }

/*
 * xfs_iomap_t:  File system I/O map
 *
 * The iomap_bn field is expressed in 512-byte blocks, and is where the
 * mapping starts on disk.
 *
 * The iomap_offset, iomap_bsize and iomap_delta fields are in bytes.
 * iomap_offset is the offset of the mapping in the file itself.
 * iomap_bsize is the size of the mapping,  iomap_delta is the
 * desired data's offset into the mapping, given the offset supplied
 * to the file I/O map routine.
 *
 * When a request is made to read beyond the logical end of the object,
 * iomap_size may be set to 0, but iomap_offset and iomap_length should be set
 * to the actual amount of underlying storage that has been allocated, if any.
 */

typedef struct xfs_iomap {
	xfs_daddr_t		iomap_bn;	/* first 512B blk of mapping */
	xfs_buftarg_t		*iomap_target;
	xfs_off_t		iomap_offset;	/* offset of mapping, bytes */
	xfs_off_t		iomap_bsize;	/* size of mapping, bytes */
	xfs_off_t		iomap_delta;	/* offset into mapping, bytes */
	iomap_flags_t		iomap_flags;
} xfs_iomap_t;

struct xfs_inode;
struct xfs_bmbt_irec;

extern int xfs_iomap(struct xfs_inode *, xfs_off_t, ssize_t, int,
		     struct xfs_iomap *, int *);
extern int xfs_iomap_write_direct(struct xfs_inode *, xfs_off_t, size_t,
				  int, struct xfs_bmbt_irec *, int *, int);
extern int xfs_iomap_write_delay(struct xfs_inode *, xfs_off_t, size_t, int,
				 struct xfs_bmbt_irec *, int *);
extern int xfs_iomap_write_allocate(struct xfs_inode *, xfs_off_t, size_t,
				struct xfs_bmbt_irec *, int *);
extern int xfs_iomap_write_unwritten(struct xfs_inode *, xfs_off_t, size_t);

#endif /* __XFS_IOMAP_H__*/
