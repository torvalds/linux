/* $OpenBSD: ssl_sess.c,v 1.129 2025/03/09 15:53:36 tb Exp $ */
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
 * Copyright (c) 1998-2006 The OpenSSL Project.  All rights reserved.
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

#include <openssl/lhash.h>
#include <openssl/opensslconf.h>

#include "ssl_local.h"

static void SSL_SESSION_list_remove(SSL_CTX *ctx, SSL_SESSION *s);
static void SSL_SESSION_list_add(SSL_CTX *ctx, SSL_SESSION *s);
static int remove_session_lock(SSL_CTX *ctx, SSL_SESSION *c, int lck);

/* aka SSL_get0_session; gets 0 objects, just returns a copy of the pointer */
SSL_SESSION *
SSL_get_session(const SSL *ssl)
{
	return (ssl->session);
}
LSSL_ALIAS(SSL_get_session);

/* variant of SSL_get_session: caller really gets something */
SSL_SESSION *
SSL_get1_session(SSL *ssl)
{
	SSL_SESSION *sess;

	/*
	 * Need to lock this all up rather than just use CRYPTO_add so that
	 * somebody doesn't free ssl->session between when we check it's
	 * non-null and when we up the reference count.
	 */
	CRYPTO_w_lock(CRYPTO_LOCK_SSL_SESSION);
	sess = ssl->session;
	if (sess)
		sess->references++;
	CRYPTO_w_unlock(CRYPTO_LOCK_SSL_SESSION);

	return (sess);
}
LSSL_ALIAS(SSL_get1_session);

int
SSL_SESSION_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
    CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	return CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_SSL_SESSION,
	    argl, argp, new_func, dup_func, free_func);
}
LSSL_ALIAS(SSL_SESSION_get_ex_new_index);

int
SSL_SESSION_set_ex_data(SSL_SESSION *s, int idx, void *arg)
{
	return (CRYPTO_set_ex_data(&s->ex_data, idx, arg));
}
LSSL_ALIAS(SSL_SESSION_set_ex_data);

void *
SSL_SESSION_get_ex_data(const SSL_SESSION *s, int idx)
{
	return (CRYPTO_get_ex_data(&s->ex_data, idx));
}
LSSL_ALIAS(SSL_SESSION_get_ex_data);

uint32_t
SSL_SESSION_get_max_early_data(const SSL_SESSION *s)
{
	return 0;
}
LSSL_ALIAS(SSL_SESSION_get_max_early_data);

int
SSL_SESSION_set_max_early_data(SSL_SESSION *s, uint32_t max_early_data)
{
	return 1;
}
LSSL_ALIAS(SSL_SESSION_set_max_early_data);

SSL_SESSION *
SSL_SESSION_new(void)
{
	SSL_SESSION *ss;

	if (!OPENSSL_init_ssl(0, NULL)) {
		SSLerrorx(SSL_R_LIBRARY_BUG);
		return(NULL);
	}

	if ((ss = calloc(1, sizeof(*ss))) == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}

	ss->verify_result = 1; /* avoid 0 (= X509_V_OK) just in case */
	ss->references = 1;
	ss->timeout = 60 * 5 + 4; /* 5 minutes 4 seconds timeout by default */
	ss->time = time(NULL);
	ss->prev = NULL;
	ss->next = NULL;
	ss->tlsext_hostname = NULL;

	ss->peer_cert_type = -1;

	ss->tlsext_ecpointformatlist_length = 0;
	ss->tlsext_ecpointformatlist = NULL;
	ss->tlsext_supportedgroups_length = 0;
	ss->tlsext_supportedgroups = NULL;

	CRYPTO_new_ex_data(CRYPTO_EX_INDEX_SSL_SESSION, ss, &ss->ex_data);

	return (ss);
}
LSSL_ALIAS(SSL_SESSION_new);

