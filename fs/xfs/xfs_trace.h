// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009, Christoph Hellwig
 * All Rights Reserved.
 *
 * NOTE: none of these tracepoints shall be considered a stable kernel ABI
 * as they can change at any time.
 *
 * Current conventions for printing numbers measuring specific units:
 *
 * agno: allocation group number
 *
 * agino: per-AG inode number
 * ino: filesystem inode number
 *
 * agbno: per-AG block number in fs blocks
 * rgbno: per-rtgroup block number in fs blocks
 * startblock: physical block number for file mappings.  This is either a
 *             segmented fsblock for data device mappings, or a rfsblock
 *             for realtime device mappings
 * fsbcount: number of blocks in an extent, in fs blocks
 *
 * gbno: generic allocation group block number.  This is an agbno for
 *       space in a per-AG or a rgbno for space in a realtime group.
 *
 * daddr: physical block number in 512b blocks
 * bbcount: number of blocks in a physical extent, in 512b blocks
 *
 * rtx: physical rt extent number for extent mappings
 * rtxcount: number of rt extents in an extent mapping
 *
 * owner: reverse-mapping owner, usually inodes
 *
 * fileoff: file offset, in fs blocks
 * pos: file offset, in bytes
 * bytecount: number of bytes
 *
 * dablk: directory or xattr block offset, in filesystem blocks
 *
 * disize: ondisk file size, in bytes
 * isize: incore file size, in bytes
 *
 * forkoff: inode fork offset, in bytes
 *
 * ireccount: number of inode records
 *
 * Numbers describing space allocations (blocks, extents, inodes) should be
 * formatted in hexadecimal.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xfs

#if !defined(_TRACE_XFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XFS_H

#include <linux/tracepoint.h>

struct xfs_agf;
struct xfs_alloc_arg;
struct xfs_attr_list_context;
struct xfs_buf_log_item;
struct xfs_da_args;
struct xfs_da_node_entry;
struct xfs_dquot;
struct xfs_log_item;
struct xlog;
struct xlog_ticket;
struct xlog_recover;
struct xlog_recover_item;
struct xlog_rec_header;
struct xlog_in_core;
struct xfs_buf_log_format;
struct xfs_inode_log_format;
struct xfs_bmbt_irec;
struct xfs_btree_cur;
struct xfs_defer_op_type;
struct xfs_refcount_irec;
struct xfs_fsmap;
struct xfs_fsmap_irec;
struct xfs_group;
struct xfs_rmap_irec;
struct xfs_icreate_log;
struct xfs_iunlink_item;
struct xfs_owner_info;
struct xfs_trans_res;
struct xfs_inobt_rec_incore;
union xfs_btree_ptr;
struct xfs_dqtrx;
struct xfs_icwalk;
struct xfs_perag;
struct xfbtree;
struct xfs_btree_ops;
struct xfs_bmap_intent;
struct xfs_exchmaps_intent;
struct xfs_exchmaps_req;
struct xfs_exchrange;
struct xfs_getparents;
struct xfs_parent_irec;
struct xfs_attrlist_cursor_kern;
struct xfs_extent_free_item;
struct xfs_rmap_intent;
struct xfs_refcount_intent;
struct xfs_metadir_update;
struct xfs_rtgroup;
struct xfs_open_zone;

#define XFS_ATTR_FILTER_FLAGS \
	{ XFS_ATTR_ROOT,	"ROOT" }, \
	{ XFS_ATTR_SECURE,	"SECURE" }, \
	{ XFS_ATTR_INCOMPLETE,	"INCOMPLETE" }, \
	{ XFS_ATTR_PARENT,	"PARENT" }

DECLARE_EVENT_CLASS(xfs_attr_list_class,
	TP_PROTO(struct xfs_attr_list_context *ctx),
	TP_ARGS(ctx),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(u32, hashval)
		__field(u32, blkno)
		__field(u32, offset)
		__field(void *, buffer)
		__field(int, bufsize)
		__field(int, count)
		__field(int, firstu)
		__field(int, dupcnt)
		__field(unsigned int, attr_filter)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ctx->dp)->i_sb->s_dev;
		__entry->ino = ctx->dp->i_ino;
		__entry->hashval = ctx->cursor.hashval;
		__entry->blkno = ctx->cursor.blkno;
		__entry->offset = ctx->cursor.offset;
		__entry->buffer = ctx->buffer;
		__entry->bufsize = ctx->bufsize;
		__entry->count = ctx->count;
		__entry->firstu = ctx->firstu;
		__entry->attr_filter = ctx->attr_filter;
	),
	TP_printk("dev %d:%d ino 0x%llx cursor h/b/o 0x%x/0x%x/%u dupcnt %u "
		  "buffer %p size %u count %u firstu %u filter %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->ino,
		   __entry->hashval,
		   __entry->blkno,
		   __entry->offset,
		   __entry->dupcnt,
		   __entry->buffer,
		   __entry->bufsize,
		   __entry->count,
		   __entry->firstu,
		   __print_flags(__entry->attr_filter, "|",
				 XFS_ATTR_FILTER_FLAGS)
	)
)

#define DEFINE_ATTR_LIST_EVENT(name) \
DEFINE_EVENT(xfs_attr_list_class, name, \
	TP_PROTO(struct xfs_attr_list_context *ctx), \
	TP_ARGS(ctx))
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_sf);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_sf_all);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_leaf);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_leaf_end);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_full);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_add);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_wrong_blk);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_notfound);
DEFINE_ATTR_LIST_EVENT(xfs_attr_leaf_list);
DEFINE_ATTR_LIST_EVENT(xfs_attr_node_list);

TRACE_EVENT(xfs_calc_atomic_write_unit_max,
	TP_PROTO(struct xfs_mount *mp, unsigned int max_write,
		 unsigned int max_ioend, unsigned int max_agsize,
		 unsigned int max_rgsize),
	TP_ARGS(mp, max_write, max_ioend, max_agsize, max_rgsize),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, max_write)
		__field(unsigned int, max_ioend)
		__field(unsigned int, max_agsize)
		__field(unsigned int, max_rgsize)
		__field(unsigned int, data_awu_max)
		__field(unsigned int, rt_awu_max)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->max_write = max_write;
		__entry->max_ioend = max_ioend;
		__entry->max_agsize = max_agsize;
		__entry->max_rgsize = max_rgsize;
		__entry->data_awu_max = mp->m_groups[XG_TYPE_AG].awu_max;
		__entry->rt_awu_max = mp->m_groups[XG_TYPE_RTG].awu_max;
	),
	TP_printk("dev %d:%d max_write %u max_ioend %u max_agsize %u max_rgsize %u data_awu_max %u rt_awu_max %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->max_write,
		  __entry->max_ioend,
		  __entry->max_agsize,
		  __entry->max_rgsize,
		  __entry->data_awu_max,
		  __entry->rt_awu_max)
);

TRACE_EVENT(xfs_calc_max_atomic_write_fsblocks,
	TP_PROTO(struct xfs_mount *mp, unsigned int per_intent,
		 unsigned int step_size, unsigned int logres,
		 unsigned int blockcount),
	TP_ARGS(mp, per_intent, step_size, logres, blockcount),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, per_intent)
		__field(unsigned int, step_size)
		__field(unsigned int, logres)
		__field(unsigned int, blockcount)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->per_intent = per_intent;
		__entry->step_size = step_size;
		__entry->logres = logres;
		__entry->blockcount = blockcount;
	),
	TP_printk("dev %d:%d per_intent %u step_size %u logres %u blockcount %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->per_intent,
		  __entry->step_size,
		  __entry->logres,
		  __entry->blockcount)
);

TRACE_EVENT(xfs_calc_max_atomic_write_log_geometry,
	TP_PROTO(struct xfs_mount *mp, unsigned int per_intent,
		 unsigned int step_size, unsigned int blockcount,
		 unsigned int min_logblocks, unsigned int logres),
	TP_ARGS(mp, per_intent, step_size, blockcount, min_logblocks, logres),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, per_intent)
		__field(unsigned int, step_size)
		__field(unsigned int, blockcount)
		__field(unsigned int, min_logblocks)
		__field(unsigned int, cur_logblocks)
		__field(unsigned int, logres)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->per_intent = per_intent;
		__entry->step_size = step_size;
		__entry->blockcount = blockcount;
		__entry->min_logblocks = min_logblocks;
		__entry->cur_logblocks = mp->m_sb.sb_logblocks;
		__entry->logres = logres;
	),
	TP_printk("dev %d:%d per_intent %u step_size %u blockcount %u min_logblocks %u logblocks %u logres %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->per_intent,
		  __entry->step_size,
		  __entry->blockcount,
		  __entry->min_logblocks,
		  __entry->cur_logblocks,
		  __entry->logres)
);

TRACE_EVENT(xlog_intent_recovery_failed,
	TP_PROTO(struct xfs_mount *mp, const struct xfs_defer_op_type *ops,
		 int error),
	TP_ARGS(mp, ops, error),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, ops->name)
		__field(int, error)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__assign_str(name);
		__entry->error = error;
	),
	TP_printk("dev %d:%d optype %s error %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->error)
);

DECLARE_EVENT_CLASS(xfs_perag_class,
	TP_PROTO(const struct xfs_perag *pag, unsigned long caller_ip),
	TP_ARGS(pag, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, refcount)
		__field(int, active_refcount)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
		__entry->refcount = atomic_read(&pag->pag_group.xg_ref);
		__entry->active_refcount =
			atomic_read(&pag->pag_group.xg_active_ref);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno 0x%x passive refs %d active refs %d caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->refcount,
		  __entry->active_refcount,
		  (char *)__entry->caller_ip)
);

#define DEFINE_PERAG_REF_EVENT(name)	\
DEFINE_EVENT(xfs_perag_class, name,	\
	TP_PROTO(const struct xfs_perag *pag, unsigned long caller_ip), \
	TP_ARGS(pag, caller_ip))
DEFINE_PERAG_REF_EVENT(xfs_perag_set_inode_tag);
DEFINE_PERAG_REF_EVENT(xfs_perag_clear_inode_tag);
DEFINE_PERAG_REF_EVENT(xfs_reclaim_inodes_count);

TRACE_DEFINE_ENUM(XG_TYPE_AG);
TRACE_DEFINE_ENUM(XG_TYPE_RTG);

DECLARE_EVENT_CLASS(xfs_group_class,
	TP_PROTO(struct xfs_group *xg, unsigned long caller_ip),
	TP_ARGS(xg, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(int, refcount)
		__field(int, active_refcount)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = xg->xg_mount->m_super->s_dev;
		__entry->type = xg->xg_type;
		__entry->agno = xg->xg_gno;
		__entry->refcount = atomic_read(&xg->xg_ref);
		__entry->active_refcount = atomic_read(&xg->xg_active_ref);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d %sno 0x%x passive refs %d active refs %d caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->refcount,
		  __entry->active_refcount,
		  (char *)__entry->caller_ip)
);

#define DEFINE_GROUP_REF_EVENT(name)	\
DEFINE_EVENT(xfs_group_class, name,	\
	TP_PROTO(struct xfs_group *xg, unsigned long caller_ip), \
	TP_ARGS(xg, caller_ip))
DEFINE_GROUP_REF_EVENT(xfs_group_get);
DEFINE_GROUP_REF_EVENT(xfs_group_hold);
DEFINE_GROUP_REF_EVENT(xfs_group_put);
DEFINE_GROUP_REF_EVENT(xfs_group_grab);
DEFINE_GROUP_REF_EVENT(xfs_group_grab_next_tag);
DEFINE_GROUP_REF_EVENT(xfs_group_rele);

#ifdef CONFIG_XFS_RT
DECLARE_EVENT_CLASS(xfs_zone_class,
	TP_PROTO(struct xfs_rtgroup *rtg),
	TP_ARGS(rtg),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_rgnumber_t, rgno)
		__field(xfs_rgblock_t, used)
		__field(unsigned int, nr_open)
	),
	TP_fast_assign(
		struct xfs_mount	*mp = rtg_mount(rtg);

		__entry->dev = mp->m_super->s_dev;
		__entry->rgno = rtg_rgno(rtg);
		__entry->used = rtg_rmap(rtg)->i_used_blocks;
		__entry->nr_open = mp->m_zone_info->zi_nr_open_zones;
	),
	TP_printk("dev %d:%d rgno 0x%x used 0x%x nr_open %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rgno,
		  __entry->used,
		  __entry->nr_open)
);

#define DEFINE_ZONE_EVENT(name)				\
DEFINE_EVENT(xfs_zone_class, name,			\
	TP_PROTO(struct xfs_rtgroup *rtg),		\
	TP_ARGS(rtg))
DEFINE_ZONE_EVENT(xfs_zone_emptied);
DEFINE_ZONE_EVENT(xfs_zone_full);
DEFINE_ZONE_EVENT(xfs_zone_opened);
DEFINE_ZONE_EVENT(xfs_zone_reset);
DEFINE_ZONE_EVENT(xfs_zone_gc_target_opened);

TRACE_EVENT(xfs_zone_free_blocks,
	TP_PROTO(struct xfs_rtgroup *rtg, xfs_rgblock_t rgbno,
		 xfs_extlen_t len),
	TP_ARGS(rtg, rgbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_rgnumber_t, rgno)
		__field(xfs_rgblock_t, used)
		__field(xfs_rgblock_t, rgbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = rtg_mount(rtg)->m_super->s_dev;
		__entry->rgno = rtg_rgno(rtg);
		__entry->used = rtg_rmap(rtg)->i_used_blocks;
		__entry->rgbno = rgbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d rgno 0x%x used 0x%x rgbno 0x%x len 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rgno,
		  __entry->used,
		  __entry->rgbno,
		  __entry->len)
);

DECLARE_EVENT_CLASS(xfs_zone_alloc_class,
	TP_PROTO(struct xfs_open_zone *oz, xfs_rgblock_t rgbno,
		 xfs_extlen_t len),
	TP_ARGS(oz, rgbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_rgnumber_t, rgno)
		__field(xfs_rgblock_t, used)
		__field(xfs_rgblock_t, written)
		__field(xfs_rgblock_t, write_pointer)
		__field(xfs_rgblock_t, rgbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = rtg_mount(oz->oz_rtg)->m_super->s_dev;
		__entry->rgno = rtg_rgno(oz->oz_rtg);
		__entry->used = rtg_rmap(oz->oz_rtg)->i_used_blocks;
		__entry->written = oz->oz_written;
		__entry->write_pointer = oz->oz_write_pointer;
		__entry->rgbno = rgbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d rgno 0x%x used 0x%x written 0x%x wp 0x%x rgbno 0x%x len 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rgno,
		  __entry->used,
		  __entry->written,
		  __entry->write_pointer,
		  __entry->rgbno,
		  __entry->len)
);

#define DEFINE_ZONE_ALLOC_EVENT(name)				\
DEFINE_EVENT(xfs_zone_alloc_class, name,			\
	TP_PROTO(struct xfs_open_zone *oz, xfs_rgblock_t rgbno,	\
		 xfs_extlen_t len),				\
	TP_ARGS(oz, rgbno, len))
DEFINE_ZONE_ALLOC_EVENT(xfs_zone_record_blocks);
DEFINE_ZONE_ALLOC_EVENT(xfs_zone_alloc_blocks);

TRACE_EVENT(xfs_zone_gc_select_victim,
	TP_PROTO(struct xfs_rtgroup *rtg, unsigned int bucket),
	TP_ARGS(rtg, bucket),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_rgnumber_t, rgno)
		__field(xfs_rgblock_t, used)
		__field(unsigned int, bucket)
	),
	TP_fast_assign(
		__entry->dev = rtg_mount(rtg)->m_super->s_dev;
		__entry->rgno = rtg_rgno(rtg);
		__entry->used = rtg_rmap(rtg)->i_used_blocks;
		__entry->bucket = bucket;
	),
	TP_printk("dev %d:%d rgno 0x%x used 0x%x bucket %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rgno,
		  __entry->used,
		  __entry->bucket)
);

TRACE_EVENT(xfs_zones_mount,
	TP_PROTO(struct xfs_mount *mp),
	TP_ARGS(mp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_rgnumber_t, rgcount)
		__field(uint32_t, blocks)
		__field(unsigned int, max_open_zones)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->rgcount = mp->m_sb.sb_rgcount;
		__entry->blocks = mp->m_groups[XG_TYPE_RTG].blocks;
		__entry->max_open_zones = mp->m_max_open_zones;
	),
	TP_printk("dev %d:%d zoned %u blocks_per_zone %u, max_open %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		__entry->rgcount,
		__entry->blocks,
		__entry->max_open_zones)
);
#endif /* CONFIG_XFS_RT */

TRACE_EVENT(xfs_inodegc_worker,
	TP_PROTO(struct xfs_mount *mp, unsigned int shrinker_hits),
	TP_ARGS(mp, shrinker_hits),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, shrinker_hits)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->shrinker_hits = shrinker_hits;
	),
	TP_printk("dev %d:%d shrinker_hits %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->shrinker_hits)
);

DECLARE_EVENT_CLASS(xfs_fs_class,
	TP_PROTO(struct xfs_mount *mp, void *caller_ip),
	TP_ARGS(mp, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long long, mflags)
		__field(unsigned long, opstate)
		__field(unsigned long, sbflags)
		__field(void *, caller_ip)
	),
	TP_fast_assign(
		if (mp) {
			__entry->dev = mp->m_super->s_dev;
			__entry->mflags = mp->m_features;
			__entry->opstate = mp->m_opstate;
			__entry->sbflags = mp->m_super->s_flags;
		}
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d m_features 0x%llx opstate (%s) s_flags 0x%lx caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->mflags,
		  __print_flags(__entry->opstate, "|", XFS_OPSTATE_STRINGS),
		  __entry->sbflags,
		  __entry->caller_ip)
);

#define DEFINE_FS_EVENT(name)	\
DEFINE_EVENT(xfs_fs_class, name,					\
	TP_PROTO(struct xfs_mount *mp, void *caller_ip), \
	TP_ARGS(mp, caller_ip))
DEFINE_FS_EVENT(xfs_inodegc_flush);
DEFINE_FS_EVENT(xfs_inodegc_push);
DEFINE_FS_EVENT(xfs_inodegc_start);
DEFINE_FS_EVENT(xfs_inodegc_stop);
DEFINE_FS_EVENT(xfs_inodegc_queue);
DEFINE_FS_EVENT(xfs_inodegc_throttle);
DEFINE_FS_EVENT(xfs_fs_sync_fs);
DEFINE_FS_EVENT(xfs_blockgc_start);
DEFINE_FS_EVENT(xfs_blockgc_stop);
DEFINE_FS_EVENT(xfs_blockgc_worker);
DEFINE_FS_EVENT(xfs_blockgc_flush_all);

TRACE_EVENT(xfs_inodegc_shrinker_scan,
	TP_PROTO(struct xfs_mount *mp, struct shrink_control *sc,
		 void *caller_ip),
	TP_ARGS(mp, sc, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, nr_to_scan)
		__field(void *, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->nr_to_scan = sc->nr_to_scan;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d nr_to_scan %lu caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->nr_to_scan,
		  __entry->caller_ip)
);

DECLARE_EVENT_CLASS(xfs_ag_class,
	TP_PROTO(const struct xfs_perag *pag),
	TP_ARGS(pag),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
	),
	TP_printk("dev %d:%d agno 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno)
);
#define DEFINE_AG_EVENT(name)	\
DEFINE_EVENT(xfs_ag_class, name,	\
	TP_PROTO(const struct xfs_perag *pag),	\
	TP_ARGS(pag))

DEFINE_AG_EVENT(xfs_read_agf);
DEFINE_AG_EVENT(xfs_alloc_read_agf);
DEFINE_AG_EVENT(xfs_read_agi);
DEFINE_AG_EVENT(xfs_ialloc_read_agi);

TRACE_EVENT(xfs_attr_list_node_descend,
	TP_PROTO(struct xfs_attr_list_context *ctx,
		 struct xfs_da_node_entry *btree),
	TP_ARGS(ctx, btree),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(u32, hashval)
		__field(u32, blkno)
		__field(u32, offset)
		__field(void *, buffer)
		__field(int, bufsize)
		__field(int, count)
		__field(int, firstu)
		__field(int, dupcnt)
		__field(unsigned int, attr_filter)
		__field(u32, bt_hashval)
		__field(u32, bt_before)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ctx->dp)->i_sb->s_dev;
		__entry->ino = ctx->dp->i_ino;
		__entry->hashval = ctx->cursor.hashval;
		__entry->blkno = ctx->cursor.blkno;
		__entry->offset = ctx->cursor.offset;
		__entry->buffer = ctx->buffer;
		__entry->bufsize = ctx->bufsize;
		__entry->count = ctx->count;
		__entry->firstu = ctx->firstu;
		__entry->attr_filter = ctx->attr_filter;
		__entry->bt_hashval = be32_to_cpu(btree->hashval);
		__entry->bt_before = be32_to_cpu(btree->before);
	),
	TP_printk("dev %d:%d ino 0x%llx cursor h/b/o 0x%x/0x%x/%u dupcnt %u "
		  "buffer %p size %u count %u firstu %u filter %s "
		  "node hashval %u, node before %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->ino,
		   __entry->hashval,
		   __entry->blkno,
		   __entry->offset,
		   __entry->dupcnt,
		   __entry->buffer,
		   __entry->bufsize,
		   __entry->count,
		   __entry->firstu,
		   __print_flags(__entry->attr_filter, "|",
				 XFS_ATTR_FILTER_FLAGS),
		   __entry->bt_hashval,
		   __entry->bt_before)
);

DECLARE_EVENT_CLASS(xfs_bmap_class,
	TP_PROTO(struct xfs_inode *ip, struct xfs_iext_cursor *cur, int state,
		 unsigned long caller_ip),
	TP_ARGS(ip, cur, state, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(void *, leaf)
		__field(int, pos)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
		__field(xfs_exntst_t, state)
		__field(int, bmap_state)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		struct xfs_ifork	*ifp;
		struct xfs_bmbt_irec	r;

		ifp = xfs_iext_state_to_fork(ip, state);
		xfs_iext_get_extent(ifp, cur, &r);
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->leaf = cur->leaf;
		__entry->pos = cur->pos;
		__entry->startoff = r.br_startoff;
		__entry->startblock = r.br_startblock;
		__entry->blockcount = r.br_blockcount;
		__entry->state = r.br_state;
		__entry->bmap_state = state;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx state %s cur %p/%d "
		  "fileoff 0x%llx startblock 0x%llx fsbcount 0x%llx flag %d caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->bmap_state, "|", XFS_BMAP_EXT_FLAGS),
		  __entry->leaf,
		  __entry->pos,
		  __entry->startoff,
		  (int64_t)__entry->startblock,
		  __entry->blockcount,
		  __entry->state,
		  (char *)__entry->caller_ip)
)

#define DEFINE_BMAP_EVENT(name) \
DEFINE_EVENT(xfs_bmap_class, name, \
	TP_PROTO(struct xfs_inode *ip, struct xfs_iext_cursor *cur, int state, \
		 unsigned long caller_ip), \
	TP_ARGS(ip, cur, state, caller_ip))
DEFINE_BMAP_EVENT(xfs_iext_insert);
DEFINE_BMAP_EVENT(xfs_iext_remove);
DEFINE_BMAP_EVENT(xfs_bmap_pre_update);
DEFINE_BMAP_EVENT(xfs_bmap_post_update);
DEFINE_BMAP_EVENT(xfs_read_extent);
DEFINE_BMAP_EVENT(xfs_write_extent);

DECLARE_EVENT_CLASS(xfs_buf_class,
	TP_PROTO(struct xfs_buf *bp, unsigned long caller_ip),
	TP_ARGS(bp, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(int, nblks)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(unsigned, flags)
		__field(unsigned long, caller_ip)
		__field(const void *, buf_ops)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = xfs_buf_daddr(bp);
		__entry->nblks = bp->b_length;
		__entry->hold = bp->b_hold;
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = bp->b_sema.count;
		__entry->flags = bp->b_flags;
		__entry->caller_ip = caller_ip;
		__entry->buf_ops = bp->b_ops;
	),
	TP_printk("dev %d:%d daddr 0x%llx bbcount 0x%x hold %d pincount %d "
		  "lock %d flags %s bufops %pS caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->nblks,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  __entry->buf_ops,
		  (void *)__entry->caller_ip)
)

#define DEFINE_BUF_EVENT(name) \
DEFINE_EVENT(xfs_buf_class, name, \
	TP_PROTO(struct xfs_buf *bp, unsigned long caller_ip), \
	TP_ARGS(bp, caller_ip))
DEFINE_BUF_EVENT(xfs_buf_init);
DEFINE_BUF_EVENT(xfs_buf_free);
DEFINE_BUF_EVENT(xfs_buf_hold);
DEFINE_BUF_EVENT(xfs_buf_rele);
DEFINE_BUF_EVENT(xfs_buf_iodone);
DEFINE_BUF_EVENT(xfs_buf_submit);
DEFINE_BUF_EVENT(xfs_buf_lock);
DEFINE_BUF_EVENT(xfs_buf_lock_done);
DEFINE_BUF_EVENT(xfs_buf_trylock_fail);
DEFINE_BUF_EVENT(xfs_buf_trylock);
DEFINE_BUF_EVENT(xfs_buf_unlock);
DEFINE_BUF_EVENT(xfs_buf_iowait);
DEFINE_BUF_EVENT(xfs_buf_iowait_done);
DEFINE_BUF_EVENT(xfs_buf_delwri_queue);
DEFINE_BUF_EVENT(xfs_buf_delwri_queued);
DEFINE_BUF_EVENT(xfs_buf_delwri_split);
DEFINE_BUF_EVENT(xfs_buf_delwri_pushbuf);
DEFINE_BUF_EVENT(xfs_buf_get_uncached);
DEFINE_BUF_EVENT(xfs_buf_item_relse);
DEFINE_BUF_EVENT(xfs_buf_iodone_async);
DEFINE_BUF_EVENT(xfs_buf_error_relse);
DEFINE_BUF_EVENT(xfs_buf_drain_buftarg);
DEFINE_BUF_EVENT(xfs_trans_read_buf_shut);
DEFINE_BUF_EVENT(xfs_buf_backing_folio);
DEFINE_BUF_EVENT(xfs_buf_backing_kmem);
DEFINE_BUF_EVENT(xfs_buf_backing_vmalloc);
DEFINE_BUF_EVENT(xfs_buf_backing_fallback);

