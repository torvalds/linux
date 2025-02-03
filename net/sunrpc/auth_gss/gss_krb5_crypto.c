/*
 *  linux/net/sunrpc/gss_krb5_crypto.c
 *
 *  Copyright (c) 2000-2008 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 *  Bruce Fields   <bfields@umich.edu>
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <crypto/utils.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/random.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>
#include <kunit/visibility.h>

#include "gss_krb5_internal.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

/**
 * krb5_make_confounder - Generate a confounder string
 * @p: memory location into which to write the string
 * @conflen: string length to write, in octets
 *
 * RFCs 1964 and 3961 mention only "a random confounder" without going
 * into detail about its function or cryptographic requirements. The
 * assumed purpose is to prevent repeated encryption of a plaintext with
 * the same key from generating the same ciphertext. It is also used to
 * pad minimum plaintext length to at least a single cipher block.
 *
 * However, in situations like the GSS Kerberos 5 mechanism, where the
 * encryption IV is always all zeroes, the confounder also effectively
 * functions like an IV. Thus, not only must it be unique from message
 * to message, but it must also be difficult to predict. Otherwise an
 * attacker can correlate the confounder to previous or future values,
 * making the encryption easier to break.
 *
 * Given that the primary consumer of this encryption mechanism is a
 * network storage protocol, a type of traffic that often carries
 * predictable payloads (eg, all zeroes when reading unallocated blocks
 * from a file), our confounder generation has to be cryptographically
 * strong.
 */
void krb5_make_confounder(u8 *p, int conflen)
{
	get_random_bytes(p, conflen);
}

/**
 * krb5_encrypt - simple encryption of an RPCSEC GSS payload
 * @tfm: initialized cipher transform
 * @iv: pointer to an IV
 * @in: plaintext to encrypt
 * @out: OUT: ciphertext
 * @length: length of input and output buffers, in bytes
 *
 * @iv may be NULL to force the use of an all-zero IV.
 * The buffer containing the IV must be as large as the
 * cipher's ivsize.
 *
 * Return values:
 *   %0: @in successfully encrypted into @out
 *   negative errno: @in not encrypted
 */
u32
krb5_encrypt(
	struct crypto_sync_skcipher *tfm,
	void * iv,
	void * in,
	void * out,
	int length)
{
	u32 ret = -EINVAL;
	struct scatterlist sg[1];
	u8 local_iv[GSS_KRB5_MAX_BLOCKSIZE] = {0};
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, tfm);

	if (length % crypto_sync_skcipher_blocksize(tfm) != 0)
		goto out;

	if (crypto_sync_skcipher_ivsize(tfm) > GSS_KRB5_MAX_BLOCKSIZE) {
		dprintk("RPC:       gss_k5encrypt: tfm iv size too large %d\n",
			crypto_sync_skcipher_ivsize(tfm));
		goto out;
	}

	if (iv)
		memcpy(local_iv, iv, crypto_sync_skcipher_ivsize(tfm));

	memcpy(out, in, length);
	sg_init_one(sg, out, length);

	skcipher_request_set_sync_tfm(req, tfm);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg, sg, length, local_iv);

	ret = crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);
out:
	dprintk("RPC:       krb5_encrypt returns %d\n", ret);
	return ret;
}

static int
checksummer(struct scatterlist *sg, void *data)
{
	struct ahash_request *req = data;

	ahash_request_set_crypt(req, sg, NULL, sg->length);

	return crypto_ahash_update(req);
}

/**
 * gss_krb5_checksum - Compute the MAC for a GSS Wrap or MIC token
 * @tfm: an initialized hash transform
 * @header: pointer to a buffer containing the token header, or NULL
 * @hdrlen: number of octets in @header
 * @body: xdr_buf containing an RPC message (body.len is the message length)
 * @body_offset: byte offset into @body to start checksumming
 * @cksumout: OUT: a buffer to be filled in with the computed HMAC
 *
 * Usually expressed as H = HMAC(K, message)[1..h] .
 *
 * Caller provides the truncation length of the output token (h) in
 * cksumout.len.
 *
 * Return values:
 *   %GSS_S_COMPLETE: Digest computed, @cksumout filled in
 *   %GSS_S_FAILURE: Call failed
 */
