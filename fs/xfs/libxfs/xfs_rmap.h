// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_RMAP_H__
#define __XFS_RMAP_H__

struct xfs_perag;
struct xfs_rtgroup;

static inline void
xfs_rmap_ino_bmbt_owner(
	struct xfs_owner_info	*oi,
	xfs_ino_t		ino,
	int			whichfork)
{
	oi->oi_owner = ino;
	oi->oi_offset = 0;
	oi->oi_flags = XFS_OWNER_INFO_BMBT_BLOCK;
	if (whichfork == XFS_ATTR_FORK)
		oi->oi_flags |= XFS_OWNER_INFO_ATTR_FORK;
}

static inline void
xfs_rmap_ino_owner(
	struct xfs_owner_info	*oi,
	xfs_ino_t		ino,
	int			whichfork,
	xfs_fileoff_t		offset)
{
	oi->oi_owner = ino;
	oi->oi_offset = offset;
	oi->oi_flags = 0;
	if (whichfork == XFS_ATTR_FORK)
		oi->oi_flags |= XFS_OWNER_INFO_ATTR_FORK;
}

static inline bool
xfs_rmap_should_skip_owner_update(
	const struct xfs_owner_info	*oi)
{
	return oi->oi_owner == XFS_RMAP_OWN_NULL;
}

/* Reverse mapping functions. */

struct xfs_buf;

static inline __u64
xfs_rmap_irec_offset_pack(
	const struct xfs_rmap_irec	*irec)
{
	__u64			x;

	x = XFS_RMAP_OFF(irec->rm_offset);
	if (irec->rm_flags & XFS_RMAP_ATTR_FORK)
		x |= XFS_RMAP_OFF_ATTR_FORK;
	if (irec->rm_flags & XFS_RMAP_BMBT_BLOCK)
		x |= XFS_RMAP_OFF_BMBT_BLOCK;
	if (irec->rm_flags & XFS_RMAP_UNWRITTEN)
		x |= XFS_RMAP_OFF_UNWRITTEN;
	return x;
}

static inline xfs_failaddr_t
xfs_rmap_irec_offset_unpack(
	__u64			offset,
	struct xfs_rmap_irec	*irec)
{
	if (offset & ~(XFS_RMAP_OFF_MASK | XFS_RMAP_OFF_FLAGS))
		return __this_address;

	irec->rm_offset = XFS_RMAP_OFF(offset);
	irec->rm_flags = 0;
	if (offset & XFS_RMAP_OFF_ATTR_FORK)
		irec->rm_flags |= XFS_RMAP_ATTR_FORK;
	if (offset & XFS_RMAP_OFF_BMBT_BLOCK)
		irec->rm_flags |= XFS_RMAP_BMBT_BLOCK;
	if (offset & XFS_RMAP_OFF_UNWRITTEN)
		irec->rm_flags |= XFS_RMAP_UNWRITTEN;
	return NULL;
}

static inline void
xfs_owner_info_unpack(
	const struct xfs_owner_info	*oinfo,
	uint64_t			*owner,
	uint64_t			*offset,
	unsigned int			*flags)
{
	unsigned int			r = 0;

	*owner = oinfo->oi_owner;
	*offset = oinfo->oi_offset;
	if (oinfo->oi_flags & XFS_OWNER_INFO_ATTR_FORK)
		r |= XFS_RMAP_ATTR_FORK;
	if (oinfo->oi_flags & XFS_OWNER_INFO_BMBT_BLOCK)
		r |= XFS_RMAP_BMBT_BLOCK;
	*flags = r;
}

static inline void
xfs_owner_info_pack(
	struct xfs_owner_info	*oinfo,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags)
{
	oinfo->oi_owner = owner;
	oinfo->oi_offset = XFS_RMAP_OFF(offset);
	oinfo->oi_flags = 0;
	if (flags & XFS_RMAP_ATTR_FORK)
		oinfo->oi_flags |= XFS_OWNER_INFO_ATTR_FORK;
	if (flags & XFS_RMAP_BMBT_BLOCK)
		oinfo->oi_flags |= XFS_OWNER_INFO_BMBT_BLOCK;
}

