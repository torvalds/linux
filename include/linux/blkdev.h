#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#ifdef CONFIG_BLOCK

#include <linux/sched.h>
#include <linux/major.h>
#include <linux/genhd.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/wait.h>
#include <linux/mempool.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/gfp.h>
#include <linux/bsg.h>
#include <linux/smp.h>

#include <asm/scatterlist.h>

struct scsi_ioctl_command;

struct request_queue;
struct elevator_queue;
struct request_pm_state;
struct blk_trace;
struct request;
struct sg_io_hdr;

#define BLKDEV_MIN_RQ	4
#define BLKDEV_MAX_RQ	128	/* Default maximum */

struct request;
typedef void (rq_end_io_fn)(struct request *, int);

struct request_list {
	/*
	 * count[], starved[], and wait[] are indexed by
	 * BLK_RW_SYNC/BLK_RW_ASYNC
	 */
	int count[2];
	int starved[2];
	int elvpriv;
	mempool_t *rq_pool;
	wait_queue_head_t wait[2];
};

/*
 * request command types
 */
enum rq_cmd_type_bits {
	REQ_TYPE_FS		= 1,	/* fs request */
	REQ_TYPE_BLOCK_PC,		/* scsi command */
	REQ_TYPE_SENSE,			/* sense request */
	REQ_TYPE_PM_SUSPEND,		/* suspend request */
	REQ_TYPE_PM_RESUME,		/* resume request */
	REQ_TYPE_PM_SHUTDOWN,		/* shutdown request */
	REQ_TYPE_SPECIAL,		/* driver defined type */
	REQ_TYPE_LINUX_BLOCK,		/* generic block layer message */
	/*
	 * for ATA/ATAPI devices. this really doesn't belong here, ide should
	 * use REQ_TYPE_SPECIAL and use rq->cmd[0] with the range of driver
	 * private REQ_LB opcodes to differentiate what type of request this is
	 */
	REQ_TYPE_ATA_TASKFILE,
	REQ_TYPE_ATA_PC,
};

/*
 * For request of type REQ_TYPE_LINUX_BLOCK, rq->cmd[0] is the opcode being
 * sent down (similar to how REQ_TYPE_BLOCK_PC means that ->cmd[] holds a
 * SCSI cdb.
 *
 * 0x00 -> 0x3f are driver private, to be used for whatever purpose they need,
 * typically to differentiate REQ_TYPE_SPECIAL requests.
 *
 */
enum {
	REQ_LB_OP_EJECT	= 0x40,		/* eject request */
	REQ_LB_OP_FLUSH = 0x41,		/* flush request */
};

/*
 * request type modified bits. first four bits match BIO_RW* bits, important
 */
enum rq_flag_bits {
	__REQ_RW,		/* not set, read. set, write */
	__REQ_FAILFAST_DEV,	/* no driver retries of device errors */
	__REQ_FAILFAST_TRANSPORT, /* no driver retries of transport errors */
	__REQ_FAILFAST_DRIVER,	/* no driver retries of driver errors */
	/* above flags must match BIO_RW_* */
	__REQ_DISCARD,		/* request to discard sectors */
	__REQ_SORTED,		/* elevator knows about this request */
	__REQ_SOFTBARRIER,	/* may not be passed by ioscheduler */
	__REQ_HARDBARRIER,	/* may not be passed by drive either */
	__REQ_FUA,		/* forced unit access */
	__REQ_NOMERGE,		/* don't touch this for merging */
	__REQ_STARTED,		/* drive already may have started this one */
	__REQ_DONTPREP,		/* don't call prep for this one */
	__REQ_QUEUED,		/* uses queueing */
	__REQ_ELVPRIV,		/* elevator private data attached */
	__REQ_FAILED,		/* set if the request failed */
	__REQ_QUIET,		/* don't worry about errors */
	__REQ_PREEMPT,		/* set for "ide_preempt" requests */
	__REQ_ORDERED_COLOR,	/* is before or after barrier */
	__REQ_RW_SYNC,		/* request is sync (sync write or read) */
	__REQ_ALLOCED,		/* request came from our alloc pool */
	__REQ_RW_META,		/* metadata io request */
	__REQ_COPY_USER,	/* contains copies of user pages */
	__REQ_INTEGRITY,	/* integrity metadata has been remapped */
	__REQ_NOIDLE,		/* Don't anticipate more IO after this one */
	__REQ_IO_STAT,		/* account I/O stat */
	__REQ_MIXED_MERGE,	/* merge of different types, fail separately */
	__REQ_NR_BITS,		/* stops here */
};

#define REQ_RW		(1 << __REQ_RW)
#define REQ_FAILFAST_DEV	(1 << __REQ_FAILFAST_DEV)
#define REQ_FAILFAST_TRANSPORT	(1 << __REQ_FAILFAST_TRANSPORT)
#define REQ_FAILFAST_DRIVER	(1 << __REQ_FAILFAST_DRIVER)
#define REQ_DISCARD	(1 << __REQ_DISCARD)
#define REQ_SORTED	(1 << __REQ_SORTED)
#define REQ_SOFTBARRIER	(1 << __REQ_SOFTBARRIER)
#define REQ_HARDBARRIER	(1 << __REQ_HARDBARRIER)
#define REQ_FUA		(1 << __REQ_FUA)
#define REQ_NOMERGE	(1 << __REQ_NOMERGE)
#define REQ_STARTED	(1 << __REQ_STARTED)
#define REQ_DONTPREP	(1 << __REQ_DONTPREP)
#define REQ_QUEUED	(1 << __REQ_QUEUED)
#define REQ_ELVPRIV	(1 << __REQ_ELVPRIV)
#define REQ_FAILED	(1 << __REQ_FAILED)
#define REQ_QUIET	(1 << __REQ_QUIET)
#define REQ_PREEMPT	(1 << __REQ_PREEMPT)
#define REQ_ORDERED_COLOR	(1 << __REQ_ORDERED_COLOR)
#define REQ_RW_SYNC	(1 << __REQ_RW_SYNC)
#define REQ_ALLOCED	(1 << __REQ_ALLOCED)
#define REQ_RW_META	(1 << __REQ_RW_META)
#define REQ_COPY_USER	(1 << __REQ_COPY_USER)
#define REQ_INTEGRITY	(1 << __REQ_INTEGRITY)
#define REQ_NOIDLE	(1 << __REQ_NOIDLE)
#define REQ_IO_STAT	(1 << __REQ_IO_STAT)
#define REQ_MIXED_MERGE	(1 << __REQ_MIXED_MERGE)

