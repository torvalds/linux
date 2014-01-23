#ifndef BLK_MQ_H
#define BLK_MQ_H

#include <linux/blkdev.h>

struct blk_mq_tags;

struct blk_mq_cpu_notifier {
	struct list_head list;
	void *data;
	void (*notify)(void *data, unsigned long action, unsigned int cpu);
};

struct blk_mq_hw_ctx {
	struct {
		spinlock_t		lock;
		struct list_head	dispatch;
	} ____cacheline_aligned_in_smp;

	unsigned long		state;		/* BLK_MQ_S_* flags */
	struct delayed_work	delayed_work;

	unsigned long		flags;		/* BLK_MQ_F_* flags */

	struct request_queue	*queue;
	unsigned int		queue_num;

	void			*driver_data;

	unsigned int		nr_ctx;
	struct blk_mq_ctx	**ctxs;
	unsigned int 		nr_ctx_map;
	unsigned long		*ctx_map;

	struct request		**rqs;
	struct list_head	page_list;
	struct blk_mq_tags	*tags;

	unsigned long		queued;
	unsigned long		run;
#define BLK_MQ_MAX_DISPATCH_ORDER	10
	unsigned long		dispatched[BLK_MQ_MAX_DISPATCH_ORDER];

	unsigned int		queue_depth;
	unsigned int		numa_node;
	unsigned int		cmd_size;	/* per-request extra data */

	struct blk_mq_cpu_notifier	cpu_notifier;
	struct kobject		kobj;
};

struct blk_mq_reg {
	struct blk_mq_ops	*ops;
	unsigned int		nr_hw_queues;
	unsigned int		queue_depth;
	unsigned int		reserved_tags;
	unsigned int		cmd_size;	/* per-request extra data */
	int			numa_node;
	unsigned int		timeout;
	unsigned int		flags;		/* BLK_MQ_F_* */
};

typedef int (queue_rq_fn)(struct blk_mq_hw_ctx *, struct request *);
typedef struct blk_mq_hw_ctx *(map_queue_fn)(struct request_queue *, const int);
typedef struct blk_mq_hw_ctx *(alloc_hctx_fn)(struct blk_mq_reg *,unsigned int);
typedef void (free_hctx_fn)(struct blk_mq_hw_ctx *, unsigned int);
typedef int (init_hctx_fn)(struct blk_mq_hw_ctx *, void *, unsigned int);
typedef void (exit_hctx_fn)(struct blk_mq_hw_ctx *, unsigned int);

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
	rq_timed_out_fn		*timeout;

	/*
	 * Override for hctx allocations (should probably go)
	 */
	alloc_hctx_fn		*alloc_hctx;
	free_hctx_fn		*free_hctx;

	/*
	 * Called when the block layer side of a hardware queue has been
	 * set up, allowing the driver to allocate/init matching structures.
	 * Ditto for exit/teardown.
	 */
	init_hctx_fn		*init_hctx;
	exit_hctx_fn		*exit_hctx;
};

enum {
	BLK_MQ_RQ_QUEUE_OK	= 0,	/* queued fine */
	BLK_MQ_RQ_QUEUE_BUSY	= 1,	/* requeue IO for later */
	BLK_MQ_RQ_QUEUE_ERROR	= 2,	/* end IO with error */

	BLK_MQ_F_SHOULD_MERGE	= 1 << 0,
	BLK_MQ_F_SHOULD_SORT	= 1 << 1,
	BLK_MQ_F_SHOULD_IPI	= 1 << 2,

	BLK_MQ_S_STOPPED	= 1 << 0,

	BLK_MQ_MAX_DEPTH	= 2048,
};

struct request_queue *blk_mq_init_queue(struct blk_mq_reg *, void *);
void blk_mq_free_queue(struct request_queue *);
int blk_mq_register_disk(struct gendisk *);
void blk_mq_unregister_disk(struct gendisk *);
void blk_mq_init_commands(struct request_queue *, void (*init)(void *data, struct blk_mq_hw_ctx *, struct request *, unsigned int), void *data);

void blk_mq_flush_plug_list(struct blk_plug *plug, bool from_schedule);

void blk_mq_insert_request(struct request_queue *, struct request *, bool);
void blk_mq_run_queues(struct request_queue *q, bool async);
void blk_mq_free_request(struct request *rq);
bool blk_mq_can_queue(struct blk_mq_hw_ctx *);
struct request *blk_mq_alloc_request(struct request_queue *q, int rw, gfp_t gfp, bool reserved);
struct request *blk_mq_alloc_reserved_request(struct request_queue *q, int rw, gfp_t gfp);
struct request *blk_mq_rq_from_tag(struct request_queue *q, unsigned int tag);

struct blk_mq_hw_ctx *blk_mq_map_queue(struct request_queue *, const int ctx_index);
struct blk_mq_hw_ctx *blk_mq_alloc_single_hw_queue(struct blk_mq_reg *, unsigned int);
void blk_mq_free_single_hw_queue(struct blk_mq_hw_ctx *, unsigned int);

void blk_mq_end_io(struct request *rq, int error);

void blk_mq_stop_hw_queue(struct blk_mq_hw_ctx *hctx);
void blk_mq_start_hw_queue(struct blk_mq_hw_ctx *hctx);
void blk_mq_stop_hw_queues(struct request_queue *q);
void blk_mq_start_stopped_hw_queues(struct request_queue *q);

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

static inline struct request *blk_mq_tag_to_rq(struct blk_mq_hw_ctx *hctx,
					       unsigned int tag)
{
	return hctx->rqs[tag];
}

#define queue_for_each_hw_ctx(q, hctx, i)				\
	for ((i) = 0, hctx = (q)->queue_hw_ctx[0];			\
	     (i) < (q)->nr_hw_queues; (i)++, hctx = (q)->queue_hw_ctx[i])

#define queue_for_each_ctx(q, ctx, i)					\
	for ((i) = 0, ctx = per_cpu_ptr((q)->queue_ctx, 0);		\
	     (i) < (q)->nr_queues; (i)++, ctx = per_cpu_ptr(q->queue_ctx, (i)))

#define hctx_for_each_ctx(hctx, ctx, i)					\
	for ((i) = 0, ctx = (hctx)->ctxs[0];				\
	     (i) < (hctx)->nr_ctx; (i)++, ctx = (hctx)->ctxs[(i)])

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
