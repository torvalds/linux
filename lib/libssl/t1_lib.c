/* $OpenBSD: t1_lib.c,v 1.206 2025/05/31 15:17:11 tb Exp $ */
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

#include <stdio.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/objects.h>
#include <openssl/ocsp.h>

#include "bytestring.h"
#include "ssl_local.h"
#include "ssl_sigalgs.h"
#include "ssl_tlsext.h"

static int tls_decrypt_ticket(SSL *s, CBS *ticket, int *alert,
    SSL_SESSION **psess);

int
tls1_new(SSL *s)
{
	if (!ssl3_new(s))
		return 0;
	s->method->ssl_clear(s);
	return 1;
}

void
tls1_free(SSL *s)
{
	if (s == NULL)
		return;

	free(s->tlsext_session_ticket);
	ssl3_free(s);
}

void
tls1_clear(SSL *s)
{
	ssl3_clear(s);
	s->version = s->method->version;
}

struct supported_group {
	uint16_t group_id;
	int nid;
	int bits;
};

/*
 * Supported groups (formerly known as named curves)
 * https://www.iana.org/assignments/tls-parameters/#tls-parameters-8
 */
static const struct supported_group nid_list[] = {
	{
		.group_id = 1,
		.nid = NID_sect163k1,
		.bits = 80,
	},
	{
		.group_id = 2,
		.nid = NID_sect163r1,
		.bits = 80,
	},
	{
		.group_id = 3,
		.nid = NID_sect163r2,
		.bits = 80,
	},
	{
		.group_id = 4,
		.nid = NID_sect193r1,
		.bits = 80,
	},
	{
		.group_id = 5,
		.nid = NID_sect193r2,
		.bits = 80,
	},
	{
		.group_id = 6,
		.nid = NID_sect233k1,
		.bits = 112,
	},
	{
		.group_id = 7,
		.nid = NID_sect233r1,
		.bits = 112,
	},
	{
		.group_id = 8,
		.nid = NID_sect239k1,
		.bits = 112,
	},
	{
		.group_id = 9,
		.nid = NID_sect283k1,
		.bits = 128,
	},
	{
		.group_id = 10,
		.nid = NID_sect283r1,
		.bits = 128,
	},
	{
		.group_id = 11,
		.nid = NID_sect409k1,
		.bits = 192,
	},
	{
		.group_id = 12,
		.nid = NID_sect409r1,
		.bits = 192,
	},
	{
		.group_id = 13,
		.nid = NID_sect571k1,
		.bits = 256,
	},
	{
		.group_id = 14,
		.nid = NID_sect571r1,
		.bits = 256,
	},
	{
		.group_id = 15,
		.nid = NID_secp160k1,
		.bits = 80,
	},
	{
		.group_id = 16,
		.nid = NID_secp160r1,
		.bits = 80,
	},
	{
		.group_id = 17,
		.nid = NID_secp160r2,
		.bits = 80,
	},
	{
		.group_id = 18,
		.nid = NID_secp192k1,
		.bits = 80,
	},
	{
		.group_id = 19,
		.nid = NID_X9_62_prime192v1,	/* aka secp192r1 */
		.bits = 80,
	},
	{
		.group_id = 20,
		.nid = NID_secp224k1,
		.bits = 112,
	},
	{
		.group_id = 21,
		.nid = NID_secp224r1,
		.bits = 112,
	},
	{
		.group_id = 22,
		.nid = NID_secp256k1,
		.bits = 128,
	},
	{
		.group_id = 23,
		.nid = NID_X9_62_prime256v1,	/* aka secp256r1 */
		.bits = 128,
	},
	{
		.group_id = 24,
		.nid = NID_secp384r1,
		.bits = 192,
	},
	{
		.group_id = 25,
		.nid = NID_secp521r1,
		.bits = 256,
	},
	{
		.group_id = 26,
		.nid = NID_brainpoolP256r1,
		.bits = 128,
	},
	{
		.group_id = 27,
		.nid = NID_brainpoolP384r1,
		.bits = 192,
	},
	{
		.group_id = 28,
		.nid = NID_brainpoolP512r1,
		.bits = 256,
	},
	{
		.group_id = 29,
		.nid = NID_X25519,
		.bits = 128,
	},
};

