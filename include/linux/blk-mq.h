/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_MQ_H
#define BLK_MQ_H

#include <linux/blkdev.h>
#include <linux/sbitmap.h>
#include <linux/srcu.h>

struct blk_mq_tags;
struct blk_flush_queue;

/**
 * struct blk_mq_hw_ctx - State for a hardware queue facing the hardware block device
 */
struct blk_mq_hw_ctx {
	struct {
		spinlock_t		lock;
		struct list_head	dispatch;
		unsigned long		state;		/* BLK_MQ_S_* flags */
	} ____cacheline_aligned_in_smp;

	struct delayed_work	run_work;
	cpumask_var_t		cpumask;
	int			next_cpu;
	int			next_cpu_batch;

	unsigned long		flags;		/* BLK_MQ_F_* flags */

	void			*sched_data;
	struct request_queue	*queue;
	struct blk_flush_queue	*fq;

	void			*driver_data;

	struct sbitmap		ctx_map;

	struct blk_mq_ctx	*dispatch_from;

	struct blk_mq_ctx	**ctxs;
	unsigned int		nr_ctx;

	wait_queue_entry_t	dispatch_wait;
	atomic_t		wait_index;

	struct blk_mq_tags	*tags;
	struct blk_mq_tags	*sched_tags;

	unsigned long		queued;
	unsigned long		run;
#define BLK_MQ_MAX_DISPATCH_ORDER	7
	unsigned long		dispatched[BLK_MQ_MAX_DISPATCH_ORDER];

	unsigned int		numa_node;
	unsigned int		queue_num;

	atomic_t		nr_active;
	unsigned int		nr_expired;

	struct hlist_node	cpuhp_dead;
	struct kobject		kobj;

	unsigned long		poll_considered;
	unsigned long		poll_invoked;
	unsigned long		poll_success;

#ifdef CONFIG_BLK_DEBUG_FS
	struct dentry		*debugfs_dir;
	struct dentry		*sched_debugfs_dir;
#endif

	/* Must be the last member - see also blk_mq_hw_ctx_size(). */
	struct srcu_struct	srcu[0];
};

struct blk_mq_tag_set {
	unsigned int		*mq_map;
	const struct blk_mq_ops	*ops;
	unsigned int		nr_hw_queues;
	unsigned int		queue_depth;	/* max hw supported */
	unsigned int		reserved_tags;
	unsigned int		cmd_size;	/* per-request extra data */
	int			numa_node;
	unsigned int		timeout;
	unsigned int		flags;		/* BLK_MQ_F_* */
	void			*driver_data;

	struct blk_mq_tags	**tags;

	struct mutex		tag_list_lock;
	struct list_head	tag_list;
};

struct blk_mq_queue_data {
	struct request *rq;
	bool last;
};

typedef blk_status_t (queue_rq_fn)(struct blk_mq_hw_ctx *,
		const struct blk_mq_queue_data *);
typedef bool (get_budget_fn)(struct blk_mq_hw_ctx *);
typedef void (put_budget_fn)(struct blk_mq_hw_ctx *);
typedef enum blk_eh_timer_return (timeout_fn)(struct request *, bool);
typedef int (init_hctx_fn)(struct blk_mq_hw_ctx *, void *, unsigned int);
typedef void (exit_hctx_fn)(struct blk_mq_hw_ctx *, unsigned int);
typedef int (init_request_fn)(struct blk_mq_tag_set *set, struct request *,
		unsigned int, unsigned int);
typedef void (exit_request_fn)(struct blk_mq_tag_set *set, struct request *,
		unsigned int);

typedef void (busy_iter_fn)(struct blk_mq_hw_ctx *, struct request *, void *,
		bool);
typedef void (busy_tag_iter_fn)(struct request *, void *, bool);
typedef int (poll_fn)(struct blk_mq_hw_ctx *, unsigned int);
typedef int (map_queues_fn)(struct blk_mq_tag_set *set);


struct blk_mq_ops {
	/*
	 * Queue request
	 */
	queue_rq_fn		*queue_rq;

	/*
	 * Reserve budget before queue request, once .queue_rq is
	 * run, it is driver's responsibility to release the
	 * reserved budget. Also we have to handle failure case
	 * of .get_budget for avoiding I/O deadlock.
	 */
	get_budget_fn		*get_budget;
	put_budget_fn		*put_budget;

	/*
	 * Called on request timeout
	 */
	timeout_fn		*timeout;

	/*
	 * Called to poll for completion of a specific tag.
	 */
	poll_fn			*poll;

	softirq_done_fn		*complete;

