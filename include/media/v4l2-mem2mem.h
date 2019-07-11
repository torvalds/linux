/*
 * Memory-to-memory device framework for Video for Linux 2.
 *
 * Helper functions for devices that use memory buffers for both source
 * and destination.
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#ifndef _MEDIA_V4L2_MEM2MEM_H
#define _MEDIA_V4L2_MEM2MEM_H

#include <media/videobuf2-v4l2.h>

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
 * @job_abort:	optional. Informs the driver that it has to abort the currently
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

struct video_device;
struct v4l2_m2m_dev;

/**
 * struct v4l2_m2m_queue_ctx - represents a queue for buffers ready to be
 *	processed
 *
 * @q:		pointer to struct &vb2_queue
 * @rdy_queue:	List of V4L2 mem-to-mem queues
 * @rdy_spinlock: spin lock to protect the struct usage
 * @num_rdy:	number of buffers ready to be processed
 * @buffered:	is the queue buffered?
 *
 * Queue for buffers ready to be processed as soon as this
 * instance receives access to the device.
 */

struct v4l2_m2m_queue_ctx {
	struct vb2_queue	q;

	struct list_head	rdy_queue;
	spinlock_t		rdy_spinlock;
	u8			num_rdy;
	bool			buffered;
};

/**
 * struct v4l2_m2m_ctx - Memory to memory context structure
 *
 * @q_lock: struct &mutex lock
 * @m2m_dev: opaque pointer to the internal data to handle M2M context
 * @cap_q_ctx: Capture (output to memory) queue context
 * @out_q_ctx: Output (input from memory) queue context
 * @queue: List of memory to memory contexts
 * @job_flags: Job queue flags, used internally by v4l2-mem2mem.c:
 *		%TRANS_QUEUED, %TRANS_RUNNING and %TRANS_ABORT.
 * @finished: Wait queue used to signalize when a job queue finished.
 * @priv: Instance private data
 *
 * The memory to memory context is specific to a file handle, NOT to e.g.
 * a device.
 */
struct v4l2_m2m_ctx {
	/* optional cap/out vb2 queues lock */
	struct mutex			*q_lock;

	/* internal use only */
	struct v4l2_m2m_dev		*m2m_dev;

	struct v4l2_m2m_queue_ctx	cap_q_ctx;

	struct v4l2_m2m_queue_ctx	out_q_ctx;

	/* For device job queue */
	struct list_head		queue;
	unsigned long			job_flags;
	wait_queue_head_t		finished;

	void				*priv;
};

/**
 * struct v4l2_m2m_buffer - Memory to memory buffer
 *
 * @vb: pointer to struct &vb2_v4l2_buffer
 * @list: list of m2m buffers
 */
struct v4l2_m2m_buffer {
	struct vb2_v4l2_buffer	vb;
	struct list_head	list;
};

/**
 * v4l2_m2m_get_curr_priv() - return driver private data for the currently
 * running instance or NULL if no instance is running
 *
 * @m2m_dev: opaque pointer to the internal data to handle M2M context
 */
void *v4l2_m2m_get_curr_priv(struct v4l2_m2m_dev *m2m_dev);

/**
 * v4l2_m2m_get_vq() - return vb2_queue for the given type
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @type: type of the V4L2 buffer, as defined by enum &v4l2_buf_type
 */
struct vb2_queue *v4l2_m2m_get_vq(struct v4l2_m2m_ctx *m2m_ctx,
				       enum v4l2_buf_type type);

/**
 * v4l2_m2m_try_schedule() - check whether an instance is ready to be added to
 * the pending job queue and add it if so.
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 *
 * There are three basic requirements an instance has to meet to be able to run:
 * 1) at least one source buffer has to be queued,
 * 2) at least one destination buffer has to be queued,
 * 3) streaming has to be on.
 *
 * If a queue is buffered (for example a decoder hardware ringbuffer that has
 * to be drained before doing streamoff), allow scheduling without v4l2 buffers
 * on that queue.
 *
 * There may also be additional, custom requirements. In such case the driver
 * should supply a custom callback (job_ready in v4l2_m2m_ops) that should
 * return 1 if the instance is ready.
 * An example of the above could be an instance that requires more than one
 * src/dst buffer per transaction.
 */
void v4l2_m2m_try_schedule(struct v4l2_m2m_ctx *m2m_ctx);

