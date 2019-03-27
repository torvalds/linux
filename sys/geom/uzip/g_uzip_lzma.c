/*-
 * Copyright (c) 2004 Max Khon
 * Copyright (c) 2014 Juniper Networks, Inc.
 * Copyright (c) 2006-2016 Maxim Sobolev <sobomax@FreeBSD.org>
 * Copyright (c) 2010-2012 Aleksandr Rybalko
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
#include <sys/malloc.h>
#include <sys/systm.h>

#include <contrib/xz-embedded/linux/include/linux/xz.h>

#include <geom/uzip/g_uzip.h>
#include <geom/uzip/g_uzip_dapi.h>
#include <geom/uzip/g_uzip_lzma.h>

struct g_uzip_lzma {
	struct g_uzip_dapi pub;
	uint32_t blksz;
	/* XZ decoder structs */
	struct xz_buf b;
	struct xz_dec *s;
};

static int g_uzip_lzma_nop(struct g_uzip_dapi *, const char *);

static void
g_uzip_lzma_free(struct g_uzip_dapi *lzpp)
{
	struct g_uzip_lzma *lzp;

	lzp = (struct g_uzip_lzma *)lzpp->pvt;
	if (lzp->s != NULL) {
		xz_dec_end(lzp->s);
		lzp->s = NULL;
	}

	free(lzp, M_GEOM_UZIP);
}

static int
g_uzip_lzma_decompress(struct g_uzip_dapi *lzpp, const char *gp_name,
    void *ibp, size_t ilen, void *obp)
{
	struct g_uzip_lzma *lzp;
	int err;

	lzp = (struct g_uzip_lzma *)lzpp->pvt;

	lzp->b.in = ibp;
	lzp->b.out = obp;
	lzp->b.in_pos = lzp->b.out_pos = 0;
	lzp->b.in_size = ilen;
	lzp->b.out_size = lzp->blksz;
	err = (xz_dec_run(lzp->s, &lzp->b) != XZ_STREAM_END) ? 1 : 0;
	/* TODO decoder recovery, if needed */
	if (err != 0) {
		printf("%s: ibp=%p, obp=%p, in_pos=%jd, out_pos=%jd, "
		    "in_size=%jd, out_size=%jd\n", __func__, ibp, obp,
		    (intmax_t)lzp->b.in_pos, (intmax_t)lzp->b.out_pos,
		    (intmax_t)lzp->b.in_size, (intmax_t)lzp->b.out_size);
	}

	return (err);
}

static int
LZ4_compressBound(int isize)
{

        return (isize + (isize / 255) + 16);
}

struct g_uzip_dapi *
g_uzip_lzma_ctor(uint32_t blksz)
{
	struct g_uzip_lzma *lzp;

	lzp = malloc(sizeof(struct g_uzip_lzma), M_GEOM_UZIP, M_WAITOK);
	lzp->s = xz_dec_init(XZ_SINGLE, 0);
	if (lzp->s == NULL) {
		goto e1;
	}
	lzp->blksz = blksz;
	lzp->pub.max_blen = LZ4_compressBound(blksz);
	lzp->pub.decompress = &g_uzip_lzma_decompress;
	lzp->pub.free = &g_uzip_lzma_free;
	lzp->pub.rewind = &g_uzip_lzma_nop;
	lzp->pub.pvt = lzp;
	return (&lzp->pub);
e1:
        free(lzp, M_GEOM_UZIP);
        return (NULL);
}

static int
g_uzip_lzma_nop(struct g_uzip_dapi *zpp, const char *gp_name)
{

        return (0);
}
