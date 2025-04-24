// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for utf-8 support.
 *
 * Copyright 2017 Collabora Ltd.
 */

#include <linux/unicode.h>
#include <kunit/test.h>

#include "../utf8n.h"

static const struct {
	/* UTF-8 strings in this vector _must_ be NULL-terminated. */
	unsigned char str[10];
	unsigned char dec[10];
} nfdi_test_data[] = {
	/* Trivial sequence */
	{
		/* "ABba" decomposes to itself */
		.str = "aBba",
		.dec = "aBba",
	},
	/* Simple equivalent sequences */
	{
               /* 'VULGAR FRACTION ONE QUARTER' cannot decompose to
                  'NUMBER 1' + 'FRACTION SLASH' + 'NUMBER 4' on
                  canonical decomposition */
               .str = {0xc2, 0xbc, 0x00},
	       .dec = {0xc2, 0xbc, 0x00},
	},
	{
		/* 'LATIN SMALL LETTER A WITH DIAERESIS' decomposes to
		   'LETTER A' + 'COMBINING DIAERESIS' */
		.str = {0xc3, 0xa4, 0x00},
		.dec = {0x61, 0xcc, 0x88, 0x00},
	},
	{
		/* 'LATIN SMALL LETTER LJ' can't decompose to
		   'LETTER L' + 'LETTER J' on canonical decomposition */
		.str = {0xC7, 0x89, 0x00},
		.dec = {0xC7, 0x89, 0x00},
	},
	{
		/* GREEK ANO TELEIA decomposes to MIDDLE DOT */
		.str = {0xCE, 0x87, 0x00},
		.dec = {0xC2, 0xB7, 0x00}
	},
	/* Canonical ordering */
	{
		/* A + 'COMBINING ACUTE ACCENT' + 'COMBINING OGONEK' decomposes
		   to A + 'COMBINING OGONEK' + 'COMBINING ACUTE ACCENT' */
		.str = {0x41, 0xcc, 0x81, 0xcc, 0xa8, 0x0},
		.dec = {0x41, 0xcc, 0xa8, 0xcc, 0x81, 0x0},
	},
	{
		/* 'LATIN SMALL LETTER A WITH DIAERESIS' + 'COMBINING OGONEK'
		   decomposes to
		   'LETTER A' + 'COMBINING OGONEK' + 'COMBINING DIAERESIS' */
		.str = {0xc3, 0xa4, 0xCC, 0xA8, 0x00},

		.dec = {0x61, 0xCC, 0xA8, 0xcc, 0x88, 0x00},
	},

};

