/* $OpenBSD: ssl_clnt.c,v 1.169 2025/03/09 15:53:36 tb Exp $ */
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
#include <stdint.h>
#include <stdio.h>

#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/curve25519.h>
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/objects.h>
#include <openssl/opensslconf.h>

#include "bytestring.h"
#include "dtls_local.h"
#include "ssl_local.h"
#include "ssl_sigalgs.h"
#include "ssl_tlsext.h"

static int ca_dn_cmp(const X509_NAME * const *a, const X509_NAME * const *b);

static int ssl3_send_client_hello(SSL *s);
static int ssl3_get_dtls_hello_verify(SSL *s);
static int ssl3_get_server_hello(SSL *s);
static int ssl3_get_certificate_request(SSL *s);
static int ssl3_get_new_session_ticket(SSL *s);
static int ssl3_get_cert_status(SSL *s);
static int ssl3_get_server_done(SSL *s);
static int ssl3_send_client_verify(SSL *s);
static int ssl3_send_client_certificate(SSL *s);
static int ssl_do_client_cert_cb(SSL *s, X509 **px509, EVP_PKEY **ppkey);
static int ssl3_send_client_key_exchange(SSL *s);
static int ssl3_get_server_key_exchange(SSL *s);
static int ssl3_get_server_certificate(SSL *s);
static int ssl3_check_cert_and_algorithm(SSL *s);
static int ssl3_check_finished(SSL *s);
static int ssl3_send_client_change_cipher_spec(SSL *s);
static int ssl3_send_client_finished(SSL *s);
static int ssl3_get_server_finished(SSL *s);