/* not really buffer traces, but the buf provides useful information */
DEFINE_BUF_EVENT(xfs_btree_corrupt);
DEFINE_BUF_EVENT(xfs_reset_dqcounts);

/* pass flags explicitly */
DECLARE_EVENT_CLASS(xfs_buf_flags_class,
	TP_PROTO(struct xfs_buf *bp, unsigned flags, unsigned long caller_ip),
	TP_ARGS(bp, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(unsigned int, length)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(unsigned, flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = xfs_buf_daddr(bp);
		__entry->length = bp->b_length;
		__entry->flags = flags;
		__entry->hold = bp->b_hold;
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = bp->b_sema.count;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d daddr 0x%llx bbcount 0x%x hold %d pincount %d "
		  "lock %d flags %s caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->length,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  (void *)__entry->caller_ip)
)

#define DEFINE_BUF_FLAGS_EVENT(name) \
DEFINE_EVENT(xfs_buf_flags_class, name, \
	TP_PROTO(struct xfs_buf *bp, unsigned flags, unsigned long caller_ip), \
	TP_ARGS(bp, flags, caller_ip))
DEFINE_BUF_FLAGS_EVENT(xfs_buf_find);
DEFINE_BUF_FLAGS_EVENT(xfs_buf_get);
DEFINE_BUF_FLAGS_EVENT(xfs_buf_read);
DEFINE_BUF_FLAGS_EVENT(xfs_buf_readahead);

TRACE_EVENT(xfs_buf_ioerror,
	TP_PROTO(struct xfs_buf *bp, int error, xfs_failaddr_t caller_ip),
	TP_ARGS(bp, error, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(unsigned int, length)
		__field(unsigned, flags)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(int, error)
		__field(xfs_failaddr_t, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = xfs_buf_daddr(bp);
		__entry->length = bp->b_length;
		__entry->hold = bp->b_hold;
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = bp->b_sema.count;
		__entry->error = error;
		__entry->flags = bp->b_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d daddr 0x%llx bbcount 0x%x hold %d pincount %d "
		  "lock %d error %d flags %s caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->length,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __entry->error,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  (void *)__entry->caller_ip)
);

DECLARE_EVENT_CLASS(xfs_buf_item_class,
	TP_PROTO(struct xfs_buf_log_item *bip),
	TP_ARGS(bip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, buf_bno)
		__field(unsigned int, buf_len)
		__field(int, buf_hold)
		__field(int, buf_pincount)
		__field(int, buf_lockval)
		__field(unsigned, buf_flags)
		__field(unsigned, bli_recur)
		__field(int, bli_refcount)
		__field(unsigned, bli_flags)
		__field(unsigned long, li_flags)
	),
	TP_fast_assign(
		__entry->dev = bip->bli_buf->b_target->bt_dev;
		__entry->bli_flags = bip->bli_flags;
		__entry->bli_recur = bip->bli_recur;
		__entry->bli_refcount = atomic_read(&bip->bli_refcount);
		__entry->buf_bno = xfs_buf_daddr(bip->bli_buf);
		__entry->buf_len = bip->bli_buf->b_length;
		__entry->buf_flags = bip->bli_buf->b_flags;
		__entry->buf_hold = bip->bli_buf->b_hold;
		__entry->buf_pincount = atomic_read(&bip->bli_buf->b_pin_count);
		__entry->buf_lockval = bip->bli_buf->b_sema.count;
		__entry->li_flags = bip->bli_item.li_flags;
	),
	TP_printk("dev %d:%d daddr 0x%llx bbcount 0x%x hold %d pincount %d "
		  "lock %d flags %s recur %d refcount %d bliflags %s "
		  "liflags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->buf_bno,
		  __entry->buf_len,
		  __entry->buf_hold,
		  __entry->buf_pincount,
		  __entry->buf_lockval,
		  __print_flags(__entry->buf_flags, "|", XFS_BUF_FLAGS),
		  __entry->bli_recur,
		  __entry->bli_refcount,
		  __print_flags(__entry->bli_flags, "|", XFS_BLI_FLAGS),
		  __print_flags(__entry->li_flags, "|", XFS_LI_FLAGS))
)

#define DEFINE_BUF_ITEM_EVENT(name) \
DEFINE_EVENT(xfs_buf_item_class, name, \
	TP_PROTO(struct xfs_buf_log_item *bip), \
	TP_ARGS(bip))
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_size);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_size_ordered);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_size_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_format);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_format_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_ordered);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_pin);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unpin);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unpin_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_release);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_committed);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_push);
DEFINE_BUF_ITEM_EVENT(xfs_trans_get_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_get_buf_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_getsb);
DEFINE_BUF_ITEM_EVENT(xfs_trans_getsb_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_read_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_read_buf_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_log_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_brelse);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bdetach);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bjoin);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bhold);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bhold_release);
DEFINE_BUF_ITEM_EVENT(xfs_trans_binval);

DECLARE_EVENT_CLASS(xfs_filestream_class,
	TP_PROTO(const struct xfs_perag *pag, xfs_ino_t ino),
	TP_ARGS(pag, ino),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_agnumber_t, agno)
		__field(int, streams)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->ino = ino;
		__entry->agno = pag_agno(pag);
		__entry->streams = atomic_read(&pag->pagf_fstrms);
	),
	TP_printk("dev %d:%d ino 0x%llx agno 0x%x streams %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->agno,
		  __entry->streams)
)
#define DEFINE_FILESTREAM_EVENT(name) \
DEFINE_EVENT(xfs_filestream_class, name, \
	TP_PROTO(const struct xfs_perag *pag, xfs_ino_t ino), \
	TP_ARGS(pag, ino))
DEFINE_FILESTREAM_EVENT(xfs_filestream_free);
DEFINE_FILESTREAM_EVENT(xfs_filestream_lookup);
DEFINE_FILESTREAM_EVENT(xfs_filestream_scan);

TRACE_EVENT(xfs_filestream_pick,
	TP_PROTO(const struct xfs_perag *pag, xfs_ino_t ino),
	TP_ARGS(pag, ino),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_agnumber_t, agno)
		__field(int, streams)
		__field(xfs_extlen_t, free)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->ino = ino;
		__entry->agno = pag_agno(pag);
		__entry->streams = atomic_read(&pag->pagf_fstrms);
		__entry->free = pag->pagf_freeblks;
	),
	TP_printk("dev %d:%d ino 0x%llx agno 0x%x streams %d free %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->agno,
		  __entry->streams,
		  __entry->free)
);

DECLARE_EVENT_CLASS(xfs_lock_class,
	TP_PROTO(struct xfs_inode *ip, unsigned lock_flags,
		 unsigned long caller_ip),
	TP_ARGS(ip,  lock_flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, lock_flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->lock_flags = lock_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx flags %s caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->lock_flags, "|", XFS_LOCK_FLAGS),
		  (void *)__entry->caller_ip)
)

#define DEFINE_LOCK_EVENT(name) \
DEFINE_EVENT(xfs_lock_class, name, \
	TP_PROTO(struct xfs_inode *ip, unsigned lock_flags, \
		 unsigned long caller_ip), \
	TP_ARGS(ip,  lock_flags, caller_ip))
DEFINE_LOCK_EVENT(xfs_ilock);
DEFINE_LOCK_EVENT(xfs_ilock_nowait);
DEFINE_LOCK_EVENT(xfs_ilock_demote);
DEFINE_LOCK_EVENT(xfs_iunlock);

DECLARE_EVENT_CLASS(xfs_inode_class,
	TP_PROTO(struct xfs_inode *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned long, iflags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->iflags = ip->i_flags;
	),
	TP_printk("dev %d:%d ino 0x%llx iflags 0x%lx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->iflags)
)

#define DEFINE_INODE_EVENT(name) \
DEFINE_EVENT(xfs_inode_class, name, \
	TP_PROTO(struct xfs_inode *ip), \
	TP_ARGS(ip))
DEFINE_INODE_EVENT(xfs_iget_skip);
DEFINE_INODE_EVENT(xfs_iget_recycle);
DEFINE_INODE_EVENT(xfs_iget_recycle_fail);
DEFINE_INODE_EVENT(xfs_iget_hit);
DEFINE_INODE_EVENT(xfs_iget_miss);

DEFINE_INODE_EVENT(xfs_getattr);
DEFINE_INODE_EVENT(xfs_setattr);
DEFINE_INODE_EVENT(xfs_readlink);
DEFINE_INODE_EVENT(xfs_inactive_symlink);
DEFINE_INODE_EVENT(xfs_alloc_file_space);
DEFINE_INODE_EVENT(xfs_free_file_space);
DEFINE_INODE_EVENT(xfs_zero_file_space);
DEFINE_INODE_EVENT(xfs_collapse_file_space);
DEFINE_INODE_EVENT(xfs_insert_file_space);
DEFINE_INODE_EVENT(xfs_readdir);
#ifdef CONFIG_XFS_POSIX_ACL
DEFINE_INODE_EVENT(xfs_get_acl);
#endif
DEFINE_INODE_EVENT(xfs_vm_bmap);
DEFINE_INODE_EVENT(xfs_file_ioctl);
DEFINE_INODE_EVENT(xfs_file_compat_ioctl);
DEFINE_INODE_EVENT(xfs_ioctl_setattr);
DEFINE_INODE_EVENT(xfs_dir_fsync);
DEFINE_INODE_EVENT(xfs_file_fsync);
DEFINE_INODE_EVENT(xfs_destroy_inode);
DEFINE_INODE_EVENT(xfs_update_time);

DEFINE_INODE_EVENT(xfs_dquot_dqalloc);
DEFINE_INODE_EVENT(xfs_dquot_dqdetach);

DEFINE_INODE_EVENT(xfs_inode_set_eofblocks_tag);
DEFINE_INODE_EVENT(xfs_inode_clear_eofblocks_tag);
DEFINE_INODE_EVENT(xfs_inode_free_eofblocks_invalid);
DEFINE_INODE_EVENT(xfs_inode_set_cowblocks_tag);
DEFINE_INODE_EVENT(xfs_inode_clear_cowblocks_tag);
DEFINE_INODE_EVENT(xfs_inode_free_cowblocks_invalid);
DEFINE_INODE_EVENT(xfs_inode_set_reclaimable);
DEFINE_INODE_EVENT(xfs_inode_reclaiming);
DEFINE_INODE_EVENT(xfs_inode_set_need_inactive);
DEFINE_INODE_EVENT(xfs_inode_inactivating);

/*
 * ftrace's __print_symbolic requires that all enum values be wrapped in the
 * TRACE_DEFINE_ENUM macro so that the enum value can be encoded in the ftrace
 * ring buffer.  Somehow this was only worth mentioning in the ftrace sample
 * code.
 */
TRACE_DEFINE_ENUM(XFS_REFC_DOMAIN_SHARED);
TRACE_DEFINE_ENUM(XFS_REFC_DOMAIN_COW);

DECLARE_EVENT_CLASS(xfs_fault_class,
	TP_PROTO(struct xfs_inode *ip, unsigned int order),
	TP_ARGS(ip, order),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned int, order)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->order = order;
	),
	TP_printk("dev %d:%d ino 0x%llx order %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->order)
)

#define DEFINE_FAULT_EVENT(name) \
DEFINE_EVENT(xfs_fault_class, name, \
	TP_PROTO(struct xfs_inode *ip, unsigned int order), \
	TP_ARGS(ip, order))
DEFINE_FAULT_EVENT(xfs_read_fault);
DEFINE_FAULT_EVENT(xfs_write_fault);

DECLARE_EVENT_CLASS(xfs_iref_class,
	TP_PROTO(struct xfs_inode *ip, unsigned long caller_ip),
	TP_ARGS(ip, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, count)
		__field(int, pincount)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->count = atomic_read(&VFS_I(ip)->i_count);
		__entry->pincount = atomic_read(&ip->i_pincount);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx count %d pincount %d caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->count,
		  __entry->pincount,
		  (char *)__entry->caller_ip)
)

TRACE_EVENT(xfs_iomap_prealloc_size,
	TP_PROTO(struct xfs_inode *ip, xfs_fsblock_t blocks, int shift,
		 unsigned int writeio_blocks),
	TP_ARGS(ip, blocks, shift, writeio_blocks),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsblock_t, blocks)
		__field(int, shift)
		__field(unsigned int, writeio_blocks)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->blocks = blocks;
		__entry->shift = shift;
		__entry->writeio_blocks = writeio_blocks;
	),
	TP_printk("dev %d:%d ino 0x%llx prealloc blocks %llu shift %d "
		  "m_allocsize_blocks %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		  __entry->blocks, __entry->shift, __entry->writeio_blocks)
)

TRACE_EVENT(xfs_irec_merge_pre,
	TP_PROTO(const struct xfs_perag *pag,
		 const struct xfs_inobt_rec_incore *rec,
		 const struct xfs_inobt_rec_incore *nrec),
	TP_ARGS(pag, rec, nrec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
		__field(uint16_t, holemask)
		__field(xfs_agino_t, nagino)
		__field(uint16_t, nholemask)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
		__entry->agino = rec->ir_startino;
		__entry->holemask = rec->ir_holemask;
		__entry->nagino = nrec->ir_startino;
		__entry->nholemask = nrec->ir_holemask;
	),
	TP_printk("dev %d:%d agno 0x%x agino 0x%x holemask 0x%x new_agino 0x%x new_holemask 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agino,
		  __entry->holemask,
		  __entry->nagino,
		  __entry->nholemask)
)

TRACE_EVENT(xfs_irec_merge_post,
	TP_PROTO(const struct xfs_perag *pag,
		 const struct xfs_inobt_rec_incore *nrec),
	TP_ARGS(pag, nrec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
		__field(uint16_t, holemask)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
		__entry->agino = nrec->ir_startino;
		__entry->holemask = nrec->ir_holemask;
	),
	TP_printk("dev %d:%d agno 0x%x agino 0x%x holemask 0x%x",
		  MAJOR(__entry->dev),
		  MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agino,
		  __entry->holemask)
)

#define DEFINE_IREF_EVENT(name) \
DEFINE_EVENT(xfs_iref_class, name, \
	TP_PROTO(struct xfs_inode *ip, unsigned long caller_ip), \
	TP_ARGS(ip, caller_ip))
DEFINE_IREF_EVENT(xfs_irele);
DEFINE_IREF_EVENT(xfs_inode_pin);
DEFINE_IREF_EVENT(xfs_inode_unpin);
DEFINE_IREF_EVENT(xfs_inode_unpin_nowait);

DECLARE_EVENT_CLASS(xfs_namespace_class,
	TP_PROTO(struct xfs_inode *dp, const struct xfs_name *name),
	TP_ARGS(dp, name),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, dp_ino)
		__field(int, namelen)
		__dynamic_array(char, name, name->len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(dp)->i_sb->s_dev;
		__entry->dp_ino = dp->i_ino;
		__entry->namelen = name->len;
		memcpy(__get_str(name), name->name, name->len);
	),
	TP_printk("dev %d:%d dp ino 0x%llx name %.*s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dp_ino,
		  __entry->namelen,
		  __get_str(name))
)

#define DEFINE_NAMESPACE_EVENT(name) \
DEFINE_EVENT(xfs_namespace_class, name, \
	TP_PROTO(struct xfs_inode *dp, const struct xfs_name *name), \
	TP_ARGS(dp, name))
DEFINE_NAMESPACE_EVENT(xfs_remove);
DEFINE_NAMESPACE_EVENT(xfs_link);
DEFINE_NAMESPACE_EVENT(xfs_lookup);
DEFINE_NAMESPACE_EVENT(xfs_create);
DEFINE_NAMESPACE_EVENT(xfs_symlink);

TRACE_EVENT(xfs_rename,
	TP_PROTO(struct xfs_inode *src_dp, struct xfs_inode *target_dp,
		 struct xfs_name *src_name, struct xfs_name *target_name),
	TP_ARGS(src_dp, target_dp, src_name, target_name),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, src_dp_ino)
		__field(xfs_ino_t, target_dp_ino)
		__field(int, src_namelen)
		__field(int, target_namelen)
		__dynamic_array(char, src_name, src_name->len)
		__dynamic_array(char, target_name, target_name->len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(src_dp)->i_sb->s_dev;
		__entry->src_dp_ino = src_dp->i_ino;
		__entry->target_dp_ino = target_dp->i_ino;
		__entry->src_namelen = src_name->len;
		__entry->target_namelen = target_name->len;
		memcpy(__get_str(src_name), src_name->name, src_name->len);
		memcpy(__get_str(target_name), target_name->name,
			target_name->len);
	),
	TP_printk("dev %d:%d src dp ino 0x%llx target dp ino 0x%llx"
		  " src name %.*s target name %.*s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->src_dp_ino,
		  __entry->target_dp_ino,
		  __entry->src_namelen,
		  __get_str(src_name),
		  __entry->target_namelen,
		  __get_str(target_name))
)

DECLARE_EVENT_CLASS(xfs_dquot_class,
	TP_PROTO(struct xfs_dquot *dqp),
	TP_ARGS(dqp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u32, id)
		__field(xfs_dqtype_t, type)
		__field(unsigned, flags)
		__field(unsigned, nrefs)
		__field(unsigned long long, res_bcount)
		__field(unsigned long long, res_rtbcount)
		__field(unsigned long long, res_icount)

		__field(unsigned long long, bcount)
		__field(unsigned long long, rtbcount)
		__field(unsigned long long, icount)

		__field(unsigned long long, blk_hardlimit)
		__field(unsigned long long, blk_softlimit)
		__field(unsigned long long, rtb_hardlimit)
		__field(unsigned long long, rtb_softlimit)
		__field(unsigned long long, ino_hardlimit)
		__field(unsigned long long, ino_softlimit)
	),
	TP_fast_assign(
		__entry->dev = dqp->q_mount->m_super->s_dev;
		__entry->id = dqp->q_id;
		__entry->type = dqp->q_type;
		__entry->flags = dqp->q_flags;
		__entry->nrefs = dqp->q_nrefs;

		__entry->res_bcount = dqp->q_blk.reserved;
		__entry->res_rtbcount = dqp->q_rtb.reserved;
		__entry->res_icount = dqp->q_ino.reserved;

		__entry->bcount = dqp->q_blk.count;
		__entry->rtbcount = dqp->q_rtb.count;
		__entry->icount = dqp->q_ino.count;

		__entry->blk_hardlimit = dqp->q_blk.hardlimit;
		__entry->blk_softlimit = dqp->q_blk.softlimit;
		__entry->rtb_hardlimit = dqp->q_rtb.hardlimit;
		__entry->rtb_softlimit = dqp->q_rtb.softlimit;
		__entry->ino_hardlimit = dqp->q_ino.hardlimit;
		__entry->ino_softlimit = dqp->q_ino.softlimit;
	),
	TP_printk("dev %d:%d id 0x%x type %s flags %s nrefs %u "
		  "res_bc 0x%llx res_rtbc 0x%llx res_ic 0x%llx "
		  "bcnt 0x%llx bhardlimit 0x%llx bsoftlimit 0x%llx "
		  "rtbcnt 0x%llx rtbhardlimit 0x%llx rtbsoftlimit 0x%llx "
		  "icnt 0x%llx ihardlimit 0x%llx isoftlimit 0x%llx]",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->id,
		  __print_flags(__entry->type, "|", XFS_DQTYPE_STRINGS),
		  __print_flags(__entry->flags, "|", XFS_DQFLAG_STRINGS),
		  __entry->nrefs,
		  __entry->res_bcount,
		  __entry->res_rtbcount,
		  __entry->res_icount,
		  __entry->bcount,
		  __entry->blk_hardlimit,
		  __entry->blk_softlimit,
		  __entry->rtbcount,
		  __entry->rtb_hardlimit,
		  __entry->rtb_softlimit,
		  __entry->icount,
		  __entry->ino_hardlimit,
		  __entry->ino_softlimit)
)

#define DEFINE_DQUOT_EVENT(name) \
DEFINE_EVENT(xfs_dquot_class, name, \
	TP_PROTO(struct xfs_dquot *dqp), \
	TP_ARGS(dqp))
DEFINE_DQUOT_EVENT(xfs_dqadjust);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_want);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_dirty);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_busy);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_done);
DEFINE_DQUOT_EVENT(xfs_dqattach_found);
DEFINE_DQUOT_EVENT(xfs_dqattach_get);
DEFINE_DQUOT_EVENT(xfs_dqalloc);
DEFINE_DQUOT_EVENT(xfs_dqtobp_read);
DEFINE_DQUOT_EVENT(xfs_dqread);
DEFINE_DQUOT_EVENT(xfs_dqread_fail);
DEFINE_DQUOT_EVENT(xfs_dqget_hit);
DEFINE_DQUOT_EVENT(xfs_dqget_miss);
DEFINE_DQUOT_EVENT(xfs_dqget_freeing);
DEFINE_DQUOT_EVENT(xfs_dqget_dup);
DEFINE_DQUOT_EVENT(xfs_dqput);
DEFINE_DQUOT_EVENT(xfs_dqput_free);
DEFINE_DQUOT_EVENT(xfs_dqrele);
DEFINE_DQUOT_EVENT(xfs_dqflush);
DEFINE_DQUOT_EVENT(xfs_dqflush_force);
DEFINE_DQUOT_EVENT(xfs_dqflush_done);
DEFINE_DQUOT_EVENT(xfs_trans_apply_dquot_deltas_before);
DEFINE_DQUOT_EVENT(xfs_trans_apply_dquot_deltas_after);

TRACE_EVENT(xfs_trans_mod_dquot,
	TP_PROTO(struct xfs_trans *tp, struct xfs_dquot *dqp,
		 unsigned int field, int64_t delta),
	TP_ARGS(tp, dqp, field, delta),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_dqtype_t, type)
		__field(unsigned int, flags)
		__field(unsigned int, dqid)
		__field(unsigned int, field)
		__field(int64_t, delta)
	),
	TP_fast_assign(
		__entry->dev = tp->t_mountp->m_super->s_dev;
		__entry->type = dqp->q_type;
		__entry->flags = dqp->q_flags;
		__entry->dqid = dqp->q_id;
		__entry->field = field;
		__entry->delta = delta;
	),
	TP_printk("dev %d:%d dquot id 0x%x type %s flags %s field %s delta %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dqid,
		  __print_flags(__entry->type, "|", XFS_DQTYPE_STRINGS),
		  __print_flags(__entry->flags, "|", XFS_DQFLAG_STRINGS),
		  __print_flags(__entry->field, "|", XFS_QMOPT_FLAGS),
		  __entry->delta)
);

DECLARE_EVENT_CLASS(xfs_dqtrx_class,
	TP_PROTO(struct xfs_dqtrx *qtrx),
	TP_ARGS(qtrx),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_dqtype_t, type)
		__field(unsigned int, flags)
		__field(u32, dqid)

		__field(uint64_t, blk_res)
		__field(int64_t,  bcount_delta)
		__field(int64_t,  delbcnt_delta)

		__field(uint64_t, rtblk_res)
		__field(uint64_t, rtblk_res_used)
		__field(int64_t,  rtbcount_delta)
		__field(int64_t,  delrtb_delta)

		__field(uint64_t, ino_res)
		__field(uint64_t, ino_res_used)
		__field(int64_t,  icount_delta)
	),
	TP_fast_assign(
		__entry->dev = qtrx->qt_dquot->q_mount->m_super->s_dev;
		__entry->type = qtrx->qt_dquot->q_type;
		__entry->flags = qtrx->qt_dquot->q_flags;
		__entry->dqid = qtrx->qt_dquot->q_id;

		__entry->blk_res = qtrx->qt_blk_res;
		__entry->bcount_delta = qtrx->qt_bcount_delta;
		__entry->delbcnt_delta = qtrx->qt_delbcnt_delta;

		__entry->rtblk_res = qtrx->qt_rtblk_res;
		__entry->rtblk_res_used = qtrx->qt_rtblk_res_used;
		__entry->rtbcount_delta = qtrx->qt_rtbcount_delta;
		__entry->delrtb_delta = qtrx->qt_delrtb_delta;

		__entry->ino_res = qtrx->qt_ino_res;
		__entry->ino_res_used = qtrx->qt_ino_res_used;
		__entry->icount_delta = qtrx->qt_icount_delta;
	),
	TP_printk("dev %d:%d dquot id 0x%x type %s flags %s "
		  "blk_res %llu bcount_delta %lld delbcnt_delta %lld "
		  "rtblk_res %llu rtblk_res_used %llu rtbcount_delta %lld delrtb_delta %lld "
		  "ino_res %llu ino_res_used %llu icount_delta %lld",
		MAJOR(__entry->dev), MINOR(__entry->dev),
		__entry->dqid,
		  __print_flags(__entry->type, "|", XFS_DQTYPE_STRINGS),
		  __print_flags(__entry->flags, "|", XFS_DQFLAG_STRINGS),

		__entry->blk_res,
		__entry->bcount_delta,
		__entry->delbcnt_delta,

		__entry->rtblk_res,
		__entry->rtblk_res_used,
		__entry->rtbcount_delta,
		__entry->delrtb_delta,

		__entry->ino_res,
		__entry->ino_res_used,
		__entry->icount_delta)
)

#define DEFINE_DQTRX_EVENT(name) \
DEFINE_EVENT(xfs_dqtrx_class, name, \
	TP_PROTO(struct xfs_dqtrx *qtrx), \
	TP_ARGS(qtrx))
DEFINE_DQTRX_EVENT(xfs_trans_apply_dquot_deltas);
DEFINE_DQTRX_EVENT(xfs_trans_mod_dquot_before);
DEFINE_DQTRX_EVENT(xfs_trans_mod_dquot_after);

