/*	$OpenBSD: verifytest.c,v 1.8 2023/05/28 09:02:01 beck Exp $	*/
/*
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
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
#include <stdlib.h>

#include <openssl/x509v3.h>
#include <tls.h>

extern int tls_check_name(struct tls *ctx, X509 *cert, const char *name,
    int *match);

struct alt_name {
	const char name[128];
	int name_len;
	int name_type;
};

struct verify_test {
	const char common_name[128];
	int common_name_len;
	struct alt_name alt_name1;
	struct alt_name alt_name2;
	struct alt_name alt_name3;
	const char name[128];
	int want_return;
	int want_match;
	int name_type;
};

struct verify_test verify_tests[] = {
	{
		/* CN without SANs - matching. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* Zero length name - non-matching. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.name = "",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN wildcard without SANs - matching. */
		.common_name = "*.openbsd.org",
		.common_name_len = -1,
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* CN without SANs - non-matching. */
		.common_name = "www.openbsdfoundation.org",
		.common_name_len = -1,
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN wildcard without SANs - invalid CN wildcard. */
		.common_name = "w*.openbsd.org",
		.common_name_len = -1,
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN wildcard without SANs - invalid CN wildcard. */
		.common_name = "www.*.org",
		.common_name_len = -1,
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN wildcard without SANs - invalid CN wildcard. */
		.common_name = "www.openbsd.*",
		.common_name_len = -1,
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN wildcard without SANs - invalid CN wildcard. */
		.common_name = "*",
		.common_name_len = -1,
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN wildcard without SANs - invalid CN wildcard. */
		.common_name = "*.org",
		.common_name_len = -1,
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN wildcard without SANs - invalid CN wildcard. */
		.common_name = "*.org",
		.common_name_len = -1,
		.name = "openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN IPv4 without SANs - matching. */
		.common_name = "1.2.3.4",
		.common_name_len = -1,
		.name = "1.2.3.4",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* CN IPv4 wildcard without SANS - invalid IP wildcard. */
		.common_name = "*.2.3.4",
		.common_name_len = -1,
		.name = "1.2.3.4",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN IPv6 without SANs - matching. */
		.common_name = "cafe::beef",
		.common_name_len = -1,
		.name = "cafe::beef",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* CN without SANs - error due to embedded NUL in CN. */
		.common_name = {
			0x77, 0x77, 0x77, 0x2e, 0x6f, 0x70, 0x65, 0x6e,
			0x62, 0x73, 0x64, 0x2e, 0x6f, 0x72, 0x67, 0x00,
			0x6e, 0x61, 0x73, 0x74, 0x79, 0x2e, 0x6f, 0x72,
			0x67,
		},
		.common_name_len = 25,
		.name = "www.openbsd.org",
		.want_return = -1,
		.want_match = 0,
	},
	{
		/* CN wildcard without SANs - invalid non-matching name. */
		.common_name = "*.openbsd.org",
		.common_name_len = -1,
		.name = ".openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN with SANs - matching on first SAN. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "www.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = "ftp.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* SANs only - matching on first SAN. */
		.common_name_len = 0,
		.alt_name1 = {
			.name = "www.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = "ftp.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* SANs only - matching on second SAN. */
		.common_name_len = 0,
		.alt_name1 = {
			.name = "www.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = "ftp.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "ftp.openbsd.org",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* SANs only - non-matching. */
		.common_name_len = 0,
		.alt_name1 = {
			.name = "www.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = "ftp.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "mail.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN with SANs - matching on second SAN. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "www.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = "ftp.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "ftp.openbsd.org",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* CN with SANs - matching on wildcard second SAN. */
		.common_name = "www.openbsdfoundation.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "www.openbsdfoundation.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = "*.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* CN with SANs - non-matching invalid wildcard. */
		.common_name = "www.openbsdfoundation.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "www.openbsdfoundation.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = "*.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN with SANs - non-matching IPv4 due to GEN_DNS SAN. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "www.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = "1.2.3.4",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "1.2.3.4",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN with SANs - matching IPv4 on GEN_IPADD SAN. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "www.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = {0x01, 0x02, 0x03, 0x04},
			.name_len = 4,
			.name_type = GEN_IPADD,
		},
		.name = "1.2.3.4",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* CN with SANs - matching IPv6 on GEN_IPADD SAN. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "www.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = {
				0xca, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbe, 0xef,
			},
			.name_len = 16,
			.name_type = GEN_IPADD,
		},
		.name = "cafe::beef",
		.want_return = 0,
		.want_match = 1,
	},
	{
		/* CN with SANs - error due to embedded NUL in GEN_DNS. */
		.common_name = "www.openbsd.org.nasty.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "www.openbsd.org.nasty.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.alt_name2 = {
			.name = {
				0x77, 0x77, 0x77, 0x2e, 0x6f, 0x70, 0x65, 0x6e,
				0x62, 0x73, 0x64, 0x2e, 0x6f, 0x72, 0x67, 0x00,
				0x6e, 0x61, 0x73, 0x74, 0x79, 0x2e, 0x6f, 0x72,
				0x67,
			},
			.name_len = 25,
			.name_type = GEN_DNS,
		},
		.name = "www.openbsd.org",
		.want_return = -1,
		.want_match = 0,
	},
	{
		/* CN with SAN - non-matching due to non-matching SAN. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = "ftp.openbsd.org",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "www.openbsd.org",
		.want_return = 0,
		.want_match = 0,
	},
	{
		/* CN with SAN - error due to illegal dNSName. */
		.common_name = "www.openbsd.org",
		.common_name_len = -1,
		.alt_name1 = {
			.name = " ",
			.name_len = -1,
			.name_type = GEN_DNS,
		},
		.name = "www.openbsd.org",
		.want_return = -1,
		.want_match = 0,
	},
};

