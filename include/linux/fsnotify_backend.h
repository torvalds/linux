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

#define FS_OPEN_PERM		0x00010000	/* open event in an permission hook */
#define FS_ACCESS_PERM		0x00020000	/* access event in a permissions hook */

#define FS_EXCL_UNLINK		0x04000000	/* do not send events if object is unlinked */
#define FS_ISDIR		0x40000000	/* event occurred against dir */
#define FS_IN_ONESHOT		0x80000000	/* only send event once */

#define FS_DN_RENAME		0x10000000	/* file renamed */
#define FS_DN_MULTISHOT		0x20000000	/* dnotify multishot */

/* This inode cares about things that happen to its children.  Always set for
 * dnotify and inotify. */
#define FS_EVENT_ON_CHILD	0x08000000

/* This is a list of all events that may get sent to a parernt based on fs event
 * happening to inodes inside that directory */
#define FS_EVENTS_POSS_ON_CHILD   (FS_ACCESS | FS_MODIFY | FS_ATTRIB |\
				   FS_CLOSE_WRITE | FS_CLOSE_NOWRITE | FS_OPEN |\
				   FS_MOVED_FROM | FS_MOVED_TO | FS_CREATE |\
				   FS_DELETE | FS_OPEN_PERM | FS_ACCESS_PERM)

#define FS_MOVE			(FS_MOVED_FROM | FS_MOVED_TO)

#define ALL_FSNOTIFY_PERM_EVENTS (FS_OPEN_PERM | FS_ACCESS_PERM)

#define ALL_FSNOTIFY_EVENTS (FS_ACCESS | FS_MODIFY | FS_ATTRIB | \
			     FS_CLOSE_WRITE | FS_CLOSE_NOWRITE | FS_OPEN | \
			     FS_MOVED_FROM | FS_MOVED_TO | FS_CREATE | \
			     FS_DELETE | FS_DELETE_SELF | FS_MOVE_SELF | \
			     FS_UNMOUNT | FS_Q_OVERFLOW | FS_IN_IGNORED | \
			     FS_OPEN_PERM | FS_ACCESS_PERM | FS_EXCL_UNLINK | \
			     FS_ISDIR | FS_IN_ONESHOT | FS_DN_RENAME | \
			     FS_DN_MULTISHOT | FS_EVENT_ON_CHILD)

struct fsnotify_group;
struct fsnotify_event;
struct fsnotify_mark;
struct fsnotify_event_private_data;
struct fsnotify_fname;

/*
 * Each group much define these ops.  The fsnotify infrastructure will call
 * these operations for each relevant group.
 *
 * handle_event - main call for a group to handle an fs event
 * free_group_priv - called when a group refcnt hits 0 to clean up the private union
 * freeing_mark - called when a mark is being destroyed for some reason.  The group
 * 		MUST be holding a reference on each mark and that reference must be
 * 		dropped in this function.  inotify uses this function to send
 * 		userspace messages that marks have been removed.
 */
struct fsnotify_ops {
	int (*handle_event)(struct fsnotify_group *group,
			    struct inode *inode,
			    struct fsnotify_mark *inode_mark,
			    struct fsnotify_mark *vfsmount_mark,
			    u32 mask, void *data, int data_type,
			    const unsigned char *file_name, u32 cookie);
	void (*free_group_priv)(struct fsnotify_group *group);
	void (*freeing_mark)(struct fsnotify_mark *mark, struct fsnotify_group *group);
	void (*free_event)(struct fsnotify_event *event);
};

/*
 * all of the information about the original object we want to now send to
 * a group.  If you want to carry more info from the accessing task to the
 * listener this structure is where you need to be adding fields.
 */
struct fsnotify_event {
	struct list_head list;
	/* inode may ONLY be dereferenced during handle_event(). */
	struct inode *inode;	/* either the inode the event happened to or its parent */
	u32 mask;		/* the type of access, bitwise OR for FS_* event types */
};

/*
 * A group is a "thing" that wants to receive notification about filesystem
 * events.  The mask holds the subset of event types this group cares about.
 * refcnt on a group is up to the implementor and at any moment if it goes 0
 * everything will be cleaned up.
 */
