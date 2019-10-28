#if !defined(_TRACE_ANDROID_FS_TEMPLATE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ANDROID_FS_TEMPLATE_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(android_fs_data_start_template,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes,
		 pid_t pid, char *pathname, char *command),
	TP_ARGS(inode, offset, bytes, pid, pathname, command),
	TP_STRUCT__entry(
		__string(pathbuf, pathname);
		__field(loff_t,	offset);
		__field(int,	bytes);
		__field(loff_t,	i_size);
		__string(cmdline, command);
		__field(pid_t,	pid);
		__field(ino_t,	ino);
	),
	TP_fast_assign(
		{
			/*
			 * Replace the spaces in filenames and cmdlines
			 * because this screws up the tooling that parses
			 * the traces.
			 */
			__assign_str(pathbuf, pathname);
			(void)strreplace(__get_str(pathbuf), ' ', '_');
			__entry->offset		= offset;
			__entry->bytes		= bytes;
			__entry->i_size		= i_size_read(inode);
			__assign_str(cmdline, command);
			(void)strreplace(__get_str(cmdline), ' ', '_');
			__entry->pid		= pid;
			__entry->ino		= inode->i_ino;
		}
	),
	TP_printk("entry_name %s, offset %llu, bytes %d, cmdline %s,"
		  " pid %d, i_size %llu, ino %lu",
		  __get_str(pathbuf), __entry->offset, __entry->bytes,
		  __get_str(cmdline), __entry->pid, __entry->i_size,
		  (unsigned long) __entry->ino)
);

DECLARE_EVENT_CLASS(android_fs_data_end_template,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes),
	TP_ARGS(inode, offset, bytes),
	TP_STRUCT__entry(
		__field(ino_t,	ino);
		__field(loff_t,	offset);
		__field(int,	bytes);
	),
	TP_fast_assign(
		{
			__entry->ino		= inode->i_ino;
			__entry->offset		= offset;
			__entry->bytes		= bytes;
		}
	),
	TP_printk("ino %lu, offset %llu, bytes %d",
		  (unsigned long) __entry->ino,
		  __entry->offset, __entry->bytes)
);

DECLARE_EVENT_CLASS(android_fs_fsync_start_template,
	TP_PROTO(struct inode *inode,
		 pid_t pid, char *pathname, char *command),
	TP_ARGS(inode, pid, pathname, command),
	TP_STRUCT__entry(
		__string(pathbuf, pathname);
		__field(loff_t,	i_size);
		__string(cmdline, command);
		__field(pid_t,	pid);
		__field(ino_t,	ino);
	),
	TP_fast_assign(
		{
			/*
			 * Replace the spaces in filenames and cmdlines
			 * because this screws up the tooling that parses
			 * the traces.
			 */
			__assign_str(pathbuf, pathname);
			(void)strreplace(__get_str(pathbuf), ' ', '_');
			__entry->i_size		= i_size_read(inode);
			__assign_str(cmdline, command);
			(void)strreplace(__get_str(cmdline), ' ', '_');
			__entry->pid		= pid;
			__entry->ino		= inode->i_ino;
		}
	),
	TP_printk("entry_name %s, cmdline %s,"
		  " pid %d, i_size %llu, ino %lu",
		  __get_str(pathbuf),
		  __get_str(cmdline), __entry->pid, __entry->i_size,
		  (unsigned long) __entry->ino)
);

#endif /* _TRACE_ANDROID_FS_TEMPLATE_H */
