/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#ifndef __MACHINE_COUNTER_H__
#define __MACHINE_COUNTER_H__

#include <sys/pcpu.h>
#ifdef INVARIANTS
#include <sys/proc.h>
#endif
#include <machine/md_var.h>
#include <machine/specialreg.h>

extern struct pcpu __pcpu[];

#define	EARLY_COUNTER	&__pcpu[0].pc_early_dummy_counter

#define	counter_enter()	do {				\
	if ((cpu_feature & CPUID_CX8) == 0)		\
		critical_enter();			\
} while (0)

#define	counter_exit()	do {				\
	if ((cpu_feature & CPUID_CX8) == 0)		\
		critical_exit();			\
} while (0)

static inline void
counter_64_inc_8b(uint64_t *p, int64_t inc)
{

	__asm __volatile(
	"movl	%%fs:(%%esi),%%eax\n\t"
	"movl	%%fs:4(%%esi),%%edx\n"
"1:\n\t"
	"movl	%%eax,%%ebx\n\t"
	"movl	%%edx,%%ecx\n\t"
	"addl	(%%edi),%%ebx\n\t"
	"adcl	4(%%edi),%%ecx\n\t"
	"cmpxchg8b %%fs:(%%esi)\n\t"
	"jnz	1b"
	:
	: "S" ((char *)p - (char *)&__pcpu[0]), "D" (&inc)
	: "memory", "cc", "eax", "edx", "ebx", "ecx");
}

#ifdef IN_SUBR_COUNTER_C
struct counter_u64_fetch_cx8_arg {
	uint64_t res;
	uint64_t *p;
};

static uint64_t
counter_u64_read_one_8b(uint64_t *p)
{
	uint32_t res_lo, res_high;

	__asm __volatile(
	"movl	%%eax,%%ebx\n\t"
	"movl	%%edx,%%ecx\n\t"
	"cmpxchg8b	(%2)"
	: "=a" (res_lo), "=d"(res_high)
	: "SD" (p)
	: "cc", "ebx", "ecx");
	return (res_lo + ((uint64_t)res_high << 32));
}

static void
counter_u64_fetch_cx8_one(void *arg1)
{
	struct counter_u64_fetch_cx8_arg *arg;
	uint64_t val;

	arg = arg1;
	val = counter_u64_read_one_8b((uint64_t *)((char *)arg->p +
	    UMA_PCPU_ALLOC_SIZE * PCPU_GET(cpuid)));
	atomic_add_64(&arg->res, val);
}

static inline uint64_t
counter_u64_fetch_inline(uint64_t *p)
{
	struct counter_u64_fetch_cx8_arg arg;
	uint64_t res;
	int i;

	res = 0;
	if ((cpu_feature & CPUID_CX8) == 0) {
		/*
		 * The machines without cmpxchg8b are not SMP.
		 * Disabling the preemption provides atomicity of the
		 * counter reading, since update is done in the
		 * critical section as well.
		 */
		critical_enter();
		CPU_FOREACH(i) {
			res += *(uint64_t *)((char *)p +
			    UMA_PCPU_ALLOC_SIZE * i);
		}
		critical_exit();
	} else {
		arg.p = p;
		arg.res = 0;
		smp_rendezvous(NULL, counter_u64_fetch_cx8_one, NULL, &arg);
		res = arg.res;
	}
	return (res);
}

static inline void
counter_u64_zero_one_8b(uint64_t *p)
{

	__asm __volatile(
	"movl	(%0),%%eax\n\t"
	"movl	4(%0),%%edx\n"
	"xorl	%%ebx,%%ebx\n\t"
	"xorl	%%ecx,%%ecx\n\t"
"1:\n\t"
	"cmpxchg8b	(%0)\n\t"
	"jnz	1b"
	:
	: "SD" (p)
	: "memory", "cc", "eax", "edx", "ebx", "ecx");
}

static void
counter_u64_zero_one_cpu(void *arg)
{
	uint64_t *p;

	p = (uint64_t *)((char *)arg + UMA_PCPU_ALLOC_SIZE * PCPU_GET(cpuid));
	counter_u64_zero_one_8b(p);
}

static inline void
counter_u64_zero_inline(counter_u64_t c)
{
	int i;

	if ((cpu_feature & CPUID_CX8) == 0) {
		critical_enter();
		CPU_FOREACH(i)
			*(uint64_t *)((char *)c + UMA_PCPU_ALLOC_SIZE * i) = 0;
		critical_exit();
	} else {
		smp_rendezvous(smp_no_rendezvous_barrier,
		    counter_u64_zero_one_cpu, smp_no_rendezvous_barrier, c);
	}
}
#endif

#define	counter_u64_add_protected(c, inc)	do {	\
	if ((cpu_feature & CPUID_CX8) == 0) {		\
		CRITICAL_ASSERT(curthread);		\
		*(uint64_t *)zpcpu_get(c) += (inc);	\
	} else						\
		counter_64_inc_8b((c), (inc));		\
} while (0)

static inline void
counter_u64_add(counter_u64_t c, int64_t inc)
{

	if ((cpu_feature & CPUID_CX8) == 0) {
		critical_enter();
		*(uint64_t *)zpcpu_get(c) += inc;
		critical_exit();
	} else {
		counter_64_inc_8b(c, inc);
	}
}

#endif	/* ! __MACHINE_COUNTER_H__ */
