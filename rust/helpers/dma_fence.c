// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-fence.h>

__rust_helper void rust_helper_dma_fence_get(struct dma_fence *f)
{
	dma_fence_get(f);
}

__rust_helper void rust_helper_dma_fence_put(struct dma_fence *f)
{
	dma_fence_put(f);
}

__rust_helper bool rust_helper_dma_fence_begin_signalling(void)
{
	return dma_fence_begin_signalling();
}

__rust_helper void rust_helper_dma_fence_end_signalling(bool cookie)
{
	dma_fence_end_signalling(cookie);
}

__rust_helper bool rust_helper_dma_fence_is_signaled(struct dma_fence *f)
{
	return dma_fence_is_signaled(f);
}

__rust_helper bool rust_helper_dma_fence_test_signaled_flag(struct dma_fence *f)
{
	return dma_fence_test_signaled_flag(f);
}

__rust_helper void rust_helper_dma_fence_lock_irqsave(struct dma_fence *f, unsigned long *flags)
{
	dma_fence_lock_irqsave(f, *flags);
}

__rust_helper void rust_helper_dma_fence_unlock_irqrestore(struct dma_fence *f,
							   unsigned long *flags)
{
	dma_fence_unlock_irqrestore(f, *flags);
}

__rust_helper void rust_helper_dma_fence_set_error(struct dma_fence *f, int error)
{
	dma_fence_set_error(f, error);
}
