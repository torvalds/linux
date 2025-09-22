/* $OpenBSD: ssl_both.c,v 1.47 2024/02/03 15:58:33 beck Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2002 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECC cipher suite support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "bytestring.h"
#include "dtls_local.h"
#include "ssl_local.h"

/*
 * Send s->init_buf in records of type 'type' (SSL3_RT_HANDSHAKE or
 * SSL3_RT_CHANGE_CIPHER_SPEC).
 */
int
ssl3_do_write(SSL *s, int type)
{
	int ret;

	ret = ssl3_write_bytes(s, type, &s->init_buf->data[s->init_off],
	    s->init_num);
	if (ret < 0)
		return (-1);

	if (type == SSL3_RT_HANDSHAKE)
		/*
		 * Should not be done for 'Hello Request's, but in that case
		 * we'll ignore the result anyway.
		 */
		tls1_transcript_record(s,
		    (unsigned char *)&s->init_buf->data[s->init_off], ret);

	if (ret == s->init_num) {
		ssl_msg_callback(s, 1, type, s->init_buf->data,
		    (size_t)(s->init_off + s->init_num));
		return (1);
	}

	s->init_off += ret;
	s->init_num -= ret;

	return (0);
}

static int
ssl3_add_cert(CBB *cbb, X509 *x)
{
	unsigned char *data;
	int cert_len;
	int ret = 0;
	CBB cert;

	if ((cert_len = i2d_X509(x, NULL)) < 0)
		goto err;

	if (!CBB_add_u24_length_prefixed(cbb, &cert))
		goto err;
	if (!CBB_add_space(&cert, &data, cert_len))
		goto err;
	if (i2d_X509(x, &data) < 0)
		goto err;
	if (!CBB_flush(cbb))
		goto err;

	ret = 1;

 err:
	return (ret);
}

int
ssl3_output_cert_chain(SSL *s, CBB *cbb, SSL_CERT_PKEY *cpk)
{
	X509_STORE_CTX *xs_ctx = NULL;
	STACK_OF(X509) *chain;
	CBB cert_list;
	X509 *x;
	int ret = 0;
	int i;

	if (!CBB_add_u24_length_prefixed(cbb, &cert_list))
		goto err;

	/* Send an empty certificate list when no certificate is available. */
	if (cpk == NULL)
		goto done;

	if ((chain = cpk->chain) == NULL)
		chain = s->ctx->extra_certs;

	if (chain != NULL || (s->mode & SSL_MODE_NO_AUTO_CHAIN)) {
		if (!ssl3_add_cert(&cert_list, cpk->x509))
			goto err;
	} else {
		if ((xs_ctx = X509_STORE_CTX_new()) == NULL)
			goto err;
		if (!X509_STORE_CTX_init(xs_ctx, s->ctx->cert_store,
		    cpk->x509, NULL)) {
			SSLerror(s, ERR_R_X509_LIB);
			goto err;
		}
		X509_VERIFY_PARAM_set_flags(X509_STORE_CTX_get0_param(xs_ctx),
		    X509_V_FLAG_LEGACY_VERIFY);
		X509_verify_cert(xs_ctx);
		ERR_clear_error();
		chain = X509_STORE_CTX_get0_chain(xs_ctx);
	}

	for (i = 0; i < sk_X509_num(chain); i++) {
		x = sk_X509_value(chain, i);
		if (!ssl3_add_cert(&cert_list, x))
			goto err;
	}

 done:
	if (!CBB_flush(cbb))
		goto err;

	ret = 1;

 err:
	X509_STORE_CTX_free(xs_ctx);

	return (ret);
}

/*
 * Obtain handshake message of message type 'mt' (any if mt == -1),
 * maximum acceptable body length 'max'.
 * The first four bytes (msg_type and length) are read in state 'st1',
 * the body is read in state 'stn'.
 */
