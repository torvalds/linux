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
 * Flags for xfs_free_eofblocks
 */
#define XFS_FREE_EOF_LOCK	(1<<0)
#define XFS_FREE_EOF_NOLOCK	(1<<1)


/*
 * helper function to extract extent size hint from inode
 */
STATIC_INLINE xfs_extlen_t
xfs_get_extsz_hint(
	xfs_inode_t	*ip)
{
	xfs_extlen_t	extsz;

	if (unlikely(XFS_IS_REALTIME_INODE(ip))) {
		extsz = (ip->i_d.di_flags & XFS_DIFLAG_EXTSIZE)
				? ip->i_d.di_extsize
				: ip->i_mount->m_sb.sb_rextsize;
		ASSERT(extsz);
	} else {
		extsz = (ip->i_d.di_flags & XFS_DIFLAG_EXTSIZE)
				? ip->i_d.di_extsize : 0;
	}
	return extsz;
}

/*
 * Prototypes for functions in xfs_rw.c.
 */
extern int xfs_write_clear_setuid(struct xfs_inode *ip);
extern int xfs_write_sync_logforce(struct xfs_mount *mp, struct xfs_inode *ip);
extern int xfs_bwrite(struct xfs_mount *mp, struct xfs_buf *bp);
extern int xfs_bioerror(struct xfs_buf *bp);
extern int xfs_bioerror_relse(struct xfs_buf *bp);
extern int xfs_read_buf(struct xfs_mount *mp, xfs_buftarg_t *btp,
			xfs_daddr_t blkno, int len, uint flags,
			struct xfs_buf **bpp);
extern void xfs_ioerror_alert(char *func, struct xfs_mount *mp,
				xfs_buf_t *bp, xfs_daddr_t blkno);

/*
 * Prototypes for functions in xfs_vnodeops.c.
 */
extern int xfs_free_eofblocks(struct xfs_mount *mp, struct xfs_inode *ip,
			int flags);

#endif /* __XFS_RW_H__ */
