/* $OpenBSD: ssl_lib.c,v 1.333 2025/06/09 10:14:38 tb Exp $ */
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
 * Copyright (c) 1998-2007 The OpenSSL Project.  All rights reserved.
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
/* ====================================================================
 * Copyright 2005 Nokia. All rights reserved.
 *
 * The portions of the attached software ("Contribution") is developed by
 * Nokia Corporation and is licensed pursuant to the OpenSSL open source
 * license.
 *
 * The Contribution, originally written by Mika Kousa and Pasi Eronen of
 * Nokia Corporation, consists of the "PSK" (Pre-Shared Key) ciphersuites
 * support (see RFC 4279) to OpenSSL.
 *
 * No patent licenses or other rights except those expressly stated in
 * the OpenSSL open source license shall be deemed granted or received
 * expressly, by implication, estoppel, or otherwise.
 *
 * No assurances are provided by Nokia that the Contribution does not
 * infringe the patent or other intellectual property rights of any third
 * party or that the license provides you with all the necessary rights
 * to make use of the Contribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. IN
 * ADDITION TO THE DISCLAIMERS INCLUDED IN THE LICENSE, NOKIA
 * SPECIFICALLY DISCLAIMS ANY LIABILITY FOR CLAIMS BROUGHT BY YOU OR ANY
 * OTHER ENTITY BASED ON INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS OR
 * OTHERWISE.
 */

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <limits.h>
#include <stdio.h>

#include <openssl/dh.h>
#include <openssl/lhash.h>
#include <openssl/objects.h>
#include <openssl/ocsp.h>
#include <openssl/opensslconf.h>
#include <openssl/x509v3.h>

#include "bytestring.h"
#include "dtls_local.h"
#include "ssl_local.h"
#include "ssl_sigalgs.h"
#include "ssl_tlsext.h"
#include "tls12_internal.h"

int
SSL_clear(SSL *s)
{
	if (s->method == NULL) {
		SSLerror(s, SSL_R_NO_METHOD_SPECIFIED);
		return (0);
	}

	if (ssl_clear_bad_session(s)) {
		SSL_SESSION_free(s->session);
		s->session = NULL;
	}

	s->error = 0;
	s->hit = 0;
	s->shutdown = 0;

	if (s->renegotiate) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return (0);
	}

	s->version = s->method->version;
	s->client_version = s->version;
	s->rwstate = SSL_NOTHING;
	s->rstate = SSL_ST_READ_HEADER;

	tls13_ctx_free(s->tls13);
	s->tls13 = NULL;

	ssl3_release_init_buffer(s);

	ssl_clear_cipher_state(s);

	s->first_packet = 0;

	/*
	 * Check to see if we were changed into a different method, if
	 * so, revert back if we are not doing session-id reuse.
	 */
	if (!s->in_handshake && (s->session == NULL) &&
	    (s->method != s->ctx->method)) {
		s->method->ssl_free(s);
		s->method = s->ctx->method;
		if (!s->method->ssl_new(s))
			return (0);
	} else
		s->method->ssl_clear(s);

	return (1);
}
LSSL_ALIAS(SSL_clear);

/* Used to change an SSL_CTXs default SSL method type */
int
SSL_CTX_set_ssl_version(SSL_CTX *ctx, const SSL_METHOD *meth)
{
	STACK_OF(SSL_CIPHER) *ciphers;

	ctx->method = meth;

	ciphers = ssl_create_cipher_list(ctx->method, &ctx->cipher_list,
	    ctx->cipher_list_tls13, SSL_DEFAULT_CIPHER_LIST,
	    ctx->cert);
	if (ciphers == NULL || sk_SSL_CIPHER_num(ciphers) <= 0) {
		SSLerrorx(SSL_R_SSL_LIBRARY_HAS_NO_CIPHERS);
		return (0);
	}
	return (1);
}
LSSL_ALIAS(SSL_CTX_set_ssl_version);

SSL *
SSL_new(SSL_CTX *ctx)
{
	SSL *s;
	CBS cbs;

	if (ctx == NULL) {
		SSLerrorx(SSL_R_NULL_SSL_CTX);
		return (NULL);
	}
	if (ctx->method == NULL) {
		SSLerrorx(SSL_R_SSL_CTX_HAS_NO_DEFAULT_SSL_VERSION);
		return (NULL);
	}

	if ((s = calloc(1, sizeof(*s))) == NULL)
		goto err;

	if ((s->rl = tls12_record_layer_new()) == NULL)
		goto err;

	s->min_tls_version = ctx->min_tls_version;
	s->max_tls_version = ctx->max_tls_version;
	s->min_proto_version = ctx->min_proto_version;
	s->max_proto_version = ctx->max_proto_version;

	s->options = ctx->options;
	s->mode = ctx->mode;
	s->max_cert_list = ctx->max_cert_list;
	s->num_tickets = ctx->num_tickets;

	if ((s->cert = ssl_cert_dup(ctx->cert)) == NULL)
		goto err;

	s->read_ahead = ctx->read_ahead;
	s->msg_callback = ctx->msg_callback;
	s->msg_callback_arg = ctx->msg_callback_arg;
	s->verify_mode = ctx->verify_mode;
	s->sid_ctx_length = ctx->sid_ctx_length;
	OPENSSL_assert(s->sid_ctx_length <= sizeof s->sid_ctx);
	memcpy(&s->sid_ctx, &ctx->sid_ctx, sizeof(s->sid_ctx));
	s->verify_callback = ctx->default_verify_callback;
	s->generate_session_id = ctx->generate_session_id;

	s->param = X509_VERIFY_PARAM_new();
	if (!s->param)
		goto err;
	X509_VERIFY_PARAM_inherit(s->param, ctx->param);
	s->quiet_shutdown = ctx->quiet_shutdown;
	s->max_send_fragment = ctx->max_send_fragment;

	CRYPTO_add(&ctx->references, 1, CRYPTO_LOCK_SSL_CTX);
	s->ctx = ctx;
	s->tlsext_debug_cb = NULL;
	s->tlsext_debug_arg = NULL;
	s->tlsext_ticket_expected = 0;
	s->tlsext_status_type = -1;
	s->tlsext_status_expected = 0;
	s->tlsext_ocsp_ids = NULL;
	s->tlsext_ocsp_exts = NULL;
	s->tlsext_ocsp_resp = NULL;
	s->tlsext_ocsp_resp_len = 0;
	CRYPTO_add(&ctx->references, 1, CRYPTO_LOCK_SSL_CTX);
	s->initial_ctx = ctx;

	if (!tlsext_randomize_build_order(s))
		goto err;

	if (ctx->tlsext_ecpointformatlist != NULL) {
		s->tlsext_ecpointformatlist =
		    calloc(ctx->tlsext_ecpointformatlist_length,
			sizeof(ctx->tlsext_ecpointformatlist[0]));
		if (s->tlsext_ecpointformatlist == NULL)
			goto err;
		memcpy(s->tlsext_ecpointformatlist,
		    ctx->tlsext_ecpointformatlist,
		    ctx->tlsext_ecpointformatlist_length *
		    sizeof(ctx->tlsext_ecpointformatlist[0]));
		s->tlsext_ecpointformatlist_length =
		    ctx->tlsext_ecpointformatlist_length;
	}
	if (ctx->tlsext_supportedgroups != NULL) {
		s->tlsext_supportedgroups =
		    calloc(ctx->tlsext_supportedgroups_length,
			sizeof(ctx->tlsext_supportedgroups[0]));
		if (s->tlsext_supportedgroups == NULL)
			goto err;
		memcpy(s->tlsext_supportedgroups,
		    ctx->tlsext_supportedgroups,
		    ctx->tlsext_supportedgroups_length *
		    sizeof(ctx->tlsext_supportedgroups[0]));
		s->tlsext_supportedgroups_length =
		    ctx->tlsext_supportedgroups_length;
	}

	CBS_init(&cbs, ctx->alpn_client_proto_list,
	    ctx->alpn_client_proto_list_len);
	if (!CBS_stow(&cbs, &s->alpn_client_proto_list,
	    &s->alpn_client_proto_list_len))
		goto err;

	s->verify_result = X509_V_OK;

	s->method = ctx->method;
	s->quic_method = ctx->quic_method;

	if (!s->method->ssl_new(s))
		goto err;

	s->references = 1;
	s->server = ctx->method->server;

	SSL_clear(s);

	CRYPTO_new_ex_data(CRYPTO_EX_INDEX_SSL, s, &s->ex_data);

	return (s);

 err:
	SSL_free(s);
	SSLerrorx(ERR_R_MALLOC_FAILURE);
	return (NULL);
}
LSSL_ALIAS(SSL_new);

int
SSL_CTX_set_session_id_context(SSL_CTX *ctx, const unsigned char *sid_ctx,
    unsigned int sid_ctx_len)
{
	if (sid_ctx_len > sizeof ctx->sid_ctx) {
		SSLerrorx(SSL_R_SSL_SESSION_ID_CONTEXT_TOO_LONG);
		return (0);
	}
	ctx->sid_ctx_length = sid_ctx_len;
	memcpy(ctx->sid_ctx, sid_ctx, sid_ctx_len);

	return (1);
}
LSSL_ALIAS(SSL_CTX_set_session_id_context);

int
SSL_set_session_id_context(SSL *ssl, const unsigned char *sid_ctx,
    unsigned int sid_ctx_len)
{
	if (sid_ctx_len > SSL_MAX_SID_CTX_LENGTH) {
		SSLerror(ssl, SSL_R_SSL_SESSION_ID_CONTEXT_TOO_LONG);
		return (0);
	}
	ssl->sid_ctx_length = sid_ctx_len;
	memcpy(ssl->sid_ctx, sid_ctx, sid_ctx_len);

	return (1);
}
LSSL_ALIAS(SSL_set_session_id_context);

int
SSL_CTX_set_generate_session_id(SSL_CTX *ctx, GEN_SESSION_CB cb)
{
	CRYPTO_w_lock(CRYPTO_LOCK_SSL_CTX);
	ctx->generate_session_id = cb;
	CRYPTO_w_unlock(CRYPTO_LOCK_SSL_CTX);
	return (1);
}
LSSL_ALIAS(SSL_CTX_set_generate_session_id);

int
SSL_set_generate_session_id(SSL *ssl, GEN_SESSION_CB cb)
{
	CRYPTO_w_lock(CRYPTO_LOCK_SSL);
	ssl->generate_session_id = cb;
	CRYPTO_w_unlock(CRYPTO_LOCK_SSL);
	return (1);
}
LSSL_ALIAS(SSL_set_generate_session_id);

int
SSL_has_matching_session_id(const SSL *ssl, const unsigned char *id,
    unsigned int id_len)
{
	/*
	 * A quick examination of SSL_SESSION_hash and SSL_SESSION_cmp
	 * shows how we can "construct" a session to give us the desired
	 * check - ie. to find if there's a session in the hash table
	 * that would conflict with any new session built out of this
	 * id/id_len and the ssl_version in use by this SSL.
	 */
	SSL_SESSION r, *p;

	if (id_len > sizeof r.session_id)
		return (0);

	r.ssl_version = ssl->version;
	r.session_id_length = id_len;
	memcpy(r.session_id, id, id_len);

	CRYPTO_r_lock(CRYPTO_LOCK_SSL_CTX);
	p = lh_SSL_SESSION_retrieve(ssl->ctx->sessions, &r);
	CRYPTO_r_unlock(CRYPTO_LOCK_SSL_CTX);
	return (p != NULL);
}
LSSL_ALIAS(SSL_has_matching_session_id);

int
SSL_CTX_set_purpose(SSL_CTX *s, int purpose)
{
	return (X509_VERIFY_PARAM_set_purpose(s->param, purpose));
}
LSSL_ALIAS(SSL_CTX_set_purpose);

int
SSL_set_purpose(SSL *s, int purpose)
{
	return (X509_VERIFY_PARAM_set_purpose(s->param, purpose));
}
LSSL_ALIAS(SSL_set_purpose);

int
SSL_CTX_set_trust(SSL_CTX *s, int trust)
{
	return (X509_VERIFY_PARAM_set_trust(s->param, trust));
}
LSSL_ALIAS(SSL_CTX_set_trust);

int
SSL_set_trust(SSL *s, int trust)
{
	return (X509_VERIFY_PARAM_set_trust(s->param, trust));
}
LSSL_ALIAS(SSL_set_trust);

int
SSL_set1_host(SSL *s, const char *hostname)
{
	struct in_addr ina;
	struct in6_addr in6a;

	if (hostname != NULL && *hostname != '\0' &&
	    (inet_pton(AF_INET, hostname, &ina) == 1 ||
	    inet_pton(AF_INET6, hostname, &in6a) == 1))
		return X509_VERIFY_PARAM_set1_ip_asc(s->param, hostname);
	else
		return X509_VERIFY_PARAM_set1_host(s->param, hostname, 0);
}
LSSL_ALIAS(SSL_set1_host);

void
SSL_set_hostflags(SSL *s, unsigned int flags)
{
	X509_VERIFY_PARAM_set_hostflags(s->param, flags);
}
LSSL_ALIAS(SSL_set_hostflags);

const char *
SSL_get0_peername(SSL *s)
{
	return X509_VERIFY_PARAM_get0_peername(s->param);
}
LSSL_ALIAS(SSL_get0_peername);

X509_VERIFY_PARAM *
SSL_CTX_get0_param(SSL_CTX *ctx)
{
	return (ctx->param);
}
LSSL_ALIAS(SSL_CTX_get0_param);