int
ssl3_connect(SSL *s)
{
	int new_state, state, skip = 0;
	int ret = -1;

	ERR_clear_error();
	errno = 0;

	s->in_handshake++;
	if (!SSL_in_init(s) || SSL_in_before(s))
		SSL_clear(s);

	for (;;) {
		state = s->s3->hs.state;

		switch (s->s3->hs.state) {
		case SSL_ST_RENEGOTIATE:
			s->renegotiate = 1;
			s->s3->hs.state = SSL_ST_CONNECT;
			s->ctx->stats.sess_connect_renegotiate++;
			/* break */
		case SSL_ST_BEFORE:
		case SSL_ST_CONNECT:
		case SSL_ST_BEFORE|SSL_ST_CONNECT:
		case SSL_ST_OK|SSL_ST_CONNECT:

			s->server = 0;

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
			if (!ssl_init_wbio_buffer(s, 0)) {
				ret = -1;
				goto end;
			}

			/* don't push the buffering BIO quite yet */

			if (!tls1_transcript_init(s)) {
				ret = -1;
				goto end;
			}

			s->s3->hs.state = SSL3_ST_CW_CLNT_HELLO_A;
			s->ctx->stats.sess_connect++;
			s->init_num = 0;

			if (SSL_is_dtls(s)) {
				/* mark client_random uninitialized */
				memset(s->s3->client_random, 0,
				    sizeof(s->s3->client_random));
				s->d1->send_cookie = 0;
				s->hit = 0;
			}
			break;

		case SSL3_ST_CW_CLNT_HELLO_A:
		case SSL3_ST_CW_CLNT_HELLO_B:
			s->shutdown = 0;

			if (SSL_is_dtls(s)) {
				/* every DTLS ClientHello resets Finished MAC */
				tls1_transcript_reset(s);

				dtls1_start_timer(s);
			}

			ret = ssl3_send_client_hello(s);
			if (ret <= 0)
				goto end;

			if (SSL_is_dtls(s) && s->d1->send_cookie) {
				s->s3->hs.state = SSL3_ST_CW_FLUSH;
				s->s3->hs.tls12.next_state = SSL3_ST_CR_SRVR_HELLO_A;
			} else
				s->s3->hs.state = SSL3_ST_CR_SRVR_HELLO_A;

			s->init_num = 0;

			/* turn on buffering for the next lot of output */
			if (s->bbio != s->wbio)
				s->wbio = BIO_push(s->bbio, s->wbio);

			break;

		case SSL3_ST_CR_SRVR_HELLO_A:
		case SSL3_ST_CR_SRVR_HELLO_B:
			ret = ssl3_get_server_hello(s);
			if (ret <= 0)
				goto end;

			if (s->hit) {
				s->s3->hs.state = SSL3_ST_CR_FINISHED_A;
				if (!SSL_is_dtls(s)) {
					if (s->tlsext_ticket_expected) {
						/* receive renewed session ticket */
						s->s3->hs.state = SSL3_ST_CR_SESSION_TICKET_A;
					}

					/* No client certificate verification. */
					tls1_transcript_free(s);
				}
			} else if (SSL_is_dtls(s)) {
				s->s3->hs.state = DTLS1_ST_CR_HELLO_VERIFY_REQUEST_A;
			} else {
				s->s3->hs.state = SSL3_ST_CR_CERT_A;
			}
			s->init_num = 0;
			break;

		case DTLS1_ST_CR_HELLO_VERIFY_REQUEST_A:
		case DTLS1_ST_CR_HELLO_VERIFY_REQUEST_B:
			ret = ssl3_get_dtls_hello_verify(s);
			if (ret <= 0)
				goto end;
			dtls1_stop_timer(s);
			if (s->d1->send_cookie) /* start again, with a cookie */
				s->s3->hs.state = SSL3_ST_CW_CLNT_HELLO_A;
			else
				s->s3->hs.state = SSL3_ST_CR_CERT_A;
			s->init_num = 0;
			break;

		case SSL3_ST_CR_CERT_A:
		case SSL3_ST_CR_CERT_B:
			ret = ssl3_check_finished(s);
			if (ret <= 0)
				goto end;
			if (ret == 2) {
				s->hit = 1;
				if (s->tlsext_ticket_expected)
					s->s3->hs.state = SSL3_ST_CR_SESSION_TICKET_A;
				else
					s->s3->hs.state = SSL3_ST_CR_FINISHED_A;
				s->init_num = 0;
				break;
			}
			/* Check if it is anon DH/ECDH. */
			if (!(s->s3->hs.cipher->algorithm_auth &
			    SSL_aNULL)) {
				ret = ssl3_get_server_certificate(s);
				if (ret <= 0)
					goto end;
				if (s->tlsext_status_expected)
					s->s3->hs.state = SSL3_ST_CR_CERT_STATUS_A;
				else
					s->s3->hs.state = SSL3_ST_CR_KEY_EXCH_A;
			} else {
				skip = 1;
				s->s3->hs.state = SSL3_ST_CR_KEY_EXCH_A;
			}
			s->init_num = 0;
			break;

		case SSL3_ST_CR_KEY_EXCH_A:
		case SSL3_ST_CR_KEY_EXCH_B:
			ret = ssl3_get_server_key_exchange(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_CR_CERT_REQ_A;
			s->init_num = 0;

			/*
			 * At this point we check that we have the
			 * required stuff from the server.
			 */
			if (!ssl3_check_cert_and_algorithm(s)) {
				ret = -1;
				goto end;
			}
			break;

		case SSL3_ST_CR_CERT_REQ_A:
		case SSL3_ST_CR_CERT_REQ_B:
			ret = ssl3_get_certificate_request(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_CR_SRVR_DONE_A;
			s->init_num = 0;
			break;

		case SSL3_ST_CR_SRVR_DONE_A:
		case SSL3_ST_CR_SRVR_DONE_B:
			ret = ssl3_get_server_done(s);
			if (ret <= 0)
				goto end;
			if (SSL_is_dtls(s))
				dtls1_stop_timer(s);
			if (s->s3->hs.tls12.cert_request)
				s->s3->hs.state = SSL3_ST_CW_CERT_A;
			else
				s->s3->hs.state = SSL3_ST_CW_KEY_EXCH_A;
			s->init_num = 0;

			break;

		case SSL3_ST_CW_CERT_A:
		case SSL3_ST_CW_CERT_B:
		case SSL3_ST_CW_CERT_C:
		case SSL3_ST_CW_CERT_D:
			if (SSL_is_dtls(s))
				dtls1_start_timer(s);
			ret = ssl3_send_client_certificate(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_CW_KEY_EXCH_A;
			s->init_num = 0;
			break;

		case SSL3_ST_CW_KEY_EXCH_A:
		case SSL3_ST_CW_KEY_EXCH_B:
			if (SSL_is_dtls(s))
				dtls1_start_timer(s);
			ret = ssl3_send_client_key_exchange(s);
			if (ret <= 0)
				goto end;
			/*
			 * EAY EAY EAY need to check for DH fix cert
			 * sent back
			 */
			/*
			 * For TLS, cert_req is set to 2, so a cert chain
			 * of nothing is sent, but no verify packet is sent
			 */
			/*
			 * XXX: For now, we do not support client
			 * authentication in ECDH cipher suites with
			 * ECDH (rather than ECDSA) certificates.
			 * We need to skip the certificate verify
			 * message when client's ECDH public key is sent
			 * inside the client certificate.
			 */
			if (s->s3->hs.tls12.cert_request == 1) {
				s->s3->hs.state = SSL3_ST_CW_CERT_VRFY_A;
			} else {
				s->s3->hs.state = SSL3_ST_CW_CHANGE_A;
				s->s3->change_cipher_spec = 0;
			}

			s->init_num = 0;
			break;

		case SSL3_ST_CW_CERT_VRFY_A:
		case SSL3_ST_CW_CERT_VRFY_B:
			if (SSL_is_dtls(s))
				dtls1_start_timer(s);
			ret = ssl3_send_client_verify(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_CW_CHANGE_A;
			s->init_num = 0;
			s->s3->change_cipher_spec = 0;
			break;

		case SSL3_ST_CW_CHANGE_A:
		case SSL3_ST_CW_CHANGE_B:
			if (SSL_is_dtls(s) && !s->hit)
				dtls1_start_timer(s);
			ret = ssl3_send_client_change_cipher_spec(s);
			if (ret <= 0)
				goto end;

			s->s3->hs.state = SSL3_ST_CW_FINISHED_A;
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

		case SSL3_ST_CW_FINISHED_A:
		case SSL3_ST_CW_FINISHED_B:
			if (SSL_is_dtls(s) && !s->hit)
				dtls1_start_timer(s);
			ret = ssl3_send_client_finished(s);
			if (ret <= 0)
				goto end;
			if (!SSL_is_dtls(s))
				s->s3->flags |= SSL3_FLAGS_CCS_OK;
			s->s3->hs.state = SSL3_ST_CW_FLUSH;

			/* clear flags */
			if (s->hit) {
				s->s3->hs.tls12.next_state = SSL_ST_OK;
			} else {
				/* Allow NewSessionTicket if ticket expected */
				if (s->tlsext_ticket_expected)
					s->s3->hs.tls12.next_state =
					    SSL3_ST_CR_SESSION_TICKET_A;
				else
					s->s3->hs.tls12.next_state =
					    SSL3_ST_CR_FINISHED_A;
			}
			s->init_num = 0;
			break;

		case SSL3_ST_CR_SESSION_TICKET_A:
		case SSL3_ST_CR_SESSION_TICKET_B:
			ret = ssl3_get_new_session_ticket(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_CR_FINISHED_A;
			s->init_num = 0;
			break;

		case SSL3_ST_CR_CERT_STATUS_A:
		case SSL3_ST_CR_CERT_STATUS_B:
			ret = ssl3_get_cert_status(s);
			if (ret <= 0)
				goto end;
			s->s3->hs.state = SSL3_ST_CR_KEY_EXCH_A;
			s->init_num = 0;
			break;

		case SSL3_ST_CR_FINISHED_A:
		case SSL3_ST_CR_FINISHED_B:
			if (SSL_is_dtls(s))
				s->d1->change_cipher_spec_ok = 1;
			else
				s->s3->flags |= SSL3_FLAGS_CCS_OK;
			ret = ssl3_get_server_finished(s);
			if (ret <= 0)
				goto end;
			if (SSL_is_dtls(s))
				dtls1_stop_timer(s);

			if (s->hit)
				s->s3->hs.state = SSL3_ST_CW_CHANGE_A;
			else
				s->s3->hs.state = SSL_ST_OK;
			s->init_num = 0;
			break;

		case SSL3_ST_CW_FLUSH:
			s->rwstate = SSL_WRITING;
			if (BIO_flush(s->wbio) <= 0) {
				if (SSL_is_dtls(s)) {
					/* If the write error was fatal, stop trying */
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

			ssl_free_wbio_buffer(s);

			s->init_num = 0;
			s->renegotiate = 0;
			s->new_session = 0;

			ssl_update_cache(s, SSL_SESS_CACHE_CLIENT);
			if (s->hit)
				s->ctx->stats.sess_hit++;

			ret = 1;
			/* s->server=0; */
			s->handshake_func = ssl3_connect;
			s->ctx->stats.sess_connect_good++;

			ssl_info_callback(s, SSL_CB_HANDSHAKE_DONE, 1);

			if (SSL_is_dtls(s)) {
				/* done with handshaking */
				s->d1->handshake_read_seq = 0;
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

		/* did we do anything */
		if (!s->s3->hs.tls12.reuse_message && !skip) {
			if (s->s3->hs.state != state) {
				new_state = s->s3->hs.state;
				s->s3->hs.state = state;
				ssl_info_callback(s, SSL_CB_CONNECT_LOOP, 1);
				s->s3->hs.state = new_state;
			}
		}
		skip = 0;
	}

 end:
	s->in_handshake--;
	ssl_info_callback(s, SSL_CB_CONNECT_EXIT, ret);

	return (ret);
}

static int
ssl3_send_client_hello(SSL *s)
{
	CBB cbb, client_hello, session_id, cookie, cipher_suites;
	CBB compression_methods;
	uint16_t max_version;
	size_t sl;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_CW_CLNT_HELLO_A) {
		SSL_SESSION *sess = s->session;

		if (!ssl_max_supported_version(s, &max_version)) {
			SSLerror(s, SSL_R_NO_PROTOCOLS_AVAILABLE);
			return (-1);
		}
		s->version = max_version;

		if (sess == NULL || sess->ssl_version != s->version ||
		    (sess->session_id_length == 0 && sess->tlsext_tick == NULL) ||
		    sess->not_resumable) {
			if (!ssl_get_new_session(s, 0))
				goto err;
		}
		/* else use the pre-loaded session */

		/*
		 * If a DTLS ClientHello message is being resent after a
		 * HelloVerifyRequest, we must retain the original client
		 * random value.
		 */
		if (!SSL_is_dtls(s) || s->d1->send_cookie == 0)
			arc4random_buf(s->s3->client_random, SSL3_RANDOM_SIZE);

		if (!ssl3_handshake_msg_start(s, &cbb, &client_hello,
		    SSL3_MT_CLIENT_HELLO))
			goto err;

		if (!CBB_add_u16(&client_hello, s->version))
			goto err;

		/* Random stuff */
		if (!CBB_add_bytes(&client_hello, s->s3->client_random,
		    sizeof(s->s3->client_random)))
			goto err;

		/* Session ID */
		if (!CBB_add_u8_length_prefixed(&client_hello, &session_id))
			goto err;
		if (!s->new_session &&
		    s->session->session_id_length > 0) {
			sl = s->session->session_id_length;
			if (sl > sizeof(s->session->session_id)) {
				SSLerror(s, ERR_R_INTERNAL_ERROR);
				goto err;
			}
			if (!CBB_add_bytes(&session_id,
			    s->session->session_id, sl))
				goto err;
		}

		/* DTLS Cookie. */
		if (SSL_is_dtls(s)) {
			if (s->d1->cookie_len > sizeof(s->d1->cookie)) {
				SSLerror(s, ERR_R_INTERNAL_ERROR);
				goto err;
			}
			if (!CBB_add_u8_length_prefixed(&client_hello, &cookie))
				goto err;
			if (!CBB_add_bytes(&cookie, s->d1->cookie,
			    s->d1->cookie_len))
				goto err;
		}

		/* Ciphers supported */
		if (!CBB_add_u16_length_prefixed(&client_hello, &cipher_suites))
			return 0;
		if (!ssl_cipher_list_to_bytes(s, SSL_get_ciphers(s),
		    &cipher_suites)) {
			SSLerror(s, SSL_R_NO_CIPHERS_AVAILABLE);
			goto err;
		}

		/* Add in compression methods (null) */
		if (!CBB_add_u8_length_prefixed(&client_hello,
		    &compression_methods))
			goto err;
		if (!CBB_add_u8(&compression_methods, 0))
			goto err;

		/* TLS extensions */
		if (!tlsext_client_build(s, SSL_TLSEXT_MSG_CH, &client_hello)) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}

		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_CW_CLNT_HELLO_B;
	}

	/* SSL3_ST_CW_CLNT_HELLO_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_get_dtls_hello_verify(SSL *s)
{
	CBS hello_verify_request, cookie;
	size_t cookie_len;
	uint16_t ssl_version;
	int al, ret;

	if ((ret = ssl3_get_message(s, DTLS1_ST_CR_HELLO_VERIFY_REQUEST_A,
	    DTLS1_ST_CR_HELLO_VERIFY_REQUEST_B, -1, s->max_cert_list)) <= 0)
		return ret;

	if (s->s3->hs.tls12.message_type != DTLS1_MT_HELLO_VERIFY_REQUEST) {
		s->d1->send_cookie = 0;
		s->s3->hs.tls12.reuse_message = 1;
		return (1);
	}

	if (s->init_num < 0)
		goto decode_err;

	CBS_init(&hello_verify_request, s->init_msg,
	    s->init_num);

	if (!CBS_get_u16(&hello_verify_request, &ssl_version))
		goto decode_err;
	if (!CBS_get_u8_length_prefixed(&hello_verify_request, &cookie))
		goto decode_err;
	if (CBS_len(&hello_verify_request) != 0)
		goto decode_err;

	/*
	 * Per RFC 6347 section 4.2.1, the HelloVerifyRequest should always
	 * contain DTLSv1.0 the version that is going to be negotiated.
	 * Tolerate DTLSv1.2 just in case.
	 */
	if (ssl_version != DTLS1_VERSION && ssl_version != DTLS1_2_VERSION) {
		SSLerror(s, SSL_R_WRONG_SSL_VERSION);
		s->version = (s->version & 0xff00) | (ssl_version & 0xff);
		al = SSL_AD_PROTOCOL_VERSION;
		goto fatal_err;
	}

	if (!CBS_write_bytes(&cookie, s->d1->cookie,
	    sizeof(s->d1->cookie), &cookie_len)) {
		s->d1->cookie_len = 0;
		al = SSL_AD_ILLEGAL_PARAMETER;
		goto fatal_err;
	}
	s->d1->cookie_len = cookie_len;
	s->d1->send_cookie = 1;

	return 1;

 decode_err:
	al = SSL_AD_DECODE_ERROR;
 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
	return -1;
}

static int
ssl3_get_server_hello(SSL *s)
{
	CBS cbs, server_random, session_id;
	uint16_t server_version, cipher_suite;
	uint8_t compression_method;
	const SSL_CIPHER *cipher;
	const SSL_METHOD *method;
	int al, ret;

	s->first_packet = 1;
	if ((ret = ssl3_get_message(s, SSL3_ST_CR_SRVR_HELLO_A,
	    SSL3_ST_CR_SRVR_HELLO_B, -1, 20000 /* ?? */)) <= 0)
		return ret;
	s->first_packet = 0;

	if (s->init_num < 0)
		goto decode_err;

	CBS_init(&cbs, s->init_msg, s->init_num);

	if (SSL_is_dtls(s)) {
		if (s->s3->hs.tls12.message_type == DTLS1_MT_HELLO_VERIFY_REQUEST) {
			if (s->d1->send_cookie == 0) {
				s->s3->hs.tls12.reuse_message = 1;
				return (1);
			} else {
				/* Already sent a cookie. */
				al = SSL_AD_UNEXPECTED_MESSAGE;
				SSLerror(s, SSL_R_BAD_MESSAGE_TYPE);
				goto fatal_err;
			}
		}
	}

	if (s->s3->hs.tls12.message_type != SSL3_MT_SERVER_HELLO) {
		al = SSL_AD_UNEXPECTED_MESSAGE;
		SSLerror(s, SSL_R_BAD_MESSAGE_TYPE);
		goto fatal_err;
	}

	if (!CBS_get_u16(&cbs, &server_version))
		goto decode_err;

	if (!ssl_check_version_from_server(s, server_version)) {
		SSLerror(s, SSL_R_WRONG_SSL_VERSION);
		s->version = (s->version & 0xff00) | (server_version & 0xff);
		al = SSL_AD_PROTOCOL_VERSION;
		goto fatal_err;
	}
	s->s3->hs.peer_legacy_version = server_version;
	s->version = server_version;

	s->s3->hs.negotiated_tls_version = ssl_tls_version(server_version);
	if (s->s3->hs.negotiated_tls_version == 0) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		goto err;
	}

	if ((method = ssl_get_method(server_version)) == NULL) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		goto err;
	}
	s->method = method;

	/* Server random. */
	if (!CBS_get_bytes(&cbs, &server_random, SSL3_RANDOM_SIZE))
		goto decode_err;
	if (!CBS_write_bytes(&server_random, s->s3->server_random,
	    sizeof(s->s3->server_random), NULL))
		goto err;

	if (s->s3->hs.our_max_tls_version >= TLS1_2_VERSION &&
	    s->s3->hs.negotiated_tls_version < s->s3->hs.our_max_tls_version) {
		/*
		 * RFC 8446 section 4.1.3. We must not downgrade if the server
		 * random value contains the TLS 1.2 or TLS 1.1 magical value.
		 */
		if (!CBS_skip(&server_random,
		    CBS_len(&server_random) - sizeof(tls13_downgrade_12)))
			goto err;
		if (s->s3->hs.negotiated_tls_version == TLS1_2_VERSION &&
		    CBS_mem_equal(&server_random, tls13_downgrade_12,
		    sizeof(tls13_downgrade_12))) {
			al = SSL_AD_ILLEGAL_PARAMETER;
			SSLerror(s, SSL_R_INAPPROPRIATE_FALLBACK);
			goto fatal_err;
		}
		if (CBS_mem_equal(&server_random, tls13_downgrade_11,
		    sizeof(tls13_downgrade_11))) {
			al = SSL_AD_ILLEGAL_PARAMETER;
			SSLerror(s, SSL_R_INAPPROPRIATE_FALLBACK);
			goto fatal_err;
		}
	}

	/* Session ID. */
	if (!CBS_get_u8_length_prefixed(&cbs, &session_id))
		goto decode_err;

	if (CBS_len(&session_id) > SSL3_SESSION_ID_SIZE) {
		al = SSL_AD_ILLEGAL_PARAMETER;
		SSLerror(s, SSL_R_SSL3_SESSION_ID_TOO_LONG);
		goto fatal_err;
	}

	/* Cipher suite. */
	if (!CBS_get_u16(&cbs, &cipher_suite))
		goto decode_err;

	/*
	 * Check if we want to resume the session based on external
	 * pre-shared secret.
	 */
	if (s->tls_session_secret_cb != NULL) {
		const SSL_CIPHER *pref_cipher = NULL;
		int master_key_length = sizeof(s->session->master_key);

		if (!s->tls_session_secret_cb(s,
		    s->session->master_key, &master_key_length, NULL,
		    &pref_cipher, s->tls_session_secret_cb_arg)) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}
		if (master_key_length <= 0) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}
		s->session->master_key_length = master_key_length;

		/*
		 * XXX - this appears to be completely broken. The
		 * client cannot change the cipher at this stage,
		 * as the server has already made a selection.
		 */
		if ((s->s3->hs.cipher = pref_cipher) == NULL)
			s->s3->hs.cipher =
			    ssl3_get_cipher_by_value(cipher_suite);
		s->s3->flags |= SSL3_FLAGS_CCS_OK;
	}

	if (s->session->session_id_length != 0 &&
	    CBS_mem_equal(&session_id, s->session->session_id,
		s->session->session_id_length)) {
		if (s->sid_ctx_length != s->session->sid_ctx_length ||
		    timingsafe_memcmp(s->session->sid_ctx,
		    s->sid_ctx, s->sid_ctx_length) != 0) {
			/* actually a client application bug */
			al = SSL_AD_ILLEGAL_PARAMETER;
			SSLerror(s, SSL_R_ATTEMPT_TO_REUSE_SESSION_IN_DIFFERENT_CONTEXT);
			goto fatal_err;
		}
		s->s3->flags |= SSL3_FLAGS_CCS_OK;
		s->hit = 1;
	} else {
		/* a miss or crap from the other end */

		/* If we were trying for session-id reuse, make a new
		 * SSL_SESSION so we don't stuff up other people */
		s->hit = 0;
		if (s->session->session_id_length > 0) {
			if (!ssl_get_new_session(s, 0)) {
				al = SSL_AD_INTERNAL_ERROR;
				goto fatal_err;
			}
		}

		/*
		 * XXX - improve the handling for the case where there is a
		 * zero length session identifier.
		 */
		if (!CBS_write_bytes(&session_id, s->session->session_id,
		    sizeof(s->session->session_id),
		    &s->session->session_id_length))
			goto err;

		s->session->ssl_version = s->version;
	}

	if ((cipher = ssl3_get_cipher_by_value(cipher_suite)) == NULL) {
		al = SSL_AD_ILLEGAL_PARAMETER;
		SSLerror(s, SSL_R_UNKNOWN_CIPHER_RETURNED);
		goto fatal_err;
	}

	/* TLS v1.2 only ciphersuites require v1.2 or later. */
	if ((cipher->algorithm_ssl & SSL_TLSV1_2) &&
	    s->s3->hs.negotiated_tls_version < TLS1_2_VERSION) {
		al = SSL_AD_ILLEGAL_PARAMETER;
		SSLerror(s, SSL_R_WRONG_CIPHER_RETURNED);
		goto fatal_err;
	}

	if (!ssl_cipher_in_list(SSL_get_ciphers(s), cipher)) {
		/* we did not say we would use this cipher */
		al = SSL_AD_ILLEGAL_PARAMETER;
		SSLerror(s, SSL_R_WRONG_CIPHER_RETURNED);
		goto fatal_err;
	}

	/*
	 * Depending on the session caching (internal/external), the cipher
	 * and/or cipher_id values may not be set. Make sure that
	 * cipher_id is set and use it for comparison.
	 */
	if (s->hit && (s->session->cipher_value != cipher->value)) {
		al = SSL_AD_ILLEGAL_PARAMETER;
		SSLerror(s, SSL_R_OLD_SESSION_CIPHER_NOT_RETURNED);
		goto fatal_err;
	}
	s->s3->hs.cipher = cipher;
	s->session->cipher_value = cipher->value;

	if (!tls1_transcript_hash_init(s))
		goto err;

	/*
	 * Don't digest cached records if no sigalgs: we may need them for
	 * client authentication.
	 */
	if (!SSL_USE_SIGALGS(s))
		tls1_transcript_free(s);

	if (!CBS_get_u8(&cbs, &compression_method))
		goto decode_err;

	if (compression_method != 0) {
		al = SSL_AD_ILLEGAL_PARAMETER;
		SSLerror(s, SSL_R_UNSUPPORTED_COMPRESSION_ALGORITHM);
		goto fatal_err;
	}

	if (!tlsext_client_parse(s, SSL_TLSEXT_MSG_SH, &cbs, &al)) {
		SSLerror(s, SSL_R_PARSE_TLSEXT);
		goto fatal_err;
	}

	if (CBS_len(&cbs) != 0)
		goto decode_err;

	/*
	 * Determine if we need to see RI. Strictly speaking if we want to
	 * avoid an attack we should *always* see RI even on initial server
	 * hello because the client doesn't see any renegotiation during an
	 * attack. However this would mean we could not connect to any server
	 * which doesn't support RI so for the immediate future tolerate RI
	 * absence on initial connect only.
	 */
	if (!s->s3->renegotiate_seen &&
	    !(s->options & SSL_OP_LEGACY_SERVER_CONNECT)) {
		al = SSL_AD_HANDSHAKE_FAILURE;
		SSLerror(s, SSL_R_UNSAFE_LEGACY_RENEGOTIATION_DISABLED);
		goto fatal_err;
	}

	if (ssl_check_serverhello_tlsext(s) <= 0) {
		SSLerror(s, SSL_R_SERVERHELLO_TLSEXT);
		goto err;
	}

	return (1);

 decode_err:
	/* wrong packet length */
	al = SSL_AD_DECODE_ERROR;
	SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
	return (-1);
}

static int
ssl3_get_server_certificate(SSL *s)
{
	CBS cbs, cert_list, cert_data;
	STACK_OF(X509) *certs = NULL;
	X509 *cert = NULL;
	const uint8_t *p;
	int al, ret;

	if ((ret = ssl3_get_message(s, SSL3_ST_CR_CERT_A,
	    SSL3_ST_CR_CERT_B, -1, s->max_cert_list)) <= 0)
		return ret;

	ret = -1;

	if (s->s3->hs.tls12.message_type == SSL3_MT_SERVER_KEY_EXCHANGE) {
		s->s3->hs.tls12.reuse_message = 1;
		return (1);
	}

	if (s->s3->hs.tls12.message_type != SSL3_MT_CERTIFICATE) {
		al = SSL_AD_UNEXPECTED_MESSAGE;
		SSLerror(s, SSL_R_BAD_MESSAGE_TYPE);
		goto fatal_err;
	}

	if ((certs = sk_X509_new_null()) == NULL) {
		SSLerror(s, ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (s->init_num < 0)
		goto decode_err;

	CBS_init(&cbs, s->init_msg, s->init_num);

	if (!CBS_get_u24_length_prefixed(&cbs, &cert_list))
		goto decode_err;
	if (CBS_len(&cbs) != 0)
		goto decode_err;

	while (CBS_len(&cert_list) > 0) {
		if (!CBS_get_u24_length_prefixed(&cert_list, &cert_data))
			goto decode_err;
		p = CBS_data(&cert_data);
		if ((cert = d2i_X509(NULL, &p, CBS_len(&cert_data))) == NULL) {
			al = SSL_AD_BAD_CERTIFICATE;
			SSLerror(s, ERR_R_ASN1_LIB);
			goto fatal_err;
		}
		if (p != CBS_data(&cert_data) + CBS_len(&cert_data))
			goto decode_err;
		if (!sk_X509_push(certs, cert)) {
			SSLerror(s, ERR_R_MALLOC_FAILURE);
			goto err;
		}
		cert = NULL;
	}

	/* A server must always provide a non-empty certificate list. */
	if (sk_X509_num(certs) < 1) {
		SSLerror(s, SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE);
		goto decode_err;
	}

	if (ssl_verify_cert_chain(s, certs) <= 0 &&
	    s->verify_mode != SSL_VERIFY_NONE) {
		al = ssl_verify_alarm_type(s->verify_result);
		SSLerror(s, SSL_R_CERTIFICATE_VERIFY_FAILED);
		goto fatal_err;
	}
	s->session->verify_result = s->verify_result;
	ERR_clear_error();

	if (!tls_process_peer_certs(s, certs))
		goto err;

	ret = 1;

	if (0) {
 decode_err:
		/* wrong packet length */
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
ssl3_get_server_kex_dhe(SSL *s, CBS *cbs)
{
	int decode_error, invalid_params, invalid_key;
	int nid = NID_dhKeyAgreement;

	tls_key_share_free(s->s3->hs.key_share);
	if ((s->s3->hs.key_share = tls_key_share_new_nid(nid)) == NULL)
		goto err;

	if (!tls_key_share_peer_params(s->s3->hs.key_share, cbs,
	    &decode_error, &invalid_params)) {
		if (decode_error) {
			SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		}
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

	if (invalid_params) {
		SSLerror(s, SSL_R_BAD_DH_P_LENGTH);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
		goto err;
	}
	if (invalid_key) {
		SSLerror(s, SSL_R_BAD_DH_PUB_KEY_LENGTH);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
		goto err;
	}

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
ssl3_get_server_kex_ecdhe(SSL *s, CBS *cbs)
{
	uint8_t curve_type;
	uint16_t group_id;
	int decode_error;
	CBS public;

	if (!CBS_get_u8(cbs, &curve_type))
		goto decode_err;
	if (!CBS_get_u16(cbs, &group_id))
		goto decode_err;

	/* Only named curves are supported. */
	if (curve_type != NAMED_CURVE_TYPE) {
		SSLerror(s, SSL_R_UNSUPPORTED_ELLIPTIC_CURVE);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		goto err;
	}

	if (!CBS_get_u8_length_prefixed(cbs, &public))
		goto decode_err;

	/*
	 * Check that the group is one of our preferences - if it is not,
	 * the server has sent us an invalid group.
	 */
	if (!tls1_check_group(s, group_id)) {
		SSLerror(s, SSL_R_WRONG_CURVE);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
		goto err;
	}

	tls_key_share_free(s->s3->hs.key_share);
	if ((s->s3->hs.key_share = tls_key_share_new(group_id)) == NULL)
		goto err;

	if (!tls_key_share_peer_public(s->s3->hs.key_share, &public,
	    &decode_error, NULL)) {
		if (decode_error)
			goto decode_err;
		goto err;
	}

	return 1;

 decode_err:
	SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
	ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
 err:
	return 0;
}

static int
ssl3_get_server_key_exchange(SSL *s)
{
	CBB cbb;
	CBS cbs, params, signature;
	EVP_MD_CTX *md_ctx;
	unsigned char *signed_params = NULL;
	size_t signed_params_len;
	size_t params_len;
	long alg_k, alg_a;
	int al, ret;

	memset(&cbb, 0, sizeof(cbb));

	alg_k = s->s3->hs.cipher->algorithm_mkey;
	alg_a = s->s3->hs.cipher->algorithm_auth;

	/*
	 * Use same message size as in ssl3_get_certificate_request()
	 * as ServerKeyExchange message may be skipped.
	 */
	if ((ret = ssl3_get_message(s, SSL3_ST_CR_KEY_EXCH_A,
	    SSL3_ST_CR_KEY_EXCH_B, -1, s->max_cert_list)) <= 0)
		return ret;

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	if (s->init_num < 0)
		goto err;

	CBS_init(&cbs, s->init_msg, s->init_num);

	if (s->s3->hs.tls12.message_type != SSL3_MT_SERVER_KEY_EXCHANGE) {
		/*
		 * Do not skip server key exchange if this cipher suite uses
		 * ephemeral keys.
		 */
		if (alg_k & (SSL_kDHE|SSL_kECDHE)) {
			SSLerror(s, SSL_R_UNEXPECTED_MESSAGE);
			al = SSL_AD_UNEXPECTED_MESSAGE;
			goto fatal_err;
		}

		s->s3->hs.tls12.reuse_message = 1;
		EVP_MD_CTX_free(md_ctx);
		return (1);
	}

	if (!CBB_init(&cbb, 0))
		goto err;
	if (!CBB_add_bytes(&cbb, s->s3->client_random, SSL3_RANDOM_SIZE))
		goto err;
	if (!CBB_add_bytes(&cbb, s->s3->server_random, SSL3_RANDOM_SIZE))
		goto err;

	CBS_dup(&cbs, &params);

	if (alg_k & SSL_kDHE) {
		if (!ssl3_get_server_kex_dhe(s, &cbs))
			goto err;
	} else if (alg_k & SSL_kECDHE) {
		if (!ssl3_get_server_kex_ecdhe(s, &cbs))
			goto err;
	} else if (alg_k != 0) {
		al = SSL_AD_UNEXPECTED_MESSAGE;
		SSLerror(s, SSL_R_UNEXPECTED_MESSAGE);
		goto fatal_err;
	}

	if ((params_len = CBS_offset(&cbs)) > CBS_len(&params))
		goto err;
	if (!CBB_add_bytes(&cbb, CBS_data(&params), params_len))
		goto err;
	if (!CBB_finish(&cbb, &signed_params, &signed_params_len))
		goto err;

	/* if it was signed, check the signature */
	if ((alg_a & SSL_aNULL) == 0) {
		uint16_t sigalg_value = SIGALG_NONE;
		const struct ssl_sigalg *sigalg;
		EVP_PKEY_CTX *pctx;
		EVP_PKEY *pkey = NULL;

		if ((alg_a & SSL_aRSA) != 0 &&
		    s->session->peer_cert_type == SSL_PKEY_RSA) {
			pkey = X509_get0_pubkey(s->session->peer_cert);
		} else if ((alg_a & SSL_aECDSA) != 0 &&
		    s->session->peer_cert_type == SSL_PKEY_ECC) {
			pkey = X509_get0_pubkey(s->session->peer_cert);
		}
		if (pkey == NULL) {
			al = SSL_AD_ILLEGAL_PARAMETER;
			SSLerror(s, SSL_R_UNKNOWN_CERTIFICATE_TYPE);
			goto fatal_err;
		}

		if (SSL_USE_SIGALGS(s)) {
			if (!CBS_get_u16(&cbs, &sigalg_value))
				goto decode_err;
		}
		if (!CBS_get_u16_length_prefixed(&cbs, &signature))
			goto decode_err;
		if (CBS_len(&signature) > EVP_PKEY_size(pkey)) {
			al = SSL_AD_DECODE_ERROR;
			SSLerror(s, SSL_R_WRONG_SIGNATURE_LENGTH);
			goto fatal_err;
		}

		if ((sigalg = ssl_sigalg_for_peer(s, pkey,
		    sigalg_value)) == NULL) {
			al = SSL_AD_DECODE_ERROR;
			goto fatal_err;
		}
		s->s3->hs.peer_sigalg = sigalg;

		if (!EVP_DigestVerifyInit(md_ctx, &pctx, sigalg->md(),
		    NULL, pkey))
			goto err;
		if ((sigalg->flags & SIGALG_FLAG_RSA_PSS) &&
		    (!EVP_PKEY_CTX_set_rsa_padding(pctx,
		    RSA_PKCS1_PSS_PADDING) ||
		    !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1)))
			goto err;
		if (EVP_DigestVerify(md_ctx, CBS_data(&signature),
		    CBS_len(&signature), signed_params, signed_params_len) <= 0) {
			al = SSL_AD_DECRYPT_ERROR;
			SSLerror(s, SSL_R_BAD_SIGNATURE);
			goto fatal_err;
		}
	}

	if (CBS_len(&cbs) != 0) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_EXTRA_DATA_IN_MESSAGE);
		goto fatal_err;
	}

	EVP_MD_CTX_free(md_ctx);
	free(signed_params);

	return (1);

 decode_err:
	al = SSL_AD_DECODE_ERROR;
	SSLerror(s, SSL_R_BAD_PACKET_LENGTH);

 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);

 err:
	CBB_cleanup(&cbb);
	EVP_MD_CTX_free(md_ctx);
	free(signed_params);

	return (-1);
}

static int
ssl3_get_certificate_request(SSL *s)
{
	CBS cert_request, cert_types, rdn_list;
	X509_NAME *xn = NULL;
	const unsigned char *q;
	STACK_OF(X509_NAME) *ca_sk = NULL;
	int ret;

	if ((ret = ssl3_get_message(s, SSL3_ST_CR_CERT_REQ_A,
	    SSL3_ST_CR_CERT_REQ_B, -1, s->max_cert_list)) <= 0)
		return ret;

	ret = 0;

	s->s3->hs.tls12.cert_request = 0;

	if (s->s3->hs.tls12.message_type == SSL3_MT_SERVER_DONE) {
		s->s3->hs.tls12.reuse_message = 1;
		/*
		 * If we get here we don't need any cached handshake records
		 * as we wont be doing client auth.
		 */
		tls1_transcript_free(s);
		return (1);
	}

	if (s->s3->hs.tls12.message_type != SSL3_MT_CERTIFICATE_REQUEST) {
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_UNEXPECTED_MESSAGE);
		SSLerror(s, SSL_R_WRONG_MESSAGE_TYPE);
		goto err;
	}

	/* TLS does not like anon-DH with client cert */
	if (s->s3->hs.cipher->algorithm_auth & SSL_aNULL) {
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_UNEXPECTED_MESSAGE);
		SSLerror(s, SSL_R_TLS_CLIENT_CERT_REQ_WITH_ANON_CIPHER);
		goto err;
	}

	if (s->init_num < 0)
		goto decode_err;
	CBS_init(&cert_request, s->init_msg, s->init_num);

	if ((ca_sk = sk_X509_NAME_new(ca_dn_cmp)) == NULL) {
		SSLerror(s, ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!CBS_get_u8_length_prefixed(&cert_request, &cert_types))
		goto decode_err;

	if (SSL_USE_SIGALGS(s)) {
		CBS sigalgs;

		if (CBS_len(&cert_request) < 2) {
			SSLerror(s, SSL_R_DATA_LENGTH_TOO_LONG);
			goto err;
		}
		if (!CBS_get_u16_length_prefixed(&cert_request, &sigalgs)) {
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
			SSLerror(s, SSL_R_DATA_LENGTH_TOO_LONG);
			goto err;
		}
		if (CBS_len(&sigalgs) % 2 != 0 || CBS_len(&sigalgs) > 64) {
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
			SSLerror(s, SSL_R_SIGNATURE_ALGORITHMS_ERROR);
			goto err;
		}
		if (!CBS_stow(&sigalgs, &s->s3->hs.sigalgs,
		    &s->s3->hs.sigalgs_len))
			goto err;
	}

	/* get the CA RDNs */
	if (CBS_len(&cert_request) < 2) {
		SSLerror(s, SSL_R_DATA_LENGTH_TOO_LONG);
		goto err;
	}

	if (!CBS_get_u16_length_prefixed(&cert_request, &rdn_list) ||
	    CBS_len(&cert_request) != 0) {
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		SSLerror(s, SSL_R_LENGTH_MISMATCH);
		goto err;
	}

	while (CBS_len(&rdn_list) > 0) {
		CBS rdn;

		if (CBS_len(&rdn_list) < 2) {
			SSLerror(s, SSL_R_DATA_LENGTH_TOO_LONG);
			goto err;
		}

		if (!CBS_get_u16_length_prefixed(&rdn_list, &rdn)) {
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
			SSLerror(s, SSL_R_CA_DN_TOO_LONG);
			goto err;
		}

		q = CBS_data(&rdn);
		if ((xn = d2i_X509_NAME(NULL, &q, CBS_len(&rdn))) == NULL) {
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_DECODE_ERROR);
			SSLerror(s, ERR_R_ASN1_LIB);
			goto err;
		}

		if (q != CBS_data(&rdn) + CBS_len(&rdn)) {
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
			SSLerror(s, SSL_R_CA_DN_LENGTH_MISMATCH);
			goto err;
		}
		if (!sk_X509_NAME_push(ca_sk, xn)) {
			SSLerror(s, ERR_R_MALLOC_FAILURE);
			goto err;
		}
		xn = NULL;	/* avoid free in err block */
	}

	/* we should setup a certificate to return.... */
	s->s3->hs.tls12.cert_request = 1;
	sk_X509_NAME_pop_free(s->s3->hs.tls12.ca_names, X509_NAME_free);
	s->s3->hs.tls12.ca_names = ca_sk;
	ca_sk = NULL;

	ret = 1;
	if (0) {
 decode_err:
		SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
	}
 err:
	X509_NAME_free(xn);
	sk_X509_NAME_pop_free(ca_sk, X509_NAME_free);
	return (ret);
}

static int
ca_dn_cmp(const X509_NAME * const *a, const X509_NAME * const *b)
{
	return (X509_NAME_cmp(*a, *b));
}

static int
ssl3_get_new_session_ticket(SSL *s)
{
	uint32_t lifetime_hint;
	CBS cbs, session_ticket;
	unsigned int session_id_length = 0;
	int al, ret;

	if ((ret = ssl3_get_message(s, SSL3_ST_CR_SESSION_TICKET_A,
	    SSL3_ST_CR_SESSION_TICKET_B, -1, 16384)) <= 0)
		return ret;

	if (s->s3->hs.tls12.message_type == SSL3_MT_FINISHED) {
		s->s3->hs.tls12.reuse_message = 1;
		return (1);
	}
	if (s->s3->hs.tls12.message_type != SSL3_MT_NEWSESSION_TICKET) {
		al = SSL_AD_UNEXPECTED_MESSAGE;
		SSLerror(s, SSL_R_BAD_MESSAGE_TYPE);
		goto fatal_err;
	}

	if (s->init_num < 0) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_LENGTH_MISMATCH);
		goto fatal_err;
	}

	CBS_init(&cbs, s->init_msg, s->init_num);
	if (!CBS_get_u32(&cbs, &lifetime_hint) ||
	    !CBS_get_u16_length_prefixed(&cbs, &session_ticket) ||
	    CBS_len(&cbs) != 0) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_LENGTH_MISMATCH);
		goto fatal_err;
	}
	s->session->tlsext_tick_lifetime_hint = lifetime_hint;

	if (!CBS_stow(&session_ticket, &s->session->tlsext_tick,
	    &s->session->tlsext_ticklen)) {
		SSLerror(s, ERR_R_MALLOC_FAILURE);
		goto err;
	}

	/*
	 * There are two ways to detect a resumed ticket session.
	 * One is to set an appropriate session ID and then the server
	 * must return a match in ServerHello. This allows the normal
	 * client session ID matching to work and we know much
	 * earlier that the ticket has been accepted.
	 *
	 * The other way is to set zero length session ID when the
	 * ticket is presented and rely on the handshake to determine
	 * session resumption.
	 *
	 * We choose the former approach because this fits in with
	 * assumptions elsewhere in OpenSSL. The session ID is set
	 * to the SHA256 hash of the ticket.
	 */
	/* XXX - ensure this doesn't overflow session_id if hash is changed. */
	if (!EVP_Digest(CBS_data(&session_ticket), CBS_len(&session_ticket),
	    s->session->session_id, &session_id_length, EVP_sha256(), NULL)) {
		al = SSL_AD_INTERNAL_ERROR;
		SSLerror(s, ERR_R_EVP_LIB);
		goto fatal_err;
	}
	s->session->session_id_length = session_id_length;

	return (1);

 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
	return (-1);
}

