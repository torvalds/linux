/* General filesystem caching backing cache interface
 *
 * Copyright (C) 2004-2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * NOTE!!! See:
 *
 *	Documentation/filesystems/caching/backend-api.txt
 *
 * for a description of the cache backend interface declared here.
 */

#ifndef _LINUX_FSCACHE_CACHE_H
#define _LINUX_FSCACHE_CACHE_H

#include <linux/fscache.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#define NR_MAXCACHES BITS_PER_LONG

struct fscache_cache;
struct fscache_cache_ops;
struct fscache_object;
struct fscache_operation;

/*
 * cache tag definition
 */
struct fscache_cache_tag {
	struct list_head	link;
	struct fscache_cache	*cache;		/* cache referred to by this tag */
	unsigned long		flags;
#define FSCACHE_TAG_RESERVED	0		/* T if tag is reserved for a cache */
	atomic_t		usage;
	char			name[0];	/* tag name */
};

/*
 * cache definition
 */
struct fscache_cache {
	const struct fscache_cache_ops *ops;
	struct fscache_cache_tag *tag;		/* tag representing this cache */
	struct kobject		*kobj;		/* system representation of this cache */
	struct list_head	link;		/* link in list of caches */
	size_t			max_index_size;	/* maximum size of index data */
	char			identifier[36];	/* cache label */

	/* node management */
	struct work_struct	op_gc;		/* operation garbage collector */
	struct list_head	object_list;	/* list of data/index objects */
	struct list_head	op_gc_list;	/* list of ops to be deleted */
	spinlock_t		object_list_lock;
	spinlock_t		op_gc_list_lock;
	atomic_t		object_count;	/* no. of live objects in this cache */
	struct fscache_object	*fsdef;		/* object for the fsdef index */
	unsigned long		flags;
#define FSCACHE_IOERROR		0	/* cache stopped on I/O error */
#define FSCACHE_CACHE_WITHDRAWN	1	/* cache has been withdrawn */
};

extern wait_queue_head_t fscache_cache_cleared_wq;

/*
 * operation to be applied to a cache object
 * - retrieval initiation operations are done in the context of the process
 *   that issued them, and not in an async thread pool
 */
typedef void (*fscache_operation_release_t)(struct fscache_operation *op);
typedef void (*fscache_operation_processor_t)(struct fscache_operation *op);

struct fscache_operation {
	struct work_struct	work;		/* record for async ops */
	struct list_head	pend_link;	/* link in object->pending_ops */
	struct fscache_object	*object;	/* object to be operated upon */

	unsigned long		flags;
#define FSCACHE_OP_TYPE		0x000f	/* operation type */
#define FSCACHE_OP_ASYNC	0x0001	/* - async op, processor may sleep for disk */
#define FSCACHE_OP_MYTHREAD	0x0002	/* - processing is done be issuing thread, not pool */
#define FSCACHE_OP_WAITING	4	/* cleared when op is woken */
#define FSCACHE_OP_EXCLUSIVE	5	/* exclusive op, other ops must wait */
#define FSCACHE_OP_DEAD		6	/* op is now dead */
#define FSCACHE_OP_DEC_READ_CNT	7	/* decrement object->n_reads on destruction */
#define FSCACHE_OP_KEEP_FLAGS	0xc0	/* flags to keep when repurposing an op */

	atomic_t		usage;
	unsigned		debug_id;	/* debugging ID */

	/* operation processor callback
	 * - can be NULL if FSCACHE_OP_WAITING is going to be used to perform
	 *   the op in a non-pool thread */
	fscache_operation_processor_t processor;

	/* operation releaser */
	fscache_operation_release_t release;
};

extern atomic_t fscache_op_debug_id;
extern void fscache_op_work_func(struct work_struct *work);

extern void fscache_enqueue_operation(struct fscache_operation *);
extern void fscache_put_operation(struct fscache_operation *);

