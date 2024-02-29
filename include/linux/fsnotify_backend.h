/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Filesystem access notification for Linux
 *
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

#ifndef __LINUX_FSNOTIFY_BACKEND_H
#define __LINUX_FSNOTIFY_BACKEND_H

#ifdef __KERNEL__

#include <linux/idr.h> /* inotify uses this */
#include <linux/fs.h> /* struct inode */
#include <linux/list.h>
#include <linux/path.h> /* struct path */
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/user_namespace.h>
#include <linux/refcount.h>
#include <linux/mempool.h>
#include <linux/sched/mm.h>

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
#define FS_OPEN_EXEC		0x00001000	/* File was opened for exec */

#define FS_UNMOUNT		0x00002000	/* inode on umount fs */
#define FS_Q_OVERFLOW		0x00004000	/* Event queued overflowed */
#define FS_ERROR		0x00008000	/* Filesystem Error (fanotify) */

/*
 * FS_IN_IGNORED overloads FS_ERROR.  It is only used internally by inotify
 * which does not support FS_ERROR.
 */
#define FS_IN_IGNORED		0x00008000	/* last inotify event here */

#define FS_OPEN_PERM		0x00010000	/* open event in an permission hook */
#define FS_ACCESS_PERM		0x00020000	/* access event in a permissions hook */
#define FS_OPEN_EXEC_PERM	0x00040000	/* open/exec event in a permission hook */

/*
 * Set on inode mark that cares about things that happen to its children.
 * Always set for dnotify and inotify.
 * Set on inode/sb/mount marks that care about parent/name info.
 */
#define FS_EVENT_ON_CHILD	0x08000000

#define FS_RENAME		0x10000000	/* File was renamed */
#define FS_DN_MULTISHOT		0x20000000	/* dnotify multishot */
#define FS_ISDIR		0x40000000	/* event occurred against dir */

#define FS_MOVE			(FS_MOVED_FROM | FS_MOVED_TO)

/*
 * Directory entry modification events - reported only to directory
 * where entry is modified and not to a watching parent.
 * The watching parent may get an FS_ATTRIB|FS_EVENT_ON_CHILD event
 * when a directory entry inside a child subdir changes.
 */
#define ALL_FSNOTIFY_DIRENT_EVENTS (FS_CREATE | FS_DELETE | FS_MOVE | FS_RENAME)

#define ALL_FSNOTIFY_PERM_EVENTS (FS_OPEN_PERM | FS_ACCESS_PERM | \
				  FS_OPEN_EXEC_PERM)

/*
 * This is a list of all events that may get sent to a parent that is watching
 * with flag FS_EVENT_ON_CHILD based on fs event on a child of that directory.
 */
#define FS_EVENTS_POSS_ON_CHILD   (ALL_FSNOTIFY_PERM_EVENTS | \
				   FS_ACCESS | FS_MODIFY | FS_ATTRIB | \
				   FS_CLOSE_WRITE | FS_CLOSE_NOWRITE | \
				   FS_OPEN | FS_OPEN_EXEC)

/*
 * This is a list of all events that may get sent with the parent inode as the
 * @to_tell argument of fsnotify().
 * It may include events that can be sent to an inode/sb/mount mark, but cannot
 * be sent to a parent watching children.
 */
#define FS_EVENTS_POSS_TO_PARENT (FS_EVENTS_POSS_ON_CHILD)

/* Events that can be reported to backends */
#define ALL_FSNOTIFY_EVENTS (ALL_FSNOTIFY_DIRENT_EVENTS | \
			     FS_EVENTS_POSS_ON_CHILD | \
			     FS_DELETE_SELF | FS_MOVE_SELF | \
			     FS_UNMOUNT | FS_Q_OVERFLOW | FS_IN_IGNORED | \
			     FS_ERROR)

/* Extra flags that may be reported with event or control handling of events */
#define ALL_FSNOTIFY_FLAGS  (FS_ISDIR | FS_EVENT_ON_CHILD | FS_DN_MULTISHOT)

#define ALL_FSNOTIFY_BITS   (ALL_FSNOTIFY_EVENTS | ALL_FSNOTIFY_FLAGS)