static int
ssl3_get_cert_status(SSL *s)
{
	CBS cert_status, response;
	uint8_t	status_type;
	int al, ret;

	if ((ret = ssl3_get_message(s, SSL3_ST_CR_CERT_STATUS_A,
	    SSL3_ST_CR_CERT_STATUS_B, -1, 16384)) <= 0)
		return ret;

	if (s->s3->hs.tls12.message_type == SSL3_MT_SERVER_KEY_EXCHANGE) {
		/*
		 * Tell the callback the server did not send us an OSCP
		 * response, and has decided to head directly to key exchange.
		 */
		if (s->ctx->tlsext_status_cb) {
			free(s->tlsext_ocsp_resp);
			s->tlsext_ocsp_resp = NULL;
			s->tlsext_ocsp_resp_len = 0;

			ret = s->ctx->tlsext_status_cb(s,
			    s->ctx->tlsext_status_arg);
			if (ret == 0) {
				al = SSL_AD_BAD_CERTIFICATE_STATUS_RESPONSE;
				SSLerror(s, SSL_R_INVALID_STATUS_RESPONSE);
				goto fatal_err;
			}
			if (ret < 0) {
				al = SSL_AD_INTERNAL_ERROR;
				SSLerror(s, ERR_R_MALLOC_FAILURE);
				goto fatal_err;
			}
		}
		s->s3->hs.tls12.reuse_message = 1;
		return (1);
	}

	if (s->s3->hs.tls12.message_type != SSL3_MT_CERTIFICATE &&
	    s->s3->hs.tls12.message_type != SSL3_MT_CERTIFICATE_STATUS) {
		al = SSL_AD_UNEXPECTED_MESSAGE;
		SSLerror(s, SSL_R_BAD_MESSAGE_TYPE);
		goto fatal_err;
	}

	if (s->init_num < 0) {
		/* need at least status type + length */
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_LENGTH_MISMATCH);
		goto fatal_err;
	}

	CBS_init(&cert_status, s->init_msg, s->init_num);
	if (!CBS_get_u8(&cert_status, &status_type) ||
	    CBS_len(&cert_status) < 3) {
		/* need at least status type + length */
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_LENGTH_MISMATCH);
		goto fatal_err;
	}

	if (status_type != TLSEXT_STATUSTYPE_ocsp) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_UNSUPPORTED_STATUS_TYPE);
		goto fatal_err;
	}

	if (!CBS_get_u24_length_prefixed(&cert_status, &response) ||
	    CBS_len(&cert_status) != 0) {
		al = SSL_AD_DECODE_ERROR;
		SSLerror(s, SSL_R_LENGTH_MISMATCH);
		goto fatal_err;
	}

	if (!CBS_stow(&response, &s->tlsext_ocsp_resp,
	    &s->tlsext_ocsp_resp_len)) {
		al = SSL_AD_INTERNAL_ERROR;
		SSLerror(s, ERR_R_MALLOC_FAILURE);
		goto fatal_err;
	}

	if (s->ctx->tlsext_status_cb) {
		ret = s->ctx->tlsext_status_cb(s,
		    s->ctx->tlsext_status_arg);
		if (ret == 0) {
			al = SSL_AD_BAD_CERTIFICATE_STATUS_RESPONSE;
			SSLerror(s, SSL_R_INVALID_STATUS_RESPONSE);
			goto fatal_err;
		}
		if (ret < 0) {
			al = SSL_AD_INTERNAL_ERROR;
			SSLerror(s, ERR_R_MALLOC_FAILURE);
			goto fatal_err;
		}
	}
	return (1);
 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
	return (-1);
}