SSL_SESSION *
ssl_session_dup(SSL_SESSION *sess, int include_ticket)
{
	SSL_SESSION *copy;
	CBS cbs;

	if ((copy = calloc(1, sizeof(*copy))) == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	copy->ssl_version = sess->ssl_version;

	CBS_init(&cbs, sess->master_key, sess->master_key_length);
	if (!CBS_write_bytes(&cbs, copy->master_key, sizeof(copy->master_key),
	    &copy->master_key_length))
		goto err;

	CBS_init(&cbs, sess->session_id, sess->session_id_length);
	if (!CBS_write_bytes(&cbs, copy->session_id, sizeof(copy->session_id),
	    &copy->session_id_length))
		goto err;

	CBS_init(&cbs, sess->sid_ctx, sess->sid_ctx_length);
	if (!CBS_write_bytes(&cbs, copy->sid_ctx, sizeof(copy->sid_ctx),
	    &copy->sid_ctx_length))
		goto err;

	if (sess->peer_cert != NULL) {
		if (!X509_up_ref(sess->peer_cert))
			goto err;
		copy->peer_cert = sess->peer_cert;
	}
	copy->peer_cert_type = sess->peer_cert_type;

	copy->verify_result = sess->verify_result;

	copy->timeout = sess->timeout;
	copy->time = sess->time;
	copy->references = 1;

	copy->cipher_value = sess->cipher_value;

	if (sess->tlsext_hostname != NULL) {
		copy->tlsext_hostname = strdup(sess->tlsext_hostname);
		if (copy->tlsext_hostname == NULL)
			goto err;
	}

	if (include_ticket) {
		CBS_init(&cbs, sess->tlsext_tick, sess->tlsext_ticklen);
		if (!CBS_stow(&cbs, &copy->tlsext_tick, &copy->tlsext_ticklen))
			goto err;
		copy->tlsext_tick_lifetime_hint =
		    sess->tlsext_tick_lifetime_hint;

		/*
		 * XXX - copy sess->resumption_master_secret and all other
		 * TLSv1.3 info here.
		 */
	}

	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_SSL_SESSION, copy,
	    &copy->ex_data))
		goto err;

	if (!CRYPTO_dup_ex_data(CRYPTO_EX_INDEX_SSL_SESSION, &copy->ex_data,
	    &sess->ex_data))
		goto err;

	/* Omit prev/next: the new session gets its own slot in the cache. */

	copy->not_resumable = sess->not_resumable;

	CBS_init(&cbs, sess->tlsext_ecpointformatlist,
	    sess->tlsext_ecpointformatlist_length);
	if (!CBS_stow(&cbs, &copy->tlsext_ecpointformatlist,
	    &copy->tlsext_ecpointformatlist_length))
		goto err;

	if (sess->tlsext_supportedgroups != NULL) {
		if ((copy->tlsext_supportedgroups = calloc(sizeof(uint16_t),
		    sess->tlsext_supportedgroups_length)) == NULL)
			goto err;
		memcpy(copy->tlsext_supportedgroups,
		    sess->tlsext_supportedgroups,
		    sizeof(uint16_t) * sess->tlsext_supportedgroups_length);
		copy->tlsext_supportedgroups_length =
		    sess->tlsext_supportedgroups_length;
	}

	return copy;

 err:
	SSL_SESSION_free(copy);

	return NULL;
}

const unsigned char *
SSL_SESSION_get_id(const SSL_SESSION *ss, unsigned int *len)
{
	if (len != NULL)
		*len = (unsigned int)ss->session_id_length;
	return ss->session_id;
}
LSSL_ALIAS(SSL_SESSION_get_id);

const unsigned char *
SSL_SESSION_get0_id_context(const SSL_SESSION *ss, unsigned int *len)
{
	if (len != NULL)
		*len = (unsigned int)ss->sid_ctx_length;
	return ss->sid_ctx;
}
LSSL_ALIAS(SSL_SESSION_get0_id_context);

unsigned int
SSL_SESSION_get_compress_id(const SSL_SESSION *ss)
{
	return 0;
}
LSSL_ALIAS(SSL_SESSION_get_compress_id);

unsigned long
SSL_SESSION_get_ticket_lifetime_hint(const SSL_SESSION *s)
{
	return s->tlsext_tick_lifetime_hint;
}
LSSL_ALIAS(SSL_SESSION_get_ticket_lifetime_hint);

int
SSL_SESSION_has_ticket(const SSL_SESSION *s)
{
	return (s->tlsext_ticklen > 0) ? 1 : 0;
}
LSSL_ALIAS(SSL_SESSION_has_ticket);

/*
 * SSLv3/TLSv1 has 32 bytes (256 bits) of session ID space. As such, filling
 * the ID with random gunk repeatedly until we have no conflict is going to
 * complete in one iteration pretty much "most" of the time (btw:
 * understatement). So, if it takes us 10 iterations and we still can't avoid
 * a conflict - well that's a reasonable point to call it quits. Either the
 * arc4random code is broken or someone is trying to open roughly very close to
 * 2^128 (or 2^256) SSL sessions to our server. How you might store that many
 * sessions is perhaps a more interesting question...
 */

#define MAX_SESS_ID_ATTEMPTS 10

static int
def_generate_session_id(const SSL *ssl, unsigned char *id, unsigned int *id_len)
{
	unsigned int retry = 0;

	do {
		arc4random_buf(id, *id_len);
	} while (SSL_has_matching_session_id(ssl, id, *id_len) &&
	    (++retry < MAX_SESS_ID_ATTEMPTS));

	if (retry < MAX_SESS_ID_ATTEMPTS)
		return 1;

	/* else - woops a session_id match */
	/* XXX We should also check the external cache --
	 * but the probability of a collision is negligible, and
	 * we could not prevent the concurrent creation of sessions
	 * with identical IDs since we currently don't have means
	 * to atomically check whether a session ID already exists
	 * and make a reservation for it if it does not
	 * (this problem applies to the internal cache as well).
	 */
	return 0;
}

