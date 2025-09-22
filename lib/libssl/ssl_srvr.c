/* $OpenBSD: ssl_srvr.c,v 1.166 2025/03/09 15:53:36 tb Exp $ */
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
 *
 * Portions of the attached software ("Contribution") are developed by
 * SUN MICROSYSTEMS, INC., and are contributed to the OpenSSL project.
 *
 * The Contribution is licensed pursuant to the OpenSSL open source
 * license provided above.
 *
 * ECC cipher suite support in OpenSSL originally written by
 * Vipul Gupta and Sumit Gupta of Sun Microsystems Laboratories.
 *
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

#include <limits.h>
#include <stdio.h>

#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/curve25519.h>
#include <openssl/evp.h>
#include <openssl/dh.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/objects.h>
#include <openssl/opensslconf.h>
#include <openssl/x509.h>

#include "bytestring.h"
#include "crypto_internal.h"
#include "dtls_local.h"
#include "ssl_local.h"
#include "ssl_sigalgs.h"
#include "ssl_tlsext.h"

static int ssl3_get_client_hello(SSL *s);
static int ssl3_send_dtls_hello_verify_request(SSL *s);
static int ssl3_send_server_hello(SSL *s);
static int ssl3_send_hello_request(SSL *s);
static int ssl3_send_server_certificate(SSL *s);
static int ssl3_send_server_key_exchange(SSL *s);
static int ssl3_send_certificate_request(SSL *s);
static int ssl3_send_server_done(SSL *s);
static int ssl3_get_client_certificate(SSL *s);
static int ssl3_get_client_key_exchange(SSL *s);
static int ssl3_get_cert_verify(SSL *s);
static int ssl3_send_newsession_ticket(SSL *s);
static int ssl3_send_cert_status(SSL *s);
static int ssl3_send_server_change_cipher_spec(SSL *s);
static int ssl3_send_server_finished(SSL *s);
static int ssl3_get_client_finished(SSL *s);