static int
ssl3_get_server_done(SSL *s)
{
	int ret;

	if ((ret = ssl3_get_message(s, SSL3_ST_CR_SRVR_DONE_A,
	    SSL3_ST_CR_SRVR_DONE_B, SSL3_MT_SERVER_DONE, 
	    30 /* should be very small, like 0 :-) */)) <= 0)
		return ret;

	if (s->init_num != 0) {
		/* should contain no data */
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		SSLerror(s, SSL_R_LENGTH_MISMATCH);
		return -1;
	}

	return 1;
}

static int
ssl3_send_client_kex_rsa(SSL *s, CBB *cbb)
{
	unsigned char pms[SSL_MAX_MASTER_KEY_LENGTH];
	unsigned char *enc_pms = NULL;
	uint16_t max_legacy_version;
	EVP_PKEY *pkey;
	RSA *rsa;
	int ret = 0;
	int enc_len;
	CBB epms;

	/*
	 * RSA-Encrypted Premaster Secret Message - RFC 5246 section 7.4.7.1.
	 */

	pkey = X509_get0_pubkey(s->session->peer_cert);
	if (pkey == NULL || (rsa = EVP_PKEY_get0_RSA(pkey)) == NULL) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		goto err;
	}

	/*
	 * Our maximum legacy protocol version - while RFC 5246 section 7.4.7.1
	 * says "The latest (newest) version supported by the client", if we're
	 * doing RSA key exchange then we have to presume that we're talking to
	 * a server that does not understand the supported versions extension
	 * and therefore our maximum version is that sent in the ClientHello.
	 */
	if (!ssl_max_legacy_version(s, &max_legacy_version))
		goto err;
	pms[0] = max_legacy_version >> 8;
	pms[1] = max_legacy_version & 0xff;
	arc4random_buf(&pms[2], sizeof(pms) - 2);

	if ((enc_pms = malloc(RSA_size(rsa))) == NULL) {
		SSLerror(s, ERR_R_MALLOC_FAILURE);
		goto err;
	}

	enc_len = RSA_public_encrypt(sizeof(pms), pms, enc_pms, rsa,
	    RSA_PKCS1_PADDING);
	if (enc_len <= 0) {
		SSLerror(s, SSL_R_BAD_RSA_ENCRYPT);
		goto err;
	}

	if (!CBB_add_u16_length_prefixed(cbb, &epms))
		goto err;
	if (!CBB_add_bytes(&epms, enc_pms, enc_len))
		goto err;
	if (!CBB_flush(cbb))
		goto err;

	if (!tls12_derive_master_secret(s, pms, sizeof(pms)))
		goto err;

	ret = 1;

 err:
	explicit_bzero(pms, sizeof(pms));
	free(enc_pms);

	return ret;
}

