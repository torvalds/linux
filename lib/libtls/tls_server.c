/* $OpenBSD: tls_server.c,v 1.52 2025/06/04 10:25:30 tb Exp $ */
/*
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>

#include <arpa/inet.h>

#include <string.h>

#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <tls.h>
#include "tls_internal.h"

struct tls *
tls_server(void)
{
	struct tls *ctx;

	if (tls_init() == -1)
		return (NULL);

	if ((ctx = tls_new()) == NULL)
		return (NULL);

	ctx->flags |= TLS_SERVER;

	return (ctx);
}

struct tls *
tls_server_conn(struct tls *ctx)
{
	struct tls *conn_ctx;

	if ((conn_ctx = tls_new()) == NULL)
		return (NULL);

	conn_ctx->flags |= TLS_SERVER_CONN;

	pthread_mutex_lock(&ctx->config->mutex);
	ctx->config->refcount++;
	pthread_mutex_unlock(&ctx->config->mutex);

	conn_ctx->config = ctx->config;
	conn_ctx->keypair = ctx->config->keypair;

	return (conn_ctx);
}

static int
tls_server_alpn_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg)
{
	struct tls *ctx = arg;

	if (SSL_select_next_proto((unsigned char**)out, outlen,
	    ctx->config->alpn, ctx->config->alpn_len, in, inlen) ==
	    OPENSSL_NPN_NEGOTIATED)
		return (SSL_TLSEXT_ERR_OK);

	return (SSL_TLSEXT_ERR_ALERT_FATAL);
}

static int
tls_servername_cb(SSL *ssl, int *al, void *arg)
{
	struct tls *ctx = (struct tls *)arg;
	struct tls_sni_ctx *sni_ctx;
	union tls_addr addrbuf;
	struct tls *conn_ctx;
	const char *name;
	int match;

	if ((conn_ctx = SSL_get_app_data(ssl)) == NULL)
		goto err;

	if ((name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name)) ==
	    NULL) {
		/*
		 * The servername callback gets called even when there is no
		 * TLS servername extension provided by the client. Sigh!
		 */
		return (SSL_TLSEXT_ERR_NOACK);
	}

	/*
	 * Per RFC 6066 section 3: ensure that name is not an IP literal.
	 *
	 * While we should treat this as an error, a number of clients
	 * (Python, Ruby and Safari) are not RFC compliant. To avoid handshake
	 * failures, pretend that we did not receive the extension.
	 */
	if (inet_pton(AF_INET, name, &addrbuf) == 1 ||
            inet_pton(AF_INET6, name, &addrbuf) == 1)
		return (SSL_TLSEXT_ERR_NOACK);

	free(conn_ctx->servername);
	if ((conn_ctx->servername = strdup(name)) == NULL)
		goto err;

	/* Find appropriate SSL context for requested servername. */
	for (sni_ctx = ctx->sni_ctx; sni_ctx != NULL; sni_ctx = sni_ctx->next) {
		if (tls_check_name(ctx, sni_ctx->ssl_cert, name,
		    &match) == -1)
			goto err;
		if (match) {
			conn_ctx->keypair = sni_ctx->keypair;
			SSL_set_SSL_CTX(conn_ctx->ssl_conn, sni_ctx->ssl_ctx);
			return (SSL_TLSEXT_ERR_OK);
		}
	}

	/* No match, use the existing context/certificate. */
	return (SSL_TLSEXT_ERR_OK);

 err:
	/*
	 * There is no way to tell libssl that an internal failure occurred.
	 * The only option we have is to return a fatal alert.
	 */
	*al = SSL_AD_INTERNAL_ERROR;
	return (SSL_TLSEXT_ERR_ALERT_FATAL);
}

static struct tls_ticket_key *
tls_server_ticket_key(struct tls_config *config, unsigned char *keyname)
{
	struct tls_ticket_key *key = NULL;
	time_t now;
	int i;

	now = time(NULL);
	if (config->ticket_autorekey == 1) {
		if (now - 3 * (config->session_lifetime / 4) >
		    config->ticket_keys[0].time) {
			if (tls_config_ticket_autorekey(config) == -1)
				return (NULL);
		}
	}
	for (i = 0; i < TLS_NUM_TICKETS; i++) {
		struct tls_ticket_key *tk = &config->ticket_keys[i];
		if (now - config->session_lifetime > tk->time)
			continue;
		if (keyname == NULL || timingsafe_memcmp(keyname,
		    tk->key_name, sizeof(tk->key_name)) == 0) {
			key = tk;
			break;
		}
	}
	return (key);
}

