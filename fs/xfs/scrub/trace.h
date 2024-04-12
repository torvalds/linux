// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 *
 * NOTE: none of these tracepoints shall be considered a stable kernel ABI
 * as they can change at any time.  See xfs_trace.h for documentation of
 * specific units found in tracepoint output.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xfs_scrub

#if !defined(_TRACE_XFS_SCRUB_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XFS_SCRUB_TRACE_H

#include <linux/tracepoint.h>
#include "xfs_bit.h"
#include "xfs_quota_defs.h"

struct xfs_scrub;
struct xfile;
struct xfarray;
struct xfarray_sortinfo;
struct xchk_dqiter;
struct xchk_iscan;
struct xchk_nlink;
struct xchk_fscounters;
struct xfs_rmap_update_params;

/*
 * ftrace's __print_symbolic requires that all enum values be wrapped in the
 * TRACE_DEFINE_ENUM macro so that the enum value can be encoded in the ftrace
 * ring buffer.  Somehow this was only worth mentioning in the ftrace sample
 * code.
 */
TRACE_DEFINE_ENUM(XFS_REFC_DOMAIN_SHARED);
TRACE_DEFINE_ENUM(XFS_REFC_DOMAIN_COW);

TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_PROBE);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_SB);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_AGF);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_AGFL);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_AGI);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_BNOBT);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_CNTBT);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_INOBT);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_FINOBT);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_RMAPBT);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_REFCNTBT);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_INODE);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_BMBTD);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_BMBTA);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_BMBTC);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_DIR);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_XATTR);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_SYMLINK);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_PARENT);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_RTBITMAP);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_RTSUM);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_UQUOTA);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_GQUOTA);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_PQUOTA);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_FSCOUNTERS);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_QUOTACHECK);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_NLINKS);
TRACE_DEFINE_ENUM(XFS_SCRUB_TYPE_HEALTHY);

#define XFS_SCRUB_TYPE_STRINGS \
	{ XFS_SCRUB_TYPE_PROBE,		"probe" }, \
	{ XFS_SCRUB_TYPE_SB,		"sb" }, \
	{ XFS_SCRUB_TYPE_AGF,		"agf" }, \
	{ XFS_SCRUB_TYPE_AGFL,		"agfl" }, \
	{ XFS_SCRUB_TYPE_AGI,		"agi" }, \
	{ XFS_SCRUB_TYPE_BNOBT,		"bnobt" }, \
	{ XFS_SCRUB_TYPE_CNTBT,		"cntbt" }, \
	{ XFS_SCRUB_TYPE_INOBT,		"inobt" }, \
	{ XFS_SCRUB_TYPE_FINOBT,	"finobt" }, \
	{ XFS_SCRUB_TYPE_RMAPBT,	"rmapbt" }, \
	{ XFS_SCRUB_TYPE_REFCNTBT,	"refcountbt" }, \
	{ XFS_SCRUB_TYPE_INODE,		"inode" }, \
	{ XFS_SCRUB_TYPE_BMBTD,		"bmapbtd" }, \
	{ XFS_SCRUB_TYPE_BMBTA,		"bmapbta" }, \
	{ XFS_SCRUB_TYPE_BMBTC,		"bmapbtc" }, \
	{ XFS_SCRUB_TYPE_DIR,		"directory" }, \
	{ XFS_SCRUB_TYPE_XATTR,		"xattr" }, \
	{ XFS_SCRUB_TYPE_SYMLINK,	"symlink" }, \
	{ XFS_SCRUB_TYPE_PARENT,	"parent" }, \
	{ XFS_SCRUB_TYPE_RTBITMAP,	"rtbitmap" }, \
	{ XFS_SCRUB_TYPE_RTSUM,		"rtsummary" }, \
	{ XFS_SCRUB_TYPE_UQUOTA,	"usrquota" }, \
	{ XFS_SCRUB_TYPE_GQUOTA,	"grpquota" }, \
	{ XFS_SCRUB_TYPE_PQUOTA,	"prjquota" }, \
	{ XFS_SCRUB_TYPE_FSCOUNTERS,	"fscounters" }, \
	{ XFS_SCRUB_TYPE_QUOTACHECK,	"quotacheck" }, \
	{ XFS_SCRUB_TYPE_NLINKS,	"nlinks" }, \
	{ XFS_SCRUB_TYPE_HEALTHY,	"healthy" }

#define XFS_SCRUB_FLAG_STRINGS \
	{ XFS_SCRUB_IFLAG_REPAIR,		"repair" }, \
	{ XFS_SCRUB_OFLAG_CORRUPT,		"corrupt" }, \
	{ XFS_SCRUB_OFLAG_PREEN,		"preen" }, \
	{ XFS_SCRUB_OFLAG_XFAIL,		"xfail" }, \
	{ XFS_SCRUB_OFLAG_XCORRUPT,		"xcorrupt" }, \
	{ XFS_SCRUB_OFLAG_INCOMPLETE,		"incomplete" }, \
	{ XFS_SCRUB_OFLAG_WARNING,		"warning" }, \
	{ XFS_SCRUB_OFLAG_NO_REPAIR_NEEDED,	"norepair" }, \
	{ XFS_SCRUB_IFLAG_FORCE_REBUILD,	"rebuild" }

#define XFS_SCRUB_STATE_STRINGS \
	{ XCHK_TRY_HARDER,			"try_harder" }, \
	{ XCHK_HAVE_FREEZE_PROT,		"nofreeze" }, \
	{ XCHK_FSGATES_DRAIN,			"fsgates_drain" }, \
	{ XCHK_NEED_DRAIN,			"need_drain" }, \
	{ XCHK_FSGATES_QUOTA,			"fsgates_quota" }, \
	{ XCHK_FSGATES_DIRENTS,			"fsgates_dirents" }, \
	{ XCHK_FSGATES_RMAP,			"fsgates_rmap" }, \
	{ XREP_RESET_PERAG_RESV,		"reset_perag_resv" }, \
	{ XREP_ALREADY_FIXED,			"already_fixed" }

TRACE_DEFINE_ENUM(XFS_RMAP_MAP);
TRACE_DEFINE_ENUM(XFS_RMAP_MAP_SHARED);
TRACE_DEFINE_ENUM(XFS_RMAP_UNMAP);
TRACE_DEFINE_ENUM(XFS_RMAP_UNMAP_SHARED);
TRACE_DEFINE_ENUM(XFS_RMAP_CONVERT);
TRACE_DEFINE_ENUM(XFS_RMAP_CONVERT_SHARED);
TRACE_DEFINE_ENUM(XFS_RMAP_ALLOC);
TRACE_DEFINE_ENUM(XFS_RMAP_FREE);

DECLARE_EVENT_CLASS(xchk_class,
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
	TP_printk("dev %d:%d ino 0x%llx type %s agno 0x%x inum 0x%llx gen 0x%x flags (%s) error %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->agno,
		  __entry->inum,
		  __entry->gen,
		  __print_flags(__entry->flags, "|", XFS_SCRUB_FLAG_STRINGS),
		  __entry->error)
)
#define DEFINE_SCRUB_EVENT(name) \
DEFINE_EVENT(xchk_class, name, \
	TP_PROTO(struct xfs_inode *ip, struct xfs_scrub_metadata *sm, \
		 int error), \
	TP_ARGS(ip, sm, error))

DEFINE_SCRUB_EVENT(xchk_start);
DEFINE_SCRUB_EVENT(xchk_done);
DEFINE_SCRUB_EVENT(xchk_deadlock_retry);
DEFINE_SCRUB_EVENT(xrep_attempt);
DEFINE_SCRUB_EVENT(xrep_done);

DECLARE_EVENT_CLASS(xchk_fsgate_class,
	TP_PROTO(struct xfs_scrub *sc, unsigned int fsgate_flags),
	TP_ARGS(sc, fsgate_flags),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__field(unsigned int, fsgate_flags)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->fsgate_flags = fsgate_flags;
	),
	TP_printk("dev %d:%d type %s fsgates '%s'",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __print_flags(__entry->fsgate_flags, "|", XFS_SCRUB_STATE_STRINGS))
)

#define DEFINE_SCRUB_FSHOOK_EVENT(name) \
DEFINE_EVENT(xchk_fsgate_class, name, \
	TP_PROTO(struct xfs_scrub *sc, unsigned int fsgates_flags), \
	TP_ARGS(sc, fsgates_flags))

DEFINE_SCRUB_FSHOOK_EVENT(xchk_fsgates_enable);
DEFINE_SCRUB_FSHOOK_EVENT(xchk_fsgates_disable);

