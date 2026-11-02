// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_DMA_BUF_H
#define IOU_DMA_BUF_H

#include <linux/dma-buf-io.h>
#include <linux/io_uring_types.h>

#include "rsrc.h"

struct io_buf_dma {
	struct dma_buf_io_ctx		*ctx;
	struct file			*target_file;
};

int __io_register_dmabuf(struct io_ring_ctx *ctx,
			struct io_uring_regbuf_desc *desc,
			struct io_mapped_ubuf *imu);

static inline void io_detach_dmabuf_map(struct io_kiocb *req)
{
	if (!IS_ENABLED(CONFIG_DMA_SHARED_BUFFER))
		return;
	if (!(req->flags & REQ_F_DMABUF) || !req->dmabuf_map)
		return;
	dma_buf_io_map_drop(req->dmabuf_map);
	req->dmabuf_map = NULL;
}

static inline int io_attach_dmabuf_map(struct io_kiocb *req,
					struct iov_iter *iter,
					unsigned issue_flags)
{
	bool nowait = issue_flags & IO_URING_F_NONBLOCK;
	struct dma_buf_io_map *map;
	struct io_buf_dma *bd;

	if (!(req->flags & REQ_F_DMABUF))
		return 0;
	if (!IS_ENABLED(CONFIG_DMA_SHARED_BUFFER))
		return -EOPNOTSUPP;

	bd = req->buf_node->buf->priv;
	map = dma_buf_io_get_map(bd->ctx, nowait);
	if (unlikely(IS_ERR(map)))
		return PTR_ERR(map);
	req->dmabuf_map = map;
	iter->dmabuf_map = map;
	return 0;
}

static inline int io_import_dmabuf_map(struct io_kiocb *req,
				int ddir, struct iov_iter *iter,
				struct io_mapped_ubuf *imu,
				size_t len, size_t offset)
{
	struct io_buf_dma *bd = imu->priv;

	if (!IS_ENABLED(CONFIG_DMA_SHARED_BUFFER))
		return -EOPNOTSUPP;
	if (req->file != bd->target_file)
		return -EBADF;
	req->flags |= REQ_F_DMABUF;
	iov_iter_dmabuf_map(iter, ddir, NULL, offset, len);
	return 0;
}

static inline int io_register_dmabuf(struct io_ring_ctx *ctx,
			struct io_uring_regbuf_desc *desc,
			struct io_mapped_ubuf *imu)
{
	if (!IS_ENABLED(CONFIG_DMA_SHARED_BUFFER))
		return -EOPNOTSUPP;
	return __io_register_dmabuf(ctx, desc, imu);
}

#endif /* IOU_DMA_BUF_H */
