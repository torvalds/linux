/*	$OpenBSD: ssl_seclevel.c,v 1.30 2025/01/18 10:52:09 tb Exp $ */
/*
 * Copyright (c) 2020-2022 Theo Buehler <tb@openbsd.org>
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

#include <stddef.h>

#include <openssl/asn1.h>
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/ossl_typ.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "bytestring.h"
#include "ssl_local.h"

static int
ssl_security_normalize_level(const SSL_CTX *ctx, const SSL *ssl, int *out_level)
{
	int security_level;

	if (ctx != NULL)
		security_level = SSL_CTX_get_security_level(ctx);
	else
		security_level = SSL_get_security_level(ssl);

	if (security_level < 0)
		security_level = 0;
	if (security_level > 5)
		security_level = 5;

	*out_level = security_level;

	return 1;
}

static int
ssl_security_level_to_minimum_bits(int security_level, int *out_minimum_bits)
{
	if (security_level < 0)
		return 0;

	if (security_level == 0)
		*out_minimum_bits = 0;
	else if (security_level == 1)
		*out_minimum_bits = 80;
	else if (security_level == 2)
		*out_minimum_bits = 112;
	else if (security_level == 3)
		*out_minimum_bits = 128;
	else if (security_level == 4)
		*out_minimum_bits = 192;
	else if (security_level >= 5)
		*out_minimum_bits = 256;

	return 1;
}

static int
ssl_security_level_and_minimum_bits(const SSL_CTX *ctx, const SSL *ssl,
    int *out_level, int *out_minimum_bits)
{
	int security_level = 0, minimum_bits = 0;

	if (!ssl_security_normalize_level(ctx, ssl, &security_level))
		return 0;
	if (!ssl_security_level_to_minimum_bits(security_level, &minimum_bits))
		return 0;

	if (out_level != NULL)
		*out_level = security_level;
	if (out_minimum_bits != NULL)
		*out_minimum_bits = minimum_bits;

	return 1;
}

static int
ssl_security_secop_cipher(const SSL_CTX *ctx, const SSL *ssl, int bits,
    void *arg)
{
	const SSL_CIPHER *cipher = arg;
	int security_level, minimum_bits;

	if (!ssl_security_level_and_minimum_bits(ctx, ssl, &security_level,
	    &minimum_bits))
		return 0;

	if (security_level <= 0)
		return 1;

	if (bits < minimum_bits)
		return 0;

	/* No unauthenticated ciphersuites. */
	if (cipher->algorithm_auth & SSL_aNULL)
		return 0;

	if (cipher->algorithm_mac & SSL_MD5)
		return 0;

	if (security_level <= 1)
		return 1;

	if (cipher->algorithm_enc & SSL_RC4)
		return 0;

	if (security_level <= 2)
		return 1;

	/* Security level >= 3 requires a cipher with forward secrecy. */
	if ((cipher->algorithm_mkey & (SSL_kDHE | SSL_kECDHE)) == 0 &&
	    cipher->algorithm_ssl != SSL_TLSV1_3)
		return 0;

	if (security_level <= 3)
		return 1;

	if (cipher->algorithm_mac & SSL_SHA1)
		return 0;

	return 1;
}

static int
ssl_security_secop_version(const SSL_CTX *ctx, const SSL *ssl, int version)
{
	int min_version = TLS1_2_VERSION;
	int security_level;

	if (!ssl_security_level_and_minimum_bits(ctx, ssl, &security_level, NULL))
		return 0;

	if (security_level < 4)
		min_version = TLS1_1_VERSION;
	if (security_level < 3)
		min_version = TLS1_VERSION;

	return ssl_tls_version(version) >= min_version;
}

static int
ssl_security_secop_compression(const SSL_CTX *ctx, const SSL *ssl)
{
	return 0;
}

static int
ssl_security_secop_tickets(const SSL_CTX *ctx, const SSL *ssl)
{
	int security_level;

	if (!ssl_security_level_and_minimum_bits(ctx, ssl, &security_level, NULL))
		return 0;

	return security_level < 3;
}

static int
ssl_security_secop_tmp_dh(const SSL_CTX *ctx, const SSL *ssl, int bits)
{
	int security_level, minimum_bits;

	if (!ssl_security_level_and_minimum_bits(ctx, ssl, &security_level,
	    &minimum_bits))
		return 0;

	/* Disallow DHE keys weaker than 1024 bits even at security level 0. */
	if (security_level <= 0 && bits < 80)
		return 0;

	return bits >= minimum_bits;
}