DECLARE_EVENT_CLASS(xfs_loggrant_class,
	TP_PROTO(struct xlog *log, struct xlog_ticket *tic),
	TP_ARGS(log, tic),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, tic)
		__field(char, ocnt)
		__field(char, cnt)
		__field(int, curr_res)
		__field(int, unit_res)
		__field(unsigned int, flags)
		__field(int, reserveq)
		__field(int, writeq)
		__field(uint64_t, grant_reserve_bytes)
		__field(uint64_t, grant_write_bytes)
		__field(uint64_t, tail_space)
		__field(int, curr_cycle)
		__field(int, curr_block)
		__field(xfs_lsn_t, tail_lsn)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->tic = (unsigned long)tic;
		__entry->ocnt = tic->t_ocnt;
		__entry->cnt = tic->t_cnt;
		__entry->curr_res = tic->t_curr_res;
		__entry->unit_res = tic->t_unit_res;
		__entry->flags = tic->t_flags;
		__entry->reserveq = list_empty(&log->l_reserve_head.waiters);
		__entry->writeq = list_empty(&log->l_write_head.waiters);
		__entry->tail_space = READ_ONCE(log->l_tail_space);
		__entry->grant_reserve_bytes = __entry->tail_space +
			atomic64_read(&log->l_reserve_head.grant);
		__entry->grant_write_bytes = __entry->tail_space +
			atomic64_read(&log->l_write_head.grant);
		__entry->curr_cycle = log->l_curr_cycle;
		__entry->curr_block = log->l_curr_block;
		__entry->tail_lsn = atomic64_read(&log->l_tail_lsn);
	),
	TP_printk("dev %d:%d tic 0x%lx t_ocnt %u t_cnt %u t_curr_res %u "
		  "t_unit_res %u t_flags %s reserveq %s writeq %s "
		  "tail space %llu grant_reserve_bytes %llu "
		  "grant_write_bytes %llu curr_cycle %d curr_block %d "
		  "tail_cycle %d tail_block %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tic,
		  __entry->ocnt,
		  __entry->cnt,
		  __entry->curr_res,
		  __entry->unit_res,
		  __print_flags(__entry->flags, "|", XLOG_TIC_FLAGS),
		  __entry->reserveq ? "empty" : "active",
		  __entry->writeq ? "empty" : "active",
		  __entry->tail_space,
		  __entry->grant_reserve_bytes,
		  __entry->grant_write_bytes,
		  __entry->curr_cycle,
		  __entry->curr_block,
		  CYCLE_LSN(__entry->tail_lsn),
		  BLOCK_LSN(__entry->tail_lsn)
	)
)

#define DEFINE_LOGGRANT_EVENT(name) \
DEFINE_EVENT(xfs_loggrant_class, name, \
	TP_PROTO(struct xlog *log, struct xlog_ticket *tic), \
	TP_ARGS(log, tic))
DEFINE_LOGGRANT_EVENT(xfs_log_umount_write);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_sleep);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_wake);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_wake_up);
DEFINE_LOGGRANT_EVENT(xfs_log_reserve);
DEFINE_LOGGRANT_EVENT(xfs_log_reserve_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_ticket_regrant);
DEFINE_LOGGRANT_EVENT(xfs_log_ticket_regrant_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_ticket_regrant_sub);
DEFINE_LOGGRANT_EVENT(xfs_log_ticket_ungrant);
DEFINE_LOGGRANT_EVENT(xfs_log_ticket_ungrant_sub);
DEFINE_LOGGRANT_EVENT(xfs_log_ticket_ungrant_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_cil_wait);
DEFINE_LOGGRANT_EVENT(xfs_log_cil_return);

DECLARE_EVENT_CLASS(xfs_log_item_class,
	TP_PROTO(struct xfs_log_item *lip),
	TP_ARGS(lip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(void *, lip)
		__field(uint, type)
		__field(unsigned long, flags)
		__field(xfs_lsn_t, lsn)
	),
	TP_fast_assign(
		__entry->dev = lip->li_log->l_mp->m_super->s_dev;
		__entry->lip = lip;
		__entry->type = lip->li_type;
		__entry->flags = lip->li_flags;
		__entry->lsn = lip->li_lsn;
	),
	TP_printk("dev %d:%d lip %p lsn %d/%d type %s flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lip,
		  CYCLE_LSN(__entry->lsn), BLOCK_LSN(__entry->lsn),
		  __print_symbolic(__entry->type, XFS_LI_TYPE_DESC),
		  __print_flags(__entry->flags, "|", XFS_LI_FLAGS))
)

TRACE_EVENT(xfs_log_force,
	TP_PROTO(struct xfs_mount *mp, xfs_lsn_t lsn, unsigned long caller_ip),
	TP_ARGS(mp, lsn, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_lsn_t, lsn)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->lsn = lsn;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d lsn 0x%llx caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lsn, (void *)__entry->caller_ip)
)

#define DEFINE_LOG_ITEM_EVENT(name) \
DEFINE_EVENT(xfs_log_item_class, name, \
	TP_PROTO(struct xfs_log_item *lip), \
	TP_ARGS(lip))
DEFINE_LOG_ITEM_EVENT(xfs_ail_push);
DEFINE_LOG_ITEM_EVENT(xfs_ail_pinned);
DEFINE_LOG_ITEM_EVENT(xfs_ail_locked);
DEFINE_LOG_ITEM_EVENT(xfs_ail_flushing);
DEFINE_LOG_ITEM_EVENT(xfs_cil_whiteout_mark);
DEFINE_LOG_ITEM_EVENT(xfs_cil_whiteout_skip);
DEFINE_LOG_ITEM_EVENT(xfs_cil_whiteout_unpin);

DECLARE_EVENT_CLASS(xfs_ail_class,
	TP_PROTO(struct xfs_log_item *lip, xfs_lsn_t old_lsn, xfs_lsn_t new_lsn),
	TP_ARGS(lip, old_lsn, new_lsn),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(void *, lip)
		__field(uint, type)
		__field(unsigned long, flags)
		__field(xfs_lsn_t, old_lsn)
		__field(xfs_lsn_t, new_lsn)
	),
	TP_fast_assign(
		__entry->dev = lip->li_log->l_mp->m_super->s_dev;
		__entry->lip = lip;
		__entry->type = lip->li_type;
		__entry->flags = lip->li_flags;
		__entry->old_lsn = old_lsn;
		__entry->new_lsn = new_lsn;
	),
	TP_printk("dev %d:%d lip %p old lsn %d/%d new lsn %d/%d type %s flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lip,
		  CYCLE_LSN(__entry->old_lsn), BLOCK_LSN(__entry->old_lsn),
		  CYCLE_LSN(__entry->new_lsn), BLOCK_LSN(__entry->new_lsn),
		  __print_symbolic(__entry->type, XFS_LI_TYPE_DESC),
		  __print_flags(__entry->flags, "|", XFS_LI_FLAGS))
)

#define DEFINE_AIL_EVENT(name) \
DEFINE_EVENT(xfs_ail_class, name, \
	TP_PROTO(struct xfs_log_item *lip, xfs_lsn_t old_lsn, xfs_lsn_t new_lsn), \
	TP_ARGS(lip, old_lsn, new_lsn))
DEFINE_AIL_EVENT(xfs_ail_insert);
DEFINE_AIL_EVENT(xfs_ail_move);
DEFINE_AIL_EVENT(xfs_ail_delete);

TRACE_EVENT(xfs_log_assign_tail_lsn,
	TP_PROTO(struct xlog *log, xfs_lsn_t new_lsn),
	TP_ARGS(log, new_lsn),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_lsn_t, new_lsn)
		__field(xfs_lsn_t, old_lsn)
		__field(xfs_lsn_t, head_lsn)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->new_lsn = new_lsn;
		__entry->old_lsn = atomic64_read(&log->l_tail_lsn);
		__entry->head_lsn = log->l_ailp->ail_head_lsn;
	),
	TP_printk("dev %d:%d new tail lsn %d/%d, old lsn %d/%d, head lsn %d/%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  CYCLE_LSN(__entry->new_lsn), BLOCK_LSN(__entry->new_lsn),
		  CYCLE_LSN(__entry->old_lsn), BLOCK_LSN(__entry->old_lsn),
		  CYCLE_LSN(__entry->head_lsn), BLOCK_LSN(__entry->head_lsn))
)

DECLARE_EVENT_CLASS(xfs_file_class,
	TP_PROTO(struct kiocb *iocb, struct iov_iter *iter),
	TP_ARGS(iocb, iter),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(loff_t, offset)
		__field(size_t, count)
	),
	TP_fast_assign(
		__entry->dev = file_inode(iocb->ki_filp)->i_sb->s_dev;
		__entry->ino = XFS_I(file_inode(iocb->ki_filp))->i_ino;
		__entry->size = XFS_I(file_inode(iocb->ki_filp))->i_disk_size;
		__entry->offset = iocb->ki_pos;
		__entry->count = iov_iter_count(iter);
	),
	TP_printk("dev %d:%d ino 0x%llx disize 0x%llx pos 0x%llx bytecount 0x%zx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->offset,
		  __entry->count)
)

#define DEFINE_RW_EVENT(name)		\
DEFINE_EVENT(xfs_file_class, name,	\
	TP_PROTO(struct kiocb *iocb, struct iov_iter *iter),		\
	TP_ARGS(iocb, iter))
DEFINE_RW_EVENT(xfs_file_buffered_read);
DEFINE_RW_EVENT(xfs_file_direct_read);
DEFINE_RW_EVENT(xfs_file_dax_read);
DEFINE_RW_EVENT(xfs_file_buffered_write);
DEFINE_RW_EVENT(xfs_file_direct_write);
DEFINE_RW_EVENT(xfs_file_dax_write);
DEFINE_RW_EVENT(xfs_reflink_bounce_dio_write);

TRACE_EVENT(xfs_iomap_atomic_write_cow,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count),
	TP_ARGS(ip, offset, count),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_off_t, offset)
		__field(ssize_t, count)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->offset = offset;
		__entry->count = count;
	),
	TP_printk("dev %d:%d ino 0x%llx pos 0x%llx bytecount 0x%zx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->offset,
		  __entry->count)
)

DECLARE_EVENT_CLASS(xfs_imap_class,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count,
		 int whichfork, struct xfs_bmbt_irec *irec),
	TP_ARGS(ip, offset, count, whichfork, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(loff_t, size)
		__field(loff_t, offset)
		__field(size_t, count)
		__field(int, whichfork)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_disk_size;
		__entry->offset = offset;
		__entry->count = count;
		__entry->whichfork = whichfork;
		__entry->startoff = irec ? irec->br_startoff : 0;
		__entry->startblock = irec ? irec->br_startblock : 0;
		__entry->blockcount = irec ? irec->br_blockcount : 0;
	),
	TP_printk("dev %d:%d ino 0x%llx disize 0x%llx pos 0x%llx bytecount 0x%zx "
		  "fork %s startoff 0x%llx startblock 0x%llx fsbcount 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->offset,
		  __entry->count,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __entry->startoff,
		  (int64_t)__entry->startblock,
		  __entry->blockcount)
)

#define DEFINE_IMAP_EVENT(name)	\
DEFINE_EVENT(xfs_imap_class, name,	\
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count,	\
		 int whichfork, struct xfs_bmbt_irec *irec),		\
	TP_ARGS(ip, offset, count, whichfork, irec))
DEFINE_IMAP_EVENT(xfs_map_blocks_found);
DEFINE_IMAP_EVENT(xfs_map_blocks_alloc);
DEFINE_IMAP_EVENT(xfs_iomap_alloc);
DEFINE_IMAP_EVENT(xfs_iomap_found);

DECLARE_EVENT_CLASS(xfs_simple_io_class,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count),
	TP_ARGS(ip, offset, count),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(loff_t, isize)
		__field(loff_t, disize)
		__field(loff_t, offset)
		__field(size_t, count)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->isize = VFS_I(ip)->i_size;
		__entry->disize = ip->i_disk_size;
		__entry->offset = offset;
		__entry->count = count;
	),
	TP_printk("dev %d:%d ino 0x%llx isize 0x%llx disize 0x%llx "
		  "pos 0x%llx bytecount 0x%zx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->isize,
		  __entry->disize,
		  __entry->offset,
		  __entry->count)
);

#define DEFINE_SIMPLE_IO_EVENT(name)	\
DEFINE_EVENT(xfs_simple_io_class, name,	\
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count),	\
	TP_ARGS(ip, offset, count))
DEFINE_SIMPLE_IO_EVENT(xfs_delalloc_enospc);
DEFINE_SIMPLE_IO_EVENT(xfs_unwritten_convert);
DEFINE_SIMPLE_IO_EVENT(xfs_setfilesize);
DEFINE_SIMPLE_IO_EVENT(xfs_zero_eof);
DEFINE_SIMPLE_IO_EVENT(xfs_end_io_direct_write);
DEFINE_SIMPLE_IO_EVENT(xfs_end_io_direct_write_unwritten);
DEFINE_SIMPLE_IO_EVENT(xfs_end_io_direct_write_append);
DEFINE_SIMPLE_IO_EVENT(xfs_file_splice_read);
DEFINE_SIMPLE_IO_EVENT(xfs_zoned_map_blocks);

DECLARE_EVENT_CLASS(xfs_itrunc_class,
	TP_PROTO(struct xfs_inode *ip, xfs_fsize_t new_size),
	TP_ARGS(ip, new_size),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_fsize_t, new_size)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_disk_size;
		__entry->new_size = new_size;
	),
	TP_printk("dev %d:%d ino 0x%llx disize 0x%llx new_size 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->new_size)
)

#define DEFINE_ITRUNC_EVENT(name) \
DEFINE_EVENT(xfs_itrunc_class, name, \
	TP_PROTO(struct xfs_inode *ip, xfs_fsize_t new_size), \
	TP_ARGS(ip, new_size))
DEFINE_ITRUNC_EVENT(xfs_itruncate_extents_start);
DEFINE_ITRUNC_EVENT(xfs_itruncate_extents_end);

TRACE_EVENT(xfs_pagecache_inval,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t start, xfs_off_t finish),
	TP_ARGS(ip, start, finish),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_off_t, start)
		__field(xfs_off_t, finish)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_disk_size;
		__entry->start = start;
		__entry->finish = finish;
	),
	TP_printk("dev %d:%d ino 0x%llx disize 0x%llx start 0x%llx finish 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->start,
		  __entry->finish)
);

TRACE_EVENT(xfs_bunmap,
	TP_PROTO(struct xfs_inode *ip, xfs_fileoff_t fileoff, xfs_filblks_t len,
		 int flags, unsigned long caller_ip),
	TP_ARGS(ip, fileoff, len, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_fileoff_t, fileoff)
		__field(xfs_filblks_t, len)
		__field(unsigned long, caller_ip)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_disk_size;
		__entry->fileoff = fileoff;
		__entry->len = len;
		__entry->caller_ip = caller_ip;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d ino 0x%llx disize 0x%llx fileoff 0x%llx fsbcount 0x%llx "
		  "flags %s caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->fileoff,
		  __entry->len,
		  __print_flags(__entry->flags, "|", XFS_BMAPI_FLAGS),
		  (void *)__entry->caller_ip)

);

DECLARE_EVENT_CLASS(xfs_extent_busy_class,
	TP_PROTO(const struct xfs_group *xg, xfs_agblock_t agbno,
		 xfs_extlen_t len),
	TP_ARGS(xg, agbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = xg->xg_mount->m_super->s_dev;
		__entry->type = xg->xg_type;
		__entry->agno = xg->xg_gno;
		__entry->agbno = agbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d %sno 0x%x %sbno 0x%x fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agbno,
		  __entry->len)
);
#define DEFINE_BUSY_EVENT(name) \
DEFINE_EVENT(xfs_extent_busy_class, name, \
	TP_PROTO(const struct xfs_group *xg, xfs_agblock_t agbno, \
		 xfs_extlen_t len), \
	TP_ARGS(xg, agbno, len))
DEFINE_BUSY_EVENT(xfs_extent_busy);
DEFINE_BUSY_EVENT(xfs_extent_busy_force);
DEFINE_BUSY_EVENT(xfs_extent_busy_reuse);
DEFINE_BUSY_EVENT(xfs_extent_busy_clear);

TRACE_EVENT(xfs_extent_busy_trim,
	TP_PROTO(const struct xfs_group *xg, xfs_agblock_t agbno,
		 xfs_extlen_t len, xfs_agblock_t tbno, xfs_extlen_t tlen),
	TP_ARGS(xg, agbno, len, tbno, tlen),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(xfs_agblock_t, tbno)
		__field(xfs_extlen_t, tlen)
	),
	TP_fast_assign(
		__entry->dev = xg->xg_mount->m_super->s_dev;
		__entry->type = xg->xg_type;
		__entry->agno = xg->xg_gno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->tbno = tbno;
		__entry->tlen = tlen;
	),
	TP_printk("dev %d:%d %sno 0x%x %sbno 0x%x fsbcount 0x%x found_agbno 0x%x found_fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agbno,
		  __entry->len,
		  __entry->tbno,
		  __entry->tlen)
);

#ifdef CONFIG_XFS_RT
TRACE_EVENT(xfs_rtalloc_extent_busy,
	TP_PROTO(struct xfs_rtgroup *rtg, xfs_rtxnum_t start,
		 xfs_rtxlen_t minlen, xfs_rtxlen_t maxlen,
		 xfs_rtxlen_t len, xfs_rtxlen_t prod, xfs_rtxnum_t rtx,
		 unsigned busy_gen),
	TP_ARGS(rtg, start, minlen, maxlen, len, prod, rtx, busy_gen),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_rgnumber_t, rgno)
		__field(xfs_rtxnum_t, start)
		__field(xfs_rtxlen_t, minlen)
		__field(xfs_rtxlen_t, maxlen)
		__field(xfs_rtxlen_t, mod)
		__field(xfs_rtxlen_t, prod)
		__field(xfs_rtxlen_t, len)
		__field(xfs_rtxnum_t, rtx)
		__field(unsigned, busy_gen)
	),
	TP_fast_assign(
		__entry->dev = rtg_mount(rtg)->m_super->s_dev;
		__entry->rgno = rtg_rgno(rtg);
		__entry->start = start;
		__entry->minlen = minlen;
		__entry->maxlen = maxlen;
		__entry->prod = prod;
		__entry->len = len;
		__entry->rtx = rtx;
		__entry->busy_gen = busy_gen;
	),
	TP_printk("dev %d:%d rgno 0x%x startrtx 0x%llx minlen %u maxlen %u "
		  "prod %u len %u rtx 0%llx busy_gen 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rgno,
		  __entry->start,
		  __entry->minlen,
		  __entry->maxlen,
		  __entry->prod,
		  __entry->len,
		  __entry->rtx,
		  __entry->busy_gen)
)

TRACE_EVENT(xfs_rtalloc_extent_busy_trim,
	TP_PROTO(struct xfs_rtgroup *rtg, xfs_rtxnum_t old_rtx,
		 xfs_rtxlen_t old_len, xfs_rtxnum_t new_rtx,
		 xfs_rtxlen_t new_len),
	TP_ARGS(rtg, old_rtx, old_len, new_rtx, new_len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_rgnumber_t, rgno)
		__field(xfs_rtxnum_t, old_rtx)
		__field(xfs_rtxnum_t, new_rtx)
		__field(xfs_rtxlen_t, old_len)
		__field(xfs_rtxlen_t, new_len)
	),
	TP_fast_assign(
		__entry->dev = rtg_mount(rtg)->m_super->s_dev;
		__entry->rgno = rtg_rgno(rtg);
		__entry->old_rtx = old_rtx;
		__entry->old_len = old_len;
		__entry->new_rtx = new_rtx;
		__entry->new_len = new_len;
	),
	TP_printk("dev %d:%d rgno 0x%x rtx 0x%llx rtxcount 0x%x -> rtx 0x%llx rtxcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rgno,
		  __entry->old_rtx,
		  __entry->old_len,
		  __entry->new_rtx,
		  __entry->new_len)
);
#endif /* CONFIG_XFS_RT */

DECLARE_EVENT_CLASS(xfs_agf_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_agf *agf, int flags,
		 unsigned long caller_ip),
	TP_ARGS(mp, agf, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, flags)
		__field(__u32, length)
		__field(__u32, bno_root)
		__field(__u32, cnt_root)
		__field(__u32, bno_level)
		__field(__u32, cnt_level)
		__field(__u32, flfirst)
		__field(__u32, fllast)
		__field(__u32, flcount)
		__field(__u32, freeblks)
		__field(__u32, longest)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = be32_to_cpu(agf->agf_seqno),
		__entry->flags = flags;
		__entry->length = be32_to_cpu(agf->agf_length),
		__entry->bno_root = be32_to_cpu(agf->agf_bno_root),
		__entry->cnt_root = be32_to_cpu(agf->agf_cnt_root),
		__entry->bno_level = be32_to_cpu(agf->agf_bno_level),
		__entry->cnt_level = be32_to_cpu(agf->agf_cnt_level),
		__entry->flfirst = be32_to_cpu(agf->agf_flfirst),
		__entry->fllast = be32_to_cpu(agf->agf_fllast),
		__entry->flcount = be32_to_cpu(agf->agf_flcount),
		__entry->freeblks = be32_to_cpu(agf->agf_freeblks),
		__entry->longest = be32_to_cpu(agf->agf_longest);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno 0x%x flags %s length %u roots b %u c %u "
		  "levels b %u c %u flfirst %u fllast %u flcount %u "
		  "freeblks %u longest %u caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __print_flags(__entry->flags, "|", XFS_AGF_FLAGS),
		  __entry->length,
		  __entry->bno_root,
		  __entry->cnt_root,
		  __entry->bno_level,
		  __entry->cnt_level,
		  __entry->flfirst,
		  __entry->fllast,
		  __entry->flcount,
		  __entry->freeblks,
		  __entry->longest,
		  (void *)__entry->caller_ip)
);
#define DEFINE_AGF_EVENT(name) \
DEFINE_EVENT(xfs_agf_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_agf *agf, int flags, \
		 unsigned long caller_ip), \
	TP_ARGS(mp, agf, flags, caller_ip))
DEFINE_AGF_EVENT(xfs_agf);
DEFINE_AGF_EVENT(xfs_agfl_reset);

TRACE_EVENT(xfs_free_extent,
	TP_PROTO(const struct xfs_perag *pag, xfs_agblock_t agbno,
		 xfs_extlen_t len, enum xfs_ag_resv_type resv, int haveleft,
		 int haveright),
	TP_ARGS(pag, agbno, len, resv, haveleft, haveright),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(int, resv)
		__field(int, haveleft)
		__field(int, haveright)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->resv = resv;
		__entry->haveleft = haveleft;
		__entry->haveright = haveright;
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x resv %d %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->resv,
		  __entry->haveleft ?
			(__entry->haveright ? "both" : "left") :
			(__entry->haveright ? "right" : "none"))

);

DECLARE_EVENT_CLASS(xfs_alloc_class,
	TP_PROTO(struct xfs_alloc_arg *args),
	TP_ARGS(args),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, minlen)
		__field(xfs_extlen_t, maxlen)
		__field(xfs_extlen_t, mod)
		__field(xfs_extlen_t, prod)
		__field(xfs_extlen_t, minleft)
		__field(xfs_extlen_t, total)
		__field(xfs_extlen_t, alignment)
		__field(xfs_extlen_t, minalignslop)
		__field(xfs_extlen_t, len)
		__field(char, wasdel)
		__field(char, wasfromfl)
		__field(int, resv)
		__field(int, datatype)
		__field(xfs_agnumber_t, highest_agno)
	),
	TP_fast_assign(
		__entry->dev = args->mp->m_super->s_dev;
		__entry->agno = args->agno;
		__entry->agbno = args->agbno;
		__entry->minlen = args->minlen;
		__entry->maxlen = args->maxlen;
		__entry->mod = args->mod;
		__entry->prod = args->prod;
		__entry->minleft = args->minleft;
		__entry->total = args->total;
		__entry->alignment = args->alignment;
		__entry->minalignslop = args->minalignslop;
		__entry->len = args->len;
		__entry->wasdel = args->wasdel;
		__entry->wasfromfl = args->wasfromfl;
		__entry->resv = args->resv;
		__entry->datatype = args->datatype;
		__entry->highest_agno = args->tp->t_highest_agno;
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x minlen %u maxlen %u mod %u "
		  "prod %u minleft %u total %u alignment %u minalignslop %u "
		  "len %u wasdel %d wasfromfl %d resv %d "
		  "datatype 0x%x highest_agno 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->minlen,
		  __entry->maxlen,
		  __entry->mod,
		  __entry->prod,
		  __entry->minleft,
		  __entry->total,
		  __entry->alignment,
		  __entry->minalignslop,
		  __entry->len,
		  __entry->wasdel,
		  __entry->wasfromfl,
		  __entry->resv,
		  __entry->datatype,
		  __entry->highest_agno)
)

#define DEFINE_ALLOC_EVENT(name) \
DEFINE_EVENT(xfs_alloc_class, name, \
	TP_PROTO(struct xfs_alloc_arg *args), \
	TP_ARGS(args))
DEFINE_ALLOC_EVENT(xfs_alloc_exact_done);
DEFINE_ALLOC_EVENT(xfs_alloc_exact_notfound);
DEFINE_ALLOC_EVENT(xfs_alloc_exact_error);
DEFINE_ALLOC_EVENT(xfs_alloc_near_nominleft);
DEFINE_ALLOC_EVENT(xfs_alloc_near_first);
DEFINE_ALLOC_EVENT(xfs_alloc_cur);
DEFINE_ALLOC_EVENT(xfs_alloc_cur_right);
DEFINE_ALLOC_EVENT(xfs_alloc_cur_left);
DEFINE_ALLOC_EVENT(xfs_alloc_cur_lookup);
DEFINE_ALLOC_EVENT(xfs_alloc_cur_lookup_done);
DEFINE_ALLOC_EVENT(xfs_alloc_near_error);
DEFINE_ALLOC_EVENT(xfs_alloc_near_noentry);
DEFINE_ALLOC_EVENT(xfs_alloc_near_busy);
DEFINE_ALLOC_EVENT(xfs_alloc_size_neither);
DEFINE_ALLOC_EVENT(xfs_alloc_size_noentry);
DEFINE_ALLOC_EVENT(xfs_alloc_size_nominleft);
DEFINE_ALLOC_EVENT(xfs_alloc_size_done);
DEFINE_ALLOC_EVENT(xfs_alloc_size_error);
DEFINE_ALLOC_EVENT(xfs_alloc_size_busy);
DEFINE_ALLOC_EVENT(xfs_alloc_small_freelist);
DEFINE_ALLOC_EVENT(xfs_alloc_small_notenough);
DEFINE_ALLOC_EVENT(xfs_alloc_small_done);
DEFINE_ALLOC_EVENT(xfs_alloc_small_error);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_badargs);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_skip_deadlock);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_nofix);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_noagbp);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_loopfailed);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_allfailed);

