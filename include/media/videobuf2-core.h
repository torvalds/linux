/*
 * videobuf2-core.h - Video Buffer 2 Core Framework
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Pawel Osciak <pawel@osciak.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#ifndef _MEDIA_VIDEOBUF2_CORE_H
#define _MEDIA_VIDEOBUF2_CORE_H

#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/dma-buf.h>
#include <linux/bitops.h>
#include <media/media-request.h>
#include <media/frame_vector.h>

#define VB2_MAX_FRAME	(64)
#define VB2_MAX_PLANES	(8)

/**
 * enum vb2_memory - type of memory model used to make the buffers visible
 *	on userspace.
 *
 * @VB2_MEMORY_UNKNOWN:	Buffer status is unknown or it is not used yet on
 *			userspace.
 * @VB2_MEMORY_MMAP:	The buffers are allocated by the Kernel and it is
 *			memory mapped via mmap() ioctl. This model is
 *			also used when the user is using the buffers via
 *			read() or write() system calls.
 * @VB2_MEMORY_USERPTR:	The buffers was allocated in userspace and it is
 *			memory mapped via mmap() ioctl.
 * @VB2_MEMORY_DMABUF:	The buffers are passed to userspace via DMA buffer.
 */
enum vb2_memory {
	VB2_MEMORY_UNKNOWN	= 0,
	VB2_MEMORY_MMAP		= 1,
	VB2_MEMORY_USERPTR	= 2,
	VB2_MEMORY_DMABUF	= 4,
};

struct vb2_fileio_data;
struct vb2_threadio_data;

/**
 * struct vb2_mem_ops - memory handling/memory allocator operations.
 * @alloc:	allocate video memory and, optionally, allocator private data,
 *		return ERR_PTR() on failure or a pointer to allocator private,
 *		per-buffer data on success; the returned private structure
 *		will then be passed as @buf_priv argument to other ops in this
 *		structure. Additional gfp_flags to use when allocating the
 *		are also passed to this operation. These flags are from the
 *		gfp_flags field of vb2_queue. The size argument to this function
 *		shall be *page aligned*.
 * @put:	inform the allocator that the buffer will no longer be used;
 *		usually will result in the allocator freeing the buffer (if
 *		no other users of this buffer are present); the @buf_priv
 *		argument is the allocator private per-buffer structure
 *		previously returned from the alloc callback.
 * @get_dmabuf: acquire userspace memory for a hardware operation; used for
 *		 DMABUF memory types.
 * @get_userptr: acquire userspace memory for a hardware operation; used for
 *		 USERPTR memory types; vaddr is the address passed to the
 *		 videobuf layer when queuing a video buffer of USERPTR type;
 *		 should return an allocator private per-buffer structure
 *		 associated with the buffer on success, ERR_PTR() on failure;
 *		 the returned private structure will then be passed as @buf_priv
 *		 argument to other ops in this structure.
 * @put_userptr: inform the allocator that a USERPTR buffer will no longer
 *		 be used.
 * @attach_dmabuf: attach a shared &struct dma_buf for a hardware operation;
 *		   used for DMABUF memory types; dev is the alloc device
 *		   dbuf is the shared dma_buf; returns ERR_PTR() on failure;
 *		   allocator private per-buffer structure on success;
 *		   this needs to be used for further accesses to the buffer.
 * @detach_dmabuf: inform the exporter of the buffer that the current DMABUF
 *		   buffer is no longer used; the @buf_priv argument is the
 *		   allocator private per-buffer structure previously returned
 *		   from the attach_dmabuf callback.
 * @map_dmabuf: request for access to the dmabuf from allocator; the allocator
 *		of dmabuf is informed that this driver is going to use the
 *		dmabuf.
 * @unmap_dmabuf: releases access control to the dmabuf - allocator is notified
 *		  that this driver is done using the dmabuf for now.
 * @prepare:	called every time the buffer is passed from userspace to the
 *		driver, useful for cache synchronisation, optional.
 * @finish:	called every time the buffer is passed back from the driver
 *		to the userspace, also optional.
 * @vaddr:	return a kernel virtual address to a given memory buffer
 *		associated with the passed private structure or NULL if no
 *		such mapping exists.
 * @cookie:	return allocator specific cookie for a given memory buffer
 *		associated with the passed private structure or NULL if not
 *		available.
 * @num_users:	return the current number of users of a memory buffer;
 *		return 1 if the videobuf layer (or actually the driver using
 *		it) is the only user.
 * @mmap:	setup a userspace mapping for a given memory buffer under
 *		the provided virtual memory region.
 *
 * Those operations are used by the videobuf2 core to implement the memory
 * handling/memory allocators for each type of supported streaming I/O method.
 *
 * .. note::
 *    #) Required ops for USERPTR types: get_userptr, put_userptr.
 *
 *    #) Required ops for MMAP types: alloc, put, num_users, mmap.
 *
 *    #) Required ops for read/write access types: alloc, put, num_users, vaddr.
 *
 *    #) Required ops for DMABUF types: attach_dmabuf, detach_dmabuf,
 *       map_dmabuf, unmap_dmabuf.
 */
struct vb2_mem_ops {
	void		*(*alloc)(struct device *dev, unsigned long attrs,
				  unsigned long size,
				  enum dma_data_direction dma_dir,
				  gfp_t gfp_flags);
	void		(*put)(void *buf_priv);
	struct dma_buf *(*get_dmabuf)(void *buf_priv, unsigned long flags);

	void		*(*get_userptr)(struct device *dev, unsigned long vaddr,
					unsigned long size,
					enum dma_data_direction dma_dir);
	void		(*put_userptr)(void *buf_priv);

	void		(*prepare)(void *buf_priv);
	void		(*finish)(void *buf_priv);

	void		*(*attach_dmabuf)(struct device *dev,
					  struct dma_buf *dbuf,
					  unsigned long size,
					  enum dma_data_direction dma_dir);
	void		(*detach_dmabuf)(void *buf_priv);
	int		(*map_dmabuf)(void *buf_priv);
	void		(*unmap_dmabuf)(void *buf_priv);

	void		*(*vaddr)(void *buf_priv);
	void		*(*cookie)(void *buf_priv);

	unsigned int	(*num_users)(void *buf_priv);

	int		(*mmap)(void *buf_priv, struct vm_area_struct *vma);
};