struct fsnotify_group;
struct fsnotify_event;
struct fsnotify_mark;
struct fsnotify_event_private_data;
struct fsnotify_fname;
struct fsnotify_iter_info;

struct mem_cgroup;

/*
 * Each group much define these ops.  The fsnotify infrastructure will call
 * these operations for each relevant group.
 *
 * handle_event - main call for a group to handle an fs event
 * @group:	group to notify
 * @mask:	event type and flags
 * @data:	object that event happened on
 * @data_type:	type of object for fanotify_data_XXX() accessors
 * @dir:	optional directory associated with event -
 *		if @file_name is not NULL, this is the directory that
 *		@file_name is relative to
 * @file_name:	optional file name associated with event
 * @cookie:	inotify rename cookie
 * @iter_info:	array of marks from this group that are interested in the event
 *
 * handle_inode_event - simple variant of handle_event() for groups that only
 *		have inode marks and don't have ignore mask
 * @mark:	mark to notify
 * @mask:	event type and flags
 * @inode:	inode that event happened on
 * @dir:	optional directory associated with event -
 *		if @file_name is not NULL, this is the directory that
 *		@file_name is relative to.
 *		Either @inode or @dir must be non-NULL.
 * @file_name:	optional file name associated with event
 * @cookie:	inotify rename cookie
 *
 * free_group_priv - called when a group refcnt hits 0 to clean up the private union
 * freeing_mark - called when a mark is being destroyed for some reason.  The group
 *		MUST be holding a reference on each mark and that reference must be
 *		dropped in this function.  inotify uses this function to send
 *		userspace messages that marks have been removed.
 */
struct fsnotify_ops {
	int (*handle_event)(struct fsnotify_group *group, u32 mask,
			    const void *data, int data_type, struct inode *dir,
			    const struct qstr *file_name, u32 cookie,
			    struct fsnotify_iter_info *iter_info);
	int (*handle_inode_event)(struct fsnotify_mark *mark, u32 mask,
			    struct inode *inode, struct inode *dir,
			    const struct qstr *file_name, u32 cookie);
	void (*free_group_priv)(struct fsnotify_group *group);
	void (*freeing_mark)(struct fsnotify_mark *mark, struct fsnotify_group *group);
	void (*free_event)(struct fsnotify_group *group, struct fsnotify_event *event);
	/* called on final put+free to free memory */
	void (*free_mark)(struct fsnotify_mark *mark);
};

/*
 * all of the information about the original object we want to now send to
 * a group.  If you want to carry more info from the accessing task to the
 * listener this structure is where you need to be adding fields.
 */
struct fsnotify_event {
	struct list_head list;
};

/*
 * A group is a "thing" that wants to receive notification about filesystem
 * events.  The mask holds the subset of event types this group cares about.
 * refcnt on a group is up to the implementor and at any moment if it goes 0
 * everything will be cleaned up.
 */
struct fsnotify_group {
	const struct fsnotify_ops *ops;	/* how this group handles things */

	/*
	 * How the refcnt is used is up to each group.  When the refcnt hits 0
	 * fsnotify will clean up all of the resources associated with this group.
	 * As an example, the dnotify group will always have a refcnt=1 and that
	 * will never change.  Inotify, on the other hand, has a group per
	 * inotify_init() and the refcnt will hit 0 only when that fd has been
	 * closed.
	 */
	refcount_t refcnt;		/* things with interest in this group */

	/* needed to send notification to userspace */
	spinlock_t notification_lock;		/* protect the notification_list */
	struct list_head notification_list;	/* list of event_holder this group needs to send to userspace */
	wait_queue_head_t notification_waitq;	/* read() on the notification file blocks on this waitq */
	unsigned int q_len;			/* events on the queue */
	unsigned int max_events;		/* maximum events allowed on the list */
	/*
	 * Valid fsnotify group priorities.  Events are send in order from highest
	 * priority to lowest priority.  We default to the lowest priority.
	 */
	#define FS_PRIO_0	0 /* normal notifiers, no permissions */
	#define FS_PRIO_1	1 /* fanotify content based access control */
	#define FS_PRIO_2	2 /* fanotify pre-content access */
	unsigned int priority;
	bool shutdown;		/* group is being shut down, don't queue more events */

#define FSNOTIFY_GROUP_USER	0x01 /* user allocated group */
#define FSNOTIFY_GROUP_DUPS	0x02 /* allow multiple marks per object */
#define FSNOTIFY_GROUP_NOFS	0x04 /* group lock is not direct reclaim safe */
	int flags;
	unsigned int owner_flags;	/* stored flags of mark_mutex owner */