DEFINE_ALLOC_EVENT(xfs_alloc_vextent_this_ag);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_start_ag);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_first_ag);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_exact_bno);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_near_bno);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_finish);

TRACE_EVENT(xfs_alloc_cur_check,
	TP_PROTO(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		 xfs_extlen_t len, xfs_extlen_t diff, bool new),
	TP_ARGS(cur, bno, len, diff, new),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, cur->bc_ops->name)
		__field(xfs_agblock_t, bno)
		__field(xfs_extlen_t, len)
		__field(xfs_extlen_t, diff)
		__field(bool, new)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__assign_str(name);
		__entry->bno = bno;
		__entry->len = len;
		__entry->diff = diff;
		__entry->new = new;
	),
	TP_printk("dev %d:%d %sbt agbno 0x%x fsbcount 0x%x diff 0x%x new %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->bno, __entry->len, __entry->diff, __entry->new)
)

DECLARE_EVENT_CLASS(xfs_da_class,
	TP_PROTO(struct xfs_da_args *args),
	TP_ARGS(args),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__dynamic_array(char, name, args->namelen)
		__field(int, namelen)
		__field(xfs_dahash_t, hashval)
		__field(xfs_ino_t, inumber)
		__field(uint32_t, op_flags)
		__field(xfs_ino_t, owner)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		if (args->namelen)
			memcpy(__get_str(name), args->name, args->namelen);
		__entry->namelen = args->namelen;
		__entry->hashval = args->hashval;
		__entry->inumber = args->inumber;
		__entry->op_flags = args->op_flags;
		__entry->owner = args->owner;
	),
	TP_printk("dev %d:%d ino 0x%llx name %.*s namelen %d hashval 0x%x "
		  "inumber 0x%llx op_flags %s owner 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->namelen,
		  __entry->namelen ? __get_str(name) : NULL,
		  __entry->namelen,
		  __entry->hashval,
		  __entry->inumber,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS),
		  __entry->owner)
)

#define DEFINE_DIR2_EVENT(name) \
DEFINE_EVENT(xfs_da_class, name, \
	TP_PROTO(struct xfs_da_args *args), \
	TP_ARGS(args))
DEFINE_DIR2_EVENT(xfs_dir2_sf_addname);
DEFINE_DIR2_EVENT(xfs_dir2_sf_create);
DEFINE_DIR2_EVENT(xfs_dir2_sf_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_sf_replace);
DEFINE_DIR2_EVENT(xfs_dir2_sf_removename);
DEFINE_DIR2_EVENT(xfs_dir2_sf_toino4);
DEFINE_DIR2_EVENT(xfs_dir2_sf_toino8);
DEFINE_DIR2_EVENT(xfs_dir2_sf_to_block);
DEFINE_DIR2_EVENT(xfs_dir2_block_addname);
DEFINE_DIR2_EVENT(xfs_dir2_block_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_block_replace);
DEFINE_DIR2_EVENT(xfs_dir2_block_removename);
DEFINE_DIR2_EVENT(xfs_dir2_block_to_sf);
DEFINE_DIR2_EVENT(xfs_dir2_block_to_leaf);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_addname);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_replace);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_removename);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_to_block);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_to_node);
DEFINE_DIR2_EVENT(xfs_dir2_node_addname);
DEFINE_DIR2_EVENT(xfs_dir2_node_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_node_replace);
DEFINE_DIR2_EVENT(xfs_dir2_node_removename);
DEFINE_DIR2_EVENT(xfs_dir2_node_to_leaf);

DECLARE_EVENT_CLASS(xfs_attr_class,
	TP_PROTO(struct xfs_da_args *args),
	TP_ARGS(args),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__dynamic_array(char, name, args->namelen)
		__field(int, namelen)
		__field(int, valuelen)
		__field(xfs_dahash_t, hashval)
		__field(unsigned int, attr_filter)
		__field(uint32_t, op_flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		if (args->namelen)
			memcpy(__get_str(name), args->name, args->namelen);
		__entry->namelen = args->namelen;
		__entry->valuelen = args->valuelen;
		__entry->hashval = args->hashval;
		__entry->attr_filter = args->attr_filter;
		__entry->op_flags = args->op_flags;
	),
	TP_printk("dev %d:%d ino 0x%llx name %.*s namelen %d valuelen %d "
		  "hashval 0x%x filter %s op_flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->namelen,
		  __entry->namelen ? __get_str(name) : NULL,
		  __entry->namelen,
		  __entry->valuelen,
		  __entry->hashval,
		  __print_flags(__entry->attr_filter, "|",
				XFS_ATTR_FILTER_FLAGS),
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS))
)

#define DEFINE_ATTR_EVENT(name) \
DEFINE_EVENT(xfs_attr_class, name, \
	TP_PROTO(struct xfs_da_args *args), \
	TP_ARGS(args))
DEFINE_ATTR_EVENT(xfs_attr_sf_add);
DEFINE_ATTR_EVENT(xfs_attr_sf_addname);
DEFINE_ATTR_EVENT(xfs_attr_sf_create);
DEFINE_ATTR_EVENT(xfs_attr_sf_lookup);
DEFINE_ATTR_EVENT(xfs_attr_sf_remove);
DEFINE_ATTR_EVENT(xfs_attr_sf_to_leaf);

DEFINE_ATTR_EVENT(xfs_attr_leaf_add);
DEFINE_ATTR_EVENT(xfs_attr_leaf_add_old);
DEFINE_ATTR_EVENT(xfs_attr_leaf_add_new);
DEFINE_ATTR_EVENT(xfs_attr_leaf_add_work);
DEFINE_ATTR_EVENT(xfs_attr_leaf_create);
DEFINE_ATTR_EVENT(xfs_attr_leaf_compact);
DEFINE_ATTR_EVENT(xfs_attr_leaf_get);
DEFINE_ATTR_EVENT(xfs_attr_leaf_lookup);
DEFINE_ATTR_EVENT(xfs_attr_leaf_replace);
DEFINE_ATTR_EVENT(xfs_attr_leaf_remove);
DEFINE_ATTR_EVENT(xfs_attr_leaf_removename);
DEFINE_ATTR_EVENT(xfs_attr_leaf_split);
DEFINE_ATTR_EVENT(xfs_attr_leaf_split_before);
DEFINE_ATTR_EVENT(xfs_attr_leaf_split_after);
DEFINE_ATTR_EVENT(xfs_attr_leaf_clearflag);
DEFINE_ATTR_EVENT(xfs_attr_leaf_setflag);
DEFINE_ATTR_EVENT(xfs_attr_leaf_flipflags);
DEFINE_ATTR_EVENT(xfs_attr_leaf_to_sf);
DEFINE_ATTR_EVENT(xfs_attr_leaf_to_node);
DEFINE_ATTR_EVENT(xfs_attr_leaf_rebalance);
DEFINE_ATTR_EVENT(xfs_attr_leaf_unbalance);
DEFINE_ATTR_EVENT(xfs_attr_leaf_toosmall);

DEFINE_ATTR_EVENT(xfs_attr_node_addname);
DEFINE_ATTR_EVENT(xfs_attr_node_get);
DEFINE_ATTR_EVENT(xfs_attr_node_replace);
DEFINE_ATTR_EVENT(xfs_attr_node_removename);

DEFINE_ATTR_EVENT(xfs_attr_fillstate);
DEFINE_ATTR_EVENT(xfs_attr_refillstate);

DEFINE_ATTR_EVENT(xfs_attr_rmtval_get);
DEFINE_ATTR_EVENT(xfs_attr_rmtval_set);

#define DEFINE_DA_EVENT(name) \
DEFINE_EVENT(xfs_da_class, name, \
	TP_PROTO(struct xfs_da_args *args), \
	TP_ARGS(args))
DEFINE_DA_EVENT(xfs_da_split);
DEFINE_DA_EVENT(xfs_da_join);
DEFINE_DA_EVENT(xfs_da_link_before);
DEFINE_DA_EVENT(xfs_da_link_after);
DEFINE_DA_EVENT(xfs_da_unlink_back);
DEFINE_DA_EVENT(xfs_da_unlink_forward);
DEFINE_DA_EVENT(xfs_da_root_split);
DEFINE_DA_EVENT(xfs_da_root_join);
DEFINE_DA_EVENT(xfs_da_node_add);
DEFINE_DA_EVENT(xfs_da_node_create);
DEFINE_DA_EVENT(xfs_da_node_split);
DEFINE_DA_EVENT(xfs_da_node_remove);
DEFINE_DA_EVENT(xfs_da_node_rebalance);
DEFINE_DA_EVENT(xfs_da_node_unbalance);
DEFINE_DA_EVENT(xfs_da_node_toosmall);
DEFINE_DA_EVENT(xfs_da_swap_lastblock);
DEFINE_DA_EVENT(xfs_da_grow_inode);
DEFINE_DA_EVENT(xfs_da_shrink_inode);
DEFINE_DA_EVENT(xfs_da_fixhashpath);
DEFINE_DA_EVENT(xfs_da_path_shift);

DECLARE_EVENT_CLASS(xfs_dir2_space_class,
	TP_PROTO(struct xfs_da_args *args, int idx),
	TP_ARGS(args, idx),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(uint32_t, op_flags)
		__field(int, idx)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		__entry->op_flags = args->op_flags;
		__entry->idx = idx;
	),
	TP_printk("dev %d:%d ino 0x%llx op_flags %s index %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS),
		  __entry->idx)
)

#define DEFINE_DIR2_SPACE_EVENT(name) \
DEFINE_EVENT(xfs_dir2_space_class, name, \
	TP_PROTO(struct xfs_da_args *args, int idx), \
	TP_ARGS(args, idx))
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_leafn_add);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_leafn_remove);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_grow_inode);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_shrink_inode);

TRACE_EVENT(xfs_dir2_leafn_moveents,
	TP_PROTO(struct xfs_da_args *args, int src_idx, int dst_idx, int count),
	TP_ARGS(args, src_idx, dst_idx, count),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(uint32_t, op_flags)
		__field(int, src_idx)
		__field(int, dst_idx)
		__field(int, count)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		__entry->op_flags = args->op_flags;
		__entry->src_idx = src_idx;
		__entry->dst_idx = dst_idx;
		__entry->count = count;
	),
	TP_printk("dev %d:%d ino 0x%llx op_flags %s "
		  "src_idx %d dst_idx %d count %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS),
		  __entry->src_idx,
		  __entry->dst_idx,
		  __entry->count)
);

#define XFS_SWAPEXT_INODES \
	{ 0,	"target" }, \
	{ 1,	"temp" }

TRACE_DEFINE_ENUM(XFS_DINODE_FMT_DEV);
TRACE_DEFINE_ENUM(XFS_DINODE_FMT_LOCAL);
TRACE_DEFINE_ENUM(XFS_DINODE_FMT_EXTENTS);
TRACE_DEFINE_ENUM(XFS_DINODE_FMT_BTREE);
TRACE_DEFINE_ENUM(XFS_DINODE_FMT_UUID);
TRACE_DEFINE_ENUM(XFS_DINODE_FMT_META_BTREE);

DECLARE_EVENT_CLASS(xfs_swap_extent_class,
	TP_PROTO(struct xfs_inode *ip, int which),
	TP_ARGS(ip, which),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, which)
		__field(xfs_ino_t, ino)
		__field(int, format)
		__field(xfs_extnum_t, nex)
		__field(int, broot_size)
		__field(int, fork_off)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->which = which;
		__entry->ino = ip->i_ino;
		__entry->format = ip->i_df.if_format;
		__entry->nex = ip->i_df.if_nextents;
		__entry->broot_size = ip->i_df.if_broot_bytes;
		__entry->fork_off = xfs_inode_fork_boff(ip);
	),
	TP_printk("dev %d:%d ino 0x%llx (%s), %s format, num_extents %llu, "
		  "broot size %d, forkoff 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->which, XFS_SWAPEXT_INODES),
		  __print_symbolic(__entry->format, XFS_INODE_FORMAT_STR),
		  __entry->nex,
		  __entry->broot_size,
		  __entry->fork_off)
)

#define DEFINE_SWAPEXT_EVENT(name) \
DEFINE_EVENT(xfs_swap_extent_class, name, \
	TP_PROTO(struct xfs_inode *ip, int which), \
	TP_ARGS(ip, which))

DEFINE_SWAPEXT_EVENT(xfs_swap_extent_before);
DEFINE_SWAPEXT_EVENT(xfs_swap_extent_after);

TRACE_EVENT(xfs_log_recover,
	TP_PROTO(struct xlog *log, xfs_daddr_t headblk, xfs_daddr_t tailblk),
	TP_ARGS(log, headblk, tailblk),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, headblk)
		__field(xfs_daddr_t, tailblk)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->headblk = headblk;
		__entry->tailblk = tailblk;
	),
	TP_printk("dev %d:%d headblk 0x%llx tailblk 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->headblk,
		  __entry->tailblk)
)

TRACE_EVENT(xfs_log_recover_record,
	TP_PROTO(struct xlog *log, struct xlog_rec_header *rhead, int pass),
	TP_ARGS(log, rhead, pass),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_lsn_t, lsn)
		__field(int, len)
		__field(int, num_logops)
		__field(int, pass)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->lsn = be64_to_cpu(rhead->h_lsn);
		__entry->len = be32_to_cpu(rhead->h_len);
		__entry->num_logops = be32_to_cpu(rhead->h_num_logops);
		__entry->pass = pass;
	),
	TP_printk("dev %d:%d lsn 0x%llx len 0x%x num_logops 0x%x pass %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lsn, __entry->len, __entry->num_logops,
		   __entry->pass)
)

DECLARE_EVENT_CLASS(xfs_log_recover_item_class,
	TP_PROTO(struct xlog *log, struct xlog_recover *trans,
		struct xlog_recover_item *item, int pass),
	TP_ARGS(log, trans, item, pass),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, item)
		__field(xlog_tid_t, tid)
		__field(xfs_lsn_t, lsn)
		__field(int, type)
		__field(int, pass)
		__field(int, count)
		__field(int, total)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->item = (unsigned long)item;
		__entry->tid = trans->r_log_tid;
		__entry->lsn = trans->r_lsn;
		__entry->type = ITEM_TYPE(item);
		__entry->pass = pass;
		__entry->count = item->ri_cnt;
		__entry->total = item->ri_total;
	),
	TP_printk("dev %d:%d tid 0x%x lsn 0x%llx, pass %d, item %p, "
		  "item type %s item region count/total %d/%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tid,
		  __entry->lsn,
		  __entry->pass,
		  (void *)__entry->item,
		  __print_symbolic(__entry->type, XFS_LI_TYPE_DESC),
		  __entry->count,
		  __entry->total)
)

#define DEFINE_LOG_RECOVER_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_item_class, name, \
	TP_PROTO(struct xlog *log, struct xlog_recover *trans, \
		struct xlog_recover_item *item, int pass), \
	TP_ARGS(log, trans, item, pass))

DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_add);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_add_cont);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_reorder_head);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_reorder_tail);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_recover);

DECLARE_EVENT_CLASS(xfs_log_recover_buf_item_class,
	TP_PROTO(struct xlog *log, struct xfs_buf_log_format *buf_f),
	TP_ARGS(log, buf_f),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int64_t, blkno)
		__field(unsigned short, len)
		__field(unsigned short, flags)
		__field(unsigned short, size)
		__field(unsigned int, map_size)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->blkno = buf_f->blf_blkno;
		__entry->len = buf_f->blf_len;
		__entry->flags = buf_f->blf_flags;
		__entry->size = buf_f->blf_size;
		__entry->map_size = buf_f->blf_map_size;
	),
	TP_printk("dev %d:%d daddr 0x%llx, bbcount 0x%x, flags 0x%x, size %d, "
			"map_size %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->blkno,
		  __entry->len,
		  __entry->flags,
		  __entry->size,
		  __entry->map_size)
)

#define DEFINE_LOG_RECOVER_BUF_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_buf_item_class, name, \
	TP_PROTO(struct xlog *log, struct xfs_buf_log_format *buf_f), \
	TP_ARGS(log, buf_f))

DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_not_cancel);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel_add);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel_ref_inc);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_recover);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_skip);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_inode_buf);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_reg_buf);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_dquot_buf);

DECLARE_EVENT_CLASS(xfs_log_recover_ino_item_class,
	TP_PROTO(struct xlog *log, struct xfs_inode_log_format *in_f),
	TP_ARGS(log, in_f),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned short, size)
		__field(int, fields)
		__field(unsigned short, asize)
		__field(unsigned short, dsize)
		__field(int64_t, blkno)
		__field(int, len)
		__field(int, boffset)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->ino = in_f->ilf_ino;
		__entry->size = in_f->ilf_size;
		__entry->fields = in_f->ilf_fields;
		__entry->asize = in_f->ilf_asize;
		__entry->dsize = in_f->ilf_dsize;
		__entry->blkno = in_f->ilf_blkno;
		__entry->len = in_f->ilf_len;
		__entry->boffset = in_f->ilf_boffset;
	),
	TP_printk("dev %d:%d ino 0x%llx, size %u, fields 0x%x, asize %d, "
			"dsize %d, daddr 0x%llx, bbcount 0x%x, boffset %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->fields,
		  __entry->asize,
		  __entry->dsize,
		  __entry->blkno,
		  __entry->len,
		  __entry->boffset)
)
#define DEFINE_LOG_RECOVER_INO_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_ino_item_class, name, \
	TP_PROTO(struct xlog *log, struct xfs_inode_log_format *in_f), \
	TP_ARGS(log, in_f))

DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_recover);
DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_cancel);
DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_skip);

DECLARE_EVENT_CLASS(xfs_log_recover_icreate_item_class,
	TP_PROTO(struct xlog *log, struct xfs_icreate_log *in_f),
	TP_ARGS(log, in_f),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(unsigned int, count)
		__field(unsigned int, isize)
		__field(xfs_agblock_t, length)
		__field(unsigned int, gen)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->agno = be32_to_cpu(in_f->icl_ag);
		__entry->agbno = be32_to_cpu(in_f->icl_agbno);
		__entry->count = be32_to_cpu(in_f->icl_count);
		__entry->isize = be32_to_cpu(in_f->icl_isize);
		__entry->length = be32_to_cpu(in_f->icl_length);
		__entry->gen = be32_to_cpu(in_f->icl_gen);
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x ireccount %u isize %u gen 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->length,
		  __entry->count,
		  __entry->isize,
		  __entry->gen)
)
#define DEFINE_LOG_RECOVER_ICREATE_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_icreate_item_class, name, \
	TP_PROTO(struct xlog *log, struct xfs_icreate_log *in_f), \
	TP_ARGS(log, in_f))

DEFINE_LOG_RECOVER_ICREATE_ITEM(xfs_log_recover_icreate_cancel);
DEFINE_LOG_RECOVER_ICREATE_ITEM(xfs_log_recover_icreate_recover);

DECLARE_EVENT_CLASS(xfs_discard_class,
	TP_PROTO(const struct xfs_group *xg, xfs_agblock_t agbno,
		 xfs_extlen_t len),
	TP_ARGS(xg, agbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = xg->xg_mount->m_super->s_dev;
		__entry->type = xg->xg_type;
		__entry->agno = xg->xg_gno;
		__entry->agbno = agbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d %sno 0x%x gbno 0x%x fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len)
)

#define DEFINE_DISCARD_EVENT(name) \
DEFINE_EVENT(xfs_discard_class, name, \
	TP_PROTO(const struct xfs_group *xg, xfs_agblock_t agbno, \
		 xfs_extlen_t len), \
	TP_ARGS(xg, agbno, len))
DEFINE_DISCARD_EVENT(xfs_discard_extent);
DEFINE_DISCARD_EVENT(xfs_discard_toosmall);
DEFINE_DISCARD_EVENT(xfs_discard_exclude);
DEFINE_DISCARD_EVENT(xfs_discard_busy);

DECLARE_EVENT_CLASS(xfs_rtdiscard_class,
	TP_PROTO(struct xfs_mount *mp,
		 xfs_rtblock_t rtbno, xfs_rtblock_t len),
	TP_ARGS(mp, rtbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_rtblock_t, rtbno)
		__field(xfs_rtblock_t, len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_rtdev_targp->bt_dev;
		__entry->rtbno = rtbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d rtbno 0x%llx rtbcount 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rtbno,
		  __entry->len)
)

#define DEFINE_RTDISCARD_EVENT(name) \
DEFINE_EVENT(xfs_rtdiscard_class, name, \
	TP_PROTO(struct xfs_mount *mp, \
		 xfs_rtblock_t rtbno, xfs_rtblock_t len), \
	TP_ARGS(mp, rtbno, len))
DEFINE_RTDISCARD_EVENT(xfs_discard_rtextent);
DEFINE_RTDISCARD_EVENT(xfs_discard_rttoosmall);
DEFINE_RTDISCARD_EVENT(xfs_discard_rtrelax);

DECLARE_EVENT_CLASS(xfs_btree_cur_class,
	TP_PROTO(struct xfs_btree_cur *cur, int level, struct xfs_buf *bp),
	TP_ARGS(cur, level, bp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, cur->bc_ops->name)
		__field(int, level)
		__field(int, nlevels)
		__field(int, ptr)
		__field(xfs_daddr_t, daddr)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__assign_str(name);
		__entry->level = level;
		__entry->nlevels = cur->bc_nlevels;
		__entry->ptr = cur->bc_levels[level].ptr;
		__entry->daddr = bp ? xfs_buf_daddr(bp) : -1;
	),
	TP_printk("dev %d:%d %sbt level %d/%d ptr %d daddr 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->level,
		  __entry->nlevels,
		  __entry->ptr,
		  (unsigned long long)__entry->daddr)
)

#define DEFINE_BTREE_CUR_EVENT(name) \
DEFINE_EVENT(xfs_btree_cur_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, int level, struct xfs_buf *bp), \
	TP_ARGS(cur, level, bp))
DEFINE_BTREE_CUR_EVENT(xfs_btree_updkeys);
DEFINE_BTREE_CUR_EVENT(xfs_btree_overlapped_query_range);

TRACE_EVENT(xfs_btree_alloc_block,
	TP_PROTO(struct xfs_btree_cur *cur, union xfs_btree_ptr *ptr, int stat,
		 int error),
	TP_ARGS(cur, ptr, stat, error),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_ino_t, ino)
		__string(name, cur->bc_ops->name)
		__field(int, error)
		__field(xfs_agblock_t, agbno)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		switch (cur->bc_ops->type) {
		case XFS_BTREE_TYPE_INODE:
			__entry->agno = 0;
			__entry->ino = cur->bc_ino.ip->i_ino;
			break;
		case XFS_BTREE_TYPE_AG:
			__entry->agno = cur->bc_group->xg_gno;
			__entry->ino = 0;
			break;
		case XFS_BTREE_TYPE_MEM:
			__entry->agno = 0;
			__entry->ino = 0;
			break;
		}
		__assign_str(name);
		__entry->error = error;
		if (!error && stat) {
			if (cur->bc_ops->ptr_len == XFS_BTREE_LONG_PTR_LEN) {
				xfs_fsblock_t	fsb = be64_to_cpu(ptr->l);

				__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp,
								fsb);
				__entry->agbno = XFS_FSB_TO_AGBNO(cur->bc_mp,
								fsb);
			} else {
				__entry->agbno = be32_to_cpu(ptr->s);
			}
		} else {
			__entry->agbno = NULLAGBLOCK;
		}
	),
	TP_printk("dev %d:%d %sbt agno 0x%x ino 0x%llx agbno 0x%x error %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->agno,
		  __entry->ino,
		  __entry->agbno,
		  __entry->error)
);

TRACE_EVENT(xfs_btree_free_block,
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_buf *bp),
	TP_ARGS(cur, bp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_ino_t, ino)
		__string(name, cur->bc_ops->name)
		__field(xfs_agblock_t, agbno)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->agno = xfs_daddr_to_agno(cur->bc_mp,
							xfs_buf_daddr(bp));
		if (cur->bc_ops->type == XFS_BTREE_TYPE_INODE)
			__entry->ino = cur->bc_ino.ip->i_ino;
		else
			__entry->ino = 0;
		__assign_str(name);
		__entry->agbno = xfs_daddr_to_agbno(cur->bc_mp,
							xfs_buf_daddr(bp));
	),
	TP_printk("dev %d:%d %sbt agno 0x%x ino 0x%llx agbno 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->agno,
		  __entry->ino,
		  __entry->agbno)
);

/* deferred ops */
struct xfs_defer_pending;

DECLARE_EVENT_CLASS(xfs_defer_class,
	TP_PROTO(struct xfs_trans *tp, unsigned long caller_ip),
	TP_ARGS(tp, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(struct xfs_trans *, tp)
		__field(char, committed)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = tp->t_mountp->m_super->s_dev;
		__entry->tp = tp;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d tp %p caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tp,
		  (char *)__entry->caller_ip)
)
#define DEFINE_DEFER_EVENT(name) \
DEFINE_EVENT(xfs_defer_class, name, \
	TP_PROTO(struct xfs_trans *tp, unsigned long caller_ip), \
	TP_ARGS(tp, caller_ip))

DECLARE_EVENT_CLASS(xfs_defer_error_class,
	TP_PROTO(struct xfs_trans *tp, int error),
	TP_ARGS(tp, error),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(struct xfs_trans *, tp)
		__field(char, committed)
		__field(int, error)
	),
	TP_fast_assign(
		__entry->dev = tp->t_mountp->m_super->s_dev;
		__entry->tp = tp;
		__entry->error = error;
	),
	TP_printk("dev %d:%d tp %p err %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tp,
		  __entry->error)
)
#define DEFINE_DEFER_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_defer_error_class, name, \
	TP_PROTO(struct xfs_trans *tp, int error), \
	TP_ARGS(tp, error))

DECLARE_EVENT_CLASS(xfs_defer_pending_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_pending *dfp),
	TP_ARGS(mp, dfp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, dfp->dfp_ops->name)
		__field(void *, intent)
		__field(unsigned int, flags)
		__field(char, committed)
		__field(int, nr)
	),
	TP_fast_assign(
		__entry->dev = mp ? mp->m_super->s_dev : 0;
		__assign_str(name);
		__entry->intent = dfp->dfp_intent;
		__entry->flags = dfp->dfp_flags;
		__entry->committed = dfp->dfp_done != NULL;
		__entry->nr = dfp->dfp_count;
	),
	TP_printk("dev %d:%d optype %s intent %p flags %s committed %d nr %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->intent,
		  __print_flags(__entry->flags, "|", XFS_DEFER_PENDING_STRINGS),
		  __entry->committed,
		  __entry->nr)
)
#define DEFINE_DEFER_PENDING_EVENT(name) \
DEFINE_EVENT(xfs_defer_pending_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_pending *dfp), \
	TP_ARGS(mp, dfp))

