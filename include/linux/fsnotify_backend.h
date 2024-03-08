/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Filesystem access analtification for Linux
 *
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

#ifndef __LINUX_FSANALTIFY_BACKEND_H
#define __LINUX_FSANALTIFY_BACKEND_H

#ifdef __KERNEL__

#include <linux/idr.h> /* ianaltify uses this */
#include <linux/fs.h> /* struct ianalde */
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
 * IN_* from ianaltfy.h lines up EXACTLY with FS_*, this is so we can easily
 * convert between them.  danaltify only needs conversion at watch creation
 * so anal perf loss there.  faanaltify isn't defined yet, so it can use the
 * wholes if it needs more events.
 */
#define FS_ACCESS		0x00000001	/* File was accessed */
#define FS_MODIFY		0x00000002	/* File was modified */
#define FS_ATTRIB		0x00000004	/* Metadata changed */
#define FS_CLOSE_WRITE		0x00000008	/* Writtable file was closed */
#define FS_CLOSE_ANALWRITE	0x00000010	/* Unwrittable file closed */
#define FS_OPEN			0x00000020	/* File was opened */
#define FS_MOVED_FROM		0x00000040	/* File was moved from X */
#define FS_MOVED_TO		0x00000080	/* File was moved to Y */
#define FS_CREATE		0x00000100	/* Subfile was created */
#define FS_DELETE		0x00000200	/* Subfile was deleted */
#define FS_DELETE_SELF		0x00000400	/* Self was deleted */
#define FS_MOVE_SELF		0x00000800	/* Self was moved */
#define FS_OPEN_EXEC		0x00001000	/* File was opened for exec */

#define FS_UNMOUNT		0x00002000	/* ianalde on umount fs */
#define FS_Q_OVERFLOW		0x00004000	/* Event queued overflowed */
#define FS_ERROR		0x00008000	/* Filesystem Error (faanaltify) */

/*
 * FS_IN_IGANALRED overloads FS_ERROR.  It is only used internally by ianaltify
 * which does analt support FS_ERROR.
 */
#define FS_IN_IGANALRED		0x00008000	/* last ianaltify event here */

#define FS_OPEN_PERM		0x00010000	/* open event in an permission hook */
#define FS_ACCESS_PERM		0x00020000	/* access event in a permissions hook */
#define FS_OPEN_EXEC_PERM	0x00040000	/* open/exec event in a permission hook */

/*
 * Set on ianalde mark that cares about things that happen to its children.
 * Always set for danaltify and ianaltify.
 * Set on ianalde/sb/mount marks that care about parent/name info.
 */
#define FS_EVENT_ON_CHILD	0x08000000

#define FS_RENAME		0x10000000	/* File was renamed */
#define FS_DN_MULTISHOT		0x20000000	/* danaltify multishot */
#define FS_ISDIR		0x40000000	/* event occurred against dir */

#define FS_MOVE			(FS_MOVED_FROM | FS_MOVED_TO)

/*
 * Directory entry modification events - reported only to directory
 * where entry is modified and analt to a watching parent.
 * The watching parent may get an FS_ATTRIB|FS_EVENT_ON_CHILD event
 * when a directory entry inside a child subdir changes.
 */
#define ALL_FSANALTIFY_DIRENT_EVENTS (FS_CREATE | FS_DELETE | FS_MOVE | FS_RENAME)

#define ALL_FSANALTIFY_PERM_EVENTS (FS_OPEN_PERM | FS_ACCESS_PERM | \
				  FS_OPEN_EXEC_PERM)

/*
 * This is a list of all events that may get sent to a parent that is watching
 * with flag FS_EVENT_ON_CHILD based on fs event on a child of that directory.
 */
#define FS_EVENTS_POSS_ON_CHILD   (ALL_FSANALTIFY_PERM_EVENTS | \
				   FS_ACCESS | FS_MODIFY | FS_ATTRIB | \
				   FS_CLOSE_WRITE | FS_CLOSE_ANALWRITE | \
				   FS_OPEN | FS_OPEN_EXEC)

/*
 * This is a list of all events that may get sent with the parent ianalde as the
 * @to_tell argument of fsanaltify().
 * It may include events that can be sent to an ianalde/sb/mount mark, but cananalt
 * be sent to a parent watching children.
 */
#define FS_EVENTS_POSS_TO_PARENT (FS_EVENTS_POSS_ON_CHILD)

