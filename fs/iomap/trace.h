/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2009-2021 Christoph Hellwig
 *
 * NOTE: none of these tracepoints shall be considered a stable kernel ABI
 * as they can change at any time.
 *
 * Current conventions for printing numbers measuring specific units:
 *
 * offset: byte offset into a subcomponent of a file operation
 * pos: file offset, in bytes
 * length: length of a file operation, in bytes
 * ino: inode number
 *
 * Numbers describing space allocations should be formatted in hexadecimal.
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
	TP_PROTO(struct inode *inode, loff_t off, u64 len),
	TP_ARGS(inode, off, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, ino)
		__field(loff_t, size)
		__field(loff_t, offset)
		__field(u64, length)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->size = i_size_read(inode);
		__entry->offset = off;
		__entry->length = len;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx offset 0x%llx length 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->offset,
		  __entry->length)
)

#define DEFINE_RANGE_EVENT(name)		\
DEFINE_EVENT(iomap_range_class, name,	\
	TP_PROTO(struct inode *inode, loff_t off, u64 len),\
	TP_ARGS(inode, off, len))
DEFINE_RANGE_EVENT(iomap_writepage);
DEFINE_RANGE_EVENT(iomap_release_folio);
DEFINE_RANGE_EVENT(iomap_invalidate_folio);
DEFINE_RANGE_EVENT(iomap_dio_invalidate_fail);
DEFINE_RANGE_EVENT(iomap_dio_rw_queued);

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
	{ IOMAP_NOWAIT,		"NOWAIT" }, \
	{ IOMAP_OVERWRITE_ONLY,	"OVERWRITE_ONLY" }, \
	{ IOMAP_UNSHARE,	"UNSHARE" }, \
	{ IOMAP_DAX,		"DAX" }, \
	{ IOMAP_ATOMIC,		"ATOMIC" }, \
	{ IOMAP_DONTCACHE,	"DONTCACHE" }

#define IOMAP_F_FLAGS_STRINGS \
	{ IOMAP_F_NEW,		"NEW" }, \
	{ IOMAP_F_DIRTY,	"DIRTY" }, \
	{ IOMAP_F_SHARED,	"SHARED" }, \
	{ IOMAP_F_MERGED,	"MERGED" }, \
	{ IOMAP_F_BUFFER_HEAD,	"BH" }, \
	{ IOMAP_F_XATTR,	"XATTR" }, \
	{ IOMAP_F_BOUNDARY,	"BOUNDARY" }, \
	{ IOMAP_F_ANON_WRITE,	"ANON_WRITE" }, \
	{ IOMAP_F_ATOMIC_BIO,	"ATOMIC_BIO" }, \
	{ IOMAP_F_PRIVATE,	"PRIVATE" }, \
	{ IOMAP_F_SIZE_CHANGED,	"SIZE_CHANGED" }, \
	{ IOMAP_F_STALE,	"STALE" }


#define IOMAP_DIO_STRINGS \
	{IOMAP_DIO_FORCE_WAIT,	"DIO_FORCE_WAIT" }, \
	{IOMAP_DIO_OVERWRITE_ONLY, "DIO_OVERWRITE_ONLY" }, \
	{IOMAP_DIO_PARTIAL,	"DIO_PARTIAL" }

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
	TP_printk("dev %d:%d ino 0x%llx bdev %d:%d addr 0x%llx offset 0x%llx "
		  "length 0x%llx type %s (0x%x) flags %s (0x%x)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  MAJOR(__entry->bdev), MINOR(__entry->bdev),
		  __entry->addr,
		  __entry->offset,
		  __entry->length,
		  __print_symbolic(__entry->type, IOMAP_TYPE_STRINGS),
		  __entry->type,
		  __print_flags(__entry->flags, "|", IOMAP_F_FLAGS_STRINGS),
		  __entry->flags)
)

#define DEFINE_IOMAP_EVENT(name)		\
DEFINE_EVENT(iomap_class, name,	\
	TP_PROTO(struct inode *inode, struct iomap *iomap), \
	TP_ARGS(inode, iomap))
DEFINE_IOMAP_EVENT(iomap_iter_dstmap);
DEFINE_IOMAP_EVENT(iomap_iter_srcmap);

