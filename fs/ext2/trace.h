// SPDX-License-Identifier: GPL-2.0

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ext2

#if !defined(_EXT2_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _EXT2_TRACE_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(ext2_dio_class,
	TP_PROTO(struct kiocb *iocb, struct iov_iter *iter, ssize_t ret),
	TP_ARGS(iocb, iter, ret),
	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t, isize)
		__field(loff_t, pos)
		__field(size_t,	count)
		__field(int,	ki_flags)
		__field(bool,	aio)
		__field(ssize_t, ret)
	),
	TP_fast_assign(
		__entry->dev = file_inode(iocb->ki_filp)->i_sb->s_dev;
		__entry->ino = file_inode(iocb->ki_filp)->i_ino;
		__entry->isize = file_inode(iocb->ki_filp)->i_size;
		__entry->pos = iocb->ki_pos;
		__entry->count = iov_iter_count(iter);
		__entry->ki_flags = iocb->ki_flags;
		__entry->aio = !is_sync_kiocb(iocb);
		__entry->ret = ret;
	),
	TP_printk("dev %d:%d ino 0x%lx isize 0x%llx pos 0x%llx len %zu flags %s aio %d ret %zd",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->isize,
		  __entry->pos,
		  __entry->count,
		  __print_flags(__entry->ki_flags, "|", TRACE_IOCB_STRINGS),
		  __entry->aio,
		  __entry->ret)
);

#define DEFINE_DIO_RW_EVENT(name)					  \
DEFINE_EVENT(ext2_dio_class, name,					  \
	TP_PROTO(struct kiocb *iocb, struct iov_iter *iter, ssize_t ret), \
	TP_ARGS(iocb, iter, ret))
DEFINE_DIO_RW_EVENT(ext2_dio_write_begin);
DEFINE_DIO_RW_EVENT(ext2_dio_write_end);
DEFINE_DIO_RW_EVENT(ext2_dio_write_buff_end);
DEFINE_DIO_RW_EVENT(ext2_dio_read_begin);
DEFINE_DIO_RW_EVENT(ext2_dio_read_end);

TRACE_EVENT(ext2_dio_write_endio,
	TP_PROTO(struct kiocb *iocb, ssize_t size, int ret),
	TP_ARGS(iocb, size, ret),
	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t, isize)
		__field(loff_t, pos)
		__field(ssize_t, size)
		__field(int,	ki_flags)
		__field(bool,	aio)
		__field(int,	ret)
	),
	TP_fast_assign(
		__entry->dev = file_inode(iocb->ki_filp)->i_sb->s_dev;
		__entry->ino = file_inode(iocb->ki_filp)->i_ino;
		__entry->isize = file_inode(iocb->ki_filp)->i_size;
		__entry->pos = iocb->ki_pos;
		__entry->size = size;
		__entry->ki_flags = iocb->ki_flags;
		__entry->aio = !is_sync_kiocb(iocb);
		__entry->ret = ret;
	),
	TP_printk("dev %d:%d ino 0x%lx isize 0x%llx pos 0x%llx len %zd flags %s aio %d ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->isize,
		  __entry->pos,
		  __entry->size,
		  __print_flags(__entry->ki_flags, "|", TRACE_IOCB_STRINGS),
		  __entry->aio,
		  __entry->ret)
);

#endif /* _EXT2_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
