// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/io_uring.h>

#include <trace/events/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "refs.h"
#include "cancel.h"
#include "timeout.h"

struct io_timeout {
	struct file			*file;
	u32				off;
	u32				target_seq;
	u32				repeats;
	struct list_head		list;
	/* head of the link, used by linked timeouts only */
	struct io_kiocb			*head;
	/* for linked completions */
	struct io_kiocb			*prev;
};

struct io_timeout_rem {
	struct file			*file;
	u64				addr;

	/* timeout update */
	struct timespec64		ts;
	u32				flags;
	bool				ltimeout;
};

static struct io_kiocb *__io_disarm_linked_timeout(struct io_kiocb *req,
						   struct io_kiocb *link);

static inline bool io_is_timeout_noseq(struct io_kiocb *req)
{
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_timeout_data *data = req->async_data;

	return !timeout->off || data->flags & IORING_TIMEOUT_MULTISHOT;
}

static inline void io_put_req(struct io_kiocb *req)
{
	if (req_ref_put_and_test(req)) {
		io_queue_next(req);
		io_free_req(req);
	}
}

static inline bool io_timeout_finish(struct io_timeout *timeout,
				     struct io_timeout_data *data)
{
	if (!(data->flags & IORING_TIMEOUT_MULTISHOT))
		return true;

	if (!timeout->off || (timeout->repeats && --timeout->repeats))
		return false;

	return true;
}

static enum hrtimer_restart io_timeout_fn(struct hrtimer *timer);

static void io_timeout_complete(struct io_kiocb *req, io_tw_token_t tw)
{
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_timeout_data *data = req->async_data;
	struct io_ring_ctx *ctx = req->ctx;

	if (!io_timeout_finish(timeout, data)) {
		if (io_req_post_cqe(req, -ETIME, IORING_CQE_F_MORE)) {
			/* re-arm timer */
			raw_spin_lock_irq(&ctx->timeout_lock);
			list_add(&timeout->list, ctx->timeout_list.prev);
			hrtimer_start(&data->timer, timespec64_to_ktime(data->ts), data->mode);
			raw_spin_unlock_irq(&ctx->timeout_lock);
			return;
		}
	}

	io_req_task_complete(req, tw);
}

static __cold bool io_flush_killed_timeouts(struct list_head *list, int err)
{
	if (list_empty(list))
		return false;

	while (!list_empty(list)) {
		struct io_timeout *timeout;
		struct io_kiocb *req;

		timeout = list_first_entry(list, struct io_timeout, list);
		list_del_init(&timeout->list);
		req = cmd_to_io_kiocb(timeout);
		if (err)
			req_set_fail(req);
		io_req_queue_tw_complete(req, err);
	}

	return true;
}

static void io_kill_timeout(struct io_kiocb *req, struct list_head *list)
	__must_hold(&req->ctx->timeout_lock)
{
	struct io_timeout_data *io = req->async_data;

	if (hrtimer_try_to_cancel(&io->timer) != -1) {
		struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);

		atomic_set(&req->ctx->cq_timeouts,
			atomic_read(&req->ctx->cq_timeouts) + 1);
		list_move_tail(&timeout->list, list);
	}
}

__cold void io_flush_timeouts(struct io_ring_ctx *ctx)
{
	struct io_timeout *timeout, *tmp;
	LIST_HEAD(list);
	u32 seq;

	raw_spin_lock_irq(&ctx->timeout_lock);
	seq = ctx->cached_cq_tail - atomic_read(&ctx->cq_timeouts);

	list_for_each_entry_safe(timeout, tmp, &ctx->timeout_list, list) {
		struct io_kiocb *req = cmd_to_io_kiocb(timeout);
		u32 events_needed, events_got;

		if (io_is_timeout_noseq(req))
			break;

		/*
		 * Since seq can easily wrap around over time, subtract
		 * the last seq at which timeouts were flushed before comparing.
		 * Assuming not more than 2^31-1 events have happened since,
		 * these subtractions won't have wrapped, so we can check if
		 * target is in [last_seq, current_seq] by comparing the two.
		 */
		events_needed = timeout->target_seq - ctx->cq_last_tm_flush;
		events_got = seq - ctx->cq_last_tm_flush;
		if (events_got < events_needed)
			break;

		io_kill_timeout(req, &list);
	}
	ctx->cq_last_tm_flush = seq;
	raw_spin_unlock_irq(&ctx->timeout_lock);
	io_flush_killed_timeouts(&list, 0);
}