#define REQ_FAILFAST_MASK	(REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT | \
				 REQ_FAILFAST_DRIVER)

#define BLK_MAX_CDB	16

/*
 * try to put the fields that are referenced together in the same cacheline.
 * if you modify this structure, be sure to check block/blk-core.c:rq_init()
 * as well!
 */
struct request {
	struct list_head queuelist;
	struct call_single_data csd;
	int cpu;

	struct request_queue *q;

	unsigned int cmd_flags;
	enum rq_cmd_type_bits cmd_type;
	unsigned long atomic_flags;

	/* the following two fields are internal, NEVER access directly */
	sector_t __sector;		/* sector cursor */
	unsigned int __data_len;	/* total data len */

	struct bio *bio;
	struct bio *biotail;

	struct hlist_node hash;	/* merge hash */
	/*
	 * The rb_node is only used inside the io scheduler, requests
	 * are pruned when moved to the dispatch queue. So let the
	 * completion_data share space with the rb_node.
	 */
	union {
		struct rb_node rb_node;	/* sort/lookup */
		void *completion_data;
	};

	/*
	 * two pointers are available for the IO schedulers, if they need
	 * more they have to dynamically allocate it.
	 */
	void *elevator_private;
	void *elevator_private2;

	struct gendisk *rq_disk;
	unsigned long start_time;

	/* Number of scatter-gather DMA addr+len pairs after
	 * physical address coalescing is performed.
	 */
	unsigned short nr_phys_segments;

	unsigned short ioprio;

	void *special;		/* opaque pointer available for LLD use */
	char *buffer;		/* kaddr of the current segment if available */

	int tag;
	int errors;

	int ref_count;

	/*
	 * when request is used as a packet command carrier
	 */
	unsigned short cmd_len;
	unsigned char __cmd[BLK_MAX_CDB];
	unsigned char *cmd;

	unsigned int extra_len;	/* length of alignment and padding */
	unsigned int sense_len;
	unsigned int resid_len;	/* residual count */
	void *sense;

	unsigned long deadline;
	struct list_head timeout_list;
	unsigned int timeout;
	int retries;

	/*
	 * completion callback.
	 */
	rq_end_io_fn *end_io;
	void *end_io_data;

	/* for bidi */
	struct request *next_rq;
};

static inline unsigned short req_get_ioprio(struct request *req)
{
	return req->ioprio;
}

/*
 * State information carried for REQ_TYPE_PM_SUSPEND and REQ_TYPE_PM_RESUME
 * requests. Some step values could eventually be made generic.
 */
struct request_pm_state
{
	/* PM state machine step value, currently driver specific */
	int	pm_step;
	/* requested PM state value (S1, S2, S3, S4, ...) */
	u32	pm_state;
	void*	data;		/* for driver use */
};

#include <linux/elevator.h>

typedef void (request_fn_proc) (struct request_queue *q);
typedef int (make_request_fn) (struct request_queue *q, struct bio *bio);
typedef int (prep_rq_fn) (struct request_queue *, struct request *);
typedef void (unplug_fn) (struct request_queue *);

struct bio_vec;
struct bvec_merge_data {
	struct block_device *bi_bdev;
	sector_t bi_sector;
	unsigned bi_size;
	unsigned long bi_rw;
};
typedef int (merge_bvec_fn) (struct request_queue *, struct bvec_merge_data *,
			     struct bio_vec *);
typedef void (prepare_flush_fn) (struct request_queue *, struct request *);
typedef void (softirq_done_fn)(struct request *);
typedef int (dma_drain_needed_fn)(struct request *);
typedef int (lld_busy_fn) (struct request_queue *q);

enum blk_eh_timer_return {
	BLK_EH_NOT_HANDLED,
	BLK_EH_HANDLED,
	BLK_EH_RESET_TIMER,
};

typedef enum blk_eh_timer_return (rq_timed_out_fn)(struct request *);

enum blk_queue_state {
	Queue_down,
	Queue_up,
};

struct blk_queue_tag {
	struct request **tag_index;	/* map of busy tags */
	unsigned long *tag_map;		/* bit map of free/busy tags */
	int busy;			/* current depth */
	int max_depth;			/* what we will send to device */
	int real_max_depth;		/* what the array can hold */
	atomic_t refcnt;		/* map can be shared */
};

#define BLK_SCSI_MAX_CMDS	(256)
#define BLK_SCSI_CMD_PER_LONG	(BLK_SCSI_MAX_CMDS / (sizeof(long) * 8))

struct queue_limits {
	unsigned long		bounce_pfn;
	unsigned long		seg_boundary_mask;

	unsigned int		max_hw_sectors;
	unsigned int		max_sectors;
	unsigned int		max_segment_size;
	unsigned int		physical_block_size;
	unsigned int		alignment_offset;
	unsigned int		io_min;
	unsigned int		io_opt;
	unsigned int		max_discard_sectors;
	unsigned int		discard_granularity;
	unsigned int		discard_alignment;

	unsigned short		logical_block_size;
	unsigned short		max_segments;

	unsigned char		misaligned;
	unsigned char		discard_misaligned;
	unsigned char		no_cluster;
	signed char		discard_zeroes_data;
};

struct request_queue
{
	/*
	 * Together with queue_head for cacheline sharing
	 */
	struct list_head	queue_head;
	struct request		*last_merge;
	struct elevator_queue	*elevator;

	/*
	 * the queue request freelist, one for reads and one for writes
	 */
	struct request_list	rq;

	request_fn_proc		*request_fn;
	make_request_fn		*make_request_fn;
	prep_rq_fn		*prep_rq_fn;
	unplug_fn		*unplug_fn;
	merge_bvec_fn		*merge_bvec_fn;
	prepare_flush_fn	*prepare_flush_fn;
	softirq_done_fn		*softirq_done_fn;
	rq_timed_out_fn		*rq_timed_out_fn;
	dma_drain_needed_fn	*dma_drain_needed;
	lld_busy_fn		*lld_busy_fn;

	/*
	 * Dispatch queue sorting
	 */
	sector_t		end_sector;
	struct request		*boundary_rq;

