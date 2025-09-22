/* $OpenBSD: asn1time.c,v 1.31 2025/05/22 04:54:14 joshua Exp $ */
/*
 * Copyright (c) 2015 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2024 Google Inc.
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

#include <openssl/asn1.h>
#include <openssl/posix_time.h>

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "asn1_local.h"

int ASN1_UTCTIME_cmp_time_t(const ASN1_UTCTIME *s, time_t t);

struct asn1_time_test {
	const char *str;
	const char *data;
	const unsigned char der[32];
	time_t time;
	int generalized_time;
};

static const struct asn1_time_test asn1_invtime_tests[] = {
	{
		.str = "",
	},
	{
		.str = "2015",
	},
	{
		.str = "201509",
	},
	{
		.str = "20150923",
	},
	{
		.str = "20150923032700",
	},
	{
		.str = "20150923032700.Z",
	},
	{
		.str = "20150923032700.123",
	},
	{
		.str = "20150923032700+1.09",
	},
	{
		.str = "20150923032700+1100Z",
	},
	{
		.str = "20150923032700-11001",
	},
	{
		/* UTC time cannot have fractional seconds. */
		.str = "150923032700.123Z",
	},
	{
		.str = "aaaaaaaaaaaaaaZ",
	},
	{
		/* UTC time with omitted seconds, should fail */
		.str = "1609082343Z",
	},
	{
		/* Generalized time with omitted seconds, should fail */
		.str = "201612081934Z",
		.generalized_time = 1,
	},
	{
		/* Valid UTC time, should fail as a generalized time */
		.str = "160908234300Z",
		.generalized_time = 1,
	},
};

static const struct asn1_time_test asn1_gentime_tests[] = {
	{
		.str = "20161208193400Z",
		.data = "20161208193400Z",
		.time = 1481225640,
		.der = {
			0x18, 0x0f, 0x32, 0x30, 0x31, 0x36, 0x31, 0x32,
			0x30, 0x38, 0x31, 0x39, 0x33, 0x34, 0x30, 0x30,
			0x5a,
		},
	},
	{
		.str = "19700101000000Z",
		.data = "19700101000000Z",
		.time = 0,
		.der = {
			0x18, 0x0f, 0x31, 0x39, 0x37, 0x30, 0x30, 0x31,
			0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
			0x5a,
		},
	},
	{
		.str = "20150923032700Z",
		.data = "20150923032700Z",
		.time = 1442978820,
		.der = {
			0x18, 0x0f, 0x32, 0x30, 0x31, 0x35, 0x30, 0x39,
			0x32, 0x33, 0x30, 0x33, 0x32, 0x37, 0x30, 0x30,
			0x5a,
		},
	},
	{
		/* 1 second after the 32-bit epoch wraps. */
		.str = "20380119031408Z",
		.data = "20380119031408Z",
		.time = 2147483648LL,
		.der = {
			0x18, 0x0f, 0x32, 0x30, 0x33, 0x38, 0x30, 0x31,
			0x31, 0x39, 0x30, 0x33, 0x31, 0x34, 0x30, 0x38,
			0x5a,
		},

	},
};

static const struct asn1_time_test asn1_utctime_tests[] = {
	{
		.str = "700101000000Z",
		.data = "700101000000Z",
		.time = 0,
		.der = {
			0x17, 0x0d, 0x37, 0x30, 0x30, 0x31, 0x30, 0x31,
			0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a,
		},
	},
	{
		.str = "150923032700Z",
		.data = "150923032700Z",
		.time = 1442978820,
		.der = {
			0x17, 0x0d, 0x31, 0x35, 0x30, 0x39, 0x32, 0x33,
			0x30, 0x33, 0x32, 0x37, 0x30, 0x30, 0x5a,
		},
	},
	{
		.str = "140524144512Z",
		.data = "140524144512Z",
		.time = 1400942712,
		.der = {
			0x17, 0x0d, 0x31, 0x34, 0x30, 0x35, 0x32, 0x34,
			0x31, 0x34, 0x34, 0x35, 0x31, 0x32, 0x5a,
		},
	},
	{
		.str = "240401144512Z",
		.data = "240401144512Z",
		.time = 1711982712,
		.der = {
			0x17, 0x0d, 0x32, 0x34, 0x30, 0x34, 0x30, 0x31,
			0x31, 0x34, 0x34, 0x35, 0x31, 0x32, 0x5a
		},
	},
};

