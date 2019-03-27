/*
 * Copyright (c) 2004-2016 Maxim Sobolev <sobomax@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(MKUZ_DEBUG)
# include <assert.h>
#endif

#include "mkuzip.h"
#include "mkuz_fqueue.h"
#include "mkuz_conveyor.h"
#include "mkuz_blk.h"
#include "mkuz_blk_chain.h"

struct mkuz_fifo_queue *
mkuz_fqueue_ctor(int wakeup_len)
{
    struct mkuz_fifo_queue *fqp;

    fqp = mkuz_safe_zmalloc(sizeof(struct mkuz_fifo_queue));
    fqp->wakeup_len = wakeup_len;
    if (pthread_mutex_init(&fqp->mtx, NULL) != 0) {
        errx(1, "pthread_mutex_init() failed");
    }
    if (pthread_cond_init(&fqp->cvar, NULL) != 0) {
        errx(1, "pthread_cond_init() failed");
    }
    return (fqp);
}

void
mkuz_fqueue_enq(struct mkuz_fifo_queue *fqp, struct mkuz_blk *bp)
{
    struct mkuz_bchain_link *ip;

    ip = mkuz_safe_zmalloc(sizeof(struct mkuz_bchain_link));
    ip->this = bp;

    pthread_mutex_lock(&fqp->mtx);
    if (fqp->first != NULL) {
        fqp->first->prev = ip;
    } else {
        fqp->last = ip;
    }
    fqp->first = ip;
    fqp->length += 1;
    if (fqp->length >= fqp->wakeup_len) {
        pthread_cond_signal(&fqp->cvar);
    }
    pthread_mutex_unlock(&fqp->mtx);
}

#if defined(NOTYET)
int
mkuz_fqueue_enq_all(struct mkuz_fifo_queue *fqp, struct mkuz_bchain_link *cip_f,
  struct mkuz_bchain_link *cip_l, int clen)
{
    int rval;

    pthread_mutex_lock(&fqp->mtx);
    if (fqp->first != NULL) {
        fqp->first->prev = cip_l;
    } else {
        fqp->last = cip_l;
    }
    fqp->first = cip_f;
    fqp->length += clen;
    rval = fqp->length;
    if (fqp->length >= fqp->wakeup_len) {
        pthread_cond_signal(&fqp->cvar);
    }
    pthread_mutex_unlock(&fqp->mtx);
    return (rval);
}
#endif

static int
mkuz_fqueue_check(struct mkuz_fifo_queue *fqp, cmp_cb_t cmp_cb, void *cap)
{
    struct mkuz_bchain_link *ip;

    for (ip = fqp->last; ip != NULL; ip = ip->prev) {
        if (cmp_cb(ip->this, cap)) {
            return (1);
        }
    }
    return (0);
}

struct mkuz_blk *
mkuz_fqueue_deq_when(struct mkuz_fifo_queue *fqp, cmp_cb_t cmp_cb, void *cap)
{
    struct mkuz_bchain_link *ip, *newlast, *newfirst, *mip;
    struct mkuz_blk *bp;

    pthread_mutex_lock(&fqp->mtx);
    while (fqp->last == NULL || !mkuz_fqueue_check(fqp, cmp_cb, cap)) {
        pthread_cond_wait(&fqp->cvar, &fqp->mtx);
    }
    if (cmp_cb(fqp->last->this, cap)) {
        mip = fqp->last;
        fqp->last = mip->prev;
        if (fqp->last == NULL) {
#if defined(MKUZ_DEBUG)
            assert(fqp->length == 1);
#endif
            fqp->first = NULL;
        }
    } else {
#if defined(MKUZ_DEBUG)
        assert(fqp->length > 1);
#endif
        newfirst = newlast = fqp->last;
        mip = NULL;
        for (ip = fqp->last->prev; ip != NULL; ip = ip->prev) {
            if (cmp_cb(ip->this, cap)) {
                mip = ip;
                continue;
            }
            newfirst->prev = ip;
            newfirst = ip;
        }
        newfirst->prev = NULL;
        fqp->first = newfirst;
        fqp->last = newlast;
    }
    fqp->length -= 1;
    pthread_mutex_unlock(&fqp->mtx);
    bp = mip->this;
    free(mip);

    return bp;
}

struct mkuz_blk *
mkuz_fqueue_deq(struct mkuz_fifo_queue *fqp)
{
    struct mkuz_bchain_link *ip;
    struct mkuz_blk *bp;

    pthread_mutex_lock(&fqp->mtx);
    while (fqp->last == NULL) {
        pthread_cond_wait(&fqp->cvar, &fqp->mtx);
    }
#if defined(MKUZ_DEBUG)
    assert(fqp->length > 0);
#endif
    ip = fqp->last;
    fqp->last = ip->prev;
    if (fqp->last == NULL) {
#if defined(MKUZ_DEBUG)
        assert(fqp->length == 1);
#endif
        fqp->first = NULL;
    }
    fqp->length -= 1;
    pthread_mutex_unlock(&fqp->mtx);
    bp = ip->this;
    free(ip);

    return bp;
}

#if defined(NOTYET)
struct mkuz_bchain_link *
mkuz_fqueue_deq_all(struct mkuz_fifo_queue *fqp, int *rclen)
{
    struct mkuz_bchain_link *rchain;

    pthread_mutex_lock(&fqp->mtx);
    while (fqp->last == NULL) {
        pthread_cond_wait(&fqp->cvar, &fqp->mtx);
    }
#if defined(MKUZ_DEBUG)
    assert(fqp->length > 0);
#endif
    rchain = fqp->last;
    fqp->first = fqp->last = NULL;
    *rclen = fqp->length;
    fqp->length = 0;
    pthread_mutex_unlock(&fqp->mtx);
    return (rchain);
}
#endif
