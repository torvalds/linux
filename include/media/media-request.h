// SPDX-License-Identifier: GPL-2.0
/*
 * Media device request objects
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 * Copyright (C) 2018 Intel Corporation
 *
 * Author: Hans Verkuil <hans.verkuil@cisco.com>
 * Author: Sakari Ailus <sakari.ailus@linux.intel.com>
 */

#ifndef MEDIA_REQUEST_H
#define MEDIA_REQUEST_H

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/refcount.h>

#include <media/media-device.h>

/**
 * enum media_request_state - media request state
 *
 * @MEDIA_REQUEST_STATE_IDLE:		Idle
 * @MEDIA_REQUEST_STATE_VALIDATING:	Validating the request, no state changes
 *					allowed
 * @MEDIA_REQUEST_STATE_QUEUED:		Queued
 * @MEDIA_REQUEST_STATE_COMPLETE:	Completed, the request is done
 * @MEDIA_REQUEST_STATE_CLEANING:	Cleaning, the request is being re-inited
 * @MEDIA_REQUEST_STATE_UPDATING:	The request is being updated, i.e.
 *					request objects are being added,
 *					modified or removed
 * @NR_OF_MEDIA_REQUEST_STATE:		The number of media request states, used
 *					internally for sanity check purposes
 */
enum media_request_state {
	MEDIA_REQUEST_STATE_IDLE,
	MEDIA_REQUEST_STATE_VALIDATING,
	MEDIA_REQUEST_STATE_QUEUED,
	MEDIA_REQUEST_STATE_COMPLETE,
	MEDIA_REQUEST_STATE_CLEANING,
	MEDIA_REQUEST_STATE_UPDATING,
	NR_OF_MEDIA_REQUEST_STATE,
};

struct media_request_object;

/**
 * struct media_request - Media device request
 * @mdev: Media device this request belongs to
 * @kref: Reference count
 * @debug_str: Prefix for debug messages (process name:fd)
 * @state: The state of the request
 * @updating_count: count the number of request updates that are in progress
 * @access_count: count the number of request accesses that are in progress
 * @objects: List of @struct media_request_object request objects
 * @num_incomplete_objects: The number of incomplete objects in the request
 * @poll_wait: Wait queue for poll
 * @lock: Serializes access to this struct
 */
struct media_request {
	struct media_device *mdev;
	struct kref kref;
	char debug_str[TASK_COMM_LEN + 11];
	enum media_request_state state;
	unsigned int updating_count;
	unsigned int access_count;
	struct list_head objects;
	unsigned int num_incomplete_objects;
	wait_queue_head_t poll_wait;
	spinlock_t lock;
};

#ifdef CONFIG_MEDIA_CONTROLLER

/**
 * media_request_lock_for_access - Lock the request to access its objects
 *
 * @req: The media request
 *
 * Use before accessing a completed request. A reference to the request must
 * be held during the access. This usually takes place automatically through
 * a file handle. Use @media_request_unlock_for_access when done.
 */
static inline int __must_check
media_request_lock_for_access(struct media_request *req)
{
	unsigned long flags;
	int ret = -EBUSY;

	spin_lock_irqsave(&req->lock, flags);
	if (req->state == MEDIA_REQUEST_STATE_COMPLETE) {
		req->access_count++;
		ret = 0;
	}
	spin_unlock_irqrestore(&req->lock, flags);

	return ret;
}

/**
 * media_request_unlock_for_access - Unlock a request previously locked for
 *				     access
 *
 * @req: The media request
 *
 * Unlock a request that has previously been locked using
 * @media_request_lock_for_access.
 */
static inline void media_request_unlock_for_access(struct media_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&req->lock, flags);
	if (!WARN_ON(!req->access_count))
		req->access_count--;
	spin_unlock_irqrestore(&req->lock, flags);
}

