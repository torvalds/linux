/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Filesystem access yestification for Linux
 *
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

#ifndef __LINUX_FSNOTIFY_BACKEND_H
#define __LINUX_FSNOTIFY_BACKEND_H

#ifdef __KERNEL__

#include <linux/idr.h> /* iyestify uses this */
#include <linux/fs.h> /* struct iyesde */
#include <linux/list.h>
#include <linux/path.h> /* struct path */
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/user_namespace.h>
#include <linux/refcount.h>

/*
 * IN_* from iyestfy.h lines up EXACTLY with FS_*, this is so we can easily
 * convert between them.  dyestify only needs conversion at watch creation
 * so yes perf loss there.  fayestify isn't defined yet, so it can use the
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

#define FS_UNMOUNT		0x00002000	/* iyesde on umount fs */
#define FS_Q_OVERFLOW		0x00004000	/* Event queued overflowed */
#define FS_IN_IGNORED		0x00008000	/* last iyestify event here */

#define FS_OPEN_PERM		0x00010000	/* open event in an permission hook */
#define FS_ACCESS_PERM		0x00020000	/* access event in a permissions hook */
#define FS_OPEN_EXEC_PERM	0x00040000	/* open/exec event in a permission hook */

#define FS_EXCL_UNLINK		0x04000000	/* do yest send events if object is unlinked */
#define FS_ISDIR		0x40000000	/* event occurred against dir */
#define FS_IN_ONESHOT		0x80000000	/* only send event once */

#define FS_DN_RENAME		0x10000000	/* file renamed */
#define FS_DN_MULTISHOT		0x20000000	/* dyestify multishot */

/* This iyesde cares about things that happen to its children.  Always set for
 * dyestify and iyestify. */
#define FS_EVENT_ON_CHILD	0x08000000

#define FS_MOVE			(FS_MOVED_FROM | FS_MOVED_TO)

/*
 * Directory entry modification events - reported only to directory
 * where entry is modified and yest to a watching parent.
 * The watching parent may get an FS_ATTRIB|FS_EVENT_ON_CHILD event
 * when a directory entry inside a child subdir changes.
 */
#define ALL_FSNOTIFY_DIRENT_EVENTS	(FS_CREATE | FS_DELETE | FS_MOVE)

#define ALL_FSNOTIFY_PERM_EVENTS (FS_OPEN_PERM | FS_ACCESS_PERM | \
				  FS_OPEN_EXEC_PERM)

/*
 * This is a list of all events that may get sent to a parent based on fs event
 * happening to iyesdes inside that directory.
 */
#define FS_EVENTS_POSS_ON_CHILD   (ALL_FSNOTIFY_PERM_EVENTS | \
				   FS_ACCESS | FS_MODIFY | FS_ATTRIB | \
				   FS_CLOSE_WRITE | FS_CLOSE_NOWRITE | \
				   FS_OPEN | FS_OPEN_EXEC)

/* Events that can be reported to backends */
#define ALL_FSNOTIFY_EVENTS (ALL_FSNOTIFY_DIRENT_EVENTS | \
			     FS_EVENTS_POSS_ON_CHILD | \
			     FS_DELETE_SELF | FS_MOVE_SELF | FS_DN_RENAME | \
			     FS_UNMOUNT | FS_Q_OVERFLOW | FS_IN_IGNORED)

/* Extra flags that may be reported with event or control handling of events */
#define ALL_FSNOTIFY_FLAGS  (FS_EXCL_UNLINK | FS_ISDIR | FS_IN_ONESHOT | \
			     FS_DN_MULTISHOT | FS_EVENT_ON_CHILD)

#define ALL_FSNOTIFY_BITS   (ALL_FSNOTIFY_EVENTS | ALL_FSNOTIFY_FLAGS)

struct fsyestify_group;
struct fsyestify_event;
struct fsyestify_mark;
struct fsyestify_event_private_data;
struct fsyestify_fname;
struct fsyestify_iter_info;