int
SSL_CTX_set1_param(SSL_CTX *ctx, X509_VERIFY_PARAM *vpm)
{
	return (X509_VERIFY_PARAM_set1(ctx->param, vpm));
}
LSSL_ALIAS(SSL_CTX_set1_param);

X509_VERIFY_PARAM *
SSL_get0_param(SSL *ssl)
{
	return (ssl->param);
}
LSSL_ALIAS(SSL_get0_param);

int
SSL_set1_param(SSL *ssl, X509_VERIFY_PARAM *vpm)
{
	return (X509_VERIFY_PARAM_set1(ssl->param, vpm));
}
LSSL_ALIAS(SSL_set1_param);

void
SSL_free(SSL *s)
{
	int	i;

	if (s == NULL)
		return;

	i = CRYPTO_add(&s->references, -1, CRYPTO_LOCK_SSL);
	if (i > 0)
		return;

	X509_VERIFY_PARAM_free(s->param);

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_SSL, s, &s->ex_data);

	if (s->bbio != NULL) {
		/* If the buffering BIO is in place, pop it off */
		if (s->bbio == s->wbio) {
			s->wbio = BIO_pop(s->wbio);
		}
		BIO_free(s->bbio);
		s->bbio = NULL;
	}

	if (s->rbio != s->wbio)
		BIO_free_all(s->rbio);
	BIO_free_all(s->wbio);

	tls13_ctx_free(s->tls13);

	ssl3_release_init_buffer(s);

	sk_SSL_CIPHER_free(s->cipher_list);
	sk_SSL_CIPHER_free(s->cipher_list_tls13);

	/* Make the next call work :-) */
	if (s->session != NULL) {
		ssl_clear_bad_session(s);
		SSL_SESSION_free(s->session);
	}

	ssl_clear_cipher_state(s);

	ssl_cert_free(s->cert);

	free(s->tlsext_build_order);

	free(s->tlsext_hostname);
	SSL_CTX_free(s->initial_ctx);

	free(s->tlsext_ecpointformatlist);
	free(s->tlsext_supportedgroups);

	sk_X509_EXTENSION_pop_free(s->tlsext_ocsp_exts,
	    X509_EXTENSION_free);
	sk_OCSP_RESPID_pop_free(s->tlsext_ocsp_ids, OCSP_RESPID_free);
	free(s->tlsext_ocsp_resp);

	sk_X509_NAME_pop_free(s->client_CA, X509_NAME_free);

	if (s->method != NULL)
		s->method->ssl_free(s);

	SSL_CTX_free(s->ctx);

	free(s->alpn_client_proto_list);

	free(s->quic_transport_params);

#ifndef OPENSSL_NO_SRTP
	sk_SRTP_PROTECTION_PROFILE_free(s->srtp_profiles);
#endif

	tls12_record_layer_free(s->rl);

	free(s);
}
LSSL_ALIAS(SSL_free);

int
SSL_up_ref(SSL *s)
{
	return CRYPTO_add(&s->references, 1, CRYPTO_LOCK_SSL) > 1;
}
LSSL_ALIAS(SSL_up_ref);

void
SSL_set_bio(SSL *s, BIO *rbio, BIO *wbio)
{
	/* If the output buffering BIO is still in place, remove it */
	if (s->bbio != NULL) {
		if (s->wbio == s->bbio) {
			s->wbio = BIO_next(s->wbio);
			BIO_set_next(s->bbio, NULL);
		}
	}

	if (s->rbio != rbio && s->rbio != s->wbio)
		BIO_free_all(s->rbio);
	if (s->wbio != wbio)
		BIO_free_all(s->wbio);
	s->rbio = rbio;
	s->wbio = wbio;
}
LSSL_ALIAS(SSL_set_bio);

BIO *
SSL_get_rbio(const SSL *s)
{
	return (s->rbio);
}
LSSL_ALIAS(SSL_get_rbio);

void
SSL_set0_rbio(SSL *s, BIO *rbio)
{
	BIO_free_all(s->rbio);
	s->rbio = rbio;
}
LSSL_ALIAS(SSL_set0_rbio);

BIO *
SSL_get_wbio(const SSL *s)
{
	return (s->wbio);
}
LSSL_ALIAS(SSL_get_wbio);

int
SSL_get_fd(const SSL *s)
{
	return (SSL_get_rfd(s));
}
LSSL_ALIAS(SSL_get_fd);

int
SSL_get_rfd(const SSL *s)
{
	int	 ret = -1;
	BIO	*b, *r;

	b = SSL_get_rbio(s);
	r = BIO_find_type(b, BIO_TYPE_DESCRIPTOR);
	if (r != NULL)
		BIO_get_fd(r, &ret);
	return (ret);
}
LSSL_ALIAS(SSL_get_rfd);

int
SSL_get_wfd(const SSL *s)
{
	int	 ret = -1;
	BIO	*b, *r;

	b = SSL_get_wbio(s);
	r = BIO_find_type(b, BIO_TYPE_DESCRIPTOR);
	if (r != NULL)
		BIO_get_fd(r, &ret);
	return (ret);
}
LSSL_ALIAS(SSL_get_wfd);

int
SSL_set_fd(SSL *s, int fd)
{
	int	 ret = 0;
	BIO	*bio = NULL;

	bio = BIO_new(BIO_s_socket());

	if (bio == NULL) {
		SSLerror(s, ERR_R_BUF_LIB);
		goto err;
	}
	BIO_set_fd(bio, fd, BIO_NOCLOSE);
	SSL_set_bio(s, bio, bio);
	ret = 1;
 err:
	return (ret);
}
LSSL_ALIAS(SSL_set_fd);

int
SSL_set_wfd(SSL *s, int fd)
{
	int	 ret = 0;
	BIO	*bio = NULL;

	if ((s->rbio == NULL) || (BIO_method_type(s->rbio) != BIO_TYPE_SOCKET)
	    || ((int)BIO_get_fd(s->rbio, NULL) != fd)) {
		bio = BIO_new(BIO_s_socket());

		if (bio == NULL) {
			SSLerror(s, ERR_R_BUF_LIB);
			goto err;
		}
		BIO_set_fd(bio, fd, BIO_NOCLOSE);
		SSL_set_bio(s, SSL_get_rbio(s), bio);
	} else
		SSL_set_bio(s, SSL_get_rbio(s), SSL_get_rbio(s));
	ret = 1;
 err:
	return (ret);
}
LSSL_ALIAS(SSL_set_wfd);

int
SSL_set_rfd(SSL *s, int fd)
{
	int	 ret = 0;
	BIO	*bio = NULL;

	if ((s->wbio == NULL) || (BIO_method_type(s->wbio) != BIO_TYPE_SOCKET)
	    || ((int)BIO_get_fd(s->wbio, NULL) != fd)) {
		bio = BIO_new(BIO_s_socket());

		if (bio == NULL) {
			SSLerror(s, ERR_R_BUF_LIB);
			goto err;
		}
		BIO_set_fd(bio, fd, BIO_NOCLOSE);
		SSL_set_bio(s, bio, SSL_get_wbio(s));
	} else
		SSL_set_bio(s, SSL_get_wbio(s), SSL_get_wbio(s));
	ret = 1;
 err:
	return (ret);
}
LSSL_ALIAS(SSL_set_rfd);


/* return length of latest Finished message we sent, copy to 'buf' */
size_t
SSL_get_finished(const SSL *s, void *buf, size_t count)
{
	size_t	ret;

	ret = s->s3->hs.finished_len;
	if (count > ret)
		count = ret;
	memcpy(buf, s->s3->hs.finished, count);
	return (ret);
}
LSSL_ALIAS(SSL_get_finished);

/* return length of latest Finished message we expected, copy to 'buf' */
size_t
SSL_get_peer_finished(const SSL *s, void *buf, size_t count)
{
	size_t	ret;

	ret = s->s3->hs.peer_finished_len;
	if (count > ret)
		count = ret;
	memcpy(buf, s->s3->hs.peer_finished, count);
	return (ret);
}
LSSL_ALIAS(SSL_get_peer_finished);


int
SSL_get_verify_mode(const SSL *s)
{
	return (s->verify_mode);
}
LSSL_ALIAS(SSL_get_verify_mode);

int
SSL_get_verify_depth(const SSL *s)
{
	return (X509_VERIFY_PARAM_get_depth(s->param));
}
LSSL_ALIAS(SSL_get_verify_depth);

int
(*SSL_get_verify_callback(const SSL *s))(int, X509_STORE_CTX *)
{
	return (s->verify_callback);
}
LSSL_ALIAS(SSL_get_verify_callback);

void
SSL_CTX_set_keylog_callback(SSL_CTX *ctx, SSL_CTX_keylog_cb_func cb)
{
	ctx->keylog_callback = cb;
}
LSSL_ALIAS(SSL_CTX_set_keylog_callback);

SSL_CTX_keylog_cb_func
SSL_CTX_get_keylog_callback(const SSL_CTX *ctx)
{
	return (ctx->keylog_callback);
}
LSSL_ALIAS(SSL_CTX_get_keylog_callback);

int
SSL_set_num_tickets(SSL *s, size_t num_tickets)
{
	s->num_tickets = num_tickets;

	return 1;
}
LSSL_ALIAS(SSL_set_num_tickets);

size_t
SSL_get_num_tickets(const SSL *s)
{
	return s->num_tickets;
}
LSSL_ALIAS(SSL_get_num_tickets);

int
SSL_CTX_set_num_tickets(SSL_CTX *ctx, size_t num_tickets)
{
	ctx->num_tickets = num_tickets;

	return 1;
}
LSSL_ALIAS(SSL_CTX_set_num_tickets);

size_t
SSL_CTX_get_num_tickets(const SSL_CTX *ctx)
{
	return ctx->num_tickets;
}
LSSL_ALIAS(SSL_CTX_get_num_tickets);

int
SSL_CTX_get_verify_mode(const SSL_CTX *ctx)
{
	return (ctx->verify_mode);
}
LSSL_ALIAS(SSL_CTX_get_verify_mode);

int
SSL_CTX_get_verify_depth(const SSL_CTX *ctx)
{
	return (X509_VERIFY_PARAM_get_depth(ctx->param));
}
LSSL_ALIAS(SSL_CTX_get_verify_depth);

int
(*SSL_CTX_get_verify_callback(const SSL_CTX *ctx))(int, X509_STORE_CTX *)
{
	return (ctx->default_verify_callback);
}
LSSL_ALIAS(SSL_CTX_get_verify_callback);

void
SSL_set_verify(SSL *s, int mode,
    int (*callback)(int ok, X509_STORE_CTX *ctx))
{
	s->verify_mode = mode;
	if (callback != NULL)
		s->verify_callback = callback;
}
LSSL_ALIAS(SSL_set_verify);

void
SSL_set_verify_depth(SSL *s, int depth)
{
	X509_VERIFY_PARAM_set_depth(s->param, depth);
}
LSSL_ALIAS(SSL_set_verify_depth);

void
SSL_set_read_ahead(SSL *s, int yes)
{
	s->read_ahead = yes;
}
LSSL_ALIAS(SSL_set_read_ahead);

int
SSL_get_read_ahead(const SSL *s)
{
	return (s->read_ahead);
}
LSSL_ALIAS(SSL_get_read_ahead);

int
SSL_pending(const SSL *s)
{
	return (s->method->ssl_pending(s));
}
LSSL_ALIAS(SSL_pending);

X509 *
SSL_get_peer_certificate(const SSL *s)
{
	X509 *cert;

	if (s == NULL || s->session == NULL)
		return NULL;

	if ((cert = s->session->peer_cert) == NULL)
		return NULL;

	X509_up_ref(cert);

	return cert;
}
LSSL_ALIAS(SSL_get_peer_certificate);

STACK_OF(X509) *
SSL_get_peer_cert_chain(const SSL *s)
{
	if (s == NULL)
		return NULL;

	/*
	 * Achtung! Due to API inconsistency, a client includes the peer's leaf
	 * certificate in the peer certificate chain, while a server does not.
	 */
	if (!s->server)
		return s->s3->hs.peer_certs;

	return s->s3->hs.peer_certs_no_leaf;
}
LSSL_ALIAS(SSL_get_peer_cert_chain);

STACK_OF(X509) *
SSL_get0_verified_chain(const SSL *s)
{
	if (s->s3 == NULL)
		return NULL;
	return s->s3->hs.verified_chain;
}
LSSL_ALIAS(SSL_get0_verified_chain);

/*
 * Now in theory, since the calling process own 't' it should be safe to
 * modify.  We need to be able to read f without being hassled
 */
int
SSL_copy_session_id(SSL *t, const SSL *f)
{
	SSL_CERT *tmp;

	/* Do we need to do SSL locking? */
	if (!SSL_set_session(t, SSL_get_session(f)))
		return 0;

	/* What if we are set up for one protocol but want to talk another? */
	if (t->method != f->method) {
		t->method->ssl_free(t);
		t->method = f->method;
		if (!t->method->ssl_new(t))
			return 0;
	}

	tmp = t->cert;
	if (f->cert != NULL) {
		CRYPTO_add(&f->cert->references, 1, CRYPTO_LOCK_SSL_CERT);
		t->cert = f->cert;
	} else
		t->cert = NULL;
	ssl_cert_free(tmp);

	if (!SSL_set_session_id_context(t, f->sid_ctx, f->sid_ctx_length))
		return 0;

	return 1;
}
LSSL_ALIAS(SSL_copy_session_id);