int
ssl3_accept(SSL *s)
{
	unsigned long alg_k;
	int new_state, state, skip = 0;
	int listen = 0;
	int ret = -1;

	ERR_clear_error();
	errno = 0;

	if (SSL_is_dtls(s))
		listen = s->d1->listen;

	/* init things to blank */
	s->in_handshake++;
	if (!SSL_in_init(s) || SSL_in_before(s))
		SSL_clear(s);

	if (SSL_is_dtls(s))
		s->d1->listen = listen;

	for (;;) {
		state = s->s3->hs.state;

		switch (s->s3->hs.state) {
		case SSL_ST_RENEGOTIATE:
			s->renegotiate = 1;
			/* s->s3->hs.state=SSL_ST_ACCEPT; */

		case SSL_ST_BEFORE:
		case SSL_ST_ACCEPT:
		case SSL_ST_BEFORE|SSL_ST_ACCEPT:
		case SSL_ST_OK|SSL_ST_ACCEPT:
			s->server = 1;

			ssl_info_callback(s, SSL_CB_HANDSHAKE_START, 1);

			if (!ssl_legacy_stack_version(s, s->version)) {
				SSLerror(s, ERR_R_INTERNAL_ERROR);
				ret = -1;
				goto end;
			}

			if (!ssl_supported_tls_version_range(s,
			    &s->s3->hs.our_min_tls_version,
			    &s->s3->hs.our_max_tls_version)) {
				SSLerror(s, SSL_R_NO_PROTOCOLS_AVAILABLE);
				ret = -1;
				goto end;
			}

			if (!ssl_security_version(s,
			    s->s3->hs.our_min_tls_version)) {
				SSLerror(s, SSL_R_VERSION_TOO_LOW);
				ret = -1;
				goto end;
			}

			if (!ssl3_setup_init_buffer(s)) {
				ret = -1;
				goto end;
			}
			if (!ssl3_setup_buffers(s)) {
				ret = -1;
				goto end;
			}

			s->init_num = 0;

			if (s->s3->hs.state != SSL_ST_RENEGOTIATE) {
				/*
				 * Ok, we now need to push on a buffering BIO
				 * so that the output is sent in a way that
				 * TCP likes :-)
				 */
				if (!ssl_init_wbio_buffer(s, 1)) {
					ret = -1;
					goto end;
				}

				if (!tls1_transcript_init(s)) {
					ret = -1;
					goto end;
				}

				s->s3->hs.state = SSL3_ST_SR_CLNT_HELLO_A;
				s->ctx->stats.sess_accept++;
			} else if (!SSL_is_dtls(s) && !s->s3->send_connection_binding) {
				/*
				 * Server attempting to renegotiate with
				 * client that doesn't support secure
				 * renegotiation.
				 */
				SSLerror(s, SSL_R_UNSAFE_LEGACY_RENEGOTIATION_DISABLED);
				ssl3_send_alert(s, SSL3_AL_FATAL,
				    SSL_AD_HANDSHAKE_FAILURE);
				ret = -1;
				goto end;
			} else {
				/*
				 * s->s3->hs.state == SSL_ST_RENEGOTIATE,
				 * we will just send a HelloRequest.
				 */
				s->ctx->stats.sess_accept_renegotiate++;
				s->s3->hs.state = SSL3_ST_SW_HELLO_REQ_A;
			}
			break;

		case SSL3_ST_SW_HELLO_REQ_A:
		case SSL3_ST_SW_HELLO_REQ_B:
			s->shutdown = 0;
			if (SSL_is_dtls(s)) {
				dtls1_clear_record_buffer(s);
				dtls1_start_timer(s);
			}
			ret = ssl3_send_hello_request(s);
			if (ret <= 0)
				goto end;
			if (SSL_is_dtls(s))
				s->s3->hs.tls12.next_state = SSL3_ST_SR_CLNT_HELLO_A;
			else
				s->s3->hs.tls12.next_state = SSL3_ST_SW_HELLO_REQ_C;
			s->s3->hs.state = SSL3_ST_SW_FLUSH;
			s->init_num = 0;

			if (SSL_is_dtls(s)) {
				if (!tls1_transcript_init(s)) {
					ret = -1;
					goto end;
				}
			}
			break;

		case SSL3_ST_SW_HELLO_REQ_C:
			s->s3->hs.state = SSL_ST_OK;
			break;

		case SSL3_ST_SR_CLNT_HELLO_A:
		case SSL3_ST_SR_CLNT_HELLO_B:
		case SSL3_ST_SR_CLNT_HELLO_C:
			s->shutdown = 0;
			if (SSL_is_dtls(s)) {
				ret = ssl3_get_client_hello(s);
				if (ret <= 0)
					goto end;
				dtls1_stop_timer(s);

				if (ret == 1 &&
				    (SSL_get_options(s) & SSL_OP_COOKIE_EXCHANGE))
					s->s3->hs.state = DTLS1_ST_SW_HELLO_VERIFY_REQUEST_A;
				else
					s->s3->hs.state = SSL3_ST_SW_SRVR_HELLO_A;

				s->init_num = 0;

				/*
				 * Reflect ClientHello sequence to remain
				 * stateless while listening.
				 */
				if (listen) {
					tls12_record_layer_reflect_seq_num(
					    s->rl);
				}

				/* If we're just listening, stop here */
				if (listen && s->s3->hs.state == SSL3_ST_SW_SRVR_HELLO_A) {
					ret = 2;
					s->d1->listen = 0;
					/*
					 * Set expected sequence numbers to
					 * continue the handshake.
					 */
					s->d1->handshake_read_seq = 2;
					s->d1->handshake_write_seq = 1;
					s->d1->next_handshake_write_seq = 1;
					goto end;
				}
			} else {
				if (s->rwstate != SSL_X509_LOOKUP) {
					ret = ssl3_get_client_hello(s);
					if (ret <= 0)
						goto end;
				}

				s->renegotiate = 2;
				s->s3->hs.state = SSL3_ST_SW_SRVR_HELLO_A;
				s->init_num = 0;
			}
			break;

		case DTLS1_ST_SW_HELLO_VERIFY_REQUEST_A:
		case DTLS1_ST_SW_HELLO_VERIFY_REQUEST_B:
			ret = ssl3_send_dtls_hello_verify_request(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_SW_FLUSH;
			s->s3->hs.tls12.next_state = SSL3_ST_SR_CLNT_HELLO_A;

			/* HelloVerifyRequest resets Finished MAC. */
			tls1_transcript_reset(s);
			break;

		case SSL3_ST_SW_SRVR_HELLO_A:
		case SSL3_ST_SW_SRVR_HELLO_B:
			if (SSL_is_dtls(s)) {
				s->renegotiate = 2;
				dtls1_start_timer(s);
			}
			ret = ssl3_send_server_hello(s);
			if (ret <= 0)
				goto end;
			if (s->hit) {
				if (s->tlsext_ticket_expected)
					s->s3->hs.state = SSL3_ST_SW_SESSION_TICKET_A;
				else
					s->s3->hs.state = SSL3_ST_SW_CHANGE_A;
			} else {
				s->s3->hs.state = SSL3_ST_SW_CERT_A;
			}
			s->init_num = 0;
			break;

		case SSL3_ST_SW_CERT_A:
		case SSL3_ST_SW_CERT_B:
			/* Check if it is anon DH or anon ECDH. */
			if (!(s->s3->hs.cipher->algorithm_auth &
			    SSL_aNULL)) {
				if (SSL_is_dtls(s))
					dtls1_start_timer(s);
				ret = ssl3_send_server_certificate(s);
				if (ret <= 0)
					goto end;
				if (s->tlsext_status_expected)
					s->s3->hs.state = SSL3_ST_SW_CERT_STATUS_A;
				else
					s->s3->hs.state = SSL3_ST_SW_KEY_EXCH_A;
			} else {
				skip = 1;
				s->s3->hs.state = SSL3_ST_SW_KEY_EXCH_A;
			}
			s->init_num = 0;
			break;

		case SSL3_ST_SW_KEY_EXCH_A:
		case SSL3_ST_SW_KEY_EXCH_B:
			alg_k = s->s3->hs.cipher->algorithm_mkey;

			/*
			 * Only send if using a DH key exchange.
			 *
			 * For ECC ciphersuites, we send a ServerKeyExchange
			 * message only if the cipher suite is ECDHE. In other
			 * cases, the server certificate contains the server's
			 * public key for key exchange.
			 */
			if (alg_k & (SSL_kDHE|SSL_kECDHE)) {
				if (SSL_is_dtls(s))
					dtls1_start_timer(s);
				ret = ssl3_send_server_key_exchange(s);
				if (ret <= 0)
					goto end;
			} else
				skip = 1;

			s->s3->hs.state = SSL3_ST_SW_CERT_REQ_A;
			s->init_num = 0;
			break;

		case SSL3_ST_SW_CERT_REQ_A:
		case SSL3_ST_SW_CERT_REQ_B:
			/*
			 * Determine whether or not we need to request a
			 * certificate.
			 *
			 * Do not request a certificate if:
			 *
			 * - We did not ask for it (SSL_VERIFY_PEER is unset).
			 *
			 * - SSL_VERIFY_CLIENT_ONCE is set and we are
			 *   renegotiating.
			 *
			 * - We are using an anonymous ciphersuites
			 *   (see section "Certificate request" in SSL 3 drafts
			 *   and in RFC 2246) ... except when the application
			 *   insists on verification (against the specs, but
			 *   s3_clnt.c accepts this for SSL 3).
			 */
			if (!(s->verify_mode & SSL_VERIFY_PEER) ||
			    ((s->session->peer_cert != NULL) &&
			     (s->verify_mode & SSL_VERIFY_CLIENT_ONCE)) ||
			    ((s->s3->hs.cipher->algorithm_auth &
			     SSL_aNULL) && !(s->verify_mode &
			     SSL_VERIFY_FAIL_IF_NO_PEER_CERT))) {
				/* No cert request. */
				skip = 1;
				s->s3->hs.tls12.cert_request = 0;
				s->s3->hs.state = SSL3_ST_SW_SRVR_DONE_A;

				if (!SSL_is_dtls(s))
					tls1_transcript_free(s);
			} else {
				s->s3->hs.tls12.cert_request = 1;
				if (SSL_is_dtls(s))
					dtls1_start_timer(s);
				ret = ssl3_send_certificate_request(s);
				if (ret <= 0)
					goto end;
				s->s3->hs.state = SSL3_ST_SW_SRVR_DONE_A;
				s->init_num = 0;
			}
			break;

		case SSL3_ST_SW_SRVR_DONE_A:
		case SSL3_ST_SW_SRVR_DONE_B:
			if (SSL_is_dtls(s))
				dtls1_start_timer(s);
			ret = ssl3_send_server_done(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.tls12.next_state = SSL3_ST_SR_CERT_A;
			s->s3->hs.state = SSL3_ST_SW_FLUSH;
			s->init_num = 0;
			break;

		case SSL3_ST_SW_FLUSH:
			/*
			 * This code originally checked to see if
			 * any data was pending using BIO_CTRL_INFO
			 * and then flushed. This caused problems
			 * as documented in PR#1939. The proposed
			 * fix doesn't completely resolve this issue
			 * as buggy implementations of BIO_CTRL_PENDING
			 * still exist. So instead we just flush
			 * unconditionally.
			 */
			s->rwstate = SSL_WRITING;
			if (BIO_flush(s->wbio) <= 0) {
				if (SSL_is_dtls(s)) {
					/* If the write error was fatal, stop trying. */
					if (!BIO_should_retry(s->wbio)) {
						s->rwstate = SSL_NOTHING;
						s->s3->hs.state = s->s3->hs.tls12.next_state;
					}
				}
				ret = -1;
				goto end;
			}
			s->rwstate = SSL_NOTHING;
			s->s3->hs.state = s->s3->hs.tls12.next_state;
			break;

		case SSL3_ST_SR_CERT_A:
		case SSL3_ST_SR_CERT_B:
			if (s->s3->hs.tls12.cert_request != 0) {
				ret = ssl3_get_client_certificate(s);
				if (ret <= 0)
					goto end;
			}
			s->init_num = 0;
			s->s3->hs.state = SSL3_ST_SR_KEY_EXCH_A;
			break;

		case SSL3_ST_SR_KEY_EXCH_A:
		case SSL3_ST_SR_KEY_EXCH_B:
			ret = ssl3_get_client_key_exchange(s);
			if (ret <= 0)
				goto end;

			if (SSL_is_dtls(s)) {
				s->s3->hs.state = SSL3_ST_SR_CERT_VRFY_A;
				s->init_num = 0;
			}

			alg_k = s->s3->hs.cipher->algorithm_mkey;
			if (SSL_USE_SIGALGS(s)) {
				s->s3->hs.state = SSL3_ST_SR_CERT_VRFY_A;
				s->init_num = 0;
				if (!s->session->peer_cert)
					break;
				/*
				 * Freeze the transcript for use during client
				 * certificate verification.
				 */
				tls1_transcript_freeze(s);
			} else {
				s->s3->hs.state = SSL3_ST_SR_CERT_VRFY_A;
				s->init_num = 0;

				tls1_transcript_free(s);

				/*
				 * We need to get hashes here so if there is
				 * a client cert, it can be verified.
				 */
				if (!tls1_transcript_hash_value(s,
				    s->s3->hs.tls12.cert_verify,
				    sizeof(s->s3->hs.tls12.cert_verify),
				    NULL)) {
					ret = -1;
					goto end;
				}
			}
			break;

		case SSL3_ST_SR_CERT_VRFY_A:
		case SSL3_ST_SR_CERT_VRFY_B:
			if (SSL_is_dtls(s))
				s->d1->change_cipher_spec_ok = 1;
			else
				s->s3->flags |= SSL3_FLAGS_CCS_OK;

			/* we should decide if we expected this one */
			ret = ssl3_get_cert_verify(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_SR_FINISHED_A;
			s->init_num = 0;
			break;

		case SSL3_ST_SR_FINISHED_A:
		case SSL3_ST_SR_FINISHED_B:
			if (SSL_is_dtls(s))
				s->d1->change_cipher_spec_ok = 1;
			else
				s->s3->flags |= SSL3_FLAGS_CCS_OK;
			ret = ssl3_get_client_finished(s);
			if (ret <= 0)
				goto end;
			if (SSL_is_dtls(s))
				dtls1_stop_timer(s);
			if (s->hit)
				s->s3->hs.state = SSL_ST_OK;
			else if (s->tlsext_ticket_expected)
				s->s3->hs.state = SSL3_ST_SW_SESSION_TICKET_A;
			else
				s->s3->hs.state = SSL3_ST_SW_CHANGE_A;
			s->init_num = 0;
			break;

		case SSL3_ST_SW_SESSION_TICKET_A:
		case SSL3_ST_SW_SESSION_TICKET_B:
			ret = ssl3_send_newsession_ticket(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_SW_CHANGE_A;
			s->init_num = 0;
			break;

		case SSL3_ST_SW_CERT_STATUS_A:
		case SSL3_ST_SW_CERT_STATUS_B:
			ret = ssl3_send_cert_status(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_SW_KEY_EXCH_A;
			s->init_num = 0;
			break;

		case SSL3_ST_SW_CHANGE_A:
		case SSL3_ST_SW_CHANGE_B:
			ret = ssl3_send_server_change_cipher_spec(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_SW_FINISHED_A;
			s->init_num = 0;
			s->session->cipher_value = s->s3->hs.cipher->value;

			if (!tls1_setup_key_block(s)) {
				ret = -1;
				goto end;
			}
			if (!tls1_change_write_cipher_state(s)) {
				ret = -1;
				goto end;
			}
			break;

		case SSL3_ST_SW_FINISHED_A:
		case SSL3_ST_SW_FINISHED_B:
			ret = ssl3_send_server_finished(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_SW_FLUSH;
			if (s->hit) {
				s->s3->hs.tls12.next_state = SSL3_ST_SR_FINISHED_A;
				tls1_transcript_free(s);
			} else
				s->s3->hs.tls12.next_state = SSL_ST_OK;
			s->init_num = 0;
			break;

		case SSL_ST_OK:
			/* clean a few things up */
			tls1_cleanup_key_block(s);

			if (s->s3->handshake_transcript != NULL) {
				SSLerror(s, ERR_R_INTERNAL_ERROR);
				ret = -1;
				goto end;
			}

			if (!SSL_is_dtls(s))
				ssl3_release_init_buffer(s);

			/* remove buffering on output */
			ssl_free_wbio_buffer(s);

			s->init_num = 0;

			/* Skipped if we just sent a HelloRequest. */
			if (s->renegotiate == 2) {
				s->renegotiate = 0;
				s->new_session = 0;

				ssl_update_cache(s, SSL_SESS_CACHE_SERVER);

				s->ctx->stats.sess_accept_good++;
				/* s->server=1; */
				s->handshake_func = ssl3_accept;

				ssl_info_callback(s, SSL_CB_HANDSHAKE_DONE, 1);
			}

			ret = 1;

			if (SSL_is_dtls(s)) {
				/* Done handshaking, next message is client hello. */
				s->d1->handshake_read_seq = 0;
				/* Next message is server hello. */
				s->d1->handshake_write_seq = 0;
				s->d1->next_handshake_write_seq = 0;
			}
			goto end;
			/* break; */

		default:
			SSLerror(s, SSL_R_UNKNOWN_STATE);
			ret = -1;
			goto end;
			/* break; */
		}

		if (!s->s3->hs.tls12.reuse_message && !skip) {
			if (s->s3->hs.state != state) {
				new_state = s->s3->hs.state;
				s->s3->hs.state = state;
				ssl_info_callback(s, SSL_CB_ACCEPT_LOOP, 1);
				s->s3->hs.state = new_state;
			}
		}
		skip = 0;
	}
 end:
	/* BIO_flush(s->wbio); */
	s->in_handshake--;
	ssl_info_callback(s, SSL_CB_ACCEPT_EXIT, ret);

	return (ret);
}

static int
ssl3_send_hello_request(SSL *s)
{
	CBB cbb, hello;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_SW_HELLO_REQ_A) {
		if (!ssl3_handshake_msg_start(s, &cbb, &hello,
		    SSL3_MT_HELLO_REQUEST))
			goto err;
		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_SW_HELLO_REQ_B;
	}

	/* SSL3_ST_SW_HELLO_REQ_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_get_client_hello(SSL *s)
{
	CBS cbs, client_random, session_id, cookie, cipher_suites;
	CBS compression_methods;
	uint16_t client_version;
	uint8_t comp_method;
	int comp_null;
	int i, j, al, ret, cookie_valid = 0;
	SSL_CIPHER *c;
	STACK_OF(SSL_CIPHER) *ciphers = NULL;
	const SSL_METHOD *method;
	uint16_t shared_version;

	/*
	 * We do this so that we will respond with our native type.
	 * If we are TLSv1 and we get SSLv3, we will respond with TLSv1,
	 * This down switching should be handled by a different method.
	 * If we are SSLv3, we will respond with SSLv3, even if prompted with
	 * TLSv1.
	 */
	if (s->s3->hs.state == SSL3_ST_SR_CLNT_HELLO_A)
		s->s3->hs.state = SSL3_ST_SR_CLNT_HELLO_B;

	s->first_packet = 1;
	if ((ret = ssl3_get_message(s, SSL3_ST_SR_CLNT_HELLO_B,
	    SSL3_ST_SR_CLNT_HELLO_C, SSL3_MT_CLIENT_HELLO,
	    SSL3_RT_MAX_PLAIN_LENGTH)) <= 0)
		return ret;
	s->first_packet = 0;

	ret = -1;

	if (s->init_num < 0)
		goto err;

	CBS_init(&cbs, s->init_msg, s->init_num);

	/* Parse client hello up until the extensions (if any). */
	if (!CBS_get_u16(&cbs, &client_version))
		goto decode_err;
	if (!CBS_get_bytes(&cbs, &client_random, SSL3_RANDOM_SIZE))
		goto decode_err;
	if (!CBS_get_u8_length_prefixed(&cbs, &session_id))
		goto decode_err;
	if (CBS_len(&session_id) > SSL3_SESSION_ID_SIZE) {
		al = SSL_AD_ILLEGAL_PARAMETER;
		SSLerror(s, SSL_R_SSL3_SESSION_ID_TOO_LONG);
		goto fatal_err;
	}
	if (SSL_is_dtls(s)) {
		if (!CBS_get_u8_length_prefixed(&cbs, &cookie))
			goto decode_err;
	}
	if (!CBS_get_u16_length_prefixed(&cbs, &cipher_suites))
		goto decode_err;
	if (!CBS_get_u8_length_prefixed(&cbs, &compression_methods))
		goto decode_err;

	/*
	 * Use version from inside client hello, not from record header.
	 * (may differ: see RFC 2246, Appendix E, second paragraph)
	 */
	if (!ssl_max_shared_version(s, client_version, &shared_version)) {
		if ((client_version >> 8) == SSL3_VERSION_MAJOR &&
		    !tls12_record_layer_write_protected(s->rl)) {
			/*
			 * Similar to ssl3_get_record, send alert using remote
			 * version number.
			 */
			s->version = client_version;
		}
		SSLerror(s, SSL_R_WRONG_VERSION_NUMBER);
		al = SSL_AD_PROTOCOL_VERSION;
		goto fatal_err;
	}
	s->s3->hs.peer_legacy_version = client_version;
	s->version = shared_version;

	s->s3->hs.negotiated_tls_version = ssl_tls_version(shared_version);
	if (s->s3->hs.negotiated_tls_version == 0) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		goto err;
	}

	if ((method = ssl_get_method(shared_version)) == NULL) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		goto err;
	}
	s->method = method;

	/*
	 * If we require cookies (DTLS) and this ClientHello does not contain
	 * one, just return since we do not want to allocate any memory yet.
	 * So check cookie length...
	 */
	if (SSL_is_dtls(s)) {
		if (SSL_get_options(s) & SSL_OP_COOKIE_EXCHANGE) {
			if (CBS_len(&cookie) == 0)
				return (1);
		}
	}

	if (!CBS_write_bytes(&client_random, s->s3->client_random,
	    sizeof(s->s3->client_random), NULL))
		goto err;

	s->hit = 0;

	/*
	 * Versions before 0.9.7 always allow clients to resume sessions in
	 * renegotiation. 0.9.7 and later allow this by default, but optionally
	 * ignore resumption requests with flag
	 * SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION (it's a new flag
	 * rather than a change to default behavior so that applications
	 * relying on this for security won't even compile against older
	 * library versions).
	 *
	 * 1.0.1 and later also have a function SSL_renegotiate_abbreviated()
	 * to request renegotiation but not a new session (s->new_session
	 * remains unset): for servers, this essentially just means that the
	 * SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION setting will be
	 * ignored.
	 */
	if ((s->new_session && (s->options &
	    SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION))) {
		if (!ssl_get_new_session(s, 1))
			goto err;
	} else {
		CBS ext_block;

		CBS_dup(&cbs, &ext_block);

		i = ssl_get_prev_session(s, &session_id, &ext_block, &al);
		if (i == 1) { /* previous session */
			s->hit = 1;
		} else if (i == -1)
			goto fatal_err;
		else {
			/* i == 0 */
			if (!ssl_get_new_session(s, 1))
				goto err;
		}
	}

	if (SSL_is_dtls(s)) {
		/*
		 * The ClientHello may contain a cookie even if the HelloVerify
		 * message has not been sent - make sure that it does not cause
		 * an overflow.
		 */
		if (CBS_len(&cookie) > sizeof(s->d1->rcvd_cookie)) {
			al = SSL_AD_DECODE_ERROR;
			SSLerror(s, SSL_R_COOKIE_MISMATCH);
			goto fatal_err;
		}

		/* Verify the cookie if appropriate option is set. */
		if ((SSL_get_options(s) & SSL_OP_COOKIE_EXCHANGE) &&
		    CBS_len(&cookie) > 0) {
			size_t cookie_len;

			/* XXX - rcvd_cookie seems to only be used here... */
			if (!CBS_write_bytes(&cookie, s->d1->rcvd_cookie,
			    sizeof(s->d1->rcvd_cookie), &cookie_len))
				goto err;

			if (s->ctx->app_verify_cookie_cb != NULL) {
				if (s->ctx->app_verify_cookie_cb(s,
				    s->d1->rcvd_cookie, cookie_len) == 0) {
					al = SSL_AD_HANDSHAKE_FAILURE;
					SSLerror(s, SSL_R_COOKIE_MISMATCH);
					goto fatal_err;
				}
				/* else cookie verification succeeded */
			/* XXX - can d1->cookie_len > sizeof(rcvd_cookie) ? */
			} else if (timingsafe_memcmp(s->d1->rcvd_cookie,
			    s->d1->cookie, s->d1->cookie_len) != 0) {
				/* default verification */
				al = SSL_AD_HANDSHAKE_FAILURE;
				SSLerror(s, SSL_R_COOKIE_MISMATCH);
				goto fatal_err;
			}
			cookie_valid = 1;
		}
	}

	/* XXX - This logic seems wrong... */
	if (CBS_len(&cipher_suites) == 0 && CBS_len(&session_id) != 0) {
		/* we need a cipher if we are not resuming a session */
		al = SSL_AD_ILLEGAL_PARAMETER;
		SSLerror(s, SSL_R_NO_CIPHERS_SPECIFIED);
		goto fatal_err;
	}

	if (CBS_len(&cipher_suites) > 0) {
		if ((ciphers = ssl_bytes_to_cipher_list(s,
		    &cipher_suites)) == NULL)
			goto err;
	}

	/* If it is a hit, check that the cipher is in the list */
	/* XXX - CBS_len(&cipher_suites) will always be zero here... */
	if (s->hit && CBS_len(&cipher_suites) > 0) {
		j = 0;

		for (i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
			c = sk_SSL_CIPHER_value(ciphers, i);
			if (c->value == s->session->cipher_value) {
				j = 1;
				break;
			}
		}
		if (j == 0) {
			/*
			 * We need to have the cipher in the cipher
			 * list if we are asked to reuse it
			 */
			al = SSL_AD_ILLEGAL_PARAMETER;
			SSLerror(s, SSL_R_REQUIRED_CIPHER_MISSING);
			goto fatal_err;
		}
	}

	comp_null = 0;
	while (CBS_len(&compression_methods) > 0) {
		if (!CBS_get_u8(&compression_methods, &comp_method))
			goto decode_err;
		if (comp_method == 0)
			comp_null = 1;
	}
	if (comp_null == 0) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_NO_COMPRESSION_SPECIFIED);
		goto fatal_err;
	}

	if (!tlsext_server_parse(s, SSL_TLSEXT_MSG_CH, &cbs, &al)) {
		SSLerror(s, SSL_R_PARSE_TLSEXT);
		goto fatal_err;
	}

	if (CBS_len(&cbs) != 0)
		goto decode_err;

	if (!s->s3->renegotiate_seen && s->renegotiate) {
		al = SSL_AD_HANDSHAKE_FAILURE;
		SSLerror(s, SSL_R_UNSAFE_LEGACY_RENEGOTIATION_DISABLED);
		goto fatal_err;
	}

	if (ssl_check_clienthello_tlsext_early(s) <= 0) {
		SSLerror(s, SSL_R_CLIENTHELLO_TLSEXT);
		goto err;
	}

	/*
	 * Check if we want to use external pre-shared secret for this
	 * handshake for not reused session only. We need to generate
	 * server_random before calling tls_session_secret_cb in order to allow
	 * SessionTicket processing to use it in key derivation.
	 */
	arc4random_buf(s->s3->server_random, SSL3_RANDOM_SIZE);

	if (s->s3->hs.our_max_tls_version >= TLS1_2_VERSION &&
	    s->s3->hs.negotiated_tls_version < s->s3->hs.our_max_tls_version) {
		/*
		 * RFC 8446 section 4.1.3. If we are downgrading from TLS 1.3
		 * we must set the last 8 bytes of the server random to magical
		 * values to indicate we meant to downgrade.  For TLS 1.2 it is
		 * recommended that we do the same.
		 */
		size_t index = SSL3_RANDOM_SIZE - sizeof(tls13_downgrade_12);
		uint8_t *magic = &s->s3->server_random[index];
		if (s->s3->hs.negotiated_tls_version == TLS1_2_VERSION) {
			/* Indicate we chose to downgrade to 1.2. */
			memcpy(magic, tls13_downgrade_12,
			    sizeof(tls13_downgrade_12));
		} else {
			/* Indicate we chose to downgrade to 1.1 or lower */
			memcpy(magic, tls13_downgrade_11,
			    sizeof(tls13_downgrade_11));
		}
	}

	if (!s->hit && s->tls_session_secret_cb != NULL) {
		const SSL_CIPHER *pref_cipher = NULL;
		int master_key_length = sizeof(s->session->master_key);

		if (!s->tls_session_secret_cb(s,
		    s->session->master_key, &master_key_length, ciphers,
		    &pref_cipher, s->tls_session_secret_cb_arg)) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}
		if (master_key_length <= 0) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}
		s->session->master_key_length = master_key_length;

		s->hit = 1;
		s->session->verify_result = X509_V_OK;

		sk_SSL_CIPHER_free(s->s3->hs.client_ciphers);
		s->s3->hs.client_ciphers = ciphers;
		ciphers = NULL;

		/*
		 * XXX - this allows the callback to use any client cipher and
		 * completely ignore the server cipher list. We should ensure
		 * that the pref_cipher is in both the client list and the
		 * server list.
		 */

		/* Check if some cipher was preferred by the callback. */
		if (pref_cipher == NULL)
			pref_cipher = ssl3_choose_cipher(s, s->s3->hs.client_ciphers,
			    SSL_get_ciphers(s));
		if (pref_cipher == NULL) {
			al = SSL_AD_HANDSHAKE_FAILURE;
			SSLerror(s, SSL_R_NO_SHARED_CIPHER);
			goto fatal_err;
		}
		s->s3->hs.cipher = pref_cipher;

		/* XXX - why? */
		sk_SSL_CIPHER_free(s->cipher_list);
		s->cipher_list = sk_SSL_CIPHER_dup(s->s3->hs.client_ciphers);
	}

	/*
	 * Given s->session->ciphers and SSL_get_ciphers, we must
	 * pick a cipher
	 */

	if (!s->hit) {
		if (ciphers == NULL) {
			al = SSL_AD_ILLEGAL_PARAMETER;
			SSLerror(s, SSL_R_NO_CIPHERS_PASSED);
			goto fatal_err;
		}
		sk_SSL_CIPHER_free(s->s3->hs.client_ciphers);
		s->s3->hs.client_ciphers = ciphers;
		ciphers = NULL;

		if ((c = ssl3_choose_cipher(s, s->s3->hs.client_ciphers,
		    SSL_get_ciphers(s))) == NULL) {
			al = SSL_AD_HANDSHAKE_FAILURE;
			SSLerror(s, SSL_R_NO_SHARED_CIPHER);
			goto fatal_err;
		}
		s->s3->hs.cipher = c;
		s->session->cipher_value = s->s3->hs.cipher->value;
	} else {
		s->s3->hs.cipher = ssl3_get_cipher_by_value(s->session->cipher_value);
		if (s->s3->hs.cipher == NULL)
			goto fatal_err;
	}

	if (!tls1_transcript_hash_init(s))
		goto err;

	if (!SSL_USE_SIGALGS(s) || !(s->verify_mode & SSL_VERIFY_PEER))
		tls1_transcript_free(s);

	/*
	 * We now have the following setup.
	 * client_random
	 * cipher_list		- our preferred list of ciphers
	 * ciphers		- the clients preferred list of ciphers
	 * compression		- basically ignored right now
	 * ssl version is set	- sslv3
	 * s->session		- The ssl session has been setup.
	 * s->hit		- session reuse flag
	 * s->hs.cipher	- the new cipher to use.
	 */

	/* Handles TLS extensions that we couldn't check earlier */
	if (ssl_check_clienthello_tlsext_late(s) <= 0) {
		SSLerror(s, SSL_R_CLIENTHELLO_TLSEXT);
		goto err;
	}

	ret = cookie_valid ? 2 : 1;

	if (0) {
 decode_err:
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
 fatal_err:
		ssl3_send_alert(s, SSL3_AL_FATAL, al);
	}
 err:
	sk_SSL_CIPHER_free(ciphers);

	return (ret);
}