/**
 * struct vb2_plane - plane information.
 * @mem_priv:	private data with this plane.
 * @dbuf:	dma_buf - shared buffer object.
 * @dbuf_mapped:	flag to show whether dbuf is mapped or not
 * @bytesused:	number of bytes occupied by data in the plane (payload).
 * @length:	size of this plane (NOT the payload) in bytes.
 * @min_length:	minimum required size of this plane (NOT the payload) in bytes.
 *		@length is always greater or equal to @min_length.
 * @m:		Union with memtype-specific data.
 * @m.offset:	when memory in the associated struct vb2_buffer is
 *		%VB2_MEMORY_MMAP, equals the offset from the start of
 *		the device memory for this plane (or is a "cookie" that
 *		should be passed to mmap() called on the video node).
 * @m.userptr:	when memory is %VB2_MEMORY_USERPTR, a userspace pointer
 *		pointing to this plane.
 * @m.fd:	when memory is %VB2_MEMORY_DMABUF, a userspace file
 *		descriptor associated with this plane.
 * @data_offset:	offset in the plane to the start of data; usually 0,
 *		unless there is a header in front of the data.
 *
 * Should contain enough information to be able to cover all the fields
 * of &struct v4l2_plane at videodev2.h.
 */
struct vb2_plane {
	void			*mem_priv;
	struct dma_buf		*dbuf;
	unsigned int		dbuf_mapped;
	unsigned int		bytesused;
	unsigned int		length;
	unsigned int		min_length;
	union {
		unsigned int	offset;
		unsigned long	userptr;
		int		fd;
	} m;
	unsigned int		data_offset;
};

/**
 * enum vb2_io_modes - queue access methods.
 * @VB2_MMAP:		driver supports MMAP with streaming API.
 * @VB2_USERPTR:	driver supports USERPTR with streaming API.
 * @VB2_READ:		driver supports read() style access.
 * @VB2_WRITE:		driver supports write() style access.
 * @VB2_DMABUF:		driver supports DMABUF with streaming API.
 */
enum vb2_io_modes {
	VB2_MMAP	= BIT(0),
	VB2_USERPTR	= BIT(1),
	VB2_READ	= BIT(2),
	VB2_WRITE	= BIT(3),
	VB2_DMABUF	= BIT(4),
};

/**
 * enum vb2_buffer_state - current video buffer state.
 * @VB2_BUF_STATE_DEQUEUED:	buffer under userspace control.
 * @VB2_BUF_STATE_IN_REQUEST:	buffer is queued in media request.
 * @VB2_BUF_STATE_PREPARING:	buffer is being prepared in videobuf.
 * @VB2_BUF_STATE_QUEUED:	buffer queued in videobuf, but not in driver.
 * @VB2_BUF_STATE_ACTIVE:	buffer queued in driver and possibly used
 *				in a hardware operation.
 * @VB2_BUF_STATE_DONE:		buffer returned from driver to videobuf, but
 *				not yet dequeued to userspace.
 * @VB2_BUF_STATE_ERROR:	same as above, but the operation on the buffer
 *				has ended with an error, which will be reported
 *				to the userspace when it is dequeued.
 */
enum vb2_buffer_state {
	VB2_BUF_STATE_DEQUEUED,
	VB2_BUF_STATE_IN_REQUEST,
	VB2_BUF_STATE_PREPARING,
	VB2_BUF_STATE_QUEUED,
	VB2_BUF_STATE_ACTIVE,
	VB2_BUF_STATE_DONE,
	VB2_BUF_STATE_ERROR,
};

struct vb2_queue;

/**
 * struct vb2_buffer - represents a video buffer.
 * @vb2_queue:		pointer to &struct vb2_queue with the queue to
 *			which this driver belongs.
 * @index:		id number of the buffer.
 * @type:		buffer type.
 * @memory:		the method, in which the actual data is passed.
 * @num_planes:		number of planes in the buffer
 *			on an internal driver queue.
 * @timestamp:		frame timestamp in ns.
 * @request:		the request this buffer is associated with.
 * @req_obj:		used to bind this buffer to a request. This
 *			request object has a refcount.
 */
struct vb2_buffer {
	struct vb2_queue	*vb2_queue;
	unsigned int		index;
	unsigned int		type;
	unsigned int		memory;
	unsigned int		num_planes;
	u64			timestamp;
	struct media_request	*request;
	struct media_request_object	req_obj;

	/* private: internal use only
	 *
	 * state:		current buffer state; do not change
	 * synced:		this buffer has been synced for DMA, i.e. the
	 *			'prepare' memop was called. It is cleared again
	 *			after the 'finish' memop is called.
	 * prepared:		this buffer has been prepared, i.e. the
	 *			buf_prepare op was called. It is cleared again
	 *			after the 'buf_finish' op is called.
	 * copied_timestamp:	the timestamp of this capture buffer was copied
	 *			from an output buffer.
	 * need_cache_sync_on_prepare: when set buffer's ->prepare() function
	 *			performs cache sync/invalidation.
	 * need_cache_sync_on_finish: when set buffer's ->finish() function
	 *			performs cache sync/invalidation.
	 * queued_entry:	entry on the queued buffers list, which holds
	 *			all buffers queued from userspace
	 * done_entry:		entry on the list that stores all buffers ready
	 *			to be dequeued to userspace
	 * vb2_plane:		per-plane information; do not change
	 */
	enum vb2_buffer_state	state;
	unsigned int		synced:1;
	unsigned int		prepared:1;
	unsigned int		copied_timestamp:1;
	unsigned int		need_cache_sync_on_prepare:1;
	unsigned int		need_cache_sync_on_finish:1;

	struct vb2_plane	planes[VB2_MAX_PLANES];
	struct list_head	queued_entry;
	struct list_head	done_entry;
#ifdef CONFIG_VIDEO_ADV_DEBUG
	/*
	 * Counters for how often these buffer-related ops are
	 * called. Used to check for unbalanced ops.
	 */
	u32		cnt_mem_alloc;
	u32		cnt_mem_put;
	u32		cnt_mem_get_dmabuf;
	u32		cnt_mem_get_userptr;
	u32		cnt_mem_put_userptr;
	u32		cnt_mem_prepare;
	u32		cnt_mem_finish;
	u32		cnt_mem_attach_dmabuf;
	u32		cnt_mem_detach_dmabuf;
	u32		cnt_mem_map_dmabuf;
	u32		cnt_mem_unmap_dmabuf;
	u32		cnt_mem_vaddr;
	u32		cnt_mem_cookie;
	u32		cnt_mem_num_users;
	u32		cnt_mem_mmap;