struct fsnotify_group {
	/*
	 * How the refcnt is used is up to each group.  When the refcnt hits 0
	 * fsnotify will clean up all of the resources associated with this group.
	 * As an example, the dnotify group will always have a refcnt=1 and that
	 * will never change.  Inotify, on the other hand, has a group per
	 * inotify_init() and the refcnt will hit 0 only when that fd has been
	 * closed.
	 */
	atomic_t refcnt;		/* things with interest in this group */

	const struct fsnotify_ops *ops;	/* how this group handles things */

	/* needed to send notification to userspace */
	struct mutex notification_mutex;	/* protect the notification_list */
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

	/* stores all fastpath marks assoc with this group so they can be cleaned on unregister */
	struct mutex mark_mutex;	/* protect marks_list */
	atomic_t num_marks;		/* 1 for each mark and 1 for not being
					 * past the point of no return when freeing
					 * a group */
	struct list_head marks_list;	/* all inode marks for this group */

	struct fasync_struct *fsn_fa;    /* async notification */

	struct fsnotify_event *overflow_event;	/* Event we queue when the
						 * notification list is too
						 * full */

	/* groups can define private fields here or use the void *private */
	union {
		void *private;
#ifdef CONFIG_INOTIFY_USER
		struct inotify_group_private_data {
			spinlock_t	idr_lock;
			struct idr      idr;
			struct user_struct      *user;
		} inotify_data;
#endif
#ifdef CONFIG_FANOTIFY
		struct fanotify_group_private_data {
#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
			/* allows a group to block waiting for a userspace response */
			spinlock_t access_lock;
			struct list_head access_list;
			wait_queue_head_t access_waitq;
#endif /* CONFIG_FANOTIFY_ACCESS_PERMISSIONS */
			int f_flags;
			unsigned int max_marks;
			struct user_struct *user;
		} fanotify_data;
#endif /* CONFIG_FANOTIFY */
	};
};

/* when calling fsnotify tell it if the data is a path or inode */
#define FSNOTIFY_EVENT_NONE	0
#define FSNOTIFY_EVENT_PATH	1
#define FSNOTIFY_EVENT_INODE	2

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
	atomic_t refcnt;
	/* Group this mark is for. Set on mark creation, stable until last ref
	 * is dropped */
	struct fsnotify_group *group;
	/* List of marks by group->i_fsnotify_marks. Also reused for queueing
	 * mark into destroy_list when it's waiting for the end of SRCU period
	 * before it can be freed. [group->mark_mutex] */
	struct list_head g_list;
	/* Protects inode / mnt pointers, flags, masks */
	spinlock_t lock;
	/* List of marks for inode / vfsmount [obj_lock] */
	struct hlist_node obj_list;
	union {	/* Object pointer [mark->lock, group->mark_mutex] */
		struct inode *inode;	/* inode this mark is associated with */
		struct vfsmount *mnt;	/* vfsmount this mark is associated with */
	};
	/* Events types to ignore [mark->lock, group->mark_mutex] */
	__u32 ignored_mask;
#define FSNOTIFY_MARK_FLAG_INODE		0x01
#define FSNOTIFY_MARK_FLAG_VFSMOUNT		0x02
#define FSNOTIFY_MARK_FLAG_OBJECT_PINNED	0x04
#define FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY	0x08
#define FSNOTIFY_MARK_FLAG_ALIVE		0x10
#define FSNOTIFY_MARK_FLAG_ATTACHED		0x20
	unsigned int flags;		/* flags [mark->lock] */
	void (*free_mark)(struct fsnotify_mark *mark); /* called on final put+free */
};

#ifdef CONFIG_FSNOTIFY

/* called from the vfs helpers */

/* main fsnotify call to send events */
extern int fsnotify(struct inode *to_tell, __u32 mask, void *data, int data_is,
		    const unsigned char *name, u32 cookie);
extern int __fsnotify_parent(struct path *path, struct dentry *dentry, __u32 mask);
extern void __fsnotify_inode_delete(struct inode *inode);
extern void __fsnotify_vfsmount_delete(struct vfsmount *mnt);
extern u32 fsnotify_get_cookie(void);

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
extern struct fsnotify_group *fsnotify_alloc_group(const struct fsnotify_ops *ops);
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
extern int fsnotify_add_event(struct fsnotify_group *group,
			      struct fsnotify_event *event,
			      int (*merge)(struct list_head *,
					   struct fsnotify_event *));
