/* SPDX-License-Identifier: GPL-2.0
 *
 * FUSE: Filesystem in Userspace
 * Copyright (c) 2023-2024 DataDirect Networks.
 */

#ifndef _FS_FUSE_DEV_URING_I_H
#define _FS_FUSE_DEV_URING_I_H

#include "fuse_i.h"

#ifdef CONFIG_FUSE_IO_URING

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

	/* fuse requests waiting for an entry slot */
	struct list_head fuse_req_queue;

	struct fuse_pqueue fpq;
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
};

bool fuse_uring_enabled(void);
void fuse_uring_destruct(struct fuse_conn *fc);
int fuse_uring_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags);

#else /* CONFIG_FUSE_IO_URING */

struct fuse_ring;

static inline void fuse_uring_create(struct fuse_conn *fc)
{
}

static inline void fuse_uring_destruct(struct fuse_conn *fc)
{
}

static inline bool fuse_uring_enabled(void)
{
	return false;
}

#endif /* CONFIG_FUSE_IO_URING */

#endif /* _FS_FUSE_DEV_URING_I_H */
