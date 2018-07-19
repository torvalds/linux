// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xfs_scrub

#if !defined(_TRACE_XFS_SCRUB_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XFS_SCRUB_TRACE_H

#include <linux/tracepoint.h>
#include "xfs_bit.h"

DECLARE_EVENT_CLASS(xfs_scrub_class,
	TP_PROTO(struct xfs_inode *ip, struct xfs_scrub_metadata *sm,
		 int error),
	TP_ARGS(ip, sm, error),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned int, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_ino_t, inum)
		__field(unsigned int, gen)
		__field(unsigned int, flags)
		__field(int, error)
	),
	TP_fast_assign(
		__entry->dev = ip->i_mount->m_super->s_dev;
		__entry->ino = ip->i_ino;
		__entry->type = sm->sm_type;
		__entry->agno = sm->sm_agno;
		__entry->inum = sm->sm_ino;
		__entry->gen = sm->sm_gen;
		__entry->flags = sm->sm_flags;
		__entry->error = error;
	),
	TP_printk("dev %d:%d ino 0x%llx type %u agno %u inum %llu gen %u flags 0x%x error %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->type,
		  __entry->agno,
		  __entry->inum,
		  __entry->gen,
		  __entry->flags,
		  __entry->error)
)
#define DEFINE_SCRUB_EVENT(name) \
DEFINE_EVENT(xfs_scrub_class, name, \
	TP_PROTO(struct xfs_inode *ip, struct xfs_scrub_metadata *sm, \
		 int error), \
	TP_ARGS(ip, sm, error))

DEFINE_SCRUB_EVENT(xfs_scrub_start);
DEFINE_SCRUB_EVENT(xfs_scrub_done);
DEFINE_SCRUB_EVENT(xfs_scrub_deadlock_retry);
DEFINE_SCRUB_EVENT(xfs_repair_attempt);
DEFINE_SCRUB_EVENT(xfs_repair_done);

TRACE_EVENT(xfs_scrub_op_error,
	TP_PROTO(struct xfs_scrub_context *sc, xfs_agnumber_t agno,
		 xfs_agblock_t bno, int error, void *ret_ip),
	TP_ARGS(sc, agno, bno, error, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, error)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->agno = agno;
		__entry->bno = bno;
		__entry->error = error;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %u agno %u agbno %u error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->agno,
		  __entry->bno,
		  __entry->error,
		  __entry->ret_ip)
);

TRACE_EVENT(xfs_scrub_file_op_error,
	TP_PROTO(struct xfs_scrub_context *sc, int whichfork,
		 xfs_fileoff_t offset, int error, void *ret_ip),
	TP_ARGS(sc, whichfork, offset, error, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, whichfork)
		__field(unsigned int, type)
		__field(xfs_fileoff_t, offset)
		__field(int, error)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		__entry->dev = sc->ip->i_mount->m_super->s_dev;
		__entry->ino = sc->ip->i_ino;
		__entry->whichfork = whichfork;
		__entry->type = sc->sm->sm_type;
		__entry->offset = offset;
		__entry->error = error;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx fork %d type %u offset %llu error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->whichfork,
		  __entry->type,
		  __entry->offset,
		  __entry->error,
		  __entry->ret_ip)
);

DECLARE_EVENT_CLASS(xfs_scrub_block_error_class,
	TP_PROTO(struct xfs_scrub_context *sc, xfs_daddr_t daddr, void *ret_ip),
	TP_ARGS(sc, daddr, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t	fsbno;
		xfs_agnumber_t	agno;
		xfs_agblock_t	bno;

		fsbno = XFS_DADDR_TO_FSB(sc->mp, daddr);
		agno = XFS_FSB_TO_AGNO(sc->mp, fsbno);
		bno = XFS_FSB_TO_AGBNO(sc->mp, fsbno);

		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->agno = agno;
		__entry->bno = bno;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %u agno %u agbno %u ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->agno,
		  __entry->bno,
		  __entry->ret_ip)
)

#define DEFINE_SCRUB_BLOCK_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_scrub_block_error_class, name, \
	TP_PROTO(struct xfs_scrub_context *sc, xfs_daddr_t daddr, \
		 void *ret_ip), \
	TP_ARGS(sc, daddr, ret_ip))

DEFINE_SCRUB_BLOCK_ERROR_EVENT(xfs_scrub_block_error);
DEFINE_SCRUB_BLOCK_ERROR_EVENT(xfs_scrub_block_preen);