DEFINE_DEFER_EVENT(xfs_defer_cancel);
DEFINE_DEFER_EVENT(xfs_defer_trans_roll);
DEFINE_DEFER_EVENT(xfs_defer_trans_abort);
DEFINE_DEFER_EVENT(xfs_defer_finish);
DEFINE_DEFER_EVENT(xfs_defer_finish_done);

DEFINE_DEFER_ERROR_EVENT(xfs_defer_trans_roll_error);
DEFINE_DEFER_ERROR_EVENT(xfs_defer_finish_error);

DEFINE_DEFER_PENDING_EVENT(xfs_defer_create_intent);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_cancel_list);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_pending_finish);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_pending_abort);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_relog_intent);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_isolate_paused);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_item_pause);
DEFINE_DEFER_PENDING_EVENT(xfs_defer_item_unpause);

DECLARE_EVENT_CLASS(xfs_free_extent_deferred_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_extent_free_item *free),
	TP_ARGS(mp, free),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->type = free->xefi_group->xg_type;
		__entry->agno = free->xefi_group->xg_gno;
		__entry->agbno = xfs_fsb_to_gbno(mp, free->xefi_startblock,
						free->xefi_group->xg_type);
		__entry->len = free->xefi_blockcount;
		__entry->flags = free->xefi_flags;
	),
	TP_printk("dev %d:%d %sno 0x%x gbno 0x%x fsbcount 0x%x flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->flags)
);
#define DEFINE_FREE_EXTENT_DEFERRED_EVENT(name) \
DEFINE_EVENT(xfs_free_extent_deferred_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_extent_free_item *free), \
	TP_ARGS(mp, free))
DEFINE_FREE_EXTENT_DEFERRED_EVENT(xfs_agfl_free_deferred);
DEFINE_FREE_EXTENT_DEFERRED_EVENT(xfs_extent_free_defer);
DEFINE_FREE_EXTENT_DEFERRED_EVENT(xfs_extent_free_deferred);

DECLARE_EVENT_CLASS(xfs_defer_pending_item_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_pending *dfp,
		 void *item),
	TP_ARGS(mp, dfp, item),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, dfp->dfp_ops->name)
		__field(void *, intent)
		__field(void *, item)
		__field(char, committed)
		__field(unsigned int, flags)
		__field(int, nr)
	),
	TP_fast_assign(
		__entry->dev = mp ? mp->m_super->s_dev : 0;
		__assign_str(name);
		__entry->intent = dfp->dfp_intent;
		__entry->item = item;
		__entry->committed = dfp->dfp_done != NULL;
		__entry->flags = dfp->dfp_flags;
		__entry->nr = dfp->dfp_count;
	),
	TP_printk("dev %d:%d optype %s intent %p item %p flags %s committed %d nr %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->intent,
		  __entry->item,
		  __print_flags(__entry->flags, "|", XFS_DEFER_PENDING_STRINGS),
		  __entry->committed,
		  __entry->nr)
)
#define DEFINE_DEFER_PENDING_ITEM_EVENT(name) \
DEFINE_EVENT(xfs_defer_pending_item_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_defer_pending *dfp, \
		 void *item), \
	TP_ARGS(mp, dfp, item))

DEFINE_DEFER_PENDING_ITEM_EVENT(xfs_defer_add_item);
DEFINE_DEFER_PENDING_ITEM_EVENT(xfs_defer_cancel_item);
DEFINE_DEFER_PENDING_ITEM_EVENT(xfs_defer_finish_item);

/* rmap tracepoints */
DECLARE_EVENT_CLASS(xfs_rmap_class,
	TP_PROTO(struct xfs_btree_cur *cur,
		 xfs_agblock_t gbno, xfs_extlen_t len, bool unwritten,
		 const struct xfs_owner_info *oinfo),
	TP_ARGS(cur, gbno, len, unwritten, oinfo),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, gbno)
		__field(xfs_extlen_t, len)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(unsigned long, flags)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->gbno = gbno;
		__entry->len = len;
		__entry->owner = oinfo->oi_owner;
		__entry->offset = oinfo->oi_offset;
		__entry->flags = oinfo->oi_flags;
		if (unwritten)
			__entry->flags |= XFS_RMAP_UNWRITTEN;
	),
	TP_printk("dev %d:%d %sno 0x%x gbno 0x%x fsbcount 0x%x owner 0x%llx fileoff 0x%llx flags 0x%lx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->gbno,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);
#define DEFINE_RMAP_EVENT(name) \
DEFINE_EVENT(xfs_rmap_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, \
		 xfs_agblock_t gbno, xfs_extlen_t len, bool unwritten, \
		 const struct xfs_owner_info *oinfo), \
	TP_ARGS(cur, gbno, len, unwritten, oinfo))

/* btree cursor error/%ip tracepoint class */
DECLARE_EVENT_CLASS(xfs_btree_error_class,
	TP_PROTO(struct xfs_btree_cur *cur, int error,
		 unsigned long caller_ip),
	TP_ARGS(cur, error, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_ino_t, ino)
		__field(int, error)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		switch (cur->bc_ops->type) {
		case XFS_BTREE_TYPE_INODE:
			__entry->agno = 0;
			__entry->ino = cur->bc_ino.ip->i_ino;
			break;
		case XFS_BTREE_TYPE_AG:
			__entry->agno = cur->bc_group->xg_gno;
			__entry->ino = 0;
			break;
		case XFS_BTREE_TYPE_MEM:
			__entry->agno = 0;
			__entry->ino = 0;
			break;
		}
		__entry->error = error;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno 0x%x ino 0x%llx error %d caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->ino,
		  __entry->error,
		  (char *)__entry->caller_ip)
);

#define DEFINE_BTREE_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_btree_error_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, int error, \
		 unsigned long caller_ip), \
	TP_ARGS(cur, error, caller_ip))

DEFINE_RMAP_EVENT(xfs_rmap_unmap);
DEFINE_RMAP_EVENT(xfs_rmap_unmap_done);
DEFINE_BTREE_ERROR_EVENT(xfs_rmap_unmap_error);
DEFINE_RMAP_EVENT(xfs_rmap_map);
DEFINE_RMAP_EVENT(xfs_rmap_map_done);
DEFINE_BTREE_ERROR_EVENT(xfs_rmap_map_error);
DEFINE_RMAP_EVENT(xfs_rmap_convert);
DEFINE_RMAP_EVENT(xfs_rmap_convert_done);
DEFINE_BTREE_ERROR_EVENT(xfs_rmap_convert_error);

TRACE_EVENT(xfs_rmap_convert_state,
	TP_PROTO(struct xfs_btree_cur *cur, int state,
		 unsigned long caller_ip),
	TP_ARGS(cur, state, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(int, state)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->state = state;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d %sno 0x%x state %d caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->state,
		  (char *)__entry->caller_ip)
);

DECLARE_EVENT_CLASS(xfs_rmapbt_class,
	TP_PROTO(struct xfs_btree_cur *cur,
		 xfs_agblock_t gbno, xfs_extlen_t len,
		 uint64_t owner, uint64_t offset, unsigned int flags),
	TP_ARGS(cur, gbno, len, owner, offset, flags),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, gbno)
		__field(xfs_extlen_t, len)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->gbno = gbno;
		__entry->len = len;
		__entry->owner = owner;
		__entry->offset = offset;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d %sno 0x%x gbno 0x%x fsbcount 0x%x owner 0x%llx fileoff 0x%llx flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->gbno,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);
#define DEFINE_RMAPBT_EVENT(name) \
DEFINE_EVENT(xfs_rmapbt_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, \
		 xfs_agblock_t gbno, xfs_extlen_t len, \
		 uint64_t owner, uint64_t offset, unsigned int flags), \
	TP_ARGS(cur, gbno, len, owner, offset, flags))

TRACE_DEFINE_ENUM(XFS_RMAP_MAP);
TRACE_DEFINE_ENUM(XFS_RMAP_MAP_SHARED);
TRACE_DEFINE_ENUM(XFS_RMAP_UNMAP);
TRACE_DEFINE_ENUM(XFS_RMAP_UNMAP_SHARED);
TRACE_DEFINE_ENUM(XFS_RMAP_CONVERT);
TRACE_DEFINE_ENUM(XFS_RMAP_CONVERT_SHARED);
TRACE_DEFINE_ENUM(XFS_RMAP_ALLOC);
TRACE_DEFINE_ENUM(XFS_RMAP_FREE);

DECLARE_EVENT_CLASS(xfs_rmap_deferred_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_rmap_intent *ri),
	TP_ARGS(mp, ri),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long long, owner)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, gbno)
		__field(int, whichfork)
		__field(xfs_fileoff_t, l_loff)
		__field(xfs_filblks_t, l_len)
		__field(xfs_exntst_t, l_state)
		__field(int, op)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->type = ri->ri_group->xg_type;
		__entry->agno = ri->ri_group->xg_gno;
		__entry->gbno = xfs_fsb_to_gbno(mp,
						ri->ri_bmap.br_startblock,
						ri->ri_group->xg_type);
		__entry->owner = ri->ri_owner;
		__entry->whichfork = ri->ri_whichfork;
		__entry->l_loff = ri->ri_bmap.br_startoff;
		__entry->l_len = ri->ri_bmap.br_blockcount;
		__entry->l_state = ri->ri_bmap.br_state;
		__entry->op = ri->ri_type;
	),
	TP_printk("dev %d:%d op %s %sno 0x%x gbno 0x%x owner 0x%llx %s fileoff 0x%llx fsbcount 0x%llx state %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->op, XFS_RMAP_INTENT_STRINGS),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->gbno,
		  __entry->owner,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __entry->l_loff,
		  __entry->l_len,
		  __entry->l_state)
);
#define DEFINE_RMAP_DEFERRED_EVENT(name) \
DEFINE_EVENT(xfs_rmap_deferred_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_rmap_intent *ri), \
	TP_ARGS(mp, ri))
DEFINE_RMAP_DEFERRED_EVENT(xfs_rmap_defer);
DEFINE_RMAP_DEFERRED_EVENT(xfs_rmap_deferred);

DEFINE_RMAPBT_EVENT(xfs_rmap_update);
DEFINE_RMAPBT_EVENT(xfs_rmap_insert);
DEFINE_RMAPBT_EVENT(xfs_rmap_delete);
DEFINE_BTREE_ERROR_EVENT(xfs_rmap_insert_error);
DEFINE_BTREE_ERROR_EVENT(xfs_rmap_delete_error);
DEFINE_BTREE_ERROR_EVENT(xfs_rmap_update_error);

DEFINE_RMAPBT_EVENT(xfs_rmap_find_left_neighbor_candidate);
DEFINE_RMAPBT_EVENT(xfs_rmap_find_left_neighbor_query);
DEFINE_RMAPBT_EVENT(xfs_rmap_lookup_le_range_candidate);
DEFINE_RMAPBT_EVENT(xfs_rmap_lookup_le_range);
DEFINE_RMAPBT_EVENT(xfs_rmap_lookup_le_range_result);
DEFINE_RMAPBT_EVENT(xfs_rmap_find_right_neighbor_result);
DEFINE_RMAPBT_EVENT(xfs_rmap_find_left_neighbor_result);

/* deferred bmbt updates */
TRACE_DEFINE_ENUM(XFS_BMAP_MAP);
TRACE_DEFINE_ENUM(XFS_BMAP_UNMAP);

DECLARE_EVENT_CLASS(xfs_bmap_deferred_class,
	TP_PROTO(struct xfs_bmap_intent *bi),
	TP_ARGS(bi),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_ino_t, ino)
		__field(unsigned long long, gbno)
		__field(int, whichfork)
		__field(xfs_fileoff_t, l_loff)
		__field(xfs_filblks_t, l_len)
		__field(xfs_exntst_t, l_state)
		__field(int, op)
	),
	TP_fast_assign(
		struct xfs_inode	*ip = bi->bi_owner;
		struct xfs_mount	*mp = ip->i_mount;

		__entry->dev = mp->m_super->s_dev;
		__entry->type = bi->bi_group->xg_type;
		__entry->agno = bi->bi_group->xg_gno;
		if (bi->bi_group->xg_type == XG_TYPE_RTG &&
		    !xfs_has_rtgroups(mp)) {
			/*
			 * Legacy rt filesystems do not have allocation groups
			 * ondisk.  We emulate this incore with one gigantic
			 * rtgroup whose size can exceed a 32-bit block number.
			 * For this tracepoint, we report group 0 and a 64-bit
			 * group block number.
			 */
			__entry->gbno = bi->bi_bmap.br_startblock;
		} else {
			__entry->gbno = xfs_fsb_to_gbno(mp,
						bi->bi_bmap.br_startblock,
						bi->bi_group->xg_type);
		}
		__entry->ino = ip->i_ino;
		__entry->whichfork = bi->bi_whichfork;
		__entry->l_loff = bi->bi_bmap.br_startoff;
		__entry->l_len = bi->bi_bmap.br_blockcount;
		__entry->l_state = bi->bi_bmap.br_state;
		__entry->op = bi->bi_type;
	),
	TP_printk("dev %d:%d op %s ino 0x%llx %sno 0x%x gbno 0x%llx %s fileoff 0x%llx fsbcount 0x%llx state %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->op, XFS_BMAP_INTENT_STRINGS),
		  __entry->ino,
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->gbno,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __entry->l_loff,
		  __entry->l_len,
		  __entry->l_state)
);
#define DEFINE_BMAP_DEFERRED_EVENT(name) \
DEFINE_EVENT(xfs_bmap_deferred_class, name, \
	TP_PROTO(struct xfs_bmap_intent *bi), \
	TP_ARGS(bi))
DEFINE_BMAP_DEFERRED_EVENT(xfs_bmap_defer);
DEFINE_BMAP_DEFERRED_EVENT(xfs_bmap_deferred);

/* per-AG reservation */
DECLARE_EVENT_CLASS(xfs_ag_resv_class,
	TP_PROTO(struct xfs_perag *pag, enum xfs_ag_resv_type resv,
		 xfs_extlen_t len),
	TP_ARGS(pag, resv, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, resv)
		__field(xfs_extlen_t, freeblks)
		__field(xfs_extlen_t, flcount)
		__field(xfs_extlen_t, reserved)
		__field(xfs_extlen_t, asked)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		struct xfs_ag_resv	*r = xfs_perag_resv(pag, resv);

		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
		__entry->resv = resv;
		__entry->freeblks = pag->pagf_freeblks;
		__entry->flcount = pag->pagf_flcount;
		__entry->reserved = r ? r->ar_reserved : 0;
		__entry->asked = r ? r->ar_asked : 0;
		__entry->len = len;
	),
	TP_printk("dev %d:%d agno 0x%x resv %d freeblks %u flcount %u "
		  "resv %u ask %u len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->resv,
		  __entry->freeblks,
		  __entry->flcount,
		  __entry->reserved,
		  __entry->asked,
		  __entry->len)
)
#define DEFINE_AG_RESV_EVENT(name) \
DEFINE_EVENT(xfs_ag_resv_class, name, \
	TP_PROTO(struct xfs_perag *pag, enum xfs_ag_resv_type type, \
		 xfs_extlen_t len), \
	TP_ARGS(pag, type, len))

/* per-AG reservation tracepoints */
DEFINE_AG_RESV_EVENT(xfs_ag_resv_init);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_free);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_alloc_extent);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_free_extent);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_critical);
DEFINE_AG_RESV_EVENT(xfs_ag_resv_needed);

TRACE_EVENT(xfs_ag_resv_init_error,
	TP_PROTO(const struct xfs_perag *pag, int error,
		 unsigned long caller_ip),
	TP_ARGS(pag, error, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, error)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
		__entry->error = error;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno 0x%x error %d caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->error,
		  (char *)__entry->caller_ip)
);

/* refcount tracepoint classes */

DECLARE_EVENT_CLASS(xfs_refcount_class,
	TP_PROTO(struct xfs_btree_cur *cur, xfs_agblock_t gbno,
		xfs_extlen_t len),
	TP_ARGS(cur, gbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, gbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->gbno = gbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d %sno 0x%x gbno 0x%x fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->gbno,
		  __entry->len)
);
#define DEFINE_REFCOUNT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, xfs_agblock_t gbno, \
		xfs_extlen_t len), \
	TP_ARGS(cur, gbno, len))

TRACE_DEFINE_ENUM(XFS_LOOKUP_EQi);
TRACE_DEFINE_ENUM(XFS_LOOKUP_LEi);
TRACE_DEFINE_ENUM(XFS_LOOKUP_GEi);
TRACE_EVENT(xfs_refcount_lookup,
	TP_PROTO(struct xfs_btree_cur *cur, xfs_agblock_t gbno,
		xfs_lookup_t dir),
	TP_ARGS(cur, gbno, dir),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, gbno)
		__field(xfs_lookup_t, dir)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->gbno = gbno;
		__entry->dir = dir;
	),
	TP_printk("dev %d:%d %sno 0x%x gbno 0x%x cmp %s(%d)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->gbno,
		  __print_symbolic(__entry->dir, XFS_AG_BTREE_CMP_FORMAT_STR),
		  __entry->dir)
)

/* single-rcext tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_extent_class,
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *irec),
	TP_ARGS(cur, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(enum xfs_refc_domain, domain)
		__field(xfs_agblock_t, startblock)
		__field(xfs_extlen_t, blockcount)
		__field(xfs_nlink_t, refcount)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->domain = irec->rc_domain;
		__entry->startblock = irec->rc_startblock;
		__entry->blockcount = irec->rc_blockcount;
		__entry->refcount = irec->rc_refcount;
	),
	TP_printk("dev %d:%d %sno 0x%x dom %s gbno 0x%x fsbcount 0x%x refcount %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __print_symbolic(__entry->domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->refcount)
)

#define DEFINE_REFCOUNT_EXTENT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_extent_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *irec), \
	TP_ARGS(cur, irec))

/* single-rcext and an agbno tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_extent_at_class,
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *irec,
		 xfs_agblock_t gbno),
	TP_ARGS(cur, irec, gbno),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(enum xfs_refc_domain, domain)
		__field(xfs_agblock_t, startblock)
		__field(xfs_extlen_t, blockcount)
		__field(xfs_nlink_t, refcount)
		__field(xfs_agblock_t, gbno)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->domain = irec->rc_domain;
		__entry->startblock = irec->rc_startblock;
		__entry->blockcount = irec->rc_blockcount;
		__entry->refcount = irec->rc_refcount;
		__entry->gbno = gbno;
	),
	TP_printk("dev %d:%d %sno 0x%x dom %s gbno 0x%x fsbcount 0x%x refcount %u @ gbno 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __print_symbolic(__entry->domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->refcount,
		  __entry->gbno)
)

#define DEFINE_REFCOUNT_EXTENT_AT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_extent_at_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *irec, \
		 xfs_agblock_t gbno), \
	TP_ARGS(cur, irec, gbno))

/* double-rcext tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_double_extent_class,
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *i1,
		 struct xfs_refcount_irec *i2),
	TP_ARGS(cur, i1, i2),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(enum xfs_refc_domain, i1_domain)
		__field(xfs_agblock_t, i1_startblock)
		__field(xfs_extlen_t, i1_blockcount)
		__field(xfs_nlink_t, i1_refcount)
		__field(enum xfs_refc_domain, i2_domain)
		__field(xfs_agblock_t, i2_startblock)
		__field(xfs_extlen_t, i2_blockcount)
		__field(xfs_nlink_t, i2_refcount)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->i1_domain = i1->rc_domain;
		__entry->i1_startblock = i1->rc_startblock;
		__entry->i1_blockcount = i1->rc_blockcount;
		__entry->i1_refcount = i1->rc_refcount;
		__entry->i2_domain = i2->rc_domain;
		__entry->i2_startblock = i2->rc_startblock;
		__entry->i2_blockcount = i2->rc_blockcount;
		__entry->i2_refcount = i2->rc_refcount;
	),
	TP_printk("dev %d:%d %sno 0x%x dom %s gbno 0x%x fsbcount 0x%x refcount %u -- "
		  "dom %s gbno 0x%x fsbcount 0x%x refcount %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __print_symbolic(__entry->i1_domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->i1_startblock,
		  __entry->i1_blockcount,
		  __entry->i1_refcount,
		  __print_symbolic(__entry->i2_domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->i2_startblock,
		  __entry->i2_blockcount,
		  __entry->i2_refcount)
)

#define DEFINE_REFCOUNT_DOUBLE_EXTENT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_double_extent_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *i1, \
		 struct xfs_refcount_irec *i2), \
	TP_ARGS(cur, i1, i2))

/* double-rcext and an agbno tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_double_extent_at_class,
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *i1,
		 struct xfs_refcount_irec *i2, xfs_agblock_t gbno),
	TP_ARGS(cur, i1, i2, gbno),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(enum xfs_refc_domain, i1_domain)
		__field(xfs_agblock_t, i1_startblock)
		__field(xfs_extlen_t, i1_blockcount)
		__field(xfs_nlink_t, i1_refcount)
		__field(enum xfs_refc_domain, i2_domain)
		__field(xfs_agblock_t, i2_startblock)
		__field(xfs_extlen_t, i2_blockcount)
		__field(xfs_nlink_t, i2_refcount)
		__field(xfs_agblock_t, gbno)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->i1_domain = i1->rc_domain;
		__entry->i1_startblock = i1->rc_startblock;
		__entry->i1_blockcount = i1->rc_blockcount;
		__entry->i1_refcount = i1->rc_refcount;
		__entry->i2_domain = i2->rc_domain;
		__entry->i2_startblock = i2->rc_startblock;
		__entry->i2_blockcount = i2->rc_blockcount;
		__entry->i2_refcount = i2->rc_refcount;
		__entry->gbno = gbno;
	),
	TP_printk("dev %d:%d %sno 0x%x dom %s gbno 0x%x fsbcount 0x%x refcount %u -- "
		  "dom %s gbno 0x%x fsbcount 0x%x refcount %u @ gbno 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __print_symbolic(__entry->i1_domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->i1_startblock,
		  __entry->i1_blockcount,
		  __entry->i1_refcount,
		  __print_symbolic(__entry->i2_domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->i2_startblock,
		  __entry->i2_blockcount,
		  __entry->i2_refcount,
		  __entry->gbno)
)

#define DEFINE_REFCOUNT_DOUBLE_EXTENT_AT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_double_extent_at_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *i1, \
		struct xfs_refcount_irec *i2, xfs_agblock_t gbno), \
	TP_ARGS(cur, i1, i2, gbno))

/* triple-rcext tracepoint class */
DECLARE_EVENT_CLASS(xfs_refcount_triple_extent_class,
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *i1,
		struct xfs_refcount_irec *i2, struct xfs_refcount_irec *i3),
	TP_ARGS(cur, i1, i2, i3),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(enum xfs_refc_domain, i1_domain)
		__field(xfs_agblock_t, i1_startblock)
		__field(xfs_extlen_t, i1_blockcount)
		__field(xfs_nlink_t, i1_refcount)
		__field(enum xfs_refc_domain, i2_domain)
		__field(xfs_agblock_t, i2_startblock)
		__field(xfs_extlen_t, i2_blockcount)
		__field(xfs_nlink_t, i2_refcount)
		__field(enum xfs_refc_domain, i3_domain)
		__field(xfs_agblock_t, i3_startblock)
		__field(xfs_extlen_t, i3_blockcount)
		__field(xfs_nlink_t, i3_refcount)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__entry->type = cur->bc_group->xg_type;
		__entry->agno = cur->bc_group->xg_gno;
		__entry->i1_domain = i1->rc_domain;
		__entry->i1_startblock = i1->rc_startblock;
		__entry->i1_blockcount = i1->rc_blockcount;
		__entry->i1_refcount = i1->rc_refcount;
		__entry->i2_domain = i2->rc_domain;
		__entry->i2_startblock = i2->rc_startblock;
		__entry->i2_blockcount = i2->rc_blockcount;
		__entry->i2_refcount = i2->rc_refcount;
		__entry->i3_domain = i3->rc_domain;
		__entry->i3_startblock = i3->rc_startblock;
		__entry->i3_blockcount = i3->rc_blockcount;
		__entry->i3_refcount = i3->rc_refcount;
	),
	TP_printk("dev %d:%d %sno 0x%x dom %s gbno 0x%x fsbcount 0x%x refcount %u -- "
		  "dom %s gbno 0x%x fsbcount 0x%x refcount %u -- "
		  "dom %s gbno 0x%x fsbcount 0x%x refcount %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __print_symbolic(__entry->i1_domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->i1_startblock,
		  __entry->i1_blockcount,
		  __entry->i1_refcount,
		  __print_symbolic(__entry->i2_domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->i2_startblock,
		  __entry->i2_blockcount,
		  __entry->i2_refcount,
		  __print_symbolic(__entry->i3_domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->i3_startblock,
		  __entry->i3_blockcount,
		  __entry->i3_refcount)
);

#define DEFINE_REFCOUNT_TRIPLE_EXTENT_EVENT(name) \
DEFINE_EVENT(xfs_refcount_triple_extent_class, name, \
	TP_PROTO(struct xfs_btree_cur *cur, struct xfs_refcount_irec *i1, \
		struct xfs_refcount_irec *i2, struct xfs_refcount_irec *i3), \
	TP_ARGS(cur, i1, i2, i3))

/* refcount btree tracepoints */
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_get);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_update);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_insert);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_delete);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_insert_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_delete_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_update_error);