TRACE_EVENT(xchk_op_error,
	TP_PROTO(struct xfs_scrub *sc, xfs_agnumber_t agno,
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
	TP_printk("dev %d:%d type %s agno 0x%x agbno 0x%x error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->agno,
		  __entry->bno,
		  __entry->error,
		  __entry->ret_ip)
);

TRACE_EVENT(xchk_file_op_error,
	TP_PROTO(struct xfs_scrub *sc, int whichfork,
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
	TP_printk("dev %d:%d ino 0x%llx fork %s type %s fileoff 0x%llx error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->offset,
		  __entry->error,
		  __entry->ret_ip)
);

DECLARE_EVENT_CLASS(xchk_block_error_class,
	TP_PROTO(struct xfs_scrub *sc, xfs_daddr_t daddr, void *ret_ip),
	TP_ARGS(sc, daddr, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->agno = xfs_daddr_to_agno(sc->mp, daddr);
		__entry->agbno = xfs_daddr_to_agbno(sc->mp, daddr);
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %s agno 0x%x agbno 0x%x ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->agno,
		  __entry->agbno,
		  __entry->ret_ip)
)

#define DEFINE_SCRUB_BLOCK_ERROR_EVENT(name) \
DEFINE_EVENT(xchk_block_error_class, name, \
	TP_PROTO(struct xfs_scrub *sc, xfs_daddr_t daddr, \
		 void *ret_ip), \
	TP_ARGS(sc, daddr, ret_ip))

DEFINE_SCRUB_BLOCK_ERROR_EVENT(xchk_fs_error);
DEFINE_SCRUB_BLOCK_ERROR_EVENT(xchk_block_error);
DEFINE_SCRUB_BLOCK_ERROR_EVENT(xchk_block_preen);

DECLARE_EVENT_CLASS(xchk_ino_error_class,
	TP_PROTO(struct xfs_scrub *sc, xfs_ino_t ino, void *ret_ip),
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
	TP_printk("dev %d:%d ino 0x%llx type %s ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->ret_ip)
)

#define DEFINE_SCRUB_INO_ERROR_EVENT(name) \
DEFINE_EVENT(xchk_ino_error_class, name, \
	TP_PROTO(struct xfs_scrub *sc, xfs_ino_t ino, \
		 void *ret_ip), \
	TP_ARGS(sc, ino, ret_ip))

DEFINE_SCRUB_INO_ERROR_EVENT(xchk_ino_error);
DEFINE_SCRUB_INO_ERROR_EVENT(xchk_ino_preen);
DEFINE_SCRUB_INO_ERROR_EVENT(xchk_ino_warning);

DECLARE_EVENT_CLASS(xchk_fblock_error_class,
	TP_PROTO(struct xfs_scrub *sc, int whichfork,
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
	TP_printk("dev %d:%d ino 0x%llx fork %s type %s fileoff 0x%llx ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->offset,
		  __entry->ret_ip)
);

#define DEFINE_SCRUB_FBLOCK_ERROR_EVENT(name) \
DEFINE_EVENT(xchk_fblock_error_class, name, \
	TP_PROTO(struct xfs_scrub *sc, int whichfork, \
		 xfs_fileoff_t offset, void *ret_ip), \
	TP_ARGS(sc, whichfork, offset, ret_ip))

DEFINE_SCRUB_FBLOCK_ERROR_EVENT(xchk_fblock_error);
DEFINE_SCRUB_FBLOCK_ERROR_EVENT(xchk_fblock_warning);

#ifdef CONFIG_XFS_QUOTA
DECLARE_EVENT_CLASS(xchk_dqiter_class,
	TP_PROTO(struct xchk_dqiter *cursor, uint64_t id),
	TP_ARGS(cursor, id),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_dqtype_t, dqtype)
		__field(xfs_ino_t, ino)
		__field(unsigned long long, cur_id)
		__field(unsigned long long, id)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
		__field(xfs_exntst_t, state)
	),
	TP_fast_assign(
		__entry->dev = cursor->sc->ip->i_mount->m_super->s_dev;
		__entry->dqtype = cursor->dqtype;
		__entry->ino = cursor->quota_ip->i_ino;
		__entry->cur_id = cursor->id;
		__entry->startoff = cursor->bmap.br_startoff;
		__entry->startblock = cursor->bmap.br_startblock;
		__entry->blockcount = cursor->bmap.br_blockcount;
		__entry->state = cursor->bmap.br_state;
		__entry->id = id;
	),
	TP_printk("dev %d:%d dquot type %s ino 0x%llx cursor_id 0x%llx startoff 0x%llx startblock 0x%llx blockcount 0x%llx state %u id 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->dqtype, XFS_DQTYPE_STRINGS),
		  __entry->ino,
		  __entry->cur_id,
		  __entry->startoff,
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->state,
		  __entry->id)
);

#define DEFINE_SCRUB_DQITER_EVENT(name) \
DEFINE_EVENT(xchk_dqiter_class, name, \
	TP_PROTO(struct xchk_dqiter *cursor, uint64_t id), \
	TP_ARGS(cursor, id))
DEFINE_SCRUB_DQITER_EVENT(xchk_dquot_iter_revalidate_bmap);
DEFINE_SCRUB_DQITER_EVENT(xchk_dquot_iter_advance_bmap);
DEFINE_SCRUB_DQITER_EVENT(xchk_dquot_iter_advance_incore);
DEFINE_SCRUB_DQITER_EVENT(xchk_dquot_iter);

TRACE_EVENT(xchk_qcheck_error,
	TP_PROTO(struct xfs_scrub *sc, xfs_dqtype_t dqtype, xfs_dqid_t id,
		 void *ret_ip),
	TP_ARGS(sc, dqtype, id, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_dqtype_t, dqtype)
		__field(xfs_dqid_t, id)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->dqtype = dqtype;
		__entry->id = id;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d dquot type %s id 0x%x ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->dqtype, XFS_DQTYPE_STRINGS),
		  __entry->id,
		  __entry->ret_ip)
);
#endif /* CONFIG_XFS_QUOTA */

TRACE_EVENT(xchk_incomplete,
	TP_PROTO(struct xfs_scrub *sc, void *ret_ip),
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
	TP_printk("dev %d:%d type %s ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->ret_ip)
);

TRACE_EVENT(xchk_btree_op_error,
	TP_PROTO(struct xfs_scrub *sc, struct xfs_btree_cur *cur,
		 int level, int error, void *ret_ip),
	TP_ARGS(sc, cur, level, error, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__string(name, cur->bc_ops->name)
		__field(int, level)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, ptr)
		__field(int, error)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xchk_btree_cur_fsbno(cur, level);

		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__assign_str(name, cur->bc_ops->name);
		__entry->level = level;
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->ptr = cur->bc_levels[level].ptr;
		__entry->error = error;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %s %sbt level %d ptr %d agno 0x%x agbno 0x%x error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __get_str(name),
		  __entry->level,
		  __entry->ptr,
		  __entry->agno,
		  __entry->bno,
		  __entry->error,
		  __entry->ret_ip)
);

TRACE_EVENT(xchk_ifork_btree_op_error,
	TP_PROTO(struct xfs_scrub *sc, struct xfs_btree_cur *cur,
		 int level, int error, void *ret_ip),
	TP_ARGS(sc, cur, level, error, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, whichfork)
		__field(unsigned int, type)
		__string(name, cur->bc_ops->name)
		__field(int, level)
		__field(int, ptr)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, error)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xchk_btree_cur_fsbno(cur, level);
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->ip->i_ino;
		__entry->whichfork = cur->bc_ino.whichfork;
		__entry->type = sc->sm->sm_type;
		__assign_str(name, cur->bc_ops->name);
		__entry->level = level;
		__entry->ptr = cur->bc_levels[level].ptr;
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->error = error;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx fork %s type %s %sbt level %d ptr %d agno 0x%x agbno 0x%x error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __get_str(name),
		  __entry->level,
		  __entry->ptr,
		  __entry->agno,
		  __entry->bno,
		  __entry->error,
		  __entry->ret_ip)
);

TRACE_EVENT(xchk_btree_error,
	TP_PROTO(struct xfs_scrub *sc, struct xfs_btree_cur *cur,
		 int level, void *ret_ip),
	TP_ARGS(sc, cur, level, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__string(name, cur->bc_ops->name)
		__field(int, level)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, ptr)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xchk_btree_cur_fsbno(cur, level);
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__assign_str(name, cur->bc_ops->name);
		__entry->level = level;
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->ptr = cur->bc_levels[level].ptr;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d type %s %sbt level %d ptr %d agno 0x%x agbno 0x%x ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __get_str(name),
		  __entry->level,
		  __entry->ptr,
		  __entry->agno,
		  __entry->bno,
		  __entry->ret_ip)
);