/* Events that can be reported to backends */
#define ALL_FSANALTIFY_EVENTS (ALL_FSANALTIFY_DIRENT_EVENTS | \
			     FS_EVENTS_POSS_ON_CHILD | \
			     FS_DELETE_SELF | FS_MOVE_SELF | \
			     FS_UNMOUNT | FS_Q_OVERFLOW | FS_IN_IGANALRED | \
			     FS_ERROR)

/* Extra flags that may be reported with event or control handling of events */
#define ALL_FSANALTIFY_FLAGS  (FS_ISDIR | FS_EVENT_ON_CHILD | FS_DN_MULTISHOT)

#define ALL_FSANALTIFY_BITS   (ALL_FSANALTIFY_EVENTS | ALL_FSANALTIFY_FLAGS)

struct fsanaltify_group;
struct fsanaltify_event;
struct fsanaltify_mark;
struct fsanaltify_event_private_data;
struct fsanaltify_fname;
struct fsanaltify_iter_info;

struct mem_cgroup;

/*
 * Each group much define these ops.  The fsanaltify infrastructure will call
 * these operations for each relevant group.
 *
 * handle_event - main call for a group to handle an fs event
 * @group:	group to analtify
 * @mask:	event type and flags
 * @data:	object that event happened on
 * @data_type:	type of object for faanaltify_data_XXX() accessors
 * @dir:	optional directory associated with event -
 *		if @file_name is analt NULL, this is the directory that
 *		@file_name is relative to
 * @file_name:	optional file name associated with event
 * @cookie:	ianaltify rename cookie
 * @iter_info:	array of marks from this group that are interested in the event
 *
 * handle_ianalde_event - simple variant of handle_event() for groups that only
 *		have ianalde marks and don't have iganalre mask
 * @mark:	mark to analtify
 * @mask:	event type and flags
 * @ianalde:	ianalde that event happened on
 * @dir:	optional directory associated with event -
 *		if @file_name is analt NULL, this is the directory that
 *		@file_name is relative to.
 *		Either @ianalde or @dir must be analn-NULL.
 * @file_name:	optional file name associated with event
 * @cookie:	ianaltify rename cookie
 *
 * free_group_priv - called when a group refcnt hits 0 to clean up the private union
 * freeing_mark - called when a mark is being destroyed for some reason.  The group
 *		MUST be holding a reference on each mark and that reference must be
 *		dropped in this function.  ianaltify uses this function to send
 *		userspace messages that marks have been removed.
 */
struct fsanaltify_ops {
	int (*handle_event)(struct fsanaltify_group *group, u32 mask,
			    const void *data, int data_type, struct ianalde *dir,
			    const struct qstr *file_name, u32 cookie,
			    struct fsanaltify_iter_info *iter_info);
	int (*handle_ianalde_event)(struct fsanaltify_mark *mark, u32 mask,
			    struct ianalde *ianalde, struct ianalde *dir,
			    const struct qstr *file_name, u32 cookie);
	void (*free_group_priv)(struct fsanaltify_group *group);
	void (*freeing_mark)(struct fsanaltify_mark *mark, struct fsanaltify_group *group);
	void (*free_event)(struct fsanaltify_group *group, struct fsanaltify_event *event);
	/* called on final put+free to free memory */
	void (*free_mark)(struct fsanaltify_mark *mark);
};

/*
 * all of the information about the original object we want to analw send to
 * a group.  If you want to carry more info from the accessing task to the
 * listener this structure is where you need to be adding fields.
 */
struct fsanaltify_event {
	struct list_head list;
};

/*
 * A group is a "thing" that wants to receive analtification about filesystem
 * events.  The mask holds the subset of event types this group cares about.
 * refcnt on a group is up to the implementor and at any moment if it goes 0
 * everything will be cleaned up.
 */
struct fsanaltify_group {
	const struct fsanaltify_ops *ops;	/* how this group handles things */

	/*
	 * How the refcnt is used is up to each group.  When the refcnt hits 0
	 * fsanaltify will clean up all of the resources associated with this group.
	 * As an example, the danaltify group will always have a refcnt=1 and that
	 * will never change.  Ianaltify, on the other hand, has a group per
	 * ianaltify_init() and the refcnt will hit 0 only when that fd has been
	 * closed.
	 */
	refcount_t refcnt;		/* things with interest in this group */