	/*
	 * Called when the block layer side of a hardware queue has been
	 * set up, allowing the driver to allocate/init matching structures.
	 * Ditto for exit/teardown.
	 */
	init_hctx_fn		*init_hctx;
	exit_hctx_fn		*exit_hctx;

	/*
	 * Called for every command allocated by the block layer to allow
	 * the driver to set up driver specific data.
	 *
	 * Tag greater than or equal to queue_depth is for setting up
	 * flush request.
	 *
	 * Ditto for exit/teardown.
	 */
	init_request_fn		*init_request;
	exit_request_fn		*exit_request;
	/* Called from inside blk_get_request() */
	void (*initialize_rq_fn)(struct request *rq);

	map_queues_fn		*map_queues;

#ifdef CONFIG_BLK_DEBUG_FS
	/*
	 * Used by the debugfs implementation to show driver-specific
	 * information about a request.
	 */
	void (*show_rq)(struct seq_file *m, struct request *rq);
#endif
};

enum {
	BLK_MQ_F_SHOULD_MERGE	= 1 << 0,
	BLK_MQ_F_TAG_SHARED	= 1 << 1,
	BLK_MQ_F_SG_MERGE	= 1 << 2,
	BLK_MQ_F_BLOCKING	= 1 << 5,
	BLK_MQ_F_NO_SCHED	= 1 << 6,
	BLK_MQ_F_ALLOC_POLICY_START_BIT = 8,
	BLK_MQ_F_ALLOC_POLICY_BITS = 1,

	BLK_MQ_S_STOPPED	= 0,
	BLK_MQ_S_TAG_ACTIVE	= 1,
	BLK_MQ_S_SCHED_RESTART	= 2,

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
struct request_queue *blk_mq_init_allocated_queue(struct blk_mq_tag_set *set,
						  struct request_queue *q);
int blk_mq_register_dev(struct device *, struct request_queue *);
void blk_mq_unregister_dev(struct device *, struct request_queue *);

int blk_mq_alloc_tag_set(struct blk_mq_tag_set *set);
void blk_mq_free_tag_set(struct blk_mq_tag_set *set);

void blk_mq_flush_plug_list(struct blk_plug *plug, bool from_schedule);

void blk_mq_free_request(struct request *rq);
bool blk_mq_can_queue(struct blk_mq_hw_ctx *);

enum {
	/* return when out of requests */
	BLK_MQ_REQ_NOWAIT	= (__force blk_mq_req_flags_t)(1 << 0),
	/* allocate from reserved pool */
	BLK_MQ_REQ_RESERVED	= (__force blk_mq_req_flags_t)(1 << 1),
	/* allocate internal/sched tag */
	BLK_MQ_REQ_INTERNAL	= (__force blk_mq_req_flags_t)(1 << 2),
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


int blk_mq_request_started(struct request *rq);
void blk_mq_start_request(struct request *rq);
void blk_mq_end_request(struct request *rq, blk_status_t error);
void __blk_mq_end_request(struct request *rq, blk_status_t error);

void blk_mq_requeue_request(struct request *rq, bool kick_requeue_list);
void blk_mq_add_to_requeue_list(struct request *rq, bool at_head,
				bool kick_requeue_list);
void blk_mq_kick_requeue_list(struct request_queue *q);
void blk_mq_delay_kick_requeue_list(struct request_queue *q, unsigned long msecs);
void blk_mq_complete_request(struct request *rq);
bool blk_mq_bio_list_merge(struct request_queue *q, struct list_head *list,
			   struct bio *bio);
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
bool blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async);
void blk_mq_run_hw_queues(struct request_queue *q, bool async);
void blk_mq_tagset_busy_iter(struct blk_mq_tag_set *tagset,
		busy_tag_iter_fn *fn, void *priv);
void blk_mq_freeze_queue(struct request_queue *q);
void blk_mq_unfreeze_queue(struct request_queue *q);
void blk_freeze_queue_start(struct request_queue *q);
void blk_mq_freeze_queue_wait(struct request_queue *q);
int blk_mq_freeze_queue_wait_timeout(struct request_queue *q,
				     unsigned long timeout);
int blk_mq_tagset_iter(struct blk_mq_tag_set *set, void *data,
		int (reinit_request)(void *, struct request *));

int blk_mq_map_queues(struct blk_mq_tag_set *set);
void blk_mq_update_nr_hw_queues(struct blk_mq_tag_set *set, int nr_hw_queues);

void blk_mq_quiesce_queue_nowait(struct request_queue *q);

/*
 * Driver command data is immediately after the request. So subtract request
 * size to get back to the original request, add request size to get the PDU.
 */
static inline struct request *blk_mq_rq_from_pdu(void *pdu)
{
	return pdu - sizeof(struct request);
}
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

#endif
