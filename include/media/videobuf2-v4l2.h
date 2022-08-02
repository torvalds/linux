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

struct video_device;

/**
 * struct vb2_v4l2_buffer - video buffer information for v4l2.
 *
 * @vb2_buf:	embedded struct &vb2_buffer.
 * @flags:	buffer informational flags.
 * @field:	field order of the image in the buffer, as defined by
 *		&enum v4l2_field.
 * @timecode:	frame timecode.
 * @sequence:	sequence count of this frame.
 * @request_fd:	the request_fd associated with this buffer
 * @is_held:	if true, then this capture buffer was held
 * @planes:	plane information (userptr/fd, length, bytesused, data_offset).
 *
 * Should contain enough information to be able to cover all the fields
 * of &struct v4l2_buffer at ``videodev2.h``.
 */
struct vb2_v4l2_buffer {
	struct vb2_buffer	vb2_buf;

	__u32			flags;
	__u32			field;
	struct v4l2_timecode	timecode;
	__u32			sequence;
	__s32			request_fd;
	bool			is_held;
	struct vb2_plane	planes[VB2_MAX_PLANES];
};

/* VB2 V4L2 flags as set in vb2_queue.subsystem_flags */
#define VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF (1 << 0)

/*
 * to_vb2_v4l2_buffer() - cast struct vb2_buffer * to struct vb2_v4l2_buffer *
 */
#define to_vb2_v4l2_buffer(vb) \
	container_of(vb, struct vb2_v4l2_buffer, vb2_buf)

/**
 * vb2_find_timestamp() - Find buffer with given timestamp in the queue
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @timestamp:	the timestamp to find.
 * @start_idx:	the start index (usually 0) in the buffer array to start
 *		searching from. Note that there may be multiple buffers
 *		with the same timestamp value, so you can restart the search
 *		by setting @start_idx to the previously found index + 1.
 *
 * Returns the buffer index of the buffer with the given @timestamp, or
 * -1 if no buffer with @timestamp was found.
 */
int vb2_find_timestamp(const struct vb2_queue *q, u64 timestamp,
		       unsigned int start_idx);

int vb2_querybuf(struct vb2_queue *q, struct v4l2_buffer *b);

/**
 * vb2_reqbufs() - Wrapper for vb2_core_reqbufs() that also verifies
 * the memory and type values.
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @req:	&struct v4l2_requestbuffers passed from userspace to
 *		&v4l2_ioctl_ops->vidioc_reqbufs handler in driver.
 */
int vb2_reqbufs(struct vb2_queue *q, struct v4l2_requestbuffers *req);

/**
 * vb2_create_bufs() - Wrapper for vb2_core_create_bufs() that also verifies
 * the memory and type values.
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @create:	creation parameters, passed from userspace to
 *		&v4l2_ioctl_ops->vidioc_create_bufs handler in driver
 */
int vb2_create_bufs(struct vb2_queue *q, struct v4l2_create_buffers *create);

/**
 * vb2_prepare_buf() - Pass ownership of a buffer from userspace to the kernel
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @mdev:	pointer to &struct media_device, may be NULL.
 * @b:		buffer structure passed from userspace to
 *		&v4l2_ioctl_ops->vidioc_prepare_buf handler in driver
 *
 * Should be called from &v4l2_ioctl_ops->vidioc_prepare_buf ioctl handler
 * of a driver.
 *
 * This function:
 *
 * #) verifies the passed buffer,
 * #) calls &vb2_ops->buf_prepare callback in the driver (if provided),
 *    in which driver-specific buffer initialization can be performed.
 * #) if @b->request_fd is non-zero and @mdev->ops->req_queue is set,
 *    then bind the prepared buffer to the request.
 *
 * The return values from this function are intended to be directly returned
 * from &v4l2_ioctl_ops->vidioc_prepare_buf handler in driver.
 */
int vb2_prepare_buf(struct vb2_queue *q, struct media_device *mdev,
		    struct v4l2_buffer *b);

