/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FANOTIFY_H
#define _LINUX_FANOTIFY_H

#include <linux/sysctl.h>
#include <uapi/linux/fanotify.h>

#define FAN_GROUP_FLAG(group, flag) \
	((group)->fanotify_data.flags & (flag))

/*
 * Flags allowed to be passed from/to userspace.
 *
 * We intentionally do not add new bits to the old FAN_ALL_* constants, because
 * they are uapi exposed constants. If there are programs out there using
 * these constant, the programs may break if re-compiled with new uapi headers
 * and then run on an old kernel.
 */

/* Group classes where permission events are allowed */
#define FANOTIFY_PERM_CLASSES	(FAN_CLASS_CONTENT | \
				 FAN_CLASS_PRE_CONTENT)

#define FANOTIFY_CLASS_BITS	(FAN_CLASS_NOTIF | FANOTIFY_PERM_CLASSES)

#define FANOTIFY_FID_BITS	(FAN_REPORT_DFID_NAME_TARGET)

#define FANOTIFY_INFO_MODES	(FANOTIFY_FID_BITS | FAN_REPORT_PIDFD)

/*
 * fanotify_init() flags that require CAP_SYS_ADMIN.
 * We do not allow unprivileged groups to request permission events.
 * We do not allow unprivileged groups to get other process pid in events.
 * We do not allow unprivileged groups to use unlimited resources.
 */
#define FANOTIFY_ADMIN_INIT_FLAGS	(FANOTIFY_PERM_CLASSES | \
					 FAN_REPORT_TID | \
					 FAN_REPORT_PIDFD | \
					 FAN_UNLIMITED_QUEUE | \
					 FAN_UNLIMITED_MARKS)

/*
 * fanotify_init() flags that are allowed for user without CAP_SYS_ADMIN.
 * FAN_CLASS_NOTIF is the only class we allow for unprivileged group.
 * We do not allow unprivileged groups to get file descriptors in events,
 * so one of the flags for reporting file handles is required.
 */
#define FANOTIFY_USER_INIT_FLAGS	(FAN_CLASS_NOTIF | \
					 FANOTIFY_FID_BITS | \
					 FAN_CLOEXEC | FAN_NONBLOCK)

#define FANOTIFY_INIT_FLAGS	(FANOTIFY_ADMIN_INIT_FLAGS | \
				 FANOTIFY_USER_INIT_FLAGS)

/* Internal group flags */
#define FANOTIFY_UNPRIV		0x80000000
#define FANOTIFY_INTERNAL_GROUP_FLAGS	(FANOTIFY_UNPRIV)

#define FANOTIFY_MARK_TYPE_BITS	(FAN_MARK_INODE | FAN_MARK_MOUNT | \
				 FAN_MARK_FILESYSTEM)

#define FANOTIFY_MARK_CMD_BITS	(FAN_MARK_ADD | FAN_MARK_REMOVE | \
				 FAN_MARK_FLUSH)

#define FANOTIFY_MARK_IGNORE_BITS (FAN_MARK_IGNORED_MASK | \
				   FAN_MARK_IGNORE)

#define FANOTIFY_MARK_FLAGS	(FANOTIFY_MARK_TYPE_BITS | \
				 FANOTIFY_MARK_CMD_BITS | \
				 FANOTIFY_MARK_IGNORE_BITS | \
				 FAN_MARK_DONT_FOLLOW | \
				 FAN_MARK_ONLYDIR | \
				 FAN_MARK_IGNORED_SURV_MODIFY | \
				 FAN_MARK_EVICTABLE)

/*
 * Events that can be reported with data type FSNOTIFY_EVENT_PATH.
 * Note that FAN_MODIFY can also be reported with data type
 * FSNOTIFY_EVENT_INODE.
 */
#define FANOTIFY_PATH_EVENTS	(FAN_ACCESS | FAN_MODIFY | \
				 FAN_CLOSE | FAN_OPEN | FAN_OPEN_EXEC)

/*
 * Directory entry modification events - reported only to directory
 * where entry is modified and not to a watching parent.
 */
#define FANOTIFY_DIRENT_EVENTS	(FAN_MOVE | FAN_CREATE | FAN_DELETE | \
				 FAN_RENAME)

/* Events that can be reported with event->fd */
#define FANOTIFY_FD_EVENTS (FANOTIFY_PATH_EVENTS | FANOTIFY_PERM_EVENTS)

/* Events that can only be reported with data type FSNOTIFY_EVENT_INODE */
#define FANOTIFY_INODE_EVENTS	(FANOTIFY_DIRENT_EVENTS | \
				 FAN_ATTRIB | FAN_MOVE_SELF | FAN_DELETE_SELF)

/* Events that can only be reported with data type FSNOTIFY_EVENT_ERROR */
#define FANOTIFY_ERROR_EVENTS	(FAN_FS_ERROR)

/* Events that user can request to be notified on */
#define FANOTIFY_EVENTS		(FANOTIFY_PATH_EVENTS | \
				 FANOTIFY_INODE_EVENTS | \
				 FANOTIFY_ERROR_EVENTS)

/* Events that require a permission response from user */
#define FANOTIFY_PERM_EVENTS	(FAN_OPEN_PERM | FAN_ACCESS_PERM | \
				 FAN_OPEN_EXEC_PERM)

/* Extra flags that may be reported with event or control handling of events */
#define FANOTIFY_EVENT_FLAGS	(FAN_EVENT_ON_CHILD | FAN_ONDIR)

/* Events that may be reported to user */
#define FANOTIFY_OUTGOING_EVENTS	(FANOTIFY_EVENTS | \
					 FANOTIFY_PERM_EVENTS | \
					 FAN_Q_OVERFLOW | FAN_ONDIR)

/* Events and flags relevant only for directories */
#define FANOTIFY_DIRONLY_EVENT_BITS	(FANOTIFY_DIRENT_EVENTS | \
					 FAN_EVENT_ON_CHILD | FAN_ONDIR)

#define ALL_FANOTIFY_EVENT_BITS		(FANOTIFY_OUTGOING_EVENTS | \
					 FANOTIFY_EVENT_FLAGS)

/* Do not use these old uapi constants internally */
#undef FAN_ALL_CLASS_BITS
#undef FAN_ALL_INIT_FLAGS
#undef FAN_ALL_MARK_FLAGS
#undef FAN_ALL_EVENTS
#undef FAN_ALL_PERM_EVENTS
#undef FAN_ALL_OUTGOING_EVENTS

#endif /* _LINUX_FANOTIFY_H */
