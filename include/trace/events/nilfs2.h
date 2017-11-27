/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM nilfs2

#if !defined(_TRACE_NILFS2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NILFS2_H

#include <linux/tracepoint.h>

struct nilfs_sc_info;

#define show_collection_stage(type)					\
	__print_symbolic(type,						\
	{ NILFS_ST_INIT, "ST_INIT" },					\
	{ NILFS_ST_GC, "ST_GC" },					\
	{ NILFS_ST_FILE, "ST_FILE" },					\
	{ NILFS_ST_IFILE, "ST_IFILE" },					\
	{ NILFS_ST_CPFILE, "ST_CPFILE" },				\
	{ NILFS_ST_SUFILE, "ST_SUFILE" },				\
	{ NILFS_ST_DAT, "ST_DAT" },					\
	{ NILFS_ST_SR, "ST_SR" },					\
	{ NILFS_ST_DSYNC, "ST_DSYNC" },					\
	{ NILFS_ST_DONE, "ST_DONE"})

TRACE_EVENT(nilfs2_collection_stage_transition,

	    TP_PROTO(struct nilfs_sc_info *sci),

	    TP_ARGS(sci),

	    TP_STRUCT__entry(
		    __field(void *, sci)
		    __field(int, stage)
	    ),

	    TP_fast_assign(
			__entry->sci = sci;
			__entry->stage = sci->sc_stage.scnt;
		    ),

	    TP_printk("sci = %p stage = %s",
		      __entry->sci,
		      show_collection_stage(__entry->stage))
);

#ifndef TRACE_HEADER_MULTI_READ
enum nilfs2_transaction_transition_state {
	TRACE_NILFS2_TRANSACTION_BEGIN,
	TRACE_NILFS2_TRANSACTION_COMMIT,
	TRACE_NILFS2_TRANSACTION_ABORT,
	TRACE_NILFS2_TRANSACTION_TRYLOCK,
	TRACE_NILFS2_TRANSACTION_LOCK,
	TRACE_NILFS2_TRANSACTION_UNLOCK,
};
#endif

#define show_transaction_state(type)					\
	__print_symbolic(type,						\
			 { TRACE_NILFS2_TRANSACTION_BEGIN, "BEGIN" },	\
			 { TRACE_NILFS2_TRANSACTION_COMMIT, "COMMIT" },	\
			 { TRACE_NILFS2_TRANSACTION_ABORT, "ABORT" },	\
			 { TRACE_NILFS2_TRANSACTION_TRYLOCK, "TRYLOCK" }, \
			 { TRACE_NILFS2_TRANSACTION_LOCK, "LOCK" },	\
			 { TRACE_NILFS2_TRANSACTION_UNLOCK, "UNLOCK" })

TRACE_EVENT(nilfs2_transaction_transition,
	    TP_PROTO(struct super_block *sb,
		     struct nilfs_transaction_info *ti,
		     int count,
		     unsigned int flags,
		     enum nilfs2_transaction_transition_state state),

	    TP_ARGS(sb, ti, count, flags, state),

	    TP_STRUCT__entry(
		    __field(void *, sb)
		    __field(void *, ti)
		    __field(int, count)
		    __field(unsigned int, flags)
		    __field(int, state)
	    ),

	    TP_fast_assign(
		    __entry->sb = sb;
		    __entry->ti = ti;
		    __entry->count = count;
		    __entry->flags = flags;
		    __entry->state = state;
		    ),

	    TP_printk("sb = %p ti = %p count = %d flags = %x state = %s",
		      __entry->sb,
		      __entry->ti,
		      __entry->count,
		      __entry->flags,
		      show_transaction_state(__entry->state))
);

TRACE_EVENT(nilfs2_segment_usage_check,
	    TP_PROTO(struct inode *sufile,
		     __u64 segnum,
		     unsigned long cnt),

	    TP_ARGS(sufile, segnum, cnt),

	    TP_STRUCT__entry(
		    __field(struct inode *, sufile)
		    __field(__u64, segnum)
		    __field(unsigned long, cnt)
	    ),

	    TP_fast_assign(
		    __entry->sufile = sufile;
		    __entry->segnum = segnum;
		    __entry->cnt = cnt;
		    ),

	    TP_printk("sufile = %p segnum = %llu cnt = %lu",
		      __entry->sufile,
		      __entry->segnum,
		      __entry->cnt)
);

TRACE_EVENT(nilfs2_segment_usage_allocated,
	    TP_PROTO(struct inode *sufile,
		     __u64 segnum),

	    TP_ARGS(sufile, segnum),

	    TP_STRUCT__entry(
		    __field(struct inode *, sufile)
		    __field(__u64, segnum)
	    ),

	    TP_fast_assign(
		    __entry->sufile = sufile;
		    __entry->segnum = segnum;
		    ),

	    TP_printk("sufile = %p segnum = %llu",
		      __entry->sufile,
		      __entry->segnum)
);

TRACE_EVENT(nilfs2_segment_usage_freed,
	    TP_PROTO(struct inode *sufile,
		     __u64 segnum),

	    TP_ARGS(sufile, segnum),

	    TP_STRUCT__entry(
		    __field(struct inode *, sufile)
		    __field(__u64, segnum)
	    ),

	    TP_fast_assign(
		    __entry->sufile = sufile;
		    __entry->segnum = segnum;
		    ),

	    TP_printk("sufile = %p segnum = %llu",
		      __entry->sufile,
		      __entry->segnum)
);

TRACE_EVENT(nilfs2_mdt_insert_new_block,
	    TP_PROTO(struct inode *inode,
		     unsigned long ino,
		     unsigned long block),

	    TP_ARGS(inode, ino, block),

	    TP_STRUCT__entry(
		    __field(struct inode *, inode)
		    __field(unsigned long, ino)
		    __field(unsigned long, block)
	    ),

	    TP_fast_assign(
		    __entry->inode = inode;
		    __entry->ino = ino;
		    __entry->block = block;
		    ),

	    TP_printk("inode = %p ino = %lu block = %lu",
		      __entry->inode,
		      __entry->ino,
		      __entry->block)
);

TRACE_EVENT(nilfs2_mdt_submit_block,
	    TP_PROTO(struct inode *inode,
		     unsigned long ino,
		     unsigned long blkoff,
		     int mode),

	    TP_ARGS(inode, ino, blkoff, mode),

	    TP_STRUCT__entry(
		    __field(struct inode *, inode)
		    __field(unsigned long, ino)
		    __field(unsigned long, blkoff)
		    __field(int, mode)
	    ),

	    TP_fast_assign(
		    __entry->inode = inode;
		    __entry->ino = ino;
		    __entry->blkoff = blkoff;
		    __entry->mode = mode;
		    ),

	    TP_printk("inode = %p ino = %lu blkoff = %lu mode = %x",
		      __entry->inode,
		      __entry->ino,
		      __entry->blkoff,
		      __entry->mode)
);

#endif /* _TRACE_NILFS2_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE nilfs2
#include <trace/define_trace.h>
