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

struct file;
struct xfs_iomap;
struct attrlist_cursor_kern;

typedef struct inode	bhv_vnode_t;

/*
 * Vnode to Linux inode mapping.
 */
static inline bhv_vnode_t *vn_from_inode(struct inode *inode)
{
	return inode;
}
static inline struct inode *vn_to_inode(bhv_vnode_t *vnode)
{
	return vnode;
}

/*
 * Return values for xfs_inactive.  A return value of
 * VN_INACTIVE_NOCACHE implies that the file system behavior
 * has disassociated its state and bhv_desc_t from the vnode.
 */
#define	VN_INACTIVE_CACHE	0
#define	VN_INACTIVE_NOCACHE	1

/*
 * Flags for read/write calls - same values as IRIX
 */
#define IO_ISAIO	0x00001		/* don't wait for completion */
#define IO_ISDIRECT	0x00004		/* bypass page cache */
#define IO_INVIS	0x00020		/* don't update inode timestamps */

/*
 * Flags for xfs_inode_flush
 */
#define FLUSH_SYNC		1	/* wait for flush to complete	*/

/*
 * Flush/Invalidate options for vop_toss/flush/flushinval_pages.
 */
#define FI_NONE			0	/* none */
#define FI_REMAPF		1	/* Do a remapf prior to the operation */
#define FI_REMAPF_LOCKED	2	/* Do a remapf prior to the operation.
					   Prevent VM access to the pages until
					   the operation completes. */


extern void	vn_init(void);

/*
 * Yeah, these don't take vnode anymore at all, all this should be
 * cleaned up at some point.
 */
extern void	vn_iowait(struct xfs_inode *ip);
extern void	vn_iowake(struct xfs_inode *ip);
extern void	vn_ioerror(struct xfs_inode *ip, int error, char *f, int l);

static inline int vn_count(bhv_vnode_t *vp)
{
	return atomic_read(&vn_to_inode(vp)->i_count);
}

/*
 * Vnode reference counting functions (and macros for compatibility).
 */
extern bhv_vnode_t	*vn_hold(bhv_vnode_t *);

#if defined(XFS_INODE_TRACE)
#define VN_HOLD(vp)		\
	((void)vn_hold(vp),	\
	  xfs_itrace_hold(xfs_vtoi(vp), __FILE__, __LINE__, (inst_t *)__return_address))
#define VN_RELE(vp)		\
	  (xfs_itrace_rele(xfs_vtoi(vp), __FILE__, __LINE__, (inst_t *)__return_address), \
	   iput(vn_to_inode(vp)))
#else
#define VN_HOLD(vp)		((void)vn_hold(vp))
#define VN_RELE(vp)		(iput(vn_to_inode(vp)))
#endif

static inline bhv_vnode_t *vn_grab(bhv_vnode_t *vp)
{
	struct inode *inode = igrab(vn_to_inode(vp));
	return inode ? vn_from_inode(inode) : NULL;
}

/*
 * Dealing with bad inodes
 */
static inline int VN_BAD(bhv_vnode_t *vp)
{
	return is_bad_inode(vn_to_inode(vp));
}

/*
 * Extracting atime values in various formats
 */
static inline void vn_atime_to_bstime(bhv_vnode_t *vp, xfs_bstime_t *bs_atime)
{
	bs_atime->tv_sec = vp->i_atime.tv_sec;
	bs_atime->tv_nsec = vp->i_atime.tv_nsec;
}

static inline void vn_atime_to_timespec(bhv_vnode_t *vp, struct timespec *ts)
{
	*ts = vp->i_atime;
}

static inline void vn_atime_to_time_t(bhv_vnode_t *vp, time_t *tt)
{
	*tt = vp->i_atime.tv_sec;
}

/*
 * Some useful predicates.
 */
#define VN_MAPPED(vp)	mapping_mapped(vn_to_inode(vp)->i_mapping)
#define VN_CACHED(vp)	(vn_to_inode(vp)->i_mapping->nrpages)
#define VN_DIRTY(vp)	mapping_tagged(vn_to_inode(vp)->i_mapping, \
					PAGECACHE_TAG_DIRTY)


/*
 * Tracking vnode activity.
 */
#if defined(XFS_INODE_TRACE)

#define	INODE_TRACE_SIZE	16		/* number of trace entries */
#define	INODE_KTRACE_ENTRY	1
#define	INODE_KTRACE_EXIT	2
#define	INODE_KTRACE_HOLD	3
#define	INODE_KTRACE_REF	4
#define	INODE_KTRACE_RELE	5

extern void _xfs_itrace_entry(struct xfs_inode *, const char *, inst_t *);
extern void _xfs_itrace_exit(struct xfs_inode *, const char *, inst_t *);
extern void xfs_itrace_hold(struct xfs_inode *, char *, int, inst_t *);
extern void _xfs_itrace_ref(struct xfs_inode *, char *, int, inst_t *);
extern void xfs_itrace_rele(struct xfs_inode *, char *, int, inst_t *);
#define xfs_itrace_entry(ip)	\
	_xfs_itrace_entry(ip, __func__, (inst_t *)__return_address)
#define xfs_itrace_exit(ip)	\
	_xfs_itrace_exit(ip, __func__, (inst_t *)__return_address)
#define xfs_itrace_exit_tag(ip, tag)	\
	_xfs_itrace_exit(ip, tag, (inst_t *)__return_address)
#define xfs_itrace_ref(ip)	\
	_xfs_itrace_ref(ip, __FILE__, __LINE__, (inst_t *)__return_address)

#else
#define	xfs_itrace_entry(a)
#define	xfs_itrace_exit(a)
#define	xfs_itrace_exit_tag(a, b)
#define	xfs_itrace_hold(a, b, c, d)
#define	xfs_itrace_ref(a)
#define	xfs_itrace_rele(a, b, c, d)
#endif

#endif	/* __XFS_VNODE_H__ */