int
ssl_get_new_session(SSL *s, int session)
{
	unsigned int tmp;
	SSL_SESSION *ss = NULL;
	GEN_SESSION_CB cb = def_generate_session_id;

	/* This gets used by clients and servers. */

	if ((ss = SSL_SESSION_new()) == NULL)
		return (0);

	/* If the context has a default timeout, use it */
	if (s->session_ctx->session_timeout == 0)
		ss->timeout = SSL_get_default_timeout(s);
	else
		ss->timeout = s->session_ctx->session_timeout;

	if (s->session != NULL) {
		SSL_SESSION_free(s->session);
		s->session = NULL;
	}

	if (session) {
		switch (s->version) {
		case TLS1_VERSION:
		case TLS1_1_VERSION:
		case TLS1_2_VERSION:
		case DTLS1_VERSION:
		case DTLS1_2_VERSION:
			ss->ssl_version = s->version;
			ss->session_id_length = SSL3_SSL_SESSION_ID_LENGTH;
			break;
		default:
			SSLerror(s, SSL_R_UNSUPPORTED_SSL_VERSION);
			SSL_SESSION_free(ss);
			return (0);
		}

		/* If RFC4507 ticket use empty session ID. */
		if (s->tlsext_ticket_expected) {
			ss->session_id_length = 0;
			goto sess_id_done;
		}

		/* Choose which callback will set the session ID. */
		CRYPTO_r_lock(CRYPTO_LOCK_SSL_CTX);
		if (s->generate_session_id)
			cb = s->generate_session_id;
		else if (s->session_ctx->generate_session_id)
			cb = s->session_ctx->generate_session_id;
		CRYPTO_r_unlock(CRYPTO_LOCK_SSL_CTX);

		/* Choose a session ID. */
		tmp = ss->session_id_length;
		if (!cb(s, ss->session_id, &tmp)) {
			/* The callback failed */
			SSLerror(s, SSL_R_SSL_SESSION_ID_CALLBACK_FAILED);
			SSL_SESSION_free(ss);
			return (0);
		}

		/*
		 * Don't allow the callback to set the session length to zero.
		 * nor set it higher than it was.
		 */
		if (tmp == 0 || tmp > ss->session_id_length) {
			/* The callback set an illegal length */
			SSLerror(s, SSL_R_SSL_SESSION_ID_HAS_BAD_LENGTH);
			SSL_SESSION_free(ss);
			return (0);
		}
		ss->session_id_length = tmp;

		/* Finally, check for a conflict. */
		if (SSL_has_matching_session_id(s, ss->session_id,
		    ss->session_id_length)) {
			SSLerror(s, SSL_R_SSL_SESSION_ID_CONFLICT);
			SSL_SESSION_free(ss);
			return (0);
		}

 sess_id_done:
		if (s->tlsext_hostname) {
			ss->tlsext_hostname = strdup(s->tlsext_hostname);
			if (ss->tlsext_hostname == NULL) {
				SSLerror(s, ERR_R_INTERNAL_ERROR);
				SSL_SESSION_free(ss);
				return 0;
			}
		}
	} else {
		ss->session_id_length = 0;
	}

	if (s->sid_ctx_length > sizeof ss->sid_ctx) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		SSL_SESSION_free(ss);
		return 0;
	}

	memcpy(ss->sid_ctx, s->sid_ctx, s->sid_ctx_length);
	ss->sid_ctx_length = s->sid_ctx_length;
	s->session = ss;
	ss->ssl_version = s->version;
	ss->verify_result = X509_V_OK;

	return (1);
}

static SSL_SESSION *
ssl_session_from_cache(SSL *s, CBS *session_id)
{
	SSL_SESSION *sess;
	SSL_SESSION data;

	if ((s->session_ctx->session_cache_mode &
	     SSL_SESS_CACHE_NO_INTERNAL_LOOKUP))
		return NULL;

	memset(&data, 0, sizeof(data));

	data.ssl_version = s->version;

	if (!CBS_write_bytes(session_id, data.session_id,
	    sizeof(data.session_id), &data.session_id_length))
		return NULL;

	CRYPTO_r_lock(CRYPTO_LOCK_SSL_CTX);
	sess = lh_SSL_SESSION_retrieve(s->session_ctx->sessions, &data);
	if (sess != NULL)
		CRYPTO_add(&sess->references, 1, CRYPTO_LOCK_SSL_SESSION);
	CRYPTO_r_unlock(CRYPTO_LOCK_SSL_CTX);

	if (sess == NULL)
		s->session_ctx->stats.sess_miss++;

	return sess;
}