#define N_INVTIME_TESTS \
    (sizeof(asn1_invtime_tests) / sizeof(*asn1_invtime_tests))
#define N_INVGENTIME_TESTS \
    (sizeof(asn1_invgentime_tests) / sizeof(*asn1_invgentime_tests))
#define N_GENTIME_TESTS \
    (sizeof(asn1_gentime_tests) / sizeof(*asn1_gentime_tests))
#define N_UTCTIME_TESTS \
    (sizeof(asn1_utctime_tests) / sizeof(*asn1_utctime_tests))

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
asn1_compare_bytes(int test_no, const unsigned char *d1,
    const unsigned char *d2, int len1, int len2)
{
	if (len1 != len2) {
		fprintf(stderr, "FAIL: test %d - byte lengths differ "
		    "(%d != %d)\n", test_no, len1, len2);
		return (1);
	}
	if (memcmp(d1, d2, len1) != 0) {
		fprintf(stderr, "FAIL: test %d - bytes differ\n", test_no);
		fprintf(stderr, "Got:\n");
		hexdump(d1, len1);
		fprintf(stderr, "Want:\n");
		hexdump(d2, len2);
		return (1);
	}
	return (0);
}

static int
asn1_compare_str(int test_no, const struct asn1_string_st *asn1str,
    const char *str)
{
	int length = strlen(str);

	if (asn1str->length != length) {
		fprintf(stderr, "FAIL: test %d - string lengths differ "
		    "(%d != %d)\n", test_no, asn1str->length, length);
		return (1);
	}
	if (strncmp(asn1str->data, str, length) != 0) {
		fprintf(stderr, "FAIL: test %d - strings differ "
		    "('%s' != '%s')\n", test_no, asn1str->data, str);
		return (1);
	}

	return (0);
}

static int
asn1_invtime_test(int test_no, const struct asn1_time_test *att)
{
	ASN1_GENERALIZEDTIME *gt = NULL;
	ASN1_UTCTIME *ut = NULL;
	ASN1_TIME *t = NULL;
	int failure = 1;

	if ((gt = ASN1_GENERALIZEDTIME_new()) == NULL)
		goto done;
	if ((ut = ASN1_UTCTIME_new()) == NULL)
		goto done;
	if ((t = ASN1_TIME_new()) == NULL)
		goto done;

	if (ASN1_GENERALIZEDTIME_set_string(gt, att->str) != 0) {
		fprintf(stderr, "FAIL: test %d - successfully set "
		    "GENERALIZEDTIME string '%s'\n", test_no, att->str);
		goto done;
	}

	if (att->generalized_time)  {
		failure = 0;
		goto done;
	}

	if (ASN1_UTCTIME_set_string(ut, att->str) != 0) {
		fprintf(stderr, "FAIL: test %d - successfully set UTCTIME "
		    "string '%s'\n", test_no, att->str);
		goto done;
	}
	if (ASN1_TIME_set_string(t, att->str) != 0) {
		fprintf(stderr, "FAIL: test %d - successfully set TIME "
		    "string '%s'\n", test_no, att->str);
		goto done;
	}
	if (ASN1_TIME_set_string_X509(t, att->str) != 0) {
		fprintf(stderr, "FAIL: test %d - successfully set x509 TIME "
		    "string '%s'\n", test_no, att->str);
		goto done;
	}

	failure = 0;

 done:
	ASN1_GENERALIZEDTIME_free(gt);
	ASN1_UTCTIME_free(ut);
	ASN1_TIME_free(t);

	return (failure);
}