	/*
	 * Auto-unplugging state
	 */
	struct timer_list	unplug_timer;
	int			unplug_thresh;	/* After this many requests */
	unsigned long		unplug_delay;	/* After this many jiffies */
	struct work_struct	unplug_work;

	struct backing_dev_info	backing_dev_info;

	/*
	 * The queue owner gets to use this for whatever they like.
	 * ll_rw_blk doesn't touch it.
	 */
	void			*queuedata;

	/*
	 * queue needs bounce pages for pages above this limit
	 */
	gfp_t			bounce_gfp;

	/*
	 * various queue flags, see QUEUE_* below
	 */
	unsigned long		queue_flags;

	/*
	 * protects queue structures from reentrancy. ->__queue_lock should
	 * _never_ be used directly, it is queue private. always use
	 * ->queue_lock.
	 */
	spinlock_t		__queue_lock;
	spinlock_t		*queue_lock;

	/*
	 * queue kobject
	 */
	struct kobject kobj;

	/*
	 * queue settings
	 */
	unsigned long		nr_requests;	/* Max # of requests */
	unsigned int		nr_congestion_on;
	unsigned int		nr_congestion_off;
	unsigned int		nr_batching;

	void			*dma_drain_buffer;
	unsigned int		dma_drain_size;
	unsigned int		dma_pad_mask;
	unsigned int		dma_alignment;

	struct blk_queue_tag	*queue_tags;
	struct list_head	tag_busy_list;

	unsigned int		nr_sorted;
	unsigned int		in_flight[2];

	unsigned int		rq_timeout;
	struct timer_list	timeout;
	struct list_head	timeout_list;

	struct queue_limits	limits;

	/*
	 * sg stuff
	 */
	unsigned int		sg_timeout;
	unsigned int		sg_reserved_size;
	int			node;
#ifdef CONFIG_BLK_DEV_IO_TRACE
	struct blk_trace	*blk_trace;
#endif
	/*
	 * reserved for flush operations
	 */
	unsigned int		ordered, next_ordered, ordseq;
	int			orderr, ordcolor;
	struct request		pre_flush_rq, bar_rq, post_flush_rq;
	struct request		*orig_bar_rq;

	struct mutex		sysfs_lock;

#if defined(CONFIG_BLK_DEV_BSG)
	struct bsg_class_device bsg_dev;
#endif
};

#define QUEUE_FLAG_CLUSTER	0	/* cluster several segments into 1 */
#define QUEUE_FLAG_QUEUED	1	/* uses generic tag queueing */
#define QUEUE_FLAG_STOPPED	2	/* queue is stopped */
#define	QUEUE_FLAG_SYNCFULL	3	/* read queue has been filled */
#define QUEUE_FLAG_ASYNCFULL	4	/* write queue has been filled */
#define QUEUE_FLAG_DEAD		5	/* queue being torn down */
#define QUEUE_FLAG_REENTER	6	/* Re-entrancy avoidance */
#define QUEUE_FLAG_PLUGGED	7	/* queue is plugged */
#define QUEUE_FLAG_ELVSWITCH	8	/* don't use elevator, just do FIFO */
#define QUEUE_FLAG_BIDI		9	/* queue supports bidi requests */
#define QUEUE_FLAG_NOMERGES    10	/* disable merge attempts */
#define QUEUE_FLAG_SAME_COMP   11	/* force complete on same CPU */
#define QUEUE_FLAG_FAIL_IO     12	/* fake timeout */
#define QUEUE_FLAG_STACKABLE   13	/* supports request stacking */
#define QUEUE_FLAG_NONROT      14	/* non-rotational device (SSD) */
#define QUEUE_FLAG_VIRT        QUEUE_FLAG_NONROT /* paravirt device */
#define QUEUE_FLAG_IO_STAT     15	/* do IO stats */
#define QUEUE_FLAG_DISCARD     16	/* supports DISCARD */
#define QUEUE_FLAG_NOXMERGES   17	/* No extended merges */

#define QUEUE_FLAG_DEFAULT	((1 << QUEUE_FLAG_IO_STAT) |		\
				 (1 << QUEUE_FLAG_CLUSTER) |		\
				 (1 << QUEUE_FLAG_STACKABLE)	|	\
				 (1 << QUEUE_FLAG_SAME_COMP))

static inline int queue_is_locked(struct request_queue *q)
{
#ifdef CONFIG_SMP
	spinlock_t *lock = q->queue_lock;
	return lock && spin_is_locked(lock);
#else
	return 1;
#endif
}

static inline void queue_flag_set_unlocked(unsigned int flag,
					   struct request_queue *q)
{
	__set_bit(flag, &q->queue_flags);
}

static inline int queue_flag_test_and_clear(unsigned int flag,
					    struct request_queue *q)
{
	WARN_ON_ONCE(!queue_is_locked(q));

	if (test_bit(flag, &q->queue_flags)) {
		__clear_bit(flag, &q->queue_flags);
		return 1;
	}

	return 0;
}

static inline int queue_flag_test_and_set(unsigned int flag,
					  struct request_queue *q)
{
	WARN_ON_ONCE(!queue_is_locked(q));

	if (!test_bit(flag, &q->queue_flags)) {
		__set_bit(flag, &q->queue_flags);
		return 0;
	}

	return 1;
}

static inline void queue_flag_set(unsigned int flag, struct request_queue *q)
{
	WARN_ON_ONCE(!queue_is_locked(q));
	__set_bit(flag, &q->queue_flags);
}

static inline void queue_flag_clear_unlocked(unsigned int flag,
					     struct request_queue *q)
{
	__clear_bit(flag, &q->queue_flags);
}

static inline int queue_in_flight(struct request_queue *q)
{
	return q->in_flight[0] + q->in_flight[1];
}

static inline void queue_flag_clear(unsigned int flag, struct request_queue *q)
{
	WARN_ON_ONCE(!queue_is_locked(q));
	__clear_bit(flag, &q->queue_flags);
}