TRACE_EVENT(xchk_ifork_btree_error,
	TP_PROTO(struct xfs_scrub *sc, struct xfs_btree_cur *cur,
		 int level, void *ret_ip),
	TP_ARGS(sc, cur, level, ret_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, whichfork)
		__field(unsigned int, type)
		__string(name, cur->bc_ops->name)
		__field(int, level)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, ptr)
		__field(void *, ret_ip)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xchk_btree_cur_fsbno(cur, level);
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->ip->i_ino;
		__entry->whichfork = cur->bc_ino.whichfork;
		__entry->type = sc->sm->sm_type;
		__assign_str(name, cur->bc_ops->name);
		__entry->level = level;
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->ptr = cur->bc_levels[level].ptr;
		__entry->ret_ip = ret_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx fork %s type %s %sbt level %d ptr %d agno 0x%x agbno 0x%x ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __get_str(name),
		  __entry->level,
		  __entry->ptr,
		  __entry->agno,
		  __entry->bno,
		  __entry->ret_ip)
);

DECLARE_EVENT_CLASS(xchk_sbtree_class,
	TP_PROTO(struct xfs_scrub *sc, struct xfs_btree_cur *cur,
		 int level),
	TP_ARGS(sc, cur, level),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, type)
		__string(name, cur->bc_ops->name)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, bno)
		__field(int, level)
		__field(int, nlevels)
		__field(int, ptr)
	),
	TP_fast_assign(
		xfs_fsblock_t fsbno = xchk_btree_cur_fsbno(cur, level);

		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__assign_str(name, cur->bc_ops->name);
		__entry->agno = XFS_FSB_TO_AGNO(cur->bc_mp, fsbno);
		__entry->bno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
		__entry->level = level;
		__entry->nlevels = cur->bc_nlevels;
		__entry->ptr = cur->bc_levels[level].ptr;
	),
	TP_printk("dev %d:%d type %s %sbt agno 0x%x agbno 0x%x level %d nlevels %d ptr %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __get_str(name),
		  __entry->agno,
		  __entry->bno,
		  __entry->level,
		  __entry->nlevels,
		  __entry->ptr)
)
#define DEFINE_SCRUB_SBTREE_EVENT(name) \
DEFINE_EVENT(xchk_sbtree_class, name, \
	TP_PROTO(struct xfs_scrub *sc, struct xfs_btree_cur *cur, \
		 int level), \
	TP_ARGS(sc, cur, level))

DEFINE_SCRUB_SBTREE_EVENT(xchk_btree_rec);
DEFINE_SCRUB_SBTREE_EVENT(xchk_btree_key);

TRACE_EVENT(xchk_xref_error,
	TP_PROTO(struct xfs_scrub *sc, int error, void *ret_ip),
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
	TP_printk("dev %d:%d type %s xref error %d ret_ip %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->error,
		  __entry->ret_ip)
);

TRACE_EVENT(xchk_iallocbt_check_cluster,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agino_t startino, xfs_daddr_t map_daddr,
		 unsigned short map_len, unsigned int chunk_ino,
		 unsigned int nr_inodes, uint16_t cluster_mask,
		 uint16_t holemask, unsigned int cluster_ino),
	TP_ARGS(mp, agno, startino, map_daddr, map_len, chunk_ino, nr_inodes,
		cluster_mask, holemask, cluster_ino),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agino_t, startino)
		__field(xfs_daddr_t, map_daddr)
		__field(unsigned short, map_len)
		__field(unsigned int, chunk_ino)
		__field(unsigned int, nr_inodes)
		__field(unsigned int, cluster_ino)
		__field(uint16_t, cluster_mask)
		__field(uint16_t, holemask)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->startino = startino;
		__entry->map_daddr = map_daddr;
		__entry->map_len = map_len;
		__entry->chunk_ino = chunk_ino;
		__entry->nr_inodes = nr_inodes;
		__entry->cluster_mask = cluster_mask;
		__entry->holemask = holemask;
		__entry->cluster_ino = cluster_ino;
	),
	TP_printk("dev %d:%d agno 0x%x startino 0x%x daddr 0x%llx bbcount 0x%x chunkino 0x%x nr_inodes %u cluster_mask 0x%x holemask 0x%x cluster_ino 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->startino,
		  __entry->map_daddr,
		  __entry->map_len,
		  __entry->chunk_ino,
		  __entry->nr_inodes,
		  __entry->cluster_mask,
		  __entry->holemask,
		  __entry->cluster_ino)
)

TRACE_EVENT(xchk_inode_is_allocated,
	TP_PROTO(struct xfs_inode *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned long, iflags)
		__field(umode_t, mode)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->iflags = ip->i_flags;
		__entry->mode = VFS_I(ip)->i_mode;
	),
	TP_printk("dev %d:%d ino 0x%llx iflags 0x%lx mode 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->iflags,
		  __entry->mode)
);

TRACE_EVENT(xchk_fscounters_calc,
	TP_PROTO(struct xfs_mount *mp, uint64_t icount, uint64_t ifree,
		 uint64_t fdblocks, uint64_t delalloc),
	TP_ARGS(mp, icount, ifree, fdblocks, delalloc),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int64_t, icount_sb)
		__field(uint64_t, icount_calculated)
		__field(int64_t, ifree_sb)
		__field(uint64_t, ifree_calculated)
		__field(int64_t, fdblocks_sb)
		__field(uint64_t, fdblocks_calculated)
		__field(uint64_t, delalloc)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->icount_sb = mp->m_sb.sb_icount;
		__entry->icount_calculated = icount;
		__entry->ifree_sb = mp->m_sb.sb_ifree;
		__entry->ifree_calculated = ifree;
		__entry->fdblocks_sb = mp->m_sb.sb_fdblocks;
		__entry->fdblocks_calculated = fdblocks;
		__entry->delalloc = delalloc;
	),
	TP_printk("dev %d:%d icount %lld:%llu ifree %lld::%llu fdblocks %lld::%llu delalloc %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->icount_sb,
		  __entry->icount_calculated,
		  __entry->ifree_sb,
		  __entry->ifree_calculated,
		  __entry->fdblocks_sb,
		  __entry->fdblocks_calculated,
		  __entry->delalloc)
)

TRACE_EVENT(xchk_fscounters_within_range,
	TP_PROTO(struct xfs_mount *mp, uint64_t expected, int64_t curr_value,
		 int64_t old_value),
	TP_ARGS(mp, expected, curr_value, old_value),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(uint64_t, expected)
		__field(int64_t, curr_value)
		__field(int64_t, old_value)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->expected = expected;
		__entry->curr_value = curr_value;
		__entry->old_value = old_value;
	),
	TP_printk("dev %d:%d expected %llu curr_value %lld old_value %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->expected,
		  __entry->curr_value,
		  __entry->old_value)
)

DECLARE_EVENT_CLASS(xchk_fsfreeze_class,
	TP_PROTO(struct xfs_scrub *sc, int error),
	TP_ARGS(sc, error),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned int, type)
		__field(int, error)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->type = sc->sm->sm_type;
		__entry->error = error;
	),
	TP_printk("dev %d:%d type %s error %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->type, XFS_SCRUB_TYPE_STRINGS),
		  __entry->error)
);
#define DEFINE_XCHK_FSFREEZE_EVENT(name) \
DEFINE_EVENT(xchk_fsfreeze_class, name, \
	TP_PROTO(struct xfs_scrub *sc, int error), \
	TP_ARGS(sc, error))
DEFINE_XCHK_FSFREEZE_EVENT(xchk_fsfreeze);
DEFINE_XCHK_FSFREEZE_EVENT(xchk_fsthaw);

TRACE_EVENT(xchk_refcount_incorrect,
	TP_PROTO(struct xfs_perag *pag, const struct xfs_refcount_irec *irec,
		 xfs_nlink_t seen),
	TP_ARGS(pag, irec, seen),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(enum xfs_refc_domain, domain)
		__field(xfs_agblock_t, startblock)
		__field(xfs_extlen_t, blockcount)
		__field(xfs_nlink_t, refcount)
		__field(xfs_nlink_t, seen)
	),
	TP_fast_assign(
		__entry->dev = pag->pag_mount->m_super->s_dev;
		__entry->agno = pag->pag_agno;
		__entry->domain = irec->rc_domain;
		__entry->startblock = irec->rc_startblock;
		__entry->blockcount = irec->rc_blockcount;
		__entry->refcount = irec->rc_refcount;
		__entry->seen = seen;
	),
	TP_printk("dev %d:%d agno 0x%x dom %s agbno 0x%x fsbcount 0x%x refcount %u seen %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __print_symbolic(__entry->domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->refcount,
		  __entry->seen)
)

