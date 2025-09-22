/* $OpenBSD: tls.c,v 1.104 2024/04/08 20:47:32 tb Exp $ */
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

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/safestack.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <tls.h>
#include "tls_internal.h"

static struct tls_config *tls_config_default;

static int tls_init_rv = -1;

static void
tls_do_init(void)
{
	OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG, NULL);

	if (BIO_sock_init() != 1)
		return;

	if ((tls_config_default = tls_config_new_internal()) == NULL)
		return;

	tls_config_default->refcount++;

	tls_init_rv = 0;
}

int
tls_init(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	if (pthread_once(&once, tls_do_init) != 0)
		return -1;

	return tls_init_rv;
}

const char *
tls_error(struct tls *ctx)
{
	return ctx->error.msg;
}

int
tls_error_code(struct tls *ctx)
{
	return ctx->error.code;
}

void
tls_error_clear(struct tls_error *error)
{
	free(error->msg);
	error->msg = NULL;
	error->code = TLS_ERROR_UNKNOWN;
	error->errno_value = 0;
	error->tls = 0;
}

static int
tls_error_vset(struct tls_error *error, int code, int errno_value,
    const char *fmt, va_list ap)
{
	char *errmsg = NULL;
	int rv = -1;

	tls_error_clear(error);

	error->code = code;
	error->errno_value = errno_value;
	error->tls = 1;

	if (vasprintf(&errmsg, fmt, ap) == -1) {
		errmsg = NULL;
		goto err;
	}

	if (errno_value == -1) {
		error->msg = errmsg;
		return (0);
	}

	if (asprintf(&error->msg, "%s: %s", errmsg, strerror(errno_value)) == -1) {
		error->msg = NULL;
		goto err;
	}
	rv = 0;

 err:
	free(errmsg);

	return (rv);
}

int
tls_error_set(struct tls_error *error, int code, const char *fmt, ...)
{
	va_list ap;
	int errno_value, rv;

	errno_value = errno;

	va_start(ap, fmt);
	rv = tls_error_vset(error, code, errno_value, fmt, ap);
	va_end(ap);

	return (rv);
}

int
tls_error_setx(struct tls_error *error, int code, const char *fmt, ...)
{
	va_list ap;
	int rv;

	va_start(ap, fmt);
	rv = tls_error_vset(error, code, -1, fmt, ap);
	va_end(ap);

	return (rv);
}

int
tls_config_set_error(struct tls_config *config, int code, const char *fmt, ...)
{
	va_list ap;
	int errno_value, rv;

	errno_value = errno;

	va_start(ap, fmt);
	rv = tls_error_vset(&config->error, code, errno_value, fmt, ap);
	va_end(ap);

	return (rv);
}

int
tls_config_set_errorx(struct tls_config *config, int code, const char *fmt, ...)
{
	va_list ap;
	int rv;

	va_start(ap, fmt);
	rv = tls_error_vset(&config->error, code, -1, fmt, ap);
	va_end(ap);

	return (rv);
}

int
tls_set_error(struct tls *ctx, int code, const char *fmt, ...)
{
	va_list ap;
	int errno_value, rv;

	errno_value = errno;

	va_start(ap, fmt);
	rv = tls_error_vset(&ctx->error, code, errno_value, fmt, ap);
	va_end(ap);

	return (rv);
}

int
tls_set_errorx(struct tls *ctx, int code, const char *fmt, ...)
{
	va_list ap;
	int rv;

	va_start(ap, fmt);
	rv = tls_error_vset(&ctx->error, code, -1, fmt, ap);
	va_end(ap);

	return (rv);
}

int
tls_set_ssl_errorx(struct tls *ctx, int code, const char *fmt, ...)
{
	va_list ap;
	int rv;

	/* Only set an error if a more specific one does not already exist. */
	if (ctx->error.tls != 0)
		return (0);

	va_start(ap, fmt);
	rv = tls_error_vset(&ctx->error, code, -1, fmt, ap);
	va_end(ap);

	return (rv);
}

struct tls_sni_ctx *
tls_sni_ctx_new(void)
{
	return (calloc(1, sizeof(struct tls_sni_ctx)));
}