static int
ssl3_send_client_kex_dhe(SSL *s, CBB *cbb)
{
	uint8_t *key = NULL;
	size_t key_len = 0;
	int ret = 0;

	/* Ensure that we have an ephemeral key from the server for DHE. */
	if (s->s3->hs.key_share == NULL) {
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		SSLerror(s, SSL_R_UNABLE_TO_FIND_DH_PARAMETERS);
		goto err;
	}

	if (!tls_key_share_generate(s->s3->hs.key_share))
		goto err;
	if (!tls_key_share_public(s->s3->hs.key_share, cbb))
		goto err;
	if (!tls_key_share_derive(s->s3->hs.key_share, &key, &key_len))
		goto err;

	if (!tls_key_share_peer_security(s, s->s3->hs.key_share)) {
		SSLerror(s, SSL_R_DH_KEY_TOO_SMALL);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		return 0;
	}

	if (!tls12_derive_master_secret(s, key, key_len))
		goto err;

	ret = 1;

 err:
	freezero(key, key_len);

	return ret;
}

static int
ssl3_send_client_kex_ecdhe(SSL *s, CBB *cbb)
{
	uint8_t *key = NULL;
	size_t key_len = 0;
	CBB public;
	int ret = 0;

	/* Ensure that we have an ephemeral key for ECDHE. */
	if (s->s3->hs.key_share == NULL) {
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		goto err;
	}

	if (!tls_key_share_generate(s->s3->hs.key_share))
		goto err;

	if (!CBB_add_u8_length_prefixed(cbb, &public))
		return 0;
	if (!tls_key_share_public(s->s3->hs.key_share, &public))
		goto err;
	if (!CBB_flush(cbb))
		goto err;

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
ssl3_send_client_key_exchange(SSL *s)
{
	unsigned long alg_k;
	CBB cbb, kex;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_CW_KEY_EXCH_A) {
		alg_k = s->s3->hs.cipher->algorithm_mkey;

		if (!ssl3_handshake_msg_start(s, &cbb, &kex,
		    SSL3_MT_CLIENT_KEY_EXCHANGE))
			goto err;

		if (alg_k & SSL_kRSA) {
			if (!ssl3_send_client_kex_rsa(s, &kex))
				goto err;
		} else if (alg_k & SSL_kDHE) {
			if (!ssl3_send_client_kex_dhe(s, &kex))
				goto err;
		} else if (alg_k & SSL_kECDHE) {
			if (!ssl3_send_client_kex_ecdhe(s, &kex))
				goto err;
		} else {
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_HANDSHAKE_FAILURE);
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}

		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_CW_KEY_EXCH_B;
	}

	/* SSL3_ST_CW_KEY_EXCH_B */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_send_client_verify_sigalgs(SSL *s, EVP_PKEY *pkey,
    const struct ssl_sigalg *sigalg, CBB *cert_verify)
{
	CBB cbb_signature;
	EVP_PKEY_CTX *pctx = NULL;
	EVP_MD_CTX *mctx = NULL;
	const unsigned char *hdata;
	unsigned char *signature = NULL;
	size_t signature_len, hdata_len;
	int ret = 0;

	if ((mctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	if (!tls1_transcript_data(s, &hdata, &hdata_len)) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		goto err;
	}
	if (!EVP_DigestSignInit(mctx, &pctx, sigalg->md(), NULL, pkey)) {
		SSLerror(s, ERR_R_EVP_LIB);
		goto err;
	}
	if ((sigalg->flags & SIGALG_FLAG_RSA_PSS) &&
	    (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) ||
	    !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1))) {
		SSLerror(s, ERR_R_EVP_LIB);
		goto err;
	}
	if (!EVP_DigestSign(mctx, NULL, &signature_len, hdata, hdata_len)) {
		SSLerror(s, ERR_R_EVP_LIB);
		goto err;
	}
	if ((signature = calloc(1, signature_len)) == NULL) {
		SSLerror(s, ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (!EVP_DigestSign(mctx, signature, &signature_len, hdata, hdata_len)) {
		SSLerror(s, ERR_R_EVP_LIB);
		goto err;
	}

	if (!CBB_add_u16(cert_verify, sigalg->value))
		goto err;
	if (!CBB_add_u16_length_prefixed(cert_verify, &cbb_signature))
		goto err;
	if (!CBB_add_bytes(&cbb_signature, signature, signature_len))
		goto err;
	if (!CBB_flush(cert_verify))
		goto err;

	ret = 1;

 err:
	EVP_MD_CTX_free(mctx);
	free(signature);
	return ret;
}

static int
ssl3_send_client_verify_rsa(SSL *s, EVP_PKEY *pkey, CBB *cert_verify)
{
	CBB cbb_signature;
	RSA *rsa;
	unsigned char data[EVP_MAX_MD_SIZE];
	unsigned char *signature = NULL;
	unsigned int signature_len;
	size_t data_len;
	int ret = 0;

	if (!tls1_transcript_hash_value(s, data, sizeof(data), &data_len))
		goto err;
	if ((signature = calloc(1, EVP_PKEY_size(pkey))) == NULL)
		goto err;
	if ((rsa = EVP_PKEY_get0_RSA(pkey)) == NULL)
		goto err;
	if (RSA_sign(NID_md5_sha1, data, data_len, signature, &signature_len,
	    rsa) <= 0 ) {
		SSLerror(s, ERR_R_RSA_LIB);
		goto err;
	}

	if (!CBB_add_u16_length_prefixed(cert_verify, &cbb_signature))
		goto err;
	if (!CBB_add_bytes(&cbb_signature, signature, signature_len))
		goto err;
	if (!CBB_flush(cert_verify))
		goto err;

	ret = 1;
 err:
	free(signature);
	return ret;
}

static int
ssl3_send_client_verify_ec(SSL *s, EVP_PKEY *pkey, CBB *cert_verify)
{
	CBB cbb_signature;
	EC_KEY *eckey;
	unsigned char data[EVP_MAX_MD_SIZE];
	unsigned char *signature = NULL;
	unsigned int signature_len;
	int ret = 0;

	if (!tls1_transcript_hash_value(s, data, sizeof(data), NULL))
		goto err;
	if ((signature = calloc(1, EVP_PKEY_size(pkey))) == NULL)
		goto err;
	if ((eckey = EVP_PKEY_get0_EC_KEY(pkey)) == NULL)
		goto err;
	if (!ECDSA_sign(0, &data[MD5_DIGEST_LENGTH], SHA_DIGEST_LENGTH,
	    signature, &signature_len, eckey)) {
		SSLerror(s, ERR_R_ECDSA_LIB);
		goto err;
	}

	if (!CBB_add_u16_length_prefixed(cert_verify, &cbb_signature))
		goto err;
	if (!CBB_add_bytes(&cbb_signature, signature, signature_len))
		goto err;
	if (!CBB_flush(cert_verify))
		goto err;

	ret = 1;
 err:
	free(signature);
	return ret;
}

static int
ssl3_send_client_verify(SSL *s)
{
	const struct ssl_sigalg *sigalg;
	CBB cbb, cert_verify;
	EVP_PKEY *pkey;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_CW_CERT_VRFY_A) {
		if (!ssl3_handshake_msg_start(s, &cbb, &cert_verify,
		    SSL3_MT_CERTIFICATE_VERIFY))
			goto err;

		pkey = s->cert->key->privatekey;
		if ((sigalg = ssl_sigalg_select(s, pkey)) == NULL) {
			SSLerror(s, SSL_R_SIGNATURE_ALGORITHMS_ERROR);
			goto err;
		}
		s->s3->hs.our_sigalg = sigalg;

		/*
		 * For TLS v1.2 send signature algorithm and signature using
		 * agreed digest and cached handshake records.
		 */
		if (SSL_USE_SIGALGS(s)) {
			if (!ssl3_send_client_verify_sigalgs(s, pkey, sigalg,
			    &cert_verify))
				goto err;
		} else if (EVP_PKEY_id(pkey) == EVP_PKEY_RSA) {
			if (!ssl3_send_client_verify_rsa(s, pkey, &cert_verify))
				goto err;
		} else if (EVP_PKEY_id(pkey) == EVP_PKEY_EC) {
			if (!ssl3_send_client_verify_ec(s, pkey, &cert_verify))
				goto err;
		} else {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			goto err;
		}

		tls1_transcript_free(s);

		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_CW_CERT_VRFY_B;
	}

	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_send_client_certificate(SSL *s)
{
	EVP_PKEY *pkey = NULL;
	X509 *x509 = NULL;
	CBB cbb, client_cert;
	int i;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_CW_CERT_A) {
		if (s->cert->key->x509 == NULL ||
		    s->cert->key->privatekey == NULL)
			s->s3->hs.state = SSL3_ST_CW_CERT_B;
		else
			s->s3->hs.state = SSL3_ST_CW_CERT_C;
	}

	/* We need to get a client cert */
	if (s->s3->hs.state == SSL3_ST_CW_CERT_B) {
		/*
		 * If we get an error, we need to
		 * ssl->rwstate = SSL_X509_LOOKUP; return(-1);
		 * We then get retried later.
		 */
		i = ssl_do_client_cert_cb(s, &x509, &pkey);
		if (i < 0) {
			s->rwstate = SSL_X509_LOOKUP;
			return (-1);
		}
		s->rwstate = SSL_NOTHING;
		if ((i == 1) && (pkey != NULL) && (x509 != NULL)) {
			s->s3->hs.state = SSL3_ST_CW_CERT_B;
			if (!SSL_use_certificate(s, x509) ||
			    !SSL_use_PrivateKey(s, pkey))
				i = 0;
		} else if (i == 1) {
			i = 0;
			SSLerror(s, SSL_R_BAD_DATA_RETURNED_BY_CALLBACK);
		}

		X509_free(x509);
		EVP_PKEY_free(pkey);
		if (i == 0) {
			s->s3->hs.tls12.cert_request = 2;

			/* There is no client certificate to verify. */
			tls1_transcript_free(s);
		}

		/* Ok, we have a cert */
		s->s3->hs.state = SSL3_ST_CW_CERT_C;
	}

	if (s->s3->hs.state == SSL3_ST_CW_CERT_C) {
		if (!ssl3_handshake_msg_start(s, &cbb, &client_cert,
		    SSL3_MT_CERTIFICATE))
			goto err;
		if (!ssl3_output_cert_chain(s, &client_cert,
		    (s->s3->hs.tls12.cert_request == 2) ? NULL : s->cert->key))
			goto err;
		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_CW_CERT_D;
	}

	/* SSL3_ST_CW_CERT_D */
	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (0);
}

