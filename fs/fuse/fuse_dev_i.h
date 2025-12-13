/* SPDX-License-Identifier: GPL-2.0
 *
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>
 */
#ifndef _FS_FUSE_DEV_I_H
#define _FS_FUSE_DEV_I_H

#include <linux/types.h>

/* Ordinary requests have even IDs, while interrupts IDs are odd */
#define FUSE_INT_REQ_BIT (1ULL << 0)
#define FUSE_REQ_ID_STEP (1ULL << 1)

extern struct wait_queue_head fuse_dev_waitq;

struct fuse_arg;
struct fuse_args;
struct fuse_pqueue;
struct fuse_req;
struct fuse_iqueue;
struct fuse_forget_link;

struct fuse_copy_state {
	struct fuse_req *req;
	struct iov_iter *iter;
	struct pipe_buffer *pipebufs;
	struct pipe_buffer *currbuf;
	struct pipe_inode_info *pipe;
	unsigned long nr_segs;
	struct page *pg;
	unsigned int len;
	unsigned int offset;
	bool write:1;
	bool move_folios:1;
	bool is_uring:1;
	struct {
		unsigned int copied_sz; /* copied size into the user buffer */
	} ring;
};

#define FUSE_DEV_SYNC_INIT ((struct fuse_dev *) 1)
#define FUSE_DEV_PTR_MASK (~1UL)

static inline struct fuse_dev *__fuse_get_dev(struct file *file)
{
	/*
	 * Lockless access is OK, because file->private data is set
	 * once during mount and is valid until the file is released.
	 */
	struct fuse_dev *fud = READ_ONCE(file->private_data);

	return (typeof(fud)) ((unsigned long) fud & FUSE_DEV_PTR_MASK);
}

struct fuse_dev *fuse_get_dev(struct file *file);

unsigned int fuse_req_hash(u64 unique);
struct fuse_req *fuse_request_find(struct fuse_pqueue *fpq, u64 unique);

void fuse_dev_end_requests(struct list_head *head);

void fuse_copy_init(struct fuse_copy_state *cs, bool write,
			   struct iov_iter *iter);
int fuse_copy_args(struct fuse_copy_state *cs, unsigned int numargs,
		   unsigned int argpages, struct fuse_arg *args,
		   int zeroing);
int fuse_copy_out_args(struct fuse_copy_state *cs, struct fuse_args *args,
		       unsigned int nbytes);
void fuse_dev_queue_forget(struct fuse_iqueue *fiq,
			   struct fuse_forget_link *forget);
void fuse_dev_queue_interrupt(struct fuse_iqueue *fiq, struct fuse_req *req);
bool fuse_remove_pending_req(struct fuse_req *req, spinlock_t *lock);

bool fuse_request_expired(struct fuse_conn *fc, struct list_head *list);

#endif

