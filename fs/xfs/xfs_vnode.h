/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#ifndef __XFS_VNODE_H__
#define __XFS_VNODE_H__

#include "xfs_fs.h"

struct file;
struct xfs_inode;
struct attrlist_cursor_kern;

/*
 * Flags for read/write calls - same values as IRIX
 */
#define IO_ISDIRECT	0x00004		/* bypass page cache */
#define IO_INVIS	0x00020		/* don't update inode timestamps */

#define XFS_IO_FLAGS \
	{ IO_ISDIRECT,	"DIRECT" }, \
	{ IO_INVIS,	"INVIS"}

#endif	/* __XFS_VNODE_H__ */
