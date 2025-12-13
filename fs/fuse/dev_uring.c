// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE: Filesystem in Userspace
 * Copyright (c) 2023-2024 DataDirect Networks.
 */

#include "fuse_i.h"
#include "dev_uring_i.h"
#include "fuse_dev_i.h"
#include "fuse_trace.h"

#include <linux/fs.h>
#include <linux/io_uring/cmd.h>

static bool __read_mostly enable_uring;
module_param(enable_uring, bool, 0644);
MODULE_PARM_DESC(enable_uring,
		 "Enable userspace communication through io-uring");

#define FUSE_URING_IOV_SEGS 2 /* header and payload */


bool fuse_uring_enabled(void)
{
	return enable_uring;
}

struct fuse_uring_pdu {
	struct fuse_ring_ent *ent;
};

static const struct fuse_iqueue_ops fuse_io_uring_ops;

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
	struct fuse_conn *fc = ring->fc;

	lockdep_assert_held(&queue->lock);
	lockdep_assert_held(&fc->bg_lock);

	/*
	 * Allow one bg request per queue, ignoring global fc limits.
	 * This prevents a single queue from consuming all resources and
	 * eliminates the need for remote queue wake-ups when global
	 * limits are met but this queue has no more waiting requests.
	 */
	while ((fc->active_background < fc->max_background ||
		!queue->active_background) &&
	       (!list_empty(&queue->fuse_req_bg_queue))) {
		struct fuse_req *req;

		req = list_first_entry(&queue->fuse_req_bg_queue,
				       struct fuse_req, list);
		fc->active_background++;
		queue->active_background++;

		list_move_tail(&req->list, &queue->fuse_req_queue);
	}
}

static void fuse_uring_req_end(struct fuse_ring_ent *ent, struct fuse_req *req,
			       int error)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_ring *ring = queue->ring;
	struct fuse_conn *fc = ring->fc;

	lockdep_assert_not_held(&queue->lock);
	spin_lock(&queue->lock);
	ent->fuse_req = NULL;
	if (test_bit(FR_BACKGROUND, &req->flags)) {
		queue->active_background--;
		spin_lock(&fc->bg_lock);
		fuse_uring_flush_bg(queue);
		spin_unlock(&fc->bg_lock);
	}

	spin_unlock(&queue->lock);

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
	struct fuse_conn *fc = ring->fc;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		queue = READ_ONCE(ring->queues[qid]);
		if (!queue)
			continue;

		queue->stopped = true;

		WARN_ON_ONCE(ring->fc->max_background != UINT_MAX);
		spin_lock(&queue->lock);
		spin_lock(&fc->bg_lock);
		fuse_uring_flush_bg(queue);
		spin_unlock(&fc->bg_lock);
		spin_unlock(&queue->lock);
		fuse_uring_abort_end_queue_requests(queue);
	}
}

static bool ent_list_request_expired(struct fuse_conn *fc, struct list_head *list)
{
	struct fuse_ring_ent *ent;
	struct fuse_req *req;

	ent = list_first_entry_or_null(list, struct fuse_ring_ent, list);
	if (!ent)
		return false;

	req = ent->fuse_req;

	return time_is_before_jiffies(req->create_time +
				      fc->timeout.req_timeout);
}

bool fuse_uring_request_expired(struct fuse_conn *fc)
{
	struct fuse_ring *ring = fc->ring;
	struct fuse_ring_queue *queue;
	int qid;

	if (!ring)
		return false;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		queue = READ_ONCE(ring->queues[qid]);
		if (!queue)
			continue;

		spin_lock(&queue->lock);
		if (fuse_request_expired(fc, &queue->fuse_req_queue) ||
		    fuse_request_expired(fc, &queue->fuse_req_bg_queue) ||
		    ent_list_request_expired(fc, &queue->ent_w_req_queue) ||
		    ent_list_request_expired(fc, &queue->ent_in_userspace)) {
			spin_unlock(&queue->lock);
			return true;
		}
		spin_unlock(&queue->lock);
	}

	return false;
}

void fuse_uring_destruct(struct fuse_conn *fc)
{
	struct fuse_ring *ring = fc->ring;
	int qid;

	if (!ring)
		return;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		struct fuse_ring_queue *queue = ring->queues[qid];
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
		kfree(queue);
		ring->queues[qid] = NULL;
	}

	kfree(ring->queues);
	kfree(ring);
	fc->ring = NULL;
}

/*
 * Basic ring setup for this connection based on the provided configuration
 */
static struct fuse_ring *fuse_uring_create(struct fuse_conn *fc)
{
	struct fuse_ring *ring;
	size_t nr_queues = num_possible_cpus();
	struct fuse_ring *res = NULL;
	size_t max_payload_size;

