#undef TRACE_SYSTEM
#define TRACE_SYSTEM jbd2

#if !defined(_TRACE_JBD2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_JBD2_H

#include <linux/jbd2.h>
#include <linux/tracepoint.h>

struct transaction_chp_stats_s;
struct transaction_run_stats_s;

TRACE_EVENT(jbd2_checkpoint,

	TP_PROTO(journal_t *journal, int result),

	TP_ARGS(journal, result),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	result			)
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
		__entry->result		= result;
	),

	TP_printk("dev %s result %d",
		  jbd2_dev_to_name(__entry->dev), __entry->result)
);

DECLARE_EVENT_CLASS(jbd2_commit,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	char,	sync_commit		  )
		__field(	int,	transaction		  )
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
		__entry->sync_commit = commit_transaction->t_synchronous_commit;
		__entry->transaction	= commit_transaction->t_tid;
	),

	TP_printk("dev %s transaction %d sync %d",
		  jbd2_dev_to_name(__entry->dev), __entry->transaction,
		  __entry->sync_commit)
);

DEFINE_EVENT(jbd2_commit, jbd2_start_commit,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
);

DEFINE_EVENT(jbd2_commit, jbd2_commit_locking,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
);

DEFINE_EVENT(jbd2_commit, jbd2_commit_flushing,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
);

DEFINE_EVENT(jbd2_commit, jbd2_commit_logging,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
);

TRACE_EVENT(jbd2_end_commit,
	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	char,	sync_commit		  )
		__field(	int,	transaction		  )
		__field(	int,	head		  	  )
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
		__entry->sync_commit = commit_transaction->t_synchronous_commit;
		__entry->transaction	= commit_transaction->t_tid;
		__entry->head		= journal->j_tail_sequence;
	),

	TP_printk("dev %s transaction %d sync %d head %d",
		  jbd2_dev_to_name(__entry->dev), __entry->transaction,
		  __entry->sync_commit, __entry->head)
);

TRACE_EVENT(jbd2_submit_inode_data,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
	),

	TP_printk("dev %s ino %lu",
		  jbd2_dev_to_name(__entry->dev), (unsigned long) __entry->ino)
);

TRACE_EVENT(jbd2_run_stats,
	TP_PROTO(dev_t dev, unsigned long tid,
		 struct transaction_run_stats_s *stats),

	TP_ARGS(dev, tid, stats),

	TP_STRUCT__entry(
		__field(		dev_t,	dev		)
		__field(	unsigned long,	tid		)
		__field(	unsigned long,	wait		)
		__field(	unsigned long,	running		)
		__field(	unsigned long,	locked		)
		__field(	unsigned long,	flushing	)
		__field(	unsigned long,	logging		)
		__field(		__u32,	handle_count	)
		__field(		__u32,	blocks		)
		__field(		__u32,	blocks_logged	)
	),

	TP_fast_assign(
		__entry->dev		= dev;
		__entry->tid		= tid;
		__entry->wait		= stats->rs_wait;
		__entry->running	= stats->rs_running;
		__entry->locked		= stats->rs_locked;
		__entry->flushing	= stats->rs_flushing;
		__entry->logging	= stats->rs_logging;
		__entry->handle_count	= stats->rs_handle_count;
		__entry->blocks		= stats->rs_blocks;
		__entry->blocks_logged	= stats->rs_blocks_logged;
	),

	TP_printk("dev %s tid %lu wait %u running %u locked %u flushing %u "
		  "logging %u handle_count %u blocks %u blocks_logged %u",
		  jbd2_dev_to_name(__entry->dev), __entry->tid,
		  jiffies_to_msecs(__entry->wait),
		  jiffies_to_msecs(__entry->running),
		  jiffies_to_msecs(__entry->locked),
		  jiffies_to_msecs(__entry->flushing),
		  jiffies_to_msecs(__entry->logging),
		  __entry->handle_count, __entry->blocks,
		  __entry->blocks_logged)
);

TRACE_EVENT(jbd2_checkpoint_stats,
	TP_PROTO(dev_t dev, unsigned long tid,
		 struct transaction_chp_stats_s *stats),

	TP_ARGS(dev, tid, stats),

	TP_STRUCT__entry(
		__field(		dev_t,	dev		)
		__field(	unsigned long,	tid		)
		__field(	unsigned long,	chp_time	)
		__field(		__u32,	forced_to_close	)
		__field(		__u32,	written		)
		__field(		__u32,	dropped		)
	),

	TP_fast_assign(
		__entry->dev		= dev;
		__entry->tid		= tid;
		__entry->chp_time	= stats->cs_chp_time;
		__entry->forced_to_close= stats->cs_forced_to_close;
		__entry->written	= stats->cs_written;
		__entry->dropped	= stats->cs_dropped;
	),

	TP_printk("dev %s tid %lu chp_time %u forced_to_close %u "
		  "written %u dropped %u",
		  jbd2_dev_to_name(__entry->dev), __entry->tid,
		  jiffies_to_msecs(__entry->chp_time),
		  __entry->forced_to_close, __entry->written, __entry->dropped)
);

#endif /* _TRACE_JBD2_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
