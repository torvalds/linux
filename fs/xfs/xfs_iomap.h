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

#include <linux/iomap.h>

struct xfs_inode;
struct xfs_bmbt_irec;

int xfs_iomap_write_direct(struct xfs_inode *, xfs_off_t, size_t,
			struct xfs_bmbt_irec *, int);
int xfs_iomap_write_delay(struct xfs_inode *, xfs_off_t, size_t,
			struct xfs_bmbt_irec *);
int xfs_iomap_write_allocate(struct xfs_inode *, xfs_off_t,
			struct xfs_bmbt_irec *);
int xfs_iomap_write_unwritten(struct xfs_inode *, xfs_off_t, xfs_off_t);

void xfs_bmbt_to_iomap(struct xfs_inode *, struct iomap *,
		struct xfs_bmbt_irec *);

extern struct iomap_ops xfs_iomap_ops;
extern struct iomap_ops xfs_xattr_iomap_ops;

#endif /* __XFS_IOMAP_H__*/
