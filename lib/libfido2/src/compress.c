/*
 * Copyright (c) 2020-2022 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <zlib.h>
#include "fido.h"

#define BOUND (1024UL * 1024UL)

/* zlib inflate (raw + headers) */
static int
rfc1950_inflate(fido_blob_t *out, const fido_blob_t *in, size_t origsiz)
{
	u_long ilen, olen;
	int z;

	memset(out, 0, sizeof(*out));

	if (in->len > ULONG_MAX || (ilen = (u_long)in->len) > BOUND ||
	    origsiz > ULONG_MAX || (olen = (u_long)origsiz) > BOUND) {
		fido_log_debug("%s: in->len=%zu, origsiz=%zu", __func__,
		    in->len, origsiz);
		return FIDO_ERR_INVALID_ARGUMENT;
	}

	if ((out->ptr = calloc(1, olen)) == NULL)
		return FIDO_ERR_INTERNAL;
	out->len = olen;

	if ((z = uncompress(out->ptr, &olen, in->ptr, ilen)) != Z_OK ||
	    olen > SIZE_MAX || olen != out->len) {
		fido_log_debug("%s: uncompress: %d, olen=%lu, out->len=%zu",
		    __func__, z, olen, out->len);
		fido_blob_reset(out);
		return FIDO_ERR_COMPRESS;
	}

	return FIDO_OK;
}

/* raw inflate */
static int
rfc1951_inflate(fido_blob_t *out, const fido_blob_t *in, size_t origsiz)
{
	z_stream zs;
	u_int ilen, olen;
	int r, z;

	memset(&zs, 0, sizeof(zs));
	memset(out, 0, sizeof(*out));

	if (in->len > UINT_MAX || (ilen = (u_int)in->len) > BOUND ||
	    origsiz > UINT_MAX || (olen = (u_int)origsiz) > BOUND) {
		fido_log_debug("%s: in->len=%zu, origsiz=%zu", __func__,
		    in->len, origsiz);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	if ((z = inflateInit2(&zs, -MAX_WBITS)) != Z_OK) {
		fido_log_debug("%s: inflateInit2: %d", __func__, z);
		return FIDO_ERR_COMPRESS;
	}

	if ((out->ptr = calloc(1, olen)) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	out->len = olen;
	zs.next_in = in->ptr;
	zs.avail_in = ilen;
	zs.next_out = out->ptr;
	zs.avail_out = olen;

	if ((z = inflate(&zs, Z_FINISH)) != Z_STREAM_END) {
		fido_log_debug("%s: inflate: %d", __func__, z);
		r = FIDO_ERR_COMPRESS;
		goto fail;
	}
	if (zs.avail_out != 0) {
		fido_log_debug("%s: %u != 0", __func__, zs.avail_out);
		r = FIDO_ERR_COMPRESS;
		goto fail;
	}

	r = FIDO_OK;
fail:
	if ((z = inflateEnd(&zs)) != Z_OK) {
		fido_log_debug("%s: inflateEnd: %d", __func__, z);
		r = FIDO_ERR_COMPRESS;
	}
	if (r != FIDO_OK)
		fido_blob_reset(out);

	return r;
}

/* raw deflate */
static int
rfc1951_deflate(fido_blob_t *out, const fido_blob_t *in)
{
	z_stream zs;
	u_int ilen, olen;
	int r, z;

	memset(&zs, 0, sizeof(zs));
	memset(out, 0, sizeof(*out));

	if (in->len > UINT_MAX || (ilen = (u_int)in->len) > BOUND) {
		fido_log_debug("%s: in->len=%zu", __func__, in->len);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	if ((z = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
	    -MAX_WBITS, 8, Z_DEFAULT_STRATEGY)) != Z_OK) {
		fido_log_debug("%s: deflateInit2: %d", __func__, z);
		return FIDO_ERR_COMPRESS;
	}

	olen = BOUND;
	if ((out->ptr = calloc(1, olen)) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	out->len = olen;
	zs.next_in = in->ptr;
	zs.avail_in = ilen;
	zs.next_out = out->ptr;
	zs.avail_out = olen;

	if ((z = deflate(&zs, Z_FINISH)) != Z_STREAM_END) {
		fido_log_debug("%s: inflate: %d", __func__, z);
		r = FIDO_ERR_COMPRESS;
		goto fail;
	}
	if (zs.avail_out >= out->len) {
		fido_log_debug("%s: %u > %zu", __func__, zs.avail_out,
		    out->len);
		r = FIDO_ERR_COMPRESS;
		goto fail;
	}
	out->len -= zs.avail_out;

	r = FIDO_OK;
fail:
	if ((z = deflateEnd(&zs)) != Z_OK) {
		fido_log_debug("%s: deflateEnd: %d", __func__, z);
		r = FIDO_ERR_COMPRESS;
	}
	if (r != FIDO_OK)
		fido_blob_reset(out);

	return r;
}

int
fido_compress(fido_blob_t *out, const fido_blob_t *in)
{
	return rfc1951_deflate(out, in);
}

int
fido_uncompress(fido_blob_t *out, const fido_blob_t *in, size_t origsiz)
{
	if (rfc1950_inflate(out, in, origsiz) == FIDO_OK)
		return FIDO_OK; /* backwards compat with libfido2 < 1.11 */
	return rfc1951_inflate(out, in, origsiz);
}