static void io_req_tw_fail_links(struct io_kiocb *link, io_tw_token_t tw)
{
	io_tw_lock(link->ctx, tw);
	while (link) {
		struct io_kiocb *nxt = link->link;
		long res = -ECANCELED;

		if (link->flags & REQ_F_FAIL)
			res = link->cqe.res;
		link->link = NULL;
		io_req_set_res(link, res, 0);
		io_req_task_complete(link, tw);
		link = nxt;
	}
}

static void io_fail_links(struct io_kiocb *req)
	__must_hold(&req->ctx->completion_lock)
{
	struct io_kiocb *link = req->link;
	bool ignore_cqes = req->flags & REQ_F_SKIP_LINK_CQES;

	if (!link)
		return;

	while (link) {
		if (ignore_cqes)
			link->flags |= REQ_F_CQE_SKIP;
		else
			link->flags &= ~REQ_F_CQE_SKIP;
		trace_io_uring_fail_link(req, link);
		link = link->link;
	}

	link = req->link;
	link->io_task_work.func = io_req_tw_fail_links;
	io_req_task_work_add(link);
	req->link = NULL;
}

static inline void io_remove_next_linked(struct io_kiocb *req)
{
	struct io_kiocb *nxt = req->link;

	req->link = nxt->link;
	nxt->link = NULL;
}

void io_disarm_next(struct io_kiocb *req)
	__must_hold(&req->ctx->completion_lock)
{
	struct io_kiocb *link = NULL;

	if (req->flags & REQ_F_ARM_LTIMEOUT) {
		link = req->link;
		req->flags &= ~REQ_F_ARM_LTIMEOUT;
		if (link && link->opcode == IORING_OP_LINK_TIMEOUT) {
			io_remove_next_linked(req);
			io_req_queue_tw_complete(link, -ECANCELED);
		}
	} else if (req->flags & REQ_F_LINK_TIMEOUT) {
		struct io_ring_ctx *ctx = req->ctx;

		raw_spin_lock_irq(&ctx->timeout_lock);
		if (req->link && req->link->opcode == IORING_OP_LINK_TIMEOUT)
			link = __io_disarm_linked_timeout(req, req->link);

		raw_spin_unlock_irq(&ctx->timeout_lock);
		if (link)
			io_req_queue_tw_complete(link, -ECANCELED);
	}
	if (unlikely((req->flags & REQ_F_FAIL) &&
		     !(req->flags & REQ_F_HARDLINK)))
		io_fail_links(req);
}

static struct io_kiocb *__io_disarm_linked_timeout(struct io_kiocb *req,
						   struct io_kiocb *link)
	__must_hold(&req->ctx->completion_lock)
	__must_hold(&req->ctx->timeout_lock)
{
	struct io_timeout_data *io = link->async_data;
	struct io_timeout *timeout = io_kiocb_to_cmd(link, struct io_timeout);

	io_remove_next_linked(req);
	timeout->head = NULL;
	if (hrtimer_try_to_cancel(&io->timer) != -1) {
		list_del(&timeout->list);
		return link;
	}

	return NULL;
}

static enum hrtimer_restart io_timeout_fn(struct hrtimer *timer)
{
	struct io_timeout_data *data = container_of(timer,
						struct io_timeout_data, timer);
	struct io_kiocb *req = data->req;
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_ring_ctx *ctx = req->ctx;
	unsigned long flags;

