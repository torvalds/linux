/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/stdarg.h>

#include <linux/bitmap.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/err.h>

#define	MAX_IDR_LEVEL	((MAX_IDR_SHIFT + IDR_BITS - 1) / IDR_BITS)
#define	MAX_IDR_FREE	(MAX_IDR_LEVEL * 2)

struct linux_idr_cache {
	spinlock_t lock;
	struct idr_layer *head;
	unsigned count;
};

DPCPU_DEFINE_STATIC(struct linux_idr_cache, linux_idr_cache);

/*
 * IDR Implementation.
 *
 * This is quick and dirty and not as re-entrant as the linux version
 * however it should be fairly fast.  It is basically a radix tree with
 * a builtin bitmap for allocation.
 */
static MALLOC_DEFINE(M_IDR, "idr", "Linux IDR compat");

static struct idr_layer *
idr_preload_dequeue_locked(struct linux_idr_cache *lic)
{
	struct idr_layer *retval;

	/* check if wrong thread is trying to dequeue */
	if (mtx_owned(&lic->lock.m) == 0)
		return (NULL);

	retval = lic->head;
	if (likely(retval != NULL)) {
		lic->head = retval->ary[0];
		lic->count--;
		retval->ary[0] = NULL;
	}
	return (retval);
}

static void
idr_preload_init(void *arg)
{
	int cpu;

	CPU_FOREACH(cpu) {
		struct linux_idr_cache *lic =
		    DPCPU_ID_PTR(cpu, linux_idr_cache);

		spin_lock_init(&lic->lock);
	}
}
SYSINIT(idr_preload_init, SI_SUB_CPU, SI_ORDER_ANY, idr_preload_init, NULL);

static void
idr_preload_uninit(void *arg)
{
	int cpu;

	CPU_FOREACH(cpu) {
		struct idr_layer *cacheval;
		struct linux_idr_cache *lic =
		    DPCPU_ID_PTR(cpu, linux_idr_cache);

		while (1) {
			spin_lock(&lic->lock);
			cacheval = idr_preload_dequeue_locked(lic);
			spin_unlock(&lic->lock);

			if (cacheval == NULL)
				break;
			free(cacheval, M_IDR);
		}
		spin_lock_destroy(&lic->lock);
	}
}
SYSUNINIT(idr_preload_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, idr_preload_uninit, NULL);

void
idr_preload(gfp_t gfp_mask)
{
	struct linux_idr_cache *lic;
	struct idr_layer *cacheval;

	sched_pin();

	lic = &DPCPU_GET(linux_idr_cache);

	/* fill up cache */
	spin_lock(&lic->lock);
	while (lic->count < MAX_IDR_FREE) {
		spin_unlock(&lic->lock);
		cacheval = malloc(sizeof(*cacheval), M_IDR, M_ZERO | gfp_mask);
		spin_lock(&lic->lock);
		if (cacheval == NULL)
			break;
		cacheval->ary[0] = lic->head;
		lic->head = cacheval;
		lic->count++;
	}
}

void
idr_preload_end(void)
{
	struct linux_idr_cache *lic;

	lic = &DPCPU_GET(linux_idr_cache);
	spin_unlock(&lic->lock);
	sched_unpin();
}

static inline int
idr_max(struct idr *idr)
{
	return (1 << (idr->layers * IDR_BITS)) - 1;
}

static inline int
idr_pos(int id, int layer)
{
	return (id >> (IDR_BITS * layer)) & IDR_MASK;
}

void
idr_init(struct idr *idr)
{
	bzero(idr, sizeof(*idr));
	mtx_init(&idr->lock, "idr", NULL, MTX_DEF);
}

/* Only frees cached pages. */
void
idr_destroy(struct idr *idr)
{
	struct idr_layer *il, *iln;

	idr_remove_all(idr);
	mtx_lock(&idr->lock);
	for (il = idr->free; il != NULL; il = iln) {
		iln = il->ary[0];
		free(il, M_IDR);
	}
	mtx_unlock(&idr->lock);
	mtx_destroy(&idr->lock);
}

