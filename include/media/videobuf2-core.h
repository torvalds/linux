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

#define VB2_MAX_FRAME	(32)
#define VB2_MAX_PLANES	(8)

enum vb2_memory {
	VB2_MEMORY_UNKNOWN	= 0,
	VB2_MEMORY_MMAP		= 1,
	VB2_MEMORY_USERPTR	= 2,
	VB2_MEMORY_DMABUF	= 4,
};

struct vb2_alloc_ctx;
struct vb2_fileio_data;
struct vb2_threadio_data;

/**
 * struct vb2_mem_ops - memory handling/memory allocator operations
 * @alloc:	allocate video memory and, optionally, allocator private data,
 *		return NULL on failure or a pointer to allocator private,
 *		per-buffer data on success; the returned private structure
 *		will then be passed as buf_priv argument to other ops in this
 *		structure. Additional gfp_flags to use when allocating the
 *		are also passed to this operation. These flags are from the
 *		gfp_flags field of vb2_queue.
 * @put:	inform the allocator that the buffer will no longer be used;
 *		usually will result in the allocator freeing the buffer (if
 *		no other users of this buffer are present); the buf_priv
 *		argument is the allocator private per-buffer structure
 *		previously returned from the alloc callback.
 * @get_dmabuf: acquire userspace memory for a hardware operation; used for
 *		 DMABUF memory types.
 * @get_userptr: acquire userspace memory for a hardware operation; used for
 *		 USERPTR memory types; vaddr is the address passed to the
 *		 videobuf layer when queuing a video buffer of USERPTR type;
 *		 should return an allocator private per-buffer structure
 *		 associated with the buffer on success, NULL on failure;
 *		 the returned private structure will then be passed as buf_priv
 *		 argument to other ops in this structure.
 * @put_userptr: inform the allocator that a USERPTR buffer will no longer
 *		 be used.
 * @attach_dmabuf: attach a shared struct dma_buf for a hardware operation;
 *		   used for DMABUF memory types; alloc_ctx is the alloc context
 *		   dbuf is the shared dma_buf; returns NULL on failure;
 *		   allocator private per-buffer structure on success;
 *		   this needs to be used for further accesses to the buffer.
 * @detach_dmabuf: inform the exporter of the buffer that the current DMABUF
 *		   buffer is no longer used; the buf_priv argument is the
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
 * Required ops for USERPTR types: get_userptr, put_userptr.
 * Required ops for MMAP types: alloc, put, num_users, mmap.
 * Required ops for read/write access types: alloc, put, num_users, vaddr.
 * Required ops for DMABUF types: attach_dmabuf, detach_dmabuf, map_dmabuf,
 *				  unmap_dmabuf.
 */
struct vb2_mem_ops {
	void		*(*alloc)(void *alloc_ctx, unsigned long size,
				  enum dma_data_direction dma_dir,
				  gfp_t gfp_flags);
	void		(*put)(void *buf_priv);
	struct dma_buf *(*get_dmabuf)(void *buf_priv, unsigned long flags);

	void		*(*get_userptr)(void *alloc_ctx, unsigned long vaddr,
					unsigned long size,
					enum dma_data_direction dma_dir);
	void		(*put_userptr)(void *buf_priv);

	void		(*prepare)(void *buf_priv);
	void		(*finish)(void *buf_priv);