#define NID_LIST_LEN (sizeof(nid_list) / sizeof(nid_list[0]))

#if 0
static const uint8_t ecformats_list[] = {
	TLSEXT_ECPOINTFORMAT_uncompressed,
	TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime,
	TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2
};
#endif

static const uint8_t ecformats_default[] = {
	TLSEXT_ECPOINTFORMAT_uncompressed,
};

#if 0
static const uint16_t ecgroups_list[] = {
	29,			/* X25519 (29) */
	14,			/* sect571r1 (14) */
	13,			/* sect571k1 (13) */
	25,			/* secp521r1 (25) */
	28,			/* brainpoolP512r1 (28) */
	11,			/* sect409k1 (11) */
	12,			/* sect409r1 (12) */
	27,			/* brainpoolP384r1 (27) */
	24,			/* secp384r1 (24) */
	9,			/* sect283k1 (9) */
	10,			/* sect283r1 (10) */
	26,			/* brainpoolP256r1 (26) */
	22,			/* secp256k1 (22) */
	23,			/* secp256r1 (23) */
	8,			/* sect239k1 (8) */
	6,			/* sect233k1 (6) */
	7,			/* sect233r1 (7) */
	20,			/* secp224k1 (20) */
	21,			/* secp224r1 (21) */
	4,			/* sect193r1 (4) */
	5,			/* sect193r2 (5) */
	18,			/* secp192k1 (18) */
	19,			/* secp192r1 (19) */
	1,			/* sect163k1 (1) */
	2,			/* sect163r1 (2) */
	3,			/* sect163r2 (3) */
	15,			/* secp160k1 (15) */
	16,			/* secp160r1 (16) */
	17,			/* secp160r2 (17) */
};
#endif

static const uint16_t ecgroups_client_default[] = {
	29,			/* X25519 (29) */
	23,			/* secp256r1 (23) */
	24,			/* secp384r1 (24) */
	25,			/* secp521r1 (25) */
};

static const uint16_t ecgroups_server_default[] = {
	29,			/* X25519 (29) */
	23,			/* secp256r1 (23) */
	24,			/* secp384r1 (24) */
};

static const struct supported_group *
tls1_supported_group_by_id(uint16_t group_id)
{
	int i;

	for (i = 0; i < NID_LIST_LEN; i++) {
		if (group_id == nid_list[i].group_id)
			return &nid_list[i];
	}

	return NULL;
}

static const struct supported_group *
tls1_supported_group_by_nid(int nid)
{
	int i;

	for (i = 0; i < NID_LIST_LEN; i++) {
		if (nid == nid_list[i].nid)
			return &nid_list[i];
	}

	return NULL;
}

int
tls1_ec_group_id2nid(uint16_t group_id, int *out_nid)
{
	const struct supported_group *sg;

	if ((sg = tls1_supported_group_by_id(group_id)) == NULL)
		return 0;

	*out_nid = sg->nid;

	return 1;
}

int
tls1_ec_group_id2bits(uint16_t group_id, int *out_bits)
{
	const struct supported_group *sg;

	if ((sg = tls1_supported_group_by_id(group_id)) == NULL)
		return 0;

	*out_bits = sg->bits;

	return 1;
}

int
tls1_ec_nid2group_id(int nid, uint16_t *out_group_id)
{
	const struct supported_group *sg;

	if ((sg = tls1_supported_group_by_nid(nid)) == NULL)
		return 0;

	*out_group_id = sg->group_id;

	return 1;
}

/*
 * Return the appropriate format list. If client_formats is non-zero, return
 * the client/session formats. Otherwise return the custom format list if one
 * exists, or the default formats if a custom list has not been specified.
 */