	u32		cnt_buf_out_validate;
	u32		cnt_buf_init;
	u32		cnt_buf_prepare;
	u32		cnt_buf_finish;
	u32		cnt_buf_cleanup;
	u32		cnt_buf_queue;
	u32		cnt_buf_request_complete;

	/* This counts the number of calls to vb2_buffer_done() */
	u32		cnt_buf_done;
#endif
};

/**
 * struct vb2_ops - driver-specific callbacks.
 *
 * These operations are not called from interrupt context except where
 * mentioned specifically.
 *
 * @queue_setup:	called from VIDIOC_REQBUFS() and VIDIOC_CREATE_BUFS()
 *			handlers before memory allocation. It can be called
 *			twice: if the original number of requested buffers
 *			could not be allocated, then it will be called a
 *			second time with the actually allocated number of
 *			buffers to verify if that is OK.
 *			The driver should return the required number of buffers
 *			in \*num_buffers, the required number of planes per
 *			buffer in \*num_planes, the size of each plane should be
 *			set in the sizes\[\] array and optional per-plane
 *			allocator specific device in the alloc_devs\[\] array.
 *			When called from VIDIOC_REQBUFS(), \*num_planes == 0,
 *			the driver has to use the currently configured format to
 *			determine the plane sizes and \*num_buffers is the total
 *			number of buffers that are being allocated. When called
 *			from VIDIOC_CREATE_BUFS(), \*num_planes != 0 and it
 *			describes the requested number of planes and sizes\[\]
 *			contains the requested plane sizes. In this case
 *			\*num_buffers are being allocated additionally to
 *			q->num_buffers. If either \*num_planes or the requested
 *			sizes are invalid callback must return %-EINVAL.
 * @wait_prepare:	release any locks taken while calling vb2 functions;
 *			it is called before an ioctl needs to wait for a new
 *			buffer to arrive; required to avoid a deadlock in
 *			blocking access type.
 * @wait_finish:	reacquire all locks released in the previous callback;
 *			required to continue operation after sleeping while
 *			waiting for a new buffer to arrive.
 * @buf_out_validate:	called when the output buffer is prepared or queued
 *			to a request; drivers can use this to validate
 *			userspace-provided information; this is required only
 *			for OUTPUT queues.
 * @buf_init:		called once after allocating a buffer (in MMAP case)
 *			or after acquiring a new USERPTR buffer; drivers may
 *			perform additional buffer-related initialization;
 *			initialization failure (return != 0) will prevent
 *			queue setup from completing successfully; optional.
 * @buf_prepare:	called every time the buffer is queued from userspace
 *			and from the VIDIOC_PREPARE_BUF() ioctl; drivers may
 *			perform any initialization required before each
 *			hardware operation in this callback; drivers can
 *			access/modify the buffer here as it is still synced for
 *			the CPU; drivers that support VIDIOC_CREATE_BUFS() must
 *			also validate the buffer size; if an error is returned,
 *			the buffer will not be queued in driver; optional.
 * @buf_finish:		called before every dequeue of the buffer back to
 *			userspace; the buffer is synced for the CPU, so drivers
 *			can access/modify the buffer contents; drivers may
 *			perform any operations required before userspace
 *			accesses the buffer; optional. The buffer state can be
 *			one of the following: %DONE and %ERROR occur while
 *			streaming is in progress, and the %PREPARED state occurs
 *			when the queue has been canceled and all pending
 *			buffers are being returned to their default %DEQUEUED
 *			state. Typically you only have to do something if the
 *			state is %VB2_BUF_STATE_DONE, since in all other cases
 *			the buffer contents will be ignored anyway.
 * @buf_cleanup:	called once before the buffer is freed; drivers may
 *			perform any additional cleanup; optional.
 * @start_streaming:	called once to enter 'streaming' state; the driver may
 *			receive buffers with @buf_queue callback
 *			before @start_streaming is called; the driver gets the
 *			number of already queued buffers in count parameter;
 *			driver can return an error if hardware fails, in that
 *			case all buffers that have been already given by
 *			the @buf_queue callback are to be returned by the driver
 *			by calling vb2_buffer_done() with %VB2_BUF_STATE_QUEUED.
 *			If you need a minimum number of buffers before you can
 *			start streaming, then set
 *			&vb2_queue->min_buffers_needed. If that is non-zero
 *			then @start_streaming won't be called until at least
 *			that many buffers have been queued up by userspace.
 * @stop_streaming:	called when 'streaming' state must be disabled; driver
 *			should stop any DMA transactions or wait until they
 *			finish and give back all buffers it got from &buf_queue
 *			callback by calling vb2_buffer_done() with either
 *			%VB2_BUF_STATE_DONE or %VB2_BUF_STATE_ERROR; may use
 *			vb2_wait_for_all_buffers() function
 * @buf_queue:		passes buffer vb to the driver; driver may start
 *			hardware operation on this buffer; driver should give
 *			the buffer back by calling vb2_buffer_done() function;
 *			it is always called after calling VIDIOC_STREAMON()
 *			ioctl; might be called before @start_streaming callback
 *			if user pre-queued buffers before calling
 *			VIDIOC_STREAMON().
 * @buf_request_complete: a buffer that was never queued to the driver but is
 *			associated with a queued request was canceled.
 *			The driver will have to mark associated objects in the
 *			request as completed; required if requests are
 *			supported.
 */
struct vb2_ops {
	int (*queue_setup)(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[]);

	void (*wait_prepare)(struct vb2_queue *q);
	void (*wait_finish)(struct vb2_queue *q);

	int (*buf_out_validate)(struct vb2_buffer *vb);
	int (*buf_init)(struct vb2_buffer *vb);
	int (*buf_prepare)(struct vb2_buffer *vb);
	void (*buf_finish)(struct vb2_buffer *vb);
	void (*buf_cleanup)(struct vb2_buffer *vb);

	int (*start_streaming)(struct vb2_queue *q, unsigned int count);
	void (*stop_streaming)(struct vb2_queue *q);

	void (*buf_queue)(struct vb2_buffer *vb);

	void (*buf_request_complete)(struct vb2_buffer *vb);
};