/* refcount adjustment tracepoints */
DEFINE_REFCOUNT_EVENT(xfs_refcount_increase);
DEFINE_REFCOUNT_EVENT(xfs_refcount_decrease);
DEFINE_REFCOUNT_EVENT(xfs_refcount_cow_increase);
DEFINE_REFCOUNT_EVENT(xfs_refcount_cow_decrease);
DEFINE_REFCOUNT_TRIPLE_EXTENT_EVENT(xfs_refcount_merge_center_extents);
DEFINE_REFCOUNT_EXTENT_EVENT(xfs_refcount_modify_extent);
DEFINE_REFCOUNT_EXTENT_AT_EVENT(xfs_refcount_split_extent);
DEFINE_REFCOUNT_DOUBLE_EXTENT_EVENT(xfs_refcount_merge_left_extent);
DEFINE_REFCOUNT_DOUBLE_EXTENT_EVENT(xfs_refcount_merge_right_extent);
DEFINE_REFCOUNT_DOUBLE_EXTENT_AT_EVENT(xfs_refcount_find_left_extent);
DEFINE_REFCOUNT_DOUBLE_EXTENT_AT_EVENT(xfs_refcount_find_right_extent);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_adjust_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_adjust_cow_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_merge_center_extents_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_modify_extent_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_split_extent_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_merge_left_extent_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_merge_right_extent_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_find_left_extent_error);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_find_right_extent_error);

/* reflink helpers */
DEFINE_REFCOUNT_EVENT(xfs_refcount_find_shared);
DEFINE_REFCOUNT_EVENT(xfs_refcount_find_shared_result);
DEFINE_BTREE_ERROR_EVENT(xfs_refcount_find_shared_error);

TRACE_DEFINE_ENUM(XFS_REFCOUNT_INCREASE);
TRACE_DEFINE_ENUM(XFS_REFCOUNT_DECREASE);
TRACE_DEFINE_ENUM(XFS_REFCOUNT_ALLOC_COW);
TRACE_DEFINE_ENUM(XFS_REFCOUNT_FREE_COW);

DECLARE_EVENT_CLASS(xfs_refcount_deferred_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_refcount_intent *refc),
	TP_ARGS(mp, refc),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(xfs_agnumber_t, agno)
		__field(int, op)
		__field(xfs_agblock_t, gbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->type = refc->ri_group->xg_type;
		__entry->agno = refc->ri_group->xg_gno;
		__entry->op = refc->ri_type;
		__entry->gbno = xfs_fsb_to_gbno(mp, refc->ri_startblock,
						   refc->ri_group->xg_type);
		__entry->len = refc->ri_blockcount;
	),
	TP_printk("dev %d:%d op %s %sno 0x%x gbno 0x%x fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->op, XFS_REFCOUNT_INTENT_STRINGS),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->agno,
		  __entry->gbno,
		  __entry->len)
);
#define DEFINE_REFCOUNT_DEFERRED_EVENT(name) \
DEFINE_EVENT(xfs_refcount_deferred_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_refcount_intent *refc), \
	TP_ARGS(mp, refc))
DEFINE_REFCOUNT_DEFERRED_EVENT(xfs_refcount_defer);
DEFINE_REFCOUNT_DEFERRED_EVENT(xfs_refcount_deferred);
DEFINE_REFCOUNT_DEFERRED_EVENT(xfs_refcount_finish_one_leftover);

/* simple inode-based error/%ip tracepoint class */
DECLARE_EVENT_CLASS(xfs_inode_error_class,
	TP_PROTO(struct xfs_inode *ip, int error, unsigned long caller_ip),
	TP_ARGS(ip, error, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, error)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->error = error;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx error %d caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->error,
		  (char *)__entry->caller_ip)
);

#define DEFINE_INODE_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_inode_error_class, name, \
	TP_PROTO(struct xfs_inode *ip, int error, \
		 unsigned long caller_ip), \
	TP_ARGS(ip, error, caller_ip))

/* reflink tracepoint classes */

/* two-file io tracepoint class */
DECLARE_EVENT_CLASS(xfs_double_io_class,
	TP_PROTO(struct xfs_inode *src, xfs_off_t soffset, xfs_off_t len,
		 struct xfs_inode *dest, xfs_off_t doffset),
	TP_ARGS(src, soffset, len, dest, doffset),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, src_ino)
		__field(loff_t, src_isize)
		__field(loff_t, src_disize)
		__field(loff_t, src_offset)
		__field(long long, len)
		__field(xfs_ino_t, dest_ino)
		__field(loff_t, dest_isize)
		__field(loff_t, dest_disize)
		__field(loff_t, dest_offset)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(src)->i_sb->s_dev;
		__entry->src_ino = src->i_ino;
		__entry->src_isize = VFS_I(src)->i_size;
		__entry->src_disize = src->i_disk_size;
		__entry->src_offset = soffset;
		__entry->len = len;
		__entry->dest_ino = dest->i_ino;
		__entry->dest_isize = VFS_I(dest)->i_size;
		__entry->dest_disize = dest->i_disk_size;
		__entry->dest_offset = doffset;
	),
	TP_printk("dev %d:%d bytecount 0x%llx "
		  "ino 0x%llx isize 0x%llx disize 0x%llx pos 0x%llx -> "
		  "ino 0x%llx isize 0x%llx disize 0x%llx pos 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->len,
		  __entry->src_ino,
		  __entry->src_isize,
		  __entry->src_disize,
		  __entry->src_offset,
		  __entry->dest_ino,
		  __entry->dest_isize,
		  __entry->dest_disize,
		  __entry->dest_offset)
)

#define DEFINE_DOUBLE_IO_EVENT(name)	\
DEFINE_EVENT(xfs_double_io_class, name,	\
	TP_PROTO(struct xfs_inode *src, xfs_off_t soffset, xfs_off_t len, \
		 struct xfs_inode *dest, xfs_off_t doffset), \
	TP_ARGS(src, soffset, len, dest, doffset))

/* inode/irec events */
DECLARE_EVENT_CLASS(xfs_inode_irec_class,
	TP_PROTO(struct xfs_inode *ip, struct xfs_bmbt_irec *irec),
	TP_ARGS(ip, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fileoff_t, lblk)
		__field(xfs_extlen_t, len)
		__field(xfs_fsblock_t, pblk)
		__field(int, state)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->lblk = irec->br_startoff;
		__entry->len = irec->br_blockcount;
		__entry->pblk = irec->br_startblock;
		__entry->state = irec->br_state;
	),
	TP_printk("dev %d:%d ino 0x%llx fileoff 0x%llx fsbcount 0x%x startblock 0x%llx st %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->lblk,
		  __entry->len,
		  __entry->pblk,
		  __entry->state)
);
#define DEFINE_INODE_IREC_EVENT(name) \
DEFINE_EVENT(xfs_inode_irec_class, name, \
	TP_PROTO(struct xfs_inode *ip, struct xfs_bmbt_irec *irec), \
	TP_ARGS(ip, irec))

/* inode iomap invalidation events */
DECLARE_EVENT_CLASS(xfs_wb_invalid_class,
	TP_PROTO(struct xfs_inode *ip, const struct iomap *iomap, unsigned int wpcseq, int whichfork),
	TP_ARGS(ip, iomap, wpcseq, whichfork),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(u64, addr)
		__field(loff_t, pos)
		__field(u64, len)
		__field(u16, type)
		__field(u16, flags)
		__field(u32, wpcseq)
		__field(u32, forkseq)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->addr = iomap->addr;
		__entry->pos = iomap->offset;
		__entry->len = iomap->length;
		__entry->type = iomap->type;
		__entry->flags = iomap->flags;
		__entry->wpcseq = wpcseq;
		__entry->forkseq = READ_ONCE(xfs_ifork_ptr(ip, whichfork)->if_seq);
	),
	TP_printk("dev %d:%d ino 0x%llx pos 0x%llx addr 0x%llx bytecount 0x%llx type 0x%x flags 0x%x wpcseq 0x%x forkseq 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->pos,
		  __entry->addr,
		  __entry->len,
		  __entry->type,
		  __entry->flags,
		  __entry->wpcseq,
		  __entry->forkseq)
);
#define DEFINE_WB_INVALID_EVENT(name) \
DEFINE_EVENT(xfs_wb_invalid_class, name, \
	TP_PROTO(struct xfs_inode *ip, const struct iomap *iomap, unsigned int wpcseq, int whichfork), \
	TP_ARGS(ip, iomap, wpcseq, whichfork))
DEFINE_WB_INVALID_EVENT(xfs_wb_cow_iomap_invalid);
DEFINE_WB_INVALID_EVENT(xfs_wb_data_iomap_invalid);

DECLARE_EVENT_CLASS(xfs_iomap_invalid_class,
	TP_PROTO(struct xfs_inode *ip, const struct iomap *iomap),
	TP_ARGS(ip, iomap),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(u64, addr)
		__field(loff_t, pos)
		__field(u64, len)
		__field(u64, validity_cookie)
		__field(u64, inodeseq)
		__field(u16, type)
		__field(u16, flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->addr = iomap->addr;
		__entry->pos = iomap->offset;
		__entry->len = iomap->length;
		__entry->validity_cookie = iomap->validity_cookie;
		__entry->type = iomap->type;
		__entry->flags = iomap->flags;
		__entry->inodeseq = xfs_iomap_inode_sequence(ip, iomap->flags);
	),
	TP_printk("dev %d:%d ino 0x%llx pos 0x%llx addr 0x%llx bytecount 0x%llx type 0x%x flags 0x%x validity_cookie 0x%llx inodeseq 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->pos,
		  __entry->addr,
		  __entry->len,
		  __entry->type,
		  __entry->flags,
		  __entry->validity_cookie,
		  __entry->inodeseq)
);
#define DEFINE_IOMAP_INVALID_EVENT(name) \
DEFINE_EVENT(xfs_iomap_invalid_class, name, \
	TP_PROTO(struct xfs_inode *ip, const struct iomap *iomap), \
	TP_ARGS(ip, iomap))
DEFINE_IOMAP_INVALID_EVENT(xfs_iomap_invalid);

/* refcount/reflink tracepoint definitions */

/* reflink tracepoints */
DEFINE_INODE_EVENT(xfs_reflink_set_inode_flag);
DEFINE_INODE_EVENT(xfs_reflink_unset_inode_flag);
DEFINE_ITRUNC_EVENT(xfs_reflink_update_inode_size);
TRACE_EVENT(xfs_reflink_remap_blocks,
	TP_PROTO(struct xfs_inode *src, xfs_fileoff_t soffset,
		 xfs_filblks_t len, struct xfs_inode *dest,
		 xfs_fileoff_t doffset),
	TP_ARGS(src, soffset, len, dest, doffset),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, src_ino)
		__field(xfs_fileoff_t, src_lblk)
		__field(xfs_filblks_t, len)
		__field(xfs_ino_t, dest_ino)
		__field(xfs_fileoff_t, dest_lblk)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(src)->i_sb->s_dev;
		__entry->src_ino = src->i_ino;
		__entry->src_lblk = soffset;
		__entry->len = len;
		__entry->dest_ino = dest->i_ino;
		__entry->dest_lblk = doffset;
	),
	TP_printk("dev %d:%d fsbcount 0x%llx "
		  "ino 0x%llx fileoff 0x%llx -> ino 0x%llx fileoff 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->len,
		  __entry->src_ino,
		  __entry->src_lblk,
		  __entry->dest_ino,
		  __entry->dest_lblk)
);
DEFINE_DOUBLE_IO_EVENT(xfs_reflink_remap_range);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_remap_range_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_set_inode_flag_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_update_inode_size_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_remap_blocks_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_remap_extent_error);
DEFINE_INODE_IREC_EVENT(xfs_reflink_remap_extent_src);
DEFINE_INODE_IREC_EVENT(xfs_reflink_remap_extent_dest);

/* dedupe tracepoints */
DEFINE_DOUBLE_IO_EVENT(xfs_reflink_compare_extents);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_compare_extents_error);

/* ioctl tracepoints */
TRACE_EVENT(xfs_ioctl_clone,
	TP_PROTO(struct inode *src, struct inode *dest),
	TP_ARGS(src, dest),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, src_ino)
		__field(loff_t, src_isize)
		__field(unsigned long, dest_ino)
		__field(loff_t, dest_isize)
	),
	TP_fast_assign(
		__entry->dev = src->i_sb->s_dev;
		__entry->src_ino = src->i_ino;
		__entry->src_isize = i_size_read(src);
		__entry->dest_ino = dest->i_ino;
		__entry->dest_isize = i_size_read(dest);
	),
	TP_printk("dev %d:%d ino 0x%lx isize 0x%llx -> ino 0x%lx isize 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->src_ino,
		  __entry->src_isize,
		  __entry->dest_ino,
		  __entry->dest_isize)
);

/* unshare tracepoints */
DEFINE_SIMPLE_IO_EVENT(xfs_reflink_unshare);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_unshare_error);

/* copy on write */
DEFINE_INODE_IREC_EVENT(xfs_reflink_trim_around_shared);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_found);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_enospc);
DEFINE_INODE_IREC_EVENT(xfs_reflink_convert_cow);

DEFINE_SIMPLE_IO_EVENT(xfs_reflink_cancel_cow_range);
DEFINE_SIMPLE_IO_EVENT(xfs_reflink_end_cow);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_remap_from);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_remap_to);
DEFINE_INODE_IREC_EVENT(xfs_reflink_cow_remap_skip);

DEFINE_INODE_ERROR_EVENT(xfs_reflink_cancel_cow_range_error);
DEFINE_INODE_ERROR_EVENT(xfs_reflink_end_cow_error);


DEFINE_INODE_IREC_EVENT(xfs_reflink_cancel_cow);

/* rmap swapext tracepoints */
DEFINE_INODE_IREC_EVENT(xfs_swap_extent_rmap_remap);
DEFINE_INODE_IREC_EVENT(xfs_swap_extent_rmap_remap_piece);
DEFINE_INODE_ERROR_EVENT(xfs_swap_extent_rmap_error);

/* fsmap traces */
TRACE_EVENT(xfs_fsmap_mapping,
	TP_PROTO(struct xfs_mount *mp, u32 keydev, xfs_agnumber_t agno,
		 const struct xfs_fsmap_irec *frec),
	TP_ARGS(mp, keydev, agno, frec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(dev_t, keydev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_daddr_t, start_daddr)
		__field(xfs_daddr_t, len_daddr)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->keydev = new_decode_dev(keydev);
		__entry->agno = agno;
		__entry->agbno = frec->rec_key;
		__entry->start_daddr = frec->start_daddr;
		__entry->len_daddr = frec->len_daddr;
		__entry->owner = frec->owner;
		__entry->offset = frec->offset;
		__entry->flags = frec->rm_flags;
	),
	TP_printk("dev %d:%d keydev %d:%d agno 0x%x gbno 0x%x start_daddr 0x%llx len_daddr 0x%llx owner 0x%llx fileoff 0x%llx flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  MAJOR(__entry->keydev), MINOR(__entry->keydev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->start_daddr,
		  __entry->len_daddr,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);

DECLARE_EVENT_CLASS(xfs_fsmap_group_key_class,
	TP_PROTO(struct xfs_mount *mp, u32 keydev, xfs_agnumber_t agno,
		 const struct xfs_rmap_irec *rmap),
	TP_ARGS(mp, keydev, agno, rmap),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(dev_t, keydev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->keydev = new_decode_dev(keydev);
		__entry->agno = agno;
		__entry->agbno = rmap->rm_startblock;
		__entry->owner = rmap->rm_owner;
		__entry->offset = rmap->rm_offset;
		__entry->flags = rmap->rm_flags;
	),
	TP_printk("dev %d:%d keydev %d:%d agno 0x%x startblock 0x%x owner 0x%llx fileoff 0x%llx flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  MAJOR(__entry->keydev), MINOR(__entry->keydev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
)
#define DEFINE_FSMAP_GROUP_KEY_EVENT(name) \
DEFINE_EVENT(xfs_fsmap_group_key_class, name, \
	TP_PROTO(struct xfs_mount *mp, u32 keydev, xfs_agnumber_t agno, \
		 const struct xfs_rmap_irec *rmap), \
	TP_ARGS(mp, keydev, agno, rmap))
DEFINE_FSMAP_GROUP_KEY_EVENT(xfs_fsmap_low_group_key);
DEFINE_FSMAP_GROUP_KEY_EVENT(xfs_fsmap_high_group_key);

DECLARE_EVENT_CLASS(xfs_fsmap_linear_key_class,
	TP_PROTO(struct xfs_mount *mp, u32 keydev, xfs_fsblock_t bno),
	TP_ARGS(mp, keydev, bno),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(dev_t, keydev)
		__field(xfs_fsblock_t, bno)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->keydev = new_decode_dev(keydev);
		__entry->bno = bno;
	),
	TP_printk("dev %d:%d keydev %d:%d bno 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  MAJOR(__entry->keydev), MINOR(__entry->keydev),
		  __entry->bno)
)
#define DEFINE_FSMAP_LINEAR_KEY_EVENT(name) \
DEFINE_EVENT(xfs_fsmap_linear_key_class, name, \
	TP_PROTO(struct xfs_mount *mp, u32 keydev, uint64_t bno), \
	TP_ARGS(mp, keydev, bno))
DEFINE_FSMAP_LINEAR_KEY_EVENT(xfs_fsmap_low_linear_key);
DEFINE_FSMAP_LINEAR_KEY_EVENT(xfs_fsmap_high_linear_key);

DECLARE_EVENT_CLASS(xfs_getfsmap_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_fsmap *fsmap),
	TP_ARGS(mp, fsmap),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(dev_t, keydev)
		__field(xfs_daddr_t, block)
		__field(xfs_daddr_t, len)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(uint64_t, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->keydev = new_decode_dev(fsmap->fmr_device);
		__entry->block = fsmap->fmr_physical;
		__entry->len = fsmap->fmr_length;
		__entry->owner = fsmap->fmr_owner;
		__entry->offset = fsmap->fmr_offset;
		__entry->flags = fsmap->fmr_flags;
	),
	TP_printk("dev %d:%d keydev %d:%d daddr 0x%llx bbcount 0x%llx owner 0x%llx fileoff_daddr 0x%llx flags 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  MAJOR(__entry->keydev), MINOR(__entry->keydev),
		  __entry->block,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
)
#define DEFINE_GETFSMAP_EVENT(name) \
DEFINE_EVENT(xfs_getfsmap_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_fsmap *fsmap), \
	TP_ARGS(mp, fsmap))
DEFINE_GETFSMAP_EVENT(xfs_getfsmap_low_key);
DEFINE_GETFSMAP_EVENT(xfs_getfsmap_high_key);
DEFINE_GETFSMAP_EVENT(xfs_getfsmap_mapping);

DECLARE_EVENT_CLASS(xfs_trans_resv_class,
	TP_PROTO(struct xfs_mount *mp, unsigned int type,
		 struct xfs_trans_res *res),
	TP_ARGS(mp, type, res),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, type)
		__field(uint, logres)
		__field(int, logcount)
		__field(int, logflags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->type = type;
		__entry->logres = res->tr_logres;
		__entry->logcount = res->tr_logcount;
		__entry->logflags = res->tr_logflags;
	),
	TP_printk("dev %d:%d type %d logres %u logcount %d flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->logres,
		  __entry->logcount,
		  __entry->logflags)
)

#define DEFINE_TRANS_RESV_EVENT(name) \
DEFINE_EVENT(xfs_trans_resv_class, name, \
	TP_PROTO(struct xfs_mount *mp, unsigned int type, \
		 struct xfs_trans_res *res), \
	TP_ARGS(mp, type, res))
DEFINE_TRANS_RESV_EVENT(xfs_trans_resv_calc);
DEFINE_TRANS_RESV_EVENT(xfs_trans_resv_calc_minlogsize);

TRACE_EVENT(xfs_log_get_max_trans_res,
	TP_PROTO(struct xfs_mount *mp, const struct xfs_trans_res *res),
	TP_ARGS(mp, res),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(uint, logres)
		__field(int, logcount)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->logres = res->tr_logres;
		__entry->logcount = res->tr_logcount;
	),
	TP_printk("dev %d:%d logres %u logcount %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->logres,
		  __entry->logcount)
);

DECLARE_EVENT_CLASS(xfs_trans_class,
	TP_PROTO(struct xfs_trans *tp, unsigned long caller_ip),
	TP_ARGS(tp, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(uint32_t, tid)
		__field(uint32_t, flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = tp->t_mountp->m_super->s_dev;
		__entry->tid = 0;
		if (tp->t_ticket)
			__entry->tid = tp->t_ticket->t_tid;
		__entry->flags = tp->t_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d trans %x flags 0x%x caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tid,
		  __entry->flags,
		  (char *)__entry->caller_ip)
)

#define DEFINE_TRANS_EVENT(name) \
DEFINE_EVENT(xfs_trans_class, name, \
	TP_PROTO(struct xfs_trans *tp, unsigned long caller_ip), \
	TP_ARGS(tp, caller_ip))
DEFINE_TRANS_EVENT(xfs_trans_alloc);
DEFINE_TRANS_EVENT(xfs_trans_cancel);
DEFINE_TRANS_EVENT(xfs_trans_commit);
DEFINE_TRANS_EVENT(xfs_trans_dup);
DEFINE_TRANS_EVENT(xfs_trans_free);
DEFINE_TRANS_EVENT(xfs_trans_roll);
DEFINE_TRANS_EVENT(xfs_trans_add_item);
DEFINE_TRANS_EVENT(xfs_trans_commit_items);
DEFINE_TRANS_EVENT(xfs_trans_free_items);

TRACE_EVENT(xfs_iunlink_update_bucket,
	TP_PROTO(const struct xfs_perag *pag, unsigned int bucket,
		 xfs_agino_t old_ptr, xfs_agino_t new_ptr),
	TP_ARGS(pag, bucket, old_ptr, new_ptr),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(unsigned int, bucket)
		__field(xfs_agino_t, old_ptr)
		__field(xfs_agino_t, new_ptr)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
		__entry->bucket = bucket;
		__entry->old_ptr = old_ptr;
		__entry->new_ptr = new_ptr;
	),
	TP_printk("dev %d:%d agno 0x%x bucket %u old 0x%x new 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->bucket,
		  __entry->old_ptr,
		  __entry->new_ptr)
);

TRACE_EVENT(xfs_iunlink_update_dinode,
	TP_PROTO(const struct xfs_iunlink_item *iup, xfs_agino_t old_ptr),
	TP_ARGS(iup, old_ptr),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
		__field(xfs_agino_t, old_ptr)
		__field(xfs_agino_t, new_ptr)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(iup->pag)->m_super->s_dev;
		__entry->agno = pag_agno(iup->pag);
		__entry->agino =
			XFS_INO_TO_AGINO(iup->ip->i_mount, iup->ip->i_ino);
		__entry->old_ptr = old_ptr;
		__entry->new_ptr = iup->next_agino;
	),
	TP_printk("dev %d:%d agno 0x%x agino 0x%x old 0x%x new 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agino,
		  __entry->old_ptr,
		  __entry->new_ptr)
);

TRACE_EVENT(xfs_iunlink_reload_next,
	TP_PROTO(struct xfs_inode *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
		__field(xfs_agino_t, prev_agino)
		__field(xfs_agino_t, next_agino)
	),
	TP_fast_assign(
		__entry->dev = ip->i_mount->m_super->s_dev;
		__entry->agno = XFS_INO_TO_AGNO(ip->i_mount, ip->i_ino);
		__entry->agino = XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino);
		__entry->prev_agino = ip->i_prev_unlinked;
		__entry->next_agino = ip->i_next_unlinked;
	),
	TP_printk("dev %d:%d agno 0x%x agino 0x%x prev_unlinked 0x%x next_unlinked 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agino,
		  __entry->prev_agino,
		  __entry->next_agino)
);

TRACE_EVENT(xfs_inode_reload_unlinked_bucket,
	TP_PROTO(struct xfs_inode *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
	),
	TP_fast_assign(
		__entry->dev = ip->i_mount->m_super->s_dev;
		__entry->agno = XFS_INO_TO_AGNO(ip->i_mount, ip->i_ino);
		__entry->agino = XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino);
	),
	TP_printk("dev %d:%d agno 0x%x agino 0x%x bucket %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agino,
		  __entry->agino % XFS_AGI_UNLINKED_BUCKETS)
);

DECLARE_EVENT_CLASS(xfs_ag_inode_class,
	TP_PROTO(struct xfs_inode *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->agno = XFS_INO_TO_AGNO(ip->i_mount, ip->i_ino);
		__entry->agino = XFS_INO_TO_AGINO(ip->i_mount, ip->i_ino);
	),
	TP_printk("dev %d:%d agno 0x%x agino 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno, __entry->agino)
)

#define DEFINE_AGINODE_EVENT(name) \
DEFINE_EVENT(xfs_ag_inode_class, name, \
	TP_PROTO(struct xfs_inode *ip), \
	TP_ARGS(ip))
DEFINE_AGINODE_EVENT(xfs_iunlink);
DEFINE_AGINODE_EVENT(xfs_iunlink_remove);

DECLARE_EVENT_CLASS(xfs_fs_corrupt_class,
	TP_PROTO(struct xfs_mount *mp, unsigned int flags),
	TP_ARGS(mp, flags),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->flags)
);
#define DEFINE_FS_CORRUPT_EVENT(name)	\
DEFINE_EVENT(xfs_fs_corrupt_class, name,	\
	TP_PROTO(struct xfs_mount *mp, unsigned int flags), \
	TP_ARGS(mp, flags))
DEFINE_FS_CORRUPT_EVENT(xfs_fs_mark_sick);
DEFINE_FS_CORRUPT_EVENT(xfs_fs_mark_corrupt);
DEFINE_FS_CORRUPT_EVENT(xfs_fs_mark_healthy);
DEFINE_FS_CORRUPT_EVENT(xfs_fs_unfixed_corruption);

DECLARE_EVENT_CLASS(xfs_group_corrupt_class,
	TP_PROTO(const struct xfs_group *xg, unsigned int flags),
	TP_ARGS(xg, flags),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(uint32_t, index)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = xg->xg_mount->m_super->s_dev;
		__entry->type = xg->xg_type;
		__entry->index = xg->xg_gno;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d %sno 0x%x flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->index, __entry->flags)
);
#define DEFINE_GROUP_CORRUPT_EVENT(name)	\
DEFINE_EVENT(xfs_group_corrupt_class, name,	\
	TP_PROTO(const struct xfs_group *xg, unsigned int flags), \
	TP_ARGS(xg, flags))
DEFINE_GROUP_CORRUPT_EVENT(xfs_group_mark_sick);
DEFINE_GROUP_CORRUPT_EVENT(xfs_group_mark_corrupt);
DEFINE_GROUP_CORRUPT_EVENT(xfs_group_mark_healthy);
DEFINE_GROUP_CORRUPT_EVENT(xfs_group_unfixed_corruption);