static int
tls_server_ticket_cb(SSL *ssl, unsigned char *keyname, unsigned char *iv,
    EVP_CIPHER_CTX *ctx, HMAC_CTX *hctx, int mode)
{
	struct tls_ticket_key *key;
	struct tls *tls_ctx;

	if ((tls_ctx = SSL_get_app_data(ssl)) == NULL)
		return (-1);

	if (mode == 1) {
		/* create new session */
		key = tls_server_ticket_key(tls_ctx->config, NULL);
		if (key == NULL) {
			tls_set_errorx(tls_ctx, TLS_ERROR_UNKNOWN,
			    "no valid ticket key found");
			return (-1);
		}

		memcpy(keyname, key->key_name, sizeof(key->key_name));
		arc4random_buf(iv, EVP_MAX_IV_LENGTH);
		if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
		    key->aes_key, iv)) {
			tls_set_errorx(tls_ctx, TLS_ERROR_UNKNOWN,
			    "failed to init encrypt");
			return (-1);
		}
		if (!HMAC_Init_ex(hctx, key->hmac_key, sizeof(key->hmac_key),
		    EVP_sha256(), NULL)) {
			tls_set_errorx(tls_ctx, TLS_ERROR_UNKNOWN,
			    "failed to init hmac");
			return (-1);
		}
		return (0);
	} else {
		/* get key by name */
		key = tls_server_ticket_key(tls_ctx->config, keyname);
		if (key == NULL)
			return (0);

		if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
		    key->aes_key, iv)) {
			tls_set_errorx(tls_ctx, TLS_ERROR_UNKNOWN,
			    "failed to init decrypt");
			return (-1);
		}
		if (!HMAC_Init_ex(hctx, key->hmac_key, sizeof(key->hmac_key),
		    EVP_sha256(), NULL)) {
			tls_set_errorx(tls_ctx, TLS_ERROR_UNKNOWN,
			    "failed to init hmac");
			return (-1);
		}

		/* time to renew the ticket? is it the primary key? */
		if (key != &tls_ctx->config->ticket_keys[0])
			return (2);
		return (1);
	}
}

static int
tls_configure_server_ssl(struct tls *ctx, SSL_CTX **ssl_ctx,
    struct tls_keypair *keypair)
{
	SSL_CTX_free(*ssl_ctx);

	if ((*ssl_ctx = SSL_CTX_new(SSLv23_server_method())) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "ssl context failure");
		goto err;
	}

	SSL_CTX_set_options(*ssl_ctx, SSL_OP_NO_CLIENT_RENEGOTIATION);

	if (SSL_CTX_set_tlsext_servername_callback(*ssl_ctx,
	    tls_servername_cb) != 1) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "failed to set servername callback");
		goto err;
	}
	if (SSL_CTX_set_tlsext_servername_arg(*ssl_ctx, ctx) != 1) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "failed to set servername callback arg");
		goto err;
	}

	if (tls_configure_ssl(ctx, *ssl_ctx) != 0)
		goto err;
	if (tls_configure_ssl_keypair(ctx, *ssl_ctx, keypair, 1) != 0)
		goto err;
	if (ctx->config->verify_client != 0) {
		int verify = SSL_VERIFY_PEER;
		if (ctx->config->verify_client == 1)
			verify |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
		if (tls_configure_ssl_verify(ctx, *ssl_ctx, verify) == -1)
			goto err;
	}

	if (ctx->config->alpn != NULL)
		SSL_CTX_set_alpn_select_cb(*ssl_ctx, tls_server_alpn_cb,
		    ctx);

	if (ctx->config->dheparams == -1)
		SSL_CTX_set_dh_auto(*ssl_ctx, 1);
	else if (ctx->config->dheparams == 1024)
		SSL_CTX_set_dh_auto(*ssl_ctx, 2);

	if (ctx->config->ecdhecurves != NULL) {
		SSL_CTX_set_ecdh_auto(*ssl_ctx, 1);
		if (SSL_CTX_set1_groups(*ssl_ctx, ctx->config->ecdhecurves,
		    ctx->config->ecdhecurves_len) != 1) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to set ecdhe curves");
			goto err;
		}
	}

	if (ctx->config->ciphers_server == 1)
		SSL_CTX_set_options(*ssl_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

	if (SSL_CTX_set_tlsext_status_cb(*ssl_ctx, tls_ocsp_stapling_cb) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "failed to add OCSP stapling callback");
		goto err;
	}

	if (ctx->config->session_lifetime > 0) {
		/* set the session lifetime and enable tickets */
		SSL_CTX_set_timeout(*ssl_ctx, ctx->config->session_lifetime);
		SSL_CTX_clear_options(*ssl_ctx, SSL_OP_NO_TICKET);
		if (!SSL_CTX_set_tlsext_ticket_key_cb(*ssl_ctx,
		    tls_server_ticket_cb)) {
			tls_set_error(ctx, TLS_ERROR_UNKNOWN,
			    "failed to set the TLS ticket callback");
			goto err;
		}
	}

	if (SSL_CTX_set_session_id_context(*ssl_ctx, ctx->config->session_id,
	    sizeof(ctx->config->session_id)) != 1) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "failed to set session id context");
		goto err;
	}

	return (0);

  err:
	SSL_CTX_free(*ssl_ctx);
	*ssl_ctx = NULL;

	return (-1);
}

