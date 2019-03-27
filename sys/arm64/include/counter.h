/*-
 * Copyright (c) 2012 Konstantin Belousov <kib@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _MACHINE_COUNTER_H_
#define	_MACHINE_COUNTER_H_

#include <sys/pcpu.h>
#include <machine/atomic.h>

extern struct pcpu __pcpu[];

#define	EARLY_COUNTER	&__pcpu[0].pc_early_dummy_counter

#define	counter_enter()	do {} while (0)
#define	counter_exit()	do {} while (0)

#ifdef IN_SUBR_COUNTER_C
static inline uint64_t
counter_u64_read_one(uint64_t *p, int cpu)
{

	return (*(uint64_t *)((char *)p + UMA_PCPU_ALLOC_SIZE * cpu));
}

static inline uint64_t
counter_u64_fetch_inline(uint64_t *p)
{
	uint64_t r;
	int i;

	r = 0;
	CPU_FOREACH(i)
		r += counter_u64_read_one((uint64_t *)p, i);

	return (r);
}

static void
counter_u64_zero_one_cpu(void *arg)
{

	*((uint64_t *)((char *)arg + UMA_PCPU_ALLOC_SIZE *
	    PCPU_GET(cpuid))) = 0;
}

static inline void
counter_u64_zero_inline(counter_u64_t c)
{

	smp_rendezvous(smp_no_rendezvous_barrier, counter_u64_zero_one_cpu,
	    smp_no_rendezvous_barrier, c);
}
#endif

#define	counter_u64_add_protected(c, inc)	counter_u64_add(c, inc)

static inline void
counter_u64_add(counter_u64_t c, int64_t inc)
{

	atomic_add_64((uint64_t *)zpcpu_get(c), inc);
}

#endif	/* ! _MACHINE_COUNTER_H_ */