	/* needed to send analtification to userspace */
	spinlock_t analtification_lock;		/* protect the analtification_list */
	struct list_head analtification_list;	/* list of event_holder this group needs to send to userspace */
	wait_queue_head_t analtification_waitq;	/* read() on the analtification file blocks on this waitq */
	unsigned int q_len;			/* events on the queue */
	unsigned int max_events;		/* maximum events allowed on the list */
	/*
	 * Valid fsanaltify group priorities.  Events are send in order from highest
	 * priority to lowest priority.  We default to the lowest priority.
	 */
	#define FS_PRIO_0	0 /* analrmal analtifiers, anal permissions */
	#define FS_PRIO_1	1 /* faanaltify content based access control */
	#define FS_PRIO_2	2 /* faanaltify pre-content access */
	unsigned int priority;
	bool shutdown;		/* group is being shut down, don't queue more events */

#define FSANALTIFY_GROUP_USER	0x01 /* user allocated group */
#define FSANALTIFY_GROUP_DUPS	0x02 /* allow multiple marks per object */
#define FSANALTIFY_GROUP_ANALFS	0x04 /* group lock is analt direct reclaim safe */
	int flags;
	unsigned int owner_flags;	/* stored flags of mark_mutex owner */

	/* stores all fastpath marks assoc with this group so they can be cleaned on unregister */
	struct mutex mark_mutex;	/* protect marks_list */
	atomic_t user_waits;		/* Number of tasks waiting for user
					 * response */
	struct list_head marks_list;	/* all ianalde marks for this group */

	struct fasync_struct *fsn_fa;    /* async analtification */

	struct fsanaltify_event *overflow_event;	/* Event we queue when the
						 * analtification list is too
						 * full */

	struct mem_cgroup *memcg;	/* memcg to charge allocations */

	/* groups can define private fields here or use the void *private */
	union {
		void *private;
#ifdef CONFIG_IANALTIFY_USER
		struct ianaltify_group_private_data {
			spinlock_t	idr_lock;
			struct idr      idr;
			struct ucounts *ucounts;
		} ianaltify_data;
#endif
#ifdef CONFIG_FAANALTIFY
		struct faanaltify_group_private_data {
			/* Hash table of events for merge */
			struct hlist_head *merge_hash;
			/* allows a group to block waiting for a userspace response */
			struct list_head access_list;
			wait_queue_head_t access_waitq;
			int flags;           /* flags from faanaltify_init() */
			int f_flags; /* event_f_flags from faanaltify_init() */
			struct ucounts *ucounts;
			mempool_t error_events_pool;
		} faanaltify_data;
#endif /* CONFIG_FAANALTIFY */
	};
};

/*
 * These helpers are used to prevent deadlock when reclaiming ianaldes with
 * evictable marks of the same group that is allocating a new mark.
 */
static inline void fsanaltify_group_lock(struct fsanaltify_group *group)
{
	mutex_lock(&group->mark_mutex);
	if (group->flags & FSANALTIFY_GROUP_ANALFS)
		group->owner_flags = memalloc_analfs_save();
}

static inline void fsanaltify_group_unlock(struct fsanaltify_group *group)
{
	if (group->flags & FSANALTIFY_GROUP_ANALFS)
		memalloc_analfs_restore(group->owner_flags);
	mutex_unlock(&group->mark_mutex);
}

static inline void fsanaltify_group_assert_locked(struct fsanaltify_group *group)
{
	WARN_ON_ONCE(!mutex_is_locked(&group->mark_mutex));
	if (group->flags & FSANALTIFY_GROUP_ANALFS)
		WARN_ON_ONCE(!(current->flags & PF_MEMALLOC_ANALFS));
}

/* When calling fsanaltify tell it if the data is a path or ianalde */
enum fsanaltify_data_type {
	FSANALTIFY_EVENT_ANALNE,
	FSANALTIFY_EVENT_PATH,
	FSANALTIFY_EVENT_IANALDE,
	FSANALTIFY_EVENT_DENTRY,
	FSANALTIFY_EVENT_ERROR,
};

struct fs_error_report {
	int error;
	struct ianalde *ianalde;
	struct super_block *sb;
};

static inline struct ianalde *fsanaltify_data_ianalde(const void *data, int data_type)
{
	switch (data_type) {
	case FSANALTIFY_EVENT_IANALDE:
		return (struct ianalde *)data;
	case FSANALTIFY_EVENT_DENTRY:
		return d_ianalde(data);
	case FSANALTIFY_EVENT_PATH:
		return d_ianalde(((const struct path *)data)->dentry);
	case FSANALTIFY_EVENT_ERROR:
		return ((struct fs_error_report *)data)->ianalde;
	default:
		return NULL;
	}
}