	void		*(*attach_dmabuf)(void *alloc_ctx, struct dma_buf *dbuf,
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
 * struct vb2_plane - plane information
 * @mem_priv:	private data with this plane
 * @dbuf:	dma_buf - shared buffer object
 * @dbuf_mapped:	flag to show whether dbuf is mapped or not
 * @bytesused:	number of bytes occupied by data in the plane (payload)
 * @length:	size of this plane (NOT the payload) in bytes
 * @min_length:	minimum required size of this plane (NOT the payload) in bytes.
 *		@length is always greater or equal to @min_length.
 * @offset:	when memory in the associated struct vb2_buffer is
 *		VB2_MEMORY_MMAP, equals the offset from the start of
 *		the device memory for this plane (or is a "cookie" that
 *		should be passed to mmap() called on the video node)
 * @userptr:	when memory is VB2_MEMORY_USERPTR, a userspace pointer
 *		pointing to this plane
 * @fd:		when memory is VB2_MEMORY_DMABUF, a userspace file
 *		descriptor associated with this plane
 * @m:		Union with memtype-specific data (@offset, @userptr or
 *		@fd).
 * @data_offset:	offset in the plane to the start of data; usually 0,
 *		unless there is a header in front of the data
 * Should contain enough information to be able to cover all the fields
 * of struct v4l2_plane at videodev2.h
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
 * enum vb2_io_modes - queue access methods
 * @VB2_MMAP:		driver supports MMAP with streaming API
 * @VB2_USERPTR:	driver supports USERPTR with streaming API
 * @VB2_READ:		driver supports read() style access
 * @VB2_WRITE:		driver supports write() style access
 * @VB2_DMABUF:		driver supports DMABUF with streaming API
 */
enum vb2_io_modes {
	VB2_MMAP	= (1 << 0),
	VB2_USERPTR	= (1 << 1),
	VB2_READ	= (1 << 2),
	VB2_WRITE	= (1 << 3),
	VB2_DMABUF	= (1 << 4),
};

/**
 * enum vb2_buffer_state - current video buffer state
 * @VB2_BUF_STATE_DEQUEUED:	buffer under userspace control
 * @VB2_BUF_STATE_PREPARING:	buffer is being prepared in videobuf
 * @VB2_BUF_STATE_PREPARED:	buffer prepared in videobuf and by the driver
 * @VB2_BUF_STATE_QUEUED:	buffer queued in videobuf, but not in driver
 * @VB2_BUF_STATE_REQUEUEING:	re-queue a buffer to the driver
 * @VB2_BUF_STATE_ACTIVE:	buffer queued in driver and possibly used
 *				in a hardware operation
 * @VB2_BUF_STATE_DONE:		buffer returned from driver to videobuf, but
 *				not yet dequeued to userspace
 * @VB2_BUF_STATE_ERROR:	same as above, but the operation on the buffer
 *				has ended with an error, which will be reported
 *				to the userspace when it is dequeued
 */
enum vb2_buffer_state {
	VB2_BUF_STATE_DEQUEUED,
	VB2_BUF_STATE_PREPARING,
	VB2_BUF_STATE_PREPARED,
	VB2_BUF_STATE_QUEUED,
	VB2_BUF_STATE_REQUEUEING,
	VB2_BUF_STATE_ACTIVE,
	VB2_BUF_STATE_DONE,
	VB2_BUF_STATE_ERROR,
};

struct vb2_queue;

/**
 * struct vb2_buffer - represents a video buffer
 * @vb2_queue:		the queue to which this driver belongs
 * @index:		id number of the buffer
 * @type:		buffer type
 * @memory:		the method, in which the actual data is passed
 * @num_planes:		number of planes in the buffer
 *			on an internal driver queue
 * @planes:		private per-plane information; do not change
 * @timestamp:		frame timestamp in ns
 */
struct vb2_buffer {
	struct vb2_queue	*vb2_queue;
	unsigned int		index;
	unsigned int		type;
	unsigned int		memory;
	unsigned int		num_planes;
	struct vb2_plane	planes[VB2_MAX_PLANES];
	u64			timestamp;

	/* private: internal use only
	 *
	 * state:		current buffer state; do not change
	 * queued_entry:	entry on the queued buffers list, which holds
	 *			all buffers queued from userspace
	 * done_entry:		entry on the list that stores all buffers ready
	 *			to be dequeued to userspace
	 */
	enum vb2_buffer_state	state;

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

	u32		cnt_buf_init;
	u32		cnt_buf_prepare;
	u32		cnt_buf_finish;
	u32		cnt_buf_cleanup;
	u32		cnt_buf_queue;

	/* This counts the number of calls to vb2_buffer_done() */
	u32		cnt_buf_done;
#endif
};

/**
 * struct vb2_ops - driver-specific callbacks
 *
 * @queue_setup:	called from VIDIOC_REQBUFS and VIDIOC_CREATE_BUFS
 *			handlers before memory allocation. It can be called
 *			twice: if the original number of requested buffers
 *			could not be allocated, then it will be called a
 *			second time with the actually allocated number of
 *			buffers to verify if that is OK.
 *			The driver should return the required number of buffers
 *			in *num_buffers, the required number of planes per
 *			buffer in *num_planes, the size of each plane should be
 *			set in the sizes[] array and optional per-plane
 *			allocator specific context in the alloc_ctxs[] array.
 *			When called from VIDIOC_REQBUFS, *num_planes == 0, the
 *			driver has to use the currently configured format to
 *			determine the plane sizes and *num_buffers is the total
 *			number of buffers that are being allocated. When called
 *			from VIDIOC_CREATE_BUFS, *num_planes != 0 and it
 *			describes the requested number of planes and sizes[]
 *			contains the requested plane sizes. If either
 *			*num_planes or the requested sizes are invalid callback
 *			must return -EINVAL. In this case *num_buffers are
 *			being allocated additionally to q->num_buffers.
 * @wait_prepare:	release any locks taken while calling vb2 functions;
 *			it is called before an ioctl needs to wait for a new
 *			buffer to arrive; required to avoid a deadlock in
 *			blocking access type.
 * @wait_finish:	reacquire all locks released in the previous callback;
 *			required to continue operation after sleeping while
 *			waiting for a new buffer to arrive.
 * @buf_init:		called once after allocating a buffer (in MMAP case)
 *			or after acquiring a new USERPTR buffer; drivers may
 *			perform additional buffer-related initialization;
 *			initialization failure (return != 0) will prevent
 *			queue setup from completing successfully; optional.
 * @buf_prepare:	called every time the buffer is queued from userspace
 *			and from the VIDIOC_PREPARE_BUF ioctl; drivers may
 *			perform any initialization required before each
 *			hardware operation in this callback; drivers can
 *			access/modify the buffer here as it is still synced for
 *			the CPU; drivers that support VIDIOC_CREATE_BUFS must
 *			also validate the buffer size; if an error is returned,
 *			the buffer will not be queued in driver; optional.
 * @buf_finish:		called before every dequeue of the buffer back to
 *			userspace; the buffer is synced for the CPU, so drivers
 *			can access/modify the buffer contents; drivers may
 *			perform any operations required before userspace
 *			accesses the buffer; optional. The buffer state can be
 *			one of the following: DONE and ERROR occur while
 *			streaming is in progress, and the PREPARED state occurs
 *			when the queue has been canceled and all pending
 *			buffers are being returned to their default DEQUEUED
 *			state. Typically you only have to do something if the
 *			state is VB2_BUF_STATE_DONE, since in all other cases
 *			the buffer contents will be ignored anyway.
 * @buf_cleanup:	called once before the buffer is freed; drivers may
 *			perform any additional cleanup; optional.
 * @start_streaming:	called once to enter 'streaming' state; the driver may
 *			receive buffers with @buf_queue callback before
 *			@start_streaming is called; the driver gets the number
 *			of already queued buffers in count parameter; driver
 *			can return an error if hardware fails, in that case all
 *			buffers that have been already given by the @buf_queue
 *			callback are to be returned by the driver by calling
 *			@vb2_buffer_done(VB2_BUF_STATE_QUEUED).
 *			If you need a minimum number of buffers before you can
 *			start streaming, then set @min_buffers_needed in the
 *			vb2_queue structure. If that is non-zero then
 *			start_streaming won't be called until at least that
 *			many buffers have been queued up by userspace.
 * @stop_streaming:	called when 'streaming' state must be disabled; driver
 *			should stop any DMA transactions or wait until they
 *			finish and give back all buffers it got from buf_queue()
 *			callback by calling @vb2_buffer_done() with either
 *			VB2_BUF_STATE_DONE or VB2_BUF_STATE_ERROR; may use
 *			vb2_wait_for_all_buffers() function
 * @buf_queue:		passes buffer vb to the driver; driver may start
 *			hardware operation on this buffer; driver should give
 *			the buffer back by calling vb2_buffer_done() function;
 *			it is allways called after calling STREAMON ioctl;
 *			might be called before start_streaming callback if user
 *			pre-queued buffers before calling STREAMON.
 */
struct vb2_ops {
	int (*queue_setup)(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], void *alloc_ctxs[]);

	void (*wait_prepare)(struct vb2_queue *q);
	void (*wait_finish)(struct vb2_queue *q);

	int (*buf_init)(struct vb2_buffer *vb);
	int (*buf_prepare)(struct vb2_buffer *vb);
	void (*buf_finish)(struct vb2_buffer *vb);
	void (*buf_cleanup)(struct vb2_buffer *vb);

	int (*start_streaming)(struct vb2_queue *q, unsigned int count);
	void (*stop_streaming)(struct vb2_queue *q);

	void (*buf_queue)(struct vb2_buffer *vb);
};

/**
 * struct vb2_ops - driver-specific callbacks
 *
 * @verify_planes_array: Verify that a given user space structure contains
 *			enough planes for the buffer. This is called
 *			for each dequeued buffer.
 * @fill_user_buffer:	given a vb2_buffer fill in the userspace structure.
 *			For V4L2 this is a struct v4l2_buffer.
 * @fill_vb2_buffer:	given a userspace structure, fill in the vb2_buffer.
 *			If the userspace structure is invalid, then this op
 *			will return an error.
 * @copy_timestamp:	copy the timestamp from a userspace structure to
 *			the vb2_buffer struct.
 */
struct vb2_buf_ops {
	int (*verify_planes_array)(struct vb2_buffer *vb, const void *pb);
	void (*fill_user_buffer)(struct vb2_buffer *vb, void *pb);
	int (*fill_vb2_buffer)(struct vb2_buffer *vb, const void *pb,
				struct vb2_plane *planes);
	void (*copy_timestamp)(struct vb2_buffer *vb, const void *pb);
};

/**
 * struct vb2_queue - a videobuf queue
 *
 * @type:	private buffer type whose content is defined by the vb2-core
 *		caller. For example, for V4L2, it should match
 *		the V4L2_BUF_TYPE_* in include/uapi/linux/videodev2.h
 * @io_modes:	supported io methods (see vb2_io_modes enum)
 * @fileio_read_once:		report EOF after reading the first buffer
 * @fileio_write_immediately:	queue buffer after each write() call
 * @allow_zero_bytesused:	allow bytesused == 0 to be passed to the driver
 * @quirk_poll_must_check_waiting_for_buffers: Return POLLERR at poll when QBUF
 *              has not been called. This is a vb1 idiom that has been adopted
 *              also by vb2.
 * @lock:	pointer to a mutex that protects the vb2_queue struct. The
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
 * @buf_ops:	callbacks to deliver buffer information
 *		between user-space and kernel-space
 * @drv_priv:	driver private data
 * @buf_struct_size: size of the driver-specific buffer structure;
 *		"0" indicates the driver doesn't want to use a custom buffer
 *		structure type. for example, sizeof(struct vb2_v4l2_buffer)
 *		will be used for v4l2.
 * @timestamp_flags: Timestamp flags; V4L2_BUF_FLAG_TIMESTAMP_* and
 *		V4L2_BUF_FLAG_TSTAMP_SRC_*
 * @gfp_flags:	additional gfp flags used when allocating the buffers.
 *		Typically this is 0, but it may be e.g. GFP_DMA or __GFP_DMA32
 *		to force the buffer allocation to a specific memory zone.
 * @min_buffers_needed: the minimum number of buffers needed before
 *		start_streaming() can be called. Used when a DMA engine
 *		cannot be started unless at least this number of buffers
 *		have been queued into the driver.
 */
/*
 * Private elements (won't appear at the DocBook):
 * @mmap_lock:	private mutex used when buffers are allocated/freed/mmapped
 * @memory:	current memory type used
 * @bufs:	videobuf buffer structures
 * @num_buffers: number of allocated/used buffers
 * @queued_list: list of buffers currently queued from userspace
 * @queued_count: number of buffers queued and ready for streaming.
 * @owned_by_drv_count: number of buffers owned by the driver
 * @done_list:	list of buffers ready to be dequeued to userspace
 * @done_lock:	lock to protect done_list list
 * @done_wq:	waitqueue for processes waiting for buffers ready to be dequeued
 * @alloc_ctx:	memory type/allocator-specific contexts for each plane
 * @streaming:	current streaming state
 * @start_streaming_called: start_streaming() was called successfully and we
 *		started streaming.
 * @error:	a fatal error occurred on the queue
 * @waiting_for_buffers: used in poll() to check if vb2 is still waiting for
 *		buffers. Only set for capture queues if qbuf has not yet been
 *		called since poll() needs to return POLLERR in that situation.
 * @is_multiplanar: set if buffer type is multiplanar
 * @is_output:	set if buffer type is output
 * @copy_timestamp: set if vb2-core should set timestamps
 * @last_buffer_dequeued: used in poll() and DQBUF to immediately return if the
 *		last decoded buffer was already dequeued. Set for capture queues
 *		when a buffer with the V4L2_BUF_FLAG_LAST is dequeued.
 * @fileio:	file io emulator internal data, used only if emulator is active
 * @threadio:	thread io internal data, used only if thread is active
 */
struct vb2_queue {
	unsigned int			type;
	unsigned int			io_modes;
	unsigned			fileio_read_once:1;
	unsigned			fileio_write_immediately:1;
	unsigned			allow_zero_bytesused:1;
	unsigned		   quirk_poll_must_check_waiting_for_buffers:1;