static int
tls_configure_server_sni(struct tls *ctx)
{
	struct tls_sni_ctx **sni_ctx;
	struct tls_keypair *kp;

	if (ctx->config->keypair->next == NULL)
		return (0);

	/* Set up additional SSL contexts for SNI. */
	sni_ctx = &ctx->sni_ctx;
	for (kp = ctx->config->keypair->next; kp != NULL; kp = kp->next) {
		if ((*sni_ctx = tls_sni_ctx_new()) == NULL) {
			tls_set_errorx(ctx, TLS_ERROR_OUT_OF_MEMORY, "out of memory");
			goto err;
		}
		(*sni_ctx)->keypair = kp;
		if (tls_configure_server_ssl(ctx, &(*sni_ctx)->ssl_ctx, kp) == -1)
			goto err;
		if (tls_keypair_load_cert(kp, &ctx->error,
		    &(*sni_ctx)->ssl_cert) == -1)
			goto err;
		sni_ctx = &(*sni_ctx)->next;
	}

	return (0);

 err:
	return (-1);
}

int
tls_configure_server(struct tls *ctx)
{
	if (tls_configure_server_ssl(ctx, &ctx->ssl_ctx,
	    ctx->config->keypair) == -1)
		goto err;
	if (tls_configure_server_sni(ctx) == -1)
		goto err;

	return (0);

 err:
	return (-1);
}

static struct tls *
tls_accept_common(struct tls *ctx)
{
	struct tls *conn_ctx = NULL;

	if ((ctx->flags & TLS_SERVER) == 0) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_CONTEXT,
		    "not a server context");
		goto err;
	}

	if ((conn_ctx = tls_server_conn(ctx)) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "connection context failure");
		goto err;
	}

	if ((conn_ctx->ssl_conn = SSL_new(ctx->ssl_ctx)) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "ssl failure");
		goto err;
	}

	if (SSL_set_app_data(conn_ctx->ssl_conn, conn_ctx) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ssl application data failure");
		goto err;
	}

	return conn_ctx;

 err:
	tls_free(conn_ctx);

	return (NULL);
}

int
tls_accept_socket(struct tls *ctx, struct tls **cctx, int s)
{
	return (tls_accept_fds(ctx, cctx, s, s));
}

int
tls_accept_fds(struct tls *ctx, struct tls **cctx, int fd_read, int fd_write)
{
	struct tls *conn_ctx;

	if ((conn_ctx = tls_accept_common(ctx)) == NULL)
		goto err;

	if (SSL_set_rfd(conn_ctx->ssl_conn, fd_read) != 1 ||
	    SSL_set_wfd(conn_ctx->ssl_conn, fd_write) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ssl file descriptor failure");
		goto err;
	}

	*cctx = conn_ctx;

	return (0);
 err:
	tls_free(conn_ctx);
	*cctx = NULL;

	return (-1);
}

int
tls_accept_cbs(struct tls *ctx, struct tls **cctx,
    tls_read_cb read_cb, tls_write_cb write_cb, void *cb_arg)
{
	struct tls *conn_ctx;

	if ((conn_ctx = tls_accept_common(ctx)) == NULL)
		goto err;

	if (tls_set_cbs(conn_ctx, read_cb, write_cb, cb_arg) != 0)
		goto err;

	*cctx = conn_ctx;

	return (0);
 err:
	tls_free(conn_ctx);
	*cctx = NULL;

	return (-1);
}

int
tls_handshake_server(struct tls *ctx)
{
	int ssl_ret;
	int rv = -1;

	if ((ctx->flags & TLS_SERVER_CONN) == 0) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_CONTEXT,
		    "not a server connection context");
		goto err;
	}

	ctx->state |= TLS_SSL_NEEDS_SHUTDOWN;

	ERR_clear_error();
	if ((ssl_ret = SSL_accept(ctx->ssl_conn)) != 1) {
		rv = tls_ssl_error(ctx, ctx->ssl_conn, ssl_ret, "handshake");
		goto err;
	}

	ctx->state |= TLS_HANDSHAKE_COMPLETE;
	rv = 0;

 err:
	return (rv);
}
