// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_LOG_PRIV_H__
#define __XFS_LOG_PRIV_H__

#include "xfs_extent_busy.h"	/* for struct xfs_busy_extents */

struct xfs_buf;
struct xlog;
struct xlog_ticket;
struct xfs_mount;

/*
 * get client id from packed copy.
 *
 * this hack is here because the xlog_pack code copies four bytes
 * of xlog_op_header containing the fields oh_clientid, oh_flags
 * and oh_res2 into the packed copy.
 *
 * later on this four byte chunk is treated as an int and the
 * client id is pulled out.
 *
 * this has endian issues, of course.
 */
static inline uint xlog_get_client_id(__be32 i)
{
	return be32_to_cpu(i) >> 24;
}

/*
 * In core log state
 */
enum xlog_iclog_state {
	XLOG_STATE_ACTIVE,	/* Current IC log being written to */
	XLOG_STATE_WANT_SYNC,	/* Want to sync this iclog; no more writes */
	XLOG_STATE_SYNCING,	/* This IC log is syncing */
	XLOG_STATE_DONE_SYNC,	/* Done syncing to disk */
	XLOG_STATE_CALLBACK,	/* Callback functions now */
	XLOG_STATE_DIRTY,	/* Dirty IC log, not ready for ACTIVE status */
};

#define XLOG_STATE_STRINGS \
	{ XLOG_STATE_ACTIVE,	"XLOG_STATE_ACTIVE" }, \
	{ XLOG_STATE_WANT_SYNC,	"XLOG_STATE_WANT_SYNC" }, \
	{ XLOG_STATE_SYNCING,	"XLOG_STATE_SYNCING" }, \
	{ XLOG_STATE_DONE_SYNC,	"XLOG_STATE_DONE_SYNC" }, \
	{ XLOG_STATE_CALLBACK,	"XLOG_STATE_CALLBACK" }, \
	{ XLOG_STATE_DIRTY,	"XLOG_STATE_DIRTY" }

/*
 * In core log flags
 */
#define XLOG_ICL_NEED_FLUSH	(1u << 0)	/* iclog needs REQ_PREFLUSH */
#define XLOG_ICL_NEED_FUA	(1u << 1)	/* iclog needs REQ_FUA */

#define XLOG_ICL_STRINGS \
	{ XLOG_ICL_NEED_FLUSH,	"XLOG_ICL_NEED_FLUSH" }, \
	{ XLOG_ICL_NEED_FUA,	"XLOG_ICL_NEED_FUA" }


/*
 * Log ticket flags
 */
#define XLOG_TIC_PERM_RESERV	(1u << 0)	/* permanent reservation */

#define XLOG_TIC_FLAGS \
	{ XLOG_TIC_PERM_RESERV,	"XLOG_TIC_PERM_RESERV" }

