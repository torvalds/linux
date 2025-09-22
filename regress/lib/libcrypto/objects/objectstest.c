/* $OpenBSD: objectstest.c,v 1.8 2023/05/23 11:06:52 tb Exp $ */
/*
 * Copyright (c) 2017, 2022 Joel Sing <jsing@openbsd.org>
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

#include <openssl/objects.h>

#include <err.h>
#include <stdio.h>
#include <string.h>

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
obj_compare_bytes(const char *label, const unsigned char *d1, int len1,
    const unsigned char *d2, int len2)
{
	if (len1 != len2) {
		fprintf(stderr, "FAIL: %s - byte lengths differ "
		    "(%d != %d)\n", label, len1, len2);
		fprintf(stderr, "Got:\n");
		hexdump(d1, len1);
		fprintf(stderr, "Want:\n");
		hexdump(d2, len2);
		return 0;
	}
	if (memcmp(d1, d2, len1) != 0) {
		fprintf(stderr, "FAIL: %s - bytes differ\n", label);
		fprintf(stderr, "Got:\n");
		hexdump(d1, len1);
		fprintf(stderr, "Want:\n");
		hexdump(d2, len2);
		return 0;
	}
	return 1;
}

struct obj_test {
	const char *oid;
	const char *sn;
	const char *ln;
	int nid;
	uint8_t data[255];
	size_t data_len;
};

struct obj_test obj_tests[] = {
	{
		.oid = NULL,
		.sn = "UNDEF",
		.ln = "undefined",
		.nid = NID_undef,
	},
	{
		.oid = "2.5.4.10",
		.sn = "O",
		.ln = "organizationName",
		.nid = NID_organizationName,
		.data = {
			0x55, 0x04, 0x0a,
		},
		.data_len = 3,
	},
	{
		.oid = "2.5.4.8",
		.sn = "ST",
		.ln = "stateOrProvinceName",
		.nid = NID_stateOrProvinceName,
		.data = {
			0x55, 0x04, 0x08,
		},
		.data_len = 3,
	},
	{
		.oid = "2.23.43.1",
		.sn = "wap-wsg",
		.nid = NID_wap_wsg,
		.data = {
			0x67, 0x2b, 0x01,
		},
		.data_len = 3,
	},
	{
		.oid = "1.3.6.1.4.1.11129.2.4.5",
		.sn = "ct_cert_scts",
		.ln = "CT Certificate SCTs",
		.nid = NID_ct_cert_scts,
		.data = {
			0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02,
			0x04, 0x05,
		},
		.data_len = 10,
	},
	{
		.oid = "1.3.6.1.4.1",
		.sn = "enterprises",
		.ln = "Enterprises",
		.nid = NID_Enterprises,
		.data = {
			0x2b, 0x06, 0x01, 0x04, 0x01,
		},
		.data_len = 5,
	},
	{
		.oid = "1.3.6.1.4.1.5454.1.70.6.11.2",
		.nid = NID_undef,
		.data = {
			0x2b, 0x06, 0x01, 0x04, 0x01, 0xaa, 0x4e, 0x01,
			0x46, 0x06, 0x0b, 0x02,
		},
		.data_len = 12,
	},
	{
		.oid = "1.3.6.1.4.1.890.1.5.8.60.102.2",
		.nid = NID_undef,
		.data = {
			0x2b, 0x06, 0x01, 0x04, 0x01, 0x86, 0x7a, 0x01,
			0x05, 0x08, 0x3c, 0x66, 0x02,
		},
		.data_len = 13,
	},
	{
		.oid = "1.3.6.1.4.1.173.7.3.4.1.1.26",
		.nid = NID_undef,
		.data = {
			0x2b, 0x06, 0x01, 0x04, 0x01, 0x81, 0x2d, 0x07,
			0x03, 0x04, 0x01, 0x01, 0x1a,
		},
		.data_len = 13,
	},
};

#define N_OBJ_TESTS (sizeof(obj_tests) / sizeof(*obj_tests))

static int
obj_name_test(struct obj_test *ot)
{
	const char *ln, *sn;
	int nid;
	int failed = 1;

	if (ot->ln != NULL) {
		if ((nid = OBJ_ln2nid(ot->ln)) != ot->nid) {
			fprintf(stderr, "FAIL: OBJ_ln2nid() for '%s' = %d, "
			    "want %d\n", ot->ln, nid, ot->nid);
			goto failed;
		}
		if ((ln = OBJ_nid2ln(ot->nid)) == NULL) {
			fprintf(stderr, "FAIL: OBJ_nid2ln() for '%s' returned "
			    "NULL\n", ot->oid);
			goto failed;
		}
		if (strcmp(ln, ot->ln) != 0) {
			fprintf(stderr, "FAIL: OBJ_nid2ln() for '%s' = '%s', "
			    "want '%s'\n", ot->oid, ln, ot->ln);
			goto failed;
		}
	}
	if (ot->sn != NULL) {
		if ((nid = OBJ_sn2nid(ot->sn)) != ot->nid) {
			fprintf(stderr, "FAIL: OBJ_sn2nid() for '%s' = %d, "
			    "want %d\n", ot->sn, nid, ot->nid);
			goto failed;
		}
		if ((sn = OBJ_nid2sn(ot->nid)) == NULL) {
			fprintf(stderr, "FAIL: OBJ_nid2sn() for '%s' returned "
			    "NULL\n", ot->oid);
			goto failed;
		}
		if (strcmp(sn, ot->sn) != 0) {
			fprintf(stderr, "FAIL: OBJ_nid2sn() for '%s' = '%s', "
			    "want '%s'\n", ot->oid, sn, ot->sn);
			goto failed;
		}
	}

	failed = 0;

 failed:
	return failed;
}

static int
obj_name_tests(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < N_OBJ_TESTS; i++)
		failed |= obj_name_test(&obj_tests[i]);

	return failed;
}

static int
obj_nid_test(struct obj_test *ot)
{
	ASN1_OBJECT *obj = NULL;
	int nid;
	int failed = 1;

	if (ot->nid == NID_undef && ot->oid != NULL)
		return 0;

	if ((obj = OBJ_nid2obj(ot->nid)) == NULL) {
		fprintf(stderr, "FAIL: OBJ_nid2obj() failed for '%s' (NID %d)\n",
		    ot->oid, ot->nid);
		goto failed;
	}
	if ((nid = OBJ_obj2nid(obj)) != ot->nid) {
		fprintf(stderr, "FAIL: OBJ_obj2nid() failed for '%s' - got %d, "
		    "want %d\n", ot->oid ? ot->oid : "undef", nid, ot->nid);
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_OBJECT_free(obj);

	return failed;
}

static int
obj_nid_tests(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < N_OBJ_TESTS; i++)
		failed |= obj_nid_test(&obj_tests[i]);

	return failed;
}

static int
obj_oid_test(struct obj_test *ot)
{
	ASN1_OBJECT *obj = NULL;
	char buf[1024];
	int len, nid;
	int failed = 1;

	if (ot->oid == NULL)
		return 0;

	if ((obj = OBJ_txt2obj(ot->oid, 0)) == NULL) {
		fprintf(stderr, "FAIL: OBJ_txt2obj() failed for '%s'\n", ot->oid);
		goto failed;
	}
	if ((nid = OBJ_txt2nid(ot->oid)) != ot->nid) {
		fprintf(stderr, "FAIL: OBJ_txt2nid() failed for '%s', got %d "
		    "want %d\n", ot->oid, nid, ot->nid);
		goto failed;
	}

	if (!obj_compare_bytes("object data", OBJ_get0_data(obj), OBJ_length(obj),
	    ot->data, ot->data_len))
		goto failed;

	len = OBJ_obj2txt(buf, sizeof(buf), obj, 1);
	if (len <= 0 || (size_t)len >= sizeof(buf)) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() failed for '%s'\n", ot->oid);
		goto failed;
	}
	if (strcmp(buf, ot->oid) != 0) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() returned '%s', want '%s'\n",
		    buf, ot->oid);
		goto failed;
	}

	if ((OBJ_obj2txt(NULL, 0, obj, 1) != len)) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() with NULL buffer != %d\n",
		    len);
		goto failed;
	}
	if ((OBJ_obj2txt(buf, 3, obj, 1) != len)) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() with short buffer != %d\n",
		    len);
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_OBJECT_free(obj);

	return failed;
}

static int
obj_oid_tests(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < N_OBJ_TESTS; i++)
		failed |= obj_oid_test(&obj_tests[i]);

	return failed;
}

static int
obj_txt_test(struct obj_test *ot)
{
	ASN1_OBJECT *obj = NULL;
	const char *want;
	char buf[1024];
	int len, nid;
	int failed = 1;

	if (ot->oid == NULL)
		return 0;

	if (ot->sn != NULL) {
		if ((obj = OBJ_txt2obj(ot->sn, 0)) == NULL) {
			fprintf(stderr, "FAIL: OBJ_txt2obj() failed for '%s'\n",
			    ot->sn);
			goto failed;
		}
		if ((nid = OBJ_obj2nid(obj)) != ot->nid) {
			fprintf(stderr, "FAIL: OBJ_txt2obj() failed for '%s', "
			    "got nid %d want %d\n", ot->sn, nid, ot->nid);
			goto failed;
		}
		ASN1_OBJECT_free(obj);
		obj = NULL;
	}
	if (ot->ln != NULL) {
		if ((obj = OBJ_txt2obj(ot->ln, 0)) == NULL) {
			fprintf(stderr, "FAIL: OBJ_txt2obj() failed for '%s'\n",
			    ot->ln);
			goto failed;
		}
		if ((nid = OBJ_obj2nid(obj)) != ot->nid) {
			fprintf(stderr, "FAIL: OBJ_txt2obj() failed for '%s', "
			    "got nid %d want %d\n", ot->ln, nid, ot->nid);
			goto failed;
		}
		ASN1_OBJECT_free(obj);
		obj = NULL;
	}

	if ((obj = OBJ_txt2obj(ot->oid, 0)) == NULL) {
		fprintf(stderr, "FAIL: OBJ_txt2obj() failed for '%s'\n", ot->oid);
		goto failed;
	}
	if ((nid = OBJ_obj2nid(obj)) != ot->nid) {
		fprintf(stderr, "FAIL: OBJ_txt2obj() failed for '%s', "
		    "got nid %d want %d\n", ot->oid, nid, ot->nid);
		goto failed;
	}

	len = OBJ_obj2txt(buf, sizeof(buf), obj, 0);
	if (len <= 0 || (size_t)len >= sizeof(buf)) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() failed for '%s'\n", ot->oid);
		goto failed;
	}
	want = ot->ln;
	if (want == NULL)
		want = ot->sn;
	if (want == NULL)
		want = ot->oid;
	if (strcmp(buf, want) != 0) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() returned '%s', want '%s'\n",
		    buf, want);
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_OBJECT_free(obj);

	return failed;
}

static int
obj_txt_early_nul_test(void)
{
	ASN1_OBJECT *obj = NULL;
	char buf[2];
	int failed = 1;

	buf[0] = 'x';
	buf[1] = '\0';

	if (OBJ_obj2txt(buf, sizeof(buf), NULL, 1) != 0) {
		fprintf(stderr, "FAIL: OBJ_obj2txt(NULL) succeded\n");
		goto failed;
	}
	if (buf[0] != '\0') {
		fprintf(stderr, "FAIL: OBJ_obj2txt(NULL) did not NUL terminate\n");
		goto failed;
	}

	if ((obj = ASN1_OBJECT_new()) == NULL)
		errx(1, "ASN1_OBJECT_new");

	buf[0] = 'x';
	buf[1] = '\0';

	if (OBJ_obj2txt(buf, sizeof(buf), obj, 1) != 0) {
		fprintf(stderr, "FAIL: OBJ_obj2txt(obj) succeeded\n");
		goto failed;
	}
	if (buf[0] != '\0') {
		fprintf(stderr, "FAIL: OBJ_obj2txt(obj) did not NUL terminate\n");
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_OBJECT_free(obj);

	return failed;
}

static int
obj_txt_tests(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < N_OBJ_TESTS; i++)
		failed |= obj_txt_test(&obj_tests[i]);

	failed |= obj_txt_early_nul_test();

	return failed;
}

/* OID 1.3.18446744073709551615 (64 bits). */
const uint8_t asn1_large_oid1[] = {
	0x06, 0x0b,
	0x2b, 0x81, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x7f,
};

