/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_WATCH_QUEUE_H
#define _UAPI_LINUX_WATCH_QUEUE_H

#include <linux/types.h>

enum watch_notification_type {
	WATCH_TYPE_META		= 0,	/* Special record */
	WATCH_TYPE__NR		= 1
};

enum watch_meta_notification_subtype {
	WATCH_META_REMOVAL_NOTIFICATION	= 0,	/* Watched object was removed */
	WATCH_META_LOSS_NOTIFICATION	= 1,	/* Data loss occurred */
};

/*
 * Notification record header.  This is aligned to 64-bits so that subclasses
 * can contain __u64 fields.
 */
struct watch_notification {
	__u32			type:24;	/* enum watch_notification_type */
	__u32			subtype:8;	/* Type-specific subtype (filterable) */
	__u32			info;
#define WATCH_INFO_LENGTH	0x0000007f	/* Length of record */
#define WATCH_INFO_LENGTH__SHIFT 0
#define WATCH_INFO_ID		0x0000ff00	/* ID of watchpoint */
#define WATCH_INFO_ID__SHIFT	8
#define WATCH_INFO_TYPE_INFO	0xffff0000	/* Type-specific info */
#define WATCH_INFO_TYPE_INFO__SHIFT 16
#define WATCH_INFO_FLAG_0	0x00010000	/* Type-specific info, flag bit 0 */
#define WATCH_INFO_FLAG_1	0x00020000	/* ... */
#define WATCH_INFO_FLAG_2	0x00040000
#define WATCH_INFO_FLAG_3	0x00080000
#define WATCH_INFO_FLAG_4	0x00100000
#define WATCH_INFO_FLAG_5	0x00200000
#define WATCH_INFO_FLAG_6	0x00400000
#define WATCH_INFO_FLAG_7	0x00800000
};


/*
 * Extended watch removal notification.  This is used optionally if the type
 * wants to indicate an identifier for the object being watched, if there is
 * such.  This can be distinguished by the length.
 *
 * type -> WATCH_TYPE_META
 * subtype -> WATCH_META_REMOVAL_NOTIFICATION
 */
struct watch_notification_removal {
	struct watch_notification watch;
	__u64	id;		/* Type-dependent identifier */
};

#endif /* _UAPI_LINUX_WATCH_QUEUE_H */
