/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_MQ_H
#define BLK_MQ_H

#include <linux/blkdev.h>
#include <linux/sbitmap.h>
#include <linux/srcu.h>

struct blk_mq_tags;
struct blk_flush_queue;

/**
 * struct blk_mq_hw_ctx - State for a hardware queue facing the hardware
 * block device
 */
struct blk_mq_hw_ctx {
	struct {
		/** @lock: Protects the dispatch list. */
		spinlock_t		lock;
		/**
		 * @dispatch: Used for requests that are ready to be
		 * dispatched to the hardware but for some reason (e.g. lack of
		 * resources) could not be sent to the hardware. As soon as the
		 * driver can send new requests, requests at this list will
		 * be sent first for a fairer dispatch.
		 */
		struct list_head	dispatch;
		 /**
		  * @state: BLK_MQ_S_* flags. Defines the state of the hw
		  * queue (active, scheduled to restart, stopped).
		  */
		unsigned long		state;
	} ____cacheline_aligned_in_smp;

	/**
	 * @run_work: Used for scheduling a hardware queue run at a later time.
	 */
	struct delayed_work	run_work;
	/** @cpumask: Map of available CPUs where this hctx can run. */
	cpumask_var_t		cpumask;
	/**
	 * @next_cpu: Used by blk_mq_hctx_next_cpu() for round-robin CPU
	 * selection from @cpumask.
	 */
	int			next_cpu;
	/**
	 * @next_cpu_batch: Counter of how many works left in the batch before
	 * changing to the next CPU.
	 */
	int			next_cpu_batch;

	/** @flags: BLK_MQ_F_* flags. Defines the behaviour of the queue. */
	unsigned long		flags;

	/**
	 * @sched_data: Pointer owned by the IO scheduler attached to a request
	 * queue. It's up to the IO scheduler how to use this pointer.
	 */
	void			*sched_data;
	/**
	 * @queue: Pointer to the request queue that owns this hardware context.
	 */
	struct request_queue	*queue;
	/** @fq: Queue of requests that need to perform a flush operation. */
	struct blk_flush_queue	*fq;

	/**
	 * @driver_data: Pointer to data owned by the block driver that created
	 * this hctx
	 */
	void			*driver_data;

	/**
	 * @ctx_map: Bitmap for each software queue. If bit is on, there is a
	 * pending request in that software queue.
	 */
	struct sbitmap		ctx_map;

	/**
	 * @dispatch_from: Software queue to be used when no scheduler was
	 * selected.
	 */
	struct blk_mq_ctx	*dispatch_from;
	/**
	 * @dispatch_busy: Number used by blk_mq_update_dispatch_busy() to
	 * decide if the hw_queue is busy using Exponential Weighted Moving
	 * Average algorithm.
	 */
	unsigned int		dispatch_busy;

	/** @type: HCTX_TYPE_* flags. Type of hardware queue. */
	unsigned short		type;
	/** @nr_ctx: Number of software queues. */
	unsigned short		nr_ctx;
	/** @ctxs: Array of software queues. */
	struct blk_mq_ctx	**ctxs;

	/** @dispatch_wait_lock: Lock for dispatch_wait queue. */
	spinlock_t		dispatch_wait_lock;
	/**
	 * @dispatch_wait: Waitqueue to put requests when there is no tag
	 * available at the moment, to wait for another try in the future.
	 */
	wait_queue_entry_t	dispatch_wait;

	/**
	 * @wait_index: Index of next available dispatch_wait queue to insert
	 * requests.
	 */
	atomic_t		wait_index;

	/**
	 * @tags: Tags owned by the block driver. A tag at this set is only
	 * assigned when a request is dispatched from a hardware queue.
	 */
	struct blk_mq_tags	*tags;
	/**
	 * @sched_tags: Tags owned by I/O scheduler. If there is an I/O
	 * scheduler associated with a request queue, a tag is assigned when
	 * that request is allocated. Else, this member is not used.
	 */
	struct blk_mq_tags	*sched_tags;

	/** @queued: Number of queued requests. */
	unsigned long		queued;
	/** @run: Number of dispatched requests. */
	unsigned long		run;
#define BLK_MQ_MAX_DISPATCH_ORDER	7
	/** @dispatched: Number of dispatch requests by queue. */
	unsigned long		dispatched[BLK_MQ_MAX_DISPATCH_ORDER];

	/** @numa_node: NUMA node the storage adapter has been connected to. */
	unsigned int		numa_node;
	/** @queue_num: Index of this hardware queue. */
	unsigned int		queue_num;