void
tls_sni_ctx_free(struct tls_sni_ctx *sni_ctx)
{
	if (sni_ctx == NULL)
		return;

	SSL_CTX_free(sni_ctx->ssl_ctx);
	X509_free(sni_ctx->ssl_cert);

	free(sni_ctx);
}

struct tls *
tls_new(void)
{
	struct tls *ctx;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		return (NULL);

	tls_reset(ctx);

	if (tls_configure(ctx, tls_config_default) == -1) {
		free(ctx);
		return NULL;
	}

	return (ctx);
}

int
tls_configure(struct tls *ctx, struct tls_config *config)
{
	if (config == NULL)
		config = tls_config_default;

	pthread_mutex_lock(&config->mutex);
	config->refcount++;
	pthread_mutex_unlock(&config->mutex);

	tls_config_free(ctx->config);

	ctx->config = config;
	ctx->keypair = config->keypair;

	if ((ctx->flags & TLS_SERVER) != 0)
		return (tls_configure_server(ctx));

	return (0);
}

int
tls_cert_hash(X509 *cert, char **hash)
{
	char d[EVP_MAX_MD_SIZE], *dhex = NULL;
	int dlen, rv = -1;

	free(*hash);
	*hash = NULL;

	if (X509_digest(cert, EVP_sha256(), d, &dlen) != 1)
		goto err;

	if (tls_hex_string(d, dlen, &dhex, NULL) != 0)
		goto err;

	if (asprintf(hash, "SHA256:%s", dhex) == -1) {
		*hash = NULL;
		goto err;
	}

	rv = 0;
 err:
	free(dhex);

	return (rv);
}

int
tls_cert_pubkey_hash(X509 *cert, char **hash)
{
	char d[EVP_MAX_MD_SIZE], *dhex = NULL;
	int dlen, rv = -1;

	free(*hash);
	*hash = NULL;

	if (X509_pubkey_digest(cert, EVP_sha256(), d, &dlen) != 1)
		goto err;

	if (tls_hex_string(d, dlen, &dhex, NULL) != 0)
		goto err;

	if (asprintf(hash, "SHA256:%s", dhex) == -1) {
		*hash = NULL;
		goto err;
	}

	rv = 0;

 err:
	free(dhex);

	return (rv);
}

static int
tls_keypair_to_pkey(struct tls *ctx, struct tls_keypair *keypair, EVP_PKEY **pkey)
{
	BIO *bio = NULL;
	X509 *x509 = NULL;
	char *mem;
	size_t len;
	int ret = -1;

	*pkey = NULL;

	if (ctx->config->use_fake_private_key) {
		mem = keypair->cert_mem;
		len = keypair->cert_len;
	} else {
		mem = keypair->key_mem;
		len = keypair->key_len;
	}

	if (mem == NULL)
		return (0);

	if (len > INT_MAX) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_ARGUMENT,
		    ctx->config->use_fake_private_key ?
		    "certificate too long" : "key too long");
		goto err;
	}

	if ((bio = BIO_new_mem_buf(mem, len)) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "failed to create buffer");
		goto err;
	}

	if (ctx->config->use_fake_private_key) {
		if ((x509 = PEM_read_bio_X509(bio, NULL, tls_password_cb,
		    NULL)) == NULL) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to read X509 certificate");
			goto err;
		}
		if ((*pkey = X509_get_pubkey(x509)) == NULL) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to retrieve pubkey");
			goto err;
		}
	} else {
		if ((*pkey = PEM_read_bio_PrivateKey(bio, NULL, tls_password_cb,
		    NULL)) ==  NULL) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to read private key");
			goto err;
		}
	}

	ret = 0;
 err:
	BIO_free(bio);
	X509_free(x509);
	return (ret);
}

