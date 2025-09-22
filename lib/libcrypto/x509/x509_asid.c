/*	$OpenBSD: x509_asid.c,v 1.46 2025/05/10 05:54:39 tb Exp $ */
/*
 * Contributed to the OpenSSL Project by the American Registry for
 * Internet Numbers ("ARIN").
 */
/* ====================================================================
 * Copyright (c) 2006-2018 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
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
 */

/*
 * Implementation of RFC 3779 section 3.2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/conf.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

#ifndef OPENSSL_NO_RFC3779

static const ASN1_TEMPLATE ASRange_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ASRange, min),
		.field_name = "min",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ASRange, max),
		.field_name = "max",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM ASRange_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ASRange_seq_tt,
	.tcount = sizeof(ASRange_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ASRange),
	.sname = "ASRange",
};
LCRYPTO_ALIAS(ASRange_it);

static const ASN1_TEMPLATE ASIdOrRange_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ASIdOrRange, u.id),
		.field_name = "u.id",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ASIdOrRange, u.range),
		.field_name = "u.range",
		.item = &ASRange_it,
	},
};

const ASN1_ITEM ASIdOrRange_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(ASIdOrRange, type),
	.templates = ASIdOrRange_ch_tt,
	.tcount = sizeof(ASIdOrRange_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ASIdOrRange),
	.sname = "ASIdOrRange",
};
LCRYPTO_ALIAS(ASIdOrRange_it);

static const ASN1_TEMPLATE ASIdentifierChoice_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ASIdentifierChoice, u.inherit),
		.field_name = "u.inherit",
		.item = &ASN1_NULL_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(ASIdentifierChoice, u.asIdsOrRanges),
		.field_name = "u.asIdsOrRanges",
		.item = &ASIdOrRange_it,
	},
};

const ASN1_ITEM ASIdentifierChoice_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(ASIdentifierChoice, type),
	.templates = ASIdentifierChoice_ch_tt,
	.tcount = sizeof(ASIdentifierChoice_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ASIdentifierChoice),
	.sname = "ASIdentifierChoice",
};
LCRYPTO_ALIAS(ASIdentifierChoice_it);

static const ASN1_TEMPLATE ASIdentifiers_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(ASIdentifiers, asnum),
		.field_name = "asnum",
		.item = &ASIdentifierChoice_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(ASIdentifiers, rdi),
		.field_name = "rdi",
		.item = &ASIdentifierChoice_it,
	},
};

const ASN1_ITEM ASIdentifiers_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ASIdentifiers_seq_tt,
	.tcount = sizeof(ASIdentifiers_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ASIdentifiers),
	.sname = "ASIdentifiers",
};
LCRYPTO_ALIAS(ASIdentifiers_it);

ASRange *
d2i_ASRange(ASRange **a, const unsigned char **in, long len)
{
	return (ASRange *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASRange_it);
}
LCRYPTO_ALIAS(d2i_ASRange);

int
i2d_ASRange(ASRange *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASRange_it);
}
LCRYPTO_ALIAS(i2d_ASRange);

ASRange *
ASRange_new(void)
{
	return (ASRange *)ASN1_item_new(&ASRange_it);
}
LCRYPTO_ALIAS(ASRange_new);

void
ASRange_free(ASRange *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASRange_it);
}
LCRYPTO_ALIAS(ASRange_free);

ASIdOrRange *
d2i_ASIdOrRange(ASIdOrRange **a, const unsigned char **in, long len)
{
	return (ASIdOrRange *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASIdOrRange_it);
}
LCRYPTO_ALIAS(d2i_ASIdOrRange);

int
i2d_ASIdOrRange(ASIdOrRange *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASIdOrRange_it);
}
LCRYPTO_ALIAS(i2d_ASIdOrRange);

ASIdOrRange *
ASIdOrRange_new(void)
{
	return (ASIdOrRange *)ASN1_item_new(&ASIdOrRange_it);
}
LCRYPTO_ALIAS(ASIdOrRange_new);

void
ASIdOrRange_free(ASIdOrRange *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASIdOrRange_it);
}
LCRYPTO_ALIAS(ASIdOrRange_free);

ASIdentifierChoice *
d2i_ASIdentifierChoice(ASIdentifierChoice **a, const unsigned char **in,
    long len)
{
	return (ASIdentifierChoice *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASIdentifierChoice_it);
}
LCRYPTO_ALIAS(d2i_ASIdentifierChoice);

int
i2d_ASIdentifierChoice(ASIdentifierChoice *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASIdentifierChoice_it);
}
LCRYPTO_ALIAS(i2d_ASIdentifierChoice);

ASIdentifierChoice *
ASIdentifierChoice_new(void)
{
	return (ASIdentifierChoice *)ASN1_item_new(&ASIdentifierChoice_it);
}
LCRYPTO_ALIAS(ASIdentifierChoice_new);

void
ASIdentifierChoice_free(ASIdentifierChoice *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASIdentifierChoice_it);
}
LCRYPTO_ALIAS(ASIdentifierChoice_free);

ASIdentifiers *
d2i_ASIdentifiers(ASIdentifiers **a, const unsigned char **in, long len)
{
	return (ASIdentifiers *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASIdentifiers_it);
}
LCRYPTO_ALIAS(d2i_ASIdentifiers);

int
i2d_ASIdentifiers(ASIdentifiers *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASIdentifiers_it);
}
LCRYPTO_ALIAS(i2d_ASIdentifiers);

ASIdentifiers *
ASIdentifiers_new(void)
{
	return (ASIdentifiers *)ASN1_item_new(&ASIdentifiers_it);
}
LCRYPTO_ALIAS(ASIdentifiers_new);

void
ASIdentifiers_free(ASIdentifiers *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASIdentifiers_it);
}
LCRYPTO_ALIAS(ASIdentifiers_free);

/*
 * i2r method for an ASIdentifierChoice.
 */
