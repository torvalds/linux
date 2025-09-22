/* Public domain. */

#ifndef _LINUX_DMA_FENCE_ARRAY_H
#define _LINUX_DMA_FENCE_ARRAY_H

#include <linux/dma-fence.h>
#include <linux/irq_work.h>

struct dma_fence_array_cb {
	struct dma_fence_cb cb;
	struct dma_fence_array *array;
};

struct dma_fence_array {
	struct dma_fence base;
	unsigned int num_fences;
	struct dma_fence **fences;
	struct mutex lock;
	struct timeout to;
	int num_pending;
};

extern const struct dma_fence_ops dma_fence_array_ops;

static inline struct dma_fence_array *
to_dma_fence_array(struct dma_fence *fence)
{
	if (fence->ops != &dma_fence_array_ops)
		return NULL;

	return container_of(fence, struct dma_fence_array, base);
}

static inline bool
dma_fence_is_array(struct dma_fence *fence)
{
	return fence->ops == &dma_fence_array_ops;
}

struct dma_fence_array *dma_fence_array_create(int, struct dma_fence **,
    u64, unsigned, bool);
struct dma_fence *dma_fence_array_first(struct dma_fence *);
struct dma_fence *dma_fence_array_next(struct dma_fence *, unsigned int);

#define dma_fence_array_for_each(f, i, h)		\
	for (i = 0, f = dma_fence_array_first(h);	\
	     f != NULL;					\
	     ++(i), f = dma_fence_array_next(h, i))	\

#endif
