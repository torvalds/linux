#undef TRACE_SYSTEM
#define TRACE_SYSTEM gfs2

#if !defined(_TRACE_GFS2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GFS2_H

#include <linux/tracepoint.h>

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/dlmconstants.h>
#include <linux/gfs2_ondisk.h>
#include <linux/writeback.h>
#include <linux/ktime.h>
#include "incore.h"
#include "glock.h"
#include "rgrp.h"

#define dlm_state_name(nn) { DLM_LOCK_##nn, #nn }
#define glock_trace_name(x) __print_symbolic(x,		\
			    dlm_state_name(IV),		\
			    dlm_state_name(NL),		\
			    dlm_state_name(CR),		\
			    dlm_state_name(CW),		\
			    dlm_state_name(PR),		\
			    dlm_state_name(PW),		\
			    dlm_state_name(EX))

#define block_state_name(x) __print_symbolic(x,			\
			    { GFS2_BLKST_FREE, "free" },	\
			    { GFS2_BLKST_USED, "used" },	\
			    { GFS2_BLKST_DINODE, "dinode" },	\
			    { GFS2_BLKST_UNLINKED, "unlinked" })

#define TRACE_RS_DELETE  0
#define TRACE_RS_TREEDEL 1
#define TRACE_RS_INSERT  2
#define TRACE_RS_CLAIM   3

#define rs_func_name(x) __print_symbolic(x,	\
					 { 0, "del " },	\
					 { 1, "tdel" },	\
					 { 2, "ins " },	\
					 { 3, "clm " })

#define show_glock_flags(flags) __print_flags(flags, "",	\
	{(1UL << GLF_LOCK),			"l" },		\
	{(1UL << GLF_DEMOTE),			"D" },		\
	{(1UL << GLF_PENDING_DEMOTE),		"d" },		\
	{(1UL << GLF_DEMOTE_IN_PROGRESS),	"p" },		\
	{(1UL << GLF_DIRTY),			"y" },		\
	{(1UL << GLF_LFLUSH),			"f" },		\
	{(1UL << GLF_INVALIDATE_IN_PROGRESS),	"i" },		\
	{(1UL << GLF_REPLY_PENDING),		"r" },		\
	{(1UL << GLF_INITIAL),			"I" },		\
	{(1UL << GLF_FROZEN),			"F" },		\
	{(1UL << GLF_QUEUED),			"q" },		\
	{(1UL << GLF_LRU),			"L" },		\
	{(1UL << GLF_OBJECT),			"o" },		\
	{(1UL << GLF_BLOCKING),			"b" })

#ifndef NUMPTY
#define NUMPTY
static inline u8 glock_trace_state(unsigned int state)
{
	switch(state) {
	case LM_ST_SHARED:
		return DLM_LOCK_PR;
	case LM_ST_DEFERRED:
		return DLM_LOCK_CW;
	case LM_ST_EXCLUSIVE:
		return DLM_LOCK_EX;
	}
	return DLM_LOCK_NL;
}
#endif

/* Section 1 - Locking
 *
 * Objectives:
 * Latency: Remote demote request to state change
 * Latency: Local lock request to state change
 * Latency: State change to lock grant
 * Correctness: Ordering of local lock state vs. I/O requests
 * Correctness: Responses to remote demote requests
 */

/* General glock state change (DLM lock request completes) */
TRACE_EVENT(gfs2_glock_state_change,

	TP_PROTO(const struct gfs2_glock *gl, unsigned int new_state),

	TP_ARGS(gl, new_state),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	u64,	glnum			)
		__field(	u32,	gltype			)
		__field(	u8,	cur_state		)
		__field(	u8,	new_state		)
		__field(	u8,	dmt_state		)
		__field(	u8,	tgt_state		)
		__field(	unsigned long,	flags		)
	),

	TP_fast_assign(
		__entry->dev		= gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->glnum		= gl->gl_name.ln_number;
		__entry->gltype		= gl->gl_name.ln_type;
		__entry->cur_state	= glock_trace_state(gl->gl_state);
		__entry->new_state	= glock_trace_state(new_state);
		__entry->tgt_state	= glock_trace_state(gl->gl_target);
		__entry->dmt_state	= glock_trace_state(gl->gl_demote_state);
		__entry->flags		= gl->gl_flags | (gl->gl_object ? (1UL<<GLF_OBJECT) : 0);
	),

	TP_printk("%u,%u glock %d:%lld state %s to %s tgt:%s dmt:%s flags:%s",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->gltype,
		 (unsigned long long)__entry->glnum,
		  glock_trace_name(__entry->cur_state),
		  glock_trace_name(__entry->new_state),
		  glock_trace_name(__entry->tgt_state),
		  glock_trace_name(__entry->dmt_state),
		  show_glock_flags(__entry->flags))
);

