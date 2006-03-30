/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
 * Maximum count of bmaps used by read and write paths.
 */
#define	XFS_MAX_RW_NBMAPS	4

/*
 * Counts of readahead buffers to use based on physical memory size.
 * None of these should be more than XFS_MAX_RW_NBMAPS.
 */
#define	XFS_RW_NREADAHEAD_16MB	2
#define	XFS_RW_NREADAHEAD_32MB	3
#define	XFS_RW_NREADAHEAD_K32	4
#define	XFS_RW_NREADAHEAD_K64	4

/*
 * Maximum size of a buffer that we\'ll map.  Making this
 * too big will degrade performance due to the number of
 * pages which need to be gathered.  Making it too small
 * will prevent us from doing large I/O\'s to hardware that
 * needs it.
 *
 * This is currently set to 512 KB.
 */
#define	XFS_MAX_BMAP_LEN_BB	1024
#define	XFS_MAX_BMAP_LEN_BYTES	524288

/*
 * Convert the given file system block to a disk block.
 * We have to treat it differently based on whether the
 * file is a real time file or not, because the bmap code
 * does.
 */
#define	XFS_FSB_TO_DB(ip,fsb)	xfs_fsb_to_db(ip,fsb)
static inline xfs_daddr_t
xfs_fsb_to_db(struct xfs_inode *ip, xfs_fsblock_t fsb)
{
	return (((ip)->i_d.di_flags & XFS_DIFLAG_REALTIME) ? \
		 (xfs_daddr_t)XFS_FSB_TO_BB((ip)->i_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((ip)->i_mount, (fsb)));
}
#define XFS_FSB_TO_DB_IO(io,fsb) xfs_fsb_to_db_io(io,fsb)
static inline xfs_daddr_t
xfs_fsb_to_db_io(struct xfs_iocore *io, xfs_fsblock_t fsb)
{
	return (((io)->io_flags & XFS_IOCORE_RT) ? \
		 XFS_FSB_TO_BB((io)->io_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((io)->io_mount, (fsb)));
}

/*
 * Prototypes for functions in xfs_rw.c.
 */
extern int xfs_write_clear_setuid(struct xfs_inode *ip);
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
extern int xfs_rwlock(bhv_desc_t *bdp, vrwlock_t write_lock);
extern void xfs_rwunlock(bhv_desc_t *bdp, vrwlock_t write_lock);
extern int xfs_setattr(bhv_desc_t *bdp, vattr_t *vap, int flags, cred_t *credp);
extern int xfs_change_file_space(bhv_desc_t *bdp, int cmd, xfs_flock64_t *bf,
				 xfs_off_t offset, cred_t *credp, int flags);
extern int xfs_set_dmattrs(bhv_desc_t *bdp, u_int evmask, u_int16_t state,
			   cred_t *credp);

#endif /* __XFS_RW_H__ */