static const struct {
	/* UTF-8 strings in this vector _must_ be NULL-terminated. */
	unsigned char str[30];
	unsigned char ncf[30];
} nfdicf_test_data[] = {
	/* Trivial sequences */
	{
		/* "ABba" folds to lowercase */
		.str = {0x41, 0x42, 0x62, 0x61, 0x00},
		.ncf = {0x61, 0x62, 0x62, 0x61, 0x00},
	},
	{
		/* All ASCII folds to lower-case */
		.str = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0.1",
		.ncf = "abcdefghijklmnopqrstuvwxyz0.1",
	},
	{
		/* LATIN SMALL LETTER SHARP S folds to
		   LATIN SMALL LETTER S + LATIN SMALL LETTER S */
		.str = {0xc3, 0x9f, 0x00},
		.ncf = {0x73, 0x73, 0x00},
	},
	{
		/* LATIN CAPITAL LETTER A WITH RING ABOVE folds to
		   LATIN SMALL LETTER A + COMBINING RING ABOVE */
		.str = {0xC3, 0x85, 0x00},
		.ncf = {0x61, 0xcc, 0x8a, 0x00},
	},
	/* Introduced by UTF-8.0.0. */
	/* Cherokee letters are interesting test-cases because they fold
	   to upper-case.  Before 8.0.0, Cherokee lowercase were
	   undefined, thus, the folding from LC is not stable between
	   7.0.0 -> 8.0.0, but it is from UC. */
	{
		/* CHEROKEE SMALL LETTER A folds to CHEROKEE LETTER A */
		.str = {0xea, 0xad, 0xb0, 0x00},
		.ncf = {0xe1, 0x8e, 0xa0, 0x00},
	},
	{
		/* CHEROKEE SMALL LETTER YE folds to CHEROKEE LETTER YE */
		.str = {0xe1, 0x8f, 0xb8, 0x00},
		.ncf = {0xe1, 0x8f, 0xb0, 0x00},
	},
	{
		/* OLD HUNGARIAN CAPITAL LETTER AMB folds to
		   OLD HUNGARIAN SMALL LETTER AMB */
		.str = {0xf0, 0x90, 0xb2, 0x83, 0x00},
		.ncf = {0xf0, 0x90, 0xb3, 0x83, 0x00},
	},
	/* Introduced by UTF-9.0.0. */
	{
		/* OSAGE CAPITAL LETTER CHA folds to
		   OSAGE SMALL LETTER CHA */
		.str = {0xf0, 0x90, 0x92, 0xb5, 0x00},
		.ncf = {0xf0, 0x90, 0x93, 0x9d, 0x00},
	},
	{
		/* LATIN CAPITAL LETTER SMALL CAPITAL I folds to
		   LATIN LETTER SMALL CAPITAL I */
		.str = {0xea, 0x9e, 0xae, 0x00},
		.ncf = {0xc9, 0xaa, 0x00},
	},
	/* Introduced by UTF-11.0.0. */
	{
		/* GEORGIAN SMALL LETTER AN folds to GEORGIAN MTAVRULI
		   CAPITAL LETTER AN */
		.str = {0xe1, 0xb2, 0x90, 0x00},
		.ncf = {0xe1, 0x83, 0x90, 0x00},
	}
};

static ssize_t utf8len(const struct unicode_map *um, enum utf8_normalization n,
		const char *s)
{
	return utf8nlen(um, n, s, (size_t)-1);
}

static int utf8cursor(struct utf8cursor *u8c, const struct unicode_map *um,
		enum utf8_normalization n, const char *s)
{
	return utf8ncursor(u8c, um, n, s, (unsigned int)-1);
}

static void check_utf8_nfdi(struct kunit *test)
{
	int i;
	struct utf8cursor u8c;
	struct unicode_map *um = test->priv;

	for (i = 0; i < ARRAY_SIZE(nfdi_test_data); i++) {
		int len = strlen(nfdi_test_data[i].str);
		int nlen = strlen(nfdi_test_data[i].dec);
		int j = 0;
		unsigned char c;
		int ret;

		KUNIT_EXPECT_EQ(test, utf8len(um, UTF8_NFDI, nfdi_test_data[i].str), nlen);
		KUNIT_EXPECT_EQ(test, utf8nlen(um, UTF8_NFDI, nfdi_test_data[i].str, len),
				nlen);


		ret = utf8cursor(&u8c, um, UTF8_NFDI, nfdi_test_data[i].str);
		KUNIT_EXPECT_TRUE_MSG(test, ret >= 0, "Can't create cursor\n");

		while ((c = utf8byte(&u8c)) > 0) {
			KUNIT_EXPECT_EQ_MSG(test, c, nfdi_test_data[i].dec[j],
					    "Unexpected byte 0x%x should be 0x%x\n",
					    c, nfdi_test_data[i].dec[j]);
			j++;
		}

		KUNIT_EXPECT_EQ(test, j, nlen);
	}
}

static void check_utf8_nfdicf(struct kunit *test)
{
	int i;
	struct utf8cursor u8c;
	struct unicode_map *um = test->priv;

	for (i = 0; i < ARRAY_SIZE(nfdicf_test_data); i++) {
		int len = strlen(nfdicf_test_data[i].str);
		int nlen = strlen(nfdicf_test_data[i].ncf);
		int j = 0;
		int ret;
		unsigned char c;

		KUNIT_EXPECT_EQ(test, utf8len(um, UTF8_NFDICF, nfdicf_test_data[i].str),
				nlen);
		KUNIT_EXPECT_EQ(test, utf8nlen(um, UTF8_NFDICF, nfdicf_test_data[i].str, len),
				nlen);

		ret = utf8cursor(&u8c, um, UTF8_NFDICF, nfdicf_test_data[i].str);
		KUNIT_EXPECT_TRUE_MSG(test, ret >= 0, "Can't create cursor\n");

		while ((c = utf8byte(&u8c)) > 0) {
			KUNIT_EXPECT_EQ_MSG(test, c, nfdicf_test_data[i].ncf[j],
					    "Unexpected byte 0x%x should be 0x%x\n",
					    c, nfdicf_test_data[i].ncf[j]);
			j++;
		}

		KUNIT_EXPECT_EQ(test, j, nlen);
	}
}