static int
asn1_gentime_test(int test_no, const struct asn1_time_test *att)
{
	const unsigned char *der;
	unsigned char *p = NULL;
	ASN1_GENERALIZEDTIME *gt = NULL;
	time_t t;
	int failure = 1;
	int len;
	struct tm tm;

	if (ASN1_GENERALIZEDTIME_set_string(NULL, att->str) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string '%s'\n",
		    test_no, att->str);
		goto done;
	}

	if ((gt = ASN1_GENERALIZEDTIME_new()) == NULL)
		goto done;

	if (ASN1_GENERALIZEDTIME_set_string(gt, att->str) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string '%s'\n",
		    test_no, att->str);
		goto done;
	}
	if (asn1_compare_str(test_no, gt, att->str) != 0)
		goto done;

	if (ASN1_TIME_to_tm(gt, &tm) == 0)  {
		fprintf(stderr, "FAIL: test %d - ASN1_time_to_tm failed '%s'\n",
		    test_no, att->str);
		goto done;
	}

	if (!OPENSSL_timegm(&tm, &t)) {
		/* things with crappy time_t should die in fire */
		fprintf(stderr, "FAIL: test %d - OPENSSL_timegm failed\n",
		    test_no);
	}

	if (t != att->time) {
		/* things with crappy time_t should die in fire */
		int64_t a = t, b = att->time;

		fprintf(stderr, "FAIL: test %d - times don't match, "
		    "expected %lld got %lld\n",
		    test_no, (long long)b, (long long)a);
		goto done;
	}

	if ((len = i2d_ASN1_GENERALIZEDTIME(gt, &p)) <= 0) {
		fprintf(stderr, "FAIL: test %d - i2d_ASN1_GENERALIZEDTIME "
		    "failed\n", test_no);
		goto done;
	}
	der = att->der;
	if (asn1_compare_bytes(test_no, p, der, len, strlen(der)) != 0)
		goto done;

	len = strlen(att->der);
	if (d2i_ASN1_GENERALIZEDTIME(&gt, &der, len) == NULL) {
		fprintf(stderr, "FAIL: test %d - d2i_ASN1_GENERALIZEDTIME "
		    "failed\n", test_no);
		goto done;
	}
	if (asn1_compare_str(test_no, gt, att->str) != 0)
		goto done;

	ASN1_GENERALIZEDTIME_free(gt);

	if ((gt = ASN1_GENERALIZEDTIME_set(NULL, att->time)) == NULL) {
		fprintf(stderr, "FAIL: test %d - failed to set time %lld\n",
		    test_no, (long long)att->time);
		goto done;
	}
	if (asn1_compare_str(test_no, gt, att->data) != 0)
		goto done;

	failure = 0;

 done:
	ASN1_GENERALIZEDTIME_free(gt);
	free(p);

	return (failure);
}

