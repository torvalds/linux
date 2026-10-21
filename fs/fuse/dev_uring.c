// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE: Filesystem in Userspace
 * Copyright (c) 2023-2024 DataDirect Networks.
 */

#include "dev.h"
#include "args.h"
#include "dev_uring_i.h"
#include "fuse_trace.h"

#include <linux/bitmap.h>
#include <linux/fs.h>
#include <linux/io_uring/cmd.h>

static bool __read_mostly enable_uring;
module_param(enable_uring, bool, 0644);
MODULE_PARM_DESC(enable_uring,
		 "Enable userspace communication through io-uring");

#define FUSE_URING_IOV_SEGS 2 /* header and payload */
#define FUSE_URING_IOV_HEADERS 0
#define FUSE_URING_IOV_PAYLOAD 1

#define FUSE_URING_ADD_QUEUE_FLAGS	(FUSE_URING_ZERO_COPY)

bool fuse_uring_enabled(void)
{
	return enable_uring;
}

struct fuse_uring_pdu {
	struct fuse_ring_ent *ent;
};

struct fuse_zero_copy_bvs {
	unsigned int nr_bvs;
	struct bio_vec bvs[];
};

static const struct fuse_iqueue_ops fuse_io_uring_ops;

enum fuse_uring_header_type {
	/* struct fuse_in_header / struct fuse_out_header */
	FUSE_URING_HEADER_IN_OUT,
	/* per op code header */
	FUSE_URING_HEADER_OP,
	/* struct fuse_uring_ent_in_out header */
	FUSE_URING_HEADER_RING_ENT,
};

static inline bool bufpool_enabled(struct fuse_ring_queue *queue)
{
	return queue->payload_mode == FUSE_PAYLOAD_BUFPOOL;
}

static inline bool bufpool_registered(struct fuse_ring_queue *queue)
{
	return queue->bufpool && queue->bufpool->registered;
}

/*
 * For a registered bufpool, every sqe that drives a payload import (REGISTER,
 * COMMIT_AND_FETCH) must carry the registered buffer index of the pool.
 * This also must be called from the command's issue handler, where cmd->sqe is
 * still valid
 */
static inline bool fuse_uring_cmd_index_ok(struct io_uring_cmd *cmd,
					   struct fuse_ring_queue *queue)
{
	if (!bufpool_registered(queue))
		return true;

	return (cmd->flags & IORING_URING_CMD_FIXED) &&
	       READ_ONCE(cmd->sqe->buf_index) == queue->bufpool->registered_index;
}

static void uring_cmd_set_ring_ent(struct io_uring_cmd *cmd,
				   struct fuse_ring_ent *ring_ent)
{
	struct fuse_uring_pdu *pdu =
		io_uring_cmd_to_pdu(cmd, struct fuse_uring_pdu);

	pdu->ent = ring_ent;
}

static struct fuse_ring_ent *uring_cmd_to_ring_ent(struct io_uring_cmd *cmd)
{
	struct fuse_uring_pdu *pdu =
		io_uring_cmd_to_pdu(cmd, struct fuse_uring_pdu);

	return pdu->ent;
}

static void fuse_uring_flush_bg(struct fuse_ring_queue *queue)
{
	struct fuse_ring *ring = queue->ring;
	struct fuse_chan *fch = ring->chan;

	lockdep_assert_held(&queue->lock);
	lockdep_assert_held(&fch->bg_lock);

	/*
	 * Allow one bg request per queue, ignoring global fc limits.
	 * This prevents a single queue from consuming all resources and
	 * eliminates the need for remote queue wake-ups when global
	 * limits are met but this queue has no more waiting requests.
	 */
	while ((fch->active_background < fch->max_background ||
		!queue->active_background) &&
	       (!list_empty(&queue->fuse_req_bg_queue))) {
		struct fuse_req *req;

		req = list_first_entry(&queue->fuse_req_bg_queue,
				       struct fuse_req, list);
		fch->active_background++;
		queue->active_background++;

		list_move_tail(&req->list, &queue->fuse_req_queue);
	}
}

static bool can_zero_copy_req(struct fuse_ring_ent *ent, struct fuse_req *req)
{
	struct fuse_args *args = req->args;

	if (!ent->queue->zero_copy || !args->zero_copy)
		return false;

	if (args->opcode != FUSE_READ && args->opcode != FUSE_WRITE)
		return false;

	return args->in_pages || args->out_pages;
}

static void zero_copy_unregister(struct io_uring_cmd *cmd,
				 struct fuse_ring_ent *ent,
				 unsigned int issue_flags)
{
	if (ent->zero_copied) {
		int err = io_buffer_unregister(cmd, ent->zero_copy_index,
					       issue_flags);

		if (err)
			pr_warn_ratelimited("qid=%d zero-copy unregister failed: %d\n",
					    ent->queue->qid, err);
		ent->zero_copied = false;
	}
}

static void fuse_uring_req_end(struct fuse_ring_ent *ent, struct fuse_req *req,
			       int error, unsigned int issue_flags)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_ring *ring = queue->ring;
	struct fuse_chan *fch = ring->chan;

	lockdep_assert_not_held(&queue->lock);
	spin_lock(&queue->lock);
	ent->fuse_req = NULL;
	list_del_init(&req->list);
	if (test_bit(FR_BACKGROUND, &req->flags)) {
		queue->active_background--;
		spin_lock(&fch->bg_lock);
		fuse_request_bg_finish(fch, req);
		fuse_uring_flush_bg(queue);
		spin_unlock(&fch->bg_lock);
	}

	spin_unlock(&queue->lock);

	zero_copy_unregister(ent->cmd, ent, issue_flags);

	if (error)
		req->out.h.error = error;

	clear_bit(FR_SENT, &req->flags);
	fuse_request_end(req);
}

/* Abort all list queued request on the given ring queue */
static void fuse_uring_abort_end_queue_requests(struct fuse_ring_queue *queue)
{
	struct fuse_req *req;
	LIST_HEAD(req_list);

	spin_lock(&queue->lock);
	list_for_each_entry(req, &queue->fuse_req_queue, list)
		clear_bit(FR_PENDING, &req->flags);
	list_splice_init(&queue->fuse_req_queue, &req_list);
	spin_unlock(&queue->lock);

	/* must not hold queue lock to avoid order issues with fi->lock */
	fuse_dev_end_requests(&req_list);
}

void fuse_uring_abort_end_requests(struct fuse_ring *ring)
{
	int qid;
	struct fuse_ring_queue *queue;
	struct fuse_chan *fch = ring->chan;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		queue = READ_ONCE(ring->queues[qid]);
		if (!queue)
			continue;

		WARN_ON_ONCE(fch->max_background != UINT_MAX);
		spin_lock(&queue->lock);
		queue->stopped = true;
		spin_lock(&fch->bg_lock);
		fuse_uring_flush_bg(queue);
		spin_unlock(&fch->bg_lock);
		spin_unlock(&queue->lock);
		fuse_uring_abort_end_queue_requests(queue);
	}
}

static bool ent_list_request_expired(struct fuse_chan *fch, struct list_head *list)
{
	struct fuse_ring_ent *ent;
	struct fuse_req *req;

	ent = list_first_entry_or_null(list, struct fuse_ring_ent, list);
	if (!ent)
		return false;

	req = ent->fuse_req;

	return time_is_before_jiffies(req->create_time +
				      fch->timeout.req_timeout);
}