static int
i2r_ASIdentifierChoice(BIO *out, ASIdentifierChoice *choice, int indent,
    const char *msg)
{
	int i;
	char *s;
	if (choice == NULL)
		return 1;
	BIO_printf(out, "%*s%s:\n", indent, "", msg);
	switch (choice->type) {
	case ASIdentifierChoice_inherit:
		BIO_printf(out, "%*sinherit\n", indent + 2, "");
		break;
	case ASIdentifierChoice_asIdsOrRanges:
		for (i = 0; i < sk_ASIdOrRange_num(choice->u.asIdsOrRanges);
		    i++) {
			ASIdOrRange *aor =
			    sk_ASIdOrRange_value(choice->u.asIdsOrRanges, i);
			switch (aor->type) {
			case ASIdOrRange_id:
				if ((s = i2s_ASN1_INTEGER(NULL, aor->u.id)) ==
				    NULL)
					return 0;
				BIO_printf(out, "%*s%s\n", indent + 2, "", s);
				free(s);
				break;
			case ASIdOrRange_range:
				if ((s = i2s_ASN1_INTEGER(NULL,
				    aor->u.range->min)) == NULL)
					return 0;
				BIO_printf(out, "%*s%s-", indent + 2, "", s);
				free(s);
				if ((s = i2s_ASN1_INTEGER(NULL,
				    aor->u.range->max)) == NULL)
					return 0;
				BIO_printf(out, "%s\n", s);
				free(s);
				break;
			default:
				return 0;
			}
		}
		break;
	default:
		return 0;
	}
	return 1;
}

/*
 * i2r method for an ASIdentifier extension.
 */
static int
i2r_ASIdentifiers(const X509V3_EXT_METHOD *method, void *ext, BIO *out,
    int indent)
{
	ASIdentifiers *asid = ext;
	return (i2r_ASIdentifierChoice(out, asid->asnum, indent,
	    "Autonomous System Numbers") &&
	    i2r_ASIdentifierChoice(out, asid->rdi, indent,
	    "Routing Domain Identifiers"));
}

/*
 * Sort comparison function for a sequence of ASIdOrRange elements.
 */
