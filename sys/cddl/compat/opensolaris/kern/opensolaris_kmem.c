/*-
 * Copyright (c) 2006-2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/vmmeter.h>

#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>

#ifdef KMEM_DEBUG
#include <sys/queue.h>
#include <sys/stack.h>
#endif

#ifdef _KERNEL
MALLOC_DEFINE(M_SOLARIS, "solaris", "Solaris");
#else
#define	malloc(size, type, flags)	malloc(size)
#define	free(addr, type)		free(addr)
#endif

#ifdef KMEM_DEBUG
struct kmem_item {
	struct stack	stack;
	LIST_ENTRY(kmem_item) next;
};
static LIST_HEAD(, kmem_item) kmem_items;
static struct mtx kmem_items_mtx;
MTX_SYSINIT(kmem_items_mtx, &kmem_items_mtx, "kmem_items", MTX_DEF);
#endif	/* KMEM_DEBUG */

#include <sys/vmem.h>

void *
zfs_kmem_alloc(size_t size, int kmflags)
{
	void *p;
#ifdef KMEM_DEBUG
	struct kmem_item *i;

	size += sizeof(struct kmem_item);
#endif
	p = malloc(size, M_SOLARIS, kmflags);
#ifndef _KERNEL
	if (kmflags & KM_SLEEP)
		assert(p != NULL);
#endif
#ifdef KMEM_DEBUG
	if (p != NULL) {
		i = p;
		p = (u_char *)p + sizeof(struct kmem_item);
		stack_save(&i->stack);
		mtx_lock(&kmem_items_mtx);
		LIST_INSERT_HEAD(&kmem_items, i, next);
		mtx_unlock(&kmem_items_mtx);
	}
#endif
	return (p);
}

void
zfs_kmem_free(void *buf, size_t size __unused)
{
#ifdef KMEM_DEBUG
	if (buf == NULL) {
		printf("%s: attempt to free NULL\n", __func__);
		return;
	}
	struct kmem_item *i;

	buf = (u_char *)buf - sizeof(struct kmem_item);
	mtx_lock(&kmem_items_mtx);
	LIST_FOREACH(i, &kmem_items, next) {
		if (i == buf)
			break;
	}
	ASSERT(i != NULL);
	LIST_REMOVE(i, next);
	mtx_unlock(&kmem_items_mtx);
#endif
	free(buf, M_SOLARIS);
}

static uint64_t kmem_size_val;

static void
kmem_size_init(void *unused __unused)
{

	kmem_size_val = (uint64_t)vm_cnt.v_page_count * PAGE_SIZE;
	if (kmem_size_val > vm_kmem_size)
		kmem_size_val = vm_kmem_size;
}
SYSINIT(kmem_size_init, SI_SUB_KMEM, SI_ORDER_ANY, kmem_size_init, NULL);

uint64_t
kmem_size(void)
{

	return (kmem_size_val);
}

static int
kmem_std_constructor(void *mem, int size __unused, void *private, int flags)
{
	struct kmem_cache *cache = private;

	return (cache->kc_constructor(mem, cache->kc_private, flags));
}

static void
kmem_std_destructor(void *mem, int size __unused, void *private)
{
	struct kmem_cache *cache = private;

	cache->kc_destructor(mem, cache->kc_private);
}

kmem_cache_t *
kmem_cache_create(char *name, size_t bufsize, size_t align,
    int (*constructor)(void *, void *, int), void (*destructor)(void *, void *),
    void (*reclaim)(void *) __unused, void *private, vmem_t *vmp, int cflags)
{
	kmem_cache_t *cache;

	ASSERT(vmp == NULL);

	cache = kmem_alloc(sizeof(*cache), KM_SLEEP);
	strlcpy(cache->kc_name, name, sizeof(cache->kc_name));
	cache->kc_constructor = constructor;
	cache->kc_destructor = destructor;
	cache->kc_private = private;
#if defined(_KERNEL) && !defined(KMEM_DEBUG)
	cache->kc_zone = uma_zcreate(cache->kc_name, bufsize,
	    constructor != NULL ? kmem_std_constructor : NULL,
	    destructor != NULL ? kmem_std_destructor : NULL,
	    NULL, NULL, align > 0 ? align - 1 : 0, cflags);
#else
	cache->kc_size = bufsize;
#endif

	return (cache);
}

void
kmem_cache_destroy(kmem_cache_t *cache)
{
#if defined(_KERNEL) && !defined(KMEM_DEBUG)
	uma_zdestroy(cache->kc_zone);
#endif
	kmem_free(cache, sizeof(*cache));
}

void *
kmem_cache_alloc(kmem_cache_t *cache, int flags)
{
#if defined(_KERNEL) && !defined(KMEM_DEBUG)
	return (uma_zalloc_arg(cache->kc_zone, cache, flags));
#else
	void *p;

	p = kmem_alloc(cache->kc_size, flags);
	if (p != NULL && cache->kc_constructor != NULL)
		kmem_std_constructor(p, cache->kc_size, cache, flags);
	return (p);
#endif
}

void
kmem_cache_free(kmem_cache_t *cache, void *buf)
{
#if defined(_KERNEL) && !defined(KMEM_DEBUG)
	uma_zfree_arg(cache->kc_zone, buf, cache);
#else
	if (cache->kc_destructor != NULL)
		kmem_std_destructor(buf, cache->kc_size, cache);
	kmem_free(buf, cache->kc_size);
#endif
}

/*
 * Allow our caller to determine if there are running reaps.
 *
 * This call is very conservative and may return B_TRUE even when
 * reaping activity isn't active. If it returns B_FALSE, then reaping
 * activity is definitely inactive.
 */
boolean_t
kmem_cache_reap_active(void)
{

	return (B_FALSE);
}

/*
 * Reap (almost) everything soon.
 *
 * Note: this does not wait for the reap-tasks to complete. Caller
 * should use kmem_cache_reap_active() (above) and/or moderation to
 * avoid scheduling too many reap-tasks.
 */
#ifdef _KERNEL
void
kmem_cache_reap_soon(kmem_cache_t *cache)
{
#ifndef KMEM_DEBUG
	zone_drain(cache->kc_zone);
#endif
}

void
kmem_reap(void)
{
	uma_reclaim();
}
#else
void
kmem_cache_reap_soon(kmem_cache_t *cache __unused)
{
}

void
kmem_reap(void)
{
}
#endif

int
kmem_debugging(void)
{
	return (0);
}

void *
calloc(size_t n, size_t s)
{
	return (kmem_zalloc(n * s, KM_NOSLEEP));
}

#ifdef KMEM_DEBUG
void kmem_show(void *);
void
kmem_show(void *dummy __unused)
{
	struct kmem_item *i;

	mtx_lock(&kmem_items_mtx);
	if (LIST_EMPTY(&kmem_items))
		printf("KMEM_DEBUG: No leaked elements.\n");
	else {
		printf("KMEM_DEBUG: Leaked elements:\n\n");
		LIST_FOREACH(i, &kmem_items, next) {
			printf("address=%p\n", i);
			stack_print_ddb(&i->stack);
			printf("\n");
		}
	}
	mtx_unlock(&kmem_items_mtx);
}

SYSUNINIT(sol_kmem, SI_SUB_CPU, SI_ORDER_FIRST, kmem_show, NULL);
#endif	/* KMEM_DEBUG */
