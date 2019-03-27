/* $OpenBSD: deflate.c,v 1.3 2001/08/20 02:45:22 hugh Exp $ */

/*-
 * Copyright (c) 2001 Jean-Jacques Bernard-Gundol (jj@wabbitt.org)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains a wrapper around the deflate algo compression
 * functions using the zlib library (see libkern/zlib.c and sys/zlib.h})
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/sdt.h>
#include <sys/systm.h>
#include <sys/zlib.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/deflate.h>

SDT_PROVIDER_DECLARE(opencrypto);
SDT_PROBE_DEFINE2(opencrypto, deflate, deflate_global, entry,
    "int", "u_int32_t");
SDT_PROBE_DEFINE5(opencrypto, deflate, deflate_global, bad,
    "int", "int", "int", "int", "int");
SDT_PROBE_DEFINE5(opencrypto, deflate, deflate_global, iter,
    "int", "int", "int", "int", "int");
SDT_PROBE_DEFINE2(opencrypto, deflate, deflate_global, return,
    "int", "u_int32_t");

int window_inflate = -1 * MAX_WBITS;
int window_deflate = -12;

/*
 * This function takes a block of data and (de)compress it using the deflate
 * algorithm
 */

u_int32_t
deflate_global(data, size, decomp, out)
	u_int8_t *data;
	u_int32_t size;
	int decomp;
	u_int8_t **out;
{
	/* decomp indicates whether we compress (0) or decompress (1) */

	z_stream zbuf;
	u_int8_t *output;
	u_int32_t count, result;
	int error, i;
	struct deflate_buf *bufh, *bufp;

	SDT_PROBE2(opencrypto, deflate, deflate_global, entry, decomp, size);

	bufh = bufp = NULL;
	if (!decomp) {
		i = 1;
	} else {
		/*
	 	 * Choose a buffer with 4x the size of the input buffer
	 	 * for the size of the output buffer in the case of
	 	 * decompression. If it's not sufficient, it will need to be
	 	 * updated while the decompression is going on.
	 	 */
		i = 4;
	}
	/*
	 * Make sure we do have enough output space.  Repeated calls to
	 * deflate need at least 6 bytes of output buffer space to avoid
	 * repeated markers.  We will always provide at least 16 bytes.
	 */
	while ((size * i) < 16)
		i++;

	bufh = bufp = malloc(sizeof(*bufp) + (size_t)(size * i),
	    M_CRYPTO_DATA, M_NOWAIT);
	if (bufp == NULL) {
		SDT_PROBE5(opencrypto, deflate, deflate_global, bad,
		    decomp, 0, __LINE__, 0, 0);
		goto bad2;
	}
	bufp->next = NULL;
	bufp->size = size * i;

	bzero(&zbuf, sizeof(z_stream));
	zbuf.zalloc = z_alloc;
	zbuf.zfree = z_free;
	zbuf.opaque = Z_NULL;
	zbuf.next_in = data;	/* Data that is going to be processed. */
	zbuf.avail_in = size;	/* Total length of data to be processed. */
	zbuf.next_out = bufp->data;
	zbuf.avail_out = bufp->size;

	error = decomp ? inflateInit2(&zbuf, window_inflate) :
	    deflateInit2(&zbuf, Z_DEFAULT_COMPRESSION, Z_METHOD,
		    window_deflate, Z_MEMLEVEL, Z_DEFAULT_STRATEGY);
	if (error != Z_OK) {
		SDT_PROBE5(opencrypto, deflate, deflate_global, bad,
		    decomp, error, __LINE__, 0, 0);
		goto bad;
	}

	for (;;) {
		error = decomp ? inflate(&zbuf, Z_SYNC_FLUSH) :
				 deflate(&zbuf, Z_FINISH);
		if (error != Z_OK && error != Z_STREAM_END) {
			/*
			 * Unfortunately we are limited to 5 arguments,
			 * thus use two probes.
			 */
			SDT_PROBE5(opencrypto, deflate, deflate_global, bad,
			    decomp, error, __LINE__,
			    zbuf.avail_in, zbuf.avail_out);
			SDT_PROBE5(opencrypto, deflate, deflate_global, bad,
			    decomp, error, __LINE__,
			    zbuf.state->dummy, zbuf.total_out);
			goto bad;
		}
		SDT_PROBE5(opencrypto, deflate, deflate_global, iter,
		    decomp, error, __LINE__,
		    zbuf.avail_in, zbuf.avail_out);
		SDT_PROBE5(opencrypto, deflate, deflate_global, iter,
		    decomp, error, __LINE__,
		    zbuf.state->dummy, zbuf.total_out);
		if (decomp && zbuf.avail_in == 0 && error == Z_STREAM_END) {
			/* Done. */
			break;
		} else if (!decomp && error == Z_STREAM_END) {
			/* Done. */
			break;
		} else if (zbuf.avail_out == 0) {
			struct deflate_buf *p;

			/* We need more output space for another iteration. */
			p = malloc(sizeof(*p) + (size_t)(size * i),
			    M_CRYPTO_DATA, M_NOWAIT);
			if (p == NULL) {
				SDT_PROBE5(opencrypto, deflate, deflate_global,
				    bad, decomp, 0, __LINE__, 0, 0);
				goto bad;
			}
			p->next = NULL;
			p->size = size * i;
			bufp->next = p;
			bufp = p;
			zbuf.next_out = bufp->data;
			zbuf.avail_out = bufp->size;
		} else {
			/* Unexpect result. */
			/*
			 * Unfortunately we are limited to 5 arguments,
			 * thus, again, use two probes.
			 */
			SDT_PROBE5(opencrypto, deflate, deflate_global, bad,
			    decomp, error, __LINE__,
			    zbuf.avail_in, zbuf.avail_out);
			SDT_PROBE5(opencrypto, deflate, deflate_global, bad,
			    decomp, error, __LINE__,
			    zbuf.state->dummy, zbuf.total_out);
			goto bad;
		}
	}

	result = count = zbuf.total_out;

	*out = malloc(result, M_CRYPTO_DATA, M_NOWAIT);
	if (*out == NULL) {
		SDT_PROBE5(opencrypto, deflate, deflate_global, bad,
		    decomp, 0, __LINE__, 0, 0);
		goto bad;
	}
	if (decomp)
		inflateEnd(&zbuf);
	else
		deflateEnd(&zbuf);
	output = *out;
	for (bufp = bufh; bufp != NULL; ) {
		if (count > bufp->size) {
			struct deflate_buf *p;

			bcopy(bufp->data, *out, bufp->size);
			*out += bufp->size;
			count -= bufp->size;
			p = bufp;
			bufp = bufp->next;
			free(p, M_CRYPTO_DATA);
		} else {
			/* It should be the last buffer. */
			bcopy(bufp->data, *out, count);
			*out += count;
			free(bufp, M_CRYPTO_DATA);
			bufp = NULL;
			count = 0;
		}
	}
	*out = output;
	SDT_PROBE2(opencrypto, deflate, deflate_global, return, decomp, result);
	return result;

bad:
	if (decomp)
		inflateEnd(&zbuf);
	else
		deflateEnd(&zbuf);
	for (bufp = bufh; bufp != NULL; ) {
		struct deflate_buf *p;

		p = bufp;
		bufp = bufp->next;
		free(p, M_CRYPTO_DATA);
	}
bad2:
	*out = NULL;
	return 0;
}

void *
z_alloc(nil, type, size)
	void *nil;
	u_int type, size;
{
	void *ptr;

	ptr = malloc(type *size, M_CRYPTO_DATA, M_NOWAIT);
	return ptr;
}

void
z_free(nil, ptr)
	void *nil, *ptr;
{
	free(ptr, M_CRYPTO_DATA);
}