static int
ASIdOrRange_cmp(const ASIdOrRange *const *a_, const ASIdOrRange *const *b_)
{
	const ASIdOrRange *a = *a_, *b = *b_;

	/* XXX: these asserts need to be replaced */
	OPENSSL_assert((a->type == ASIdOrRange_id && a->u.id != NULL) ||
	    (a->type == ASIdOrRange_range && a->u.range != NULL &&
	     a->u.range->min != NULL && a->u.range->max != NULL));

	OPENSSL_assert((b->type == ASIdOrRange_id && b->u.id != NULL) ||
	    (b->type == ASIdOrRange_range && b->u.range != NULL &&
	     b->u.range->min != NULL && b->u.range->max != NULL));

	if (a->type == ASIdOrRange_id && b->type == ASIdOrRange_id)
		return ASN1_INTEGER_cmp(a->u.id, b->u.id);

	if (a->type == ASIdOrRange_range && b->type == ASIdOrRange_range) {
		int r = ASN1_INTEGER_cmp(a->u.range->min, b->u.range->min);
		return r != 0 ? r : ASN1_INTEGER_cmp(a->u.range->max,
		    b->u.range->max);
	}

	if (a->type == ASIdOrRange_id)
		return ASN1_INTEGER_cmp(a->u.id, b->u.range->min);
	else
		return ASN1_INTEGER_cmp(a->u.range->min, b->u.id);
}

/*
 * Add an inherit element.
 */
int
X509v3_asid_add_inherit(ASIdentifiers *asid, int which)
{
	ASIdentifierChoice **choice;
	ASIdentifierChoice *aic = NULL;
	int ret = 0;

	if (asid == NULL)
		goto err;

	switch (which) {
	case V3_ASID_ASNUM:
		choice = &asid->asnum;
		break;
	case V3_ASID_RDI:
		choice = &asid->rdi;
		break;
	default:
		goto err;
	}

	if (*choice != NULL) {
		if ((*choice)->type != ASIdentifierChoice_inherit)
			goto err;
	} else {
		if ((aic = ASIdentifierChoice_new()) == NULL)
			goto err;
		if ((aic->u.inherit = ASN1_NULL_new()) == NULL)
			goto err;
		aic->type = ASIdentifierChoice_inherit;

		*choice = aic;
		aic = NULL;
	}

	ret = 1;

 err:
	ASIdentifierChoice_free(aic);

	return ret;
}
LCRYPTO_ALIAS(X509v3_asid_add_inherit);

static int
ASIdOrRanges_add_id_or_range(ASIdOrRanges *aors, ASN1_INTEGER *min,
    ASN1_INTEGER *max)
{
	ASIdOrRange *aor = NULL;
	ASRange *asr = NULL;
	int ret = 0;

	/* Preallocate since we must not fail after sk_ASIdOrRange_push(). */
	if (max != NULL) {
		if ((asr = ASRange_new()) == NULL)
			goto err;
	}

	if ((aor = ASIdOrRange_new()) == NULL)
		goto err;
	if (sk_ASIdOrRange_push(aors, aor) <= 0)
		goto err;

	if (max == NULL) {
		aor->type = ASIdOrRange_id;
		aor->u.id = min;
	} else {
		ASN1_INTEGER_free(asr->min);
		asr->min = min;
		ASN1_INTEGER_free(asr->max);
		asr->max = max;

		aor->type = ASIdOrRange_range;
		aor->u.range = asr;
		asr = NULL;
	}

	aor = NULL;

	ret = 1;

 err:
	ASIdOrRange_free(aor);
	ASRange_free(asr);

	return ret;
}

/*
 * Add an ID or range to an ASIdentifierChoice.
 */
int
X509v3_asid_add_id_or_range(ASIdentifiers *asid, int which, ASN1_INTEGER *min,
    ASN1_INTEGER *max)
{
	ASIdentifierChoice **choice;
	ASIdentifierChoice *aic = NULL, *new_aic = NULL;
	int ret = 0;

	if (asid == NULL)
		goto err;

	switch (which) {
	case V3_ASID_ASNUM:
		choice = &asid->asnum;
		break;
	case V3_ASID_RDI:
		choice = &asid->rdi;
		break;
	default:
		goto err;
	}

	if ((aic = *choice) != NULL) {
		if (aic->type != ASIdentifierChoice_asIdsOrRanges)
			goto err;
	} else {
		if ((aic = new_aic = ASIdentifierChoice_new()) == NULL)
			goto err;
		aic->u.asIdsOrRanges = sk_ASIdOrRange_new(ASIdOrRange_cmp);
		if (aic->u.asIdsOrRanges == NULL)
			goto err;
		aic->type = ASIdentifierChoice_asIdsOrRanges;
	}

	if (!ASIdOrRanges_add_id_or_range(aic->u.asIdsOrRanges, min, max))
		goto err;

	*choice = aic;
	aic = new_aic = NULL;

	ret = 1;

 err:
	ASIdentifierChoice_free(new_aic);

	return ret;
}
LCRYPTO_ALIAS(X509v3_asid_add_id_or_range);

