// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_INODE_FORK_H__
#define	__XFS_INODE_FORK_H__

struct xfs_inode_log_item;
struct xfs_dinode;

/*
 * File incore extent information, present for each of data & attr forks.
 */
struct xfs_ifork {
	int64_t			if_bytes;	/* bytes in if_u1 */
	struct xfs_btree_block	*if_broot;	/* file's incore btree root */
	unsigned int		if_seq;		/* fork mod counter */
	int			if_height;	/* height of the extent tree */
	union {
		void		*if_root;	/* extent tree root */
		char		*if_data;	/* inline file data */
	} if_u1;
	short			if_broot_bytes;	/* bytes allocated for root */
	unsigned char		if_flags;	/* per-fork flags */
	int8_t			if_format;	/* format of this fork */
	xfs_extnum_t		if_nextents;	/* # of extents in this fork */
};

/*
 * Per-fork incore inode flags.
 */
#define	XFS_IFINLINE	0x01	/* Inline data is read in */
#define	XFS_IFEXTENTS	0x02	/* All extent pointers are read in */
#define	XFS_IFBROOT	0x04	/* i_broot points to the bmap b-tree root */

/*
 * Fork handling.
 */

#define XFS_IFORK_Q(ip)			((ip)->i_d.di_forkoff != 0)
#define XFS_IFORK_BOFF(ip)		((int)((ip)->i_d.di_forkoff << 3))

#define XFS_IFORK_PTR(ip,w)		\
	((w) == XFS_DATA_FORK ? \
		&(ip)->i_df : \
		((w) == XFS_ATTR_FORK ? \
			(ip)->i_afp : \
			(ip)->i_cowfp))
#define XFS_IFORK_DSIZE(ip) \
	(XFS_IFORK_Q(ip) ? XFS_IFORK_BOFF(ip) : XFS_LITINO((ip)->i_mount))
#define XFS_IFORK_ASIZE(ip) \
	(XFS_IFORK_Q(ip) ? XFS_LITINO((ip)->i_mount) - XFS_IFORK_BOFF(ip) : 0)
#define XFS_IFORK_SIZE(ip,w) \
	((w) == XFS_DATA_FORK ? \
		XFS_IFORK_DSIZE(ip) : \
		((w) == XFS_ATTR_FORK ? \
			XFS_IFORK_ASIZE(ip) : \
			0))
#define XFS_IFORK_MAXEXT(ip, w) \
	(XFS_IFORK_SIZE(ip, w) / sizeof(xfs_bmbt_rec_t))

static inline bool xfs_ifork_has_extents(struct xfs_ifork *ifp)
{
	return ifp->if_format == XFS_DINODE_FMT_EXTENTS ||
		ifp->if_format == XFS_DINODE_FMT_BTREE;
}

static inline xfs_extnum_t xfs_ifork_nextents(struct xfs_ifork *ifp)
{
	if (!ifp)
		return 0;
	return ifp->if_nextents;
}

static inline int8_t xfs_ifork_format(struct xfs_ifork *ifp)
{
	if (!ifp)
		return XFS_DINODE_FMT_EXTENTS;
	return ifp->if_format;
}

struct xfs_ifork *xfs_iext_state_to_fork(struct xfs_inode *ip, int state);

int		xfs_iformat_data_fork(struct xfs_inode *, struct xfs_dinode *);
int		xfs_iformat_attr_fork(struct xfs_inode *, struct xfs_dinode *);
void		xfs_iflush_fork(struct xfs_inode *, struct xfs_dinode *,
				struct xfs_inode_log_item *, int);
void		xfs_idestroy_fork(struct xfs_ifork *ifp);
void		xfs_idata_realloc(struct xfs_inode *ip, int64_t byte_diff,
				int whichfork);
void		xfs_iroot_realloc(struct xfs_inode *, int, int);
int		xfs_iread_extents(struct xfs_trans *, struct xfs_inode *, int);
int		xfs_iextents_copy(struct xfs_inode *, struct xfs_bmbt_rec *,
				  int);
