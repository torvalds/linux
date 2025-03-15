// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 * Copyright (C) 2006, 2007 University of Szeged, Hungary
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 *          Zoltan Sogor
 */

/*
 * This file provides a single place to access to compression and
 * decompression.
 */

#include <crypto/acompress.h>
#include <linux/highmem.h>
#include "ubifs.h"

/* Fake description object for the "none" compressor */
static struct ubifs_compressor none_compr = {
	.compr_type = UBIFS_COMPR_NONE,
	.name = "none",
	.capi_name = "",
};

#ifdef CONFIG_UBIFS_FS_LZO
static struct ubifs_compressor lzo_compr = {
	.compr_type = UBIFS_COMPR_LZO,
	.name = "lzo",
	.capi_name = "lzo",
};
#else
static struct ubifs_compressor lzo_compr = {
	.compr_type = UBIFS_COMPR_LZO,
	.name = "lzo",
};
#endif

#ifdef CONFIG_UBIFS_FS_ZLIB
static struct ubifs_compressor zlib_compr = {
	.compr_type = UBIFS_COMPR_ZLIB,
	.name = "zlib",
	.capi_name = "deflate",
};
#else
static struct ubifs_compressor zlib_compr = {
	.compr_type = UBIFS_COMPR_ZLIB,
	.name = "zlib",
};
#endif

#ifdef CONFIG_UBIFS_FS_ZSTD
static struct ubifs_compressor zstd_compr = {
	.compr_type = UBIFS_COMPR_ZSTD,
	.name = "zstd",
	.capi_name = "zstd",
};
#else
static struct ubifs_compressor zstd_compr = {
	.compr_type = UBIFS_COMPR_ZSTD,
	.name = "zstd",
};
#endif

/* All UBIFS compressors */
struct ubifs_compressor *ubifs_compressors[UBIFS_COMPR_TYPES_CNT];

static int ubifs_compress_req(const struct ubifs_info *c,
			      struct acomp_req *req,
			      void *out_buf, int *out_len,
			      const char *compr_name)
{
	struct crypto_wait wait;
	int in_len = req->slen;
	int dlen = *out_len;
	int err;

	dlen = min(dlen, in_len - UBIFS_MIN_COMPRESS_DIFF);

	crypto_init_wait(&wait);
	acomp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);
	acomp_request_set_dst_dma(req, out_buf, dlen);
	err = crypto_acomp_compress(req);
	err = crypto_wait_req(err, &wait);
	*out_len = req->dlen;
	acomp_request_free(req);

	return err;
}

/**
 * ubifs_compress - compress data.
 * @c: UBIFS file-system description object
 * @in_buf: data to compress
 * @in_len: length of the data to compress
 * @out_buf: output buffer where compressed data should be stored
 * @out_len: output buffer length is returned here
 * @compr_type: type of compression to use on enter, actually used compression
 *              type on exit
 *
 * This function compresses input buffer @in_buf of length @in_len and stores
 * the result in the output buffer @out_buf and the resulting length in
 * @out_len. If the input buffer does not compress, it is just copied to the
 * @out_buf. The same happens if @compr_type is %UBIFS_COMPR_NONE or if
 * compression error occurred.
 *
 * Note, if the input buffer was not compressed, it is copied to the output
 * buffer and %UBIFS_COMPR_NONE is returned in @compr_type.
 */
void ubifs_compress(const struct ubifs_info *c, const void *in_buf,
		    int in_len, void *out_buf, int *out_len, int *compr_type)
{
	int err;
	struct ubifs_compressor *compr = ubifs_compressors[*compr_type];

	if (*compr_type == UBIFS_COMPR_NONE)
		goto no_compr;

	/* If the input data is small, do not even try to compress it */
	if (in_len < UBIFS_MIN_COMPR_LEN)
		goto no_compr;

	{
		ACOMP_REQUEST_ALLOC(req, compr->cc, GFP_NOFS | __GFP_NOWARN);

		acomp_request_set_src_dma(req, in_buf, in_len);
		err = ubifs_compress_req(c, req, out_buf, out_len, compr->name);
	}

	if (err)
		goto no_compr;

	return;

no_compr:
	memcpy(out_buf, in_buf, in_len);
	*out_len = in_len;
	*compr_type = UBIFS_COMPR_NONE;
}

