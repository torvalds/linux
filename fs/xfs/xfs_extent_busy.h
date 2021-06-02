// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2010 David Chinner.
 * Copyright (c) 2011 Christoph Hellwig.
 * All Rights Reserved.
 */
#ifndef __XFS_EXTENT_BUSY_H__
#define	__XFS_EXTENT_BUSY_H__

struct xfs_mount;
struct xfs_trans;
struct xfs_alloc_arg;

/*
 * Busy block/extent entry.  Indexed by a rbtree in perag to mark blocks that
 * have been freed but whose transactions aren't committed to disk yet.
 *
 * Note that we use the transaction ID to record the transaction, not the
 * transaction structure itself. See xfs_extent_busy_insert() for details.
 */
struct xfs_extent_busy {
	struct rb_node	rb_node;	/* ag by-bno indexed search tree */
	struct list_head list;		/* transaction busy extent list */
	xfs_agnumber_t	agno;
	xfs_agblock_t	bno;
	xfs_extlen_t	length;
	unsigned int	flags;
#define XFS_EXTENT_BUSY_DISCARDED	0x01	/* undergoing a discard op. */
#define XFS_EXTENT_BUSY_SKIP_DISCARD	0x02	/* do not discard */
};

void
xfs_extent_busy_insert(struct xfs_trans *tp, xfs_agnumber_t agno,
	xfs_agblock_t bno, xfs_extlen_t len, unsigned int flags);

void
xfs_extent_busy_clear(struct xfs_mount *mp, struct list_head *list,
	bool do_discard);

int
xfs_extent_busy_search(struct xfs_mount *mp, xfs_agnumber_t agno,
	xfs_agblock_t bno, xfs_extlen_t len);

void
xfs_extent_busy_reuse(struct xfs_mount *mp, xfs_agnumber_t agno,
	xfs_agblock_t fbno, xfs_extlen_t flen, bool userdata);

bool
xfs_extent_busy_trim(struct xfs_alloc_arg *args, xfs_agblock_t *bno,
		xfs_extlen_t *len, unsigned *busy_gen);

void
xfs_extent_busy_flush(struct xfs_mount *mp, struct xfs_perag *pag,
	unsigned busy_gen);

void
xfs_extent_busy_wait_all(struct xfs_mount *mp);

int
xfs_extent_busy_ag_cmp(void *priv, const struct list_head *a,
	const struct list_head *b);

static inline void xfs_extent_busy_sort(struct list_head *list)
{
	list_sort(NULL, list, xfs_extent_busy_ag_cmp);
}

#endif /* __XFS_EXTENT_BUSY_H__ */