bool fuse_uring_request_expired(struct fuse_chan *fch)
{
	struct fuse_ring *ring = fch->ring;
	struct fuse_ring_queue *queue;
	int qid;

	if (!ring)
		return false;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		queue = READ_ONCE(ring->queues[qid]);
		if (!queue)
			continue;

		spin_lock(&queue->lock);
		if (fuse_request_expired(fch, &queue->fuse_req_queue) ||
		    fuse_request_expired(fch, &queue->fuse_req_bg_queue) ||
		    ent_list_request_expired(fch, &queue->ent_w_req_queue) ||
		    ent_list_request_expired(fch, &queue->ent_in_userspace)) {
			spin_unlock(&queue->lock);
			return true;
		}
		spin_unlock(&queue->lock);
	}

	return false;
}

void fuse_uring_destruct(struct fuse_chan *fch)
{
	struct fuse_ring *ring = fch->ring;
	int qid;

	if (!ring)
		return;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		struct fuse_ring_queue *queue = READ_ONCE(ring->queues[qid]);
		struct fuse_ring_ent *ent, *next;

		if (!queue)
			continue;

		WARN_ON(!list_empty(&queue->ent_avail_queue));
		WARN_ON(!list_empty(&queue->ent_w_req_queue));
		WARN_ON(!list_empty(&queue->ent_commit_queue));
		WARN_ON(!list_empty(&queue->ent_in_userspace));

		list_for_each_entry_safe(ent, next, &queue->ent_released,
					 list) {
			list_del_init(&ent->list);
			kfree(ent);
		}

		kfree(queue->fpq.processing);
		kfree(queue->bufpool);
		kfree(queue);
		WRITE_ONCE(ring->queues[qid], NULL);
	}

	kfree(ring->queues);
	kfree(ring);
	fch->ring = NULL;
}

/*
 * Basic ring setup for this connection based on the provided configuration
 */
static struct fuse_ring *fuse_uring_create(struct fuse_chan *fch)
{
	struct fuse_ring *ring;
	size_t nr_queues = num_possible_cpus();
	size_t max_payload_size;

	ring = kzalloc_obj(*ring, GFP_KERNEL_ACCOUNT);
	if (!ring)
		return NULL;

	ring->queues = kzalloc_objs(struct fuse_ring_queue *, nr_queues,
				    GFP_KERNEL_ACCOUNT);
	if (!ring->queues)
		goto out_err;

	max_payload_size = max(FUSE_MIN_READ_BUFFER, fch->max_write);
	max_payload_size = max(max_payload_size, fch->max_pages * PAGE_SIZE);

	spin_lock(&fch->lock);
	if (!fch->connected) {
		spin_unlock(&fch->lock);
		goto out_err;
	}

	init_waitqueue_head(&ring->stop_waitq);

	ring->nr_queues = nr_queues;
	ring->chan = fch;
	ring->max_payload_sz = max_payload_size;
	smp_store_release(&fch->ring, ring);

	spin_unlock(&fch->lock);
	return ring;

out_err:
	kfree(ring->queues);
	kfree(ring);
	return NULL;
}

void fuse_uring_conn_init(struct fuse_chan *fch)
{
	if (fuse_uring_create(fch))
		fch->io_uring = 1;
}

static struct fuse_ring_queue *fuse_uring_create_queue(struct fuse_ring *ring,
						       int qid, bool zero_copy,
						       bool fail_if_exists)
{
	struct fuse_chan *fch = ring->chan;
	struct fuse_ring_queue *queue;
	struct list_head *pq;

	queue = kzalloc_obj(*queue, GFP_KERNEL_ACCOUNT);
	if (!queue)
		return ERR_PTR(-ENOMEM);
	pq = fuse_pqueue_alloc();
	if (!pq) {
		kfree(queue);
		return ERR_PTR(-ENOMEM);
	}

	queue->qid = qid;
	queue->ring = ring;
	spin_lock_init(&queue->lock);
	queue->zero_copy = zero_copy;

	INIT_LIST_HEAD(&queue->ent_avail_queue);
	INIT_LIST_HEAD(&queue->ent_commit_queue);
	INIT_LIST_HEAD(&queue->ent_w_req_queue);
	INIT_LIST_HEAD(&queue->ent_in_userspace);
	INIT_LIST_HEAD(&queue->fuse_req_queue);
	INIT_LIST_HEAD(&queue->fuse_req_bg_queue);
	INIT_LIST_HEAD(&queue->ent_released);

	fuse_pqueue_init(&queue->fpq);
	queue->fpq.processing = pq;

	spin_lock(&fch->lock);
	if (ring->queues[qid]) {
		spin_unlock(&fch->lock);
		kfree(queue->fpq.processing);
		kfree(queue->bufpool);
		kfree(queue);
		return fail_if_exists ? ERR_PTR(-EEXIST) : ring->queues[qid];
	}

	/*
	 * fch->lock serializes concurrent creators for this qid.
	 * smp_store_release() are for the lockless readers who must see a
	 * fully initialized queue after &ring->queues[qid] is set
	 */
	smp_store_release(&ring->queues[qid], queue);
	spin_unlock(&fch->lock);

	return queue;
}

static void fuse_uring_stop_fuse_req_end(struct fuse_req *req)
{
	clear_bit(FR_SENT, &req->flags);
	req->out.h.error = -ECONNABORTED;
	fuse_request_end(req);
}

/*
 * Release a request/entry on connection tear down
 */
static void fuse_uring_entry_teardown(struct fuse_ring_ent *ent)
{
	struct fuse_req *req;
	struct io_uring_cmd *cmd;

	struct fuse_ring_queue *queue = ent->queue;

	spin_lock(&queue->lock);
	cmd = ent->cmd;
	ent->cmd = NULL;
	req = ent->fuse_req;
	ent->fuse_req = NULL;
	if (req) {
		/* remove entry from queue->fpq->processing */
		list_del_init(&req->list);
	}

	/*
	 * The entry must not be freed immediately, due to access of direct
	 * pointer access of entries through IO_URING_F_CANCEL - there is a risk
	 * of race between daemon termination (which triggers IO_URING_F_CANCEL
	 * and accesses entries without checking the list state first
	 */
	list_move(&ent->list, &queue->ent_released);
	ent->state = FRRS_RELEASED;
	spin_unlock(&queue->lock);

	if (cmd)
		io_uring_cmd_done(cmd, -ENOTCONN, IO_URING_F_UNLOCKED);

	if (req)
		fuse_uring_stop_fuse_req_end(req);
}

static void fuse_uring_stop_list_entries(struct list_head *head,
					 struct fuse_ring_queue *queue,
					 enum fuse_ring_req_state exp_state)
{
	struct fuse_ring *ring = queue->ring;
	struct fuse_ring_ent *ent, *next;
	ssize_t queue_refs = SSIZE_MAX;
	LIST_HEAD(to_teardown);

	spin_lock(&queue->lock);
	list_for_each_entry_safe(ent, next, head, list) {
		if (ent->state != exp_state) {
			pr_warn("entry teardown qid=%d state=%d expected=%d",
				queue->qid, ent->state, exp_state);
			continue;
		}

		ent->state = FRRS_TEARDOWN;
		list_move(&ent->list, &to_teardown);
	}
	spin_unlock(&queue->lock);

	/* no queue lock to avoid lock order issues */
	list_for_each_entry_safe(ent, next, &to_teardown, list) {
		fuse_uring_entry_teardown(ent);
		queue_refs = atomic_dec_return(&ring->queue_refs);
		WARN_ON_ONCE(queue_refs < 0);
	}
}

static void fuse_uring_teardown_entries(struct fuse_ring_queue *queue)
{
	fuse_uring_stop_list_entries(&queue->ent_in_userspace, queue,
				     FRRS_USERSPACE);
	fuse_uring_stop_list_entries(&queue->ent_avail_queue, queue,
				     FRRS_AVAILABLE);
}