static int
ssl_security_secop_default(const SSL_CTX *ctx, const SSL *ssl, int bits)
{
	int minimum_bits;

	if (!ssl_security_level_and_minimum_bits(ctx, ssl, NULL, &minimum_bits))
		return 0;

	return bits >= minimum_bits;
}

int
ssl_security_default_cb(const SSL *ssl, const SSL_CTX *ctx, int secop, int bits,
    int version, void *cipher, void *ex_data)
{
	switch (secop) {
	case SSL_SECOP_CIPHER_SUPPORTED:
	case SSL_SECOP_CIPHER_SHARED:
	case SSL_SECOP_CIPHER_CHECK:
		return ssl_security_secop_cipher(ctx, ssl, bits, cipher);
	case SSL_SECOP_VERSION:
		return ssl_security_secop_version(ctx, ssl, version);
	case SSL_SECOP_COMPRESSION:
		return ssl_security_secop_compression(ctx, ssl);
	case SSL_SECOP_TICKET:
		return ssl_security_secop_tickets(ctx, ssl);
	case SSL_SECOP_TMP_DH:
		return ssl_security_secop_tmp_dh(ctx, ssl, bits);
	default:
		return ssl_security_secop_default(ctx, ssl, bits);
	}
}

static int
ssl_ctx_security(const SSL_CTX *ctx, int secop, int bits, int nid, void *other)
{
	return ctx->cert->security_cb(NULL, ctx, secop, bits, nid,
	    other, ctx->cert->security_ex_data);
}

static int
ssl_security(const SSL *ssl, int secop, int bits, int nid, void *other)
{
	return ssl->cert->security_cb(ssl, NULL, secop, bits, nid, other,
	    ssl->cert->security_ex_data);
}

int
ssl_security_sigalg_check(const SSL *ssl, const EVP_PKEY *pkey)
{
	int bits;

	bits = EVP_PKEY_security_bits(pkey);

	return ssl_security(ssl, SSL_SECOP_SIGALG_CHECK, bits, 0, NULL);
}

int
ssl_security_tickets(const SSL *ssl)
{
	return ssl_security(ssl, SSL_SECOP_TICKET, 0, 0, NULL);
}

int
ssl_security_version(const SSL *ssl, int version)
{
	return ssl_security(ssl, SSL_SECOP_VERSION, 0, version, NULL);
}

static int
ssl_security_cipher(const SSL *ssl, SSL_CIPHER *cipher, int secop)
{
	return ssl_security(ssl, secop, cipher->strength_bits, 0, cipher);
}

int
ssl_security_cipher_check(const SSL *ssl, SSL_CIPHER *cipher)
{
	return ssl_security_cipher(ssl, cipher, SSL_SECOP_CIPHER_CHECK);
}

int
ssl_security_shared_cipher(const SSL *ssl, SSL_CIPHER *cipher)
{
	return ssl_security_cipher(ssl, cipher, SSL_SECOP_CIPHER_SHARED);
}

int
ssl_security_supported_cipher(const SSL *ssl, SSL_CIPHER *cipher)
{
	return ssl_security_cipher(ssl, cipher, SSL_SECOP_CIPHER_SUPPORTED);
}

int
ssl_ctx_security_dh(const SSL_CTX *ctx, DH *dh)
{
	int bits;

	bits = DH_security_bits(dh);

	return ssl_ctx_security(ctx, SSL_SECOP_TMP_DH, bits, 0, dh);
}

int
ssl_security_dh(const SSL *ssl, DH *dh)
{
	int bits;

	bits = DH_security_bits(dh);

	return ssl_security(ssl, SSL_SECOP_TMP_DH, bits, 0, dh);
}

static int
ssl_cert_pubkey_security_bits(const X509 *x509)
{
	EVP_PKEY *pkey;

	if ((pkey = X509_get0_pubkey(x509)) == NULL)
		return -1;

	return EVP_PKEY_security_bits(pkey);
}

static int
ssl_security_cert_key(const SSL_CTX *ctx, const SSL *ssl, X509 *x509, int secop)
{
	int security_bits;

	security_bits = ssl_cert_pubkey_security_bits(x509);

	if (ssl != NULL)
		return ssl_security(ssl, secop, security_bits, 0, x509);

	return ssl_ctx_security(ctx, secop, security_bits, 0, x509);
}

