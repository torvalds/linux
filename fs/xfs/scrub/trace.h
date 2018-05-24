/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
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

#endif /* _TRACE_XFS_SCRUB_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE scrub/trace
#include <trace/define_trace.h>