	/* stores all fastpath marks assoc with this group so they can be cleaned on unregister */
	struct mutex mark_mutex;	/* protect marks_list */
	atomic_t user_waits;		/* Number of tasks waiting for user
					 * response */
	struct list_head marks_list;	/* all inode marks for this group */

	struct fasync_struct *fsn_fa;    /* async notification */

	struct fsnotify_event *overflow_event;	/* Event we queue when the
						 * notification list is too
						 * full */

	struct mem_cgroup *memcg;	/* memcg to charge allocations */

	/* groups can define private fields here or use the void *private */
	union {
		void *private;
#ifdef CONFIG_INOTIFY_USER
		struct inotify_group_private_data {
			spinlock_t	idr_lock;
			struct idr      idr;
			struct ucounts *ucounts;
		} inotify_data;
#endif
#ifdef CONFIG_FANOTIFY
		struct fanotify_group_private_data {
			/* Hash table of events for merge */
			struct hlist_head *merge_hash;
			/* allows a group to block waiting for a userspace response */
			struct list_head access_list;
			wait_queue_head_t access_waitq;
			int flags;           /* flags from fanotify_init() */
			int f_flags; /* event_f_flags from fanotify_init() */
			struct ucounts *ucounts;
			mempool_t error_events_pool;
		} fanotify_data;
#endif /* CONFIG_FANOTIFY */
	};
};

/*
 * These helpers are used to prevent deadlock when reclaiming inodes with
 * evictable marks of the same group that is allocating a new mark.
 */
static inline void fsnotify_group_lock(struct fsnotify_group *group)
{
	mutex_lock(&group->mark_mutex);
	if (group->flags & FSNOTIFY_GROUP_NOFS)
		group->owner_flags = memalloc_nofs_save();
}

static inline void fsnotify_group_unlock(struct fsnotify_group *group)
{
	if (group->flags & FSNOTIFY_GROUP_NOFS)
		memalloc_nofs_restore(group->owner_flags);
	mutex_unlock(&group->mark_mutex);
}

static inline void fsnotify_group_assert_locked(struct fsnotify_group *group)
{
	WARN_ON_ONCE(!mutex_is_locked(&group->mark_mutex));
	if (group->flags & FSNOTIFY_GROUP_NOFS)
		WARN_ON_ONCE(!(current->flags & PF_MEMALLOC_NOFS));
}

/* When calling fsnotify tell it if the data is a path or inode */
enum fsnotify_data_type {
	FSNOTIFY_EVENT_NONE,
	FSNOTIFY_EVENT_PATH,
	FSNOTIFY_EVENT_INODE,
	FSNOTIFY_EVENT_DENTRY,
	FSNOTIFY_EVENT_ERROR,
};

struct fs_error_report {
	int error;
	struct inode *inode;
	struct super_block *sb;
};

static inline struct inode *fsnotify_data_inode(const void *data, int data_type)
{
	switch (data_type) {
	case FSNOTIFY_EVENT_INODE:
		return (struct inode *)data;
	case FSNOTIFY_EVENT_DENTRY:
		return d_inode(data);
	case FSNOTIFY_EVENT_PATH:
		return d_inode(((const struct path *)data)->dentry);
	case FSNOTIFY_EVENT_ERROR:
		return ((struct fs_error_report *)data)->inode;
	default:
		return NULL;
	}
}

static inline struct dentry *fsnotify_data_dentry(const void *data, int data_type)
{
	switch (data_type) {
	case FSNOTIFY_EVENT_DENTRY:
		/* Non const is needed for dget() */
		return (struct dentry *)data;
	case FSNOTIFY_EVENT_PATH:
		return ((const struct path *)data)->dentry;
	default:
		return NULL;
	}
}