/**
 * struct vb2_buf_ops - driver-specific callbacks.
 *
 * @verify_planes_array: Verify that a given user space structure contains
 *			enough planes for the buffer. This is called
 *			for each dequeued buffer.
 * @init_buffer:	given a &vb2_buffer initialize the extra data after
 *			struct vb2_buffer.
 *			For V4L2 this is a &struct vb2_v4l2_buffer.
 * @fill_user_buffer:	given a &vb2_buffer fill in the userspace structure.
 *			For V4L2 this is a &struct v4l2_buffer.
 * @fill_vb2_buffer:	given a userspace structure, fill in the &vb2_buffer.
 *			If the userspace structure is invalid, then this op
 *			will return an error.
 * @copy_timestamp:	copy the timestamp from a userspace structure to
 *			the &struct vb2_buffer.
 */
struct vb2_buf_ops {
	int (*verify_planes_array)(struct vb2_buffer *vb, const void *pb);
	void (*init_buffer)(struct vb2_buffer *vb);
	void (*fill_user_buffer)(struct vb2_buffer *vb, void *pb);
	int (*fill_vb2_buffer)(struct vb2_buffer *vb, struct vb2_plane *planes);
	void (*copy_timestamp)(struct vb2_buffer *vb, const void *pb);
};

/**
 * struct vb2_queue - a videobuf queue.
 *
 * @type:	private buffer type whose content is defined by the vb2-core
 *		caller. For example, for V4L2, it should match
 *		the types defined on &enum v4l2_buf_type.
 * @io_modes:	supported io methods (see &enum vb2_io_modes).
 * @alloc_devs:	&struct device memory type/allocator-specific per-plane device
 * @dev:	device to use for the default allocation context if the driver
 *		doesn't fill in the @alloc_devs array.
 * @dma_attrs:	DMA attributes to use for the DMA.
 * @bidirectional: when this flag is set the DMA direction for the buffers of
 *		this queue will be overridden with %DMA_BIDIRECTIONAL direction.
 *		This is useful in cases where the hardware (firmware) writes to
 *		a buffer which is mapped as read (%DMA_TO_DEVICE), or reads from
 *		buffer which is mapped for write (%DMA_FROM_DEVICE) in order
 *		to satisfy some internal hardware restrictions or adds a padding
 *		needed by the processing algorithm. In case the DMA mapping is
 *		not bidirectional but the hardware (firmware) trying to access
 *		the buffer (in the opposite direction) this could lead to an
 *		IOMMU protection faults.
 * @fileio_read_once:		report EOF after reading the first buffer
 * @fileio_write_immediately:	queue buffer after each write() call
 * @allow_zero_bytesused:	allow bytesused == 0 to be passed to the driver
 * @quirk_poll_must_check_waiting_for_buffers: Return %EPOLLERR at poll when QBUF
 *              has not been called. This is a vb1 idiom that has been adopted
 *              also by vb2.
 * @supports_requests: this queue supports the Request API.
 * @requires_requests: this queue requires the Request API. If this is set to 1,
 *		then supports_requests must be set to 1 as well.
 * @uses_qbuf:	qbuf was used directly for this queue. Set to 1 the first
 *		time this is called. Set to 0 when the queue is canceled.
 *		If this is 1, then you cannot queue buffers from a request.
 * @uses_requests: requests are used for this queue. Set to 1 the first time
 *		a request is queued. Set to 0 when the queue is canceled.
 *		If this is 1, then you cannot queue buffers directly.
 * @allow_cache_hints: when set user-space can pass cache management hints in
 *		order to skip cache flush/invalidation on ->prepare() or/and
 *		->finish().
 * @lock:	pointer to a mutex that protects the &struct vb2_queue. The
 *		driver can set this to a mutex to let the v4l2 core serialize
 *		the queuing ioctls. If the driver wants to handle locking
 *		itself, then this should be set to NULL. This lock is not used
 *		by the videobuf2 core API.
 * @owner:	The filehandle that 'owns' the buffers, i.e. the filehandle
 *		that called reqbufs, create_buffers or started fileio.
 *		This field is not used by the videobuf2 core API, but it allows
 *		drivers to easily associate an owner filehandle with the queue.
 * @ops:	driver-specific callbacks
 * @mem_ops:	memory allocator specific callbacks
 * @buf_ops:	callbacks to deliver buffer information.
 *		between user-space and kernel-space.
 * @drv_priv:	driver private data.
 * @subsystem_flags: Flags specific to the subsystem (V4L2/DVB/etc.). Not used
 *		by the vb2 core.
 * @buf_struct_size: size of the driver-specific buffer structure;
 *		"0" indicates the driver doesn't want to use a custom buffer
 *		structure type. In that case a subsystem-specific struct
 *		will be used (in the case of V4L2 that is
 *		``sizeof(struct vb2_v4l2_buffer)``). The first field of the
 *		driver-specific buffer structure must be the subsystem-specific
 *		struct (vb2_v4l2_buffer in the case of V4L2).
 * @timestamp_flags: Timestamp flags; ``V4L2_BUF_FLAG_TIMESTAMP_*`` and
 *		``V4L2_BUF_FLAG_TSTAMP_SRC_*``
 * @gfp_flags:	additional gfp flags used when allocating the buffers.
 *		Typically this is 0, but it may be e.g. %GFP_DMA or %__GFP_DMA32
 *		to force the buffer allocation to a specific memory zone.
 * @min_buffers_needed: the minimum number of buffers needed before
 *		@start_streaming can be called. Used when a DMA engine
 *		cannot be started unless at least this number of buffers
 *		have been queued into the driver.
 */
/*
 * Private elements (won't appear at the uAPI book):
 * @mmap_lock:	private mutex used when buffers are allocated/freed/mmapped
 * @memory:	current memory type used
 * @dma_dir:	DMA mapping direction.
 * @bufs:	videobuf buffer structures
 * @num_buffers: number of allocated/used buffers
 * @queued_list: list of buffers currently queued from userspace
 * @queued_count: number of buffers queued and ready for streaming.
 * @owned_by_drv_count: number of buffers owned by the driver
 * @done_list:	list of buffers ready to be dequeued to userspace
 * @done_lock:	lock to protect done_list list
 * @done_wq:	waitqueue for processes waiting for buffers ready to be dequeued
 * @streaming:	current streaming state
 * @start_streaming_called: @start_streaming was called successfully and we
 *		started streaming.
 * @error:	a fatal error occurred on the queue
 * @waiting_for_buffers: used in poll() to check if vb2 is still waiting for
 *		buffers. Only set for capture queues if qbuf has not yet been
 *		called since poll() needs to return %EPOLLERR in that situation.
 * @is_multiplanar: set if buffer type is multiplanar
 * @is_output:	set if buffer type is output
 * @copy_timestamp: set if vb2-core should set timestamps
 * @last_buffer_dequeued: used in poll() and DQBUF to immediately return if the
 *		last decoded buffer was already dequeued. Set for capture queues
 *		when a buffer with the %V4L2_BUF_FLAG_LAST is dequeued.
 * @fileio:	file io emulator internal data, used only if emulator is active
 * @threadio:	thread io internal data, used only if thread is active
 * @name:	queue name, used for logging purpose. Initialized automatically
 *		if left empty by drivers.
 */