u32
gss_krb5_checksum(struct crypto_ahash *tfm, char *header, int hdrlen,
		  const struct xdr_buf *body, int body_offset,
		  struct xdr_netobj *cksumout)
{
	struct ahash_request *req;
	int err = -ENOMEM;
	u8 *checksumdata;

	checksumdata = kmalloc(crypto_ahash_digestsize(tfm), GFP_KERNEL);
	if (!checksumdata)
		return GSS_S_FAILURE;

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto out_free_cksum;
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP, NULL, NULL);
	err = crypto_ahash_init(req);
	if (err)
		goto out_free_ahash;

	/*
	 * Per RFC 4121 Section 4.2.4, the checksum is performed over the
	 * data body first, then over the octets in "header".
	 */
	err = xdr_process_buf(body, body_offset, body->len - body_offset,
			      checksummer, req);
	if (err)
		goto out_free_ahash;
	if (header) {
		struct scatterlist sg[1];

		sg_init_one(sg, header, hdrlen);
		ahash_request_set_crypt(req, sg, NULL, hdrlen);
		err = crypto_ahash_update(req);
		if (err)
			goto out_free_ahash;
	}

	ahash_request_set_crypt(req, NULL, checksumdata, 0);
	err = crypto_ahash_final(req);
	if (err)
		goto out_free_ahash;

	memcpy(cksumout->data, checksumdata,
	       min_t(int, cksumout->len, crypto_ahash_digestsize(tfm)));

out_free_ahash:
	ahash_request_free(req);
out_free_cksum:
	kfree_sensitive(checksumdata);
	return err ? GSS_S_FAILURE : GSS_S_COMPLETE;
}
EXPORT_SYMBOL_IF_KUNIT(gss_krb5_checksum);

struct encryptor_desc {
	u8 iv[GSS_KRB5_MAX_BLOCKSIZE];
	struct skcipher_request *req;
	int pos;
	struct xdr_buf *outbuf;
	struct page **pages;
	struct scatterlist infrags[4];
	struct scatterlist outfrags[4];
	int fragno;
	int fraglen;
};

static int
encryptor(struct scatterlist *sg, void *data)
{
	struct encryptor_desc *desc = data;
	struct xdr_buf *outbuf = desc->outbuf;
	struct crypto_sync_skcipher *tfm =
		crypto_sync_skcipher_reqtfm(desc->req);
	struct page *in_page;
	int thislen = desc->fraglen + sg->length;
	int fraglen, ret;
	int page_pos;

	/* Worst case is 4 fragments: head, end of page 1, start
	 * of page 2, tail.  Anything more is a bug. */
	BUG_ON(desc->fragno > 3);

	page_pos = desc->pos - outbuf->head[0].iov_len;
	if (page_pos >= 0 && page_pos < outbuf->page_len) {
		/* pages are not in place: */
		int i = (page_pos + outbuf->page_base) >> PAGE_SHIFT;
		in_page = desc->pages[i];
	} else {
		in_page = sg_page(sg);
	}
	sg_set_page(&desc->infrags[desc->fragno], in_page, sg->length,
		    sg->offset);
	sg_set_page(&desc->outfrags[desc->fragno], sg_page(sg), sg->length,
		    sg->offset);
	desc->fragno++;
	desc->fraglen += sg->length;
	desc->pos += sg->length;

	fraglen = thislen & (crypto_sync_skcipher_blocksize(tfm) - 1);
	thislen -= fraglen;

	if (thislen == 0)
		return 0;

	sg_mark_end(&desc->infrags[desc->fragno - 1]);
	sg_mark_end(&desc->outfrags[desc->fragno - 1]);

	skcipher_request_set_crypt(desc->req, desc->infrags, desc->outfrags,
				   thislen, desc->iv);

	ret = crypto_skcipher_encrypt(desc->req);
	if (ret)
		return ret;

	sg_init_table(desc->infrags, 4);
	sg_init_table(desc->outfrags, 4);

	if (fraglen) {
		sg_set_page(&desc->outfrags[0], sg_page(sg), fraglen,
				sg->offset + sg->length - fraglen);
		desc->infrags[0] = desc->outfrags[0];
		sg_assign_page(&desc->infrags[0], in_page);
		desc->fragno = 1;
		desc->fraglen = fraglen;
	} else {
		desc->fragno = 0;
		desc->fraglen = 0;
	}
	return 0;
}

