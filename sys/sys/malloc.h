/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2005, 2009 Robert N. M. Watson
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)malloc.h	8.5 (Berkeley) 5/3/95
 * $FreeBSD$
 */

#ifndef _SYS_MALLOC_H_
#define	_SYS_MALLOC_H_

#include <sys/param.h>
#ifdef _KERNEL
#include <sys/systm.h>
#endif
#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <machine/_limits.h>

#define	MINALLOCSIZE	UMA_SMALLEST_UNIT

/*
 * Flags to memory allocation functions.
 */
#define	M_NOWAIT	0x0001		/* do not block */
#define	M_WAITOK	0x0002		/* ok to block */
#define	M_ZERO		0x0100		/* bzero the allocation */
#define	M_NOVM		0x0200		/* don't ask VM for pages */
#define	M_USE_RESERVE	0x0400		/* can alloc out of reserve memory */
#define	M_NODUMP	0x0800		/* don't dump pages in this allocation */
#define	M_FIRSTFIT	0x1000		/* Only for vmem, fast fit. */
#define	M_BESTFIT	0x2000		/* Only for vmem, low fragmentation. */
#define	M_EXEC		0x4000		/* allocate executable space. */

#define	M_MAGIC		877983977	/* time when first defined :-) */

/*
 * Two malloc type structures are present: malloc_type, which is used by a
 * type owner to declare the type, and malloc_type_internal, which holds
 * malloc-owned statistics and other ABI-sensitive fields, such as the set of
 * malloc statistics indexed by the compile-time MAXCPU constant.
 * Applications should avoid introducing dependence on the allocator private
 * data layout and size.
 *
 * The malloc_type ks_next field is protected by malloc_mtx.  Other fields in
 * malloc_type are static after initialization so unsynchronized.
 *
 * Statistics in malloc_type_stats are written only when holding a critical
 * section and running on the CPU associated with the index into the stat
 * array, but read lock-free resulting in possible (minor) races, which the
 * monitoring app should take into account.
 */
struct malloc_type_stats {
	uint64_t	mts_memalloced;	/* Bytes allocated on CPU. */
	uint64_t	mts_memfreed;	/* Bytes freed on CPU. */
	uint64_t	mts_numallocs;	/* Number of allocates on CPU. */
	uint64_t	mts_numfrees;	/* number of frees on CPU. */
	uint64_t	mts_size;	/* Bitmask of sizes allocated on CPU. */
	uint64_t	_mts_reserved1;	/* Reserved field. */
	uint64_t	_mts_reserved2;	/* Reserved field. */
	uint64_t	_mts_reserved3;	/* Reserved field. */
};

/*
 * Index definitions for the mti_probes[] array.
 */
#define DTMALLOC_PROBE_MALLOC		0
#define DTMALLOC_PROBE_FREE		1
#define DTMALLOC_PROBE_MAX		2

struct malloc_type_internal {
	uint32_t	mti_probes[DTMALLOC_PROBE_MAX];
					/* DTrace probe ID array. */
	u_char		mti_zone;
	struct malloc_type_stats	*mti_stats;
};

/*
 * Public data structure describing a malloc type.  Private data is hung off
 * of ks_handle to avoid encoding internal malloc(9) data structures in
 * modules, which will statically allocate struct malloc_type.
 */
struct malloc_type {
	struct malloc_type *ks_next;	/* Next in global chain. */
	u_long		 ks_magic;	/* Detect programmer error. */
	const char	*ks_shortdesc;	/* Printable type name. */
	void		*ks_handle;	/* Priv. data, was lo_class. */
};

/*
 * Statistics structure headers for user space.  The kern.malloc sysctl
 * exposes a structure stream consisting of a stream header, then a series of
 * malloc type headers and statistics structures (quantity maxcpus).  For
 * convenience, the kernel will provide the current value of maxcpus at the
 * head of the stream.
 */
#define	MALLOC_TYPE_STREAM_VERSION	0x00000001
struct malloc_type_stream_header {
	uint32_t	mtsh_version;	/* Stream format version. */
	uint32_t	mtsh_maxcpus;	/* Value of MAXCPU for stream. */
	uint32_t	mtsh_count;	/* Number of records. */
	uint32_t	_mtsh_pad;	/* Pad/reserved field. */
};

#define	MALLOC_MAX_NAME	32
struct malloc_type_header {
	char				mth_name[MALLOC_MAX_NAME];
};

#ifdef _KERNEL
#define	MALLOC_DEFINE(type, shortdesc, longdesc)			\
	struct malloc_type type[1] = {					\
		{ NULL, M_MAGIC, shortdesc, NULL }			\
	};								\
	SYSINIT(type##_init, SI_SUB_KMEM, SI_ORDER_THIRD, malloc_init,	\
	    type);							\
	SYSUNINIT(type##_uninit, SI_SUB_KMEM, SI_ORDER_ANY,		\
	    malloc_uninit, type)

#define	MALLOC_DECLARE(type) \
	extern struct malloc_type type[1]

MALLOC_DECLARE(M_CACHE);
MALLOC_DECLARE(M_DEVBUF);
MALLOC_DECLARE(M_TEMP);