TRACE_EVENT(xfile_create,
	TP_PROTO(struct xfile *xf),
	TP_ARGS(xf),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, ino)
		__array(char, pathname, 256)
	),
	TP_fast_assign(
		char		pathname[257];
		char		*path;

		__entry->ino = file_inode(xf->file)->i_ino;
		memset(pathname, 0, sizeof(pathname));
		path = file_path(xf->file, pathname, sizeof(pathname) - 1);
		if (IS_ERR(path))
			path = "(unknown)";
		strncpy(__entry->pathname, path, sizeof(__entry->pathname));
	),
	TP_printk("xfino 0x%lx path '%s'",
		  __entry->ino,
		  __entry->pathname)
);

TRACE_EVENT(xfile_destroy,
	TP_PROTO(struct xfile *xf),
	TP_ARGS(xf),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long long, bytes)
		__field(loff_t, size)
	),
	TP_fast_assign(
		struct inode		*inode = file_inode(xf->file);

		__entry->ino = inode->i_ino;
		__entry->bytes = inode->i_blocks << SECTOR_SHIFT;
		__entry->size = i_size_read(inode);
	),
	TP_printk("xfino 0x%lx mem_bytes 0x%llx isize 0x%llx",
		  __entry->ino,
		  __entry->bytes,
		  __entry->size)
);

DECLARE_EVENT_CLASS(xfile_class,
	TP_PROTO(struct xfile *xf, loff_t pos, unsigned long long bytecount),
	TP_ARGS(xf, pos, bytecount),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long long, bytes_used)
		__field(loff_t, pos)
		__field(loff_t, size)
		__field(unsigned long long, bytecount)
	),
	TP_fast_assign(
		struct inode		*inode = file_inode(xf->file);

		__entry->ino = inode->i_ino;
		__entry->bytes_used = inode->i_blocks << SECTOR_SHIFT;
		__entry->pos = pos;
		__entry->size = i_size_read(inode);
		__entry->bytecount = bytecount;
	),
	TP_printk("xfino 0x%lx mem_bytes 0x%llx pos 0x%llx bytecount 0x%llx isize 0x%llx",
		  __entry->ino,
		  __entry->bytes_used,
		  __entry->pos,
		  __entry->bytecount,
		  __entry->size)
);
#define DEFINE_XFILE_EVENT(name) \
DEFINE_EVENT(xfile_class, name, \
	TP_PROTO(struct xfile *xf, loff_t pos, unsigned long long bytecount), \
	TP_ARGS(xf, pos, bytecount))
DEFINE_XFILE_EVENT(xfile_load);
DEFINE_XFILE_EVENT(xfile_store);
DEFINE_XFILE_EVENT(xfile_seek_data);
DEFINE_XFILE_EVENT(xfile_get_folio);
DEFINE_XFILE_EVENT(xfile_put_folio);

TRACE_EVENT(xfarray_create,
	TP_PROTO(struct xfarray *xfa, unsigned long long required_capacity),
	TP_ARGS(xfa, required_capacity),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(uint64_t, max_nr)
		__field(size_t, obj_size)
		__field(int, obj_size_log)
		__field(unsigned long long, required_capacity)
	),
	TP_fast_assign(
		__entry->max_nr = xfa->max_nr;
		__entry->obj_size = xfa->obj_size;
		__entry->obj_size_log = xfa->obj_size_log;
		__entry->ino = file_inode(xfa->xfile->file)->i_ino;
		__entry->required_capacity = required_capacity;
	),
	TP_printk("xfino 0x%lx max_nr %llu reqd_nr %llu objsz %zu objszlog %d",
		  __entry->ino,
		  __entry->max_nr,
		  __entry->required_capacity,
		  __entry->obj_size,
		  __entry->obj_size_log)
);

TRACE_EVENT(xfarray_isort,
	TP_PROTO(struct xfarray_sortinfo *si, uint64_t lo, uint64_t hi),
	TP_ARGS(si, lo, hi),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long long, lo)
		__field(unsigned long long, hi)
	),
	TP_fast_assign(
		__entry->ino = file_inode(si->array->xfile->file)->i_ino;
		__entry->lo = lo;
		__entry->hi = hi;
	),
	TP_printk("xfino 0x%lx lo %llu hi %llu elts %llu",
		  __entry->ino,
		  __entry->lo,
		  __entry->hi,
		  __entry->hi - __entry->lo)
);

TRACE_EVENT(xfarray_foliosort,
	TP_PROTO(struct xfarray_sortinfo *si, uint64_t lo, uint64_t hi),
	TP_ARGS(si, lo, hi),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long long, lo)
		__field(unsigned long long, hi)
	),
	TP_fast_assign(
		__entry->ino = file_inode(si->array->xfile->file)->i_ino;
		__entry->lo = lo;
		__entry->hi = hi;
	),
	TP_printk("xfino 0x%lx lo %llu hi %llu elts %llu",
		  __entry->ino,
		  __entry->lo,
		  __entry->hi,
		  __entry->hi - __entry->lo)
);

TRACE_EVENT(xfarray_qsort,
	TP_PROTO(struct xfarray_sortinfo *si, uint64_t lo, uint64_t hi),
	TP_ARGS(si, lo, hi),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long long, lo)
		__field(unsigned long long, hi)
		__field(int, stack_depth)
		__field(int, max_stack_depth)
	),
	TP_fast_assign(
		__entry->ino = file_inode(si->array->xfile->file)->i_ino;
		__entry->lo = lo;
		__entry->hi = hi;
		__entry->stack_depth = si->stack_depth;
		__entry->max_stack_depth = si->max_stack_depth;
	),
	TP_printk("xfino 0x%lx lo %llu hi %llu elts %llu stack %d/%d",
		  __entry->ino,
		  __entry->lo,
		  __entry->hi,
		  __entry->hi - __entry->lo,
		  __entry->stack_depth,
		  __entry->max_stack_depth)
);

TRACE_EVENT(xfarray_sort,
	TP_PROTO(struct xfarray_sortinfo *si, size_t bytes),
	TP_ARGS(si, bytes),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long long, nr)
		__field(size_t, obj_size)
		__field(size_t, bytes)
		__field(unsigned int, max_stack_depth)
	),
	TP_fast_assign(
		__entry->nr = si->array->nr;
		__entry->obj_size = si->array->obj_size;
		__entry->ino = file_inode(si->array->xfile->file)->i_ino;
		__entry->bytes = bytes;
		__entry->max_stack_depth = si->max_stack_depth;
	),
	TP_printk("xfino 0x%lx nr %llu objsz %zu stack %u bytes %zu",
		  __entry->ino,
		  __entry->nr,
		  __entry->obj_size,
		  __entry->max_stack_depth,
		  __entry->bytes)
);

TRACE_EVENT(xfarray_sort_scan,
	TP_PROTO(struct xfarray_sortinfo *si, unsigned long long idx),
	TP_ARGS(si, idx),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(unsigned long long, nr)
		__field(size_t, obj_size)
		__field(unsigned long long, idx)
		__field(unsigned long long, folio_pos)
		__field(unsigned long, folio_bytes)
		__field(unsigned long long, first_idx)
		__field(unsigned long long, last_idx)
	),
	TP_fast_assign(
		__entry->nr = si->array->nr;
		__entry->obj_size = si->array->obj_size;
		__entry->ino = file_inode(si->array->xfile->file)->i_ino;
		__entry->idx = idx;
		if (si->folio) {
			__entry->folio_pos = folio_pos(si->folio);
			__entry->folio_bytes = folio_size(si->folio);
			__entry->first_idx = si->first_folio_idx;
			__entry->last_idx = si->last_folio_idx;
		} else {
			__entry->folio_pos = 0;
			__entry->folio_bytes = 0;
			__entry->first_idx = 0;
			__entry->last_idx = 0;
		}
	),
	TP_printk("xfino 0x%lx nr %llu objsz %zu idx %llu folio_pos 0x%llx folio_bytes 0x%lx first_idx %llu last_idx %llu",
		  __entry->ino,
		  __entry->nr,
		  __entry->obj_size,
		  __entry->idx,
		  __entry->folio_pos,
		  __entry->folio_bytes,
		  __entry->first_idx,
		  __entry->last_idx)
);