static SSL_SESSION *
ssl_session_from_callback(SSL *s, CBS *session_id)
{
	SSL_SESSION *sess;
	int copy;

	if (s->session_ctx->get_session_cb == NULL)
		return NULL;

	copy = 1;
	if ((sess = s->session_ctx->get_session_cb(s,
	    CBS_data(session_id), CBS_len(session_id), &copy)) == NULL)
		return NULL;
	/*
	 * The copy handler may have set copy == 0 to indicate that the session
	 * structures are shared between threads and that it handles the
	 * reference count itself. If it didn't set copy to zero, we must
	 * increment the reference count.
	 */
	if (copy)
		CRYPTO_add(&sess->references, 1, CRYPTO_LOCK_SSL_SESSION);

	s->session_ctx->stats.sess_cb_hit++;

	/* Add the externally cached session to the internal cache as well. */
	if (!(s->session_ctx->session_cache_mode &
	    SSL_SESS_CACHE_NO_INTERNAL_STORE)) {
		/*
		 * The following should not return 1,
		 * otherwise, things are very strange.
		 */
		SSL_CTX_add_session(s->session_ctx, sess);
	}

	return sess;
}

static SSL_SESSION *
ssl_session_by_id(SSL *s, CBS *session_id)
{
	SSL_SESSION *sess;

	if (CBS_len(session_id) == 0)
		return NULL;

	if ((sess = ssl_session_from_cache(s, session_id)) == NULL)
		sess = ssl_session_from_callback(s, session_id);

	return sess;
}

/*
 * ssl_get_prev_session attempts to find an SSL_SESSION to be used to resume
 * this connection. It is only called by servers.
 *
 *   session_id: points at the session ID in the ClientHello. This code will
 *       read past the end of this in order to parse out the session ticket
 *       extension, if any.
 *   ext_block: a CBS for the ClientHello extensions block.
 *   alert: alert that the caller should send in case of failure.
 *
 * Returns:
 *   -1: error
 *    0: a session may have been found.
 *
 * Side effects:
 *   - If a session is found then s->session is pointed at it (after freeing
 *     an existing session if need be) and s->verify_result is set from the
 *     session.
 *   - For both new and resumed sessions, s->tlsext_ticket_expected
 *     indicates whether the server should issue a new session ticket or not.
 */
int
ssl_get_prev_session(SSL *s, CBS *session_id, CBS *ext_block, int *alert)
{
	SSL_SESSION *sess = NULL;
	int alert_desc = SSL_AD_INTERNAL_ERROR, fatal = 0;
	int ticket_decrypted = 0;

	/* This is used only by servers. */

	if (CBS_len(session_id) > SSL_MAX_SSL_SESSION_ID_LENGTH)
		goto err;

	/* Sets s->tlsext_ticket_expected. */
	switch (tls1_process_ticket(s, ext_block, &alert_desc, &sess)) {
	case TLS1_TICKET_FATAL_ERROR:
		fatal = 1;
		goto err;
	case TLS1_TICKET_NONE:
	case TLS1_TICKET_EMPTY:
		if ((sess = ssl_session_by_id(s, session_id)) == NULL)
			goto err;
		break;
	case TLS1_TICKET_NOT_DECRYPTED:
		goto err;
	case TLS1_TICKET_DECRYPTED:
		ticket_decrypted = 1;

		/*
		 * The session ID is used by some clients to detect that the
		 * ticket has been accepted so we copy it into sess.
		 */
		if (!CBS_write_bytes(session_id, sess->session_id,
		    sizeof(sess->session_id), &sess->session_id_length)) {
			fatal = 1;
			goto err;
		}
		break;
	default:
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		fatal = 1;
		goto err;
	}

	/* Now sess is non-NULL and we own one of its reference counts. */

	if (sess->sid_ctx_length != s->sid_ctx_length ||
	    timingsafe_memcmp(sess->sid_ctx, s->sid_ctx,
	    sess->sid_ctx_length) != 0) {
		/*
		 * We have the session requested by the client, but we don't
		 * want to use it in this context. Treat it like a cache miss.
		 */
		goto err;
	}

	if ((s->verify_mode & SSL_VERIFY_PEER) && s->sid_ctx_length == 0) {
		/*
		 * We can't be sure if this session is being used out of
		 * context, which is especially important for SSL_VERIFY_PEER.
		 * The application should have used
		 * SSL[_CTX]_set_session_id_context.
		 *
		 * For this error case, we generate an error instead of treating
		 * the event like a cache miss (otherwise it would be easy for
		 * applications to effectively disable the session cache by
		 * accident without anyone noticing).
		 */
		SSLerror(s, SSL_R_SESSION_ID_CONTEXT_UNINITIALIZED);
		fatal = 1;
		goto err;
	}

	if (sess->timeout < (time(NULL) - sess->time)) {
		s->session_ctx->stats.sess_timeout++;
		if (!ticket_decrypted) {
			/* The session was from the cache, so remove it. */
			SSL_CTX_remove_session(s->session_ctx, sess);
		}
		goto err;
	}

	s->session_ctx->stats.sess_hit++;

	SSL_SESSION_free(s->session);
	s->session = sess;
	s->verify_result = s->session->verify_result;

	return 1;

 err:
	SSL_SESSION_free(sess);
	if (ticket_decrypted) {
		/*
		 * The session was from a ticket. Issue a ticket for the new
		 * session.
		 */
		s->tlsext_ticket_expected = 1;
	}
	if (fatal) {
		*alert = alert_desc;
		return -1;
	}
	return 0;
}