/**
 * v4l2_m2m_job_finish() - inform the framework that a job has been finished
 * and have it clean up
 *
 * @m2m_dev: opaque pointer to the internal data to handle M2M context
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 *
 * Called by a driver to yield back the device after it has finished with it.
 * Should be called as soon as possible after reaching a state which allows
 * other instances to take control of the device.
 *
 * This function has to be called only after &v4l2_m2m_ops->device_run
 * callback has been called on the driver. To prevent recursion, it should
 * not be called directly from the &v4l2_m2m_ops->device_run callback though.
 */
void v4l2_m2m_job_finish(struct v4l2_m2m_dev *m2m_dev,
			 struct v4l2_m2m_ctx *m2m_ctx);

static inline void
v4l2_m2m_buf_done(struct vb2_v4l2_buffer *buf, enum vb2_buffer_state state)
{
	vb2_buffer_done(&buf->vb2_buf, state);
}

/**
 * v4l2_m2m_reqbufs() - multi-queue-aware REQBUFS multiplexer
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @reqbufs: pointer to struct &v4l2_requestbuffers
 */
int v4l2_m2m_reqbufs(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		     struct v4l2_requestbuffers *reqbufs);

/**
 * v4l2_m2m_querybuf() - multi-queue-aware QUERYBUF multiplexer
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @buf: pointer to struct &v4l2_buffer
 *
 * See v4l2_m2m_mmap() documentation for details.
 */
int v4l2_m2m_querybuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		      struct v4l2_buffer *buf);

/**
 * v4l2_m2m_qbuf() - enqueue a source or destination buffer, depending on
 * the type
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @buf: pointer to struct &v4l2_buffer
 */
int v4l2_m2m_qbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		  struct v4l2_buffer *buf);

/**
 * v4l2_m2m_dqbuf() - dequeue a source or destination buffer, depending on
 * the type
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @buf: pointer to struct &v4l2_buffer
 */
int v4l2_m2m_dqbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		   struct v4l2_buffer *buf);

/**
 * v4l2_m2m_prepare_buf() - prepare a source or destination buffer, depending on
 * the type
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @buf: pointer to struct &v4l2_buffer
 */
int v4l2_m2m_prepare_buf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			 struct v4l2_buffer *buf);

/**
 * v4l2_m2m_create_bufs() - create a source or destination buffer, depending
 * on the type
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @create: pointer to struct &v4l2_create_buffers
 */
int v4l2_m2m_create_bufs(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			 struct v4l2_create_buffers *create);

/**
 * v4l2_m2m_expbuf() - export a source or destination buffer, depending on
 * the type
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @eb: pointer to struct &v4l2_exportbuffer
 */
int v4l2_m2m_expbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		   struct v4l2_exportbuffer *eb);

/**
 * v4l2_m2m_streamon() - turn on streaming for a video queue
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @type: type of the V4L2 buffer, as defined by enum &v4l2_buf_type
 */
int v4l2_m2m_streamon(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		      enum v4l2_buf_type type);

/**
 * v4l2_m2m_streamoff() - turn off streaming for a video queue
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @type: type of the V4L2 buffer, as defined by enum &v4l2_buf_type
 */
int v4l2_m2m_streamoff(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		       enum v4l2_buf_type type);

/**
 * v4l2_m2m_poll() - poll replacement, for destination buffers only
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @wait: pointer to struct &poll_table_struct
 *
 * Call from the driver's poll() function. Will poll both queues. If a buffer
 * is available to dequeue (with dqbuf) from the source queue, this will
 * indicate that a non-blocking write can be performed, while read will be
 * returned in case of the destination queue.
 */
__poll_t v4l2_m2m_poll(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			   struct poll_table_struct *wait);

/**
 * v4l2_m2m_mmap() - source and destination queues-aware mmap multiplexer
 *
 * @file: pointer to struct &file
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @vma: pointer to struct &vm_area_struct
 *
 * Call from driver's mmap() function. Will handle mmap() for both queues
 * seamlessly for videobuffer, which will receive normal per-queue offsets and
 * proper videobuf queue pointers. The differentiation is made outside videobuf
 * by adding a predefined offset to buffers from one of the queues and
 * subtracting it before passing it back to videobuf. Only drivers (and
 * thus applications) receive modified offsets.
 */
int v4l2_m2m_mmap(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		  struct vm_area_struct *vma);