struct vb2_queue {
	unsigned int			type;
	unsigned int			io_modes;
	struct device			*dev;
	unsigned long			dma_attrs;
	unsigned int			bidirectional:1;
	unsigned int			fileio_read_once:1;
	unsigned int			fileio_write_immediately:1;
	unsigned int			allow_zero_bytesused:1;
	unsigned int		   quirk_poll_must_check_waiting_for_buffers:1;
	unsigned int			supports_requests:1;
	unsigned int			requires_requests:1;
	unsigned int			uses_qbuf:1;
	unsigned int			uses_requests:1;
	unsigned int			allow_cache_hints:1;

	struct mutex			*lock;
	void				*owner;

	const struct vb2_ops		*ops;
	const struct vb2_mem_ops	*mem_ops;
	const struct vb2_buf_ops	*buf_ops;

	void				*drv_priv;
	u32				subsystem_flags;
	unsigned int			buf_struct_size;
	u32				timestamp_flags;
	gfp_t				gfp_flags;
	u32				min_buffers_needed;

	struct device			*alloc_devs[VB2_MAX_PLANES];

	/* private: internal use only */
	struct mutex			mmap_lock;
	unsigned int			memory;
	enum dma_data_direction		dma_dir;
	struct vb2_buffer		*bufs[VB2_MAX_FRAME];
	unsigned int			num_buffers;

	struct list_head		queued_list;
	unsigned int			queued_count;

	atomic_t			owned_by_drv_count;
	struct list_head		done_list;
	spinlock_t			done_lock;
	wait_queue_head_t		done_wq;

	unsigned int			streaming:1;
	unsigned int			start_streaming_called:1;
	unsigned int			error:1;
	unsigned int			waiting_for_buffers:1;
	unsigned int			waiting_in_dqbuf:1;
	unsigned int			is_multiplanar:1;
	unsigned int			is_output:1;
	unsigned int			copy_timestamp:1;
	unsigned int			last_buffer_dequeued:1;

	struct vb2_fileio_data		*fileio;
	struct vb2_threadio_data	*threadio;

	char				name[32];

#ifdef CONFIG_VIDEO_ADV_DEBUG
	/*
	 * Counters for how often these queue-related ops are
	 * called. Used to check for unbalanced ops.
	 */
	u32				cnt_queue_setup;
	u32				cnt_wait_prepare;
	u32				cnt_wait_finish;
	u32				cnt_start_streaming;
	u32				cnt_stop_streaming;
#endif
};

/**
 * vb2_queue_allows_cache_hints() - Return true if the queue allows cache
 * and memory consistency hints.
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue
 */
static inline bool vb2_queue_allows_cache_hints(struct vb2_queue *q)
{
	return q->allow_cache_hints && q->memory == VB2_MEMORY_MMAP;
}

/**
 * vb2_plane_vaddr() - Return a kernel virtual address of a given plane.
 * @vb:		pointer to &struct vb2_buffer to which the plane in
 *		question belongs to.
 * @plane_no:	plane number for which the address is to be returned.
 *
 * This function returns a kernel virtual address of a given plane if
 * such a mapping exist, NULL otherwise.
 */
void *vb2_plane_vaddr(struct vb2_buffer *vb, unsigned int plane_no);

/**
 * vb2_plane_cookie() - Return allocator specific cookie for the given plane.
 * @vb:		pointer to &struct vb2_buffer to which the plane in
 *		question belongs to.
 * @plane_no:	plane number for which the cookie is to be returned.
 *
 * This function returns an allocator specific cookie for a given plane if
 * available, NULL otherwise. The allocator should provide some simple static
 * inline function, which would convert this cookie to the allocator specific
 * type that can be used directly by the driver to access the buffer. This can
 * be for example physical address, pointer to scatter list or IOMMU mapping.
 */
void *vb2_plane_cookie(struct vb2_buffer *vb, unsigned int plane_no);

/**
 * vb2_buffer_done() - inform videobuf that an operation on a buffer
 *	is finished.
 * @vb:		pointer to &struct vb2_buffer to be used.
 * @state:	state of the buffer, as defined by &enum vb2_buffer_state.
 *		Either %VB2_BUF_STATE_DONE if the operation finished
 *		successfully, %VB2_BUF_STATE_ERROR if the operation finished
 *		with an error or %VB2_BUF_STATE_QUEUED.
 *
 * This function should be called by the driver after a hardware operation on
 * a buffer is finished and the buffer may be returned to userspace. The driver
 * cannot use this buffer anymore until it is queued back to it by videobuf
 * by the means of &vb2_ops->buf_queue callback. Only buffers previously queued
 * to the driver by &vb2_ops->buf_queue can be passed to this function.
 *
 * While streaming a buffer can only be returned in state DONE or ERROR.
 * The &vb2_ops->start_streaming op can also return them in case the DMA engine
 * cannot be started for some reason. In that case the buffers should be
 * returned with state QUEUED to put them back into the queue.
 */
void vb2_buffer_done(struct vb2_buffer *vb, enum vb2_buffer_state state);

/**
 * vb2_discard_done() - discard all buffers marked as DONE.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *
 * This function is intended to be used with suspend/resume operations. It
 * discards all 'done' buffers as they would be too old to be requested after
 * resume.
 *
 * Drivers must stop the hardware and synchronize with interrupt handlers and/or
 * delayed works before calling this function to make sure no buffer will be
 * touched by the driver and/or hardware.
 */
void vb2_discard_done(struct vb2_queue *q);