static int
ssl_security_cert_sig_security_bits(X509 *x509, int *out_md_nid)
{
	int pkey_nid, security_bits;
	uint32_t flags;

	*out_md_nid = NID_undef;

	/*
	 * Returning -1 security bits makes the default security callback fail
	 * to match bonkers behavior in OpenSSL. This in turn lets a security
	 * callback override such failures.
	 */
	if (!X509_get_signature_info(x509, out_md_nid, &pkey_nid, &security_bits,
	    &flags))
		return -1;
	/*
	 * OpenSSL doesn't check flags. Test RSA-PSS certs we were provided have
	 * a salt length distinct from hash length and thus fail this check.
	 */
	if ((flags & X509_SIG_INFO_TLS) == 0)
		return -1;

	/* Weird OpenSSL behavior only relevant for EdDSA certs in LibreSSL. */
	if (*out_md_nid == NID_undef)
		*out_md_nid = pkey_nid;

	return security_bits;
}

static int
ssl_security_cert_sig(const SSL_CTX *ctx, const SSL *ssl, X509 *x509, int secop)
{
	int md_nid = NID_undef, security_bits = -1;

	/* Don't check signature if self signed. */
	if ((X509_get_extension_flags(x509) & EXFLAG_SS) != 0)
		return 1;

	/*
	 * The default security callback fails on -1 security bits. It ignores
	 * the md_nid (aka version) argument we pass from here.
	 */
	security_bits = ssl_security_cert_sig_security_bits(x509, &md_nid);

	if (ssl != NULL)
		return ssl_security(ssl, secop, security_bits, md_nid, x509);

	return ssl_ctx_security(ctx, secop, security_bits, md_nid, x509);
}

int
ssl_security_cert(const SSL_CTX *ctx, const SSL *ssl, X509 *x509,
    int is_ee, int *out_error)
{
	int key_error, operation;

	*out_error = 0;

	if (is_ee) {
		operation = SSL_SECOP_EE_KEY;
		key_error = SSL_R_EE_KEY_TOO_SMALL;
	} else {
		operation = SSL_SECOP_CA_KEY;
		key_error = SSL_R_CA_KEY_TOO_SMALL;
	}

	if (!ssl_security_cert_key(ctx, ssl, x509, operation)) {
		*out_error = key_error;
		return 0;
	}

	if (!ssl_security_cert_sig(ctx, ssl, x509, SSL_SECOP_CA_MD)) {
		*out_error = SSL_R_CA_MD_TOO_WEAK;
		return 0;
	}

	return 1;
}

/*
 * Check security of a chain. If |sk| includes the end entity certificate
 * then |x509| must be NULL.
 */
int
ssl_security_cert_chain(const SSL *ssl, STACK_OF(X509) *sk, X509 *x509,
    int *out_error)
{
	int start_idx = 0;
	int is_ee;
	int i;

	if (x509 == NULL) {
		x509 = sk_X509_value(sk, 0);
		start_idx = 1;
	}

	is_ee = 1;
	if (!ssl_security_cert(NULL, ssl, x509, is_ee, out_error))
		return 0;

	is_ee = 0;
	for (i = start_idx; i < sk_X509_num(sk); i++) {
		x509 = sk_X509_value(sk, i);

		if (!ssl_security_cert(NULL, ssl, x509, is_ee, out_error))
			return 0;
	}

	return 1;
}

static int
ssl_security_group(const SSL *ssl, uint16_t group_id, int secop)
{
	CBB cbb;
	int bits, nid;
	uint8_t group[2];

	memset(&cbb, 0, sizeof(cbb));

	if (!tls1_ec_group_id2bits(group_id, &bits))
		goto err;
	if (!tls1_ec_group_id2nid(group_id, &nid))
		goto err;

	if (!CBB_init_fixed(&cbb, group, sizeof(group)))
		goto err;
	if (!CBB_add_u16(&cbb, group_id))
		goto err;
	if (!CBB_finish(&cbb, NULL, NULL))
		goto err;

	return ssl_security(ssl, secop, bits, nid, group);

 err:
	CBB_cleanup(&cbb);

	return 0;
}

int
ssl_security_shared_group(const SSL *ssl, uint16_t group_id)
{
	return ssl_security_group(ssl, group_id, SSL_SECOP_CURVE_SHARED);
}

int
ssl_security_supported_group(const SSL *ssl, uint16_t group_id)
{
	return ssl_security_group(ssl, group_id, SSL_SECOP_CURVE_SUPPORTED);
}
