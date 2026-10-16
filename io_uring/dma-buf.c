#include "dma-buf.h"

static inline void io_release_dmabuf(void *priv)
{
	struct io_buf_dma *bd = priv;

	fput(bd->target_file);
	dma_buf_io_ctx_release(bd->ctx);
	kfree(bd);
}

int __io_register_dmabuf(struct io_ring_ctx *ctx,
			struct io_uring_regbuf_desc *desc,
			struct io_mapped_ubuf *imu)
{
	struct io_buf_dma *bd = NULL;
	struct file *target_file = NULL;
	struct dma_buf *dmabuf = NULL;
	int ret;

	/*
	 * We have to drop the dma-buf map early when IO is finished. Disable
	 * IOPOLL for now as we can't wait potentially infintely for a task
	 * to do that.
	 */
	if (ctx->flags & IORING_SETUP_IOPOLL)
		return -EOPNOTSUPP;
	if (desc->uaddr || desc->size)
		return -EINVAL;

	bd = kzalloc(sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	target_file = fget(desc->target_fd);
	if (!target_file) {
		ret = -EBADF;
		goto err;
	}
	dmabuf = dma_buf_get(desc->dmabuf_fd);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		dmabuf = NULL;
		goto err;
	}
	ret = io_validate_user_buf_range(0, dmabuf->size);
	if (ret)
		goto err;

	ret = dma_buf_io_ctx_create(target_file, dmabuf, DMA_BIDIRECTIONAL,
				    &bd->ctx);
	if (ret)
		goto err;

	bd->target_file = target_file;
	imu->len = dmabuf->size;
	imu->release = io_release_dmabuf;
	imu->priv = bd;
	imu->flags = IO_REGBUF_F_DMABUF;
	imu->dir = IO_BUF_DEST | IO_BUF_SOURCE;
	dma_buf_put(dmabuf);
	return 0;
err:
	kfree(bd);
	if (target_file)
		fput(target_file);
	if (dmabuf)
		dma_buf_put(dmabuf);
	return ret;
}

