/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Red Hat, Inc.
 * All rights reserved.
 */

#ifndef __LIBXFS_AG_H
#define __LIBXFS_AG_H 1

struct xfs_mount;
struct xfs_trans;
struct xfs_perag;

/*
 * Per-ag infrastructure
 */

/* per-AG block reservation data structures*/
struct xfs_ag_resv {
	/* number of blocks originally reserved here */
	xfs_extlen_t			ar_orig_reserved;
	/* number of blocks reserved here */
	xfs_extlen_t			ar_reserved;
	/* number of blocks originally asked for */
	xfs_extlen_t			ar_asked;
};

/*
 * Per-ag incore structure, copies of information in agf and agi, to improve the
 * performance of allocation group selection.
 */
struct xfs_perag {
	struct xfs_mount *pag_mount;	/* owner filesystem */
	xfs_agnumber_t	pag_agno;	/* AG this structure belongs to */
	atomic_t	pag_ref;	/* perag reference count */
	char		pagf_init;	/* this agf's entry is initialized */
	char		pagi_init;	/* this agi's entry is initialized */
	char		pagf_metadata;	/* the agf is preferred to be metadata */
	char		pagi_inodeok;	/* The agi is ok for inodes */
	uint8_t		pagf_levels[XFS_BTNUM_AGF];
					/* # of levels in bno & cnt btree */
	bool		pagf_agflreset; /* agfl requires reset before use */
	uint32_t	pagf_flcount;	/* count of blocks in freelist */
	xfs_extlen_t	pagf_freeblks;	/* total free blocks */
	xfs_extlen_t	pagf_longest;	/* longest free space */
	uint32_t	pagf_btreeblks;	/* # of blocks held in AGF btrees */
	xfs_agino_t	pagi_freecount;	/* number of free inodes */
	xfs_agino_t	pagi_count;	/* number of allocated inodes */

	/*
	 * Inode allocation search lookup optimisation.
	 * If the pagino matches, the search for new inodes
	 * doesn't need to search the near ones again straight away
	 */
	xfs_agino_t	pagl_pagino;
	xfs_agino_t	pagl_leftrec;
	xfs_agino_t	pagl_rightrec;

	int		pagb_count;	/* pagb slots in use */
	uint8_t		pagf_refcount_level; /* recount btree height */

	/* Blocks reserved for all kinds of metadata. */
	struct xfs_ag_resv	pag_meta_resv;
	/* Blocks reserved for the reverse mapping btree. */
	struct xfs_ag_resv	pag_rmapbt_resv;

	/* -- kernel only structures below this line -- */

	/*
	 * Bitsets of per-ag metadata that have been checked and/or are sick.
	 * Callers should hold pag_state_lock before accessing this field.
	 */
	uint16_t	pag_checked;
	uint16_t	pag_sick;
	spinlock_t	pag_state_lock;

	spinlock_t	pagb_lock;	/* lock for pagb_tree */
	struct rb_root	pagb_tree;	/* ordered tree of busy extents */
	unsigned int	pagb_gen;	/* generation count for pagb_tree */
	wait_queue_head_t pagb_wait;	/* woken when pagb_gen changes */

	atomic_t        pagf_fstrms;    /* # of filestreams active in this AG */

	spinlock_t	pag_ici_lock;	/* incore inode cache lock */
	struct radix_tree_root pag_ici_root;	/* incore inode cache root */
	int		pag_ici_reclaimable;	/* reclaimable inodes */
	unsigned long	pag_ici_reclaim_cursor;	/* reclaim restart point */

	/* buffer cache index */
	spinlock_t	pag_buf_lock;	/* lock for pag_buf_hash */
	struct rhashtable pag_buf_hash;

	/* for rcu-safe freeing */
	struct rcu_head	rcu_head;

	/* background prealloc block trimming */
	struct delayed_work	pag_blockgc_work;

	/*
	 * Unlinked inode information.  This incore information reflects
	 * data stored in the AGI, so callers must hold the AGI buffer lock
	 * or have some other means to control concurrency.
	 */
	struct rhashtable	pagi_unlinked_hash;
};

int xfs_initialize_perag(struct xfs_mount *mp, xfs_agnumber_t agcount,
			xfs_agnumber_t *maxagi);
int xfs_initialize_perag_data(struct xfs_mount *mp, xfs_agnumber_t agno);
void xfs_free_perag(struct xfs_mount *mp);

struct xfs_perag *xfs_perag_get(struct xfs_mount *mp, xfs_agnumber_t agno);
struct xfs_perag *xfs_perag_get_tag(struct xfs_mount *mp, xfs_agnumber_t agno,
		unsigned int tag);
void xfs_perag_put(struct xfs_perag *pag);

/*
 * Perag iteration APIs
 */
static inline struct xfs_perag *
xfs_perag_next(
	struct xfs_perag	*pag,
	xfs_agnumber_t		*agno,
	xfs_agnumber_t		end_agno)
{
	struct xfs_mount	*mp = pag->pag_mount;

	*agno = pag->pag_agno + 1;
	xfs_perag_put(pag);
	if (*agno > end_agno)
		return NULL;
	return xfs_perag_get(mp, *agno);
}

#define for_each_perag_range(mp, agno, end_agno, pag) \
	for ((pag) = xfs_perag_get((mp), (agno)); \
		(pag) != NULL; \
		(pag) = xfs_perag_next((pag), &(agno), (end_agno)))

#define for_each_perag_from(mp, agno, pag) \
	for_each_perag_range((mp), (agno), (mp)->m_sb.sb_agcount - 1, (pag))


#define for_each_perag(mp, agno, pag) \
	(agno) = 0; \
	for_each_perag_from((mp), (agno), (pag))

#define for_each_perag_tag(mp, agno, pag, tag) \
	for ((agno) = 0, (pag) = xfs_perag_get_tag((mp), 0, (tag)); \
		(pag) != NULL; \
		(agno) = (pag)->pag_agno + 1, \
		xfs_perag_put(pag), \
		(pag) = xfs_perag_get_tag((mp), (agno), (tag)))

struct aghdr_init_data {
	/* per ag data */
	xfs_agblock_t		agno;		/* ag to init */
	xfs_extlen_t		agsize;		/* new AG size */
	struct list_head	buffer_list;	/* buffer writeback list */
	xfs_rfsblock_t		nfree;		/* cumulative new free space */

	/* per header data */
	xfs_daddr_t		daddr;		/* header location */
	size_t			numblks;	/* size of header */
	xfs_btnum_t		type;		/* type of btree root block */
};

int xfs_ag_init_headers(struct xfs_mount *mp, struct aghdr_init_data *id);
int xfs_ag_shrink_space(struct xfs_mount *mp, struct xfs_trans **tpp,
			xfs_agnumber_t agno, xfs_extlen_t delta);
int xfs_ag_extend_space(struct xfs_mount *mp, struct xfs_trans *tp,
			struct aghdr_init_data *id, xfs_extlen_t len);
int xfs_ag_get_geometry(struct xfs_mount *mp, xfs_agnumber_t agno,
			struct xfs_ag_geometry *ageo);

#endif /* __LIBXFS_AG_H */