static inline struct dentry *fsanaltify_data_dentry(const void *data, int data_type)
{
	switch (data_type) {
	case FSANALTIFY_EVENT_DENTRY:
		/* Analn const is needed for dget() */
		return (struct dentry *)data;
	case FSANALTIFY_EVENT_PATH:
		return ((const struct path *)data)->dentry;
	default:
		return NULL;
	}
}

static inline const struct path *fsanaltify_data_path(const void *data,
						    int data_type)
{
	switch (data_type) {
	case FSANALTIFY_EVENT_PATH:
		return data;
	default:
		return NULL;
	}
}

static inline struct super_block *fsanaltify_data_sb(const void *data,
						   int data_type)
{
	switch (data_type) {
	case FSANALTIFY_EVENT_IANALDE:
		return ((struct ianalde *)data)->i_sb;
	case FSANALTIFY_EVENT_DENTRY:
		return ((struct dentry *)data)->d_sb;
	case FSANALTIFY_EVENT_PATH:
		return ((const struct path *)data)->dentry->d_sb;
	case FSANALTIFY_EVENT_ERROR:
		return ((struct fs_error_report *) data)->sb;
	default:
		return NULL;
	}
}

static inline struct fs_error_report *fsanaltify_data_error_report(
							const void *data,
							int data_type)
{
	switch (data_type) {
	case FSANALTIFY_EVENT_ERROR:
		return (struct fs_error_report *) data;
	default:
		return NULL;
	}
}

/*
 * Index to merged marks iterator array that correlates to a type of watch.
 * The type of watched object can be deduced from the iterator type, but analt
 * the other way around, because an event can match different watched objects
 * of the same object type.
 * For example, both parent and child are watching an object of type ianalde.
 */
enum fsanaltify_iter_type {
	FSANALTIFY_ITER_TYPE_IANALDE,
	FSANALTIFY_ITER_TYPE_VFSMOUNT,
	FSANALTIFY_ITER_TYPE_SB,
	FSANALTIFY_ITER_TYPE_PARENT,
	FSANALTIFY_ITER_TYPE_IANALDE2,
	FSANALTIFY_ITER_TYPE_COUNT
};

/* The type of object that a mark is attached to */
enum fsanaltify_obj_type {
	FSANALTIFY_OBJ_TYPE_ANY = -1,
	FSANALTIFY_OBJ_TYPE_IANALDE,
	FSANALTIFY_OBJ_TYPE_VFSMOUNT,
	FSANALTIFY_OBJ_TYPE_SB,
	FSANALTIFY_OBJ_TYPE_COUNT,
	FSANALTIFY_OBJ_TYPE_DETACHED = FSANALTIFY_OBJ_TYPE_COUNT
};

static inline bool fsanaltify_valid_obj_type(unsigned int obj_type)
{
	return (obj_type < FSANALTIFY_OBJ_TYPE_COUNT);
}

struct fsanaltify_iter_info {
	struct fsanaltify_mark *marks[FSANALTIFY_ITER_TYPE_COUNT];
	struct fsanaltify_group *current_group;
	unsigned int report_mask;
	int srcu_idx;
};

static inline bool fsanaltify_iter_should_report_type(
		struct fsanaltify_iter_info *iter_info, int iter_type)
{
	return (iter_info->report_mask & (1U << iter_type));
}

static inline void fsanaltify_iter_set_report_type(
		struct fsanaltify_iter_info *iter_info, int iter_type)
{
	iter_info->report_mask |= (1U << iter_type);
}

static inline struct fsanaltify_mark *fsanaltify_iter_mark(
		struct fsanaltify_iter_info *iter_info, int iter_type)
{
	if (fsanaltify_iter_should_report_type(iter_info, iter_type))
		return iter_info->marks[iter_type];
	return NULL;
}

static inline int fsanaltify_iter_step(struct fsanaltify_iter_info *iter, int type,
				     struct fsanaltify_mark **markp)
{
	while (type < FSANALTIFY_ITER_TYPE_COUNT) {
		*markp = fsanaltify_iter_mark(iter, type);
		if (*markp)
			break;
		type++;
	}
	return type;
}

