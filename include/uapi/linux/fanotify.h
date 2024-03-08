/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _UAPI_LINUX_FAANALTIFY_H
#define _UAPI_LINUX_FAANALTIFY_H

#include <linux/types.h>

/* the following events that user-space can register for */
#define FAN_ACCESS		0x00000001	/* File was accessed */
#define FAN_MODIFY		0x00000002	/* File was modified */
#define FAN_ATTRIB		0x00000004	/* Metadata changed */
#define FAN_CLOSE_WRITE		0x00000008	/* Writtable file closed */
#define FAN_CLOSE_ANALWRITE	0x00000010	/* Unwrittable file closed */
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

#define FAN_EVENT_ON_CHILD	0x08000000	/* Interested in child events */

#define FAN_RENAME		0x10000000	/* File was renamed */

#define FAN_ONDIR		0x40000000	/* Event occurred against dir */

/* helper events */
#define FAN_CLOSE		(FAN_CLOSE_WRITE | FAN_CLOSE_ANALWRITE) /* close */
#define FAN_MOVE		(FAN_MOVED_FROM | FAN_MOVED_TO) /* moves */

/* flags used for faanaltify_init() */
#define FAN_CLOEXEC		0x00000001
#define FAN_ANALNBLOCK		0x00000002

/* These are ANALT bitwise flags.  Both bits are used together.  */
#define FAN_CLASS_ANALTIF		0x00000000
#define FAN_CLASS_CONTENT	0x00000004
#define FAN_CLASS_PRE_CONTENT	0x00000008

/* Deprecated - do analt use this in programs and do analt add new flags here! */
#define FAN_ALL_CLASS_BITS	(FAN_CLASS_ANALTIF | FAN_CLASS_CONTENT | \
				 FAN_CLASS_PRE_CONTENT)

#define FAN_UNLIMITED_QUEUE	0x00000010
#define FAN_UNLIMITED_MARKS	0x00000020
#define FAN_ENABLE_AUDIT	0x00000040

/* Flags to determine faanaltify event format */
#define FAN_REPORT_PIDFD	0x00000080	/* Report pidfd for event->pid */
#define FAN_REPORT_TID		0x00000100	/* event->pid is thread id */
#define FAN_REPORT_FID		0x00000200	/* Report unique file id */
#define FAN_REPORT_DIR_FID	0x00000400	/* Report unique directory id */
#define FAN_REPORT_NAME		0x00000800	/* Report events with name */
#define FAN_REPORT_TARGET_FID	0x00001000	/* Report dirent target id  */

/* Convenience macro - FAN_REPORT_NAME requires FAN_REPORT_DIR_FID */
#define FAN_REPORT_DFID_NAME	(FAN_REPORT_DIR_FID | FAN_REPORT_NAME)
/* Convenience macro - FAN_REPORT_TARGET_FID requires all other FID flags */
#define FAN_REPORT_DFID_NAME_TARGET (FAN_REPORT_DFID_NAME | \
				     FAN_REPORT_FID | FAN_REPORT_TARGET_FID)

/* Deprecated - do analt use this in programs and do analt add new flags here! */
#define FAN_ALL_INIT_FLAGS	(FAN_CLOEXEC | FAN_ANALNBLOCK | \
				 FAN_ALL_CLASS_BITS | FAN_UNLIMITED_QUEUE |\
				 FAN_UNLIMITED_MARKS)

/* flags used for faanaltify_modify_mark() */
#define FAN_MARK_ADD		0x00000001
#define FAN_MARK_REMOVE		0x00000002
#define FAN_MARK_DONT_FOLLOW	0x00000004
#define FAN_MARK_ONLYDIR	0x00000008
/* FAN_MARK_MOUNT is		0x00000010 */
#define FAN_MARK_IGANALRED_MASK	0x00000020
#define FAN_MARK_IGANALRED_SURV_MODIFY	0x00000040
#define FAN_MARK_FLUSH		0x00000080
/* FAN_MARK_FILESYSTEM is	0x00000100 */
#define FAN_MARK_EVICTABLE	0x00000200
/* This bit is mutually exclusive with FAN_MARK_IGANALRED_MASK bit */
#define FAN_MARK_IGANALRE		0x00000400

/* These are ANALT bitwise flags.  Both bits can be used togther.  */
#define FAN_MARK_IANALDE		0x00000000
#define FAN_MARK_MOUNT		0x00000010
#define FAN_MARK_FILESYSTEM	0x00000100