/*
 * Below are states for covering allocation transactions.
 * By covering, we mean changing the h_tail_lsn in the last on-disk
 * log write such that no allocation transactions will be re-done during
 * recovery after a system crash. Recovery starts at the last on-disk
 * log write.
 *
 * These states are used to insert dummy log entries to cover
 * space allocation transactions which can undo non-transactional changes
 * after a crash. Writes to a file with space
 * already allocated do not result in any transactions. Allocations
 * might include space beyond the EOF. So if we just push the EOF a
 * little, the last transaction for the file could contain the wrong
 * size. If there is no file system activity, after an allocation
 * transaction, and the system crashes, the allocation transaction
 * will get replayed and the file will be truncated. This could
 * be hours/days/... after the allocation occurred.
 *
 * The fix for this is to do two dummy transactions when the
 * system is idle. We need two dummy transaction because the h_tail_lsn
 * in the log record header needs to point beyond the last possible
 * non-dummy transaction. The first dummy changes the h_tail_lsn to
 * the first transaction before the dummy. The second dummy causes
 * h_tail_lsn to point to the first dummy. Recovery starts at h_tail_lsn.
 *
 * These dummy transactions get committed when everything
 * is idle (after there has been some activity).
 *
 * There are 5 states used to control this.
 *
 *  IDLE -- no logging has been done on the file system or
 *		we are done covering previous transactions.
 *  NEED -- logging has occurred and we need a dummy transaction
 *		when the log becomes idle.
 *  DONE -- we were in the NEED state and have committed a dummy
 *		transaction.
 *  NEED2 -- we detected that a dummy transaction has gone to the
 *		on disk log with no other transactions.
 *  DONE2 -- we committed a dummy transaction when in the NEED2 state.
 *
 * There are two places where we switch states:
 *
 * 1.) In xfs_sync, when we detect an idle log and are in NEED or NEED2.
 *	We commit the dummy transaction and switch to DONE or DONE2,
 *	respectively. In all other states, we don't do anything.
 *
 * 2.) When we finish writing the on-disk log (xlog_state_clean_log).
 *
 *	No matter what state we are in, if this isn't the dummy
 *	transaction going out, the next state is NEED.
 *	So, if we aren't in the DONE or DONE2 states, the next state
 *	is NEED. We can't be finishing a write of the dummy record
 *	unless it was committed and the state switched to DONE or DONE2.
 *
 *	If we are in the DONE state and this was a write of the
 *		dummy transaction, we move to NEED2.
 *
 *	If we are in the DONE2 state and this was a write of the
 *		dummy transaction, we move to IDLE.
 *
 *
 * Writing only one dummy transaction can get appended to
 * one file space allocation. When this happens, the log recovery
 * code replays the space allocation and a file could be truncated.
 * This is why we have the NEED2 and DONE2 states before going idle.
 */

#define XLOG_STATE_COVER_IDLE	0
#define XLOG_STATE_COVER_NEED	1
#define XLOG_STATE_COVER_DONE	2
#define XLOG_STATE_COVER_NEED2	3
#define XLOG_STATE_COVER_DONE2	4

#define XLOG_COVER_OPS		5

typedef struct xlog_ticket {
	struct list_head	t_queue;	/* reserve/write queue */
	struct task_struct	*t_task;	/* task that owns this ticket */
	xlog_tid_t		t_tid;		/* transaction identifier */
	atomic_t		t_ref;		/* ticket reference count */
	int			t_curr_res;	/* current reservation */
	int			t_unit_res;	/* unit reservation */
	char			t_ocnt;		/* original unit count */
	char			t_cnt;		/* current unit count */
	uint8_t			t_flags;	/* properties of reservation */
	int			t_iclog_hdrs;	/* iclog hdrs in t_curr_res */
} xlog_ticket_t;

/*
 * - A log record header is 512 bytes.  There is plenty of room to grow the
 *	xlog_rec_header_t into the reserved space.
 * - ic_data follows, so a write to disk can start at the beginning of
 *	the iclog.
 * - ic_forcewait is used to implement synchronous forcing of the iclog to disk.
 * - ic_next is the pointer to the next iclog in the ring.
 * - ic_log is a pointer back to the global log structure.
 * - ic_size is the full size of the log buffer, minus the cycle headers.
 * - ic_offset is the current number of bytes written to in this iclog.
 * - ic_refcnt is bumped when someone is writing to the log.
 * - ic_state is the state of the iclog.
 *
 * Because of cacheline contention on large machines, we need to separate
 * various resources onto different cachelines. To start with, make the
 * structure cacheline aligned. The following fields can be contended on
 * by independent processes:
 *
 *	- ic_callbacks
 *	- ic_refcnt
 *	- fields protected by the global l_icloglock
 *
 * so we need to ensure that these fields are located in separate cachelines.
 * We'll put all the read-only and l_icloglock fields in the first cacheline,
 * and move everything else out to subsequent cachelines.
 */
typedef struct xlog_in_core {
	wait_queue_head_t	ic_force_wait;
	wait_queue_head_t	ic_write_wait;
	struct xlog_in_core	*ic_next;
	struct xlog_in_core	*ic_prev;
	struct xlog		*ic_log;
	u32			ic_size;
	u32			ic_offset;
	enum xlog_iclog_state	ic_state;
	unsigned int		ic_flags;
	void			*ic_datap;	/* pointer to iclog data */
	struct list_head	ic_callbacks;

	/* reference counts need their own cacheline */
	atomic_t		ic_refcnt ____cacheline_aligned_in_smp;
	xlog_in_core_2_t	*ic_data;
#define ic_header	ic_data->hic_header
#ifdef DEBUG
	bool			ic_fail_crc : 1;
#endif
	struct semaphore	ic_sema;
	struct work_struct	ic_end_io_work;
	struct bio		ic_bio;
	struct bio_vec		ic_bvec[];
} xlog_in_core_t;

