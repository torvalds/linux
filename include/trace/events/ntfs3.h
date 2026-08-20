/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ntfs3

#if !defined(_TRACE_NTFS3_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NTFS3_H

#include <linux/fs.h>
#include <linux/tracepoint.h>

TRACE_EVENT(ntfs3_fill_super,
	TP_PROTO(struct super_block *sb, bool ro, u32 cluster_size,
		 u32 record_size, u32 index_size, int err),
	TP_ARGS(sb, ro, cluster_size, record_size, index_size, err),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u32, cluster_size)
		__field(u32, record_size)
		__field(u32, index_size)
		__field(int, err)
		__field(bool, ro)
	),
	TP_fast_assign(
		__entry->dev = sb->s_bdev->bd_dev;
		__entry->cluster_size = cluster_size;
		__entry->record_size = record_size;
		__entry->index_size = index_size;
		__entry->err = err;
		__entry->ro = ro;
	),
	TP_printk("dev=(%d,%d) ro=%d cluster=%u record=%u index=%u err=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ro,
		  __entry->cluster_size, __entry->record_size,
		  __entry->index_size, __entry->err)
);

TRACE_EVENT(ntfs3_init_from_boot,
	TP_PROTO(struct super_block *sb, u32 media_sector_size,
		 u32 boot_sector_size, bool used_alt_boot, int err),
	TP_ARGS(sb, media_sector_size, boot_sector_size, used_alt_boot, err),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u32, media_sector_size)
		__field(u32, boot_sector_size)
		__field(int, err)
		__field(bool, used_alt_boot)
	),
	TP_fast_assign(
		__entry->dev = sb->s_bdev->bd_dev;
		__entry->media_sector_size = media_sector_size;
		__entry->boot_sector_size = boot_sector_size;
		__entry->err = err;
		__entry->used_alt_boot = used_alt_boot;
	),
	TP_printk("dev=(%d,%d) media_sector=%u boot_sector=%u alt_boot=%d err=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->media_sector_size, __entry->boot_sector_size,
		  __entry->used_alt_boot, __entry->err)
);

TRACE_EVENT(ntfs3_log_replay,
	TP_PROTO(struct inode *inode, bool initialized, int err),
	TP_ARGS(inode, initialized, err),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(loff_t, size)
		__field(dev_t, dev)
		__field(int, err)
		__field(bool, initialized)
	),
	TP_fast_assign(
		__entry->ino = inode->i_ino;
		__entry->size = i_size_read(inode);
		__entry->dev = inode->i_sb->s_dev;
		__entry->err = err;
		__entry->initialized = initialized;
	),
	TP_printk("dev=(%d,%d) ino=%lu size=%lld initialized=%d err=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		  __entry->size, __entry->initialized, __entry->err)
);

#endif /* _TRACE_NTFS3_H */

#include <trace/define_trace.h>