	raw_spin_lock_irqsave(&ctx->timeout_lock, flags);
	list_del_init(&timeout->list);
	atomic_set(&req->ctx->cq_timeouts,
		atomic_read(&req->ctx->cq_timeouts) + 1);
	raw_spin_unlock_irqrestore(&ctx->timeout_lock, flags);

	if (!(data->flags & IORING_TIMEOUT_ETIME_SUCCESS))
		req_set_fail(req);

	io_req_set_res(req, -ETIME, 0);
	req->io_task_work.func = io_timeout_complete;
	io_req_task_work_add(req);
	return HRTIMER_NORESTART;
}

static struct io_kiocb *io_timeout_extract(struct io_ring_ctx *ctx,
					   struct io_cancel_data *cd)
	__must_hold(&ctx->timeout_lock)
{
	struct io_timeout *timeout;
	struct io_timeout_data *io;
	struct io_kiocb *req = NULL;

	list_for_each_entry(timeout, &ctx->timeout_list, list) {
		struct io_kiocb *tmp = cmd_to_io_kiocb(timeout);

		if (io_cancel_req_match(tmp, cd)) {
			req = tmp;
			break;
		}
	}
	if (!req)
		return ERR_PTR(-ENOENT);

	io = req->async_data;
	if (hrtimer_try_to_cancel(&io->timer) == -1)
		return ERR_PTR(-EALREADY);
	timeout = io_kiocb_to_cmd(req, struct io_timeout);
	list_del_init(&timeout->list);
	return req;
}

int io_timeout_cancel(struct io_ring_ctx *ctx, struct io_cancel_data *cd)
	__must_hold(&ctx->completion_lock)
{
	struct io_kiocb *req;

	raw_spin_lock_irq(&ctx->timeout_lock);
	req = io_timeout_extract(ctx, cd);
	raw_spin_unlock_irq(&ctx->timeout_lock);

	if (IS_ERR(req))
		return PTR_ERR(req);
	io_req_task_queue_fail(req, -ECANCELED);
	return 0;
}

static void io_req_task_link_timeout(struct io_kiocb *req, io_tw_token_t tw)
{
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_kiocb *prev = timeout->prev;
	int ret;

	if (prev) {
		if (!io_should_terminate_tw(req->ctx)) {
			struct io_cancel_data cd = {
				.ctx		= req->ctx,
				.data		= prev->cqe.user_data,
			};

			ret = io_try_cancel(req->tctx, &cd, 0);
		} else {
			ret = -ECANCELED;
		}
		io_req_set_res(req, ret ?: -ETIME, 0);
		io_req_task_complete(req, tw);
		io_put_req(prev);
	} else {
		io_req_set_res(req, -ETIME, 0);
		io_req_task_complete(req, tw);
	}
}

static enum hrtimer_restart io_link_timeout_fn(struct hrtimer *timer)
{
	struct io_timeout_data *data = container_of(timer,
						struct io_timeout_data, timer);
	struct io_kiocb *prev, *req = data->req;
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_ring_ctx *ctx = req->ctx;
	unsigned long flags;

	raw_spin_lock_irqsave(&ctx->timeout_lock, flags);
	prev = timeout->head;
	timeout->head = NULL;

	/*
	 * We don't expect the list to be empty, that will only happen if we
	 * race with the completion of the linked work.
	 */
	if (prev) {
		io_remove_next_linked(prev);
		if (!req_ref_inc_not_zero(prev))
			prev = NULL;
	}
	list_del(&timeout->list);
	timeout->prev = prev;
	raw_spin_unlock_irqrestore(&ctx->timeout_lock, flags);

	req->io_task_work.func = io_req_task_link_timeout;
	io_req_task_work_add(req);
	return HRTIMER_NORESTART;
}

