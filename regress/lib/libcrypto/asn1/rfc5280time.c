/* $OpenBSD: rfc5280time.c,v 1.8 2024/04/08 19:57:40 beck Exp $ */
/*
 * Copyright (c) 2015 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2015 Bob Beck <beck@opebsd.org>
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
#include <openssl/x509.h>

#include <err.h>
#include <stdio.h>
#include <string.h>

struct rfc5280_time_test {
	const char *str;
	const char *data;
	time_t time;
};

struct rfc5280_time_test rfc5280_invtime_tests[] = {
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
		/* UTC time must have seconds */
		.str = "7001010000Z",
	},
	{
		.str = "201509230327Z",
	},
	{
		.str = "20150923032700.Z",
	},
	{
		.str = "20150923032700.123",
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
		/* Gen time cannot have +- TZ. */
		.str = "20150923032712+1115",
	},
	{
		/* Gen time cannot have fractional seconds */
		.str = "20150923032712.123Z",
	},
	{
		.str = "aaaaaaaaaaaaaaZ",
	},
	{
		/* Must be a UTC time per RFC 5280 */
		.str = "19700101000000Z",
		.data = "19700101000000Z",
		.time = 0,
	},
	{
		/* (times before 2050 must be UTCTIME) Per RFC 5280 4.1.2.5 */
		.str = "20150923032700Z",
		.data = "20150923032700Z",
		.time = 1442978820,
	},
	{
		/* (times before 2050 must be UTCTIME) Per RFC 5280 4.1.2.5 */
		.str = "00000101000000Z",
		.data = "00000101000000Z",
		.time = -62167219200LL,
	},
	{
		/* (times before 2050 must be UTCTIME) Per RFC 5280 4.1.2.5 */
		.str = "20491231235959Z",
		.data = "20491231235959Z",
		.time = 2524607999LL,
	},
	{
		/* (times before 2050 must be UTCTIME) Per RFC 5280 4.1.2.5 */
		.str = "19500101000000Z",
		.data = "19500101000000Z",
		.time = -631152000LL,
	},
};

struct rfc5280_time_test rfc5280_gentime_tests[] = {
	{
		/* Biggest RFC 5280 time */
		.str = "99991231235959Z",
		.data = "99991231235959Z",
		.time = 253402300799LL,
	},
	{
		.str = "21600218104000Z",
		.data = "21600218104000Z",
		.time = 6000000000LL,
	},
	{
		/* Smallest RFC 5280 gen time */
		.str = "20500101000000Z",
		.data = "20500101000000Z",
		.time =  2524608000LL,
	},
};
struct rfc5280_time_test rfc5280_utctime_tests[] = {
	{
		.str = "500101000000Z",
		.data = "500101000000Z",
		.time = -631152000,
	},
	{
		.str = "540226230640Z",
		.data = "540226230640Z",
		.time = -500000000,
	},
	{
		.str = "491231235959Z",
		.data = "491231235959Z",
		.time = 2524607999LL,
	},
	{
		.str = "700101000000Z",
		.data = "700101000000Z",
		.time = 0,
	},
	{
		.str = "150923032700Z",
		.data = "150923032700Z",
		.time = 1442978820,
	},
	{
		.str = "150923102700Z",
		.data = "150923102700Z",
		.time = 1443004020,
	},
	{
		.str = "150922162712Z",
		.data = "150922162712Z",
		.time = 1442939232,
	},
	{
		.str = "140524144512Z",
		.data = "140524144512Z",
		.time = 1400942712,
	},
	{
		.str = "240401144512Z",
		.data = "240401144512Z",
		.time = 1711982712,
	},
};

#define N_INVTIME_TESTS \
    (sizeof(rfc5280_invtime_tests) / sizeof(*rfc5280_invtime_tests))
#define N_GENTIME_TESTS \
    (sizeof(rfc5280_gentime_tests) / sizeof(*rfc5280_gentime_tests))
#define N_UTCTIME_TESTS \
    (sizeof(rfc5280_utctime_tests) / sizeof(*rfc5280_utctime_tests))