#define has_bits(i,m)	(((i)&(m)) == (m))

static int
ssl3_check_cert_and_algorithm(SSL *s)
{
	long alg_k, alg_a;
	int nid = NID_undef;
	int i;

	alg_k = s->s3->hs.cipher->algorithm_mkey;
	alg_a = s->s3->hs.cipher->algorithm_auth;

	/* We don't have a certificate. */
	if (alg_a & SSL_aNULL)
		return (1);

	if (s->s3->hs.key_share != NULL)
		nid = tls_key_share_nid(s->s3->hs.key_share);

	/* This is the passed certificate. */

	if (s->session->peer_cert_type == SSL_PKEY_ECC) {
		if (!ssl_check_srvr_ecc_cert_and_alg(s, s->session->peer_cert)) {
			SSLerror(s, SSL_R_BAD_ECC_CERT);
			goto fatal_err;
		}
		return (1);
	}

	i = X509_certificate_type(s->session->peer_cert, NULL);

	/* Check that we have a certificate if we require one. */
	if ((alg_a & SSL_aRSA) && !has_bits(i, EVP_PK_RSA|EVP_PKT_SIGN)) {
		SSLerror(s, SSL_R_MISSING_RSA_SIGNING_CERT);
		goto fatal_err;
	}
	if ((alg_k & SSL_kRSA) && !has_bits(i, EVP_PK_RSA|EVP_PKT_ENC)) {
		SSLerror(s, SSL_R_MISSING_RSA_ENCRYPTING_CERT);
		goto fatal_err;
	}
	if ((alg_k & SSL_kDHE) &&
	    !(has_bits(i, EVP_PK_DH|EVP_PKT_EXCH) || (nid == NID_dhKeyAgreement))) {
		SSLerror(s, SSL_R_MISSING_DH_KEY);
		goto fatal_err;
	}

	return (1);

 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);

	return (0);
}