TRACE_EVENT(xfarray_sort_stats,
	TP_PROTO(struct xfarray_sortinfo *si, int error),
	TP_ARGS(si, error),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
#ifdef DEBUG
		__field(unsigned long long, loads)
		__field(unsigned long long, stores)
		__field(unsigned long long, compares)
		__field(unsigned long long, heapsorts)
#endif
		__field(unsigned int, max_stack_depth)
		__field(unsigned int, max_stack_used)
		__field(int, error)
	),
	TP_fast_assign(
		__entry->ino = file_inode(si->array->xfile->file)->i_ino;
#ifdef DEBUG
		__entry->loads = si->loads;
		__entry->stores = si->stores;
		__entry->compares = si->compares;
		__entry->heapsorts = si->heapsorts;
#endif
		__entry->max_stack_depth = si->max_stack_depth;
		__entry->max_stack_used = si->max_stack_used;
		__entry->error = error;
	),
	TP_printk(
#ifdef DEBUG
		  "xfino 0x%lx loads %llu stores %llu compares %llu heapsorts %llu stack_depth %u/%u error %d",
#else
		  "xfino 0x%lx stack_depth %u/%u error %d",
#endif
		  __entry->ino,
#ifdef DEBUG
		  __entry->loads,
		  __entry->stores,
		  __entry->compares,
		  __entry->heapsorts,
#endif
		  __entry->max_stack_used,
		  __entry->max_stack_depth,
		  __entry->error)
);

#ifdef CONFIG_XFS_RT
TRACE_EVENT(xchk_rtsum_record_free,
	TP_PROTO(struct xfs_mount *mp, xfs_rtxnum_t start,
		 xfs_rtbxlen_t len, unsigned int log, loff_t pos,
		 xfs_suminfo_t value),
	TP_ARGS(mp, start, len, log, pos, value),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(dev_t, rtdev)
		__field(xfs_rtxnum_t, start)
		__field(unsigned long long, len)
		__field(unsigned int, log)
		__field(loff_t, pos)
		__field(xfs_suminfo_t, value)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->rtdev = mp->m_rtdev_targp->bt_dev;
		__entry->start = start;
		__entry->len = len;
		__entry->log = log;
		__entry->pos = pos;
		__entry->value = value;
	),
	TP_printk("dev %d:%d rtdev %d:%d rtx 0x%llx rtxcount 0x%llx log %u rsumpos 0x%llx sumcount %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  MAJOR(__entry->rtdev), MINOR(__entry->rtdev),
		  __entry->start,
		  __entry->len,
		  __entry->log,
		  __entry->pos,
		  __entry->value)
);
#endif /* CONFIG_XFS_RT */

DECLARE_EVENT_CLASS(xchk_iscan_class,
	TP_PROTO(struct xchk_iscan *iscan),
	TP_ARGS(iscan),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, cursor)
		__field(xfs_ino_t, visited)
	),
	TP_fast_assign(
		__entry->dev = iscan->sc->mp->m_super->s_dev;
		__entry->cursor = iscan->cursor_ino;
		__entry->visited = iscan->__visited_ino;
	),
	TP_printk("dev %d:%d iscan cursor 0x%llx visited 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->cursor,
		  __entry->visited)
)
#define DEFINE_ISCAN_EVENT(name) \
DEFINE_EVENT(xchk_iscan_class, name, \
	TP_PROTO(struct xchk_iscan *iscan), \
	TP_ARGS(iscan))
DEFINE_ISCAN_EVENT(xchk_iscan_move_cursor);
DEFINE_ISCAN_EVENT(xchk_iscan_visit);
DEFINE_ISCAN_EVENT(xchk_iscan_skip);
DEFINE_ISCAN_EVENT(xchk_iscan_advance_ag);

DECLARE_EVENT_CLASS(xchk_iscan_ino_class,
	TP_PROTO(struct xchk_iscan *iscan, xfs_ino_t ino),
	TP_ARGS(iscan, ino),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, startino)
		__field(xfs_ino_t, cursor)
		__field(xfs_ino_t, visited)
		__field(xfs_ino_t, ino)
	),
	TP_fast_assign(
		__entry->dev = iscan->sc->mp->m_super->s_dev;
		__entry->startino = iscan->scan_start_ino;
		__entry->cursor = iscan->cursor_ino;
		__entry->visited = iscan->__visited_ino;
		__entry->ino = ino;
	),
	TP_printk("dev %d:%d iscan start 0x%llx cursor 0x%llx visited 0x%llx ino 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->startino,
		  __entry->cursor,
		  __entry->visited,
		  __entry->ino)
)
#define DEFINE_ISCAN_INO_EVENT(name) \
DEFINE_EVENT(xchk_iscan_ino_class, name, \
	TP_PROTO(struct xchk_iscan *iscan, xfs_ino_t ino), \
	TP_ARGS(iscan, ino))
DEFINE_ISCAN_INO_EVENT(xchk_iscan_want_live_update);
DEFINE_ISCAN_INO_EVENT(xchk_iscan_start);

TRACE_EVENT(xchk_iscan_iget,
	TP_PROTO(struct xchk_iscan *iscan, int error),
	TP_ARGS(iscan, error),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, cursor)
		__field(xfs_ino_t, visited)
		__field(int, error)
	),
	TP_fast_assign(
		__entry->dev = iscan->sc->mp->m_super->s_dev;
		__entry->cursor = iscan->cursor_ino;
		__entry->visited = iscan->__visited_ino;
		__entry->error = error;
	),
	TP_printk("dev %d:%d iscan cursor 0x%llx visited 0x%llx error %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->cursor,
		  __entry->visited,
		  __entry->error)
);

TRACE_EVENT(xchk_iscan_iget_batch,
	TP_PROTO(struct xfs_mount *mp, struct xchk_iscan *iscan,
		 unsigned int nr, unsigned int avail),
	TP_ARGS(mp, iscan, nr, avail),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, cursor)
		__field(xfs_ino_t, visited)
		__field(unsigned int, nr)
		__field(unsigned int, avail)
		__field(unsigned int, unavail)
		__field(xfs_ino_t, batch_ino)
		__field(unsigned long long, skipmask)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->cursor = iscan->cursor_ino;
		__entry->visited = iscan->__visited_ino;
		__entry->nr = nr;
		__entry->avail = avail;
		__entry->unavail = hweight64(iscan->__skipped_inomask);
		__entry->batch_ino = iscan->__batch_ino;
		__entry->skipmask = iscan->__skipped_inomask;
	),
	TP_printk("dev %d:%d iscan cursor 0x%llx visited 0x%llx batchino 0x%llx skipmask 0x%llx nr %u avail %u unavail %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->cursor,
		  __entry->visited,
		  __entry->batch_ino,
		  __entry->skipmask,
		  __entry->nr,
		  __entry->avail,
		  __entry->unavail)
);

TRACE_EVENT(xchk_iscan_iget_retry_wait,
	TP_PROTO(struct xchk_iscan *iscan),
	TP_ARGS(iscan),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, cursor)
		__field(xfs_ino_t, visited)
		__field(unsigned int, retry_delay)
		__field(unsigned long, remaining)
		__field(unsigned int, iget_timeout)
	),
	TP_fast_assign(
		__entry->dev = iscan->sc->mp->m_super->s_dev;
		__entry->cursor = iscan->cursor_ino;
		__entry->visited = iscan->__visited_ino;
		__entry->retry_delay = iscan->iget_retry_delay;
		__entry->remaining = jiffies_to_msecs(iscan->__iget_deadline - jiffies);
		__entry->iget_timeout = iscan->iget_timeout;
	),
	TP_printk("dev %d:%d iscan cursor 0x%llx visited 0x%llx remaining %lu timeout %u delay %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->cursor,
		  __entry->visited,
		  __entry->remaining,
		  __entry->iget_timeout,
		  __entry->retry_delay)
);

TRACE_EVENT(xchk_nlinks_collect_dirent,
	TP_PROTO(struct xfs_mount *mp, struct xfs_inode *dp,
		 xfs_ino_t ino, const struct xfs_name *name),
	TP_ARGS(mp, dp, ino, name),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, dir)
		__field(xfs_ino_t, ino)
		__field(unsigned int, namelen)
		__dynamic_array(char, name, name->len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->dir = dp->i_ino;
		__entry->ino = ino;
		__entry->namelen = name->len;
		memcpy(__get_str(name), name->name, name->len);
	),
	TP_printk("dev %d:%d dir 0x%llx -> ino 0x%llx name '%.*s'",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dir,
		  __entry->ino,
		  __entry->namelen,
		  __get_str(name))
);

TRACE_EVENT(xchk_nlinks_collect_metafile,
	TP_PROTO(struct xfs_mount *mp, xfs_ino_t ino),
	TP_ARGS(mp, ino),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->ino = ino;
	),
	TP_printk("dev %d:%d ino 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino)
);

