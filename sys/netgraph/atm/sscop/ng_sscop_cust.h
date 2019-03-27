/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
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
 *
 * Customisation of the SSCOP code to ng_sscop.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <machine/stdarg.h>

#include <netnatm/saal/sscopdef.h>

/*
 * Allocate zeroed or non-zeroed memory of some size and cast it.
 * Return NULL on failure.
 */
#ifndef SSCOP_DEBUG

#define	MEMINIT() \
	MALLOC_DECLARE(M_NG_SSCOP); \
	DECL_MSGQ_GET \
	DECL_SIGQ_GET \
	DECL_MBUF_ALLOC

#define	MEMZALLOC(PTR, CAST, SIZE) \
	((PTR) = (CAST)malloc((SIZE), M_NG_SSCOP, M_NOWAIT | M_ZERO))
#define	MEMFREE(PTR) \
	free((PTR), M_NG_SSCOP)

#define	MSG_ALLOC(PTR) \
	MEMZALLOC(PTR, struct sscop_msg *, sizeof(struct sscop_msg))
#define	MSG_FREE(PTR) \
	MEMFREE(PTR)

#define	SIG_ALLOC(PTR) \
	MEMZALLOC(PTR, struct sscop_sig *, sizeof(struct sscop_sig))
#define	SIG_FREE(PTR) \
	MEMFREE(PTR)

#else

#define	MEMINIT() 							\
	MALLOC_DEFINE(M_NG_SSCOP_INS, "sscop_ins", "SSCOP instances");	\
	MALLOC_DEFINE(M_NG_SSCOP_MSG, "sscop_msg", "SSCOP buffers");	\
	MALLOC_DEFINE(M_NG_SSCOP_SIG, "sscop_sig", "SSCOP signals");	\
	DECL_MSGQ_GET \
	DECL_SIGQ_GET \
	DECL_MBUF_ALLOC

#define	MEMZALLOC(PTR, CAST, SIZE)					\
	((PTR) = (CAST)malloc((SIZE), M_NG_SSCOP_INS, M_NOWAIT | M_ZERO))
#define	MEMFREE(PTR)							\
	free((PTR), M_NG_SSCOP_INS)

#define	MSG_ALLOC(PTR)							\
	((PTR) = malloc(sizeof(struct sscop_msg),			\
	    M_NG_SSCOP_MSG, M_NOWAIT | M_ZERO))
#define	MSG_FREE(PTR)							\
	free((PTR), M_NG_SSCOP_MSG)

#define	SIG_ALLOC(PTR)							\
	((PTR) = malloc(sizeof(struct sscop_sig),			\
	    M_NG_SSCOP_SIG, M_NOWAIT | M_ZERO))
#define	SIG_FREE(PTR)							\
	free((PTR), M_NG_SSCOP_SIG)

#endif

/*
 * Timer support.
 */
typedef struct callout sscop_timer_t;
#define	TIMER_INIT(S, T)	ng_callout_init(&(S)->t_##T)
#define	TIMER_STOP(S,T)	do {						\
	ng_uncallout(&(S)->t_##T, (S)->aarg);				\
    } while (0)
#define	TIMER_RESTART(S, T) do {					\
	TIMER_STOP(S, T);						\
	ng_callout(&(S)->t_##T, (S)->aarg, NULL,			\
	    hz * (S)->timer##T / 1000, T##_func, (S), 0);		\
    } while (0)
#define	TIMER_ISACT(S, T) (callout_pending(&(S)->t_##T))

/*
 * This assumes, that the user argument is the node pointer.
 */
