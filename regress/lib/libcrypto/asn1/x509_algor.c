/*	$OpenBSD: x509_algor.c,v 1.7 2024/02/29 20:03:47 tb Exp $ */
/*
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
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

#include <err.h>
#include <stdio.h>

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

int X509_ALGOR_set_evp_md(X509_ALGOR *alg, const EVP_MD *md);

static int
x509_algor_new_test(void)
{
	X509_ALGOR *alg = NULL;
	const ASN1_OBJECT *aobj;
	int failed = 1;

	if ((alg = X509_ALGOR_new()) == NULL)
		errx(1, "%s: X509_ALGOR_new", __func__);

	if ((aobj = OBJ_nid2obj(NID_undef)) == NULL)
		errx(1, "%s: OBJ_nid2obj", __func__);

	if (alg->algorithm != aobj) {
		fprintf(stderr, "FAIL: %s: want NID_undef OID\n", __func__);
		goto failure;
	}
	if (alg->parameter != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL parameters\n", __func__);
		goto failure;
	}

	failed = 0;

 failure:
	X509_ALGOR_free(alg);

	return failed;
}

static int
x509_algor_set0_test(void)
{
	X509_ALGOR *alg = NULL;
	ASN1_TYPE *old_parameter;
	ASN1_OBJECT *oid;
	ASN1_INTEGER *aint = NULL, *aint_ref;
	int ret;
	int failed = 1;

	if ((ret = X509_ALGOR_set0(NULL, NULL, 0, NULL)) != 0) {
		fprintf(stderr, "FAIL: %s: X509_ALGOR_set0(NULL, NULL, 0, NULL)"
		    ", want: %d, got %d\n", __func__, 0, ret);
		goto failure;
	}

	if ((alg = X509_ALGOR_new()) == NULL)
		errx(1, "%s: X509_ALGOR_new", __func__);

	/* This sets algorithm to NULL and allocates new parameters. */
	if ((ret = X509_ALGOR_set0(alg, NULL, 0, NULL)) != 1) {
		fprintf(stderr, "FAIL: %s: X509_ALGOR_set0(alg, NULL, 0, NULL)"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	if (alg->algorithm != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL algorithm after "
		    "X509_ALGOR_set0(alg, NULL, 0, NULL)\n", __func__);
		goto failure;
	}
	if ((old_parameter = alg->parameter) == NULL) {
		fprintf(stderr, "FAIL: %s: want non-NULL parameter after "
		    "X509_ALGOR_set0(alg, NULL, 0, NULL)\n", __func__);
		goto failure;
	}
	if (alg->parameter->type != V_ASN1_UNDEF) {
		fprintf(stderr, "FAIL: %s: want %d parameter type after "
		    "X509_ALGOR_set0(alg, NULL, 0, NULL), got %d\n",
		    __func__, V_ASN1_UNDEF, alg->parameter->type);
		goto failure;
	}
	if (alg->parameter->value.ptr != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL parameter value after "
		    "X509_ALGOR_set0(alg, NULL, 0, NULL)\n", __func__);
		goto failure;
	}

	/* This should leave algorithm at NULL and parameters untouched. */
	if ((ret = X509_ALGOR_set0(alg, NULL, 0, NULL)) != 1) {
		fprintf(stderr, "FAIL: %s: X509_ALGOR_set0(alg, NULL, 0, NULL)"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	if (alg->algorithm != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL algorithm after second"
		    "X509_ALGOR_set0(alg, NULL, 0, NULL)\n", __func__);
		goto failure;
	}
	if (alg->parameter != old_parameter) {
		fprintf(stderr, "FAIL: %s: parameter changed after second"
		    "X509_ALGOR_set0(alg, NULL, 0, NULL)\n", __func__);
		goto failure;
	}

	/* This ignores pval (old_parameter). */
	if ((ret = X509_ALGOR_set0(alg, NULL, 0, old_parameter)) != 1) {
		fprintf(stderr, "FAIL: %s: X509_ALGOR_set0(alg, NULL, 0, ptr)"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	if (alg->algorithm != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL algorithm after "
		    "X509_ALGOR_set0(alg, NULL, 0, ptr)\n", __func__);
		goto failure;
	}
	if (alg->parameter == NULL) {
		fprintf(stderr, "FAIL: %s: want non-NULL parameter after "
		    "X509_ALGOR_set0(alg, NULL, 0, ptr)\n", __func__);
		goto failure;
	}
	if (alg->parameter->type != V_ASN1_UNDEF) {
		fprintf(stderr, "FAIL: %s: want %d parameter type after "
		    "X509_ALGOR_set0(alg, NULL, 0, ptr), got %d\n",
		    __func__, V_ASN1_UNDEF, alg->parameter->type);
		goto failure;
	}
	if (alg->parameter->value.ptr != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL parameter value after "
		    "X509_ALGOR_set0(alg, NULL, 0, ptr)\n", __func__);
		goto failure;
	}

	old_parameter = NULL;

	/* This frees parameters and ignores pval. */
	if ((ret = X509_ALGOR_set0(alg, NULL, V_ASN1_UNDEF, NULL)) != 1) {
		fprintf(stderr, "FAIL: %s: "
		    "X509_ALGOR_set0(alg, NULL, V_ASN1_UNDEF, NULL)"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	if (alg->algorithm != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL algorithm after "
		    "X509_ALGOR_set0(alg, NULL, V_ASN1_UNDEF, NULL)\n", __func__);
		goto failure;
	}
	if (alg->parameter != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL parameter after "
		    "X509_ALGOR_set0(alg, NULL, V_ASN1_UNDEF, NULL)\n", __func__);
		goto failure;
	}

	/* This frees parameters and ignores "foo". */
	if ((ret = X509_ALGOR_set0(alg, NULL, V_ASN1_UNDEF, "foo")) != 1) {
		fprintf(stderr, "FAIL: %s: X509_ALGOR_set0(alg, NULL, 0, \"foo\")"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	if (alg->algorithm != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL algorithm after "
		    "X509_ALGOR_set0(alg, NULL, V_ASN1_UNDEF, \"foo\")\n", __func__);
		goto failure;
	}
	if (alg->parameter != NULL) {
		fprintf(stderr, "FAIL: %s: want NULL parameter after "
		    "X509_ALGOR_set0(alg, NULL, V_ASN1_UNDEF, \"foo\")\n", __func__);
		goto failure;
	}

	if ((oid = OBJ_nid2obj(NID_sha512_224)) == NULL) {
		fprintf(stderr, "FAIL: %s: OBJ_nid2obj(NID_sha512_224)\n", __func__);
		goto failure;
	}
	if ((aint = aint_ref = ASN1_INTEGER_new()) == NULL)
		errx(1, "%s: ASN1_INTEGER_new()", __func__);
	if (!ASN1_INTEGER_set_uint64(aint, 57))
		errx(1, "%s: ASN1_INTEGER_set_uint64()", __func__);

	if ((ret = X509_ALGOR_set0(alg, oid, V_ASN1_INTEGER, aint)) != 1) {
		fprintf(stderr, "Fail: %s: "
		    "X509_ALGOR_set0(alg, oid, V_ASN1_INTEGER, aint)"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	aint = NULL;
	if (alg->algorithm != oid) {
		fprintf(stderr, "FAIL: %s: unexpected oid on alg after "
		    "X509_ALGOR_set0(alg, oid, V_ASN1_INTEGER, aint)"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	if (alg->parameter == NULL) {
		fprintf(stderr, "FAIL: %s: expected non-NULL parameter after "
		    "X509_ALGOR_set0(alg, oid, V_ASN1_INTEGER, aint)"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	if (alg->parameter->type != V_ASN1_INTEGER) {
		fprintf(stderr, "FAIL: %s: want %d parameter type after "
		    "X509_ALGOR_set0(alg, oid, V_ASN1_INTEGER, aint), got %d\n",
		    __func__, V_ASN1_INTEGER, alg->parameter->type);
		goto failure;
	}
	if (alg->parameter->value.asn1_string != aint_ref) {
		fprintf(stderr, "FAIL: %s: unexpected parameter value after "
		    "X509_ALGOR_set0(alg, oid, V_ASN1_NULL, aint)\n", __func__);
		goto failure;
	}

	failed = 0;

 failure:
	X509_ALGOR_free(alg);
	ASN1_INTEGER_free(aint);

	return failed;
}

static int
x509_algor_get0_test(void)
{
	X509_ALGOR *alg;
	const ASN1_OBJECT *aobj = NULL;
	int ptype = 0;
	const void *pval = NULL;
	ASN1_OBJECT *oid;
	ASN1_INTEGER *aint = NULL, *aint_ref = NULL;
	int ret;
	int failed = 1;

	if ((alg = X509_ALGOR_new()) == NULL)
		errx(1, "%s: X509_ALGOR_new", __func__);

	X509_ALGOR_get0(&aobj, NULL, NULL, alg);
	if (aobj == NULL) {
		fprintf(stderr, "FAIL: %s: expected non-NULL aobj\n", __func__);
		goto failure;
	}
	X509_ALGOR_get0(NULL, &ptype, NULL, alg);
	if (ptype != V_ASN1_UNDEF) {
		fprintf(stderr, "FAIL: %s: want %d, got %d\n",
		    __func__, V_ASN1_UNDEF, ptype);
		goto failure;
	}

	if ((oid = OBJ_nid2obj(NID_ED25519)) == NULL)
		errx(1, "%s: OBJ_nid2obj(NID_ED25519)", __func__);
	if ((aint = aint_ref = ASN1_INTEGER_new()) == NULL)
		errx(1, "%s: ASN1_INTEGER_new()", __func__);
	if (!ASN1_INTEGER_set_uint64(aint, 99))
		errx(1, "%s: ASN1_INTEGER_set_uint64()", __func__);

	if ((ret = X509_ALGOR_set0(alg, oid, V_ASN1_INTEGER, aint)) != 1) {
		fprintf(stderr, "Fail: %s: "
		    "X509_ALGOR_set0(alg, oid, V_ASN1_INTEGER, aint)"
		    ", want: %d, got %d\n", __func__, 1, ret);
		goto failure;
	}
	aint = NULL;

	X509_ALGOR_get0(&aobj, NULL, NULL, alg);
	if (aobj != oid) {
		fprintf(stderr, "FAIL: %s: expected Ed25519 oid\n", __func__);
		goto failure;
	}
	X509_ALGOR_get0(NULL, &ptype, NULL, alg);
	if (ptype != V_ASN1_INTEGER) {
		fprintf(stderr, "FAIL: %s: expected %d, got %d\n",
		    __func__, V_ASN1_INTEGER, ptype);
		goto failure;
	}
	pval = oid;
	X509_ALGOR_get0(NULL, NULL, &pval, alg);
	if (pval != NULL) {
		fprintf(stderr, "FAIL: %s: got non-NULL pval\n", __func__);
		goto failure;
	}

	aobj = NULL;
	ptype = V_ASN1_UNDEF;
	pval = oid;
	X509_ALGOR_get0(&aobj, &ptype, &pval, alg);
	if (aobj != oid) {
		fprintf(stderr, "FAIL: %s: expected Ed25519 oid 2\n", __func__);
		goto failure;
	}
	if (ptype != V_ASN1_INTEGER) {
		fprintf(stderr, "FAIL: %s: expected %d, got %d 2\n",
		    __func__, V_ASN1_INTEGER, ptype);
		goto failure;
	}
	if (pval != aint_ref) {
		fprintf(stderr, "FAIL: %s: expected ASN.1 integer\n", __func__);
		goto failure;
	}

	failed = 0;

 failure:
	X509_ALGOR_free(alg);
	ASN1_INTEGER_free(aint);

	return failed;
}

static int
x509_algor_set_evp_md_test(void)
{
	X509_ALGOR *alg = NULL;
	const ASN1_OBJECT *aobj;
	int ptype = 0, nid = 0;
	int failed = 1;

	if ((alg = X509_ALGOR_new()) == NULL)
		errx(1, "%s: X509_ALGOR_new", __func__);

	if (!X509_ALGOR_set_evp_md(alg, EVP_sm3())) {
		fprintf(stderr, "%s: X509_ALGOR_set_evp_md to sm3 failed\n",
		    __func__);
		goto failure;
	}
	X509_ALGOR_get0(&aobj, &ptype, NULL, alg);
	if ((nid = OBJ_obj2nid(aobj)) != NID_sm3) {
		fprintf(stderr, "%s: sm3 want %d, got %d\n", __func__,
		    NID_sm3, nid);
		goto failure;
	}
	if (ptype != V_ASN1_UNDEF) {
		fprintf(stderr, "%s: sm3 want %d, got %d\n", __func__,
		    V_ASN1_UNDEF, ptype);
		goto failure;
	}

	/* Preallocate as recommended in the manual. */
	if (!X509_ALGOR_set0(alg, NULL, 0, NULL))
		errx(1, "%s: X509_ALGOR_set0", __func__);

	if (!X509_ALGOR_set_evp_md(alg, EVP_md5())) {
		fprintf(stderr, "%s: X509_ALGOR_set_evp_md to md5 failed\n",
		    __func__);
		goto failure;
	}
	X509_ALGOR_get0(&aobj, &ptype, NULL, alg);
	if ((nid = OBJ_obj2nid(aobj)) != NID_md5) {
		fprintf(stderr, "%s: md5 want %d, got %d\n", __func__,
		    NID_sm3, nid);
		goto failure;
	}
	if (ptype != V_ASN1_NULL) {
		fprintf(stderr, "%s: md5 want %d, got %d\n", __func__,
		    V_ASN1_NULL, ptype);
		goto failure;
	}

	failed = 0;

 failure:
	X509_ALGOR_free(alg);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= x509_algor_new_test();
	failed |= x509_algor_set0_test();
	failed |= x509_algor_get0_test();
	failed |= x509_algor_set_evp_md_test();

	return failed;
}