static void fuse_uring_teardown_all_queues(struct fuse_ring *ring)
{
	int qid;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		struct fuse_ring_queue *queue = READ_ONCE(ring->queues[qid]);

		if (!queue)
			continue;

		fuse_uring_teardown_entries(queue);
	}
}

/*
 * Log state debug info
 */
static void fuse_uring_log_ent_state(struct fuse_ring *ring)
{
	int qid;
	struct fuse_ring_ent *ent;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		struct fuse_ring_queue *queue = READ_ONCE(ring->queues[qid]);

		if (!queue)
			continue;

		spin_lock(&queue->lock);
		/*
		 * Log entries from the intermediate queue, the other queues
		 * should be empty
		 */
		list_for_each_entry(ent, &queue->ent_w_req_queue, list) {
			pr_info(" ent-req-queue ring=%p qid=%d ent=%p state=%d\n",
				ring, qid, ent, ent->state);
		}
		list_for_each_entry(ent, &queue->ent_commit_queue, list) {
			pr_info(" ent-commit-queue ring=%p qid=%d ent=%p state=%d\n",
				ring, qid, ent, ent->state);
		}
		spin_unlock(&queue->lock);
	}
	ring->stop_debug_log = 1;
}

static void fuse_uring_async_stop_queues(struct work_struct *work)
{
	struct fuse_ring *ring =
		container_of(work, struct fuse_ring, async_teardown_work.work);

	fuse_uring_teardown_all_queues(ring);

	/*
	 * Some ring entries might be in the middle of IO operations,
	 * i.e. in process to get handled by file_operations::uring_cmd
	 * or on the way to userspace - we could handle that with conditions in
	 * run time code, but easier/cleaner to have an async tear down handler
	 * If there are still queue references left
	 */
	if (atomic_read(&ring->queue_refs) > 0) {
		if (time_after(jiffies,
			       ring->teardown_time + FUSE_URING_TEARDOWN_TIMEOUT))
			fuse_uring_log_ent_state(ring);

		schedule_delayed_work(&ring->async_teardown_work,
				      FUSE_URING_TEARDOWN_INTERVAL);
	} else {
		wake_up_all(&ring->stop_waitq);
		fuse_conn_put(ring->chan->conn);
	}
}

/*
 * Stop the ring queues
 */
void fuse_uring_stop_queues(struct fuse_ring *ring)
{
	fuse_uring_teardown_all_queues(ring);

	if (atomic_read(&ring->queue_refs) > 0) {
		fuse_conn_get(ring->chan->conn);
		ring->teardown_time = jiffies;
		INIT_DELAYED_WORK(&ring->async_teardown_work,
				  fuse_uring_async_stop_queues);
		schedule_delayed_work(&ring->async_teardown_work,
				      FUSE_URING_TEARDOWN_INTERVAL);
	} else {
		wake_up_all(&ring->stop_waitq);
	}
}

/*
 * Handle IO_URING_F_CANCEL, typically should come on daemon termination.
 *
 * Releasing the last entry should trigger fuse_dev_release() if
 * the daemon was terminated
 */
static void fuse_uring_cancel(struct io_uring_cmd *cmd,
			      unsigned int issue_flags)
{
	struct fuse_ring_ent *ent = uring_cmd_to_ring_ent(cmd);
	struct fuse_ring_queue *queue;
	bool need_cmd_done = false;

	/*
	 * direct access on ent - it must not be destructed as long as
	 * IO_URING_F_CANCEL might come up
	 */
	queue = ent->queue;
	spin_lock(&queue->lock);
	if (ent->state == FRRS_AVAILABLE) {
		list_del_init(&ent->list);
		need_cmd_done = true;
		ent->cmd = NULL;
	}
	spin_unlock(&queue->lock);

	if (need_cmd_done) {
		/* no queue lock to avoid lock order issues */
		io_uring_cmd_done(cmd, -ENOTCONN, issue_flags);
		kfree(ent);
		if (atomic_dec_and_test(&queue->ring->queue_refs))
			wake_up_all(&queue->ring->stop_waitq);
	}
}

static void fuse_uring_prepare_cancel(struct io_uring_cmd *cmd, int issue_flags,
				      struct fuse_ring_ent *ring_ent)
{
	uring_cmd_set_ring_ent(cmd, ring_ent);
	io_uring_cmd_mark_cancelable(cmd, issue_flags);
}

/*
 * Checks for errors and stores it into the request
 */
static int fuse_uring_out_header_has_err(struct fuse_out_header *oh,
					 struct fuse_req *req)
{
	int err;

	err = -EINVAL;
	if (oh->unique == 0) {
		/* Not supported through io-uring yet */
		pr_warn_once("notify through fuse-io-uring not supported\n");
		goto err;
	}

	if (oh->error <= -ERESTARTSYS || oh->error > 0)
		goto err;

	if (oh->error) {
		err = oh->error;
		goto err;
	}

	err = -ENOENT;
	if ((oh->unique & ~FUSE_INT_REQ_BIT) != req->in.h.unique) {
		pr_warn_ratelimited("unique mismatch, expected: %llu got %llu\n",
				    req->in.h.unique,
				    oh->unique & ~FUSE_INT_REQ_BIT);
		goto err;
	}

	/*
	 * Is it an interrupt reply ID?
	 * XXX: Not supported through fuse-io-uring yet, it should not even
	 *      find the request - should not happen.
	 */
	WARN_ON_ONCE(oh->unique & FUSE_INT_REQ_BIT);

	err = 0;
err:
	return err;
}

static int ring_header_type_offset(enum fuse_uring_header_type type)
{
	switch (type) {
	case FUSE_URING_HEADER_IN_OUT:
		return 0;
	case FUSE_URING_HEADER_OP:
		return offsetof(struct fuse_uring_req_header, op_in);
	case FUSE_URING_HEADER_RING_ENT:
		return offsetof(struct fuse_uring_req_header, ring_ent_in_out);
	default:
		WARN_ONCE(1, "Invalid header type: %d\n", type);
		return -EINVAL;
	}
}

static int copy_header_to_ring(struct fuse_ring_ent *ent,
			       enum fuse_uring_header_type type,
			       const void *header, size_t header_size)
{
	int offset = ring_header_type_offset(type);
	void __user *ring;

	if (offset < 0)
		return offset;

	ring = (void __user *)ent->headers + offset;

	if (copy_to_user(ring, header, header_size)) {
		pr_info_ratelimited("Copying header to ring failed.\n");
		return -EFAULT;
	}

	return 0;
}

static int copy_header_from_ring(struct fuse_ring_ent *ent,
				 enum fuse_uring_header_type type, void *header,
				 size_t header_size)
{
	int offset = ring_header_type_offset(type);
	const void __user *ring;

	if (offset < 0)
		return offset;

	ring = (void __user *)ent->headers + offset;

	if (copy_from_user(header, ring, header_size)) {
		pr_info_ratelimited("Copying header from ring failed.\n");
		return -EFAULT;
	}

	return 0;
}

static int fuse_uring_import_payload(struct fuse_ring_ent *ent, int dir,
				     struct iov_iter *iter,
				     unsigned int issue_flags)
{
	void __user *base = ent->payload.iov_base;
	size_t len = ent->payload.iov_len;
	int err = 0;

	if (!base) {
		memset(iter, 0, sizeof(*iter));
		return 0;
	}

	if (bufpool_registered(ent->queue))
		err = io_uring_cmd_import_fixed((u64)(uintptr_t)base, len, dir,
						iter, ent->cmd, issue_flags);
	else
		err = import_ubuf(dir, base, len, iter);

	if (err)
		pr_info_ratelimited("fuse: Import of user buffer failed\n");

	return err;
}