/**
 * media_request_lock_for_update - Lock the request for updating its objects
 *
 * @req: The media request
 *
 * Use before updating a request, i.e. adding, modifying or removing a request
 * object in it. A reference to the request must be held during the update. This
 * usually takes place automatically through a file handle. Use
 * @media_request_unlock_for_update when done.
 */
static inline int __must_check
media_request_lock_for_update(struct media_request *req)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&req->lock, flags);
	if (req->state == MEDIA_REQUEST_STATE_IDLE ||
	    req->state == MEDIA_REQUEST_STATE_UPDATING) {
		req->state = MEDIA_REQUEST_STATE_UPDATING;
		req->updating_count++;
	} else {
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&req->lock, flags);

	return ret;
}

/**
 * media_request_unlock_for_update - Unlock a request previously locked for
 *				     update
 *
 * @req: The media request
 *
 * Unlock a request that has previously been locked using
 * @media_request_lock_for_update.
 */
static inline void media_request_unlock_for_update(struct media_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&req->lock, flags);
	WARN_ON(req->updating_count <= 0);
	if (!--req->updating_count)
		req->state = MEDIA_REQUEST_STATE_IDLE;
	spin_unlock_irqrestore(&req->lock, flags);
}

/**
 * media_request_get - Get the media request
 *
 * @req: The media request
 *
 * Get the media request.
 */
static inline void media_request_get(struct media_request *req)
{
	kref_get(&req->kref);
}

/**
 * media_request_put - Put the media request
 *
 * @req: The media request
 *
 * Put the media request. The media request will be released
 * when the refcount reaches 0.
 */
void media_request_put(struct media_request *req);

/**
 * media_request_get_by_fd - Get a media request by fd
 *
 * @mdev: Media device this request belongs to
 * @request_fd: The file descriptor of the request
 *
 * Get the request represented by @request_fd that is owned
 * by the media device.
 *
 * Return a -EACCES error pointer if requests are not supported
 * by this driver. Return -EINVAL if the request was not found.
 * Return the pointer to the request if found: the caller will
 * have to call @media_request_put when it finished using the
 * request.
 */
struct media_request *
media_request_get_by_fd(struct media_device *mdev, int request_fd);

/**
 * media_request_alloc - Allocate the media request
 *
 * @mdev: Media device this request belongs to
 * @alloc_fd: Store the request's file descriptor in this int
 *
 * Allocated the media request and put the fd in @alloc_fd.
 */
int media_request_alloc(struct media_device *mdev,
			int *alloc_fd);

#else

static inline void media_request_get(struct media_request *req)
{
}

static inline void media_request_put(struct media_request *req)
{
}

static inline struct media_request *
media_request_get_by_fd(struct media_device *mdev, int request_fd)
{
	return ERR_PTR(-EACCES);
}

#endif

/**
 * struct media_request_object_ops - Media request object operations
 * @prepare: Validate and prepare the request object, optional.
 * @unprepare: Unprepare the request object, optional.
 * @queue: Queue the request object, optional.
 * @unbind: Unbind the request object, optional.
 * @release: Release the request object, required.
 */
struct media_request_object_ops {
	int (*prepare)(struct media_request_object *object);
	void (*unprepare)(struct media_request_object *object);
	void (*queue)(struct media_request_object *object);
	void (*unbind)(struct media_request_object *object);
	void (*release)(struct media_request_object *object);
};

/**
 * struct media_request_object - An opaque object that belongs to a media
 *				 request
 *
 * @ops: object's operations
 * @priv: object's priv pointer
 * @req: the request this object belongs to (can be NULL)
 * @list: List entry of the object for @struct media_request
 * @kref: Reference count of the object, acquire before releasing req->lock
 * @completed: If true, then this object was completed.
 *
 * An object related to the request. This struct is always embedded in
 * another struct that contains the actual data for this request object.
 */
struct media_request_object {
	const struct media_request_object_ops *ops;
	void *priv;
	struct media_request *req;
	struct list_head list;
	struct kref kref;
	bool completed;
};

