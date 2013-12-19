/*
 * Copyright (c) 2006-2007 Silicon Graphics, Inc.
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
#ifndef __XFS_FILESTREAM_H__
#define __XFS_FILESTREAM_H__

struct xfs_mount;
struct xfs_inode;
struct xfs_perag;
struct xfs_bmalloca;

#ifdef XFS_FILESTREAMS_TRACE
#define XFS_FSTRM_KTRACE_INFO		1
#define XFS_FSTRM_KTRACE_AGSCAN		2
#define XFS_FSTRM_KTRACE_AGPICK1	3
#define XFS_FSTRM_KTRACE_AGPICK2	4
#define XFS_FSTRM_KTRACE_UPDATE		5
#define XFS_FSTRM_KTRACE_FREE		6
#define	XFS_FSTRM_KTRACE_ITEM_LOOKUP	7
#define	XFS_FSTRM_KTRACE_ASSOCIATE	8
#define	XFS_FSTRM_KTRACE_MOVEAG		9
#define	XFS_FSTRM_KTRACE_ORPHAN		10

#define XFS_FSTRM_KTRACE_SIZE	16384
extern ktrace_t *xfs_filestreams_trace_buf;

#endif

/* allocation selection flags */
typedef enum xfs_fstrm_alloc {
	XFS_PICK_USERDATA = 1,
	XFS_PICK_LOWSPACE = 2,
} xfs_fstrm_alloc_t;

/* prototypes for filestream.c */
int xfs_filestream_init(void);
void xfs_filestream_uninit(void);
int xfs_filestream_mount(struct xfs_mount *mp);
void xfs_filestream_unmount(struct xfs_mount *mp);
xfs_agnumber_t xfs_filestream_lookup_ag(struct xfs_inode *ip);
int xfs_filestream_associate(struct xfs_inode *dip, struct xfs_inode *ip);
void xfs_filestream_deassociate(struct xfs_inode *ip);
int xfs_filestream_new_ag(struct xfs_bmalloca *ap, xfs_agnumber_t *agp);


/* filestreams for the inode? */
static inline int
xfs_inode_is_filestream(
	struct xfs_inode	*ip)
{
	return (ip->i_mount->m_flags & XFS_MOUNT_FILESTREAMS) ||
		xfs_iflags_test(ip, XFS_IFILESTREAM) ||
		(ip->i_d.di_flags & XFS_DIFLAG_FILESTREAM);
}

#endif /* __XFS_FILESTREAM_H__ */