DECLARE_EVENT_CLASS(xfs_scrub_ino_error_class,
	TP_PROTO(struct xfs_scrub_context *sc, xfs_ino_t ino, void *ret_ip),
	TP_ARGS(sc, ino, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned int, type)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = ino;
		__entry->type = sc->sm->sm_type;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx type %u ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->type,
		  __entry->ret_ip)
)

#define DEFINE_SCRUB_INO_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_scrub_ino_error_class, name, \
	TP_PROTO(struct xfs_scrub_context *sc, xfs_ino_t ino, \
		 void *ret_ip), \
	TP_ARGS(sc, ino, ret_ip))

DEFINE_SCRUB_INO_ERROR_EVENT(xfs_scrub_ino_error);
DEFINE_SCRUB_INO_ERROR_EVENT(xfs_scrub_ino_preen);
DEFINE_SCRUB_INO_ERROR_EVENT(xfs_scrub_ino_warning);

DECLARE_EVENT_CLASS(xfs_scrub_fblock_error_class,
	TP_PROTO(struct xfs_scrub_context *sc, int whichfork,
		 xfs_fileoff_t offset, void *ret_ip),
	TP_ARGS(sc, whichfork, offset, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, whichfork)
		__field(unsigned int, type)
		__field(xfs_fileoff_t, offset)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		__entry->dev = sc->ip->i_mount->m_super->s_dev;
		__entry->ino = sc->ip->i_ino;
		__entry->whichfork = whichfork;
		__entry->type = sc->sm->sm_type;
		__entry->offset = offset;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx fork %d type %u offset %llu ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->whichfork,
		  __entry->type,
		  __entry->offset,
		  __entry->ret_ip)
);

#define DEFINE_SCRUB_FBLOCK_ERROR_EVENT(name) \
DEFINE_EVENT(xfs_scrub_fblock_error_class, name, \
	TP_PROTO(struct xfs_scrub_context *sc, int whichfork, \
		 xfs_fileoff_t offset, void *ret_ip), \
	TP_ARGS(sc, whichfork, offset, ret_ip))

DEFINE_SCRUB_FBLOCK_ERROR_EVENT(xfs_scrub_fblock_error);
DEFINE_SCRUB_FBLOCK_ERROR_EVENT(xfs_scrub_fblock_warning);

TRACE_EVENT(xfs_scrub_incomplete,
	TP_PROTO(struct xfs_scrub_context *sc, void *ret_ip),
	TP_ARGS(sc, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %u ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->ret_ip)
);

TRACE_EVENT(xfs_scrub_btree_op_error,
	TP_PROTO(struct xfs_scrub_context *sc, struct xfs_btree_cur *cur,
		 int level, int error, void *ret_ip),
	TP_ARGS(sc, cur, level, error, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__field(xfs_btnum_t, btnum)
		__field(int, level)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, ptr);
		__field(int, error)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xfs_scrub_btree_cur_fsbno(cur, level);

		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->btnum = cur->bc_btnum;
		__entry->level = level;
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->ptr = cur->bc_ptrs[level];
		__entry->error = error;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %u btnum %d level %d ptr %d agno %u agbno %u error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->btnum,
		  __entry->level,
		  __entry->ptr,
		  __entry->agno,
		  __entry->bno,
		  __entry->error,
		  __entry->ret_ip)
);

TRACE_EVENT(xfs_scrub_ifork_btree_op_error,
	TP_PROTO(struct xfs_scrub_context *sc, struct xfs_btree_cur *cur,
		 int level, int error, void *ret_ip),
	TP_ARGS(sc, cur, level, error, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, whichfork)
		__field(unsigned int, type)
		__field(xfs_btnum_t, btnum)
		__field(int, level)
		__field(int, ptr)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, error)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xfs_scrub_btree_cur_fsbno(cur, level);
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->ip->i_ino;
		__entry->whichfork = cur->bc_private.b.whichfork;
		__entry->type = sc->sm->sm_type;
		__entry->btnum = cur->bc_btnum;
		__entry->level = level;
		__entry->ptr = cur->bc_ptrs[level];
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->error = error;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx fork %d type %u btnum %d level %d ptr %d agno %u agbno %u error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->whichfork,
		  __entry->type,
		  __entry->btnum,
		  __entry->level,
		  __entry->ptr,
		  __entry->agno,
		  __entry->bno,
		  __entry->error,
		  __entry->ret_ip)
);