enum {
	/*
	 * Hardbarrier is supported with one of the following methods.
	 *
	 * NONE		: hardbarrier unsupported
	 * DRAIN	: ordering by draining is enough
	 * DRAIN_FLUSH	: ordering by draining w/ pre and post flushes
	 * DRAIN_FUA	: ordering by draining w/ pre flush and FUA write
	 * TAG		: ordering by tag is enough
	 * TAG_FLUSH	: ordering by tag w/ pre and post flushes
	 * TAG_FUA	: ordering by tag w/ pre flush and FUA write
	 */
	QUEUE_ORDERED_BY_DRAIN		= 0x01,
	QUEUE_ORDERED_BY_TAG		= 0x02,
	QUEUE_ORDERED_DO_PREFLUSH	= 0x10,
	QUEUE_ORDERED_DO_BAR		= 0x20,
	QUEUE_ORDERED_DO_POSTFLUSH	= 0x40,
	QUEUE_ORDERED_DO_FUA		= 0x80,

	QUEUE_ORDERED_NONE		= 0x00,

	QUEUE_ORDERED_DRAIN		= QUEUE_ORDERED_BY_DRAIN |
					  QUEUE_ORDERED_DO_BAR,
	QUEUE_ORDERED_DRAIN_FLUSH	= QUEUE_ORDERED_DRAIN |
					  QUEUE_ORDERED_DO_PREFLUSH |
					  QUEUE_ORDERED_DO_POSTFLUSH,
	QUEUE_ORDERED_DRAIN_FUA		= QUEUE_ORDERED_DRAIN |
					  QUEUE_ORDERED_DO_PREFLUSH |
					  QUEUE_ORDERED_DO_FUA,

	QUEUE_ORDERED_TAG		= QUEUE_ORDERED_BY_TAG |
					  QUEUE_ORDERED_DO_BAR,
	QUEUE_ORDERED_TAG_FLUSH		= QUEUE_ORDERED_TAG |
					  QUEUE_ORDERED_DO_PREFLUSH |
					  QUEUE_ORDERED_DO_POSTFLUSH,
	QUEUE_ORDERED_TAG_FUA		= QUEUE_ORDERED_TAG |
					  QUEUE_ORDERED_DO_PREFLUSH |
					  QUEUE_ORDERED_DO_FUA,

	/*
	 * Ordered operation sequence
	 */
	QUEUE_ORDSEQ_STARTED	= 0x01,	/* flushing in progress */
	QUEUE_ORDSEQ_DRAIN	= 0x02,	/* waiting for the queue to be drained */
	QUEUE_ORDSEQ_PREFLUSH	= 0x04,	/* pre-flushing in progress */
	QUEUE_ORDSEQ_BAR	= 0x08,	/* original barrier req in progress */
	QUEUE_ORDSEQ_POSTFLUSH	= 0x10,	/* post-flushing in progress */
	QUEUE_ORDSEQ_DONE	= 0x20,
};

#define blk_queue_plugged(q)	test_bit(QUEUE_FLAG_PLUGGED, &(q)->queue_flags)
#define blk_queue_tagged(q)	test_bit(QUEUE_FLAG_QUEUED, &(q)->queue_flags)
#define blk_queue_stopped(q)	test_bit(QUEUE_FLAG_STOPPED, &(q)->queue_flags)
#define blk_queue_nomerges(q)	test_bit(QUEUE_FLAG_NOMERGES, &(q)->queue_flags)
#define blk_queue_noxmerges(q)	\
	test_bit(QUEUE_FLAG_NOXMERGES, &(q)->queue_flags)
#define blk_queue_nonrot(q)	test_bit(QUEUE_FLAG_NONROT, &(q)->queue_flags)
#define blk_queue_io_stat(q)	test_bit(QUEUE_FLAG_IO_STAT, &(q)->queue_flags)
#define blk_queue_flushing(q)	((q)->ordseq)
#define blk_queue_stackable(q)	\
	test_bit(QUEUE_FLAG_STACKABLE, &(q)->queue_flags)
#define blk_queue_discard(q)	test_bit(QUEUE_FLAG_DISCARD, &(q)->queue_flags)

#define blk_fs_request(rq)	((rq)->cmd_type == REQ_TYPE_FS)
#define blk_pc_request(rq)	((rq)->cmd_type == REQ_TYPE_BLOCK_PC)
#define blk_special_request(rq)	((rq)->cmd_type == REQ_TYPE_SPECIAL)
#define blk_sense_request(rq)	((rq)->cmd_type == REQ_TYPE_SENSE)

#define blk_failfast_dev(rq)	((rq)->cmd_flags & REQ_FAILFAST_DEV)
#define blk_failfast_transport(rq) ((rq)->cmd_flags & REQ_FAILFAST_TRANSPORT)
#define blk_failfast_driver(rq)	((rq)->cmd_flags & REQ_FAILFAST_DRIVER)
#define blk_noretry_request(rq)	(blk_failfast_dev(rq) ||	\
				 blk_failfast_transport(rq) ||	\
				 blk_failfast_driver(rq))
#define blk_rq_started(rq)	((rq)->cmd_flags & REQ_STARTED)
#define blk_rq_io_stat(rq)	((rq)->cmd_flags & REQ_IO_STAT)
#define blk_rq_quiet(rq)	((rq)->cmd_flags & REQ_QUIET)

#define blk_account_rq(rq)	(blk_rq_started(rq) && (blk_fs_request(rq) || blk_discard_rq(rq))) 

#define blk_pm_suspend_request(rq)	((rq)->cmd_type == REQ_TYPE_PM_SUSPEND)
#define blk_pm_resume_request(rq)	((rq)->cmd_type == REQ_TYPE_PM_RESUME)
#define blk_pm_request(rq)	\
	(blk_pm_suspend_request(rq) || blk_pm_resume_request(rq))

#define blk_rq_cpu_valid(rq)	((rq)->cpu != -1)
#define blk_sorted_rq(rq)	((rq)->cmd_flags & REQ_SORTED)
#define blk_barrier_rq(rq)	((rq)->cmd_flags & REQ_HARDBARRIER)
#define blk_fua_rq(rq)		((rq)->cmd_flags & REQ_FUA)
#define blk_discard_rq(rq)	((rq)->cmd_flags & REQ_DISCARD)
#define blk_bidi_rq(rq)		((rq)->next_rq != NULL)
/* rq->queuelist of dequeued request must be list_empty() */
#define blk_queued_rq(rq)	(!list_empty(&(rq)->queuelist))

#define list_entry_rq(ptr)	list_entry((ptr), struct request, queuelist)

#define rq_data_dir(rq)		((rq)->cmd_flags & 1)