void
tls1_get_formatlist(const SSL *s, int client_formats, const uint8_t **pformats,
    size_t *pformatslen)
{
	if (client_formats != 0) {
		*pformats = s->session->tlsext_ecpointformatlist;
		*pformatslen = s->session->tlsext_ecpointformatlist_length;
		return;
	}

	*pformats = s->tlsext_ecpointformatlist;
	*pformatslen = s->tlsext_ecpointformatlist_length;
	if (*pformats == NULL) {
		*pformats = ecformats_default;
		*pformatslen = sizeof(ecformats_default);
	}
}

/*
 * Return the appropriate group list. If client_groups is non-zero, return
 * the client/session groups. Otherwise return the custom group list if one
 * exists, or the default groups if a custom list has not been specified.
 */
void
tls1_get_group_list(const SSL *s, int client_groups, const uint16_t **pgroups,
    size_t *pgroupslen)
{
	if (client_groups != 0) {
		*pgroups = s->session->tlsext_supportedgroups;
		*pgroupslen = s->session->tlsext_supportedgroups_length;
		return;
	}

	*pgroups = s->tlsext_supportedgroups;
	*pgroupslen = s->tlsext_supportedgroups_length;
	if (*pgroups != NULL)
		return;

	if (!s->server) {
		*pgroups = ecgroups_client_default;
		*pgroupslen = sizeof(ecgroups_client_default) / 2;
	} else {
		*pgroups = ecgroups_server_default;
		*pgroupslen = sizeof(ecgroups_server_default) / 2;
	}
}

static int
tls1_get_group_lists(const SSL *ssl, const uint16_t **pref, size_t *preflen,
    const uint16_t **supp, size_t *supplen)
{
	unsigned long server_pref;

	/* Cannot do anything on the client side. */
	if (!ssl->server)
		return 0;

	server_pref = (ssl->options & SSL_OP_CIPHER_SERVER_PREFERENCE);
	tls1_get_group_list(ssl, (server_pref == 0), pref, preflen);
	tls1_get_group_list(ssl, (server_pref != 0), supp, supplen);

	return 1;
}

static int
tls1_group_id_present(uint16_t group_id, const uint16_t *list, size_t list_len)
{
	size_t i;

	for (i = 0; i < list_len; i++) {
		if (group_id == list[i])
			return 1;
	}

	return 0;
}

int
tls1_count_shared_groups(const SSL *ssl, size_t *out_count)
{
	size_t count, preflen, supplen, i;
	const uint16_t *pref, *supp;

	if (!tls1_get_group_lists(ssl, &pref, &preflen, &supp, &supplen))
		return 0;

	count = 0;
	for (i = 0; i < preflen; i++) {
		if (!tls1_group_id_present(pref[i], supp, supplen))
			continue;

		if (!ssl_security_shared_group(ssl, pref[i]))
			continue;

		count++;
	}

	*out_count = count;

	return 1;
}

static int
tls1_group_by_index(const SSL *ssl, size_t n, int *out_nid,
    int (*ssl_security_fn)(const SSL *, uint16_t))
{
	size_t count, preflen, supplen, i;
	const uint16_t *pref, *supp;

	if (!tls1_get_group_lists(ssl, &pref, &preflen, &supp, &supplen))
		return 0;

	count = 0;
	for (i = 0; i < preflen; i++) {
		if (!tls1_group_id_present(pref[i], supp, supplen))
			continue;

		if (!ssl_security_fn(ssl, pref[i]))
			continue;

		if (count++ == n)
			return tls1_ec_group_id2nid(pref[i], out_nid);
	}

	return 0;
}

int
tls1_get_shared_group_by_index(const SSL *ssl, size_t index, int *out_nid)
{
	return tls1_group_by_index(ssl, index, out_nid,
	    ssl_security_shared_group);
}

int
tls1_get_supported_group(const SSL *ssl, int *out_nid)
{
	return tls1_group_by_index(ssl, 0, out_nid,
	    ssl_security_supported_group);
}

