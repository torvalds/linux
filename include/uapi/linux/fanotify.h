/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_FANOTIFY_H
#define _UAPI_LINUX_FANOTIFY_H

#include <linux/types.h>

/* the following events that user-space can register for */
#define FAN_ACCESS		0x00000001	/* File was accessed */
#define FAN_MODIFY		0x00000002	/* File was modified */
#define FAN_ATTRIB		0x00000004	/* Metadata changed */
#define FAN_CLOSE_WRITE		0x00000008	/* Writable file closed */
#define FAN_CLOSE_NOWRITE	0x00000010	/* Unwritable file closed */
#define FAN_OPEN		0x00000020	/* File was opened */
#define FAN_MOVED_FROM		0x00000040	/* File was moved from X */
#define FAN_MOVED_TO		0x00000080	/* File was moved to Y */
#define FAN_CREATE		0x00000100	/* Subfile was created */
#define FAN_DELETE		0x00000200	/* Subfile was deleted */
#define FAN_DELETE_SELF		0x00000400	/* Self was deleted */
#define FAN_MOVE_SELF		0x00000800	/* Self was moved */
#define FAN_OPEN_EXEC		0x00001000	/* File was opened for exec */

#define FAN_Q_OVERFLOW		0x00004000	/* Event queued overflowed */
#define FAN_FS_ERROR		0x00008000	/* Filesystem error */

#define FAN_OPEN_PERM		0x00010000	/* File open in perm check */
#define FAN_ACCESS_PERM		0x00020000	/* File accessed in perm check */
#define FAN_OPEN_EXEC_PERM	0x00040000	/* File open/exec in perm check */
/* #define FAN_DIR_MODIFY	0x00080000 */	/* Deprecated (reserved) */

#define FAN_PRE_ACCESS		0x00100000	/* Pre-content access hook */
#define FAN_MNT_ATTACH		0x01000000	/* Mount was attached */
#define FAN_MNT_DETACH		0x02000000	/* Mount was detached */

#define FAN_EVENT_ON_CHILD	0x08000000	/* Interested in child events */

#define FAN_RENAME		0x10000000	/* File was renamed */

#define FAN_ONDIR		0x40000000	/* Event occurred against dir */

/* helper events */
#define FAN_CLOSE		(FAN_CLOSE_WRITE | FAN_CLOSE_NOWRITE) /* close */
#define FAN_MOVE		(FAN_MOVED_FROM | FAN_MOVED_TO) /* moves */

/* flags used for fanotify_init() */
#define FAN_CLOEXEC		0x00000001
#define FAN_NONBLOCK		0x00000002

/* These are NOT bitwise flags.  Both bits are used together.  */
#define FAN_CLASS_NOTIF		0x00000000
#define FAN_CLASS_CONTENT	0x00000004
#define FAN_CLASS_PRE_CONTENT	0x00000008

/* Deprecated - do not use this in programs and do not add new flags here! */
#define FAN_ALL_CLASS_BITS	(FAN_CLASS_NOTIF | FAN_CLASS_CONTENT | \
				 FAN_CLASS_PRE_CONTENT)

#define FAN_UNLIMITED_QUEUE	0x00000010
#define FAN_UNLIMITED_MARKS	0x00000020
#define FAN_ENABLE_AUDIT	0x00000040

/* Flags to determine fanotify event format */
#define FAN_REPORT_PIDFD	0x00000080	/* Report pidfd for event->pid */
#define FAN_REPORT_TID		0x00000100	/* event->pid is thread id */
#define FAN_REPORT_FID		0x00000200	/* Report unique file id */
#define FAN_REPORT_DIR_FID	0x00000400	/* Report unique directory id */
#define FAN_REPORT_NAME		0x00000800	/* Report events with name */
#define FAN_REPORT_TARGET_FID	0x00001000	/* Report dirent target id  */
#define FAN_REPORT_FD_ERROR	0x00002000	/* event->fd can report error */
#define FAN_REPORT_MNT		0x00004000	/* Report mount events */

