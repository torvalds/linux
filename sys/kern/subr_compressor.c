/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014, 2017 Mark Johnston <markj@FreeBSD.org>
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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

/*
 * Subroutines used for writing compressed user process and kernel core dumps.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_gzio.h"
#include "opt_zstdio.h"

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/compressor.h>
#include <sys/kernel.h>
#include <sys/linker_set.h>
#include <sys/malloc.h>

MALLOC_DEFINE(M_COMPRESS, "compressor", "kernel compression subroutines");

struct compressor_methods {
	int format;
	void *(* const init)(size_t, int);
	void (* const reset)(void *);
	int (* const write)(void *, void *, size_t, compressor_cb_t, void *);
	void (* const fini)(void *);
};

struct compressor {
	const struct compressor_methods *methods;
	compressor_cb_t cb;
	void *priv;
	void *arg;
};

SET_DECLARE(compressors, struct compressor_methods);

#ifdef GZIO

#include <sys/zutil.h>

struct gz_stream {
	uint8_t		*gz_buffer;	/* output buffer */
	size_t		gz_bufsz;	/* output buffer size */
	off_t		gz_off;		/* offset into the output stream */
	uint32_t	gz_crc;		/* stream CRC32 */
	z_stream	gz_stream;	/* zlib state */
};

static void 	*gz_init(size_t maxiosize, int level);
static void	gz_reset(void *stream);
static int	gz_write(void *stream, void *data, size_t len, compressor_cb_t,
		    void *);
static void	gz_fini(void *stream);

static void *
gz_alloc(void *arg __unused, u_int n, u_int sz)
{

	/*
	 * Memory for zlib state is allocated using M_NODUMP since it may be
	 * used to compress a kernel dump, and we don't want zlib to attempt to
	 * compress its own state.
	 */
	return (malloc(n * sz, M_COMPRESS, M_WAITOK | M_ZERO | M_NODUMP));
}

static void
gz_free(void *arg __unused, void *ptr)
{

	free(ptr, M_COMPRESS);
}