TRACE_EVENT(iomap_writepage_map,
	TP_PROTO(struct inode *inode, u64 pos, unsigned int dirty_len,
		 struct iomap *iomap),
	TP_ARGS(inode, pos, dirty_len, iomap),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, ino)
		__field(u64, pos)
		__field(u64, dirty_len)
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
		__entry->pos = pos;
		__entry->dirty_len = dirty_len;
		__entry->addr = iomap->addr;
		__entry->offset = iomap->offset;
		__entry->length = iomap->length;
		__entry->type = iomap->type;
		__entry->flags = iomap->flags;
		__entry->bdev = iomap->bdev ? iomap->bdev->bd_dev : 0;
	),
	TP_printk("dev %d:%d ino 0x%llx bdev %d:%d pos 0x%llx dirty len 0x%llx "
		  "addr 0x%llx offset 0x%llx length 0x%llx type %s (0x%x) flags %s (0x%x)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  MAJOR(__entry->bdev), MINOR(__entry->bdev),
		  __entry->pos,
		  __entry->dirty_len,
		  __entry->addr,
		  __entry->offset,
		  __entry->length,
		  __print_symbolic(__entry->type, IOMAP_TYPE_STRINGS),
		  __entry->type,
		  __print_flags(__entry->flags, "|", IOMAP_F_FLAGS_STRINGS),
		  __entry->flags)
);

TRACE_EVENT(iomap_iter,
	TP_PROTO(struct iomap_iter *iter, const void *ops,
		 unsigned long caller),
	TP_ARGS(iter, ops, caller),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, ino)
		__field(loff_t, pos)
		__field(u64, length)
		__field(int, status)
		__field(unsigned int, flags)
		__field(const void *, ops)
		__field(unsigned long, caller)
	),
	TP_fast_assign(
		__entry->dev = iter->inode->i_sb->s_dev;
		__entry->ino = iter->inode->i_ino;
		__entry->pos = iter->pos;
		__entry->length = iomap_length(iter);
		__entry->status = iter->status;
		__entry->flags = iter->flags;
		__entry->ops = ops;
		__entry->caller = caller;
	),
	TP_printk("dev %d:%d ino 0x%llx pos 0x%llx length 0x%llx status %d flags %s (0x%x) ops %ps caller %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->ino,
		   __entry->pos,
		   __entry->length,
		   __entry->status,
		   __print_flags(__entry->flags, "|", IOMAP_FLAGS_STRINGS),
		   __entry->flags,
		   __entry->ops,
		   (void *)__entry->caller)
);

TRACE_EVENT(iomap_dio_rw_begin,
	TP_PROTO(struct kiocb *iocb, struct iov_iter *iter,
		 unsigned int dio_flags, size_t done_before),
	TP_ARGS(iocb, iter, dio_flags, done_before),
	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t, isize)
		__field(loff_t, pos)
		__field(size_t,	count)
		__field(size_t,	done_before)
		__field(int,	ki_flags)
		__field(unsigned int,	dio_flags)
		__field(bool,	aio)
	),
	TP_fast_assign(
		__entry->dev = file_inode(iocb->ki_filp)->i_sb->s_dev;
		__entry->ino = file_inode(iocb->ki_filp)->i_ino;
		__entry->isize = file_inode(iocb->ki_filp)->i_size;
		__entry->pos = iocb->ki_pos;
		__entry->count = iov_iter_count(iter);
		__entry->done_before = done_before;
		__entry->ki_flags = iocb->ki_flags;
		__entry->dio_flags = dio_flags;
		__entry->aio = !is_sync_kiocb(iocb);
	),
	TP_printk("dev %d:%d ino 0x%lx size 0x%llx offset 0x%llx length 0x%zx done_before 0x%zx flags %s dio_flags %s aio %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->isize,
		  __entry->pos,
		  __entry->count,
		  __entry->done_before,
		  __print_flags(__entry->ki_flags, "|", TRACE_IOCB_STRINGS),
		  __print_flags(__entry->dio_flags, "|", IOMAP_DIO_STRINGS),
		  __entry->aio)
);

TRACE_EVENT(iomap_dio_complete,
	TP_PROTO(struct kiocb *iocb, int error, ssize_t ret),
	TP_ARGS(iocb, error, ret),
	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t, isize)
		__field(loff_t, pos)
		__field(int,	ki_flags)
		__field(bool,	aio)
		__field(int,	error)
		__field(ssize_t, ret)
	),
	TP_fast_assign(
		__entry->dev = file_inode(iocb->ki_filp)->i_sb->s_dev;
		__entry->ino = file_inode(iocb->ki_filp)->i_ino;
		__entry->isize = file_inode(iocb->ki_filp)->i_size;
		__entry->pos = iocb->ki_pos;
		__entry->ki_flags = iocb->ki_flags;
		__entry->aio = !is_sync_kiocb(iocb);
		__entry->error = error;
		__entry->ret = ret;
	),
	TP_printk("dev %d:%d ino 0x%lx size 0x%llx offset 0x%llx flags %s aio %d error %d ret %zd",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->isize,
		  __entry->pos,
		  __print_flags(__entry->ki_flags, "|", TRACE_IOCB_STRINGS),
		  __entry->aio,
		  __entry->error,
		  __entry->ret)
);

#endif /* _IOMAP_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