#ifdef CONFIG_MEDIA_CONTROLLER

/**
 * media_request_object_get - Get a media request object
 *
 * @obj: The object
 *
 * Get a media request object.
 */
static inline void media_request_object_get(struct media_request_object *obj)
{
	kref_get(&obj->kref);
}

/**
 * media_request_object_put - Put a media request object
 *
 * @obj: The object
 *
 * Put a media request object. Once all references are gone, the
 * object's memory is released.
 */
void media_request_object_put(struct media_request_object *obj);

/**
 * media_request_object_find - Find an object in a request
 *
 * @req: The media request
 * @ops: Find an object with this ops value
 * @priv: Find an object with this priv value
 *
 * Both @ops and @priv must be non-NULL.
 *
 * Returns the object pointer or NULL if not found. The caller must
 * call media_request_object_put() once it finished using the object.
 *
 * Since this function needs to walk the list of objects it takes
 * the @req->lock spin lock to make this safe.
 */
struct media_request_object *
media_request_object_find(struct media_request *req,
			  const struct media_request_object_ops *ops,
			  void *priv);

/**
 * media_request_object_init - Initialise a media request object
 *
 * @obj: The object
 *
 * Initialise a media request object. The object will be released using the
 * release callback of the ops once it has no references (this function
 * initialises references to one).
 */
void media_request_object_init(struct media_request_object *obj);

/**
 * media_request_object_bind - Bind a media request object to a request
 *
 * @req: The media request
 * @ops: The object ops for this object
 * @priv: A driver-specific priv pointer associated with this object
 * @is_buffer: Set to true if the object a buffer object.
 * @obj: The object
 *
 * Bind this object to the request and set the ops and priv values of
 * the object so it can be found later with media_request_object_find().
 *
 * Every bound object must be unbound or completed by the kernel at some
 * point in time, otherwise the request will never complete. When the
 * request is released all completed objects will be unbound by the
 * request core code.
 *
 * Buffer objects will be added to the end of the request's object
 * list, non-buffer objects will be added to the front of the list.
 * This ensures that all buffer objects are at the end of the list
 * and that all non-buffer objects that they depend on are processed
 * first.
 */
int media_request_object_bind(struct media_request *req,
			      const struct media_request_object_ops *ops,
			      void *priv, bool is_buffer,
			      struct media_request_object *obj);

/**
 * media_request_object_unbind - Unbind a media request object
 *
 * @obj: The object
 *
 * Unbind the media request object from the request.
 */
void media_request_object_unbind(struct media_request_object *obj);

/**
 * media_request_object_complete - Mark the media request object as complete
 *
 * @obj: The object
 *
 * Mark the media request object as complete. Only bound objects can
 * be completed.
 */
void media_request_object_complete(struct media_request_object *obj);

#else

static inline int __must_check
media_request_lock_for_access(struct media_request *req)
{
	return -EINVAL;
}

static inline void media_request_unlock_for_access(struct media_request *req)
{
}

static inline int __must_check
media_request_lock_for_update(struct media_request *req)
{
	return -EINVAL;
}

static inline void media_request_unlock_for_update(struct media_request *req)
{
}

static inline void media_request_object_get(struct media_request_object *obj)
{
}

static inline void media_request_object_put(struct media_request_object *obj)
{
}

static inline struct media_request_object *
media_request_object_find(struct media_request *req,
			  const struct media_request_object_ops *ops,
			  void *priv)
{
	return NULL;
}

static inline void media_request_object_init(struct media_request_object *obj)
{
	obj->ops = NULL;
	obj->req = NULL;
}

static inline int media_request_object_bind(struct media_request *req,
			       const struct media_request_object_ops *ops,
			       void *priv, bool is_buffer,
			       struct media_request_object *obj)
{
	return 0;
}

static inline void media_request_object_unbind(struct media_request_object *obj)
{
}

static inline void media_request_object_complete(struct media_request_object *obj)
{
}

#endif

#endif