int
SSL_CTX_add_session(SSL_CTX *ctx, SSL_SESSION *c)
{
	int ret = 0;
	SSL_SESSION *s;

	/*
	 * Add just 1 reference count for the SSL_CTX's session cache
	 * even though it has two ways of access: each session is in a
	 * doubly linked list and an lhash.
	 */
	CRYPTO_add(&c->references, 1, CRYPTO_LOCK_SSL_SESSION);

	/*
	 * If session c is in already in cache, we take back the increment
	 * later.
	 */
	CRYPTO_w_lock(CRYPTO_LOCK_SSL_CTX);
	s = lh_SSL_SESSION_insert(ctx->sessions, c);

	/*
	 * s != NULL iff we already had a session with the given PID.
	 * In this case, s == c should hold (then we did not really modify
	 * ctx->sessions), or we're in trouble.
	 */
	if (s != NULL && s != c) {
		/* We *are* in trouble ... */
		SSL_SESSION_list_remove(ctx, s);
		SSL_SESSION_free(s);
		/*
		 * ... so pretend the other session did not exist in cache
		 * (we cannot handle two SSL_SESSION structures with identical
		 * session ID in the same cache, which could happen e.g. when
		 * two threads concurrently obtain the same session from an
		 * external cache).
		 */
		s = NULL;
	}

	/* Put at the head of the queue unless it is already in the cache */
	if (s == NULL)
		SSL_SESSION_list_add(ctx, c);

	if (s != NULL) {
		/*
		 * existing cache entry -- decrement previously incremented
		 * reference count because it already takes into account the
		 * cache.
		 */
		SSL_SESSION_free(s); /* s == c */
		ret = 0;
	} else {
		/*
		 * New cache entry -- remove old ones if cache has become
		 * too large.
		 */

		ret = 1;

		if (SSL_CTX_sess_get_cache_size(ctx) > 0) {
			while (SSL_CTX_sess_number(ctx) >
			    SSL_CTX_sess_get_cache_size(ctx)) {
				if (!remove_session_lock(ctx,
				    ctx->session_cache_tail, 0))
					break;
				else
					ctx->stats.sess_cache_full++;
			}
		}
	}
	CRYPTO_w_unlock(CRYPTO_LOCK_SSL_CTX);
	return (ret);
}
LSSL_ALIAS(SSL_CTX_add_session);

int
SSL_CTX_remove_session(SSL_CTX *ctx, SSL_SESSION *c)
{
	return remove_session_lock(ctx, c, 1);
}
LSSL_ALIAS(SSL_CTX_remove_session);

static int
remove_session_lock(SSL_CTX *ctx, SSL_SESSION *c, int lck)
{
	SSL_SESSION *r;
	int ret = 0;

	if (c == NULL || c->session_id_length == 0)
		return 0;

	if (lck)
		CRYPTO_w_lock(CRYPTO_LOCK_SSL_CTX);
	if ((r = lh_SSL_SESSION_retrieve(ctx->sessions, c)) == c) {
		ret = 1;
		r = lh_SSL_SESSION_delete(ctx->sessions, c);
		SSL_SESSION_list_remove(ctx, c);
	}
	if (lck)
		CRYPTO_w_unlock(CRYPTO_LOCK_SSL_CTX);

	if (ret) {
		r->not_resumable = 1;
		if (ctx->remove_session_cb != NULL)
			ctx->remove_session_cb(ctx, r);
		SSL_SESSION_free(r);
	}

	return ret;
}

void
SSL_SESSION_free(SSL_SESSION *ss)
{
	int i;

	if (ss == NULL)
		return;

	i = CRYPTO_add(&ss->references, -1, CRYPTO_LOCK_SSL_SESSION);
	if (i > 0)
		return;

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_SSL_SESSION, ss, &ss->ex_data);

	explicit_bzero(ss->master_key, sizeof ss->master_key);
	explicit_bzero(ss->session_id, sizeof ss->session_id);

	X509_free(ss->peer_cert);

	free(ss->tlsext_hostname);
	free(ss->tlsext_tick);
	free(ss->tlsext_ecpointformatlist);
	free(ss->tlsext_supportedgroups);

	tls13_secret_cleanup(&ss->resumption_master_secret);

	freezero(ss, sizeof(*ss));
}
LSSL_ALIAS(SSL_SESSION_free);

int
SSL_SESSION_up_ref(SSL_SESSION *ss)
{
	return CRYPTO_add(&ss->references, 1, CRYPTO_LOCK_SSL_SESSION) > 1;
}
LSSL_ALIAS(SSL_SESSION_up_ref);

