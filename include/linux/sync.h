/*
 * include/linux/sync.h
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_SYNC_H
#define _LINUX_SYNC_H

#include <linux/types.h>
#ifdef __KERNEL__

#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

struct sync_timeline;
struct sync_pt;
struct sync_fence;

/**
 * struct sync_timeline_ops - sync object implementation ops
 * @driver_name:	name of the implentation
 * @dup:		duplicate a sync_pt
 * @has_signaled:	returns:
 *			  1 if pt has signaled
 *			  0 if pt has not signaled
 *			 <0 on error
 * @compare:		returns:
 *			  1 if b will signal before a
 *			  0 if a and b will signal at the same time
 *			 -1 if a will signabl before b
 * @free_pt:		called before sync_pt is freed
 * @release_obj:	called before sync_timeline is freed
 * @print_obj:		deprecated
 * @print_pt:		deprecated
 * @fill_driver_data:	write implmentation specific driver data to data.
 *			  should return an error if there is not enough room
 *			  as specified by size.  This information is returned
 *			  to userspace by SYNC_IOC_FENCE_INFO.
 * @timeline_value_str: fill str with the value of the sync_timeline's counter
 * @pt_value_str:	fill str with the value of the sync_pt
 */
struct sync_timeline_ops {
	const char *driver_name;

	/* required */
	struct sync_pt *(*dup)(struct sync_pt *pt);

	/* required */
	int (*has_signaled)(struct sync_pt *pt);

	/* required */
	int (*compare)(struct sync_pt *a, struct sync_pt *b);

	/* optional */
	void (*free_pt)(struct sync_pt *sync_pt);

	/* optional */
	void (*release_obj)(struct sync_timeline *sync_timeline);

	/* deprecated */
	void (*print_obj)(struct seq_file *s,
			  struct sync_timeline *sync_timeline);

	/* deprecated */
	void (*print_pt)(struct seq_file *s, struct sync_pt *sync_pt);

	/* optional */
	int (*fill_driver_data)(struct sync_pt *syncpt, void *data, int size);

	/* optional */
	void (*timeline_value_str)(struct sync_timeline *timeline, char *str,
				   int size);

	/* optional */
	void (*pt_value_str)(struct sync_pt *pt, char *str, int size);
};

/**
 * struct sync_timeline - sync object
 * @kref:		reference count on fence.
 * @ops:		ops that define the implementaiton of the sync_timeline
 * @name:		name of the sync_timeline. Useful for debugging
 * @destoryed:		set when sync_timeline is destroyed
 * @child_list_head:	list of children sync_pts for this sync_timeline
 * @child_list_lock:	lock protecting @child_list_head, destroyed, and
 *			  sync_pt.status
 * @active_list_head:	list of active (unsignaled/errored) sync_pts
 * @sync_timeline_list:	membership in global sync_timeline_list
 */
struct sync_timeline {
	struct kref		kref;
	const struct sync_timeline_ops	*ops;
	char			name[32];

	/* protected by child_list_lock */
	bool			destroyed;

	struct list_head	child_list_head;
	spinlock_t		child_list_lock;

	struct list_head	active_list_head;
	spinlock_t		active_list_lock;

	struct list_head	sync_timeline_list;
};

/**
 * struct sync_pt - sync point
 * @parent:		sync_timeline to which this sync_pt belongs
 * @child_list:		membership in sync_timeline.child_list_head
 * @active_list:	membership in sync_timeline.active_list_head
 * @signaled_list:	membership in temorary signaled_list on stack
 * @fence:		sync_fence to which the sync_pt belongs
 * @pt_list:		membership in sync_fence.pt_list_head
 * @status:		1: signaled, 0:active, <0: error
 * @timestamp:		time which sync_pt status transitioned from active to
 *			  singaled or error.
 */
struct sync_pt {
	struct sync_timeline		*parent;
	struct list_head	child_list;

	struct list_head	active_list;
	struct list_head	signaled_list;

	struct sync_fence	*fence;
	struct list_head	pt_list;

	/* protected by parent->active_list_lock */
	int			status;

	ktime_t			timestamp;
};

/**
 * struct sync_fence - sync fence
 * @file:		file representing this fence
 * @kref:		referenace count on fence.
 * @name:		name of sync_fence.  Useful for debugging
 * @pt_list_head:	list of sync_pts in ths fence.  immutable once fence
 *			  is created
 * @waiter_list_head:	list of asynchronous waiters on this fence
 * @waiter_list_lock:	lock protecting @waiter_list_head and @status
 * @status:		1: signaled, 0:active, <0: error
 *
 * @wq:			wait queue for fence signaling
 * @sync_fence_list:	membership in global fence list
 */