/* Convenience macro - FAN_REPORT_NAME requires FAN_REPORT_DIR_FID */
#define FAN_REPORT_DFID_NAME	(FAN_REPORT_DIR_FID | FAN_REPORT_NAME)
/* Convenience macro - FAN_REPORT_TARGET_FID requires all other FID flags */
#define FAN_REPORT_DFID_NAME_TARGET (FAN_REPORT_DFID_NAME | \
				     FAN_REPORT_FID | FAN_REPORT_TARGET_FID)

/* Deprecated - do not use this in programs and do not add new flags here! */
#define FAN_ALL_INIT_FLAGS	(FAN_CLOEXEC | FAN_NONBLOCK | \
				 FAN_ALL_CLASS_BITS | FAN_UNLIMITED_QUEUE |\
				 FAN_UNLIMITED_MARKS)

/* flags used for fanotify_modify_mark() */
#define FAN_MARK_ADD		0x00000001
#define FAN_MARK_REMOVE		0x00000002
#define FAN_MARK_DONT_FOLLOW	0x00000004
#define FAN_MARK_ONLYDIR	0x00000008
/* FAN_MARK_MOUNT is		0x00000010 */
#define FAN_MARK_IGNORED_MASK	0x00000020
#define FAN_MARK_IGNORED_SURV_MODIFY	0x00000040
#define FAN_MARK_FLUSH		0x00000080
/* FAN_MARK_FILESYSTEM is	0x00000100 */
#define FAN_MARK_EVICTABLE	0x00000200
/* This bit is mutually exclusive with FAN_MARK_IGNORED_MASK bit */
#define FAN_MARK_IGNORE		0x00000400

/* These are NOT bitwise flags.  Both bits can be used togther.  */
#define FAN_MARK_INODE		0x00000000
#define FAN_MARK_MOUNT		0x00000010
#define FAN_MARK_FILESYSTEM	0x00000100
#define FAN_MARK_MNTNS		0x00000110

/*
 * Convenience macro - FAN_MARK_IGNORE requires FAN_MARK_IGNORED_SURV_MODIFY
 * for non-inode mark types.
 */
#define FAN_MARK_IGNORE_SURV	(FAN_MARK_IGNORE | FAN_MARK_IGNORED_SURV_MODIFY)

/* Deprecated - do not use this in programs and do not add new flags here! */
#define FAN_ALL_MARK_FLAGS	(FAN_MARK_ADD |\
				 FAN_MARK_REMOVE |\
				 FAN_MARK_DONT_FOLLOW |\
				 FAN_MARK_ONLYDIR |\
				 FAN_MARK_MOUNT |\
				 FAN_MARK_IGNORED_MASK |\
				 FAN_MARK_IGNORED_SURV_MODIFY |\
				 FAN_MARK_FLUSH)

/* Deprecated - do not use this in programs and do not add new flags here! */
#define FAN_ALL_EVENTS (FAN_ACCESS |\
			FAN_MODIFY |\
			FAN_CLOSE |\
			FAN_OPEN)

/*
 * All events which require a permission response from userspace
 */
/* Deprecated - do not use this in programs and do not add new flags here! */
#define FAN_ALL_PERM_EVENTS (FAN_OPEN_PERM |\
			     FAN_ACCESS_PERM)

/* Deprecated - do not use this in programs and do not add new flags here! */
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

#define FAN_EVENT_INFO_TYPE_FID		1
#define FAN_EVENT_INFO_TYPE_DFID_NAME	2
#define FAN_EVENT_INFO_TYPE_DFID	3
#define FAN_EVENT_INFO_TYPE_PIDFD	4
#define FAN_EVENT_INFO_TYPE_ERROR	5
#define FAN_EVENT_INFO_TYPE_RANGE	6
#define FAN_EVENT_INFO_TYPE_MNT		7

/* Special info types for FAN_RENAME */
#define FAN_EVENT_INFO_TYPE_OLD_DFID_NAME	10
/* Reserved for FAN_EVENT_INFO_TYPE_OLD_DFID	11 */
#define FAN_EVENT_INFO_TYPE_NEW_DFID_NAME	12
/* Reserved for FAN_EVENT_INFO_TYPE_NEW_DFID	13 */