struct mem_cgroup;

/*
 * Each group much define these ops.  The fsyestify infrastructure will call
 * these operations for each relevant group.
 *
 * handle_event - main call for a group to handle an fs event
 * free_group_priv - called when a group refcnt hits 0 to clean up the private union
 * freeing_mark - called when a mark is being destroyed for some reason.  The group
 * 		MUST be holding a reference on each mark and that reference must be
 * 		dropped in this function.  iyestify uses this function to send
 * 		userspace messages that marks have been removed.
 */
struct fsyestify_ops {
	int (*handle_event)(struct fsyestify_group *group,
			    struct iyesde *iyesde,
			    u32 mask, const void *data, int data_type,
			    const struct qstr *file_name, u32 cookie,
			    struct fsyestify_iter_info *iter_info);
	void (*free_group_priv)(struct fsyestify_group *group);
	void (*freeing_mark)(struct fsyestify_mark *mark, struct fsyestify_group *group);
	void (*free_event)(struct fsyestify_event *event);
	/* called on final put+free to free memory */
	void (*free_mark)(struct fsyestify_mark *mark);
};

/*
 * all of the information about the original object we want to yesw send to
 * a group.  If you want to carry more info from the accessing task to the
 * listener this structure is where you need to be adding fields.
 */
struct fsyestify_event {
	struct list_head list;
	/* iyesde may ONLY be dereferenced during handle_event(). */
	struct iyesde *iyesde;	/* either the iyesde the event happened to or its parent */
};

/*
 * A group is a "thing" that wants to receive yestification about filesystem
 * events.  The mask holds the subset of event types this group cares about.
 * refcnt on a group is up to the implementor and at any moment if it goes 0
 * everything will be cleaned up.
 */
struct fsyestify_group {
	const struct fsyestify_ops *ops;	/* how this group handles things */

	/*
	 * How the refcnt is used is up to each group.  When the refcnt hits 0
	 * fsyestify will clean up all of the resources associated with this group.
	 * As an example, the dyestify group will always have a refcnt=1 and that
	 * will never change.  Iyestify, on the other hand, has a group per
	 * iyestify_init() and the refcnt will hit 0 only when that fd has been
	 * closed.
	 */
	refcount_t refcnt;		/* things with interest in this group */

	/* needed to send yestification to userspace */
	spinlock_t yestification_lock;		/* protect the yestification_list */
	struct list_head yestification_list;	/* list of event_holder this group needs to send to userspace */
	wait_queue_head_t yestification_waitq;	/* read() on the yestification file blocks on this waitq */
	unsigned int q_len;			/* events on the queue */
	unsigned int max_events;		/* maximum events allowed on the list */
	/*
	 * Valid fsyestify group priorities.  Events are send in order from highest
	 * priority to lowest priority.  We default to the lowest priority.
	 */
	#define FS_PRIO_0	0 /* yesrmal yestifiers, yes permissions */
	#define FS_PRIO_1	1 /* fayestify content based access control */
	#define FS_PRIO_2	2 /* fayestify pre-content access */
	unsigned int priority;
	bool shutdown;		/* group is being shut down, don't queue more events */

	/* stores all fastpath marks assoc with this group so they can be cleaned on unregister */
	struct mutex mark_mutex;	/* protect marks_list */
	atomic_t num_marks;		/* 1 for each mark and 1 for yest being
					 * past the point of yes return when freeing
					 * a group */
	atomic_t user_waits;		/* Number of tasks waiting for user
					 * response */
	struct list_head marks_list;	/* all iyesde marks for this group */

	struct fasync_struct *fsn_fa;    /* async yestification */

	struct fsyestify_event *overflow_event;	/* Event we queue when the
						 * yestification list is too
						 * full */

	struct mem_cgroup *memcg;	/* memcg to charge allocations */