TRACE_EVENT(xchk_nlinks_live_update,
	TP_PROTO(struct xfs_mount *mp, const struct xfs_inode *dp,
		 int action, xfs_ino_t ino, int delta,
		 const char *name, unsigned int namelen),
	TP_ARGS(mp, dp, action, ino, delta, name, namelen),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, dir)
		__field(int, action)
		__field(xfs_ino_t, ino)
		__field(int, delta)
		__field(unsigned int, namelen)
		__dynamic_array(char, name, namelen)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->dir = dp ? dp->i_ino : NULLFSINO;
		__entry->action = action;
		__entry->ino = ino;
		__entry->delta = delta;
		__entry->namelen = namelen;
		memcpy(__get_str(name), name, namelen);
	),
	TP_printk("dev %d:%d dir 0x%llx ino 0x%llx nlink_delta %d name '%.*s'",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dir,
		  __entry->ino,
		  __entry->delta,
		  __entry->namelen,
		  __get_str(name))
);

TRACE_EVENT(xchk_nlinks_check_zero,
	TP_PROTO(struct xfs_mount *mp, xfs_ino_t ino,
		 const struct xchk_nlink *live),
	TP_ARGS(mp, ino, live),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_nlink_t, parents)
		__field(xfs_nlink_t, backrefs)
		__field(xfs_nlink_t, children)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->ino = ino;
		__entry->parents = live->parents;
		__entry->backrefs = live->backrefs;
		__entry->children = live->children;
	),
	TP_printk("dev %d:%d ino 0x%llx parents %u backrefs %u children %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->parents,
		  __entry->backrefs,
		  __entry->children)
);

TRACE_EVENT(xchk_nlinks_update_incore,
	TP_PROTO(struct xfs_mount *mp, xfs_ino_t ino,
		 const struct xchk_nlink *live, int parents_delta,
		 int backrefs_delta, int children_delta),
	TP_ARGS(mp, ino, live, parents_delta, backrefs_delta, children_delta),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_nlink_t, parents)
		__field(xfs_nlink_t, backrefs)
		__field(xfs_nlink_t, children)
		__field(int, parents_delta)
		__field(int, backrefs_delta)
		__field(int, children_delta)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->ino = ino;
		__entry->parents = live->parents;
		__entry->backrefs = live->backrefs;
		__entry->children = live->children;
		__entry->parents_delta = parents_delta;
		__entry->backrefs_delta = backrefs_delta;
		__entry->children_delta = children_delta;
	),
	TP_printk("dev %d:%d ino 0x%llx parents %d:%u backrefs %d:%u children %d:%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->parents_delta,
		  __entry->parents,
		  __entry->backrefs_delta,
		  __entry->backrefs,
		  __entry->children_delta,
		  __entry->children)
);

DECLARE_EVENT_CLASS(xchk_nlinks_diff_class,
	TP_PROTO(struct xfs_mount *mp, struct xfs_inode *ip,
		 const struct xchk_nlink *live),
	TP_ARGS(mp, ip, live),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(uint8_t, ftype)
		__field(xfs_nlink_t, nlink)
		__field(xfs_nlink_t, parents)
		__field(xfs_nlink_t, backrefs)
		__field(xfs_nlink_t, children)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->ino = ip->i_ino;
		__entry->ftype = xfs_mode_to_ftype(VFS_I(ip)->i_mode);
		__entry->nlink = VFS_I(ip)->i_nlink;
		__entry->parents = live->parents;
		__entry->backrefs = live->backrefs;
		__entry->children = live->children;
	),
	TP_printk("dev %d:%d ino 0x%llx ftype %s nlink %u parents %u backrefs %u children %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->ftype, XFS_DIR3_FTYPE_STR),
		  __entry->nlink,
		  __entry->parents,
		  __entry->backrefs,
		  __entry->children)
);
#define DEFINE_SCRUB_NLINKS_DIFF_EVENT(name) \
DEFINE_EVENT(xchk_nlinks_diff_class, name, \
	TP_PROTO(struct xfs_mount *mp, struct xfs_inode *ip, \
		 const struct xchk_nlink *live), \
	TP_ARGS(mp, ip, live))
DEFINE_SCRUB_NLINKS_DIFF_EVENT(xchk_nlinks_compare_inode);

/* repair tracepoints */
#if IS_ENABLED(CONFIG_XFS_ONLINE_REPAIR)

DECLARE_EVENT_CLASS(xrep_extent_class,
	TP_PROTO(struct xfs_perag *pag, xfs_agblock_t agbno, xfs_extlen_t len),
	TP_ARGS(pag, agbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = pag->pag_mount->m_super->s_dev;
		__entry->agno = pag->pag_agno;
		__entry->agbno = agbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len)
);
#define DEFINE_REPAIR_EXTENT_EVENT(name) \
DEFINE_EVENT(xrep_extent_class, name, \
	TP_PROTO(struct xfs_perag *pag, xfs_agblock_t agbno, xfs_extlen_t len), \
	TP_ARGS(pag, agbno, len))
DEFINE_REPAIR_EXTENT_EVENT(xreap_dispose_unmap_extent);
DEFINE_REPAIR_EXTENT_EVENT(xreap_dispose_free_extent);
DEFINE_REPAIR_EXTENT_EVENT(xreap_agextent_binval);
DEFINE_REPAIR_EXTENT_EVENT(xrep_agfl_insert);

DECLARE_EVENT_CLASS(xrep_reap_find_class,
	TP_PROTO(struct xfs_perag *pag, xfs_agblock_t agbno, xfs_extlen_t len,
		bool crosslinked),
	TP_ARGS(pag, agbno, len, crosslinked),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(bool, crosslinked)
	),
	TP_fast_assign(
		__entry->dev = pag->pag_mount->m_super->s_dev;
		__entry->agno = pag->pag_agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->crosslinked = crosslinked;
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x crosslinked %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->crosslinked ? 1 : 0)
);
#define DEFINE_REPAIR_REAP_FIND_EVENT(name) \
DEFINE_EVENT(xrep_reap_find_class, name, \
	TP_PROTO(struct xfs_perag *pag, xfs_agblock_t agbno, xfs_extlen_t len, \
		 bool crosslinked), \
	TP_ARGS(pag, agbno, len, crosslinked))
DEFINE_REPAIR_REAP_FIND_EVENT(xreap_agextent_select);

DECLARE_EVENT_CLASS(xrep_rmap_class,
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
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x owner 0x%llx fileoff 0x%llx flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);
#define DEFINE_REPAIR_RMAP_EVENT(name) \
DEFINE_EVENT(xrep_rmap_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_extlen_t len, \
		 uint64_t owner, uint64_t offset, unsigned int flags), \
	TP_ARGS(mp, agno, agbno, len, owner, offset, flags))
DEFINE_REPAIR_RMAP_EVENT(xrep_ibt_walk_rmap);
DEFINE_REPAIR_RMAP_EVENT(xrep_bmap_walk_rmap);

TRACE_EVENT(xrep_abt_found,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 const struct xfs_alloc_rec_incore *rec),
	TP_ARGS(mp, agno, rec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, startblock)
		__field(xfs_extlen_t, blockcount)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->startblock = rec->ar_startblock;
		__entry->blockcount = rec->ar_blockcount;
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->startblock,
		  __entry->blockcount)
)

TRACE_EVENT(xrep_ibt_found,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 const struct xfs_inobt_rec_incore *rec),
	TP_ARGS(mp, agno, rec),
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
		__entry->startino = rec->ir_startino;
		__entry->holemask = rec->ir_holemask;
		__entry->count = rec->ir_count;
		__entry->freecount = rec->ir_freecount;
		__entry->freemask = rec->ir_free;
	),
	TP_printk("dev %d:%d agno 0x%x agino 0x%x holemask 0x%x count 0x%x freecount 0x%x freemask 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->startino,
		  __entry->holemask,
		  __entry->count,
		  __entry->freecount,
		  __entry->freemask)
)

TRACE_EVENT(xrep_refc_found,
	TP_PROTO(struct xfs_perag *pag, const struct xfs_refcount_irec *rec),
	TP_ARGS(pag, rec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(enum xfs_refc_domain, domain)
		__field(xfs_agblock_t, startblock)
		__field(xfs_extlen_t, blockcount)
		__field(xfs_nlink_t, refcount)
	),
	TP_fast_assign(
		__entry->dev = pag->pag_mount->m_super->s_dev;
		__entry->agno = pag->pag_agno;
		__entry->domain = rec->rc_domain;
		__entry->startblock = rec->rc_startblock;
		__entry->blockcount = rec->rc_blockcount;
		__entry->refcount = rec->rc_refcount;
	),
	TP_printk("dev %d:%d agno 0x%x dom %s agbno 0x%x fsbcount 0x%x refcount %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __print_symbolic(__entry->domain, XFS_REFC_DOMAIN_STRINGS),
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->refcount)
)