static void
idr_remove_layer(struct idr_layer *il, int layer)
{
	int i;

	if (il == NULL)
		return;
	if (layer == 0) {
		free(il, M_IDR);
		return;
	}
	for (i = 0; i < IDR_SIZE; i++)
		if (il->ary[i])
			idr_remove_layer(il->ary[i], layer - 1);
}

void
idr_remove_all(struct idr *idr)
{

	mtx_lock(&idr->lock);
	idr_remove_layer(idr->top, idr->layers - 1);
	idr->top = NULL;
	idr->layers = 0;
	mtx_unlock(&idr->lock);
}

static void *
idr_remove_locked(struct idr *idr, int id)
{
	struct idr_layer *il;
	void *res;
	int layer;
	int idx;

	id &= MAX_ID_MASK;
	il = idr->top;
	layer = idr->layers - 1;
	if (il == NULL || id > idr_max(idr))
		return (NULL);
	/*
	 * Walk down the tree to this item setting bitmaps along the way
	 * as we know at least one item will be free along this path.
	 */
	while (layer && il) {
		idx = idr_pos(id, layer);
		il->bitmap |= 1 << idx;
		il = il->ary[idx];
		layer--;
	}
	idx = id & IDR_MASK;
	/*
	 * At this point we've set free space bitmaps up the whole tree.
	 * We could make this non-fatal and unwind but linux dumps a stack
	 * and a warning so I don't think it's necessary.
	 */
	if (il == NULL || (il->bitmap & (1 << idx)) != 0)
		panic("idr_remove: Item %d not allocated (%p, %p)\n",
		    id, idr, il);
	res = il->ary[idx];
	il->ary[idx] = NULL;
	il->bitmap |= 1 << idx;

	return (res);
}

void *
idr_remove(struct idr *idr, int id)
{
	void *res;

	mtx_lock(&idr->lock);
	res = idr_remove_locked(idr, id);
	mtx_unlock(&idr->lock);

	return (res);
}


static inline struct idr_layer *
idr_find_layer_locked(struct idr *idr, int id)
{
	struct idr_layer *il;
	int layer;

	id &= MAX_ID_MASK;
	il = idr->top;
	layer = idr->layers - 1;
	if (il == NULL || id > idr_max(idr))
		return (NULL);
	while (layer && il) {
		il = il->ary[idr_pos(id, layer)];
		layer--;
	}
	return (il);
}

void *
idr_replace(struct idr *idr, void *ptr, int id)
{
	struct idr_layer *il;
	void *res;
	int idx;

	mtx_lock(&idr->lock);
	il = idr_find_layer_locked(idr, id);
	idx = id & IDR_MASK;

	/* Replace still returns an error if the item was not allocated. */
	if (il == NULL || (il->bitmap & (1 << idx))) {
		res = ERR_PTR(-ENOENT);
	} else {
		res = il->ary[idx];
		il->ary[idx] = ptr;
	}
	mtx_unlock(&idr->lock);
	return (res);
}

static inline void *
idr_find_locked(struct idr *idr, int id)
{
	struct idr_layer *il;
	void *res;

	mtx_assert(&idr->lock, MA_OWNED);
	il = idr_find_layer_locked(idr, id);
	if (il != NULL)
		res = il->ary[id & IDR_MASK];
	else
		res = NULL;
	return (res);
}

void *
idr_find(struct idr *idr, int id)
{
	void *res;

	mtx_lock(&idr->lock);
	res = idr_find_locked(idr, id);
	mtx_unlock(&idr->lock);
	return (res);
}

void *
idr_get_next(struct idr *idr, int *nextidp)
{
	void *res = NULL;
	int id = *nextidp;

	mtx_lock(&idr->lock);
	for (; id <= idr_max(idr); id++) {
		res = idr_find_locked(idr, id);
		if (res == NULL)
			continue;
		*nextidp = id;
		break;
	}
	mtx_unlock(&idr->lock);
	return (res);
}