/*
 * We regard a request as sync, if either a read or a sync write
 */
static inline bool rw_is_sync(unsigned int rw_flags)
{
	return !(rw_flags & REQ_RW) || (rw_flags & REQ_RW_SYNC);
}

static inline bool rq_is_sync(struct request *rq)
{
	return rw_is_sync(rq->cmd_flags);
}

#define rq_is_meta(rq)		((rq)->cmd_flags & REQ_RW_META)
#define rq_noidle(rq)		((rq)->cmd_flags & REQ_NOIDLE)

static inline int blk_queue_full(struct request_queue *q, int sync)
{
	if (sync)
		return test_bit(QUEUE_FLAG_SYNCFULL, &q->queue_flags);
	return test_bit(QUEUE_FLAG_ASYNCFULL, &q->queue_flags);
}

static inline void blk_set_queue_full(struct request_queue *q, int sync)
{
	if (sync)
		queue_flag_set(QUEUE_FLAG_SYNCFULL, q);
	else
		queue_flag_set(QUEUE_FLAG_ASYNCFULL, q);
}

static inline void blk_clear_queue_full(struct request_queue *q, int sync)
{
	if (sync)
		queue_flag_clear(QUEUE_FLAG_SYNCFULL, q);
	else
		queue_flag_clear(QUEUE_FLAG_ASYNCFULL, q);
}


/*
 * mergeable request must not have _NOMERGE or _BARRIER bit set, nor may
 * it already be started by driver.
 */
#define RQ_NOMERGE_FLAGS	\
	(REQ_NOMERGE | REQ_STARTED | REQ_HARDBARRIER | REQ_SOFTBARRIER)
#define rq_mergeable(rq)	\
	(!((rq)->cmd_flags & RQ_NOMERGE_FLAGS) && \
	 (blk_discard_rq(rq) || blk_fs_request((rq))))

/*
 * q->prep_rq_fn return values
 */
#define BLKPREP_OK		0	/* serve it */
#define BLKPREP_KILL		1	/* fatal error, kill */
#define BLKPREP_DEFER		2	/* leave on queue */

extern unsigned long blk_max_low_pfn, blk_max_pfn;

/*
 * standard bounce addresses:
 *
 * BLK_BOUNCE_HIGH	: bounce all highmem pages
 * BLK_BOUNCE_ANY	: don't bounce anything
 * BLK_BOUNCE_ISA	: bounce pages above ISA DMA boundary
 */

#if BITS_PER_LONG == 32
#define BLK_BOUNCE_HIGH		((u64)blk_max_low_pfn << PAGE_SHIFT)
#else
#define BLK_BOUNCE_HIGH		-1ULL
#endif
#define BLK_BOUNCE_ANY		(-1ULL)
#define BLK_BOUNCE_ISA		(ISA_DMA_THRESHOLD)

/*
 * default timeout for SG_IO if none specified
 */
#define BLK_DEFAULT_SG_TIMEOUT	(60 * HZ)
#define BLK_MIN_SG_TIMEOUT	(7 * HZ)

#ifdef CONFIG_BOUNCE
extern int init_emergency_isa_pool(void);
extern void blk_queue_bounce(struct request_queue *q, struct bio **bio);
#else
static inline int init_emergency_isa_pool(void)
{
	return 0;
}
static inline void blk_queue_bounce(struct request_queue *q, struct bio **bio)
{
}
#endif /* CONFIG_MMU */

struct rq_map_data {
	struct page **pages;
	int page_order;
	int nr_entries;
	unsigned long offset;
	int null_mapped;
	int from_user;
};

struct req_iterator {
	int i;
	struct bio *bio;
};

/* This should not be used directly - use rq_for_each_segment */
#define for_each_bio(_bio)		\
	for (; _bio; _bio = _bio->bi_next)
#define __rq_for_each_bio(_bio, rq)	\
	if ((rq->bio))			\
		for (_bio = (rq)->bio; _bio; _bio = _bio->bi_next)

#define rq_for_each_segment(bvl, _rq, _iter)			\
	__rq_for_each_bio(_iter.bio, _rq)			\
		bio_for_each_segment(bvl, _iter.bio, _iter.i)

#define rq_iter_last(rq, _iter)					\
		(_iter.bio->bi_next == NULL && _iter.i == _iter.bio->bi_vcnt-1)

#ifndef ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
# error	"You should define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE for your platform"
#endif
#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
extern void rq_flush_dcache_pages(struct request *rq);
#else
static inline void rq_flush_dcache_pages(struct request *rq)
{
}
#endif

extern int blk_register_queue(struct gendisk *disk);
extern void blk_unregister_queue(struct gendisk *disk);
extern void register_disk(struct gendisk *dev);
extern void generic_make_request(struct bio *bio);
extern void blk_rq_init(struct request_queue *q, struct request *rq);
extern void blk_put_request(struct request *);
extern void __blk_put_request(struct request_queue *, struct request *);
extern struct request *blk_get_request(struct request_queue *, int, gfp_t);
extern struct request *blk_make_request(struct request_queue *, struct bio *,
					gfp_t);
extern void blk_insert_request(struct request_queue *, struct request *, int, void *);
extern void blk_requeue_request(struct request_queue *, struct request *);
extern int blk_rq_check_limits(struct request_queue *q, struct request *rq);
extern int blk_lld_busy(struct request_queue *q);
extern int blk_rq_prep_clone(struct request *rq, struct request *rq_src,
			     struct bio_set *bs, gfp_t gfp_mask,
			     int (*bio_ctr)(struct bio *, struct bio *, void *),
			     void *data);
extern void blk_rq_unprep_clone(struct request *rq);
extern int blk_insert_cloned_request(struct request_queue *q,
				     struct request *rq);
extern void blk_plug_device(struct request_queue *);
extern void blk_plug_device_unlocked(struct request_queue *);
extern int blk_remove_plug(struct request_queue *);
extern void blk_recount_segments(struct request_queue *, struct bio *);
extern int scsi_cmd_ioctl(struct request_queue *, struct gendisk *, fmode_t,
			  unsigned int, void __user *);
extern int sg_scsi_ioctl(struct request_queue *, struct gendisk *, fmode_t,
			 struct scsi_ioctl_command __user *);

/*
 * A queue has just exitted congestion.  Note this in the global counter of
 * congested queues, and wake up anyone who was waiting for requests to be
 * put back.
 */
