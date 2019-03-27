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
 * Customisation of the SSCFU code to ng_sscfu.
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/callout.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <machine/stdarg.h>

/*
 * Allocate zeroed or non-zeroed memory of some size and cast it.
 * Return NULL on failure.
 */
#ifndef SSCFU_DEBUG

#define	MEMINIT() \
	MALLOC_DECLARE(M_NG_SSCFU); \
	DECL_SIGQ_GET

#define	MEMZALLOC(PTR, CAST, SIZE) \
	((PTR) = (CAST)malloc((SIZE), M_NG_SSCFU, M_NOWAIT | M_ZERO))
#define	MEMFREE(PTR) \
	free(PTR, M_NG_SSCFU)

#define	SIG_ALLOC(PTR) \
	MEMZALLOC(PTR, struct sscfu_sig *, sizeof(struct sscfu_sig))
#define	SIG_FREE(PTR) \
	MEMFREE(PTR)

#else

#define	MEMINIT() 							\
	MALLOC_DEFINE(M_NG_SSCFU_INS, "sscfu_ins", "SSCFU instances");	\
	MALLOC_DEFINE(M_NG_SSCFU_SIG, "sscfu_sig", "SSCFU signals");	\
	DECL_SIGQ_GET

#define	MEMZALLOC(PTR, CAST, SIZE)					\
	((PTR) = (CAST)malloc((SIZE), M_NG_SSCFU_INS, M_NOWAIT | M_ZERO))
#define	MEMFREE(PTR)							\
	free(PTR, M_NG_SSCFU_INS)

#define	SIG_ALLOC(PTR)							\
	((PTR) = malloc(sizeof(struct sscfu_sig),			\
	    M_NG_SSCFU_SIG, M_NOWAIT | M_ZERO))
#define	SIG_FREE(PTR)							\
	free(PTR, M_NG_SSCFU_SIG)

#endif


/*
 * Signal queues
 */
typedef TAILQ_ENTRY(sscfu_sig) sscfu_sigq_link_t;
typedef TAILQ_HEAD(sscfu_sigq, sscfu_sig) sscfu_sigq_head_t;
#define	SIGQ_INIT(Q) 		TAILQ_INIT(Q)
#define	SIGQ_APPEND(Q, S)	TAILQ_INSERT_TAIL(Q, S, link)

#define	SIGQ_GET(Q) ng_sscfu_sigq_get((Q))

#define	DECL_SIGQ_GET							\
static __inline struct sscfu_sig *					\
ng_sscfu_sigq_get(struct sscfu_sigq *q)					\
{									\
	struct sscfu_sig *s;						\
									\
	s = TAILQ_FIRST(q);						\
	if (s != NULL)							\
		TAILQ_REMOVE(q, s, link);				\
	return (s);							\
}

#define	SIGQ_CLEAR(Q)							\
    do {								\
	struct sscfu_sig *_s1, *_s2;					\
									\
	_s1 = TAILQ_FIRST(Q);						\
	while (_s1 != NULL) {						\
		_s2 = TAILQ_NEXT(_s1, link);				\
		if (_s1->m)						\
			MBUF_FREE(_s1->m);				\
		SIG_FREE(_s1);						\
		_s1 = _s2;						\
	}								\
	TAILQ_INIT(Q);							\
    } while (0)


/*
 * Message buffers
 */
#define	MBUF_FREE(M)	m_freem(M)

#ifdef SSCFU_DEBUG
#define	ASSERT(S)	KASSERT(S, (#S))
#else
#define	ASSERT(S)
#endif