int
tls1_set_groups(uint16_t **out_group_ids, size_t *out_group_ids_len,
    const int *groups, size_t ngroups)
{
	uint16_t *group_ids;
	size_t i;

	if ((group_ids = calloc(ngroups, sizeof(uint16_t))) == NULL)
		return 0;

	for (i = 0; i < ngroups; i++) {
		if (!tls1_ec_nid2group_id(groups[i], &group_ids[i])) {
			free(group_ids);
			return 0;
		}
	}

	free(*out_group_ids);
	*out_group_ids = group_ids;
	*out_group_ids_len = ngroups;

	return 1;
}

int
tls1_set_group_list(uint16_t **out_group_ids, size_t *out_group_ids_len,
    const char *groups)
{
	uint16_t *new_group_ids, *group_ids = NULL;
	size_t ngroups = 0;
	char *gs, *p, *q;
	int nid;

	if ((gs = strdup(groups)) == NULL)
		return 0;

	q = gs;
	while ((p = strsep(&q, ":")) != NULL) {
		nid = OBJ_sn2nid(p);
		if (nid == NID_undef)
			nid = OBJ_ln2nid(p);
		if (nid == NID_undef)
			nid = EC_curve_nist2nid(p);
		if (nid == NID_undef)
			goto err;

		if ((new_group_ids = reallocarray(group_ids, ngroups + 1,
		    sizeof(uint16_t))) == NULL)
			goto err;
		group_ids = new_group_ids;

		if (!tls1_ec_nid2group_id(nid, &group_ids[ngroups]))
			goto err;

		ngroups++;
	}

	free(gs);
	free(*out_group_ids);
	*out_group_ids = group_ids;
	*out_group_ids_len = ngroups;

	return 1;

 err:
	free(gs);
	free(group_ids);

	return 0;
}

/* Check that a group is one of our preferences. */
int
tls1_check_group(SSL *s, uint16_t group_id)
{
	const uint16_t *groups;
	size_t groupslen, i;

	tls1_get_group_list(s, 0, &groups, &groupslen);

	for (i = 0; i < groupslen; i++) {
		if (!ssl_security_supported_group(s, groups[i]))
			continue;
		if (groups[i] == group_id)
			return 1;
	}
	return 0;
}

/* For an EC key set TLS ID and required compression based on parameters. */
static int
tls1_set_ec_id(uint16_t *group_id, uint8_t *comp_id, EC_KEY *ec)
{
	const EC_GROUP *group;
	int nid;

	if ((group = EC_KEY_get0_group(ec)) == NULL)
		return 0;

	/* Determine group ID. */
	nid = EC_GROUP_get_curve_name(group);
	if (!tls1_ec_nid2group_id(nid, group_id))
		return 0;

	/* Specify the compression identifier. */
	if (EC_KEY_get0_public_key(ec) == NULL)
		return 0;
	*comp_id = TLSEXT_ECPOINTFORMAT_uncompressed;
	if (EC_KEY_get_conv_form(ec) == POINT_CONVERSION_COMPRESSED) {
		*comp_id = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime;
	}

	return 1;
}

/* Check that an EC key is compatible with extensions. */
static int
tls1_check_ec_key(SSL *s, const uint16_t group_id, const uint8_t comp_id)
{
	size_t groupslen, formatslen, i;
	const uint16_t *groups;
	const uint8_t *formats;

	/*
	 * Check point formats extension if present, otherwise everything
	 * is supported (see RFC4492).
	 */
	tls1_get_formatlist(s, 1, &formats, &formatslen);
	if (formats != NULL) {
		for (i = 0; i < formatslen; i++) {
			if (formats[i] == comp_id)
				break;
		}
		if (i == formatslen)
			return 0;
	}

	/*
	 * Check group list if present, otherwise everything is supported.
	 */
	tls1_get_group_list(s, 1, &groups, &groupslen);
	if (groups != NULL) {
		for (i = 0; i < groupslen; i++) {
			if (groups[i] == group_id)
				break;
		}
		if (i == groupslen)
			return 0;
	}

	return 1;
}