/*
 * Check to see if handshake is full or resumed. Usually this is just a
 * case of checking to see if a cache hit has occurred. In the case of
 * session tickets we have to check the next message to be sure.
 */

static int
ssl3_check_finished(SSL *s)
{
	int ret;

	/* If we have no ticket it cannot be a resumed session. */
	if (!s->session->tlsext_tick)
		return (1);
	/* this function is called when we really expect a Certificate
	 * message, so permit appropriate message length */
	if ((ret = ssl3_get_message(s, SSL3_ST_CR_CERT_A,
	    SSL3_ST_CR_CERT_B, -1, s->max_cert_list)) <= 0)
		return ret;

	s->s3->hs.tls12.reuse_message = 1;
	if ((s->s3->hs.tls12.message_type == SSL3_MT_FINISHED) ||
	    (s->s3->hs.tls12.message_type == SSL3_MT_NEWSESSION_TICKET))
		return (2);

	return (1);
}

static int
ssl_do_client_cert_cb(SSL *s, X509 **px509, EVP_PKEY **ppkey)
{
	if (s->ctx->client_cert_cb == NULL)
		return 0;

	return s->ctx->client_cert_cb(s, px509, ppkey);
}

static int
ssl3_send_client_change_cipher_spec(SSL *s)
{
	size_t outlen;
	CBB cbb;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_CW_CHANGE_A) {
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

		s->s3->hs.state = SSL3_ST_CW_CHANGE_B;
	}

	/* SSL3_ST_CW_CHANGE_B */
	return ssl3_record_write(s, SSL3_RT_CHANGE_CIPHER_SPEC);

 err:
	CBB_cleanup(&cbb);

	return -1;
}