struct decryptor_desc {
	u8 iv[GSS_KRB5_MAX_BLOCKSIZE];
	struct skcipher_request *req;
	struct scatterlist frags[4];
	int fragno;
	int fraglen;
};

static int
decryptor(struct scatterlist *sg, void *data)
{
	struct decryptor_desc *desc = data;
	int thislen = desc->fraglen + sg->length;
	struct crypto_sync_skcipher *tfm =
		crypto_sync_skcipher_reqtfm(desc->req);
	int fraglen, ret;

	/* Worst case is 4 fragments: head, end of page 1, start
	 * of page 2, tail.  Anything more is a bug. */
	BUG_ON(desc->fragno > 3);
	sg_set_page(&desc->frags[desc->fragno], sg_page(sg), sg->length,
		    sg->offset);
	desc->fragno++;
	desc->fraglen += sg->length;

	fraglen = thislen & (crypto_sync_skcipher_blocksize(tfm) - 1);
	thislen -= fraglen;

	if (thislen == 0)
		return 0;

	sg_mark_end(&desc->frags[desc->fragno - 1]);

	skcipher_request_set_crypt(desc->req, desc->frags, desc->frags,
				   thislen, desc->iv);

	ret = crypto_skcipher_decrypt(desc->req);
	if (ret)
		return ret;

	sg_init_table(desc->frags, 4);

	if (fraglen) {
		sg_set_page(&desc->frags[0], sg_page(sg), fraglen,
				sg->offset + sg->length - fraglen);
		desc->fragno = 1;
		desc->fraglen = fraglen;
	} else {
		desc->fragno = 0;
		desc->fraglen = 0;
	}
	return 0;
}

/*
 * This function makes the assumption that it was ultimately called
 * from gss_wrap().
 *
 * The client auth_gss code moves any existing tail data into a
 * separate page before calling gss_wrap.
 * The server svcauth_gss code ensures that both the head and the
 * tail have slack space of RPC_MAX_AUTH_SIZE before calling gss_wrap.
 *
 * Even with that guarantee, this function may be called more than
 * once in the processing of gss_wrap().  The best we can do is
 * verify at compile-time (see GSS_KRB5_SLACK_CHECK) that the
 * largest expected shift will fit within RPC_MAX_AUTH_SIZE.
 * At run-time we can verify that a single invocation of this
 * function doesn't attempt to use more the RPC_MAX_AUTH_SIZE.
 */

int
xdr_extend_head(struct xdr_buf *buf, unsigned int base, unsigned int shiftlen)
{
	u8 *p;

	if (shiftlen == 0)
		return 0;

	BUG_ON(shiftlen > RPC_MAX_AUTH_SIZE);

	p = buf->head[0].iov_base + base;

	memmove(p + shiftlen, p, buf->head[0].iov_len - base);

	buf->head[0].iov_len += shiftlen;
	buf->len += shiftlen;

	return 0;
}

