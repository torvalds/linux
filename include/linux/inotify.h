/*
 * Inode based directory notification for Linux
 *
 * Copyright (C) 2005 John McCutchan
 */

#ifndef _LINUX_INOTIFY_H
#define _LINUX_INOTIFY_H

#include <linux/types.h>

/*
 * struct inotify_event - structure read from the inotify device for each event
 *
 * When you are watching a directory, you will receive the filename for events
 * such as IN_CREATE, IN_DELETE, IN_OPEN, IN_CLOSE, ..., relative to the wd.
 */
struct inotify_event {
	__s32		wd;		/* watch descriptor */
	__u32		mask;		/* watch mask */
	__u32		cookie;		/* cookie to synchronize two events */
	__u32		len;		/* length (including nulls) of name */
	char		name[0];	/* stub for possible name */
};

/* the following are legal, implemented events that user-space can watch for */
#define IN_ACCESS		0x00000001	/* File was accessed */
#define IN_MODIFY		0x00000002	/* File was modified */
#define IN_ATTRIB		0x00000004	/* Metadata changed */
#define IN_CLOSE_WRITE		0x00000008	/* Writtable file was closed */
#define IN_CLOSE_NOWRITE	0x00000010	/* Unwrittable file closed */
#define IN_OPEN			0x00000020	/* File was opened */
#define IN_MOVED_FROM		0x00000040	/* File was moved from X */
#define IN_MOVED_TO		0x00000080	/* File was moved to Y */
#define IN_CREATE		0x00000100	/* Subfile was created */
#define IN_DELETE		0x00000200	/* Subfile was deleted */
#define IN_DELETE_SELF		0x00000400	/* Self was deleted */
#define IN_MOVE_SELF		0x00000800	/* Self was moved */

/* the following are legal events.  they are sent as needed to any watch */
#define IN_UNMOUNT		0x00002000	/* Backing fs was unmounted */
#define IN_Q_OVERFLOW		0x00004000	/* Event queued overflowed */
#define IN_IGNORED		0x00008000	/* File was ignored */

/* helper events */
#define IN_CLOSE		(IN_CLOSE_WRITE | IN_CLOSE_NOWRITE) /* close */
#define IN_MOVE			(IN_MOVED_FROM | IN_MOVED_TO) /* moves */

/* special flags */
#define IN_ONLYDIR		0x01000000	/* only watch the path if it is a directory */
#define IN_DONT_FOLLOW		0x02000000	/* don't follow a sym link */
#define IN_MASK_ADD		0x20000000	/* add to the mask of an already existing watch */
#define IN_ISDIR		0x40000000	/* event occurred against dir */
#define IN_ONESHOT		0x80000000	/* only send event once */

/*
 * All of the events - we build the list by hand so that we can add flags in
 * the future and not break backward compatibility.  Apps will get only the
 * events that they originally wanted.  Be sure to add new events here!
 */
#define IN_ALL_EVENTS	(IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | \
			 IN_CLOSE_NOWRITE | IN_OPEN | IN_MOVED_FROM | \
			 IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_DELETE_SELF | \
			 IN_MOVE_SELF)

#ifdef __KERNEL__

#include <linux/dcache.h>
#include <linux/fs.h>

#ifdef CONFIG_INOTIFY

extern void inotify_d_instantiate(struct dentry *, struct inode *);
extern void inotify_d_move(struct dentry *);
extern void inotify_inode_queue_event(struct inode *, __u32, __u32,
				      const char *);
extern void inotify_dentry_parent_queue_event(struct dentry *, __u32, __u32,
					      const char *);
extern void inotify_unmount_inodes(struct list_head *);
extern void inotify_inode_is_dead(struct inode *);
extern u32 inotify_get_cookie(void);

#else

static inline void inotify_d_instantiate(struct dentry *dentry,
					struct inode *inode)
{
}

static inline void inotify_d_move(struct dentry *dentry)
{
}

static inline void inotify_inode_queue_event(struct inode *inode,
					     __u32 mask, __u32 cookie,
					     const char *filename)
{
}

static inline void inotify_dentry_parent_queue_event(struct dentry *dentry,
						     __u32 mask, __u32 cookie,
						     const char *filename)
{
}

static inline void inotify_unmount_inodes(struct list_head *list)
{
}

static inline void inotify_inode_is_dead(struct inode *inode)
{
}

static inline u32 inotify_get_cookie(void)
{
	return 0;
}

#endif	/* CONFIG_INOTIFY */

#endif	/* __KERNEL __ */

#endif	/* _LINUX_INOTIFY_H */