static int setup_fuse_copy_state(struct fuse_copy_state *cs,
				 struct fuse_req *req,
				 struct fuse_ring_ent *ent, int dir,
				 struct iov_iter *iter,
				 unsigned int issue_flags)
{
	int err;

	err = fuse_uring_import_payload(ent, dir, iter, issue_flags);
	if (err)
		return err;

	fuse_copy_init(cs, dir == ITER_DEST, iter);

	if (ent->zero_copied)
		cs->skip_folio_copy = true;

	cs->is_uring = true;
	cs->req = req;

	return 0;
}

static int fuse_uring_copy_from_ring(struct fuse_req *req,
				     struct fuse_ring_ent *ent,
				     unsigned int issue_flags)
{
	struct fuse_copy_state cs;
	struct fuse_args *args = req->args;
	struct iov_iter iter;
	int err;
	struct fuse_uring_ent_in_out ring_in_out;

	err = copy_header_from_ring(ent, FUSE_URING_HEADER_RING_ENT,
				    &ring_in_out, sizeof(ring_in_out));
	if (err)
		return err;

	err = setup_fuse_copy_state(&cs, req, ent, ITER_SOURCE, &iter,
				    issue_flags);
	if (err)
		return err;

	err = fuse_copy_out_args(&cs, args, ring_in_out.payload_sz);
	fuse_copy_finish(&cs);
	return err;
}

static void fuse_zero_copy_release(void *priv)
{
	struct fuse_zero_copy_bvs *zc_bvs = priv;
	unsigned int i;

	for (i = 0; i < zc_bvs->nr_bvs; i++)
		folio_put(page_folio(zc_bvs->bvs[i].bv_page));

	kvfree(zc_bvs);
}

static int fuse_uring_set_up_zero_copy(struct fuse_ring_ent *ent,
				       struct fuse_req *req,
				       unsigned int issue_flags)
{
	struct fuse_args_pages *ap;
	int err, i, ddir = 0;
	struct fuse_zero_copy_bvs *zc_bvs;
	struct bio_vec *bvs;

	/* out_pages indicates a read, in_pages indicates a write */
	if (req->args->out_pages)
		ddir |= IO_BUF_DEST;
	if (req->args->in_pages)
		ddir |= IO_BUF_SOURCE;

	ap = container_of(req->args, typeof(*ap), args);

	zc_bvs = kvmalloc_flex(*zc_bvs, bvs, ap->num_folios,
			       GFP_KERNEL_ACCOUNT);
	if (!zc_bvs)
		return -ENOMEM;

	zc_bvs->nr_bvs = ap->num_folios;
	bvs = zc_bvs->bvs;
	for (i = 0; i < ap->num_folios; i++) {
		bvs[i].bv_page = folio_page(ap->folios[i], 0);
		bvs[i].bv_offset = ap->descs[i].offset;
		bvs[i].bv_len = ap->descs[i].length;
		folio_get(ap->folios[i]);
	}

	err = io_buffer_register_bvec(ent->cmd, bvs, ap->num_folios,
				      fuse_zero_copy_release, zc_bvs,
				      ddir, ent->zero_copy_index,
				      issue_flags);
	if (err) {
		fuse_zero_copy_release(zc_bvs);
		return err;
	}

	ent->zero_copied = true;

	return 0;
}

/*
 * Copy data from the req to the ring buffer
 */
static int fuse_uring_args_to_ring(struct fuse_req *req,
				   struct fuse_ring_ent *ent,
				   unsigned int issue_flags)
{
	struct fuse_copy_state cs;
	struct fuse_args *args = req->args;
	struct fuse_in_arg *in_args = args->in_args;
	int num_args = args->in_numargs;
	int err;
	struct iov_iter iter;
	struct fuse_uring_ent_in_out ent_in_out = {
		.flags = 0,
		.commit_id = req->in.h.unique,
	};

	if (can_zero_copy_req(ent, req)) {
		ent_in_out.flags |= FUSE_URING_ENT_ZERO_COPY;
		err = fuse_uring_set_up_zero_copy(ent, req, issue_flags);
		if (err)
			return err;
	}

	err = setup_fuse_copy_state(&cs, req, ent, ITER_DEST, &iter,
				    issue_flags);
	if (err)
		return err;

	if (num_args > 0) {
		/*
		 * Expectation is that the first argument is the per op header.
		 * Some op code have that as zero size.
		 */
		if (args->in_args[0].size > 0) {
			err = copy_header_to_ring(ent, FUSE_URING_HEADER_OP,
						  in_args->value,
						  in_args->size);
			if (err)
				return err;
		}
		in_args++;
		num_args--;
	}

	/* copy the payload */
	err = fuse_copy_args(&cs, num_args, args->in_pages,
			     (struct fuse_arg *)in_args, 0);
	fuse_copy_finish(&cs);
	if (err) {
		pr_info_ratelimited("%s fuse_copy_args failed\n", __func__);
		return err;
	}

	ent_in_out.payload_sz = cs.ring.copied_sz;
	/*
	 * on a zero-copied write the pages are registered for the server to
	 * read via a fixed-buffer op rather than copied into the payload
	 * buffer, so copied_sz does not account for it. The server still needs
	 * the total inbound size to know how many bytes to read from the
	 * registered buffer, so add the page arg (always the last in-arg) back
	 * in
	 */
	if (cs.skip_folio_copy && args->in_pages)
		ent_in_out.payload_sz +=
			args->in_args[args->in_numargs - 1].size;

	if (bufpool_enabled(ent->queue) && ent->payload.iov_base)
		ent_in_out.offset =
			(uintptr_t)ent->payload.iov_base - ent->queue->bufpool->base_uaddr;

	return copy_header_to_ring(ent, FUSE_URING_HEADER_RING_ENT,
				   &ent_in_out, sizeof(ent_in_out));
}

static int fuse_uring_copy_to_ring(struct fuse_ring_ent *ent,
				   struct fuse_req *req,
				   unsigned int issue_flags)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_in_header in_header;
	int err;

	err = -EIO;
	if (WARN_ON(ent->state != FRRS_FUSE_REQ)) {
		pr_err("qid=%d ring-req=%p invalid state %d on send\n",
		       queue->qid, ent, ent->state);
		return err;
	}

	err = -EINVAL;
	if (WARN_ON(req->in.h.unique == 0))
		return err;

	/* copy the request */
	err = fuse_uring_args_to_ring(req, ent, issue_flags);
	if (unlikely(err)) {
		pr_info_ratelimited("Copy to ring failed: %d\n", err);
		return err;
	}

	/* copy fuse_in_header */
	in_header = req->in.h;
	return copy_header_to_ring(ent, FUSE_URING_HEADER_IN_OUT, &in_header,
				   sizeof(in_header));
}

static bool fuse_uring_req_has_copyable_payload(struct fuse_ring_ent *ent,
						struct fuse_req *req)
{
	struct fuse_args *args = req->args;

	if (!can_zero_copy_req(ent, req))
		return args->in_numargs > 1 || args->out_numargs;

	/*
	 * the asymmetry between in_numargs > 2 and out_numargs > 1 is because
	 * the per-op header is extracted before fuse_copy_args() for inargs but
	 * not for outargs
	 */
	if ((args->in_numargs > 1) && (!args->in_pages || args->in_numargs > 2))
		return true;
	if (args->out_numargs && (!args->out_pages || args->out_numargs > 1))
		return true;

	return false;
}