struct sync_fence {
	struct file		*file;
	struct kref		kref;
	char			name[32];

	/* this list is immutable once the fence is created */
	struct list_head	pt_list_head;

	struct list_head	waiter_list_head;
	spinlock_t		waiter_list_lock; /* also protects status */
	int			status;

	wait_queue_head_t	wq;

	struct list_head	sync_fence_list;
};

struct sync_fence_waiter;
typedef void (*sync_callback_t)(struct sync_fence *fence,
				struct sync_fence_waiter *waiter);

/**
 * struct sync_fence_waiter - metadata for asynchronous waiter on a fence
 * @waiter_list:	membership in sync_fence.waiter_list_head
 * @callback:		function pointer to call when fence signals
 * @callback_data:	pointer to pass to @callback
 */
struct sync_fence_waiter {
	struct list_head	waiter_list;

	sync_callback_t		callback;
};

static inline void sync_fence_waiter_init(struct sync_fence_waiter *waiter,
					  sync_callback_t callback)
{
	waiter->callback = callback;
}

/*
 * API for sync_timeline implementers
 */

/**
 * sync_timeline_create() - creates a sync object
 * @ops:	specifies the implemention ops for the object
 * @size:	size to allocate for this obj
 * @name:	sync_timeline name
 *
 * Creates a new sync_timeline which will use the implemetation specified by
 * @ops.  @size bytes will be allocated allowing for implemntation specific
 * data to be kept after the generic sync_timeline stuct.
 */
struct sync_timeline *sync_timeline_create(const struct sync_timeline_ops *ops,
					   int size, const char *name);

/**
 * sync_timeline_destory() - destorys a sync object
 * @obj:	sync_timeline to destroy
 *
 * A sync implemntation should call this when the @obj is going away
 * (i.e. module unload.)  @obj won't actually be freed until all its childern
 * sync_pts are freed.
 */
void sync_timeline_destroy(struct sync_timeline *obj);

/**
 * sync_timeline_signal() - signal a status change on a sync_timeline
 * @obj:	sync_timeline to signal
 *
 * A sync implemntation should call this any time one of it's sync_pts
 * has signaled or has an error condition.
 */
void sync_timeline_signal(struct sync_timeline *obj);

/**
 * sync_pt_create() - creates a sync pt
 * @parent:	sync_pt's parent sync_timeline
 * @size:	size to allocate for this pt
 *
 * Creates a new sync_pt as a chiled of @parent.  @size bytes will be
 * allocated allowing for implemntation specific data to be kept after
 * the generic sync_timeline struct.
 */
struct sync_pt *sync_pt_create(struct sync_timeline *parent, int size);

/**
 * sync_pt_free() - frees a sync pt
 * @pt:		sync_pt to free
 *
 * This should only be called on sync_pts which have been created but
 * not added to a fence.
 */
void sync_pt_free(struct sync_pt *pt);

/**
 * sync_fence_create() - creates a sync fence
 * @name:	name of fence to create
 * @pt:		sync_pt to add to the fence
 *
 * Creates a fence containg @pt.  Once this is called, the fence takes
 * ownership of @pt.
 */
struct sync_fence *sync_fence_create(const char *name, struct sync_pt *pt);

/*
 * API for sync_fence consumers
 */

/**
 * sync_fence_merge() - merge two fences
 * @name:	name of new fence
 * @a:		fence a
 * @b:		fence b
 *
 * Creates a new fence which contains copies of all the sync_pts in both
 * @a and @b.  @a and @b remain valid, independent fences.
 */
struct sync_fence *sync_fence_merge(const char *name,
				    struct sync_fence *a, struct sync_fence *b);

/**
 * sync_fence_fdget() - get a fence from an fd
 * @fd:		fd referencing a fence
 *
 * Ensures @fd references a valid fence, increments the refcount of the backing
 * file, and returns the fence.
 */
struct sync_fence *sync_fence_fdget(int fd);

/**
 * sync_fence_put() - puts a refernnce of a sync fence
 * @fence:	fence to put
 *
 * Puts a reference on @fence.  If this is the last reference, the fence and
 * all it's sync_pts will be freed
 */
void sync_fence_put(struct sync_fence *fence);

/**
 * sync_fence_install() - installs a fence into a file descriptor
 * @fence:	fence to instal
 * @fd:		file descriptor in which to install the fence
 *
 * Installs @fence into @fd.  @fd's should be acquired through get_unused_fd().
 */