int
SSL_set_session(SSL *s, SSL_SESSION *session)
{
	const SSL_METHOD *method;

	if (session == NULL) {
		SSL_SESSION_free(s->session);
		s->session = NULL;

		return SSL_set_ssl_method(s, s->ctx->method);
	}

	if ((method = ssl_get_method(session->ssl_version)) == NULL) {
		SSLerror(s, SSL_R_UNABLE_TO_FIND_SSL_METHOD);
		return (0);
	}

	if (!SSL_set_ssl_method(s, method))
		return (0);

	CRYPTO_add(&session->references, 1, CRYPTO_LOCK_SSL_SESSION);
	SSL_SESSION_free(s->session);
	s->session = session;
	s->verify_result = s->session->verify_result;

	return (1);
}
LSSL_ALIAS(SSL_set_session);

size_t
SSL_SESSION_get_master_key(const SSL_SESSION *ss, unsigned char *out,
    size_t max_out)
{
	size_t len = ss->master_key_length;

	if (out == NULL)
		return len;

	if (len > max_out)
		len = max_out;

	memcpy(out, ss->master_key, len);

	return len;
}
LSSL_ALIAS(SSL_SESSION_get_master_key);

long
SSL_SESSION_set_timeout(SSL_SESSION *s, long t)
{
	if (s == NULL)
		return (0);
	s->timeout = t;
	return (1);
}
LSSL_ALIAS(SSL_SESSION_set_timeout);

long
SSL_SESSION_get_timeout(const SSL_SESSION *s)
{
	if (s == NULL)
		return (0);
	return (s->timeout);
}
LSSL_ALIAS(SSL_SESSION_get_timeout);

/* XXX 2038 */
long
SSL_SESSION_get_time(const SSL_SESSION *s)
{
	if (s == NULL)
		return (0);
	return (s->time);
}
LSSL_ALIAS(SSL_SESSION_get_time);

/* XXX 2038 */
long
SSL_SESSION_set_time(SSL_SESSION *s, long t)
{
	if (s == NULL)
		return (0);
	s->time = t;
	return (t);
}
LSSL_ALIAS(SSL_SESSION_set_time);

int
SSL_SESSION_get_protocol_version(const SSL_SESSION *s)
{
	return s->ssl_version;
}
LSSL_ALIAS(SSL_SESSION_get_protocol_version);

const SSL_CIPHER *
SSL_SESSION_get0_cipher(const SSL_SESSION *s)
{
	return ssl3_get_cipher_by_value(s->cipher_value);
}
LSSL_ALIAS(SSL_SESSION_get0_cipher);

X509 *
SSL_SESSION_get0_peer(SSL_SESSION *s)
{
	return s->peer_cert;
}
LSSL_ALIAS(SSL_SESSION_get0_peer);

int
SSL_SESSION_set1_id(SSL_SESSION *s, const unsigned char *sid,
    unsigned int sid_len)
{
	if (sid_len > SSL_MAX_SSL_SESSION_ID_LENGTH) {
		SSLerrorx(SSL_R_SSL_SESSION_ID_TOO_LONG);
		return 0;
	}
	s->session_id_length = sid_len;
	memmove(s->session_id, sid, sid_len);
	return 1;
}
LSSL_ALIAS(SSL_SESSION_set1_id);

int
SSL_SESSION_set1_id_context(SSL_SESSION *s, const unsigned char *sid_ctx,
    unsigned int sid_ctx_len)
{
	if (sid_ctx_len > SSL_MAX_SID_CTX_LENGTH) {
		SSLerrorx(SSL_R_SSL_SESSION_ID_CONTEXT_TOO_LONG);
		return 0;
	}
	s->sid_ctx_length = sid_ctx_len;
	memcpy(s->sid_ctx, sid_ctx, sid_ctx_len);

	return 1;
}
LSSL_ALIAS(SSL_SESSION_set1_id_context);

int
SSL_SESSION_is_resumable(const SSL_SESSION *s)
{
	return 0;
}
LSSL_ALIAS(SSL_SESSION_is_resumable);

long
SSL_CTX_set_timeout(SSL_CTX *s, long t)
{
	long l;

	if (s == NULL)
		return (0);
	l = s->session_timeout;
	s->session_timeout = t;

	return (l);
}
LSSL_ALIAS(SSL_CTX_set_timeout);

long
SSL_CTX_get_timeout(const SSL_CTX *s)
{
	if (s == NULL)
		return (0);
	return (s->session_timeout);
}
LSSL_ALIAS(SSL_CTX_get_timeout);

int
SSL_set_session_secret_cb(SSL *s, int (*tls_session_secret_cb)(SSL *s,
    void *secret, int *secret_len, STACK_OF(SSL_CIPHER) *peer_ciphers,
    const SSL_CIPHER **cipher, void *arg), void *arg)
{
	if (s == NULL)
		return (0);
	s->tls_session_secret_cb = tls_session_secret_cb;
	s->tls_session_secret_cb_arg = arg;
	return (1);
}
LSSL_ALIAS(SSL_set_session_secret_cb);