int
idr_pre_get(struct idr *idr, gfp_t gfp_mask)
{
	struct idr_layer *il, *iln;
	struct idr_layer *head;
	int need;

	mtx_lock(&idr->lock);
	for (;;) {
		need = idr->layers + 1;
		for (il = idr->free; il != NULL; il = il->ary[0])
			need--;
		mtx_unlock(&idr->lock);
		if (need <= 0)
			break;
		for (head = NULL; need; need--) {
			iln = malloc(sizeof(*il), M_IDR, M_ZERO | gfp_mask);
			if (iln == NULL)
				break;
			bitmap_fill(&iln->bitmap, IDR_SIZE);
			if (head != NULL) {
				il->ary[0] = iln;
				il = iln;
			} else
				head = il = iln;
		}
		if (head == NULL)
			return (0);
		mtx_lock(&idr->lock);
		il->ary[0] = idr->free;
		idr->free = head;
	}
	return (1);
}

static struct idr_layer *
idr_free_list_get(struct idr *idp)
{
	struct idr_layer *il;

	if ((il = idp->free) != NULL) {
		idp->free = il->ary[0];
		il->ary[0] = NULL;
	}
	return (il);
}

static inline struct idr_layer *
idr_get(struct idr *idp)
{
	struct idr_layer *il;

	if ((il = idr_free_list_get(idp)) != NULL) {
		MPASS(il->bitmap != 0);
	} else if ((il = malloc(sizeof(*il), M_IDR, M_ZERO | M_NOWAIT)) != NULL) {
		bitmap_fill(&il->bitmap, IDR_SIZE);
	} else if ((il = idr_preload_dequeue_locked(&DPCPU_GET(linux_idr_cache))) != NULL) {
		bitmap_fill(&il->bitmap, IDR_SIZE);
	} else {
		return (NULL);
	}
	return (il);
}

/*
 * Could be implemented as get_new_above(idr, ptr, 0, idp) but written
 * first for simplicity sake.
 */
static int
idr_get_new_locked(struct idr *idr, void *ptr, int *idp)
{
	struct idr_layer *stack[MAX_LEVEL];
	struct idr_layer *il;
	int error;
	int layer;
	int idx;
	int id;

	mtx_assert(&idr->lock, MA_OWNED);

	error = -EAGAIN;
	/*
	 * Expand the tree until there is free space.
	 */
	if (idr->top == NULL || idr->top->bitmap == 0) {
		if (idr->layers == MAX_LEVEL + 1) {
			error = -ENOSPC;
			goto out;
		}
		il = idr_get(idr);
		if (il == NULL)
			goto out;
		il->ary[0] = idr->top;
		if (idr->top)
			il->bitmap &= ~1;
		idr->top = il;
		idr->layers++;
	}
	il = idr->top;
	id = 0;
	/*
	 * Walk the tree following free bitmaps, record our path.
	 */
	for (layer = idr->layers - 1;; layer--) {
		stack[layer] = il;
		idx = ffsl(il->bitmap);
		if (idx == 0)
			panic("idr_get_new: Invalid leaf state (%p, %p)\n",
			    idr, il);
		idx--;
		id |= idx << (layer * IDR_BITS);
		if (layer == 0)
			break;
		if (il->ary[idx] == NULL) {
			il->ary[idx] = idr_get(idr);
			if (il->ary[idx] == NULL)
				goto out;
		}
		il = il->ary[idx];
	}
	/*
	 * Allocate the leaf to the consumer.
	 */
	il->bitmap &= ~(1 << idx);
	il->ary[idx] = ptr;
	*idp = id;
	/*
	 * Clear bitmaps potentially up to the root.
	 */
	while (il->bitmap == 0 && ++layer < idr->layers) {
		il = stack[layer];
		il->bitmap &= ~(1 << idr_pos(id, layer));
	}
	error = 0;
out:
#ifdef INVARIANTS
	if (error == 0 && idr_find_locked(idr, id) != ptr) {
		panic("idr_get_new: Failed for idr %p, id %d, ptr %p\n",
		    idr, id, ptr);
	}
#endif
	return (error);
}

int
idr_get_new(struct idr *idr, void *ptr, int *idp)
{
	int retval;

	mtx_lock(&idr->lock);
	retval = idr_get_new_locked(idr, ptr, idp);
	mtx_unlock(&idr->lock);
	return (retval);
}

