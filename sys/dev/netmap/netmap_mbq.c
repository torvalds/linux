/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2013-2014 Vincenzo Maffione
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 */

/*
 * $FreeBSD$
 */


#ifdef linux
#include "bsd_glue.h"
#elif defined (_WIN32)
#include "win_glue.h"
#else   /* __FreeBSD__ */
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#endif  /* __FreeBSD__ */

#include "netmap_mbq.h"


static inline void __mbq_init(struct mbq *q)
{
    q->head = q->tail = NULL;
    q->count = 0;
}


void mbq_safe_init(struct mbq *q)
{
    mtx_init(&q->lock, "mbq", NULL, MTX_SPIN);
    __mbq_init(q);
}


void mbq_init(struct mbq *q)
{
    __mbq_init(q);
}


static inline void __mbq_enqueue(struct mbq *q, struct mbuf *m)
{
    m->m_nextpkt = NULL;
    if (q->tail) {
        q->tail->m_nextpkt = m;
        q->tail = m;
    } else {
        q->head = q->tail = m;
    }
    q->count++;
}


void mbq_safe_enqueue(struct mbq *q, struct mbuf *m)
{
    mbq_lock(q);
    __mbq_enqueue(q, m);
    mbq_unlock(q);
}


void mbq_enqueue(struct mbq *q, struct mbuf *m)
{
    __mbq_enqueue(q, m);
}


static inline struct mbuf *__mbq_dequeue(struct mbq *q)
{
    struct mbuf *ret = NULL;

    if (q->head) {
        ret = q->head;
        q->head = ret->m_nextpkt;
        if (q->head == NULL) {
            q->tail = NULL;
        }
        q->count--;
        ret->m_nextpkt = NULL;
    }

    return ret;
}


struct mbuf *mbq_safe_dequeue(struct mbq *q)
{
    struct mbuf *ret;

    mbq_lock(q);
    ret =  __mbq_dequeue(q);
    mbq_unlock(q);

    return ret;
}


struct mbuf *mbq_dequeue(struct mbq *q)
{
    return __mbq_dequeue(q);
}


/* XXX seems pointless to have a generic purge */
static void __mbq_purge(struct mbq *q, int safe)
{
    struct mbuf *m;

    for (;;) {
        m = safe ? mbq_safe_dequeue(q) : mbq_dequeue(q);
        if (m) {
            m_freem(m);
        } else {
            break;
        }
    }
}


void mbq_purge(struct mbq *q)
{
    __mbq_purge(q, 0);
}


void mbq_safe_purge(struct mbq *q)
{
    __mbq_purge(q, 1);
}


void mbq_safe_fini(struct mbq *q)
{
    mtx_destroy(&q->lock);
}


void mbq_fini(struct mbq *q)
{
}