static int
asn1_compare_str(int test_no, struct asn1_string_st *asn1str, const char *str)
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
rfc5280_invtime_test(int test_no, struct rfc5280_time_test *att)
{
	ASN1_GENERALIZEDTIME *gt = NULL;
	ASN1_UTCTIME *ut = NULL;
	ASN1_TIME *t = NULL;
	int failure = 1;
	time_t now = time(NULL);

	if ((gt = ASN1_GENERALIZEDTIME_new()) == NULL)
		goto done;
	if ((ut = ASN1_UTCTIME_new()) == NULL)
		goto done;
	if ((t = ASN1_TIME_new()) == NULL)
		goto done;

	if (ASN1_GENERALIZEDTIME_set_string(gt, att->str) != 0) {
		if (X509_cmp_time(gt, &now) != 0) {
			fprintf(stderr, "FAIL: test %d - successfully parsed as GENTIME "
			    "string '%s'\n", test_no, att->str);
			goto done;
		}
	}
	if (ASN1_UTCTIME_set_string(ut, att->str) != 0) {
		if (X509_cmp_time(ut, &now) != 0) {
			fprintf(stderr, "FAIL: test %d - successfully parsed as UTCTIME "
			    "string '%s'\n", test_no, att->str);
			goto done;
		}
	}

	failure = 0;

 done:
	ASN1_GENERALIZEDTIME_free(gt);
	ASN1_UTCTIME_free(ut);
	ASN1_TIME_free(t);

	return (failure);
}

static int
rfc5280_gentime_test(int test_no, struct rfc5280_time_test *att)
{
	unsigned char *p = NULL;
	ASN1_GENERALIZEDTIME *gt;
	int failure = 1;
	int i;

	if ((gt = ASN1_GENERALIZEDTIME_new()) == NULL)
		goto done;

	if (ASN1_GENERALIZEDTIME_set_string(gt, att->str) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string '%s'\n",
		    test_no, att->str);
		goto done;
	}
	if (asn1_compare_str(test_no, gt, att->str) != 0)
		goto done;

	if ((i = X509_cmp_time(gt, &att->time)) != -1) {
		fprintf(stderr, "FAIL: test %d - X509_cmp_time failed - returned %d compared to %lld\n",
		    test_no, i, (long long)att->time);
		goto done;
	}

	att->time--;
	if ((i = X509_cmp_time(gt, &att->time)) != 1) {
		fprintf(stderr, "FAIL: test %d - X509_cmp_time failed - returned %d compared to %lld\n",
		    test_no, i, (long long)att->time);
		goto done;
	}
	att->time++;

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
rfc5280_utctime_test(int test_no, struct rfc5280_time_test *att)
{
	unsigned char *p = NULL;
	ASN1_UTCTIME *ut;
	int failure = 1;
	int i;

	if ((ut = ASN1_UTCTIME_new()) == NULL)
		goto done;

	if (ASN1_UTCTIME_set_string(ut, att->str) != 1) {
		fprintf(stderr, "FAIL: test %d - failed to set string '%s'\n",
		    test_no, att->str);
		goto done;
	}
	if (asn1_compare_str(test_no, ut, att->str) != 0)
		goto done;

	if ((i = X509_cmp_time(ut, &att->time)) != -1) {
		fprintf(stderr, "FAIL: test %d - X509_cmp_time failed - returned %d compared to %lld\n",
		    test_no, i, (long long)att->time);
		goto done;
	}

	att->time--;
	if ((i = X509_cmp_time(ut, &att->time)) != 1) {
		fprintf(stderr, "FAIL: test %d - X509_cmp_time failed - returned %d compared to %lld\n",
		    test_no, i, (long long)att->time);
		goto done;
	}
	att->time++;

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

int
main(int argc, char **argv)
{
	struct rfc5280_time_test *att;
	int failed = 0;
	size_t i;

	fprintf(stderr, "RFC5280 Invalid time tests...\n");
	for (i = 0; i < N_INVTIME_TESTS; i++) {
		att = &rfc5280_invtime_tests[i];
		failed |= rfc5280_invtime_test(i, att);
	}

	fprintf(stderr, "RFC5280 GENERALIZEDTIME tests...\n");
	for (i = 0; i < N_GENTIME_TESTS; i++) {
		att = &rfc5280_gentime_tests[i];
		failed |= rfc5280_gentime_test(i, att);
	}

	fprintf(stderr, "RFC5280 UTCTIME tests...\n");
	for (i = 0; i < N_UTCTIME_TESTS; i++) {
		att = &rfc5280_utctime_tests[i];
		failed |= rfc5280_utctime_test(i, att);
	}
	return (failed);
}
