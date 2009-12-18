#ifndef _LINUX_FANOTIFY_H
#define _LINUX_FANOTIFY_H

#include <linux/types.h>

/* the following events that user-space can register for */
#define FAN_ACCESS		0x00000001	/* File was accessed */
#define FAN_MODIFY		0x00000002	/* File was modified */
#define FAN_CLOSE_WRITE		0x00000008	/* Unwrittable file closed */
#define FAN_CLOSE_NOWRITE	0x00000010	/* Writtable file closed */
#define FAN_OPEN		0x00000020	/* File was opened */

#define FAN_EVENT_ON_CHILD	0x08000000	/* interested in child events */

/* FIXME currently Q's have no limit.... */
#define FAN_Q_OVERFLOW		0x00004000	/* Event queued overflowed */

/* helper events */
#define FAN_CLOSE		(FAN_CLOSE_WRITE | FAN_CLOSE_NOWRITE) /* close */

/* flags used for fanotify_init() */
#define FAN_CLOEXEC		0x00000001
#define FAN_NONBLOCK		0x00000002

#define FAN_ALL_INIT_FLAGS	(FAN_CLOEXEC | FAN_NONBLOCK)

/* flags used for fanotify_modify_mark() */
#define FAN_MARK_ADD		0x00000001
#define FAN_MARK_REMOVE		0x00000002
#define FAN_MARK_DONT_FOLLOW	0x00000004
#define FAN_MARK_ONLYDIR	0x00000008
#define FAN_MARK_MOUNT		0x00000010

#define FAN_ALL_MARK_FLAGS	(FAN_MARK_ADD |\
				 FAN_MARK_REMOVE |\
				 FAN_MARK_DONT_FOLLOW |\
				 FAN_MARK_ONLYDIR |\
				 FAN_MARK_MOUNT)

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
 * All legal FAN bits userspace can request (although possibly not all
 * at the same time.
 */
#define FAN_ALL_INCOMING_EVENTS	(FAN_ALL_EVENTS |\
				 FAN_EVENT_ON_CHILD)

#define FAN_ALL_OUTGOING_EVENTS	(FAN_ALL_EVENTS |\
				 FAN_Q_OVERFLOW)

#define FANOTIFY_METADATA_VERSION	1

struct fanotify_event_metadata {
	__u32 event_len;
	__u32 vers;
	__s32 fd;
	__u64 mask;
	__s64 pid;
} __attribute__ ((packed));

/* Helper functions to deal with fanotify_event_metadata buffers */
#define FAN_EVENT_METADATA_LEN (sizeof(struct fanotify_event_metadata))

#define FAN_EVENT_NEXT(meta, len) ((len) -= (meta)->event_len, \
				   (struct fanotify_event_metadata*)(((char *)(meta)) + \
				   (meta)->event_len))

#define FAN_EVENT_OK(meta, len)	((long)(len) >= (long)FAN_EVENT_METADATA_LEN && \
				(long)(meta)->event_len >= (long)FAN_EVENT_METADATA_LEN && \
				(long)(meta)->event_len <= (long)(len))

#ifdef __KERNEL__

#endif /* __KERNEL__ */
#endif /* _LINUX_FANOTIFY_H */