static u32
gss_krb5_cts_crypt(struct crypto_sync_skcipher *cipher, struct xdr_buf *buf,
		   u32 offset, u8 *iv, struct page **pages, int encrypt)
{
	u32 ret;
	struct scatterlist sg[1];
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, cipher);
	u8 *data;
	struct page **save_pages;
	u32 len = buf->len - offset;

	if (len > GSS_KRB5_MAX_BLOCKSIZE * 2) {
		WARN_ON(0);
		return -ENOMEM;
	}
	data = kmalloc(GSS_KRB5_MAX_BLOCKSIZE * 2, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/*
	 * For encryption, we want to read from the cleartext
	 * page cache pages, and write the encrypted data to
	 * the supplied xdr_buf pages.
	 */
	save_pages = buf->pages;
	if (encrypt)
		buf->pages = pages;

	ret = read_bytes_from_xdr_buf(buf, offset, data, len);
	buf->pages = save_pages;
	if (ret)
		goto out;

	sg_init_one(sg, data, len);

	skcipher_request_set_sync_tfm(req, cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg, sg, len, iv);

	if (encrypt)
		ret = crypto_skcipher_encrypt(req);
	else
		ret = crypto_skcipher_decrypt(req);

	skcipher_request_zero(req);

	if (ret)
		goto out;

	ret = write_bytes_to_xdr_buf(buf, offset, data, len);

#if IS_ENABLED(CONFIG_KUNIT)
	/*
	 * CBC-CTS does not define an output IV but RFC 3962 defines it as the
	 * penultimate block of ciphertext, so copy that into the IV buffer
	 * before returning.
	 */
	if (encrypt)
		memcpy(iv, data, crypto_sync_skcipher_ivsize(cipher));
#endif

out:
	kfree(data);
	return ret;
}

/**
 * krb5_cbc_cts_encrypt - encrypt in CBC mode with CTS
 * @cts_tfm: CBC cipher with CTS
 * @cbc_tfm: base CBC cipher
 * @offset: starting byte offset for plaintext
 * @buf: OUT: output buffer
 * @pages: plaintext
 * @iv: output CBC initialization vector, or NULL
 * @ivsize: size of @iv, in octets
 *
 * To provide confidentiality, encrypt using cipher block chaining
 * with ciphertext stealing. Message integrity is handled separately.
 *
 * Return values:
 *   %0: encryption successful
 *   negative errno: encryption could not be completed
 */
