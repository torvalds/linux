/*	$OpenBSD: x509_siginfo.c,v 1.1 2024/08/28 07:15:04 tb Exp $ */

/*
 * Copyright (c) 2024 Theo Buehler <tb@openbsd.org>
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

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "evp_local.h"

#include "x509_internal.h"

static int
x509_find_sigid_algs(const X509 *x509, int *out_md_nid, int *out_pkey_nid)
{
	const ASN1_OBJECT *aobj;
	int nid;

	*out_md_nid = NID_undef;
	*out_pkey_nid = NID_undef;

	X509_ALGOR_get0(&aobj, NULL, NULL, x509->sig_alg);
	if ((nid = OBJ_obj2nid(aobj)) == NID_undef)
		return 0;

	return OBJ_find_sigid_algs(nid, out_md_nid, out_pkey_nid);
}

int
X509_get_signature_info(X509 *x509, int *out_md_nid, int *out_pkey_nid,
    int *out_security_bits, uint32_t *out_flags)
{
	const EVP_MD *md;
	int md_nid = NID_undef, pkey_nid = NID_undef, security_bits = -1;
	uint32_t flags = 0;

	if (out_md_nid != NULL)
		*out_md_nid = md_nid;
	if (out_pkey_nid != NULL)
		*out_pkey_nid = pkey_nid;
	if (out_security_bits != NULL)
		*out_security_bits = security_bits;
	if (out_flags != NULL)
		*out_flags = flags;

	if (!x509v3_cache_extensions(x509))
		goto err;

	if (!x509_find_sigid_algs(x509, &md_nid, &pkey_nid))
		goto err;

	/*
	 * If md_nid == NID_undef, this means we need to consult the ameth.
	 * Handlers are available for EdDSA and RSA-PSS. No other signature
	 * algorithm with NID_undef should appear in a certificate.
	 */
	if (md_nid == NID_undef) {
		const EVP_PKEY_ASN1_METHOD *ameth;

		if ((ameth = EVP_PKEY_asn1_find(NULL, pkey_nid)) == NULL ||
		    ameth->signature_info == NULL)
			goto err;

		if (!ameth->signature_info(x509->sig_alg, &md_nid, &pkey_nid,
		    &security_bits, &flags))
			goto err;

		goto done;
	}

	/* XXX - OpenSSL 3 special cases SHA-1 (63 bits) and MD5 (39 bits). */
	if ((md = EVP_get_digestbynid(md_nid)) == NULL)
		goto err;

	/* Assume 4 bits of collision resistance per octet. */
	if ((security_bits = EVP_MD_size(md)) <= 0)
		goto err;
	security_bits *= 4;

	if (md_nid == NID_sha1 || md_nid == NID_sha256 ||
	    md_nid == NID_sha384 || md_nid == NID_sha512)
		flags |= X509_SIG_INFO_TLS;

	flags |= X509_SIG_INFO_VALID;

 done:
	if (out_md_nid != NULL)
		*out_md_nid = md_nid;
	if (out_pkey_nid != NULL)
		*out_pkey_nid = pkey_nid;
	if (out_security_bits != NULL)
		*out_security_bits = security_bits;
	if (out_flags != NULL)
		*out_flags = flags;

 err:
	return (flags & X509_SIG_INFO_VALID) != 0;
}
LCRYPTO_ALIAS(X509_get_signature_info);