/*
 * Extract min and max values from an ASIdOrRange.
 */
static int
extract_min_max(ASIdOrRange *aor, ASN1_INTEGER **min, ASN1_INTEGER **max)
{
	switch (aor->type) {
	case ASIdOrRange_id:
		*min = aor->u.id;
		*max = aor->u.id;
		return 1;
	case ASIdOrRange_range:
		*min = aor->u.range->min;
		*max = aor->u.range->max;
		return 1;
	}
	*min = NULL;
	*max = NULL;

	return 0;
}

/*
 * Check whether an ASIdentifierChoice is in canonical form.
 */
static int
ASIdentifierChoice_is_canonical(ASIdentifierChoice *choice)
{
	ASIdOrRange *a, *b;
	ASN1_INTEGER *a_min = NULL, *a_max = NULL, *b_min = NULL, *b_max = NULL;
	ASN1_INTEGER *a_max_plus_one = NULL;
	ASN1_INTEGER *orig;
	BIGNUM *bn = NULL;
	int i, ret = 0;

	/*
	 * Empty element or inheritance is canonical.
	 */
	if (choice == NULL || choice->type == ASIdentifierChoice_inherit)
		return 1;

	/*
	 * If not a list, or if empty list, it's broken.
	 */
	if (choice->type != ASIdentifierChoice_asIdsOrRanges ||
	    sk_ASIdOrRange_num(choice->u.asIdsOrRanges) == 0)
		return 0;

	/*
	 * It's a list, check it.
	 */
	for (i = 0; i < sk_ASIdOrRange_num(choice->u.asIdsOrRanges) - 1; i++) {
		a = sk_ASIdOrRange_value(choice->u.asIdsOrRanges, i);
		b = sk_ASIdOrRange_value(choice->u.asIdsOrRanges, i + 1);

		if (!extract_min_max(a, &a_min, &a_max) ||
		    !extract_min_max(b, &b_min, &b_max))
			goto done;

		/*
		 * Punt misordered list, overlapping start, or inverted range.
		 */
		if (ASN1_INTEGER_cmp(a_min, b_min) >= 0 ||
		    ASN1_INTEGER_cmp(a_min, a_max) > 0 ||
		    ASN1_INTEGER_cmp(b_min, b_max) > 0)
			goto done;

		/*
		 * Calculate a_max + 1 to check for adjacency.
		 */
		if ((bn == NULL && (bn = BN_new()) == NULL) ||
		    ASN1_INTEGER_to_BN(a_max, bn) == NULL ||
		    !BN_add_word(bn, 1)) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto done;
		}

		if ((a_max_plus_one =
		    BN_to_ASN1_INTEGER(bn, orig = a_max_plus_one)) == NULL) {
			a_max_plus_one = orig;
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto done;
		}

		/*
		 * Punt if adjacent or overlapping.
		 */
		if (ASN1_INTEGER_cmp(a_max_plus_one, b_min) >= 0)
			goto done;
	}

	/*
	 * Check for inverted range.
	 */
	i = sk_ASIdOrRange_num(choice->u.asIdsOrRanges) - 1;
	a = sk_ASIdOrRange_value(choice->u.asIdsOrRanges, i);
	if (a != NULL && a->type == ASIdOrRange_range) {
		if (!extract_min_max(a, &a_min, &a_max) ||
		    ASN1_INTEGER_cmp(a_min, a_max) > 0)
			goto done;
	}

	ret = 1;

 done:
	ASN1_INTEGER_free(a_max_plus_one);
	BN_free(bn);
	return ret;
}

/*
 * Check whether an ASIdentifier extension is in canonical form.
 */
int
X509v3_asid_is_canonical(ASIdentifiers *asid)
{
	return (asid == NULL ||
	    (ASIdentifierChoice_is_canonical(asid->asnum) &&
	     ASIdentifierChoice_is_canonical(asid->rdi)));
}
LCRYPTO_ALIAS(X509v3_asid_is_canonical);

/*
 * Whack an ASIdentifierChoice into canonical form.
 */
