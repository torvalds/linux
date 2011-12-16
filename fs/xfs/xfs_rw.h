/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#ifndef	__XFS_RW_H__
#define	__XFS_RW_H__

struct xfs_buf;
struct xfs_inode;
struct xfs_mount;

/*
 * Convert the given file system block to a disk block.
 * We have to treat it differently based on whether the
 * file is a real time file or not, because the bmap code
 * does.
 */
static inline xfs_daddr_t
xfs_fsb_to_db(struct xfs_inode *ip, xfs_fsblock_t fsb)
{
	return (XFS_IS_REALTIME_INODE(ip) ? \
		 (xfs_daddr_t)XFS_FSB_TO_BB((ip)->i_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((ip)->i_mount, (fsb)));
}

/*
 * Prototypes for functions in xfs_rw.c.
 */
extern int xfs_read_buf(struct xfs_mount *mp, xfs_buftarg_t *btp,
			xfs_daddr_t blkno, int len, uint flags,
			struct xfs_buf **bpp);
extern xfs_extlen_t xfs_get_extsz_hint(struct xfs_inode *ip);

#endif /* __XFS_RW_H__ */