static int
idr_get_new_above_locked(struct idr *idr, void *ptr, int starting_id, int *idp)
{
	struct idr_layer *stack[MAX_LEVEL];
	struct idr_layer *il;
	int error;
	int layer;
	int idx, sidx;
	int id;

	mtx_assert(&idr->lock, MA_OWNED);

	error = -EAGAIN;
	/*
	 * Compute the layers required to support starting_id and the mask
	 * at the top layer.
	 */
restart:
	idx = starting_id;
	layer = 0;
	while (idx & ~IDR_MASK) {
		layer++;
		idx >>= IDR_BITS;
	}
	if (layer == MAX_LEVEL + 1) {
		error = -ENOSPC;
		goto out;
	}
	/*
	 * Expand the tree until there is free space at or beyond starting_id.
	 */
	while (idr->layers <= layer ||
	    idr->top->bitmap < (1 << idr_pos(starting_id, idr->layers - 1))) {
		if (idr->layers == MAX_LEVEL + 1) {
			error = -ENOSPC;
			goto out;
		}
		il = idr_get(idr);
		if (il == NULL)
			goto out;
		il->ary[0] = idr->top;
		if (idr->top && idr->top->bitmap == 0)
			il->bitmap &= ~1;
		idr->top = il;
		idr->layers++;
	}
	il = idr->top;
	id = 0;
	/*
	 * Walk the tree following free bitmaps, record our path.
	 */
	for (layer = idr->layers - 1;; layer--) {
		stack[layer] = il;
		sidx = idr_pos(starting_id, layer);
		/* Returns index numbered from 0 or size if none exists. */
		idx = find_next_bit(&il->bitmap, IDR_SIZE, sidx);
		if (idx == IDR_SIZE && sidx == 0)
			panic("idr_get_new: Invalid leaf state (%p, %p)\n",
			    idr, il);
		/*
		 * We may have walked a path where there was a free bit but
		 * it was lower than what we wanted.  Restart the search with
		 * a larger starting id.  id contains the progress we made so
		 * far.  Search the leaf one above this level.  This may
		 * restart as many as MAX_LEVEL times but that is expected
		 * to be rare.
		 */
		if (idx == IDR_SIZE) {
			starting_id = id + (1 << ((layer + 1) * IDR_BITS));
			goto restart;
		}
		if (idx > sidx)
			starting_id = 0;	/* Search the whole subtree. */
		id |= idx << (layer * IDR_BITS);
		if (layer == 0)
			break;
		if (il->ary[idx] == NULL) {
			il->ary[idx] = idr_get(idr);
			if (il->ary[idx] == NULL)
				goto out;
		}
		il = il->ary[idx];
	}
	/*
	 * Allocate the leaf to the consumer.
	 */
	il->bitmap &= ~(1 << idx);
	il->ary[idx] = ptr;
	*idp = id;
	/*
	 * Clear bitmaps potentially up to the root.
	 */
	while (il->bitmap == 0 && ++layer < idr->layers) {
		il = stack[layer];
		il->bitmap &= ~(1 << idr_pos(id, layer));
	}
	error = 0;
out:
#ifdef INVARIANTS
	if (error == 0 && idr_find_locked(idr, id) != ptr) {
		panic("idr_get_new_above: Failed for idr %p, id %d, ptr %p\n",
		    idr, id, ptr);
	}
#endif
	return (error);
}

int
idr_get_new_above(struct idr *idr, void *ptr, int starting_id, int *idp)
{
	int retval;

	mtx_lock(&idr->lock);
	retval = idr_get_new_above_locked(idr, ptr, starting_id, idp);
	mtx_unlock(&idr->lock);
	return (retval);
}

int
ida_get_new_above(struct ida *ida, int starting_id, int *p_id)
{
	return (idr_get_new_above(&ida->idr, NULL, starting_id, p_id));
}

