/* Public domain. */

#ifndef _LINUX_DMA_FENCE_H
#define _LINUX_DMA_FENCE_H

#include <sys/types.h>
#include <sys/mutex.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>

#define DMA_FENCE_TRACE(fence, fmt, args...) do {} while(0)

struct dma_fence {
	struct kref refcount;
	const struct dma_fence_ops *ops;
	unsigned long flags;
	uint64_t context;
	uint64_t seqno;
	struct mutex *lock;
	union {
		struct list_head cb_list;
		ktime_t timestamp;
		struct rcu_head rcu;
	};
	int error;
};

enum dma_fence_flag_bits {
	DMA_FENCE_FLAG_SIGNALED_BIT,
	DMA_FENCE_FLAG_TIMESTAMP_BIT,
	DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
	DMA_FENCE_FLAG_USER_BITS,
};

struct dma_fence_ops {
	const char * (*get_driver_name)(struct dma_fence *);
	const char * (*get_timeline_name)(struct dma_fence *);
	bool (*enable_signaling)(struct dma_fence *);
	bool (*signaled)(struct dma_fence *);
	long (*wait)(struct dma_fence *, bool, long);
	void (*release)(struct dma_fence *);
	void (*set_deadline)(struct dma_fence *, ktime_t);
	bool use_64bit_seqno;
};

struct dma_fence_cb;
typedef void (*dma_fence_func_t)(struct dma_fence *fence, struct dma_fence_cb *cb);

struct dma_fence_cb {
	struct list_head node;
	dma_fence_func_t func;
};

uint64_t dma_fence_context_alloc(unsigned int);
struct dma_fence *dma_fence_get(struct dma_fence *);
struct dma_fence *dma_fence_get_rcu(struct dma_fence *);
struct dma_fence *dma_fence_get_rcu_safe(struct dma_fence **);
void dma_fence_release(struct kref *);
void dma_fence_put(struct dma_fence *);
int dma_fence_signal(struct dma_fence *);
int dma_fence_signal_locked(struct dma_fence *);
int dma_fence_signal_timestamp(struct dma_fence *, ktime_t);
int dma_fence_signal_timestamp_locked(struct dma_fence *, ktime_t);
bool dma_fence_is_signaled(struct dma_fence *);
bool dma_fence_is_signaled_locked(struct dma_fence *);
ktime_t dma_fence_timestamp(struct dma_fence *);
long dma_fence_default_wait(struct dma_fence *, bool, long);
long dma_fence_wait_any_timeout(struct dma_fence **, uint32_t, bool, long,
    uint32_t *);
long dma_fence_wait_timeout(struct dma_fence *, bool, long);
long dma_fence_wait(struct dma_fence *, bool);
void dma_fence_enable_sw_signaling(struct dma_fence *);
void dma_fence_init(struct dma_fence *, const struct dma_fence_ops *,
    struct mutex *, uint64_t, uint64_t);
int dma_fence_add_callback(struct dma_fence *, struct dma_fence_cb *,
    dma_fence_func_t);
bool dma_fence_remove_callback(struct dma_fence *, struct dma_fence_cb *);
bool dma_fence_is_container(struct dma_fence *);
void dma_fence_set_deadline(struct dma_fence *, ktime_t);

struct dma_fence *dma_fence_get_stub(void);
struct dma_fence *dma_fence_allocate_private_stub(ktime_t);

static inline void
dma_fence_free(struct dma_fence *fence)
{
	free(fence, M_DRM, 0);
}

/*
 * is a later than b
 * if a and b are the same, should return false to avoid unwanted work
 */
static inline bool
__dma_fence_is_later(uint64_t a, uint64_t b, const struct dma_fence_ops *ops)
{
	uint32_t al, bl;

	if (ops->use_64bit_seqno)
		return a > b;

	al = a & 0xffffffff;
	bl = b & 0xffffffff;

	return (int)(al - bl) > 0;
}

static inline bool
dma_fence_is_later(struct dma_fence *a, struct dma_fence *b)
{
	if (a->context != b->context)
		return false;
	return __dma_fence_is_later(a->seqno, b->seqno, a->ops);
}

static inline bool
dma_fence_is_later_or_same(struct dma_fence *a, struct dma_fence *b)
{
	if (a == b)
		return true;
	return dma_fence_is_later(a, b);
}

static inline void
dma_fence_set_error(struct dma_fence *fence, int error)
{
	fence->error = error;
}

static inline bool
dma_fence_begin_signalling(void)
{
	return true;
}

static inline void
dma_fence_end_signalling(bool x)
{
}

#endif