/**
 * ubifs_compress_folio - compress folio.
 * @c: UBIFS file-system description object
 * @in_folio: data to compress
 * @in_offset: offset into @in_folio
 * @in_len: length of the data to compress
 * @out_buf: output buffer where compressed data should be stored
 * @out_len: output buffer length is returned here
 * @compr_type: type of compression to use on enter, actually used compression
 *              type on exit
 *
 * This function compresses input folio @in_folio of length @in_len and
 * stores the result in the output buffer @out_buf and the resulting length
 * in @out_len. If the input buffer does not compress, it is just copied
 * to the @out_buf. The same happens if @compr_type is %UBIFS_COMPR_NONE
 * or if compression error occurred.
 *
 * Note, if the input buffer was not compressed, it is copied to the output
 * buffer and %UBIFS_COMPR_NONE is returned in @compr_type.
 */
void ubifs_compress_folio(const struct ubifs_info *c, struct folio *in_folio,
			  size_t in_offset, int in_len, void *out_buf,
			  int *out_len, int *compr_type)
{
	int err;
	struct ubifs_compressor *compr = ubifs_compressors[*compr_type];

	if (*compr_type == UBIFS_COMPR_NONE)
		goto no_compr;

	/* If the input data is small, do not even try to compress it */
	if (in_len < UBIFS_MIN_COMPR_LEN)
		goto no_compr;

	{
		ACOMP_REQUEST_ALLOC(req, compr->cc, GFP_NOFS | __GFP_NOWARN);

		acomp_request_set_src_folio(req, in_folio, in_offset, in_len);
		err = ubifs_compress_req(c, req, out_buf, out_len, compr->name);
	}

	if (err)
		goto no_compr;

	return;

no_compr:
	memcpy_from_folio(out_buf, in_folio, in_offset, in_len);
	*out_len = in_len;
	*compr_type = UBIFS_COMPR_NONE;
}

static int ubifs_decompress_req(const struct ubifs_info *c,
				struct acomp_req *req,
				const void *in_buf, int in_len, int *out_len,
				const char *compr_name)
{
	struct crypto_wait wait;
	int err;

	crypto_init_wait(&wait);
	acomp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);
	acomp_request_set_src_dma(req, in_buf, in_len);
	err = crypto_acomp_decompress(req);
	err = crypto_wait_req(err, &wait);
	*out_len = req->dlen;

	if (err)
		ubifs_err(c, "cannot decompress %d bytes, compressor %s, error %d",
			  in_len, compr_name, err);

	acomp_request_free(req);

	return err;
}

/**
 * ubifs_decompress - decompress data.
 * @c: UBIFS file-system description object
 * @in_buf: data to decompress
 * @in_len: length of the data to decompress
 * @out_buf: output buffer where decompressed data should
 * @out_len: output length is returned here
 * @compr_type: type of compression
 *
 * This function decompresses data from buffer @in_buf into buffer @out_buf.
 * The length of the uncompressed data is returned in @out_len. This functions
 * returns %0 on success or a negative error code on failure.
 */
int ubifs_decompress(const struct ubifs_info *c, const void *in_buf,
		     int in_len, void *out_buf, int *out_len, int compr_type)
{
	struct ubifs_compressor *compr;

	if (unlikely(compr_type < 0 || compr_type >= UBIFS_COMPR_TYPES_CNT)) {
		ubifs_err(c, "invalid compression type %d", compr_type);
		return -EINVAL;
	}

	compr = ubifs_compressors[compr_type];

	if (unlikely(!compr->capi_name)) {
		ubifs_err(c, "%s compression is not compiled in", compr->name);
		return -EINVAL;
	}

	if (compr_type == UBIFS_COMPR_NONE) {
		memcpy(out_buf, in_buf, in_len);
		*out_len = in_len;
		return 0;
	}

	{
		ACOMP_REQUEST_ALLOC(req, compr->cc, GFP_NOFS | __GFP_NOWARN);

		acomp_request_set_dst_dma(req, out_buf, *out_len);
		return ubifs_decompress_req(c, req, in_buf, in_len, out_len,
					    compr->name);
	}
}