/**
 * vb2_qbuf() - Queue a buffer from userspace
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @mdev:	pointer to &struct media_device, may be NULL.
 * @b:		buffer structure passed from userspace to
 *		&v4l2_ioctl_ops->vidioc_qbuf handler in driver
 *
 * Should be called from &v4l2_ioctl_ops->vidioc_qbuf handler of a driver.
 *
 * This function:
 *
 * #) verifies the passed buffer;
 * #) if @b->request_fd is non-zero and @mdev->ops->req_queue is set,
 *    then bind the buffer to the request.
 * #) if necessary, calls &vb2_ops->buf_prepare callback in the driver
 *    (if provided), in which driver-specific buffer initialization can
 *    be performed;
 * #) if streaming is on, queues the buffer in driver by the means of
 *    &vb2_ops->buf_queue callback for processing.
 *
 * The return values from this function are intended to be directly returned
 * from &v4l2_ioctl_ops->vidioc_qbuf handler in driver.
 */
int vb2_qbuf(struct vb2_queue *q, struct media_device *mdev,
	     struct v4l2_buffer *b);

/**
 * vb2_expbuf() - Export a buffer as a file descriptor
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @eb:		export buffer structure passed from userspace to
 *		&v4l2_ioctl_ops->vidioc_expbuf handler in driver
 *
 * The return values from this function are intended to be directly returned
 * from &v4l2_ioctl_ops->vidioc_expbuf handler in driver.
 */
int vb2_expbuf(struct vb2_queue *q, struct v4l2_exportbuffer *eb);

/**
 * vb2_dqbuf() - Dequeue a buffer to the userspace
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @b:		buffer structure passed from userspace to
 *		&v4l2_ioctl_ops->vidioc_dqbuf handler in driver
 * @nonblocking: if true, this call will not sleep waiting for a buffer if no
 *		 buffers ready for dequeuing are present. Normally the driver
 *		 would be passing (&file->f_flags & %O_NONBLOCK) here
 *
 * Should be called from &v4l2_ioctl_ops->vidioc_dqbuf ioctl handler
 * of a driver.
 *
 * This function:
 *
 * #) verifies the passed buffer;
 * #) calls &vb2_ops->buf_finish callback in the driver (if provided), in which
 *    driver can perform any additional operations that may be required before
 *    returning the buffer to userspace, such as cache sync;
 * #) the buffer struct members are filled with relevant information for
 *    the userspace.
 *
 * The return values from this function are intended to be directly returned
 * from &v4l2_ioctl_ops->vidioc_dqbuf handler in driver.
 */
int vb2_dqbuf(struct vb2_queue *q, struct v4l2_buffer *b, bool nonblocking);

/**
 * vb2_streamon - start streaming
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @type:	type argument passed from userspace to vidioc_streamon handler,
 *		as defined by &enum v4l2_buf_type.
 *
 * Should be called from &v4l2_ioctl_ops->vidioc_streamon handler of a driver.
 *
 * This function:
 *
 * 1) verifies current state
 * 2) passes any previously queued buffers to the driver and starts streaming
 *
 * The return values from this function are intended to be directly returned
 * from &v4l2_ioctl_ops->vidioc_streamon handler in the driver.
 */
int vb2_streamon(struct vb2_queue *q, enum v4l2_buf_type type);

/**
 * vb2_streamoff - stop streaming
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @type:	type argument passed from userspace to vidioc_streamoff handler
 *
 * Should be called from vidioc_streamoff handler of a driver.
 *
 * This function:
 *
 * #) verifies current state,
 * #) stop streaming and dequeues any queued buffers, including those previously
 *    passed to the driver (after waiting for the driver to finish).
 *
 * This call can be used for pausing playback.
 * The return values from this function are intended to be directly returned
 * from vidioc_streamoff handler in the driver
 */
int vb2_streamoff(struct vb2_queue *q, enum v4l2_buf_type type);

/**
 * vb2_queue_init() - initialize a videobuf2 queue
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *
 * The vb2_queue structure should be allocated by the driver. The driver is
 * responsible of clearing it's content and setting initial values for some
 * required entries before calling this function.
 * q->ops, q->mem_ops, q->type and q->io_modes are mandatory. Please refer
 * to the struct vb2_queue description in include/media/videobuf2-core.h
 * for more information.
 */
int __must_check vb2_queue_init(struct vb2_queue *q);

/**
 * vb2_queue_init_name() - initialize a videobuf2 queue with a name
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @name:	the queue name
 *
 * This function initializes the vb2_queue exactly like vb2_queue_init(),
 * and additionally sets the queue name. The queue name is used for logging
 * purpose, and should uniquely identify the queue within the context of the
 * device it belongs to. This is useful to attribute kernel log messages to the
 * right queue for m2m devices or other devices that handle multiple queues.
 */
int __must_check vb2_queue_init_name(struct vb2_queue *q, const char *name);