static inline const struct path *fsnotify_data_path(const void *data,
						    int data_type)
{
	switch (data_type) {
	case FSNOTIFY_EVENT_PATH:
		return data;
	default:
		return NULL;
	}
}

static inline struct super_block *fsnotify_data_sb(const void *data,
						   int data_type)
{
	switch (data_type) {
	case FSNOTIFY_EVENT_INODE:
		return ((struct inode *)data)->i_sb;
	case FSNOTIFY_EVENT_DENTRY:
		return ((struct dentry *)data)->d_sb;
	case FSNOTIFY_EVENT_PATH:
		return ((const struct path *)data)->dentry->d_sb;
	case FSNOTIFY_EVENT_ERROR:
		return ((struct fs_error_report *) data)->sb;
	default:
		return NULL;
	}
}

static inline struct fs_error_report *fsnotify_data_error_report(
							const void *data,
							int data_type)
{
	switch (data_type) {
	case FSNOTIFY_EVENT_ERROR:
		return (struct fs_error_report *) data;
	default:
		return NULL;
	}
}

/*
 * Index to merged marks iterator array that correlates to a type of watch.
 * The type of watched object can be deduced from the iterator type, but not
 * the other way around, because an event can match different watched objects
 * of the same object type.
 * For example, both parent and child are watching an object of type inode.
 */
enum fsnotify_iter_type {
	FSNOTIFY_ITER_TYPE_INODE,
	FSNOTIFY_ITER_TYPE_VFSMOUNT,
	FSNOTIFY_ITER_TYPE_SB,
	FSNOTIFY_ITER_TYPE_PARENT,
	FSNOTIFY_ITER_TYPE_INODE2,
	FSNOTIFY_ITER_TYPE_COUNT
};

/* The type of object that a mark is attached to */
enum fsnotify_obj_type {
	FSNOTIFY_OBJ_TYPE_ANY = -1,
	FSNOTIFY_OBJ_TYPE_INODE,
	FSNOTIFY_OBJ_TYPE_VFSMOUNT,
	FSNOTIFY_OBJ_TYPE_SB,
	FSNOTIFY_OBJ_TYPE_COUNT,
	FSNOTIFY_OBJ_TYPE_DETACHED = FSNOTIFY_OBJ_TYPE_COUNT
};

static inline bool fsnotify_valid_obj_type(unsigned int obj_type)
{
	return (obj_type < FSNOTIFY_OBJ_TYPE_COUNT);
}

struct fsnotify_iter_info {
	struct fsnotify_mark *marks[FSNOTIFY_ITER_TYPE_COUNT];
	struct fsnotify_group *current_group;
	unsigned int report_mask;
	int srcu_idx;
};

static inline bool fsnotify_iter_should_report_type(
		struct fsnotify_iter_info *iter_info, int iter_type)
{
	return (iter_info->report_mask & (1U << iter_type));
}

static inline void fsnotify_iter_set_report_type(
		struct fsnotify_iter_info *iter_info, int iter_type)
{
	iter_info->report_mask |= (1U << iter_type);
}

static inline struct fsnotify_mark *fsnotify_iter_mark(
		struct fsnotify_iter_info *iter_info, int iter_type)
{
	if (fsnotify_iter_should_report_type(iter_info, iter_type))
		return iter_info->marks[iter_type];
	return NULL;
}

static inline int fsnotify_iter_step(struct fsnotify_iter_info *iter, int type,
				     struct fsnotify_mark **markp)
{
	while (type < FSNOTIFY_ITER_TYPE_COUNT) {
		*markp = fsnotify_iter_mark(iter, type);
		if (*markp)
			break;
		type++;
	}
	return type;
}