static int fuse_uring_select_buffer(struct fuse_ring_ent *ent)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_bufpool *pool = queue->bufpool;
	unsigned int id;

	lockdep_assert_held(&queue->lock);

	id = find_first_bit(pool->free_map, pool->nr_bufs);
	if (id >= pool->nr_bufs)
		return -ENOBUFS;

	WARN_ON_ONCE(ent->payload.iov_base);
	__clear_bit(id, pool->free_map);

	ent->buf_id = id;
	ent->payload.iov_base =
		(void __user *)(pool->base_uaddr + id * pool->buf_size);
	ent->payload.iov_len = pool->buf_size;

	return 0;
}

static void fuse_uring_recycle_buffer(struct fuse_ring_ent *ent)
{
	struct iovec *ent_payload = &ent->payload;
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_bufpool *pool;

	lockdep_assert_held(&queue->lock);

	if (!bufpool_enabled(queue) || !ent_payload->iov_base)
		return;

	pool = queue->bufpool;

	/* a buffer should never be recycled twice */
	WARN_ON_ONCE(test_bit(ent->buf_id, pool->free_map));
	__set_bit(ent->buf_id, pool->free_map);

	memset(ent_payload, 0, sizeof(*ent_payload));
	ent->buf_id = 0;
}

static int fuse_uring_next_req_update_buffer(struct fuse_ring_ent *ent,
					     struct fuse_req *req)
{
	bool buffer_selected;
	bool has_payload;

	if (!bufpool_enabled(ent->queue))
		return 0;

	buffer_selected = !!ent->payload.iov_base;
	has_payload = fuse_uring_req_has_copyable_payload(ent, req);

	if (has_payload && !buffer_selected)
		return fuse_uring_select_buffer(ent);

	if (!has_payload && buffer_selected)
		fuse_uring_recycle_buffer(ent);

	return 0;
}

static int fuse_uring_prep_buffer(struct fuse_ring_ent *ent,
				  struct fuse_req *req)
{
	if (!bufpool_enabled(ent->queue))
		return 0;

	/* no payload to copy, can skip selecting a buffer */
	if (!fuse_uring_req_has_copyable_payload(ent, req))
		return 0;

	return fuse_uring_select_buffer(ent);
}

static int fuse_uring_prepare_send(struct fuse_ring_ent *ent,
				   struct fuse_req *req,
				   unsigned int issue_flags)
{
	int err;

	err = fuse_uring_copy_to_ring(ent, req, issue_flags);
	if (!err) {
		set_bit(FR_SENT, &req->flags);
		trace_fuse_request_sent(req);
	} else {
		/*
		 * Copying the request failed. Remove the entry from the
		 * ent_w_req_queue list and terminate the request
		 */
		spin_lock(&ent->queue->lock);
		list_del_init(&ent->list);
		ent->state = FRRS_INVALID;
		spin_unlock(&ent->queue->lock);

		fuse_uring_req_end(ent, req, err, issue_flags);
	}

	return err;
}

/* Used to find the request on SQE commit */
static void fuse_uring_add_to_pq(struct fuse_ring_ent *ent)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_pqueue *fpq = &queue->fpq;
	unsigned int hash;
	struct fuse_req *req = ent->fuse_req;

	req->ring_entry = ent;
	hash = fuse_req_hash(req->in.h.unique);
	list_move_tail(&req->list, &fpq->processing[hash]);
}

/*
 * Make a ring entry available for fuse_req assignment
 */
static void fuse_uring_ent_avail(struct fuse_ring_ent *ent,
				 struct fuse_ring_queue *queue)
{
	WARN_ON_ONCE(!ent->cmd);
	list_move(&ent->list, &queue->ent_avail_queue);
	ent->state = FRRS_AVAILABLE;
}

/*
 * Assign a fuse queue entry to the given entry
 */
static void fuse_uring_add_req_to_ring_ent(struct fuse_ring_ent *ent,
					   struct fuse_req *req)
{
	struct fuse_ring_queue *queue = ent->queue;

	lockdep_assert_held(&queue->lock);

	if (WARN_ON_ONCE(ent->state != FRRS_AVAILABLE &&
			 ent->state != FRRS_COMMIT)) {
		pr_warn("%s qid=%d state=%d\n", __func__, ent->queue->qid,
			ent->state);
	}

	clear_bit(FR_PENDING, &req->flags);

	/* Until fuse_uring_add_to_pq() the req is not attached to any list */
	list_del_init(&req->list);

	ent->fuse_req = req;
	ent->state = FRRS_FUSE_REQ;
	list_move_tail(&ent->list, &queue->ent_w_req_queue);
}

/* Fetch the next fuse request if available */
static struct fuse_req *fuse_uring_ent_assign_req(struct fuse_ring_ent *ent)
	__must_hold(&queue->lock)
{
	struct fuse_req *req;
	struct fuse_ring_queue *queue = ent->queue;
	struct list_head *req_queue = &queue->fuse_req_queue;

	lockdep_assert_held(&queue->lock);

	/* get and assign the next entry while it is still holding the lock */
	req = list_first_entry_or_null(req_queue, struct fuse_req, list);
	if (!req || fuse_uring_next_req_update_buffer(ent, req)) {
		fuse_uring_recycle_buffer(ent);
		return NULL;
	}

	fuse_uring_add_req_to_ring_ent(ent, req);
	return req;
}

/*
 * Read data from the ring buffer, which user space has written to
 * This is comparible with handling of classical write(/dev/fuse).
 * Also make the ring request available again for new fuse requests.
 */
static void fuse_uring_commit(struct fuse_ring_ent *ent, struct fuse_req *req,
			      unsigned int issue_flags)
{
	struct fuse_out_header out_header;
	ssize_t err = -EFAULT;

	if (copy_header_from_ring(ent, FUSE_URING_HEADER_IN_OUT, &out_header,
				  sizeof(out_header)))
		goto out;
	req->out.h = out_header;

	err = fuse_uring_out_header_has_err(&req->out.h, req);
	if (err) {
		/* req->out.h.error already set */
		goto out;
	}

	err = fuse_uring_copy_from_ring(req, ent, issue_flags);
out:
	fuse_uring_req_end(ent, req, err, issue_flags);
}

/*
 * Get the next fuse req.
 *
 * Returns true if the next fuse request has been assigned to the ent.
 * Else, there is no next fuse request and this returns false.
 */
static bool fuse_uring_get_next_fuse_req(struct fuse_ring_ent *ent,
					 struct fuse_ring_queue *queue,
					 unsigned int issue_flags)
{
	int err;
	struct fuse_req *req;

retry:
	spin_lock(&queue->lock);
	fuse_uring_ent_avail(ent, queue);
	req = fuse_uring_ent_assign_req(ent);
	spin_unlock(&queue->lock);

	if (req) {
		err = fuse_uring_prepare_send(ent, req, issue_flags);
		if (err)
			goto retry;
	}

	return req != NULL;
}

static int fuse_ring_ent_set_commit(struct fuse_ring_ent *ent)
{
	struct fuse_ring_queue *queue = ent->queue;

	lockdep_assert_held(&queue->lock);

	if (WARN_ON_ONCE(ent->state != FRRS_USERSPACE))
		return -EIO;

	ent->state = FRRS_COMMIT;
	list_move(&ent->list, &queue->ent_commit_queue);

	return 0;
}

static void fuse_uring_send(struct fuse_ring_ent *ent, struct io_uring_cmd *cmd,
			    ssize_t ret, unsigned int issue_flags)
{
	struct fuse_ring_queue *queue = ent->queue;

	spin_lock(&queue->lock);
	ent->state = FRRS_USERSPACE;
	list_move_tail(&ent->list, &queue->ent_in_userspace);
	ent->cmd = NULL;
	fuse_uring_add_to_pq(ent);
	spin_unlock(&queue->lock);

	io_uring_cmd_done(cmd, ret, issue_flags);
}