static int
ssl3_send_dtls_hello_verify_request(SSL *s)
{
	CBB cbb, verify, cookie;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == DTLS1_ST_SW_HELLO_VERIFY_REQUEST_A) {
		if (s->ctx->app_gen_cookie_cb == NULL ||
		    s->ctx->app_gen_cookie_cb(s, s->d1->cookie,
			&(s->d1->cookie_len)) == 0) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			return 0;
		}

		/*
		 * Per RFC 6347 section 4.2.1, the HelloVerifyRequest should
		 * always contain DTLSv1.0 regardless of the version that is
		 * going to be negotiated.
		 */
		if (!ssl3_handshake_msg_start(s, &cbb, &verify,
		    DTLS1_MT_HELLO_VERIFY_REQUEST))
			goto err;
		if (!CBB_add_u16(&verify, DTLS1_VERSION))
			goto err;
		if (!CBB_add_u8_length_prefixed(&verify, &cookie))
			goto err;
		if (!CBB_add_bytes(&cookie, s->d1->cookie, s->d1->cookie_len))
			goto err;
		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = DTLS1_ST_SW_HELLO_VERIFY_REQUEST_B;
	}

	/* s->s3->hs.state = DTLS1_ST_SW_HELLO_VERIFY_REQUEST_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_send_server_hello(SSL *s)
{
	CBB cbb, server_hello, session_id;
	size_t sl;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_SW_SRVR_HELLO_A) {
		if (!ssl3_handshake_msg_start(s, &cbb, &server_hello,
		    SSL3_MT_SERVER_HELLO))
			goto err;

		if (!CBB_add_u16(&server_hello, s->version))
			goto err;
		if (!CBB_add_bytes(&server_hello, s->s3->server_random,
		    sizeof(s->s3->server_random)))
			goto err;

		/*
		 * There are several cases for the session ID to send
		 * back in the server hello:
		 *
		 * - For session reuse from the session cache,
		 *   we send back the old session ID.
		 * - If stateless session reuse (using a session ticket)
		 *   is successful, we send back the client's "session ID"
		 *   (which doesn't actually identify the session).
		 * - If it is a new session, we send back the new
		 *   session ID.
		 * - However, if we want the new session to be single-use,
		 *   we send back a 0-length session ID.
		 *
		 * s->hit is non-zero in either case of session reuse,
		 * so the following won't overwrite an ID that we're supposed
		 * to send back.
		 */
		if (!(s->ctx->session_cache_mode & SSL_SESS_CACHE_SERVER)
		    && !s->hit)
			s->session->session_id_length = 0;

		sl = s->session->session_id_length;
		if (sl > sizeof(s->session->session_id)) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}
		if (!CBB_add_u8_length_prefixed(&server_hello, &session_id))
			goto err;
		if (!CBB_add_bytes(&session_id, s->session->session_id, sl))
			goto err;

		/* Cipher suite. */
		if (!CBB_add_u16(&server_hello, s->s3->hs.cipher->value))
			goto err;

		/* Compression method (null). */
		if (!CBB_add_u8(&server_hello, 0))
			goto err;

		/* TLS extensions */
		if (!tlsext_server_build(s, SSL_TLSEXT_MSG_SH, &server_hello)) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}

		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;
	}

	/* SSL3_ST_SW_SRVR_HELLO_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_send_server_done(SSL *s)
{
	CBB cbb, done;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_SW_SRVR_DONE_A) {
		if (!ssl3_handshake_msg_start(s, &cbb, &done,
		    SSL3_MT_SERVER_DONE))
			goto err;
		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_SW_SRVR_DONE_B;
	}

	/* SSL3_ST_SW_SRVR_DONE_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_send_server_kex_dhe(SSL *s, CBB *cbb)
{
	int nid = NID_dhKeyAgreement;

	tls_key_share_free(s->s3->hs.key_share);
	if ((s->s3->hs.key_share = tls_key_share_new_nid(nid)) == NULL)
		goto err;

	if (s->cert->dhe_params_auto != 0) {
		size_t key_bits;

		if ((key_bits = ssl_dhe_params_auto_key_bits(s)) == 0) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_INTERNAL_ERROR);
			goto err;
		}
		tls_key_share_set_key_bits(s->s3->hs.key_share,
		    key_bits);
	} else {
		DH *dh_params = s->cert->dhe_params;

		if (dh_params == NULL && s->cert->dhe_params_cb != NULL)
			dh_params = s->cert->dhe_params_cb(s, 0,
			    SSL_C_PKEYLENGTH(s->s3->hs.cipher));

		if (dh_params == NULL) {
			SSLerror(s, SSL_R_MISSING_TMP_DH_KEY);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_HANDSHAKE_FAILURE);
			goto err;
		}

		if (!tls_key_share_set_dh_params(s->s3->hs.key_share, dh_params))
			goto err;
	}

	if (!tls_key_share_generate(s->s3->hs.key_share))
		goto err;

	if (!tls_key_share_params(s->s3->hs.key_share, cbb))
		goto err;
	if (!tls_key_share_public(s->s3->hs.key_share, cbb))
		goto err;

	if (!tls_key_share_peer_security(s, s->s3->hs.key_share)) {
		SSLerror(s, SSL_R_DH_KEY_TOO_SMALL);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		return 0;
	}

	return 1;

 err:
	return 0;
}

static int
ssl3_send_server_kex_ecdhe(SSL *s, CBB *cbb)
{
	CBB public;
	int nid;

	if (!tls1_get_supported_group(s, &nid)) {
		SSLerror(s, SSL_R_UNSUPPORTED_ELLIPTIC_CURVE);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		goto err;
	}

	tls_key_share_free(s->s3->hs.key_share);
	if ((s->s3->hs.key_share = tls_key_share_new_nid(nid)) == NULL)
		goto err;

	if (!tls_key_share_generate(s->s3->hs.key_share))
		goto err;

	/*
	 * ECC key exchange - see RFC 8422, section 5.4.
	 */
	if (!CBB_add_u8(cbb, NAMED_CURVE_TYPE))
		goto err;
	if (!CBB_add_u16(cbb, tls_key_share_group(s->s3->hs.key_share)))
		goto err;
	if (!CBB_add_u8_length_prefixed(cbb, &public))
		goto err;
	if (!tls_key_share_public(s->s3->hs.key_share, &public))
		goto err;
	if (!CBB_flush(cbb))
		goto err;

	return 1;

 err:
	return 0;
}