#define	TIMER_FUNC(T,N)							\
static void								\
T##_func(node_p node, hook_p hook, void *arg1, int arg2)		\
{									\
	struct sscop *sscop = arg1;					\
									\
	VERBOSE(sscop, SSCOP_DBG_TIMER, (sscop, sscop->aarg,		\
	    "timer_" #T " expired"));					\
	sscop_signal(sscop, SIG_T_##N, NULL);				\
}


/*
 * Message queues
 */
typedef TAILQ_ENTRY(sscop_msg) sscop_msgq_link_t;
typedef TAILQ_HEAD(sscop_msgq, sscop_msg) sscop_msgq_head_t;
#define	MSGQ_EMPTY(Q)		TAILQ_EMPTY(Q)
#define	MSGQ_INIT(Q)		TAILQ_INIT(Q)
#define	MSGQ_FOREACH(P, Q)	TAILQ_FOREACH(P, Q, link)
#define	MSGQ_REMOVE(Q, M)	TAILQ_REMOVE(Q, M, link)
#define	MSGQ_INSERT_BEFORE(B, M) TAILQ_INSERT_BEFORE(B, M, link)
#define	MSGQ_APPEND(Q, M)	TAILQ_INSERT_TAIL(Q, M, link)
#define	MSGQ_PEEK(Q)		TAILQ_FIRST((Q))

#define	MSGQ_GET(Q) ng_sscop_msgq_get((Q))

#define DECL_MSGQ_GET							\
static __inline struct sscop_msg *					\
ng_sscop_msgq_get(struct sscop_msgq *q)					\
{									\
	struct sscop_msg *m;						\
									\
	m = TAILQ_FIRST(q);						\
	if (m != NULL)							\
		TAILQ_REMOVE(q, m, link);				\
	return (m);							\
}

#define	MSGQ_CLEAR(Q)							\
	do {								\
		struct sscop_msg *_m1, *_m2;				\
									\
		_m1 = TAILQ_FIRST(Q);					\
		while (_m1 != NULL) {					\
			_m2 = TAILQ_NEXT(_m1, link);			\
			SSCOP_MSG_FREE(_m1);				\
			_m1 = _m2;					\
		}							\
		TAILQ_INIT((Q));					\
	} while (0)

/*
 * Signal queues
 */
typedef TAILQ_ENTRY(sscop_sig) sscop_sigq_link_t;
typedef TAILQ_HEAD(sscop_sigq, sscop_sig) sscop_sigq_head_t;
#define	SIGQ_INIT(Q) 		TAILQ_INIT(Q)
#define	SIGQ_APPEND(Q, S)	TAILQ_INSERT_TAIL(Q, S, link)
#define	SIGQ_EMPTY(Q)		TAILQ_EMPTY(Q)

#define	SIGQ_GET(Q)	ng_sscop_sigq_get((Q))
#define	DECL_SIGQ_GET							\
static __inline struct sscop_sig *					\
ng_sscop_sigq_get(struct sscop_sigq *q)					\
{									\
	struct sscop_sig *s;						\
									\
	s = TAILQ_FIRST(q);						\
	if (s != NULL)							\
		TAILQ_REMOVE(q, s, link);				\
	return (s);							\
}

#define	SIGQ_MOVE(F, T)							\
    do {								\
	struct sscop_sig *_s;						\
									\
	while (!TAILQ_EMPTY(F)) {					\
		_s = TAILQ_FIRST(F);					\
		TAILQ_REMOVE(F, _s, link);				\
		TAILQ_INSERT_TAIL(T, _s, link);				\
	}								\
    } while (0)

#define	SIGQ_PREPEND(F, T)						\
    do {								\
	struct sscop_sig *_s;						\
									\
	while (!TAILQ_EMPTY(F)) {					\
		_s = TAILQ_LAST(F, sscop_sigq);				\
		TAILQ_REMOVE(F, _s, link);				\
		TAILQ_INSERT_HEAD(T, _s, link);				\
	}								\
    } while (0)

#define	SIGQ_CLEAR(Q)							\
    do {								\
	struct sscop_sig *_s1, *_s2;					\
									\
	_s1 = TAILQ_FIRST(Q);						\
	while (_s1 != NULL) {						\
		_s2 = TAILQ_NEXT(_s1, link);				\
		SSCOP_MSG_FREE(_s1->msg);				\
		SIG_FREE(_s1);						\
		_s1 = _s2;						\
	}								\
	TAILQ_INIT(Q);							\
    } while (0)

/*
 * Message buffers
 */
#define	MBUF_FREE(M)	do { if ((M)) m_freem((M)); } while(0)
#define	MBUF_DUP(M)	m_copypacket((M), M_NOWAIT)
#define	MBUF_LEN(M) 	((size_t)(M)->m_pkthdr.len)

/*
 * Return the i-th word counted from the end of the buffer.
 * i=-1 will return the last 32bit word, i=-2 the 2nd last.
 * Assumes that there is enough space.
 */
#define	MBUF_TRAIL32(M ,I) ng_sscop_mbuf_trail32((M), (I))

static uint32_t __inline	
ng_sscop_mbuf_trail32(const struct mbuf *m, int i)
{
	uint32_t w;

	m_copydata(m, m->m_pkthdr.len + 4 * i, 4, (caddr_t)&w);
	return (ntohl(w));
}

/*
 * Strip 32bit value from the end
 */
#define	MBUF_STRIP32(M) ng_sscop_mbuf_strip32((M))

static uint32_t __inline
ng_sscop_mbuf_strip32(struct mbuf *m)
{
	uint32_t w;

	m_copydata(m, m->m_pkthdr.len - 4, 4, (caddr_t)&w);
	m_adj(m, -4);
	return (ntohl(w));
}

#define	MBUF_GET32(M) ng_sscop_mbuf_get32((M))

static uint32_t __inline
ng_sscop_mbuf_get32(struct mbuf *m)
{
	uint32_t w;

	m_copydata(m, 0, 4, (caddr_t)&w);
	m_adj(m, 4);
	return (ntohl(w));
}

/*
 * Append a 32bit value to an mbuf. Failures are ignored.
 */
#define	MBUF_APPEND32(M, W)						\
     do {								\
	uint32_t _w = (W);						\
									\
	_w = htonl(_w);							\
	m_copyback((M), (M)->m_pkthdr.len, 4, (caddr_t)&_w);		\
    } while (0)

/*
 * Pad a message to a multiple of four byte and return the amount of padding
 * Failures are ignored.
 */
#define	MBUF_PAD4(M) ng_sscop_mbuf_pad4((M))

static u_int __inline
ng_sscop_mbuf_pad4(struct mbuf *m)
{
	static u_char pad[4] = { 0, 0, 0, 0 };
	int len = m->m_pkthdr.len;
	int npad = 3 - ((len + 3) & 3);

	if (npad != 0)
		m_copyback(m, len, npad, (caddr_t)pad);
	return (npad);
}

#define	MBUF_UNPAD(M, P) do { if( (P) > 0) m_adj((M), -(P)); } while (0)

/*
 * Allocate a message that will probably hold N bytes.
 */
#define	MBUF_ALLOC(N) ng_sscop_mbuf_alloc((N))

#define	DECL_MBUF_ALLOC							\
static __inline struct mbuf *						\
ng_sscop_mbuf_alloc(size_t n)						\
{									\
	struct mbuf *m;							\
									\
	MGETHDR(m, M_NOWAIT, MT_DATA);					\
	if (m != NULL) {						\
		m->m_len = 0;						\
		m->m_pkthdr.len = 0;					\
		if (n > MHLEN) {					\
			if (!(MCLGET(m, M_NOWAIT))){			\
				m_free(m);				\
				m = NULL;				\
			}						\
		}							\
	}								\
	return (m);							\
}

#ifdef SSCOP_DEBUG
#define	ASSERT(X)	KASSERT(X, (#X))
#else
#define	ASSERT(X)
#endif