static inline void blk_clear_queue_congested(struct request_queue *q, int sync)
{
	clear_bdi_congested(&q->backing_dev_info, sync);
}

/*
 * A queue has just entered congestion.  Flag that in the queue's VM-visible
 * state flags and increment the global gounter of congested queues.
 */
static inline void blk_set_queue_congested(struct request_queue *q, int sync)
{
	set_bdi_congested(&q->backing_dev_info, sync);
}

extern void blk_start_queue(struct request_queue *q);
extern void blk_stop_queue(struct request_queue *q);
extern void blk_sync_queue(struct request_queue *q);
extern void __blk_stop_queue(struct request_queue *q);
extern void __blk_run_queue(struct request_queue *);
extern void blk_run_queue(struct request_queue *);
extern int blk_rq_map_user(struct request_queue *, struct request *,
			   struct rq_map_data *, void __user *, unsigned long,
			   gfp_t);
extern int blk_rq_unmap_user(struct bio *);
extern int blk_rq_map_kern(struct request_queue *, struct request *, void *, unsigned int, gfp_t);
extern int blk_rq_map_user_iov(struct request_queue *, struct request *,
			       struct rq_map_data *, struct sg_iovec *, int,
			       unsigned int, gfp_t);
extern int blk_execute_rq(struct request_queue *, struct gendisk *,
			  struct request *, int);
extern void blk_execute_rq_nowait(struct request_queue *, struct gendisk *,
				  struct request *, int, rq_end_io_fn *);
extern void blk_unplug(struct request_queue *q);

static inline struct request_queue *bdev_get_queue(struct block_device *bdev)
{
	return bdev->bd_disk->queue;
}

/*
 * blk_rq_pos()			: the current sector
 * blk_rq_bytes()		: bytes left in the entire request
 * blk_rq_cur_bytes()		: bytes left in the current segment
 * blk_rq_err_bytes()		: bytes left till the next error boundary
 * blk_rq_sectors()		: sectors left in the entire request
 * blk_rq_cur_sectors()		: sectors left in the current segment
 */
static inline sector_t blk_rq_pos(const struct request *rq)
{
	return rq->__sector;
}

static inline unsigned int blk_rq_bytes(const struct request *rq)
{
	return rq->__data_len;
}

static inline int blk_rq_cur_bytes(const struct request *rq)
{
	return rq->bio ? bio_cur_bytes(rq->bio) : 0;
}

extern unsigned int blk_rq_err_bytes(const struct request *rq);

static inline unsigned int blk_rq_sectors(const struct request *rq)
{
	return blk_rq_bytes(rq) >> 9;
}

static inline unsigned int blk_rq_cur_sectors(const struct request *rq)
{
	return blk_rq_cur_bytes(rq) >> 9;
}

/*
 * Request issue related functions.
 */
extern struct request *blk_peek_request(struct request_queue *q);
extern void blk_start_request(struct request *rq);
extern struct request *blk_fetch_request(struct request_queue *q);

/*
 * Request completion related functions.
 *
 * blk_update_request() completes given number of bytes and updates
 * the request without completing it.
 *
 * blk_end_request() and friends.  __blk_end_request() must be called
 * with the request queue spinlock acquired.
 *
 * Several drivers define their own end_request and call
 * blk_end_request() for parts of the original function.
 * This prevents code duplication in drivers.
 */
extern bool blk_update_request(struct request *rq, int error,
			       unsigned int nr_bytes);
extern bool blk_end_request(struct request *rq, int error,
			    unsigned int nr_bytes);
extern void blk_end_request_all(struct request *rq, int error);
extern bool blk_end_request_cur(struct request *rq, int error);
extern bool blk_end_request_err(struct request *rq, int error);
extern bool __blk_end_request(struct request *rq, int error,
			      unsigned int nr_bytes);
extern void __blk_end_request_all(struct request *rq, int error);
extern bool __blk_end_request_cur(struct request *rq, int error);
extern bool __blk_end_request_err(struct request *rq, int error);

extern void blk_complete_request(struct request *);
extern void __blk_complete_request(struct request *);
extern void blk_abort_request(struct request *);
extern void blk_abort_queue(struct request_queue *);

/*
 * Access functions for manipulating queue properties
 */
extern struct request_queue *blk_init_queue_node(request_fn_proc *rfn,
					spinlock_t *lock, int node_id);
extern struct request_queue *blk_init_queue(request_fn_proc *, spinlock_t *);
extern void blk_cleanup_queue(struct request_queue *);
extern void blk_queue_make_request(struct request_queue *, make_request_fn *);
extern void blk_queue_bounce_limit(struct request_queue *, u64);
extern void blk_queue_max_hw_sectors(struct request_queue *, unsigned int);

/* Temporary compatibility wrapper */
static inline void blk_queue_max_sectors(struct request_queue *q, unsigned int max)
{
	blk_queue_max_hw_sectors(q, max);
}

extern void blk_queue_max_segments(struct request_queue *, unsigned short);

static inline void blk_queue_max_phys_segments(struct request_queue *q, unsigned short max)
{
	blk_queue_max_segments(q, max);
}

static inline void blk_queue_max_hw_segments(struct request_queue *q, unsigned short max)
{
	blk_queue_max_segments(q, max);
}


extern void blk_queue_max_segment_size(struct request_queue *, unsigned int);
extern void blk_queue_max_discard_sectors(struct request_queue *q,
		unsigned int max_discard_sectors);
extern void blk_queue_logical_block_size(struct request_queue *, unsigned short);
extern void blk_queue_physical_block_size(struct request_queue *, unsigned short);
extern void blk_queue_alignment_offset(struct request_queue *q,
				       unsigned int alignment);
extern void blk_limits_io_min(struct queue_limits *limits, unsigned int min);
extern void blk_queue_io_min(struct request_queue *q, unsigned int min);
extern void blk_limits_io_opt(struct queue_limits *limits, unsigned int opt);
extern void blk_queue_io_opt(struct request_queue *q, unsigned int opt);
extern void blk_set_default_limits(struct queue_limits *lim);
extern int blk_stack_limits(struct queue_limits *t, struct queue_limits *b,
			    sector_t offset);
extern int bdev_stack_limits(struct queue_limits *t, struct block_device *bdev,
			    sector_t offset);
