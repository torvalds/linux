/*	$OpenBSD: tak.c,v 1.27 2025/08/19 11:30:20 job Exp $ */
/*
 * Copyright (c) 2022 Job Snijders <job@fastly.com>
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/safestack.h>
#include <openssl/stack.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "extern.h"
#include "rpki-asn1.h"

/*
 * TAK eContent definition in RFC 9691, Appendix A.
 */

ASN1_ITEM_EXP TAKey_it;
ASN1_ITEM_EXP TAK_it;

ASN1_SEQUENCE(TAKey) = {
	ASN1_SEQUENCE_OF(TAKey, comments, ASN1_UTF8STRING),
	ASN1_SEQUENCE_OF(TAKey, certificateURIs, ASN1_IA5STRING),
	ASN1_SIMPLE(TAKey, subjectPublicKeyInfo, X509_PUBKEY),
} ASN1_SEQUENCE_END(TAKey);

ASN1_SEQUENCE(TAK) = {
	ASN1_EXP_OPT(TAK, version, ASN1_INTEGER, 0),
	ASN1_SIMPLE(TAK, current, TAKey),
	ASN1_EXP_OPT(TAK, predecessor, TAKey, 0),
	ASN1_EXP_OPT(TAK, successor, TAKey, 1),
} ASN1_SEQUENCE_END(TAK);

IMPLEMENT_ASN1_FUNCTIONS(TAK);


/*
 * On success return pointer to allocated & valid takey structure,
 * on failure return NULL.
 */
static struct takey *
parse_takey(const char *fn, const TAKey *takey)
{
	const ASN1_UTF8STRING	*comment;
	const ASN1_IA5STRING	*certURI;
	X509_PUBKEY		*pubkey;
	struct takey		*res = NULL;
	unsigned char		*der = NULL;
	size_t			 i;
	int			 der_len;

	if ((res = calloc(1, sizeof(struct takey))) == NULL)
		err(1, NULL);

	res->num_comments = sk_ASN1_UTF8STRING_num(takey->comments);
	if (res->num_comments > 0) {
		res->comments = calloc(res->num_comments, sizeof(char *));
		if (res->comments == NULL)
			err(1, NULL);

		for (i = 0; i < res->num_comments; i++) {
			comment = sk_ASN1_UTF8STRING_value(takey->comments, i);
			res->comments[i] = calloc(comment->length + 1, 4);
			if (res->comments[i] == NULL)
				err(1, NULL);
			(void)strvisx(res->comments[i], comment->data,
			    comment->length, VIS_SAFE);
		}
	}

	res->num_uris = sk_ASN1_IA5STRING_num(takey->certificateURIs);
	if (res->num_uris == 0) {
		warnx("%s: Signed TAL requires at least 1 CertificateURI", fn);
		goto err;
	}
	if ((res->uris = calloc(res->num_uris, sizeof(char *))) == NULL)
		err(1, NULL);

	for (i = 0; i < res->num_uris; i++) {
		certURI = sk_ASN1_IA5STRING_value(takey->certificateURIs, i);
		if (!valid_uri(certURI->data, certURI->length, NULL)) {
			warnx("%s: invalid TA URI", fn);
			goto err;
		}

		/* XXX: enforce that protocol is rsync or https. */

		res->uris[i] = strndup(certURI->data, certURI->length);
		if (res->uris[i] == NULL)
			err(1, NULL);
	}

	pubkey = takey->subjectPublicKeyInfo;
	if ((res->ski = x509_pubkey_get_ski(pubkey, fn)) == NULL)
		goto err;

	if ((der_len = i2d_X509_PUBKEY(pubkey, &der)) <= 0) {
		warnx("%s: i2d_X509_PUBKEY failed", fn);
		goto err;
	}
	res->pubkey = der;
	res->pubkeysz = der_len;

	return res;

 err:
	takey_free(res);
	return NULL;
}

/*
 * Parses the eContent segment of an TAK file
 * Returns zero on failure, non-zero on success.
 */
static int
tak_parse_econtent(const char *fn, struct tak *tak, const unsigned char *d,
    size_t dsz)
{
	const unsigned char	*oder;
	TAK			*tak_asn1;
	int			 rc = 0;

	oder = d;
	if ((tak_asn1 = d2i_TAK(NULL, &d, dsz)) == NULL) {
		warnx("%s: failed to parse Trust Anchor Key", fn);
		goto out;
	}
	if (d != oder + dsz) {
		warnx("%s: %td bytes trailing garbage in eContent", fn,
		    oder + dsz - d);
		goto out;
	}

	if (!valid_econtent_version(fn, tak_asn1->version, 0))
		goto out;

	tak->current = parse_takey(fn, tak_asn1->current);
	if (tak->current == NULL)
		goto out;

	if (tak_asn1->predecessor != NULL) {
		tak->predecessor = parse_takey(fn, tak_asn1->predecessor);
		if (tak->predecessor == NULL)
			goto out;
	}

	if (tak_asn1->successor != NULL) {
		tak->successor = parse_takey(fn, tak_asn1->successor);
		if (tak->successor == NULL)
			goto out;
	}

	rc = 1;
 out:
	TAK_free(tak_asn1);
	return rc;
}

/*
 * Parse a full RFC 9691 Trust Anchor Key file.
 * Returns the TAK or NULL if the object was malformed.
 */
struct tak *
tak_parse(struct cert **out_cert, const char *fn, int talid,
    const unsigned char *der, size_t len)
{
	struct tak		*tak;
	struct cert		*cert = NULL;
	unsigned char		*cms;
	size_t			 cmsz;
	time_t			 signtime = 0;
	int			 rc = 0;

	assert(*out_cert == NULL);

	cms = cms_parse_validate(&cert, fn, talid, der, len, tak_oid, &cmsz,
	    &signtime);
	if (cms == NULL)
		return NULL;

	if ((tak = calloc(1, sizeof(struct tak))) == NULL)
		err(1, NULL);
	tak->signtime = signtime;

	if (!x509_inherits(cert->x509)) {
		warnx("%s: RFC 3779 extension not set to inherit", fn);
		goto out;
	}

	if (!tak_parse_econtent(fn, tak, cms, cmsz))
		goto out;

	if (strcmp(cert->aki, tak->current->ski) != 0) {
		warnx("%s: current TAKey's SKI does not match EE AKI", fn);
		goto out;
	}

	*out_cert = cert;
	cert = NULL;

	rc = 1;
 out:
	if (rc == 0) {
		tak_free(tak);
		tak = NULL;
	}
	cert_free(cert);
	free(cms);
	return tak;
}

/*
 * Free TAKey pointer.
 */
void
takey_free(struct takey *t)
{
	size_t	i;

	if (t == NULL)
		return;

	for (i = 0; i < t->num_comments; i++)
		free(t->comments[i]);

	for (i = 0; i < t->num_uris; i++)
		free(t->uris[i]);

	free(t->comments);
	free(t->uris);
	free(t->ski);
	free(t->pubkey);
	free(t);
}

/*
 * Free an TAK pointer.
 * Safe to call with NULL.
 */
void
tak_free(struct tak *t)
{
	if (t == NULL)
		return;

	takey_free(t->current);
	takey_free(t->predecessor);
	takey_free(t->successor);
	free(t);
}