static int
asn1_utctime_test(int test_no, const struct asn1_time_test *att)
{
	const unsigned char *der;
	unsigned char *p = NULL;
	ASN1_UTCTIME *ut = NULL;
	int failure = 1;
	int len;

	if (ASN1_UTCTIME_set_string(NULL, att->str) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string '%s'\n",
		    test_no, att->str);
		goto done;
	}

	if ((ut = ASN1_UTCTIME_new()) == NULL)
		goto done;

	if (ASN1_UTCTIME_set_string(ut, att->str) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string '%s'\n",
		    test_no, att->str);
		goto done;
	}
	if (asn1_compare_str(test_no, ut, att->str) != 0)
		goto done;

	if ((len = i2d_ASN1_UTCTIME(ut, &p)) <= 0) {
		fprintf(stderr, "FAIL: test %d - i2d_ASN1_UTCTIME failed\n",
		    test_no);
		goto done;
	}
	der = att->der;
	if (asn1_compare_bytes(test_no, p, der, len, strlen(der)) != 0)
		goto done;

	len = strlen(att->der);
	if (d2i_ASN1_UTCTIME(&ut, &der, len) == NULL) {
		fprintf(stderr, "FAIL: test %d - d2i_ASN1_UTCTIME failed\n",
		    test_no);
		goto done;
	}
	if (asn1_compare_str(test_no, ut, att->str) != 0)
		goto done;

	ASN1_UTCTIME_free(ut);

	if ((ut = ASN1_UTCTIME_set(NULL, att->time)) == NULL) {
		fprintf(stderr, "FAIL: test %d - failed to set time %lld\n",
		    test_no, (long long)att->time);
		goto done;
	}
	if (asn1_compare_str(test_no, ut, att->data) != 0)
		goto done;

	failure = 0;

 done:
	ASN1_UTCTIME_free(ut);
	free(p);

	return (failure);
}

static int
asn1_time_test(int test_no, const struct asn1_time_test *att, int type)
{
	ASN1_TIME *t = NULL, *tx509 = NULL;
	char *parsed_time = NULL;
	int failure = 1;

	if (ASN1_TIME_set_string(NULL, att->str) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string '%s'\n",
		    test_no, att->str);
		goto done;
	}

	if ((t = ASN1_TIME_new()) == NULL)
		goto done;

	if ((tx509 = ASN1_TIME_new()) == NULL)
		goto done;

	switch (strlen(att->str)) {
	case 13:
		t->type = V_ASN1_UTCTIME;
		if (ASN1_UTCTIME_set_string(t, att->str) != 1) {
			fprintf(stderr, "FAIL: test %d - failed to set utc "
			    "string '%s'\n",
			    test_no, att->str);
			goto done;
		}
		break;
	case 15:
		t->type = V_ASN1_GENERALIZEDTIME;
		if (ASN1_GENERALIZEDTIME_set_string(t, att->str) != 1) {
			fprintf(stderr, "FAIL: test %d - failed to set gen "
			    "string '%s'\n",
			    test_no, att->str);
			goto done;
		}
		break;
	default:
		fprintf(stderr, "FAIL: unknown type\n");
		goto done;
	}

	if (t->type != type) {
		fprintf(stderr, "FAIL: test %d - got type %d, want %d\n",
		    test_no, t->type, type);
		goto done;
	}

	if ((parsed_time = strdup(t->data)) == NULL)
		goto done;

	if (ASN1_TIME_normalize(t) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set normalize '%s'\n",
		    test_no, att->str);
		goto done;
	}

	if (ASN1_TIME_set_string_X509(tx509, parsed_time) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string X509 '%s'\n",
		    test_no, t->data);
		goto done;
	}

	if (t->type != tx509->type) {
		fprintf(stderr, "FAIL: test %d - type %d, different from %d\n",
		    test_no, t->type, tx509->type);
		goto done;
	}

	if (ASN1_TIME_compare(t, tx509) != 0) {
		fprintf(stderr, "FAIL: ASN1_TIME values differ!\n");
		goto done;
	}

	if (ASN1_TIME_set_string(tx509, parsed_time) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string X509 '%s'\n",
		    test_no, t->data);
		goto done;
	}

	if (t->type != tx509->type) {
		fprintf(stderr, "FAIL: test %d - type %d, different from %d\n",
		    test_no, t->type, tx509->type);
		goto done;
	}

	if (ASN1_TIME_compare(t, tx509) != 0) {
		fprintf(stderr, "FAIL: ASN1_TIME values differ!\n");
		goto done;
	}


	failure = 0;

 done:

	ASN1_TIME_free(t);
	ASN1_TIME_free(tx509);
	free(parsed_time);

	return (failure);
}

static int
time_t_cmp(time_t t1, time_t t2)
{
	if (t1 < t2)
		return -1;
	if (t2 < t1)
		return 1;
	return 0;
}