	/**
	 * @nr_active: Number of active requests. Only used when a tag set is
	 * shared across request queues.
	 */
	atomic_t		nr_active;
	/**
	 * @elevator_queued: Number of queued requests on hctx.
	 */
	atomic_t                elevator_queued;

	/** @cpuhp_online: List to store request if CPU is going to die */
	struct hlist_node	cpuhp_online;
	/** @cpuhp_dead: List to store request if some CPU die. */
	struct hlist_node	cpuhp_dead;
	/** @kobj: Kernel object for sysfs. */
	struct kobject		kobj;

	/** @poll_considered: Count times blk_poll() was called. */
	unsigned long		poll_considered;
	/** @poll_invoked: Count how many requests blk_poll() polled. */
	unsigned long		poll_invoked;
	/** @poll_success: Count how many polled requests were completed. */
	unsigned long		poll_success;

#ifdef CONFIG_BLK_DEBUG_FS
	/**
	 * @debugfs_dir: debugfs directory for this hardware queue. Named
	 * as cpu<cpu_number>.
	 */
	struct dentry		*debugfs_dir;
	/** @sched_debugfs_dir:	debugfs directory for the scheduler. */
	struct dentry		*sched_debugfs_dir;
#endif

	/**
	 * @hctx_list: if this hctx is not in use, this is an entry in
	 * q->unused_hctx_list.
	 */
	struct list_head	hctx_list;

	/**
	 * @srcu: Sleepable RCU. Use as lock when type of the hardware queue is
	 * blocking (BLK_MQ_F_BLOCKING). Must be the last member - see also
	 * blk_mq_hw_ctx_size().
	 */
	struct srcu_struct	srcu[];
};

/**
 * struct blk_mq_queue_map - Map software queues to hardware queues
 * @mq_map:       CPU ID to hardware queue index map. This is an array
 *	with nr_cpu_ids elements. Each element has a value in the range
 *	[@queue_offset, @queue_offset + @nr_queues).
 * @nr_queues:    Number of hardware queues to map CPU IDs onto.
 * @queue_offset: First hardware queue to map onto. Used by the PCIe NVMe
 *	driver to map each hardware queue type (enum hctx_type) onto a distinct
 *	set of hardware queues.
 */
struct blk_mq_queue_map {
	unsigned int *mq_map;
	unsigned int nr_queues;
	unsigned int queue_offset;
};

/**
 * enum hctx_type - Type of hardware queue
 * @HCTX_TYPE_DEFAULT:	All I/O not otherwise accounted for.
 * @HCTX_TYPE_READ:	Just for READ I/O.
 * @HCTX_TYPE_POLL:	Polled I/O of any kind.
 * @HCTX_MAX_TYPES:	Number of types of hctx.
 */
enum hctx_type {
	HCTX_TYPE_DEFAULT,
	HCTX_TYPE_READ,
	HCTX_TYPE_POLL,

	HCTX_MAX_TYPES,
};

/**
 * struct blk_mq_tag_set - tag set that can be shared between request queues
 * @map:	   One or more ctx -> hctx mappings. One map exists for each
 *		   hardware queue type (enum hctx_type) that the driver wishes
 *		   to support. There are no restrictions on maps being of the
 *		   same size, and it's perfectly legal to share maps between
 *		   types.
 * @nr_maps:	   Number of elements in the @map array. A number in the range
 *		   [1, HCTX_MAX_TYPES].
 * @ops:	   Pointers to functions that implement block driver behavior.
 * @nr_hw_queues:  Number of hardware queues supported by the block driver that
 *		   owns this data structure.
 * @queue_depth:   Number of tags per hardware queue, reserved tags included.
 * @reserved_tags: Number of tags to set aside for BLK_MQ_REQ_RESERVED tag
 *		   allocations.
 * @cmd_size:	   Number of additional bytes to allocate per request. The block
 *		   driver owns these additional bytes.
 * @numa_node:	   NUMA node the storage adapter has been connected to.
 * @timeout:	   Request processing timeout in jiffies.
 * @flags:	   Zero or more BLK_MQ_F_* flags.
 * @driver_data:   Pointer to data owned by the block driver that created this
 *		   tag set.
 * @__bitmap_tags: A shared tags sbitmap, used over all hctx's
 * @__breserved_tags:
 *		   A shared reserved tags sbitmap, used over all hctx's
 * @tags:	   Tag sets. One tag set per hardware queue. Has @nr_hw_queues
 *		   elements.
 * @tag_list_lock: Serializes tag_list accesses.
 * @tag_list:	   List of the request queues that use this tag set. See also
 *		   request_queue.tag_set_list.
 */