	ring = kzalloc(sizeof(*fc->ring), GFP_KERNEL_ACCOUNT);
	if (!ring)
		return NULL;

	ring->queues = kcalloc(nr_queues, sizeof(struct fuse_ring_queue *),
			       GFP_KERNEL_ACCOUNT);
	if (!ring->queues)
		goto out_err;

	max_payload_size = max(FUSE_MIN_READ_BUFFER, fc->max_write);
	max_payload_size = max(max_payload_size, fc->max_pages * PAGE_SIZE);

	spin_lock(&fc->lock);
	if (fc->ring) {
		/* race, another thread created the ring in the meantime */
		spin_unlock(&fc->lock);
		res = fc->ring;
		goto out_err;
	}

	init_waitqueue_head(&ring->stop_waitq);

	ring->nr_queues = nr_queues;
	ring->fc = fc;
	ring->max_payload_sz = max_payload_size;
	smp_store_release(&fc->ring, ring);

	spin_unlock(&fc->lock);
	return ring;

out_err:
	kfree(ring->queues);
	kfree(ring);
	return res;
}

static struct fuse_ring_queue *fuse_uring_create_queue(struct fuse_ring *ring,
						       int qid)
{
	struct fuse_conn *fc = ring->fc;
	struct fuse_ring_queue *queue;
	struct list_head *pq;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL_ACCOUNT);
	if (!queue)
		return NULL;
	pq = kcalloc(FUSE_PQ_HASH_SIZE, sizeof(struct list_head), GFP_KERNEL);
	if (!pq) {
		kfree(queue);
		return NULL;
	}

	queue->qid = qid;
	queue->ring = ring;
	spin_lock_init(&queue->lock);

	INIT_LIST_HEAD(&queue->ent_avail_queue);
	INIT_LIST_HEAD(&queue->ent_commit_queue);
	INIT_LIST_HEAD(&queue->ent_w_req_queue);
	INIT_LIST_HEAD(&queue->ent_in_userspace);
	INIT_LIST_HEAD(&queue->fuse_req_queue);
	INIT_LIST_HEAD(&queue->fuse_req_bg_queue);
	INIT_LIST_HEAD(&queue->ent_released);

	queue->fpq.processing = pq;
	fuse_pqueue_init(&queue->fpq);

	spin_lock(&fc->lock);
	if (ring->queues[qid]) {
		spin_unlock(&fc->lock);
		kfree(queue->fpq.processing);
		kfree(queue);
		return ring->queues[qid];
	}

	/*
	 * write_once and lock as the caller mostly doesn't take the lock at all
	 */
	WRITE_ONCE(ring->queues[qid], queue);
	spin_unlock(&fc->lock);

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

/*
 * Log state debug info
 */
static void fuse_uring_log_ent_state(struct fuse_ring *ring)
{
	int qid;
	struct fuse_ring_ent *ent;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		struct fuse_ring_queue *queue = ring->queues[qid];

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
	int qid;
	struct fuse_ring *ring =
		container_of(work, struct fuse_ring, async_teardown_work.work);

	/* XXX code dup */
	for (qid = 0; qid < ring->nr_queues; qid++) {
		struct fuse_ring_queue *queue = READ_ONCE(ring->queues[qid]);

		if (!queue)
			continue;

		fuse_uring_teardown_entries(queue);
	}

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
	}
}

/*
 * Stop the ring queues
 */
void fuse_uring_stop_queues(struct fuse_ring *ring)
{
	int qid;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		struct fuse_ring_queue *queue = READ_ONCE(ring->queues[qid]);

		if (!queue)
			continue;

		fuse_uring_teardown_entries(queue);
	}

	if (atomic_read(&ring->queue_refs) > 0) {
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
		ent->state = FRRS_USERSPACE;
		list_move_tail(&ent->list, &queue->ent_in_userspace);
		need_cmd_done = true;
		ent->cmd = NULL;
	}
	spin_unlock(&queue->lock);

	if (need_cmd_done) {
		/* no queue lock to avoid lock order issues */
		io_uring_cmd_done(cmd, -ENOTCONN, issue_flags);
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
					 struct fuse_req *req,
					 struct fuse_conn *fc)
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

static int fuse_uring_copy_from_ring(struct fuse_ring *ring,
				     struct fuse_req *req,
				     struct fuse_ring_ent *ent)
{
	struct fuse_copy_state cs;
	struct fuse_args *args = req->args;
	struct iov_iter iter;
	int err;
	struct fuse_uring_ent_in_out ring_in_out;