/* Variable length info record following event metadata */
struct fanotify_event_info_header {
	__u8 info_type;
	__u8 pad;
	__u16 len;
};

/*
 * Unique file identifier info record.
 * This structure is used for records of types FAN_EVENT_INFO_TYPE_FID,
 * FAN_EVENT_INFO_TYPE_DFID and FAN_EVENT_INFO_TYPE_DFID_NAME.
 * For FAN_EVENT_INFO_TYPE_DFID_NAME there is additionally a null terminated
 * name immediately after the file handle.
 */
struct fanotify_event_info_fid {
	struct fanotify_event_info_header hdr;
	__kernel_fsid_t fsid;
	/*
	 * Following is an opaque struct file_handle that can be passed as
	 * an argument to open_by_handle_at(2).
	 */
	unsigned char handle[];
};

/*
 * This structure is used for info records of type FAN_EVENT_INFO_TYPE_PIDFD.
 * It holds a pidfd for the pid that was responsible for generating an event.
 */
struct fanotify_event_info_pidfd {
	struct fanotify_event_info_header hdr;
	__s32 pidfd;
};

struct fanotify_event_info_error {
	struct fanotify_event_info_header hdr;
	__s32 error;
	__u32 error_count;
};

struct fanotify_event_info_range {
	struct fanotify_event_info_header hdr;
	__u32 pad;
	__u64 offset;
	__u64 count;
};

struct fanotify_event_info_mnt {
	struct fanotify_event_info_header hdr;
	__u64 mnt_id;
};

/*
 * User space may need to record additional information about its decision.
 * The extra information type records what kind of information is included.
 * The default is none. We also define an extra information buffer whose
 * size is determined by the extra information type.
 *
 * If the information type is Audit Rule, then the information following
 * is the rule number that triggered the user space decision that
 * requires auditing.
 */

#define FAN_RESPONSE_INFO_NONE		0
#define FAN_RESPONSE_INFO_AUDIT_RULE	1

struct fanotify_response {
	__s32 fd;
	__u32 response;
};

struct fanotify_response_info_header {
	__u8 type;
	__u8 pad;
	__u16 len;
};

struct fanotify_response_info_audit_rule {
	struct fanotify_response_info_header hdr;
	__u32 rule_number;
	__u32 subj_trust;
	__u32 obj_trust;
};

/* Legit userspace responses to a _PERM event */
#define FAN_ALLOW	0x01
#define FAN_DENY	0x02
/* errno other than EPERM can specified in upper byte of deny response */
#define FAN_ERRNO_BITS	8
#define FAN_ERRNO_SHIFT (32 - FAN_ERRNO_BITS)
#define FAN_ERRNO_MASK	((1 << FAN_ERRNO_BITS) - 1)
#define FAN_DENY_ERRNO(err) \
	(FAN_DENY | ((((__u32)(err)) & FAN_ERRNO_MASK) << FAN_ERRNO_SHIFT))

#define FAN_AUDIT	0x10	/* Bitmask to create audit record for result */
#define FAN_INFO	0x20	/* Bitmask to indicate additional information */

/* No fd set in event */
#define FAN_NOFD	-1
#define FAN_NOPIDFD	FAN_NOFD
#define FAN_EPIDFD	-2

/* Helper functions to deal with fanotify_event_metadata buffers */
#define FAN_EVENT_METADATA_LEN (sizeof(struct fanotify_event_metadata))

#define FAN_EVENT_NEXT(meta, len) ((len) -= (meta)->event_len, \
				   (struct fanotify_event_metadata*)(((char *)(meta)) + \
				   (meta)->event_len))

#define FAN_EVENT_OK(meta, len)	((long)(len) >= (long)FAN_EVENT_METADATA_LEN && \
				(long)(meta)->event_len >= (long)FAN_EVENT_METADATA_LEN && \
				(long)(meta)->event_len <= (long)(len))

#endif /* _UAPI_LINUX_FANOTIFY_H */