static int
tls_keypair_setup_pkey(struct tls *ctx, struct tls_keypair *keypair, EVP_PKEY *pkey)
{
	RSA_METHOD *rsa_method;
	EC_KEY_METHOD *ecdsa_method;
	RSA *rsa = NULL;
	EC_KEY *eckey = NULL;
	int ret = -1;

	/* Only install the pubkey hash if fake private keys are used. */
	if (!ctx->config->skip_private_key_check)
		return (0);

	if (keypair->pubkey_hash == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "public key hash not set");
		goto err;
	}

	switch (EVP_PKEY_id(pkey)) {
	case EVP_PKEY_RSA:
		if ((rsa = EVP_PKEY_get1_RSA(pkey)) == NULL ||
		    RSA_set_ex_data(rsa, 0, keypair->pubkey_hash) == 0) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "RSA key setup failure");
			goto err;
		}
		if (ctx->config->sign_cb != NULL) {
			rsa_method = tls_signer_rsa_method();
			if (rsa_method == NULL ||
			    RSA_set_ex_data(rsa, 1, ctx->config) == 0 ||
			    RSA_set_method(rsa, rsa_method) == 0) {
				tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
				    "failed to setup RSA key");
				goto err;
			}
		}
		/* Reset the key to work around caching in OpenSSL 3. */
		if (EVP_PKEY_set1_RSA(pkey, rsa) == 0) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to set RSA key");
			goto err;
		}
		break;
	case EVP_PKEY_EC:
		if ((eckey = EVP_PKEY_get1_EC_KEY(pkey)) == NULL ||
		    EC_KEY_set_ex_data(eckey, 0, keypair->pubkey_hash) == 0) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "EC key setup failure");
			goto err;
		}
		if (ctx->config->sign_cb != NULL) {
			ecdsa_method = tls_signer_ecdsa_method();
			if (ecdsa_method == NULL ||
			    EC_KEY_set_ex_data(eckey, 1, ctx->config) == 0 ||
			    EC_KEY_set_method(eckey, ecdsa_method) == 0) {
				tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
				    "failed to setup EC key");
				goto err;
			}
		}
		/* Reset the key to work around caching in OpenSSL 3. */
		if (EVP_PKEY_set1_EC_KEY(pkey, eckey) == 0) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to set EC key");
			goto err;
		}
		break;
	default:
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "incorrect key type");
		goto err;
	}

	ret = 0;

 err:
	RSA_free(rsa);
	EC_KEY_free(eckey);
	return (ret);
}

int
tls_configure_ssl_keypair(struct tls *ctx, SSL_CTX *ssl_ctx,
    struct tls_keypair *keypair, int required)
{
	EVP_PKEY *pkey = NULL;

	if (!required &&
	    keypair->cert_mem == NULL &&
	    keypair->key_mem == NULL)
		return(0);

	if (keypair->cert_mem != NULL) {
		if (keypair->cert_len > INT_MAX) {
			tls_set_errorx(ctx, TLS_ERROR_INVALID_ARGUMENT,
			    "certificate too long");
			goto err;
		}

		if (SSL_CTX_use_certificate_chain_mem(ssl_ctx,
		    keypair->cert_mem, keypair->cert_len) != 1) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to load certificate");
			goto err;
		}
	}

	if (tls_keypair_to_pkey(ctx, keypair, &pkey) == -1)
		goto err;
	if (pkey != NULL) {
		if (tls_keypair_setup_pkey(ctx, keypair, pkey) == -1)
			goto err;
		if (SSL_CTX_use_PrivateKey(ssl_ctx, pkey) != 1) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to load private key");
			goto err;
		}
		EVP_PKEY_free(pkey);
		pkey = NULL;
	}

	if (!ctx->config->skip_private_key_check &&
	    SSL_CTX_check_private_key(ssl_ctx) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "private/public key mismatch");
		goto err;
	}

	return (0);

 err:
	EVP_PKEY_free(pkey);

	return (-1);
}

