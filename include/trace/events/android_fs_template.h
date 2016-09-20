#if !defined(_TRACE_ANDROID_FS_TEMPLATE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ANDROID_FS_TEMPLATE_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(android_fs_data_start_template,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes,
		 pid_t pid, char *command),
	TP_ARGS(inode, offset, bytes, pid, command),
	TP_STRUCT__entry(
		__array(char, path, MAX_FILTER_STR_VAL);
		__field(char *, pathname);
		__field(loff_t,	offset);
		__field(int,	bytes);
		__field(loff_t,	i_size);
		__string(cmdline, command);
		__field(pid_t,	pid);
		__field(ino_t,	ino);
	),
	TP_fast_assign(
		{
			struct dentry *d;

			/*
			 * Grab a reference to the inode here because
			 * d_obtain_alias() will either drop the inode
			 * reference if it locates an existing dentry
			 * or transfer the reference to the new dentry
			 * created. In our case, the file is still open,
			 * so the dentry is guaranteed to exist (connected),
			 * so d_obtain_alias() drops the reference we
			 * grabbed here.
			 */
			ihold(inode);
			d = d_obtain_alias(inode);
			if (!IS_ERR(d)) {
				__entry->pathname = dentry_path(d,
							__entry->path,
							MAX_FILTER_STR_VAL);
				dput(d);
			} else
				__entry->pathname = ERR_PTR(-EINVAL);
			__entry->offset		= offset;
			__entry->bytes		= bytes;
			__entry->i_size		= i_size_read(inode);
			__assign_str(cmdline, command);
			__entry->pid		= pid;
			__entry->ino		= inode->i_ino;
		}
	),
	TP_printk("entry_name %s, offset %llu, bytes %d, cmdline %s,"
		  " pid %d, i_size %llu, ino %lu",
		  (IS_ERR(__entry->pathname) ? "ERROR" : __entry->pathname),
		  __entry->offset, __entry->bytes, __get_str(cmdline),
		  __entry->pid, __entry->i_size,
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

#endif /* _TRACE_ANDROID_FS_TEMPLATE_H */