VISIBLE_IF_KUNIT
int krb5_cbc_cts_encrypt(struct crypto_sync_skcipher *cts_tfm,
			 struct crypto_sync_skcipher *cbc_tfm,
			 u32 offset, struct xdr_buf *buf, struct page **pages,
			 u8 *iv, unsigned int ivsize)
{
	u32 blocksize, nbytes, nblocks, cbcbytes;
	struct encryptor_desc desc;
	int err;

	blocksize = crypto_sync_skcipher_blocksize(cts_tfm);
	nbytes = buf->len - offset;
	nblocks = (nbytes + blocksize - 1) / blocksize;
	cbcbytes = 0;
	if (nblocks > 2)
		cbcbytes = (nblocks - 2) * blocksize;

	memset(desc.iv, 0, sizeof(desc.iv));

	/* Handle block-sized chunks of plaintext with CBC. */
	if (cbcbytes) {
		SYNC_SKCIPHER_REQUEST_ON_STACK(req, cbc_tfm);

		desc.pos = offset;
		desc.fragno = 0;
		desc.fraglen = 0;
		desc.pages = pages;
		desc.outbuf = buf;
		desc.req = req;

		skcipher_request_set_sync_tfm(req, cbc_tfm);
		skcipher_request_set_callback(req, 0, NULL, NULL);

		sg_init_table(desc.infrags, 4);
		sg_init_table(desc.outfrags, 4);

		err = xdr_process_buf(buf, offset, cbcbytes, encryptor, &desc);
		skcipher_request_zero(req);
		if (err)
			return err;
	}

	/* Remaining plaintext is handled with CBC-CTS. */
	err = gss_krb5_cts_crypt(cts_tfm, buf, offset + cbcbytes,
				 desc.iv, pages, 1);
	if (err)
		return err;

	if (unlikely(iv))
		memcpy(iv, desc.iv, ivsize);
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(krb5_cbc_cts_encrypt);

/**
 * krb5_cbc_cts_decrypt - decrypt in CBC mode with CTS
 * @cts_tfm: CBC cipher with CTS
 * @cbc_tfm: base CBC cipher
 * @offset: starting byte offset for plaintext
 * @buf: OUT: output buffer
 *
 * Return values:
 *   %0: decryption successful
 *   negative errno: decryption could not be completed
 */
VISIBLE_IF_KUNIT
int krb5_cbc_cts_decrypt(struct crypto_sync_skcipher *cts_tfm,
			 struct crypto_sync_skcipher *cbc_tfm,
			 u32 offset, struct xdr_buf *buf)
{
	u32 blocksize, nblocks, cbcbytes;
	struct decryptor_desc desc;
	int err;

	blocksize = crypto_sync_skcipher_blocksize(cts_tfm);
	nblocks = (buf->len + blocksize - 1) / blocksize;
	cbcbytes = 0;
	if (nblocks > 2)
		cbcbytes = (nblocks - 2) * blocksize;

	memset(desc.iv, 0, sizeof(desc.iv));

	/* Handle block-sized chunks of plaintext with CBC. */
	if (cbcbytes) {
		SYNC_SKCIPHER_REQUEST_ON_STACK(req, cbc_tfm);

		desc.fragno = 0;
		desc.fraglen = 0;
		desc.req = req;

		skcipher_request_set_sync_tfm(req, cbc_tfm);
		skcipher_request_set_callback(req, 0, NULL, NULL);

		sg_init_table(desc.frags, 4);

		err = xdr_process_buf(buf, 0, cbcbytes, decryptor, &desc);
		skcipher_request_zero(req);
		if (err)
			return err;
	}

	/* Remaining plaintext is handled with CBC-CTS. */
	return gss_krb5_cts_crypt(cts_tfm, buf, cbcbytes, desc.iv, NULL, 0);
}
EXPORT_SYMBOL_IF_KUNIT(krb5_cbc_cts_decrypt);

u32
gss_krb5_aes_encrypt(struct krb5_ctx *kctx, u32 offset,
		     struct xdr_buf *buf, struct page **pages)
{
	u32 err;
	struct xdr_netobj hmac;
	u8 *ecptr;
	struct crypto_sync_skcipher *cipher, *aux_cipher;
	struct crypto_ahash *ahash;
	struct page **save_pages;
	unsigned int conflen;

	if (kctx->initiate) {
		cipher = kctx->initiator_enc;
		aux_cipher = kctx->initiator_enc_aux;
		ahash = kctx->initiator_integ;
	} else {
		cipher = kctx->acceptor_enc;
		aux_cipher = kctx->acceptor_enc_aux;
		ahash = kctx->acceptor_integ;
	}
	conflen = crypto_sync_skcipher_blocksize(cipher);

	/* hide the gss token header and insert the confounder */
	offset += GSS_KRB5_TOK_HDR_LEN;
	if (xdr_extend_head(buf, offset, conflen))
		return GSS_S_FAILURE;
	krb5_make_confounder(buf->head[0].iov_base + offset, conflen);
	offset -= GSS_KRB5_TOK_HDR_LEN;

	if (buf->tail[0].iov_base != NULL) {
		ecptr = buf->tail[0].iov_base + buf->tail[0].iov_len;
	} else {
		buf->tail[0].iov_base = buf->head[0].iov_base
							+ buf->head[0].iov_len;
		buf->tail[0].iov_len = 0;
		ecptr = buf->tail[0].iov_base;
	}

	/* copy plaintext gss token header after filler (if any) */
	memcpy(ecptr, buf->head[0].iov_base + offset, GSS_KRB5_TOK_HDR_LEN);
	buf->tail[0].iov_len += GSS_KRB5_TOK_HDR_LEN;
	buf->len += GSS_KRB5_TOK_HDR_LEN;

	hmac.len = kctx->gk5e->cksumlength;
	hmac.data = buf->tail[0].iov_base + buf->tail[0].iov_len;

	/*
	 * When we are called, pages points to the real page cache
	 * data -- which we can't go and encrypt!  buf->pages points
	 * to scratch pages which we are going to send off to the
	 * client/server.  Swap in the plaintext pages to calculate
	 * the hmac.
	 */
	save_pages = buf->pages;
	buf->pages = pages;

	err = gss_krb5_checksum(ahash, NULL, 0, buf,
				offset + GSS_KRB5_TOK_HDR_LEN, &hmac);
	buf->pages = save_pages;
	if (err)
		return GSS_S_FAILURE;

	err = krb5_cbc_cts_encrypt(cipher, aux_cipher,
				   offset + GSS_KRB5_TOK_HDR_LEN,
				   buf, pages, NULL, 0);
	if (err)
		return GSS_S_FAILURE;

	/* Now update buf to account for HMAC */
	buf->tail[0].iov_len += kctx->gk5e->cksumlength;
	buf->len += kctx->gk5e->cksumlength;

	return GSS_S_COMPLETE;
}

u32
gss_krb5_aes_decrypt(struct krb5_ctx *kctx, u32 offset, u32 len,
		     struct xdr_buf *buf, u32 *headskip, u32 *tailskip)
{
	struct crypto_sync_skcipher *cipher, *aux_cipher;
	struct crypto_ahash *ahash;
	struct xdr_netobj our_hmac_obj;
	u8 our_hmac[GSS_KRB5_MAX_CKSUM_LEN];
	u8 pkt_hmac[GSS_KRB5_MAX_CKSUM_LEN];
	struct xdr_buf subbuf;
	u32 ret = 0;

	if (kctx->initiate) {
		cipher = kctx->acceptor_enc;
		aux_cipher = kctx->acceptor_enc_aux;
		ahash = kctx->acceptor_integ;
	} else {
		cipher = kctx->initiator_enc;
		aux_cipher = kctx->initiator_enc_aux;
		ahash = kctx->initiator_integ;
	}

	/* create a segment skipping the header and leaving out the checksum */
	xdr_buf_subsegment(buf, &subbuf, offset + GSS_KRB5_TOK_HDR_LEN,
				    (len - offset - GSS_KRB5_TOK_HDR_LEN -
				     kctx->gk5e->cksumlength));

	ret = krb5_cbc_cts_decrypt(cipher, aux_cipher, 0, &subbuf);
	if (ret)
		goto out_err;

	our_hmac_obj.len = kctx->gk5e->cksumlength;
	our_hmac_obj.data = our_hmac;
	ret = gss_krb5_checksum(ahash, NULL, 0, &subbuf, 0, &our_hmac_obj);
	if (ret)
		goto out_err;

	/* Get the packet's hmac value */
	ret = read_bytes_from_xdr_buf(buf, len - kctx->gk5e->cksumlength,
				      pkt_hmac, kctx->gk5e->cksumlength);
	if (ret)
		goto out_err;

	if (crypto_memneq(pkt_hmac, our_hmac, kctx->gk5e->cksumlength) != 0) {
		ret = GSS_S_BAD_SIG;
		goto out_err;
	}
	*headskip = crypto_sync_skcipher_blocksize(cipher);
	*tailskip = kctx->gk5e->cksumlength;
out_err:
	if (ret && ret != GSS_S_BAD_SIG)
		ret = GSS_S_FAILURE;
	return ret;
}

/**
 * krb5_etm_checksum - Compute a MAC for a GSS Wrap token
 * @cipher: an initialized cipher transform
 * @tfm: an initialized hash transform
 * @body: xdr_buf containing an RPC message (body.len is the message length)
 * @body_offset: byte offset into @body to start checksumming
 * @cksumout: OUT: a buffer to be filled in with the computed HMAC
 *
 * Usually expressed as H = HMAC(K, IV | ciphertext)[1..h] .
 *
 * Caller provides the truncation length of the output token (h) in
 * cksumout.len.
 *
 * Return values:
 *   %GSS_S_COMPLETE: Digest computed, @cksumout filled in
 *   %GSS_S_FAILURE: Call failed
 */
VISIBLE_IF_KUNIT
u32 krb5_etm_checksum(struct crypto_sync_skcipher *cipher,
		      struct crypto_ahash *tfm, const struct xdr_buf *body,
		      int body_offset, struct xdr_netobj *cksumout)
{
	unsigned int ivsize = crypto_sync_skcipher_ivsize(cipher);
	struct ahash_request *req;
	struct scatterlist sg[1];
	u8 *iv, *checksumdata;
	int err = -ENOMEM;

	checksumdata = kmalloc(crypto_ahash_digestsize(tfm), GFP_KERNEL);
	if (!checksumdata)
		return GSS_S_FAILURE;
	/* For RPCSEC, the "initial cipher state" is always all zeroes. */
	iv = kzalloc(ivsize, GFP_KERNEL);
	if (!iv)
		goto out_free_mem;

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto out_free_mem;
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP, NULL, NULL);
	err = crypto_ahash_init(req);
	if (err)
		goto out_free_ahash;

	sg_init_one(sg, iv, ivsize);
	ahash_request_set_crypt(req, sg, NULL, ivsize);
	err = crypto_ahash_update(req);
	if (err)
		goto out_free_ahash;
	err = xdr_process_buf(body, body_offset, body->len - body_offset,
			      checksummer, req);
	if (err)
		goto out_free_ahash;

	ahash_request_set_crypt(req, NULL, checksumdata, 0);
	err = crypto_ahash_final(req);
	if (err)
		goto out_free_ahash;
	memcpy(cksumout->data, checksumdata, cksumout->len);

out_free_ahash:
	ahash_request_free(req);
out_free_mem:
	kfree(iv);
	kfree_sensitive(checksumdata);
	return err ? GSS_S_FAILURE : GSS_S_COMPLETE;
}
EXPORT_SYMBOL_IF_KUNIT(krb5_etm_checksum);