/**
 * vb2_queue_release() - stop streaming, release the queue and free memory
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *
 * This function stops streaming and performs necessary clean ups, including
 * freeing video buffer memory. The driver is responsible for freeing
 * the vb2_queue structure itself.
 */
void vb2_queue_release(struct vb2_queue *q);

/**
 * vb2_queue_change_type() - change the type of an inactive vb2_queue
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @type:	the type to change to (V4L2_BUF_TYPE_VIDEO_*)
 *
 * This function changes the type of the vb2_queue. This is only possible
 * if the queue is not busy (i.e. no buffers have been allocated).
 *
 * vb2_queue_change_type() can be used to support multiple buffer types using
 * the same queue. The driver can implement v4l2_ioctl_ops.vidioc_reqbufs and
 * v4l2_ioctl_ops.vidioc_create_bufs functions and call vb2_queue_change_type()
 * before calling vb2_ioctl_reqbufs() or vb2_ioctl_create_bufs(), and thus
 * "lock" the buffer type until the buffers have been released.
 */
int vb2_queue_change_type(struct vb2_queue *q, unsigned int type);

/**
 * vb2_poll() - implements poll userspace operation
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @file:	file argument passed to the poll file operation handler
 * @wait:	wait argument passed to the poll file operation handler
 *
 * This function implements poll file operation handler for a driver.
 * For CAPTURE queues, if a buffer is ready to be dequeued, the userspace will
 * be informed that the file descriptor of a video device is available for
 * reading.
 * For OUTPUT queues, if a buffer is ready to be dequeued, the file descriptor
 * will be reported as available for writing.
 *
 * If the driver uses struct v4l2_fh, then vb2_poll() will also check for any
 * pending events.
 *
 * The return values from this function are intended to be directly returned
 * from poll handler in driver.
 */
__poll_t vb2_poll(struct vb2_queue *q, struct file *file, poll_table *wait);

/*
 * The following functions are not part of the vb2 core API, but are simple
 * helper functions that you can use in your struct v4l2_file_operations,
 * struct v4l2_ioctl_ops and struct vb2_ops. They will serialize if vb2_queue->lock
 * or video_device->lock is set, and they will set and test the queue owner
 * (vb2_queue->owner) to check if the calling filehandle is permitted to do the
 * queuing operation.
 */

/**
 * vb2_queue_is_busy() - check if the queue is busy
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @file:	file through which the vb2 queue access is performed
 *
 * The queue is considered busy if it has an owner and the owner is not the
 * @file.
 *
 * Queue ownership is acquired and checked by some of the v4l2_ioctl_ops helpers
 * below. Drivers can also use this function directly when they need to
 * open-code ioctl handlers, for instance to add additional checks between the
 * queue ownership test and the call to the corresponding vb2 operation.
 */
static inline bool vb2_queue_is_busy(struct vb2_queue *q, struct file *file)
{
	return q->owner && q->owner != file->private_data;
}

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
__poll_t vb2_fop_poll(struct file *file, poll_table *wait);
#ifndef CONFIG_MMU
unsigned long vb2_fop_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);
#endif

/**
 * vb2_video_unregister_device - unregister the video device and release queue
 *
 * @vdev: pointer to &struct video_device
 *
 * If the driver uses vb2_fop_release()/_vb2_fop_release(), then it should use
 * vb2_video_unregister_device() instead of video_unregister_device().
 *
 * This function will call video_unregister_device() and then release the
 * vb2_queue if streaming is in progress. This will stop streaming and
 * this will simplify the unbind sequence since after this call all subdevs
 * will have stopped streaming as well.
 */
void vb2_video_unregister_device(struct video_device *vdev);

/**
 * vb2_ops_wait_prepare - helper function to lock a struct &vb2_queue
 *
 * @vq: pointer to &struct vb2_queue
 *
 * ..note:: only use if vq->lock is non-NULL.
 */
void vb2_ops_wait_prepare(struct vb2_queue *vq);

/**
 * vb2_ops_wait_finish - helper function to unlock a struct &vb2_queue
 *
 * @vq: pointer to &struct vb2_queue
 *
 * ..note:: only use if vq->lock is non-NULL.
 */
void vb2_ops_wait_finish(struct vb2_queue *vq);

struct media_request;
int vb2_request_validate(struct media_request *req);
void vb2_request_queue(struct media_request *req);

#endif /* _MEDIA_VIDEOBUF2_V4L2_H */