/* Check EC server key is compatible with client extensions. */
int
tls1_check_ec_server_key(SSL *s)
{
	SSL_CERT_PKEY *cpk = s->cert->pkeys + SSL_PKEY_ECC;
	uint16_t group_id;
	uint8_t comp_id;
	EC_KEY *eckey;
	EVP_PKEY *pkey;

	if (cpk->x509 == NULL || cpk->privatekey == NULL)
		return 0;
	if ((pkey = X509_get0_pubkey(cpk->x509)) == NULL)
		return 0;
	if ((eckey = EVP_PKEY_get0_EC_KEY(pkey)) == NULL)
		return 0;
	if (!tls1_set_ec_id(&group_id, &comp_id, eckey))
		return 0;

	return tls1_check_ec_key(s, group_id, comp_id);
}

int
ssl_check_clienthello_tlsext_early(SSL *s)
{
	int ret = SSL_TLSEXT_ERR_NOACK;
	int al = SSL_AD_UNRECOGNIZED_NAME;

	/* The handling of the ECPointFormats extension is done elsewhere, namely in
	 * ssl3_choose_cipher in s3_lib.c.
	 */
	/* The handling of the EllipticCurves extension is done elsewhere, namely in
	 * ssl3_choose_cipher in s3_lib.c.
	 */

	if (s->ctx != NULL && s->ctx->tlsext_servername_callback != 0)
		ret = s->ctx->tlsext_servername_callback(s, &al,
		    s->ctx->tlsext_servername_arg);
	else if (s->initial_ctx != NULL && s->initial_ctx->tlsext_servername_callback != 0)
		ret = s->initial_ctx->tlsext_servername_callback(s, &al,
		    s->initial_ctx->tlsext_servername_arg);

	switch (ret) {
	case SSL_TLSEXT_ERR_ALERT_FATAL:
		ssl3_send_alert(s, SSL3_AL_FATAL, al);
		return -1;
	case SSL_TLSEXT_ERR_ALERT_WARNING:
		ssl3_send_alert(s, SSL3_AL_WARNING, al);
		return 1;
	case SSL_TLSEXT_ERR_NOACK:
	default:
		return 1;
	}
}

int
ssl_check_clienthello_tlsext_late(SSL *s)
{
	int ret = SSL_TLSEXT_ERR_OK;
	int al = 0;	/* XXX gcc3 */

	/* If status request then ask callback what to do.
 	 * Note: this must be called after servername callbacks in case
 	 * the certificate has changed, and must be called after the cipher
	 * has been chosen because this may influence which certificate is sent
 	 */
	if ((s->tlsext_status_type != -1) &&
	    s->ctx && s->ctx->tlsext_status_cb) {
		int r;
		SSL_CERT_PKEY *certpkey;
		certpkey = ssl_get_server_send_pkey(s);
		/* If no certificate can't return certificate status */
		if (certpkey == NULL) {
			s->tlsext_status_expected = 0;
			return 1;
		}
		/* Set current certificate to one we will use so
		 * SSL_get_certificate et al can pick it up.
		 */
		s->cert->key = certpkey;
		r = s->ctx->tlsext_status_cb(s,
		    s->ctx->tlsext_status_arg);
		switch (r) {
			/* We don't want to send a status request response */
		case SSL_TLSEXT_ERR_NOACK:
			s->tlsext_status_expected = 0;
			break;
			/* status request response should be sent */
		case SSL_TLSEXT_ERR_OK:
			if (s->tlsext_ocsp_resp)
				s->tlsext_status_expected = 1;
			else
				s->tlsext_status_expected = 0;
			break;
			/* something bad happened */
		case SSL_TLSEXT_ERR_ALERT_FATAL:
			ret = SSL_TLSEXT_ERR_ALERT_FATAL;
			al = SSL_AD_INTERNAL_ERROR;
			goto err;
		}
	} else
		s->tlsext_status_expected = 0;

 err:
	switch (ret) {
	case SSL_TLSEXT_ERR_ALERT_FATAL:
		ssl3_send_alert(s, SSL3_AL_FATAL, al);
		return -1;
	case SSL_TLSEXT_ERR_ALERT_WARNING:
		ssl3_send_alert(s, SSL3_AL_WARNING, al);
		return 1;
	default:
		return 1;
	}
}