extern void disk_stack_limits(struct gendisk *disk, struct block_device *bdev,
			      sector_t offset);
extern void blk_queue_stack_limits(struct request_queue *t, struct request_queue *b);
extern void blk_queue_dma_pad(struct request_queue *, unsigned int);
extern void blk_queue_update_dma_pad(struct request_queue *, unsigned int);
extern int blk_queue_dma_drain(struct request_queue *q,
			       dma_drain_needed_fn *dma_drain_needed,
			       void *buf, unsigned int size);
extern void blk_queue_lld_busy(struct request_queue *q, lld_busy_fn *fn);
extern void blk_queue_segment_boundary(struct request_queue *, unsigned long);
extern void blk_queue_prep_rq(struct request_queue *, prep_rq_fn *pfn);
extern void blk_queue_merge_bvec(struct request_queue *, merge_bvec_fn *);
extern void blk_queue_dma_alignment(struct request_queue *, int);
extern void blk_queue_update_dma_alignment(struct request_queue *, int);
extern void blk_queue_softirq_done(struct request_queue *, softirq_done_fn *);
extern void blk_queue_rq_timed_out(struct request_queue *, rq_timed_out_fn *);
extern void blk_queue_rq_timeout(struct request_queue *, unsigned int);
extern struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev);
extern int blk_queue_ordered(struct request_queue *, unsigned, prepare_flush_fn *);
extern bool blk_do_ordered(struct request_queue *, struct request **);
extern unsigned blk_ordered_cur_seq(struct request_queue *);
extern unsigned blk_ordered_req_seq(struct request *);
extern bool blk_ordered_complete_seq(struct request_queue *, unsigned, int);

extern int blk_rq_map_sg(struct request_queue *, struct request *, struct scatterlist *);
extern void blk_dump_rq_flags(struct request *, char *);
extern void generic_unplug_device(struct request_queue *);
extern long nr_blockdev_pages(void);

int blk_get_queue(struct request_queue *);
struct request_queue *blk_alloc_queue(gfp_t);
struct request_queue *blk_alloc_queue_node(gfp_t, int);
extern void blk_put_queue(struct request_queue *);

/*
 * tag stuff
 */
#define blk_rq_tagged(rq)		((rq)->cmd_flags & REQ_QUEUED)
extern int blk_queue_start_tag(struct request_queue *, struct request *);
extern struct request *blk_queue_find_tag(struct request_queue *, int);
extern void blk_queue_end_tag(struct request_queue *, struct request *);
extern int blk_queue_init_tags(struct request_queue *, int, struct blk_queue_tag *);
extern void blk_queue_free_tags(struct request_queue *);
extern int blk_queue_resize_tags(struct request_queue *, int);
extern void blk_queue_invalidate_tags(struct request_queue *);
extern struct blk_queue_tag *blk_init_tags(int);
extern void blk_free_tags(struct blk_queue_tag *);

static inline struct request *blk_map_queue_find_tag(struct blk_queue_tag *bqt,
						int tag)
{
	if (unlikely(bqt == NULL || tag >= bqt->real_max_depth))
		return NULL;
	return bqt->tag_index[tag];
}

extern int blkdev_issue_flush(struct block_device *, sector_t *);
#define DISCARD_FL_WAIT		0x01	/* wait for completion */
#define DISCARD_FL_BARRIER	0x02	/* issue DISCARD_BARRIER request */
extern int blkdev_issue_discard(struct block_device *, sector_t sector,
		sector_t nr_sects, gfp_t, int flags);

static inline int sb_issue_discard(struct super_block *sb,
				   sector_t block, sector_t nr_blocks)
{
	block <<= (sb->s_blocksize_bits - 9);
	nr_blocks <<= (sb->s_blocksize_bits - 9);
	return blkdev_issue_discard(sb->s_bdev, block, nr_blocks, GFP_KERNEL,
				    DISCARD_FL_BARRIER);
}

extern int blk_verify_command(unsigned char *cmd, fmode_t has_write_perm);

#define MAX_PHYS_SEGMENTS 128
#define MAX_HW_SEGMENTS 128
#define SAFE_MAX_SECTORS 255
#define MAX_SEGMENT_SIZE	65536

enum blk_default_limits {
	BLK_MAX_SEGMENTS	= 128,
	BLK_SAFE_MAX_SECTORS	= 255,
	BLK_DEF_MAX_SECTORS	= 1024,
	BLK_MAX_SEGMENT_SIZE	= 65536,
	BLK_SEG_BOUNDARY_MASK	= 0xFFFFFFFFUL,
};

#define blkdev_entry_to_request(entry) list_entry((entry), struct request, queuelist)

static inline unsigned long queue_bounce_pfn(struct request_queue *q)
{
	return q->limits.bounce_pfn;
}

static inline unsigned long queue_segment_boundary(struct request_queue *q)
{
	return q->limits.seg_boundary_mask;
}

static inline unsigned int queue_max_sectors(struct request_queue *q)
{
	return q->limits.max_sectors;
}

static inline unsigned int queue_max_hw_sectors(struct request_queue *q)
{
	return q->limits.max_hw_sectors;
}

static inline unsigned short queue_max_segments(struct request_queue *q)
{
	return q->limits.max_segments;
}

static inline unsigned int queue_max_segment_size(struct request_queue *q)
{
	return q->limits.max_segment_size;
}

static inline unsigned short queue_logical_block_size(struct request_queue *q)
{
	int retval = 512;

	if (q && q->limits.logical_block_size)
		retval = q->limits.logical_block_size;

	return retval;
}

static inline unsigned short bdev_logical_block_size(struct block_device *bdev)
{
	return queue_logical_block_size(bdev_get_queue(bdev));
}

static inline unsigned int queue_physical_block_size(struct request_queue *q)
{
	return q->limits.physical_block_size;
}

static inline int bdev_physical_block_size(struct block_device *bdev)
{
	return queue_physical_block_size(bdev_get_queue(bdev));
}

static inline unsigned int queue_io_min(struct request_queue *q)
{
	return q->limits.io_min;
}

static inline int bdev_io_min(struct block_device *bdev)
{
	return queue_io_min(bdev_get_queue(bdev));
}

static inline unsigned int queue_io_opt(struct request_queue *q)
{
	return q->limits.io_opt;
}

static inline int bdev_io_opt(struct block_device *bdev)
{
	return queue_io_opt(bdev_get_queue(bdev));
}