/* Fix this so it checks all the valid key/cert options */
int
SSL_CTX_check_private_key(const SSL_CTX *ctx)
{
	if ((ctx == NULL) || (ctx->cert == NULL) ||
	    (ctx->cert->key->x509 == NULL)) {
		SSLerrorx(SSL_R_NO_CERTIFICATE_ASSIGNED);
		return (0);
	}
	if (ctx->cert->key->privatekey == NULL) {
		SSLerrorx(SSL_R_NO_PRIVATE_KEY_ASSIGNED);
		return (0);
	}
	return (X509_check_private_key(ctx->cert->key->x509,
	    ctx->cert->key->privatekey));
}
LSSL_ALIAS(SSL_CTX_check_private_key);

/* Fix this function so that it takes an optional type parameter */
int
SSL_check_private_key(const SSL *ssl)
{
	if (ssl == NULL) {
		SSLerrorx(ERR_R_PASSED_NULL_PARAMETER);
		return (0);
	}
	if (ssl->cert == NULL) {
		SSLerror(ssl, SSL_R_NO_CERTIFICATE_ASSIGNED);
		return (0);
	}
	if (ssl->cert->key->x509 == NULL) {
		SSLerror(ssl, SSL_R_NO_CERTIFICATE_ASSIGNED);
		return (0);
	}
	if (ssl->cert->key->privatekey == NULL) {
		SSLerror(ssl, SSL_R_NO_PRIVATE_KEY_ASSIGNED);
		return (0);
	}
	return (X509_check_private_key(ssl->cert->key->x509,
	    ssl->cert->key->privatekey));
}
LSSL_ALIAS(SSL_check_private_key);

int
SSL_accept(SSL *s)
{
	if (s->handshake_func == NULL)
		SSL_set_accept_state(s); /* Not properly initialized yet */

	return (s->method->ssl_accept(s));
}
LSSL_ALIAS(SSL_accept);

int
SSL_connect(SSL *s)
{
	if (s->handshake_func == NULL)
		SSL_set_connect_state(s); /* Not properly initialized yet */

	return (s->method->ssl_connect(s));
}
LSSL_ALIAS(SSL_connect);

int
SSL_is_dtls(const SSL *s)
{
	return s->method->dtls;
}
LSSL_ALIAS(SSL_is_dtls);

int
SSL_is_server(const SSL *s)
{
	return s->server;
}
LSSL_ALIAS(SSL_is_server);

static long
ssl_get_default_timeout(void)
{
	/*
	 * 2 hours, the 24 hours mentioned in the TLSv1 spec
	 * is way too long for http, the cache would over fill.
	 */
	return (2 * 60 * 60);
}

long
SSL_get_default_timeout(const SSL *s)
{
	return (ssl_get_default_timeout());
}
LSSL_ALIAS(SSL_get_default_timeout);