	struct mutex			*lock;
	void				*owner;

	const struct vb2_ops		*ops;
	const struct vb2_mem_ops	*mem_ops;
	const struct vb2_buf_ops	*buf_ops;

	void				*drv_priv;
	unsigned int			buf_struct_size;
	u32				timestamp_flags;
	gfp_t				gfp_flags;
	u32				min_buffers_needed;

	/* private: internal use only */
	struct mutex			mmap_lock;
	unsigned int			memory;
	struct vb2_buffer		*bufs[VB2_MAX_FRAME];
	unsigned int			num_buffers;

	struct list_head		queued_list;
	unsigned int			queued_count;

	atomic_t			owned_by_drv_count;
	struct list_head		done_list;
	spinlock_t			done_lock;
	wait_queue_head_t		done_wq;

	void				*alloc_ctx[VB2_MAX_PLANES];

	unsigned int			streaming:1;
	unsigned int			start_streaming_called:1;
	unsigned int			error:1;
	unsigned int			waiting_for_buffers:1;
	unsigned int			is_multiplanar:1;
	unsigned int			is_output:1;
	unsigned int			copy_timestamp:1;
	unsigned int			last_buffer_dequeued:1;

	struct vb2_fileio_data		*fileio;
	struct vb2_threadio_data	*threadio;

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

void *vb2_plane_vaddr(struct vb2_buffer *vb, unsigned int plane_no);
void *vb2_plane_cookie(struct vb2_buffer *vb, unsigned int plane_no);

void vb2_buffer_done(struct vb2_buffer *vb, enum vb2_buffer_state state);
void vb2_discard_done(struct vb2_queue *q);
int vb2_wait_for_all_buffers(struct vb2_queue *q);

void vb2_core_querybuf(struct vb2_queue *q, unsigned int index, void *pb);
int vb2_core_reqbufs(struct vb2_queue *q, enum vb2_memory memory,
		unsigned int *count);
int vb2_core_create_bufs(struct vb2_queue *q, enum vb2_memory memory,
		unsigned int *count, unsigned requested_planes,
		const unsigned int requested_sizes[]);
int vb2_core_prepare_buf(struct vb2_queue *q, unsigned int index, void *pb);
int vb2_core_qbuf(struct vb2_queue *q, unsigned int index, void *pb);
int vb2_core_dqbuf(struct vb2_queue *q, unsigned int *pindex, void *pb,
		   bool nonblocking);

int vb2_core_streamon(struct vb2_queue *q, unsigned int type);
int vb2_core_streamoff(struct vb2_queue *q, unsigned int type);

int vb2_core_expbuf(struct vb2_queue *q, int *fd, unsigned int type,
		unsigned int index, unsigned int plane, unsigned int flags);

int vb2_core_queue_init(struct vb2_queue *q);
void vb2_core_queue_release(struct vb2_queue *q);

void vb2_queue_error(struct vb2_queue *q);

int vb2_mmap(struct vb2_queue *q, struct vm_area_struct *vma);
#ifndef CONFIG_MMU
unsigned long vb2_get_unmapped_area(struct vb2_queue *q,
				    unsigned long addr,
				    unsigned long len,
				    unsigned long pgoff,
				    unsigned long flags);
#endif
unsigned int vb2_core_poll(struct vb2_queue *q, struct file *file,
		poll_table *wait);
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

/**
 * vb2_is_streaming() - return streaming status of the queue
 * @q:		videobuf queue
 */
static inline bool vb2_is_streaming(struct vb2_queue *q)
{
	return q->streaming;
}

/**
 * vb2_fileio_is_active() - return true if fileio is active.
 * @q:		videobuf queue
 *
 * This returns true if read() or write() is used to stream the data
 * as opposed to stream I/O. This is almost never an important distinction,
 * except in rare cases. One such case is that using read() or write() to
 * stream a format using V4L2_FIELD_ALTERNATE is not allowed since there
 * is no way you can pass the field information of each buffer to/from
 * userspace. A driver that supports this field format should check for
 * this in the queue_setup op and reject it if this function returns true.
 */
static inline bool vb2_fileio_is_active(struct vb2_queue *q)
{
	return q->fileio;
}

/**
 * vb2_is_busy() - return busy status of the queue
 * @q:		videobuf queue
 *
 * This function checks if queue has any buffers allocated.
 */
static inline bool vb2_is_busy(struct vb2_queue *q)
{
	return (q->num_buffers > 0);
}

/**
 * vb2_get_drv_priv() - return driver private data associated with the queue
 * @q:		videobuf queue
 */
static inline void *vb2_get_drv_priv(struct vb2_queue *q)
{
	return q->drv_priv;
}

/**
 * vb2_set_plane_payload() - set bytesused for the plane plane_no
 * @vb:		buffer for which plane payload should be set
 * @plane_no:	plane number for which payload should be set
 * @size:	payload in bytes
 */
static inline void vb2_set_plane_payload(struct vb2_buffer *vb,
				 unsigned int plane_no, unsigned long size)
{
	if (plane_no < vb->num_planes)
		vb->planes[plane_no].bytesused = size;
}

/**
 * vb2_get_plane_payload() - get bytesused for the plane plane_no
 * @vb:		buffer for which plane payload should be set
 * @plane_no:	plane number for which payload should be set
 */
static inline unsigned long vb2_get_plane_payload(struct vb2_buffer *vb,
				 unsigned int plane_no)
{
	if (plane_no < vb->num_planes)
		return vb->planes[plane_no].bytesused;
	return 0;
}

/**
 * vb2_plane_size() - return plane size in bytes
 * @vb:		buffer for which plane size should be returned
 * @plane_no:	plane number for which size should be returned
 */
static inline unsigned long
vb2_plane_size(struct vb2_buffer *vb, unsigned int plane_no)
{
	if (plane_no < vb->num_planes)
		return vb->planes[plane_no].length;
	return 0;
}

/**
 * vb2_start_streaming_called() - return streaming status of driver
 * @q:		videobuf queue
 */
static inline bool vb2_start_streaming_called(struct vb2_queue *q)
{
	return q->start_streaming_called;
}

/**
 * vb2_clear_last_buffer_dequeued() - clear last buffer dequeued flag of queue
 * @q:		videobuf queue
 */
static inline void vb2_clear_last_buffer_dequeued(struct vb2_queue *q)
{
	q->last_buffer_dequeued = false;
}

/*
 * The following functions are not part of the vb2 core API, but are useful
 * functions for videobuf2-*.
 */
bool vb2_buffer_in_use(struct vb2_queue *q, struct vb2_buffer *vb);
int vb2_verify_memory_type(struct vb2_queue *q,
		enum vb2_memory memory, unsigned int type);
#endif /* _MEDIA_VIDEOBUF2_CORE_H */
