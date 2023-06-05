// SPDX-License-Identifier: GPL-2.0
/* User-mappable watch queue
 *
 * Copyright (C) 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * See Documentation/core-api/watch_queue.rst
 */

#ifndef _LINUX_WATCH_QUEUE_H
#define _LINUX_WATCH_QUEUE_H

#include <uapi/linux/watch_queue.h>
#include <linux/kref.h>
#include <linux/rcupdate.h>

#ifdef CONFIG_WATCH_QUEUE

struct cred;

struct watch_type_filter {
	enum watch_notification_type type;
	__u32		subtype_filter[1];	/* Bitmask of subtypes to filter on */
	__u32		info_filter;		/* Filter on watch_notification::info */
	__u32		info_mask;		/* Mask of relevant bits in info_filter */
};

struct watch_filter {
	union {
		struct rcu_head	rcu;
		/* Bitmask of accepted types */
		DECLARE_BITMAP(type_filter, WATCH_TYPE__NR);
	};
	u32			nr_filters;	/* Number of filters */
	struct watch_type_filter filters[];
};

struct watch_queue {
	struct rcu_head		rcu;
	struct watch_filter __rcu *filter;
	struct pipe_inode_info	*pipe;		/* Pipe we use as a buffer, NULL if queue closed */
	struct hlist_head	watches;	/* Contributory watches */
	struct page		**notes;	/* Preallocated notifications */
	unsigned long		*notes_bitmap;	/* Allocation bitmap for notes */
	struct kref		usage;		/* Object usage count */
	spinlock_t		lock;
	unsigned int		nr_notes;	/* Number of notes */
	unsigned int		nr_pages;	/* Number of pages in notes[] */
};

/*
 * Representation of a watch on an object.
 */
struct watch {
	union {
		struct rcu_head	rcu;
		u32		info_id;	/* ID to be OR'd in to info field */
	};
	struct watch_queue __rcu *queue;	/* Queue to post events to */
	struct hlist_node	queue_node;	/* Link in queue->watches */
	struct watch_list __rcu	*watch_list;
	struct hlist_node	list_node;	/* Link in watch_list->watchers */
	const struct cred	*cred;		/* Creds of the owner of the watch */
	void			*private;	/* Private data for the watched object */
	u64			id;		/* Internal identifier */
	struct kref		usage;		/* Object usage count */
};

/*
 * List of watches on an object.
 */
struct watch_list {
	struct rcu_head		rcu;
	struct hlist_head	watchers;
	void (*release_watch)(struct watch *);
	spinlock_t		lock;
};

extern void __post_watch_notification(struct watch_list *,
				      struct watch_notification *,
				      const struct cred *,
				      u64);
extern struct watch_queue *get_watch_queue(int);
extern void put_watch_queue(struct watch_queue *);
extern void init_watch(struct watch *, struct watch_queue *);
extern int add_watch_to_object(struct watch *, struct watch_list *);
extern int remove_watch_from_object(struct watch_list *, struct watch_queue *, u64, bool);
extern long watch_queue_set_size(struct pipe_inode_info *, unsigned int);
extern long watch_queue_set_filter(struct pipe_inode_info *,
				   struct watch_notification_filter __user *);
extern int watch_queue_init(struct pipe_inode_info *);
extern void watch_queue_clear(struct watch_queue *);

static inline void init_watch_list(struct watch_list *wlist,
				   void (*release_watch)(struct watch *))
{
	INIT_HLIST_HEAD(&wlist->watchers);
	spin_lock_init(&wlist->lock);
	wlist->release_watch = release_watch;
}

static inline void post_watch_notification(struct watch_list *wlist,
					   struct watch_notification *n,
					   const struct cred *cred,
					   u64 id)
{
	if (unlikely(wlist))
		__post_watch_notification(wlist, n, cred, id);
}

static inline void remove_watch_list(struct watch_list *wlist, u64 id)
{
	if (wlist) {
		remove_watch_from_object(wlist, NULL, id, true);
		kfree_rcu(wlist, rcu);
	}
}

/**
 * watch_sizeof - Calculate the information part of the size of a watch record,
 * given the structure size.
 */
#define watch_sizeof(STRUCT) (sizeof(STRUCT) << WATCH_INFO_LENGTH__SHIFT)

#else
static inline int watch_queue_init(struct pipe_inode_info *pipe)
{
	return -ENOPKG;
}

#endif

#endif /* _LINUX_WATCH_QUEUE_H */