static clockid_t io_timeout_get_clock(struct io_timeout_data *data)
{
	switch (data->flags & IORING_TIMEOUT_CLOCK_MASK) {
	case IORING_TIMEOUT_BOOTTIME:
		return CLOCK_BOOTTIME;
	case IORING_TIMEOUT_REALTIME:
		return CLOCK_REALTIME;
	default:
		/* can't happen, vetted at prep time */
		WARN_ON_ONCE(1);
		fallthrough;
	case 0:
		return CLOCK_MONOTONIC;
	}
}

static int io_linked_timeout_update(struct io_ring_ctx *ctx, __u64 user_data,
				    struct timespec64 *ts, enum hrtimer_mode mode)
	__must_hold(&ctx->timeout_lock)
{
	struct io_timeout_data *io;
	struct io_timeout *timeout;
	struct io_kiocb *req = NULL;

	list_for_each_entry(timeout, &ctx->ltimeout_list, list) {
		struct io_kiocb *tmp = cmd_to_io_kiocb(timeout);

		if (user_data == tmp->cqe.user_data) {
			req = tmp;
			break;
		}
	}
	if (!req)
		return -ENOENT;

	io = req->async_data;
	if (hrtimer_try_to_cancel(&io->timer) == -1)
		return -EALREADY;
	hrtimer_setup(&io->timer, io_link_timeout_fn, io_timeout_get_clock(io), mode);
	hrtimer_start(&io->timer, timespec64_to_ktime(*ts), mode);
	return 0;
}

static int io_timeout_update(struct io_ring_ctx *ctx, __u64 user_data,
			     struct timespec64 *ts, enum hrtimer_mode mode)
	__must_hold(&ctx->timeout_lock)
{
	struct io_cancel_data cd = { .ctx = ctx, .data = user_data, };
	struct io_kiocb *req = io_timeout_extract(ctx, &cd);
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_timeout_data *data;

	if (IS_ERR(req))
		return PTR_ERR(req);

	timeout->off = 0; /* noseq */
	data = req->async_data;
	data->ts = *ts;

	list_add_tail(&timeout->list, &ctx->timeout_list);
	hrtimer_setup(&data->timer, io_timeout_fn, io_timeout_get_clock(data), mode);
	hrtimer_start(&data->timer, timespec64_to_ktime(data->ts), mode);
	return 0;
}

int io_timeout_remove_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_timeout_rem *tr = io_kiocb_to_cmd(req, struct io_timeout_rem);

	if (unlikely(req->flags & (REQ_F_FIXED_FILE | REQ_F_BUFFER_SELECT)))
		return -EINVAL;
	if (sqe->buf_index || sqe->len || sqe->splice_fd_in)
		return -EINVAL;

	tr->ltimeout = false;
	tr->addr = READ_ONCE(sqe->addr);
	tr->flags = READ_ONCE(sqe->timeout_flags);
	if (tr->flags & IORING_TIMEOUT_UPDATE_MASK) {
		if (hweight32(tr->flags & IORING_TIMEOUT_CLOCK_MASK) > 1)
			return -EINVAL;
		if (tr->flags & IORING_LINK_TIMEOUT_UPDATE)
			tr->ltimeout = true;
		if (tr->flags & ~(IORING_TIMEOUT_UPDATE_MASK|IORING_TIMEOUT_ABS))
			return -EINVAL;
		if (get_timespec64(&tr->ts, u64_to_user_ptr(sqe->addr2)))
			return -EFAULT;
		if (tr->ts.tv_sec < 0 || tr->ts.tv_nsec < 0)
			return -EINVAL;
	} else if (tr->flags) {
		/* timeout removal doesn't support flags */
		return -EINVAL;
	}

	return 0;
}

static inline enum hrtimer_mode io_translate_timeout_mode(unsigned int flags)
{
	return (flags & IORING_TIMEOUT_ABS) ? HRTIMER_MODE_ABS
					    : HRTIMER_MODE_REL;
}