static int
ssl3_send_server_key_exchange(SSL *s)
{
	CBB cbb, cbb_signature, cbb_signed_params, server_kex;
	CBS params;
	const struct ssl_sigalg *sigalg = NULL;
	unsigned char *signed_params = NULL;
	size_t signed_params_len;
	unsigned char *signature = NULL;
	size_t signature_len = 0;
	const EVP_MD *md = NULL;
	unsigned long type;
	EVP_MD_CTX *md_ctx = NULL;
	EVP_PKEY_CTX *pctx;
	EVP_PKEY *pkey;
	int al;

	memset(&cbb, 0, sizeof(cbb));
	memset(&cbb_signed_params, 0, sizeof(cbb_signed_params));

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	if (s->s3->hs.state == SSL3_ST_SW_KEY_EXCH_A) {

		if (!ssl3_handshake_msg_start(s, &cbb, &server_kex,
		    SSL3_MT_SERVER_KEY_EXCHANGE))
			goto err;

		if (!CBB_init(&cbb_signed_params, 0))
			goto err;

		if (!CBB_add_bytes(&cbb_signed_params, s->s3->client_random,
		    SSL3_RANDOM_SIZE)) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}
		if (!CBB_add_bytes(&cbb_signed_params, s->s3->server_random,
		    SSL3_RANDOM_SIZE)) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}

		type = s->s3->hs.cipher->algorithm_mkey;
		if (type & SSL_kDHE) {
			if (!ssl3_send_server_kex_dhe(s, &cbb_signed_params))
				goto err;
		} else if (type & SSL_kECDHE) {
			if (!ssl3_send_server_kex_ecdhe(s, &cbb_signed_params))
				goto err;
		} else {
			al = SSL_AD_HANDSHAKE_FAILURE;
			SSLerror(s, SSL_R_UNKNOWN_KEY_EXCHANGE_TYPE);
			goto fatal_err;
		}

		if (!CBB_finish(&cbb_signed_params, &signed_params,
		    &signed_params_len))
			goto err;

		CBS_init(&params, signed_params, signed_params_len);
		if (!CBS_skip(&params, 2 * SSL3_RANDOM_SIZE))
			goto err;

		if (!CBB_add_bytes(&server_kex, CBS_data(&params),
		    CBS_len(&params)))
			goto err;

		/* Add signature unless anonymous. */
		if (!(s->s3->hs.cipher->algorithm_auth & SSL_aNULL)) {
			if ((pkey = ssl_get_sign_pkey(s, s->s3->hs.cipher,
			    &md, &sigalg)) == NULL) {
				al = SSL_AD_DECODE_ERROR;
				goto fatal_err;
			}
			s->s3->hs.our_sigalg = sigalg;

			/* Send signature algorithm. */
			if (SSL_USE_SIGALGS(s)) {
				if (!CBB_add_u16(&server_kex, sigalg->value)) {
					al = SSL_AD_INTERNAL_ERROR;
					SSLerror(s, ERR_R_INTERNAL_ERROR);
					goto fatal_err;
				}
			}

			if (!EVP_DigestSignInit(md_ctx, &pctx, md, NULL, pkey)) {
				SSLerror(s, ERR_R_EVP_LIB);
				goto err;
			}
			if ((sigalg->flags & SIGALG_FLAG_RSA_PSS) &&
			    (!EVP_PKEY_CTX_set_rsa_padding(pctx,
			    RSA_PKCS1_PSS_PADDING) ||
			    !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1))) {
				SSLerror(s, ERR_R_EVP_LIB);
				goto err;
			}
			if (!EVP_DigestSign(md_ctx, NULL, &signature_len,
			    signed_params, signed_params_len)) {
				SSLerror(s, ERR_R_EVP_LIB);
				goto err;
			}
			if ((signature = calloc(1, signature_len)) == NULL) {
				SSLerror(s, ERR_R_MALLOC_FAILURE);
				goto err;
			}
			if (!EVP_DigestSign(md_ctx, signature, &signature_len,
			    signed_params, signed_params_len)) {
				SSLerror(s, ERR_R_EVP_LIB);
				goto err;
			}

			if (!CBB_add_u16_length_prefixed(&server_kex,
			    &cbb_signature))
				goto err;
			if (!CBB_add_bytes(&cbb_signature, signature,
			    signature_len))
				goto err;
		}

		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_SW_KEY_EXCH_B;
	}

	EVP_MD_CTX_free(md_ctx);
	free(signature);
	free(signed_params);

	return (ssl3_handshake_write(s));

 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
	CBB_cleanup(&cbb_signed_params);
	CBB_cleanup(&cbb);
	EVP_MD_CTX_free(md_ctx);
	free(signature);
	free(signed_params);

	return (-1);
}

