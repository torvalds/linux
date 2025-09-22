/* $OpenBSD: tls_conninfo.c,v 1.28 2024/12/10 08:40:30 tb Exp $ */
/*
 * Copyright (c) 2015 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2015 Bob Beck <beck@openbsd.org>
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

#include <stdio.h>
#include <string.h>

#include <openssl/posix_time.h>
#include <openssl/x509.h>

#include <tls.h>
#include "tls_internal.h"

static int
tls_convert_notafter(struct tm *tm, time_t *out_time)
{
	int64_t posix_time;

	/* OPENSSL_timegm() fails if tm is not representable in a time_t */
	if (OPENSSL_timegm(tm, out_time))
		return 1;
	if (!OPENSSL_tm_to_posix(tm, &posix_time))
		return 0;
	if (posix_time < INT32_MIN)
		return 0;
	*out_time = (posix_time > INT32_MAX) ? INT32_MAX : posix_time;
	return 1;
}

int
tls_hex_string(const unsigned char *in, size_t inlen, char **out,
    size_t *outlen)
{
	static const char hex[] = "0123456789abcdef";
	size_t i, len;
	char *p;

	if (outlen != NULL)
		*outlen = 0;

	if (inlen >= SIZE_MAX)
		return (-1);
	if ((*out = reallocarray(NULL, inlen + 1, 2)) == NULL)
		return (-1);

	p = *out;
	len = 0;
	for (i = 0; i < inlen; i++) {
		p[len++] = hex[(in[i] >> 4) & 0x0f];
		p[len++] = hex[in[i] & 0x0f];
	}
	p[len++] = 0;

	if (outlen != NULL)
		*outlen = len;

	return (0);
}

static int
tls_get_peer_cert_hash(struct tls *ctx, char **hash)
{
	*hash = NULL;
	if (ctx->ssl_peer_cert == NULL)
		return (0);

	if (tls_cert_hash(ctx->ssl_peer_cert, hash) == -1) {
		tls_set_errorx(ctx, TLS_ERROR_OUT_OF_MEMORY, "out of memory");
		*hash = NULL;
		return -1;
	}
	return 0;
}

static int
tls_get_peer_cert_issuer(struct tls *ctx,  char **issuer)
{
	X509_NAME *name = NULL;

	*issuer = NULL;
	if (ctx->ssl_peer_cert == NULL)
		return (-1);
	if ((name = X509_get_issuer_name(ctx->ssl_peer_cert)) == NULL)
		return (-1);
	*issuer = X509_NAME_oneline(name, 0, 0);
	if (*issuer == NULL)
		return (-1);
	return (0);
}

static int
tls_get_peer_cert_subject(struct tls *ctx, char **subject)
{
	X509_NAME *name = NULL;

	*subject = NULL;
	if (ctx->ssl_peer_cert == NULL)
		return (-1);
	if ((name = X509_get_subject_name(ctx->ssl_peer_cert)) == NULL)
		return (-1);
	*subject = X509_NAME_oneline(name, 0, 0);
	if (*subject == NULL)
		return (-1);
	return (0);
}

static int
tls_get_peer_cert_common_name(struct tls *ctx, char **common_name)
{
	if (ctx->ssl_peer_cert == NULL)
		return (-1);
	return tls_get_common_name(ctx, ctx->ssl_peer_cert, NULL, common_name);
}

static int
tls_get_peer_cert_times(struct tls *ctx, time_t *notbefore,
    time_t *notafter)
{
	struct tm before_tm, after_tm;
	ASN1_TIME *before, *after;

	if (ctx->ssl_peer_cert == NULL)
		return (-1);

	if ((before = X509_get_notBefore(ctx->ssl_peer_cert)) == NULL)
		goto err;
	if ((after = X509_get_notAfter(ctx->ssl_peer_cert)) == NULL)
		goto err;
	if (!ASN1_TIME_to_tm(before, &before_tm))
		goto err;
	if (!ASN1_TIME_to_tm(after, &after_tm))
		goto err;
	if (!tls_convert_notafter(&after_tm, notafter))
		goto err;
	if (!OPENSSL_timegm(&before_tm, notbefore))
		goto err;
	return (0);

 err:
	return (-1);
}

static int
tls_get_peer_cert_info(struct tls *ctx)
{
	if (ctx->ssl_peer_cert == NULL)
		return (0);

	if (tls_get_peer_cert_hash(ctx, &ctx->conninfo->hash) == -1)
		goto err;
	if (tls_get_peer_cert_subject(ctx, &ctx->conninfo->subject) == -1)
		goto err;
	if (tls_get_peer_cert_issuer(ctx, &ctx->conninfo->issuer) == -1)
		goto err;
	if (tls_get_peer_cert_common_name(ctx,
	    &ctx->conninfo->common_name) == -1)
		goto err;
	if (tls_get_peer_cert_times(ctx, &ctx->conninfo->notbefore,
	    &ctx->conninfo->notafter) == -1)
		goto err;

	return (0);

 err:
	return (-1);
}