/* FUSE_URING_CMD_COMMIT_AND_FETCH handler */
static int fuse_uring_commit_fetch(struct io_uring_cmd *cmd, int issue_flags,
				   struct fuse_chan *fch)
{
	const struct fuse_uring_cmd_req *cmd_req = io_uring_sqe128_cmd(cmd->sqe,
								       struct fuse_uring_cmd_req);
	struct fuse_ring_ent *ent;
	int err;
	struct fuse_ring *ring = fch->ring;
	struct fuse_ring_queue *queue;
	uint64_t commit_id = READ_ONCE(cmd_req->commit_id);
	unsigned int qid = READ_ONCE(cmd_req->qid);
	struct fuse_pqueue *fpq;
	struct fuse_req *req;

	err = -ENOTCONN;
	if (!ring)
		return err;

	if (qid >= ring->nr_queues)
		return -EINVAL;

	queue = READ_ONCE(ring->queues[qid]);
	if (!queue)
		return err;
	fpq = &queue->fpq;

	if (!READ_ONCE(fch->connected))
		return err;

	spin_lock(&queue->lock);
	if (unlikely(queue->stopped)) {
		spin_unlock(&queue->lock);
		return err;
	}

	if (!fuse_uring_cmd_index_ok(cmd, queue)) {
		spin_unlock(&queue->lock);
		return -EINVAL;
	}

	/* Find a request based on the unique ID of the fuse request
	 * This should get revised, as it needs a hash calculation and list
	 * search. And full struct fuse_pqueue is needed (memory overhead).
	 * As well as the link from req to ring_ent.
	 */
	req = fuse_request_find(fpq, commit_id);
	err = -ENOENT;
	if (!req) {
		pr_info("qid=%d commit_id %llu not found\n", queue->qid,
			commit_id);
		spin_unlock(&queue->lock);
		return err;
	}
	list_del_init(&req->list);
	ent = req->ring_entry;
	req->ring_entry = NULL;

	err = fuse_ring_ent_set_commit(ent);
	if (err != 0) {
		pr_info_ratelimited("qid=%d commit_id %llu state %d",
				    queue->qid, commit_id, ent->state);
		fuse_uring_recycle_buffer(ent);
		spin_unlock(&queue->lock);
		/*
		 * Unregister any zero copyable pages since ent->cmd is null
		 * when it hits fuse_uring_req_end() in this path
		 */
		zero_copy_unregister(cmd, ent, issue_flags);
		fuse_uring_req_end(ent, req, err, issue_flags);
		return err;
	}

	ent->cmd = cmd;
	spin_unlock(&queue->lock);

	/* without the queue lock, as other locks are taken */
	fuse_uring_prepare_cancel(cmd, issue_flags, ent);
	fuse_uring_commit(ent, req, issue_flags);

	/*
	 * Fetching the next request is absolutely required as queued
	 * fuse requests would otherwise not get processed - committing
	 * and fetching is done in one step vs legacy fuse, which has separated
	 * read (fetch request) and write (commit result).
	 *
	 * If there is no next request or if all buffers are busy (if using a
	 * bufpool), the cmd is not returned to userspace. The entry is left
	 * available and the cmd only returns to userspace when there's a
	 * next request and an available buffer.
	 */
	if (fuse_uring_get_next_fuse_req(ent, queue, issue_flags))
		fuse_uring_send(ent, cmd, 0, issue_flags);
	return 0;
}

static bool is_ring_ready(struct fuse_ring *ring, int current_qid)
{
	int qid;
	struct fuse_ring_queue *queue;
	bool ready = true;

	for (qid = 0; qid < ring->nr_queues && ready; qid++) {
		if (current_qid == qid)
			continue;

		queue = READ_ONCE(ring->queues[qid]);
		if (!queue) {
			ready = false;
			break;
		}

		spin_lock(&queue->lock);
		if (list_empty(&queue->ent_avail_queue))
			ready = false;
		spin_unlock(&queue->lock);
	}

	return ready;
}

/*
 * fuse_uring_req_fetch command handling
 */
static int fuse_uring_do_register(struct fuse_ring_ent *ent,
				  struct io_uring_cmd *cmd,
				  unsigned int issue_flags)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_ring *ring = queue->ring;
	struct fuse_chan *fch = ring->chan;
	struct fuse_iqueue *fiq = &fch->iq;

	spin_lock(&fch->lock);
	/* abort teardown path is running or has run */
	if (!fch->connected) {
		spin_unlock(&fch->lock);
		if (atomic_dec_and_test(&ring->queue_refs))
			wake_up_all(&ring->stop_waitq);
		kfree(ent);
		return -ECONNABORTED;
	}
	spin_unlock(&fch->lock);

	fuse_uring_prepare_cancel(cmd, issue_flags, ent);

	spin_lock(&queue->lock);
	ent->cmd = cmd;
	fuse_uring_ent_avail(ent, queue);
	spin_unlock(&queue->lock);

	if (!READ_ONCE(ring->ready)) {
		bool ready = is_ring_ready(ring, queue->qid);

		if (ready) {
			WRITE_ONCE(fiq->ops, &fuse_io_uring_ops);
			smp_store_release(&ring->ready, true);
			wake_up_all(&fch->blocked_waitq);
		}
	}
	return 0;
}

/*
 * sqe->addr is a ptr to an iovec array, iov[FUSE_URING_IOV_HEADERS] has the
 * headers, iov[FUSE_URING_IOV_PAYLOAD] the payload
 */
static int fuse_uring_get_iovec_from_sqe(const struct io_uring_sqe *sqe,
					 struct iovec iov[FUSE_URING_IOV_SEGS])
{
	struct iovec __user *uiov = u64_to_user_ptr(READ_ONCE(sqe->addr));
	struct iov_iter iter;
	ssize_t ret;

	if (sqe->len != FUSE_URING_IOV_SEGS)
		return -EINVAL;

	/*
	 * Direction for buffer access will actually be READ and WRITE,
	 * using write for the import should include READ access as well.
	 */
	ret = import_iovec(WRITE, uiov, FUSE_URING_IOV_SEGS,
			   FUSE_URING_IOV_SEGS, &iov, &iter);
	if (ret < 0)
		return ret;

	return 0;
}

static struct fuse_ring_ent *
fuse_uring_create_ring_ent(struct io_uring_cmd *cmd,
			   struct fuse_ring_queue *queue)
{
	const struct fuse_uring_cmd_req *cmd_req =
		io_uring_sqe128_cmd(cmd->sqe, struct fuse_uring_cmd_req);
	struct fuse_ring *ring = queue->ring;
	struct fuse_ring_ent *ent;
	struct iovec iov[FUSE_URING_IOV_SEGS];
	struct iovec *headers, *payload;
	unsigned int zero_copy_index;

	int err;

	err = fuse_uring_get_iovec_from_sqe(cmd->sqe, iov);
	if (err) {
		pr_info_ratelimited("Failed to get iovec from sqe, err=%d\n",
				    err);
		return ERR_PTR(err);
	}

	zero_copy_index = READ_ONCE(cmd_req->ent_zero_copy_buf_index);
	if (zero_copy_index && !queue->zero_copy)
		return ERR_PTR(-EINVAL);

	err = -EINVAL;
	headers = &iov[FUSE_URING_IOV_HEADERS];
	if (headers->iov_len < sizeof(struct fuse_uring_req_header)) {
		pr_info_ratelimited("Invalid header len %zu\n", headers->iov_len);
		return ERR_PTR(err);
	}

	payload = &iov[FUSE_URING_IOV_PAYLOAD];