TRACE_EVENT(xrep_bmap_found,
	TP_PROTO(struct xfs_inode *ip, int whichfork,
		 struct xfs_bmbt_irec *irec),
	TP_ARGS(ip, whichfork, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, whichfork)
		__field(xfs_fileoff_t, lblk)
		__field(xfs_filblks_t, len)
		__field(xfs_fsblock_t, pblk)
		__field(int, state)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->whichfork = whichfork;
		__entry->lblk = irec->br_startoff;
		__entry->len = irec->br_blockcount;
		__entry->pblk = irec->br_startblock;
		__entry->state = irec->br_state;
	),
	TP_printk("dev %d:%d ino 0x%llx whichfork %s fileoff 0x%llx fsbcount 0x%llx startblock 0x%llx state %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->whichfork, XFS_WHICHFORK_STRINGS),
		  __entry->lblk,
		  __entry->len,
		  __entry->pblk,
		  __entry->state)
);

TRACE_EVENT(xrep_rmap_found,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 const struct xfs_rmap_irec *rec),
	TP_ARGS(mp, agno, rec),
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
		__entry->agbno = rec->rm_startblock;
		__entry->len = rec->rm_blockcount;
		__entry->owner = rec->rm_owner;
		__entry->offset = rec->rm_offset;
		__entry->flags = rec->rm_flags;
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x owner 0x%llx fileoff 0x%llx flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);

TRACE_EVENT(xrep_findroot_block,
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
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x magic 0x%x level %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->magic,
		  __entry->level)
)
TRACE_EVENT(xrep_calc_ag_resblks,
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
	TP_printk("dev %d:%d agno 0x%x icount %u aglen %u freelen %u usedlen %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->icount,
		  __entry->aglen,
		  __entry->freelen,
		  __entry->usedlen)
)
TRACE_EVENT(xrep_calc_ag_resblks_btsize,
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
	TP_printk("dev %d:%d agno 0x%x bnobt %u inobt %u rmapbt %u refcountbt %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->bnobt_sz,
		  __entry->inobt_sz,
		  __entry->rmapbt_sz,
		  __entry->refcbt_sz)
)
TRACE_EVENT(xrep_reset_counters,
	TP_PROTO(struct xfs_mount *mp, struct xchk_fscounters *fsc),
	TP_ARGS(mp, fsc),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(uint64_t, icount)
		__field(uint64_t, ifree)
		__field(uint64_t, fdblocks)
		__field(uint64_t, frextents)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->icount = fsc->icount;
		__entry->ifree = fsc->ifree;
		__entry->fdblocks = fsc->fdblocks;
		__entry->frextents = fsc->frextents;
	),
	TP_printk("dev %d:%d icount %llu ifree %llu fdblocks %llu frextents %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->icount,
		  __entry->ifree,
		  __entry->fdblocks,
		  __entry->frextents)
)

DECLARE_EVENT_CLASS(xrep_newbt_extent_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len,
		 int64_t owner),
	TP_ARGS(mp, agno, agbno, len, owner),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(int64_t, owner)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->owner = owner;
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x owner 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->owner)
);
#define DEFINE_NEWBT_EXTENT_EVENT(name) \
DEFINE_EVENT(xrep_newbt_extent_class, name, \
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, \
		 xfs_agblock_t agbno, xfs_extlen_t len, \
		 int64_t owner), \
	TP_ARGS(mp, agno, agbno, len, owner))
DEFINE_NEWBT_EXTENT_EVENT(xrep_newbt_alloc_ag_blocks);
DEFINE_NEWBT_EXTENT_EVENT(xrep_newbt_alloc_file_blocks);
DEFINE_NEWBT_EXTENT_EVENT(xrep_newbt_free_blocks);
DEFINE_NEWBT_EXTENT_EVENT(xrep_newbt_claim_block);

DECLARE_EVENT_CLASS(xrep_dinode_class,
	TP_PROTO(struct xfs_scrub *sc, struct xfs_dinode *dip),
	TP_ARGS(sc, dip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(uint16_t, mode)
		__field(uint8_t, version)
		__field(uint8_t, format)
		__field(uint32_t, uid)
		__field(uint32_t, gid)
		__field(uint64_t, size)
		__field(uint64_t, nblocks)
		__field(uint32_t, extsize)
		__field(uint32_t, nextents)
		__field(uint16_t, anextents)
		__field(uint8_t, forkoff)
		__field(uint8_t, aformat)
		__field(uint16_t, flags)
		__field(uint32_t, gen)
		__field(uint64_t, flags2)
		__field(uint32_t, cowextsize)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->sm->sm_ino;
		__entry->mode = be16_to_cpu(dip->di_mode);
		__entry->version = dip->di_version;
		__entry->format = dip->di_format;
		__entry->uid = be32_to_cpu(dip->di_uid);
		__entry->gid = be32_to_cpu(dip->di_gid);
		__entry->size = be64_to_cpu(dip->di_size);
		__entry->nblocks = be64_to_cpu(dip->di_nblocks);
		__entry->extsize = be32_to_cpu(dip->di_extsize);
		__entry->nextents = be32_to_cpu(dip->di_nextents);
		__entry->anextents = be16_to_cpu(dip->di_anextents);
		__entry->forkoff = dip->di_forkoff;
		__entry->aformat = dip->di_aformat;
		__entry->flags = be16_to_cpu(dip->di_flags);
		__entry->gen = be32_to_cpu(dip->di_gen);
		__entry->flags2 = be64_to_cpu(dip->di_flags2);
		__entry->cowextsize = be32_to_cpu(dip->di_cowextsize);
	),
	TP_printk("dev %d:%d ino 0x%llx mode 0x%x version %u format %u uid %u gid %u disize 0x%llx nblocks 0x%llx extsize %u nextents %u anextents %u forkoff 0x%x aformat %u flags 0x%x gen 0x%x flags2 0x%llx cowextsize %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->mode,
		  __entry->version,
		  __entry->format,
		  __entry->uid,
		  __entry->gid,
		  __entry->size,
		  __entry->nblocks,
		  __entry->extsize,
		  __entry->nextents,
		  __entry->anextents,
		  __entry->forkoff,
		  __entry->aformat,
		  __entry->flags,
		  __entry->gen,
		  __entry->flags2,
		  __entry->cowextsize)
)

#define DEFINE_REPAIR_DINODE_EVENT(name) \
DEFINE_EVENT(xrep_dinode_class, name, \
	TP_PROTO(struct xfs_scrub *sc, struct xfs_dinode *dip), \
	TP_ARGS(sc, dip))
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_header);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_mode);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_flags);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_size);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_extsize_hints);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_zap_symlink);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_zap_dir);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_fixed);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_zap_forks);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_zap_dfork);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_zap_afork);
DEFINE_REPAIR_DINODE_EVENT(xrep_dinode_ensure_forkoff);

DECLARE_EVENT_CLASS(xrep_inode_class,
	TP_PROTO(struct xfs_scrub *sc),
	TP_ARGS(sc),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_rfsblock_t, nblocks)
		__field(uint16_t, flags)
		__field(uint64_t, flags2)
		__field(uint32_t, nextents)
		__field(uint8_t, format)
		__field(uint32_t, anextents)
		__field(uint8_t, aformat)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->sm->sm_ino;
		__entry->size = sc->ip->i_disk_size;
		__entry->nblocks = sc->ip->i_nblocks;
		__entry->flags = sc->ip->i_diflags;
		__entry->flags2 = sc->ip->i_diflags2;
		__entry->nextents = sc->ip->i_df.if_nextents;
		__entry->format = sc->ip->i_df.if_format;
		__entry->anextents = sc->ip->i_af.if_nextents;
		__entry->aformat = sc->ip->i_af.if_format;
	),
	TP_printk("dev %d:%d ino 0x%llx disize 0x%llx nblocks 0x%llx flags 0x%x flags2 0x%llx nextents %u format %u anextents %u aformat %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->nblocks,
		  __entry->flags,
		  __entry->flags2,
		  __entry->nextents,
		  __entry->format,
		  __entry->anextents,
		  __entry->aformat)
)

#define DEFINE_REPAIR_INODE_EVENT(name) \
DEFINE_EVENT(xrep_inode_class, name, \
	TP_PROTO(struct xfs_scrub *sc), \
	TP_ARGS(sc))