int
ssl3_get_message(SSL *s, int st1, int stn, int mt, long max)
{
	unsigned char *p;
	uint32_t l;
	long n;
	int i, al;
	CBS cbs;
	uint8_t u8;

	if (SSL_is_dtls(s))
		return dtls1_get_message(s, st1, stn, mt, max);

	if (s->s3->hs.tls12.reuse_message) {
		s->s3->hs.tls12.reuse_message = 0;
		if ((mt >= 0) && (s->s3->hs.tls12.message_type != mt)) {
			al = SSL_AD_UNEXPECTED_MESSAGE;
			SSLerror(s, SSL_R_UNEXPECTED_MESSAGE);
			goto fatal_err;
		}
		s->init_msg = s->init_buf->data +
		    SSL3_HM_HEADER_LENGTH;
		s->init_num = (int)s->s3->hs.tls12.message_size;
		return 1;
	}

	p = (unsigned char *)s->init_buf->data;

	if (s->s3->hs.state == st1) {
		int skip_message;

		do {
			while (s->init_num < SSL3_HM_HEADER_LENGTH) {
				i = s->method->ssl_read_bytes(s,
				    SSL3_RT_HANDSHAKE, &p[s->init_num],
				    SSL3_HM_HEADER_LENGTH - s->init_num, 0);
				if (i <= 0) {
					s->rwstate = SSL_READING;
					return i;
				}
				s->init_num += i;
			}

			skip_message = 0;
			if (!s->server && p[0] == SSL3_MT_HELLO_REQUEST) {
				/*
				 * The server may always send 'Hello Request'
				 * messages -- we are doing a handshake anyway
				 * now, so ignore them if their format is
				 * correct.  Does not count for 'Finished' MAC.
				 */
				if (p[1] == 0 && p[2] == 0 &&p[3] == 0) {
					s->init_num = 0;
					skip_message = 1;

					ssl_msg_callback(s, 0,
					    SSL3_RT_HANDSHAKE, p,
					    SSL3_HM_HEADER_LENGTH);
				}
			}
		} while (skip_message);

		if ((mt >= 0) && (*p != mt)) {
			al = SSL_AD_UNEXPECTED_MESSAGE;
			SSLerror(s, SSL_R_UNEXPECTED_MESSAGE);
			goto fatal_err;
		}

		CBS_init(&cbs, p, SSL3_HM_HEADER_LENGTH);
		if (!CBS_get_u8(&cbs, &u8) ||
		    !CBS_get_u24(&cbs, &l)) {
			SSLerror(s, ERR_R_BUF_LIB);
			goto err;
		}
		s->s3->hs.tls12.message_type = u8;

		if (l > (unsigned long)max) {
			al = SSL_AD_ILLEGAL_PARAMETER;
			SSLerror(s, SSL_R_EXCESSIVE_MESSAGE_SIZE);
			goto fatal_err;
		}
		if (l && !BUF_MEM_grow_clean(s->init_buf,
		    l + SSL3_HM_HEADER_LENGTH)) {
			SSLerror(s, ERR_R_BUF_LIB);
			goto err;
		}
		s->s3->hs.tls12.message_size = l;
		s->s3->hs.state = stn;

		s->init_msg = s->init_buf->data +
		    SSL3_HM_HEADER_LENGTH;
		s->init_num = 0;
	}

	/* next state (stn) */
	p = s->init_msg;
	n = s->s3->hs.tls12.message_size - s->init_num;
	while (n > 0) {
		i = s->method->ssl_read_bytes(s, SSL3_RT_HANDSHAKE,
		    &p[s->init_num], n, 0);
		if (i <= 0) {
			s->rwstate = SSL_READING;
			return i;
		}
		s->init_num += i;
		n -= i;
	}

	/* Feed this message into MAC computation. */
	if (s->mac_packet) {
		tls1_transcript_record(s, (unsigned char *)s->init_buf->data,
		    s->init_num + SSL3_HM_HEADER_LENGTH);

		ssl_msg_callback(s, 0, SSL3_RT_HANDSHAKE,
		    s->init_buf->data,
		    (size_t)s->init_num + SSL3_HM_HEADER_LENGTH);
	}

	return 1;

 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
	return -1;
}

int
ssl_cert_type(EVP_PKEY *pkey)
{
	if (pkey == NULL)
		return -1;

	switch (EVP_PKEY_id(pkey)) {
	case EVP_PKEY_EC:
		return SSL_PKEY_ECC;
	case EVP_PKEY_RSA:
	case EVP_PKEY_RSA_PSS:
		return SSL_PKEY_RSA;
	}

	return -1;
}

