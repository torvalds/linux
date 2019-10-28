/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM namei

#if !defined(_TRACE_INODEPATH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INODEPATH_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/device.h>
#include <linux/kdev_t.h>

TRACE_EVENT(inodepath,
		TP_PROTO(struct inode *inode, char *path),

		TP_ARGS(inode, path),

		TP_STRUCT__entry(
			/* dev_t and ino_t are arch dependent bit width
			 * so just use 64-bit
			 */
			__field(unsigned long, ino)
			__field(unsigned long, dev)
			__string(path, path)
		),

		TP_fast_assign(
			__entry->ino = inode->i_ino;
			__entry->dev = inode->i_sb->s_dev;
			__assign_str(path, path);
		),

		TP_printk("dev %d:%d ino=%lu path=%s",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			__entry->ino, __get_str(path))
);
#endif /* _TRACE_INODEPATH_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
