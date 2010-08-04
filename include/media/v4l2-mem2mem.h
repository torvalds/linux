/*
 * Memory-to-memory device framework for Video for Linux 2.
 *
 * Helper functions for devices that use memory buffers for both source
 * and destination.
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <p.osciak@samsung.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#ifndef _MEDIA_V4L2_MEM2MEM_H
#define _MEDIA_V4L2_MEM2MEM_H

#include <media/videobuf-core.h>

/**
 * struct v4l2_m2m_ops - mem-to-mem device driver callbacks
 * @device_run:	required. Begin the actual job (transaction) inside this
 *		callback.
 *		The job does NOT have to end before this callback returns
 *		(and it will be the usual case). When the job finishes,
 *		v4l2_m2m_job_finish() has to be called.
 * @job_ready:	optional. Should return 0 if the driver does not have a job
 *		fully prepared to run yet (i.e. it will not be able to finish a
 *		transaction without sleeping). If not provided, it will be
 *		assumed that one source and one destination buffer are all
 *		that is required for the driver to perform one full transaction.
 *		This method may not sleep.
 * @job_abort:	required. Informs the driver that it has to abort the currently
 *		running transaction as soon as possible (i.e. as soon as it can
 *		stop the device safely; e.g. in the next interrupt handler),
 *		even if the transaction would not have been finished by then.
 *		After the driver performs the necessary steps, it has to call
 *		v4l2_m2m_job_finish() (as if the transaction ended normally).
 *		This function does not have to (and will usually not) wait
 *		until the device enters a state when it can be stopped.
 */
struct v4l2_m2m_ops {
	void (*device_run)(void *priv);
	int (*job_ready)(void *priv);
	void (*job_abort)(void *priv);
};

struct v4l2_m2m_dev;

struct v4l2_m2m_queue_ctx {
/* private: internal use only */
	struct videobuf_queue	q;

	/* Queue for buffers ready to be processed as soon as this
	 * instance receives access to the device */
	struct list_head	rdy_queue;
	u8			num_rdy;
};

struct v4l2_m2m_ctx {
/* private: internal use only */
	struct v4l2_m2m_dev		*m2m_dev;

	/* Capture (output to memory) queue context */
	struct v4l2_m2m_queue_ctx	cap_q_ctx;

	/* Output (input from memory) queue context */
	struct v4l2_m2m_queue_ctx	out_q_ctx;

	/* For device job queue */
	struct list_head		queue;
	unsigned long			job_flags;

	/* Instance private data */
	void				*priv;
};

void *v4l2_m2m_get_curr_priv(struct v4l2_m2m_dev *m2m_dev);

struct videobuf_queue *v4l2_m2m_get_vq(struct v4l2_m2m_ctx *m2m_ctx,
				       enum v4l2_buf_type type);

void v4l2_m2m_job_finish(struct v4l2_m2m_dev *m2m_dev,
			 struct v4l2_m2m_ctx *m2m_ctx);

int v4l2_m2m_reqbufs(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		     struct v4l2_requestbuffers *reqbufs);

int v4l2_m2m_querybuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		      struct v4l2_buffer *buf);

int v4l2_m2m_qbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		  struct v4l2_buffer *buf);
int v4l2_m2m_dqbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		   struct v4l2_buffer *buf);

int v4l2_m2m_streamon(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		      enum v4l2_buf_type type);
int v4l2_m2m_streamoff(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		       enum v4l2_buf_type type);

unsigned int v4l2_m2m_poll(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			   struct poll_table_struct *wait);

int v4l2_m2m_mmap(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		  struct vm_area_struct *vma);

struct v4l2_m2m_dev *v4l2_m2m_init(struct v4l2_m2m_ops *m2m_ops);
void v4l2_m2m_release(struct v4l2_m2m_dev *m2m_dev);

struct v4l2_m2m_ctx *v4l2_m2m_ctx_init(void *priv, struct v4l2_m2m_dev *m2m_dev,
			void (*vq_init)(void *priv, struct videobuf_queue *,
					enum v4l2_buf_type));
void v4l2_m2m_ctx_release(struct v4l2_m2m_ctx *m2m_ctx);

void v4l2_m2m_buf_queue(struct v4l2_m2m_ctx *m2m_ctx, struct videobuf_queue *vq,
			struct videobuf_buffer *vb);

/**
 * v4l2_m2m_num_src_bufs_ready() - return the number of source buffers ready for
 * use
 */
static inline
unsigned int v4l2_m2m_num_src_bufs_ready(struct v4l2_m2m_ctx *m2m_ctx)
{
	return m2m_ctx->cap_q_ctx.num_rdy;
}

/**
 * v4l2_m2m_num_src_bufs_ready() - return the number of destination buffers
 * ready for use
 */
static inline
unsigned int v4l2_m2m_num_dst_bufs_ready(struct v4l2_m2m_ctx *m2m_ctx)
{
	return m2m_ctx->out_q_ctx.num_rdy;
}

void *v4l2_m2m_next_buf(struct v4l2_m2m_ctx *m2m_ctx, enum v4l2_buf_type type);

/**
 * v4l2_m2m_next_src_buf() - return next source buffer from the list of ready
 * buffers
 */
static inline void *v4l2_m2m_next_src_buf(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_next_buf(m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
}

/**
 * v4l2_m2m_next_dst_buf() - return next destination buffer from the list of
 * ready buffers
 */
static inline void *v4l2_m2m_next_dst_buf(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_next_buf(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
}

/**
 * v4l2_m2m_get_src_vq() - return videobuf_queue for source buffers
 */
static inline
struct videobuf_queue *v4l2_m2m_get_src_vq(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
}

/**
 * v4l2_m2m_get_dst_vq() - return videobuf_queue for destination buffers
 */
static inline
struct videobuf_queue *v4l2_m2m_get_dst_vq(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
}

void *v4l2_m2m_buf_remove(struct v4l2_m2m_ctx *m2m_ctx,
			  enum v4l2_buf_type type);

/**
 * v4l2_m2m_src_buf_remove() - take off a source buffer from the list of ready
 * buffers and return it
 */
static inline void *v4l2_m2m_src_buf_remove(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_buf_remove(m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
}

/**
 * v4l2_m2m_dst_buf_remove() - take off a destination buffer from the list of
 * ready buffers and return it
 */
static inline void *v4l2_m2m_dst_buf_remove(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_buf_remove(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
}

#endif /* _MEDIA_V4L2_MEM2MEM_H */