/*
 * The CIL context is used to aggregate per-transaction details as well be
 * passed to the iclog for checkpoint post-commit processing.  After being
 * passed to the iclog, another context needs to be allocated for tracking the
 * next set of transactions to be aggregated into a checkpoint.
 */
struct xfs_cil;

struct xfs_cil_ctx {
	struct xfs_cil		*cil;
	xfs_csn_t		sequence;	/* chkpt sequence # */
	xfs_lsn_t		start_lsn;	/* first LSN of chkpt commit */
	xfs_lsn_t		commit_lsn;	/* chkpt commit record lsn */
	struct xlog_in_core	*commit_iclog;
	struct xlog_ticket	*ticket;	/* chkpt ticket */
	atomic_t		space_used;	/* aggregate size of regions */
	struct xfs_busy_extents	busy_extents;
	struct list_head	log_items;	/* log items in chkpt */
	struct list_head	lv_chain;	/* logvecs being pushed */
	struct list_head	iclog_entry;
	struct list_head	committing;	/* ctx committing list */
	struct work_struct	push_work;
	atomic_t		order_id;

	/*
	 * CPUs that could have added items to the percpu CIL data.  Access is
	 * coordinated with xc_ctx_lock.
	 */
	struct cpumask		cil_pcpmask;
};

/*
 * Per-cpu CIL tracking items
 */
struct xlog_cil_pcp {
	int32_t			space_used;
	uint32_t		space_reserved;
	struct list_head	busy_extents;
	struct list_head	log_items;
};

/*
 * Committed Item List structure
 *
 * This structure is used to track log items that have been committed but not
 * yet written into the log. It is used only when the delayed logging mount
 * option is enabled.
 *
 * This structure tracks the list of committing checkpoint contexts so
 * we can avoid the problem of having to hold out new transactions during a
 * flush until we have a the commit record LSN of the checkpoint. We can
 * traverse the list of committing contexts in xlog_cil_push_lsn() to find a
 * sequence match and extract the commit LSN directly from there. If the
 * checkpoint is still in the process of committing, we can block waiting for
 * the commit LSN to be determined as well. This should make synchronous
 * operations almost as efficient as the old logging methods.
 */
struct xfs_cil {
	struct xlog		*xc_log;
	unsigned long		xc_flags;
	atomic_t		xc_iclog_hdrs;
	struct workqueue_struct	*xc_push_wq;

	struct rw_semaphore	xc_ctx_lock ____cacheline_aligned_in_smp;
	struct xfs_cil_ctx	*xc_ctx;

	spinlock_t		xc_push_lock ____cacheline_aligned_in_smp;
	xfs_csn_t		xc_push_seq;
	bool			xc_push_commit_stable;
	struct list_head	xc_committing;
	wait_queue_head_t	xc_commit_wait;
	wait_queue_head_t	xc_start_wait;
	xfs_csn_t		xc_current_sequence;
	wait_queue_head_t	xc_push_wait;	/* background push throttle */

	void __percpu		*xc_pcp;	/* percpu CIL structures */
} ____cacheline_aligned_in_smp;

/* xc_flags bit values */
#define	XLOG_CIL_EMPTY		1
#define XLOG_CIL_PCP_SPACE	2

