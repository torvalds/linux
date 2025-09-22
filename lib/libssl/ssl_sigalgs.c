/* $OpenBSD: ssl_sigalgs.c,v 1.50 2024/07/09 13:43:57 beck Exp $ */
/*
 * Copyright (c) 2018-2020 Bob Beck <beck@openbsd.org>
 * Copyright (c) 2021 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <string.h>
#include <stdlib.h>

#include <openssl/evp.h>
#include <openssl/opensslconf.h>

#include "bytestring.h"
#include "ssl_local.h"
#include "ssl_sigalgs.h"
#include "tls13_internal.h"

const struct ssl_sigalg sigalgs[] = {
	{
		.value = SIGALG_RSA_PKCS1_SHA512,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha512,
		.security_level = 5,
	},
	{
		.value = SIGALG_ECDSA_SECP521R1_SHA512,
		.key_type = EVP_PKEY_EC,
		.md = EVP_sha512,
		.security_level = 5,
		.group_nid = NID_secp521r1,
	},
	{
		.value = SIGALG_RSA_PKCS1_SHA384,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha384,
		.security_level = 4,
	},
	{
		.value = SIGALG_ECDSA_SECP384R1_SHA384,
		.key_type = EVP_PKEY_EC,
		.md = EVP_sha384,
		.security_level = 4,
		.group_nid = NID_secp384r1,
	},
	{
		.value = SIGALG_RSA_PKCS1_SHA256,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha256,
		.security_level = 3,
	},
	{
		.value = SIGALG_ECDSA_SECP256R1_SHA256,
		.key_type = EVP_PKEY_EC,
		.md = EVP_sha256,
		.security_level = 3,
		.group_nid = NID_X9_62_prime256v1,
	},
	{
		.value = SIGALG_RSA_PSS_RSAE_SHA256,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha256,
		.security_level = 3,
		.flags = SIGALG_FLAG_RSA_PSS,
	},
	{
		.value = SIGALG_RSA_PSS_RSAE_SHA384,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha384,
		.security_level = 4,
		.flags = SIGALG_FLAG_RSA_PSS,
	},
	{
		.value = SIGALG_RSA_PSS_RSAE_SHA512,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha512,
		.security_level = 5,
		.flags = SIGALG_FLAG_RSA_PSS,
	},
	{
		.value = SIGALG_RSA_PSS_PSS_SHA256,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha256,
		.security_level = 3,
		.flags = SIGALG_FLAG_RSA_PSS,
	},
	{
		.value = SIGALG_RSA_PSS_PSS_SHA384,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha384,
		.security_level = 4,
		.flags = SIGALG_FLAG_RSA_PSS,
	},
	{
		.value = SIGALG_RSA_PSS_PSS_SHA512,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha512,
		.security_level = 5,
		.flags = SIGALG_FLAG_RSA_PSS,
	},
	{
		.value = SIGALG_RSA_PKCS1_SHA224,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha224,
		.security_level = 2,
	},
	{
		.value = SIGALG_ECDSA_SECP224R1_SHA224,
		.key_type = EVP_PKEY_EC,
		.md = EVP_sha224,
		.security_level = 2,
	},
	{
		.value = SIGALG_RSA_PKCS1_SHA1,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_sha1,
		.security_level = 1,
	},
	{
		.value = SIGALG_ECDSA_SHA1,
		.key_type = EVP_PKEY_EC,
		.md = EVP_sha1,
		.security_level = 1,
	},
	{
		.value = SIGALG_RSA_PKCS1_MD5_SHA1,
		.key_type = EVP_PKEY_RSA,
		.md = EVP_md5_sha1,
		.security_level = 1,
	},
	{
		.value = SIGALG_NONE,
	},
};

/* Sigalgs for TLSv1.3, in preference order. */
const uint16_t tls13_sigalgs[] = {
	SIGALG_RSA_PSS_RSAE_SHA512,
	SIGALG_RSA_PKCS1_SHA512,
	SIGALG_ECDSA_SECP521R1_SHA512,
	SIGALG_RSA_PSS_RSAE_SHA384,
	SIGALG_RSA_PKCS1_SHA384,
	SIGALG_ECDSA_SECP384R1_SHA384,
	SIGALG_RSA_PSS_RSAE_SHA256,
	SIGALG_RSA_PKCS1_SHA256,
	SIGALG_ECDSA_SECP256R1_SHA256,
};
const size_t tls13_sigalgs_len = (sizeof(tls13_sigalgs) / sizeof(tls13_sigalgs[0]));