void sync_fence_install(struct sync_fence *fence, int fd);

/**
 * sync_fence_wait_async() - registers and async wait on the fence
 * @fence:		fence to wait on
 * @waiter:		waiter callback struck
 *
 * Returns 1 if @fence has already signaled.
 *
 * Registers a callback to be called when @fence signals or has an error.
 * @waiter should be initialized with sync_fence_waiter_init().
 */
int sync_fence_wait_async(struct sync_fence *fence,
			  struct sync_fence_waiter *waiter);

/**
 * sync_fence_cancel_async() - cancels an async wait
 * @fence:		fence to wait on
 * @waiter:		waiter callback struck
 *
 * returns 0 if waiter was removed from fence's async waiter list.
 * returns -ENOENT if waiter was not found on fence's async waiter list.
 *
 * Cancels a previously registered async wait.  Will fail gracefully if
 * @waiter was never registered or if @fence has already signaled @waiter.
 */
int sync_fence_cancel_async(struct sync_fence *fence,
			    struct sync_fence_waiter *waiter);

/**
 * sync_fence_wait() - wait on fence
 * @fence:	fence to wait on
 * @tiemout:	timeout in ms
 *
 * Wait for @fence to be signaled or have an error.  Waits indefinitely
 * if @timeout < 0
 */
int sync_fence_wait(struct sync_fence *fence, long timeout);

#endif /* __KERNEL__ */

/**
 * struct sync_merge_data - data passed to merge ioctl
 * @fd2:	file descriptor of second fence
 * @name:	name of new fence
 * @fence:	returns the fd of the new fence to userspace
 */
struct sync_merge_data {
	__s32	fd2; /* fd of second fence */
	char	name[32]; /* name of new fence */
	__s32	fence; /* fd on newly created fence */
};

/**
 * struct sync_pt_info - detailed sync_pt information
 * @len:		length of sync_pt_info including any driver_data
 * @obj_name:		name of parent sync_timeline
 * @driver_name:	name of driver implmenting the parent
 * @status:		status of the sync_pt 0:active 1:signaled <0:error
 * @timestamp_ns:	timestamp of status change in nanoseconds
 * @driver_data:	any driver dependant data
 */
struct sync_pt_info {
	__u32	len;
	char	obj_name[32];
	char	driver_name[32];
	__s32	status;
	__u64	timestamp_ns;

	__u8	driver_data[0];
};

/**
 * struct sync_fence_info_data - data returned from fence info ioctl
 * @len:	ioctl caller writes the size of the buffer its passing in.
 *		ioctl returns length of sync_fence_data reutnred to userspace
 *		including pt_info.
 * @name:	name of fence
 * @status:	status of fence. 1: signaled 0:active <0:error
 * @pt_info:	a sync_pt_info struct for every sync_pt in the fence
 */
struct sync_fence_info_data {
	__u32	len;
	char	name[32];
	__s32	status;

	__u8	pt_info[0];
};

#define SYNC_IOC_MAGIC		'>'

/**
 * DOC: SYNC_IOC_WAIT - wait for a fence to signal
 *
 * pass timeout in milliseconds.  Waits indefinitely timeout < 0.
 */
#define SYNC_IOC_WAIT		_IOW(SYNC_IOC_MAGIC, 0, __s32)

/**
 * DOC: SYNC_IOC_MERGE - merge two fences
 *
 * Takes a struct sync_merge_data.  Creates a new fence containing copies of
 * the sync_pts in both the calling fd and sync_merge_data.fd2.  Returns the
 * new fence's fd in sync_merge_data.fence
 */
#define SYNC_IOC_MERGE		_IOWR(SYNC_IOC_MAGIC, 1, struct sync_merge_data)

/**
 * DOC: SYNC_IOC_FENCE_INFO - get detailed information on a fence
 *
 * Takes a struct sync_fence_info_data with extra space allocated for pt_info.
 * Caller should write the size of the buffer into len.  On return, len is
 * updated to reflect the total size of the sync_fence_info_data including
 * pt_info.
 *
 * pt_info is a buffer containing sync_pt_infos for every sync_pt in the fence.
 * To itterate over the sync_pt_infos, use the sync_pt_info.len field.
 */
#define SYNC_IOC_FENCE_INFO	_IOWR(SYNC_IOC_MAGIC, 2,\
	struct sync_fence_info_data)

#endif /* _LINUX_SYNC_H */