struct blk_mq_tag_set {
	struct blk_mq_queue_map	map[HCTX_MAX_TYPES];
	unsigned int		nr_maps;
	const struct blk_mq_ops	*ops;
	unsigned int		nr_hw_queues;
	unsigned int		queue_depth;
	unsigned int		reserved_tags;
	unsigned int		cmd_size;
	int			numa_node;
	unsigned int		timeout;
	unsigned int		flags;
	void			*driver_data;
	atomic_t		active_queues_shared_sbitmap;

	struct sbitmap_queue	__bitmap_tags;
	struct sbitmap_queue	__breserved_tags;
	struct blk_mq_tags	**tags;

	struct mutex		tag_list_lock;
	struct list_head	tag_list;
};

/**
 * struct blk_mq_queue_data - Data about a request inserted in a queue
 *
 * @rq:   Request pointer.
 * @last: If it is the last request in the queue.
 */
struct blk_mq_queue_data {
	struct request *rq;
	bool last;
};

typedef bool (busy_iter_fn)(struct blk_mq_hw_ctx *, struct request *, void *,
		bool);
typedef bool (busy_tag_iter_fn)(struct request *, void *, bool);

/**
 * struct blk_mq_ops - Callback functions that implements block driver
 * behaviour.
 */
struct blk_mq_ops {
	/**
	 * @queue_rq: Queue a new request from block IO.
	 */
	blk_status_t (*queue_rq)(struct blk_mq_hw_ctx *,
				 const struct blk_mq_queue_data *);

	/**
	 * @commit_rqs: If a driver uses bd->last to judge when to submit
	 * requests to hardware, it must define this function. In case of errors
	 * that make us stop issuing further requests, this hook serves the
	 * purpose of kicking the hardware (which the last request otherwise
	 * would have done).
	 */
	void (*commit_rqs)(struct blk_mq_hw_ctx *);

	/**
	 * @get_budget: Reserve budget before queue request, once .queue_rq is
	 * run, it is driver's responsibility to release the
	 * reserved budget. Also we have to handle failure case
	 * of .get_budget for avoiding I/O deadlock.
	 */
	bool (*get_budget)(struct request_queue *);

	/**
	 * @put_budget: Release the reserved budget.
	 */
	void (*put_budget)(struct request_queue *);

	/**
	 * @timeout: Called on request timeout.
	 */
	enum blk_eh_timer_return (*timeout)(struct request *, bool);

	/**
	 * @poll: Called to poll for completion of a specific tag.
	 */
	int (*poll)(struct blk_mq_hw_ctx *);

	/**
	 * @complete: Mark the request as complete.
	 */
	void (*complete)(struct request *);

	/**
	 * @init_hctx: Called when the block layer side of a hardware queue has
	 * been set up, allowing the driver to allocate/init matching
	 * structures.
	 */
	int (*init_hctx)(struct blk_mq_hw_ctx *, void *, unsigned int);
	/**
	 * @exit_hctx: Ditto for exit/teardown.
	 */
	void (*exit_hctx)(struct blk_mq_hw_ctx *, unsigned int);

	/**
	 * @init_request: Called for every command allocated by the block layer
	 * to allow the driver to set up driver specific data.
	 *
	 * Tag greater than or equal to queue_depth is for setting up
	 * flush request.
	 */
	int (*init_request)(struct blk_mq_tag_set *set, struct request *,
			    unsigned int, unsigned int);
	/**
	 * @exit_request: Ditto for exit/teardown.
	 */
	void (*exit_request)(struct blk_mq_tag_set *set, struct request *,
			     unsigned int);

	/**
	 * @initialize_rq_fn: Called from inside blk_get_request().
	 */
	void (*initialize_rq_fn)(struct request *rq);

	/**
	 * @cleanup_rq: Called before freeing one request which isn't completed
	 * yet, and usually for freeing the driver private data.
	 */
	void (*cleanup_rq)(struct request *);

	/**
	 * @busy: If set, returns whether or not this queue currently is busy.
	 */
	bool (*busy)(struct request_queue *);

	/**
	 * @map_queues: This allows drivers specify their own queue mapping by
	 * overriding the setup-time function that builds the mq_map.
	 */
	int (*map_queues)(struct blk_mq_tag_set *set);

#ifdef CONFIG_BLK_DEBUG_FS
	/**
	 * @show_rq: Used by the debugfs implementation to show driver-specific
	 * information about a request.
	 */
	void (*show_rq)(struct seq_file *m, struct request *rq);
#endif
};