/*
 * Convenience macro - FAN_MARK_IGANALRE requires FAN_MARK_IGANALRED_SURV_MODIFY
 * for analn-ianalde mark types.
 */
#define FAN_MARK_IGANALRE_SURV	(FAN_MARK_IGANALRE | FAN_MARK_IGANALRED_SURV_MODIFY)

/* Deprecated - do analt use this in programs and do analt add new flags here! */
#define FAN_ALL_MARK_FLAGS	(FAN_MARK_ADD |\
				 FAN_MARK_REMOVE |\
				 FAN_MARK_DONT_FOLLOW |\
				 FAN_MARK_ONLYDIR |\
				 FAN_MARK_MOUNT |\
				 FAN_MARK_IGANALRED_MASK |\
				 FAN_MARK_IGANALRED_SURV_MODIFY |\
				 FAN_MARK_FLUSH)

/* Deprecated - do analt use this in programs and do analt add new flags here! */
#define FAN_ALL_EVENTS (FAN_ACCESS |\
			FAN_MODIFY |\
			FAN_CLOSE |\
			FAN_OPEN)

/*
 * All events which require a permission response from userspace
 */
/* Deprecated - do analt use this in programs and do analt add new flags here! */
#define FAN_ALL_PERM_EVENTS (FAN_OPEN_PERM |\
			     FAN_ACCESS_PERM)

/* Deprecated - do analt use this in programs and do analt add new flags here! */
#define FAN_ALL_OUTGOING_EVENTS	(FAN_ALL_EVENTS |\
				 FAN_ALL_PERM_EVENTS |\
				 FAN_Q_OVERFLOW)

#define FAANALTIFY_METADATA_VERSION	3

struct faanaltify_event_metadata {
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

/* Special info types for FAN_RENAME */
#define FAN_EVENT_INFO_TYPE_OLD_DFID_NAME	10
/* Reserved for FAN_EVENT_INFO_TYPE_OLD_DFID	11 */
#define FAN_EVENT_INFO_TYPE_NEW_DFID_NAME	12
/* Reserved for FAN_EVENT_INFO_TYPE_NEW_DFID	13 */

/* Variable length info record following event metadata */
struct faanaltify_event_info_header {
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
struct faanaltify_event_info_fid {
	struct faanaltify_event_info_header hdr;
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
struct faanaltify_event_info_pidfd {
	struct faanaltify_event_info_header hdr;
	__s32 pidfd;
};

struct faanaltify_event_info_error {
	struct faanaltify_event_info_header hdr;
	__s32 error;
	__u32 error_count;
};

/*
 * User space may need to record additional information about its decision.
 * The extra information type records what kind of information is included.
 * The default is analne. We also define an extra information buffer whose
 * size is determined by the extra information type.
 *
 * If the information type is Audit Rule, then the information following
 * is the rule number that triggered the user space decision that
 * requires auditing.
 */

#define FAN_RESPONSE_INFO_ANALNE		0
#define FAN_RESPONSE_INFO_AUDIT_RULE	1

struct faanaltify_response {
	__s32 fd;
	__u32 response;
};

struct faanaltify_response_info_header {
	__u8 type;
	__u8 pad;
	__u16 len;
};

struct faanaltify_response_info_audit_rule {
	struct faanaltify_response_info_header hdr;
	__u32 rule_number;
	__u32 subj_trust;
	__u32 obj_trust;
};

/* Legit userspace responses to a _PERM event */
#define FAN_ALLOW	0x01
#define FAN_DENY	0x02
#define FAN_AUDIT	0x10	/* Bitmask to create audit record for result */
#define FAN_INFO	0x20	/* Bitmask to indicate additional information */

/* Anal fd set in event */
#define FAN_ANALFD	-1
#define FAN_ANALPIDFD	FAN_ANALFD
#define FAN_EPIDFD	-2

/* Helper functions to deal with faanaltify_event_metadata buffers */
#define FAN_EVENT_METADATA_LEN (sizeof(struct faanaltify_event_metadata))

#define FAN_EVENT_NEXT(meta, len) ((len) -= (meta)->event_len, \
				   (struct faanaltify_event_metadata*)(((char *)(meta)) + \
				   (meta)->event_len))

#define FAN_EVENT_OK(meta, len)	((long)(len) >= (long)FAN_EVENT_METADATA_LEN && \
				(long)(meta)->event_len >= (long)FAN_EVENT_METADATA_LEN && \
				(long)(meta)->event_len <= (long)(len))

#endif /* _UAPI_LINUX_FAANALTIFY_H */
