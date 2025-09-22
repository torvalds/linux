// SPDX-License-Identifier: MIT

/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_sw_fence_work.h"

static void fence_complete(struct dma_fence_work *f)
{
	if (f->ops->release)
		f->ops->release(f);
	dma_fence_signal(&f->dma);
}

static void fence_work(struct work_struct *work)
{
	struct dma_fence_work *f = container_of(work, typeof(*f), work);

	f->ops->work(f);

	fence_complete(f);
	dma_fence_put(&f->dma);
}

static int
fence_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	struct dma_fence_work *f = container_of(fence, typeof(*f), chain);

	switch (state) {
	case FENCE_COMPLETE:
		if (fence->error)
			dma_fence_set_error(&f->dma, fence->error);

		if (!f->dma.error) {
			dma_fence_get(&f->dma);
			if (test_bit(DMA_FENCE_WORK_IMM, &f->dma.flags))
				fence_work(&f->work);
			else
				queue_work(system_unbound_wq, &f->work);
		} else {
			fence_complete(f);
		}
		break;

	case FENCE_FREE:
		dma_fence_put(&f->dma);
		break;
	}

	return NOTIFY_DONE;
}

static const char *get_driver_name(struct dma_fence *fence)
{
	return "dma-fence";
}

static const char *get_timeline_name(struct dma_fence *fence)
{
	struct dma_fence_work *f = container_of(fence, typeof(*f), dma);

	return f->ops->name ?: "work";
}

static void fence_release(struct dma_fence *fence)
{
	struct dma_fence_work *f = container_of(fence, typeof(*f), dma);

	i915_sw_fence_fini(&f->chain);

	BUILD_BUG_ON(offsetof(typeof(*f), dma));
	dma_fence_free(&f->dma);
}

static const struct dma_fence_ops fence_ops = {
	.get_driver_name = get_driver_name,
	.get_timeline_name = get_timeline_name,
	.release = fence_release,
};

void dma_fence_work_init(struct dma_fence_work *f,
			 const struct dma_fence_work_ops *ops)
{
	f->ops = ops;
	mtx_init(&f->lock, IPL_TTY);
	dma_fence_init(&f->dma, &fence_ops, &f->lock, 0, 0);
	i915_sw_fence_init(&f->chain, fence_notify);
	INIT_WORK(&f->work, fence_work);
}

int dma_fence_work_chain(struct dma_fence_work *f, struct dma_fence *signal)
{
	if (!signal)
		return 0;

	return __i915_sw_fence_await_dma_fence(&f->chain, signal, &f->cb);
}