static int
ASIdentifierChoice_canonize(ASIdentifierChoice *choice)
{
	ASIdOrRange *a, *b;
	ASN1_INTEGER *a_min = NULL, *a_max = NULL, *b_min = NULL, *b_max = NULL;
	ASN1_INTEGER *a_max_plus_one = NULL;
	ASN1_INTEGER *orig;
	BIGNUM *bn = NULL;
	int i, ret = 0;

	/*
	 * Nothing to do for empty element or inheritance.
	 */
	if (choice == NULL || choice->type == ASIdentifierChoice_inherit)
		return 1;

	/*
	 * If not a list, or if empty list, it's broken.
	 */
	if (choice->type != ASIdentifierChoice_asIdsOrRanges ||
	    sk_ASIdOrRange_num(choice->u.asIdsOrRanges) == 0) {
		X509V3error(X509V3_R_EXTENSION_VALUE_ERROR);
		return 0;
	}

	/*
	 * We have a non-empty list.  Sort it.
	 */
	sk_ASIdOrRange_sort(choice->u.asIdsOrRanges);

	/*
	 * Now check for errors and suboptimal encoding, rejecting the
	 * former and fixing the latter.
	 */
	for (i = 0; i < sk_ASIdOrRange_num(choice->u.asIdsOrRanges) - 1; i++) {
		a = sk_ASIdOrRange_value(choice->u.asIdsOrRanges, i);
		b = sk_ASIdOrRange_value(choice->u.asIdsOrRanges, i + 1);

		if (!extract_min_max(a, &a_min, &a_max) ||
		    !extract_min_max(b, &b_min, &b_max))
			goto done;

		/*
		 * Make sure we're properly sorted (paranoia).
		 */
		if (ASN1_INTEGER_cmp(a_min, b_min) > 0)
			goto done;

		/*
		 * Punt inverted ranges.
		 */
		if (ASN1_INTEGER_cmp(a_min, a_max) > 0 ||
		    ASN1_INTEGER_cmp(b_min, b_max) > 0)
			goto done;

		/*
		 * Check for overlaps.
		 */
		if (ASN1_INTEGER_cmp(a_max, b_min) >= 0) {
			X509V3error(X509V3_R_EXTENSION_VALUE_ERROR);
			goto done;
		}

		/*
		 * Calculate a_max + 1 to check for adjacency.
		 */
		if ((bn == NULL && (bn = BN_new()) == NULL) ||
		    ASN1_INTEGER_to_BN(a_max, bn) == NULL ||
		    !BN_add_word(bn, 1)) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto done;
		}

		if ((a_max_plus_one =
		    BN_to_ASN1_INTEGER(bn, orig = a_max_plus_one)) == NULL) {
			a_max_plus_one = orig;
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto done;
		}

		/*
		 * If a and b are adjacent, merge them.
		 */
		if (ASN1_INTEGER_cmp(a_max_plus_one, b_min) == 0) {
			ASRange *r;
			switch (a->type) {
			case ASIdOrRange_id:
				if ((r = calloc(1, sizeof(*r))) == NULL) {
					X509V3error(ERR_R_MALLOC_FAILURE);
					goto done;
				}
				r->min = a_min;
				r->max = b_max;
				a->type = ASIdOrRange_range;
				a->u.range = r;
				break;
			case ASIdOrRange_range:
				ASN1_INTEGER_free(a->u.range->max);
				a->u.range->max = b_max;
				break;
			}
			switch (b->type) {
			case ASIdOrRange_id:
				b->u.id = NULL;
				break;
			case ASIdOrRange_range:
				b->u.range->max = NULL;
				break;
			}
			ASIdOrRange_free(b);
			(void)sk_ASIdOrRange_delete(choice->u.asIdsOrRanges,
			    i + 1);
			i--;
			continue;
		}
	}

	/*
	 * Check for final inverted range.
	 */
	i = sk_ASIdOrRange_num(choice->u.asIdsOrRanges) - 1;
	a = sk_ASIdOrRange_value(choice->u.asIdsOrRanges, i);
	if (a != NULL && a->type == ASIdOrRange_range) {
		if (!extract_min_max(a, &a_min, &a_max) ||
		    ASN1_INTEGER_cmp(a_min, a_max) > 0)
			goto done;
	}

	/* Paranoia */
	if (!ASIdentifierChoice_is_canonical(choice))
		goto done;

	ret = 1;

 done:
	ASN1_INTEGER_free(a_max_plus_one);
	BN_free(bn);
	return ret;
}

/*
 * Whack an ASIdentifier extension into canonical form.
 */