TRACE_EVENT(xfs_scrub_btree_error,
	TP_PROTO(struct xfs_scrub_context *sc, struct xfs_btree_cur *cur,
		 int level, void *ret_ip),
	TP_ARGS(sc, cur, level, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__field(xfs_btnum_t, btnum)
		__field(int, level)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, ptr);
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xfs_scrub_btree_cur_fsbno(cur, level);
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->btnum = cur->bc_btnum;
		__entry->level = level;
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->ptr = cur->bc_ptrs[level];
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %u btnum %d level %d ptr %d agno %u agbno %u ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->btnum,
		  __entry->level,
		  __entry->ptr,
		  __entry->agno,
		  __entry->bno,
		  __entry->ret_ip)
);

TRACE_EVENT(xfs_scrub_ifork_btree_error,
	TP_PROTO(struct xfs_scrub_context *sc, struct xfs_btree_cur *cur,
		 int level, void *ret_ip),
	TP_ARGS(sc, cur, level, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, whichfork)
		__field(unsigned int, type)
		__field(xfs_btnum_t, btnum)
		__field(int, level)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, ptr);
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xfs_scrub_btree_cur_fsbno(cur, level);
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->ip->i_ino;
		__entry->whichfork = cur->bc_private.b.whichfork;
		__entry->type = sc->sm->sm_type;
		__entry->btnum = cur->bc_btnum;
		__entry->level = level;
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->ptr = cur->bc_ptrs[level];
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx fork %d type %u btnum %d level %d ptr %d agno %u agbno %u ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->whichfork,
		  __entry->type,
		  __entry->btnum,
		  __entry->level,
		  __entry->ptr,
		  __entry->agno,
		  __entry->bno,
		  __entry->ret_ip)
);

DECLARE_EVENT_CLASS(xfs_scrub_sbtree_class,
	TP_PROTO(struct xfs_scrub_context *sc, struct xfs_btree_cur *cur,
		 int level),
	TP_ARGS(sc, cur, level),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, type)
		__field(xfs_btnum_t, btnum)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, level)
		__field(int, nlevels)
		__field(int, ptr)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xfs_scrub_btree_cur_fsbno(cur, level);

		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->btnum = cur->bc_btnum;
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->level = level;
		__entry->nlevels = cur->bc_nlevels;
		__entry->ptr = cur->bc_ptrs[level];
	),
	TP_printk("dev %d:%d type %u btnum %d agno %u agbno %u level %d nlevels %d ptr %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->btnum,
		  __entry->agno,
		  __entry->bno,
		  __entry->level,
		  __entry->nlevels,
		  __entry->ptr)
)
#define DEFINE_SCRUB_SBTREE_EVENT(name) \
DEFINE_EVENT(xfs_scrub_sbtree_class, name, \
	TP_PROTO(struct xfs_scrub_context *sc, struct xfs_btree_cur *cur, \
		 int level), \
	TP_ARGS(sc, cur, level))

DEFINE_SCRUB_SBTREE_EVENT(xfs_scrub_btree_rec);
DEFINE_SCRUB_SBTREE_EVENT(xfs_scrub_btree_key);

TRACE_EVENT(xfs_scrub_xref_error,
	TP_PROTO(struct xfs_scrub_context *sc, int error, void *ret_ip),
	TP_ARGS(sc, error, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, type)
		__field(int, error)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->error = error;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %u xref error %d ret_ip %pF",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->type,
		  __entry->error,
		  __entry->ret_ip)
);

/* repair tracepoints */
#if IS_ENABLED(CONFIG_XFS_ONLINE_REPAIR)

DECLARE_EVENT_CLASS(xfs_repair_extent_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len),
	TP_ARGS(mp, agno, agbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len)
);
#define DEFINE_REPAIR_EXTENT_EVENT(name) \
DEFINE_EVENT(xfs_repair_extent_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_extlen_t len), \
	TP_ARGS(mp, agno, agbno, len))
DEFINE_REPAIR_EXTENT_EVENT(xfs_repair_dispose_btree_extent);
DEFINE_REPAIR_EXTENT_EVENT(xfs_repair_collect_btree_extent);
DEFINE_REPAIR_EXTENT_EVENT(xfs_repair_agfl_insert);

DECLARE_EVENT_CLASS(xfs_repair_rmap_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len,
		 uint64_t owner, uint64_t offset, unsigned int flags),
	TP_ARGS(mp, agno, agbno, len, owner, offset, flags),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->owner = owner;
		__entry->offset = offset;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u owner %lld offset %llu flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);
#define DEFINE_REPAIR_RMAP_EVENT(name) \
DEFINE_EVENT(xfs_repair_rmap_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_extlen_t len, \
		 uint64_t owner, uint64_t offset, unsigned int flags), \
	TP_ARGS(mp, agno, agbno, len, owner, offset, flags))