/**
 * vb2_wait_for_all_buffers() - wait until all buffers are given back to vb2.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *
 * This function will wait until all buffers that have been given to the driver
 * by &vb2_ops->buf_queue are given back to vb2 with vb2_buffer_done(). It
 * doesn't call &vb2_ops->wait_prepare/&vb2_ops->wait_finish pair.
 * It is intended to be called with all locks taken, for example from
 * &vb2_ops->stop_streaming callback.
 */
int vb2_wait_for_all_buffers(struct vb2_queue *q);

/**
 * vb2_core_querybuf() - query video buffer information.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @index:	id number of the buffer.
 * @pb:		buffer struct passed from userspace.
 *
 * Videobuf2 core helper to implement VIDIOC_QUERYBUF() operation. It is called
 * internally by VB2 by an API-specific handler, like ``videobuf2-v4l2.h``.
 *
 * The passed buffer should have been verified.
 *
 * This function fills the relevant information for the userspace.
 *
 * Return: returns zero on success; an error code otherwise.
 */
void vb2_core_querybuf(struct vb2_queue *q, unsigned int index, void *pb);

/**
 * vb2_core_reqbufs() - Initiate streaming.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @memory:	memory type, as defined by &enum vb2_memory.
 * @count:	requested buffer count.
 *
 * Videobuf2 core helper to implement VIDIOC_REQBUF() operation. It is called
 * internally by VB2 by an API-specific handler, like ``videobuf2-v4l2.h``.
 *
 * This function:
 *
 * #) verifies streaming parameters passed from the userspace;
 * #) sets up the queue;
 * #) negotiates number of buffers and planes per buffer with the driver
 *    to be used during streaming;
 * #) allocates internal buffer structures (&struct vb2_buffer), according to
 *    the agreed parameters;
 * #) for MMAP memory type, allocates actual video memory, using the
 *    memory handling/allocation routines provided during queue initialization.
 *
 * If req->count is 0, all the memory will be freed instead.
 *
 * If the queue has been allocated previously by a previous vb2_core_reqbufs()
 * call and the queue is not busy, memory will be reallocated.
 *
 * Return: returns zero on success; an error code otherwise.
 */
int vb2_core_reqbufs(struct vb2_queue *q, enum vb2_memory memory,
		    unsigned int *count);

/**
 * vb2_core_create_bufs() - Allocate buffers and any required auxiliary structs
 * @q: pointer to &struct vb2_queue with videobuf2 queue.
 * @memory: memory type, as defined by &enum vb2_memory.
 * @count: requested buffer count.
 * @requested_planes: number of planes requested.
 * @requested_sizes: array with the size of the planes.
 *
 * Videobuf2 core helper to implement VIDIOC_CREATE_BUFS() operation. It is
 * called internally by VB2 by an API-specific handler, like
 * ``videobuf2-v4l2.h``.
 *
 * This function:
 *
 * #) verifies parameter sanity;
 * #) calls the &vb2_ops->queue_setup queue operation;
 * #) performs any necessary memory allocations.
 *
 * Return: returns zero on success; an error code otherwise.
 */
int vb2_core_create_bufs(struct vb2_queue *q, enum vb2_memory memory,
			 unsigned int *count,
			 unsigned int requested_planes,
			 const unsigned int requested_sizes[]);

/**
 * vb2_core_prepare_buf() - Pass ownership of a buffer from userspace
 *			to the kernel.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @index:	id number of the buffer.
 * @pb:		buffer structure passed from userspace to
 *		&v4l2_ioctl_ops->vidioc_prepare_buf handler in driver.
 *
 * Videobuf2 core helper to implement VIDIOC_PREPARE_BUF() operation. It is
 * called internally by VB2 by an API-specific handler, like
 * ``videobuf2-v4l2.h``.
 *
 * The passed buffer should have been verified.
 *
 * This function calls vb2_ops->buf_prepare callback in the driver
 * (if provided), in which driver-specific buffer initialization can
 * be performed.
 *
 * Return: returns zero on success; an error code otherwise.
 */
int vb2_core_prepare_buf(struct vb2_queue *q, unsigned int index, void *pb);

/**
 * vb2_core_qbuf() - Queue a buffer from userspace
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @index:	id number of the buffer
 * @pb:		buffer structure passed from userspace to
 *		v4l2_ioctl_ops->vidioc_qbuf handler in driver
 * @req:	pointer to &struct media_request, may be NULL.
 *
 * Videobuf2 core helper to implement VIDIOC_QBUF() operation. It is called
 * internally by VB2 by an API-specific handler, like ``videobuf2-v4l2.h``.
 *
 * This function:
 *
 * #) If @req is non-NULL, then the buffer will be bound to this
 *    media request and it returns. The buffer will be prepared and
 *    queued to the driver (i.e. the next two steps) when the request
 *    itself is queued.
 * #) if necessary, calls &vb2_ops->buf_prepare callback in the driver
 *    (if provided), in which driver-specific buffer initialization can
 *    be performed;
 * #) if streaming is on, queues the buffer in driver by the means of
 *    &vb2_ops->buf_queue callback for processing.
 *
 * Return: returns zero on success; an error code otherwise.
 */
int vb2_core_qbuf(struct vb2_queue *q, unsigned int index, void *pb,
		  struct media_request *req);

/**
 * vb2_core_dqbuf() - Dequeue a buffer to the userspace
 * @q:		pointer to &struct vb2_queue with videobuf2 queue
 * @pindex:	pointer to the buffer index. May be NULL
 * @pb:		buffer structure passed from userspace to
 *		v4l2_ioctl_ops->vidioc_dqbuf handler in driver.
 * @nonblocking: if true, this call will not sleep waiting for a buffer if no
 *		 buffers ready for dequeuing are present. Normally the driver
 *		 would be passing (file->f_flags & O_NONBLOCK) here.
 *
 * Videobuf2 core helper to implement VIDIOC_DQBUF() operation. It is called
 * internally by VB2 by an API-specific handler, like ``videobuf2-v4l2.h``.
 *
 * This function:
 *
 * #) calls buf_finish callback in the driver (if provided), in which
 *    driver can perform any additional operations that may be required before
 *    returning the buffer to userspace, such as cache sync,
 * #) the buffer struct members are filled with relevant information for
 *    the userspace.
 *
 * Return: returns zero on success; an error code otherwise.
 */
int vb2_core_dqbuf(struct vb2_queue *q, unsigned int *pindex, void *pb,
		   bool nonblocking);