	err = copy_from_user(&ring_in_out, &ent->headers->ring_ent_in_out,
			     sizeof(ring_in_out));
	if (err)
		return -EFAULT;

	err = import_ubuf(ITER_SOURCE, ent->payload, ring->max_payload_sz,
			  &iter);
	if (err)
		return err;

	fuse_copy_init(&cs, false, &iter);
	cs.is_uring = true;
	cs.req = req;

	return fuse_copy_out_args(&cs, args, ring_in_out.payload_sz);
}

 /*
  * Copy data from the req to the ring buffer
  */
static int fuse_uring_args_to_ring(struct fuse_ring *ring, struct fuse_req *req,
				   struct fuse_ring_ent *ent)
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

	err = import_ubuf(ITER_DEST, ent->payload, ring->max_payload_sz, &iter);
	if (err) {
		pr_info_ratelimited("fuse: Import of user buffer failed\n");
		return err;
	}

	fuse_copy_init(&cs, true, &iter);
	cs.is_uring = true;
	cs.req = req;

	if (num_args > 0) {
		/*
		 * Expectation is that the first argument is the per op header.
		 * Some op code have that as zero size.
		 */
		if (args->in_args[0].size > 0) {
			err = copy_to_user(&ent->headers->op_in, in_args->value,
					   in_args->size);
			if (err) {
				pr_info_ratelimited(
					"Copying the header failed.\n");
				return -EFAULT;
			}
		}
		in_args++;
		num_args--;
	}

	/* copy the payload */
	err = fuse_copy_args(&cs, num_args, args->in_pages,
			     (struct fuse_arg *)in_args, 0);
	if (err) {
		pr_info_ratelimited("%s fuse_copy_args failed\n", __func__);
		return err;
	}

	ent_in_out.payload_sz = cs.ring.copied_sz;
	err = copy_to_user(&ent->headers->ring_ent_in_out, &ent_in_out,
			   sizeof(ent_in_out));
	return err ? -EFAULT : 0;
}

static int fuse_uring_copy_to_ring(struct fuse_ring_ent *ent,
				   struct fuse_req *req)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_ring *ring = queue->ring;
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
	err = fuse_uring_args_to_ring(ring, req, ent);
	if (unlikely(err)) {
		pr_info_ratelimited("Copy to ring failed: %d\n", err);
		return err;
	}

	/* copy fuse_in_header */
	err = copy_to_user(&ent->headers->in_out, &req->in.h,
			   sizeof(req->in.h));
	if (err) {
		err = -EFAULT;
		return err;
	}

	return 0;
}

static int fuse_uring_prepare_send(struct fuse_ring_ent *ent,
				   struct fuse_req *req)
{
	int err;

	err = fuse_uring_copy_to_ring(ent, req);
	if (!err)
		set_bit(FR_SENT, &req->flags);
	else
		fuse_uring_req_end(ent, req, err);

	return err;
}

/*
 * Write data to the ring buffer and send the request to userspace,
 * userspace will read it
 * This is comparable with classical read(/dev/fuse)
 */
static int fuse_uring_send_next_to_ring(struct fuse_ring_ent *ent,
					struct fuse_req *req,
					unsigned int issue_flags)
{
	struct fuse_ring_queue *queue = ent->queue;
	int err;
	struct io_uring_cmd *cmd;

	err = fuse_uring_prepare_send(ent, req);
	if (err)
		return err;

	spin_lock(&queue->lock);
	cmd = ent->cmd;
	ent->cmd = NULL;
	ent->state = FRRS_USERSPACE;
	list_move_tail(&ent->list, &queue->ent_in_userspace);
	spin_unlock(&queue->lock);

	io_uring_cmd_done(cmd, 0, issue_flags);
	return 0;
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

/* Used to find the request on SQE commit */
static void fuse_uring_add_to_pq(struct fuse_ring_ent *ent,
				 struct fuse_req *req)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_pqueue *fpq = &queue->fpq;
	unsigned int hash;

	req->ring_entry = ent;
	hash = fuse_req_hash(req->in.h.unique);
	list_move_tail(&req->list, &fpq->processing[hash]);
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
	ent->fuse_req = req;
	ent->state = FRRS_FUSE_REQ;
	list_move_tail(&ent->list, &queue->ent_w_req_queue);
	fuse_uring_add_to_pq(ent, req);
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
	if (req)
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
	struct fuse_ring *ring = ent->queue->ring;
	struct fuse_conn *fc = ring->fc;
	ssize_t err = 0;

	err = copy_from_user(&req->out.h, &ent->headers->in_out,
			     sizeof(req->out.h));
	if (err) {
		req->out.h.error = -EFAULT;
		goto out;
	}

	err = fuse_uring_out_header_has_err(&req->out.h, req, fc);
	if (err) {
		/* req->out.h.error already set */
		goto out;
	}

	err = fuse_uring_copy_from_ring(ring, req, ent);
out:
	fuse_uring_req_end(ent, req, err);
}