int
ssl_check_serverhello_tlsext(SSL *s)
{
	int ret = SSL_TLSEXT_ERR_NOACK;
	int al = SSL_AD_UNRECOGNIZED_NAME;

	ret = SSL_TLSEXT_ERR_OK;

	if (s->ctx != NULL && s->ctx->tlsext_servername_callback != 0)
		ret = s->ctx->tlsext_servername_callback(s, &al,
		    s->ctx->tlsext_servername_arg);
	else if (s->initial_ctx != NULL && s->initial_ctx->tlsext_servername_callback != 0)
		ret = s->initial_ctx->tlsext_servername_callback(s, &al,
		    s->initial_ctx->tlsext_servername_arg);

	/* If we've requested certificate status and we wont get one
 	 * tell the callback
 	 */
	if ((s->tlsext_status_type != -1) && !(s->tlsext_status_expected) &&
	    s->ctx && s->ctx->tlsext_status_cb) {
		int r;

		free(s->tlsext_ocsp_resp);
		s->tlsext_ocsp_resp = NULL;
		s->tlsext_ocsp_resp_len = 0;

		r = s->ctx->tlsext_status_cb(s,
		    s->ctx->tlsext_status_arg);
		if (r == 0) {
			al = SSL_AD_BAD_CERTIFICATE_STATUS_RESPONSE;
			ret = SSL_TLSEXT_ERR_ALERT_FATAL;
		}
		if (r < 0) {
			al = SSL_AD_INTERNAL_ERROR;
			ret = SSL_TLSEXT_ERR_ALERT_FATAL;
		}
	}

	switch (ret) {
	case SSL_TLSEXT_ERR_ALERT_FATAL:
		ssl3_send_alert(s, SSL3_AL_FATAL, al);
		return -1;
	case SSL_TLSEXT_ERR_ALERT_WARNING:
		ssl3_send_alert(s, SSL3_AL_WARNING, al);
		return 1;
	case SSL_TLSEXT_ERR_NOACK:
	default:
		return 1;
	}
}

/* Since the server cache lookup is done early on in the processing of the
 * ClientHello, and other operations depend on the result, we need to handle
 * any TLS session ticket extension at the same time.
 *
 *   ext_block: a CBS for the ClientHello extensions block.
 *   ret: (output) on return, if a ticket was decrypted, then this is set to
 *       point to the resulting session.
 *
 * If s->tls_session_secret_cb is set then we are expecting a pre-shared key
 * ciphersuite, in which case we have no use for session tickets and one will
 * never be decrypted, nor will s->tlsext_ticket_expected be set to 1.
 *
 * Returns:
 *    TLS1_TICKET_FATAL_ERROR: error from parsing or decrypting the ticket.
 *    TLS1_TICKET_NONE: no ticket was found (or was ignored, based on settings).
 *    TLS1_TICKET_EMPTY: a zero length extension was found, indicating that the
 *       client supports session tickets but doesn't currently have one to offer.
 *    TLS1_TICKET_NOT_DECRYPTED: either s->tls_session_secret_cb was
 *       set, or a ticket was offered but couldn't be decrypted because of a
 *       non-fatal error.
 *    TLS1_TICKET_DECRYPTED: a ticket was successfully decrypted and *ret was set.
 *
 * Side effects:
 *   Sets s->tlsext_ticket_expected to 1 if the server will have to issue
 *   a new session ticket to the client because the client indicated support
 *   (and s->tls_session_secret_cb is NULL) but the client either doesn't have
 *   a session ticket or we couldn't use the one it gave us, or if
 *   s->ctx->tlsext_ticket_key_cb asked to renew the client's ticket.
 *   Otherwise, s->tlsext_ticket_expected is set to 0.
 */