/**
 * fscache_operation_init - Do basic initialisation of an operation
 * @op: The operation to initialise
 * @release: The release function to assign
 *
 * Do basic initialisation of an operation.  The caller must still set flags,
 * object and processor if needed.
 */
static inline void fscache_operation_init(struct fscache_operation *op,
					fscache_operation_processor_t processor,
					fscache_operation_release_t release)
{
	INIT_WORK(&op->work, fscache_op_work_func);
	atomic_set(&op->usage, 1);
	op->debug_id = atomic_inc_return(&fscache_op_debug_id);
	op->processor = processor;
	op->release = release;
	INIT_LIST_HEAD(&op->pend_link);
}

/*
 * data read operation
 */
struct fscache_retrieval {
	struct fscache_operation op;
	struct address_space	*mapping;	/* netfs pages */
	fscache_rw_complete_t	end_io_func;	/* function to call on I/O completion */
	void			*context;	/* netfs read context (pinned) */
	struct list_head	to_do;		/* list of things to be done by the backend */
	unsigned long		start_time;	/* time at which retrieval started */
};

typedef int (*fscache_page_retrieval_func_t)(struct fscache_retrieval *op,
					     struct page *page,
					     gfp_t gfp);

typedef int (*fscache_pages_retrieval_func_t)(struct fscache_retrieval *op,
					      struct list_head *pages,
					      unsigned *nr_pages,
					      gfp_t gfp);

/**
 * fscache_get_retrieval - Get an extra reference on a retrieval operation
 * @op: The retrieval operation to get a reference on
 *
 * Get an extra reference on a retrieval operation.
 */
static inline
struct fscache_retrieval *fscache_get_retrieval(struct fscache_retrieval *op)
{
	atomic_inc(&op->op.usage);
	return op;
}

/**
 * fscache_enqueue_retrieval - Enqueue a retrieval operation for processing
 * @op: The retrieval operation affected
 *
 * Enqueue a retrieval operation for processing by the FS-Cache thread pool.
 */
static inline void fscache_enqueue_retrieval(struct fscache_retrieval *op)
{
	fscache_enqueue_operation(&op->op);
}

/**
 * fscache_put_retrieval - Drop a reference to a retrieval operation
 * @op: The retrieval operation affected
 *
 * Drop a reference to a retrieval operation.
 */
static inline void fscache_put_retrieval(struct fscache_retrieval *op)
{
	fscache_put_operation(&op->op);
}

/*
 * cached page storage work item
 * - used to do three things:
 *   - batch writes to the cache
 *   - do cache writes asynchronously
 *   - defer writes until cache object lookup completion
 */
struct fscache_storage {
	struct fscache_operation op;
	pgoff_t			store_limit;	/* don't write more than this */
};

/*
 * cache operations
 */
struct fscache_cache_ops {
	/* name of cache provider */
	const char *name;

	/* allocate an object record for a cookie */
	struct fscache_object *(*alloc_object)(struct fscache_cache *cache,
					       struct fscache_cookie *cookie);

	/* look up the object for a cookie
	 * - return -ETIMEDOUT to be requeued
	 */
	int (*lookup_object)(struct fscache_object *object);

	/* finished looking up */
	void (*lookup_complete)(struct fscache_object *object);

	/* increment the usage count on this object (may fail if unmounting) */
	struct fscache_object *(*grab_object)(struct fscache_object *object);

	/* pin an object in the cache */
	int (*pin_object)(struct fscache_object *object);

	/* unpin an object in the cache */
	void (*unpin_object)(struct fscache_object *object);

	/* store the updated auxiliary data on an object */
	void (*update_object)(struct fscache_object *object);

	/* discard the resources pinned by an object and effect retirement if
	 * necessary */
	void (*drop_object)(struct fscache_object *object);

	/* dispose of a reference to an object */
	void (*put_object)(struct fscache_object *object);

	/* sync a cache */
	void (*sync_cache)(struct fscache_cache *cache);

