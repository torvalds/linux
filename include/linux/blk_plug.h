/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BLK_PLUG_H
#define _LINUX_BLK_PLUG_H

#include <linux/sched.h>

struct blk_plug_cb;
typedef void (*blk_plug_cb_fn)(struct blk_plug_cb *cb, bool from_schedule);

struct rq_list {
	struct request *head;
	struct request *tail;
};

#ifdef CONFIG_BLOCK
/*
 * blk_plug permits building a queue of related requests by holding the I/O
 * fragments for a short period. This allows merging of sequential requests
 * into single larger request. As the requests are moved from a per-task list to
 * the device's request_queue in a batch, this results in improved scalability
 * as the lock contention for request_queue lock is reduced.
 *
 * It is ok not to disable preemption when adding the request to the plug list
 * or when attempting a merge. For details, please see schedule() where
 * blk_flush_plug() is called.
 */
struct blk_plug {
	struct rq_list mq_list; /* blk-mq requests */

	/* if ios_left is > 1, we can batch tag/rq allocations */
	struct rq_list cached_rqs;
	u64 cur_ktime;
	unsigned short nr_ios;

	unsigned short rq_count;

	bool multiple_queues;
	bool has_elevator;

	struct list_head cb_list; /* md requires an unplug callback */
};

void blk_start_plug(struct blk_plug *);
void blk_start_plug_nr_ios(struct blk_plug *, unsigned short);
void blk_finish_plug(struct blk_plug *);

void __blk_flush_plug(struct blk_plug *plug, bool from_schedule);
static inline void blk_flush_plug(struct blk_plug *plug, bool async)
{
	if (plug)
		__blk_flush_plug(plug, async);
}

static __always_inline void blk_plug_invalidate_ts(void)
{
	if (unlikely(current->flags & PF_BLOCK_TS)) {
		current->plug->cur_ktime = 0;
		current->flags &= ~PF_BLOCK_TS;
	}
}

struct blk_plug_cb {
	struct list_head list;
	blk_plug_cb_fn callback;
	void *data;
};

struct blk_plug_cb *blk_check_plugged(blk_plug_cb_fn unplug, void *data,
		int size);
#else /* CONFIG_BLOCK */
struct blk_plug {
};

static inline void blk_start_plug(struct blk_plug *plug)
{
}

static inline void blk_start_plug_nr_ios(struct blk_plug *plug,
					 unsigned short nr_ios)
{
}

static inline void blk_finish_plug(struct blk_plug *plug)
{
}

static inline void blk_flush_plug(struct blk_plug *plug, bool async)
{
}

static inline void blk_plug_invalidate_ts(void)
{
}
#endif /* CONFIG_BLOCK */
#endif /* _LINUX_BLK_PLUG_H */