int
tls_configure_ssl(struct tls *ctx, SSL_CTX *ssl_ctx)
{
	SSL_CTX_clear_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);

	SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
	SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

	SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2);
	SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv3);
	SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TLSv1);
	SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TLSv1_1);

	SSL_CTX_clear_options(ssl_ctx, SSL_OP_NO_TLSv1_2);
	SSL_CTX_clear_options(ssl_ctx, SSL_OP_NO_TLSv1_3);

	if ((ctx->config->protocols & TLS_PROTOCOL_TLSv1_2) == 0)
		SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TLSv1_2);
	if ((ctx->config->protocols & TLS_PROTOCOL_TLSv1_3) == 0)
		SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TLSv1_3);

	if (ctx->config->alpn != NULL) {
		if (SSL_CTX_set_alpn_protos(ssl_ctx, ctx->config->alpn,
		    ctx->config->alpn_len) != 0) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to set alpn");
			goto err;
		}
	}

	if (ctx->config->ciphers != NULL) {
		if (SSL_CTX_set_cipher_list(ssl_ctx,
		    ctx->config->ciphers) != 1) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to set ciphers");
			goto err;
		}
	}

	if (ctx->config->verify_time == 0) {
		X509_VERIFY_PARAM_set_flags(SSL_CTX_get0_param(ssl_ctx),
		    X509_V_FLAG_NO_CHECK_TIME);
	}

	/* Disable any form of session caching by default */
	SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_OFF);
	SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TICKET);

	return (0);

 err:
	return (-1);
}

static int
tls_ssl_cert_verify_cb(X509_STORE_CTX *x509_ctx, void *arg)
{
	struct tls *ctx = arg;
	int x509_err;

	if (ctx->config->verify_cert == 0)
		return (1);

	if ((X509_verify_cert(x509_ctx)) < 0) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "X509 verify cert failed");
		return (0);
	}

	x509_err = X509_STORE_CTX_get_error(x509_ctx);
	if (x509_err == X509_V_OK)
		return (1);

	tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
	    "certificate verification failed: %s",
	    X509_verify_cert_error_string(x509_err));

	return (0);
}

int
tls_configure_ssl_verify(struct tls *ctx, SSL_CTX *ssl_ctx, int verify)
{
	size_t ca_len = ctx->config->ca_len;
	char *ca_mem = ctx->config->ca_mem;
	char *crl_mem = ctx->config->crl_mem;
	size_t crl_len = ctx->config->crl_len;
	char *ca_free = NULL;
	STACK_OF(X509_INFO) *xis = NULL;
	X509_STORE *store;
	X509_INFO *xi;
	BIO *bio = NULL;
	int rv = -1;
	int i;

	SSL_CTX_set_verify(ssl_ctx, verify, NULL);
	SSL_CTX_set_cert_verify_callback(ssl_ctx, tls_ssl_cert_verify_cb, ctx);

	if (ctx->config->verify_depth >= 0)
		SSL_CTX_set_verify_depth(ssl_ctx, ctx->config->verify_depth);

	if (ctx->config->verify_cert == 0)
		goto done;

	/* If no CA has been specified, attempt to load the default. */
	if (ctx->config->ca_mem == NULL && ctx->config->ca_path == NULL) {
		if (tls_config_load_file(&ctx->error, "CA", tls_default_ca_cert_file(),
		    &ca_mem, &ca_len) != 0)
			goto err;
		ca_free = ca_mem;
	}

	if (ca_mem != NULL) {
		if (ca_len > INT_MAX) {
			tls_set_errorx(ctx, TLS_ERROR_INVALID_ARGUMENT,
			    "ca too long");
			goto err;
		}
		if (SSL_CTX_load_verify_mem(ssl_ctx, ca_mem, ca_len) != 1) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "ssl verify memory setup failure");
			goto err;
		}
	} else if (SSL_CTX_load_verify_locations(ssl_ctx, NULL,
	    ctx->config->ca_path) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ssl verify locations failure");
		goto err;
	}

	if (crl_mem != NULL) {
		if (crl_len > INT_MAX) {
			tls_set_errorx(ctx, TLS_ERROR_INVALID_ARGUMENT,
			    "crl too long");
			goto err;
		}
		if ((bio = BIO_new_mem_buf(crl_mem, crl_len)) == NULL) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to create buffer");
			goto err;
		}
		if ((xis = PEM_X509_INFO_read_bio(bio, NULL, tls_password_cb,
		    NULL)) == NULL) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to parse crl");
			goto err;
		}
		store = SSL_CTX_get_cert_store(ssl_ctx);
		for (i = 0; i < sk_X509_INFO_num(xis); i++) {
			xi = sk_X509_INFO_value(xis, i);
			if (xi->crl == NULL)
				continue;
			if (!X509_STORE_add_crl(store, xi->crl)) {
				tls_set_error(ctx, TLS_ERROR_UNKNOWN,
				    "failed to add crl");
				goto err;
			}
		}
		X509_STORE_set_flags(store,
		    X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
	}

 done:
	rv = 0;

 err:
	sk_X509_INFO_pop_free(xis, X509_INFO_free);
	BIO_free(bio);
	free(ca_free);

	return (rv);
}