int
SSL_read(SSL *s, void *buf, int num)
{
	if (num < 0) {
		SSLerror(s, SSL_R_BAD_LENGTH);
		return -1;
	}

	if (SSL_is_quic(s)) {
		SSLerror(s, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return (-1);
	}

	if (s->handshake_func == NULL) {
		SSLerror(s, SSL_R_UNINITIALIZED);
		return (-1);
	}

	if (s->shutdown & SSL_RECEIVED_SHUTDOWN) {
		s->rwstate = SSL_NOTHING;
		return (0);
	}
	return ssl3_read(s, buf, num);
}
LSSL_ALIAS(SSL_read);

int
SSL_read_ex(SSL *s, void *buf, size_t num, size_t *bytes_read)
{
	int ret;

	/* We simply don't bother supporting enormous reads */
	if (num > INT_MAX) {
		SSLerror(s, SSL_R_BAD_LENGTH);
		return 0;
	}

	ret = SSL_read(s, buf, (int)num);
	if (ret < 0)
		ret = 0;
	*bytes_read = ret;

	return ret > 0;
}
LSSL_ALIAS(SSL_read_ex);

int
SSL_peek(SSL *s, void *buf, int num)
{
	if (num < 0) {
		SSLerror(s, SSL_R_BAD_LENGTH);
		return -1;
	}

	if (SSL_is_quic(s)) {
		SSLerror(s, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return (-1);
	}

	if (s->handshake_func == NULL) {
		SSLerror(s, SSL_R_UNINITIALIZED);
		return (-1);
	}

	if (s->shutdown & SSL_RECEIVED_SHUTDOWN) {
		return (0);
	}
	return ssl3_peek(s, buf, num);
}
LSSL_ALIAS(SSL_peek);

int
SSL_peek_ex(SSL *s, void *buf, size_t num, size_t *bytes_peeked)
{
	int ret;

	/* We simply don't bother supporting enormous peeks */
	if (num > INT_MAX) {
		SSLerror(s, SSL_R_BAD_LENGTH);
		return 0;
	}

	ret = SSL_peek(s, buf, (int)num);
	if (ret < 0)
		ret = 0;
	*bytes_peeked = ret;

	return ret > 0;
}
LSSL_ALIAS(SSL_peek_ex);

int
SSL_write(SSL *s, const void *buf, int num)
{
	if (num < 0) {
		SSLerror(s, SSL_R_BAD_LENGTH);
		return -1;
	}

	if (SSL_is_quic(s)) {
		SSLerror(s, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return (-1);
	}

	if (s->handshake_func == NULL) {
		SSLerror(s, SSL_R_UNINITIALIZED);
		return (-1);
	}

	if (s->shutdown & SSL_SENT_SHUTDOWN) {
		s->rwstate = SSL_NOTHING;
		SSLerror(s, SSL_R_PROTOCOL_IS_SHUTDOWN);
		return (-1);
	}
	return ssl3_write(s, buf, num);
}
LSSL_ALIAS(SSL_write);

int
SSL_write_ex(SSL *s, const void *buf, size_t num, size_t *bytes_written)
{
	int ret;

	/* We simply don't bother supporting enormous writes */
	if (num > INT_MAX) {
		SSLerror(s, SSL_R_BAD_LENGTH);
		return 0;
	}

	if (num == 0) {
		/* This API is special */
		bytes_written = 0;
		return 1;
	}

	ret = SSL_write(s, buf, (int)num);
	if (ret < 0)
		ret = 0;
	*bytes_written = ret;

	return ret > 0;
}
LSSL_ALIAS(SSL_write_ex);

uint32_t
SSL_CTX_get_max_early_data(const SSL_CTX *ctx)
{
	return 0;
}
LSSL_ALIAS(SSL_CTX_get_max_early_data);

int
SSL_CTX_set_max_early_data(SSL_CTX *ctx, uint32_t max_early_data)
{
	return 1;
}
LSSL_ALIAS(SSL_CTX_set_max_early_data);

uint32_t
SSL_get_max_early_data(const SSL *s)
{
	return 0;
}
LSSL_ALIAS(SSL_get_max_early_data);

int
SSL_set_max_early_data(SSL *s, uint32_t max_early_data)
{
	return 1;
}
LSSL_ALIAS(SSL_set_max_early_data);

int
SSL_get_early_data_status(const SSL *s)
{
	return SSL_EARLY_DATA_REJECTED;
}
LSSL_ALIAS(SSL_get_early_data_status);

int
SSL_read_early_data(SSL *s, void *buf, size_t num, size_t *readbytes)
{
	*readbytes = 0;

	if (!s->server) {
		SSLerror(s, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return SSL_READ_EARLY_DATA_ERROR;
	}

	return SSL_READ_EARLY_DATA_FINISH;
}
LSSL_ALIAS(SSL_read_early_data);

int
SSL_write_early_data(SSL *s, const void *buf, size_t num, size_t *written)
{
	*written = 0;
	SSLerror(s, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
	return 0;
}
LSSL_ALIAS(SSL_write_early_data);

int
SSL_shutdown(SSL *s)
{
	/*
	 * Note that this function behaves differently from what one might
	 * expect.  Return values are 0 for no success (yet),
	 * 1 for success; but calling it once is usually not enough,
	 * even if blocking I/O is used (see ssl3_shutdown).
	 */

	if (s->handshake_func == NULL) {
		SSLerror(s, SSL_R_UNINITIALIZED);
		return (-1);
	}

	if (!SSL_in_init(s))
		return (s->method->ssl_shutdown(s));

	return (1);
}
LSSL_ALIAS(SSL_shutdown);

int
SSL_renegotiate(SSL *s)
{
	if ((s->options & SSL_OP_NO_RENEGOTIATION) != 0) {
		SSLerror(s, SSL_R_NO_RENEGOTIATION);
		return 0;
	}

	if (s->renegotiate == 0)
		s->renegotiate = 1;

	s->new_session = 1;

	return (s->method->ssl_renegotiate(s));
}
LSSL_ALIAS(SSL_renegotiate);

int
SSL_renegotiate_abbreviated(SSL *s)
{
	if ((s->options & SSL_OP_NO_RENEGOTIATION) != 0) {
		SSLerror(s, SSL_R_NO_RENEGOTIATION);
		return 0;
	}

	if (s->renegotiate == 0)
		s->renegotiate = 1;

	s->new_session = 0;

	return (s->method->ssl_renegotiate(s));
}
LSSL_ALIAS(SSL_renegotiate_abbreviated);

int
SSL_renegotiate_pending(SSL *s)
{
	/*
	 * Becomes true when negotiation is requested;
	 * false again once a handshake has finished.
	 */
	return (s->renegotiate != 0);
}
LSSL_ALIAS(SSL_renegotiate_pending);

long
SSL_ctrl(SSL *s, int cmd, long larg, void *parg)
{
	long	l;

	switch (cmd) {
	case SSL_CTRL_GET_READ_AHEAD:
		return (s->read_ahead);
	case SSL_CTRL_SET_READ_AHEAD:
		l = s->read_ahead;
		s->read_ahead = larg;
		return (l);

	case SSL_CTRL_SET_MSG_CALLBACK_ARG:
		s->msg_callback_arg = parg;
		return (1);

	case SSL_CTRL_OPTIONS:
		return (s->options|=larg);
	case SSL_CTRL_CLEAR_OPTIONS:
		return (s->options&=~larg);
	case SSL_CTRL_MODE:
		return (s->mode|=larg);
	case SSL_CTRL_CLEAR_MODE:
		return (s->mode &=~larg);
	case SSL_CTRL_GET_MAX_CERT_LIST:
		return (s->max_cert_list);
	case SSL_CTRL_SET_MAX_CERT_LIST:
		l = s->max_cert_list;
		s->max_cert_list = larg;
		return (l);
	case SSL_CTRL_SET_MTU:
		if (larg < (long)dtls1_min_mtu())
			return (0);
		if (SSL_is_dtls(s)) {
			s->d1->mtu = larg;
			return (larg);
		}
		return (0);
	case SSL_CTRL_SET_MAX_SEND_FRAGMENT:
		if (larg < 512 || larg > SSL3_RT_MAX_PLAIN_LENGTH)
			return (0);
		s->max_send_fragment = larg;
		return (1);
	case SSL_CTRL_GET_RI_SUPPORT:
		if (s->s3)
			return (s->s3->send_connection_binding);
		else return (0);
	default:
		if (SSL_is_dtls(s))
			return dtls1_ctrl(s, cmd, larg, parg);
		return ssl3_ctrl(s, cmd, larg, parg);
	}
}
LSSL_ALIAS(SSL_ctrl);

long
SSL_callback_ctrl(SSL *s, int cmd, void (*fp)(void))
{
	switch (cmd) {
	case SSL_CTRL_SET_MSG_CALLBACK:
		s->msg_callback = (ssl_msg_callback_fn *)(fp);
		return (1);

	default:
		return (ssl3_callback_ctrl(s, cmd, fp));
	}
}
LSSL_ALIAS(SSL_callback_ctrl);

struct lhash_st_SSL_SESSION *
SSL_CTX_sessions(SSL_CTX *ctx)
{
	return (ctx->sessions);
}
LSSL_ALIAS(SSL_CTX_sessions);

long
SSL_CTX_ctrl(SSL_CTX *ctx, int cmd, long larg, void *parg)
{
	long	l;

	switch (cmd) {
	case SSL_CTRL_GET_READ_AHEAD:
		return (ctx->read_ahead);
	case SSL_CTRL_SET_READ_AHEAD:
		l = ctx->read_ahead;
		ctx->read_ahead = larg;
		return (l);

	case SSL_CTRL_SET_MSG_CALLBACK_ARG:
		ctx->msg_callback_arg = parg;
		return (1);

	case SSL_CTRL_GET_MAX_CERT_LIST:
		return (ctx->max_cert_list);
	case SSL_CTRL_SET_MAX_CERT_LIST:
		l = ctx->max_cert_list;
		ctx->max_cert_list = larg;
		return (l);

	case SSL_CTRL_SET_SESS_CACHE_SIZE:
		l = ctx->session_cache_size;
		ctx->session_cache_size = larg;
		return (l);
	case SSL_CTRL_GET_SESS_CACHE_SIZE:
		return (ctx->session_cache_size);
	case SSL_CTRL_SET_SESS_CACHE_MODE:
		l = ctx->session_cache_mode;
		ctx->session_cache_mode = larg;
		return (l);
	case SSL_CTRL_GET_SESS_CACHE_MODE:
		return (ctx->session_cache_mode);

	case SSL_CTRL_SESS_NUMBER:
		return (lh_SSL_SESSION_num_items(ctx->sessions));
	case SSL_CTRL_SESS_CONNECT:
		return (ctx->stats.sess_connect);
	case SSL_CTRL_SESS_CONNECT_GOOD:
		return (ctx->stats.sess_connect_good);
	case SSL_CTRL_SESS_CONNECT_RENEGOTIATE:
		return (ctx->stats.sess_connect_renegotiate);
	case SSL_CTRL_SESS_ACCEPT:
		return (ctx->stats.sess_accept);
	case SSL_CTRL_SESS_ACCEPT_GOOD:
		return (ctx->stats.sess_accept_good);
	case SSL_CTRL_SESS_ACCEPT_RENEGOTIATE:
		return (ctx->stats.sess_accept_renegotiate);
	case SSL_CTRL_SESS_HIT:
		return (ctx->stats.sess_hit);
	case SSL_CTRL_SESS_CB_HIT:
		return (ctx->stats.sess_cb_hit);
	case SSL_CTRL_SESS_MISSES:
		return (ctx->stats.sess_miss);
	case SSL_CTRL_SESS_TIMEOUTS:
		return (ctx->stats.sess_timeout);
	case SSL_CTRL_SESS_CACHE_FULL:
		return (ctx->stats.sess_cache_full);
	case SSL_CTRL_OPTIONS:
		return (ctx->options|=larg);
	case SSL_CTRL_CLEAR_OPTIONS:
		return (ctx->options&=~larg);
	case SSL_CTRL_MODE:
		return (ctx->mode|=larg);
	case SSL_CTRL_CLEAR_MODE:
		return (ctx->mode&=~larg);
	case SSL_CTRL_SET_MAX_SEND_FRAGMENT:
		if (larg < 512 || larg > SSL3_RT_MAX_PLAIN_LENGTH)
			return (0);
		ctx->max_send_fragment = larg;
		return (1);
	default:
		return (ssl3_ctx_ctrl(ctx, cmd, larg, parg));
	}
}
LSSL_ALIAS(SSL_CTX_ctrl);

long
SSL_CTX_callback_ctrl(SSL_CTX *ctx, int cmd, void (*fp)(void))
{
	switch (cmd) {
	case SSL_CTRL_SET_MSG_CALLBACK:
		ctx->msg_callback = (ssl_msg_callback_fn *)fp;
		return (1);

	default:
		return (ssl3_ctx_callback_ctrl(ctx, cmd, fp));
	}
}
LSSL_ALIAS(SSL_CTX_callback_ctrl);

STACK_OF(SSL_CIPHER) *
SSL_get_ciphers(const SSL *s)
{
	if (s == NULL)
		return (NULL);
	if (s->cipher_list != NULL)
		return (s->cipher_list);

	return (s->ctx->cipher_list);
}
LSSL_ALIAS(SSL_get_ciphers);

STACK_OF(SSL_CIPHER) *
SSL_get_client_ciphers(const SSL *s)
{
	if (s == NULL || !s->server)
		return NULL;
	return s->s3->hs.client_ciphers;
}
LSSL_ALIAS(SSL_get_client_ciphers);

STACK_OF(SSL_CIPHER) *
SSL_get1_supported_ciphers(SSL *s)
{
	STACK_OF(SSL_CIPHER) *supported_ciphers = NULL, *ciphers;
	SSL_CIPHER *cipher;
	uint16_t min_vers, max_vers;
	int i;

	if (s == NULL)
		return NULL;
	if (!ssl_supported_tls_version_range(s, &min_vers, &max_vers))
		return NULL;
	if ((ciphers = SSL_get_ciphers(s)) == NULL)
		return NULL;
	if ((supported_ciphers = sk_SSL_CIPHER_new_null()) == NULL)
		return NULL;

	for (i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
		if ((cipher = sk_SSL_CIPHER_value(ciphers, i)) == NULL)
			goto err;
		if (!ssl_cipher_allowed_in_tls_version_range(cipher, min_vers,
		    max_vers))
			continue;
		if (!ssl_security_supported_cipher(s, cipher))
			continue;
		if (!sk_SSL_CIPHER_push(supported_ciphers, cipher))
			goto err;
	}

	if (sk_SSL_CIPHER_num(supported_ciphers) > 0)
		return supported_ciphers;

 err:
	sk_SSL_CIPHER_free(supported_ciphers);
	return NULL;
}
LSSL_ALIAS(SSL_get1_supported_ciphers);

/* See if we have any ECC cipher suites. */
int
ssl_has_ecc_ciphers(SSL *s)
{
	STACK_OF(SSL_CIPHER) *ciphers;
	unsigned long alg_k, alg_a;
	SSL_CIPHER *cipher;
	int i;

	if ((ciphers = SSL_get_ciphers(s)) == NULL)
		return 0;

	for (i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
		cipher = sk_SSL_CIPHER_value(ciphers, i);

		alg_k = cipher->algorithm_mkey;
		alg_a = cipher->algorithm_auth;

		if ((alg_k & SSL_kECDHE) || (alg_a & SSL_aECDSA))
			return 1;
	}

	return 0;
}

/* The old interface to get the same thing as SSL_get_ciphers(). */
const char *
SSL_get_cipher_list(const SSL *s, int n)
{
	STACK_OF(SSL_CIPHER) *ciphers;
	const SSL_CIPHER *cipher;

	if ((ciphers = SSL_get_ciphers(s)) == NULL)
		return (NULL);
	if ((cipher = sk_SSL_CIPHER_value(ciphers, n)) == NULL)
		return (NULL);

	return (cipher->name);
}
LSSL_ALIAS(SSL_get_cipher_list);

STACK_OF(SSL_CIPHER) *
SSL_CTX_get_ciphers(const SSL_CTX *ctx)
{
	if (ctx == NULL)
		return NULL;
	return ctx->cipher_list;
}
LSSL_ALIAS(SSL_CTX_get_ciphers);

/* Specify the ciphers to be used by default by the SSL_CTX. */
int
SSL_CTX_set_cipher_list(SSL_CTX *ctx, const char *str)
{
	STACK_OF(SSL_CIPHER) *ciphers;

	/*
	 * ssl_create_cipher_list may return an empty stack if it was unable to
	 * find a cipher matching the given rule string (for example if the
	 * rule string specifies a cipher which has been disabled). This is not
	 * an error as far as ssl_create_cipher_list is concerned, and hence
	 * ctx->cipher_list has been updated.
	 */
	ciphers = ssl_create_cipher_list(ctx->method, &ctx->cipher_list,
	    ctx->cipher_list_tls13, str, ctx->cert);
	if (ciphers == NULL) {
		return (0);
	} else if (sk_SSL_CIPHER_num(ciphers) == 0) {
		SSLerrorx(SSL_R_NO_CIPHER_MATCH);
		return (0);
	}
	return (1);
}
LSSL_ALIAS(SSL_CTX_set_cipher_list);

int
SSL_CTX_set_ciphersuites(SSL_CTX *ctx, const char *str)
{
	if (!ssl_parse_ciphersuites(&ctx->cipher_list_tls13, str)) {
		SSLerrorx(SSL_R_NO_CIPHER_MATCH);
		return 0;
	}
	if (!ssl_merge_cipherlists(ctx->cipher_list,
	    ctx->cipher_list_tls13, &ctx->cipher_list))
		return 0;

	return 1;
}
LSSL_ALIAS(SSL_CTX_set_ciphersuites);

/* Specify the ciphers to be used by the SSL. */
int
SSL_set_cipher_list(SSL *s, const char *str)
{
	STACK_OF(SSL_CIPHER) *ciphers, *ciphers_tls13;

	if ((ciphers_tls13 = s->cipher_list_tls13) == NULL)
		ciphers_tls13 = s->ctx->cipher_list_tls13;

	/* See comment in SSL_CTX_set_cipher_list. */
	ciphers = ssl_create_cipher_list(s->ctx->method, &s->cipher_list,
	    ciphers_tls13, str, s->cert);
	if (ciphers == NULL) {
		return (0);
	} else if (sk_SSL_CIPHER_num(ciphers) == 0) {
		SSLerror(s, SSL_R_NO_CIPHER_MATCH);
		return (0);
	}
	return (1);
}
LSSL_ALIAS(SSL_set_cipher_list);

int
SSL_set_ciphersuites(SSL *s, const char *str)
{
	STACK_OF(SSL_CIPHER) *ciphers;

	if ((ciphers = s->cipher_list) == NULL)
		ciphers = s->ctx->cipher_list;

	if (!ssl_parse_ciphersuites(&s->cipher_list_tls13, str)) {
		SSLerrorx(SSL_R_NO_CIPHER_MATCH);
		return (0);
	}
	if (!ssl_merge_cipherlists(ciphers, s->cipher_list_tls13,
	    &s->cipher_list))
		return 0;

	return 1;
}
LSSL_ALIAS(SSL_set_ciphersuites);

char *
SSL_get_shared_ciphers(const SSL *s, char *buf, int len)
{
	STACK_OF(SSL_CIPHER) *client_ciphers, *server_ciphers;
	const SSL_CIPHER *cipher;
	size_t curlen = 0;
	char *end;
	int i;

	if (!s->server || len < 2)
		return NULL;

	if ((client_ciphers = s->s3->hs.client_ciphers) == NULL)
		return NULL;
	if ((server_ciphers = SSL_get_ciphers(s)) == NULL)
		return NULL;
	if (sk_SSL_CIPHER_num(client_ciphers) == 0 ||
	    sk_SSL_CIPHER_num(server_ciphers) == 0)
		return NULL;

	buf[0] = '\0';
	for (i = 0; i < sk_SSL_CIPHER_num(client_ciphers); i++) {
		cipher = sk_SSL_CIPHER_value(client_ciphers, i);

		if (sk_SSL_CIPHER_find(server_ciphers, cipher) < 0)
			continue;

		end = buf + curlen;
		if (strlcat(buf, cipher->name, len) >= len ||
		    (curlen = strlcat(buf, ":", len)) >= len) {
			/* remove truncated cipher from list */
			*end = '\0';
			break;
		}
	}
	/* remove trailing colon */
	if ((end = strrchr(buf, ':')) != NULL)
		*end = '\0';
	return buf;
}
LSSL_ALIAS(SSL_get_shared_ciphers);

/*
 * Return a servername extension value if provided in Client Hello, or NULL.
 * So far, only host_name types are defined (RFC 3546).
 */
const char *
SSL_get_servername(const SSL *s, const int type)
{
	if (type != TLSEXT_NAMETYPE_host_name)
		return (NULL);

	return (s->session && !s->tlsext_hostname ?
	    s->session->tlsext_hostname :
	    s->tlsext_hostname);
}
LSSL_ALIAS(SSL_get_servername);

int
SSL_get_servername_type(const SSL *s)
{
	if (s->session &&
	    (!s->tlsext_hostname ?
	    s->session->tlsext_hostname : s->tlsext_hostname))
		return (TLSEXT_NAMETYPE_host_name);
	return (-1);
}
LSSL_ALIAS(SSL_get_servername_type);

/*
 * SSL_select_next_proto implements standard protocol selection. It is
 * expected that this function is called from the callback set by
 * SSL_CTX_set_alpn_select_cb.
 *
 * The protocol data is assumed to be a vector of 8-bit, length prefixed byte
 * strings. The length byte itself is not included in the length. A byte
 * string of length 0 is invalid. No byte string may be truncated.
 *
 * It returns either:
 * OPENSSL_NPN_NEGOTIATED if a common protocol was found, or
 * OPENSSL_NPN_NO_OVERLAP if the fallback case was reached.
 *
 * XXX - the out argument points into server_list or client_list and should
 * therefore really be const. We can't fix that without breaking the callers.
 */
int
SSL_select_next_proto(unsigned char **out, unsigned char *outlen,
    const unsigned char *peer_list, unsigned int peer_list_len,
    const unsigned char *supported_list, unsigned int supported_list_len)
{
	CBS peer, peer_proto, supported, supported_proto;

	*out = NULL;
	*outlen = 0;

	/* First check that the supported list is well-formed. */
	CBS_init(&supported, supported_list, supported_list_len);
	if (!tlsext_alpn_check_format(&supported))
		goto err;

	/*
	 * Use first supported protocol as fallback. This is one way of doing
	 * NPN's "opportunistic" protocol selection (see security considerations
	 * in draft-agl-tls-nextprotoneg-04), and it is the documented behavior
	 * of this API. For ALPN it's the callback's responsibility to fail on
	 * OPENSSL_NPN_NO_OVERLAP.
	 */

	if (!CBS_get_u8_length_prefixed(&supported, &supported_proto))
		goto err;

	*out = (unsigned char *)CBS_data(&supported_proto);
	*outlen = CBS_len(&supported_proto);

	/* Now check that the peer list is well-formed. */
	CBS_init(&peer, peer_list, peer_list_len);
	if (!tlsext_alpn_check_format(&peer))
		goto err;

	/*
	 * Walk the peer list and select the first protocol that appears in
	 * the supported list. Thus we honor peer preference rather than local
	 * preference contrary to a SHOULD in RFC 7301, section 3.2.
	 */
	while (CBS_len(&peer) > 0) {
		if (!CBS_get_u8_length_prefixed(&peer, &peer_proto))
			goto err;

		CBS_init(&supported, supported_list, supported_list_len);

		while (CBS_len(&supported) > 0) {
			if (!CBS_get_u8_length_prefixed(&supported,
			    &supported_proto))
				goto err;

			if (CBS_mem_equal(&supported_proto,
			    CBS_data(&peer_proto), CBS_len(&peer_proto))) {
				*out = (unsigned char *)CBS_data(&peer_proto);
				*outlen = CBS_len(&peer_proto);

				return OPENSSL_NPN_NEGOTIATED;
			}
		}
	}

 err:
	return OPENSSL_NPN_NO_OVERLAP;
}
LSSL_ALIAS(SSL_select_next_proto);

/* SSL_get0_next_proto_negotiated is deprecated. */
void
SSL_get0_next_proto_negotiated(const SSL *s, const unsigned char **data,
    unsigned int *len)
{
	*data = NULL;
	*len = 0;
}
LSSL_ALIAS(SSL_get0_next_proto_negotiated);

/* SSL_CTX_set_next_protos_advertised_cb is deprecated. */
void
SSL_CTX_set_next_protos_advertised_cb(SSL_CTX *ctx, int (*cb) (SSL *ssl,
    const unsigned char **out, unsigned int *outlen, void *arg), void *arg)
{
}
LSSL_ALIAS(SSL_CTX_set_next_protos_advertised_cb);

/* SSL_CTX_set_next_proto_select_cb is deprecated. */
void
SSL_CTX_set_next_proto_select_cb(SSL_CTX *ctx, int (*cb) (SSL *s,
    unsigned char **out, unsigned char *outlen, const unsigned char *in,
    unsigned int inlen, void *arg), void *arg)
{
}
LSSL_ALIAS(SSL_CTX_set_next_proto_select_cb);

/*
 * SSL_CTX_set_alpn_protos sets the ALPN protocol list to the specified
 * protocols, which must be in wire-format (i.e. a series of non-empty,
 * 8-bit length-prefixed strings). Returns 0 on success.
 */
int
SSL_CTX_set_alpn_protos(SSL_CTX *ctx, const unsigned char *protos,
    unsigned int protos_len)
{
	CBS cbs;
	int failed = 1;

	if (protos == NULL)
		protos_len = 0;

	CBS_init(&cbs, protos, protos_len);

	if (protos_len > 0) {
		if (!tlsext_alpn_check_format(&cbs))
			goto err;
	}

	if (!CBS_stow(&cbs, &ctx->alpn_client_proto_list,
	    &ctx->alpn_client_proto_list_len))
		goto err;

	failed = 0;

 err:
	/* NOTE: Return values are the reverse of what you expect. */
	return failed;
}
LSSL_ALIAS(SSL_CTX_set_alpn_protos);

/*
 * SSL_set_alpn_protos sets the ALPN protocol list to the specified
 * protocols, which must be in wire-format (i.e. a series of non-empty,
 * 8-bit length-prefixed strings). Returns 0 on success.
 */
int
SSL_set_alpn_protos(SSL *ssl, const unsigned char *protos,
    unsigned int protos_len)
{
	CBS cbs;
	int failed = 1;

	if (protos == NULL)
		protos_len = 0;

	CBS_init(&cbs, protos, protos_len);

	if (protos_len > 0) {
		if (!tlsext_alpn_check_format(&cbs))
			goto err;
	}

	if (!CBS_stow(&cbs, &ssl->alpn_client_proto_list,
	    &ssl->alpn_client_proto_list_len))
		goto err;

	failed = 0;

 err:
	/* NOTE: Return values are the reverse of what you expect. */
	return failed;
}
LSSL_ALIAS(SSL_set_alpn_protos);

/*
 * SSL_CTX_set_alpn_select_cb sets a callback function that is called during
 * ClientHello processing in order to select an ALPN protocol from the
 * client's list of offered protocols.
 */
void
SSL_CTX_set_alpn_select_cb(SSL_CTX* ctx,
    int (*cb) (SSL *ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg), void *arg)
{
	ctx->alpn_select_cb = cb;
	ctx->alpn_select_cb_arg = arg;
}
LSSL_ALIAS(SSL_CTX_set_alpn_select_cb);

/*
 * SSL_get0_alpn_selected gets the selected ALPN protocol (if any). On return
 * it sets data to point to len bytes of protocol name (not including the
 * leading length-prefix byte). If the server didn't respond with* a negotiated
 * protocol then len will be zero.
 */
void
SSL_get0_alpn_selected(const SSL *ssl, const unsigned char **data,
    unsigned int *len)
{
	*data = ssl->s3->alpn_selected;
	*len = ssl->s3->alpn_selected_len;
}
LSSL_ALIAS(SSL_get0_alpn_selected);

void
SSL_set_psk_use_session_callback(SSL *s, SSL_psk_use_session_cb_func cb)
{
	return;
}
LSSL_ALIAS(SSL_set_psk_use_session_callback);

int
SSL_export_keying_material(SSL *s, unsigned char *out, size_t out_len,
    const char *label, size_t label_len, const unsigned char *context,
    size_t context_len, int use_context)
{
	if (s->tls13 != NULL && s->version == TLS1_3_VERSION) {
		if (!use_context) {
			context = NULL;
			context_len = 0;
		}
		return tls13_exporter(s->tls13, label, label_len, context,
		    context_len, out, out_len);
	}

	return tls12_exporter(s, label, label_len, context, context_len,
	    use_context, out, out_len);
}
LSSL_ALIAS(SSL_export_keying_material);

static unsigned long
ssl_session_hash(const SSL_SESSION *a)
{
	unsigned long	l;

	l = (unsigned long)
	    ((unsigned int) a->session_id[0]     )|
	    ((unsigned int) a->session_id[1]<< 8L)|
	    ((unsigned long)a->session_id[2]<<16L)|
	    ((unsigned long)a->session_id[3]<<24L);
	return (l);
}

/*
 * NB: If this function (or indeed the hash function which uses a sort of
 * coarser function than this one) is changed, ensure
 * SSL_CTX_has_matching_session_id() is checked accordingly. It relies on being
 * able to construct an SSL_SESSION that will collide with any existing session
 * with a matching session ID.
 */
static int
ssl_session_cmp(const SSL_SESSION *a, const SSL_SESSION *b)
{
	if (a->ssl_version != b->ssl_version)
		return (1);
	if (a->session_id_length != b->session_id_length)
		return (1);
	if (timingsafe_memcmp(a->session_id, b->session_id, a->session_id_length) != 0)
		return (1);
	return (0);
}

/*
 * These wrapper functions should remain rather than redeclaring
 * SSL_SESSION_hash and SSL_SESSION_cmp for void* types and casting each
 * variable. The reason is that the functions aren't static, they're exposed via
 * ssl.h.
 */
static unsigned long
ssl_session_LHASH_HASH(const void *arg)
{
	const SSL_SESSION *a = arg;

	return ssl_session_hash(a);
}

static int
ssl_session_LHASH_COMP(const void *arg1, const void *arg2)
{
	const SSL_SESSION *a = arg1;
	const SSL_SESSION *b = arg2;

	return ssl_session_cmp(a, b);
}

SSL_CTX *
SSL_CTX_new(const SSL_METHOD *meth)
{
	SSL_CTX	*ret;

	if (!OPENSSL_init_ssl(0, NULL)) {
		SSLerrorx(SSL_R_LIBRARY_BUG);
		return (NULL);
	}

	if (meth == NULL) {
		SSLerrorx(SSL_R_NULL_SSL_METHOD_PASSED);
		return (NULL);
	}

	if ((ret = calloc(1, sizeof(*ret))) == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}

	if (SSL_get_ex_data_X509_STORE_CTX_idx() < 0) {
		SSLerrorx(SSL_R_X509_VERIFICATION_SETUP_PROBLEMS);
		goto err;
	}

	ret->method = meth;
	ret->min_tls_version = meth->min_tls_version;
	ret->max_tls_version = meth->max_tls_version;
	ret->min_proto_version = 0;
	ret->max_proto_version = 0;
	ret->mode = SSL_MODE_AUTO_RETRY;

	ret->cert_store = NULL;
	ret->session_cache_mode = SSL_SESS_CACHE_SERVER;
	ret->session_cache_size = SSL_SESSION_CACHE_MAX_SIZE_DEFAULT;
	ret->session_cache_head = NULL;
	ret->session_cache_tail = NULL;

	/* We take the system default */
	ret->session_timeout = ssl_get_default_timeout();

	ret->new_session_cb = NULL;
	ret->remove_session_cb = NULL;
	ret->get_session_cb = NULL;
	ret->generate_session_id = NULL;

	memset((char *)&ret->stats, 0, sizeof(ret->stats));

	ret->references = 1;
	ret->quiet_shutdown = 0;

	ret->info_callback = NULL;

	ret->app_verify_callback = NULL;
	ret->app_verify_arg = NULL;

	ret->max_cert_list = SSL_MAX_CERT_LIST_DEFAULT;
	ret->read_ahead = 0;
	ret->msg_callback = NULL;
	ret->msg_callback_arg = NULL;
	ret->verify_mode = SSL_VERIFY_NONE;
	ret->sid_ctx_length = 0;
	ret->default_verify_callback = NULL;

	if ((ret->cert = ssl_cert_new()) == NULL)
		goto err;

	ret->default_passwd_callback = NULL;
	ret->default_passwd_callback_userdata = NULL;
	ret->client_cert_cb = NULL;
	ret->app_gen_cookie_cb = NULL;
	ret->app_verify_cookie_cb = NULL;

	ret->sessions = lh_SSL_SESSION_new();
	if (ret->sessions == NULL)
		goto err;
	ret->cert_store = X509_STORE_new();
	if (ret->cert_store == NULL)
		goto err;

	ssl_create_cipher_list(ret->method, &ret->cipher_list,
	    NULL, SSL_DEFAULT_CIPHER_LIST, ret->cert);
	if (ret->cipher_list == NULL ||
	    sk_SSL_CIPHER_num(ret->cipher_list) <= 0) {
		SSLerrorx(SSL_R_LIBRARY_HAS_NO_CIPHERS);
		goto err2;
	}

	ret->param = X509_VERIFY_PARAM_new();
	if (!ret->param)
		goto err;

	if ((ret->client_CA = sk_X509_NAME_new_null()) == NULL)
		goto err;

	CRYPTO_new_ex_data(CRYPTO_EX_INDEX_SSL_CTX, ret, &ret->ex_data);

	ret->extra_certs = NULL;

	ret->max_send_fragment = SSL3_RT_MAX_PLAIN_LENGTH;

	ret->tlsext_servername_callback = 0;
	ret->tlsext_servername_arg = NULL;

	/* Setup RFC4507 ticket keys */
	arc4random_buf(ret->tlsext_tick_key_name, 16);
	arc4random_buf(ret->tlsext_tick_hmac_key, 16);
	arc4random_buf(ret->tlsext_tick_aes_key, 16);

	ret->tlsext_status_cb = 0;
	ret->tlsext_status_arg = NULL;

	/*
	 * Default is to connect to non-RI servers. When RI is more widely
	 * deployed might change this.
	 */
	ret->options |= SSL_OP_LEGACY_SERVER_CONNECT;

	return (ret);
 err:
	SSLerrorx(ERR_R_MALLOC_FAILURE);
 err2:
	SSL_CTX_free(ret);
	return (NULL);
}
LSSL_ALIAS(SSL_CTX_new);

void
SSL_CTX_free(SSL_CTX *ctx)
{
	int	i;

	if (ctx == NULL)
		return;

	i = CRYPTO_add(&ctx->references, -1, CRYPTO_LOCK_SSL_CTX);
	if (i > 0)
		return;

	X509_VERIFY_PARAM_free(ctx->param);

	/*
	 * Free internal session cache. However: the remove_cb() may reference
	 * the ex_data of SSL_CTX, thus the ex_data store can only be removed
	 * after the sessions were flushed.
	 * As the ex_data handling routines might also touch the session cache,
	 * the most secure solution seems to be: empty (flush) the cache, then
	 * free ex_data, then finally free the cache.
	 * (See ticket [openssl.org #212].)
	 */
	if (ctx->sessions != NULL)
		SSL_CTX_flush_sessions(ctx, 0);

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_SSL_CTX, ctx, &ctx->ex_data);

	lh_SSL_SESSION_free(ctx->sessions);

	X509_STORE_free(ctx->cert_store);
	sk_SSL_CIPHER_free(ctx->cipher_list);
	sk_SSL_CIPHER_free(ctx->cipher_list_tls13);
	ssl_cert_free(ctx->cert);
	sk_X509_NAME_pop_free(ctx->client_CA, X509_NAME_free);
	sk_X509_pop_free(ctx->extra_certs, X509_free);

#ifndef OPENSSL_NO_SRTP
	if (ctx->srtp_profiles)
		sk_SRTP_PROTECTION_PROFILE_free(ctx->srtp_profiles);
#endif

	free(ctx->tlsext_ecpointformatlist);
	free(ctx->tlsext_supportedgroups);

	free(ctx->alpn_client_proto_list);

	free(ctx);
}
LSSL_ALIAS(SSL_CTX_free);

int
SSL_CTX_up_ref(SSL_CTX *ctx)
{
	return CRYPTO_add(&ctx->references, 1, CRYPTO_LOCK_SSL_CTX) > 1;
}
LSSL_ALIAS(SSL_CTX_up_ref);

pem_password_cb *
SSL_CTX_get_default_passwd_cb(SSL_CTX *ctx)
{
	return (ctx->default_passwd_callback);
}
LSSL_ALIAS(SSL_CTX_get_default_passwd_cb);

void
SSL_CTX_set_default_passwd_cb(SSL_CTX *ctx, pem_password_cb *cb)
{
	ctx->default_passwd_callback = cb;
}
LSSL_ALIAS(SSL_CTX_set_default_passwd_cb);

void *
SSL_CTX_get_default_passwd_cb_userdata(SSL_CTX *ctx)
{
	return ctx->default_passwd_callback_userdata;
}
LSSL_ALIAS(SSL_CTX_get_default_passwd_cb_userdata);

void
SSL_CTX_set_default_passwd_cb_userdata(SSL_CTX *ctx, void *u)
{
	ctx->default_passwd_callback_userdata = u;
}
LSSL_ALIAS(SSL_CTX_set_default_passwd_cb_userdata);

void
SSL_CTX_set_cert_verify_callback(SSL_CTX *ctx,
    int (*cb)(X509_STORE_CTX *, void *), void *arg)
{
	ctx->app_verify_callback = cb;
	ctx->app_verify_arg = arg;
}
LSSL_ALIAS(SSL_CTX_set_cert_verify_callback);

void
SSL_CTX_set_verify(SSL_CTX *ctx, int mode, int (*cb)(int, X509_STORE_CTX *))
{
	ctx->verify_mode = mode;
	ctx->default_verify_callback = cb;
}
LSSL_ALIAS(SSL_CTX_set_verify);

void
SSL_CTX_set_verify_depth(SSL_CTX *ctx, int depth)
{
	X509_VERIFY_PARAM_set_depth(ctx->param, depth);
}
LSSL_ALIAS(SSL_CTX_set_verify_depth);

void
ssl_set_cert_masks(SSL_CERT *c, const SSL_CIPHER *cipher)
{
	unsigned long mask_a, mask_k;
	SSL_CERT_PKEY *cpk;

	if (c == NULL)
		return;

	mask_a = SSL_aNULL | SSL_aTLS1_3;
	mask_k = SSL_kECDHE | SSL_kTLS1_3;

	if (c->dhe_params != NULL || c->dhe_params_cb != NULL ||
	    c->dhe_params_auto != 0)
		mask_k |= SSL_kDHE;

	cpk = &(c->pkeys[SSL_PKEY_ECC]);
	if (cpk->x509 != NULL && cpk->privatekey != NULL) {
		/* Key usage, if present, must allow signing. */
		if (X509_get_key_usage(cpk->x509) & X509v3_KU_DIGITAL_SIGNATURE)
			mask_a |= SSL_aECDSA;
	}

	cpk = &(c->pkeys[SSL_PKEY_RSA]);
	if (cpk->x509 != NULL && cpk->privatekey != NULL) {
		mask_a |= SSL_aRSA;
		mask_k |= SSL_kRSA;
	}

	c->mask_k = mask_k;
	c->mask_a = mask_a;
	c->valid = 1;
}

/* See if this handshake is using an ECC cipher suite. */
int
ssl_using_ecc_cipher(SSL *s)
{
	unsigned long alg_a, alg_k;

	alg_a = s->s3->hs.cipher->algorithm_auth;
	alg_k = s->s3->hs.cipher->algorithm_mkey;

	return s->session->tlsext_ecpointformatlist != NULL &&
	    s->session->tlsext_ecpointformatlist_length > 0 &&
	    ((alg_k & SSL_kECDHE) || (alg_a & SSL_aECDSA));
}

int
ssl_check_srvr_ecc_cert_and_alg(SSL *s, X509 *x)
{
	const SSL_CIPHER *cs = s->s3->hs.cipher;
	unsigned long alg_a;

	alg_a = cs->algorithm_auth;

	if (alg_a & SSL_aECDSA) {
		/* Key usage, if present, must allow signing. */
		if (!(X509_get_key_usage(x) & X509v3_KU_DIGITAL_SIGNATURE)) {
			SSLerror(s, SSL_R_ECC_CERT_NOT_FOR_SIGNING);
			return (0);
		}
	}

	return (1);
}

SSL_CERT_PKEY *
ssl_get_server_send_pkey(const SSL *s)
{
	unsigned long alg_a;
	SSL_CERT *c;
	int i;

	c = s->cert;
	ssl_set_cert_masks(c, s->s3->hs.cipher);

	alg_a = s->s3->hs.cipher->algorithm_auth;

	if (alg_a & SSL_aECDSA) {
		i = SSL_PKEY_ECC;
	} else if (alg_a & SSL_aRSA) {
		i = SSL_PKEY_RSA;
	} else { /* if (alg_a & SSL_aNULL) */
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return (NULL);
	}

	return (c->pkeys + i);
}

EVP_PKEY *
ssl_get_sign_pkey(SSL *s, const SSL_CIPHER *cipher, const EVP_MD **pmd,
    const struct ssl_sigalg **sap)
{
	const struct ssl_sigalg *sigalg = NULL;
	EVP_PKEY *pkey = NULL;
	unsigned long alg_a;
	SSL_CERT *c;
	int idx = -1;

	alg_a = cipher->algorithm_auth;
	c = s->cert;

	if (alg_a & SSL_aRSA) {
		idx = SSL_PKEY_RSA;
	} else if ((alg_a & SSL_aECDSA) &&
	    (c->pkeys[SSL_PKEY_ECC].privatekey != NULL))
		idx = SSL_PKEY_ECC;
	if (idx == -1) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return (NULL);
	}

	pkey = c->pkeys[idx].privatekey;
	if ((sigalg = ssl_sigalg_select(s, pkey)) == NULL) {
		SSLerror(s, SSL_R_SIGNATURE_ALGORITHMS_ERROR);
		return (NULL);
	}
	*pmd = sigalg->md();
	*sap = sigalg;

	return (pkey);
}

size_t
ssl_dhe_params_auto_key_bits(SSL *s)
{
	SSL_CERT_PKEY *cpk;
	int key_bits;

	if (s->cert->dhe_params_auto == 2) {
		key_bits = 1024;
	} else if (s->s3->hs.cipher->algorithm_auth & SSL_aNULL) {
		key_bits = 1024;
		if (s->s3->hs.cipher->strength_bits == 256)
			key_bits = 3072;
	} else {
		if ((cpk = ssl_get_server_send_pkey(s)) == NULL)
			return 0;
		if (cpk->privatekey == NULL ||
		    EVP_PKEY_get0_RSA(cpk->privatekey) == NULL)
			return 0;
		if ((key_bits = EVP_PKEY_bits(cpk->privatekey)) <= 0)
			return 0;
	}

	return key_bits;
}

static int
ssl_should_update_external_cache(SSL *s, int mode)
{
	int cache_mode;

	cache_mode = s->session_ctx->session_cache_mode;

	/* Don't cache if mode says not to */
	if ((cache_mode & mode) == 0)
		return 0;

	/* if it is not already cached, cache it */
	if (!s->hit)
		return 1;

	/* If it's TLS 1.3, do it to match OpenSSL */
	if (s->s3->hs.negotiated_tls_version >= TLS1_3_VERSION)
		return 1;

	return 0;
}

static int
ssl_should_update_internal_cache(SSL *s, int mode)
{
	int cache_mode;

	cache_mode = s->session_ctx->session_cache_mode;

	/* Don't cache if mode says not to */
	if ((cache_mode & mode) == 0)
		return 0;

	/* If it is already cached, don't cache it again */
	if (s->hit)
		return 0;

	if ((cache_mode & SSL_SESS_CACHE_NO_INTERNAL_STORE) != 0)
		return 0;

	/* If we are lesser than TLS 1.3, Cache it. */
	if (s->s3->hs.negotiated_tls_version < TLS1_3_VERSION)
		return 1;

	/* Below this we consider TLS 1.3 or later */

	/* If it's not a server, add it? OpenSSL does this. */
	if (!s->server)
		return 1;

	/* XXX if we support early data / PSK need to add */

	/*
	 * If we have the remove session callback, we will want
	 * to know about this even if it's a stateless ticket
	 * from 1.3 so we can know when it is removed.
	 */
	if (s->session_ctx->remove_session_cb != NULL)
		return 1;

	/* If we have set OP_NO_TICKET, cache it. */
	if ((s->options & SSL_OP_NO_TICKET) != 0)
		return 1;

	/* Otherwise do not cache */
	return 0;
}

void
ssl_update_cache(SSL *s, int mode)
{
	int cache_mode, do_callback;

	if (s->session->session_id_length == 0)
		return;

	cache_mode = s->session_ctx->session_cache_mode;
	do_callback = ssl_should_update_external_cache(s, mode);

	if (ssl_should_update_internal_cache(s, mode)) {
		/*
		 * XXX should we fail if the add to the internal cache
		 * fails? OpenSSL doesn't care..
		 */
		(void) SSL_CTX_add_session(s->session_ctx, s->session);
	}

	/*
	 * Update the "external cache" by calling the new session
	 * callback if present, even with TLS 1.3 without early data
	 * "because some application just want to know about the
	 * creation of a session and aren't doing a full cache".
	 * Apparently, if they are doing a full cache, they'll have
	 * some fun, but we endeavour to give application writers the
	 * same glorious experience they expect from OpenSSL which
	 * does it this way.
	 */
	if (do_callback && s->session_ctx->new_session_cb != NULL) {
		    CRYPTO_add(&s->session->references, 1, CRYPTO_LOCK_SSL_SESSION);
		    if (!s->session_ctx->new_session_cb(s, s->session))
			    SSL_SESSION_free(s->session);
	}

	/* Auto flush every 255 connections. */
	if (!(cache_mode & SSL_SESS_CACHE_NO_AUTO_CLEAR) &&
	    (cache_mode & mode) != 0) {
		int connections;
		if (mode & SSL_SESS_CACHE_CLIENT)
			connections = s->session_ctx->stats.sess_connect_good;
		else
			connections = s->session_ctx->stats.sess_accept_good;
		if ((connections & 0xff) == 0xff)
			SSL_CTX_flush_sessions(s->session_ctx, time(NULL));
	}
}

const SSL_METHOD *
SSL_get_ssl_method(SSL *s)
{
	return (s->method);
}
LSSL_ALIAS(SSL_get_ssl_method);

int
SSL_set_ssl_method(SSL *s, const SSL_METHOD *method)
{
	int (*handshake_func)(SSL *) = NULL;
	int ret = 1;

	if (s->method == method)
		return (ret);

	if (s->handshake_func == s->method->ssl_connect)
		handshake_func = method->ssl_connect;
	else if (s->handshake_func == s->method->ssl_accept)
		handshake_func = method->ssl_accept;

	if (s->method->version == method->version) {
		s->method = method;
	} else {
		s->method->ssl_free(s);
		s->method = method;
		ret = s->method->ssl_new(s);
	}
	s->handshake_func = handshake_func;

	return (ret);
}
LSSL_ALIAS(SSL_set_ssl_method);

int
SSL_get_error(const SSL *s, int i)
{
	unsigned long l;
	int reason;
	BIO *bio;

	if (i > 0)
		return (SSL_ERROR_NONE);

	/*
	 * Make things return SSL_ERROR_SYSCALL when doing SSL_do_handshake
	 * etc, where we do encode the error.
	 */
	if ((l = ERR_peek_error()) != 0) {
		if (ERR_GET_LIB(l) == ERR_LIB_SYS)
			return (SSL_ERROR_SYSCALL);
		else
			return (SSL_ERROR_SSL);
	}

	if (SSL_want_read(s)) {
		bio = SSL_get_rbio(s);
		if (BIO_should_read(bio)) {
			return (SSL_ERROR_WANT_READ);
		} else if (BIO_should_write(bio)) {
			/*
			 * This one doesn't make too much sense...  We never
			 * try to write to the rbio, and an application
			 * program where rbio and wbio are separate couldn't
			 * even know what it should wait for.  However if we
			 * ever set s->rwstate incorrectly (so that we have
			 * SSL_want_read(s) instead of SSL_want_write(s))
			 * and rbio and wbio *are* the same, this test works
			 * around that bug; so it might be safer to keep it.
			 */
			return (SSL_ERROR_WANT_WRITE);
		} else if (BIO_should_io_special(bio)) {
			reason = BIO_get_retry_reason(bio);
			if (reason == BIO_RR_CONNECT)
				return (SSL_ERROR_WANT_CONNECT);
			else if (reason == BIO_RR_ACCEPT)
				return (SSL_ERROR_WANT_ACCEPT);
			else
				return (SSL_ERROR_SYSCALL); /* unknown */
		}
	}

	if (SSL_want_write(s)) {
		bio = SSL_get_wbio(s);
		if (BIO_should_write(bio)) {
			return (SSL_ERROR_WANT_WRITE);
		} else if (BIO_should_read(bio)) {
			/*
			 * See above (SSL_want_read(s) with
			 * BIO_should_write(bio))
			 */
			return (SSL_ERROR_WANT_READ);
		} else if (BIO_should_io_special(bio)) {
			reason = BIO_get_retry_reason(bio);
			if (reason == BIO_RR_CONNECT)
				return (SSL_ERROR_WANT_CONNECT);
			else if (reason == BIO_RR_ACCEPT)
				return (SSL_ERROR_WANT_ACCEPT);
			else
				return (SSL_ERROR_SYSCALL);
		}
	}

	if (SSL_want_x509_lookup(s))
		return (SSL_ERROR_WANT_X509_LOOKUP);

	if ((s->shutdown & SSL_RECEIVED_SHUTDOWN) &&
	    (s->s3->warn_alert == SSL_AD_CLOSE_NOTIFY))
		return (SSL_ERROR_ZERO_RETURN);

	return (SSL_ERROR_SYSCALL);
}
LSSL_ALIAS(SSL_get_error);

int
SSL_CTX_set_quic_method(SSL_CTX *ctx, const SSL_QUIC_METHOD *quic_method)
{
	if (ctx->method->dtls)
		return 0;

	ctx->quic_method = quic_method;

	return 1;
}
LSSL_ALIAS(SSL_CTX_set_quic_method);

int
SSL_set_quic_method(SSL *ssl, const SSL_QUIC_METHOD *quic_method)
{
	if (ssl->method->dtls)
		return 0;

	ssl->quic_method = quic_method;

	return 1;
}
LSSL_ALIAS(SSL_set_quic_method);

size_t
SSL_quic_max_handshake_flight_len(const SSL *ssl,
    enum ssl_encryption_level_t level)
{
	size_t flight_len;

	/* Limit flights to 16K when there are no large certificate messages. */
	flight_len = 16384;

	switch (level) {
	case ssl_encryption_initial:
		return flight_len;

	case ssl_encryption_early_data:
		/* QUIC does not send EndOfEarlyData. */
		return 0;

	case ssl_encryption_handshake:
		if (ssl->server) {
			/*
			 * Servers may receive Certificate message if configured
			 * to request client certificates.
			 */
			if ((SSL_get_verify_mode(ssl) & SSL_VERIFY_PEER) != 0 &&
			    ssl->max_cert_list > flight_len)
				flight_len = ssl->max_cert_list;
		} else {
			/*
			 * Clients may receive both Certificate message and a
			 * CertificateRequest message.
			 */
			if (ssl->max_cert_list * 2 > flight_len)
				flight_len = ssl->max_cert_list * 2;
		}
		return flight_len;
	case ssl_encryption_application:
		/*
		 * Note there is not actually a bound on the number of
		 * NewSessionTickets one may send in a row. This level may need
		 * more involved flow control.
		 */
		return flight_len;
	}

	return 0;
}
LSSL_ALIAS(SSL_quic_max_handshake_flight_len);

enum ssl_encryption_level_t
SSL_quic_read_level(const SSL *ssl)
{
	return ssl->s3->hs.tls13.quic_read_level;
}
LSSL_ALIAS(SSL_quic_read_level);

enum ssl_encryption_level_t
SSL_quic_write_level(const SSL *ssl)
{
	return ssl->s3->hs.tls13.quic_write_level;
}
LSSL_ALIAS(SSL_quic_write_level);

int
SSL_provide_quic_data(SSL *ssl, enum ssl_encryption_level_t level,
    const uint8_t *data, size_t len)
{
	if (!SSL_is_quic(ssl)) {
		SSLerror(ssl, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return 0;
	}

	if (level != SSL_quic_read_level(ssl)) {
		SSLerror(ssl, SSL_R_WRONG_ENCRYPTION_LEVEL_RECEIVED);
		return 0;
	}

	if (ssl->s3->hs.tls13.quic_read_buffer == NULL) {
		ssl->s3->hs.tls13.quic_read_buffer = tls_buffer_new(0);
		if (ssl->s3->hs.tls13.quic_read_buffer == NULL) {
			SSLerror(ssl, ERR_R_MALLOC_FAILURE);
			return 0;
		}
	}

	/* XXX - note that this does not currently downsize. */
	tls_buffer_set_capacity_limit(ssl->s3->hs.tls13.quic_read_buffer,
	    SSL_quic_max_handshake_flight_len(ssl, level));

	/*
	 * XXX - an append that fails due to exceeding capacity should set
	 * SSL_R_EXCESSIVE_MESSAGE_SIZE.
	 */
	return tls_buffer_append(ssl->s3->hs.tls13.quic_read_buffer, data, len);
}
LSSL_ALIAS(SSL_provide_quic_data);

int
SSL_process_quic_post_handshake(SSL *ssl)
{
	/* XXX - this needs to run PHH received. */
	return 1;
}
LSSL_ALIAS(SSL_process_quic_post_handshake);

int
SSL_do_handshake(SSL *s)
{
	if (s->handshake_func == NULL) {
		SSLerror(s, SSL_R_CONNECTION_TYPE_NOT_SET);
		return (-1);
	}

	s->method->ssl_renegotiate_check(s);

	if (!SSL_in_init(s) && !SSL_in_before(s))
		return 1;

	return s->handshake_func(s);
}
LSSL_ALIAS(SSL_do_handshake);

/*
 * For the next 2 functions, SSL_clear() sets shutdown and so
 * one of these calls will reset it
 */
void
SSL_set_accept_state(SSL *s)
{
	s->server = 1;
	s->shutdown = 0;
	s->s3->hs.state = SSL_ST_ACCEPT|SSL_ST_BEFORE;
	s->handshake_func = s->method->ssl_accept;
	ssl_clear_cipher_state(s);
}
LSSL_ALIAS(SSL_set_accept_state);

void
SSL_set_connect_state(SSL *s)
{
	s->server = 0;
	s->shutdown = 0;
	s->s3->hs.state = SSL_ST_CONNECT|SSL_ST_BEFORE;
	s->handshake_func = s->method->ssl_connect;
	ssl_clear_cipher_state(s);
}
LSSL_ALIAS(SSL_set_connect_state);

int
ssl_undefined_function(SSL *s)
{
	SSLerror(s, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
	return (0);
}

int
ssl_undefined_void_function(void)
{
	SSLerrorx(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
	return (0);
}

int
ssl_undefined_const_function(const SSL *s)
{
	SSLerror(s, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
	return (0);
}

const char *
ssl_version_string(int ver)
{
	switch (ver) {
	case TLS1_VERSION:
		return (SSL_TXT_TLSV1);
	case TLS1_1_VERSION:
		return (SSL_TXT_TLSV1_1);
	case TLS1_2_VERSION:
		return (SSL_TXT_TLSV1_2);
	case TLS1_3_VERSION:
		return (SSL_TXT_TLSV1_3);
	case DTLS1_VERSION:
		return (SSL_TXT_DTLS1);
	case DTLS1_2_VERSION:
		return (SSL_TXT_DTLS1_2);
	default:
		return ("unknown");
	}
}

const char *
SSL_get_version(const SSL *s)
{
	return ssl_version_string(s->version);
}
LSSL_ALIAS(SSL_get_version);

SSL *
SSL_dup(SSL *s)
{
	STACK_OF(X509_NAME) *sk;
	X509_NAME *xn;
	SSL *ret;
	int i;

	if ((ret = SSL_new(SSL_get_SSL_CTX(s))) == NULL)
		goto err;

	ret->version = s->version;
	ret->method = s->method;

	if (s->session != NULL) {
		if (!SSL_copy_session_id(ret, s))
			goto err;
	} else {
		/*
		 * No session has been established yet, so we have to expect
		 * that s->cert or ret->cert will be changed later --
		 * they should not both point to the same object,
		 * and thus we can't use SSL_copy_session_id.
		 */

		ret->method->ssl_free(ret);
		ret->method = s->method;
		ret->method->ssl_new(ret);

		ssl_cert_free(ret->cert);
		if ((ret->cert = ssl_cert_dup(s->cert)) == NULL)
			goto err;

		if (!SSL_set_session_id_context(ret, s->sid_ctx,
		    s->sid_ctx_length))
			goto err;
	}

	ret->options = s->options;
	ret->mode = s->mode;
	SSL_set_max_cert_list(ret, SSL_get_max_cert_list(s));
	SSL_set_read_ahead(ret, SSL_get_read_ahead(s));
	ret->msg_callback = s->msg_callback;
	ret->msg_callback_arg = s->msg_callback_arg;
	SSL_set_verify(ret, SSL_get_verify_mode(s),
	SSL_get_verify_callback(s));
	SSL_set_verify_depth(ret, SSL_get_verify_depth(s));
	ret->generate_session_id = s->generate_session_id;

	SSL_set_info_callback(ret, SSL_get_info_callback(s));

	/* copy app data, a little dangerous perhaps */
	if (!CRYPTO_dup_ex_data(CRYPTO_EX_INDEX_SSL,
	    &ret->ex_data, &s->ex_data))
		goto err;

	/* setup rbio, and wbio */
	if (s->rbio != NULL) {
		if (!BIO_dup_state(s->rbio,(char *)&ret->rbio))
			goto err;
	}
	if (s->wbio != NULL) {
		if (s->wbio != s->rbio) {
			if (!BIO_dup_state(s->wbio,(char *)&ret->wbio))
				goto err;
		} else
			ret->wbio = ret->rbio;
	}
	ret->rwstate = s->rwstate;
	ret->in_handshake = s->in_handshake;
	ret->handshake_func = s->handshake_func;
	ret->server = s->server;
	ret->renegotiate = s->renegotiate;
	ret->new_session = s->new_session;
	ret->quiet_shutdown = s->quiet_shutdown;
	ret->shutdown = s->shutdown;
	/* SSL_dup does not really work at any state, though */
	ret->s3->hs.state = s->s3->hs.state;
	ret->rstate = s->rstate;

	/*
	 * Would have to copy ret->init_buf, ret->init_msg, ret->init_num,
	 * ret->init_off
	 */
	ret->init_num = 0;

	ret->hit = s->hit;

	X509_VERIFY_PARAM_inherit(ret->param, s->param);

	if (s->cipher_list != NULL) {
		if ((ret->cipher_list =
		    sk_SSL_CIPHER_dup(s->cipher_list)) == NULL)
			goto err;
	}
	if (s->cipher_list_tls13 != NULL) {
		if ((ret->cipher_list_tls13 =
		    sk_SSL_CIPHER_dup(s->cipher_list_tls13)) == NULL)
			goto err;
	}

	/* Dup the client_CA list */
	if (s->client_CA != NULL) {
		if ((sk = sk_X509_NAME_dup(s->client_CA)) == NULL)
			goto err;
		ret->client_CA = sk;
		for (i = 0; i < sk_X509_NAME_num(sk); i++) {
			xn = sk_X509_NAME_value(sk, i);
			if (sk_X509_NAME_set(sk, i,
			    X509_NAME_dup(xn)) == NULL) {
				X509_NAME_free(xn);
				goto err;
			}
		}
	}

	return ret;
 err:
	SSL_free(ret);
	return NULL;
}
LSSL_ALIAS(SSL_dup);

void
ssl_clear_cipher_state(SSL *s)
{
	tls12_record_layer_clear_read_state(s->rl);
	tls12_record_layer_clear_write_state(s->rl);
}

void
ssl_info_callback(const SSL *s, int type, int value)
{
	ssl_info_callback_fn *cb;

	if ((cb = s->info_callback) == NULL)
		cb = s->ctx->info_callback;
	if (cb != NULL)
		cb(s, type, value);
}

void
ssl_msg_callback(SSL *s, int is_write, int content_type,
    const void *msg_buf, size_t msg_len)
{
	if (s->msg_callback == NULL)
		return;

	s->msg_callback(is_write, s->version, content_type,
	    msg_buf, msg_len, s, s->msg_callback_arg);
}

void
ssl_msg_callback_cbs(SSL *s, int is_write, int content_type, CBS *cbs)
{
	ssl_msg_callback(s, is_write, content_type, CBS_data(cbs), CBS_len(cbs));
}

/* Fix this function so that it takes an optional type parameter */
X509 *
SSL_get_certificate(const SSL *s)
{
	return (s->cert->key->x509);
}
LSSL_ALIAS(SSL_get_certificate);

/* Fix this function so that it takes an optional type parameter */
EVP_PKEY *
SSL_get_privatekey(const SSL *s)
{
	return (s->cert->key->privatekey);
}
LSSL_ALIAS(SSL_get_privatekey);

const SSL_CIPHER *
SSL_get_current_cipher(const SSL *s)
{
	return s->s3->hs.cipher;
}
LSSL_ALIAS(SSL_get_current_cipher);

const void *
SSL_get_current_compression(SSL *s)
{
	return (NULL);
}
LSSL_ALIAS(SSL_get_current_compression);

const void *
SSL_get_current_expansion(SSL *s)
{
	return (NULL);
}
LSSL_ALIAS(SSL_get_current_expansion);

size_t
SSL_get_client_random(const SSL *s, unsigned char *out, size_t max_out)
{
	size_t len = sizeof(s->s3->client_random);

	if (out == NULL)
		return len;

	if (len > max_out)
		len = max_out;

	memcpy(out, s->s3->client_random, len);

	return len;
}
LSSL_ALIAS(SSL_get_client_random);

size_t
SSL_get_server_random(const SSL *s, unsigned char *out, size_t max_out)
{
	size_t len = sizeof(s->s3->server_random);

	if (out == NULL)
		return len;

	if (len > max_out)
		len = max_out;

	memcpy(out, s->s3->server_random, len);

	return len;
}
LSSL_ALIAS(SSL_get_server_random);

int
ssl_init_wbio_buffer(SSL *s, int push)
{
	BIO	*bbio;

	if (s->bbio == NULL) {
		bbio = BIO_new(BIO_f_buffer());
		if (bbio == NULL)
			return (0);
		s->bbio = bbio;
	} else {
		bbio = s->bbio;
		if (s->bbio == s->wbio)
			s->wbio = BIO_pop(s->wbio);
	}
	(void)BIO_reset(bbio);
/*	if (!BIO_set_write_buffer_size(bbio,16*1024)) */
	if (!BIO_set_read_buffer_size(bbio, 1)) {
		SSLerror(s, ERR_R_BUF_LIB);
		return (0);
	}
	if (push) {
		if (s->wbio != bbio)
			s->wbio = BIO_push(bbio, s->wbio);
	} else {
		if (s->wbio == bbio)
			s->wbio = BIO_pop(bbio);
	}
	return (1);
}

void
ssl_free_wbio_buffer(SSL *s)
{
	if (s == NULL)
		return;

	if (s->bbio == NULL)
		return;

	if (s->bbio == s->wbio) {
		/* remove buffering */
		s->wbio = BIO_pop(s->wbio);
	}
	BIO_free(s->bbio);
	s->bbio = NULL;
}

void
SSL_CTX_set_quiet_shutdown(SSL_CTX *ctx, int mode)
{
	ctx->quiet_shutdown = mode;
}
LSSL_ALIAS(SSL_CTX_set_quiet_shutdown);

int
SSL_CTX_get_quiet_shutdown(const SSL_CTX *ctx)
{
	return (ctx->quiet_shutdown);
}
LSSL_ALIAS(SSL_CTX_get_quiet_shutdown);

void
SSL_set_quiet_shutdown(SSL *s, int mode)
{
	s->quiet_shutdown = mode;
}
LSSL_ALIAS(SSL_set_quiet_shutdown);

int
SSL_get_quiet_shutdown(const SSL *s)
{
	return (s->quiet_shutdown);
}
LSSL_ALIAS(SSL_get_quiet_shutdown);

void
SSL_set_shutdown(SSL *s, int mode)
{
	s->shutdown = mode;
}
LSSL_ALIAS(SSL_set_shutdown);

int
SSL_get_shutdown(const SSL *s)
{
	return (s->shutdown);
}
LSSL_ALIAS(SSL_get_shutdown);

int
SSL_version(const SSL *s)
{
	return (s->version);
}
LSSL_ALIAS(SSL_version);

SSL_CTX *
SSL_get_SSL_CTX(const SSL *ssl)
{
	return (ssl->ctx);
}
LSSL_ALIAS(SSL_get_SSL_CTX);

SSL_CTX *
SSL_set_SSL_CTX(SSL *ssl, SSL_CTX* ctx)
{
	SSL_CERT *new_cert;

	if (ctx == NULL)
		ctx = ssl->initial_ctx;
	if (ssl->ctx == ctx)
		return (ssl->ctx);

	if ((new_cert = ssl_cert_dup(ctx->cert)) == NULL)
		return NULL;
	ssl_cert_free(ssl->cert);
	ssl->cert = new_cert;

	SSL_CTX_up_ref(ctx);
	SSL_CTX_free(ssl->ctx); /* decrement reference count */
	ssl->ctx = ctx;

	return (ssl->ctx);
}
LSSL_ALIAS(SSL_set_SSL_CTX);

int
SSL_CTX_set_default_verify_paths(SSL_CTX *ctx)
{
	return (X509_STORE_set_default_paths(ctx->cert_store));
}
LSSL_ALIAS(SSL_CTX_set_default_verify_paths);

int
SSL_CTX_load_verify_locations(SSL_CTX *ctx, const char *CAfile,
    const char *CApath)
{
	return (X509_STORE_load_locations(ctx->cert_store, CAfile, CApath));
}
LSSL_ALIAS(SSL_CTX_load_verify_locations);

int
SSL_CTX_load_verify_mem(SSL_CTX *ctx, void *buf, int len)
{
	return (X509_STORE_load_mem(ctx->cert_store, buf, len));
}
LSSL_ALIAS(SSL_CTX_load_verify_mem);

void
SSL_set_info_callback(SSL *ssl, void (*cb)(const SSL *ssl, int type, int val))
{
	ssl->info_callback = cb;
}
LSSL_ALIAS(SSL_set_info_callback);

void (*SSL_get_info_callback(const SSL *ssl))(const SSL *ssl, int type, int val)
{
	return (ssl->info_callback);
}
LSSL_ALIAS(SSL_get_info_callback);

int
SSL_state(const SSL *ssl)
{
	return (ssl->s3->hs.state);
}
LSSL_ALIAS(SSL_state);

void
SSL_set_state(SSL *ssl, int state)
{
	ssl->s3->hs.state = state;
}
LSSL_ALIAS(SSL_set_state);

void
SSL_set_verify_result(SSL *ssl, long arg)
{
	ssl->verify_result = arg;
}
LSSL_ALIAS(SSL_set_verify_result);

long
SSL_get_verify_result(const SSL *ssl)
{
	return (ssl->verify_result);
}
LSSL_ALIAS(SSL_get_verify_result);

int
SSL_verify_client_post_handshake(SSL *ssl)
{
	return 0;
}
LSSL_ALIAS(SSL_verify_client_post_handshake);

void
SSL_CTX_set_post_handshake_auth(SSL_CTX *ctx, int val)
{
	return;
}
LSSL_ALIAS(SSL_CTX_set_post_handshake_auth);

void
SSL_set_post_handshake_auth(SSL *ssl, int val)
{
	return;
}
LSSL_ALIAS(SSL_set_post_handshake_auth);

int
SSL_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
    CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	return (CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_SSL, argl, argp,
	    new_func, dup_func, free_func));
}
LSSL_ALIAS(SSL_get_ex_new_index);

int
SSL_set_ex_data(SSL *s, int idx, void *arg)
{
	return (CRYPTO_set_ex_data(&s->ex_data, idx, arg));
}
LSSL_ALIAS(SSL_set_ex_data);

void *
SSL_get_ex_data(const SSL *s, int idx)
{
	return (CRYPTO_get_ex_data(&s->ex_data, idx));
}
LSSL_ALIAS(SSL_get_ex_data);

int
SSL_CTX_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
    CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	return (CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_SSL_CTX, argl, argp,
	    new_func, dup_func, free_func));
}
LSSL_ALIAS(SSL_CTX_get_ex_new_index);

int
SSL_CTX_set_ex_data(SSL_CTX *s, int idx, void *arg)
{
	return (CRYPTO_set_ex_data(&s->ex_data, idx, arg));
}
LSSL_ALIAS(SSL_CTX_set_ex_data);

void *
SSL_CTX_get_ex_data(const SSL_CTX *s, int idx)
{
	return (CRYPTO_get_ex_data(&s->ex_data, idx));
}
LSSL_ALIAS(SSL_CTX_get_ex_data);

int
ssl_ok(SSL *s)
{
	return (1);
}

X509_STORE *
SSL_CTX_get_cert_store(const SSL_CTX *ctx)
{
	return (ctx->cert_store);
}
LSSL_ALIAS(SSL_CTX_get_cert_store);

void
SSL_CTX_set_cert_store(SSL_CTX *ctx, X509_STORE *store)
{
	X509_STORE_free(ctx->cert_store);
	ctx->cert_store = store;
}
LSSL_ALIAS(SSL_CTX_set_cert_store);

void
SSL_CTX_set1_cert_store(SSL_CTX *ctx, X509_STORE *store)
{
	if (store != NULL)
		X509_STORE_up_ref(store);

	SSL_CTX_set_cert_store(ctx, store);
}
LSSL_ALIAS(SSL_CTX_set1_cert_store);

X509 *
SSL_CTX_get0_certificate(const SSL_CTX *ctx)
{
	if (ctx->cert == NULL)
		return NULL;

	return ctx->cert->key->x509;
}
LSSL_ALIAS(SSL_CTX_get0_certificate);

EVP_PKEY *
SSL_CTX_get0_privatekey(const SSL_CTX *ctx)
{
	if (ctx->cert == NULL)
		return NULL;

	return ctx->cert->key->privatekey;
}
LSSL_ALIAS(SSL_CTX_get0_privatekey);

int
SSL_want(const SSL *s)
{
	return (s->rwstate);
}
LSSL_ALIAS(SSL_want);

void
SSL_CTX_set_tmp_rsa_callback(SSL_CTX *ctx, RSA *(*cb)(SSL *ssl, int is_export,
    int keylength))
{
	SSL_CTX_callback_ctrl(ctx, SSL_CTRL_SET_TMP_RSA_CB,(void (*)(void))cb);
}
LSSL_ALIAS(SSL_CTX_set_tmp_rsa_callback);

void
SSL_set_tmp_rsa_callback(SSL *ssl, RSA *(*cb)(SSL *ssl, int is_export,
    int keylength))
{
	SSL_callback_ctrl(ssl, SSL_CTRL_SET_TMP_RSA_CB,(void (*)(void))cb);
}
LSSL_ALIAS(SSL_set_tmp_rsa_callback);

void
SSL_CTX_set_tmp_dh_callback(SSL_CTX *ctx, DH *(*dh)(SSL *ssl, int is_export,
    int keylength))
{
	SSL_CTX_callback_ctrl(ctx, SSL_CTRL_SET_TMP_DH_CB,(void (*)(void))dh);
}
LSSL_ALIAS(SSL_CTX_set_tmp_dh_callback);

void
SSL_set_tmp_dh_callback(SSL *ssl, DH *(*dh)(SSL *ssl, int is_export,
    int keylength))
{
	SSL_callback_ctrl(ssl, SSL_CTRL_SET_TMP_DH_CB,(void (*)(void))dh);
}
LSSL_ALIAS(SSL_set_tmp_dh_callback);

void
SSL_CTX_set_tmp_ecdh_callback(SSL_CTX *ctx, EC_KEY *(*ecdh)(SSL *ssl,
    int is_export, int keylength))
{
	SSL_CTX_callback_ctrl(ctx, SSL_CTRL_SET_TMP_ECDH_CB,
	    (void (*)(void))ecdh);
}
LSSL_ALIAS(SSL_CTX_set_tmp_ecdh_callback);

void
SSL_set_tmp_ecdh_callback(SSL *ssl, EC_KEY *(*ecdh)(SSL *ssl, int is_export,
    int keylength))
{
	SSL_callback_ctrl(ssl, SSL_CTRL_SET_TMP_ECDH_CB,(void (*)(void))ecdh);
}
LSSL_ALIAS(SSL_set_tmp_ecdh_callback);


void
SSL_CTX_set_msg_callback(SSL_CTX *ctx, void (*cb)(int write_p, int version,
    int content_type, const void *buf, size_t len, SSL *ssl, void *arg))
{
	SSL_CTX_callback_ctrl(ctx, SSL_CTRL_SET_MSG_CALLBACK,
	    (void (*)(void))cb);
}
LSSL_ALIAS(SSL_CTX_set_msg_callback);

void
SSL_set_msg_callback(SSL *ssl, void (*cb)(int write_p, int version,
    int content_type, const void *buf, size_t len, SSL *ssl, void *arg))
{
	SSL_callback_ctrl(ssl, SSL_CTRL_SET_MSG_CALLBACK, (void (*)(void))cb);
}
LSSL_ALIAS(SSL_set_msg_callback);

int
SSL_cache_hit(SSL *s)
{
	return (s->hit);
}
LSSL_ALIAS(SSL_cache_hit);

int
SSL_CTX_get_min_proto_version(SSL_CTX *ctx)
{
	return ctx->min_proto_version;
}
LSSL_ALIAS(SSL_CTX_get_min_proto_version);

int
SSL_CTX_set_min_proto_version(SSL_CTX *ctx, uint16_t version)
{
	return ssl_version_set_min(ctx->method, version,
	    ctx->max_tls_version, &ctx->min_tls_version,
	    &ctx->min_proto_version);
}
LSSL_ALIAS(SSL_CTX_set_min_proto_version);

int
SSL_CTX_get_max_proto_version(SSL_CTX *ctx)
{
	return ctx->max_proto_version;
}
LSSL_ALIAS(SSL_CTX_get_max_proto_version);

int
SSL_CTX_set_max_proto_version(SSL_CTX *ctx, uint16_t version)
{
	return ssl_version_set_max(ctx->method, version,
	    ctx->min_tls_version, &ctx->max_tls_version,
	    &ctx->max_proto_version);
}
LSSL_ALIAS(SSL_CTX_set_max_proto_version);

int
SSL_get_min_proto_version(SSL *ssl)
{
	return ssl->min_proto_version;
}
LSSL_ALIAS(SSL_get_min_proto_version);

int
SSL_set_min_proto_version(SSL *ssl, uint16_t version)
{
	return ssl_version_set_min(ssl->method, version,
	    ssl->max_tls_version, &ssl->min_tls_version,
	    &ssl->min_proto_version);
}
LSSL_ALIAS(SSL_set_min_proto_version);
int
SSL_get_max_proto_version(SSL *ssl)
{
	return ssl->max_proto_version;
}
LSSL_ALIAS(SSL_get_max_proto_version);

int
SSL_set_max_proto_version(SSL *ssl, uint16_t version)
{
	return ssl_version_set_max(ssl->method, version,
	    ssl->min_tls_version, &ssl->max_tls_version,
	    &ssl->max_proto_version);
}
LSSL_ALIAS(SSL_set_max_proto_version);

const SSL_METHOD *
SSL_CTX_get_ssl_method(const SSL_CTX *ctx)
{
	return ctx->method;
}
LSSL_ALIAS(SSL_CTX_get_ssl_method);

int
SSL_CTX_get_security_level(const SSL_CTX *ctx)
{
	return ctx->cert->security_level;
}
LSSL_ALIAS(SSL_CTX_get_security_level);

void
SSL_CTX_set_security_level(SSL_CTX *ctx, int level)
{
	ctx->cert->security_level = level;
}
LSSL_ALIAS(SSL_CTX_set_security_level);

int
SSL_get_security_level(const SSL *ssl)
{
	return ssl->cert->security_level;
}
LSSL_ALIAS(SSL_get_security_level);

void
SSL_set_security_level(SSL *ssl, int level)
{
	ssl->cert->security_level = level;
}
LSSL_ALIAS(SSL_set_security_level);

int
SSL_is_quic(const SSL *ssl)
{
	return ssl->quic_method != NULL;
}
LSSL_ALIAS(SSL_is_quic);

int
SSL_set_quic_transport_params(SSL *ssl, const uint8_t *params,
    size_t params_len)
{
	freezero(ssl->quic_transport_params,
	    ssl->quic_transport_params_len);
	ssl->quic_transport_params = NULL;
	ssl->quic_transport_params_len = 0;

	if ((ssl->quic_transport_params = malloc(params_len)) == NULL)
		return 0;

	memcpy(ssl->quic_transport_params, params, params_len);
	ssl->quic_transport_params_len = params_len;

	return 1;
}
LSSL_ALIAS(SSL_set_quic_transport_params);

void
SSL_get_peer_quic_transport_params(const SSL *ssl, const uint8_t **out_params,
    size_t *out_params_len)
{
	*out_params = ssl->s3->peer_quic_transport_params;
	*out_params_len = ssl->s3->peer_quic_transport_params_len;
}
LSSL_ALIAS(SSL_get_peer_quic_transport_params);

void
SSL_set_quic_use_legacy_codepoint(SSL *ssl, int use_legacy)
{
	/* Not supported. */
}
LSSL_ALIAS(SSL_set_quic_use_legacy_codepoint);