int xfs_rmap_alloc(struct xfs_trans *tp, struct xfs_buf *agbp,
		   struct xfs_perag *pag, xfs_agblock_t bno, xfs_extlen_t len,
		   const struct xfs_owner_info *oinfo);
int xfs_rmap_free(struct xfs_trans *tp, struct xfs_buf *agbp,
		  struct xfs_perag *pag, xfs_agblock_t bno, xfs_extlen_t len,
		  const struct xfs_owner_info *oinfo);

int xfs_rmap_lookup_le(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		uint64_t owner, uint64_t offset, unsigned int flags,
		struct xfs_rmap_irec *irec, int *stat);
int xfs_rmap_lookup_eq(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		xfs_extlen_t len, uint64_t owner, uint64_t offset,
		unsigned int flags, int *stat);
int xfs_rmap_insert(struct xfs_btree_cur *rcur, xfs_agblock_t agbno,
		xfs_extlen_t len, uint64_t owner, uint64_t offset,
		unsigned int flags);
int xfs_rmap_get_rec(struct xfs_btree_cur *cur, struct xfs_rmap_irec *irec,
		int *stat);

typedef int (*xfs_rmap_query_range_fn)(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv);

int xfs_rmap_query_range(struct xfs_btree_cur *cur,
		const struct xfs_rmap_irec *low_rec,
		const struct xfs_rmap_irec *high_rec,
		xfs_rmap_query_range_fn fn, void *priv);
int xfs_rmap_query_all(struct xfs_btree_cur *cur, xfs_rmap_query_range_fn fn,
		void *priv);

enum xfs_rmap_intent_type {
	XFS_RMAP_MAP,
	XFS_RMAP_MAP_SHARED,
	XFS_RMAP_UNMAP,
	XFS_RMAP_UNMAP_SHARED,
	XFS_RMAP_CONVERT,
	XFS_RMAP_CONVERT_SHARED,
	XFS_RMAP_ALLOC,
	XFS_RMAP_FREE,
};

#define XFS_RMAP_INTENT_STRINGS \
	{ XFS_RMAP_MAP,			"map" }, \
	{ XFS_RMAP_MAP_SHARED,		"map_shared" }, \
	{ XFS_RMAP_UNMAP,		"unmap" }, \
	{ XFS_RMAP_UNMAP_SHARED,	"unmap_shared" }, \
	{ XFS_RMAP_CONVERT,		"cvt" }, \
	{ XFS_RMAP_CONVERT_SHARED,	"cvt_shared" }, \
	{ XFS_RMAP_ALLOC,		"alloc" }, \
	{ XFS_RMAP_FREE,		"free" }

struct xfs_rmap_intent {
	struct list_head			ri_list;
	enum xfs_rmap_intent_type		ri_type;
	int					ri_whichfork;
	uint64_t				ri_owner;
	struct xfs_bmbt_irec			ri_bmap;
	struct xfs_group			*ri_group;
	bool					ri_realtime;
};

/* functions for updating the rmapbt based on bmbt map/unmap operations */
void xfs_rmap_map_extent(struct xfs_trans *tp, struct xfs_inode *ip,
		int whichfork, struct xfs_bmbt_irec *imap);
void xfs_rmap_unmap_extent(struct xfs_trans *tp, struct xfs_inode *ip,
		int whichfork, struct xfs_bmbt_irec *imap);
void xfs_rmap_convert_extent(struct xfs_mount *mp, struct xfs_trans *tp,
		struct xfs_inode *ip, int whichfork,
		struct xfs_bmbt_irec *imap);
void xfs_rmap_alloc_extent(struct xfs_trans *tp, bool isrt, xfs_fsblock_t fsbno,
		xfs_extlen_t len, uint64_t owner);
void xfs_rmap_free_extent(struct xfs_trans *tp, bool isrt, xfs_fsblock_t fsbno,
		xfs_extlen_t len, uint64_t owner);

