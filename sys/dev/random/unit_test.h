/*-
 * Copyright (c) 2013-2015 Mark R V Murray
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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
 *
 * $FreeBSD$
 */


#ifndef UNIT_TEST_H_INCLUDED
#define	UNIT_TEST_H_INCLUDED

#ifdef _KERNEL
#error "Random unit tests cannot be compiled into the kernel."
#endif

#include <sys/types.h>
#include <inttypes.h>
#include <stdint.h>

#if defined(clang) && __has_builtin(__builtin_readcyclecounter)
#define	rdtsc __builtin_readcyclecounter
#else /* !clang */
#if defined(__amd64__) || defined(__i386__)
static __inline uint64_t
rdtsc(void)
{
	uint32_t low, high;

	__asm __volatile("rdtsc" : "=a" (low), "=d" (high));
	return (low | ((uint64_t)high << 32));
}
#else /* __amd64__ || __i386__ */
#error "No rdtsc() implementation available."
#endif /* __amd64__ || __i386__ */
#endif /* !clang */

static __inline uint64_t
get_cyclecount(void)
{

	return (rdtsc());
}

#define	HARVESTSIZE	2
#define RANDOM_BLOCKSIZE	16

enum random_entropy_source {
	RANDOM_START = 0,
	RANDOM_CACHED = 0,
	ENTROPYSOURCE = 32
};

struct harvest_event {
	uintmax_t			he_somecounter;		/* fast counter for clock jitter */
	uint32_t			he_entropy[HARVESTSIZE];/* some harvested entropy */
	uint8_t				he_size;		/* harvested entropy byte count */
	uint8_t				he_destination;		/* destination pool of this entropy */
	enum random_entropy_source	he_source;		/* origin of the entropy */
	void *				he_next;		/* next item on the list */
};

struct sysctl_ctx_list;

#define	CTASSERT(x)	_Static_assert(x, "compile-time assertion failed")
#define	KASSERT(exp,msg) do {	\
	if (!(exp)) {		\
		printf msg;	\
		exit(0);	\
	}			\
} while (0)

#endif /* UNIT_TEST_H_INCLUDED */