/* State change -> unlocked, glock is being deallocated */
TRACE_EVENT(gfs2_glock_put,

	TP_PROTO(const struct gfs2_glock *gl),

	TP_ARGS(gl),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	u64,	glnum			)
		__field(	u32,	gltype			)
		__field(	u8,	cur_state		)
		__field(	unsigned long,	flags		)
	),

	TP_fast_assign(
		__entry->dev		= gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->gltype		= gl->gl_name.ln_type;
		__entry->glnum		= gl->gl_name.ln_number;
		__entry->cur_state	= glock_trace_state(gl->gl_state);
		__entry->flags		= gl->gl_flags  | (gl->gl_object ? (1UL<<GLF_OBJECT) : 0);
	),

	TP_printk("%u,%u glock %d:%lld state %s => %s flags:%s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
                  __entry->gltype, (unsigned long long)__entry->glnum,
                  glock_trace_name(__entry->cur_state),
		  glock_trace_name(DLM_LOCK_IV),
		  show_glock_flags(__entry->flags))

);

/* Callback (local or remote) requesting lock demotion */
TRACE_EVENT(gfs2_demote_rq,

	TP_PROTO(const struct gfs2_glock *gl, bool remote),

	TP_ARGS(gl, remote),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	u64,	glnum			)
		__field(	u32,	gltype			)
		__field(	u8,	cur_state		)
		__field(	u8,	dmt_state		)
		__field(	unsigned long,	flags		)
		__field(	bool,	remote			)
	),

	TP_fast_assign(
		__entry->dev		= gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->gltype		= gl->gl_name.ln_type;
		__entry->glnum		= gl->gl_name.ln_number;
		__entry->cur_state	= glock_trace_state(gl->gl_state);
		__entry->dmt_state	= glock_trace_state(gl->gl_demote_state);
		__entry->flags		= gl->gl_flags  | (gl->gl_object ? (1UL<<GLF_OBJECT) : 0);
		__entry->remote		= remote;
	),

	TP_printk("%u,%u glock %d:%lld demote %s to %s flags:%s %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->gltype,
		  (unsigned long long)__entry->glnum,
                  glock_trace_name(__entry->cur_state),
                  glock_trace_name(__entry->dmt_state),
		  show_glock_flags(__entry->flags),
		  __entry->remote ? "remote" : "local")

);

/* Promotion/grant of a glock */
TRACE_EVENT(gfs2_promote,

	TP_PROTO(const struct gfs2_holder *gh, int first),

	TP_ARGS(gh, first),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	u64,	glnum			)
		__field(	u32,	gltype			)
		__field(	int,	first			)
		__field(	u8,	state			)
	),

	TP_fast_assign(
		__entry->dev	= gh->gh_gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->glnum	= gh->gh_gl->gl_name.ln_number;
		__entry->gltype	= gh->gh_gl->gl_name.ln_type;
		__entry->first	= first;
		__entry->state	= glock_trace_state(gh->gh_state);
	),

	TP_printk("%u,%u glock %u:%llu promote %s %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->gltype,
		  (unsigned long long)__entry->glnum,
		  __entry->first ? "first": "other",
		  glock_trace_name(__entry->state))
);

/* Queue/dequeue a lock request */
TRACE_EVENT(gfs2_glock_queue,

	TP_PROTO(const struct gfs2_holder *gh, int queue),

	TP_ARGS(gh, queue),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	u64,	glnum			)
		__field(	u32,	gltype			)
		__field(	int,	queue			)
		__field(	u8,	state			)
	),

	TP_fast_assign(
		__entry->dev	= gh->gh_gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->glnum	= gh->gh_gl->gl_name.ln_number;
		__entry->gltype	= gh->gh_gl->gl_name.ln_type;
		__entry->queue	= queue;
		__entry->state	= glock_trace_state(gh->gh_state);
	),

	TP_printk("%u,%u glock %u:%llu %squeue %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->gltype,
		  (unsigned long long)__entry->glnum,
		  __entry->queue ? "" : "de",
		  glock_trace_name(__entry->state))
);

