/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dma_buf

#if !defined(_TRACE_DMA_BUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DMA_BUF_H

#include <linux/dma-buf.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(dma_buf,

	TP_PROTO(struct dma_buf *dmabuf),

	TP_ARGS(dmabuf),

	TP_STRUCT__entry(
		__string(exp_name, dmabuf->exp_name)
		__field(size_t, size)
		__field(ino_t, ino)
	),

	TP_fast_assign(
		__assign_str(exp_name);
		__entry->size = dmabuf->size;
		__entry->ino = dmabuf->file->f_inode->i_ino;
	),

	TP_printk("exp_name=%s size=%zu ino=%lu",
		  __get_str(exp_name),
		  __entry->size,
		  __entry->ino)
);

DECLARE_EVENT_CLASS(dma_buf_attach_dev,

	TP_PROTO(struct dma_buf *dmabuf, struct dma_buf_attachment *attach,
		bool is_dynamic, struct device *dev),

	TP_ARGS(dmabuf, attach, is_dynamic, dev),

	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__string(exp_name, dmabuf->exp_name)
		__field(size_t, size)
		__field(ino_t, ino)
		__field(struct dma_buf_attachment *, attach)
		__field(bool, is_dynamic)
	),

	TP_fast_assign(
		__assign_str(dev_name);
		__assign_str(exp_name);
		__entry->size = dmabuf->size;
		__entry->ino = dmabuf->file->f_inode->i_ino;
		__entry->is_dynamic = is_dynamic;
		__entry->attach = attach;
	),

	TP_printk("exp_name=%s size=%zu ino=%lu attachment:%p is_dynamic=%d dev_name=%s",
		  __get_str(exp_name),
		  __entry->size,
		  __entry->ino,
		  __entry->attach,
		  __entry->is_dynamic,
		  __get_str(dev_name))
);

DECLARE_EVENT_CLASS(dma_buf_fd,

	TP_PROTO(struct dma_buf *dmabuf, int fd),

	TP_ARGS(dmabuf, fd),

	TP_STRUCT__entry(
		__string(exp_name, dmabuf->exp_name)
		__field(size_t, size)
		__field(ino_t, ino)
		__field(int, fd)
	),

	TP_fast_assign(
		__assign_str(exp_name);
		__entry->size = dmabuf->size;
		__entry->ino = dmabuf->file->f_inode->i_ino;
		__entry->fd = fd;
	),

	TP_printk("exp_name=%s size=%zu ino=%lu fd=%d",
		  __get_str(exp_name),
		  __entry->size,
		  __entry->ino,
		  __entry->fd)
);

DEFINE_EVENT(dma_buf, dma_buf_export,

	TP_PROTO(struct dma_buf *dmabuf),

	TP_ARGS(dmabuf)
);

DEFINE_EVENT(dma_buf, dma_buf_mmap_internal,

	TP_PROTO(struct dma_buf *dmabuf),

	TP_ARGS(dmabuf)
);

DEFINE_EVENT(dma_buf, dma_buf_mmap,

	TP_PROTO(struct dma_buf *dmabuf),

	TP_ARGS(dmabuf)
);

DEFINE_EVENT(dma_buf, dma_buf_put,

	TP_PROTO(struct dma_buf *dmabuf),

	TP_ARGS(dmabuf)
);

DEFINE_EVENT(dma_buf_attach_dev, dma_buf_dynamic_attach,

	TP_PROTO(struct dma_buf *dmabuf, struct dma_buf_attachment *attach,
		bool is_dynamic, struct device *dev),

	TP_ARGS(dmabuf, attach, is_dynamic, dev)
);

DEFINE_EVENT(dma_buf_attach_dev, dma_buf_detach,

	TP_PROTO(struct dma_buf *dmabuf, struct dma_buf_attachment *attach,
		bool is_dynamic, struct device *dev),

	TP_ARGS(dmabuf, attach, is_dynamic, dev)
);

DEFINE_EVENT(dma_buf_fd, dma_buf_fd,

	TP_PROTO(struct dma_buf *dmabuf, int fd),

	TP_ARGS(dmabuf, fd)
);

DEFINE_EVENT(dma_buf_fd, dma_buf_get,

	TP_PROTO(struct dma_buf *dmabuf, int fd),

	TP_ARGS(dmabuf, fd)
);

#endif /* _TRACE_DMA_BUF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