int
tls1_process_ticket(SSL *s, CBS *ext_block, int *alert, SSL_SESSION **ret)
{
	CBS extensions, ext_data;
	uint16_t ext_type = 0;

	s->tlsext_ticket_expected = 0;
	*ret = NULL;

	/*
	 * If tickets disabled behave as if no ticket present to permit stateful
	 * resumption.
	 */
	if (SSL_get_options(s) & SSL_OP_NO_TICKET)
		return TLS1_TICKET_NONE;

	/*
	 * An empty extensions block is valid, but obviously does not contain
	 * a session ticket.
	 */
	if (CBS_len(ext_block) == 0)
		return TLS1_TICKET_NONE;

	if (!CBS_get_u16_length_prefixed(ext_block, &extensions)) {
		*alert = SSL_AD_DECODE_ERROR;
		return TLS1_TICKET_FATAL_ERROR;
	}

	while (CBS_len(&extensions) > 0) {
		if (!CBS_get_u16(&extensions, &ext_type) ||
		    !CBS_get_u16_length_prefixed(&extensions, &ext_data)) {
			*alert = SSL_AD_DECODE_ERROR;
			return TLS1_TICKET_FATAL_ERROR;
		}

		if (ext_type == TLSEXT_TYPE_session_ticket)
			break;
	}

	if (ext_type != TLSEXT_TYPE_session_ticket)
		return TLS1_TICKET_NONE;

	if (CBS_len(&ext_data) == 0) {
		/*
		 * The client will accept a ticket but does not currently
		 * have one.
		 */
		s->tlsext_ticket_expected = 1;
		return TLS1_TICKET_EMPTY;
	}

	if (s->tls_session_secret_cb != NULL) {
		/*
		 * Indicate that the ticket could not be decrypted rather than
		 * generating the session from ticket now, trigger abbreviated
		 * handshake based on external mechanism to calculate the master
		 * secret later.
		 */
		return TLS1_TICKET_NOT_DECRYPTED;
	}

	return tls_decrypt_ticket(s, &ext_data, alert, ret);
}

/* tls_decrypt_ticket attempts to decrypt a session ticket.
 *
 *   ticket: a CBS containing the body of the session ticket extension.
 *   psess: (output) on return, if a ticket was decrypted, then this is set to
 *       point to the resulting session.
 *
 * Returns:
 *    TLS1_TICKET_FATAL_ERROR: error from parsing or decrypting the ticket.
 *    TLS1_TICKET_NOT_DECRYPTED: the ticket couldn't be decrypted.
 *    TLS1_TICKET_DECRYPTED: a ticket was decrypted and *psess was set.
 */