#define N_VERIFY_TESTS \
    (sizeof(verify_tests) / sizeof(*verify_tests))

static void
alt_names_add(STACK_OF(GENERAL_NAME) *alt_name_stack, struct alt_name *alt)
{
	ASN1_STRING *alt_name_str;
	GENERAL_NAME *alt_name;

	if ((alt_name = GENERAL_NAME_new()) == NULL)
		errx(1, "failed to malloc GENERAL_NAME");
	alt_name->type = alt->name_type;

	if ((alt_name_str = ASN1_STRING_new()) == NULL)
		errx(1, "failed to malloc alt name");
	if (ASN1_STRING_set(alt_name_str, alt->name, alt->name_len) == 0)
		errx(1, "failed to set alt name");

	switch (alt_name->type) {
	case GEN_DNS:
		alt_name->d.dNSName = alt_name_str;
		break;
	case GEN_IPADD:
		alt_name->d.iPAddress = alt_name_str;
		break;
	default:
		errx(1, "unknown alt name type (%i)", alt_name->type);
	}

	if (sk_GENERAL_NAME_push(alt_name_stack, alt_name) == 0)
		errx(1, "failed to push alt_name");
}

static void
cert_add_alt_names(X509 *cert, struct verify_test *vt)
{
	STACK_OF(GENERAL_NAME) *alt_name_stack = NULL;

	if (vt->alt_name1.name_type == 0)
		return;

	if ((alt_name_stack = sk_GENERAL_NAME_new_null()) == NULL)
		errx(1, "failed to malloc sk_GENERAL_NAME");

	if (vt->alt_name1.name_type != 0)
		alt_names_add(alt_name_stack, &vt->alt_name1);
	if (vt->alt_name2.name_type != 0)
		alt_names_add(alt_name_stack, &vt->alt_name2);
	if (vt->alt_name3.name_type != 0)
		alt_names_add(alt_name_stack, &vt->alt_name3);

	if (X509_add1_ext_i2d(cert, NID_subject_alt_name,
	    alt_name_stack, 0, 0) == 0)
		errx(1, "failed to set subject alt name");

	sk_GENERAL_NAME_pop_free(alt_name_stack, GENERAL_NAME_free);
}

static int
do_verify_test(int test_no, struct verify_test *vt)
{
	struct tls *tls;
	X509_NAME *name;
	X509 *cert;
	int failed = 1;
	int match;

	/* Build certificate structure. */
	if ((cert = X509_new()) == NULL)
		errx(1, "failed to malloc X509");

	if (vt->common_name_len != 0) {
		if ((name = X509_NAME_new()) == NULL)
			errx(1, "failed to malloc X509_NAME");
		if (X509_NAME_add_entry_by_NID(name, NID_commonName,
		    vt->name_type ? vt->name_type : MBSTRING_ASC,
		    (unsigned char *)vt->common_name,
		    vt->common_name_len, -1, 0) == 0)
			errx(1, "failed to add name entry");
		if (X509_set_subject_name(cert, name) == 0)
			errx(1, "failed to set subject name");
		X509_NAME_free(name);
	}

	if ((tls = tls_client()) == NULL)
		errx(1, "failed to malloc tls_client");

	cert_add_alt_names(cert, vt);

	match = 1;

	if (tls_check_name(tls, cert, vt->name, &match) != vt->want_return) {
		fprintf(stderr, "FAIL: test %i failed for check name '%s': "
		    "%s\n", test_no, vt->name, tls_error(tls));
		goto done;
	}
	if (match != vt->want_match) {
		fprintf(stderr, "FAIL: test %i failed to match name '%s'\n",
		    test_no, vt->name);
		goto done;
	}

	failed = 0;

 done:
	X509_free(cert);
	tls_free(tls);

	return (failed);
}

int
main(int argc, char **argv)
{
	int failed = 0;
	size_t i;

	tls_init();

	for (i = 0; i < N_VERIFY_TESTS; i++)
		failed += do_verify_test(i, &verify_tests[i]);

	return (failed);
}
