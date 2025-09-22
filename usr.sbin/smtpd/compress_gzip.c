/*	$OpenBSD: compress_gzip.c,v 1.13 2021/06/14 17:58:15 eric Exp $	*/

/*
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2012 Charles Longeau <chl@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <zlib.h>

#include "smtpd.h"

#define	GZIP_BUFFER_SIZE	16384


static size_t	compress_gzip_chunk(void *, size_t, void *, size_t);
static size_t	uncompress_gzip_chunk(void *, size_t, void *, size_t);
static int	compress_gzip_file(FILE *, FILE *);
static int	uncompress_gzip_file(FILE *, FILE *);


struct compress_backend	compress_gzip = {
	compress_gzip_chunk,
	uncompress_gzip_chunk,

	compress_gzip_file,
	uncompress_gzip_file,
};

static size_t
compress_gzip_chunk(void *ib, size_t ibsz, void *ob, size_t obsz)
{
	z_stream       *strm;
	size_t		ret = 0;

	if ((strm = calloc(1, sizeof *strm)) == NULL)
		return 0;

	strm->zalloc = Z_NULL;
	strm->zfree = Z_NULL;
	strm->opaque = Z_NULL;
	if (deflateInit2(strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
		(15+16), 8, Z_DEFAULT_STRATEGY) != Z_OK)
		goto end;

	strm->avail_in  = ibsz;
	strm->next_in   = (unsigned char *)ib;
	strm->avail_out = obsz;
	strm->next_out  = (unsigned char *)ob;
	if (deflate(strm, Z_FINISH) != Z_STREAM_END)
		goto end;

	ret = strm->total_out;

end:
	deflateEnd(strm);
	free(strm);
	return ret;
}


static size_t
uncompress_gzip_chunk(void *ib, size_t ibsz, void *ob, size_t obsz)
{
	z_stream       *strm;
	size_t		ret = 0;

	if ((strm = calloc(1, sizeof *strm)) == NULL)
		return 0;

	strm->zalloc   = Z_NULL;
	strm->zfree    = Z_NULL;
	strm->opaque   = Z_NULL;
	strm->avail_in = 0;
	strm->next_in  = Z_NULL;

	if (inflateInit2(strm, (15+16)) != Z_OK)
		goto end;

	strm->avail_in  = ibsz;
	strm->next_in   = (unsigned char *)ib;
	strm->avail_out = obsz;
	strm->next_out  = (unsigned char *)ob;

	if (inflate(strm, Z_FINISH) != Z_STREAM_END)
		goto end;

	ret = strm->total_out;

end:
	deflateEnd(strm);
	free(strm);
	return ret;
}


static int
compress_gzip_file(FILE *in, FILE *out)
{
	gzFile  gzf;
	char  ibuf[GZIP_BUFFER_SIZE];
	int  r;
	int  ret = 0;

	if (in == NULL || out == NULL)
		return (0);

	gzf = gzdopen(fileno(out), "wb");
	if (gzf == NULL)
		return (0);

	while ((r = fread(ibuf, 1, GZIP_BUFFER_SIZE, in)) != 0) {
		if (gzwrite(gzf, ibuf, r) != r)
			goto end;
	}
	if (!feof(in))
		goto end;

	ret = 1;

end:
	gzclose(gzf);
	return (ret);
}


static int
uncompress_gzip_file(FILE *in, FILE *out)
{
	gzFile  gzf;
	char  obuf[GZIP_BUFFER_SIZE];
	int  r;
	int  ret = 0;

	if (in == NULL || out == NULL)
		return (0);

	gzf = gzdopen(fileno(in), "r");
	if (gzf == NULL)
		return (0);

	while ((r = gzread(gzf, obuf, sizeof(obuf))) > 0) {
		if  (fwrite(obuf, r, 1, out) != 1)
			goto end;
	}
	if (!gzeof(gzf))
		goto end;

	ret = 1;

end:
	gzclose(gzf);
	return (ret);
}