/**
 * krb5_etm_encrypt - Encrypt using the RFC 8009 rules
 * @kctx: Kerberos context
 * @offset: starting offset of the payload, in bytes
 * @buf: OUT: send buffer to contain the encrypted payload
 * @pages: plaintext payload
 *
 * The main difference with aes_encrypt is that "The HMAC is
 * calculated over the cipher state concatenated with the AES
 * output, instead of being calculated over the confounder and
 * plaintext.  This allows the message receiver to verify the
 * integrity of the message before decrypting the message."
 *
 * RFC 8009 Section 5:
 *
 * encryption function: as follows, where E() is AES encryption in
 * CBC-CS3 mode, and h is the size of truncated HMAC (128 bits or
 * 192 bits as described above).
 *
 *    N = random value of length 128 bits (the AES block size)
 *    IV = cipher state
 *    C = E(Ke, N | plaintext, IV)
 *    H = HMAC(Ki, IV | C)
 *    ciphertext = C | H[1..h]
 *
 * This encryption formula provides AEAD EtM with key separation.
 *
 * Return values:
 *   %GSS_S_COMPLETE: Encryption successful
 *   %GSS_S_FAILURE: Encryption failed
 */
u32
krb5_etm_encrypt(struct krb5_ctx *kctx, u32 offset,
		 struct xdr_buf *buf, struct page **pages)
{
	struct crypto_sync_skcipher *cipher, *aux_cipher;
	struct crypto_ahash *ahash;
	struct xdr_netobj hmac;
	unsigned int conflen;
	u8 *ecptr;
	u32 err;