	/* notification that the attributes of a non-index object (such as
	 * i_size) have changed */
	int (*attr_changed)(struct fscache_object *object);

	/* reserve space for an object's data and associated metadata */
	int (*reserve_space)(struct fscache_object *object, loff_t i_size);

	/* request a backing block for a page be read or allocated in the
	 * cache */
	fscache_page_retrieval_func_t read_or_alloc_page;

	/* request backing blocks for a list of pages be read or allocated in
	 * the cache */
	fscache_pages_retrieval_func_t read_or_alloc_pages;

	/* request a backing block for a page be allocated in the cache so that
	 * it can be written directly */
	fscache_page_retrieval_func_t allocate_page;

	/* request backing blocks for pages be allocated in the cache so that
	 * they can be written directly */
	fscache_pages_retrieval_func_t allocate_pages;

	/* write a page to its backing block in the cache */
	int (*write_page)(struct fscache_storage *op, struct page *page);

	/* detach backing block from a page (optional)
	 * - must release the cookie lock before returning
	 * - may sleep
	 */
	void (*uncache_page)(struct fscache_object *object,
			     struct page *page);

	/* dissociate a cache from all the pages it was backing */
	void (*dissociate_pages)(struct fscache_cache *cache);
};

/*
 * data file or index object cookie
 * - a file will only appear in one cache
 * - a request to cache a file may or may not be honoured, subject to
 *   constraints such as disk space
 * - indices are created on disk just-in-time
 */
struct fscache_cookie {
	atomic_t			usage;		/* number of users of this cookie */
	atomic_t			n_children;	/* number of children of this cookie */
	spinlock_t			lock;
	spinlock_t			stores_lock;	/* lock on page store tree */
	struct hlist_head		backing_objects; /* object(s) backing this file/index */
	const struct fscache_cookie_def	*def;		/* definition */
	struct fscache_cookie		*parent;	/* parent of this entry */
	void				*netfs_data;	/* back pointer to netfs */
	struct radix_tree_root		stores;		/* pages to be stored on this cookie */
#define FSCACHE_COOKIE_PENDING_TAG	0		/* pages tag: pending write to cache */
#define FSCACHE_COOKIE_STORING_TAG	1		/* pages tag: writing to cache */

	unsigned long			flags;
#define FSCACHE_COOKIE_LOOKING_UP	0	/* T if non-index cookie being looked up still */
#define FSCACHE_COOKIE_CREATING		1	/* T if non-index object being created still */
#define FSCACHE_COOKIE_NO_DATA_YET	2	/* T if new object with no cached data yet */
#define FSCACHE_COOKIE_PENDING_FILL	3	/* T if pending initial fill on object */
#define FSCACHE_COOKIE_FILLING		4	/* T if filling object incrementally */
#define FSCACHE_COOKIE_UNAVAILABLE	5	/* T if cookie is unavailable (error, etc) */
};

extern struct fscache_cookie fscache_fsdef_index;

/*
 * on-disk cache file or index handle
 */
struct fscache_object {
	enum fscache_object_state {
		FSCACHE_OBJECT_INIT,		/* object in initial unbound state */
		FSCACHE_OBJECT_LOOKING_UP,	/* looking up object */
		FSCACHE_OBJECT_CREATING,	/* creating object */

		/* active states */
		FSCACHE_OBJECT_AVAILABLE,	/* cleaning up object after creation */
		FSCACHE_OBJECT_ACTIVE,		/* object is usable */
		FSCACHE_OBJECT_UPDATING,	/* object is updating */

		/* terminal states */
		FSCACHE_OBJECT_DYING,		/* object waiting for accessors to finish */
		FSCACHE_OBJECT_LC_DYING,	/* object cleaning up after lookup/create */
		FSCACHE_OBJECT_ABORT_INIT,	/* abort the init state */
		FSCACHE_OBJECT_RELEASING,	/* releasing object */
		FSCACHE_OBJECT_RECYCLING,	/* retiring object */
		FSCACHE_OBJECT_WITHDRAWING,	/* withdrawing object */
		FSCACHE_OBJECT_DEAD,		/* object is now dead */
		FSCACHE_OBJECT__NSTATES
	} state;

