#undef TRACE_SYSTEM
#define TRACE_SYSTEM android_fs

#if !defined(_TRACE_ANDROID_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ANDROID_FS_H

#include <linux/tracepoint.h>
#include <trace/events/android_fs_template.h>

DEFINE_EVENT(android_fs_data_start_template, android_fs_dataread_start,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes,
		 pid_t pid, char *command),
	TP_ARGS(inode, offset, bytes, pid, command));

DEFINE_EVENT(android_fs_data_end_template, android_fs_dataread_end,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes),
	TP_ARGS(inode, offset, bytes));

DEFINE_EVENT(android_fs_data_start_template, android_fs_datawrite_start,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes,
		 pid_t pid, char *command),
	TP_ARGS(inode, offset, bytes, pid, command));

DEFINE_EVENT(android_fs_data_end_template, android_fs_datawrite_end,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes),
	TP_ARGS(inode, offset, bytes));

#endif /* _TRACE_ANDROID_FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