	if (kctx->initiate) {
		cipher = kctx->initiator_enc;
		aux_cipher = kctx->initiator_enc_aux;
		ahash = kctx->initiator_integ;
	} else {
		cipher = kctx->acceptor_enc;
		aux_cipher = kctx->acceptor_enc_aux;
		ahash = kctx->acceptor_integ;
	}
	conflen = crypto_sync_skcipher_blocksize(cipher);

	offset += GSS_KRB5_TOK_HDR_LEN;
	if (xdr_extend_head(buf, offset, conflen))
		return GSS_S_FAILURE;
	krb5_make_confounder(buf->head[0].iov_base + offset, conflen);
	offset -= GSS_KRB5_TOK_HDR_LEN;

	if (buf->tail[0].iov_base) {
		ecptr = buf->tail[0].iov_base + buf->tail[0].iov_len;
	} else {
		buf->tail[0].iov_base = buf->head[0].iov_base
							+ buf->head[0].iov_len;
		buf->tail[0].iov_len = 0;
		ecptr = buf->tail[0].iov_base;
	}

	memcpy(ecptr, buf->head[0].iov_base + offset, GSS_KRB5_TOK_HDR_LEN);
	buf->tail[0].iov_len += GSS_KRB5_TOK_HDR_LEN;
	buf->len += GSS_KRB5_TOK_HDR_LEN;