static void *
gz_init(size_t maxiosize, int level)
{
	struct gz_stream *s;
	int error;

	s = gz_alloc(NULL, 1, roundup2(sizeof(*s), PAGE_SIZE));
	s->gz_buffer = gz_alloc(NULL, 1, maxiosize);
	s->gz_bufsz = maxiosize;

	s->gz_stream.zalloc = gz_alloc;
	s->gz_stream.zfree = gz_free;
	s->gz_stream.opaque = NULL;
	s->gz_stream.next_in = Z_NULL;
	s->gz_stream.avail_in = 0;

	error = deflateInit2(&s->gz_stream, level, Z_DEFLATED, -MAX_WBITS,
	    DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	if (error != 0)
		goto fail;

	gz_reset(s);

	return (s);

fail:
	gz_free(NULL, s);
	return (NULL);
}

static void
gz_reset(void *stream)
{
	struct gz_stream *s;
	uint8_t *hdr;
	const size_t hdrlen = 10;

	s = stream;
	s->gz_off = 0;
	s->gz_crc = ~0U;

	(void)deflateReset(&s->gz_stream);
	s->gz_stream.avail_out = s->gz_bufsz;
	s->gz_stream.next_out = s->gz_buffer;

	/* Write the gzip header to the output buffer. */
	hdr = s->gz_buffer;
	memset(hdr, 0, hdrlen);
	hdr[0] = 0x1f;
	hdr[1] = 0x8b;
	hdr[2] = Z_DEFLATED;
	hdr[9] = OS_CODE;
	s->gz_stream.next_out += hdrlen;
	s->gz_stream.avail_out -= hdrlen;
}

static int
gz_write(void *stream, void *data, size_t len, compressor_cb_t cb,
    void *arg)
{
	struct gz_stream *s;
	uint8_t trailer[8];
	size_t room;
	int error, zerror, zflag;

	s = stream;
	zflag = data == NULL ? Z_FINISH : Z_NO_FLUSH;

	if (len > 0) {
		s->gz_stream.avail_in = len;
		s->gz_stream.next_in = data;
		s->gz_crc = crc32_raw(data, len, s->gz_crc);
	} else
		s->gz_crc ^= ~0U;

	error = 0;
	do {
		zerror = deflate(&s->gz_stream, zflag);
		if (zerror != Z_OK && zerror != Z_STREAM_END) {
			error = EIO;
			break;
		}

		if (s->gz_stream.avail_out == 0 || zerror == Z_STREAM_END) {
			/*
			 * Our output buffer is full or there's nothing left
			 * to produce, so we're flushing the buffer.
			 */
			len = s->gz_bufsz - s->gz_stream.avail_out;
			if (zerror == Z_STREAM_END) {
				/*
				 * Try to pack as much of the trailer into the
				 * output buffer as we can.
				 */
				((uint32_t *)trailer)[0] = s->gz_crc;
				((uint32_t *)trailer)[1] =
				    s->gz_stream.total_in;
				room = MIN(sizeof(trailer),
				    s->gz_bufsz - len);
				memcpy(s->gz_buffer + len, trailer, room);
				len += room;
			}

			error = cb(s->gz_buffer, len, s->gz_off, arg);
			if (error != 0)
				break;

			s->gz_off += len;
			s->gz_stream.next_out = s->gz_buffer;
			s->gz_stream.avail_out = s->gz_bufsz;

			/*
			 * If we couldn't pack the trailer into the output
			 * buffer, write it out now.
			 */
			if (zerror == Z_STREAM_END && room < sizeof(trailer))
				error = cb(trailer + room,
				    sizeof(trailer) - room, s->gz_off, arg);
		}
	} while (zerror != Z_STREAM_END &&
	    (zflag == Z_FINISH || s->gz_stream.avail_in > 0));

	return (error);
}

static void
gz_fini(void *stream)
{
	struct gz_stream *s;

	s = stream;
	(void)deflateEnd(&s->gz_stream);
	gz_free(NULL, s->gz_buffer);
	gz_free(NULL, s);
}

struct compressor_methods gzip_methods = {
	.format = COMPRESS_GZIP,
	.init = gz_init,
	.reset = gz_reset,
	.write = gz_write,
	.fini = gz_fini,
};
DATA_SET(compressors, gzip_methods);

#endif /* GZIO */

#ifdef ZSTDIO

#define	ZSTD_STATIC_LINKING_ONLY
#include <contrib/zstd/lib/zstd.h>

struct zstdio_stream {
	ZSTD_CCtx	*zst_stream;
	ZSTD_inBuffer	zst_inbuffer;
	ZSTD_outBuffer	zst_outbuffer;
	uint8_t *	zst_buffer;	/* output buffer */
	size_t		zst_maxiosz;	/* Max output IO size */
	off_t		zst_off;	/* offset into the output stream */
	void *		zst_static_wkspc;
};

static void 	*zstdio_init(size_t maxiosize, int level);
static void	zstdio_reset(void *stream);
static int	zstdio_write(void *stream, void *data, size_t len,
		    compressor_cb_t, void *);
static void	zstdio_fini(void *stream);

static void *
zstdio_init(size_t maxiosize, int level)
{
	ZSTD_CCtx *dump_compressor;
	struct zstdio_stream *s;
	void *wkspc, *owkspc, *buffer;
	size_t wkspc_size, buf_size, rc;

	s = NULL;
	wkspc_size = ZSTD_estimateCStreamSize(level);
	owkspc = wkspc = malloc(wkspc_size + 8, M_COMPRESS,
	    M_WAITOK | M_NODUMP);
	/* Zstd API requires 8-byte alignment. */
	if ((uintptr_t)wkspc % 8 != 0)
		wkspc = (void *)roundup2((uintptr_t)wkspc, 8);

	dump_compressor = ZSTD_initStaticCCtx(wkspc, wkspc_size);
	if (dump_compressor == NULL) {
		printf("%s: workspace too small.\n", __func__);
		goto out;
	}

	rc = ZSTD_CCtx_setParameter(dump_compressor, ZSTD_c_checksumFlag, 1);
	if (ZSTD_isError(rc)) {
		printf("%s: error setting checksumFlag: %s\n", __func__,
		    ZSTD_getErrorName(rc));
		goto out;
	}
	rc = ZSTD_CCtx_setParameter(dump_compressor, ZSTD_c_compressionLevel,
	    level);
	if (ZSTD_isError(rc)) {
		printf("%s: error setting compressLevel: %s\n", __func__,
		    ZSTD_getErrorName(rc));
		goto out;
	}

	buf_size = ZSTD_CStreamOutSize() * 2;
	buffer = malloc(buf_size, M_COMPRESS, M_WAITOK | M_NODUMP);

	s = malloc(sizeof(*s), M_COMPRESS, M_NODUMP | M_WAITOK);
	s->zst_buffer = buffer;
	s->zst_outbuffer.dst = buffer;
	s->zst_outbuffer.size = buf_size;
	s->zst_maxiosz = maxiosize;
	s->zst_stream = dump_compressor;
	s->zst_static_wkspc = owkspc;

	zstdio_reset(s);

out:
	if (s == NULL)
		free(owkspc, M_COMPRESS);
	return (s);
}

static void
zstdio_reset(void *stream)
{
	struct zstdio_stream *s;
	size_t res;

	s = stream;
	res = ZSTD_resetCStream(s->zst_stream, 0);
	if (ZSTD_isError(res))
		panic("%s: could not reset stream %p: %s\n", __func__, s,
		    ZSTD_getErrorName(res));

	s->zst_off = 0;
	s->zst_inbuffer.src = NULL;
	s->zst_inbuffer.size = 0;
	s->zst_inbuffer.pos = 0;
	s->zst_outbuffer.pos = 0;
}

static int
zst_flush_intermediate(struct zstdio_stream *s, compressor_cb_t cb, void *arg)
{
	size_t bytes_to_dump;
	int error;

	/* Flush as many full output blocks as possible. */
	/* XXX: 4096 is arbitrary safe HDD block size for kernel dumps */
	while (s->zst_outbuffer.pos >= 4096) {
		bytes_to_dump = rounddown(s->zst_outbuffer.pos, 4096);

		if (bytes_to_dump > s->zst_maxiosz)
			bytes_to_dump = s->zst_maxiosz;

		error = cb(s->zst_buffer, bytes_to_dump, s->zst_off, arg);
		if (error != 0)
			return (error);

		/*
		 * Shift any non-full blocks up to the front of the output
		 * buffer.
		 */
		s->zst_outbuffer.pos -= bytes_to_dump;
		memmove(s->zst_outbuffer.dst,
		    (char *)s->zst_outbuffer.dst + bytes_to_dump,
		    s->zst_outbuffer.pos);
		s->zst_off += bytes_to_dump;
	}
	return (0);
}

static int
zstdio_flush(struct zstdio_stream *s, compressor_cb_t cb, void *arg)
{
	size_t rc, lastpos;
	int error;

	/*
	 * Positive return indicates unflushed data remaining; need to call
	 * endStream again after clearing out room in output buffer.
	 */
	rc = 1;
	lastpos = s->zst_outbuffer.pos;
	while (rc > 0) {
		rc = ZSTD_endStream(s->zst_stream, &s->zst_outbuffer);
		if (ZSTD_isError(rc)) {
			printf("%s: ZSTD_endStream failed (%s)\n", __func__,
			    ZSTD_getErrorName(rc));
			return (EIO);
		}
		if (lastpos == s->zst_outbuffer.pos) {
			printf("%s: did not make forward progress endStream %zu\n",
			    __func__, lastpos);
			return (EIO);
		}

		error = zst_flush_intermediate(s, cb, arg);
		if (error != 0)
			return (error);

		lastpos = s->zst_outbuffer.pos;
	}

	/*
	 * We've already done an intermediate flush, so all full blocks have
	 * been written.  Only a partial block remains.  Padding happens in a
	 * higher layer.
	 */
	if (s->zst_outbuffer.pos != 0) {
		error = cb(s->zst_buffer, s->zst_outbuffer.pos, s->zst_off,
		    arg);
		if (error != 0)
			return (error);
	}

	return (0);
}

static int
zstdio_write(void *stream, void *data, size_t len, compressor_cb_t cb,
    void *arg)
{
	struct zstdio_stream *s;
	size_t lastpos, rc;
	int error;

	s = stream;
	if (data == NULL)
		return (zstdio_flush(s, cb, arg));

	s->zst_inbuffer.src = data;
	s->zst_inbuffer.size = len;
	s->zst_inbuffer.pos = 0;
	lastpos = 0;

	while (s->zst_inbuffer.pos < s->zst_inbuffer.size) {
		rc = ZSTD_compressStream(s->zst_stream, &s->zst_outbuffer,
		    &s->zst_inbuffer);
		if (ZSTD_isError(rc)) {
			printf("%s: Compress failed on %p! (%s)\n",
			    __func__, data, ZSTD_getErrorName(rc));
			return (EIO);
		}

		if (lastpos == s->zst_inbuffer.pos) {
			/*
			 * XXX: May need flushStream to make forward progress
			 */
			printf("ZSTD: did not make forward progress @pos %zu\n",
			    lastpos);
			return (EIO);
		}
		lastpos = s->zst_inbuffer.pos;

		error = zst_flush_intermediate(s, cb, arg);
		if (error != 0)
			return (error);
	}
	return (0);
}

static void
zstdio_fini(void *stream)
{
	struct zstdio_stream *s;

	s = stream;
	if (s->zst_static_wkspc != NULL)
		free(s->zst_static_wkspc, M_COMPRESS);
	else
		ZSTD_freeCCtx(s->zst_stream);
	free(s->zst_buffer, M_COMPRESS);
	free(s, M_COMPRESS);
}

static struct compressor_methods zstd_methods = {
	.format = COMPRESS_ZSTD,
	.init = zstdio_init,
	.reset = zstdio_reset,
	.write = zstdio_write,
	.fini = zstdio_fini,
};
DATA_SET(compressors, zstd_methods);

#endif /* ZSTDIO */

bool
compressor_avail(int format)
{
	struct compressor_methods **iter;

	SET_FOREACH(iter, compressors) {
		if ((*iter)->format == format)
			return (true);
	}
	return (false);
}

struct compressor *
compressor_init(compressor_cb_t cb, int format, size_t maxiosize, int level,
    void *arg)
{
	struct compressor_methods **iter;
	struct compressor *s;
	void *priv;

	SET_FOREACH(iter, compressors) {
		if ((*iter)->format == format)
			break;
	}
	if (iter == SET_LIMIT(compressors))
		return (NULL);

	priv = (*iter)->init(maxiosize, level);
	if (priv == NULL)
		return (NULL);

	s = malloc(sizeof(*s), M_COMPRESS, M_WAITOK | M_ZERO);
	s->methods = (*iter);
	s->priv = priv;
	s->cb = cb;
	s->arg = arg;
	return (s);
}

void
compressor_reset(struct compressor *stream)
{

	stream->methods->reset(stream->priv);
}

int
compressor_write(struct compressor *stream, void *data, size_t len)
{

	return (stream->methods->write(stream->priv, data, len, stream->cb,
	    stream->arg));
}

int
compressor_flush(struct compressor *stream)
{

	return (stream->methods->write(stream->priv, NULL, 0, stream->cb,
	    stream->arg));
}

void
compressor_fini(struct compressor *stream)
{

	stream->methods->fini(stream->priv);
}