/**
 * v4l2_m2m_init() - initialize per-driver m2m data
 *
 * @m2m_ops: pointer to struct v4l2_m2m_ops
 *
 * Usually called from driver's ``probe()`` function.
 *
 * Return: returns an opaque pointer to the internal data to handle M2M context
 */
struct v4l2_m2m_dev *v4l2_m2m_init(const struct v4l2_m2m_ops *m2m_ops);

#if defined(CONFIG_MEDIA_CONTROLLER)
void v4l2_m2m_unregister_media_controller(struct v4l2_m2m_dev *m2m_dev);
int v4l2_m2m_register_media_controller(struct v4l2_m2m_dev *m2m_dev,
			struct video_device *vdev, int function);
#else
static inline void
v4l2_m2m_unregister_media_controller(struct v4l2_m2m_dev *m2m_dev)
{
}

static inline int
v4l2_m2m_register_media_controller(struct v4l2_m2m_dev *m2m_dev,
		struct video_device *vdev, int function)
{
	return 0;
}
#endif

/**
 * v4l2_m2m_release() - cleans up and frees a m2m_dev structure
 *
 * @m2m_dev: opaque pointer to the internal data to handle M2M context
 *
 * Usually called from driver's ``remove()`` function.
 */
void v4l2_m2m_release(struct v4l2_m2m_dev *m2m_dev);

/**
 * v4l2_m2m_ctx_init() - allocate and initialize a m2m context
 *
 * @m2m_dev: opaque pointer to the internal data to handle M2M context
 * @drv_priv: driver's instance private data
 * @queue_init: a callback for queue type-specific initialization function
 *	to be used for initializing videobuf_queues
 *
 * Usually called from driver's ``open()`` function.
 */
struct v4l2_m2m_ctx *v4l2_m2m_ctx_init(struct v4l2_m2m_dev *m2m_dev,
		void *drv_priv,
		int (*queue_init)(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq));

static inline void v4l2_m2m_set_src_buffered(struct v4l2_m2m_ctx *m2m_ctx,
					     bool buffered)
{
	m2m_ctx->out_q_ctx.buffered = buffered;
}

static inline void v4l2_m2m_set_dst_buffered(struct v4l2_m2m_ctx *m2m_ctx,
					     bool buffered)
{
	m2m_ctx->cap_q_ctx.buffered = buffered;
}

/**
 * v4l2_m2m_ctx_release() - release m2m context
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 *
 * Usually called from driver's release() function.
 */
void v4l2_m2m_ctx_release(struct v4l2_m2m_ctx *m2m_ctx);

/**
 * v4l2_m2m_buf_queue() - add a buffer to the proper ready buffers list.
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @vbuf: pointer to struct &vb2_v4l2_buffer
 *
 * Call from videobuf_queue_ops->ops->buf_queue, videobuf_queue_ops callback.
 */
void v4l2_m2m_buf_queue(struct v4l2_m2m_ctx *m2m_ctx,
			struct vb2_v4l2_buffer *vbuf);

/**
 * v4l2_m2m_num_src_bufs_ready() - return the number of source buffers ready for
 * use
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline
unsigned int v4l2_m2m_num_src_bufs_ready(struct v4l2_m2m_ctx *m2m_ctx)
{
	return m2m_ctx->out_q_ctx.num_rdy;
}

/**
 * v4l2_m2m_num_dst_bufs_ready() - return the number of destination buffers
 * ready for use
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline
unsigned int v4l2_m2m_num_dst_bufs_ready(struct v4l2_m2m_ctx *m2m_ctx)
{
	return m2m_ctx->cap_q_ctx.num_rdy;
}

/**
 * v4l2_m2m_next_buf() - return next buffer from the list of ready buffers
 *
 * @q_ctx: pointer to struct @v4l2_m2m_queue_ctx
 */
struct vb2_v4l2_buffer *v4l2_m2m_next_buf(struct v4l2_m2m_queue_ctx *q_ctx);

/**
 * v4l2_m2m_next_src_buf() - return next source buffer from the list of ready
 * buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline struct vb2_v4l2_buffer *
v4l2_m2m_next_src_buf(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_next_buf(&m2m_ctx->out_q_ctx);
}

/**
 * v4l2_m2m_next_dst_buf() - return next destination buffer from the list of
 * ready buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline struct vb2_v4l2_buffer *
v4l2_m2m_next_dst_buf(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_next_buf(&m2m_ctx->cap_q_ctx);
}

/**
 * v4l2_m2m_last_buf() - return last buffer from the list of ready buffers
 *
 * @q_ctx: pointer to struct @v4l2_m2m_queue_ctx
 */
