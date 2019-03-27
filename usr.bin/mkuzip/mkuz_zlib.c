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

#include <sys/param.h>
#include <err.h>
#include <stdint.h>

#include <zlib.h>

#include "mkuzip.h"
#include "mkuz_zlib.h"
#include "mkuz_blk.h"

struct mkuz_zlib {
	uLongf oblen;
	uint32_t blksz;
};

void *
mkuz_zlib_init(uint32_t blksz)
{
	struct mkuz_zlib *zp;

	if (blksz % DEV_BSIZE != 0) {
		errx(1, "cluster size should be multiple of %d",
		    DEV_BSIZE);
		/* Not reached */
	}
	if (compressBound(blksz) > MAXPHYS) {
		errx(1, "cluster size is too large");
		/* Not reached */
	}
	zp = mkuz_safe_zmalloc(sizeof(struct mkuz_zlib));
	zp->oblen = compressBound(blksz);
	zp->blksz = blksz;

	return (void *)zp;
}

struct mkuz_blk *
mkuz_zlib_compress(void *p, const struct mkuz_blk *iblk)
{
	uLongf destlen_z;
	struct mkuz_blk *rval;
	struct mkuz_zlib *zp;

	zp = (struct mkuz_zlib *)p;

	rval = mkuz_blk_ctor(zp->oblen);

	destlen_z = rval->alen;
	if (compress2(rval->data, &destlen_z, iblk->data, zp->blksz,
	    Z_BEST_COMPRESSION) != Z_OK) {
		errx(1, "can't compress data: compress2() "
		    "failed");
		/* Not reached */
	}

	rval->info.len = (uint32_t)destlen_z;
	return (rval);
}
