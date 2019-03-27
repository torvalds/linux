/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 * Customisation of signalling source to the NG environment.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/atm/ngatmbase.h>

#define	ASSERT(E, M) KASSERT(E,M)

/*
 * Memory
 */
enum unimem {
	UNIMEM_INS = 0,
	UNIMEM_ALL,
	UNIMEM_SIG,
	UNIMEM_CALL,
	UNIMEM_PARTY,
};
#define	UNIMEM_TYPES	5

void *ng_uni_malloc(enum unimem, const char *, u_int);
void ng_uni_free(enum unimem, void *, const char *, u_int);

#define	INS_ALLOC()	ng_uni_malloc(UNIMEM_INS, __FILE__, __LINE__)
#define	INS_FREE(P)	ng_uni_free(UNIMEM_INS, P, __FILE__, __LINE__)

#define	UNI_ALLOC()	ng_uni_malloc(UNIMEM_ALL, __FILE__, __LINE__)
#define	UNI_FREE(P)	ng_uni_free(UNIMEM_ALL, P, __FILE__, __LINE__)

#define	SIG_ALLOC()	ng_uni_malloc(UNIMEM_SIG, __FILE__, __LINE__)
#define	SIG_FREE(P)	ng_uni_free(UNIMEM_SIG, P, __FILE__, __LINE__)

#define	CALL_ALLOC()	ng_uni_malloc(UNIMEM_CALL, __FILE__, __LINE__)
#define	CALL_FREE(P)	ng_uni_free(UNIMEM_CALL, P, __FILE__, __LINE__)

#define	PARTY_ALLOC()	ng_uni_malloc(UNIMEM_PARTY, __FILE__, __LINE__)
#define	PARTY_FREE(P)	ng_uni_free(UNIMEM_PARTY, P, __FILE__, __LINE__)

/*
 * Timers
 */
struct uni_timer {
	struct callout c;
};

#define	_TIMER_INIT(X,T)	ng_callout_init(&(X)->T.c)
#define	_TIMER_DESTROY(UNI,FIELD) _TIMER_STOP(UNI,FIELD)
#define	_TIMER_STOP(UNI,FIELD) do {						\
	ng_uncallout(&FIELD.c, (UNI)->arg);					\
    } while (0)
#define	TIMER_ISACT(UNI,T)	(callout_active(&(UNI)->T.c) ||		\
	callout_pending(&(UNI)->T.c))
#define	_TIMER_START(UNI,ARG,FIELD,DUE,FUNC) do {			\
	_TIMER_STOP(UNI, FIELD);					\
	ng_callout(&FIELD.c, (UNI)->arg, NULL,				\
	    hz * (DUE) / 1000, FUNC, (ARG), 0);				\
    } while (0)

#define	TIMER_FUNC_UNI(T,F)						\
static void F(struct uni *);						\
static void								\
_##T##_func(node_p node, hook_p hook, void *arg1, int arg2)		\
{									\
	struct uni *uni = (struct uni *)arg1;				\
									\
	(F)(uni);							\
	uni_work(uni);							\
}

/*
 * Be careful: call may be invalid after the call to F
 */
#define	TIMER_FUNC_CALL(T,F)						\
static void F(struct call *);						\
static void								\
_##T##_func(node_p node, hook_p hook, void *arg1, int arg2)		\
{									\
	struct call *call = (struct call *)arg1;			\
	struct uni *uni = call->uni;					\
									\
	(F)(call);							\
	uni_work(uni);							\
}

/*
 * Be careful: call/party may be invalid after the call to F
 */
#define	TIMER_FUNC_PARTY(T,F)						\
static void F(struct party *);						\
static void								\
_##T##_func(node_p node, hook_p hook, void *arg1, int arg2)		\
{									\
	struct party *party = (struct party *)arg1;			\
	struct uni *uni = party->call->uni;				\
									\
	(F)(party);							\
	uni_work(uni);							\
}

extern size_t unimem_sizes[UNIMEM_TYPES];

#define	UNICORE								\
size_t unimem_sizes[UNIMEM_TYPES] = {					\
	[UNIMEM_INS]	= sizeof(struct uni),				\
	[UNIMEM_ALL]	= sizeof(struct uni_all),			\
	[UNIMEM_SIG]	= sizeof(struct sig),				\
	[UNIMEM_CALL]	= sizeof(struct call),				\
	[UNIMEM_PARTY]	= sizeof(struct party)				\
};