/*
 * The amount of log space we allow the CIL to aggregate is difficult to size.
 * Whatever we choose, we have to make sure we can get a reservation for the
 * log space effectively, that it is large enough to capture sufficient
 * relogging to reduce log buffer IO significantly, but it is not too large for
 * the log or induces too much latency when writing out through the iclogs. We
 * track both space consumed and the number of vectors in the checkpoint
 * context, so we need to decide which to use for limiting.
 *
 * Every log buffer we write out during a push needs a header reserved, which
 * is at least one sector and more for v2 logs. Hence we need a reservation of
 * at least 512 bytes per 32k of log space just for the LR headers. That means
 * 16KB of reservation per megabyte of delayed logging space we will consume,
 * plus various headers.  The number of headers will vary based on the num of
 * io vectors, so limiting on a specific number of vectors is going to result
 * in transactions of varying size. IOWs, it is more consistent to track and
 * limit space consumed in the log rather than by the number of objects being
 * logged in order to prevent checkpoint ticket overruns.
 *
 * Further, use of static reservations through the log grant mechanism is
 * problematic. It introduces a lot of complexity (e.g. reserve grant vs write
 * grant) and a significant deadlock potential because regranting write space
 * can block on log pushes. Hence if we have to regrant log space during a log
 * push, we can deadlock.
 *
 * However, we can avoid this by use of a dynamic "reservation stealing"
 * technique during transaction commit whereby unused reservation space in the
 * transaction ticket is transferred to the CIL ctx commit ticket to cover the
 * space needed by the checkpoint transaction. This means that we never need to
 * specifically reserve space for the CIL checkpoint transaction, nor do we
 * need to regrant space once the checkpoint completes. This also means the
 * checkpoint transaction ticket is specific to the checkpoint context, rather
 * than the CIL itself.
 *
 * With dynamic reservations, we can effectively make up arbitrary limits for
 * the checkpoint size so long as they don't violate any other size rules.
 * Recovery imposes a rule that no transaction exceed half the log, so we are
 * limited by that.  Furthermore, the log transaction reservation subsystem
 * tries to keep 25% of the log free, so we need to keep below that limit or we
 * risk running out of free log space to start any new transactions.
 *
 * In order to keep background CIL push efficient, we only need to ensure the
 * CIL is large enough to maintain sufficient in-memory relogging to avoid
 * repeated physical writes of frequently modified metadata. If we allow the CIL
 * to grow to a substantial fraction of the log, then we may be pinning hundreds
 * of megabytes of metadata in memory until the CIL flushes. This can cause
 * issues when we are running low on memory - pinned memory cannot be reclaimed,
 * and the CIL consumes a lot of memory. Hence we need to set an upper physical
 * size limit for the CIL that limits the maximum amount of memory pinned by the
 * CIL but does not limit performance by reducing relogging efficiency
 * significantly.
 *
 * As such, the CIL push threshold ends up being the smaller of two thresholds:
 * - a threshold large enough that it allows CIL to be pushed and progress to be
 *   made without excessive blocking of incoming transaction commits. This is
 *   defined to be 12.5% of the log space - half the 25% push threshold of the
 *   AIL.
 * - small enough that it doesn't pin excessive amounts of memory but maintains
 *   close to peak relogging efficiency. This is defined to be 16x the iclog
 *   buffer window (32MB) as measurements have shown this to be roughly the
 *   point of diminishing performance increases under highly concurrent
 *   modification workloads.
 *
 * To prevent the CIL from overflowing upper commit size bounds, we introduce a
 * new threshold at which we block committing transactions until the background
 * CIL commit commences and switches to a new context. While this is not a hard
 * limit, it forces the process committing a transaction to the CIL to block and
 * yeild the CPU, giving the CIL push work a chance to be scheduled and start
 * work. This prevents a process running lots of transactions from overfilling
 * the CIL because it is not yielding the CPU. We set the blocking limit at
 * twice the background push space threshold so we keep in line with the AIL
 * push thresholds.
 *
 * Note: this is not a -hard- limit as blocking is applied after the transaction
 * is inserted into the CIL and the push has been triggered. It is largely a
 * throttling mechanism that allows the CIL push to be scheduled and run. A hard
 * limit will be difficult to implement without introducing global serialisation
 * in the CIL commit fast path, and it's not at all clear that we actually need
 * such hard limits given the ~7 years we've run without a hard limit before
 * finding the first situation where a checkpoint size overflow actually
 * occurred. Hence the simple throttle, and an ASSERT check to tell us that
 * we've overrun the max size.
 */
#define XLOG_CIL_SPACE_LIMIT(log)	\
	min_t(int, (log)->l_logsize >> 3, BBTOB(XLOG_TOTAL_REC_SHIFT(log)) << 4)

