/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  BSG helper library
 *
 *  Copyright (C) 2008   James Smart, Emulex Corporation
 *  Copyright (C) 2011   Red Hat, Inc.  All rights reserved.
 *  Copyright (C) 2011   Mike Christie
 */
#ifndef _BLK_BSG_
#define _BLK_BSG_

#include <linux/blkdev.h>
#include <scsi/scsi_request.h>

struct request;
struct device;
struct scatterlist;
struct request_queue;

typedef int (bsg_job_fn) (struct bsg_job *);
typedef enum blk_eh_timer_return (bsg_timeout_fn)(struct request *);

struct bsg_buffer {
	unsigned int payload_len;
	int sg_cnt;
	struct scatterlist *sg_list;
};

struct bsg_job {
	struct device *dev;

	struct kref kref;

	unsigned int timeout;

	/* Transport/driver specific request/reply structs */
	void *request;
	void *reply;

	unsigned int request_len;
	unsigned int reply_len;
	/*
	 * On entry : reply_len indicates the buffer size allocated for
	 * the reply.
	 *
	 * Upon completion : the message handler must set reply_len
	 *  to indicates the size of the reply to be returned to the
	 *  caller.
	 */

	/* DMA payloads for the request/response */
	struct bsg_buffer request_payload;
	struct bsg_buffer reply_payload;

	int result;
	unsigned int reply_payload_rcv_len;

	/* BIDI support */
	struct request *bidi_rq;
	struct bio *bidi_bio;

	void *dd_data;		/* Used for driver-specific storage */
};

void bsg_job_done(struct bsg_job *job, int result,
		  unsigned int reply_payload_rcv_len);
struct request_queue *bsg_setup_queue(struct device *dev, const char *name,
		bsg_job_fn *job_fn, bsg_timeout_fn *timeout, int dd_job_size);
void bsg_remove_queue(struct request_queue *q);
void bsg_job_put(struct bsg_job *job);
int __must_check bsg_job_get(struct bsg_job *job);

#endif
