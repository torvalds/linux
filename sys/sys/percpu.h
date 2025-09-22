/*	$OpenBSD: percpu.h,v 1.9 2023/09/16 09:33:27 mpi Exp $ */

/*
 * Copyright (c) 2016 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SYS_PERCPU_H_
#define _SYS_PERCPU_H_

#ifndef CACHELINESIZE
#define CACHELINESIZE 64
#endif

#ifndef __upunused /* this should go in param.h */
#ifdef MULTIPROCESSOR
#define __upunused
#else
#define __upunused __attribute__((__unused__))
#endif
#endif

struct cpumem {
	void		*mem;
};

struct cpumem_iter {
	unsigned int	cpu;
} __upunused;

struct counters_ref {
	uint64_t	 g;
	uint64_t	*c;
};

#ifdef _KERNEL

#include <sys/atomic.h>

struct pool;

struct cpumem	*cpumem_get(struct pool *);
void		 cpumem_put(struct pool *, struct cpumem *);

struct cpumem	*cpumem_malloc(size_t, int);
struct cpumem	*cpumem_malloc_ncpus(struct cpumem *, size_t, int);
void		 cpumem_free(struct cpumem *, int, size_t);

void		*cpumem_first(struct cpumem_iter *, struct cpumem *);
void		*cpumem_next(struct cpumem_iter *, struct cpumem *);

static inline void *
cpumem_enter(struct cpumem *cm)
{
#ifdef MULTIPROCESSOR
	return (cm[cpu_number()].mem);
#else
	return (cm);
#endif
}

static inline void
cpumem_leave(struct cpumem *cm, void *mem)
{
	/* KDASSERT? */
}

#ifdef MULTIPROCESSOR

#define CPUMEM_BOOT_MEMORY(_name, _sz)					\
static struct {								\
	unsigned char	mem[_sz];					\
	struct cpumem	cpumem;						\
} __aligned(CACHELINESIZE) _name##_boot_cpumem = {			\
	.cpumem = { _name##_boot_cpumem.mem }				\
}

#define CPUMEM_BOOT_INITIALIZER(_name)					\
	{ &_name##_boot_cpumem.cpumem }

#else /* MULTIPROCESSOR */

#define CPUMEM_BOOT_MEMORY(_name, _sz)					\
static struct {								\
	unsigned char	mem[_sz];					\
} __aligned(sizeof(uint64_t)) _name##_boot_cpumem

#define CPUMEM_BOOT_INITIALIZER(_name)					\
	{ (struct cpumem *)&_name##_boot_cpumem.mem }

#endif /* MULTIPROCESSOR */

#define CPUMEM_FOREACH(_var, _iter, _cpumem)				\
	for ((_var) = cpumem_first((_iter), (_cpumem));			\
	    (_var) != NULL;						\
	    (_var) = cpumem_next((_iter), (_cpumem)))

/*
 * per cpu counters
 */

struct cpumem	*counters_alloc(unsigned int);
struct cpumem	*counters_alloc_ncpus(struct cpumem *, unsigned int);
void		 counters_free(struct cpumem *, unsigned int);
void		 counters_read(struct cpumem *, uint64_t *, unsigned int,
		     uint64_t *);
void		 counters_zero(struct cpumem *, unsigned int);

static inline uint64_t *
counters_enter(struct counters_ref *ref, struct cpumem *cm)
{
	ref->c = cpumem_enter(cm);
#ifdef MULTIPROCESSOR
	ref->g = ++(*ref->c); /* make the generation number odd */
	membar_producer();
	return (ref->c + 1);
#else
	return (ref->c);
#endif
}

static inline void
counters_leave(struct counters_ref *ref, struct cpumem *cm)
{
#ifdef MULTIPROCESSOR
	membar_producer();
	(*ref->c) = ++ref->g; /* make the generation number even again */
#endif
	cpumem_leave(cm, ref->c);
}

static inline void
counters_inc(struct cpumem *cm, unsigned int c)
{
	struct counters_ref ref;
	uint64_t *counters;

	counters = counters_enter(&ref, cm);
	counters[c]++;
	counters_leave(&ref, cm);
}

static inline void
counters_dec(struct cpumem *cm, unsigned int c)
{
	struct counters_ref ref;
	uint64_t *counters;

	counters = counters_enter(&ref, cm);
	counters[c]--;
	counters_leave(&ref, cm);
}

static inline void
counters_add(struct cpumem *cm, unsigned int c, uint64_t v)
{
	struct counters_ref ref;
	uint64_t *counters;

	counters = counters_enter(&ref, cm);
	counters[c] += v;
	counters_leave(&ref, cm);
}

static inline void
counters_pkt(struct cpumem *cm, unsigned int c, unsigned int b, uint64_t v)
{
	struct counters_ref ref;
	uint64_t *counters;

	counters = counters_enter(&ref, cm);
	counters[c]++;
	counters[b] += v;
	counters_leave(&ref, cm);
}

#ifdef MULTIPROCESSOR
#define COUNTERS_BOOT_MEMORY(_name, _n)					\
	CPUMEM_BOOT_MEMORY(_name, ((_n) + 1) * sizeof(uint64_t))
#else
#define COUNTERS_BOOT_MEMORY(_name, _n)					\
	CPUMEM_BOOT_MEMORY(_name, (_n) * sizeof(uint64_t))
#endif

#define COUNTERS_BOOT_INITIALIZER(_name)	CPUMEM_BOOT_INITIALIZER(_name)

#endif /* _KERNEL */
#endif /* _SYS_PERCPU_H_ */
