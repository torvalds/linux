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
	int			if_bytes;	/* bytes in if_u1 */
	unsigned int		if_seq;		/* cow fork mod counter */
	struct xfs_btree_block	*if_broot;	/* file's incore btree root */
	short			if_broot_bytes;	/* bytes allocated for root */
	unsigned char		if_flags;	/* per-fork flags */
	int			if_height;	/* height of the extent tree */
	union {
		void		*if_root;	/* extent tree root */
		char		*if_data;	/* inline file data */
	} if_u1;
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
	(XFS_IFORK_Q(ip) ? \
		XFS_IFORK_BOFF(ip) : \
		XFS_LITINO((ip)->i_mount, (ip)->i_d.di_version))
#define XFS_IFORK_ASIZE(ip) \
	(XFS_IFORK_Q(ip) ? \
		XFS_LITINO((ip)->i_mount, (ip)->i_d.di_version) - \
			XFS_IFORK_BOFF(ip) : \
		0)
#define XFS_IFORK_SIZE(ip,w) \
	((w) == XFS_DATA_FORK ? \
		XFS_IFORK_DSIZE(ip) : \
		((w) == XFS_ATTR_FORK ? \
			XFS_IFORK_ASIZE(ip) : \
			0))
#define XFS_IFORK_FORMAT(ip,w) \
	((w) == XFS_DATA_FORK ? \
		(ip)->i_d.di_format : \
		((w) == XFS_ATTR_FORK ? \
			(ip)->i_d.di_aformat : \
			(ip)->i_cformat))
#define XFS_IFORK_FMT_SET(ip,w,n) \
	((w) == XFS_DATA_FORK ? \
		((ip)->i_d.di_format = (n)) : \
		((w) == XFS_ATTR_FORK ? \
			((ip)->i_d.di_aformat = (n)) : \
			((ip)->i_cformat = (n))))
#define XFS_IFORK_NEXTENTS(ip,w) \
	((w) == XFS_DATA_FORK ? \
		(ip)->i_d.di_nextents : \
		((w) == XFS_ATTR_FORK ? \
			(ip)->i_d.di_anextents : \
			(ip)->i_cnextents))
#define XFS_IFORK_NEXT_SET(ip,w,n) \
	((w) == XFS_DATA_FORK ? \
		((ip)->i_d.di_nextents = (n)) : \
		((w) == XFS_ATTR_FORK ? \
			((ip)->i_d.di_anextents = (n)) : \
			((ip)->i_cnextents = (n))))
#define XFS_IFORK_MAXEXT(ip, w) \
	(XFS_IFORK_SIZE(ip, w) / sizeof(xfs_bmbt_rec_t))

struct xfs_ifork *xfs_iext_state_to_fork(struct xfs_inode *ip, int state);

int		xfs_iformat_fork(struct xfs_inode *, struct xfs_dinode *);
void		xfs_iflush_fork(struct xfs_inode *, struct xfs_dinode *,
				struct xfs_inode_log_item *, int);
void		xfs_idestroy_fork(struct xfs_inode *, int);
void		xfs_idata_realloc(struct xfs_inode *, int, int);
void		xfs_iroot_realloc(struct xfs_inode *, int, int);
int		xfs_iread_extents(struct xfs_trans *, struct xfs_inode *, int);
int		xfs_iextents_copy(struct xfs_inode *, struct xfs_bmbt_rec *,
				  int);
void		xfs_init_local_fork(struct xfs_inode *, int, const void *, int);

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

typedef xfs_failaddr_t (*xfs_ifork_verifier_t)(struct xfs_inode *);

struct xfs_ifork_ops {
	xfs_ifork_verifier_t	verify_symlink;
	xfs_ifork_verifier_t	verify_dir;
	xfs_ifork_verifier_t	verify_attr;
};
extern struct xfs_ifork_ops	xfs_default_ifork_ops;

xfs_failaddr_t xfs_ifork_verify_data(struct xfs_inode *ip,
		struct xfs_ifork_ops *ops);
xfs_failaddr_t xfs_ifork_verify_attr(struct xfs_inode *ip,
		struct xfs_ifork_ops *ops);

#endif	/* __XFS_INODE_FORK_H__ */
