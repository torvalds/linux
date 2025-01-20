// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE: Filesystem in Userspace
 * Copyright (c) 2023-2024 DataDirect Networks.
 */

#include "fuse_i.h"
#include "dev_uring_i.h"
#include "fuse_dev_i.h"

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

void fuse_uring_destruct(struct fuse_conn *fc)
{
	struct fuse_ring *ring = fc->ring;
	int qid;

	if (!ring)
		return;

	for (qid = 0; qid < ring->nr_queues; qid++) {
		struct fuse_ring_queue *queue = ring->queues[qid];

		if (!queue)
			continue;

		WARN_ON(!list_empty(&queue->ent_avail_queue));
		WARN_ON(!list_empty(&queue->ent_commit_queue));

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

	fc->ring = ring;
	ring->nr_queues = nr_queues;
	ring->fc = fc;
	ring->max_payload_sz = max_payload_size;

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

	queue = kzalloc(sizeof(*queue), GFP_KERNEL_ACCOUNT);
	if (!queue)
		return NULL;
	queue->qid = qid;
	queue->ring = ring;
	spin_lock_init(&queue->lock);

	INIT_LIST_HEAD(&queue->ent_avail_queue);
	INIT_LIST_HEAD(&queue->ent_commit_queue);

	spin_lock(&fc->lock);
	if (ring->queues[qid]) {
		spin_unlock(&fc->lock);
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
 * fuse_uring_req_fetch command handling
 */
static void fuse_uring_do_register(struct fuse_ring_ent *ent,
				   struct io_uring_cmd *cmd,
				   unsigned int issue_flags)
{
	struct fuse_ring_queue *queue = ent->queue;

	spin_lock(&queue->lock);
	ent->cmd = cmd;
	fuse_uring_ent_avail(ent, queue);
	spin_unlock(&queue->lock);
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
	struct fuse_ring *ring = fc->ring;
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
int __maybe_unused fuse_uring_cmd(struct io_uring_cmd *cmd,
				  unsigned int issue_flags)
{
	struct fuse_dev *fud;
	struct fuse_conn *fc;
	u32 cmd_op = cmd->cmd_op;
	int err;

	if (!enable_uring) {
		pr_info_ratelimited("fuse-io-uring is disabled\n");
		return -EOPNOTSUPP;
	}

	/* This extra SQE size holds struct fuse_uring_cmd_req */
	if (!(issue_flags & IO_URING_F_SQE128))
		return -EINVAL;

	fud = fuse_get_dev(cmd->file);
	if (!fud) {
		pr_info_ratelimited("No fuse device found\n");
		return -ENOTCONN;
	}
	fc = fud->fc;

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
			return err;
		}
		break;
	default:
		return -EINVAL;
	}

	return -EIOCBQUEUED;
}