struct vb2_v4l2_buffer *v4l2_m2m_last_buf(struct v4l2_m2m_queue_ctx *q_ctx);

/**
 * v4l2_m2m_last_src_buf() - return last destination buffer from the list of
 * ready buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline struct vb2_v4l2_buffer *
v4l2_m2m_last_src_buf(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_last_buf(&m2m_ctx->out_q_ctx);
}

/**
 * v4l2_m2m_last_dst_buf() - return last destination buffer from the list of
 * ready buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline struct vb2_v4l2_buffer *
v4l2_m2m_last_dst_buf(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_last_buf(&m2m_ctx->cap_q_ctx);
}

/**
 * v4l2_m2m_for_each_dst_buf() - iterate over a list of destination ready
 * buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @b: current buffer of type struct v4l2_m2m_buffer
 */
#define v4l2_m2m_for_each_dst_buf(m2m_ctx, b)	\
	list_for_each_entry(b, &m2m_ctx->cap_q_ctx.rdy_queue, list)

/**
 * v4l2_m2m_for_each_src_buf() - iterate over a list of source ready buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @b: current buffer of type struct v4l2_m2m_buffer
 */
#define v4l2_m2m_for_each_src_buf(m2m_ctx, b)	\
	list_for_each_entry(b, &m2m_ctx->out_q_ctx.rdy_queue, list)

/**
 * v4l2_m2m_for_each_dst_buf_safe() - iterate over a list of destination ready
 * buffers safely
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @b: current buffer of type struct v4l2_m2m_buffer
 * @n: used as temporary storage
 */
#define v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, b, n)	\
	list_for_each_entry_safe(b, n, &m2m_ctx->cap_q_ctx.rdy_queue, list)

/**
 * v4l2_m2m_for_each_src_buf_safe() - iterate over a list of source ready
 * buffers safely
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @b: current buffer of type struct v4l2_m2m_buffer
 * @n: used as temporary storage
 */
#define v4l2_m2m_for_each_src_buf_safe(m2m_ctx, b, n)	\
	list_for_each_entry_safe(b, n, &m2m_ctx->out_q_ctx.rdy_queue, list)

/**
 * v4l2_m2m_get_src_vq() - return vb2_queue for source buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline
struct vb2_queue *v4l2_m2m_get_src_vq(struct v4l2_m2m_ctx *m2m_ctx)
{
	return &m2m_ctx->out_q_ctx.q;
}

/**
 * v4l2_m2m_get_dst_vq() - return vb2_queue for destination buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline
struct vb2_queue *v4l2_m2m_get_dst_vq(struct v4l2_m2m_ctx *m2m_ctx)
{
	return &m2m_ctx->cap_q_ctx.q;
}

/**
 * v4l2_m2m_buf_remove() - take off a buffer from the list of ready buffers and
 * return it
 *
 * @q_ctx: pointer to struct @v4l2_m2m_queue_ctx
 */
struct vb2_v4l2_buffer *v4l2_m2m_buf_remove(struct v4l2_m2m_queue_ctx *q_ctx);

/**
 * v4l2_m2m_src_buf_remove() - take off a source buffer from the list of ready
 * buffers and return it
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline struct vb2_v4l2_buffer *
v4l2_m2m_src_buf_remove(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_buf_remove(&m2m_ctx->out_q_ctx);
}

/**
 * v4l2_m2m_dst_buf_remove() - take off a destination buffer from the list of
 * ready buffers and return it
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 */
static inline struct vb2_v4l2_buffer *
v4l2_m2m_dst_buf_remove(struct v4l2_m2m_ctx *m2m_ctx)
{
	return v4l2_m2m_buf_remove(&m2m_ctx->cap_q_ctx);
}

/**
 * v4l2_m2m_buf_remove_by_buf() - take off exact buffer from the list of ready
 * buffers
 *
 * @q_ctx: pointer to struct @v4l2_m2m_queue_ctx
 * @vbuf: the buffer to be removed
 */
void v4l2_m2m_buf_remove_by_buf(struct v4l2_m2m_queue_ctx *q_ctx,
				struct vb2_v4l2_buffer *vbuf);