/*
 * Remove or update an existing timeout command
 */
int io_timeout_remove(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_timeout_rem *tr = io_kiocb_to_cmd(req, struct io_timeout_rem);
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	if (!(tr->flags & IORING_TIMEOUT_UPDATE)) {
		struct io_cancel_data cd = { .ctx = ctx, .data = tr->addr, };

		spin_lock(&ctx->completion_lock);
		ret = io_timeout_cancel(ctx, &cd);
		spin_unlock(&ctx->completion_lock);
	} else {
		enum hrtimer_mode mode = io_translate_timeout_mode(tr->flags);

		raw_spin_lock_irq(&ctx->timeout_lock);
		if (tr->ltimeout)
			ret = io_linked_timeout_update(ctx, tr->addr, &tr->ts, mode);
		else
			ret = io_timeout_update(ctx, tr->addr, &tr->ts, mode);
		raw_spin_unlock_irq(&ctx->timeout_lock);
	}

	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

static int __io_timeout_prep(struct io_kiocb *req,
			     const struct io_uring_sqe *sqe,
			     bool is_timeout_link)
{
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_timeout_data *data;
	unsigned flags;
	u32 off = READ_ONCE(sqe->off);

	if (sqe->buf_index || sqe->len != 1 || sqe->splice_fd_in)
		return -EINVAL;
	if (off && is_timeout_link)
		return -EINVAL;
	flags = READ_ONCE(sqe->timeout_flags);
	if (flags & ~(IORING_TIMEOUT_ABS | IORING_TIMEOUT_CLOCK_MASK |
		      IORING_TIMEOUT_ETIME_SUCCESS |
		      IORING_TIMEOUT_MULTISHOT))
		return -EINVAL;
	/* more than one clock specified is invalid, obviously */
	if (hweight32(flags & IORING_TIMEOUT_CLOCK_MASK) > 1)
		return -EINVAL;
	/* multishot requests only make sense with rel values */
	if (!(~flags & (IORING_TIMEOUT_MULTISHOT | IORING_TIMEOUT_ABS)))
		return -EINVAL;

	INIT_LIST_HEAD(&timeout->list);
	timeout->off = off;
	if (unlikely(off && !req->ctx->off_timeout_used))
		req->ctx->off_timeout_used = true;
	/*
	 * for multishot reqs w/ fixed nr of repeats, repeats tracks the
	 * remaining nr
	 */
	timeout->repeats = 0;
	if ((flags & IORING_TIMEOUT_MULTISHOT) && off > 0)
		timeout->repeats = off;

	if (WARN_ON_ONCE(req_has_async_data(req)))
		return -EFAULT;
	data = io_uring_alloc_async_data(NULL, req);
	if (!data)
		return -ENOMEM;
	data->req = req;
	data->flags = flags;

	if (get_timespec64(&data->ts, u64_to_user_ptr(sqe->addr)))
		return -EFAULT;

	if (data->ts.tv_sec < 0 || data->ts.tv_nsec < 0)
		return -EINVAL;

	data->mode = io_translate_timeout_mode(flags);

	if (is_timeout_link) {
		struct io_submit_link *link = &req->ctx->submit_state.link;

		if (!link->head)
			return -EINVAL;
		if (link->last->opcode == IORING_OP_LINK_TIMEOUT)
			return -EINVAL;
		timeout->head = link->last;
		link->last->flags |= REQ_F_ARM_LTIMEOUT;
		hrtimer_setup(&data->timer, io_link_timeout_fn, io_timeout_get_clock(data),
			      data->mode);
	} else {
		hrtimer_setup(&data->timer, io_timeout_fn, io_timeout_get_clock(data), data->mode);
	}
	return 0;
}

int io_timeout_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return __io_timeout_prep(req, sqe, false);
}

int io_link_timeout_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return __io_timeout_prep(req, sqe, true);
}

