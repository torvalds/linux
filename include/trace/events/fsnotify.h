/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fsnotify

#if !defined(_TRACE_FSNOTIFY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FSNOTIFY_H

#include <linux/tracepoint.h>

#include <trace/misc/fsnotify.h>

TRACE_EVENT(fsnotify,
	TP_PROTO(__u32 mask, const void *data, int data_type,
		 struct inode *dir, const struct qstr *file_name,
		 struct inode *inode, u32 cookie),

	TP_ARGS(mask, data, data_type, dir, file_name, inode, cookie),

	TP_STRUCT__entry(
		__field(__u32, mask)
		__field(unsigned long, dir_ino)
		__field(unsigned long, ino)
		__field(dev_t, s_dev)
		__field(int, data_type)
		__field(u32, cookie)
		__string(file_name, file_name ? (const char *)file_name->name : "")
	),

	TP_fast_assign(
		__entry->mask = mask;
		__entry->dir_ino = dir ? dir->i_ino : 0;
		__entry->ino = inode ? inode->i_ino : 0;
		__entry->s_dev = dir ? dir->i_sb->s_dev :
				 inode ? inode->i_sb->s_dev : 0;
		__entry->data_type = data_type;
		__entry->cookie = cookie;
		__assign_str(file_name);
	),

	TP_printk("dev=%d:%d dir=%lu ino=%lu data_type=%d cookie=0x%x mask=0x%x %s name=%s",
		  MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		  __entry->dir_ino, __entry->ino,
		  __entry->data_type, __entry->cookie,
		  __entry->mask, show_fsnotify_mask(__entry->mask),
		  __get_str(file_name))
);

#endif /* _TRACE_FSNOTIFY_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