/*
 * Get the next fuse req and send it
 */
static void fuse_uring_next_fuse_req(struct fuse_ring_ent *ent,
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
		err = fuse_uring_send_next_to_ring(ent, req, issue_flags);
		if (err)
			goto retry;
	}
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

/* FUSE_URING_CMD_COMMIT_AND_FETCH handler */
static int fuse_uring_commit_fetch(struct io_uring_cmd *cmd, int issue_flags,
				   struct fuse_conn *fc)
{
	const struct fuse_uring_cmd_req *cmd_req = io_uring_sqe_cmd(cmd->sqe);
	struct fuse_ring_ent *ent;
	int err;
	struct fuse_ring *ring = fc->ring;
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

	queue = ring->queues[qid];
	if (!queue)
		return err;
	fpq = &queue->fpq;

	if (!READ_ONCE(fc->connected) || READ_ONCE(queue->stopped))
		return err;

	spin_lock(&queue->lock);
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
		spin_unlock(&queue->lock);
		req->out.h.error = err;
		clear_bit(FR_SENT, &req->flags);
		fuse_request_end(req);
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
	 */
	fuse_uring_next_fuse_req(ent, queue, issue_flags);
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

		queue = ring->queues[qid];
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
static void fuse_uring_do_register(struct fuse_ring_ent *ent,
				   struct io_uring_cmd *cmd,
				   unsigned int issue_flags)
{
	struct fuse_ring_queue *queue = ent->queue;
	struct fuse_ring *ring = queue->ring;
	struct fuse_conn *fc = ring->fc;
	struct fuse_iqueue *fiq = &fc->iq;

	fuse_uring_prepare_cancel(cmd, issue_flags, ent);

	spin_lock(&queue->lock);
	ent->cmd = cmd;
	fuse_uring_ent_avail(ent, queue);
	spin_unlock(&queue->lock);

	if (!ring->ready) {
		bool ready = is_ring_ready(ring, queue->qid);

		if (ready) {
			WRITE_ONCE(fiq->ops, &fuse_io_uring_ops);
			WRITE_ONCE(ring->ready, true);
			wake_up_all(&fc->blocked_waitq);
		}
	}
}

/*
 * sqe->addr is a ptr to an iovec array, iov[0] has the headers, iov[1]
 * the payload
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
	struct fuse_ring *ring = queue->ring;
	struct fuse_ring_ent *ent;
	size_t payload_size;
	struct iovec iov[FUSE_URING_IOV_SEGS];
	int err;

	err = fuse_uring_get_iovec_from_sqe(cmd->sqe, iov);
	if (err) {
		pr_info_ratelimited("Failed to get iovec from sqe, err=%d\n",
				    err);
		return ERR_PTR(err);
	}

	err = -EINVAL;
	if (iov[0].iov_len < sizeof(struct fuse_uring_req_header)) {
		pr_info_ratelimited("Invalid header len %zu\n", iov[0].iov_len);
		return ERR_PTR(err);
	}

	payload_size = iov[1].iov_len;
	if (payload_size < ring->max_payload_sz) {
		pr_info_ratelimited("Invalid req payload len %zu\n",
				    payload_size);
		return ERR_PTR(err);
	}

	err = -ENOMEM;
	ent = kzalloc(sizeof(*ent), GFP_KERNEL_ACCOUNT);
	if (!ent)
		return ERR_PTR(err);

	INIT_LIST_HEAD(&ent->list);

	ent->queue = queue;
	ent->headers = iov[0].iov_base;
	ent->payload = iov[1].iov_base;

	atomic_inc(&ring->queue_refs);
	return ent;
}

/*
 * Register header and payload buffer with the kernel and puts the
 * entry as "ready to get fuse requests" on the queue
 */
static int fuse_uring_register(struct io_uring_cmd *cmd,
			       unsigned int issue_flags, struct fuse_conn *fc)
{
	const struct fuse_uring_cmd_req *cmd_req = io_uring_sqe_cmd(cmd->sqe);
	struct fuse_ring *ring = smp_load_acquire(&fc->ring);
	struct fuse_ring_queue *queue;
	struct fuse_ring_ent *ent;
	int err;
	unsigned int qid = READ_ONCE(cmd_req->qid);

	err = -ENOMEM;
	if (!ring) {
		ring = fuse_uring_create(fc);
		if (!ring)
			return err;
	}

	if (qid >= ring->nr_queues) {
		pr_info_ratelimited("fuse: Invalid ring qid %u\n", qid);
		return -EINVAL;
	}

	queue = ring->queues[qid];
	if (!queue) {
		queue = fuse_uring_create_queue(ring, qid);
		if (!queue)
			return err;
	}

	/*
	 * The created queue above does not need to be destructed in
	 * case of entry errors below, will be done at ring destruction time.
	 */

	ent = fuse_uring_create_ring_ent(cmd, queue);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	fuse_uring_do_register(ent, cmd, issue_flags);

	return 0;
}

/*
 * Entry function from io_uring to handle the given passthrough command
 * (op code IORING_OP_URING_CMD)
 */
int fuse_uring_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	struct fuse_dev *fud;
	struct fuse_conn *fc;
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
	fc = fud->fc;