int io_timeout(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_timeout_data *data = req->async_data;
	struct list_head *entry;
	u32 tail, off = timeout->off;

	raw_spin_lock_irq(&ctx->timeout_lock);

	/*
	 * sqe->off holds how many events that need to occur for this
	 * timeout event to be satisfied. If it isn't set, then this is
	 * a pure timeout request, sequence isn't used.
	 */
	if (io_is_timeout_noseq(req)) {
		entry = ctx->timeout_list.prev;
		goto add;
	}

	tail = data_race(ctx->cached_cq_tail) - atomic_read(&ctx->cq_timeouts);
	timeout->target_seq = tail + off;

	/* Update the last seq here in case io_flush_timeouts() hasn't.
	 * This is safe because ->completion_lock is held, and submissions
	 * and completions are never mixed in the same ->completion_lock section.
	 */
	ctx->cq_last_tm_flush = tail;

	/*
	 * Insertion sort, ensuring the first entry in the list is always
	 * the one we need first.
	 */
	list_for_each_prev(entry, &ctx->timeout_list) {
		struct io_timeout *nextt = list_entry(entry, struct io_timeout, list);
		struct io_kiocb *nxt = cmd_to_io_kiocb(nextt);

		if (io_is_timeout_noseq(nxt))
			continue;
		/* nxt.seq is behind @tail, otherwise would've been completed */
		if (off >= nextt->target_seq - tail)
			break;
	}
add:
	list_add(&timeout->list, entry);
	hrtimer_start(&data->timer, timespec64_to_ktime(data->ts), data->mode);
	raw_spin_unlock_irq(&ctx->timeout_lock);
	return IOU_ISSUE_SKIP_COMPLETE;
}

void io_queue_linked_timeout(struct io_kiocb *req)
{
	struct io_timeout *timeout = io_kiocb_to_cmd(req, struct io_timeout);
	struct io_ring_ctx *ctx = req->ctx;

	raw_spin_lock_irq(&ctx->timeout_lock);
	/*
	 * If the back reference is NULL, then our linked request finished
	 * before we got a chance to setup the timer
	 */
	if (timeout->head) {
		struct io_timeout_data *data = req->async_data;

		hrtimer_start(&data->timer, timespec64_to_ktime(data->ts),
				data->mode);
		list_add_tail(&timeout->list, &ctx->ltimeout_list);
	}
	raw_spin_unlock_irq(&ctx->timeout_lock);
	/* drop submission reference */
	io_put_req(req);
}

static bool io_match_task(struct io_kiocb *head, struct io_uring_task *tctx,
			  bool cancel_all)
	__must_hold(&head->ctx->timeout_lock)
{
	struct io_kiocb *req;

	if (tctx && head->tctx != tctx)
		return false;
	if (cancel_all)
		return true;

	io_for_each_link(req, head) {
		if (req->flags & REQ_F_INFLIGHT)
			return true;
	}
	return false;
}

/* Returns true if we found and killed one or more timeouts */
__cold bool io_kill_timeouts(struct io_ring_ctx *ctx, struct io_uring_task *tctx,
			     bool cancel_all)
{
	struct io_timeout *timeout, *tmp;
	LIST_HEAD(list);

	/*
	 * completion_lock is needed for io_match_task(). Take it before
	 * timeout_lockfirst to keep locking ordering.
	 */
	spin_lock(&ctx->completion_lock);
	raw_spin_lock_irq(&ctx->timeout_lock);
	list_for_each_entry_safe(timeout, tmp, &ctx->timeout_list, list) {
		struct io_kiocb *req = cmd_to_io_kiocb(timeout);

		if (io_match_task(req, tctx, cancel_all))
			io_kill_timeout(req, &list);
	}
	raw_spin_unlock_irq(&ctx->timeout_lock);
	spin_unlock(&ctx->completion_lock);

	return io_flush_killed_timeouts(&list, -ECANCELED);
}
