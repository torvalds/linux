// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_TRANS_H__
#define	__XFS_TRANS_H__

/* kernel only transaction subsystem defines */

struct xlog;
struct xfs_buf;
struct xfs_buftarg;
struct xfs_efd_log_item;
struct xfs_efi_log_item;
struct xfs_inode;
struct xfs_item_ops;
struct xfs_log_iovec;
struct xfs_mount;
struct xfs_trans;
struct xfs_trans_res;
struct xfs_dquot_acct;
struct xfs_rud_log_item;
struct xfs_rui_log_item;
struct xfs_btree_cur;
struct xfs_cui_log_item;
struct xfs_cud_log_item;
struct xfs_bui_log_item;
struct xfs_bud_log_item;

struct xfs_log_item {
	struct list_head		li_ail;		/* AIL pointers */
	struct list_head		li_trans;	/* transaction list */
	xfs_lsn_t			li_lsn;		/* last on-disk lsn */
	struct xlog			*li_log;
	struct xfs_ail			*li_ailp;	/* ptr to AIL */
	uint				li_type;	/* item type */
	unsigned long			li_flags;	/* misc flags */
	struct xfs_buf			*li_buf;	/* real buffer pointer */
	struct list_head		li_bio_list;	/* buffer item list */
	const struct xfs_item_ops	*li_ops;	/* function list */

	/* delayed logging */
	struct list_head		li_cil;		/* CIL pointers */
	struct xfs_log_vec		*li_lv;		/* active log vector */
	struct xfs_log_vec		*li_lv_shadow;	/* standby vector */
	xfs_csn_t			li_seq;		/* CIL commit seq */
	uint32_t			li_order_id;	/* CIL commit order */
};

/*
 * li_flags use the (set/test/clear)_bit atomic interfaces because updates can
 * race with each other and we don't want to have to use the AIL lock to
 * serialise all updates.
 */
#define	XFS_LI_IN_AIL	0
#define	XFS_LI_ABORTED	1
#define	XFS_LI_FAILED	2
#define	XFS_LI_DIRTY	3
#define	XFS_LI_WHITEOUT	4

#define XFS_LI_FLAGS \
	{ (1u << XFS_LI_IN_AIL),	"IN_AIL" }, \
	{ (1u << XFS_LI_ABORTED),	"ABORTED" }, \
	{ (1u << XFS_LI_FAILED),	"FAILED" }, \
	{ (1u << XFS_LI_DIRTY),		"DIRTY" }, \
	{ (1u << XFS_LI_WHITEOUT),	"WHITEOUT" }

struct xfs_item_ops {
	unsigned flags;
	void (*iop_size)(struct xfs_log_item *, int *, int *);
	void (*iop_format)(struct xfs_log_item *, struct xfs_log_vec *);
	void (*iop_pin)(struct xfs_log_item *);
	void (*iop_unpin)(struct xfs_log_item *, int remove);
	uint64_t (*iop_sort)(struct xfs_log_item *lip);
	int (*iop_precommit)(struct xfs_trans *tp, struct xfs_log_item *lip);
	void (*iop_committing)(struct xfs_log_item *lip, xfs_csn_t seq);
	xfs_lsn_t (*iop_committed)(struct xfs_log_item *, xfs_lsn_t);
	uint (*iop_push)(struct xfs_log_item *, struct list_head *);
	void (*iop_release)(struct xfs_log_item *);
	int (*iop_recover)(struct xfs_log_item *lip,
			   struct list_head *capture_list);
	bool (*iop_match)(struct xfs_log_item *item, uint64_t id);
	struct xfs_log_item *(*iop_relog)(struct xfs_log_item *intent,
			struct xfs_trans *tp);
	struct xfs_log_item *(*iop_intent)(struct xfs_log_item *intent_done);
};

/*
 * Log item ops flags
 */
/*
 * Release the log item when the journal commits instead of inserting into the
 * AIL for writeback tracking and/or log tail pinning.
 */
#define XFS_ITEM_RELEASE_WHEN_COMMITTED	(1 << 0)
#define XFS_ITEM_INTENT			(1 << 1)
#define XFS_ITEM_INTENT_DONE		(1 << 2)

