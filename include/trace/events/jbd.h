#undef TRACE_SYSTEM
#define TRACE_SYSTEM jbd

#if !defined(_TRACE_JBD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_JBD_H

#include <linux/jbd.h>
#include <linux/tracepoint.h>

TRACE_EVENT(jbd_checkpoint,

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

	TP_printk("dev %d,%d result %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->result)
);

DECLARE_EVENT_CLASS(jbd_commit,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	transaction		)
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
		__entry->transaction	= commit_transaction->t_tid;
	),

	TP_printk("dev %d,%d transaction %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction)
);

DEFINE_EVENT(jbd_commit, jbd_start_commit,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
);

DEFINE_EVENT(jbd_commit, jbd_commit_locking,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
);

DEFINE_EVENT(jbd_commit, jbd_commit_flushing,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
);

DEFINE_EVENT(jbd_commit, jbd_commit_logging,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
);

TRACE_EVENT(jbd_drop_transaction,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	transaction		)
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
		__entry->transaction	= commit_transaction->t_tid;
	),

	TP_printk("dev %d,%d transaction %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction)
);

TRACE_EVENT(jbd_end_commit,
	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	transaction		)
		__field(	int,	head			)
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
		__entry->transaction	= commit_transaction->t_tid;
		__entry->head		= journal->j_tail_sequence;
	),

	TP_printk("dev %d,%d transaction %d head %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction, __entry->head)
);

TRACE_EVENT(jbd_do_submit_data,
	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	transaction		)
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
		__entry->transaction	= commit_transaction->t_tid;
	),

	TP_printk("dev %d,%d transaction %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->transaction)
);

TRACE_EVENT(jbd_cleanup_journal_tail,

	TP_PROTO(journal_t *journal, tid_t first_tid,
		 unsigned long block_nr, unsigned long freed),

	TP_ARGS(journal, first_tid, block_nr, freed),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	tid_t,	tail_sequence		)
		__field(	tid_t,	first_tid		)
		__field(unsigned long,	block_nr		)
		__field(unsigned long,	freed			)
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
		__entry->tail_sequence	= journal->j_tail_sequence;
		__entry->first_tid	= first_tid;
		__entry->block_nr	= block_nr;
		__entry->freed		= freed;
	),

	TP_printk("dev %d,%d from %u to %u offset %lu freed %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tail_sequence, __entry->first_tid,
		  __entry->block_nr, __entry->freed)
);

TRACE_EVENT(journal_write_superblock,
	TP_PROTO(journal_t *journal),

	TP_ARGS(journal),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
	),

	TP_fast_assign(
		__entry->dev		= journal->j_fs_dev->bd_dev;
	),

	TP_printk("dev %d,%d", MAJOR(__entry->dev), MINOR(__entry->dev))
);

#endif /* _TRACE_JBD_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
