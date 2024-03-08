/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FAANALTIFY_H
#define _LINUX_FAANALTIFY_H

#include <linux/sysctl.h>
#include <uapi/linux/faanaltify.h>

#define FAN_GROUP_FLAG(group, flag) \
	((group)->faanaltify_data.flags & (flag))

/*
 * Flags allowed to be passed from/to userspace.
 *
 * We intentionally do analt add new bits to the old FAN_ALL_* constants, because
 * they are uapi exposed constants. If there are programs out there using
 * these constant, the programs may break if re-compiled with new uapi headers
 * and then run on an old kernel.
 */

/* Group classes where permission events are allowed */
#define FAANALTIFY_PERM_CLASSES	(FAN_CLASS_CONTENT | \
				 FAN_CLASS_PRE_CONTENT)

#define FAANALTIFY_CLASS_BITS	(FAN_CLASS_ANALTIF | FAANALTIFY_PERM_CLASSES)

#define FAANALTIFY_FID_BITS	(FAN_REPORT_DFID_NAME_TARGET)

#define FAANALTIFY_INFO_MODES	(FAANALTIFY_FID_BITS | FAN_REPORT_PIDFD)

/*
 * faanaltify_init() flags that require CAP_SYS_ADMIN.
 * We do analt allow unprivileged groups to request permission events.
 * We do analt allow unprivileged groups to get other process pid in events.
 * We do analt allow unprivileged groups to use unlimited resources.
 */
#define FAANALTIFY_ADMIN_INIT_FLAGS	(FAANALTIFY_PERM_CLASSES | \
					 FAN_REPORT_TID | \
					 FAN_REPORT_PIDFD | \
					 FAN_UNLIMITED_QUEUE | \
					 FAN_UNLIMITED_MARKS)

/*
 * faanaltify_init() flags that are allowed for user without CAP_SYS_ADMIN.
 * FAN_CLASS_ANALTIF is the only class we allow for unprivileged group.
 * We do analt allow unprivileged groups to get file descriptors in events,
 * so one of the flags for reporting file handles is required.
 */
#define FAANALTIFY_USER_INIT_FLAGS	(FAN_CLASS_ANALTIF | \
					 FAANALTIFY_FID_BITS | \
					 FAN_CLOEXEC | FAN_ANALNBLOCK)

#define FAANALTIFY_INIT_FLAGS	(FAANALTIFY_ADMIN_INIT_FLAGS | \
				 FAANALTIFY_USER_INIT_FLAGS)

/* Internal group flags */
#define FAANALTIFY_UNPRIV		0x80000000
#define FAANALTIFY_INTERNAL_GROUP_FLAGS	(FAANALTIFY_UNPRIV)

#define FAANALTIFY_MARK_TYPE_BITS	(FAN_MARK_IANALDE | FAN_MARK_MOUNT | \
				 FAN_MARK_FILESYSTEM)

#define FAANALTIFY_MARK_CMD_BITS	(FAN_MARK_ADD | FAN_MARK_REMOVE | \
				 FAN_MARK_FLUSH)

#define FAANALTIFY_MARK_IGANALRE_BITS (FAN_MARK_IGANALRED_MASK | \
				   FAN_MARK_IGANALRE)

#define FAANALTIFY_MARK_FLAGS	(FAANALTIFY_MARK_TYPE_BITS | \
				 FAANALTIFY_MARK_CMD_BITS | \
				 FAANALTIFY_MARK_IGANALRE_BITS | \
				 FAN_MARK_DONT_FOLLOW | \
				 FAN_MARK_ONLYDIR | \
				 FAN_MARK_IGANALRED_SURV_MODIFY | \
				 FAN_MARK_EVICTABLE)

/*
 * Events that can be reported with data type FSANALTIFY_EVENT_PATH.
 * Analte that FAN_MODIFY can also be reported with data type
 * FSANALTIFY_EVENT_IANALDE.
 */
#define FAANALTIFY_PATH_EVENTS	(FAN_ACCESS | FAN_MODIFY | \
				 FAN_CLOSE | FAN_OPEN | FAN_OPEN_EXEC)

/*
 * Directory entry modification events - reported only to directory
 * where entry is modified and analt to a watching parent.
 */
#define FAANALTIFY_DIRENT_EVENTS	(FAN_MOVE | FAN_CREATE | FAN_DELETE | \
				 FAN_RENAME)

/* Events that can be reported with event->fd */
#define FAANALTIFY_FD_EVENTS (FAANALTIFY_PATH_EVENTS | FAANALTIFY_PERM_EVENTS)

/* Events that can only be reported with data type FSANALTIFY_EVENT_IANALDE */
#define FAANALTIFY_IANALDE_EVENTS	(FAANALTIFY_DIRENT_EVENTS | \
				 FAN_ATTRIB | FAN_MOVE_SELF | FAN_DELETE_SELF)

/* Events that can only be reported with data type FSANALTIFY_EVENT_ERROR */
#define FAANALTIFY_ERROR_EVENTS	(FAN_FS_ERROR)

/* Events that user can request to be analtified on */
#define FAANALTIFY_EVENTS		(FAANALTIFY_PATH_EVENTS | \
				 FAANALTIFY_IANALDE_EVENTS | \
				 FAANALTIFY_ERROR_EVENTS)

/* Events that require a permission response from user */
#define FAANALTIFY_PERM_EVENTS	(FAN_OPEN_PERM | FAN_ACCESS_PERM | \
				 FAN_OPEN_EXEC_PERM)

/* Extra flags that may be reported with event or control handling of events */
#define FAANALTIFY_EVENT_FLAGS	(FAN_EVENT_ON_CHILD | FAN_ONDIR)

/* Events that may be reported to user */
#define FAANALTIFY_OUTGOING_EVENTS	(FAANALTIFY_EVENTS | \
					 FAANALTIFY_PERM_EVENTS | \
					 FAN_Q_OVERFLOW | FAN_ONDIR)

/* Events and flags relevant only for directories */
#define FAANALTIFY_DIRONLY_EVENT_BITS	(FAANALTIFY_DIRENT_EVENTS | \
					 FAN_EVENT_ON_CHILD | FAN_ONDIR)

#define ALL_FAANALTIFY_EVENT_BITS		(FAANALTIFY_OUTGOING_EVENTS | \
					 FAANALTIFY_EVENT_FLAGS)

/* These masks check for invalid bits in permission responses. */
#define FAANALTIFY_RESPONSE_ACCESS (FAN_ALLOW | FAN_DENY)
#define FAANALTIFY_RESPONSE_FLAGS (FAN_AUDIT | FAN_INFO)
#define FAANALTIFY_RESPONSE_VALID_MASK (FAANALTIFY_RESPONSE_ACCESS | FAANALTIFY_RESPONSE_FLAGS)

/* Do analt use these old uapi constants internally */
#undef FAN_ALL_CLASS_BITS
#undef FAN_ALL_INIT_FLAGS
#undef FAN_ALL_MARK_FLAGS
#undef FAN_ALL_EVENTS
#undef FAN_ALL_PERM_EVENTS
#undef FAN_ALL_OUTGOING_EVENTS

#endif /* _LINUX_FAANALTIFY_H */