static int
ssl3_send_certificate_request(SSL *s)
{
	CBB cbb, cert_request, cert_types, sigalgs, cert_auth, dn;
	STACK_OF(X509_NAME) *sk = NULL;
	X509_NAME *name;
	int i;

	/*
	 * Certificate Request - RFC 5246 section 7.4.4.
	 */

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_SW_CERT_REQ_A) {
		if (!ssl3_handshake_msg_start(s, &cbb, &cert_request,
		    SSL3_MT_CERTIFICATE_REQUEST))
			goto err;

		if (!CBB_add_u8_length_prefixed(&cert_request, &cert_types))
			goto err;
		if (!ssl3_get_req_cert_types(s, &cert_types))
			goto err;

		if (SSL_USE_SIGALGS(s)) {
			if (!CBB_add_u16_length_prefixed(&cert_request,
			    &sigalgs))
				goto err;
			if (!ssl_sigalgs_build(s->s3->hs.negotiated_tls_version,
			    &sigalgs, SSL_get_security_level(s)))
				goto err;
		}

		if (!CBB_add_u16_length_prefixed(&cert_request, &cert_auth))
			goto err;

		sk = SSL_get_client_CA_list(s);
		for (i = 0; i < sk_X509_NAME_num(sk); i++) {
			unsigned char *name_data;
			size_t name_len;

			name = sk_X509_NAME_value(sk, i);
			name_len = i2d_X509_NAME(name, NULL);

			if (!CBB_add_u16_length_prefixed(&cert_auth, &dn))
				goto err;
			if (!CBB_add_space(&dn, &name_data, name_len))
				goto err;
			if (i2d_X509_NAME(name, &name_data) != name_len)
				goto err;
		}

		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_SW_CERT_REQ_B;
	}

	/* SSL3_ST_SW_CERT_REQ_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_get_client_kex_rsa(SSL *s, CBS *cbs)
{
	uint8_t fakepms[SSL_MAX_MASTER_KEY_LENGTH];
	uint8_t *pms = NULL;
	size_t pms_len = 0;
	size_t pad_len;
	EVP_PKEY *pkey = NULL;
	RSA *rsa = NULL;
	CBS enc_pms;
	int decrypt_len;
	uint8_t mask;
	size_t i;
	int valid = 1;
	int ret = 0;

	/*
	 * Handle key exchange in the form of an RSA-Encrypted Premaster Secret
	 * Message. See RFC 5246, section 7.4.7.1.
	 */

	arc4random_buf(fakepms, sizeof(fakepms));

	fakepms[0] = s->s3->hs.peer_legacy_version >> 8;
	fakepms[1] = s->s3->hs.peer_legacy_version & 0xff;

	pkey = s->cert->pkeys[SSL_PKEY_RSA].privatekey;
	if (pkey == NULL || (rsa = EVP_PKEY_get0_RSA(pkey)) == NULL) {
		SSLerror(s, SSL_R_MISSING_RSA_CERTIFICATE);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		goto err;
	}

	/*
	 * The minimum size of an encrypted premaster secret is 11 bytes of
	 * padding (00 02 <8 or more non-zero bytes> 00) (RFC 8017, section
	 * 9.2) and 48 bytes of premaster secret (RFC 5246, section 7.4.7.1).
	 * This means an RSA key size of at least 472 bits.
	 */
	pms_len = RSA_size(rsa);
	if (pms_len < 11 + SSL_MAX_MASTER_KEY_LENGTH) {
		SSLerror(s, SSL_R_DECRYPTION_FAILED);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECRYPT_ERROR);
		goto err;
	}
	pad_len = pms_len - SSL_MAX_MASTER_KEY_LENGTH;

	if (!CBS_get_u16_length_prefixed(cbs, &enc_pms)) {
		SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		goto err;
	}
	if (CBS_len(&enc_pms) != pms_len || CBS_len(cbs) != 0) {
		SSLerror(s, SSL_R_TLS_RSA_ENCRYPTED_VALUE_LENGTH_IS_WRONG);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		goto err;
	}

	if ((pms = calloc(1, pms_len)) == NULL)
		goto err;

	decrypt_len = RSA_private_decrypt(CBS_len(&enc_pms), CBS_data(&enc_pms),
	    pms, rsa, RSA_NO_PADDING);

	if (decrypt_len != pms_len) {
		SSLerror(s, SSL_R_DECRYPTION_FAILED);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECRYPT_ERROR);
		goto err;
	}

	/*
	 * All processing from here on needs to avoid leaking any information
	 * about the decrypted content, in order to prevent oracle attacks and
	 * minimise timing attacks.
	 */

	/* Check padding - 00 02 <8 or more non-zero bytes> 00 */
	valid &= crypto_ct_eq_u8(pms[0], 0x00);
	valid &= crypto_ct_eq_u8(pms[1], 0x02);
	for (i = 2; i < pad_len - 1; i++)
		valid &= crypto_ct_ne_u8(pms[i], 0x00);
	valid &= crypto_ct_eq_u8(pms[pad_len - 1], 0x00);

	/* Ensure client version in premaster secret matches ClientHello version. */
	valid &= crypto_ct_eq_u8(pms[pad_len + 0], s->s3->hs.peer_legacy_version >> 8);
	valid &= crypto_ct_eq_u8(pms[pad_len + 1], s->s3->hs.peer_legacy_version & 0xff);

	/* Use the premaster secret if padding is correct, if not use the fake. */
	mask = crypto_ct_eq_mask_u8(valid, 1);
	for (i = 0; i < SSL_MAX_MASTER_KEY_LENGTH; i++)
		pms[i] = (pms[pad_len + i] & mask) | (fakepms[i] & ~mask);

	if (!tls12_derive_master_secret(s, pms, SSL_MAX_MASTER_KEY_LENGTH))
		goto err;

	ret = 1;

 err:
	freezero(pms, pms_len);

	return ret;
}

