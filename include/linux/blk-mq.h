#ifndef BLK_MQ_H
#define BLK_MQ_H

#include <linux/blkdev.h>

struct blk_mq_tags;
struct blk_flush_queue;

struct blk_mq_cpu_notifier {
	struct list_head list;
	void *data;
	int (*notify)(void *data, unsigned long action, unsigned int cpu);
};

struct blk_mq_ctxmap {
	unsigned int map_size;
	unsigned int bits_per_word;
	struct blk_align_bitmap *map;
};

struct blk_mq_hw_ctx {
	struct {
		spinlock_t		lock;
		struct list_head	dispatch;
	} ____cacheline_aligned_in_smp;

	unsigned long		state;		/* BLK_MQ_S_* flags */
	struct delayed_work	run_work;
	struct delayed_work	delay_work;
	cpumask_var_t		cpumask;
	int			next_cpu;
	int			next_cpu_batch;

	unsigned long		flags;		/* BLK_MQ_F_* flags */

	struct request_queue	*queue;
	unsigned int		queue_num;
	struct blk_flush_queue	*fq;

	void			*driver_data;

	struct blk_mq_ctxmap	ctx_map;

	unsigned int		nr_ctx;
	struct blk_mq_ctx	**ctxs;

	atomic_t		wait_index;

	struct blk_mq_tags	*tags;

	unsigned long		queued;
	unsigned long		run;
#define BLK_MQ_MAX_DISPATCH_ORDER	10
	unsigned long		dispatched[BLK_MQ_MAX_DISPATCH_ORDER];

	unsigned int		numa_node;
	unsigned int		cmd_size;	/* per-request extra data */

	atomic_t		nr_active;

	struct blk_mq_cpu_notifier	cpu_notifier;
	struct kobject		kobj;
};

struct blk_mq_tag_set {
	struct blk_mq_ops	*ops;
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

typedef int (queue_rq_fn)(struct blk_mq_hw_ctx *, struct request *, bool);
typedef struct blk_mq_hw_ctx *(map_queue_fn)(struct request_queue *, const int);
typedef enum blk_eh_timer_return (timeout_fn)(struct request *, bool);
typedef int (init_hctx_fn)(struct blk_mq_hw_ctx *, void *, unsigned int);
typedef void (exit_hctx_fn)(struct blk_mq_hw_ctx *, unsigned int);
typedef int (init_request_fn)(void *, struct request *, unsigned int,
		unsigned int, unsigned int);
typedef void (exit_request_fn)(void *, struct request *, unsigned int,
		unsigned int);

typedef void (busy_iter_fn)(struct blk_mq_hw_ctx *, struct request *, void *,
		bool);

struct blk_mq_ops {
	/*
	 * Queue request
	 */
	queue_rq_fn		*queue_rq;

	/*
	 * Map to specific hardware queue
	 */
	map_queue_fn		*map_queue;

	/*
	 * Called on request timeout
	 */
	timeout_fn		*timeout;

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
};

enum {
	BLK_MQ_RQ_QUEUE_OK	= 0,	/* queued fine */
	BLK_MQ_RQ_QUEUE_BUSY	= 1,	/* requeue IO for later */
	BLK_MQ_RQ_QUEUE_ERROR	= 2,	/* end IO with error */

	BLK_MQ_F_SHOULD_MERGE	= 1 << 0,
	BLK_MQ_F_TAG_SHARED	= 1 << 1,
	BLK_MQ_F_SG_MERGE	= 1 << 2,
	BLK_MQ_F_SYSFS_UP	= 1 << 3,

	BLK_MQ_S_STOPPED	= 0,
	BLK_MQ_S_TAG_ACTIVE	= 1,

	BLK_MQ_MAX_DEPTH	= 10240,

	BLK_MQ_CPU_WORK_BATCH	= 8,
};

struct request_queue *blk_mq_init_queue(struct blk_mq_tag_set *);
int blk_mq_register_disk(struct gendisk *);
void blk_mq_unregister_disk(struct gendisk *);

int blk_mq_alloc_tag_set(struct blk_mq_tag_set *set);
void blk_mq_free_tag_set(struct blk_mq_tag_set *set);

void blk_mq_flush_plug_list(struct blk_plug *plug, bool from_schedule);

void blk_mq_insert_request(struct request *, bool, bool, bool);
void blk_mq_run_queues(struct request_queue *q, bool async);
void blk_mq_free_request(struct request *rq);
bool blk_mq_can_queue(struct blk_mq_hw_ctx *);
struct request *blk_mq_alloc_request(struct request_queue *q, int rw,
		gfp_t gfp, bool reserved);
struct request *blk_mq_tag_to_rq(struct blk_mq_tags *tags, unsigned int tag);

struct blk_mq_hw_ctx *blk_mq_map_queue(struct request_queue *, const int ctx_index);
struct blk_mq_hw_ctx *blk_mq_alloc_single_hw_queue(struct blk_mq_tag_set *, unsigned int, int);

void blk_mq_start_request(struct request *rq);
void blk_mq_end_request(struct request *rq, int error);
void __blk_mq_end_request(struct request *rq, int error);

void blk_mq_requeue_request(struct request *rq);
void blk_mq_add_to_requeue_list(struct request *rq, bool at_head);
void blk_mq_kick_requeue_list(struct request_queue *q);
void blk_mq_complete_request(struct request *rq);

void blk_mq_stop_hw_queue(struct blk_mq_hw_ctx *hctx);
void blk_mq_start_hw_queue(struct blk_mq_hw_ctx *hctx);
void blk_mq_stop_hw_queues(struct request_queue *q);
void blk_mq_start_hw_queues(struct request_queue *q);
void blk_mq_start_stopped_hw_queues(struct request_queue *q, bool async);
void blk_mq_delay_queue(struct blk_mq_hw_ctx *hctx, unsigned long msecs);
void blk_mq_tag_busy_iter(struct blk_mq_hw_ctx *hctx, busy_iter_fn *fn,
		void *priv);

/*
 * Driver command data is immediately after the request. So subtract request
 * size to get back to the original request.
 */
static inline struct request *blk_mq_rq_from_pdu(void *pdu)
{
	return pdu - sizeof(struct request);
}
static inline void *blk_mq_rq_to_pdu(struct request *rq)
{
	return (void *) rq + sizeof(*rq);
}

#define queue_for_each_hw_ctx(q, hctx, i)				\
	for ((i) = 0; (i) < (q)->nr_hw_queues &&			\
	     ({ hctx = (q)->queue_hw_ctx[i]; 1; }); (i)++)

#define queue_for_each_ctx(q, ctx, i)					\
	for ((i) = 0; (i) < (q)->nr_queues &&				\
	     ({ ctx = per_cpu_ptr((q)->queue_ctx, (i)); 1; }); (i)++)

#define hctx_for_each_ctx(hctx, ctx, i)					\
	for ((i) = 0; (i) < (hctx)->nr_ctx &&				\
	     ({ ctx = (hctx)->ctxs[(i)]; 1; }); (i)++)

#define blk_ctx_sum(q, sum)						\
({									\
	struct blk_mq_ctx *__x;						\
	unsigned int __ret = 0, __i;					\
									\
	queue_for_each_ctx((q), __x, __i)				\
		__ret += sum;						\
	__ret;								\
})

#endif
