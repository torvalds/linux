/*
 * Copyright (c) 2016 Maxim Sobolev <sobomax@FreeBSD.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(MKUZ_DEBUG)
# include <assert.h>
# include <stdio.h>
#endif

#include "mkuz_blockcache.h"
#include "mkuz_blk.h"

struct mkuz_blkcache_itm {
    struct mkuz_blk_info hit;
    struct mkuz_blkcache_itm *next;
};

static struct mkuz_blkcache {
    struct mkuz_blkcache_itm first[256];
} blkcache;

static int
verify_match(int fd, const struct mkuz_blk *cbp, struct mkuz_blkcache_itm *bcep)
{
    void *vbuf;
    ssize_t rlen;
    int rval;

    rval = -1;
    vbuf = malloc(cbp->info.len);
    if (vbuf == NULL) {
        goto e0;
    }
    if (lseek(fd, bcep->hit.offset, SEEK_SET) < 0) {
        goto e1;
    }
    rlen = read(fd, vbuf, cbp->info.len);
    if (rlen < 0 || (unsigned)rlen != cbp->info.len) {
        goto e2;
    }
    rval = (memcmp(cbp->data, vbuf, cbp->info.len) == 0) ? 1 : 0;
e2:
    lseek(fd, cbp->info.offset, SEEK_SET);
e1:
    free(vbuf);
e0:
    return (rval);
}

#define I2J(x)	((intmax_t)(x))
#define U2J(x)	((uintmax_t)(x))

static unsigned char
digest_fold(const unsigned char *mdigest)
{
    int i;
    unsigned char rval;

    rval = mdigest[0];
    for (i = 1; i < 16; i++) {
        rval = rval ^ mdigest[i];
    }
    return (rval);
}

struct mkuz_blk_info *
mkuz_blkcache_regblock(int fd, const struct mkuz_blk *bp)
{
    struct mkuz_blkcache_itm *bcep;
    int rval;
    unsigned char h;

#if defined(MKUZ_DEBUG)
    assert((unsigned)lseek(fd, 0, SEEK_CUR) == bp->info.offset);
#endif
    h = digest_fold(bp->info.digest);
    if (blkcache.first[h].hit.len == 0) {
        bcep = &blkcache.first[h];
    } else {
        for (bcep = &blkcache.first[h]; bcep != NULL; bcep = bcep->next) {
            if (bcep->hit.len != bp->info.len)
                continue;
            if (memcmp(bp->info.digest, bcep->hit.digest,
              sizeof(bp->info.digest)) == 0) {
                break;
            }
        }
        if (bcep != NULL) {
            rval = verify_match(fd, bp, bcep);
            if (rval == 1) {
#if defined(MKUZ_DEBUG)
                fprintf(stderr, "cache hit %jd, %jd, %jd, %jd\n",
                  I2J(bcep->hit.blkno), I2J(bcep->hit.offset),
                  I2J(bp->info.offset), I2J(bp->info.len));
#endif
                return (&bcep->hit);
            }
            if (rval == 0) {
#if defined(MKUZ_DEBUG)
                fprintf(stderr, "block MD5 collision, you should try lottery, "
                  "man!\n");
#endif
                return (NULL);
            }
            warn("verify_match");
            return (NULL);
        }
        bcep = malloc(sizeof(struct mkuz_blkcache_itm));
        if (bcep == NULL)
            return (NULL);
        memset(bcep, '\0', sizeof(struct mkuz_blkcache_itm));
        bcep->next = blkcache.first[h].next;
        blkcache.first[h].next = bcep;
    }
    bcep->hit = bp->info;
    return (NULL);
}