DEFINE_REPAIR_INODE_EVENT(xrep_inode_blockcounts);
DEFINE_REPAIR_INODE_EVENT(xrep_inode_ids);
DEFINE_REPAIR_INODE_EVENT(xrep_inode_flags);
DEFINE_REPAIR_INODE_EVENT(xrep_inode_blockdir_size);
DEFINE_REPAIR_INODE_EVENT(xrep_inode_sfdir_size);
DEFINE_REPAIR_INODE_EVENT(xrep_inode_dir_size);
DEFINE_REPAIR_INODE_EVENT(xrep_inode_fixed);

TRACE_EVENT(xrep_dinode_count_rmaps,
	TP_PROTO(struct xfs_scrub *sc, xfs_rfsblock_t data_blocks,
		xfs_rfsblock_t rt_blocks, xfs_rfsblock_t attr_blocks,
		xfs_extnum_t data_extents, xfs_extnum_t rt_extents,
		xfs_aextnum_t attr_extents),
	TP_ARGS(sc, data_blocks, rt_blocks, attr_blocks, data_extents,
		rt_extents, attr_extents),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_rfsblock_t, data_blocks)
		__field(xfs_rfsblock_t, rt_blocks)
		__field(xfs_rfsblock_t, attr_blocks)
		__field(xfs_extnum_t, data_extents)
		__field(xfs_extnum_t, rt_extents)
		__field(xfs_aextnum_t, attr_extents)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->sm->sm_ino;
		__entry->data_blocks = data_blocks;
		__entry->rt_blocks = rt_blocks;
		__entry->attr_blocks = attr_blocks;
		__entry->data_extents = data_extents;
		__entry->rt_extents = rt_extents;
		__entry->attr_extents = attr_extents;
	),
	TP_printk("dev %d:%d ino 0x%llx dblocks 0x%llx rtblocks 0x%llx ablocks 0x%llx dextents %llu rtextents %llu aextents %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->data_blocks,
		  __entry->rt_blocks,
		  __entry->attr_blocks,
		  __entry->data_extents,
		  __entry->rt_extents,
		  __entry->attr_extents)
);

TRACE_EVENT(xrep_dinode_findmode_dirent,
	TP_PROTO(struct xfs_scrub *sc, struct xfs_inode *dp,
		 unsigned int ftype),
	TP_ARGS(sc, dp, ftype),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_ino_t, parent_ino)
		__field(unsigned int, ftype)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->sm->sm_ino;
		__entry->parent_ino = dp->i_ino;
		__entry->ftype = ftype;
	),
	TP_printk("dev %d:%d ino 0x%llx parent_ino 0x%llx ftype '%s'",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->parent_ino,
		  __print_symbolic(__entry->ftype, XFS_DIR3_FTYPE_STR))
);

TRACE_EVENT(xrep_dinode_findmode_dirent_inval,
	TP_PROTO(struct xfs_scrub *sc, struct xfs_inode *dp,
		 unsigned int ftype, unsigned int found_ftype),
	TP_ARGS(sc, dp, ftype, found_ftype),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_ino_t, parent_ino)
		__field(unsigned int, ftype)
		__field(unsigned int, found_ftype)
	),
	TP_fast_assign(
		__entry->dev = sc->mp->m_super->s_dev;
		__entry->ino = sc->sm->sm_ino;
		__entry->parent_ino = dp->i_ino;
		__entry->ftype = ftype;
		__entry->found_ftype = found_ftype;
	),
	TP_printk("dev %d:%d ino 0x%llx parent_ino 0x%llx ftype '%s' found_ftype '%s'",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->parent_ino,
		  __print_symbolic(__entry->ftype, XFS_DIR3_FTYPE_STR),
		  __print_symbolic(__entry->found_ftype, XFS_DIR3_FTYPE_STR))
);

TRACE_EVENT(xrep_cow_mark_file_range,
	TP_PROTO(struct xfs_inode *ip, xfs_fsblock_t startblock,
		 xfs_fileoff_t startoff, xfs_filblks_t blockcount),
	TP_ARGS(ip, startblock, startoff, blockcount),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_filblks_t, blockcount)
	),
	TP_fast_assign(
		__entry->dev = ip->i_mount->m_super->s_dev;
		__entry->ino = ip->i_ino;
		__entry->startoff = startoff;
		__entry->startblock = startblock;
		__entry->blockcount = blockcount;
	),
	TP_printk("dev %d:%d ino 0x%llx fileoff 0x%llx startblock 0x%llx fsbcount 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->startoff,
		  __entry->startblock,
		  __entry->blockcount)
);

TRACE_EVENT(xrep_cow_replace_mapping,
	TP_PROTO(struct xfs_inode *ip, const struct xfs_bmbt_irec *irec,
		 xfs_fsblock_t new_startblock, xfs_extlen_t new_blockcount),
	TP_ARGS(ip, irec, new_startblock, new_blockcount),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_filblks_t, blockcount)
		__field(xfs_exntst_t, state)
		__field(xfs_fsblock_t, new_startblock)
		__field(xfs_extlen_t, new_blockcount)
	),
	TP_fast_assign(
		__entry->dev = ip->i_mount->m_super->s_dev;
		__entry->ino = ip->i_ino;
		__entry->startoff = irec->br_startoff;
		__entry->startblock = irec->br_startblock;
		__entry->blockcount = irec->br_blockcount;
		__entry->state = irec->br_state;
		__entry->new_startblock = new_startblock;
		__entry->new_blockcount = new_blockcount;
	),
	TP_printk("dev %d:%d ino 0x%llx startoff 0x%llx startblock 0x%llx fsbcount 0x%llx state 0x%x new_startblock 0x%llx new_fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->startoff,
		  __entry->startblock,
		  __entry->blockcount,
		  __entry->state,
		  __entry->new_startblock,
		  __entry->new_blockcount)
);

TRACE_EVENT(xrep_cow_free_staging,
	TP_PROTO(struct xfs_perag *pag, xfs_agblock_t agbno,
		 xfs_extlen_t blockcount),
	TP_ARGS(pag, agbno, blockcount),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, blockcount)
	),
	TP_fast_assign(
		__entry->dev = pag->pag_mount->m_super->s_dev;
		__entry->agno = pag->pag_agno;
		__entry->agbno = agbno;
		__entry->blockcount = blockcount;
	),
	TP_printk("dev %d:%d agno 0x%x agbno 0x%x fsbcount 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->blockcount)
);

#ifdef CONFIG_XFS_QUOTA
DECLARE_EVENT_CLASS(xrep_dquot_class,
	TP_PROTO(struct xfs_mount *mp, uint8_t type, uint32_t id),
	TP_ARGS(mp, type, id),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(uint8_t, type)
		__field(uint32_t, id)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->id = id;
		__entry->type = type;
	),
	TP_printk("dev %d:%d type %s id 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_flags(__entry->type, "|", XFS_DQTYPE_STRINGS),
		  __entry->id)
);

#define DEFINE_XREP_DQUOT_EVENT(name) \
DEFINE_EVENT(xrep_dquot_class, name, \
	TP_PROTO(struct xfs_mount *mp, uint8_t type, uint32_t id), \
	TP_ARGS(mp, type, id))
DEFINE_XREP_DQUOT_EVENT(xrep_dquot_item);
DEFINE_XREP_DQUOT_EVENT(xrep_disk_dquot);
DEFINE_XREP_DQUOT_EVENT(xrep_dquot_item_fill_bmap_hole);
DEFINE_XREP_DQUOT_EVENT(xrep_quotacheck_dquot);
#endif /* CONFIG_XFS_QUOTA */

DEFINE_SCRUB_NLINKS_DIFF_EVENT(xrep_nlinks_update_inode);
DEFINE_SCRUB_NLINKS_DIFF_EVENT(xrep_nlinks_unfixable_inode);

TRACE_EVENT(xrep_rmap_live_update,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, unsigned int op,
		 const struct xfs_rmap_update_params *p),
	TP_ARGS(mp, agno, op, p),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(unsigned int, op)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(uint64_t, owner)
		__field(uint64_t, offset)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->op = op;
		__entry->agbno = p->startblock;
		__entry->len = p->blockcount;
		xfs_owner_info_unpack(&p->oinfo, &__entry->owner,
				&__entry->offset, &__entry->flags);
		if (p->unwritten)
			__entry->flags |= XFS_RMAP_UNWRITTEN;
	),
	TP_printk("dev %d:%d agno 0x%x op %d agbno 0x%x fsbcount 0x%x owner 0x%llx fileoff 0x%llx flags 0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->op,
		  __entry->agbno,
		  __entry->len,
		  __entry->owner,
		  __entry->offset,
		  __entry->flags)
);

#endif /* IS_ENABLED(CONFIG_XFS_ONLINE_REPAIR) */

#endif /* _TRACE_XFS_SCRUB_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE scrub/trace
#include <trace/define_trace.h>
