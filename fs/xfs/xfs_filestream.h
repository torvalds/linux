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

#ifdef __KERNEL__

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

/*
 * Allocation group filestream associations are tracked with per-ag atomic
 * counters.  These counters allow _xfs_filestream_pick_ag() to tell whether a
 * particular AG already has active filestreams associated with it. The mount
 * point's m_peraglock is used to protect these counters from per-ag array
 * re-allocation during a growfs operation.  When xfs_growfs_data_private() is
 * about to reallocate the array, it calls xfs_filestream_flush() with the
 * m_peraglock held in write mode.
 *
 * Since xfs_mru_cache_flush() guarantees that all the free functions for all
 * the cache elements have finished executing before it returns, it's safe for
 * the free functions to use the atomic counters without m_peraglock protection.
 * This allows the implementation of xfs_fstrm_free_func() to be agnostic about
 * whether it was called with the m_peraglock held in read mode, write mode or
 * not held at all.  The race condition this addresses is the following:
 *
 *  - The work queue scheduler fires and pulls a filestream directory cache
 *    element off the LRU end of the cache for deletion, then gets pre-empted.
 *  - A growfs operation grabs the m_peraglock in write mode, flushes all the
 *    remaining items from the cache and reallocates the mount point's per-ag
 *    array, resetting all the counters to zero.
 *  - The work queue thread resumes and calls the free function for the element
 *    it started cleaning up earlier.  In the process it decrements the
 *    filestreams counter for an AG that now has no references.
 *
 * With a shrinkfs feature, the above scenario could panic the system.
 *
 * All other uses of the following macros should be protected by either the
 * m_peraglock held in read mode, or the cache's internal locking exposed by the
 * interval between a call to xfs_mru_cache_lookup() and a call to
 * xfs_mru_cache_done().  In addition, the m_peraglock must be held in read mode
 * when new elements are added to the cache.
 *
 * Combined, these locking rules ensure that no associations will ever exist in
 * the cache that reference per-ag array elements that have since been
 * reallocated.
 */
/*
 * xfs_filestream_peek_ag is only used in tracing code
 */
static inline int
xfs_filestream_peek_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;
	int		ret;

	pag = xfs_perag_get(mp, agno);
	ret = atomic_read(&pag->pagf_fstrms);
	xfs_perag_put(pag);
	return ret;
}

static inline int
xfs_filestream_get_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;
	int		ret;

	pag = xfs_perag_get(mp, agno);
	ret = atomic_inc_return(&pag->pagf_fstrms);
	xfs_perag_put(pag);
	return ret;
}

static inline int
xfs_filestream_put_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;
	int		ret;

	pag = xfs_perag_get(mp, agno);
	ret = atomic_dec_return(&pag->pagf_fstrms);
	xfs_perag_put(pag);
	return ret;
}

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

#endif /* __KERNEL__ */

#endif /* __XFS_FILESTREAM_H__ */
