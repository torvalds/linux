/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_STATS_H__
#define __XFS_STATS_H__


#include <linux/percpu.h>

/*
 * The btree stats arrays have fixed offsets for the different stats. We
 * store the base index in the btree cursor via XFS_STATS_CALC_INDEX() and
 * that allows us to use fixed offsets into the stats array for each btree
 * stat. These index offsets are defined in the order they will be emitted
 * in the stats files, so it is possible to add new btree stat types by
 * appending to the enum list below.
 */
enum {
	__XBTS_lookup = 0,
	__XBTS_compare = 1,
	__XBTS_insrec = 2,
	__XBTS_delrec = 3,
	__XBTS_newroot = 4,
	__XBTS_killroot = 5,
	__XBTS_increment = 6,
	__XBTS_decrement = 7,
	__XBTS_lshift = 8,
	__XBTS_rshift = 9,
	__XBTS_split = 10,
	__XBTS_join = 11,
	__XBTS_alloc = 12,
	__XBTS_free = 13,
	__XBTS_moves = 14,

	__XBTS_MAX = 15,
};

/*
 * XFS global statistics
 */
struct __xfsstats {
# define XFSSTAT_END_EXTENT_ALLOC	4
	__uint32_t		xs_allocx;
	__uint32_t		xs_allocb;
	__uint32_t		xs_freex;
	__uint32_t		xs_freeb;
# define XFSSTAT_END_ALLOC_BTREE	(XFSSTAT_END_EXTENT_ALLOC+4)
	__uint32_t		xs_abt_lookup;
	__uint32_t		xs_abt_compare;
	__uint32_t		xs_abt_insrec;
	__uint32_t		xs_abt_delrec;
# define XFSSTAT_END_BLOCK_MAPPING	(XFSSTAT_END_ALLOC_BTREE+7)
	__uint32_t		xs_blk_mapr;
	__uint32_t		xs_blk_mapw;
	__uint32_t		xs_blk_unmap;
	__uint32_t		xs_add_exlist;
	__uint32_t		xs_del_exlist;
	__uint32_t		xs_look_exlist;
	__uint32_t		xs_cmp_exlist;
# define XFSSTAT_END_BLOCK_MAP_BTREE	(XFSSTAT_END_BLOCK_MAPPING+4)
	__uint32_t		xs_bmbt_lookup;
	__uint32_t		xs_bmbt_compare;
	__uint32_t		xs_bmbt_insrec;
	__uint32_t		xs_bmbt_delrec;
# define XFSSTAT_END_DIRECTORY_OPS	(XFSSTAT_END_BLOCK_MAP_BTREE+4)
	__uint32_t		xs_dir_lookup;
	__uint32_t		xs_dir_create;
	__uint32_t		xs_dir_remove;
	__uint32_t		xs_dir_getdents;
# define XFSSTAT_END_TRANSACTIONS	(XFSSTAT_END_DIRECTORY_OPS+3)
	__uint32_t		xs_trans_sync;
	__uint32_t		xs_trans_async;
	__uint32_t		xs_trans_empty;
# define XFSSTAT_END_INODE_OPS		(XFSSTAT_END_TRANSACTIONS+7)
	__uint32_t		xs_ig_attempts;
	__uint32_t		xs_ig_found;
	__uint32_t		xs_ig_frecycle;
	__uint32_t		xs_ig_missed;
	__uint32_t		xs_ig_dup;
	__uint32_t		xs_ig_reclaims;
	__uint32_t		xs_ig_attrchg;
# define XFSSTAT_END_LOG_OPS		(XFSSTAT_END_INODE_OPS+5)
	__uint32_t		xs_log_writes;
	__uint32_t		xs_log_blocks;
	__uint32_t		xs_log_noiclogs;
	__uint32_t		xs_log_force;
	__uint32_t		xs_log_force_sleep;
# define XFSSTAT_END_TAIL_PUSHING	(XFSSTAT_END_LOG_OPS+10)
	__uint32_t		xs_try_logspace;
	__uint32_t		xs_sleep_logspace;
	__uint32_t		xs_push_ail;
	__uint32_t		xs_push_ail_success;
	__uint32_t		xs_push_ail_pushbuf;
	__uint32_t		xs_push_ail_pinned;
	__uint32_t		xs_push_ail_locked;
	__uint32_t		xs_push_ail_flushing;
	__uint32_t		xs_push_ail_restarts;
	__uint32_t		xs_push_ail_flush;
# define XFSSTAT_END_WRITE_CONVERT	(XFSSTAT_END_TAIL_PUSHING+2)
	__uint32_t		xs_xstrat_quick;
	__uint32_t		xs_xstrat_split;
# define XFSSTAT_END_READ_WRITE_OPS	(XFSSTAT_END_WRITE_CONVERT+2)
	__uint32_t		xs_write_calls;
	__uint32_t		xs_read_calls;
# define XFSSTAT_END_ATTRIBUTE_OPS	(XFSSTAT_END_READ_WRITE_OPS+4)
	__uint32_t		xs_attr_get;
	__uint32_t		xs_attr_set;
	__uint32_t		xs_attr_remove;
	__uint32_t		xs_attr_list;
# define XFSSTAT_END_INODE_CLUSTER	(XFSSTAT_END_ATTRIBUTE_OPS+3)
	__uint32_t		xs_iflush_count;
	__uint32_t		xs_icluster_flushcnt;
	__uint32_t		xs_icluster_flushinode;
# define XFSSTAT_END_VNODE_OPS		(XFSSTAT_END_INODE_CLUSTER+8)
	__uint32_t		vn_active;	/* # vnodes not on free lists */
	__uint32_t		vn_alloc;	/* # times vn_alloc called */
	__uint32_t		vn_get;		/* # times vn_get called */
	__uint32_t		vn_hold;	/* # times vn_hold called */
	__uint32_t		vn_rele;	/* # times vn_rele called */
	__uint32_t		vn_reclaim;	/* # times vn_reclaim called */
	__uint32_t		vn_remove;	/* # times vn_remove called */
	__uint32_t		vn_free;	/* # times vn_free called */
#define XFSSTAT_END_BUF			(XFSSTAT_END_VNODE_OPS+9)
	__uint32_t		xb_get;
	__uint32_t		xb_create;
	__uint32_t		xb_get_locked;
	__uint32_t		xb_get_locked_waited;
	__uint32_t		xb_busy_locked;
	__uint32_t		xb_miss_locked;
	__uint32_t		xb_page_retries;
	__uint32_t		xb_page_found;
	__uint32_t		xb_get_read;
/* Version 2 btree counters */
#define XFSSTAT_END_ABTB_V2		(XFSSTAT_END_BUF + __XBTS_MAX)
	__uint32_t		xs_abtb_2[__XBTS_MAX];
#define XFSSTAT_END_ABTC_V2		(XFSSTAT_END_ABTB_V2 + __XBTS_MAX)
	__uint32_t		xs_abtc_2[__XBTS_MAX];
#define XFSSTAT_END_BMBT_V2		(XFSSTAT_END_ABTC_V2 + __XBTS_MAX)
	__uint32_t		xs_bmbt_2[__XBTS_MAX];
#define XFSSTAT_END_IBT_V2		(XFSSTAT_END_BMBT_V2 + __XBTS_MAX)
	__uint32_t		xs_ibt_2[__XBTS_MAX];
#define XFSSTAT_END_FIBT_V2		(XFSSTAT_END_IBT_V2 + __XBTS_MAX)
	__uint32_t		xs_fibt_2[__XBTS_MAX];
#define XFSSTAT_END_RMAP_V2		(XFSSTAT_END_FIBT_V2 + __XBTS_MAX)
	__uint32_t		xs_rmap_2[__XBTS_MAX];
#define XFSSTAT_END_REFCOUNT		(XFSSTAT_END_RMAP_V2 + __XBTS_MAX)
	__uint32_t		xs_refcbt_2[__XBTS_MAX];
#define XFSSTAT_END_XQMSTAT		(XFSSTAT_END_REFCOUNT + 6)
	__uint32_t		xs_qm_dqreclaims;
	__uint32_t		xs_qm_dqreclaim_misses;
	__uint32_t		xs_qm_dquot_dups;
	__uint32_t		xs_qm_dqcachemisses;
	__uint32_t		xs_qm_dqcachehits;
	__uint32_t		xs_qm_dqwants;
#define XFSSTAT_END_QM			(XFSSTAT_END_XQMSTAT+2)
	__uint32_t		xs_qm_dquot;
	__uint32_t		xs_qm_dquot_unused;
/* Extra precision counters */
	__uint64_t		xs_xstrat_bytes;
	__uint64_t		xs_write_bytes;
	__uint64_t		xs_read_bytes;
};