static inline bool
xlog_item_is_intent(struct xfs_log_item *lip)
{
	return lip->li_ops->flags & XFS_ITEM_INTENT;
}

static inline bool
xlog_item_is_intent_done(struct xfs_log_item *lip)
{
	return lip->li_ops->flags & XFS_ITEM_INTENT_DONE;
}

void	xfs_log_item_init(struct xfs_mount *mp, struct xfs_log_item *item,
			  int type, const struct xfs_item_ops *ops);

/*
 * Return values for the iop_push() routines.
 */
#define XFS_ITEM_SUCCESS	0
#define XFS_ITEM_PINNED		1
#define XFS_ITEM_LOCKED		2
#define XFS_ITEM_FLUSHING	3

/*
 * This is the structure maintained for every active transaction.
 */
typedef struct xfs_trans {
	unsigned int		t_magic;	/* magic number */
	unsigned int		t_log_res;	/* amt of log space resvd */
	unsigned int		t_log_count;	/* count for perm log res */
	unsigned int		t_blk_res;	/* # of blocks resvd */
	unsigned int		t_blk_res_used;	/* # of resvd blocks used */
	unsigned int		t_rtx_res;	/* # of rt extents resvd */
	unsigned int		t_rtx_res_used;	/* # of resvd rt extents used */
	unsigned int		t_flags;	/* misc flags */
	xfs_fsblock_t		t_firstblock;	/* first block allocated */
	struct xlog_ticket	*t_ticket;	/* log mgr ticket */
	struct xfs_mount	*t_mountp;	/* ptr to fs mount struct */
	struct xfs_dquot_acct   *t_dqinfo;	/* acctg info for dquots */
	int64_t			t_icount_delta;	/* superblock icount change */
	int64_t			t_ifree_delta;	/* superblock ifree change */
	int64_t			t_fdblocks_delta; /* superblock fdblocks chg */
	int64_t			t_res_fdblocks_delta; /* on-disk only chg */
	int64_t			t_frextents_delta;/* superblock freextents chg*/
	int64_t			t_res_frextents_delta; /* on-disk only chg */
	int64_t			t_dblocks_delta;/* superblock dblocks change */
	int64_t			t_agcount_delta;/* superblock agcount change */
	int64_t			t_imaxpct_delta;/* superblock imaxpct change */
	int64_t			t_rextsize_delta;/* superblock rextsize chg */
	int64_t			t_rbmblocks_delta;/* superblock rbmblocks chg */
	int64_t			t_rblocks_delta;/* superblock rblocks change */
	int64_t			t_rextents_delta;/* superblocks rextents chg */
	int64_t			t_rextslog_delta;/* superblocks rextslog chg */
	struct list_head	t_items;	/* log item descriptors */
	struct list_head	t_busy;		/* list of busy extents */
	struct list_head	t_dfops;	/* deferred operations */
	unsigned long		t_pflags;	/* saved process flags state */
} xfs_trans_t;

/*
 * XFS transaction mechanism exported interfaces that are
 * actually macros.
 */
#define	xfs_trans_set_sync(tp)		((tp)->t_flags |= XFS_TRANS_SYNC)

/*
 * XFS transaction mechanism exported interfaces.
 */
int		xfs_trans_alloc(struct xfs_mount *mp, struct xfs_trans_res *resp,
			uint blocks, uint rtextents, uint flags,
			struct xfs_trans **tpp);
int		xfs_trans_alloc_empty(struct xfs_mount *mp,
			struct xfs_trans **tpp);
void		xfs_trans_mod_sb(xfs_trans_t *, uint, int64_t);

int xfs_trans_get_buf_map(struct xfs_trans *tp, struct xfs_buftarg *target,
		struct xfs_buf_map *map, int nmaps, xfs_buf_flags_t flags,
		struct xfs_buf **bpp);

static inline int
xfs_trans_get_buf(
	struct xfs_trans	*tp,
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	int			numblks,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return xfs_trans_get_buf_map(tp, target, &map, 1, flags, bpp);
}

int		xfs_trans_read_buf_map(struct xfs_mount *mp,
				       struct xfs_trans *tp,
				       struct xfs_buftarg *target,
				       struct xfs_buf_map *map, int nmaps,
				       xfs_buf_flags_t flags,
				       struct xfs_buf **bpp,
				       const struct xfs_buf_ops *ops);