	int			debug_id;	/* debugging ID */
	int			n_children;	/* number of child objects */
	int			n_ops;		/* number of ops outstanding on object */
	int			n_obj_ops;	/* number of object ops outstanding on object */
	int			n_in_progress;	/* number of ops in progress */
	int			n_exclusive;	/* number of exclusive ops queued */
	atomic_t		n_reads;	/* number of read ops in progress */
	spinlock_t		lock;		/* state and operations lock */

	unsigned long		lookup_jif;	/* time at which lookup started */
	unsigned long		event_mask;	/* events this object is interested in */
	unsigned long		events;		/* events to be processed by this object
						 * (order is important - using fls) */
#define FSCACHE_OBJECT_EV_REQUEUE	0	/* T if object should be requeued */
#define FSCACHE_OBJECT_EV_UPDATE	1	/* T if object should be updated */
#define FSCACHE_OBJECT_EV_CLEARED	2	/* T if accessors all gone */
#define FSCACHE_OBJECT_EV_ERROR		3	/* T if fatal error occurred during processing */
#define FSCACHE_OBJECT_EV_RELEASE	4	/* T if netfs requested object release */
#define FSCACHE_OBJECT_EV_RETIRE	5	/* T if netfs requested object retirement */
#define FSCACHE_OBJECT_EV_WITHDRAW	6	/* T if cache requested object withdrawal */
#define FSCACHE_OBJECT_EVENTS_MASK	0x7f	/* mask of all events*/

	unsigned long		flags;
#define FSCACHE_OBJECT_LOCK		0	/* T if object is busy being processed */
#define FSCACHE_OBJECT_PENDING_WRITE	1	/* T if object has pending write */
#define FSCACHE_OBJECT_WAITING		2	/* T if object is waiting on its parent */

	struct list_head	cache_link;	/* link in cache->object_list */
	struct hlist_node	cookie_link;	/* link in cookie->backing_objects */
	struct fscache_cache	*cache;		/* cache that supplied this object */
	struct fscache_cookie	*cookie;	/* netfs's file/index object */
	struct fscache_object	*parent;	/* parent object */
	struct work_struct	work;		/* attention scheduling record */
	struct list_head	dependents;	/* FIFO of dependent objects */
	struct list_head	dep_link;	/* link in parent's dependents list */
	struct list_head	pending_ops;	/* unstarted operations on this object */
#ifdef CONFIG_FSCACHE_OBJECT_LIST
	struct rb_node		objlist_link;	/* link in global object list */
#endif
	pgoff_t			store_limit;	/* current storage limit */
	loff_t			store_limit_l;	/* current storage limit */
};

extern const char *fscache_object_states[];

#define fscache_object_is_active(obj)			      \
	(!test_bit(FSCACHE_IOERROR, &(obj)->cache->flags) &&  \
	 (obj)->state >= FSCACHE_OBJECT_AVAILABLE &&	      \
	 (obj)->state < FSCACHE_OBJECT_DYING)

#define fscache_object_is_dead(obj)				\
	(test_bit(FSCACHE_IOERROR, &(obj)->cache->flags) &&	\
	 (obj)->state >= FSCACHE_OBJECT_DYING)

extern void fscache_object_work_func(struct work_struct *work);

/**
 * fscache_object_init - Initialise a cache object description
 * @object: Object description
 *
 * Initialise a cache object description to its basic values.
 *
 * See Documentation/filesystems/caching/backend-api.txt for a complete
 * description.
 */
static inline
void fscache_object_init(struct fscache_object *object,
			 struct fscache_cookie *cookie,
			 struct fscache_cache *cache)
{
	atomic_inc(&cache->object_count);

