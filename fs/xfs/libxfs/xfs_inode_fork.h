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
	int64_t			if_bytes;	/* bytes in if_data */
	struct xfs_btree_block	*if_broot;	/* file's incore btree root */
	unsigned int		if_seq;		/* fork mod counter */
	int			if_height;	/* height of the extent tree */
	void			*if_data;	/* extent tree root or
						   inline data */
	xfs_extnum_t		if_nextents;	/* # of extents in this fork */
	short			if_broot_bytes;	/* bytes allocated for root */
	int8_t			if_format;	/* format of this fork */
	uint8_t			if_needextents;	/* extents have not been read */
};

/*
 * Worst-case increase in the fork extent count when we're adding a single
 * extent to a fork and there's no possibility of splitting an existing mapping.
 */
#define XFS_IEXT_ADD_NOSPLIT_CNT	(1)

/*
 * Punching out an extent from the middle of an existing extent can cause the
 * extent count to increase by 1.
 * i.e. | Old extent | Hole | Old extent |
 */
#define XFS_IEXT_PUNCH_HOLE_CNT		(1)

/*
 * Adding/removing an xattr can cause XFS_DA_NODE_MAXDEPTH extents to
 * be added. One extra extent for dabtree in case a local attr is
 * large enough to cause a double split.  It can also cause extent
 * count to increase proportional to the size of a remote xattr's
 * value.
 */
#define XFS_IEXT_ATTR_MANIP_CNT(rmt_blks) \
	(XFS_DA_NODE_MAXDEPTH + max(1, rmt_blks))

/*
 * A write to a sub-interval of an existing unwritten extent causes the original
 * extent to be split into 3 extents
 * i.e. | Unwritten | Real | Unwritten |
 * Hence extent count can increase by 2.
 */
#define XFS_IEXT_WRITE_UNWRITTEN_CNT	(2)


/*
 * Moving an extent to data fork can cause a sub-interval of an existing extent
 * to be unmapped. This will increase extent count by 1. Mapping in the new
 * extent can increase the extent count by 1 again i.e.
 * | Old extent | New extent | Old extent |
 * Hence number of extents increases by 2.
 */
#define XFS_IEXT_REFLINK_END_COW_CNT	(2)

/*
 * Removing an initial range of source/donor file's extent and adding a new
 * extent (from donor/source file) in its place will cause extent count to
 * increase by 1.
 */
#define XFS_IEXT_SWAP_RMAP_CNT		(1)

/*
 * Fork handling.
 */
#define XFS_IFORK_MAXEXT(ip, w) \
	(xfs_inode_fork_size(ip, w) / sizeof(xfs_bmbt_rec_t))

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

static inline xfs_extnum_t xfs_iext_max_nextents(bool has_large_extent_counts,
				int whichfork)
{
	switch (whichfork) {
	case XFS_DATA_FORK:
	case XFS_COW_FORK:
		if (has_large_extent_counts)
			return XFS_MAX_EXTCNT_DATA_FORK_LARGE;
		return XFS_MAX_EXTCNT_DATA_FORK_SMALL;

	case XFS_ATTR_FORK:
		if (has_large_extent_counts)
			return XFS_MAX_EXTCNT_ATTR_FORK_LARGE;
		return XFS_MAX_EXTCNT_ATTR_FORK_SMALL;

	default:
		ASSERT(0);
		return 0;
	}
}

static inline xfs_extnum_t
xfs_dfork_data_extents(
	struct xfs_dinode	*dip)
{
	if (xfs_dinode_has_large_extent_counts(dip))
		return be64_to_cpu(dip->di_big_nextents);

	return be32_to_cpu(dip->di_nextents);
}

static inline xfs_extnum_t
xfs_dfork_attr_extents(
	struct xfs_dinode	*dip)
{
	if (xfs_dinode_has_large_extent_counts(dip))
		return be32_to_cpu(dip->di_big_anextents);

	return be16_to_cpu(dip->di_anextents);
}

static inline xfs_extnum_t
xfs_dfork_nextents(
	struct xfs_dinode	*dip,
	int			whichfork)
{
	switch (whichfork) {
	case XFS_DATA_FORK:
		return xfs_dfork_data_extents(dip);
	case XFS_ATTR_FORK:
		return xfs_dfork_attr_extents(dip);
	default:
		ASSERT(0);
		break;
	}

	return 0;
}

void xfs_ifork_zap_attr(struct xfs_inode *ip);
void xfs_ifork_init_attr(struct xfs_inode *ip, enum xfs_dinode_fmt format,
		xfs_extnum_t nextents);
struct xfs_ifork *xfs_iext_state_to_fork(struct xfs_inode *ip, int state);

int		xfs_iformat_data_fork(struct xfs_inode *, struct xfs_dinode *);
int		xfs_iformat_attr_fork(struct xfs_inode *, struct xfs_dinode *);
void		xfs_iflush_fork(struct xfs_inode *, struct xfs_dinode *,
				struct xfs_inode_log_item *, int);
void		xfs_idestroy_fork(struct xfs_ifork *ifp);
void *		xfs_idata_realloc(struct xfs_inode *ip, int64_t byte_diff,
				int whichfork);
void		xfs_iroot_realloc(struct xfs_inode *, int, int);
int		xfs_iread_extents(struct xfs_trans *, struct xfs_inode *, int);
int		xfs_iextents_copy(struct xfs_inode *, struct xfs_bmbt_rec *,
				  int);
void		xfs_init_local_fork(struct xfs_inode *ip, int whichfork,
				const void *data, int64_t size);

xfs_extnum_t	xfs_iext_count(struct xfs_ifork *ifp);
void		xfs_iext_insert_raw(struct xfs_ifork *ifp,
			struct xfs_iext_cursor *cur,
			struct xfs_bmbt_irec *irec);
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

extern struct kmem_cache	*xfs_ifork_cache;

extern void xfs_ifork_init_cow(struct xfs_inode *ip);

int xfs_ifork_verify_local_data(struct xfs_inode *ip);
int xfs_ifork_verify_local_attr(struct xfs_inode *ip);
int xfs_iext_count_may_overflow(struct xfs_inode *ip, int whichfork,
		int nr_to_add);
int xfs_iext_count_upgrade(struct xfs_trans *tp, struct xfs_inode *ip,
		uint nr_to_add);
bool xfs_ifork_is_realtime(struct xfs_inode *ip, int whichfork);

/* returns true if the fork has extents but they are not read in yet. */
static inline bool xfs_need_iread_extents(const struct xfs_ifork *ifp)
{
	/* see xfs_iformat_{data,attr}_fork() for needextents semantics */
	return smp_load_acquire(&ifp->if_needextents) != 0;
}

#endif	/* __XFS_INODE_FORK_H__ */