/* DLM sends a reply to GFS2 */
TRACE_EVENT(gfs2_glock_lock_time,

	TP_PROTO(const struct gfs2_glock *gl, s64 tdiff),

	TP_ARGS(gl, tdiff),

	TP_STRUCT__entry(
		__field(	dev_t,	dev		)
		__field(	u64,	glnum		)
		__field(	u32,	gltype		)
		__field(	int,	status		)
		__field(	char,	flags		)
		__field(	s64,	tdiff		)
		__field(	u64,	srtt		)
		__field(	u64,	srttvar		)
		__field(	u64,	srttb		)
		__field(	u64,	srttvarb	)
		__field(	u64,	sirt		)
		__field(	u64,	sirtvar		)
		__field(	u64,	dcount		)
		__field(	u64,	qcount		)
	),

	TP_fast_assign(
		__entry->dev            = gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->glnum          = gl->gl_name.ln_number;
		__entry->gltype         = gl->gl_name.ln_type;
		__entry->status		= gl->gl_lksb.sb_status;
		__entry->flags		= gl->gl_lksb.sb_flags;
		__entry->tdiff		= tdiff;
		__entry->srtt		= gl->gl_stats.stats[GFS2_LKS_SRTT];
		__entry->srttvar	= gl->gl_stats.stats[GFS2_LKS_SRTTVAR];
		__entry->srttb		= gl->gl_stats.stats[GFS2_LKS_SRTTB];
		__entry->srttvarb	= gl->gl_stats.stats[GFS2_LKS_SRTTVARB];
		__entry->sirt		= gl->gl_stats.stats[GFS2_LKS_SIRT];
		__entry->sirtvar	= gl->gl_stats.stats[GFS2_LKS_SIRTVAR];
		__entry->dcount		= gl->gl_stats.stats[GFS2_LKS_DCOUNT];
		__entry->qcount		= gl->gl_stats.stats[GFS2_LKS_QCOUNT];
	),

	TP_printk("%u,%u glock %d:%lld status:%d flags:%02x tdiff:%lld srtt:%lld/%lld srttb:%lld/%lld sirt:%lld/%lld dcnt:%lld qcnt:%lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->gltype,
		  (unsigned long long)__entry->glnum,
		  __entry->status, __entry->flags,
		  (long long)__entry->tdiff,
		  (long long)__entry->srtt,
		  (long long)__entry->srttvar,
		  (long long)__entry->srttb,
		  (long long)__entry->srttvarb,
		  (long long)__entry->sirt,
		  (long long)__entry->sirtvar,
		  (long long)__entry->dcount,
		  (long long)__entry->qcount)
);

/* Section 2 - Log/journal
 *
 * Objectives:
 * Latency: Log flush time
 * Correctness: pin/unpin vs. disk I/O ordering
 * Performance: Log usage stats
 */

/* Pin/unpin a block in the log */
TRACE_EVENT(gfs2_pin,

	TP_PROTO(const struct gfs2_bufdata *bd, int pin),

	TP_ARGS(bd, pin),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	int,	pin			)
		__field(	u32,	len			)
		__field(	sector_t,	block		)
		__field(	u64,	ino			)
	),

	TP_fast_assign(
		__entry->dev		= bd->bd_gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->pin		= pin;
		__entry->len		= bd->bd_bh->b_size;
		__entry->block		= bd->bd_bh->b_blocknr;
		__entry->ino		= bd->bd_gl->gl_name.ln_number;
	),

	TP_printk("%u,%u log %s %llu/%lu inode %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->pin ? "pin" : "unpin",
		  (unsigned long long)__entry->block,
		  (unsigned long)__entry->len,
		  (unsigned long long)__entry->ino)
);

/* Flushing the log */
TRACE_EVENT(gfs2_log_flush,

	TP_PROTO(const struct gfs2_sbd *sdp, int start),

	TP_ARGS(sdp, start),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	int,	start			)
		__field(	u64,	log_seq			)
	),

	TP_fast_assign(
		__entry->dev            = sdp->sd_vfs->s_dev;
		__entry->start		= start;
		__entry->log_seq	= sdp->sd_log_sequence;
	),

	TP_printk("%u,%u log flush %s %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->start ? "start" : "end",
		  (unsigned long long)__entry->log_seq)
);

/* Reserving/releasing blocks in the log */
TRACE_EVENT(gfs2_log_blocks,

	TP_PROTO(const struct gfs2_sbd *sdp, int blocks),

	TP_ARGS(sdp, blocks),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	int,	blocks			)
	),

	TP_fast_assign(
		__entry->dev		= sdp->sd_vfs->s_dev;
		__entry->blocks		= blocks;
	),

	TP_printk("%u,%u log reserve %d", MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->blocks)
);

/* Writing back the AIL */
TRACE_EVENT(gfs2_ail_flush,

	TP_PROTO(const struct gfs2_sbd *sdp, const struct writeback_control *wbc, int start),

	TP_ARGS(sdp, wbc, start),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int, start			)
		__field(	int, sync_mode			)
		__field(	long, nr_to_write		)
	),

	TP_fast_assign(
		__entry->dev		= sdp->sd_vfs->s_dev;
		__entry->start		= start;
		__entry->sync_mode	= wbc->sync_mode;
		__entry->nr_to_write	= wbc->nr_to_write;
	),

	TP_printk("%u,%u ail flush %s %s %ld", MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->start ? "start" : "end",
		  __entry->sync_mode == WB_SYNC_ALL ? "all" : "none",
		  __entry->nr_to_write)
);

