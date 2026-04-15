// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-resv.h>

__rust_helper
int rust_helper_dma_resv_lock(struct dma_resv *obj, struct ww_acquire_ctx *ctx)
{
	return dma_resv_lock(obj, ctx);
}

__rust_helper void rust_helper_dma_resv_unlock(struct dma_resv *obj)
{
	dma_resv_unlock(obj);
}