/**
 * v4l2_m2m_src_buf_remove_by_buf() - take off exact source buffer from the list
 * of ready buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @vbuf: the buffer to be removed
 */
static inline void v4l2_m2m_src_buf_remove_by_buf(struct v4l2_m2m_ctx *m2m_ctx,
						  struct vb2_v4l2_buffer *vbuf)
{
	v4l2_m2m_buf_remove_by_buf(&m2m_ctx->out_q_ctx, vbuf);
}

/**
 * v4l2_m2m_dst_buf_remove_by_buf() - take off exact destination buffer from the
 * list of ready buffers
 *
 * @m2m_ctx: m2m context assigned to the instance given by struct &v4l2_m2m_ctx
 * @vbuf: the buffer to be removed
 */
static inline void v4l2_m2m_dst_buf_remove_by_buf(struct v4l2_m2m_ctx *m2m_ctx,
						  struct vb2_v4l2_buffer *vbuf)
{
	v4l2_m2m_buf_remove_by_buf(&m2m_ctx->cap_q_ctx, vbuf);
}

struct vb2_v4l2_buffer *
v4l2_m2m_buf_remove_by_idx(struct v4l2_m2m_queue_ctx *q_ctx, unsigned int idx);

static inline struct vb2_v4l2_buffer *
v4l2_m2m_src_buf_remove_by_idx(struct v4l2_m2m_ctx *m2m_ctx, unsigned int idx)
{
	return v4l2_m2m_buf_remove_by_idx(&m2m_ctx->out_q_ctx, idx);
}

static inline struct vb2_v4l2_buffer *
v4l2_m2m_dst_buf_remove_by_idx(struct v4l2_m2m_ctx *m2m_ctx, unsigned int idx)
{
	return v4l2_m2m_buf_remove_by_idx(&m2m_ctx->cap_q_ctx, idx);
}

/**
 * v4l2_m2m_buf_copy_metadata() - copy buffer metadata from
 * the output buffer to the capture buffer
 *
 * @out_vb: the output buffer that is the source of the metadata.
 * @cap_vb: the capture buffer that will receive the metadata.
 * @copy_frame_flags: copy the KEY/B/PFRAME flags as well.
 *
 * This helper function copies the timestamp, timecode (if the TIMECODE
 * buffer flag was set), field and the TIMECODE, KEYFRAME, BFRAME, PFRAME
 * and TSTAMP_SRC_MASK flags from @out_vb to @cap_vb.
 *
 * If @copy_frame_flags is false, then the KEYFRAME, BFRAME and PFRAME
 * flags are not copied. This is typically needed for encoders that
 * set this bits explicitly.
 */
void v4l2_m2m_buf_copy_metadata(const struct vb2_v4l2_buffer *out_vb,
				struct vb2_v4l2_buffer *cap_vb,
				bool copy_frame_flags);

/* v4l2 request helper */

void v4l2_m2m_request_queue(struct media_request *req);

/* v4l2 ioctl helpers */

int v4l2_m2m_ioctl_reqbufs(struct file *file, void *priv,
				struct v4l2_requestbuffers *rb);
int v4l2_m2m_ioctl_create_bufs(struct file *file, void *fh,
				struct v4l2_create_buffers *create);
int v4l2_m2m_ioctl_querybuf(struct file *file, void *fh,
				struct v4l2_buffer *buf);
int v4l2_m2m_ioctl_expbuf(struct file *file, void *fh,
				struct v4l2_exportbuffer *eb);
int v4l2_m2m_ioctl_qbuf(struct file *file, void *fh,
				struct v4l2_buffer *buf);
int v4l2_m2m_ioctl_dqbuf(struct file *file, void *fh,
				struct v4l2_buffer *buf);
int v4l2_m2m_ioctl_prepare_buf(struct file *file, void *fh,
			       struct v4l2_buffer *buf);
int v4l2_m2m_ioctl_streamon(struct file *file, void *fh,
				enum v4l2_buf_type type);
int v4l2_m2m_ioctl_streamoff(struct file *file, void *fh,
				enum v4l2_buf_type type);
int v4l2_m2m_fop_mmap(struct file *file, struct vm_area_struct *vma);
__poll_t v4l2_m2m_fop_poll(struct file *file, poll_table *wait);

#endif /* _MEDIA_V4L2_MEM2MEM_H */