static int
ssl3_get_client_kex_dhe(SSL *s, CBS *cbs)
{
	uint8_t *key = NULL;
	size_t key_len = 0;
	int decode_error, invalid_key;
	int ret = 0;

	if (s->s3->hs.key_share == NULL) {
		SSLerror(s, SSL_R_MISSING_TMP_DH_KEY);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		goto err;
	}

	if (!tls_key_share_peer_public(s->s3->hs.key_share, cbs,
	    &decode_error, &invalid_key)) {
		if (decode_error) {
			SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		}
		goto err;
	}
	if (invalid_key) {
		SSLerror(s, SSL_R_BAD_DH_PUB_KEY_LENGTH);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
		goto err;
	}

	if (!tls_key_share_derive(s->s3->hs.key_share, &key, &key_len))
		goto err;

	if (!tls12_derive_master_secret(s, key, key_len))
		goto err;

	ret = 1;

 err:
	freezero(key, key_len);

	return ret;
}

static int
ssl3_get_client_kex_ecdhe(SSL *s, CBS *cbs)
{
	uint8_t *key = NULL;
	size_t key_len = 0;
	int decode_error;
	CBS public;
	int ret = 0;

	if (s->s3->hs.key_share == NULL) {
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		SSLerror(s, SSL_R_MISSING_TMP_DH_KEY);
		goto err;
	}

	if (!CBS_get_u8_length_prefixed(cbs, &public)) {
		SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		goto err;
	}
	if (!tls_key_share_peer_public(s->s3->hs.key_share, &public,
	    &decode_error, NULL)) {
		if (decode_error) {
			SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		}
		goto err;
	}

	if (!tls_key_share_derive(s->s3->hs.key_share, &key, &key_len))
		goto err;

	if (!tls12_derive_master_secret(s, key, key_len))
		goto err;

	ret = 1;

 err:
	freezero(key, key_len);

	return ret;
}

static int
ssl3_get_client_key_exchange(SSL *s)
{
	unsigned long alg_k;
	int al, ret;
	CBS cbs;

	/* 2048 maxlen is a guess.  How long a key does that permit? */
	if ((ret = ssl3_get_message(s, SSL3_ST_SR_KEY_EXCH_A,
	    SSL3_ST_SR_KEY_EXCH_B, SSL3_MT_CLIENT_KEY_EXCHANGE, 2048)) <= 0)
		return ret;

	if (s->init_num < 0)
		goto err;

	CBS_init(&cbs, s->init_msg, s->init_num);

	alg_k = s->s3->hs.cipher->algorithm_mkey;

	if (alg_k & SSL_kRSA) {
		if (!ssl3_get_client_kex_rsa(s, &cbs))
			goto err;
	} else if (alg_k & SSL_kDHE) {
		if (!ssl3_get_client_kex_dhe(s, &cbs))
			goto err;
	} else if (alg_k & SSL_kECDHE) {
		if (!ssl3_get_client_kex_ecdhe(s, &cbs))
			goto err;
	} else {
		al = SSL_AD_HANDSHAKE_FAILURE;
		SSLerror(s, SSL_R_UNKNOWN_CIPHER_TYPE);
		goto fatal_err;
	}

	if (CBS_len(&cbs) != 0) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
		goto fatal_err;
	}

	return (1);

 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
	return (-1);
}

