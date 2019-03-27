/*-
 * Copyright (c) 2004 Max Khon
 * Copyright (c) 2014 Juniper Networks, Inc.
 * Copyright (c) 2006-2016 Maxim Sobolev <sobomax@FreeBSD.org>
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
#include <sys/systm.h>
#include <sys/malloc.h>

#include <sys/zlib.h>

#include <geom/uzip/g_uzip.h>
#include <geom/uzip/g_uzip_dapi.h>
#include <geom/uzip/g_uzip_zlib.h>

struct g_uzip_zlib {
	uint32_t blksz;
	struct g_uzip_dapi pub;
	/* Zlib decoder structs */
	z_stream zs;
};

static void *z_alloc(void *, u_int, u_int);
static void z_free(void *, void *);
static int g_uzip_zlib_rewind(struct g_uzip_dapi *, const char *);

static void
g_uzip_zlib_free(struct g_uzip_dapi *zpp)
{
	struct g_uzip_zlib *zp;

	zp = (struct g_uzip_zlib *)zpp->pvt;
	inflateEnd(&zp->zs);
	free(zp, M_GEOM_UZIP);
}

static int
g_uzip_zlib_decompress(struct g_uzip_dapi *zpp, const char *gp_name, void *ibp,
    size_t ilen, void *obp)
{
	int err;
	struct g_uzip_zlib *zp;

	zp = (struct g_uzip_zlib *)zpp->pvt;

	zp->zs.next_in = ibp;
	zp->zs.avail_in = ilen;
	zp->zs.next_out = obp;
	zp->zs.avail_out = zp->blksz;

	err = (inflate(&zp->zs, Z_FINISH) != Z_STREAM_END) ? 1 : 0;
	if (err != 0) {
		printf("%s: UZIP(zlib) inflate() failed\n", gp_name);
	}
	return (err);
}

static int
g_uzip_zlib_rewind(struct g_uzip_dapi *zpp, const char *gp_name)
{
	int err;
	struct g_uzip_zlib *zp;

	zp = (struct g_uzip_zlib *)zpp->pvt;

	err = 0;
	if (inflateReset(&zp->zs) != Z_OK) {
		printf("%s: UZIP(zlib) decoder reset failed\n", gp_name);
		err = 1;
	}
	return (err);
}

static int
z_compressBound(int len)
{

	return (len + (len >> 12) + (len >> 14) + 11);
}

struct g_uzip_dapi *
g_uzip_zlib_ctor(uint32_t blksz)
{
	struct g_uzip_zlib *zp;

	zp = malloc(sizeof(struct g_uzip_zlib), M_GEOM_UZIP, M_WAITOK);
	zp->zs.zalloc = z_alloc;
	zp->zs.zfree = z_free;
	if (inflateInit(&zp->zs) != Z_OK) {
		goto e1;
	}
	zp->blksz = blksz;
	zp->pub.max_blen = z_compressBound(blksz);
	zp->pub.decompress = &g_uzip_zlib_decompress;
	zp->pub.free = &g_uzip_zlib_free;
	zp->pub.rewind = &g_uzip_zlib_rewind;
	zp->pub.pvt = (void *)zp;
	return (&zp->pub);
e1:
	free(zp, M_GEOM_UZIP);
	return (NULL);
}

static void *
z_alloc(void *nil, u_int type, u_int size)
{
        void *ptr;

        ptr = malloc(type * size, M_GEOM_UZIP, M_NOWAIT);

        return (ptr);
}

static void
z_free(void *nil, void *ptr)
{

        free(ptr, M_GEOM_UZIP);
}
