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

#include <linux/err.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>

#include "gss_krb5_internal.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif


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
 * verify at compile-time (see GSS_KRB5_MAX_SLACK_NEEDED) that the
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


/**
 * gss_krb5_aead_encrypt - Encrypt a wrap token using crypto/krb5
 * @kctx: Kerberos context
 * @offset: byte offset of the GSS token header in @buf
 * @buf: OUT: send buffer
 * @pages: plaintext payload pages (page cache data)
 *
 * The xdr_buf setup mirrors the original per-enctype encrypt
 * functions, but the CBC-CTS encryption and HMAC are replaced
 * by a single AEAD operation through the crypto/krb5 library.
 *
 * Return values:
 *   %GSS_S_COMPLETE: Encryption successful
 *   %GSS_S_FAILURE: Encryption failed
 */
u32
gss_krb5_aead_encrypt(struct krb5_ctx *kctx, u32 offset,
		      struct xdr_buf *buf, struct page **pages)
{
	const struct krb5_enctype *krb5 = kctx->krb5e;
	struct crypto_aead *aead = kctx->initiate ?
		kctx->initiator_enc_aead : kctx->acceptor_enc_aead;
	unsigned int conflen = krb5->conf_len;
	unsigned int cksum_len = krb5->cksum_len;
	unsigned int sec_offset, sec_len, data_len;
	struct scatterlist sg[XDR_BUF_TO_SG_NENTS];
	struct scatterlist *sg_overflow = NULL;
	ssize_t ret;
	int nsg;

	/* Insert space for the confounder */
	if (xdr_extend_head(buf, offset + GSS_KRB5_TOK_HDR_LEN, conflen))
		return GSS_S_FAILURE;

	/* Ensure a tail segment exists */
	if (buf->tail[0].iov_base == NULL) {
		buf->tail[0].iov_base = buf->head[0].iov_base
						+ buf->head[0].iov_len;
		buf->tail[0].iov_len = 0;
	}

	/* Append a copy of the plaintext GSS token header (RFC 4121 Sec 4.2.4) */
	memcpy(buf->tail[0].iov_base + buf->tail[0].iov_len,
	       buf->head[0].iov_base + offset, GSS_KRB5_TOK_HDR_LEN);
	buf->tail[0].iov_len += GSS_KRB5_TOK_HDR_LEN;
	buf->len += GSS_KRB5_TOK_HDR_LEN;

	/* Reserve space for the integrity checksum */
	buf->tail[0].iov_len += cksum_len;
	buf->len += cksum_len;

	/*
	 * The AEAD operates in-place, but on the client send path the
	 * plaintext payload lives in page cache pages that must not be
	 * modified.  Copy the payload into the scratch output pages
	 * first.  On the server send path @pages and buf->pages are
	 * the same array, and no copy is needed.
	 *
	 * Both arrays share buf->page_base, so the same index and
	 * intra-page offset address corresponding data in each.
	 */
	if (pages != buf->pages) {
		unsigned int poff = buf->page_base;
		unsigned int plen = buf->page_len;
		unsigned int i = poff >> PAGE_SHIFT;
		unsigned int off = offset_in_page(poff);

		while (plen) {
			unsigned int n = min_t(unsigned int, plen,
					       PAGE_SIZE - off);
			memcpy_page(buf->pages[i], off, pages[i], off, n);
			plen -= n;
			i++;
			off = 0;
		}
	}

	/* Build scatterlist covering the secured region */
	sec_offset = offset + GSS_KRB5_TOK_HDR_LEN;
	sec_len = buf->len - sec_offset;
	data_len = sec_len - conflen - cksum_len;

	nsg = xdr_buf_to_sg_alloc(buf, sec_offset, sec_len,
				  sg, ARRAY_SIZE(sg),
				  &sg_overflow, GFP_NOFS);
	if (nsg < 0)
		return GSS_S_FAILURE;

	ret = crypto_krb5_encrypt(krb5, aead, sg, nsg, sec_len,
				  conflen, data_len, false);
	kfree(sg_overflow);
	if (ret < 0)
		return GSS_S_FAILURE;

	return GSS_S_COMPLETE;
}

/**
 * gss_krb5_aead_decrypt - Decrypt a wrap token using crypto/krb5
 * @kctx: Kerberos context
 * @offset: byte offset of the GSS token header in @buf
 * @len: total length of the GSS token
 * @buf: ciphertext buffer, decrypted in-place
 * @headskip: OUT: confounder length, in octets
 * @tailskip: OUT: checksum length, in octets
 *
 * Return values:
 *   %GSS_S_COMPLETE: Decryption and integrity verification succeeded
 *   %GSS_S_BAD_SIG: Integrity checksum did not match
 *   %GSS_S_DEFECTIVE_TOKEN: Token is malformed or truncated
 *   %GSS_S_FAILURE: Decryption failed
 */