/* true if the group notification queue is empty */
extern bool fsnotify_notify_queue_is_empty(struct fsnotify_group *group);
/* return, but do not dequeue the first event on the notification queue */
extern struct fsnotify_event *fsnotify_peek_first_event(struct fsnotify_group *group);
/* return AND dequeue the first event on the notification queue */
extern struct fsnotify_event *fsnotify_remove_first_event(struct fsnotify_group *group);

/* functions used to manipulate the marks attached to inodes */

/* run all marks associated with a vfsmount and update mnt->mnt_fsnotify_mask */
extern void fsnotify_recalc_vfsmount_mask(struct vfsmount *mnt);
/* run all marks associated with an inode and update inode->i_fsnotify_mask */
extern void fsnotify_recalc_inode_mask(struct inode *inode);
extern void fsnotify_init_mark(struct fsnotify_mark *mark, void (*free_mark)(struct fsnotify_mark *mark));
/* find (and take a reference) to a mark associated with group and inode */
extern struct fsnotify_mark *fsnotify_find_inode_mark(struct fsnotify_group *group, struct inode *inode);
/* find (and take a reference) to a mark associated with group and vfsmount */
extern struct fsnotify_mark *fsnotify_find_vfsmount_mark(struct fsnotify_group *group, struct vfsmount *mnt);
/* copy the values from old into new */
extern void fsnotify_duplicate_mark(struct fsnotify_mark *new, struct fsnotify_mark *old);
/* set the ignored_mask of a mark */
extern void fsnotify_set_mark_ignored_mask_locked(struct fsnotify_mark *mark, __u32 mask);
/* set the mask of a mark (might pin the object into memory */
extern void fsnotify_set_mark_mask_locked(struct fsnotify_mark *mark, __u32 mask);
/* attach the mark to both the group and the inode */
extern int fsnotify_add_mark(struct fsnotify_mark *mark, struct fsnotify_group *group,
			     struct inode *inode, struct vfsmount *mnt, int allow_dups);
extern int fsnotify_add_mark_locked(struct fsnotify_mark *mark, struct fsnotify_group *group,
				    struct inode *inode, struct vfsmount *mnt, int allow_dups);
/* given a group and a mark, flag mark to be freed when all references are dropped */
extern void fsnotify_destroy_mark(struct fsnotify_mark *mark,
				  struct fsnotify_group *group);
/* detach mark from inode / mount list, group list, drop inode reference */
extern void fsnotify_detach_mark(struct fsnotify_mark *mark);
/* free mark */
extern void fsnotify_free_mark(struct fsnotify_mark *mark);
/* run all the marks in a group, and clear all of the vfsmount marks */
extern void fsnotify_clear_vfsmount_marks_by_group(struct fsnotify_group *group);
/* run all the marks in a group, and clear all of the inode marks */
extern void fsnotify_clear_inode_marks_by_group(struct fsnotify_group *group);
/* run all the marks in a group, and clear all of the marks where mark->flags & flags is true*/
extern void fsnotify_clear_marks_by_group_flags(struct fsnotify_group *group, unsigned int flags);
extern void fsnotify_get_mark(struct fsnotify_mark *mark);
extern void fsnotify_put_mark(struct fsnotify_mark *mark);
extern void fsnotify_unmount_inodes(struct super_block *sb);

/* put here because inotify does some weird stuff when destroying watches */
extern void fsnotify_init_event(struct fsnotify_event *event,
				struct inode *to_tell, u32 mask);

#else

static inline int fsnotify(struct inode *to_tell, __u32 mask, void *data, int data_is,
			   const unsigned char *name, u32 cookie)
{
	return 0;
}

static inline int __fsnotify_parent(struct path *path, struct dentry *dentry, __u32 mask)
{
	return 0;
}

static inline void __fsnotify_inode_delete(struct inode *inode)
{}

static inline void __fsnotify_vfsmount_delete(struct vfsmount *mnt)
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