static inline int
xfs_trans_read_buf(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	int			numblks,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp,
	const struct xfs_buf_ops *ops)
{
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);
	return xfs_trans_read_buf_map(mp, tp, target, &map, 1,
				      flags, bpp, ops);
}

struct xfs_buf	*xfs_trans_getsb(struct xfs_trans *);

void		xfs_trans_brelse(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_bjoin(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_bhold(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_bhold_release(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_binval(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_inode_buf(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_stale_inode_buf(xfs_trans_t *, struct xfs_buf *);
bool		xfs_trans_ordered_buf(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_dquot_buf(xfs_trans_t *, struct xfs_buf *, uint);
void		xfs_trans_inode_alloc_buf(xfs_trans_t *, struct xfs_buf *);
void		xfs_trans_ichgtime(struct xfs_trans *, struct xfs_inode *, int);
void		xfs_trans_ijoin(struct xfs_trans *, struct xfs_inode *, uint);
void		xfs_trans_log_buf(struct xfs_trans *, struct xfs_buf *, uint,
				  uint);
void		xfs_trans_dirty_buf(struct xfs_trans *, struct xfs_buf *);
bool		xfs_trans_buf_is_dirty(struct xfs_buf *bp);
void		xfs_trans_log_inode(xfs_trans_t *, struct xfs_inode *, uint);

int		xfs_trans_commit(struct xfs_trans *);
int		xfs_trans_roll(struct xfs_trans **);
int		xfs_trans_roll_inode(struct xfs_trans **, struct xfs_inode *);
void		xfs_trans_cancel(xfs_trans_t *);
int		xfs_trans_ail_init(struct xfs_mount *);
void		xfs_trans_ail_destroy(struct xfs_mount *);

void		xfs_trans_buf_set_type(struct xfs_trans *, struct xfs_buf *,
				       enum xfs_blft);
void		xfs_trans_buf_copy_type(struct xfs_buf *dst_bp,
					struct xfs_buf *src_bp);

extern struct kmem_cache	*xfs_trans_cache;

static inline struct xfs_log_item *
xfs_trans_item_relog(
	struct xfs_log_item	*lip,
	struct xfs_trans	*tp)
{
	return lip->li_ops->iop_relog(lip, tp);
}

struct xfs_dquot;

int xfs_trans_alloc_inode(struct xfs_inode *ip, struct xfs_trans_res *resv,
		unsigned int dblocks, unsigned int rblocks, bool force,
		struct xfs_trans **tpp);
int xfs_trans_alloc_icreate(struct xfs_mount *mp, struct xfs_trans_res *resv,
		struct xfs_dquot *udqp, struct xfs_dquot *gdqp,
		struct xfs_dquot *pdqp, unsigned int dblocks,
		struct xfs_trans **tpp);
int xfs_trans_alloc_ichange(struct xfs_inode *ip, struct xfs_dquot *udqp,
		struct xfs_dquot *gdqp, struct xfs_dquot *pdqp, bool force,
		struct xfs_trans **tpp);
int xfs_trans_alloc_dir(struct xfs_inode *dp, struct xfs_trans_res *resv,
		struct xfs_inode *ip, unsigned int *dblocks,
		struct xfs_trans **tpp, int *nospace_error);

static inline void
xfs_trans_set_context(
	struct xfs_trans	*tp)
{
	ASSERT(current->journal_info == NULL);
	tp->t_pflags = memalloc_nofs_save();
	current->journal_info = tp;
}

static inline void
xfs_trans_clear_context(
	struct xfs_trans	*tp)
{
	if (current->journal_info == tp) {
		memalloc_nofs_restore(tp->t_pflags);
		current->journal_info = NULL;
	}
}

static inline void
xfs_trans_switch_context(
	struct xfs_trans	*old_tp,
	struct xfs_trans	*new_tp)
{
	ASSERT(current->journal_info == old_tp);
	new_tp->t_pflags = old_tp->t_pflags;
	old_tp->t_pflags = 0;
	current->journal_info = new_tp;
}

#endif	/* __XFS_TRANS_H__ */