static int
ssl3_send_client_finished(SSL *s)
{
	CBB cbb, finished;

	memset(&cbb, 0, sizeof(cbb));

	if (s->s3->hs.state == SSL3_ST_CW_FINISHED_A) {
		if (!tls12_derive_finished(s))
			goto err;

		/* Copy finished so we can use it for renegotiation checks. */
		memcpy(s->s3->previous_client_finished,
		    s->s3->hs.finished, s->s3->hs.finished_len);
		s->s3->previous_client_finished_len =
		    s->s3->hs.finished_len;

		if (!ssl3_handshake_msg_start(s, &cbb, &finished,
		    SSL3_MT_FINISHED))
                        goto err;
		if (!CBB_add_bytes(&finished, s->s3->hs.finished,
		    s->s3->hs.finished_len))
			goto err;
		if (!ssl3_handshake_msg_finish(s, &cbb))
			goto err;

		s->s3->hs.state = SSL3_ST_CW_FINISHED_B;
	}

	return (ssl3_handshake_write(s));

 err:
	CBB_cleanup(&cbb);

	return (-1);
}

static int
ssl3_get_server_finished(SSL *s)
{
	int al, md_len, ret;
	CBS cbs;

	/* should actually be 36+4 :-) */
	if ((ret = ssl3_get_message(s, SSL3_ST_CR_FINISHED_A,
	    SSL3_ST_CR_FINISHED_B, SSL3_MT_FINISHED, 64)) <= 0)
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
	memcpy(s->s3->previous_server_finished,
	    s->s3->hs.peer_finished, md_len);
	s->s3->previous_server_finished_len = md_len;

	return (1);
 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
	return (0);
}