	object->state = FSCACHE_OBJECT_INIT;
	spin_lock_init(&object->lock);
	INIT_LIST_HEAD(&object->cache_link);
	INIT_HLIST_NODE(&object->cookie_link);
	INIT_WORK(&object->work, fscache_object_work_func);
	INIT_LIST_HEAD(&object->dependents);
	INIT_LIST_HEAD(&object->dep_link);
	INIT_LIST_HEAD(&object->pending_ops);
	object->n_children = 0;
	object->n_ops = object->n_in_progress = object->n_exclusive = 0;
	object->events = object->event_mask = 0;
	object->flags = 0;
	object->store_limit = 0;
	object->store_limit_l = 0;
	object->cache = cache;
	object->cookie = cookie;
	object->parent = NULL;
}

extern void fscache_object_lookup_negative(struct fscache_object *object);
extern void fscache_obtained_object(struct fscache_object *object);

#ifdef CONFIG_FSCACHE_OBJECT_LIST
extern void fscache_object_destroy(struct fscache_object *object);
#else
#define fscache_object_destroy(object) do {} while(0)
#endif

/**
 * fscache_object_destroyed - Note destruction of an object in a cache
 * @cache: The cache from which the object came
 *
 * Note the destruction and deallocation of an object record in a cache.
 */
static inline void fscache_object_destroyed(struct fscache_cache *cache)
{
	if (atomic_dec_and_test(&cache->object_count))
		wake_up_all(&fscache_cache_cleared_wq);
}

/**
 * fscache_object_lookup_error - Note an object encountered an error
 * @object: The object on which the error was encountered
 *
 * Note that an object encountered a fatal error (usually an I/O error) and
 * that it should be withdrawn as soon as possible.
 */
static inline void fscache_object_lookup_error(struct fscache_object *object)
{
	set_bit(FSCACHE_OBJECT_EV_ERROR, &object->events);
}

/**
 * fscache_set_store_limit - Set the maximum size to be stored in an object
 * @object: The object to set the maximum on
 * @i_size: The limit to set in bytes
 *
 * Set the maximum size an object is permitted to reach, implying the highest
 * byte that may be written.  Intended to be called by the attr_changed() op.
 *
 * See Documentation/filesystems/caching/backend-api.txt for a complete
 * description.
 */
static inline
void fscache_set_store_limit(struct fscache_object *object, loff_t i_size)
{
	object->store_limit_l = i_size;
	object->store_limit = i_size >> PAGE_SHIFT;
	if (i_size & ~PAGE_MASK)
		object->store_limit++;
}

/**
 * fscache_end_io - End a retrieval operation on a page
 * @op: The FS-Cache operation covering the retrieval
 * @page: The page that was to be fetched
 * @error: The error code (0 if successful)
 *
 * Note the end of an operation to retrieve a page, as covered by a particular
 * operation record.
 */
static inline void fscache_end_io(struct fscache_retrieval *op,
				  struct page *page, int error)
{
	op->end_io_func(page, op->context, error);
}

/*
 * out-of-line cache backend functions
 */
extern void fscache_init_cache(struct fscache_cache *cache,
			       const struct fscache_cache_ops *ops,
			       const char *idfmt,
			       ...) __attribute__ ((format (printf, 3, 4)));

extern int fscache_add_cache(struct fscache_cache *cache,
			     struct fscache_object *fsdef,
			     const char *tagname);
extern void fscache_withdraw_cache(struct fscache_cache *cache);

extern void fscache_io_error(struct fscache_cache *cache);

extern void fscache_mark_pages_cached(struct fscache_retrieval *op,
				      struct pagevec *pagevec);

extern bool fscache_object_sleep_till_congested(signed long *timeoutp);

extern enum fscache_checkaux fscache_check_aux(struct fscache_object *object,
					       const void *data,
					       uint16_t datalen);

#endif /* _LINUX_FSCACHE_CACHE_H */