int
SSL_set_session_ticket_ext_cb(SSL *s, tls_session_ticket_ext_cb_fn cb,
    void *arg)
{
	if (s == NULL)
		return (0);
	s->tls_session_ticket_ext_cb = cb;
	s->tls_session_ticket_ext_cb_arg = arg;
	return (1);
}
LSSL_ALIAS(SSL_set_session_ticket_ext_cb);

int
SSL_set_session_ticket_ext(SSL *s, void *ext_data, int ext_len)
{
	if (s->version >= TLS1_VERSION) {
		free(s->tlsext_session_ticket);
		s->tlsext_session_ticket =
		    malloc(sizeof(TLS_SESSION_TICKET_EXT) + ext_len);
		if (!s->tlsext_session_ticket) {
			SSLerror(s, ERR_R_MALLOC_FAILURE);
			return 0;
		}

		if (ext_data) {
			s->tlsext_session_ticket->length = ext_len;
			s->tlsext_session_ticket->data =
			    s->tlsext_session_ticket + 1;
			memcpy(s->tlsext_session_ticket->data,
			    ext_data, ext_len);
		} else {
			s->tlsext_session_ticket->length = 0;
			s->tlsext_session_ticket->data = NULL;
		}

		return 1;
	}

	return 0;
}
LSSL_ALIAS(SSL_set_session_ticket_ext);

typedef struct timeout_param_st {
	SSL_CTX *ctx;
	long time;
	struct lhash_st_SSL_SESSION *cache;
} TIMEOUT_PARAM;

static void
timeout_doall_arg(SSL_SESSION *s, TIMEOUT_PARAM *p)
{
	if ((p->time == 0) || (p->time > (s->time + s->timeout))) {
		/* timeout */
		/* The reason we don't call SSL_CTX_remove_session() is to
		 * save on locking overhead */
		(void)lh_SSL_SESSION_delete(p->cache, s);
		SSL_SESSION_list_remove(p->ctx, s);
		s->not_resumable = 1;
		if (p->ctx->remove_session_cb != NULL)
			p->ctx->remove_session_cb(p->ctx, s);
		SSL_SESSION_free(s);
	}
}

static void
timeout_LHASH_DOALL_ARG(void *arg1, void *arg2)
{
	SSL_SESSION *a = arg1;
	TIMEOUT_PARAM *b = arg2;

	timeout_doall_arg(a, b);
}

/* XXX 2038 */
void
SSL_CTX_flush_sessions(SSL_CTX *s, long t)
{
	TIMEOUT_PARAM tp;

	tp.ctx = s;
	tp.cache = s->sessions;
	if (tp.cache == NULL)
		return;
	tp.time = t;

	CRYPTO_w_lock(CRYPTO_LOCK_SSL_CTX);
	lh_SSL_SESSION_doall_arg(tp.cache, timeout_LHASH_DOALL_ARG,
	    TIMEOUT_PARAM, &tp);
	CRYPTO_w_unlock(CRYPTO_LOCK_SSL_CTX);
}
LSSL_ALIAS(SSL_CTX_flush_sessions);

int
ssl_clear_bad_session(SSL *s)
{
	if ((s->session != NULL) && !(s->shutdown & SSL_SENT_SHUTDOWN) &&
	    !(SSL_in_init(s) || SSL_in_before(s))) {
		SSL_CTX_remove_session(s->ctx, s->session);
		return (1);
	} else
		return (0);
}

/* locked by SSL_CTX in the calling function */
static void
SSL_SESSION_list_remove(SSL_CTX *ctx, SSL_SESSION *s)
{
	if (s->next == NULL || s->prev == NULL)
		return;

	if (s->next == (SSL_SESSION *)&(ctx->session_cache_tail)) {
		/* last element in list */
		if (s->prev == (SSL_SESSION *)&(ctx->session_cache_head)) {
			/* only one element in list */
			ctx->session_cache_head = NULL;
			ctx->session_cache_tail = NULL;
		} else {
			ctx->session_cache_tail = s->prev;
			s->prev->next =
			    (SSL_SESSION *)&(ctx->session_cache_tail);
		}
	} else {
		if (s->prev == (SSL_SESSION *)&(ctx->session_cache_head)) {
			/* first element in list */
			ctx->session_cache_head = s->next;
			s->next->prev =
			    (SSL_SESSION *)&(ctx->session_cache_head);
		} else {
			/* middle of list */
			s->next->prev = s->prev;
			s->prev->next = s->next;
		}
	}
	s->prev = s->next = NULL;
}

static void
SSL_SESSION_list_add(SSL_CTX *ctx, SSL_SESSION *s)
{
	if (s->next != NULL && s->prev != NULL)
		SSL_SESSION_list_remove(ctx, s);

	if (ctx->session_cache_head == NULL) {
		ctx->session_cache_head = s;
		ctx->session_cache_tail = s;
		s->prev = (SSL_SESSION *)&(ctx->session_cache_head);
		s->next = (SSL_SESSION *)&(ctx->session_cache_tail);
	} else {
		s->next = ctx->session_cache_head;
		s->next->prev = s;
		s->prev = (SSL_SESSION *)&(ctx->session_cache_head);
		ctx->session_cache_head = s;
	}
}