static int
asn1_time_compare_families(const struct asn1_time_test *fam1, size_t fam1_size,
    const struct asn1_time_test *fam2, size_t fam2_size)
{
	const struct asn1_time_test *att1, *att2;
	ASN1_TIME *t1 = NULL, *t2 = NULL;
	size_t i, j;
	int asn1_cmp, time_cmp;
	int comparison_failure = 0;
	int failure = 1;

	if ((t1 = ASN1_TIME_new()) == NULL)
		goto done;
	if ((t2 = ASN1_TIME_new()) == NULL)
		goto done;

	for (i = 0; i < fam1_size; i++) {
		att1 = &fam1[i];

		if (!ASN1_TIME_set_string(t1, att1->str))
			goto done;
		for (j = 0; j < fam2_size; j++) {
			att2 = &fam2[j];

			if (!ASN1_TIME_set_string(t2, att2->str))
				goto done;

			time_cmp = time_t_cmp(att1->time, att2->time);
			asn1_cmp = ASN1_TIME_compare(t1, t2);

			if (time_cmp != asn1_cmp) {
				fprintf(stderr, "ASN1_TIME_compare - %s vs. %s: "
				    "want %d, got %d\n",
				    att1->str, att2->str, time_cmp, asn1_cmp);
				comparison_failure |= 1;
			}

			time_cmp = ASN1_TIME_cmp_time_t(t1, att2->time);
			if (time_cmp != asn1_cmp) {
				fprintf(stderr, "ASN1_TIME_cmp_time_t - %s vs. %lld: "
				    "want %d, got %d\n",
				    att1->str, (long long)att2->time,
				    asn1_cmp, time_cmp);
				comparison_failure |= 1;
			}

			time_cmp = ASN1_UTCTIME_cmp_time_t(t1, att2->time);
			if (t1->type != V_ASN1_UTCTIME)
				asn1_cmp = -2;
			if (time_cmp != asn1_cmp) {
				fprintf(stderr, "ASN1_UTCTIME_cmp_time_t - %s vs. %lld: "
				    "want %d, got %d\n",
				    att1->str, (long long)att2->time,
				    asn1_cmp, time_cmp);
				comparison_failure |= 1;
			}
		}
	}

	failure = comparison_failure;

 done:
	ASN1_TIME_free(t1);
	ASN1_TIME_free(t2);

	return failure;
}

static int
asn1_time_compare_test(void)
{
	const struct asn1_time_test *gen = asn1_gentime_tests;
	size_t gen_size = N_GENTIME_TESTS;
	const struct asn1_time_test *utc = asn1_utctime_tests;
	size_t utc_size = N_UTCTIME_TESTS;
	int failed = 0;

	failed |= asn1_time_compare_families(gen, gen_size, gen, gen_size);
	failed |= asn1_time_compare_families(gen, gen_size, utc, utc_size);
	failed |= asn1_time_compare_families(utc, utc_size, gen, gen_size);
	failed |= asn1_time_compare_families(utc, utc_size, utc, utc_size);

	return failed;
}