void		xfs_init_local_fork(struct xfs_inode *ip, int whichfork,
				const void *data, int64_t size);

xfs_extnum_t	xfs_iext_count(struct xfs_ifork *ifp);
void		xfs_iext_insert(struct xfs_inode *, struct xfs_iext_cursor *cur,
			struct xfs_bmbt_irec *, int);
void		xfs_iext_remove(struct xfs_inode *, struct xfs_iext_cursor *,
			int);
void		xfs_iext_destroy(struct xfs_ifork *);

bool		xfs_iext_lookup_extent(struct xfs_inode *ip,
			struct xfs_ifork *ifp, xfs_fileoff_t bno,
			struct xfs_iext_cursor *cur,
			struct xfs_bmbt_irec *gotp);
bool		xfs_iext_lookup_extent_before(struct xfs_inode *ip,
			struct xfs_ifork *ifp, xfs_fileoff_t *end,
			struct xfs_iext_cursor *cur,
			struct xfs_bmbt_irec *gotp);
bool		xfs_iext_get_extent(struct xfs_ifork *ifp,
			struct xfs_iext_cursor *cur,
			struct xfs_bmbt_irec *gotp);
void		xfs_iext_update_extent(struct xfs_inode *ip, int state,
			struct xfs_iext_cursor *cur,
			struct xfs_bmbt_irec *gotp);

void		xfs_iext_first(struct xfs_ifork *, struct xfs_iext_cursor *);
void		xfs_iext_last(struct xfs_ifork *, struct xfs_iext_cursor *);
void		xfs_iext_next(struct xfs_ifork *, struct xfs_iext_cursor *);
void		xfs_iext_prev(struct xfs_ifork *, struct xfs_iext_cursor *);

static inline bool xfs_iext_next_extent(struct xfs_ifork *ifp,
		struct xfs_iext_cursor *cur, struct xfs_bmbt_irec *gotp)
{
	xfs_iext_next(ifp, cur);
	return xfs_iext_get_extent(ifp, cur, gotp);
}

static inline bool xfs_iext_prev_extent(struct xfs_ifork *ifp,
		struct xfs_iext_cursor *cur, struct xfs_bmbt_irec *gotp)
{
	xfs_iext_prev(ifp, cur);
	return xfs_iext_get_extent(ifp, cur, gotp);
}

/*
 * Return the extent after cur in gotp without updating the cursor.
 */
static inline bool xfs_iext_peek_next_extent(struct xfs_ifork *ifp,
		struct xfs_iext_cursor *cur, struct xfs_bmbt_irec *gotp)
{
	struct xfs_iext_cursor ncur = *cur;

	xfs_iext_next(ifp, &ncur);
	return xfs_iext_get_extent(ifp, &ncur, gotp);
}

/*
 * Return the extent before cur in gotp without updating the cursor.
 */
static inline bool xfs_iext_peek_prev_extent(struct xfs_ifork *ifp,
		struct xfs_iext_cursor *cur, struct xfs_bmbt_irec *gotp)
{
	struct xfs_iext_cursor ncur = *cur;

	xfs_iext_prev(ifp, &ncur);
	return xfs_iext_get_extent(ifp, &ncur, gotp);
}

#define for_each_xfs_iext(ifp, ext, got)		\
	for (xfs_iext_first((ifp), (ext));		\
	     xfs_iext_get_extent((ifp), (ext), (got));	\
	     xfs_iext_next((ifp), (ext)))

extern struct kmem_zone	*xfs_ifork_zone;

extern void xfs_ifork_init_cow(struct xfs_inode *ip);

int xfs_ifork_verify_local_data(struct xfs_inode *ip);
int xfs_ifork_verify_local_attr(struct xfs_inode *ip);

#endif	/* __XFS_INODE_FORK_H__ */