static inline int queue_alignment_offset(struct request_queue *q)
{
	if (q->limits.misaligned)
		return -1;

	return q->limits.alignment_offset;
}

static inline int queue_limit_alignment_offset(struct queue_limits *lim, sector_t sector)
{
	unsigned int granularity = max(lim->physical_block_size, lim->io_min);
	unsigned int alignment = (sector << 9) & (granularity - 1);

	return (granularity + lim->alignment_offset - alignment)
		& (granularity - 1);
}

static inline int bdev_alignment_offset(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (q->limits.misaligned)
		return -1;

	if (bdev != bdev->bd_contains)
		return bdev->bd_part->alignment_offset;

	return q->limits.alignment_offset;
}

static inline int queue_discard_alignment(struct request_queue *q)
{
	if (q->limits.discard_misaligned)
		return -1;

	return q->limits.discard_alignment;
}

static inline int queue_limit_discard_alignment(struct queue_limits *lim, sector_t sector)
{
	unsigned int alignment = (sector << 9) & (lim->discard_granularity - 1);

	return (lim->discard_granularity + lim->discard_alignment - alignment)
		& (lim->discard_granularity - 1);
}

static inline unsigned int queue_discard_zeroes_data(struct request_queue *q)
{
	if (q->limits.discard_zeroes_data == 1)
		return 1;

	return 0;
}

static inline unsigned int bdev_discard_zeroes_data(struct block_device *bdev)
{
	return queue_discard_zeroes_data(bdev_get_queue(bdev));
}

static inline int queue_dma_alignment(struct request_queue *q)
{
	return q ? q->dma_alignment : 511;
}

static inline int blk_rq_aligned(struct request_queue *q, void *addr,
				 unsigned int len)
{
	unsigned int alignment = queue_dma_alignment(q) | q->dma_pad_mask;
	return !((unsigned long)addr & alignment) && !(len & alignment);
}

/* assumes size > 256 */
static inline unsigned int blksize_bits(unsigned int size)
{
	unsigned int bits = 8;
	do {
		bits++;
		size >>= 1;
	} while (size > 256);
	return bits;
}

static inline unsigned int block_size(struct block_device *bdev)
{
	return bdev->bd_block_size;
}

typedef struct {struct page *v;} Sector;

unsigned char *read_dev_sector(struct block_device *, sector_t, Sector *);

static inline void put_dev_sector(Sector p)
{
	page_cache_release(p.v);
}

struct work_struct;
int kblockd_schedule_work(struct request_queue *q, struct work_struct *work);

#define MODULE_ALIAS_BLOCKDEV(major,minor) \
	MODULE_ALIAS("block-major-" __stringify(major) "-" __stringify(minor))
#define MODULE_ALIAS_BLOCKDEV_MAJOR(major) \
	MODULE_ALIAS("block-major-" __stringify(major) "-*")

#if defined(CONFIG_BLK_DEV_INTEGRITY)

#define INTEGRITY_FLAG_READ	2	/* verify data integrity on read */
#define INTEGRITY_FLAG_WRITE	4	/* generate data integrity on write */

struct blk_integrity_exchg {
	void			*prot_buf;
	void			*data_buf;
	sector_t		sector;
	unsigned int		data_size;
	unsigned short		sector_size;
	const char		*disk_name;
};

typedef void (integrity_gen_fn) (struct blk_integrity_exchg *);
typedef int (integrity_vrfy_fn) (struct blk_integrity_exchg *);
typedef void (integrity_set_tag_fn) (void *, void *, unsigned int);
typedef void (integrity_get_tag_fn) (void *, void *, unsigned int);

struct blk_integrity {
	integrity_gen_fn	*generate_fn;
	integrity_vrfy_fn	*verify_fn;
	integrity_set_tag_fn	*set_tag_fn;
	integrity_get_tag_fn	*get_tag_fn;

	unsigned short		flags;
	unsigned short		tuple_size;
	unsigned short		sector_size;
	unsigned short		tag_size;

	const char		*name;

	struct kobject		kobj;
};

extern int blk_integrity_register(struct gendisk *, struct blk_integrity *);
extern void blk_integrity_unregister(struct gendisk *);
extern int blk_integrity_compare(struct gendisk *, struct gendisk *);
extern int blk_rq_map_integrity_sg(struct request *, struct scatterlist *);
extern int blk_rq_count_integrity_sg(struct request *);

static inline
struct blk_integrity *bdev_get_integrity(struct block_device *bdev)
{
	return bdev->bd_disk->integrity;
}

static inline struct blk_integrity *blk_get_integrity(struct gendisk *disk)
{
	return disk->integrity;
}

static inline int blk_integrity_rq(struct request *rq)
{
	if (rq->bio == NULL)
		return 0;

	return bio_integrity(rq->bio);
}

#else /* CONFIG_BLK_DEV_INTEGRITY */

#define blk_integrity_rq(rq)			(0)
#define blk_rq_count_integrity_sg(a)		(0)
#define blk_rq_map_integrity_sg(a, b)		(0)
#define bdev_get_integrity(a)			(0)
#define blk_get_integrity(a)			(0)
#define blk_integrity_compare(a, b)		(0)
#define blk_integrity_register(a, b)		(0)
#define blk_integrity_unregister(a)		do { } while (0);

#endif /* CONFIG_BLK_DEV_INTEGRITY */

struct block_device_operations {
	int (*open) (struct block_device *, fmode_t);
	int (*release) (struct gendisk *, fmode_t);
	int (*locked_ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
	int (*ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
	int (*compat_ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
	int (*direct_access) (struct block_device *, sector_t,
						void **, unsigned long *);
	int (*media_changed) (struct gendisk *);
	unsigned long long (*set_capacity) (struct gendisk *,
						unsigned long long);
	int (*revalidate_disk) (struct gendisk *);
	int (*getgeo)(struct block_device *, struct hd_geometry *);
	struct module *owner;
};

extern int __blkdev_driver_ioctl(struct block_device *, fmode_t, unsigned int,
				 unsigned long);
#else /* CONFIG_BLOCK */
/*
 * stubs for when the block layer is configured out
 */
#define buffer_heads_over_limit 0

static inline long nr_blockdev_pages(void)
{
	return 0;
}

#endif /* CONFIG_BLOCK */

#endif
