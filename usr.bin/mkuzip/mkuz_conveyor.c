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

#include <sys/types.h>
#include <err.h>
#include <inttypes.h>
#include <md5.h>
#include <pthread.h>
#include <stdlib.h>
#include <strings.h>

#if defined(MKUZ_DEBUG)
# include <stdio.h>
#endif

#include "mkuz_conveyor.h"
#include "mkuz_cfg.h"
#include "mkuzip.h"
#include "mkuz_format.h"
#include "mkuz_blk.h"
#include "mkuz_fqueue.h"
#include "mkuz_blk_chain.h"

static void compute_digest(struct mkuz_blk *);

struct cw_args {
    struct mkuz_conveyor *cvp;
    struct mkuz_cfg *cfp;
};

static void *
cworker(void *p)
{
    struct cw_args *cwp;
    struct mkuz_cfg *cfp;
    struct mkuz_blk *oblk, *iblk;
    struct mkuz_conveyor *cvp;
    void *c_ctx;

    cwp = (struct cw_args *)p;
    cfp = cwp->cfp;
    cvp = cwp->cvp;
    free(cwp);
    c_ctx = cfp->handler->f_init(cfp->blksz);
    for (;;) {
        iblk = mkuz_fqueue_deq(cvp->wrk_queue);
        if (iblk == MKUZ_BLK_EOF) {
            /* Let other threads to see the EOF block */
            mkuz_fqueue_enq(cvp->wrk_queue, iblk);
            break;
        }
        if (cfp->no_zcomp == 0 &&
          mkuz_memvcmp(iblk->data, '\0', iblk->info.len) != 0) {
            /* All zeroes block */
            oblk = mkuz_blk_ctor(0);
        } else {
            oblk = cfp->handler->f_compress(c_ctx, iblk);
            if (cfp->en_dedup != 0) {
                compute_digest(oblk);
            }
        }
        oblk->info.blkno = iblk->info.blkno;
        mkuz_fqueue_enq(cvp->results, oblk);
        free(iblk);
    }
    return (NULL);
}

static void
compute_digest(struct mkuz_blk *bp)
{
    MD5_CTX mcontext;

    MD5Init(&mcontext);
    MD5Update(&mcontext, bp->data, bp->info.len);
    MD5Final(bp->info.digest, &mcontext);
}

struct mkuz_conveyor *
mkuz_conveyor_ctor(struct mkuz_cfg *cfp)
{
    struct mkuz_conveyor *cp;
    struct cw_args *cwp;
    int i, r;

    cp = mkuz_safe_zmalloc(sizeof(struct mkuz_conveyor) +
      (sizeof(pthread_t) * cfp->nworkers));

    cp->wrk_queue = mkuz_fqueue_ctor(1);
    cp->results = mkuz_fqueue_ctor(1);

    for (i = 0; i < cfp->nworkers; i++) {
        cwp = mkuz_safe_zmalloc(sizeof(struct cw_args));
        cwp->cfp = cfp;
        cwp->cvp = cp;
        r = pthread_create(&cp->wthreads[i], NULL, cworker, (void *)cwp);
        if (r != 0) {
            errx(1, "mkuz_conveyor_ctor: pthread_create() failed");
            /* Not reached */
        }
    }
    return (cp);
}