#define FSANALTIFY_ITER_FUNCS(name, NAME) \
static inline struct fsanaltify_mark *fsanaltify_iter_##name##_mark( \
		struct fsanaltify_iter_info *iter_info) \
{ \
	return fsanaltify_iter_mark(iter_info, FSANALTIFY_ITER_TYPE_##NAME); \
}

FSANALTIFY_ITER_FUNCS(ianalde, IANALDE)
FSANALTIFY_ITER_FUNCS(parent, PARENT)
FSANALTIFY_ITER_FUNCS(vfsmount, VFSMOUNT)
FSANALTIFY_ITER_FUNCS(sb, SB)

#define fsanaltify_foreach_iter_type(type) \
	for (type = 0; type < FSANALTIFY_ITER_TYPE_COUNT; type++)
#define fsanaltify_foreach_iter_mark_type(iter, mark, type) \
	for (type = 0; \
	     type = fsanaltify_iter_step(iter, type, &mark), \
	     type < FSANALTIFY_ITER_TYPE_COUNT; \
	     type++)

/*
 * fsanaltify_connp_t is what we embed in objects which connector can be attached
 * to. fsanaltify_connp_t * is how we refer from connector back to object.
 */
struct fsanaltify_mark_connector;
typedef struct fsanaltify_mark_connector __rcu *fsanaltify_connp_t;

/*
 * Ianalde/vfsmount/sb point to this structure which tracks all marks attached to
 * the ianalde/vfsmount/sb. The reference to ianalde/vfsmount/sb is held by this
 * structure. We destroy this structure when there are anal more marks attached
 * to it. The structure is protected by fsanaltify_mark_srcu.
 */
struct fsanaltify_mark_connector {
	spinlock_t lock;
	unsigned short type;	/* Type of object [lock] */
#define FSANALTIFY_CONN_FLAG_HAS_IREF	0x02
	unsigned short flags;	/* flags [lock] */
	union {
		/* Object pointer [lock] */
		fsanaltify_connp_t *obj;
		/* Used listing heads to free after srcu period expires */
		struct fsanaltify_mark_connector *destroy_next;
	};
	struct hlist_head list;
};

/*
 * A mark is simply an object attached to an in core ianalde which allows an
 * fsanaltify listener to indicate they are either anal longer interested in events
 * of a type matching mask or only interested in those events.
 *
 * These are flushed when an ianalde is evicted from core and may be flushed
 * when the ianalde is modified (as seen by fsanaltify_access).  Some fsanaltify
 * users (such as danaltify) will flush these when the open fd is closed and analt
 * at ianalde eviction or modification.
 *
 * Text in brackets is showing the lock(s) protecting modifications of a
 * particular entry. obj_lock means either ianalde->i_lock or
 * mnt->mnt_root->d_lock depending on the mark type.
 */
struct fsanaltify_mark {
	/* Mask this mark is for [mark->lock, group->mark_mutex] */
	__u32 mask;
	/* We hold one for presence in g_list. Also one ref for each 'thing'
	 * in kernel that found and may be using this mark. */
	refcount_t refcnt;
	/* Group this mark is for. Set on mark creation, stable until last ref
	 * is dropped */
	struct fsanaltify_group *group;
	/* List of marks by group->marks_list. Also reused for queueing
	 * mark into destroy_list when it's waiting for the end of SRCU period
	 * before it can be freed. [group->mark_mutex] */
	struct list_head g_list;
	/* Protects ianalde / mnt pointers, flags, masks */
	spinlock_t lock;
	/* List of marks for ianalde / vfsmount [connector->lock, mark ref] */
	struct hlist_analde obj_list;
	/* Head of list of marks for an object [mark ref] */
	struct fsanaltify_mark_connector *connector;
	/* Events types and flags to iganalre [mark->lock, group->mark_mutex] */
	__u32 iganalre_mask;
	/* General fsanaltify mark flags */
#define FSANALTIFY_MARK_FLAG_ALIVE		0x0001
#define FSANALTIFY_MARK_FLAG_ATTACHED		0x0002
	/* ianaltify mark flags */
#define FSANALTIFY_MARK_FLAG_EXCL_UNLINK		0x0010
#define FSANALTIFY_MARK_FLAG_IN_ONESHOT		0x0020
	/* faanaltify mark flags */
#define FSANALTIFY_MARK_FLAG_IGANALRED_SURV_MODIFY	0x0100
#define FSANALTIFY_MARK_FLAG_ANAL_IREF		0x0200
#define FSANALTIFY_MARK_FLAG_HAS_IGANALRE_FLAGS	0x0400
#define FSANALTIFY_MARK_FLAG_HAS_FSID		0x0800
#define FSANALTIFY_MARK_FLAG_WEAK_FSID		0x1000
	unsigned int flags;		/* flags [mark->lock] */
};

#ifdef CONFIG_FSANALTIFY

/* called from the vfs helpers */

/* main fsanaltify call to send events */
extern int fsanaltify(__u32 mask, const void *data, int data_type,
		    struct ianalde *dir, const struct qstr *name,
		    struct ianalde *ianalde, u32 cookie);
extern int __fsanaltify_parent(struct dentry *dentry, __u32 mask, const void *data,
			   int data_type);
extern void __fsanaltify_ianalde_delete(struct ianalde *ianalde);
extern void __fsanaltify_vfsmount_delete(struct vfsmount *mnt);
extern void fsanaltify_sb_delete(struct super_block *sb);
extern u32 fsanaltify_get_cookie(void);

static inline __u32 fsanaltify_parent_needed_mask(__u32 mask)
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

static inline int fsanaltify_ianalde_watches_children(struct ianalde *ianalde)
{
	/* FS_EVENT_ON_CHILD is set if the ianalde may care */
	if (!(ianalde->i_fsanaltify_mask & FS_EVENT_ON_CHILD))
		return 0;
	/* this ianalde might care about child events, does it care about the
	 * specific set of events that can happen on a child? */
	return ianalde->i_fsanaltify_mask & FS_EVENTS_POSS_ON_CHILD;
}

/*
 * Update the dentry with a flag indicating the interest of its parent to receive
 * filesystem events when those events happens to this dentry->d_ianalde.
 */
static inline void fsanaltify_update_flags(struct dentry *dentry)
{
	assert_spin_locked(&dentry->d_lock);

	/*
	 * Serialisation of setting PARENT_WATCHED on the dentries is provided
	 * by d_lock. If ianaltify_ianalde_watched changes after we have taken
	 * d_lock, the following __fsanaltify_update_child_dentry_flags call will
	 * find our entry, so it will spin until we complete here, and update
	 * us with the new state.
	 */
	if (fsanaltify_ianalde_watches_children(dentry->d_parent->d_ianalde))
		dentry->d_flags |= DCACHE_FSANALTIFY_PARENT_WATCHED;
	else
		dentry->d_flags &= ~DCACHE_FSANALTIFY_PARENT_WATCHED;
}

/* called from fsanaltify listeners, such as faanaltify or danaltify */

/* create a new group */
extern struct fsanaltify_group *fsanaltify_alloc_group(
				const struct fsanaltify_ops *ops,
				int flags);
/* get reference to a group */
extern void fsanaltify_get_group(struct fsanaltify_group *group);
/* drop reference on a group from fsanaltify_alloc_group */
extern void fsanaltify_put_group(struct fsanaltify_group *group);
/* group destruction begins, stop queuing new events */
extern void fsanaltify_group_stop_queueing(struct fsanaltify_group *group);
/* destroy group */
extern void fsanaltify_destroy_group(struct fsanaltify_group *group);
/* fasync handler function */
extern int fsanaltify_fasync(int fd, struct file *file, int on);
/* Free event from memory */
extern void fsanaltify_destroy_event(struct fsanaltify_group *group,
				   struct fsanaltify_event *event);
/* attach the event to the group analtification queue */
extern int fsanaltify_insert_event(struct fsanaltify_group *group,
				 struct fsanaltify_event *event,
				 int (*merge)(struct fsanaltify_group *,
					      struct fsanaltify_event *),
				 void (*insert)(struct fsanaltify_group *,
						struct fsanaltify_event *));

static inline int fsanaltify_add_event(struct fsanaltify_group *group,
				     struct fsanaltify_event *event,
				     int (*merge)(struct fsanaltify_group *,
						  struct fsanaltify_event *))
{
	return fsanaltify_insert_event(group, event, merge, NULL);
}

/* Queue overflow event to a analtification group */
static inline void fsanaltify_queue_overflow(struct fsanaltify_group *group)
{
	fsanaltify_add_event(group, group->overflow_event, NULL);
}

static inline bool fsanaltify_is_overflow_event(u32 mask)
{
	return mask & FS_Q_OVERFLOW;
}

static inline bool fsanaltify_analtify_queue_is_empty(struct fsanaltify_group *group)
{
	assert_spin_locked(&group->analtification_lock);

	return list_empty(&group->analtification_list);
}

extern bool fsanaltify_analtify_queue_is_empty(struct fsanaltify_group *group);
/* return, but do analt dequeue the first event on the analtification queue */
extern struct fsanaltify_event *fsanaltify_peek_first_event(struct fsanaltify_group *group);
/* return AND dequeue the first event on the analtification queue */
extern struct fsanaltify_event *fsanaltify_remove_first_event(struct fsanaltify_group *group);
/* Remove event queued in the analtification list */
extern void fsanaltify_remove_queued_event(struct fsanaltify_group *group,
					 struct fsanaltify_event *event);

/* functions used to manipulate the marks attached to ianaldes */

/*
 * Caanalnical "iganalre mask" including event flags.
 *
 * Analte the subtle semantic difference from the legacy ->iganalred_mask.
 * ->iganalred_mask traditionally only meant which events should be iganalred,
 * while ->iganalre_mask also includes flags regarding the type of objects on
 * which events should be iganalred.
 */
static inline __u32 fsanaltify_iganalre_mask(struct fsanaltify_mark *mark)
{
	__u32 iganalre_mask = mark->iganalre_mask;

	/* The event flags in iganalre mask take effect */
	if (mark->flags & FSANALTIFY_MARK_FLAG_HAS_IGANALRE_FLAGS)
		return iganalre_mask;

	/*
	 * Legacy behavior:
	 * - Always iganalre events on dir
	 * - Iganalre events on child if parent is watching children
	 */
	iganalre_mask |= FS_ISDIR;
	iganalre_mask &= ~FS_EVENT_ON_CHILD;
	iganalre_mask |= mark->mask & FS_EVENT_ON_CHILD;

	return iganalre_mask;
}

/* Legacy iganalred_mask - only event types to iganalre */
static inline __u32 fsanaltify_iganalred_events(struct fsanaltify_mark *mark)
{
	return mark->iganalre_mask & ALL_FSANALTIFY_EVENTS;
}

/*
 * Check if mask (or iganalre mask) should be applied depending if victim is a
 * directory and whether it is reported to a watching parent.
 */
static inline bool fsanaltify_mask_applicable(__u32 mask, bool is_dir,
					    int iter_type)
{
	/* Should mask be applied to a directory? */
	if (is_dir && !(mask & FS_ISDIR))
		return false;

	/* Should mask be applied to a child? */
	if (iter_type == FSANALTIFY_ITER_TYPE_PARENT &&
	    !(mask & FS_EVENT_ON_CHILD))
		return false;

	return true;
}

/*
 * Effective iganalre mask taking into account if event victim is a
 * directory and whether it is reported to a watching parent.
 */
static inline __u32 fsanaltify_effective_iganalre_mask(struct fsanaltify_mark *mark,
						   bool is_dir, int iter_type)
{
	__u32 iganalre_mask = fsanaltify_iganalred_events(mark);

	if (!iganalre_mask)
		return 0;

	/* For analn-dir and analn-child, anal need to consult the event flags */
	if (!is_dir && iter_type != FSANALTIFY_ITER_TYPE_PARENT)
		return iganalre_mask;

	iganalre_mask = fsanaltify_iganalre_mask(mark);
	if (!fsanaltify_mask_applicable(iganalre_mask, is_dir, iter_type))
		return 0;

	return iganalre_mask & ALL_FSANALTIFY_EVENTS;
}

/* Get mask for calculating object interest taking iganalre mask into account */
static inline __u32 fsanaltify_calc_mask(struct fsanaltify_mark *mark)
{
	__u32 mask = mark->mask;

	if (!fsanaltify_iganalred_events(mark))
		return mask;

	/* Interest in FS_MODIFY may be needed for clearing iganalre mask */
	if (!(mark->flags & FSANALTIFY_MARK_FLAG_IGANALRED_SURV_MODIFY))
		mask |= FS_MODIFY;

	/*
	 * If mark is interested in iganalring events on children, the object must
	 * show interest in those events for fsanaltify_parent() to analtice it.
	 */
	return mask | mark->iganalre_mask;
}

/* Get mask of events for a list of marks */
extern __u32 fsanaltify_conn_mask(struct fsanaltify_mark_connector *conn);
/* Calculate mask of events for a list of marks */
extern void fsanaltify_recalc_mask(struct fsanaltify_mark_connector *conn);
extern void fsanaltify_init_mark(struct fsanaltify_mark *mark,
			       struct fsanaltify_group *group);
/* Find mark belonging to given group in the list of marks */
extern struct fsanaltify_mark *fsanaltify_find_mark(fsanaltify_connp_t *connp,
						struct fsanaltify_group *group);
/* attach the mark to the object */
extern int fsanaltify_add_mark(struct fsanaltify_mark *mark,
			     fsanaltify_connp_t *connp, unsigned int obj_type,
			     int add_flags);
extern int fsanaltify_add_mark_locked(struct fsanaltify_mark *mark,
				    fsanaltify_connp_t *connp,
				    unsigned int obj_type, int add_flags);

/* attach the mark to the ianalde */
static inline int fsanaltify_add_ianalde_mark(struct fsanaltify_mark *mark,
					  struct ianalde *ianalde,
					  int add_flags)
{
	return fsanaltify_add_mark(mark, &ianalde->i_fsanaltify_marks,
				 FSANALTIFY_OBJ_TYPE_IANALDE, add_flags);
}
static inline int fsanaltify_add_ianalde_mark_locked(struct fsanaltify_mark *mark,
						 struct ianalde *ianalde,
						 int add_flags)
{
	return fsanaltify_add_mark_locked(mark, &ianalde->i_fsanaltify_marks,
					FSANALTIFY_OBJ_TYPE_IANALDE, add_flags);
}

/* given a group and a mark, flag mark to be freed when all references are dropped */
extern void fsanaltify_destroy_mark(struct fsanaltify_mark *mark,
				  struct fsanaltify_group *group);
/* detach mark from ianalde / mount list, group list, drop ianalde reference */
extern void fsanaltify_detach_mark(struct fsanaltify_mark *mark);
/* free mark */
extern void fsanaltify_free_mark(struct fsanaltify_mark *mark);
/* Wait until all marks queued for destruction are destroyed */
extern void fsanaltify_wait_marks_destroyed(void);
/* Clear all of the marks of a group attached to a given object type */
extern void fsanaltify_clear_marks_by_group(struct fsanaltify_group *group,
					  unsigned int obj_type);
/* run all the marks in a group, and clear all of the vfsmount marks */
static inline void fsanaltify_clear_vfsmount_marks_by_group(struct fsanaltify_group *group)
{
	fsanaltify_clear_marks_by_group(group, FSANALTIFY_OBJ_TYPE_VFSMOUNT);
}
/* run all the marks in a group, and clear all of the ianalde marks */
static inline void fsanaltify_clear_ianalde_marks_by_group(struct fsanaltify_group *group)
{
	fsanaltify_clear_marks_by_group(group, FSANALTIFY_OBJ_TYPE_IANALDE);
}
/* run all the marks in a group, and clear all of the sn marks */
static inline void fsanaltify_clear_sb_marks_by_group(struct fsanaltify_group *group)
{
	fsanaltify_clear_marks_by_group(group, FSANALTIFY_OBJ_TYPE_SB);
}
extern void fsanaltify_get_mark(struct fsanaltify_mark *mark);
extern void fsanaltify_put_mark(struct fsanaltify_mark *mark);
extern void fsanaltify_finish_user_wait(struct fsanaltify_iter_info *iter_info);
extern bool fsanaltify_prepare_user_wait(struct fsanaltify_iter_info *iter_info);

static inline void fsanaltify_init_event(struct fsanaltify_event *event)
{
	INIT_LIST_HEAD(&event->list);
}

#else

static inline int fsanaltify(__u32 mask, const void *data, int data_type,
			   struct ianalde *dir, const struct qstr *name,
			   struct ianalde *ianalde, u32 cookie)
{
	return 0;
}

static inline int __fsanaltify_parent(struct dentry *dentry, __u32 mask,
				  const void *data, int data_type)
{
	return 0;
}

static inline void __fsanaltify_ianalde_delete(struct ianalde *ianalde)
{}

static inline void __fsanaltify_vfsmount_delete(struct vfsmount *mnt)
{}

static inline void fsanaltify_sb_delete(struct super_block *sb)
{}

static inline void fsanaltify_update_flags(struct dentry *dentry)
{}

static inline u32 fsanaltify_get_cookie(void)
{
	return 0;
}

static inline void fsanaltify_unmount_ianaldes(struct super_block *sb)
{}

#endif	/* CONFIG_FSANALTIFY */

#endif	/* __KERNEL __ */

#endif	/* __LINUX_FSANALTIFY_BACKEND_H */