/* OID 1.3.18446744073709551616 (65 bits). */
const uint8_t asn1_large_oid2[] = {
	0x06, 0x0b,
	0x2b, 0x82, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x00,
};

/* OID 1.3.340282366920938463463374607431768211455 (128 bits). */
const uint8_t asn1_large_oid3[] = {
	0x06, 0x14,
	0x2b, 0x83, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x7f,
};

/* OID 1.3.115792089237316195423570985008687907853269984665640564039457584007913129639935 (256 bits). */
const uint8_t asn1_large_oid4[] = {
	0x06, 0x26,
	0x2b, 0x8f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
};

struct oid_large_test {
	const char *oid;
	const uint8_t *asn1_der;
	size_t asn1_der_len;
	int obj2txt;
};

struct oid_large_test oid_large_tests[] = {
	{
		.oid = "1.3.18446744073709551615",
		.asn1_der = asn1_large_oid1,
		.asn1_der_len = sizeof(asn1_large_oid1),
		.obj2txt = 1,
	},
	{
		.oid = "1.3.18446744073709551616",
		.asn1_der = asn1_large_oid2,
		.asn1_der_len = sizeof(asn1_large_oid2),
		.obj2txt = 0,
	},
	{
		.oid = "1.3.340282366920938463463374607431768211455",
		.asn1_der = asn1_large_oid3,
		.asn1_der_len = sizeof(asn1_large_oid3),
		.obj2txt = 0,
	},
	{
		.oid = "1.3.115792089237316195423570985008687907853269984665640"
		    "564039457584007913129639935",
		.asn1_der = asn1_large_oid4,
		.asn1_der_len = sizeof(asn1_large_oid4),
		.obj2txt = 0,
	},
};