int
ssl_verify_alarm_type(long type)
{
	int al;

	switch (type) {
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
	case X509_V_ERR_UNABLE_TO_GET_CRL:
	case X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER:
		al = SSL_AD_UNKNOWN_CA;
		break;
	case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
	case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
	case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
	case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
	case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
	case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
	case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
	case X509_V_ERR_CERT_NOT_YET_VALID:
	case X509_V_ERR_CRL_NOT_YET_VALID:
	case X509_V_ERR_CERT_UNTRUSTED:
	case X509_V_ERR_CERT_REJECTED:
		al = SSL_AD_BAD_CERTIFICATE;
		break;
	case X509_V_ERR_CERT_SIGNATURE_FAILURE:
	case X509_V_ERR_CRL_SIGNATURE_FAILURE:
		al = SSL_AD_DECRYPT_ERROR;
		break;
	case X509_V_ERR_CERT_HAS_EXPIRED:
	case X509_V_ERR_CRL_HAS_EXPIRED:
		al = SSL_AD_CERTIFICATE_EXPIRED;
		break;
	case X509_V_ERR_CERT_REVOKED:
		al = SSL_AD_CERTIFICATE_REVOKED;
		break;
	case X509_V_ERR_OUT_OF_MEM:
		al = SSL_AD_INTERNAL_ERROR;
		break;
	case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
	case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
	case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
	case X509_V_ERR_CERT_CHAIN_TOO_LONG:
	case X509_V_ERR_PATH_LENGTH_EXCEEDED:
	case X509_V_ERR_INVALID_CA:
		al = SSL_AD_UNKNOWN_CA;
		break;
	case X509_V_ERR_APPLICATION_VERIFICATION:
		al = SSL_AD_HANDSHAKE_FAILURE;
		break;
	case X509_V_ERR_INVALID_PURPOSE:
		al = SSL_AD_UNSUPPORTED_CERTIFICATE;
		break;
	default:
		al = SSL_AD_CERTIFICATE_UNKNOWN;
		break;
	}
	return (al);
}

int
ssl3_setup_init_buffer(SSL *s)
{
	BUF_MEM *buf = NULL;

	if (s->init_buf != NULL)
		return (1);

	if ((buf = BUF_MEM_new()) == NULL)
		goto err;
	if (!BUF_MEM_grow(buf, SSL3_RT_MAX_PLAIN_LENGTH))
		goto err;

	s->init_buf = buf;
	return (1);

 err:
	BUF_MEM_free(buf);
	return (0);
}

void
ssl3_release_init_buffer(SSL *s)
{
	BUF_MEM_free(s->init_buf);
	s->init_buf = NULL;
	s->init_msg = NULL;
	s->init_num = 0;
	s->init_off = 0;
}

int
ssl3_setup_read_buffer(SSL *s)
{
	unsigned char *p;
	size_t len, align, headerlen;

	if (SSL_is_dtls(s))
		headerlen = DTLS1_RT_HEADER_LENGTH;
	else
		headerlen = SSL3_RT_HEADER_LENGTH;

	align = (-SSL3_RT_HEADER_LENGTH) & (SSL3_ALIGN_PAYLOAD - 1);

	if (s->s3->rbuf.buf == NULL) {
		len = SSL3_RT_MAX_PLAIN_LENGTH +
		    SSL3_RT_MAX_ENCRYPTED_OVERHEAD + headerlen + align;
		if ((p = calloc(1, len)) == NULL)
			goto err;
		s->s3->rbuf.buf = p;
		s->s3->rbuf.len = len;
	}

	s->packet = s->s3->rbuf.buf;
	return 1;

 err:
	SSLerror(s, ERR_R_MALLOC_FAILURE);
	return 0;
}

int
ssl3_setup_write_buffer(SSL *s)
{
	unsigned char *p;
	size_t len, align, headerlen;

	if (SSL_is_dtls(s))
		headerlen = DTLS1_RT_HEADER_LENGTH + 1;
	else
		headerlen = SSL3_RT_HEADER_LENGTH;

	align = (-SSL3_RT_HEADER_LENGTH) & (SSL3_ALIGN_PAYLOAD - 1);

	if (s->s3->wbuf.buf == NULL) {
		len = s->max_send_fragment +
		    SSL3_RT_SEND_MAX_ENCRYPTED_OVERHEAD + headerlen + align;
		if (!(s->options & SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS))
			len += headerlen + align +
			    SSL3_RT_SEND_MAX_ENCRYPTED_OVERHEAD;

		if ((p = calloc(1, len)) == NULL)
			goto err;
		s->s3->wbuf.buf = p;
		s->s3->wbuf.len = len;
	}

	return 1;

 err:
	SSLerror(s, ERR_R_MALLOC_FAILURE);
	return 0;
}

int
ssl3_setup_buffers(SSL *s)
{
	if (!ssl3_setup_read_buffer(s))
		return 0;
	if (!ssl3_setup_write_buffer(s))
		return 0;
	return 1;
}

void
ssl3_release_buffer(SSL3_BUFFER_INTERNAL *b)
{
	freezero(b->buf, b->len);
	b->buf = NULL;
	b->len = 0;
}

void
ssl3_release_read_buffer(SSL *s)
{
	ssl3_release_buffer(&s->s3->rbuf);
}

void
ssl3_release_write_buffer(SSL *s)
{
	ssl3_release_buffer(&s->s3->wbuf);
}