#define XLOG_CIL_BLOCKING_SPACE_LIMIT(log)	\
	(XLOG_CIL_SPACE_LIMIT(log) * 2)

/*
 * ticket grant locks, queues and accounting have their own cachlines
 * as these are quite hot and can be operated on concurrently.
 */
struct xlog_grant_head {
	spinlock_t		lock ____cacheline_aligned_in_smp;
	struct list_head	waiters;
	atomic64_t		grant;
};

/*
 * The reservation head lsn is not made up of a cycle number and block number.
 * Instead, it uses a cycle number and byte number.  Logs don't expect to
 * overflow 31 bits worth of byte offset, so using a byte number will mean
 * that round off problems won't occur when releasing partial reservations.
 */
struct xlog {
	/* The following fields don't need locking */
	struct xfs_mount	*l_mp;	        /* mount point */
	struct xfs_ail		*l_ailp;	/* AIL log is working with */
	struct xfs_cil		*l_cilp;	/* CIL log is working with */
	struct xfs_buftarg	*l_targ;        /* buftarg of log */
	struct workqueue_struct	*l_ioend_workqueue; /* for I/O completions */
	struct delayed_work	l_work;		/* background flush work */
	long			l_opstate;	/* operational state */
	uint			l_quotaoffs_flag; /* XFS_DQ_*, for QUOTAOFFs */
	struct list_head	*l_buf_cancel_table;
	struct list_head	r_dfops;	/* recovered log intent items */
	int			l_iclog_hsize;  /* size of iclog header */
	int			l_iclog_heads;  /* # of iclog header sectors */
	uint			l_sectBBsize;   /* sector size in BBs (2^n) */
	int			l_iclog_size;	/* size of log in bytes */
	int			l_iclog_bufs;	/* number of iclog buffers */
	xfs_daddr_t		l_logBBstart;   /* start block of log */
	int			l_logsize;      /* size of log in bytes */
	int			l_logBBsize;    /* size of log in BB chunks */

	/* The following block of fields are changed while holding icloglock */
	wait_queue_head_t	l_flush_wait ____cacheline_aligned_in_smp;
						/* waiting for iclog flush */
	int			l_covered_state;/* state of "covering disk
						 * log entries" */
	xlog_in_core_t		*l_iclog;       /* head log queue	*/
	spinlock_t		l_icloglock;    /* grab to change iclog state */
	int			l_curr_cycle;   /* Cycle number of log writes */
	int			l_prev_cycle;   /* Cycle number before last
						 * block increment */
	int			l_curr_block;   /* current logical log block */
	int			l_prev_block;   /* previous logical log block */

	/*
	 * l_last_sync_lsn and l_tail_lsn are atomics so they can be set and
	 * read without needing to hold specific locks. To avoid operations
	 * contending with other hot objects, place each of them on a separate
	 * cacheline.
	 */
	/* lsn of last LR on disk */
	atomic64_t		l_last_sync_lsn ____cacheline_aligned_in_smp;
	/* lsn of 1st LR with unflushed * buffers */
	atomic64_t		l_tail_lsn ____cacheline_aligned_in_smp;

	struct xlog_grant_head	l_reserve_head;
	struct xlog_grant_head	l_write_head;

	struct xfs_kobj		l_kobj;

	/* log recovery lsn tracking (for buffer submission */
	xfs_lsn_t		l_recovery_lsn;

	uint32_t		l_iclog_roundoff;/* padding roundoff */

	/* Users of log incompat features should take a read lock. */
	struct rw_semaphore	l_incompat_users;
};

/*
 * Bits for operational state
 */
#define XLOG_ACTIVE_RECOVERY	0	/* in the middle of recovery */
#define XLOG_RECOVERY_NEEDED	1	/* log was recovered */
#define XLOG_IO_ERROR		2	/* log hit an I/O error, and being
				   shutdown */
#define XLOG_TAIL_WARN		3	/* log tail verify warning issued */

static inline bool
xlog_recovery_needed(struct xlog *log)
{
	return test_bit(XLOG_RECOVERY_NEEDED, &log->l_opstate);
}

