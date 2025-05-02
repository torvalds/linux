/* SPDX-License-Identifier: GPL-2.0
 *
 * FUSE: Filesystem in Userspace
 * Copyright (c) 2023-2024 DataDirect Networks.
 */

#ifndef _FS_FUSE_DEV_URING_I_H
#define _FS_FUSE_DEV_URING_I_H

#include "fuse_i.h"

#ifdef CONFIG_FUSE_IO_URING

#define FUSE_URING_TEARDOWN_TIMEOUT (5 * HZ)
#define FUSE_URING_TEARDOWN_INTERVAL (HZ/20)

enum fuse_ring_req_state {
	FRRS_INVALID = 0,

	/* The ring entry received from userspace and it is being processed */
	FRRS_COMMIT,

	/* The ring entry is waiting for new fuse requests */
	FRRS_AVAILABLE,

	/* The ring entry got assigned a fuse req */
	FRRS_FUSE_REQ,

	/* The ring entry is in or on the way to user space */
	FRRS_USERSPACE,

	/* The ring entry is in teardown */
	FRRS_TEARDOWN,

	/* The ring entry is released, but not freed yet */
	FRRS_RELEASED,
};

/** A fuse ring entry, part of the ring queue */
struct fuse_ring_ent {
	/* userspace buffer */
	struct fuse_uring_req_header __user *headers;
	void __user *payload;

	/* the ring queue that owns the request */
	struct fuse_ring_queue *queue;

	/* fields below are protected by queue->lock */

	struct io_uring_cmd *cmd;

	struct list_head list;

	enum fuse_ring_req_state state;

	struct fuse_req *fuse_req;
};

struct fuse_ring_queue {
	/*
	 * back pointer to the main fuse uring structure that holds this
	 * queue
	 */
	struct fuse_ring *ring;

	/* queue id, corresponds to the cpu core */
	unsigned int qid;

	/*
	 * queue lock, taken when any value in the queue changes _and_ also
	 * a ring entry state changes.
	 */
	spinlock_t lock;

	/* available ring entries (struct fuse_ring_ent) */
	struct list_head ent_avail_queue;

	/*
	 * entries in the process of being committed or in the process
	 * to be sent to userspace
	 */
	struct list_head ent_w_req_queue;
	struct list_head ent_commit_queue;

	/* entries in userspace */
	struct list_head ent_in_userspace;

	/* entries that are released */
	struct list_head ent_released;

	/* fuse requests waiting for an entry slot */
	struct list_head fuse_req_queue;

	/* background fuse requests */
	struct list_head fuse_req_bg_queue;

	struct fuse_pqueue fpq;

	unsigned int active_background;

	bool stopped;
};

/**
 * Describes if uring is for communication and holds alls the data needed
 * for uring communication
 */
struct fuse_ring {
	/* back pointer */
	struct fuse_conn *fc;

	/* number of ring queues */
	size_t nr_queues;

	/* maximum payload/arg size */
	size_t max_payload_sz;

	struct fuse_ring_queue **queues;

	/*
	 * Log ring entry states on stop when entries cannot be released
	 */
	unsigned int stop_debug_log : 1;

	wait_queue_head_t stop_waitq;

	/* async tear down */
	struct delayed_work async_teardown_work;

	/* log */
	unsigned long teardown_time;

	atomic_t queue_refs;

	bool ready;
};

bool fuse_uring_enabled(void);
void fuse_uring_destruct(struct fuse_conn *fc);
void fuse_uring_stop_queues(struct fuse_ring *ring);
void fuse_uring_abort_end_requests(struct fuse_ring *ring);
int fuse_uring_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags);
void fuse_uring_queue_fuse_req(struct fuse_iqueue *fiq, struct fuse_req *req);
bool fuse_uring_queue_bq_req(struct fuse_req *req);
bool fuse_uring_remove_pending_req(struct fuse_req *req);
bool fuse_uring_request_expired(struct fuse_conn *fc);

static inline void fuse_uring_abort(struct fuse_conn *fc)
{
	struct fuse_ring *ring = fc->ring;

	if (ring == NULL)
		return;

	if (atomic_read(&ring->queue_refs) > 0) {
		fuse_uring_abort_end_requests(ring);
		fuse_uring_stop_queues(ring);
	}
}

static inline void fuse_uring_wait_stopped_queues(struct fuse_conn *fc)
{
	struct fuse_ring *ring = fc->ring;

	if (ring)
		wait_event(ring->stop_waitq,
			   atomic_read(&ring->queue_refs) == 0);
}

static inline bool fuse_uring_ready(struct fuse_conn *fc)
{
	return fc->ring && fc->ring->ready;
}

#else /* CONFIG_FUSE_IO_URING */

static inline void fuse_uring_destruct(struct fuse_conn *fc)
{
}

static inline bool fuse_uring_enabled(void)
{
	return false;
}

static inline void fuse_uring_abort(struct fuse_conn *fc)
{
}

static inline void fuse_uring_wait_stopped_queues(struct fuse_conn *fc)
{
}

static inline bool fuse_uring_ready(struct fuse_conn *fc)
{
	return false;
}

static inline bool fuse_uring_remove_pending_req(struct fuse_req *req)
{
	return false;
}

static inline bool fuse_uring_request_expired(struct fuse_conn *fc)
{
	return false;
}

#endif /* CONFIG_FUSE_IO_URING */

#endif /* _FS_FUSE_DEV_URING_I_H */