#define FSNOTIFY_ITER_FUNCS(name, NAME) \
static inline struct fsnotify_mark *fsnotify_iter_##name##_mark( \
		struct fsnotify_iter_info *iter_info) \
{ \
	return fsnotify_iter_mark(iter_info, FSNOTIFY_ITER_TYPE_##NAME); \
}

FSNOTIFY_ITER_FUNCS(inode, INODE)
FSNOTIFY_ITER_FUNCS(parent, PARENT)
FSNOTIFY_ITER_FUNCS(vfsmount, VFSMOUNT)
FSNOTIFY_ITER_FUNCS(sb, SB)

#define fsnotify_foreach_iter_type(type) \
	for (type = 0; type < FSNOTIFY_ITER_TYPE_COUNT; type++)
#define fsnotify_foreach_iter_mark_type(iter, mark, type) \
	for (type = 0; \
	     type = fsnotify_iter_step(iter, type, &mark), \
	     type < FSNOTIFY_ITER_TYPE_COUNT; \
	     type++)

/*
 * fsnotify_connp_t is what we embed in objects which connector can be attached
 * to. fsnotify_connp_t * is how we refer from connector back to object.
 */
struct fsnotify_mark_connector;
typedef struct fsnotify_mark_connector __rcu *fsnotify_connp_t;

/*
 * Inode/vfsmount/sb point to this structure which tracks all marks attached to
 * the inode/vfsmount/sb. The reference to inode/vfsmount/sb is held by this
 * structure. We destroy this structure when there are no more marks attached
 * to it. The structure is protected by fsnotify_mark_srcu.
 */
struct fsnotify_mark_connector {
	spinlock_t lock;
	unsigned short type;	/* Type of object [lock] */
#define FSNOTIFY_CONN_FLAG_HAS_IREF	0x02
	unsigned short flags;	/* flags [lock] */
	union {
		/* Object pointer [lock] */
		fsnotify_connp_t *obj;
		/* Used listing heads to free after srcu period expires */
		struct fsnotify_mark_connector *destroy_next;
	};
	struct hlist_head list;
};

/*
 * A mark is simply an object attached to an in core inode which allows an
 * fsnotify listener to indicate they are either no longer interested in events
 * of a type matching mask or only interested in those events.
 *
 * These are flushed when an inode is evicted from core and may be flushed
 * when the inode is modified (as seen by fsnotify_access).  Some fsnotify
 * users (such as dnotify) will flush these when the open fd is closed and not
 * at inode eviction or modification.
 *
 * Text in brackets is showing the lock(s) protecting modifications of a
 * particular entry. obj_lock means either inode->i_lock or
 * mnt->mnt_root->d_lock depending on the mark type.
 */
struct fsnotify_mark {
	/* Mask this mark is for [mark->lock, group->mark_mutex] */
	__u32 mask;
	/* We hold one for presence in g_list. Also one ref for each 'thing'
	 * in kernel that found and may be using this mark. */
	refcount_t refcnt;
	/* Group this mark is for. Set on mark creation, stable until last ref
	 * is dropped */
	struct fsnotify_group *group;
	/* List of marks by group->marks_list. Also reused for queueing
	 * mark into destroy_list when it's waiting for the end of SRCU period
	 * before it can be freed. [group->mark_mutex] */
	struct list_head g_list;
	/* Protects inode / mnt pointers, flags, masks */
	spinlock_t lock;
	/* List of marks for inode / vfsmount [connector->lock, mark ref] */
	struct hlist_node obj_list;
	/* Head of list of marks for an object [mark ref] */
	struct fsnotify_mark_connector *connector;
	/* Events types and flags to ignore [mark->lock, group->mark_mutex] */
	__u32 ignore_mask;
	/* General fsnotify mark flags */
#define FSNOTIFY_MARK_FLAG_ALIVE		0x0001
#define FSNOTIFY_MARK_FLAG_ATTACHED		0x0002
	/* inotify mark flags */
#define FSNOTIFY_MARK_FLAG_EXCL_UNLINK		0x0010
#define FSNOTIFY_MARK_FLAG_IN_ONESHOT		0x0020
	/* fanotify mark flags */
#define FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY	0x0100
#define FSNOTIFY_MARK_FLAG_NO_IREF		0x0200
#define FSNOTIFY_MARK_FLAG_HAS_IGNORE_FLAGS	0x0400
#define FSNOTIFY_MARK_FLAG_HAS_FSID		0x0800
#define FSNOTIFY_MARK_FLAG_WEAK_FSID		0x1000
	unsigned int flags;		/* flags [mark->lock] */
};

#ifdef CONFIG_FSNOTIFY

/* called from the vfs helpers */

/* main fsnotify call to send events */
extern int fsnotify(__u32 mask, const void *data, int data_type,
		    struct inode *dir, const struct qstr *name,
		    struct inode *inode, u32 cookie);
extern int __fsnotify_parent(struct dentry *dentry, __u32 mask, const void *data,
			   int data_type);
extern void __fsnotify_inode_delete(struct inode *inode);
extern void __fsnotify_vfsmount_delete(struct vfsmount *mnt);
extern void fsnotify_sb_delete(struct super_block *sb);
extern u32 fsnotify_get_cookie(void);

static inline __u32 fsnotify_parent_needed_mask(__u32 mask)
{
	/* FS_EVENT_ON_CHILD is set on marks that want parent/name info */
	if (!(mask & FS_EVENT_ON_CHILD))
		return 0;
	/*
	 * This object might be watched by a mark that cares about parent/name
	 * info, does it care about the specific set of events that can be
	 * reported with parent/name info?
	 */
	return mask & FS_EVENTS_POSS_TO_PARENT;
}

static inline int fsnotify_inode_watches_children(struct inode *inode)
{
	/* FS_EVENT_ON_CHILD is set if the inode may care */
	if (!(inode->i_fsnotify_mask & FS_EVENT_ON_CHILD))
		return 0;
	/* this inode might care about child events, does it care about the
	 * specific set of events that can happen on a child? */
	return inode->i_fsnotify_mask & FS_EVENTS_POSS_ON_CHILD;
}

/*
 * Update the dentry with a flag indicating the interest of its parent to receive
 * filesystem events when those events happens to this dentry->d_inode.
 */
static inline void fsnotify_update_flags(struct dentry *dentry)
{
	assert_spin_locked(&dentry->d_lock);

	/*
	 * Serialisation of setting PARENT_WATCHED on the dentries is provided
	 * by d_lock. If inotify_inode_watched changes after we have taken
	 * d_lock, the following __fsnotify_update_child_dentry_flags call will
	 * find our entry, so it will spin until we complete here, and update
	 * us with the new state.
	 */
	if (fsnotify_inode_watches_children(dentry->d_parent->d_inode))
		dentry->d_flags |= DCACHE_FSNOTIFY_PARENT_WATCHED;
	else
		dentry->d_flags &= ~DCACHE_FSNOTIFY_PARENT_WATCHED;
}

/* called from fsnotify listeners, such as fanotify or dnotify */

/* create a new group */
extern struct fsnotify_group *fsnotify_alloc_group(
				const struct fsnotify_ops *ops,
				int flags);
/* get reference to a group */
extern void fsnotify_get_group(struct fsnotify_group *group);
/* drop reference on a group from fsnotify_alloc_group */
extern void fsnotify_put_group(struct fsnotify_group *group);
/* group destruction begins, stop queuing new events */
extern void fsnotify_group_stop_queueing(struct fsnotify_group *group);
/* destroy group */
extern void fsnotify_destroy_group(struct fsnotify_group *group);
/* fasync handler function */
extern int fsnotify_fasync(int fd, struct file *file, int on);
/* Free event from memory */
extern void fsnotify_destroy_event(struct fsnotify_group *group,
				   struct fsnotify_event *event);
/* attach the event to the group notification queue */
extern int fsnotify_insert_event(struct fsnotify_group *group,
				 struct fsnotify_event *event,
				 int (*merge)(struct fsnotify_group *,
					      struct fsnotify_event *),
				 void (*insert)(struct fsnotify_group *,
						struct fsnotify_event *));

static inline int fsnotify_add_event(struct fsnotify_group *group,
				     struct fsnotify_event *event,
				     int (*merge)(struct fsnotify_group *,
						  struct fsnotify_event *))
{
	return fsnotify_insert_event(group, event, merge, NULL);
}

/* Queue overflow event to a notification group */
static inline void fsnotify_queue_overflow(struct fsnotify_group *group)
{
	fsnotify_add_event(group, group->overflow_event, NULL);
}

static inline bool fsnotify_is_overflow_event(u32 mask)
{
	return mask & FS_Q_OVERFLOW;
}

static inline bool fsnotify_notify_queue_is_empty(struct fsnotify_group *group)
{
	assert_spin_locked(&group->notification_lock);

	return list_empty(&group->notification_list);
}

extern bool fsnotify_notify_queue_is_empty(struct fsnotify_group *group);
/* return, but do not dequeue the first event on the notification queue */
extern struct fsnotify_event *fsnotify_peek_first_event(struct fsnotify_group *group);
/* return AND dequeue the first event on the notification queue */
extern struct fsnotify_event *fsnotify_remove_first_event(struct fsnotify_group *group);
/* Remove event queued in the notification list */
extern void fsnotify_remove_queued_event(struct fsnotify_group *group,
					 struct fsnotify_event *event);

/* functions used to manipulate the marks attached to inodes */

/*
 * Canonical "ignore mask" including event flags.
 *
 * Note the subtle semantic difference from the legacy ->ignored_mask.
 * ->ignored_mask traditionally only meant which events should be ignored,
 * while ->ignore_mask also includes flags regarding the type of objects on
 * which events should be ignored.
 */
static inline __u32 fsnotify_ignore_mask(struct fsnotify_mark *mark)
{
	__u32 ignore_mask = mark->ignore_mask;

	/* The event flags in ignore mask take effect */
	if (mark->flags & FSNOTIFY_MARK_FLAG_HAS_IGNORE_FLAGS)
		return ignore_mask;

	/*
	 * Legacy behavior:
	 * - Always ignore events on dir
	 * - Ignore events on child if parent is watching children
	 */
	ignore_mask |= FS_ISDIR;
	ignore_mask &= ~FS_EVENT_ON_CHILD;
	ignore_mask |= mark->mask & FS_EVENT_ON_CHILD;

	return ignore_mask;
}

/* Legacy ignored_mask - only event types to ignore */
static inline __u32 fsnotify_ignored_events(struct fsnotify_mark *mark)
{
	return mark->ignore_mask & ALL_FSNOTIFY_EVENTS;
}

/*
 * Check if mask (or ignore mask) should be applied depending if victim is a
 * directory and whether it is reported to a watching parent.
 */
static inline bool fsnotify_mask_applicable(__u32 mask, bool is_dir,
					    int iter_type)
{
	/* Should mask be applied to a directory? */
	if (is_dir && !(mask & FS_ISDIR))
		return false;

	/* Should mask be applied to a child? */
	if (iter_type == FSNOTIFY_ITER_TYPE_PARENT &&
	    !(mask & FS_EVENT_ON_CHILD))
		return false;

	return true;
}

/*
 * Effective ignore mask taking into account if event victim is a
 * directory and whether it is reported to a watching parent.
 */
static inline __u32 fsnotify_effective_ignore_mask(struct fsnotify_mark *mark,
						   bool is_dir, int iter_type)
{
	__u32 ignore_mask = fsnotify_ignored_events(mark);

	if (!ignore_mask)
		return 0;

	/* For non-dir and non-child, no need to consult the event flags */
	if (!is_dir && iter_type != FSNOTIFY_ITER_TYPE_PARENT)
		return ignore_mask;

	ignore_mask = fsnotify_ignore_mask(mark);
	if (!fsnotify_mask_applicable(ignore_mask, is_dir, iter_type))
		return 0;

	return ignore_mask & ALL_FSNOTIFY_EVENTS;
}

/* Get mask for calculating object interest taking ignore mask into account */
static inline __u32 fsnotify_calc_mask(struct fsnotify_mark *mark)
{
	__u32 mask = mark->mask;

	if (!fsnotify_ignored_events(mark))
		return mask;

	/* Interest in FS_MODIFY may be needed for clearing ignore mask */
	if (!(mark->flags & FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY))
		mask |= FS_MODIFY;

	/*
	 * If mark is interested in ignoring events on children, the object must
	 * show interest in those events for fsnotify_parent() to notice it.
	 */
	return mask | mark->ignore_mask;
}

/* Get mask of events for a list of marks */
extern __u32 fsnotify_conn_mask(struct fsnotify_mark_connector *conn);
/* Calculate mask of events for a list of marks */
extern void fsnotify_recalc_mask(struct fsnotify_mark_connector *conn);
extern void fsnotify_init_mark(struct fsnotify_mark *mark,
			       struct fsnotify_group *group);
/* Find mark belonging to given group in the list of marks */
extern struct fsnotify_mark *fsnotify_find_mark(fsnotify_connp_t *connp,
						struct fsnotify_group *group);
/* attach the mark to the object */
extern int fsnotify_add_mark(struct fsnotify_mark *mark,
			     fsnotify_connp_t *connp, unsigned int obj_type,
			     int add_flags);
extern int fsnotify_add_mark_locked(struct fsnotify_mark *mark,
				    fsnotify_connp_t *connp,
				    unsigned int obj_type, int add_flags);

/* attach the mark to the inode */
static inline int fsnotify_add_inode_mark(struct fsnotify_mark *mark,
					  struct inode *inode,
					  int add_flags)
{
	return fsnotify_add_mark(mark, &inode->i_fsnotify_marks,
				 FSNOTIFY_OBJ_TYPE_INODE, add_flags);
}
static inline int fsnotify_add_inode_mark_locked(struct fsnotify_mark *mark,
						 struct inode *inode,
						 int add_flags)
{
	return fsnotify_add_mark_locked(mark, &inode->i_fsnotify_marks,
					FSNOTIFY_OBJ_TYPE_INODE, add_flags);
}

/* given a group and a mark, flag mark to be freed when all references are dropped */
extern void fsnotify_destroy_mark(struct fsnotify_mark *mark,
				  struct fsnotify_group *group);
/* detach mark from inode / mount list, group list, drop inode reference */
extern void fsnotify_detach_mark(struct fsnotify_mark *mark);
/* free mark */
extern void fsnotify_free_mark(struct fsnotify_mark *mark);
/* Wait until all marks queued for destruction are destroyed */
extern void fsnotify_wait_marks_destroyed(void);
/* Clear all of the marks of a group attached to a given object type */
extern void fsnotify_clear_marks_by_group(struct fsnotify_group *group,
					  unsigned int obj_type);
/* run all the marks in a group, and clear all of the vfsmount marks */
static inline void fsnotify_clear_vfsmount_marks_by_group(struct fsnotify_group *group)
{
	fsnotify_clear_marks_by_group(group, FSNOTIFY_OBJ_TYPE_VFSMOUNT);
}
/* run all the marks in a group, and clear all of the inode marks */
static inline void fsnotify_clear_inode_marks_by_group(struct fsnotify_group *group)
{
	fsnotify_clear_marks_by_group(group, FSNOTIFY_OBJ_TYPE_INODE);
}
/* run all the marks in a group, and clear all of the sn marks */
static inline void fsnotify_clear_sb_marks_by_group(struct fsnotify_group *group)
{
	fsnotify_clear_marks_by_group(group, FSNOTIFY_OBJ_TYPE_SB);
}
extern void fsnotify_get_mark(struct fsnotify_mark *mark);
extern void fsnotify_put_mark(struct fsnotify_mark *mark);
extern void fsnotify_finish_user_wait(struct fsnotify_iter_info *iter_info);
extern bool fsnotify_prepare_user_wait(struct fsnotify_iter_info *iter_info);

static inline void fsnotify_init_event(struct fsnotify_event *event)
{
	INIT_LIST_HEAD(&event->list);
}

#else

static inline int fsnotify(__u32 mask, const void *data, int data_type,
			   struct inode *dir, const struct qstr *name,
			   struct inode *inode, u32 cookie)
{
	return 0;
}

static inline int __fsnotify_parent(struct dentry *dentry, __u32 mask,
				  const void *data, int data_type)
{
	return 0;
}

static inline void __fsnotify_inode_delete(struct inode *inode)
{}

static inline void __fsnotify_vfsmount_delete(struct vfsmount *mnt)
{}

static inline void fsnotify_sb_delete(struct super_block *sb)
{}

static inline void fsnotify_update_flags(struct dentry *dentry)
{}

static inline u32 fsnotify_get_cookie(void)
{
	return 0;
}

static inline void fsnotify_unmount_inodes(struct super_block *sb)
{}

#endif	/* CONFIG_FSNOTIFY */

#endif	/* __KERNEL __ */

#endif	/* __LINUX_FSNOTIFY_BACKEND_H */