	spin_lock(&queue->lock);
	if (bufpool_enabled(queue)) {
		if (payload->iov_base || payload->iov_len ||
		    !fuse_uring_cmd_index_ok(cmd, queue)) {
			spin_unlock(&queue->lock);
			return ERR_PTR(err);
		}
	} else {
		if (payload->iov_len < ring->max_payload_sz) {
			spin_unlock(&queue->lock);
			pr_info_ratelimited("Invalid req payload len %zu\n",
					    payload->iov_len);
			return ERR_PTR(err);
		}
		if (queue->zero_copy) {
			spin_unlock(&queue->lock);
			pr_info_ratelimited("Can only use zero copy with bufpools\n");
			return ERR_PTR(err);
		}
		queue->payload_mode = FUSE_PAYLOAD_PER_ENT;
	}
	spin_unlock(&queue->lock);

	err = -ENOMEM;
	ent = kzalloc_obj(*ent, GFP_KERNEL_ACCOUNT);
	if (!ent)
		return ERR_PTR(err);

	INIT_LIST_HEAD(&ent->list);

	ent->queue = queue;
	ent->headers = headers->iov_base;
	if (queue->payload_mode == FUSE_PAYLOAD_PER_ENT)
		ent->payload = *payload;
	ent->zero_copy_index = zero_copy_index;

	atomic_inc(&ring->queue_refs);
	return ent;
}

/*
 * Register header and payload buffer with the kernel and puts the
 * entry as "ready to get fuse requests" on the queue
 */
static int fuse_uring_register(struct io_uring_cmd *cmd,
			       unsigned int issue_flags, struct fuse_chan *fch)
{
	const struct fuse_uring_cmd_req *cmd_req = io_uring_sqe128_cmd(cmd->sqe,
								       struct fuse_uring_cmd_req);
	struct fuse_ring *ring = smp_load_acquire(&fch->ring);
	struct fuse_ring_queue *queue;
	struct fuse_ring_ent *ent;
	unsigned int qid = READ_ONCE(cmd_req->qid);

	if (!ring)
		return -EINVAL;

	if (qid >= ring->nr_queues) {
		pr_info_ratelimited("fuse: Invalid ring qid %u\n", qid);
		return -EINVAL;
	}

	queue = READ_ONCE(ring->queues[qid]);
	if (!queue) {
		queue = fuse_uring_create_queue(ring, qid, false, false);
		if (IS_ERR(queue))
			return PTR_ERR(queue);
	}

	/*
	 * The created queue above does not need to be destructed in
	 * case of entry errors below, will be done at ring destruction time.
	 */

	ent = fuse_uring_create_ring_ent(cmd, queue);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	return fuse_uring_do_register(ent, cmd, issue_flags);
}

static int fuse_uring_add_queue(struct io_uring_cmd *cmd, struct fuse_chan *fch)
{
	const struct fuse_uring_cmd_req *cmd_req =
		io_uring_sqe128_cmd(cmd->sqe, struct fuse_uring_cmd_req);
	struct fuse_ring *ring = smp_load_acquire(&fch->ring);
	unsigned int qid = READ_ONCE(cmd_req->qid);
	uint64_t flags = READ_ONCE(cmd_req->flags);
	struct fuse_ring_queue *queue;
	bool zero_copy = flags & FUSE_URING_ZERO_COPY;

	if (!ring)
		return -EINVAL;

	if (qid >= ring->nr_queues) {
		pr_info_ratelimited("fuse: Invalid ring qid %u\n", qid);
		return -EINVAL;
	}

	if (flags & ~FUSE_URING_ADD_QUEUE_FLAGS)
		return -EINVAL;

	if (zero_copy && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	queue = fuse_uring_create_queue(ring, qid, zero_copy, true);
	if (IS_ERR(queue))
		return PTR_ERR(queue);

	return 0;
}

static int fuse_uring_add_bufpool(struct io_uring_cmd *cmd,
				  struct fuse_chan *fch)
{
	const struct fuse_uring_cmd_req *cmd_req =
		io_uring_sqe128_cmd(cmd->sqe, struct fuse_uring_cmd_req);
	unsigned int qid = READ_ONCE(cmd_req->qid);
	uint64_t flags = READ_ONCE(cmd_req->flags);
	/* paired with the smp_store_release() in fuse_uring_create */
	struct fuse_ring *ring = smp_load_acquire(&fch->ring);
	struct fuse_ring_queue *queue;
	struct fuse_bufpool *pool;
	uintptr_t pool_uaddr;
	unsigned int pool_len, nr_bufs;
	size_t pool_size, buf_size;
	bool registered = cmd->flags & IORING_URING_CMD_FIXED;

	if (!ring || qid >= ring->nr_queues || flags)
		return -EINVAL;

	/* reserved for future use, must be zero */
	if (READ_ONCE(cmd_req->bufpool.reserved))
		return -EINVAL;

	/* Pairs with smp_store_release() in fuse_uring_create_queue() */
	queue = smp_load_acquire(&ring->queues[qid]);
	if (!queue)
		return -EINVAL;

	pool_uaddr = READ_ONCE(cmd_req->bufpool.uaddr);
	pool_len = READ_ONCE(cmd_req->bufpool.len);

	/* each buffer holds the max payload size */
	buf_size = queue->ring->max_payload_sz;

	nr_bufs = pool_len / buf_size;
	if (!nr_bufs)
		return -EINVAL;

	pool_size = struct_size(pool, free_map, BITS_TO_LONGS(nr_bufs));
	pool = kzalloc(pool_size, GFP_KERNEL_ACCOUNT);
	if (!pool)
		return -ENOMEM;

	pool->base_uaddr = pool_uaddr;
	pool->buf_size = buf_size;
	pool->nr_bufs = nr_bufs;
	/* all buffers are free */
	bitmap_set(pool->free_map, 0, nr_bufs);

	/*
	 * A registered bufpool is reached through an io_uring fixed buffer, so
	 * the pool is registered iff this command was submitted with
	 * IORING_URING_CMD_FIXED. The registered buffer index is taken from
	 * sqe->buf_index.
	 */
	if (registered) {
		pool->registered = true;
		pool->registered_index = READ_ONCE(cmd->sqe->buf_index);
	}

	spin_lock(&queue->lock);
	if (queue->payload_mode != FUSE_PAYLOAD_UNSET) {
		spin_unlock(&queue->lock);
		kfree(pool);
		return -EINVAL;
	}
	queue->bufpool = pool;
	queue->payload_mode = FUSE_PAYLOAD_BUFPOOL;
	spin_unlock(&queue->lock);

	return 0;
}

/*
 * Entry function from io_uring to handle the given passthrough command
 * (op code IORING_OP_URING_CMD)
 */
int fuse_uring_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	struct fuse_dev *fud;
	struct fuse_chan *fch;
	u32 cmd_op = cmd->cmd_op;
	int err;

	if ((unlikely(issue_flags & IO_URING_F_CANCEL))) {
		fuse_uring_cancel(cmd, issue_flags);
		return 0;
	}

	/* This extra SQE size holds struct fuse_uring_cmd_req */
	if (!(issue_flags & IO_URING_F_SQE128))
		return -EINVAL;

	fud = fuse_get_dev(cmd->file);
	if (IS_ERR(fud)) {
		pr_info_ratelimited("No fuse device found\n");
		return PTR_ERR(fud);
	}
	fch = fud->chan;

	/*
	 * The ring is sized from values negotiated by FUSE_INIT
	 *
	 * Pairs with smp_store_release() in fuse_chan_set_initialized()
	 */
	if (!smp_load_acquire(&fch->initialized))
		return -EAGAIN;

	if (fch->abort_with_err)
		return -ECONNABORTED;
	if (!fch->connected)
		return -ENOTCONN;

	/* Once a connection has io-uring enabled on it, it can't be disabled */
	if (!enable_uring && !fch->io_uring) {
		pr_info_ratelimited("fuse-io-uring is disabled by module parameter\n");
		return -EOPNOTSUPP;
	}

	if (!fch->io_uring) {
		pr_info_ratelimited(
			"fuse-io-uring not enabled on this connection\n");
		return -EOPNOTSUPP;
	}

	switch (cmd_op) {
	case FUSE_IO_URING_CMD_REGISTER:
		err = fuse_uring_register(cmd, issue_flags, fch);
		if (err) {
			pr_info_once("FUSE_IO_URING_CMD_REGISTER failed err=%d\n",
				     err);
			fch->io_uring = 0;
			wake_up_all(&fch->blocked_waitq);
			return err;
		}
		break;
	case FUSE_IO_URING_CMD_COMMIT_AND_FETCH:
		err = fuse_uring_commit_fetch(cmd, issue_flags, fch);
		if (err) {
			pr_info_once("FUSE_IO_URING_COMMIT_AND_FETCH failed err=%d\n",
				     err);
			return err;
		}
		break;
	case FUSE_IO_URING_CMD_ADD_QUEUE:
		err = fuse_uring_add_queue(cmd, fch);
		if (err)
			pr_info_once("FUSE_IO_URING_CMD_ADD_QUEUE failed err=%d\n",
				     err);
		return err;
	case FUSE_IO_URING_CMD_ADD_BUFPOOL:
		err = fuse_uring_add_bufpool(cmd, fch);
		if (err)
			pr_info_once("FUSE_IO_URING_ADD_BUFPOOL failed err=%d\n",
				     err);
		return err;
	default:
		return -EINVAL;
	}

	return -EIOCBQUEUED;
}