DECLARE_EVENT_CLASS(xfs_inode_corrupt_class,
	TP_PROTO(struct xfs_inode *ip, unsigned int flags),
	TP_ARGS(ip, flags),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = ip->i_mount->m_super->s_dev;
		__entry->ino = ip->i_ino;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d ino 0x%llx flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino, __entry->flags)
);
#define DEFINE_INODE_CORRUPT_EVENT(name)	\
DEFINE_EVENT(xfs_inode_corrupt_class, name,	\
	TP_PROTO(struct xfs_inode *ip, unsigned int flags), \
	TP_ARGS(ip, flags))
DEFINE_INODE_CORRUPT_EVENT(xfs_inode_mark_sick);
DEFINE_INODE_CORRUPT_EVENT(xfs_inode_mark_corrupt);
DEFINE_INODE_CORRUPT_EVENT(xfs_inode_mark_healthy);
DEFINE_INODE_CORRUPT_EVENT(xfs_inode_unfixed_corruption);

TRACE_EVENT(xfs_iwalk_ag_rec,
	TP_PROTO(const struct xfs_perag *pag, \
		 struct xfs_inobt_rec_incore *irec),
	TP_ARGS(pag, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, startino)
		__field(uint64_t, freemask)
	),
	TP_fast_assign(
		__entry->dev = pag_mount(pag)->m_super->s_dev;
		__entry->agno = pag_agno(pag);
		__entry->startino = irec->ir_startino;
		__entry->freemask = irec->ir_free;
	),
	TP_printk("dev %d:%d agno 0x%x startino 0x%x freemask 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->agno,
		  __entry->startino, __entry->freemask)
)

TRACE_EVENT(xfs_pwork_init,
	TP_PROTO(struct xfs_mount *mp, unsigned int nr_threads, pid_t pid),
	TP_ARGS(mp, nr_threads, pid),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, nr_threads)
		__field(pid_t, pid)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->nr_threads = nr_threads;
		__entry->pid = pid;
	),
	TP_printk("dev %d:%d nr_threads %u pid %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->nr_threads, __entry->pid)
)

TRACE_EVENT(xfs_check_new_dalign,
	TP_PROTO(struct xfs_mount *mp, int new_dalign, xfs_ino_t calc_rootino),
	TP_ARGS(mp, new_dalign, calc_rootino),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, new_dalign)
		__field(xfs_ino_t, sb_rootino)
		__field(xfs_ino_t, calc_rootino)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->new_dalign = new_dalign;
		__entry->sb_rootino = mp->m_sb.sb_rootino;
		__entry->calc_rootino = calc_rootino;
	),
	TP_printk("dev %d:%d new_dalign %d sb_rootino 0x%llx calc_rootino 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->new_dalign, __entry->sb_rootino,
		  __entry->calc_rootino)
)

TRACE_EVENT(xfs_btree_commit_afakeroot,
	TP_PROTO(struct xfs_btree_cur *cur),
	TP_ARGS(cur),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, cur->bc_ops->name)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(unsigned int, levels)
		__field(unsigned int, blocks)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__assign_str(name);
		__entry->agno = cur->bc_group->xg_gno;
		__entry->agbno = cur->bc_ag.afake->af_root;
		__entry->levels = cur->bc_ag.afake->af_levels;
		__entry->blocks = cur->bc_ag.afake->af_blocks;
	),
	TP_printk("dev %d:%d %sbt agno 0x%x levels %u blocks %u root %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->agno,
		  __entry->levels,
		  __entry->blocks,
		  __entry->agbno)
)

TRACE_EVENT(xfs_btree_commit_ifakeroot,
	TP_PROTO(struct xfs_btree_cur *cur),
	TP_ARGS(cur),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, cur->bc_ops->name)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, agino)
		__field(unsigned int, levels)
		__field(unsigned int, blocks)
		__field(int, whichfork)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__assign_str(name);
		__entry->agno = XFS_INO_TO_AGNO(cur->bc_mp,
					cur->bc_ino.ip->i_ino);
		__entry->agino = XFS_INO_TO_AGINO(cur->bc_mp,
					cur->bc_ino.ip->i_ino);
		__entry->levels = cur->bc_ino.ifake->if_levels;
		__entry->blocks = cur->bc_ino.ifake->if_blocks;
		__entry->whichfork = cur->bc_ino.whichfork;
	),
	TP_printk("dev %d:%d %sbt agno 0x%x agino 0x%x whichfork %s levels %u blocks %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->agno,
		  __entry->agino,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __entry->levels,
		  __entry->blocks)
)

TRACE_EVENT(xfs_btree_bload_level_geometry,
	TP_PROTO(struct xfs_btree_cur *cur, unsigned int level,
		 uint64_t nr_this_level, unsigned int nr_per_block,
		 unsigned int desired_npb, uint64_t blocks,
		 uint64_t blocks_with_extra),
	TP_ARGS(cur, level, nr_this_level, nr_per_block, desired_npb, blocks,
		blocks_with_extra),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, cur->bc_ops->name)
		__field(unsigned int, level)
		__field(unsigned int, nlevels)
		__field(uint64_t, nr_this_level)
		__field(unsigned int, nr_per_block)
		__field(unsigned int, desired_npb)
		__field(unsigned long long, blocks)
		__field(unsigned long long, blocks_with_extra)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__assign_str(name);
		__entry->level = level;
		__entry->nlevels = cur->bc_nlevels;
		__entry->nr_this_level = nr_this_level;
		__entry->nr_per_block = nr_per_block;
		__entry->desired_npb = desired_npb;
		__entry->blocks = blocks;
		__entry->blocks_with_extra = blocks_with_extra;
	),
	TP_printk("dev %d:%d %sbt level %u/%u nr_this_level %llu nr_per_block %u desired_npb %u blocks %llu blocks_with_extra %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->level,
		  __entry->nlevels,
		  __entry->nr_this_level,
		  __entry->nr_per_block,
		  __entry->desired_npb,
		  __entry->blocks,
		  __entry->blocks_with_extra)
)

TRACE_EVENT(xfs_btree_bload_block,
	TP_PROTO(struct xfs_btree_cur *cur, unsigned int level,
		 uint64_t block_idx, uint64_t nr_blocks,
		 union xfs_btree_ptr *ptr, unsigned int nr_records),
	TP_ARGS(cur, level, block_idx, nr_blocks, ptr, nr_records),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__string(name, cur->bc_ops->name)
		__field(unsigned int, level)
		__field(unsigned long long, block_idx)
		__field(unsigned long long, nr_blocks)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(unsigned int, nr_records)
	),
	TP_fast_assign(
		__entry->dev = cur->bc_mp->m_super->s_dev;
		__assign_str(name);
		__entry->level = level;
		__entry->block_idx = block_idx;
		__entry->nr_blocks = nr_blocks;
		if (cur->bc_ops->ptr_len == XFS_BTREE_LONG_PTR_LEN) {
			xfs_fsblock_t	fsb = be64_to_cpu(ptr->l);

			__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsb);
			__entry->agbno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsb);
		} else {
			__entry->agno = cur->bc_group->xg_gno;
			__entry->agbno = be32_to_cpu(ptr->s);
		}
		__entry->nr_records = nr_records;
	),
	TP_printk("dev %d:%d %sbt level %u block %llu/%llu agno 0x%x agbno 0x%x recs %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __get_str(name),
		  __entry->level,
		  __entry->block_idx,
		  __entry->nr_blocks,
		  __entry->agno,
		  __entry->agbno,
		  __entry->nr_records)
)

DECLARE_EVENT_CLASS(xfs_timestamp_range_class,
	TP_PROTO(struct xfs_mount *mp, time64_t min, time64_t max),
	TP_ARGS(mp, min, max),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(long long, min)
		__field(long long, max)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->min = min;
		__entry->max = max;
	),
	TP_printk("dev %d:%d min %lld max %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->min,
		  __entry->max)
)

#define DEFINE_TIMESTAMP_RANGE_EVENT(name) \
DEFINE_EVENT(xfs_timestamp_range_class, name, \
	TP_PROTO(struct xfs_mount *mp, long long min, long long max), \
	TP_ARGS(mp, min, max))
DEFINE_TIMESTAMP_RANGE_EVENT(xfs_inode_timestamp_range);
DEFINE_TIMESTAMP_RANGE_EVENT(xfs_quota_expiry_range);

DECLARE_EVENT_CLASS(xfs_icwalk_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_icwalk *icw,
		 unsigned long caller_ip),
	TP_ARGS(mp, icw, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(__u32, flags)
		__field(uint32_t, uid)
		__field(uint32_t, gid)
		__field(prid_t, prid)
		__field(__u64, min_file_size)
		__field(long, scan_limit)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->flags = icw ? icw->icw_flags : 0;
		__entry->uid = icw ? from_kuid(mp->m_super->s_user_ns,
						icw->icw_uid) : 0;
		__entry->gid = icw ? from_kgid(mp->m_super->s_user_ns,
						icw->icw_gid) : 0;
		__entry->prid = icw ? icw->icw_prid : 0;
		__entry->min_file_size = icw ? icw->icw_min_file_size : 0;
		__entry->scan_limit = icw ? icw->icw_scan_limit : 0;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d flags 0x%x uid %u gid %u prid %u minsize %llu scan_limit %ld caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->flags,
		  __entry->uid,
		  __entry->gid,
		  __entry->prid,
		  __entry->min_file_size,
		  __entry->scan_limit,
		  (char *)__entry->caller_ip)
);
#define DEFINE_ICWALK_EVENT(name)	\
DEFINE_EVENT(xfs_icwalk_class, name,	\
	TP_PROTO(struct xfs_mount *mp, struct xfs_icwalk *icw, \
		 unsigned long caller_ip), \
	TP_ARGS(mp, icw, caller_ip))
DEFINE_ICWALK_EVENT(xfs_ioc_free_eofblocks);
DEFINE_ICWALK_EVENT(xfs_blockgc_free_space);

TRACE_DEFINE_ENUM(XLOG_STATE_ACTIVE);
TRACE_DEFINE_ENUM(XLOG_STATE_WANT_SYNC);
TRACE_DEFINE_ENUM(XLOG_STATE_SYNCING);
TRACE_DEFINE_ENUM(XLOG_STATE_DONE_SYNC);
TRACE_DEFINE_ENUM(XLOG_STATE_CALLBACK);
TRACE_DEFINE_ENUM(XLOG_STATE_DIRTY);

DECLARE_EVENT_CLASS(xlog_iclog_class,
	TP_PROTO(struct xlog_in_core *iclog, unsigned long caller_ip),
	TP_ARGS(iclog, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(uint32_t, state)
		__field(int32_t, refcount)
		__field(uint32_t, offset)
		__field(uint32_t, flags)
		__field(unsigned long long, lsn)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = iclog->ic_log->l_mp->m_super->s_dev;
		__entry->state = iclog->ic_state;
		__entry->refcount = atomic_read(&iclog->ic_refcnt);
		__entry->offset = iclog->ic_offset;
		__entry->flags = iclog->ic_flags;
		__entry->lsn = be64_to_cpu(iclog->ic_header.h_lsn);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d state %s refcnt %d offset %u lsn 0x%llx flags %s caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->state, XLOG_STATE_STRINGS),
		  __entry->refcount,
		  __entry->offset,
		  __entry->lsn,
		  __print_flags(__entry->flags, "|", XLOG_ICL_STRINGS),
		  (char *)__entry->caller_ip)

);

#define DEFINE_ICLOG_EVENT(name)	\
DEFINE_EVENT(xlog_iclog_class, name,	\
	TP_PROTO(struct xlog_in_core *iclog, unsigned long caller_ip), \
	TP_ARGS(iclog, caller_ip))

DEFINE_ICLOG_EVENT(xlog_iclog_activate);
DEFINE_ICLOG_EVENT(xlog_iclog_clean);
DEFINE_ICLOG_EVENT(xlog_iclog_callback);
DEFINE_ICLOG_EVENT(xlog_iclog_callbacks_start);
DEFINE_ICLOG_EVENT(xlog_iclog_callbacks_done);
DEFINE_ICLOG_EVENT(xlog_iclog_force);
DEFINE_ICLOG_EVENT(xlog_iclog_force_lsn);
DEFINE_ICLOG_EVENT(xlog_iclog_get_space);
DEFINE_ICLOG_EVENT(xlog_iclog_release);
DEFINE_ICLOG_EVENT(xlog_iclog_switch);
DEFINE_ICLOG_EVENT(xlog_iclog_sync);
DEFINE_ICLOG_EVENT(xlog_iclog_syncing);
DEFINE_ICLOG_EVENT(xlog_iclog_sync_done);
DEFINE_ICLOG_EVENT(xlog_iclog_want_sync);
DEFINE_ICLOG_EVENT(xlog_iclog_wait_on);
DEFINE_ICLOG_EVENT(xlog_iclog_write);

TRACE_DEFINE_ENUM(XFS_DAS_UNINIT);
TRACE_DEFINE_ENUM(XFS_DAS_SF_ADD);
TRACE_DEFINE_ENUM(XFS_DAS_SF_REMOVE);
TRACE_DEFINE_ENUM(XFS_DAS_LEAF_ADD);
TRACE_DEFINE_ENUM(XFS_DAS_LEAF_REMOVE);
TRACE_DEFINE_ENUM(XFS_DAS_NODE_ADD);
TRACE_DEFINE_ENUM(XFS_DAS_NODE_REMOVE);
TRACE_DEFINE_ENUM(XFS_DAS_LEAF_SET_RMT);
TRACE_DEFINE_ENUM(XFS_DAS_LEAF_ALLOC_RMT);
TRACE_DEFINE_ENUM(XFS_DAS_LEAF_REPLACE);
TRACE_DEFINE_ENUM(XFS_DAS_LEAF_REMOVE_OLD);
TRACE_DEFINE_ENUM(XFS_DAS_LEAF_REMOVE_RMT);
TRACE_DEFINE_ENUM(XFS_DAS_LEAF_REMOVE_ATTR);
TRACE_DEFINE_ENUM(XFS_DAS_NODE_SET_RMT);
TRACE_DEFINE_ENUM(XFS_DAS_NODE_ALLOC_RMT);
TRACE_DEFINE_ENUM(XFS_DAS_NODE_REPLACE);
TRACE_DEFINE_ENUM(XFS_DAS_NODE_REMOVE_OLD);
TRACE_DEFINE_ENUM(XFS_DAS_NODE_REMOVE_RMT);
TRACE_DEFINE_ENUM(XFS_DAS_NODE_REMOVE_ATTR);
TRACE_DEFINE_ENUM(XFS_DAS_DONE);

DECLARE_EVENT_CLASS(xfs_das_state_class,
	TP_PROTO(int das, struct xfs_inode *ip),
	TP_ARGS(das, ip),
	TP_STRUCT__entry(
		__field(int, das)
		__field(xfs_ino_t, ino)
	),
	TP_fast_assign(
		__entry->das = das;
		__entry->ino = ip->i_ino;
	),
	TP_printk("state change %s ino 0x%llx",
		  __print_symbolic(__entry->das, XFS_DAS_STRINGS),
		  __entry->ino)
)

#define DEFINE_DAS_STATE_EVENT(name) \
DEFINE_EVENT(xfs_das_state_class, name, \
	TP_PROTO(int das, struct xfs_inode *ip), \
	TP_ARGS(das, ip))
DEFINE_DAS_STATE_EVENT(xfs_attr_sf_addname_return);
DEFINE_DAS_STATE_EVENT(xfs_attr_set_iter_return);
DEFINE_DAS_STATE_EVENT(xfs_attr_leaf_addname_return);
DEFINE_DAS_STATE_EVENT(xfs_attr_node_addname_return);
DEFINE_DAS_STATE_EVENT(xfs_attr_remove_iter_return);
DEFINE_DAS_STATE_EVENT(xfs_attr_rmtval_alloc);
DEFINE_DAS_STATE_EVENT(xfs_attr_rmtval_remove_return);
DEFINE_DAS_STATE_EVENT(xfs_attr_defer_add);


TRACE_EVENT(xfs_force_shutdown,
	TP_PROTO(struct xfs_mount *mp, int ptag, int flags, const char *fname,
		 int line_num),
	TP_ARGS(mp, ptag, flags, fname, line_num),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, ptag)
		__field(int, flags)
		__string(fname, fname)
		__field(int, line_num)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->ptag = ptag;
		__entry->flags = flags;
		__assign_str(fname);
		__entry->line_num = line_num;
	),
	TP_printk("dev %d:%d tag %s flags %s file %s line_num %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		__print_flags(__entry->ptag, "|", XFS_PTAG_STRINGS),
		__print_flags(__entry->flags, "|", XFS_SHUTDOWN_STRINGS),
		__get_str(fname),
		__entry->line_num)
);

#ifdef CONFIG_XFS_DRAIN_INTENTS
DECLARE_EVENT_CLASS(xfs_group_intents_class,
	TP_PROTO(const struct xfs_group *xg, void *caller_ip),
	TP_ARGS(xg, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_group_type, type)
		__field(uint32_t, index)
		__field(long, nr_intents)
		__field(void *, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = xg->xg_mount->m_super->s_dev;
		__entry->type = xg->xg_type;
		__entry->index = xg->xg_gno;
		__entry->nr_intents =
			atomic_read(&xg->xg_intents_drain.dr_count);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d %sno 0x%x intents %ld caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XG_TYPE_STRINGS),
		  __entry->index,
		  __entry->nr_intents,
		  __entry->caller_ip)
);

#define DEFINE_GROUP_INTENTS_EVENT(name)	\
DEFINE_EVENT(xfs_group_intents_class, name,					\
	TP_PROTO(const struct xfs_group *xg, void *caller_ip), \
	TP_ARGS(xg, caller_ip))
DEFINE_GROUP_INTENTS_EVENT(xfs_group_intent_hold);
DEFINE_GROUP_INTENTS_EVENT(xfs_group_intent_rele);
DEFINE_GROUP_INTENTS_EVENT(xfs_group_wait_intents);

#endif /* CONFIG_XFS_DRAIN_INTENTS */

#ifdef CONFIG_XFS_MEMORY_BUFS
TRACE_EVENT(xmbuf_create,
	TP_PROTO(struct xfs_buftarg *btp),
	TP_ARGS(btp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, ino)
		__array(char, pathname, MAXNAMELEN)
	),
	TP_fast_assign(
		char		*path;
		struct file	*file = btp->bt_file;

		__entry->dev = btp->bt_mount->m_super->s_dev;
		__entry->ino = file_inode(file)->i_ino;
		path = file_path(file, __entry->pathname, MAXNAMELEN);
		if (IS_ERR(path))
			strncpy(__entry->pathname, "(unknown)",
					sizeof(__entry->pathname));
	),
	TP_printk("dev %d:%d xmino 0x%lx path '%s'",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->pathname)
);

TRACE_EVENT(xmbuf_free,
	TP_PROTO(struct xfs_buftarg *btp),
	TP_ARGS(btp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, ino)
		__field(unsigned long long, bytes)
		__field(loff_t, size)
	),
	TP_fast_assign(
		struct file	*file = btp->bt_file;
		struct inode	*inode = file_inode(file);

		__entry->dev = btp->bt_mount->m_super->s_dev;
		__entry->size = i_size_read(inode);
		__entry->bytes = (inode->i_blocks << SECTOR_SHIFT) + inode->i_bytes;
		__entry->ino = inode->i_ino;
	),
	TP_printk("dev %d:%d xmino 0x%lx mem_bytes 0x%llx isize 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->bytes,
		  __entry->size)
);
#endif /* CONFIG_XFS_MEMORY_BUFS */

#ifdef CONFIG_XFS_BTREE_IN_MEM
TRACE_EVENT(xfbtree_init,
	TP_PROTO(struct xfs_mount *mp, struct xfbtree *xfbt,
		 const struct xfs_btree_ops *ops),
	TP_ARGS(mp, xfbt, ops),
	TP_STRUCT__entry(
		__field(const void *, btree_ops)
		__field(unsigned long, xfino)
		__field(unsigned int, leaf_mxr)
		__field(unsigned int, leaf_mnr)
		__field(unsigned int, node_mxr)
		__field(unsigned int, node_mnr)
		__field(unsigned long long, owner)
	),
	TP_fast_assign(
		__entry->btree_ops = ops;
		__entry->xfino = file_inode(xfbt->target->bt_file)->i_ino;
		__entry->leaf_mxr = xfbt->maxrecs[0];
		__entry->node_mxr = xfbt->maxrecs[1];
		__entry->leaf_mnr = xfbt->minrecs[0];
		__entry->node_mnr = xfbt->minrecs[1];
		__entry->owner = xfbt->owner;
	),
	TP_printk("xfino 0x%lx btree_ops %pS owner 0x%llx leaf_mxr %u leaf_mnr %u node_mxr %u node_mnr %u",
		  __entry->xfino,
		  __entry->btree_ops,
		  __entry->owner,
		  __entry->leaf_mxr,
		  __entry->leaf_mnr,
		  __entry->node_mxr,
		  __entry->node_mnr)
);

DECLARE_EVENT_CLASS(xfbtree_buf_class,
	TP_PROTO(struct xfbtree *xfbt, struct xfs_buf *bp),
	TP_ARGS(xfbt, bp),
	TP_STRUCT__entry(
		__field(unsigned long, xfino)
		__field(xfs_daddr_t, bno)
		__field(int, nblks)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned int, lockval)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->xfino = file_inode(xfbt->target->bt_file)->i_ino;
		__entry->bno = xfs_buf_daddr(bp);
		__entry->nblks = bp->b_length;
		__entry->hold = bp->b_hold;
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = bp->b_sema.count;
		__entry->flags = bp->b_flags;
	),
	TP_printk("xfino 0x%lx daddr 0x%llx bbcount 0x%x hold %d pincount %d lock %d flags %s",
		  __entry->xfino,
		  (unsigned long long)__entry->bno,
		  __entry->nblks,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS))
)

#define DEFINE_XFBTREE_BUF_EVENT(name) \
DEFINE_EVENT(xfbtree_buf_class, name, \
	TP_PROTO(struct xfbtree *xfbt, struct xfs_buf *bp), \
	TP_ARGS(xfbt, bp))
DEFINE_XFBTREE_BUF_EVENT(xfbtree_create_root_buf);
DEFINE_XFBTREE_BUF_EVENT(xfbtree_trans_commit_buf);
DEFINE_XFBTREE_BUF_EVENT(xfbtree_trans_cancel_buf);

DECLARE_EVENT_CLASS(xfbtree_freesp_class,
	TP_PROTO(struct xfbtree *xfbt, struct xfs_btree_cur *cur,
		 xfs_fileoff_t fileoff),
	TP_ARGS(xfbt, cur, fileoff),
	TP_STRUCT__entry(
		__field(unsigned long, xfino)
		__string(btname, cur->bc_ops->name)
		__field(int, nlevels)
		__field(xfs_fileoff_t, fileoff)
	),
	TP_fast_assign(
		__entry->xfino = file_inode(xfbt->target->bt_file)->i_ino;
		__assign_str(btname);
		__entry->nlevels = cur->bc_nlevels;
		__entry->fileoff = fileoff;
	),
	TP_printk("xfino 0x%lx %sbt nlevels %d fileoff 0x%llx",
		  __entry->xfino,
		  __get_str(btname),
		  __entry->nlevels,
		  (unsigned long long)__entry->fileoff)
)

#define DEFINE_XFBTREE_FREESP_EVENT(name) \
DEFINE_EVENT(xfbtree_freesp_class, name, \
	TP_PROTO(struct xfbtree *xfbt, struct xfs_btree_cur *cur, \
		 xfs_fileoff_t fileoff), \
	TP_ARGS(xfbt, cur, fileoff))
DEFINE_XFBTREE_FREESP_EVENT(xfbtree_alloc_block);
DEFINE_XFBTREE_FREESP_EVENT(xfbtree_free_block);
#endif /* CONFIG_XFS_BTREE_IN_MEM */

/* exchmaps tracepoints */
#define XFS_EXCHMAPS_STRINGS \
	{ XFS_EXCHMAPS_ATTR_FORK,		"ATTRFORK" }, \
	{ XFS_EXCHMAPS_SET_SIZES,		"SETSIZES" }, \
	{ XFS_EXCHMAPS_INO1_WRITTEN,		"INO1_WRITTEN" }, \
	{ XFS_EXCHMAPS_CLEAR_INO1_REFLINK,	"CLEAR_INO1_REFLINK" }, \
	{ XFS_EXCHMAPS_CLEAR_INO2_REFLINK,	"CLEAR_INO2_REFLINK" }, \
	{ __XFS_EXCHMAPS_INO2_SHORTFORM,	"INO2_SF" }

DEFINE_INODE_IREC_EVENT(xfs_exchmaps_mapping1_skip);
DEFINE_INODE_IREC_EVENT(xfs_exchmaps_mapping1);
DEFINE_INODE_IREC_EVENT(xfs_exchmaps_mapping2);
DEFINE_ITRUNC_EVENT(xfs_exchmaps_update_inode_size);

#define XFS_EXCHRANGE_INODES \
	{ 1,	"file1" }, \
	{ 2,	"file2" }

DECLARE_EVENT_CLASS(xfs_exchrange_inode_class,
	TP_PROTO(struct xfs_inode *ip, int whichfile),
	TP_ARGS(ip, whichfile),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, whichfile)
		__field(xfs_ino_t, ino)
		__field(int, format)
		__field(xfs_extnum_t, nex)
		__field(int, broot_size)
		__field(int, fork_off)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->whichfile = whichfile;
		__entry->ino = ip->i_ino;
		__entry->format = ip->i_df.if_format;
		__entry->nex = ip->i_df.if_nextents;
		__entry->fork_off = xfs_inode_fork_boff(ip);
	),
	TP_printk("dev %d:%d ino 0x%llx whichfile %s format %s num_extents %llu forkoff 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->whichfile, XFS_EXCHRANGE_INODES),
		  __print_symbolic(__entry->format, XFS_INODE_FORMAT_STR),
		  __entry->nex,
		  __entry->fork_off)
)

#define DEFINE_EXCHRANGE_INODE_EVENT(name) \
DEFINE_EVENT(xfs_exchrange_inode_class, name, \
	TP_PROTO(struct xfs_inode *ip, int whichfile), \
	TP_ARGS(ip, whichfile))