	/* groups can define private fields here or use the void *private */
	union {
		void *private;
#ifdef CONFIG_INOTIFY_USER
		struct iyestify_group_private_data {
			spinlock_t	idr_lock;
			struct idr      idr;
			struct ucounts *ucounts;
		} iyestify_data;
#endif
#ifdef CONFIG_FANOTIFY
		struct fayestify_group_private_data {
			/* allows a group to block waiting for a userspace response */
			struct list_head access_list;
			wait_queue_head_t access_waitq;
			int flags;           /* flags from fayestify_init() */
			int f_flags; /* event_f_flags from fayestify_init() */
			unsigned int max_marks;
			struct user_struct *user;
		} fayestify_data;
#endif /* CONFIG_FANOTIFY */
	};
};

/* when calling fsyestify tell it if the data is a path or iyesde */
#define FSNOTIFY_EVENT_NONE	0
#define FSNOTIFY_EVENT_PATH	1
#define FSNOTIFY_EVENT_INODE	2

enum fsyestify_obj_type {
	FSNOTIFY_OBJ_TYPE_INODE,
	FSNOTIFY_OBJ_TYPE_VFSMOUNT,
	FSNOTIFY_OBJ_TYPE_SB,
	FSNOTIFY_OBJ_TYPE_COUNT,
	FSNOTIFY_OBJ_TYPE_DETACHED = FSNOTIFY_OBJ_TYPE_COUNT
};

#define FSNOTIFY_OBJ_TYPE_INODE_FL	(1U << FSNOTIFY_OBJ_TYPE_INODE)
#define FSNOTIFY_OBJ_TYPE_VFSMOUNT_FL	(1U << FSNOTIFY_OBJ_TYPE_VFSMOUNT)
#define FSNOTIFY_OBJ_TYPE_SB_FL		(1U << FSNOTIFY_OBJ_TYPE_SB)
#define FSNOTIFY_OBJ_ALL_TYPES_MASK	((1U << FSNOTIFY_OBJ_TYPE_COUNT) - 1)

static inline bool fsyestify_valid_obj_type(unsigned int type)
{
	return (type < FSNOTIFY_OBJ_TYPE_COUNT);
}

struct fsyestify_iter_info {
	struct fsyestify_mark *marks[FSNOTIFY_OBJ_TYPE_COUNT];
	unsigned int report_mask;
	int srcu_idx;
};

static inline bool fsyestify_iter_should_report_type(
		struct fsyestify_iter_info *iter_info, int type)
{
	return (iter_info->report_mask & (1U << type));
}

static inline void fsyestify_iter_set_report_type(
		struct fsyestify_iter_info *iter_info, int type)
{
	iter_info->report_mask |= (1U << type);
}

static inline void fsyestify_iter_set_report_type_mark(
		struct fsyestify_iter_info *iter_info, int type,
		struct fsyestify_mark *mark)
{
	iter_info->marks[type] = mark;
	iter_info->report_mask |= (1U << type);
}

