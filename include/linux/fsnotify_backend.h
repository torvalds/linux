/*
 * Filesystem access notification for Linux
 *
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

#ifndef __LINUX_FSNOTIFY_BACKEND_H
#define __LINUX_FSNOTIFY_BACKEND_H

#ifdef __KERNEL__

#include <linux/fs.h> /* struct inode */
#include <linux/list.h>
#include <linux/path.h> /* struct path */
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/atomic.h>

/*
 * IN_* from inotfy.h lines up EXACTLY with FS_*, this is so we can easily
 * convert between them.  dnotify only needs conversion at watch creation
 * so no perf loss there.  fanotify isn't defined yet, so it can use the
 * wholes if it needs more events.
 */
#define FS_ACCESS		0x00000001	/* File was accessed */
#define FS_MODIFY		0x00000002	/* File was modified */
#define FS_ATTRIB		0x00000004	/* Metadata changed */
#define FS_CLOSE_WRITE		0x00000008	/* Writtable file was closed */
#define FS_CLOSE_NOWRITE	0x00000010	/* Unwrittable file closed */
#define FS_OPEN			0x00000020	/* File was opened */
#define FS_MOVED_FROM		0x00000040	/* File was moved from X */
#define FS_MOVED_TO		0x00000080	/* File was moved to Y */
#define FS_CREATE		0x00000100	/* Subfile was created */
#define FS_DELETE		0x00000200	/* Subfile was deleted */
#define FS_DELETE_SELF		0x00000400	/* Self was deleted */
#define FS_MOVE_SELF		0x00000800	/* Self was moved */

#define FS_UNMOUNT		0x00002000	/* inode on umount fs */
#define FS_Q_OVERFLOW		0x00004000	/* Event queued overflowed */
#define FS_IN_IGNORED		0x00008000	/* last inotify event here */

#define FS_IN_ISDIR		0x40000000	/* event occurred against dir */
#define FS_IN_ONESHOT		0x80000000	/* only send event once */

#define FS_DN_RENAME		0x10000000	/* file renamed */
#define FS_DN_MULTISHOT		0x20000000	/* dnotify multishot */

struct fsnotify_group;
struct fsnotify_event;

/*
 * Each group much define these ops.  The fsnotify infrastructure will call
 * these operations for each relevant group.
 *
 * handle_event - main call for a group to handle an fs event
 * free_group_priv - called when a group refcnt hits 0 to clean up the private union
 */
struct fsnotify_ops {
	int (*handle_event)(struct fsnotify_group *group, struct fsnotify_event *event);
	void (*free_group_priv)(struct fsnotify_group *group);
};

/*
 * A group is a "thing" that wants to receive notification about filesystem
 * events.  The mask holds the subset of event types this group cares about.
 * refcnt on a group is up to the implementor and at any moment if it goes 0
 * everything will be cleaned up.
 */
struct fsnotify_group {
	/*
	 * global list of all groups receiving events from fsnotify.
	 * anchored by fsnotify_groups and protected by either fsnotify_grp_mutex
	 * or fsnotify_grp_srcu depending on write vs read.
	 */
	struct list_head group_list;

	/*
	 * Defines all of the event types in which this group is interested.
	 * This mask is a bitwise OR of the FS_* events from above.  Each time
	 * this mask changes for a group (if it changes) the correct functions
	 * must be called to update the global structures which indicate global
	 * interest in event types.
	 */
	__u32 mask;

	/*
	 * How the refcnt is used is up to each group.  When the refcnt hits 0
	 * fsnotify will clean up all of the resources associated with this group.
	 * As an example, the dnotify group will always have a refcnt=1 and that
	 * will never change.  Inotify, on the other hand, has a group per
	 * inotify_init() and the refcnt will hit 0 only when that fd has been
	 * closed.
	 */
	atomic_t refcnt;		/* things with interest in this group */
	unsigned int group_num;		/* simply prevents accidental group collision */

	const struct fsnotify_ops *ops;	/* how this group handles things */

	/* prevents double list_del of group_list.  protected by global fsnotify_gr_mutex */
	bool on_group_list;

	/* groups can define private fields here or use the void *private */
	union {
		void *private;
	};
};

/*
 * all of the information about the original object we want to now send to
 * a group.  If you want to carry more info from the accessing task to the
 * listener this structure is where you need to be adding fields.
 */
struct fsnotify_event {
	spinlock_t lock;	/* protection for the associated event_holder and private_list */
	/* to_tell may ONLY be dereferenced during handle_event(). */
	struct inode *to_tell;	/* either the inode the event happened to or its parent */
	/*
	 * depending on the event type we should have either a path or inode
	 * We hold a reference on path, but NOT on inode.  Since we have the ref on
	 * the path, it may be dereferenced at any point during this object's
	 * lifetime.  That reference is dropped when this object's refcnt hits
	 * 0.  If this event contains an inode instead of a path, the inode may
	 * ONLY be used during handle_event().
	 */
	union {
		struct path path;
		struct inode *inode;
	};
/* when calling fsnotify tell it if the data is a path or inode */
#define FSNOTIFY_EVENT_NONE	0
#define FSNOTIFY_EVENT_PATH	1
#define FSNOTIFY_EVENT_INODE	2
#define FSNOTIFY_EVENT_FILE	3
	int data_type;		/* which of the above union we have */
	atomic_t refcnt;	/* how many groups still are using/need to send this event */
	__u32 mask;		/* the type of access, bitwise OR for FS_* event types */
};

#ifdef CONFIG_FSNOTIFY

/* called from the vfs helpers */

/* main fsnotify call to send events */
extern void fsnotify(struct inode *to_tell, __u32 mask, void *data, int data_is);


/* called from fsnotify listeners, such as fanotify or dnotify */

/* must call when a group changes its ->mask */
extern void fsnotify_recalc_global_mask(void);
/* get a reference to an existing or create a new group */
extern struct fsnotify_group *fsnotify_obtain_group(unsigned int group_num,
						    __u32 mask,
						    const struct fsnotify_ops *ops);
/* drop reference on a group from fsnotify_obtain_group */
extern void fsnotify_put_group(struct fsnotify_group *group);

/* take a reference to an event */
extern void fsnotify_get_event(struct fsnotify_event *event);
extern void fsnotify_put_event(struct fsnotify_event *event);
/* find private data previously attached to an event */
extern struct fsnotify_event_private_data *fsnotify_get_priv_from_event(struct fsnotify_group *group,
									struct fsnotify_event *event);

/* put here because inotify does some weird stuff when destroying watches */
extern struct fsnotify_event *fsnotify_create_event(struct inode *to_tell, __u32 mask,
						    void *data, int data_is);
#else

static inline void fsnotify(struct inode *to_tell, __u32 mask, void *data, int data_is)
{}
#endif	/* CONFIG_FSNOTIFY */

#endif	/* __KERNEL __ */

#endif	/* __LINUX_FSNOTIFY_BACKEND_H */