static int
tls_decrypt_ticket(SSL *s, CBS *ticket, int *alert, SSL_SESSION **psess)
{
	CBS ticket_name, ticket_iv, ticket_encdata, ticket_hmac;
	SSL_SESSION *sess = NULL;
	unsigned char *sdec = NULL;
	size_t sdec_len = 0;
	const unsigned char *p;
	unsigned char hmac[EVP_MAX_MD_SIZE];
	HMAC_CTX *hctx = NULL;
	EVP_CIPHER_CTX *cctx = NULL;
	SSL_CTX *tctx = s->initial_ctx;
	int slen, hlen, iv_len;
	int alert_desc = SSL_AD_INTERNAL_ERROR;
	int ret = TLS1_TICKET_FATAL_ERROR;

	*psess = NULL;

	if (!CBS_get_bytes(ticket, &ticket_name, 16))
		goto derr;

	/*
	 * Initialize session ticket encryption and HMAC contexts.
	 */
	if ((cctx = EVP_CIPHER_CTX_new()) == NULL)
		goto err;
	if ((hctx = HMAC_CTX_new()) == NULL)
		goto err;

	if (tctx->tlsext_ticket_key_cb != NULL) {
		int rv;

		/*
		 * The API guarantees EVP_MAX_IV_LENGTH bytes of space for
		 * the iv to tlsext_ticket_key_cb().  Since the total space
		 * required for a session cookie is never less than this,
		 * this check isn't too strict.  The exact check comes later.
		 */
		if (CBS_len(ticket) < EVP_MAX_IV_LENGTH)
			goto derr;

		if ((rv = tctx->tlsext_ticket_key_cb(s,
		    (unsigned char *)CBS_data(&ticket_name),
		    (unsigned char *)CBS_data(ticket), cctx, hctx, 0)) < 0)
			goto err;
		if (rv == 0)
			goto derr;
		if (rv == 2) {
			/* Renew ticket. */
			s->tlsext_ticket_expected = 1;
		}

		if ((iv_len = EVP_CIPHER_CTX_iv_length(cctx)) < 0)
			goto err;
		/*
		 * Now that the cipher context is initialised, we can extract
		 * the IV since its length is known.
		 */
		if (!CBS_get_bytes(ticket, &ticket_iv, iv_len))
			goto derr;
	} else {
		/* Check that the key name matches. */
		if (!CBS_mem_equal(&ticket_name,
		    tctx->tlsext_tick_key_name,
		    sizeof(tctx->tlsext_tick_key_name)))
			goto derr;
		if ((iv_len = EVP_CIPHER_iv_length(EVP_aes_128_cbc())) < 0)
			goto err;
		if (!CBS_get_bytes(ticket, &ticket_iv, iv_len))
			goto derr;
		if (!EVP_DecryptInit_ex(cctx, EVP_aes_128_cbc(), NULL,
		    tctx->tlsext_tick_aes_key, CBS_data(&ticket_iv)))
			goto err;
		if (!HMAC_Init_ex(hctx, tctx->tlsext_tick_hmac_key,
		    sizeof(tctx->tlsext_tick_hmac_key), EVP_sha256(),
		    NULL))
			goto err;
	}

	/*
	 * Attempt to process session ticket.
	 */

	if ((hlen = HMAC_size(hctx)) < 0)
		goto err;

	if (hlen > CBS_len(ticket))
		goto derr;
	if (!CBS_get_bytes(ticket, &ticket_encdata, CBS_len(ticket) - hlen))
		goto derr;
	if (!CBS_get_bytes(ticket, &ticket_hmac, hlen))
		goto derr;
	if (CBS_len(ticket) != 0) {
		alert_desc = SSL_AD_DECODE_ERROR;
		goto err;
	}

	/* Check HMAC of encrypted ticket. */
	if (HMAC_Update(hctx, CBS_data(&ticket_name),
	    CBS_len(&ticket_name)) <= 0)
		goto err;
	if (HMAC_Update(hctx, CBS_data(&ticket_iv),
	    CBS_len(&ticket_iv)) <= 0)
		goto err;
	if (HMAC_Update(hctx, CBS_data(&ticket_encdata),
	    CBS_len(&ticket_encdata)) <= 0)
		goto err;
	if (HMAC_Final(hctx, hmac, &hlen) <= 0)
		goto err;

	if (!CBS_mem_equal(&ticket_hmac, hmac, hlen))
		goto derr;

	/* Attempt to decrypt session data. */
	sdec_len = CBS_len(&ticket_encdata);
	if ((sdec = calloc(1, sdec_len)) == NULL)
		goto err;
	if (EVP_DecryptUpdate(cctx, sdec, &slen, CBS_data(&ticket_encdata),
	    CBS_len(&ticket_encdata)) <= 0)
		goto derr;
	if (EVP_DecryptFinal_ex(cctx, sdec + slen, &hlen) <= 0)
		goto derr;

	slen += hlen;

	/*
	 * For session parse failures, indicate that we need to send a new
	 * ticket.
	 */
	p = sdec;
	if ((sess = d2i_SSL_SESSION(NULL, &p, slen)) == NULL)
		goto derr;
	*psess = sess;
	sess = NULL;

	ret = TLS1_TICKET_DECRYPTED;
	goto done;

 derr:
	ERR_clear_error();
	s->tlsext_ticket_expected = 1;
	ret = TLS1_TICKET_NOT_DECRYPTED;
	goto done;

 err:
	*alert = alert_desc;
	ret = TLS1_TICKET_FATAL_ERROR;
	goto done;

 done:
	freezero(sdec, sdec_len);
	EVP_CIPHER_CTX_free(cctx);
	HMAC_CTX_free(hctx);
	SSL_SESSION_free(sess);

	return ret;
}