DEFINE_EXCHRANGE_INODE_EVENT(xfs_exchrange_before);
DEFINE_EXCHRANGE_INODE_EVENT(xfs_exchrange_after);
DEFINE_INODE_ERROR_EVENT(xfs_exchrange_error);

#define XFS_EXCHANGE_RANGE_FLAGS_STRS \
	{ XFS_EXCHANGE_RANGE_TO_EOF,		"TO_EOF" }, \
	{ XFS_EXCHANGE_RANGE_DSYNC	,	"DSYNC" }, \
	{ XFS_EXCHANGE_RANGE_DRY_RUN,		"DRY_RUN" }, \
	{ XFS_EXCHANGE_RANGE_FILE1_WRITTEN,	"F1_WRITTEN" }, \
	{ __XFS_EXCHANGE_RANGE_UPD_CMTIME1,	"CMTIME1" }, \
	{ __XFS_EXCHANGE_RANGE_UPD_CMTIME2,	"CMTIME2" }, \
	{ __XFS_EXCHANGE_RANGE_CHECK_FRESH2,	"FRESH2" }

/* file exchange-range tracepoint class */
DECLARE_EVENT_CLASS(xfs_exchrange_class,
	TP_PROTO(const struct xfs_exchrange *fxr, struct xfs_inode *ip1,
		 struct xfs_inode *ip2),
	TP_ARGS(fxr, ip1, ip2),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ip1_ino)
		__field(loff_t, ip1_isize)
		__field(loff_t, ip1_disize)
		__field(xfs_ino_t, ip2_ino)
		__field(loff_t, ip2_isize)
		__field(loff_t, ip2_disize)

		__field(loff_t, file1_offset)
		__field(loff_t, file2_offset)
		__field(unsigned long long, length)
		__field(unsigned long long, flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip1)->i_sb->s_dev;
		__entry->ip1_ino = ip1->i_ino;
		__entry->ip1_isize = VFS_I(ip1)->i_size;
		__entry->ip1_disize = ip1->i_disk_size;
		__entry->ip2_ino = ip2->i_ino;
		__entry->ip2_isize = VFS_I(ip2)->i_size;
		__entry->ip2_disize = ip2->i_disk_size;

		__entry->file1_offset = fxr->file1_offset;
		__entry->file2_offset = fxr->file2_offset;
		__entry->length = fxr->length;
		__entry->flags = fxr->flags;
	),
	TP_printk("dev %d:%d flags %s bytecount 0x%llx "
		  "ino1 0x%llx isize 0x%llx disize 0x%llx pos 0x%llx -> "
		  "ino2 0x%llx isize 0x%llx disize 0x%llx pos 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __print_flags_u64(__entry->flags, "|", XFS_EXCHANGE_RANGE_FLAGS_STRS),
		  __entry->length,
		  __entry->ip1_ino,
		  __entry->ip1_isize,
		  __entry->ip1_disize,
		  __entry->file1_offset,
		  __entry->ip2_ino,
		  __entry->ip2_isize,
		  __entry->ip2_disize,
		  __entry->file2_offset)
)

#define DEFINE_EXCHRANGE_EVENT(name)	\
DEFINE_EVENT(xfs_exchrange_class, name,	\
	TP_PROTO(const struct xfs_exchrange *fxr, struct xfs_inode *ip1, \
		 struct xfs_inode *ip2), \
	TP_ARGS(fxr, ip1, ip2))
DEFINE_EXCHRANGE_EVENT(xfs_exchrange_prep);
DEFINE_EXCHRANGE_EVENT(xfs_exchrange_flush);
DEFINE_EXCHRANGE_EVENT(xfs_exchrange_mappings);

TRACE_EVENT(xfs_exchrange_freshness,
	TP_PROTO(const struct xfs_exchrange *fxr, struct xfs_inode *ip2),
	TP_ARGS(fxr, ip2),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ip2_ino)
		__field(long long, ip2_mtime)
		__field(long long, ip2_ctime)
		__field(int, ip2_mtime_nsec)
		__field(int, ip2_ctime_nsec)

		__field(xfs_ino_t, file2_ino)
		__field(long long, file2_mtime)
		__field(long long, file2_ctime)
		__field(int, file2_mtime_nsec)
		__field(int, file2_ctime_nsec)
	),
	TP_fast_assign(
		struct timespec64	ts64;
		struct inode		*inode2 = VFS_I(ip2);

		__entry->dev = inode2->i_sb->s_dev;
		__entry->ip2_ino = ip2->i_ino;

		ts64 = inode_get_ctime(inode2);
		__entry->ip2_ctime = ts64.tv_sec;
		__entry->ip2_ctime_nsec = ts64.tv_nsec;

		ts64 = inode_get_mtime(inode2);
		__entry->ip2_mtime = ts64.tv_sec;
		__entry->ip2_mtime_nsec = ts64.tv_nsec;

		__entry->file2_ino = fxr->file2_ino;
		__entry->file2_mtime = fxr->file2_mtime.tv_sec;
		__entry->file2_ctime = fxr->file2_ctime.tv_sec;
		__entry->file2_mtime_nsec = fxr->file2_mtime.tv_nsec;
		__entry->file2_ctime_nsec = fxr->file2_ctime.tv_nsec;
	),
	TP_printk("dev %d:%d "
		  "ino 0x%llx mtime %lld:%d ctime %lld:%d -> "
		  "file 0x%llx mtime %lld:%d ctime %lld:%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ip2_ino,
		  __entry->ip2_mtime,
		  __entry->ip2_mtime_nsec,
		  __entry->ip2_ctime,
		  __entry->ip2_ctime_nsec,
		  __entry->file2_ino,
		  __entry->file2_mtime,
		  __entry->file2_mtime_nsec,
		  __entry->file2_ctime,
		  __entry->file2_ctime_nsec)
);

TRACE_EVENT(xfs_exchmaps_overhead,
	TP_PROTO(struct xfs_mount *mp, unsigned long long bmbt_blocks,
		 unsigned long long rmapbt_blocks),
	TP_ARGS(mp, bmbt_blocks, rmapbt_blocks),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long long, bmbt_blocks)
		__field(unsigned long long, rmapbt_blocks)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->bmbt_blocks = bmbt_blocks;
		__entry->rmapbt_blocks = rmapbt_blocks;
	),
	TP_printk("dev %d:%d bmbt_blocks 0x%llx rmapbt_blocks 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->bmbt_blocks,
		  __entry->rmapbt_blocks)
);

DECLARE_EVENT_CLASS(xfs_exchmaps_estimate_class,
	TP_PROTO(const struct xfs_exchmaps_req *req),
	TP_ARGS(req),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino1)
		__field(xfs_ino_t, ino2)
		__field(xfs_fileoff_t, startoff1)
		__field(xfs_fileoff_t, startoff2)
		__field(xfs_filblks_t, blockcount)
		__field(uint64_t, flags)
		__field(xfs_filblks_t, ip1_bcount)
		__field(xfs_filblks_t, ip2_bcount)
		__field(xfs_filblks_t, ip1_rtbcount)
		__field(xfs_filblks_t, ip2_rtbcount)
		__field(unsigned long long, resblks)
		__field(unsigned long long, nr_exchanges)
	),
	TP_fast_assign(
		__entry->dev = req->ip1->i_mount->m_super->s_dev;
		__entry->ino1 = req->ip1->i_ino;
		__entry->ino2 = req->ip2->i_ino;
		__entry->startoff1 = req->startoff1;
		__entry->startoff2 = req->startoff2;
		__entry->blockcount = req->blockcount;
		__entry->flags = req->flags;
		__entry->ip1_bcount = req->ip1_bcount;
		__entry->ip2_bcount = req->ip2_bcount;
		__entry->ip1_rtbcount = req->ip1_rtbcount;
		__entry->ip2_rtbcount = req->ip2_rtbcount;
		__entry->resblks = req->resblks;
		__entry->nr_exchanges = req->nr_exchanges;
	),
	TP_printk("dev %d:%d ino1 0x%llx fileoff1 0x%llx ino2 0x%llx fileoff2 0x%llx fsbcount 0x%llx flags (%s) bcount1 0x%llx rtbcount1 0x%llx bcount2 0x%llx rtbcount2 0x%llx resblks 0x%llx nr_exchanges %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino1, __entry->startoff1,
		  __entry->ino2, __entry->startoff2,
		  __entry->blockcount,
		  __print_flags_u64(__entry->flags, "|", XFS_EXCHMAPS_STRINGS),
		  __entry->ip1_bcount,
		  __entry->ip1_rtbcount,
		  __entry->ip2_bcount,
		  __entry->ip2_rtbcount,
		  __entry->resblks,
		  __entry->nr_exchanges)
);

#define DEFINE_EXCHMAPS_ESTIMATE_EVENT(name)	\
DEFINE_EVENT(xfs_exchmaps_estimate_class, name,	\
	TP_PROTO(const struct xfs_exchmaps_req *req), \
	TP_ARGS(req))
DEFINE_EXCHMAPS_ESTIMATE_EVENT(xfs_exchmaps_initial_estimate);
DEFINE_EXCHMAPS_ESTIMATE_EVENT(xfs_exchmaps_final_estimate);

DECLARE_EVENT_CLASS(xfs_exchmaps_intent_class,
	TP_PROTO(struct xfs_mount *mp, const struct xfs_exchmaps_intent *xmi),
	TP_ARGS(mp, xmi),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino1)
		__field(xfs_ino_t, ino2)
		__field(uint64_t, flags)
		__field(xfs_fileoff_t, startoff1)
		__field(xfs_fileoff_t, startoff2)
		__field(xfs_filblks_t, blockcount)
		__field(xfs_fsize_t, isize1)
		__field(xfs_fsize_t, isize2)
		__field(xfs_fsize_t, new_isize1)
		__field(xfs_fsize_t, new_isize2)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->ino1 = xmi->xmi_ip1->i_ino;
		__entry->ino2 = xmi->xmi_ip2->i_ino;
		__entry->flags = xmi->xmi_flags;
		__entry->startoff1 = xmi->xmi_startoff1;
		__entry->startoff2 = xmi->xmi_startoff2;
		__entry->blockcount = xmi->xmi_blockcount;
		__entry->isize1 = xmi->xmi_ip1->i_disk_size;
		__entry->isize2 = xmi->xmi_ip2->i_disk_size;
		__entry->new_isize1 = xmi->xmi_isize1;
		__entry->new_isize2 = xmi->xmi_isize2;
	),
	TP_printk("dev %d:%d ino1 0x%llx fileoff1 0x%llx ino2 0x%llx fileoff2 0x%llx fsbcount 0x%llx flags (%s) isize1 0x%llx newisize1 0x%llx isize2 0x%llx newisize2 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino1, __entry->startoff1,
		  __entry->ino2, __entry->startoff2,
		  __entry->blockcount,
		  __print_flags_u64(__entry->flags, "|", XFS_EXCHMAPS_STRINGS),
		  __entry->isize1, __entry->new_isize1,
		  __entry->isize2, __entry->new_isize2)
);

#define DEFINE_EXCHMAPS_INTENT_EVENT(name)	\
DEFINE_EVENT(xfs_exchmaps_intent_class, name,	\
	TP_PROTO(struct xfs_mount *mp, const struct xfs_exchmaps_intent *xmi), \
	TP_ARGS(mp, xmi))
DEFINE_EXCHMAPS_INTENT_EVENT(xfs_exchmaps_defer);
DEFINE_EXCHMAPS_INTENT_EVENT(xfs_exchmaps_recover);

TRACE_EVENT(xfs_exchmaps_delta_nextents_step,
	TP_PROTO(struct xfs_mount *mp,
		 const struct xfs_bmbt_irec *left,
		 const struct xfs_bmbt_irec *curr,
		 const struct xfs_bmbt_irec *new,
		 const struct xfs_bmbt_irec *right,
		 int delta, unsigned int state),
	TP_ARGS(mp, left, curr, new, right, delta, state),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_fileoff_t, loff)
		__field(xfs_fsblock_t, lstart)
		__field(xfs_filblks_t, lcount)
		__field(xfs_fileoff_t, coff)
		__field(xfs_fsblock_t, cstart)
		__field(xfs_filblks_t, ccount)
		__field(xfs_fileoff_t, noff)
		__field(xfs_fsblock_t, nstart)
		__field(xfs_filblks_t, ncount)
		__field(xfs_fileoff_t, roff)
		__field(xfs_fsblock_t, rstart)
		__field(xfs_filblks_t, rcount)
		__field(int, delta)
		__field(unsigned int, state)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->loff = left->br_startoff;
		__entry->lstart = left->br_startblock;
		__entry->lcount = left->br_blockcount;
		__entry->coff = curr->br_startoff;
		__entry->cstart = curr->br_startblock;
		__entry->ccount = curr->br_blockcount;
		__entry->noff = new->br_startoff;
		__entry->nstart = new->br_startblock;
		__entry->ncount = new->br_blockcount;
		__entry->roff = right->br_startoff;
		__entry->rstart = right->br_startblock;
		__entry->rcount = right->br_blockcount;
		__entry->delta = delta;
		__entry->state = state;
	),
	TP_printk("dev %d:%d left 0x%llx:0x%llx:0x%llx; curr 0x%llx:0x%llx:0x%llx <- new 0x%llx:0x%llx:0x%llx; right 0x%llx:0x%llx:0x%llx delta %d state 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		__entry->loff, __entry->lstart, __entry->lcount,
		__entry->coff, __entry->cstart, __entry->ccount,
		__entry->noff, __entry->nstart, __entry->ncount,
		__entry->roff, __entry->rstart, __entry->rcount,
		__entry->delta, __entry->state)
);

TRACE_EVENT(xfs_exchmaps_delta_nextents,
	TP_PROTO(const struct xfs_exchmaps_req *req, int64_t d_nexts1,
		 int64_t d_nexts2),
	TP_ARGS(req, d_nexts1, d_nexts2),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino1)
		__field(xfs_ino_t, ino2)
		__field(xfs_extnum_t, nexts1)
		__field(xfs_extnum_t, nexts2)
		__field(int64_t, d_nexts1)
		__field(int64_t, d_nexts2)
	),
	TP_fast_assign(
		int whichfork = xfs_exchmaps_reqfork(req);

		__entry->dev = req->ip1->i_mount->m_super->s_dev;
		__entry->ino1 = req->ip1->i_ino;
		__entry->ino2 = req->ip2->i_ino;
		__entry->nexts1 = xfs_ifork_ptr(req->ip1, whichfork)->if_nextents;
		__entry->nexts2 = xfs_ifork_ptr(req->ip2, whichfork)->if_nextents;
		__entry->d_nexts1 = d_nexts1;
		__entry->d_nexts2 = d_nexts2;
	),
	TP_printk("dev %d:%d ino1 0x%llx nexts %llu ino2 0x%llx nexts %llu delta1 %lld delta2 %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino1, __entry->nexts1,
		  __entry->ino2, __entry->nexts2,
		  __entry->d_nexts1, __entry->d_nexts2)
);

DECLARE_EVENT_CLASS(xfs_getparents_rec_class,
	TP_PROTO(struct xfs_inode *ip, const struct xfs_getparents *ppi,
		 const struct xfs_attr_list_context *context,
	         const struct xfs_getparents_rec *pptr),
	TP_ARGS(ip, ppi, context, pptr),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned int, firstu)
		__field(unsigned short, reclen)
		__field(unsigned int, bufsize)
		__field(xfs_ino_t, parent_ino)
		__field(unsigned int, parent_gen)
		__string(name, pptr->gpr_name)
	),
	TP_fast_assign(
		__entry->dev = ip->i_mount->m_super->s_dev;
		__entry->ino = ip->i_ino;
		__entry->firstu = context->firstu;
		__entry->reclen = pptr->gpr_reclen;
		__entry->bufsize = ppi->gp_bufsize;
		__entry->parent_ino = pptr->gpr_parent.ha_fid.fid_ino;
		__entry->parent_gen = pptr->gpr_parent.ha_fid.fid_gen;
		__assign_str(name);
	),
	TP_printk("dev %d:%d ino 0x%llx firstu %u reclen %u bufsize %u parent_ino 0x%llx parent_gen 0x%x name '%s'",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->firstu,
		  __entry->reclen,
		  __entry->bufsize,
		  __entry->parent_ino,
		  __entry->parent_gen,
		  __get_str(name))
)
#define DEFINE_XFS_GETPARENTS_REC_EVENT(name) \
DEFINE_EVENT(xfs_getparents_rec_class, name, \
	TP_PROTO(struct xfs_inode *ip, const struct xfs_getparents *ppi, \
		 const struct xfs_attr_list_context *context, \
	         const struct xfs_getparents_rec *pptr), \
	TP_ARGS(ip, ppi, context, pptr))
DEFINE_XFS_GETPARENTS_REC_EVENT(xfs_getparents_put_listent);
DEFINE_XFS_GETPARENTS_REC_EVENT(xfs_getparents_expand_lastrec);

DECLARE_EVENT_CLASS(xfs_getparents_class,
	TP_PROTO(struct xfs_inode *ip, const struct xfs_getparents *ppi,
		 const struct xfs_attrlist_cursor_kern *cur),
	TP_ARGS(ip, ppi, cur),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned short, iflags)
		__field(unsigned short, oflags)
		__field(unsigned int, bufsize)
		__field(unsigned int, hashval)
		__field(unsigned int, blkno)
		__field(unsigned int, offset)
		__field(int, initted)
	),
	TP_fast_assign(
		__entry->dev = ip->i_mount->m_super->s_dev;
		__entry->ino = ip->i_ino;
		__entry->iflags = ppi->gp_iflags;
		__entry->oflags = ppi->gp_oflags;
		__entry->bufsize = ppi->gp_bufsize;
		__entry->hashval = cur->hashval;
		__entry->blkno = cur->blkno;
		__entry->offset = cur->offset;
		__entry->initted = cur->initted;
	),
	TP_printk("dev %d:%d ino 0x%llx iflags 0x%x oflags 0x%x bufsize %u cur_init? %d hashval 0x%x blkno %u offset %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->iflags,
		  __entry->oflags,
		  __entry->bufsize,
		  __entry->initted,
		  __entry->hashval,
		  __entry->blkno,
		  __entry->offset)
)
#define DEFINE_XFS_GETPARENTS_EVENT(name) \
DEFINE_EVENT(xfs_getparents_class, name, \
	TP_PROTO(struct xfs_inode *ip, const struct xfs_getparents *ppi, \
		 const struct xfs_attrlist_cursor_kern *cur), \
	TP_ARGS(ip, ppi, cur))
DEFINE_XFS_GETPARENTS_EVENT(xfs_getparents_begin);
DEFINE_XFS_GETPARENTS_EVENT(xfs_getparents_end);

DECLARE_EVENT_CLASS(xfs_metadir_update_class,
	TP_PROTO(const struct xfs_metadir_update *upd),
	TP_ARGS(upd),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, dp_ino)
		__field(xfs_ino_t, ino)
		__string(fname, upd->path)
	),
	TP_fast_assign(
		__entry->dev = upd->dp->i_mount->m_super->s_dev;
		__entry->dp_ino = upd->dp->i_ino;
		__entry->ino = upd->ip ? upd->ip->i_ino : NULLFSINO;
		__assign_str(fname);
	),
	TP_printk("dev %d:%d dp 0x%llx fname '%s' ino 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dp_ino,
		  __get_str(fname),
		  __entry->ino)
)

#define DEFINE_METADIR_UPDATE_EVENT(name) \
DEFINE_EVENT(xfs_metadir_update_class, name, \
	TP_PROTO(const struct xfs_metadir_update *upd), \
	TP_ARGS(upd))
DEFINE_METADIR_UPDATE_EVENT(xfs_metadir_start_create);
DEFINE_METADIR_UPDATE_EVENT(xfs_metadir_start_link);
DEFINE_METADIR_UPDATE_EVENT(xfs_metadir_commit);
DEFINE_METADIR_UPDATE_EVENT(xfs_metadir_cancel);
DEFINE_METADIR_UPDATE_EVENT(xfs_metadir_try_create);
DEFINE_METADIR_UPDATE_EVENT(xfs_metadir_create);
DEFINE_METADIR_UPDATE_EVENT(xfs_metadir_link);

DECLARE_EVENT_CLASS(xfs_metadir_update_error_class,
	TP_PROTO(const struct xfs_metadir_update *upd, int error),
	TP_ARGS(upd, error),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, dp_ino)
		__field(xfs_ino_t, ino)
		__field(int, error)
		__string(fname, upd->path)
	),
	TP_fast_assign(
		__entry->dev = upd->dp->i_mount->m_super->s_dev;
		__entry->dp_ino = upd->dp->i_ino;
		__entry->ino = upd->ip ? upd->ip->i_ino : NULLFSINO;
		__entry->error = error;
		__assign_str(fname);
	),
	TP_printk("dev %d:%d dp 0x%llx fname '%s' ino 0x%llx error %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dp_ino,
		  __get_str(fname),
		  __entry->ino,
		  __entry->error)
)

#define DEFINE_METADIR_UPDATE_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_metadir_update_error_class, name, \
	TP_PROTO(const struct xfs_metadir_update *upd, int error), \
	TP_ARGS(upd, error))
DEFINE_METADIR_UPDATE_ERROR_EVENT(xfs_metadir_teardown);

DECLARE_EVENT_CLASS(xfs_metadir_class,
	TP_PROTO(struct xfs_inode *dp, struct xfs_name *name,
		 xfs_ino_t ino),
	TP_ARGS(dp, name, ino),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, dp_ino)
		__field(xfs_ino_t, ino)
		__field(int, ftype)
		__field(int, namelen)
		__dynamic_array(char, name, name->len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(dp)->i_sb->s_dev;
		__entry->dp_ino = dp->i_ino;
		__entry->ino = ino,
		__entry->ftype = name->type;
		__entry->namelen = name->len;
		memcpy(__get_str(name), name->name, name->len);
	),
	TP_printk("dev %d:%d dir 0x%llx type %s name '%.*s' ino 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dp_ino,
		  __print_symbolic(__entry->ftype, XFS_DIR3_FTYPE_STR),
		  __entry->namelen,
		  __get_str(name),
		  __entry->ino)
)

#define DEFINE_METADIR_EVENT(name) \
DEFINE_EVENT(xfs_metadir_class, name, \
	TP_PROTO(struct xfs_inode *dp, struct xfs_name *name, \
		 xfs_ino_t ino), \
	TP_ARGS(dp, name, ino))
DEFINE_METADIR_EVENT(xfs_metadir_lookup);

/* metadata inode space reservations */

DECLARE_EVENT_CLASS(xfs_metafile_resv_class,
	TP_PROTO(struct xfs_mount *mp, xfs_filblks_t len),
	TP_ARGS(mp, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long long, freeblks)
		__field(unsigned long long, reserved)
		__field(unsigned long long, asked)
		__field(unsigned long long, used)
		__field(unsigned long long, len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->freeblks = xfs_sum_freecounter_raw(mp, XC_FREE_BLOCKS);
		__entry->reserved = mp->m_metafile_resv_avail;
		__entry->asked = mp->m_metafile_resv_target;
		__entry->used = mp->m_metafile_resv_used;
		__entry->len = len;
	),
	TP_printk("dev %d:%d freeblks %llu resv %llu ask %llu used %llu len %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->freeblks,
		  __entry->reserved,
		  __entry->asked,
		  __entry->used,
		  __entry->len)
)
#define DEFINE_METAFILE_RESV_EVENT(name) \
DEFINE_EVENT(xfs_metafile_resv_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_filblks_t len), \
	TP_ARGS(mp, len))
DEFINE_METAFILE_RESV_EVENT(xfs_metafile_resv_init);
DEFINE_METAFILE_RESV_EVENT(xfs_metafile_resv_free);
DEFINE_METAFILE_RESV_EVENT(xfs_metafile_resv_alloc_space);
DEFINE_METAFILE_RESV_EVENT(xfs_metafile_resv_free_space);
DEFINE_METAFILE_RESV_EVENT(xfs_metafile_resv_critical);
DEFINE_METAFILE_RESV_EVENT(xfs_metafile_resv_init_error);

#ifdef CONFIG_XFS_RT
TRACE_EVENT(xfs_growfs_check_rtgeom,
	TP_PROTO(const struct xfs_mount *mp, unsigned int min_logfsbs),
	TP_ARGS(mp, min_logfsbs),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, logblocks)
		__field(unsigned int, min_logfsbs)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->logblocks = mp->m_sb.sb_logblocks;
		__entry->min_logfsbs = min_logfsbs;
	),
	TP_printk("dev %d:%d logblocks %u min_logfsbs %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->logblocks,
		  __entry->min_logfsbs)
);
#endif /* CONFIG_XFS_RT */

TRACE_DEFINE_ENUM(XC_FREE_BLOCKS);
TRACE_DEFINE_ENUM(XC_FREE_RTEXTENTS);
TRACE_DEFINE_ENUM(XC_FREE_RTAVAILABLE);

DECLARE_EVENT_CLASS(xfs_freeblocks_resv_class,
	TP_PROTO(struct xfs_mount *mp, enum xfs_free_counter ctr,
		 uint64_t delta, unsigned long caller_ip),
	TP_ARGS(mp, ctr, delta, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(enum xfs_free_counter, ctr)
		__field(uint64_t, delta)
		__field(uint64_t, avail)
		__field(uint64_t, total)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->ctr = ctr;
		__entry->delta = delta;
		__entry->avail = mp->m_free[ctr].res_avail;
		__entry->total = mp->m_free[ctr].res_total;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ctr %s delta %llu avail %llu total %llu caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->ctr, XFS_FREECOUNTER_STR),
		  __entry->delta,
		  __entry->avail,
		  __entry->total,
		  (char *)__entry->caller_ip)
)
#define DEFINE_FREEBLOCKS_RESV_EVENT(name) \
DEFINE_EVENT(xfs_freeblocks_resv_class, name, \
	TP_PROTO(struct xfs_mount *mp, enum xfs_free_counter ctr, \
		 uint64_t delta, unsigned long caller_ip), \
	TP_ARGS(mp, ctr, delta, caller_ip))
DEFINE_FREEBLOCKS_RESV_EVENT(xfs_freecounter_reserved);
DEFINE_FREEBLOCKS_RESV_EVENT(xfs_freecounter_enospc);

#endif /* _TRACE_XFS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE xfs_trace
#include <trace/define_trace.h>