/*
 * This prepares and sends the ring request in fuse-uring task context.
 * User buffers are not mapped yet - the application does not have permission
 * to write to it - this has to be executed in ring task context.
 */
static void fuse_uring_send_in_task(struct io_tw_req tw_req, io_tw_token_t tw)
{
	unsigned int issue_flags = IO_URING_CMD_TASK_WORK_ISSUE_FLAGS;
	struct io_uring_cmd *cmd = io_uring_cmd_from_tw(tw_req);
	struct fuse_ring_ent *ent = uring_cmd_to_ring_ent(cmd);
	struct fuse_ring_queue *queue = ent->queue;
	int err;

	if (!tw.cancel) {
		err = fuse_uring_prepare_send(ent, ent->fuse_req, issue_flags);
		if (err) {
			if (!fuse_uring_get_next_fuse_req(ent, queue,
							  issue_flags))
				return;
			err = 0;
		}
		fuse_uring_send(ent, cmd, err, issue_flags);
	} else {
		err = -ECANCELED;

		spin_lock(&queue->lock);
		list_del_init(&ent->list);
		fuse_uring_recycle_buffer(ent);
		spin_unlock(&queue->lock);

		io_uring_cmd_done(cmd, err, issue_flags);

		fuse_uring_req_end(ent, ent->fuse_req, err, issue_flags);
		kfree(ent);
		if (atomic_dec_and_test(&queue->ring->queue_refs))
			wake_up_all(&queue->ring->stop_waitq);
	}
}

static struct fuse_ring_queue *fuse_uring_task_to_queue(struct fuse_ring *ring)
{
	unsigned int qid;
	struct fuse_ring_queue *queue;

	qid = task_cpu(current);

	if (WARN_ONCE(qid >= ring->nr_queues,
		      "Core number (%u) exceeds nr queues (%zu)\n", qid,
		      ring->nr_queues))
		qid = 0;

	queue = READ_ONCE(ring->queues[qid]);
	WARN_ONCE(!queue, "Missing queue for qid %d\n", qid);

	return queue;
}

static void fuse_uring_dispatch_ent(struct fuse_ring_ent *ent)
{
	struct io_uring_cmd *cmd = ent->cmd;

	uring_cmd_set_ring_ent(cmd, ent);
	io_uring_cmd_complete_in_task(cmd, fuse_uring_send_in_task);
}

/* queue a fuse request and send it if a ring entry is available */
void fuse_uring_queue_fuse_req(struct fuse_iqueue *fiq, struct fuse_req *req)
{
	struct fuse_ring *ring = req->chan->ring;
	struct fuse_ring_queue *queue;
	struct fuse_ring_ent *ent = NULL;
	int err;

	err = -EINVAL;
	queue = fuse_uring_task_to_queue(ring);
	if (!queue)
		goto err;

	fuse_request_assign_unique(fiq, req);

	spin_lock(&queue->lock);
	err = -ENOTCONN;
	if (unlikely(queue->stopped))
		goto err_unlock;

	set_bit(FR_URING, &req->flags);
	req->ring_queue = queue;
	ent = list_first_entry_or_null(&queue->ent_avail_queue,
				       struct fuse_ring_ent, list);

	if (!ent || fuse_uring_prep_buffer(ent, req)) {
		list_add_tail(&req->list, &queue->fuse_req_queue);
		spin_unlock(&queue->lock);
		return;
	}

	fuse_uring_add_req_to_ring_ent(ent, req);
	spin_unlock(&queue->lock);
	fuse_uring_dispatch_ent(ent);
	return;

err_unlock:
	spin_unlock(&queue->lock);
err:
	req->out.h.error = err;
	clear_bit(FR_PENDING, &req->flags);
	fuse_request_end(req);
}

bool fuse_uring_queue_bq_req(struct fuse_req *req)
{
	struct fuse_chan *fch = req->chan;
	struct fuse_ring *ring = fch->ring;
	struct fuse_ring_queue *queue;
	struct fuse_ring_ent *ent = NULL;

	queue = fuse_uring_task_to_queue(ring);
	if (!queue)
		return false;

	spin_lock(&queue->lock);
	if (unlikely(queue->stopped)) {
		spin_unlock(&queue->lock);
		return false;
	}

	set_bit(FR_URING, &req->flags);
	req->ring_queue = queue;
	list_add_tail(&req->list, &queue->fuse_req_bg_queue);

	ent = list_first_entry_or_null(&queue->ent_avail_queue,
				       struct fuse_ring_ent, list);
	spin_lock(&fch->bg_lock);
	fch->num_background++;
	if (fch->num_background == fch->max_background)
		fch->blocked = 1;
	fuse_uring_flush_bg(queue);
	spin_unlock(&fch->bg_lock);

	/*
	 * Due to bg_queue flush limits there might be other bg requests
	 * in the queue that need to be handled first. Or no further req
	 * might be available.
	 */
	req = list_first_entry_or_null(&queue->fuse_req_queue, struct fuse_req,
				       list);
	if (ent && req && !fuse_uring_prep_buffer(ent, req)) {
		fuse_uring_add_req_to_ring_ent(ent, req);
		spin_unlock(&queue->lock);
		fuse_uring_dispatch_ent(ent);
	} else {
		spin_unlock(&queue->lock);
	}

	return true;
}

bool fuse_uring_remove_pending_req(struct fuse_req *req)
{
	struct fuse_ring_queue *queue = req->ring_queue;

	return fuse_remove_pending_req(req, &queue->lock);
}

static const struct fuse_iqueue_ops fuse_io_uring_ops = {
	/* should be send over io-uring as enhancement */
	.send_forget = fuse_dev_queue_forget,

	/*
	 * could be send over io-uring, but interrupts should be rare,
	 * no need to make the code complex
	 */
	.send_interrupt = fuse_dev_queue_interrupt,
	.send_req = fuse_uring_queue_fuse_req,
};