struct xfsstats {
	union {
		struct __xfsstats	s;
		uint32_t		a[XFSSTAT_END_XQMSTAT];
	};
};

/*
 * simple wrapper for getting the array index of s struct member offset
 */
#define XFS_STATS_CALC_INDEX(member)	\
	(offsetof(struct __xfsstats, member) / (int)sizeof(__uint32_t))


int xfs_stats_format(struct xfsstats __percpu *stats, char *buf);
void xfs_stats_clearall(struct xfsstats __percpu *stats);
extern struct xstats xfsstats;

#define XFS_STATS_INC(mp, v)					\
do {								\
	per_cpu_ptr(xfsstats.xs_stats, current_cpu())->s.v++;	\
	per_cpu_ptr(mp->m_stats.xs_stats, current_cpu())->s.v++;	\
} while (0)

#define XFS_STATS_DEC(mp, v)					\
do {								\
	per_cpu_ptr(xfsstats.xs_stats, current_cpu())->s.v--;	\
	per_cpu_ptr(mp->m_stats.xs_stats, current_cpu())->s.v--;	\
} while (0)

#define XFS_STATS_ADD(mp, v, inc)					\
do {									\
	per_cpu_ptr(xfsstats.xs_stats, current_cpu())->s.v += (inc);	\
	per_cpu_ptr(mp->m_stats.xs_stats, current_cpu())->s.v += (inc);	\
} while (0)

#define XFS_STATS_INC_OFF(mp, off)				\
do {								\
	per_cpu_ptr(xfsstats.xs_stats, current_cpu())->a[off]++;	\
	per_cpu_ptr(mp->m_stats.xs_stats, current_cpu())->a[off]++;	\
} while (0)

#define XFS_STATS_DEC_OFF(mp, off)					\
do {								\
	per_cpu_ptr(xfsstats.xs_stats, current_cpu())->a[off];	\
	per_cpu_ptr(mp->m_stats.xs_stats, current_cpu())->a[off];	\
} while (0)

#define XFS_STATS_ADD_OFF(mp, off, inc)					\
do {									\
	per_cpu_ptr(xfsstats.xs_stats, current_cpu())->a[off] += (inc);	\
	per_cpu_ptr(mp->m_stats.xs_stats, current_cpu())->a[off] += (inc);	\
} while (0)

#if defined(CONFIG_PROC_FS)

extern int xfs_init_procfs(void);
extern void xfs_cleanup_procfs(void);


#else	/* !CONFIG_PROC_FS */

static inline int xfs_init_procfs(void)
{
	return 0;
}

static inline void xfs_cleanup_procfs(void)
{
}

#endif	/* !CONFIG_PROC_FS */

#endif /* __XFS_STATS_H__ */