#define FSNOTIFY_ITER_FUNCS(name, NAME) \
static inline struct fsyestify_mark *fsyestify_iter_##name##_mark( \
		struct fsyestify_iter_info *iter_info) \
{ \
	return (iter_info->report_mask & FSNOTIFY_OBJ_TYPE_##NAME##_FL) ? \
		iter_info->marks[FSNOTIFY_OBJ_TYPE_##NAME] : NULL; \
}

FSNOTIFY_ITER_FUNCS(iyesde, INODE)
FSNOTIFY_ITER_FUNCS(vfsmount, VFSMOUNT)
FSNOTIFY_ITER_FUNCS(sb, SB)

#define fsyestify_foreach_obj_type(type) \
	for (type = 0; type < FSNOTIFY_OBJ_TYPE_COUNT; type++)

/*
 * fsyestify_connp_t is what we embed in objects which connector can be attached
 * to. fsyestify_connp_t * is how we refer from connector back to object.
 */
struct fsyestify_mark_connector;
typedef struct fsyestify_mark_connector __rcu *fsyestify_connp_t;

/*
 * Iyesde/vfsmount/sb point to this structure which tracks all marks attached to
 * the iyesde/vfsmount/sb. The reference to iyesde/vfsmount/sb is held by this
 * structure. We destroy this structure when there are yes more marks attached
 * to it. The structure is protected by fsyestify_mark_srcu.
 */
struct fsyestify_mark_connector {
	spinlock_t lock;
	unsigned short type;	/* Type of object [lock] */
#define FSNOTIFY_CONN_FLAG_HAS_FSID	0x01
	unsigned short flags;	/* flags [lock] */
	__kernel_fsid_t fsid;	/* fsid of filesystem containing object */
	union {
		/* Object pointer [lock] */
		fsyestify_connp_t *obj;
		/* Used listing heads to free after srcu period expires */
		struct fsyestify_mark_connector *destroy_next;
	};
	struct hlist_head list;
};

/*
 * A mark is simply an object attached to an in core iyesde which allows an
 * fsyestify listener to indicate they are either yes longer interested in events
 * of a type matching mask or only interested in those events.
 *
 * These are flushed when an iyesde is evicted from core and may be flushed
 * when the iyesde is modified (as seen by fsyestify_access).  Some fsyestify
 * users (such as dyestify) will flush these when the open fd is closed and yest
 * at iyesde eviction or modification.
 *
 * Text in brackets is showing the lock(s) protecting modifications of a
 * particular entry. obj_lock means either iyesde->i_lock or
 * mnt->mnt_root->d_lock depending on the mark type.
 */
struct fsyestify_mark {
	/* Mask this mark is for [mark->lock, group->mark_mutex] */
	__u32 mask;
	/* We hold one for presence in g_list. Also one ref for each 'thing'
	 * in kernel that found and may be using this mark. */
	refcount_t refcnt;
	/* Group this mark is for. Set on mark creation, stable until last ref
	 * is dropped */
	struct fsyestify_group *group;
	/* List of marks by group->marks_list. Also reused for queueing
	 * mark into destroy_list when it's waiting for the end of SRCU period
	 * before it can be freed. [group->mark_mutex] */
	struct list_head g_list;
	/* Protects iyesde / mnt pointers, flags, masks */
	spinlock_t lock;
	/* List of marks for iyesde / vfsmount [connector->lock, mark ref] */
	struct hlist_yesde obj_list;
	/* Head of list of marks for an object [mark ref] */
	struct fsyestify_mark_connector *connector;
	/* Events types to igyesre [mark->lock, group->mark_mutex] */
	__u32 igyesred_mask;
#define FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY	0x01
#define FSNOTIFY_MARK_FLAG_ALIVE		0x02
#define FSNOTIFY_MARK_FLAG_ATTACHED		0x04
	unsigned int flags;		/* flags [mark->lock] */
};

#ifdef CONFIG_FSNOTIFY

/* called from the vfs helpers */

/* main fsyestify call to send events */
extern int fsyestify(struct iyesde *to_tell, __u32 mask, const void *data, int data_is,
		    const struct qstr *name, u32 cookie);
extern int __fsyestify_parent(const struct path *path, struct dentry *dentry, __u32 mask);
extern void __fsyestify_iyesde_delete(struct iyesde *iyesde);
extern void __fsyestify_vfsmount_delete(struct vfsmount *mnt);
extern void fsyestify_sb_delete(struct super_block *sb);
extern u32 fsyestify_get_cookie(void);

static inline int fsyestify_iyesde_watches_children(struct iyesde *iyesde)
{
	/* FS_EVENT_ON_CHILD is set if the iyesde may care */
	if (!(iyesde->i_fsyestify_mask & FS_EVENT_ON_CHILD))
		return 0;
	/* this iyesde might care about child events, does it care about the
	 * specific set of events that can happen on a child? */
	return iyesde->i_fsyestify_mask & FS_EVENTS_POSS_ON_CHILD;
}

/*
 * Update the dentry with a flag indicating the interest of its parent to receive
 * filesystem events when those events happens to this dentry->d_iyesde.
 */
static inline void fsyestify_update_flags(struct dentry *dentry)
{
	assert_spin_locked(&dentry->d_lock);

	/*
	 * Serialisation of setting PARENT_WATCHED on the dentries is provided
	 * by d_lock. If iyestify_iyesde_watched changes after we have taken
	 * d_lock, the following __fsyestify_update_child_dentry_flags call will
	 * find our entry, so it will spin until we complete here, and update
	 * us with the new state.
	 */
	if (fsyestify_iyesde_watches_children(dentry->d_parent->d_iyesde))
		dentry->d_flags |= DCACHE_FSNOTIFY_PARENT_WATCHED;
	else
		dentry->d_flags &= ~DCACHE_FSNOTIFY_PARENT_WATCHED;
}

/* called from fsyestify listeners, such as fayestify or dyestify */

/* create a new group */
extern struct fsyestify_group *fsyestify_alloc_group(const struct fsyestify_ops *ops);
/* get reference to a group */
extern void fsyestify_get_group(struct fsyestify_group *group);
/* drop reference on a group from fsyestify_alloc_group */
extern void fsyestify_put_group(struct fsyestify_group *group);
/* group destruction begins, stop queuing new events */
extern void fsyestify_group_stop_queueing(struct fsyestify_group *group);
/* destroy group */
extern void fsyestify_destroy_group(struct fsyestify_group *group);
/* fasync handler function */
extern int fsyestify_fasync(int fd, struct file *file, int on);
/* Free event from memory */
extern void fsyestify_destroy_event(struct fsyestify_group *group,
				   struct fsyestify_event *event);
/* attach the event to the group yestification queue */
extern int fsyestify_add_event(struct fsyestify_group *group,
			      struct fsyestify_event *event,
			      int (*merge)(struct list_head *,
					   struct fsyestify_event *));
/* Queue overflow event to a yestification group */
static inline void fsyestify_queue_overflow(struct fsyestify_group *group)
{
	fsyestify_add_event(group, group->overflow_event, NULL);
}

/* true if the group yestification queue is empty */
extern bool fsyestify_yestify_queue_is_empty(struct fsyestify_group *group);
/* return, but do yest dequeue the first event on the yestification queue */
extern struct fsyestify_event *fsyestify_peek_first_event(struct fsyestify_group *group);
/* return AND dequeue the first event on the yestification queue */
extern struct fsyestify_event *fsyestify_remove_first_event(struct fsyestify_group *group);
/* Remove event queued in the yestification list */
extern void fsyestify_remove_queued_event(struct fsyestify_group *group,
					 struct fsyestify_event *event);

/* functions used to manipulate the marks attached to iyesdes */

/* Get mask of events for a list of marks */
extern __u32 fsyestify_conn_mask(struct fsyestify_mark_connector *conn);
/* Calculate mask of events for a list of marks */
extern void fsyestify_recalc_mask(struct fsyestify_mark_connector *conn);
extern void fsyestify_init_mark(struct fsyestify_mark *mark,
			       struct fsyestify_group *group);
/* Find mark belonging to given group in the list of marks */
extern struct fsyestify_mark *fsyestify_find_mark(fsyestify_connp_t *connp,
						struct fsyestify_group *group);
/* Get cached fsid of filesystem containing object */
extern int fsyestify_get_conn_fsid(const struct fsyestify_mark_connector *conn,
				  __kernel_fsid_t *fsid);
/* attach the mark to the object */
extern int fsyestify_add_mark(struct fsyestify_mark *mark,
			     fsyestify_connp_t *connp, unsigned int type,
			     int allow_dups, __kernel_fsid_t *fsid);
extern int fsyestify_add_mark_locked(struct fsyestify_mark *mark,
				    fsyestify_connp_t *connp,
				    unsigned int type, int allow_dups,
				    __kernel_fsid_t *fsid);

/* attach the mark to the iyesde */
static inline int fsyestify_add_iyesde_mark(struct fsyestify_mark *mark,
					  struct iyesde *iyesde,
					  int allow_dups)
{
	return fsyestify_add_mark(mark, &iyesde->i_fsyestify_marks,
				 FSNOTIFY_OBJ_TYPE_INODE, allow_dups, NULL);
}
static inline int fsyestify_add_iyesde_mark_locked(struct fsyestify_mark *mark,
						 struct iyesde *iyesde,
						 int allow_dups)
{
	return fsyestify_add_mark_locked(mark, &iyesde->i_fsyestify_marks,
					FSNOTIFY_OBJ_TYPE_INODE, allow_dups,
					NULL);
}

/* given a group and a mark, flag mark to be freed when all references are dropped */
extern void fsyestify_destroy_mark(struct fsyestify_mark *mark,
				  struct fsyestify_group *group);
/* detach mark from iyesde / mount list, group list, drop iyesde reference */
extern void fsyestify_detach_mark(struct fsyestify_mark *mark);
/* free mark */
extern void fsyestify_free_mark(struct fsyestify_mark *mark);
/* Wait until all marks queued for destruction are destroyed */
extern void fsyestify_wait_marks_destroyed(void);
/* run all the marks in a group, and clear all of the marks attached to given object type */
extern void fsyestify_clear_marks_by_group(struct fsyestify_group *group, unsigned int type);
/* run all the marks in a group, and clear all of the vfsmount marks */
static inline void fsyestify_clear_vfsmount_marks_by_group(struct fsyestify_group *group)
{
	fsyestify_clear_marks_by_group(group, FSNOTIFY_OBJ_TYPE_VFSMOUNT_FL);
}
/* run all the marks in a group, and clear all of the iyesde marks */
static inline void fsyestify_clear_iyesde_marks_by_group(struct fsyestify_group *group)
{
	fsyestify_clear_marks_by_group(group, FSNOTIFY_OBJ_TYPE_INODE_FL);
}
/* run all the marks in a group, and clear all of the sn marks */
static inline void fsyestify_clear_sb_marks_by_group(struct fsyestify_group *group)
{
	fsyestify_clear_marks_by_group(group, FSNOTIFY_OBJ_TYPE_SB_FL);
}
extern void fsyestify_get_mark(struct fsyestify_mark *mark);
extern void fsyestify_put_mark(struct fsyestify_mark *mark);
extern void fsyestify_finish_user_wait(struct fsyestify_iter_info *iter_info);
extern bool fsyestify_prepare_user_wait(struct fsyestify_iter_info *iter_info);

static inline void fsyestify_init_event(struct fsyestify_event *event,
				       struct iyesde *iyesde)
{
	INIT_LIST_HEAD(&event->list);
	event->iyesde = iyesde;
}

#else

static inline int fsyestify(struct iyesde *to_tell, __u32 mask, const void *data, int data_is,
			   const struct qstr *name, u32 cookie)
{
	return 0;
}

static inline int __fsyestify_parent(const struct path *path, struct dentry *dentry, __u32 mask)
{
	return 0;
}

static inline void __fsyestify_iyesde_delete(struct iyesde *iyesde)
{}

static inline void __fsyestify_vfsmount_delete(struct vfsmount *mnt)
{}

static inline void fsyestify_sb_delete(struct super_block *sb)
{}

static inline void fsyestify_update_flags(struct dentry *dentry)
{}

static inline u32 fsyestify_get_cookie(void)
{
	return 0;
}

static inline void fsyestify_unmount_iyesdes(struct super_block *sb)
{}

#endif	/* CONFIG_FSNOTIFY */

#endif	/* __KERNEL __ */

#endif	/* __LINUX_FSNOTIFY_BACKEND_H */