static inline bool
xlog_in_recovery(struct xlog *log)
{
	return test_bit(XLOG_ACTIVE_RECOVERY, &log->l_opstate);
}

static inline bool
xlog_is_shutdown(struct xlog *log)
{
	return test_bit(XLOG_IO_ERROR, &log->l_opstate);
}

/*
 * Wait until the xlog_force_shutdown() has marked the log as shut down
 * so xlog_is_shutdown() will always return true.
 */
static inline void
xlog_shutdown_wait(
	struct xlog	*log)
{
	wait_var_event(&log->l_opstate, xlog_is_shutdown(log));
}

/* common routines */
extern int
xlog_recover(
	struct xlog		*log);
extern int
xlog_recover_finish(
	struct xlog		*log);
extern void
xlog_recover_cancel(struct xlog *);

extern __le32	 xlog_cksum(struct xlog *log, struct xlog_rec_header *rhead,
			    char *dp, int size);

extern struct kmem_cache *xfs_log_ticket_cache;
struct xlog_ticket *xlog_ticket_alloc(struct xlog *log, int unit_bytes,
		int count, bool permanent);

void	xlog_print_tic_res(struct xfs_mount *mp, struct xlog_ticket *ticket);
void	xlog_print_trans(struct xfs_trans *);
int	xlog_write(struct xlog *log, struct xfs_cil_ctx *ctx,
		struct list_head *lv_chain, struct xlog_ticket *tic,
		uint32_t len);
void	xfs_log_ticket_ungrant(struct xlog *log, struct xlog_ticket *ticket);
void	xfs_log_ticket_regrant(struct xlog *log, struct xlog_ticket *ticket);

void xlog_state_switch_iclogs(struct xlog *log, struct xlog_in_core *iclog,
		int eventual_size);
int xlog_state_release_iclog(struct xlog *log, struct xlog_in_core *iclog,
		struct xlog_ticket *ticket);

/*
 * When we crack an atomic LSN, we sample it first so that the value will not
 * change while we are cracking it into the component values. This means we
 * will always get consistent component values to work from. This should always
 * be used to sample and crack LSNs that are stored and updated in atomic
 * variables.
 */
static inline void
xlog_crack_atomic_lsn(atomic64_t *lsn, uint *cycle, uint *block)
{
	xfs_lsn_t val = atomic64_read(lsn);

	*cycle = CYCLE_LSN(val);
	*block = BLOCK_LSN(val);
}

/*
 * Calculate and assign a value to an atomic LSN variable from component pieces.
 */
static inline void
xlog_assign_atomic_lsn(atomic64_t *lsn, uint cycle, uint block)
{
	atomic64_set(lsn, xlog_assign_lsn(cycle, block));
}

/*
 * When we crack the grant head, we sample it first so that the value will not
 * change while we are cracking it into the component values. This means we
 * will always get consistent component values to work from.
 */
static inline void
xlog_crack_grant_head_val(int64_t val, int *cycle, int *space)
{
	*cycle = val >> 32;
	*space = val & 0xffffffff;
}

static inline void
xlog_crack_grant_head(atomic64_t *head, int *cycle, int *space)
{
	xlog_crack_grant_head_val(atomic64_read(head), cycle, space);
}

static inline int64_t
xlog_assign_grant_head_val(int cycle, int space)
{
	return ((int64_t)cycle << 32) | space;
}

static inline void
xlog_assign_grant_head(atomic64_t *head, int cycle, int space)
{
	atomic64_set(head, xlog_assign_grant_head_val(cycle, space));
}

/*
 * Committed Item List interfaces
 */
int	xlog_cil_init(struct xlog *log);
void	xlog_cil_init_post_recovery(struct xlog *log);
void	xlog_cil_destroy(struct xlog *log);
bool	xlog_cil_empty(struct xlog *log);
void	xlog_cil_commit(struct xlog *log, struct xfs_trans *tp,
			xfs_csn_t *commit_seq, bool regrant);
void	xlog_cil_set_ctx_write_state(struct xfs_cil_ctx *ctx,
			struct xlog_in_core *iclog);


/*
 * CIL force routines
 */
void xlog_cil_flush(struct xlog *log);
xfs_lsn_t xlog_cil_force_seq(struct xlog *log, xfs_csn_t sequence);

