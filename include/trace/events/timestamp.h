/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM timestamp

#if !defined(_TRACE_TIMESTAMP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TIMESTAMP_H

#include <linux/tracepoint.h>
#include <linux/fs.h>

#define CTIME_QUERIED_FLAGS \
	{ I_CTIME_QUERIED, "Q" }

DECLARE_EVENT_CLASS(ctime,
	TP_PROTO(struct inode *inode,
		 struct timespec64 *ctime),

	TP_ARGS(inode, ctime),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__field(time64_t,	ctime_s)
		__field(u32,		ctime_ns)
		__field(u32,		gen)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->gen		= inode->i_generation;
		__entry->ctime_s	= ctime->tv_sec;
		__entry->ctime_ns	= ctime->tv_nsec;
	),

	TP_printk("ino=%d:%d:%ld:%u ctime=%lld.%u",
		MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino, __entry->gen,
		__entry->ctime_s, __entry->ctime_ns
	)
);

DEFINE_EVENT(ctime, inode_set_ctime_to_ts,
		TP_PROTO(struct inode *inode,
			 struct timespec64 *ctime),
		TP_ARGS(inode, ctime));

DEFINE_EVENT(ctime, ctime_xchg_skip,
		TP_PROTO(struct inode *inode,
			 struct timespec64 *ctime),
		TP_ARGS(inode, ctime));

TRACE_EVENT(ctime_ns_xchg,
	TP_PROTO(struct inode *inode,
		 u32 old,
		 u32 new,
		 u32 cur),

	TP_ARGS(inode, old, new, cur),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__field(u32,		gen)
		__field(u32,		old)
		__field(u32,		new)
		__field(u32,		cur)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->gen		= inode->i_generation;
		__entry->old		= old;
		__entry->new		= new;
		__entry->cur		= cur;
	),

	TP_printk("ino=%d:%d:%ld:%u old=%u:%s new=%u cur=%u:%s",
		MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino, __entry->gen,
		__entry->old & ~I_CTIME_QUERIED,
		__print_flags(__entry->old & I_CTIME_QUERIED, "|", CTIME_QUERIED_FLAGS),
		__entry->new,
		__entry->cur & ~I_CTIME_QUERIED,
		__print_flags(__entry->cur & I_CTIME_QUERIED, "|", CTIME_QUERIED_FLAGS)
	)
);

TRACE_EVENT(fill_mg_cmtime,
	TP_PROTO(struct inode *inode,
		 struct timespec64 *ctime,
		 struct timespec64 *mtime),

	TP_ARGS(inode, ctime, mtime),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__field(time64_t,	ctime_s)
		__field(time64_t,	mtime_s)
		__field(u32,		ctime_ns)
		__field(u32,		mtime_ns)
		__field(u32,		gen)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->gen		= inode->i_generation;
		__entry->ctime_s	= ctime->tv_sec;
		__entry->mtime_s	= mtime->tv_sec;
		__entry->ctime_ns	= ctime->tv_nsec;
		__entry->mtime_ns	= mtime->tv_nsec;
	),

	TP_printk("ino=%d:%d:%ld:%u ctime=%lld.%u mtime=%lld.%u",
		MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino, __entry->gen,
		__entry->ctime_s, __entry->ctime_ns,
		__entry->mtime_s, __entry->mtime_ns
	)
);
#endif /* _TRACE_TIMESTAMP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
