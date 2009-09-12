#undef TRACE_SYSTEM
#define TRACE_SYSTEM jbd2

#if !defined(_TRACE_JBD2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_JBD2_H

#include <linux/jbd2.h>
#include <linux/tracepoint.h>

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

TRACE_EVENT(jbd2_start_commit,

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

TRACE_EVENT(jbd2_commit_locking,

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

TRACE_EVENT(jbd2_commit_flushing,

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

TRACE_EVENT(jbd2_commit_logging,

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
		  jbd2_dev_to_name(__entry->dev), __entry->ino)
);

#endif /* _TRACE_JBD2_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