u32
gss_krb5_aead_decrypt(struct krb5_ctx *kctx, u32 offset, u32 len,
		      struct xdr_buf *buf, u32 *headskip, u32 *tailskip)
{
	const struct krb5_enctype *krb5 = kctx->krb5e;
	struct crypto_aead *aead = kctx->initiate ?
		kctx->acceptor_enc_aead : kctx->initiator_enc_aead;
	unsigned int sec_offset, sec_len;
	size_t data_offset, data_len;
	struct scatterlist sg[XDR_BUF_TO_SG_NENTS];
	struct scatterlist *sg_overflow = NULL;
	int nsg, ret;

	/* Secured region starts after the GSS token header */
	sec_offset = offset + GSS_KRB5_TOK_HDR_LEN;
	if (len < sec_offset)
		return GSS_S_DEFECTIVE_TOKEN;
	sec_len = len - sec_offset;

	nsg = xdr_buf_to_sg_alloc(buf, sec_offset, sec_len,
				  sg, ARRAY_SIZE(sg),
				  &sg_overflow, GFP_NOFS);
	if (nsg < 0)
		return GSS_S_FAILURE;

	data_offset = 0;
	data_len = sec_len;
	ret = crypto_krb5_decrypt(krb5, aead, sg, nsg,
				  &data_offset, &data_len);
	kfree(sg_overflow);
	if (ret < 0)
		return gss_krb5_errno_to_status(ret);

	*headskip = data_offset;
	*tailskip = sec_len - data_offset - data_len;
	return GSS_S_COMPLETE;
}

/**
 * gss_krb5_mic_build_sg - Build scatterlist for MIC token operations
 * @body: xdr_buf containing the message body
 * @cksum: pointer to checksum area in the token buffer
 * @cksum_len: length of checksum area
 * @hdr: pointer to GSS token header
 * @sg_head: caller-provided scatterlist array; if more than
 *	XDR_BUF_TO_SG_NENTS entries are needed, an overflow
 *	scatterlist is allocated and chained automatically
 * @sg_overflow: OUT: overflow scatterlist, caller must kfree
 *
 * Per RFC 4121 Section 4.2.4, MIC token checksums cover the
 * message body followed by the token header. The checksum
 * output or received checksum occupies the first scatterlist
 * entry.  This layout cannot be constructed by
 * xdr_buf_to_sg_alloc() because the checksum area and the GSS
 * header lie outside the xdr_buf.
 *
 * Returns the number of scatterlist entries on success, or a
 * negative errno on failure.
 */
int gss_krb5_mic_build_sg(const struct xdr_buf *body,
			  void *cksum, unsigned int cksum_len,
			  void *hdr,
			  struct scatterlist *sg_head,
			  struct scatterlist **sg_overflow)
{
	struct scatterlist *entry;
	int body_max, body_nsg, nsg;

	*sg_overflow = NULL;

	body_max = 2;
	if (body->page_len)
		body_max += DIV_ROUND_UP(body->page_len +
					 offset_in_page(body->page_base),
					 PAGE_SIZE);
	nsg = 1 + body_max + 1;
	if (nsg <= XDR_BUF_TO_SG_NENTS) {
		sg_init_table(sg_head, nsg);
	} else {
		unsigned int overflow_nents =
			nsg - XDR_BUF_TO_SG_NENTS + 1;

		*sg_overflow = kmalloc_array(overflow_nents,
					     sizeof(**sg_overflow),
					     GFP_NOFS);
		if (!*sg_overflow)
			return -ENOMEM;

		sg_init_table(sg_head, XDR_BUF_TO_SG_NENTS);
		sg_init_table(*sg_overflow, overflow_nents);
		sg_chain(sg_head, XDR_BUF_TO_SG_NENTS, *sg_overflow);
	}

	sg_set_buf(&sg_head[0], cksum, cksum_len);
	body_nsg = xdr_buf_to_sg(body, 0, body->len,
				 sg_next(&sg_head[0]), body_max);
	if (body_nsg < 0)
		goto out_err;

	/*
	 * xdr_buf_to_sg marks the last body entry as end-of-list;
	 * clear it so the trailing header entry is reachable.
	 */
	if (body_nsg > 0) {
		entry = sg_last(sg_next(&sg_head[0]), body_nsg);
		sg_unmark_end(entry);
		entry = sg_next(entry);
	} else {
		entry = sg_next(&sg_head[0]);
	}
	sg_set_buf(entry, hdr, GSS_KRB5_TOK_HDR_LEN);
	sg_mark_end(entry);
	return 1 + body_nsg + 1;

out_err:
	kfree(*sg_overflow);
	*sg_overflow = NULL;
	return body_nsg;
}
