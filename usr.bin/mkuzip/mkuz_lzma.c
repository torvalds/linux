/*
 * Copyright (c) 2004-2016 Maxim Sobolev <sobomax@FreeBSD.org>
 * Copyright (c) 2011 Aleksandr Rybalko <ray@ddteam.net>
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

#include <sys/param.h>
#include <err.h>
#include <stdint.h>

#include <lzma.h>

#include "mkuzip.h"
#include "mkuz_lzma.h"
#include "mkuz_blk.h"

#define USED_BLOCKSIZE DEV_BSIZE

struct mkuz_lzma {
	lzma_filter filters[2];
	lzma_options_lzma opt_lzma;
	lzma_stream strm;
	uint32_t blksz;
};

static const lzma_stream lzma_stream_init = LZMA_STREAM_INIT;

void *
mkuz_lzma_init(uint32_t blksz)
{
	struct mkuz_lzma *ulp;

	if (blksz % USED_BLOCKSIZE != 0) {
		errx(1, "cluster size should be multiple of %d",
		    USED_BLOCKSIZE);
		/* Not reached */
	}
	if (blksz > MAXPHYS) {
		errx(1, "cluster size is too large");
		/* Not reached */
	}
	ulp = mkuz_safe_zmalloc(sizeof(struct mkuz_lzma));

	/* Init lzma encoder */
	ulp->strm = lzma_stream_init;
	if (lzma_lzma_preset(&ulp->opt_lzma, LZMA_PRESET_DEFAULT))
		errx(1, "Error loading LZMA preset");

	ulp->filters[0].id = LZMA_FILTER_LZMA2;
	ulp->filters[0].options = &ulp->opt_lzma;
	ulp->filters[1].id = LZMA_VLI_UNKNOWN;

	ulp->blksz = blksz;

	return (void *)ulp;
}

struct mkuz_blk *
mkuz_lzma_compress(void *p, const struct mkuz_blk *iblk)
{
	lzma_ret ret;
        struct mkuz_blk *rval;
	struct mkuz_lzma *ulp;

	ulp = (struct mkuz_lzma *)p;

        rval = mkuz_blk_ctor(ulp->blksz * 2);

	ret = lzma_stream_encoder(&ulp->strm, ulp->filters, LZMA_CHECK_CRC32);
	if (ret != LZMA_OK) {
		if (ret == LZMA_MEMLIMIT_ERROR)
			errx(1, "can't compress data: LZMA_MEMLIMIT_ERROR");

		errx(1, "can't compress data: LZMA compressor ERROR");
	}

	ulp->strm.next_in = iblk->data;
	ulp->strm.avail_in = ulp->blksz;
	ulp->strm.next_out = rval->data;
	ulp->strm.avail_out = rval->alen;

	ret = lzma_code(&ulp->strm, LZMA_FINISH);

	if (ret != LZMA_STREAM_END) {
		/* Error */
		errx(1, "lzma_code FINISH failed, code=%d, pos(in=%zd, "
		    "out=%zd)", ret, (ulp->blksz - ulp->strm.avail_in),
		    (ulp->blksz * 2 - ulp->strm.avail_out));
	}

#if 0
	lzma_end(&ulp->strm);
#endif

	rval->info.len = rval->alen - ulp->strm.avail_out;
	return (rval);
}