static int
ssl3_get_cert_verify(SSL *s)
{
	CBS cbs, signature;
	const struct ssl_sigalg *sigalg = NULL;
	uint16_t sigalg_value = SIGALG_NONE;
	EVP_PKEY *pkey;
	X509 *peer_cert = NULL;
	EVP_MD_CTX *mctx = NULL;
	int al, verify;
	const unsigned char *hdata;
	size_t hdatalen;
	int type = 0;
	int ret;

	if ((ret = ssl3_get_message(s, SSL3_ST_SR_CERT_VRFY_A,
	    SSL3_ST_SR_CERT_VRFY_B, -1, SSL3_RT_MAX_PLAIN_LENGTH)) <= 0)
		return ret;

	ret = 0;

	if (s->init_num < 0)
		goto err;

	if ((mctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	CBS_init(&cbs, s->init_msg, s->init_num);

	peer_cert = s->session->peer_cert;
	pkey = X509_get0_pubkey(peer_cert);
	type = X509_certificate_type(peer_cert, pkey);

	if (s->s3->hs.tls12.message_type != SSL3_MT_CERTIFICATE_VERIFY) {
		s->s3->hs.tls12.reuse_message = 1;
		if (peer_cert != NULL) {
			al = SSL_AD_UNEXPECTED_MESSAGE;
			SSLerror(s, SSL_R_MISSING_VERIFY_MESSAGE);
			goto fatal_err;
		}
		ret = 1;
		goto end;
	}

	if (peer_cert == NULL) {
		SSLerror(s, SSL_R_NO_CLIENT_CERT_RECEIVED);
		al = SSL_AD_UNEXPECTED_MESSAGE;
		goto fatal_err;
	}

	if (!(type & EVP_PKT_SIGN)) {
		SSLerror(s, SSL_R_SIGNATURE_FOR_NON_SIGNING_CERTIFICATE);
		al = SSL_AD_ILLEGAL_PARAMETER;
		goto fatal_err;
	}

	if (s->s3->change_cipher_spec) {
		SSLerror(s, SSL_R_CCS_RECEIVED_EARLY);
		al = SSL_AD_UNEXPECTED_MESSAGE;
		goto fatal_err;
	}

	if (SSL_USE_SIGALGS(s)) {
		if (!CBS_get_u16(&cbs, &sigalg_value))
			goto decode_err;
	}
	if (!CBS_get_u16_length_prefixed(&cbs, &signature))
		goto err;
	if (CBS_len(&cbs) != 0) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_EXTRA_DATA_IN_MESSAGE);
		goto fatal_err;
	}

	if (CBS_len(&signature) > EVP_PKEY_size(pkey)) {
		SSLerror(s, SSL_R_WRONG_SIGNATURE_SIZE);
		al = SSL_AD_DECODE_ERROR;
		goto fatal_err;
	}

	if ((sigalg = ssl_sigalg_for_peer(s, pkey,
	    sigalg_value)) == NULL) {
		al = SSL_AD_DECODE_ERROR;
		goto fatal_err;
	}
	s->s3->hs.peer_sigalg = sigalg;

	if (SSL_USE_SIGALGS(s)) {
		EVP_PKEY_CTX *pctx;

		if (!tls1_transcript_data(s, &hdata, &hdatalen)) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			al = SSL_AD_INTERNAL_ERROR;
			goto fatal_err;
		}
		if (!EVP_DigestVerifyInit(mctx, &pctx, sigalg->md(),
		    NULL, pkey)) {
			SSLerror(s, ERR_R_EVP_LIB);
			al = SSL_AD_INTERNAL_ERROR;
			goto fatal_err;
		}
		if ((sigalg->flags & SIGALG_FLAG_RSA_PSS) &&
		    (!EVP_PKEY_CTX_set_rsa_padding(pctx,
			RSA_PKCS1_PSS_PADDING) ||
		    !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1))) {
			al = SSL_AD_INTERNAL_ERROR;
			goto fatal_err;
		}
		if (EVP_DigestVerify(mctx, CBS_data(&signature),
		    CBS_len(&signature), hdata, hdatalen) <= 0) {
			SSLerror(s, ERR_R_EVP_LIB);
			al = SSL_AD_INTERNAL_ERROR;
			goto fatal_err;
		}
	} else if (EVP_PKEY_id(pkey) == EVP_PKEY_RSA) {
		RSA *rsa;

		if ((rsa = EVP_PKEY_get0_RSA(pkey)) == NULL) {
			al = SSL_AD_INTERNAL_ERROR;
			SSLerror(s, ERR_R_EVP_LIB);
			goto fatal_err;
		}
		verify = RSA_verify(NID_md5_sha1, s->s3->hs.tls12.cert_verify,
		    MD5_DIGEST_LENGTH + SHA_DIGEST_LENGTH, CBS_data(&signature),
		    CBS_len(&signature), rsa);
		if (verify < 0) {
			al = SSL_AD_DECRYPT_ERROR;
			SSLerror(s, SSL_R_BAD_RSA_DECRYPT);
			goto fatal_err;
		}
		if (verify == 0) {
			al = SSL_AD_DECRYPT_ERROR;
			SSLerror(s, SSL_R_BAD_RSA_SIGNATURE);
			goto fatal_err;
		}
	} else if (EVP_PKEY_id(pkey) == EVP_PKEY_EC) {
		EC_KEY *eckey;

		if ((eckey = EVP_PKEY_get0_EC_KEY(pkey)) == NULL) {
			al = SSL_AD_INTERNAL_ERROR;
			SSLerror(s, ERR_R_EVP_LIB);
			goto fatal_err;
		}
		verify = ECDSA_verify(0,
		    &(s->s3->hs.tls12.cert_verify[MD5_DIGEST_LENGTH]),
		    SHA_DIGEST_LENGTH, CBS_data(&signature),
		    CBS_len(&signature), eckey);
		if (verify <= 0) {
			al = SSL_AD_DECRYPT_ERROR;
			SSLerror(s, SSL_R_BAD_ECDSA_SIGNATURE);
			goto fatal_err;
		}
	} else {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		al = SSL_AD_UNSUPPORTED_CERTIFICATE;
		goto fatal_err;
	}

	ret = 1;
	if (0) {
 decode_err:
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
 fatal_err:
		ssl3_send_alert(s, SSL3_AL_FATAL, al);
	}
 end:
	tls1_transcript_free(s);
 err:
	EVP_MD_CTX_free(mctx);

	return (ret);
}

static int
ssl3_get_client_certificate(SSL *s)
{
	CBS cbs, cert_list, cert_data;
	STACK_OF(X509) *certs = NULL;
	X509 *cert = NULL;
	const uint8_t *p;
	int al, ret;

	if ((ret = ssl3_get_message(s, SSL3_ST_SR_CERT_A, SSL3_ST_SR_CERT_B,
	    -1, s->max_cert_list)) <= 0)
		return ret;

	ret = -1;

	if (s->s3->hs.tls12.message_type == SSL3_MT_CLIENT_KEY_EXCHANGE) {
		if ((s->verify_mode & SSL_VERIFY_PEER) &&
		    (s->verify_mode & SSL_VERIFY_FAIL_IF_NO_PEER_CERT)) {
			SSLerror(s, SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE);
			al = SSL_AD_HANDSHAKE_FAILURE;
			goto fatal_err;
		}

		/*
		 * If we asked for a client certificate and the client has none,
		 * it must respond with a certificate list of length zero.
		 */
		if (s->s3->hs.tls12.cert_request != 0) {
			SSLerror(s, SSL_R_TLS_PEER_DID_NOT_RESPOND_WITH_CERTIFICATE_LIST);
			al = SSL_AD_UNEXPECTED_MESSAGE;
			goto fatal_err;
		}
		s->s3->hs.tls12.reuse_message = 1;
		return (1);
	}

	if (s->s3->hs.tls12.message_type != SSL3_MT_CERTIFICATE) {
		al = SSL_AD_UNEXPECTED_MESSAGE;
		SSLerror(s, SSL_R_WRONG_MESSAGE_TYPE);
		goto fatal_err;
	}

	if (s->init_num < 0)
		goto decode_err;

	CBS_init(&cbs, s->init_msg, s->init_num);

	if (!CBS_get_u24_length_prefixed(&cbs, &cert_list))
		goto decode_err;
	if (CBS_len(&cbs) != 0)
		goto decode_err;

	/*
	 * A TLS client must send an empty certificate list, if no suitable
	 * certificate is available (rather than omitting the Certificate
	 * handshake message) - see RFC 5246 section 7.4.6.
	 */
	if (CBS_len(&cert_list) == 0) {
		if ((s->verify_mode & SSL_VERIFY_PEER) &&
		    (s->verify_mode & SSL_VERIFY_FAIL_IF_NO_PEER_CERT)) {
			SSLerror(s, SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE);
			al = SSL_AD_HANDSHAKE_FAILURE;
			goto fatal_err;
		}
		/* No client certificate so free transcript. */
		tls1_transcript_free(s);
		goto done;
	}

	if ((certs = sk_X509_new_null()) == NULL) {
		SSLerror(s, ERR_R_MALLOC_FAILURE);
		goto err;
	}

	while (CBS_len(&cert_list) > 0) {
		if (!CBS_get_u24_length_prefixed(&cert_list, &cert_data))
			goto decode_err;
		p = CBS_data(&cert_data);
		if ((cert = d2i_X509(NULL, &p, CBS_len(&cert_data))) == NULL) {
			SSLerror(s, ERR_R_ASN1_LIB);
			goto err;
		}
		if (p != CBS_data(&cert_data) + CBS_len(&cert_data))
			goto decode_err;
		if (!sk_X509_push(certs, cert)) {
			SSLerror(s, ERR_R_MALLOC_FAILURE);
			goto err;
		}
		cert = NULL;
	}

	if (ssl_verify_cert_chain(s, certs) <= 0) {
		al = ssl_verify_alarm_type(s->verify_result);
		SSLerror(s, SSL_R_NO_CERTIFICATE_RETURNED);
		goto fatal_err;
	}
	s->session->verify_result = s->verify_result;
	ERR_clear_error();

	if (!tls_process_peer_certs(s, certs))
		goto err;

 done:
	ret = 1;
	if (0) {
 decode_err:
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
 fatal_err:
		ssl3_send_alert(s, SSL3_AL_FATAL, al);
	}
 err:
	sk_X509_pop_free(certs, X509_free);
	X509_free(cert);

	return (ret);
}