/*
 * XXX this should be declared in <sys/uio.h>, but that tends to fail
 * because <sys/uio.h> is included in a header before the source file
 * has a chance to include <sys/malloc.h> to get MALLOC_DECLARE() defined.
 */
MALLOC_DECLARE(M_IOV);

struct domainset;
extern struct mtx malloc_mtx;

/*
 * Function type used when iterating over the list of malloc types.
 */
typedef void malloc_type_list_func_t(struct malloc_type *, void *);

void	contigfree(void *addr, unsigned long size, struct malloc_type *type);
void	*contigmalloc(unsigned long size, struct malloc_type *type, int flags,
	    vm_paddr_t low, vm_paddr_t high, unsigned long alignment,
	    vm_paddr_t boundary) __malloc_like __result_use_check
	    __alloc_size(1) __alloc_align(6);
void	*contigmalloc_domainset(unsigned long size, struct malloc_type *type,
	    struct domainset *ds, int flags, vm_paddr_t low, vm_paddr_t high,
	    unsigned long alignment, vm_paddr_t boundary)
	    __malloc_like __result_use_check __alloc_size(1) __alloc_align(6);
void	free(void *addr, struct malloc_type *type);
void	free_domain(void *addr, struct malloc_type *type);
void	*malloc(size_t size, struct malloc_type *type, int flags) __malloc_like
	    __result_use_check __alloc_size(1);
/*
 * Try to optimize malloc(..., ..., M_ZERO) allocations by doing zeroing in
 * place if the size is known at compilation time.
 *
 * Passing the flag down requires malloc to blindly zero the entire object.
 * In practice a lot of the zeroing can be avoided if most of the object
 * gets explicitly initialized after the allocation. Letting the compiler
 * zero in place gives it the opportunity to take advantage of this state.
 *
 * Note that the operation is only applicable if both flags and size are
 * known at compilation time. If M_ZERO is passed but M_WAITOK is not, the
 * allocation can fail and a NULL check is needed. However, if M_WAITOK is
 * passed we know the allocation must succeed and the check can be elided.
 *
 *	_malloc_item = malloc(_size, type, (flags) &~ M_ZERO);
 *	if (((flags) & M_WAITOK) != 0 || _malloc_item != NULL)
 *		bzero(_malloc_item, _size);
 *
 * If the flag is set, the compiler knows the left side is always true,
 * therefore the entire statement is true and the callsite is:
 *
 *	_malloc_item = malloc(_size, type, (flags) &~ M_ZERO);
 *	bzero(_malloc_item, _size);
 *
 * If the flag is not set, the compiler knows the left size is always false
 * and the NULL check is needed, therefore the callsite is:
 *
 * 	_malloc_item = malloc(_size, type, (flags) &~ M_ZERO);
 *	if (_malloc_item != NULL)
 *		bzero(_malloc_item, _size);			
 *
 * The implementation is a macro because of what appears to be a clang 6 bug:
 * an inline function variant ended up being compiled to a mere malloc call
 * regardless of argument. gcc generates expected code (like the above).
 */
#define	malloc(size, type, flags) ({					\
	void *_malloc_item;						\
	size_t _size = (size);						\
	if (__builtin_constant_p(size) && __builtin_constant_p(flags) &&\
	    ((flags) & M_ZERO) != 0) {					\
		_malloc_item = malloc(_size, type, (flags) &~ M_ZERO);	\
		if (((flags) & M_WAITOK) != 0 ||			\
		    __predict_true(_malloc_item != NULL))		\
			bzero(_malloc_item, _size);			\
	} else {							\
		_malloc_item = malloc(_size, type, flags);		\
	}								\
	_malloc_item;							\
})

void	*malloc_domainset(size_t size, struct malloc_type *type,
	    struct domainset *ds, int flags) __malloc_like __result_use_check
	    __alloc_size(1);
void	*mallocarray(size_t nmemb, size_t size, struct malloc_type *type,
	    int flags) __malloc_like __result_use_check
	    __alloc_size2(1, 2);
void	malloc_init(void *);
int	malloc_last_fail(void);
void	malloc_type_allocated(struct malloc_type *type, unsigned long size);
void	malloc_type_freed(struct malloc_type *type, unsigned long size);
void	malloc_type_list(malloc_type_list_func_t *, void *);
void	malloc_uninit(void *);
void	*realloc(void *addr, size_t size, struct malloc_type *type, int flags)
	    __result_use_check __alloc_size(2);
void	*reallocf(void *addr, size_t size, struct malloc_type *type, int flags)
	    __result_use_check __alloc_size(2);

struct malloc_type *malloc_desc2type(const char *desc);

/*
 * This is sqrt(SIZE_MAX+1), as s1*s2 <= SIZE_MAX
 * if both s1 < MUL_NO_OVERFLOW and s2 < MUL_NO_OVERFLOW
 */
#define MUL_NO_OVERFLOW		(1UL << (sizeof(size_t) * 8 / 2))
static inline bool
WOULD_OVERFLOW(size_t nmemb, size_t size)
{

	return ((nmemb >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) &&
	    nmemb > 0 && __SIZE_T_MAX / nmemb < size);
}
#undef MUL_NO_OVERFLOW
#endif /* _KERNEL */

#endif /* !_SYS_MALLOC_H_ */