	err = krb5_cbc_cts_encrypt(cipher, aux_cipher,
				   offset + GSS_KRB5_TOK_HDR_LEN,
				   buf, pages, NULL, 0);
	if (err)
		return GSS_S_FAILURE;

	hmac.data = buf->tail[0].iov_base + buf->tail[0].iov_len;
	hmac.len = kctx->gk5e->cksumlength;
	err = krb5_etm_checksum(cipher, ahash,
				buf, offset + GSS_KRB5_TOK_HDR_LEN, &hmac);
	if (err)
		goto out_err;
	buf->tail[0].iov_len += kctx->gk5e->cksumlength;
	buf->len += kctx->gk5e->cksumlength;

	return GSS_S_COMPLETE;

out_err:
	return GSS_S_FAILURE;
}

/**
 * krb5_etm_decrypt - Decrypt using the RFC 8009 rules
 * @kctx: Kerberos context
 * @offset: starting offset of the ciphertext, in bytes
 * @len:
 * @buf:
 * @headskip: OUT: the enctype's confounder length, in octets
 * @tailskip: OUT: the enctype's HMAC length, in octets
 *
 * RFC 8009 Section 5:
 *
 * decryption function: as follows, where D() is AES decryption in
 * CBC-CS3 mode, and h is the size of truncated HMAC.
 *
 *    (C, H) = ciphertext
 *        (Note: H is the last h bits of the ciphertext.)
 *    IV = cipher state
 *    if H != HMAC(Ki, IV | C)[1..h]
 *        stop, report error
 *    (N, P) = D(Ke, C, IV)
 *
 * Return values:
 *   %GSS_S_COMPLETE: Decryption successful
 *   %GSS_S_BAD_SIG: computed HMAC != received HMAC
 *   %GSS_S_FAILURE: Decryption failed
 */
u32
krb5_etm_decrypt(struct krb5_ctx *kctx, u32 offset, u32 len,
		 struct xdr_buf *buf, u32 *headskip, u32 *tailskip)
{
	struct crypto_sync_skcipher *cipher, *aux_cipher;
	u8 our_hmac[GSS_KRB5_MAX_CKSUM_LEN];
	u8 pkt_hmac[GSS_KRB5_MAX_CKSUM_LEN];
	struct xdr_netobj our_hmac_obj;
	struct crypto_ahash *ahash;
	struct xdr_buf subbuf;
	u32 ret = 0;

	if (kctx->initiate) {
		cipher = kctx->acceptor_enc;
		aux_cipher = kctx->acceptor_enc_aux;
		ahash = kctx->acceptor_integ;
	} else {
		cipher = kctx->initiator_enc;
		aux_cipher = kctx->initiator_enc_aux;
		ahash = kctx->initiator_integ;
	}

	/* Extract the ciphertext into @subbuf. */
	xdr_buf_subsegment(buf, &subbuf, offset + GSS_KRB5_TOK_HDR_LEN,
			   (len - offset - GSS_KRB5_TOK_HDR_LEN -
			    kctx->gk5e->cksumlength));

	our_hmac_obj.data = our_hmac;
	our_hmac_obj.len = kctx->gk5e->cksumlength;
	ret = krb5_etm_checksum(cipher, ahash, &subbuf, 0, &our_hmac_obj);
	if (ret)
		goto out_err;
	ret = read_bytes_from_xdr_buf(buf, len - kctx->gk5e->cksumlength,
				      pkt_hmac, kctx->gk5e->cksumlength);
	if (ret)
		goto out_err;
	if (crypto_memneq(pkt_hmac, our_hmac, kctx->gk5e->cksumlength) != 0) {
		ret = GSS_S_BAD_SIG;
		goto out_err;
	}

	ret = krb5_cbc_cts_decrypt(cipher, aux_cipher, 0, &subbuf);
	if (ret) {
		ret = GSS_S_FAILURE;
		goto out_err;
	}

	*headskip = crypto_sync_skcipher_blocksize(cipher);
	*tailskip = kctx->gk5e->cksumlength;
	return GSS_S_COMPLETE;

out_err:
	if (ret != GSS_S_BAD_SIG)
		ret = GSS_S_FAILURE;
	return ret;
}
