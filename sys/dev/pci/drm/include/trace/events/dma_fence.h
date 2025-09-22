/* Public domain. */

#ifndef _TRACE_EVENTS_DMA_FENCE_H
#define _TRACE_EVENTS_DMA_FENCE_H

struct dma_fence;

static inline void
trace_dma_fence_destroy(struct dma_fence *f)
{
}

static inline void
trace_dma_fence_emit(struct dma_fence *f)
{
}

static inline void
trace_dma_fence_enable_signal(struct dma_fence *f)
{
}

static inline void
trace_dma_fence_init(struct dma_fence *f)
{
}

static inline void
trace_dma_fence_signaled(struct dma_fence *f)
{
}

static inline void
trace_dma_fence_wait_end(struct dma_fence *f)
{
}

static inline void
trace_dma_fence_wait_start(struct dma_fence *f)
{
}

#endif