/**
 * ubifs_decompress_folio - decompress folio.
 * @c: UBIFS file-system description object
 * @in_buf: data to decompress
 * @in_len: length of the data to decompress
 * @out_folio: output folio where decompressed data should
 * @out_offset: offset into @out_folio
 * @out_len: output length is returned here
 * @compr_type: type of compression
 *
 * This function decompresses data from buffer @in_buf into folio
 * @out_folio.  The length of the uncompressed data is returned in
 * @out_len.  This functions returns %0 on success or a negative error
 * code on failure.
 */
int ubifs_decompress_folio(const struct ubifs_info *c, const void *in_buf,
			   int in_len, struct folio *out_folio,
			   size_t out_offset, int *out_len, int compr_type)
{
	struct ubifs_compressor *compr;

	if (unlikely(compr_type < 0 || compr_type >= UBIFS_COMPR_TYPES_CNT)) {
		ubifs_err(c, "invalid compression type %d", compr_type);
		return -EINVAL;
	}

	compr = ubifs_compressors[compr_type];

	if (unlikely(!compr->capi_name)) {
		ubifs_err(c, "%s compression is not compiled in", compr->name);
		return -EINVAL;
	}

	if (compr_type == UBIFS_COMPR_NONE) {
		memcpy_to_folio(out_folio, out_offset, in_buf, in_len);
		*out_len = in_len;
		return 0;
	}

	{
		ACOMP_REQUEST_ALLOC(req, compr->cc, GFP_NOFS | __GFP_NOWARN);

		acomp_request_set_dst_folio(req, out_folio, out_offset,
					    *out_len);
		return ubifs_decompress_req(c, req, in_buf, in_len, out_len,
					    compr->name);
	}
}

/**
 * compr_init - initialize a compressor.
 * @compr: compressor description object
 *
 * This function initializes the requested compressor and returns zero in case
 * of success or a negative error code in case of failure.
 */
static int __init compr_init(struct ubifs_compressor *compr)
{
	if (compr->capi_name) {
		compr->cc = crypto_alloc_acomp(compr->capi_name, 0, 0);
		if (IS_ERR(compr->cc)) {
			pr_err("UBIFS error (pid %d): cannot initialize compressor %s, error %ld",
			       current->pid, compr->name, PTR_ERR(compr->cc));
			return PTR_ERR(compr->cc);
		}
	}

	ubifs_compressors[compr->compr_type] = compr;
	return 0;
}

/**
 * compr_exit - de-initialize a compressor.
 * @compr: compressor description object
 */
static void compr_exit(struct ubifs_compressor *compr)
{
	if (compr->capi_name)
		crypto_free_acomp(compr->cc);
}

/**
 * ubifs_compressors_init - initialize UBIFS compressors.
 *
 * This function initializes the compressor which were compiled in. Returns
 * zero in case of success and a negative error code in case of failure.
 */
int __init ubifs_compressors_init(void)
{
	int err;

	err = compr_init(&lzo_compr);
	if (err)
		return err;

	err = compr_init(&zstd_compr);
	if (err)
		goto out_lzo;

	err = compr_init(&zlib_compr);
	if (err)
		goto out_zstd;

	ubifs_compressors[UBIFS_COMPR_NONE] = &none_compr;
	return 0;

out_zstd:
	compr_exit(&zstd_compr);
out_lzo:
	compr_exit(&lzo_compr);
	return err;
}

/**
 * ubifs_compressors_exit - de-initialize UBIFS compressors.
 */
void ubifs_compressors_exit(void)
{
	compr_exit(&lzo_compr);
	compr_exit(&zlib_compr);
	compr_exit(&zstd_compr);
}