DEFINE_REPAIR_RMAP_EVENT(xfs_repair_alloc_extent_fn);
DEFINE_REPAIR_RMAP_EVENT(xfs_repair_ialloc_extent_fn);
DEFINE_REPAIR_RMAP_EVENT(xfs_repair_rmap_extent_fn);
DEFINE_REPAIR_RMAP_EVENT(xfs_repair_bmap_extent_fn);

TRACE_EVENT(xfs_repair_refcount_extent_fn,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 struct xfs_refcount_irec *irec),
	TP_ARGS(mp, agno, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, startblock)
		__field(xfs_extlen_t, blockcount)
		__field(xfs_nlink_t, refcount)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->startblock = irec->rc_startblock;
		__entry->blockcount = irec->rc_blockcount;
		__entry->refcount = irec->rc_refcount;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u refcount %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->refcount)
)

TRACE_EVENT(xfs_repair_init_btblock,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, xfs_agblock_t agbno,
		 xfs_btnum_t btnum),
	TP_ARGS(mp, agno, agbno, btnum),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(uint32_t, btnum)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->btnum = btnum;
	),
	TP_printk("dev %d:%d agno %u agbno %u btnum %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->btnum)
)
TRACE_EVENT(xfs_repair_findroot_block,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, xfs_agblock_t agbno,
		 uint32_t magic, uint16_t level),
	TP_ARGS(mp, agno, agbno, magic, level),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(uint32_t, magic)
		__field(uint16_t, level)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->magic = magic;
		__entry->level = level;
	),
	TP_printk("dev %d:%d agno %u agbno %u magic 0x%x level %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->magic,
		  __entry->level)
)
TRACE_EVENT(xfs_repair_calc_ag_resblks,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agino_t icount, xfs_agblock_t aglen, xfs_agblock_t freelen,
		 xfs_agblock_t usedlen),
	TP_ARGS(mp, agno, icount, aglen, freelen, usedlen),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, icount)
		__field(xfs_agblock_t, aglen)
		__field(xfs_agblock_t, freelen)
		__field(xfs_agblock_t, usedlen)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->icount = icount;
		__entry->aglen = aglen;
		__entry->freelen = freelen;
		__entry->usedlen = usedlen;
	),
	TP_printk("dev %d:%d agno %d icount %u aglen %u freelen %u usedlen %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->icount,
		  __entry->aglen,
		  __entry->freelen,
		  __entry->usedlen)
)
TRACE_EVENT(xfs_repair_calc_ag_resblks_btsize,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t bnobt_sz, xfs_agblock_t inobt_sz,
		 xfs_agblock_t rmapbt_sz, xfs_agblock_t refcbt_sz),
	TP_ARGS(mp, agno, bnobt_sz, inobt_sz, rmapbt_sz, refcbt_sz),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bnobt_sz)
		__field(xfs_agblock_t, inobt_sz)
		__field(xfs_agblock_t, rmapbt_sz)
		__field(xfs_agblock_t, refcbt_sz)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->bnobt_sz = bnobt_sz;
		__entry->inobt_sz = inobt_sz;
		__entry->rmapbt_sz = rmapbt_sz;
		__entry->refcbt_sz = refcbt_sz;
	),
	TP_printk("dev %d:%d agno %d bno %u ino %u rmap %u refcount %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->bnobt_sz,
		  __entry->inobt_sz,
		  __entry->rmapbt_sz,
		  __entry->refcbt_sz)
)
TRACE_EVENT(xfs_repair_reset_counters,
	TP_PROTO(struct xfs_mount *mp),
	TP_ARGS(mp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
	),
	TP_printk("dev %d:%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev))
)

TRACE_EVENT(xfs_repair_ialloc_insert,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agino_t startino, uint16_t holemask, uint8_t count,
		 uint8_t freecount, uint64_t freemask),
	TP_ARGS(mp, agno, startino, holemask, count, freecount, freemask),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, startino)
		__field(uint16_t, holemask)
		__field(uint8_t, count)
		__field(uint8_t, freecount)
		__field(uint64_t, freemask)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->startino = startino;
		__entry->holemask = holemask;
		__entry->count = count;
		__entry->freecount = freecount;
		__entry->freemask = freemask;
	),
	TP_printk("dev %d:%d agno %d startino %u holemask 0x%x count %u freecount %u freemask 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->startino,
		  __entry->holemask,
		  __entry->count,
		  __entry->freecount,
		  __entry->freemask)
)

#endif /* IS_ENABLED(CONFIG_XFS_ONLINE_REPAIR) */

#endif /* _TRACE_XFS_SCRUB_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE scrub/trace
#include <trace/define_trace.h>
