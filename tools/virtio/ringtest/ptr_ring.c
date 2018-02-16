// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "main.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <malloc.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>

#define SMP_CACHE_BYTES 64
#define cache_line_size() SMP_CACHE_BYTES
#define ____cacheline_aligned_in_smp __attribute__ ((aligned (SMP_CACHE_BYTES)))
#define unlikely(x)    (__builtin_expect(!!(x), 0))
#define likely(x)    (__builtin_expect(!!(x), 1))
#define ALIGN(x, a) (((x) + (a) - 1) / (a) * (a))
#define SIZE_MAX        (~(size_t)0)

typedef pthread_spinlock_t  spinlock_t;

typedef int gfp_t;
#define __GFP_ZERO 0x1

static void *kmalloc(unsigned size, gfp_t gfp)
{
	void *p = memalign(64, size);
	if (!p)
		return p;

	if (gfp & __GFP_ZERO)
		memset(p, 0, size);
	return p;
}

static inline void *kzalloc(unsigned size, gfp_t flags)
{
	return kmalloc(size, flags | __GFP_ZERO);
}

static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;
	return kmalloc(n * size, flags);
}

static inline void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}

static void kfree(void *p)
{
	if (p)
		free(p);
}

static void spin_lock_init(spinlock_t *lock)
{
	int r = pthread_spin_init(lock, 0);
	assert(!r);
}

static void spin_lock(spinlock_t *lock)
{
	int ret = pthread_spin_lock(lock);
	assert(!ret);
}

static void spin_unlock(spinlock_t *lock)
{
	int ret = pthread_spin_unlock(lock);
	assert(!ret);
}

static void spin_lock_bh(spinlock_t *lock)
{
	spin_lock(lock);
}

static void spin_unlock_bh(spinlock_t *lock)
{
	spin_unlock(lock);
}

static void spin_lock_irq(spinlock_t *lock)
{
	spin_lock(lock);
}

static void spin_unlock_irq(spinlock_t *lock)
{
	spin_unlock(lock);
}

static void spin_lock_irqsave(spinlock_t *lock, unsigned long f)
{
	spin_lock(lock);
}

static void spin_unlock_irqrestore(spinlock_t *lock, unsigned long f)
{
	spin_unlock(lock);
}

#include "../../../include/linux/ptr_ring.h"

static unsigned long long headcnt, tailcnt;
static struct ptr_ring array ____cacheline_aligned_in_smp;

/* implemented by ring */
void alloc_ring(void)
{
	int ret = ptr_ring_init(&array, ring_size, 0);
	assert(!ret);
	/* Hacky way to poke at ring internals. Useful for testing though. */
	if (param)
		array.batch = param;
}

/* guest side */
int add_inbuf(unsigned len, void *buf, void *datap)
{
	int ret;

	ret = __ptr_ring_produce(&array, buf);
	if (ret >= 0) {
		ret = 0;
		headcnt++;
	}

	return ret;
}

/*
 * ptr_ring API provides no way for producer to find out whether a given
 * buffer was consumed.  Our tests merely require that a successful get_buf
 * implies that add_inbuf succeed in the past, and that add_inbuf will succeed,
 * fake it accordingly.
 */
void *get_buf(unsigned *lenp, void **bufp)
{
	void *datap;

	if (tailcnt == headcnt || __ptr_ring_full(&array))
		datap = NULL;
	else {
		datap = "Buffer\n";
		++tailcnt;
	}

	return datap;
}

bool used_empty()
{
	return (tailcnt == headcnt || __ptr_ring_full(&array));
}

void disable_call()
{
	assert(0);
}

bool enable_call()
{
	assert(0);
}

void kick_available(void)
{
	assert(0);
}

/* host side */
void disable_kick()
{
	assert(0);
}

bool enable_kick()
{
	assert(0);
}

bool avail_empty()
{
	return __ptr_ring_empty(&array);
}

bool use_buf(unsigned *lenp, void **bufp)
{
	void *ptr;

	ptr = __ptr_ring_consume(&array);

	return ptr;
}

void call_used(void)
{
	assert(0);
}