enum {
	BLK_MQ_F_SHOULD_MERGE	= 1 << 0,
	BLK_MQ_F_TAG_QUEUE_SHARED = 1 << 1,
	/*
	 * Set when this device requires underlying blk-mq device for
	 * completing IO:
	 */
	BLK_MQ_F_STACKING	= 1 << 2,
	BLK_MQ_F_TAG_HCTX_SHARED = 1 << 3,
	BLK_MQ_F_BLOCKING	= 1 << 5,
	BLK_MQ_F_NO_SCHED	= 1 << 6,
	BLK_MQ_F_ALLOC_POLICY_START_BIT = 8,
	BLK_MQ_F_ALLOC_POLICY_BITS = 1,

	BLK_MQ_S_STOPPED	= 0,
	BLK_MQ_S_TAG_ACTIVE	= 1,
	BLK_MQ_S_SCHED_RESTART	= 2,

	/* hw queue is inactive after all its CPUs become offline */
	BLK_MQ_S_INACTIVE	= 3,

	BLK_MQ_MAX_DEPTH	= 10240,

	BLK_MQ_CPU_WORK_BATCH	= 8,
};
#define BLK_MQ_FLAG_TO_ALLOC_POLICY(flags) \
	((flags >> BLK_MQ_F_ALLOC_POLICY_START_BIT) & \
		((1 << BLK_MQ_F_ALLOC_POLICY_BITS) - 1))
#define BLK_ALLOC_POLICY_TO_MQ_FLAG(policy) \
	((policy & ((1 << BLK_MQ_F_ALLOC_POLICY_BITS) - 1)) \
		<< BLK_MQ_F_ALLOC_POLICY_START_BIT)

struct request_queue *blk_mq_init_queue(struct blk_mq_tag_set *);
struct request_queue *blk_mq_init_queue_data(struct blk_mq_tag_set *set,
		void *queuedata);
struct request_queue *blk_mq_init_allocated_queue(struct blk_mq_tag_set *set,
						  struct request_queue *q,
						  bool elevator_init);
struct request_queue *blk_mq_init_sq_queue(struct blk_mq_tag_set *set,
						const struct blk_mq_ops *ops,
						unsigned int queue_depth,
						unsigned int set_flags);
void blk_mq_unregister_dev(struct device *, struct request_queue *);

int blk_mq_alloc_tag_set(struct blk_mq_tag_set *set);
void blk_mq_free_tag_set(struct blk_mq_tag_set *set);

void blk_mq_flush_plug_list(struct blk_plug *plug, bool from_schedule);

void blk_mq_free_request(struct request *rq);

bool blk_mq_queue_inflight(struct request_queue *q);

enum {
	/* return when out of requests */
	BLK_MQ_REQ_NOWAIT	= (__force blk_mq_req_flags_t)(1 << 0),
	/* allocate from reserved pool */
	BLK_MQ_REQ_RESERVED	= (__force blk_mq_req_flags_t)(1 << 1),
	/* set RQF_PREEMPT */
	BLK_MQ_REQ_PREEMPT	= (__force blk_mq_req_flags_t)(1 << 3),
};

struct request *blk_mq_alloc_request(struct request_queue *q, unsigned int op,
		blk_mq_req_flags_t flags);
struct request *blk_mq_alloc_request_hctx(struct request_queue *q,
		unsigned int op, blk_mq_req_flags_t flags,
		unsigned int hctx_idx);
struct request *blk_mq_tag_to_rq(struct blk_mq_tags *tags, unsigned int tag);

enum {
	BLK_MQ_UNIQUE_TAG_BITS = 16,
	BLK_MQ_UNIQUE_TAG_MASK = (1 << BLK_MQ_UNIQUE_TAG_BITS) - 1,
};

u32 blk_mq_unique_tag(struct request *rq);

static inline u16 blk_mq_unique_tag_to_hwq(u32 unique_tag)
{
	return unique_tag >> BLK_MQ_UNIQUE_TAG_BITS;
}

static inline u16 blk_mq_unique_tag_to_tag(u32 unique_tag)
{
	return unique_tag & BLK_MQ_UNIQUE_TAG_MASK;
}

/**
 * blk_mq_rq_state() - read the current MQ_RQ_* state of a request
 * @rq: target request.
 */
static inline enum mq_rq_state blk_mq_rq_state(struct request *rq)
{
	return READ_ONCE(rq->state);
}

static inline int blk_mq_request_started(struct request *rq)
{
	return blk_mq_rq_state(rq) != MQ_RQ_IDLE;
}

static inline int blk_mq_request_completed(struct request *rq)
{
	return blk_mq_rq_state(rq) == MQ_RQ_COMPLETE;
}