static int
asn1_time_overflow(void)
{
	struct tm overflow_year = {0}, overflow_month = {0};
	struct tm copy, max_time = {0}, min_time = {0}, zero = {0};
	int64_t valid_time_range = INT64_C(315569519999);
	int64_t posix_u64;
	time_t posix_time;
	int days, secs;
	int failed = 1;

	overflow_year.tm_year = INT_MAX - 1899;
	overflow_year.tm_mday = 1;

	overflow_month.tm_mon = INT_MAX;
	overflow_month.tm_mday = 1;

	if (OPENSSL_tm_to_posix(&overflow_year, &posix_u64)) {
		fprintf(stderr, "FAIL: OPENSSL_tm_to_posix didn't fail on "
		    "overflow of years\n");
		goto err;
	}
	if (OPENSSL_tm_to_posix(&overflow_month, &posix_u64)) {
		fprintf(stderr, "FAIL: OPENSSL_tm_to_posix didn't fail on "
		    "overflow of months\n");
		goto err;
	}
	if (OPENSSL_timegm(&overflow_year, &posix_time)) {
		fprintf(stderr, "FAIL: OPENSSL_timegm didn't fail on "
		    "overflow of years\n");
		goto err;
	}
	if (OPENSSL_timegm(&overflow_month, &posix_time)) {
		fprintf(stderr, "FAIL: OPENSSL_timegm didn't fail on "
		    "overflow of months\n");
		goto err;
	}
	if (OPENSSL_gmtime_adj(&overflow_year, 0, 0)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj didn't fail on "
		    "overflow of years\n");
		goto err;
	}
	if (OPENSSL_gmtime_adj(&overflow_month, 0, 0)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj didn't fail on "
		    "overflow of months\n");
		goto err;
	}
	if (OPENSSL_gmtime_diff(&days, &secs, &overflow_year, &overflow_year)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_diff didn't fail on "
		    "overflow of years\n");
		goto err;
	}
	if (OPENSSL_gmtime_diff(&days, &secs, &overflow_month, &overflow_month)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_diff didn't fail on "
		    "overflow of months\n");
		goto err;
	}

	/* Input time is in range but adding one second puts it out of range. */
	max_time.tm_year = 9999 - 1900;
	max_time.tm_mon = 12 - 1;
	max_time.tm_mday = 31;
	max_time.tm_hour = 23;
	max_time.tm_min = 59;
	max_time.tm_sec = 59;

	copy = max_time;
	if (!OPENSSL_gmtime_adj(&copy, 0, 0)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by 0 sec didn't "
		    "succeed for maximum time\n");
		goto err;
	}
	if (memcmp(&copy, &max_time, sizeof(max_time)) != 0) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by 0 sec didn't "
		    "leave copy of max_time unmodified\n");
		goto err;
	}
	if (OPENSSL_gmtime_adj(&copy, 0, 1)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by 1 sec didn't "
		    "fail for maximum time\n");
		goto err;
	}
	if (memcmp(&zero, &copy, sizeof(copy)) != 0) {
		fprintf(stderr, "FAIL: failing OPENSSL_gmtime_adj didn't "
		    "zero out max_time\n");
		goto err;
	}

	min_time.tm_year = 0 - 1900;
	min_time.tm_mon = 1 - 1;
	min_time.tm_mday = 1;
	min_time.tm_hour = 0;
	min_time.tm_min = 0;
	min_time.tm_sec = 0;

	copy = min_time;
	if (!OPENSSL_gmtime_adj(&copy, 0, 0)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by 0 sec didn't "
		    "succeed for minimum time\n");
		goto err;
	}
	if (memcmp(&copy, &min_time, sizeof(min_time)) != 0) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by 0 sec didn't "
		    "leave copy of min_time unmodified\n");
		goto err;
	}
	if (OPENSSL_gmtime_adj(&copy, 0, -1)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by 1 sec didn't "
		    "fail for minimum time\n");
		goto err;
	}
	if (memcmp(&zero, &copy, sizeof(copy)) != 0) {
		fprintf(stderr, "FAIL: failing OPENSSL_gmtime_adj didn't "
		    "zero out max_time\n");
		goto err;
	}

	copy = min_time;
	/* Test that we can offset by the valid minimum and maximum times. */
	if (!OPENSSL_gmtime_adj(&copy, 0, valid_time_range)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by maximum range "
		    "failed\n");
		goto err;
	}
	if (memcmp(&copy, &max_time, sizeof(max_time)) != 0) {
		fprintf(stderr, "FAIL: maximally adjusted copy didn't match "
		    "max_time\n");
		goto err;
	}
	if (!OPENSSL_gmtime_adj(&copy, 0, -valid_time_range)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by maximum range "
		    "failed\n");
		goto err;
	}
	if (memcmp(&copy, &min_time, sizeof(min_time)) != 0) {
		fprintf(stderr, "FAIL: maximally adjusted copy didn't match "
		    "min_time\n");
		goto err;
	}

	/*
	 * The second offset may even exceed the valid_time_range if it is
	 * cancelled out by offset_day.
	 */
	if (!OPENSSL_gmtime_adj(&copy, -1, valid_time_range + 24 * 3600)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by maximum range "
		    "failed\n");
		goto err;
	}
	if (memcmp(&copy, &max_time, sizeof(max_time)) != 0) {
		fprintf(stderr, "FAIL: excess maximally adjusted copy didn't "
		    "match max_time\n");
		goto err;
	}
	if (!OPENSSL_gmtime_adj(&copy, 1, -valid_time_range - 24 * 3600)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_adj by maximum range "
		    "failed\n");
		goto err;
	}
	if (memcmp(&copy, &min_time, sizeof(min_time)) != 0) {
		fprintf(stderr, "FAIL: excess maximally adjusted copy didn't "
		    "match min_time\n");
		goto err;
	}

	copy = max_time;
	if (OPENSSL_gmtime_adj(&copy, INT_MAX, INT64_MAX)) {
		fprintf(stderr, "FAIL: maximal adjustments in OPENSSL_gmtime_adj"
		    "didn't fail\n");
		goto err;
	}
	copy = min_time;
	if (OPENSSL_gmtime_adj(&copy, INT_MIN, INT64_MIN)) {
		fprintf(stderr, "FAIL: minimal adjustments in OPENSSL_gmtime_adj"
		    "didn't fail\n");
		goto err;
	}

	/* Test we can diff between maximum time and minimum time. */
	if (!OPENSSL_gmtime_diff(&days, &secs, &max_time, &min_time)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_diff between maximum and "
		    "minimum time failed\n");
		goto err;
	}
	if (!OPENSSL_gmtime_diff(&days, &secs, &min_time, &max_time)) {
		fprintf(stderr, "FAIL: OPENSSL_gmtime_diff between minimum and "
		    "maximum time failed\n");
		goto err;
	}


	failed = 0;

 err:
	return failed;
}