static inline void
xlog_cil_force(struct xlog *log)
{
	xlog_cil_force_seq(log, log->l_cilp->xc_current_sequence);
}

/*
 * Wrapper function for waiting on a wait queue serialised against wakeups
 * by a spinlock. This matches the semantics of all the wait queues used in the
 * log code.
 */
static inline void
xlog_wait(
	struct wait_queue_head	*wq,
	struct spinlock		*lock)
		__releases(lock)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue_exclusive(wq, &wait);
	__set_current_state(TASK_UNINTERRUPTIBLE);
	spin_unlock(lock);
	schedule();
	remove_wait_queue(wq, &wait);
}

int xlog_wait_on_iclog(struct xlog_in_core *iclog);

/*
 * The LSN is valid so long as it is behind the current LSN. If it isn't, this
 * means that the next log record that includes this metadata could have a
 * smaller LSN. In turn, this means that the modification in the log would not
 * replay.
 */
static inline bool
xlog_valid_lsn(
	struct xlog	*log,
	xfs_lsn_t	lsn)
{
	int		cur_cycle;
	int		cur_block;
	bool		valid = true;

	/*
	 * First, sample the current lsn without locking to avoid added
	 * contention from metadata I/O. The current cycle and block are updated
	 * (in xlog_state_switch_iclogs()) and read here in a particular order
	 * to avoid false negatives (e.g., thinking the metadata LSN is valid
	 * when it is not).
	 *
	 * The current block is always rewound before the cycle is bumped in
	 * xlog_state_switch_iclogs() to ensure the current LSN is never seen in
	 * a transiently forward state. Instead, we can see the LSN in a
	 * transiently behind state if we happen to race with a cycle wrap.
	 */
	cur_cycle = READ_ONCE(log->l_curr_cycle);
	smp_rmb();
	cur_block = READ_ONCE(log->l_curr_block);

	if ((CYCLE_LSN(lsn) > cur_cycle) ||
	    (CYCLE_LSN(lsn) == cur_cycle && BLOCK_LSN(lsn) > cur_block)) {
		/*
		 * If the metadata LSN appears invalid, it's possible the check
		 * above raced with a wrap to the next log cycle. Grab the lock
		 * to check for sure.
		 */
		spin_lock(&log->l_icloglock);
		cur_cycle = log->l_curr_cycle;
		cur_block = log->l_curr_block;
		spin_unlock(&log->l_icloglock);

		if ((CYCLE_LSN(lsn) > cur_cycle) ||
		    (CYCLE_LSN(lsn) == cur_cycle && BLOCK_LSN(lsn) > cur_block))
			valid = false;
	}

	return valid;
}

/*
 * Log vector and shadow buffers can be large, so we need to use kvmalloc() here
 * to ensure success. Unfortunately, kvmalloc() only allows GFP_KERNEL contexts
 * to fall back to vmalloc, so we can't actually do anything useful with gfp
 * flags to control the kmalloc() behaviour within kvmalloc(). Hence kmalloc()
 * will do direct reclaim and compaction in the slow path, both of which are
 * horrendously expensive. We just want kmalloc to fail fast and fall back to
 * vmalloc if it can't get somethign straight away from the free lists or
 * buddy allocator. Hence we have to open code kvmalloc outselves here.
 *
 * This assumes that the caller uses memalloc_nofs_save task context here, so
 * despite the use of GFP_KERNEL here, we are going to be doing GFP_NOFS
 * allocations. This is actually the only way to make vmalloc() do GFP_NOFS
 * allocations, so lets just all pretend this is a GFP_KERNEL context
 * operation....
 */
static inline void *
xlog_kvmalloc(
	size_t		buf_size)
{
	gfp_t		flags = GFP_KERNEL;
	void		*p;

	flags &= ~__GFP_DIRECT_RECLAIM;
	flags |= __GFP_NOWARN | __GFP_NORETRY;
	do {
		p = kmalloc(buf_size, flags);
		if (!p)
			p = vmalloc(buf_size);
	} while (!p);

	return p;
}

#endif	/* __XFS_LOG_PRIV_H__ */