static int
ssl3_send_server_certificate(SSL *s)
{
	CBB cbb, server_cert;
	SSL_CERT_PKEY *cpk;

	/*
	 * Server Certificate - RFC 5246, section 7.4.2.
	 */

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_SW_CERT_A) {
		if ((cpk = ssl_get_server_send_pkey(s)) == NULL) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			return (0);
		}

		if (!ssl3_handshake_msg_start(s, &cbb, &server_cert,
		    SSL3_MT_CERTIFICATE))
			goto err;
		if (!ssl3_output_cert_chain(s, &server_cert, cpk))
			goto err;
		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_SW_CERT_B;
	}

	/* SSL3_ST_SW_CERT_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (0);
}

/* send a new session ticket (not necessarily for a new session) */
static int
ssl3_send_newsession_ticket(SSL *s)
{
	CBB cbb, session_ticket, ticket;
	SSL_CTX *tctx = s->initial_ctx;
	size_t enc_session_len, enc_session_max_len, hmac_len;
	size_t session_len = 0;
	unsigned char *enc_session = NULL, *session = NULL;
	unsigned char iv[EVP_MAX_IV_LENGTH];
	unsigned char key_name[16];
	unsigned char *hmac;
	unsigned int hlen;
	EVP_CIPHER_CTX *ctx = NULL;
	HMAC_CTX *hctx = NULL;
	int iv_len, len;

	/*
	 * New Session Ticket - RFC 5077, section 3.3.
	 */

	memset(&cbb, 0, sizeof(cbb));

	if ((ctx = EVP_CIPHER_CTX_new()) == NULL)
		goto err;
	if ((hctx = HMAC_CTX_new()) == NULL)
		goto err;

	if (s->s3->hs.state == SSL3_ST_SW_SESSION_TICKET_A) {
		if (!ssl3_handshake_msg_start(s, &cbb, &session_ticket,
		    SSL3_MT_NEWSESSION_TICKET))
			goto err;

		if (!SSL_SESSION_ticket(s->session, &session, &session_len))
			goto err;
		if (session_len > 0xffff)
			goto err;

		/*
		 * Initialize HMAC and cipher contexts. If callback is present
		 * it does all the work, otherwise use generated values from
		 * parent context.
		 */
		if (tctx->tlsext_ticket_key_cb != NULL) {
			if (tctx->tlsext_ticket_key_cb(s,
			    key_name, iv, ctx, hctx, 1) < 0)
				goto err;
		} else {
			arc4random_buf(iv, 16);
			EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL,
			    tctx->tlsext_tick_aes_key, iv);
			HMAC_Init_ex(hctx, tctx->tlsext_tick_hmac_key,
			    16, EVP_sha256(), NULL);
			memcpy(key_name, tctx->tlsext_tick_key_name, 16);
		}

		/* Encrypt the session state. */
		enc_session_max_len = session_len + EVP_MAX_BLOCK_LENGTH;
		if ((enc_session = calloc(1, enc_session_max_len)) == NULL)
			goto err;
		enc_session_len = 0;
		if (!EVP_EncryptUpdate(ctx, enc_session, &len, session,
		    session_len))
			goto err;
		enc_session_len += len;
		if (!EVP_EncryptFinal_ex(ctx, enc_session + enc_session_len,
		    &len))
			goto err;
		enc_session_len += len;

		if (enc_session_len > enc_session_max_len)
			goto err;

		/* Generate the HMAC. */
		if (!HMAC_Update(hctx, key_name, sizeof(key_name)))
			goto err;
		if (!HMAC_Update(hctx, iv, EVP_CIPHER_CTX_iv_length(ctx)))
			goto err;
		if (!HMAC_Update(hctx, enc_session, enc_session_len))
			goto err;

		if ((hmac_len = HMAC_size(hctx)) <= 0)
			goto err;

		/*
		 * Ticket lifetime hint (advisory only):
		 * We leave this unspecified for resumed session
		 * (for simplicity), and guess that tickets for new
		 * sessions will live as long as their sessions.
		 */
		if (!CBB_add_u32(&session_ticket,
		    s->hit ? 0 : s->session->timeout))
			goto err;

		if (!CBB_add_u16_length_prefixed(&session_ticket, &ticket))
			goto err;
		if (!CBB_add_bytes(&ticket, key_name, sizeof(key_name)))
			goto err;
		if ((iv_len = EVP_CIPHER_CTX_iv_length(ctx)) < 0)
			goto err;
		if (!CBB_add_bytes(&ticket, iv, iv_len))
			goto err;
		if (!CBB_add_bytes(&ticket, enc_session, enc_session_len))
			goto err;
		if (!CBB_add_space(&ticket, &hmac, hmac_len))
			goto err;

		if (!HMAC_Final(hctx, hmac, &hlen))
			goto err;
		if (hlen != hmac_len)
			goto err;

		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_SW_SESSION_TICKET_B;
	}

	EVP_CIPHER_CTX_free(ctx);
	HMAC_CTX_free(hctx);
	freezero(session, session_len);
	free(enc_session);

	/* SSL3_ST_SW_SESSION_TICKET_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);
	EVP_CIPHER_CTX_free(ctx);
	HMAC_CTX_free(hctx);
	freezero(session, session_len);
	free(enc_session);

	return (-1);
}

static int
ssl3_send_cert_status(SSL *s)
{
	CBB cbb, certstatus, ocspresp;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_SW_CERT_STATUS_A) {
		if (!ssl3_handshake_msg_start(s, &cbb, &certstatus,
		    SSL3_MT_CERTIFICATE_STATUS))
			goto err;
		if (!CBB_add_u8(&certstatus, s->tlsext_status_type))
			goto err;
		if (!CBB_add_u24_length_prefixed(&certstatus, &ocspresp))
			goto err;
		if (!CBB_add_bytes(&ocspresp, s->tlsext_ocsp_resp,
		    s->tlsext_ocsp_resp_len))
			goto err;
		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_SW_CERT_STATUS_B;
	}

	/* SSL3_ST_SW_CERT_STATUS_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_send_server_change_cipher_spec(SSL *s)
{
	size_t outlen;
	CBB cbb;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_SW_CHANGE_A) {
		if (!CBB_init_fixed(&cbb, s->init_buf->data,
		    s->init_buf->length))
			goto err;
		if (!CBB_add_u8(&cbb, SSL3_MT_CCS))
			goto err;
		if (!CBB_finish(&cbb, NULL, &outlen))
			goto err;

		if (outlen > INT_MAX)
			goto err;

		s->init_num = (int)outlen;
		s->init_off = 0;

		if (SSL_is_dtls(s)) {
			s->d1->handshake_write_seq =
			    s->d1->next_handshake_write_seq;
			dtls1_set_message_header_int(s, SSL3_MT_CCS, 0,
			    s->d1->handshake_write_seq, 0, 0);
			dtls1_buffer_message(s, 1);
		}

		s->s3->hs.state = SSL3_ST_SW_CHANGE_B;
	}

	/* SSL3_ST_SW_CHANGE_B */
	return ssl3_record_write(s, SSL3_RT_CHANGE_CIPHER_SPEC);

 err:
	CBB_cleanup(&cbb);

	return -1;
}

static int
ssl3_get_client_finished(SSL *s)
{
	int al, md_len, ret;
	CBS cbs;

	/* should actually be 36+4 :-) */
	if ((ret = ssl3_get_message(s, SSL3_ST_SR_FINISHED_A,
	    SSL3_ST_SR_FINISHED_B, SSL3_MT_FINISHED, 64)) <= 0)
		return ret;

	/* If this occurs, we have missed a message */
	if (!s->s3->change_cipher_spec) {
		al = SSL_AD_UNEXPECTED_MESSAGE;
		SSLerror(s, SSL_R_GOT_A_FIN_BEFORE_A_CCS);
		goto fatal_err;
	}
	s->s3->change_cipher_spec = 0;

	md_len = TLS1_FINISH_MAC_LENGTH;

	if (s->init_num < 0) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_BAD_DIGEST_LENGTH);
		goto fatal_err;
	}

	CBS_init(&cbs, s->init_msg, s->init_num);

	if (s->s3->hs.peer_finished_len != md_len ||
	    CBS_len(&cbs) != md_len) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_BAD_DIGEST_LENGTH);
		goto fatal_err;
	}

	if (!CBS_mem_equal(&cbs, s->s3->hs.peer_finished, CBS_len(&cbs))) {
		al = SSL_AD_DECRYPT_ERROR;
		SSLerror(s, SSL_R_DIGEST_CHECK_FAILED);
		goto fatal_err;
	}

	/* Copy finished so we can use it for renegotiation checks. */
	OPENSSL_assert(md_len <= EVP_MAX_MD_SIZE);
	memcpy(s->s3->previous_client_finished,
	    s->s3->hs.peer_finished, md_len);
	s->s3->previous_client_finished_len = md_len;

	return (1);
 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
	return (0);
}

static int
ssl3_send_server_finished(SSL *s)
{
	CBB cbb, finished;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_SW_FINISHED_A) {
		if (!tls12_derive_finished(s))
			goto err;

		/* Copy finished so we can use it for renegotiation checks. */
		memcpy(s->s3->previous_server_finished,
		    s->s3->hs.finished, s->s3->hs.finished_len);
		s->s3->previous_server_finished_len = s->s3->hs.finished_len;

		if (!ssl3_handshake_msg_start(s, &cbb, &finished,
		    SSL3_MT_FINISHED))
                        goto err;
		if (!CBB_add_bytes(&finished, s->s3->hs.finished,
		    s->s3->hs.finished_len))
			goto err;
		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_SW_FINISHED_B;
	}

	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}