/**
 * vb2_core_streamon() - Implements VB2 stream ON logic
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue
 * @type:	type of the queue to be started.
 *		For V4L2, this is defined by &enum v4l2_buf_type type.
 *
 * Videobuf2 core helper to implement VIDIOC_STREAMON() operation. It is called
 * internally by VB2 by an API-specific handler, like ``videobuf2-v4l2.h``.
 *
 * Return: returns zero on success; an error code otherwise.
 */
int vb2_core_streamon(struct vb2_queue *q, unsigned int type);

/**
 * vb2_core_streamoff() - Implements VB2 stream OFF logic
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue
 * @type:	type of the queue to be started.
 *		For V4L2, this is defined by &enum v4l2_buf_type type.
 *
 * Videobuf2 core helper to implement VIDIOC_STREAMOFF() operation. It is
 * called internally by VB2 by an API-specific handler, like
 * ``videobuf2-v4l2.h``.
 *
 * Return: returns zero on success; an error code otherwise.
 */
int vb2_core_streamoff(struct vb2_queue *q, unsigned int type);

/**
 * vb2_core_expbuf() - Export a buffer as a file descriptor.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @fd:		pointer to the file descriptor associated with DMABUF
 *		(set by driver).
 * @type:	buffer type.
 * @index:	id number of the buffer.
 * @plane:	index of the plane to be exported, 0 for single plane queues
 * @flags:	file flags for newly created file, as defined at
 *		include/uapi/asm-generic/fcntl.h.
 *		Currently, the only used flag is %O_CLOEXEC.
 *		is supported, refer to manual of open syscall for more details.
 *
 *
 * Videobuf2 core helper to implement VIDIOC_EXPBUF() operation. It is called
 * internally by VB2 by an API-specific handler, like ``videobuf2-v4l2.h``.
 *
 * Return: returns zero on success; an error code otherwise.
 */
int vb2_core_expbuf(struct vb2_queue *q, int *fd, unsigned int type,
		unsigned int index, unsigned int plane, unsigned int flags);

/**
 * vb2_core_queue_init() - initialize a videobuf2 queue
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *		This structure should be allocated in driver
 *
 * The &vb2_queue structure should be allocated by the driver. The driver is
 * responsible of clearing it's content and setting initial values for some
 * required entries before calling this function.
 *
 * .. note::
 *
 *    The following fields at @q should be set before calling this function:
 *    &vb2_queue->ops, &vb2_queue->mem_ops, &vb2_queue->type.
 */
int vb2_core_queue_init(struct vb2_queue *q);

/**
 * vb2_core_queue_release() - stop streaming, release the queue and free memory
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *
 * This function stops streaming and performs necessary clean ups, including
 * freeing video buffer memory. The driver is responsible for freeing
 * the &struct vb2_queue itself.
 */
void vb2_core_queue_release(struct vb2_queue *q);

/**
 * vb2_queue_error() - signal a fatal error on the queue
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *
 * Flag that a fatal unrecoverable error has occurred and wake up all processes
 * waiting on the queue. Polling will now set %EPOLLERR and queuing and dequeuing
 * buffers will return %-EIO.
 *
 * The error flag will be cleared when canceling the queue, either from
 * vb2_streamoff() or vb2_queue_release(). Drivers should thus not call this
 * function before starting the stream, otherwise the error flag will remain set
 * until the queue is released when closing the device node.
 */
void vb2_queue_error(struct vb2_queue *q);

/**
 * vb2_mmap() - map video buffers into application address space.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @vma:	pointer to &struct vm_area_struct with the vma passed
 *		to the mmap file operation handler in the driver.
 *
 * Should be called from mmap file operation handler of a driver.
 * This function maps one plane of one of the available video buffers to
 * userspace. To map whole video memory allocated on reqbufs, this function
 * has to be called once per each plane per each buffer previously allocated.
 *
 * When the userspace application calls mmap, it passes to it an offset returned
 * to it earlier by the means of &v4l2_ioctl_ops->vidioc_querybuf handler.
 * That offset acts as a "cookie", which is then used to identify the plane
 * to be mapped.
 *
 * This function finds a plane with a matching offset and a mapping is performed
 * by the means of a provided memory operation.
 *
 * The return values from this function are intended to be directly returned
 * from the mmap handler in driver.
 */
int vb2_mmap(struct vb2_queue *q, struct vm_area_struct *vma);

#ifndef CONFIG_MMU
/**
 * vb2_get_unmapped_area - map video buffers into application address space.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @addr:	memory address.
 * @len:	buffer size.
 * @pgoff:	page offset.
 * @flags:	memory flags.
 *
 * This function is used in noMMU platforms to propose address mapping
 * for a given buffer. It's intended to be used as a handler for the
 * &file_operations->get_unmapped_area operation.
 *
 * This is called by the mmap() syscall routines will call this
 * to get a proposed address for the mapping, when ``!CONFIG_MMU``.
 */
unsigned long vb2_get_unmapped_area(struct vb2_queue *q,
				    unsigned long addr,
				    unsigned long len,
				    unsigned long pgoff,
				    unsigned long flags);
#endif

/**
 * vb2_core_poll() - implements poll syscall() logic.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @file:	&struct file argument passed to the poll
 *		file operation handler.
 * @wait:	&poll_table wait argument passed to the poll
 *		file operation handler.
 *
 * This function implements poll file operation handler for a driver.
 * For CAPTURE queues, if a buffer is ready to be dequeued, the userspace will
 * be informed that the file descriptor of a video device is available for
 * reading.
 * For OUTPUT queues, if a buffer is ready to be dequeued, the file descriptor
 * will be reported as available for writing.
 *
 * The return values from this function are intended to be directly returned
 * from poll handler in driver.
 */
__poll_t vb2_core_poll(struct vb2_queue *q, struct file *file,
			   poll_table *wait);

/**
 * vb2_read() - implements read() syscall logic.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @data:	pointed to target userspace buffer
 * @count:	number of bytes to read
 * @ppos:	file handle position tracking pointer
 * @nonblock:	mode selector (1 means blocking calls, 0 means nonblocking)
 */
size_t vb2_read(struct vb2_queue *q, char __user *data, size_t count,
		loff_t *ppos, int nonblock);
/**
 * vb2_write() - implements write() syscall logic.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @data:	pointed to target userspace buffer
 * @count:	number of bytes to write
 * @ppos:	file handle position tracking pointer
 * @nonblock:	mode selector (1 means blocking calls, 0 means nonblocking)
 */