void
tls_free(struct tls *ctx)
{
	if (ctx == NULL)
		return;

	tls_reset(ctx);

	free(ctx);
}

void
tls_reset(struct tls *ctx)
{
	struct tls_sni_ctx *sni, *nsni;

	tls_config_free(ctx->config);
	ctx->config = NULL;

	SSL_CTX_free(ctx->ssl_ctx);
	SSL_free(ctx->ssl_conn);
	X509_free(ctx->ssl_peer_cert);

	ctx->ssl_conn = NULL;
	ctx->ssl_ctx = NULL;
	ctx->ssl_peer_cert = NULL;
	/* X509 objects in chain are freed with the SSL */
	ctx->ssl_peer_chain = NULL;

	ctx->socket = -1;
	ctx->state = 0;

	free(ctx->servername);
	ctx->servername = NULL;

	free(ctx->error.msg);
	ctx->error.msg = NULL;
	ctx->error.errno_value = -1;

	tls_conninfo_free(ctx->conninfo);
	ctx->conninfo = NULL;

	tls_ocsp_free(ctx->ocsp);
	ctx->ocsp = NULL;

	for (sni = ctx->sni_ctx; sni != NULL; sni = nsni) {
		nsni = sni->next;
		tls_sni_ctx_free(sni);
	}
	ctx->sni_ctx = NULL;

	ctx->read_cb = NULL;
	ctx->write_cb = NULL;
	ctx->cb_arg = NULL;
}

int
tls_ssl_error(struct tls *ctx, SSL *ssl_conn, int ssl_ret, const char *prefix)
{
	const char *errstr = "unknown error";
	unsigned long err;
	int ssl_err;

	ssl_err = SSL_get_error(ssl_conn, ssl_ret);
	switch (ssl_err) {
	case SSL_ERROR_NONE:
	case SSL_ERROR_ZERO_RETURN:
		return (0);

	case SSL_ERROR_WANT_READ:
		return (TLS_WANT_POLLIN);

	case SSL_ERROR_WANT_WRITE:
		return (TLS_WANT_POLLOUT);

	case SSL_ERROR_SYSCALL:
		if ((err = ERR_peek_error()) != 0) {
			errstr = ERR_error_string(err, NULL);
		} else if (ssl_ret == 0) {
			if ((ctx->state & TLS_HANDSHAKE_COMPLETE) != 0) {
				ctx->state |= TLS_EOF_NO_CLOSE_NOTIFY;
				return (0);
			}
			errstr = "unexpected EOF";
		} else if (ssl_ret == -1) {
			errstr = strerror(errno);
		}
		tls_set_ssl_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "%s failed: %s", prefix, errstr);
		return (-1);

	case SSL_ERROR_SSL:
		if ((err = ERR_peek_error()) != 0) {
			errstr = ERR_error_string(err, NULL);
		}
		tls_set_ssl_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "%s failed: %s", prefix, errstr);
		return (-1);

	case SSL_ERROR_WANT_CONNECT:
	case SSL_ERROR_WANT_ACCEPT:
	case SSL_ERROR_WANT_X509_LOOKUP:
	default:
		tls_set_ssl_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "%s failed (%d)", prefix, ssl_err);
		return (-1);
	}
}

