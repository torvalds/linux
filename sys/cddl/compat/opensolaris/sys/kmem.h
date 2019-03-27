/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_KMEM_H_
#define	_OPENSOLARIS_SYS_KMEM_H_

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/vmem.h>
#include <sys/vmmeter.h>

#include <vm/uma.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>

MALLOC_DECLARE(M_SOLARIS);

#define	POINTER_IS_VALID(p)	(!((uintptr_t)(p) & 0x3))
#define	POINTER_INVALIDATE(pp)	(*(pp) = (void *)((uintptr_t)(*(pp)) | 0x1))

#define	KM_SLEEP		M_WAITOK
#define	KM_PUSHPAGE		M_WAITOK
#define	KM_NOSLEEP		M_NOWAIT
#define	KM_NODEBUG		M_NODUMP
#define	KM_NORMALPRI		0
#define	KMC_NODEBUG		UMA_ZONE_NODUMP
#define	KMC_NOTOUCH		0

typedef struct kmem_cache {
	char		kc_name[32];
#if defined(_KERNEL) && !defined(KMEM_DEBUG)
	uma_zone_t	kc_zone;
#else
	size_t		kc_size;
#endif
	int		(*kc_constructor)(void *, void *, int);
	void		(*kc_destructor)(void *, void *);
	void		*kc_private;
} kmem_cache_t;

void *zfs_kmem_alloc(size_t size, int kmflags);
void zfs_kmem_free(void *buf, size_t size);
uint64_t kmem_size(void);
kmem_cache_t *kmem_cache_create(char *name, size_t bufsize, size_t align,
    int (*constructor)(void *, void *, int), void (*destructor)(void *, void *),
    void (*reclaim)(void *) __unused, void *private, vmem_t *vmp, int cflags);
void kmem_cache_destroy(kmem_cache_t *cache);
void *kmem_cache_alloc(kmem_cache_t *cache, int flags);
void kmem_cache_free(kmem_cache_t *cache, void *buf);
boolean_t kmem_cache_reap_active(void);
void kmem_cache_reap_soon(kmem_cache_t *);
void kmem_reap(void);
int kmem_debugging(void);
void *calloc(size_t n, size_t s);

#define	freemem				vm_free_count()
#define	minfree				vm_cnt.v_free_min
#define	heap_arena			kernel_arena
#define	zio_arena			NULL
#define	kmem_alloc(size, kmflags)	zfs_kmem_alloc((size), (kmflags))
#define	kmem_zalloc(size, kmflags)	zfs_kmem_alloc((size), (kmflags) | M_ZERO)
#define	kmem_free(buf, size)		zfs_kmem_free((buf), (size))

#define	kmem_cache_set_move(cache, movefunc)	do { } while (0)

#endif	/* _OPENSOLARIS_SYS_KMEM_H_ */