	/* Once a connection has io-uring enabled on it, it can't be disabled */
	if (!enable_uring && !fc->io_uring) {
		pr_info_ratelimited("fuse-io-uring is disabled\n");
		return -EOPNOTSUPP;
	}

	if (fc->aborted)
		return -ECONNABORTED;
	if (!fc->connected)
		return -ENOTCONN;

	/*
	 * fuse_uring_register() needs the ring to be initialized,
	 * we need to know the max payload size
	 */
	if (!fc->initialized)
		return -EAGAIN;

	switch (cmd_op) {
	case FUSE_IO_URING_CMD_REGISTER:
		err = fuse_uring_register(cmd, issue_flags, fc);
		if (err) {
			pr_info_once("FUSE_IO_URING_CMD_REGISTER failed err=%d\n",
				     err);
			fc->io_uring = 0;
			wake_up_all(&fc->blocked_waitq);
			return err;
		}
		break;
	case FUSE_IO_URING_CMD_COMMIT_AND_FETCH:
		err = fuse_uring_commit_fetch(cmd, issue_flags, fc);
		if (err) {
			pr_info_once("FUSE_IO_URING_COMMIT_AND_FETCH failed err=%d\n",
				     err);
			return err;
		}
		break;
	default:
		return -EINVAL;
	}

	return -EIOCBQUEUED;
}

static void fuse_uring_send(struct fuse_ring_ent *ent, struct io_uring_cmd *cmd,
			    ssize_t ret, unsigned int issue_flags)
{
	struct fuse_ring_queue *queue = ent->queue;

	spin_lock(&queue->lock);
	ent->state = FRRS_USERSPACE;
	list_move_tail(&ent->list, &queue->ent_in_userspace);
	ent->cmd = NULL;
	spin_unlock(&queue->lock);

	io_uring_cmd_done(cmd, ret, issue_flags);
}

/*
 * This prepares and sends the ring request in fuse-uring task context.
 * User buffers are not mapped yet - the application does not have permission
 * to write to it - this has to be executed in ring task context.
 */
static void fuse_uring_send_in_task(struct io_uring_cmd *cmd,
				    unsigned int issue_flags)
{
	struct fuse_ring_ent *ent = uring_cmd_to_ring_ent(cmd);
	struct fuse_ring_queue *queue = ent->queue;
	int err;

	if (!(issue_flags & IO_URING_F_TASK_DEAD)) {
		err = fuse_uring_prepare_send(ent, ent->fuse_req);
		if (err) {
			fuse_uring_next_fuse_req(ent, queue, issue_flags);
			return;
		}
	} else {
		err = -ECANCELED;
	}

	fuse_uring_send(ent, cmd, err, issue_flags);
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

	queue = ring->queues[qid];
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
	struct fuse_conn *fc = req->fm->fc;
	struct fuse_ring *ring = fc->ring;
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
	if (ent)
		fuse_uring_add_req_to_ring_ent(ent, req);
	else
		list_add_tail(&req->list, &queue->fuse_req_queue);
	spin_unlock(&queue->lock);

	if (ent)
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
	struct fuse_conn *fc = req->fm->fc;
	struct fuse_ring *ring = fc->ring;
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
	spin_lock(&fc->bg_lock);
	fc->num_background++;
	if (fc->num_background == fc->max_background)
		fc->blocked = 1;
	fuse_uring_flush_bg(queue);
	spin_unlock(&fc->bg_lock);

	/*
	 * Due to bg_queue flush limits there might be other bg requests
	 * in the queue that need to be handled first. Or no further req
	 * might be available.
	 */
	req = list_first_entry_or_null(&queue->fuse_req_queue, struct fuse_req,
				       list);
	if (ent && req) {
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
