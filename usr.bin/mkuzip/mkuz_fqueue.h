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
 *
 * $FreeBSD$
 */

struct mkuz_fifo_queue {
    pthread_mutex_t mtx;
    pthread_cond_t cvar;
    struct mkuz_bchain_link *first;
    struct mkuz_bchain_link *last;
    int length;
    int wakeup_len;
};

struct mkuz_blk;
struct mkuz_bchain_link;

DEFINE_RAW_METHOD(cmp_cb, int, const struct mkuz_blk *, void *);

struct mkuz_fifo_queue *mkuz_fqueue_ctor(int);
void mkuz_fqueue_enq(struct mkuz_fifo_queue *, struct mkuz_blk *);
struct mkuz_blk *mkuz_fqueue_deq(struct mkuz_fifo_queue *);
struct mkuz_blk *mkuz_fqueue_deq_when(struct mkuz_fifo_queue *, cmp_cb_t, void *);
#if defined(NOTYET)
struct mkuz_bchain_link *mkuz_fqueue_deq_all(struct mkuz_fifo_queue *, int *);
int mkuz_fqueue_enq_all(struct mkuz_fifo_queue *, struct mkuz_bchain_link *,
  struct mkuz_bchain_link *, int);
#endif