static void check_utf8_comparisons(struct kunit *test)
{
	int i;
	struct unicode_map *um = test->priv;

	for (i = 0; i < ARRAY_SIZE(nfdi_test_data); i++) {
		const struct qstr s1 = {.name = nfdi_test_data[i].str,
					.len = sizeof(nfdi_test_data[i].str)};
		const struct qstr s2 = {.name = nfdi_test_data[i].dec,
					.len = sizeof(nfdi_test_data[i].dec)};

		/* strncmp returns 0 when strings are equal */
		KUNIT_EXPECT_TRUE_MSG(test, utf8_strncmp(um, &s1, &s2) == 0,
				    "%s %s comparison mismatch\n", s1.name, s2.name);
	}

	for (i = 0; i < ARRAY_SIZE(nfdicf_test_data); i++) {
		const struct qstr s1 = {.name = nfdicf_test_data[i].str,
					.len = sizeof(nfdicf_test_data[i].str)};
		const struct qstr s2 = {.name = nfdicf_test_data[i].ncf,
					.len = sizeof(nfdicf_test_data[i].ncf)};

		/* strncasecmp returns 0 when strings are equal */
		KUNIT_EXPECT_TRUE_MSG(test, utf8_strncasecmp(um, &s1, &s2) == 0,
				    "%s %s comparison mismatch\n", s1.name, s2.name);
	}
}

static void check_supported_versions(struct kunit *test)
{
	struct unicode_map *um = test->priv;
	/* Unicode 7.0.0 should be supported. */
	KUNIT_EXPECT_TRUE(test, utf8version_is_supported(um, UNICODE_AGE(7, 0, 0)));

	/* Unicode 9.0.0 should be supported. */
	KUNIT_EXPECT_TRUE(test, utf8version_is_supported(um, UNICODE_AGE(9, 0, 0)));

	/* Unicode 1x.0.0 (the latest version) should be supported. */
	KUNIT_EXPECT_TRUE(test, utf8version_is_supported(um, UTF8_LATEST));

	/* Next versions don't exist. */
	KUNIT_EXPECT_FALSE(test, utf8version_is_supported(um, UNICODE_AGE(13, 0, 0)));
	KUNIT_EXPECT_FALSE(test, utf8version_is_supported(um, UNICODE_AGE(0, 0, 0)));
	KUNIT_EXPECT_FALSE(test, utf8version_is_supported(um, UNICODE_AGE(-1, -1, -1)));
}

static struct kunit_case unicode_normalization_test_cases[] = {
	KUNIT_CASE(check_supported_versions),
	KUNIT_CASE(check_utf8_comparisons),
	KUNIT_CASE(check_utf8_nfdicf),
	KUNIT_CASE(check_utf8_nfdi),
	{}
};

static int init_test_ucd(struct kunit *test)
{
	struct unicode_map *um = utf8_load(UTF8_LATEST);

	test->priv = um;

	KUNIT_EXPECT_EQ_MSG(test, IS_ERR(um), 0,
			    "%s: Unable to load utf8 table.\n", __func__);

	return 0;
}

static void exit_test_ucd(struct kunit *test)
{
	utf8_unload(test->priv);
}

static struct kunit_suite unicode_normalization_test_suite = {
	.name = "unicode_normalization",
	.test_cases = unicode_normalization_test_cases,
	.init = init_test_ucd,
	.exit = exit_test_ucd,
};

kunit_test_suite(unicode_normalization_test_suite);


MODULE_AUTHOR("Gabriel Krisman Bertazi <krisman@collabora.co.uk>");
MODULE_DESCRIPTION("KUnit tests for utf-8 support.");
MODULE_LICENSE("GPL");