void
SSL_CTX_sess_set_new_cb(SSL_CTX *ctx,
    int (*cb)(struct ssl_st *ssl, SSL_SESSION *sess)) {
	ctx->new_session_cb = cb;
}
LSSL_ALIAS(SSL_CTX_sess_set_new_cb);

int
(*SSL_CTX_sess_get_new_cb(SSL_CTX *ctx))(SSL *ssl, SSL_SESSION *sess)
{
	return ctx->new_session_cb;
}
LSSL_ALIAS(SSL_CTX_sess_get_new_cb);

void
SSL_CTX_sess_set_remove_cb(SSL_CTX *ctx,
    void (*cb)(SSL_CTX *ctx, SSL_SESSION *sess))
{
	ctx->remove_session_cb = cb;
}
LSSL_ALIAS(SSL_CTX_sess_set_remove_cb);

void
(*SSL_CTX_sess_get_remove_cb(SSL_CTX *ctx))(SSL_CTX * ctx, SSL_SESSION *sess)
{
	return ctx->remove_session_cb;
}
LSSL_ALIAS(SSL_CTX_sess_get_remove_cb);

void
SSL_CTX_sess_set_get_cb(SSL_CTX *ctx, SSL_SESSION *(*cb)(struct ssl_st *ssl,
    const unsigned char *data, int len, int *copy))
{
	ctx->get_session_cb = cb;
}
LSSL_ALIAS(SSL_CTX_sess_set_get_cb);

SSL_SESSION *
(*SSL_CTX_sess_get_get_cb(SSL_CTX *ctx))(SSL *ssl, const unsigned char *data,
    int len, int *copy)
{
	return ctx->get_session_cb;
}
LSSL_ALIAS(SSL_CTX_sess_get_get_cb);

void
SSL_CTX_set_info_callback(SSL_CTX *ctx,
    void (*cb)(const SSL *ssl, int type, int val))
{
	ctx->info_callback = cb;
}
LSSL_ALIAS(SSL_CTX_set_info_callback);

void
(*SSL_CTX_get_info_callback(SSL_CTX *ctx))(const SSL *ssl, int type, int val)
{
	return ctx->info_callback;
}
LSSL_ALIAS(SSL_CTX_get_info_callback);

void
SSL_CTX_set_client_cert_cb(SSL_CTX *ctx,
    int (*cb)(SSL *ssl, X509 **x509, EVP_PKEY **pkey))
{
	ctx->client_cert_cb = cb;
}
LSSL_ALIAS(SSL_CTX_set_client_cert_cb);

int
(*SSL_CTX_get_client_cert_cb(SSL_CTX *ctx))(SSL * ssl, X509 ** x509,
    EVP_PKEY **pkey)
{
	return ctx->client_cert_cb;
}
LSSL_ALIAS(SSL_CTX_get_client_cert_cb);

void
SSL_CTX_set_cookie_generate_cb(SSL_CTX *ctx,
    int (*cb)(SSL *ssl, unsigned char *cookie, unsigned int *cookie_len))
{
	ctx->app_gen_cookie_cb = cb;
}
LSSL_ALIAS(SSL_CTX_set_cookie_generate_cb);

void
SSL_CTX_set_cookie_verify_cb(SSL_CTX *ctx,
    int (*cb)(SSL *ssl, const unsigned char *cookie, unsigned int cookie_len))
{
	ctx->app_verify_cookie_cb = cb;
}
LSSL_ALIAS(SSL_CTX_set_cookie_verify_cb);

int
PEM_write_SSL_SESSION(FILE *fp, SSL_SESSION *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_SSL_SESSION,
	    PEM_STRING_SSL_SESSION, fp, x, NULL, NULL, 0, NULL, NULL);
}
LSSL_ALIAS(PEM_write_SSL_SESSION);

SSL_SESSION *
PEM_read_SSL_SESSION(FILE *fp, SSL_SESSION **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_SSL_SESSION,
	    PEM_STRING_SSL_SESSION, fp, (void **)x, cb, u);
}
LSSL_ALIAS(PEM_read_SSL_SESSION);

SSL_SESSION *
PEM_read_bio_SSL_SESSION(BIO *bp, SSL_SESSION **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_SSL_SESSION,
	    PEM_STRING_SSL_SESSION, bp, (void **)x, cb, u);
}
LSSL_ALIAS(PEM_read_bio_SSL_SESSION);

int
PEM_write_bio_SSL_SESSION(BIO *bp, SSL_SESSION *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_SSL_SESSION,
	    PEM_STRING_SSL_SESSION, bp, x, NULL, NULL, 0, NULL, NULL);
}
LSSL_ALIAS(PEM_write_bio_SSL_SESSION);
