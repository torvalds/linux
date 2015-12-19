/*
 * videobuf2-v4l2.h - V4L2 driver helper framework
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Pawel Osciak <pawel@osciak.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#ifndef _MEDIA_VIDEOBUF2_V4L2_H
#define _MEDIA_VIDEOBUF2_V4L2_H

#include <linux/videodev2.h>
#include <media/videobuf2-core.h>

#if VB2_MAX_FRAME != VIDEO_MAX_FRAME
#error VB2_MAX_FRAME != VIDEO_MAX_FRAME
#endif

#if VB2_MAX_PLANES != VIDEO_MAX_PLANES
#error VB2_MAX_PLANES != VIDEO_MAX_PLANES
#endif

/**
 * struct vb2_v4l2_buffer - video buffer information for v4l2
 * @vb2_buf:	video buffer 2
 * @flags:	buffer informational flags
 * @field:	enum v4l2_field; field order of the image in the buffer
 * @timestamp:	frame timestamp
 * @timecode:	frame timecode
 * @sequence:	sequence count of this frame
 * Should contain enough information to be able to cover all the fields
 * of struct v4l2_buffer at videodev2.h
 */
struct vb2_v4l2_buffer {
	struct vb2_buffer	vb2_buf;

	__u32			flags;
	__u32			field;
	struct timeval		timestamp;
	struct v4l2_timecode	timecode;
	__u32			sequence;
};

/*
 * to_vb2_v4l2_buffer() - cast struct vb2_buffer * to struct vb2_v4l2_buffer *
 */
#define to_vb2_v4l2_buffer(vb) \
	container_of(vb, struct vb2_v4l2_buffer, vb2_buf)

int vb2_querybuf(struct vb2_queue *q, struct v4l2_buffer *b);
int vb2_reqbufs(struct vb2_queue *q, struct v4l2_requestbuffers *req);

int vb2_create_bufs(struct vb2_queue *q, struct v4l2_create_buffers *create);
int vb2_prepare_buf(struct vb2_queue *q, struct v4l2_buffer *b);

int vb2_qbuf(struct vb2_queue *q, struct v4l2_buffer *b);
int vb2_expbuf(struct vb2_queue *q, struct v4l2_exportbuffer *eb);
int vb2_dqbuf(struct vb2_queue *q, struct v4l2_buffer *b, bool nonblocking);

int vb2_streamon(struct vb2_queue *q, enum v4l2_buf_type type);
int vb2_streamoff(struct vb2_queue *q, enum v4l2_buf_type type);

int __must_check vb2_queue_init(struct vb2_queue *q);
void vb2_queue_release(struct vb2_queue *q);

unsigned int vb2_poll(struct vb2_queue *q, struct file *file, poll_table *wait);
size_t vb2_read(struct vb2_queue *q, char __user *data, size_t count,
		loff_t *ppos, int nonblock);
size_t vb2_write(struct vb2_queue *q, const char __user *data, size_t count,
		loff_t *ppos, int nonblock);

/*
 * vb2_thread_fnc - callback function for use with vb2_thread
 *
 * This is called whenever a buffer is dequeued in the thread.
 */
typedef int (*vb2_thread_fnc)(struct vb2_buffer *vb, void *priv);

/**
 * vb2_thread_start() - start a thread for the given queue.
 * @q:		videobuf queue
 * @fnc:	callback function
 * @priv:	priv pointer passed to the callback function
 * @thread_name:the name of the thread. This will be prefixed with "vb2-".
 *
 * This starts a thread that will queue and dequeue until an error occurs
 * or @vb2_thread_stop is called.
 *
 * This function should not be used for anything else but the videobuf2-dvb
 * support. If you think you have another good use-case for this, then please
 * contact the linux-media mailinglist first.
 */
int vb2_thread_start(struct vb2_queue *q, vb2_thread_fnc fnc, void *priv,
		     const char *thread_name);

/**
 * vb2_thread_stop() - stop the thread for the given queue.
 * @q:		videobuf queue
 */
int vb2_thread_stop(struct vb2_queue *q);

/*
 * The following functions are not part of the vb2 core API, but are simple
 * helper functions that you can use in your struct v4l2_file_operations,
 * struct v4l2_ioctl_ops and struct vb2_ops. They will serialize if vb2_queue->lock
 * or video_device->lock is set, and they will set and test vb2_queue->owner
 * to check if the calling filehandle is permitted to do the queuing operation.
 */

/* struct v4l2_ioctl_ops helpers */

int vb2_ioctl_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p);
int vb2_ioctl_create_bufs(struct file *file, void *priv,
			  struct v4l2_create_buffers *p);
int vb2_ioctl_prepare_buf(struct file *file, void *priv,
			  struct v4l2_buffer *p);
int vb2_ioctl_querybuf(struct file *file, void *priv, struct v4l2_buffer *p);
int vb2_ioctl_qbuf(struct file *file, void *priv, struct v4l2_buffer *p);
int vb2_ioctl_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p);
int vb2_ioctl_streamon(struct file *file, void *priv, enum v4l2_buf_type i);
int vb2_ioctl_streamoff(struct file *file, void *priv, enum v4l2_buf_type i);
int vb2_ioctl_expbuf(struct file *file, void *priv,
	struct v4l2_exportbuffer *p);

/* struct v4l2_file_operations helpers */

int vb2_fop_mmap(struct file *file, struct vm_area_struct *vma);
int vb2_fop_release(struct file *file);
int _vb2_fop_release(struct file *file, struct mutex *lock);
ssize_t vb2_fop_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos);
ssize_t vb2_fop_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos);
unsigned int vb2_fop_poll(struct file *file, poll_table *wait);
#ifndef CONFIG_MMU
unsigned long vb2_fop_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);
#endif

/* struct vb2_ops helpers, only use if vq->lock is non-NULL. */

void vb2_ops_wait_prepare(struct vb2_queue *vq);
void vb2_ops_wait_finish(struct vb2_queue *vq);

#endif /* _MEDIA_VIDEOBUF2_V4L2_H */