#define N_OID_LARGE_TESTS (sizeof(oid_large_tests) / sizeof(*oid_large_tests))

static int
obj_oid_large_test(size_t test_no, struct oid_large_test *olt)
{
	ASN1_OBJECT *obj = NULL;
	const uint8_t *p;
	char buf[1024];
	int len;
	int failed = 1;

	p = olt->asn1_der;
	if ((obj = d2i_ASN1_OBJECT(NULL, &p, olt->asn1_der_len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_ASN1_OBJECT() failed for large "
		    "oid %zu\n", test_no);
		goto failed;
	}
	len = OBJ_obj2txt(buf, sizeof(buf), obj, 1);
	if (len < 0 || (size_t)len >= sizeof(buf)) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() failed for large "
		    "oid %zu\n", test_no);
		goto failed;
	}
	if ((len != 0) != olt->obj2txt) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() failed for large "
		    "oid %zu\n", test_no);
		goto failed;
	}
	if (len != 0 && strcmp(buf, olt->oid) != 0) {
		fprintf(stderr, "FAIL: OBJ_obj2txt() returned '%s', want '%s'\n",
		    buf, olt->oid);
		goto failed;
	}

	failed = 0;

 failed:
	ASN1_OBJECT_free(obj);

	return failed;
}

static int
obj_oid_large_tests(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < N_OID_LARGE_TESTS; i++)
		failed |= obj_oid_large_test(i, &oid_large_tests[i]);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= obj_name_tests();
	failed |= obj_nid_tests();
	failed |= obj_oid_tests();
	failed |= obj_txt_tests();
	failed |= obj_oid_large_tests();

	return (failed);
}