/* Sigalgs for TLSv1.2, in preference order. */
const uint16_t tls12_sigalgs[] = {
	SIGALG_RSA_PSS_RSAE_SHA512,
	SIGALG_RSA_PKCS1_SHA512,
	SIGALG_ECDSA_SECP521R1_SHA512,
	SIGALG_RSA_PSS_RSAE_SHA384,
	SIGALG_RSA_PKCS1_SHA384,
	SIGALG_ECDSA_SECP384R1_SHA384,
	SIGALG_RSA_PSS_RSAE_SHA256,
	SIGALG_RSA_PKCS1_SHA256,
	SIGALG_ECDSA_SECP256R1_SHA256,
	SIGALG_RSA_PKCS1_SHA1, /* XXX */
	SIGALG_ECDSA_SHA1,     /* XXX */
};
const size_t tls12_sigalgs_len = (sizeof(tls12_sigalgs) / sizeof(tls12_sigalgs[0]));

static void
ssl_sigalgs_for_version(uint16_t tls_version, const uint16_t **out_values,
    size_t *out_len)
{
	if (tls_version >= TLS1_3_VERSION) {
		*out_values = tls13_sigalgs;
		*out_len = tls13_sigalgs_len;
	} else {
		*out_values = tls12_sigalgs;
		*out_len = tls12_sigalgs_len;
	}
}

static const struct ssl_sigalg *
ssl_sigalg_lookup(uint16_t value)
{
	int i;

	for (i = 0; sigalgs[i].value != SIGALG_NONE; i++) {
		if (sigalgs[i].value == value)
			return &sigalgs[i];
	}

	return NULL;
}

static const struct ssl_sigalg *
ssl_sigalg_from_value(SSL *s, uint16_t value)
{
	const uint16_t *values;
	size_t len;
	int i;

	ssl_sigalgs_for_version(s->s3->hs.negotiated_tls_version,
	    &values, &len);

	for (i = 0; i < len; i++) {
		if (values[i] == value)
			return ssl_sigalg_lookup(value);
	}

	return NULL;
}

int
ssl_sigalgs_build(uint16_t tls_version, CBB *cbb, int security_level)
{
	const struct ssl_sigalg *sigalg;
	const uint16_t *values;
	size_t len;
	size_t i;
	int ret = 0;

	ssl_sigalgs_for_version(tls_version, &values, &len);

	/* Add values in order as long as they are supported. */
	for (i = 0; i < len; i++) {
		/* Do not allow the legacy value for < 1.2 to be used. */
		if (values[i] == SIGALG_RSA_PKCS1_MD5_SHA1)
			return 0;
		if ((sigalg = ssl_sigalg_lookup(values[i])) == NULL)
			return 0;
		if (sigalg->security_level < security_level)
			continue;

		if (!CBB_add_u16(cbb, values[i]))
			return 0;

		ret = 1;
	}
	return ret;
}

