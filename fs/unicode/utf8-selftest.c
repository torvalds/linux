/*
 * Kernel module for testing utf-8 support.
 *
 * Copyright 2017 Collabora Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/unicode.h>
#include <linux/dcache.h>

#include "utf8n.h"

unsigned int failed_tests;
unsigned int total_tests;

/* Tests will be based on this version. */
#define latest_maj 12
#define latest_min 1
#define latest_rev 0

#define _test(cond, func, line, fmt, ...) do {				\
		total_tests++;						\
		if (!cond) {						\
			failed_tests++;					\
			pr_err("test %s:%d Failed: %s%s",		\
			       func, line, #cond, (fmt?":":"."));	\
			if (fmt)					\
				pr_err(fmt, ##__VA_ARGS__);		\
		}							\
	} while (0)
#define test_f(cond, fmt, ...) _test(cond, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define test(cond) _test(cond, __func__, __LINE__, "")

const static struct {
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

const static struct {
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

static void check_utf8_nfdi(void)
{
	int i;
	struct utf8cursor u8c;
	const struct utf8data *data;

	data = utf8nfdi(UNICODE_AGE(latest_maj, latest_min, latest_rev));
	if (!data) {
		pr_err("%s: Unable to load utf8-%d.%d.%d. Skipping.\n",
		       __func__, latest_maj, latest_min, latest_rev);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(nfdi_test_data); i++) {
		int len = strlen(nfdi_test_data[i].str);
		int nlen = strlen(nfdi_test_data[i].dec);
		int j = 0;
		unsigned char c;

		test((utf8len(data, nfdi_test_data[i].str) == nlen));
		test((utf8nlen(data, nfdi_test_data[i].str, len) == nlen));

		if (utf8cursor(&u8c, data, nfdi_test_data[i].str) < 0)
			pr_err("can't create cursor\n");

		while ((c = utf8byte(&u8c)) > 0) {
			test_f((c == nfdi_test_data[i].dec[j]),
			       "Unexpected byte 0x%x should be 0x%x\n",
			       c, nfdi_test_data[i].dec[j]);
			j++;
		}

		test((j == nlen));
	}
}

static void check_utf8_nfdicf(void)
{
	int i;
	struct utf8cursor u8c;
	const struct utf8data *data;

	data = utf8nfdicf(UNICODE_AGE(latest_maj, latest_min, latest_rev));
	if (!data) {
		pr_err("%s: Unable to load utf8-%d.%d.%d. Skipping.\n",
		       __func__, latest_maj, latest_min, latest_rev);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(nfdicf_test_data); i++) {
		int len = strlen(nfdicf_test_data[i].str);
		int nlen = strlen(nfdicf_test_data[i].ncf);
		int j = 0;
		unsigned char c;

		test((utf8len(data, nfdicf_test_data[i].str) == nlen));
		test((utf8nlen(data, nfdicf_test_data[i].str, len) == nlen));

		if (utf8cursor(&u8c, data, nfdicf_test_data[i].str) < 0)
			pr_err("can't create cursor\n");

		while ((c = utf8byte(&u8c)) > 0) {
			test_f((c == nfdicf_test_data[i].ncf[j]),
			       "Unexpected byte 0x%x should be 0x%x\n",
			       c, nfdicf_test_data[i].ncf[j]);
			j++;
		}

		test((j == nlen));
	}
}

static void check_utf8_comparisons(void)
{
	int i;
	struct unicode_map *table = utf8_load("12.1.0");

	if (IS_ERR(table)) {
		pr_err("%s: Unable to load utf8 %d.%d.%d. Skipping.\n",
		       __func__, latest_maj, latest_min, latest_rev);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(nfdi_test_data); i++) {
		const struct qstr s1 = {.name = nfdi_test_data[i].str,
					.len = sizeof(nfdi_test_data[i].str)};
		const struct qstr s2 = {.name = nfdi_test_data[i].dec,
					.len = sizeof(nfdi_test_data[i].dec)};

		test_f(!utf8_strncmp(table, &s1, &s2),
		       "%s %s comparison mismatch\n", s1.name, s2.name);
	}

	for (i = 0; i < ARRAY_SIZE(nfdicf_test_data); i++) {
		const struct qstr s1 = {.name = nfdicf_test_data[i].str,
					.len = sizeof(nfdicf_test_data[i].str)};
		const struct qstr s2 = {.name = nfdicf_test_data[i].ncf,
					.len = sizeof(nfdicf_test_data[i].ncf)};

		test_f(!utf8_strncasecmp(table, &s1, &s2),
		       "%s %s comparison mismatch\n", s1.name, s2.name);
	}

	utf8_unload(table);
}

static void check_supported_versions(void)
{
	/* Unicode 7.0.0 should be supported. */
	test(utf8version_is_supported(7, 0, 0));

	/* Unicode 9.0.0 should be supported. */
	test(utf8version_is_supported(9, 0, 0));

	/* Unicode 1x.0.0 (the latest version) should be supported. */
	test(utf8version_is_supported(latest_maj, latest_min, latest_rev));

	/* Next versions don't exist. */
	test(!utf8version_is_supported(13, 0, 0));
	test(!utf8version_is_supported(0, 0, 0));
	test(!utf8version_is_supported(-1, -1, -1));
}

static int __init init_test_ucd(void)
{
	failed_tests = 0;
	total_tests = 0;

	check_supported_versions();
	check_utf8_nfdi();
	check_utf8_nfdicf();
	check_utf8_comparisons();

	if (!failed_tests)
		pr_info("All %u tests passed\n", total_tests);
	else
		pr_err("%u out of %u tests failed\n", failed_tests,
		       total_tests);
	return 0;
}

static void __exit exit_test_ucd(void)
{
}

module_init(init_test_ucd);
module_exit(exit_test_ucd);

MODULE_AUTHOR("Gabriel Krisman Bertazi <krisman@collabora.co.uk>");
MODULE_LICENSE("GPL");