int xfs_rmap_finish_one(struct xfs_trans *tp, struct xfs_rmap_intent *ri,
		struct xfs_btree_cur **pcur);
int __xfs_rmap_finish_intent(struct xfs_btree_cur *rcur,
		enum xfs_rmap_intent_type op, xfs_agblock_t bno,
		xfs_extlen_t len, const struct xfs_owner_info *oinfo,
		bool unwritten);

int xfs_rmap_lookup_le_range(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		uint64_t owner, uint64_t offset, unsigned int flags,
		struct xfs_rmap_irec *irec, int	*stat);
int xfs_rmap_compare(const struct xfs_rmap_irec *a,
		const struct xfs_rmap_irec *b);
union xfs_btree_rec;
xfs_failaddr_t xfs_rmap_btrec_to_irec(const union xfs_btree_rec *rec,
		struct xfs_rmap_irec *irec);
xfs_failaddr_t xfs_rmap_check_irec(struct xfs_perag *pag,
		const struct xfs_rmap_irec *irec);
xfs_failaddr_t xfs_rtrmap_check_irec(struct xfs_rtgroup *rtg,
		const struct xfs_rmap_irec *irec);

int xfs_rmap_has_records(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		xfs_extlen_t len, enum xbtree_recpacking *outcome);

struct xfs_rmap_matches {
	/* Number of owner matches. */
	unsigned long long	matches;

	/* Number of non-owner matches. */
	unsigned long long	non_owner_matches;

	/* Number of non-owner matches that conflict with the owner matches. */
	unsigned long long	bad_non_owner_matches;
};

int xfs_rmap_count_owners(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		xfs_extlen_t len, const struct xfs_owner_info *oinfo,
		struct xfs_rmap_matches *rmatch);
int xfs_rmap_has_other_keys(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		xfs_extlen_t len, const struct xfs_owner_info *oinfo,
		bool *has_other);
int xfs_rmap_map_raw(struct xfs_btree_cur *cur, struct xfs_rmap_irec *rmap);

extern const struct xfs_owner_info XFS_RMAP_OINFO_SKIP_UPDATE;
extern const struct xfs_owner_info XFS_RMAP_OINFO_ANY_OWNER;
extern const struct xfs_owner_info XFS_RMAP_OINFO_FS;
extern const struct xfs_owner_info XFS_RMAP_OINFO_LOG;
extern const struct xfs_owner_info XFS_RMAP_OINFO_AG;
extern const struct xfs_owner_info XFS_RMAP_OINFO_INOBT;
extern const struct xfs_owner_info XFS_RMAP_OINFO_INODES;
extern const struct xfs_owner_info XFS_RMAP_OINFO_REFC;
extern const struct xfs_owner_info XFS_RMAP_OINFO_COW;

extern struct kmem_cache	*xfs_rmap_intent_cache;

int __init xfs_rmap_intent_init_cache(void);
void xfs_rmap_intent_destroy_cache(void);

/*
 * Parameters for tracking reverse mapping changes.  The hook function arg
 * parameter is enum xfs_rmap_intent_type, and the rest is below.
 */
struct xfs_rmap_update_params {
	xfs_agblock_t			startblock;
	xfs_extlen_t			blockcount;
	struct xfs_owner_info		oinfo;
	bool				unwritten;
};

#ifdef CONFIG_XFS_LIVE_HOOKS

struct xfs_rmap_hook {
	struct xfs_hook			rmap_hook;
};

void xfs_rmap_hook_disable(void);
void xfs_rmap_hook_enable(void);

int xfs_rmap_hook_add(struct xfs_group *xg, struct xfs_rmap_hook *hook);
void xfs_rmap_hook_del(struct xfs_group *xg, struct xfs_rmap_hook *hook);
void xfs_rmap_hook_setup(struct xfs_rmap_hook *hook, notifier_fn_t mod_fn);
#endif

#endif	/* __XFS_RMAP_H__ */