int
main(int argc, char **argv)
{
	const struct asn1_time_test *att;
	int failed = 0;
	size_t i;

	fprintf(stderr, "Invalid time tests...\n");
	for (i = 0; i < N_INVTIME_TESTS; i++) {
		att = &asn1_invtime_tests[i];
		failed |= asn1_invtime_test(i, att);
	}

	fprintf(stderr, "GENERALIZEDTIME tests...\n");
	for (i = 0; i < N_GENTIME_TESTS; i++) {
		att = &asn1_gentime_tests[i];
		failed |= asn1_gentime_test(i, att);
	}

	fprintf(stderr, "UTCTIME tests...\n");
	for (i = 0; i < N_UTCTIME_TESTS; i++) {
		att = &asn1_utctime_tests[i];
		failed |= asn1_utctime_test(i, att);
	}

	fprintf(stderr, "TIME tests...\n");
	for (i = 0; i < N_UTCTIME_TESTS; i++) {
		att = &asn1_utctime_tests[i];
		failed |= asn1_time_test(i, att, V_ASN1_UTCTIME);
	}
	for (i = 0; i < N_GENTIME_TESTS; i++) {
		att = &asn1_gentime_tests[i];
		failed |= asn1_time_test(i, att, V_ASN1_GENERALIZEDTIME);
	}

	fprintf(stderr, "ASN1_TIME_compare tests...\n");
	failed |= asn1_time_compare_test();

	/* Check for a leak in ASN1_TIME_normalize(). */
	failed |= ASN1_TIME_normalize(NULL) != 0;

	fprintf(stderr, "Time overflow tests...\n");
	failed |= asn1_time_overflow();

	return (failed);
}
