/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_FANOTIFY_H
#define _UAPI_LINUX_FANOTIFY_H

#include <linux/types.h>

/* the following events that user-space can register for */
#define FAN_ACCESS		0x00000001	/* File was accessed */
#define FAN_MODIFY		0x00000002	/* File was modified */
#define FAN_CLOSE_WRITE		0x00000008	/* Writtable file closed */
#define FAN_CLOSE_NOWRITE	0x00000010	/* Unwrittable file closed */
#define FAN_OPEN		0x00000020	/* File was opened */

#define FAN_Q_OVERFLOW		0x00004000	/* Event queued overflowed */

#define FAN_OPEN_PERM		0x00010000	/* File open in perm check */
#define FAN_ACCESS_PERM		0x00020000	/* File accessed in perm check */

#define FAN_ONDIR		0x40000000	/* event occurred against dir */

#define FAN_EVENT_ON_CHILD	0x08000000	/* interested in child events */

/* helper events */
#define FAN_CLOSE		(FAN_CLOSE_WRITE | FAN_CLOSE_NOWRITE) /* close */

/* flags used for fanotify_init() */
#define FAN_CLOEXEC		0x00000001
#define FAN_NONBLOCK		0x00000002

/* These are NOT bitwise flags.  Both bits are used togther.  */
#define FAN_CLASS_NOTIF		0x00000000
#define FAN_CLASS_CONTENT	0x00000004
#define FAN_CLASS_PRE_CONTENT	0x00000008
#define FAN_ALL_CLASS_BITS	(FAN_CLASS_NOTIF | FAN_CLASS_CONTENT | \
				 FAN_CLASS_PRE_CONTENT)

#define FAN_UNLIMITED_QUEUE	0x00000010
#define FAN_UNLIMITED_MARKS	0x00000020
#define FAN_ENABLE_AUDIT	0x00000040

#define FAN_ALL_INIT_FLAGS	(FAN_CLOEXEC | FAN_NONBLOCK | \
				 FAN_ALL_CLASS_BITS | FAN_UNLIMITED_QUEUE |\
				 FAN_UNLIMITED_MARKS)

/* flags used for fanotify_modify_mark() */
#define FAN_MARK_ADD		0x00000001
#define FAN_MARK_REMOVE		0x00000002
#define FAN_MARK_DONT_FOLLOW	0x00000004
#define FAN_MARK_ONLYDIR	0x00000008
#define FAN_MARK_MOUNT		0x00000010
#define FAN_MARK_IGNORED_MASK	0x00000020
#define FAN_MARK_IGNORED_SURV_MODIFY	0x00000040
#define FAN_MARK_FLUSH		0x00000080

#define FAN_ALL_MARK_FLAGS	(FAN_MARK_ADD |\
				 FAN_MARK_REMOVE |\
				 FAN_MARK_DONT_FOLLOW |\
				 FAN_MARK_ONLYDIR |\
				 FAN_MARK_MOUNT |\
				 FAN_MARK_IGNORED_MASK |\
				 FAN_MARK_IGNORED_SURV_MODIFY |\
				 FAN_MARK_FLUSH)

/*
 * All of the events - we build the list by hand so that we can add flags in
 * the future and not break backward compatibility.  Apps will get only the
 * events that they originally wanted.  Be sure to add new events here!
 */
#define FAN_ALL_EVENTS (FAN_ACCESS |\
			FAN_MODIFY |\
			FAN_CLOSE |\
			FAN_OPEN)

/*
 * All events which require a permission response from userspace
 */
#define FAN_ALL_PERM_EVENTS (FAN_OPEN_PERM |\
			     FAN_ACCESS_PERM)

#define FAN_ALL_OUTGOING_EVENTS	(FAN_ALL_EVENTS |\
				 FAN_ALL_PERM_EVENTS |\
				 FAN_Q_OVERFLOW)

#define FANOTIFY_METADATA_VERSION	3

struct fanotify_event_metadata {
	__u32 event_len;
	__u8 vers;
	__u8 reserved;
	__u16 metadata_len;
	__aligned_u64 mask;
	__s32 fd;
	__s32 pid;
};

struct fanotify_response {
	__s32 fd;
	__u32 response;
};

/* Legit userspace responses to a _PERM event */
#define FAN_ALLOW	0x01
#define FAN_DENY	0x02
#define FAN_AUDIT	0x10	/* Bit mask to create audit record for result */

/* No fd set in event */
#define FAN_NOFD	-1

/* Helper functions to deal with fanotify_event_metadata buffers */
#define FAN_EVENT_METADATA_LEN (sizeof(struct fanotify_event_metadata))

#define FAN_EVENT_NEXT(meta, len) ((len) -= (meta)->event_len, \
				   (struct fanotify_event_metadata*)(((char *)(meta)) + \
				   (meta)->event_len))

#define FAN_EVENT_OK(meta, len)	((long)(len) >= (long)FAN_EVENT_METADATA_LEN && \
				(long)(meta)->event_len >= (long)FAN_EVENT_METADATA_LEN && \
				(long)(meta)->event_len <= (long)(len))

#endif /* _UAPI_LINUX_FANOTIFY_H */