void blk_mq_start_request(struct request *rq);
void blk_mq_end_request(struct request *rq, blk_status_t error);
void __blk_mq_end_request(struct request *rq, blk_status_t error);

void blk_mq_requeue_request(struct request *rq, bool kick_requeue_list);
void blk_mq_kick_requeue_list(struct request_queue *q);
void blk_mq_delay_kick_requeue_list(struct request_queue *q, unsigned long msecs);
void blk_mq_complete_request(struct request *rq);
bool blk_mq_complete_request_remote(struct request *rq);
bool blk_mq_queue_stopped(struct request_queue *q);
void blk_mq_stop_hw_queue(struct blk_mq_hw_ctx *hctx);
void blk_mq_start_hw_queue(struct blk_mq_hw_ctx *hctx);
void blk_mq_stop_hw_queues(struct request_queue *q);
void blk_mq_start_hw_queues(struct request_queue *q);
void blk_mq_start_stopped_hw_queue(struct blk_mq_hw_ctx *hctx, bool async);
void blk_mq_start_stopped_hw_queues(struct request_queue *q, bool async);
void blk_mq_quiesce_queue(struct request_queue *q);
void blk_mq_unquiesce_queue(struct request_queue *q);
void blk_mq_delay_run_hw_queue(struct blk_mq_hw_ctx *hctx, unsigned long msecs);
void blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async);
void blk_mq_run_hw_queues(struct request_queue *q, bool async);
void blk_mq_delay_run_hw_queues(struct request_queue *q, unsigned long msecs);
void blk_mq_tagset_busy_iter(struct blk_mq_tag_set *tagset,
		busy_tag_iter_fn *fn, void *priv);
void blk_mq_tagset_wait_completed_request(struct blk_mq_tag_set *tagset);
void blk_mq_freeze_queue(struct request_queue *q);
void blk_mq_unfreeze_queue(struct request_queue *q);
void blk_freeze_queue_start(struct request_queue *q);
void blk_mq_freeze_queue_wait(struct request_queue *q);
int blk_mq_freeze_queue_wait_timeout(struct request_queue *q,
				     unsigned long timeout);

int blk_mq_map_queues(struct blk_mq_queue_map *qmap);
void blk_mq_update_nr_hw_queues(struct blk_mq_tag_set *set, int nr_hw_queues);

void blk_mq_quiesce_queue_nowait(struct request_queue *q);

unsigned int blk_mq_rq_cpu(struct request *rq);

bool __blk_should_fake_timeout(struct request_queue *q);
static inline bool blk_should_fake_timeout(struct request_queue *q)
{
	if (IS_ENABLED(CONFIG_FAIL_IO_TIMEOUT) &&
	    test_bit(QUEUE_FLAG_FAIL_IO, &q->queue_flags))
		return __blk_should_fake_timeout(q);
	return false;
}

/**
 * blk_mq_rq_from_pdu - cast a PDU to a request
 * @pdu: the PDU (Protocol Data Unit) to be casted
 *
 * Return: request
 *
 * Driver command data is immediately after the request. So subtract request
 * size to get back to the original request.
 */
static inline struct request *blk_mq_rq_from_pdu(void *pdu)
{
	return pdu - sizeof(struct request);
}

/**
 * blk_mq_rq_to_pdu - cast a request to a PDU
 * @rq: the request to be casted
 *
 * Return: pointer to the PDU
 *
 * Driver command data is immediately after the request. So add request to get
 * the PDU.
 */
static inline void *blk_mq_rq_to_pdu(struct request *rq)
{
	return rq + 1;
}

#define queue_for_each_hw_ctx(q, hctx, i)				\
	for ((i) = 0; (i) < (q)->nr_hw_queues &&			\
	     ({ hctx = (q)->queue_hw_ctx[i]; 1; }); (i)++)

#define hctx_for_each_ctx(hctx, ctx, i)					\
	for ((i) = 0; (i) < (hctx)->nr_ctx &&				\
	     ({ ctx = (hctx)->ctxs[(i)]; 1; }); (i)++)

static inline blk_qc_t request_to_qc_t(struct blk_mq_hw_ctx *hctx,
		struct request *rq)
{
	if (rq->tag != -1)
		return rq->tag | (hctx->queue_num << BLK_QC_T_SHIFT);

	return rq->internal_tag | (hctx->queue_num << BLK_QC_T_SHIFT) |
			BLK_QC_T_INTERNAL;
}

static inline void blk_mq_cleanup_rq(struct request *rq)
{
	if (rq->q->mq_ops->cleanup_rq)
		rq->q->mq_ops->cleanup_rq(rq);
}

blk_qc_t blk_mq_submit_bio(struct bio *bio);

#endif
