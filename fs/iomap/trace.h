/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2009-2019 Christoph Hellwig
 *
 * NOTE: none of these tracepoints shall be consider a stable kernel ABI
 * as they can change at any time.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM iomap

#if !defined(_IOMAP_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _IOMAP_TRACE_H

#include <linux/tracepoint.h>

struct inode;

DECLARE_EVENT_CLASS(iomap_readpage_class,
	TP_PROTO(struct inode *inode, int nr_pages),
	TP_ARGS(inode, nr_pages),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, ino)
		__field(int, nr_pages)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->nr_pages = nr_pages;
	),
	TP_printk("dev %d:%d ino 0x%llx nr_pages %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->nr_pages)
)

#define DEFINE_READPAGE_EVENT(name)		\
DEFINE_EVENT(iomap_readpage_class, name,	\
	TP_PROTO(struct inode *inode, int nr_pages), \
	TP_ARGS(inode, nr_pages))
DEFINE_READPAGE_EVENT(iomap_readpage);
DEFINE_READPAGE_EVENT(iomap_readahead);

DECLARE_EVENT_CLASS(iomap_range_class,
	TP_PROTO(struct inode *inode, unsigned long off, unsigned int len),
	TP_ARGS(inode, off, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, ino)
		__field(loff_t, size)
		__field(unsigned long, offset)
		__field(unsigned int, length)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->size = i_size_read(inode);
		__entry->offset = off;
		__entry->length = len;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx offset %lx "
		  "length %x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->offset,
		  __entry->length)
)

#define DEFINE_RANGE_EVENT(name)		\
DEFINE_EVENT(iomap_range_class, name,	\
	TP_PROTO(struct inode *inode, unsigned long off, unsigned int len),\
	TP_ARGS(inode, off, len))
DEFINE_RANGE_EVENT(iomap_writepage);
DEFINE_RANGE_EVENT(iomap_releasepage);
DEFINE_RANGE_EVENT(iomap_invalidatepage);
DEFINE_RANGE_EVENT(iomap_dio_invalidate_fail);

#define IOMAP_TYPE_STRINGS \
	{ IOMAP_HOLE,		"HOLE" }, \
	{ IOMAP_DELALLOC,	"DELALLOC" }, \
	{ IOMAP_MAPPED,		"MAPPED" }, \
	{ IOMAP_UNWRITTEN,	"UNWRITTEN" }, \
	{ IOMAP_INLINE,		"INLINE" }

#define IOMAP_FLAGS_STRINGS \
	{ IOMAP_WRITE,		"WRITE" }, \
	{ IOMAP_ZERO,		"ZERO" }, \
	{ IOMAP_REPORT,		"REPORT" }, \
	{ IOMAP_FAULT,		"FAULT" }, \
	{ IOMAP_DIRECT,		"DIRECT" }, \
	{ IOMAP_NOWAIT,		"NOWAIT" }

#define IOMAP_F_FLAGS_STRINGS \
	{ IOMAP_F_NEW,		"NEW" }, \
	{ IOMAP_F_DIRTY,	"DIRTY" }, \
	{ IOMAP_F_SHARED,	"SHARED" }, \
	{ IOMAP_F_MERGED,	"MERGED" }, \
	{ IOMAP_F_BUFFER_HEAD,	"BH" }, \
	{ IOMAP_F_SIZE_CHANGED,	"SIZE_CHANGED" }

DECLARE_EVENT_CLASS(iomap_class,
	TP_PROTO(struct inode *inode, struct iomap *iomap),
	TP_ARGS(inode, iomap),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, ino)
		__field(u64, addr)
		__field(loff_t, offset)
		__field(u64, length)
		__field(u16, type)
		__field(u16, flags)
		__field(dev_t, bdev)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->addr = iomap->addr;
		__entry->offset = iomap->offset;
		__entry->length = iomap->length;
		__entry->type = iomap->type;
		__entry->flags = iomap->flags;
		__entry->bdev = iomap->bdev ? iomap->bdev->bd_dev : 0;
	),
	TP_printk("dev %d:%d ino 0x%llx bdev %d:%d addr %lld offset %lld "
		  "length %llu type %s flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  MAJOR(__entry->bdev), MINOR(__entry->bdev),
		  __entry->addr,
		  __entry->offset,
		  __entry->length,
		  __print_symbolic(__entry->type, IOMAP_TYPE_STRINGS),
		  __print_flags(__entry->flags, "|", IOMAP_F_FLAGS_STRINGS))
)

#define DEFINE_IOMAP_EVENT(name)		\
DEFINE_EVENT(iomap_class, name,	\
	TP_PROTO(struct inode *inode, struct iomap *iomap), \
	TP_ARGS(inode, iomap))
DEFINE_IOMAP_EVENT(iomap_apply_dstmap);
DEFINE_IOMAP_EVENT(iomap_apply_srcmap);

TRACE_EVENT(iomap_apply,
	TP_PROTO(struct inode *inode, loff_t pos, loff_t length,
		unsigned int flags, const void *ops, void *actor,
		unsigned long caller),
	TP_ARGS(inode, pos, length, flags, ops, actor, caller),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, ino)
		__field(loff_t, pos)
		__field(loff_t, length)
		__field(unsigned int, flags)
		__field(const void *, ops)
		__field(void *, actor)
		__field(unsigned long, caller)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->pos = pos;
		__entry->length = length;
		__entry->flags = flags;
		__entry->ops = ops;
		__entry->actor = actor;
		__entry->caller = caller;
	),
	TP_printk("dev %d:%d ino 0x%llx pos %lld length %lld flags %s (0x%x) "
		  "ops %ps caller %pS actor %ps",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->ino,
		   __entry->pos,
		   __entry->length,
		   __print_flags(__entry->flags, "|", IOMAP_FLAGS_STRINGS),
		   __entry->flags,
		   __entry->ops,
		   (void *)__entry->caller,
		   __entry->actor)
);

#endif /* _IOMAP_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