int
X509v3_asid_canonize(ASIdentifiers *asid)
{
	if (asid == NULL)
		return 1;

	if (!ASIdentifierChoice_canonize(asid->asnum))
		return 0;

	return ASIdentifierChoice_canonize(asid->rdi);
}
LCRYPTO_ALIAS(X509v3_asid_canonize);

/*
 * v2i method for an ASIdentifier extension.
 */
static void *
v2i_ASIdentifiers(const struct v3_ext_method *method, struct v3_ext_ctx *ctx,
    STACK_OF(CONF_VALUE)*values)
{
	ASN1_INTEGER *min = NULL, *max = NULL;
	ASIdentifiers *asid = NULL;
	int i;

	if ((asid = ASIdentifiers_new()) == NULL) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	for (i = 0; i < sk_CONF_VALUE_num(values); i++) {
		CONF_VALUE *val = sk_CONF_VALUE_value(values, i);
		int i1 = 0, i2 = 0, i3 = 0, is_range = 0, which = 0;

		/*
		 * Figure out whether this is an AS or an RDI.
		 */
		if (!name_cmp(val->name, "AS")) {
			which = V3_ASID_ASNUM;
		} else if (!name_cmp(val->name, "RDI")) {
			which = V3_ASID_RDI;
		} else {
			X509V3error(X509V3_R_EXTENSION_NAME_ERROR);
			X509V3_conf_err(val);
			goto err;
		}

		/*
		 * Handle inheritance.
		 */
		if (strcmp(val->value, "inherit") == 0) {
			if (X509v3_asid_add_inherit(asid, which))
				continue;
			X509V3error(X509V3_R_INVALID_INHERITANCE);
			X509V3_conf_err(val);
			goto err;
		}

		/*
		 * Number, range, or mistake, pick it apart and figure out which
		 */
		i1 = strspn(val->value, "0123456789");
		if (val->value[i1] == '\0') {
			is_range = 0;
		} else {
			is_range = 1;
			i2 = i1 + strspn(val->value + i1, " \t");
			if (val->value[i2] != '-') {
				X509V3error(X509V3_R_INVALID_ASNUMBER);
				X509V3_conf_err(val);
				goto err;
			}
			i2++;
			i2 = i2 + strspn(val->value + i2, " \t");
			i3 = i2 + strspn(val->value + i2, "0123456789");
			if (val->value[i3] != '\0') {
				X509V3error(X509V3_R_INVALID_ASRANGE);
				X509V3_conf_err(val);
				goto err;
			}
		}

		/*
		 * Syntax is ok, read and add it.
		 */
		if (!is_range) {
			if (!X509V3_get_value_int(val, &min)) {
				X509V3error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
		} else {
			char *s = strdup(val->value);
			if (s == NULL) {
				X509V3error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			s[i1] = '\0';
			min = s2i_ASN1_INTEGER(NULL, s);
			max = s2i_ASN1_INTEGER(NULL, s + i2);
			free(s);
			if (min == NULL || max == NULL) {
				X509V3error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			if (ASN1_INTEGER_cmp(min, max) > 0) {
				X509V3error(X509V3_R_EXTENSION_VALUE_ERROR);
				goto err;
			}
		}
		if (!X509v3_asid_add_id_or_range(asid, which, min, max)) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		min = max = NULL;
	}

	/*
	 * Canonize the result, then we're done.
	 */
	if (!X509v3_asid_canonize(asid))
		goto err;
	return asid;

 err:
	ASIdentifiers_free(asid);
	ASN1_INTEGER_free(min);
	ASN1_INTEGER_free(max);
	return NULL;
}

/*
 * OpenSSL dispatch.
 */
static const X509V3_EXT_METHOD x509v3_ext_sbgp_autonomousSysNum = {
	.ext_nid = NID_sbgp_autonomousSysNum,
	.ext_flags = 0,
	.it = &ASIdentifiers_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = v2i_ASIdentifiers,
	.i2r = i2r_ASIdentifiers,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_sbgp_autonomousSysNum(void)
{
	return &x509v3_ext_sbgp_autonomousSysNum;
}

/*
 * Figure out whether extension uses inheritance.
 */
int
X509v3_asid_inherits(ASIdentifiers *asid)
{
	if (asid == NULL)
		return 0;

	if (asid->asnum != NULL) {
		if (asid->asnum->type == ASIdentifierChoice_inherit)
			return 1;
	}

	if (asid->rdi != NULL) {
		if (asid->rdi->type == ASIdentifierChoice_inherit)
			return 1;
	}

	return 0;
}
LCRYPTO_ALIAS(X509v3_asid_inherits);

/*
 * Figure out whether parent contains child.
 */
static int
asid_contains(ASIdOrRanges *parent, ASIdOrRanges *child)
{
	ASN1_INTEGER *p_min = NULL, *p_max = NULL, *c_min = NULL, *c_max = NULL;
	int p, c;

	if (child == NULL || parent == child)
		return 1;

	if (parent == NULL)
		return 0;

	p = 0;
	for (c = 0; c < sk_ASIdOrRange_num(child); c++) {
		if (!extract_min_max(sk_ASIdOrRange_value(child, c), &c_min,
		    &c_max))
			return 0;
		for (;; p++) {
			if (p >= sk_ASIdOrRange_num(parent))
				return 0;
			if (!extract_min_max(sk_ASIdOrRange_value(parent, p),
			    &p_min, &p_max))
				return 0;
			if (ASN1_INTEGER_cmp(p_max, c_max) < 0)
				continue;
			if (ASN1_INTEGER_cmp(p_min, c_min) > 0)
				return 0;
			break;
		}
	}

	return 1;
}

/*
 * Test whether child is a subset of parent.
 */
int
X509v3_asid_subset(ASIdentifiers *child, ASIdentifiers *parent)
{
	if (child == NULL || child == parent)
		return 1;

	if (parent == NULL)
		return 0;

	if (X509v3_asid_inherits(child) || X509v3_asid_inherits(parent))
		return 0;

	if (child->asnum != NULL) {
		if (parent->asnum == NULL)
			return 0;

		if (!asid_contains(parent->asnum->u.asIdsOrRanges,
		    child->asnum->u.asIdsOrRanges))
			return 0;
	}

	if (child->rdi != NULL) {
		if (parent->rdi == NULL)
			return 0;

		if (!asid_contains(parent->rdi->u.asIdsOrRanges,
		    child->rdi->u.asIdsOrRanges))
			return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(X509v3_asid_subset);

/*
 * Validation error handling via callback.
 */
#define validation_err(_err_)           \
  do {                                  \
    if (ctx != NULL) {                  \
      ctx->error = _err_;               \
      ctx->error_depth = i;             \
      ctx->current_cert = x;            \
      ret = ctx->verify_cb(0, ctx);     \
    } else {                            \
      ret = 0;                          \
    }                                   \
    if (!ret)                           \
      goto done;                        \
  } while (0)

/*
 * Core code for RFC 3779 3.3 path validation.
 */
static int
asid_validate_path_internal(X509_STORE_CTX *ctx, STACK_OF(X509) *chain,
    ASIdentifiers *ext)
{
	ASIdOrRanges *child_as = NULL, *child_rdi = NULL;
	int i, ret = 1, inherit_as = 0, inherit_rdi = 0;
	X509 *x;

	/* We need a non-empty chain to test against. */
	if (sk_X509_num(chain) <= 0)
		goto err;
	/* We need either a store ctx or an extension to work with. */
	if (ctx == NULL && ext == NULL)
		goto err;
	/* If there is a store ctx, it needs a verify_cb. */
	if (ctx != NULL && ctx->verify_cb == NULL)
		goto err;

	/*
	 * Figure out where to start. If we don't have an extension to check,
	 * (either extracted from the leaf or passed by the caller), we're done.
	 * Otherwise, check canonical form and set up for walking up the chain.
	 */
	if (ext != NULL) {
		i = -1;
		x = NULL;
		if (!X509v3_asid_is_canonical(ext))
			validation_err(X509_V_ERR_INVALID_EXTENSION);
	} else {
		i = 0;
		x = sk_X509_value(chain, i);
		if ((X509_get_extension_flags(x) & EXFLAG_INVALID) != 0)
			goto done;
		if ((ext = x->rfc3779_asid) == NULL)
			goto done;
	}
	if (ext->asnum != NULL) {
		switch (ext->asnum->type) {
		case ASIdentifierChoice_inherit:
			inherit_as = 1;
			break;
		case ASIdentifierChoice_asIdsOrRanges:
			child_as = ext->asnum->u.asIdsOrRanges;
			break;
		}
	}
	if (ext->rdi != NULL) {
		switch (ext->rdi->type) {
		case ASIdentifierChoice_inherit:
			inherit_rdi = 1;
			break;
		case ASIdentifierChoice_asIdsOrRanges:
			child_rdi = ext->rdi->u.asIdsOrRanges;
			break;
		}
	}

	/*
	 * Now walk up the chain.  Extensions must be in canonical form, no
	 * cert may list resources that its parent doesn't list.
	 */
	for (i++; i < sk_X509_num(chain); i++) {
		x = sk_X509_value(chain, i);

		if ((X509_get_extension_flags(x) & EXFLAG_INVALID) != 0)
			validation_err(X509_V_ERR_INVALID_EXTENSION);
		if (x->rfc3779_asid == NULL) {
			if (child_as != NULL || child_rdi != NULL)
				validation_err(X509_V_ERR_UNNESTED_RESOURCE);
			continue;
		}
		if (x->rfc3779_asid->asnum == NULL && child_as != NULL) {
			validation_err(X509_V_ERR_UNNESTED_RESOURCE);
			child_as = NULL;
			inherit_as = 0;
		}
		if (x->rfc3779_asid->asnum != NULL &&
		    x->rfc3779_asid->asnum->type ==
		    ASIdentifierChoice_asIdsOrRanges) {
			if (inherit_as ||
			    asid_contains(x->rfc3779_asid->asnum->u.asIdsOrRanges,
			    child_as)) {
				child_as = x->rfc3779_asid->asnum->u.asIdsOrRanges;
				inherit_as = 0;
			} else {
				validation_err(X509_V_ERR_UNNESTED_RESOURCE);
			}
		}
		if (x->rfc3779_asid->rdi == NULL && child_rdi != NULL) {
			validation_err(X509_V_ERR_UNNESTED_RESOURCE);
			child_rdi = NULL;
			inherit_rdi = 0;
		}
		if (x->rfc3779_asid->rdi != NULL &&
		    x->rfc3779_asid->rdi->type == ASIdentifierChoice_asIdsOrRanges) {
			if (inherit_rdi ||
			    asid_contains(x->rfc3779_asid->rdi->u.asIdsOrRanges,
			    child_rdi)) {
				child_rdi = x->rfc3779_asid->rdi->u.asIdsOrRanges;
				inherit_rdi = 0;
			} else {
				validation_err(X509_V_ERR_UNNESTED_RESOURCE);
			}
		}
	}

	/*
	 * Trust anchor can't inherit.
	 */

	if (x == NULL)
		goto err;

	if (x->rfc3779_asid != NULL) {
		if (x->rfc3779_asid->asnum != NULL &&
		    x->rfc3779_asid->asnum->type == ASIdentifierChoice_inherit)
			validation_err(X509_V_ERR_UNNESTED_RESOURCE);
		if (x->rfc3779_asid->rdi != NULL &&
		    x->rfc3779_asid->rdi->type == ASIdentifierChoice_inherit)
			validation_err(X509_V_ERR_UNNESTED_RESOURCE);
	}

 done:
	return ret;

 err:
	if (ctx != NULL)
		ctx->error = X509_V_ERR_UNSPECIFIED;

	return 0;
}

#undef validation_err

/*
 * RFC 3779 3.3 path validation -- called from X509_verify_cert().
 */
int
X509v3_asid_validate_path(X509_STORE_CTX *ctx)
{
	if (sk_X509_num(ctx->chain) <= 0 || ctx->verify_cb == NULL) {
		ctx->error = X509_V_ERR_UNSPECIFIED;
		return 0;
	}
	return asid_validate_path_internal(ctx, ctx->chain, NULL);
}
LCRYPTO_ALIAS(X509v3_asid_validate_path);

/*
 * RFC 3779 3.3 path validation of an extension.
 * Test whether chain covers extension.
 */
int
X509v3_asid_validate_resource_set(STACK_OF(X509) *chain, ASIdentifiers *ext,
    int allow_inheritance)
{
	if (ext == NULL)
		return 1;
	if (sk_X509_num(chain) <= 0)
		return 0;
	if (!allow_inheritance && X509v3_asid_inherits(ext))
		return 0;
	return asid_validate_path_internal(NULL, chain, ext);
}
LCRYPTO_ALIAS(X509v3_asid_validate_resource_set);

#endif                          /* OPENSSL_NO_RFC3779 */