size_t vb2_write(struct vb2_queue *q, const char __user *data, size_t count,
		loff_t *ppos, int nonblock);

/**
 * typedef vb2_thread_fnc - callback function for use with vb2_thread.
 *
 * @vb: pointer to struct &vb2_buffer.
 * @priv: pointer to a private data.
 *
 * This is called whenever a buffer is dequeued in the thread.
 */
typedef int (*vb2_thread_fnc)(struct vb2_buffer *vb, void *priv);

/**
 * vb2_thread_start() - start a thread for the given queue.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @fnc:	&vb2_thread_fnc callback function.
 * @priv:	priv pointer passed to the callback function.
 * @thread_name:the name of the thread. This will be prefixed with "vb2-".
 *
 * This starts a thread that will queue and dequeue until an error occurs
 * or vb2_thread_stop() is called.
 *
 * .. attention::
 *
 *   This function should not be used for anything else but the videobuf2-dvb
 *   support. If you think you have another good use-case for this, then please
 *   contact the linux-media mailing list first.
 */
int vb2_thread_start(struct vb2_queue *q, vb2_thread_fnc fnc, void *priv,
		     const char *thread_name);

/**
 * vb2_thread_stop() - stop the thread for the given queue.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 */
int vb2_thread_stop(struct vb2_queue *q);

/**
 * vb2_is_streaming() - return streaming status of the queue.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 */
static inline bool vb2_is_streaming(struct vb2_queue *q)
{
	return q->streaming;
}

/**
 * vb2_fileio_is_active() - return true if fileio is active.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *
 * This returns true if read() or write() is used to stream the data
 * as opposed to stream I/O. This is almost never an important distinction,
 * except in rare cases. One such case is that using read() or write() to
 * stream a format using %V4L2_FIELD_ALTERNATE is not allowed since there
 * is no way you can pass the field information of each buffer to/from
 * userspace. A driver that supports this field format should check for
 * this in the &vb2_ops->queue_setup op and reject it if this function returns
 * true.
 */
static inline bool vb2_fileio_is_active(struct vb2_queue *q)
{
	return q->fileio;
}

/**
 * vb2_is_busy() - return busy status of the queue.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 *
 * This function checks if queue has any buffers allocated.
 */
static inline bool vb2_is_busy(struct vb2_queue *q)
{
	return (q->num_buffers > 0);
}

/**
 * vb2_get_drv_priv() - return driver private data associated with the queue.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 */
static inline void *vb2_get_drv_priv(struct vb2_queue *q)
{
	return q->drv_priv;
}

/**
 * vb2_set_plane_payload() - set bytesused for the plane @plane_no.
 * @vb:		pointer to &struct vb2_buffer to which the plane in
 *		question belongs to.
 * @plane_no:	plane number for which payload should be set.
 * @size:	payload in bytes.
 */
static inline void vb2_set_plane_payload(struct vb2_buffer *vb,
				 unsigned int plane_no, unsigned long size)
{
	if (plane_no < vb->num_planes)
		vb->planes[plane_no].bytesused = size;
}

/**
 * vb2_get_plane_payload() - get bytesused for the plane plane_no
 * @vb:		pointer to &struct vb2_buffer to which the plane in
 *		question belongs to.
 * @plane_no:	plane number for which payload should be set.
 */
static inline unsigned long vb2_get_plane_payload(struct vb2_buffer *vb,
				 unsigned int plane_no)
{
	if (plane_no < vb->num_planes)
		return vb->planes[plane_no].bytesused;
	return 0;
}

/**
 * vb2_plane_size() - return plane size in bytes.
 * @vb:		pointer to &struct vb2_buffer to which the plane in
 *		question belongs to.
 * @plane_no:	plane number for which size should be returned.
 */
static inline unsigned long
vb2_plane_size(struct vb2_buffer *vb, unsigned int plane_no)
{
	if (plane_no < vb->num_planes)
		return vb->planes[plane_no].length;
	return 0;
}

/**
 * vb2_start_streaming_called() - return streaming status of driver.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 */
static inline bool vb2_start_streaming_called(struct vb2_queue *q)
{
	return q->start_streaming_called;
}

/**
 * vb2_clear_last_buffer_dequeued() - clear last buffer dequeued flag of queue.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 */
static inline void vb2_clear_last_buffer_dequeued(struct vb2_queue *q)
{
	q->last_buffer_dequeued = false;
}

/**
 * vb2_get_buffer() - get a buffer from a queue
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @index:	buffer index
 *
 * This function obtains a buffer from a queue, by its index.
 * Keep in mind that there is no refcounting involved in this
 * operation, so the buffer lifetime should be taken into
 * consideration.
 */
static inline struct vb2_buffer *vb2_get_buffer(struct vb2_queue *q,
						unsigned int index)
{
	if (index < q->num_buffers)
		return q->bufs[index];
	return NULL;
}

/*
 * The following functions are not part of the vb2 core API, but are useful
 * functions for videobuf2-*.
 */

/**
 * vb2_buffer_in_use() - return true if the buffer is in use and
 * the queue cannot be freed (by the means of VIDIOC_REQBUFS(0)) call.
 *
 * @vb:		buffer for which plane size should be returned.
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 */
bool vb2_buffer_in_use(struct vb2_queue *q, struct vb2_buffer *vb);

/**
 * vb2_verify_memory_type() - Check whether the memory type and buffer type
 * passed to a buffer operation are compatible with the queue.
 *
 * @q:		pointer to &struct vb2_queue with videobuf2 queue.
 * @memory:	memory model, as defined by enum &vb2_memory.
 * @type:	private buffer type whose content is defined by the vb2-core
 *		caller. For example, for V4L2, it should match
 *		the types defined on enum &v4l2_buf_type.
 */
int vb2_verify_memory_type(struct vb2_queue *q,
		enum vb2_memory memory, unsigned int type);

/**
 * vb2_request_object_is_buffer() - return true if the object is a buffer
 *
 * @obj:	the request object.
 */
bool vb2_request_object_is_buffer(struct media_request_object *obj);

/**
 * vb2_request_buffer_cnt() - return the number of buffers in the request
 *
 * @req:	the request.
 */
unsigned int vb2_request_buffer_cnt(struct media_request *req);

#endif /* _MEDIA_VIDEOBUF2_CORE_H */