/* Section 3 - bmap
 *
 * Objectives:
 * Latency: Bmap request time
 * Performance: Block allocator tracing
 * Correctness: Test of disard generation vs. blocks allocated
 */

/* Map an extent of blocks, possibly a new allocation */
TRACE_EVENT(gfs2_bmap,

	TP_PROTO(const struct gfs2_inode *ip, const struct buffer_head *bh,
		sector_t lblock, int create, int errno),

	TP_ARGS(ip, bh, lblock, create, errno),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	sector_t, lblock		)
		__field(	sector_t, pblock		)
		__field(	u64,	inum			)
		__field(	unsigned long, state		)
		__field(	u32,	len			)
		__field(	int,	create			)
		__field(	int,	errno			)
	),

	TP_fast_assign(
		__entry->dev            = ip->i_gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->lblock		= lblock;
		__entry->pblock		= buffer_mapped(bh) ?  bh->b_blocknr : 0;
		__entry->inum		= ip->i_no_addr;
		__entry->state		= bh->b_state;
		__entry->len		= bh->b_size;
		__entry->create		= create;
		__entry->errno		= errno;
	),

	TP_printk("%u,%u bmap %llu map %llu/%lu to %llu flags:%08lx %s %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->inum,
		  (unsigned long long)__entry->lblock,
		  (unsigned long)__entry->len,
		  (unsigned long long)__entry->pblock,
		  __entry->state, __entry->create ? "create " : "nocreate",
		  __entry->errno)
);

/* Keep track of blocks as they are allocated/freed */
TRACE_EVENT(gfs2_block_alloc,

	TP_PROTO(const struct gfs2_inode *ip, struct gfs2_rgrpd *rgd,
		 u64 block, unsigned len, u8 block_state),

	TP_ARGS(ip, rgd, block, len, block_state),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	u64,	start			)
		__field(	u64,	inum			)
		__field(	u32,	len			)
		__field(	u8,	block_state		)
		__field(        u64,	rd_addr			)
		__field(        u32,	rd_free_clone		)
		__field(	u32,	rd_reserved		)
	),

	TP_fast_assign(
		__entry->dev		= rgd->rd_gl->gl_name.ln_sbd->sd_vfs->s_dev;
		__entry->start		= block;
		__entry->inum		= ip->i_no_addr;
		__entry->len		= len;
		__entry->block_state	= block_state;
		__entry->rd_addr	= rgd->rd_addr;
		__entry->rd_free_clone	= rgd->rd_free_clone;
		__entry->rd_reserved	= rgd->rd_reserved;
	),

	TP_printk("%u,%u bmap %llu alloc %llu/%lu %s rg:%llu rf:%u rr:%lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->inum,
		  (unsigned long long)__entry->start,
		  (unsigned long)__entry->len,
		  block_state_name(__entry->block_state),
		  (unsigned long long)__entry->rd_addr,
		  __entry->rd_free_clone, (unsigned long)__entry->rd_reserved)
);

/* Keep track of multi-block reservations as they are allocated/freed */
TRACE_EVENT(gfs2_rs,

	TP_PROTO(const struct gfs2_blkreserv *rs, u8 func),

	TP_ARGS(rs, func),

	TP_STRUCT__entry(
		__field(        dev_t,  dev                     )
		__field(	u64,	rd_addr			)
		__field(	u32,	rd_free_clone		)
		__field(	u32,	rd_reserved		)
		__field(	u64,	inum			)
		__field(	u64,	start			)
		__field(	u32,	free			)
		__field(	u8,	func			)
	),

	TP_fast_assign(
		__entry->dev		= rs->rs_rbm.rgd->rd_sbd->sd_vfs->s_dev;
		__entry->rd_addr	= rs->rs_rbm.rgd->rd_addr;
		__entry->rd_free_clone	= rs->rs_rbm.rgd->rd_free_clone;
		__entry->rd_reserved	= rs->rs_rbm.rgd->rd_reserved;
		__entry->inum		= rs->rs_inum;
		__entry->start		= gfs2_rbm_to_block(&rs->rs_rbm);
		__entry->free		= rs->rs_free;
		__entry->func		= func;
	),

	TP_printk("%u,%u bmap %llu resrv %llu rg:%llu rf:%lu rr:%lu %s f:%lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->inum,
		  (unsigned long long)__entry->start,
		  (unsigned long long)__entry->rd_addr,
		  (unsigned long)__entry->rd_free_clone,
		  (unsigned long)__entry->rd_reserved,
		  rs_func_name(__entry->func), (unsigned long)__entry->free)
);

#endif /* _TRACE_GFS2_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_gfs2
#include <trace/define_trace.h>