static int
tls_conninfo_alpn_proto(struct tls *ctx)
{
	const unsigned char *p;
	unsigned int len;

	free(ctx->conninfo->alpn);
	ctx->conninfo->alpn = NULL;

	SSL_get0_alpn_selected(ctx->ssl_conn, &p, &len);
	if (len > 0) {
		if ((ctx->conninfo->alpn = malloc(len + 1)) == NULL)
			return (-1);
		memcpy(ctx->conninfo->alpn, p, len);
		ctx->conninfo->alpn[len] = '\0';
	}

	return (0);
}

static int
tls_conninfo_cert_pem(struct tls *ctx)
{
	int i, rv = -1;
	BIO *membio = NULL;
	BUF_MEM *bptr = NULL;

	if (ctx->ssl_peer_cert == NULL)
		return 0;
	if ((membio = BIO_new(BIO_s_mem()))== NULL)
		goto err;

	/*
	 * We have to write the peer cert out separately, because
	 * the certificate chain may or may not contain it.
	 */
	if (!PEM_write_bio_X509(membio, ctx->ssl_peer_cert))
		goto err;
	for (i = 0; i < sk_X509_num(ctx->ssl_peer_chain); i++) {
		X509 *chaincert = sk_X509_value(ctx->ssl_peer_chain, i);
		if (chaincert != ctx->ssl_peer_cert &&
		    !PEM_write_bio_X509(membio, chaincert))
			goto err;
	}

	BIO_get_mem_ptr(membio, &bptr);
	free(ctx->conninfo->peer_cert);
	ctx->conninfo->peer_cert_len = 0;
	if ((ctx->conninfo->peer_cert = malloc(bptr->length)) == NULL)
		goto err;
	ctx->conninfo->peer_cert_len = bptr->length;
	memcpy(ctx->conninfo->peer_cert, bptr->data,
	    ctx->conninfo->peer_cert_len);

	/* BIO_free() will kill BUF_MEM - because we have not set BIO_NOCLOSE */
	rv = 0;
 err:
	BIO_free(membio);
	return rv;
}

static int
tls_conninfo_session(struct tls *ctx)
{
	ctx->conninfo->session_resumed = SSL_session_reused(ctx->ssl_conn);

	return 0;
}

int
tls_conninfo_populate(struct tls *ctx)
{
	const char *tmp;

	tls_conninfo_free(ctx->conninfo);

	if ((ctx->conninfo = calloc(1, sizeof(struct tls_conninfo))) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_OUT_OF_MEMORY, "out of memory");
		goto err;
	}

	if (tls_conninfo_alpn_proto(ctx) == -1)
		goto err;

	if ((tmp = SSL_get_cipher(ctx->ssl_conn)) == NULL)
		goto err;
	if ((ctx->conninfo->cipher = strdup(tmp)) == NULL)
		goto err;
	ctx->conninfo->cipher_strength = SSL_get_cipher_bits(ctx->ssl_conn, NULL);

	if (ctx->servername != NULL) {
		if ((ctx->conninfo->servername =
		    strdup(ctx->servername)) == NULL)
			goto err;
	}

	if ((tmp = SSL_get_version(ctx->ssl_conn)) == NULL)
		goto err;
	if ((ctx->conninfo->version = strdup(tmp)) == NULL)
		goto err;

	if (tls_get_peer_cert_info(ctx) == -1)
		goto err;

	if (tls_conninfo_cert_pem(ctx) == -1)
		goto err;

	if (tls_conninfo_session(ctx) == -1)
		goto err;

	return (0);

 err:
	tls_conninfo_free(ctx->conninfo);
	ctx->conninfo = NULL;

	return (-1);
}

void
tls_conninfo_free(struct tls_conninfo *conninfo)
{
	if (conninfo == NULL)
		return;

	free(conninfo->alpn);
	free(conninfo->cipher);
	free(conninfo->servername);
	free(conninfo->version);

	free(conninfo->common_name);
	free(conninfo->hash);
	free(conninfo->issuer);
	free(conninfo->subject);

	free(conninfo->peer_cert);

	free(conninfo);
}

const char *
tls_conn_alpn_selected(struct tls *ctx)
{
	if (ctx->conninfo == NULL)
		return (NULL);
	return (ctx->conninfo->alpn);
}

const char *
tls_conn_cipher(struct tls *ctx)
{
	if (ctx->conninfo == NULL)
		return (NULL);
	return (ctx->conninfo->cipher);
}

int
tls_conn_cipher_strength(struct tls *ctx)
{
	if (ctx->conninfo == NULL)
		return (0);
	return (ctx->conninfo->cipher_strength);
}

const char *
tls_conn_servername(struct tls *ctx)
{
	if (ctx->conninfo == NULL)
		return (NULL);
	return (ctx->conninfo->servername);
}

int
tls_conn_session_resumed(struct tls *ctx)
{
	if (ctx->conninfo == NULL)
		return (0);
	return (ctx->conninfo->session_resumed);
}

const char *
tls_conn_version(struct tls *ctx)
{
	if (ctx->conninfo == NULL)
		return (NULL);
	return (ctx->conninfo->version);
}