int
tls_handshake(struct tls *ctx)
{
	int rv = -1;

	tls_error_clear(&ctx->error);

	if ((ctx->flags & (TLS_CLIENT | TLS_SERVER_CONN)) == 0) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_CONTEXT,
		    "invalid operation for context");
		goto out;
	}

	if ((ctx->state & TLS_HANDSHAKE_COMPLETE) != 0) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "handshake already completed");
		goto out;
	}

	if ((ctx->flags & TLS_CLIENT) != 0)
		rv = tls_handshake_client(ctx);
	else if ((ctx->flags & TLS_SERVER_CONN) != 0)
		rv = tls_handshake_server(ctx);

	if (rv == 0) {
		ctx->ssl_peer_cert = SSL_get_peer_certificate(ctx->ssl_conn);
		ctx->ssl_peer_chain = SSL_get_peer_cert_chain(ctx->ssl_conn);
		if (tls_conninfo_populate(ctx) == -1)
			rv = -1;
		if (ctx->ocsp == NULL)
			ctx->ocsp = tls_ocsp_setup_from_peer(ctx);
	}
 out:
	/* Prevent callers from performing incorrect error handling */
	errno = 0;
	return (rv);
}

ssize_t
tls_read(struct tls *ctx, void *buf, size_t buflen)
{
	ssize_t rv = -1;
	int ssl_ret;

	tls_error_clear(&ctx->error);

	if ((ctx->state & TLS_HANDSHAKE_COMPLETE) == 0) {
		if ((rv = tls_handshake(ctx)) != 0)
			goto out;
	}

	if (buflen > INT_MAX) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_ARGUMENT,
		    "buflen too long");
		goto out;
	}

	ERR_clear_error();
	if ((ssl_ret = SSL_read(ctx->ssl_conn, buf, buflen)) > 0) {
		rv = (ssize_t)ssl_ret;
		goto out;
	}
	rv = (ssize_t)tls_ssl_error(ctx, ctx->ssl_conn, ssl_ret, "read");

 out:
	/* Prevent callers from performing incorrect error handling */
	errno = 0;
	return (rv);
}

ssize_t
tls_write(struct tls *ctx, const void *buf, size_t buflen)
{
	ssize_t rv = -1;
	int ssl_ret;

	tls_error_clear(&ctx->error);

	if ((ctx->state & TLS_HANDSHAKE_COMPLETE) == 0) {
		if ((rv = tls_handshake(ctx)) != 0)
			goto out;
	}

	if (buflen > INT_MAX) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_ARGUMENT,
		    "buflen too long");
		goto out;
	}

	ERR_clear_error();
	if ((ssl_ret = SSL_write(ctx->ssl_conn, buf, buflen)) > 0) {
		rv = (ssize_t)ssl_ret;
		goto out;
	}
	rv = (ssize_t)tls_ssl_error(ctx, ctx->ssl_conn, ssl_ret, "write");

 out:
	/* Prevent callers from performing incorrect error handling */
	errno = 0;
	return (rv);
}

int
tls_close(struct tls *ctx)
{
	int ssl_ret;
	int rv = 0;

	tls_error_clear(&ctx->error);

	if ((ctx->flags & (TLS_CLIENT | TLS_SERVER_CONN)) == 0) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_CONTEXT,
		    "invalid operation for context");
		rv = -1;
		goto out;
	}

	if (ctx->state & TLS_SSL_NEEDS_SHUTDOWN) {
		ERR_clear_error();
		ssl_ret = SSL_shutdown(ctx->ssl_conn);
		if (ssl_ret < 0) {
			rv = tls_ssl_error(ctx, ctx->ssl_conn, ssl_ret,
			    "shutdown");
			if (rv == TLS_WANT_POLLIN || rv == TLS_WANT_POLLOUT)
				goto out;
		}
		ctx->state &= ~TLS_SSL_NEEDS_SHUTDOWN;
	}

	if (ctx->socket != -1) {
		if (shutdown(ctx->socket, SHUT_RDWR) != 0) {
			if (rv == 0 &&
			    errno != ENOTCONN && errno != ECONNRESET) {
				tls_set_error(ctx, TLS_ERROR_UNKNOWN, "shutdown");
				rv = -1;
			}
		}
		if (close(ctx->socket) != 0) {
			if (rv == 0) {
				tls_set_error(ctx, TLS_ERROR_UNKNOWN, "close");
				rv = -1;
			}
		}
		ctx->socket = -1;
	}

	if ((ctx->state & TLS_EOF_NO_CLOSE_NOTIFY) != 0) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "EOF without close notify");
		rv = -1;
	}

 out:
	/* Prevent callers from performing incorrect error handling */
	errno = 0;
	return (rv);
}