static int
idr_alloc_locked(struct idr *idr, void *ptr, int start, int end)
{
	int max = end > 0 ? end - 1 : INT_MAX;
	int error;
	int id;

	mtx_assert(&idr->lock, MA_OWNED);

	if (unlikely(start < 0))
		return (-EINVAL);
	if (unlikely(max < start))
		return (-ENOSPC);

	if (start == 0)
		error = idr_get_new_locked(idr, ptr, &id);
	else
		error = idr_get_new_above_locked(idr, ptr, start, &id);

	if (unlikely(error < 0))
		return (error);
	if (unlikely(id > max)) {
		idr_remove_locked(idr, id);
		return (-ENOSPC);
	}
	return (id);
}

int
idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp_mask)
{
	int retval;

	mtx_lock(&idr->lock);
	retval = idr_alloc_locked(idr, ptr, start, end);
	mtx_unlock(&idr->lock);
	return (retval);
}

int
idr_alloc_cyclic(struct idr *idr, void *ptr, int start, int end, gfp_t gfp_mask)
{
	int retval;

	mtx_lock(&idr->lock);
	retval = idr_alloc_locked(idr, ptr, max(start, idr->next_cyclic_id), end);
	if (unlikely(retval == -ENOSPC))
		retval = idr_alloc_locked(idr, ptr, start, end);
	if (likely(retval >= 0))
		idr->next_cyclic_id = retval + 1;
	mtx_unlock(&idr->lock);
	return (retval);
}

static int
idr_for_each_layer(struct idr_layer *il, int offset, int layer,
    int (*f)(int id, void *p, void *data), void *data)
{
	int i, err;

	if (il == NULL)
		return (0);
	if (layer == 0) {
		for (i = 0; i < IDR_SIZE; i++) {
			if (il->ary[i] == NULL)
				continue;
			err = f(i + offset, il->ary[i],  data);
			if (err)
				return (err);
		}
		return (0);
	}
	for (i = 0; i < IDR_SIZE; i++) {
		if (il->ary[i] == NULL)
			continue;
		err = idr_for_each_layer(il->ary[i],
		    (i + offset) * IDR_SIZE, layer - 1, f, data);
		if (err)
			return (err);
	}
	return (0);
}

/* NOTE: It is not allowed to modify the IDR tree while it is being iterated */
int
idr_for_each(struct idr *idp, int (*f)(int id, void *p, void *data), void *data)
{
	return (idr_for_each_layer(idp->top, 0, idp->layers - 1, f, data));
}

static int
idr_has_entry(int id, void *p, void *data)
{

	return (1);
}

bool
idr_is_empty(struct idr *idp)
{

	return (idr_for_each(idp, idr_has_entry, NULL) == 0);
}

int
ida_pre_get(struct ida *ida, gfp_t flags)
{
	if (idr_pre_get(&ida->idr, flags) == 0)
		return (0);

	if (ida->free_bitmap == NULL) {
		ida->free_bitmap =
		    malloc(sizeof(struct ida_bitmap), M_IDR, flags);
	}
	return (ida->free_bitmap != NULL);
}

int
ida_simple_get(struct ida *ida, unsigned int start, unsigned int end,
    gfp_t flags)
{
	int ret, id;
	unsigned int max;

	MPASS((int)start >= 0);
	MPASS((int)end >= 0);

	if (end == 0)
		max = 0x80000000;
	else {
		MPASS(end > start);
		max = end - 1;
	}
again:
	if (!ida_pre_get(ida, flags))
		return (-ENOMEM);

	if ((ret = ida_get_new_above(ida, start, &id)) == 0) {
		if (id > max) {
			ida_remove(ida, id);
			ret = -ENOSPC;
		} else {
			ret = id;
		}
	}
	if (__predict_false(ret == -EAGAIN))
		goto again;

	return (ret);
}

void
ida_simple_remove(struct ida *ida, unsigned int id)
{
	idr_remove(&ida->idr, id);
}

void
ida_remove(struct ida *ida, int id)
{
	idr_remove(&ida->idr, id);
}

void
ida_init(struct ida *ida)
{
	idr_init(&ida->idr);
}

void
ida_destroy(struct ida *ida)
{
	idr_destroy(&ida->idr);
	free(ida->free_bitmap, M_IDR);
}