static const struct ssl_sigalg *
ssl_sigalg_for_legacy(SSL *s, EVP_PKEY *pkey)
{
	if (SSL_get_security_level(s) > 1)
		return NULL;

	/* Default signature algorithms used for TLSv1.2 and earlier. */
	switch (EVP_PKEY_id(pkey)) {
	case EVP_PKEY_RSA:
		if (s->s3->hs.negotiated_tls_version < TLS1_2_VERSION)
			return ssl_sigalg_lookup(SIGALG_RSA_PKCS1_MD5_SHA1);
		return ssl_sigalg_lookup(SIGALG_RSA_PKCS1_SHA1);
	case EVP_PKEY_EC:
		return ssl_sigalg_lookup(SIGALG_ECDSA_SHA1);
	}
	SSLerror(s, SSL_R_UNKNOWN_PKEY_TYPE);
	return NULL;
}

static int
ssl_sigalg_pkey_ok(SSL *s, const struct ssl_sigalg *sigalg, EVP_PKEY *pkey)
{
	if (sigalg == NULL || pkey == NULL)
		return 0;
	if (sigalg->key_type != EVP_PKEY_id(pkey))
		return 0;

	/* RSA PSS must have a sufficiently large RSA key. */
	if ((sigalg->flags & SIGALG_FLAG_RSA_PSS)) {
		if (EVP_PKEY_id(pkey) != EVP_PKEY_RSA ||
		    EVP_PKEY_size(pkey) < (2 * EVP_MD_size(sigalg->md()) + 2))
			return 0;
	}

	if (!ssl_security_sigalg_check(s, pkey))
		return 0;

	if (s->s3->hs.negotiated_tls_version < TLS1_3_VERSION)
		return 1;

	/* RSA cannot be used without PSS in TLSv1.3. */
	if (sigalg->key_type == EVP_PKEY_RSA &&
	    (sigalg->flags & SIGALG_FLAG_RSA_PSS) == 0)
		return 0;

	/* Ensure that group matches for EC keys. */
	if (EVP_PKEY_id(pkey) == EVP_PKEY_EC) {
		if (sigalg->group_nid == 0)
			return 0;
		if (EC_GROUP_get_curve_name(EC_KEY_get0_group(
		    EVP_PKEY_get0_EC_KEY(pkey))) != sigalg->group_nid)
			return 0;
	}

	return 1;
}

const struct ssl_sigalg *
ssl_sigalg_select(SSL *s, EVP_PKEY *pkey)
{
	CBS cbs;

	if (!SSL_USE_SIGALGS(s))
		return ssl_sigalg_for_legacy(s, pkey);

	/*
	 * RFC 5246 allows a TLS 1.2 client to send no sigalgs extension,
	 * in which case the server must use the default.
	 */
	if (s->s3->hs.negotiated_tls_version < TLS1_3_VERSION &&
	    s->s3->hs.sigalgs == NULL)
		return ssl_sigalg_for_legacy(s, pkey);

	/*
	 * If we get here, we have client or server sent sigalgs, use one.
	 */
	CBS_init(&cbs, s->s3->hs.sigalgs, s->s3->hs.sigalgs_len);
	while (CBS_len(&cbs) > 0) {
		const struct ssl_sigalg *sigalg;
		uint16_t sigalg_value;

		if (!CBS_get_u16(&cbs, &sigalg_value))
			return NULL;

		if ((sigalg = ssl_sigalg_from_value(s, sigalg_value)) == NULL)
			continue;
		if (ssl_sigalg_pkey_ok(s, sigalg, pkey))
			return sigalg;
	}

	return NULL;
}

const struct ssl_sigalg *
ssl_sigalg_for_peer(SSL *s, EVP_PKEY *pkey, uint16_t sigalg_value)
{
	const struct ssl_sigalg *sigalg;

	if (!SSL_USE_SIGALGS(s))
		return ssl_sigalg_for_legacy(s, pkey);

	if ((sigalg = ssl_sigalg_from_value(s, sigalg_value)) == NULL) {
		SSLerror(s, SSL_R_UNKNOWN_DIGEST);
		return NULL;
	}
	if (!ssl_sigalg_pkey_ok(s, sigalg, pkey)) {
		SSLerror(s, SSL_R_WRONG_SIGNATURE_TYPE);
		return NULL;
	}

	return sigalg;
}
