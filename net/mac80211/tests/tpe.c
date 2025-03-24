// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for TPE element handling
 *
 * Copyright (C) 2024 Intel Corporation
 */
#include <kunit/test.h>
#include "../ieee80211_i.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static struct ieee80211_channel chan6g_1 = {
	.band = NL80211_BAND_6GHZ,
	.center_freq = 5955,
};

static struct ieee80211_channel chan6g_33 = {
	.band = NL80211_BAND_6GHZ,
	.center_freq = 6115,
};

static struct ieee80211_channel chan6g_61 = {
	.band = NL80211_BAND_6GHZ,
	.center_freq = 6255,
};

static const struct subchan_test_case {
	const char *desc;
	struct cfg80211_chan_def c;
	u8 n;
	int expect;
} subchan_offset_cases[] = {
	{
		.desc = "identical 20 MHz",
		.c.width = NL80211_CHAN_WIDTH_20,
		.c.chan = &chan6g_1,
		.c.center_freq1 = 5955,
		.n = 1,
		.expect = 0,
	},
	{
		.desc = "identical 40 MHz",
		.c.width = NL80211_CHAN_WIDTH_40,
		.c.chan = &chan6g_1,
		.c.center_freq1 = 5965,
		.n = 2,
		.expect = 0,
	},
	{
		.desc = "identical 80+80 MHz",
		/* not really is valid? doesn't matter for the test */
		.c.width = NL80211_CHAN_WIDTH_80P80,
		.c.chan = &chan6g_1,
		.c.center_freq1 = 5985,
		.c.center_freq2 = 6225,
		.n = 16,
		.expect = 0,
	},
	{
		.desc = "identical 320 MHz",
		.c.width = NL80211_CHAN_WIDTH_320,
		.c.chan = &chan6g_1,
		.c.center_freq1 = 6105,
		.n = 16,
		.expect = 0,
	},
	{
		.desc = "lower 160 MHz of 320 MHz",
		.c.width = NL80211_CHAN_WIDTH_320,
		.c.chan = &chan6g_1,
		.c.center_freq1 = 6105,
		.n = 8,
		.expect = 0,
	},
	{
		.desc = "upper 160 MHz of 320 MHz",
		.c.width = NL80211_CHAN_WIDTH_320,
		.c.chan = &chan6g_61,
		.c.center_freq1 = 6105,
		.n = 8,
		.expect = 8,
	},
	{
		.desc = "upper 160 MHz of 320 MHz, go to 40",
		.c.width = NL80211_CHAN_WIDTH_320,
		.c.chan = &chan6g_61,
		.c.center_freq1 = 6105,
		.n = 2,
		.expect = 8 + 4 + 2,
	},
	{
		.desc = "secondary 80 above primary in 80+80 MHz",
		/* not really is valid? doesn't matter for the test */
		.c.width = NL80211_CHAN_WIDTH_80P80,
		.c.chan = &chan6g_1,
		.c.center_freq1 = 5985,
		.c.center_freq2 = 6225,
		.n = 4,
		.expect = 0,
	},
	{
		.desc = "secondary 80 below primary in 80+80 MHz",
		/* not really is valid? doesn't matter for the test */
		.c.width = NL80211_CHAN_WIDTH_80P80,
		.c.chan = &chan6g_61,
		.c.center_freq1 = 6225,
		.c.center_freq2 = 5985,
		.n = 4,
		.expect = 4,
	},
	{
		.desc = "secondary 80 below primary in 80+80 MHz, go to 20",
		/* not really is valid? doesn't matter for the test */
		.c.width = NL80211_CHAN_WIDTH_80P80,
		.c.chan = &chan6g_61,
		.c.center_freq1 = 6225,
		.c.center_freq2 = 5985,
		.n = 1,
		.expect = 7,
	},
};

KUNIT_ARRAY_PARAM_DESC(subchan_offset, subchan_offset_cases, desc);

static void subchan_offset(struct kunit *test)
{
	const struct subchan_test_case *params = test->param_value;
	int offset;

	KUNIT_ASSERT_EQ(test, cfg80211_chandef_valid(&params->c), true);

	offset = ieee80211_calc_chandef_subchan_offset(&params->c, params->n);

	KUNIT_EXPECT_EQ(test, params->expect, offset);
}

static const struct psd_reorder_test_case {
	const char *desc;
	struct cfg80211_chan_def ap, used;
	struct ieee80211_parsed_tpe_psd psd, out;
} psd_reorder_cases[] = {
	{
		.desc = "no changes, 320 MHz",

		.ap.width = NL80211_CHAN_WIDTH_320,
		.ap.chan = &chan6g_1,
		.ap.center_freq1 = 6105,

		.used.width = NL80211_CHAN_WIDTH_320,
		.used.chan = &chan6g_1,
		.used.center_freq1 = 6105,

		.psd.valid = true,
		.psd.count = 16,
		.psd.n = 8,
		.psd.power = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },

		.out.valid = true,
		.out.count = 16,
		.out.n = 8,
		.out.power = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	},
	{
		.desc = "no changes, 320 MHz, 160 MHz used, n=0",

		.ap.width = NL80211_CHAN_WIDTH_320,
		.ap.chan = &chan6g_1,
		.ap.center_freq1 = 6105,

		.used.width = NL80211_CHAN_WIDTH_160,
		.used.chan = &chan6g_1,
		.used.center_freq1 = 6025,

		.psd.valid = true,
		.psd.count = 16,
		.psd.n = 0,
		.psd.power = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },

		.out.valid = true,
		.out.count = 8,
		.out.n = 0,
		.out.power = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },
	},
	{
		.desc = "320 MHz, HE is 80, used 160, all lower",

		.ap.width = NL80211_CHAN_WIDTH_320,
		.ap.chan = &chan6g_1,
		.ap.center_freq1 = 6105,

		.used.width = NL80211_CHAN_WIDTH_160,
		.used.chan = &chan6g_1,
		.used.center_freq1 = 6025,

		.psd.valid = true,
		.psd.count = 16,
		.psd.n = 4,
		.psd.power = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },

		.out.valid = true,
		.out.count = 8,
		.out.n = 4,
		.out.power = { 0, 1, 2, 3, 4, 5, 6, 7, 127, 127, 127, 127, 127, 127, 127, 127},
	},
	{
		.desc = "320 MHz, HE is 80, used 160, all upper",
		/*
		 * EHT: | | | | | | | | | | | | | | | | |
		 * HE:                          | | | | |
		 * used:                | | | | | | | | |
		 */

		.ap.width = NL80211_CHAN_WIDTH_320,
		.ap.chan = &chan6g_61,
		.ap.center_freq1 = 6105,

		.used.width = NL80211_CHAN_WIDTH_160,
		.used.chan = &chan6g_61,
		.used.center_freq1 = 6185,

		.psd.valid = true,
		.psd.count = 16,
		.psd.n = 4,
		.psd.power = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },

		.out.valid = true,
		.out.count = 8,
		.out.n = 4,
		.out.power = { 12, 13, 14, 15, 0, 1, 2, 3, 127, 127, 127, 127, 127, 127, 127, 127},
	},
	{
		.desc = "320 MHz, HE is 80, used 160, split",
		/*
		 * EHT: | | | | | | | | | | | | | | | | |
		 * HE:                  | | | | |
		 * used:                | | | | | | | | |
		 */

		.ap.width = NL80211_CHAN_WIDTH_320,
		.ap.chan = &chan6g_33,
		.ap.center_freq1 = 6105,

		.used.width = NL80211_CHAN_WIDTH_160,
		.used.chan = &chan6g_33,
		.used.center_freq1 = 6185,

		.psd.valid = true,
		.psd.count = 16,
		.psd.n = 4,
		.psd.power = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },

		.out.valid = true,
		.out.count = 8,
		.out.n = 4,
		.out.power = { 0, 1, 2, 3, 12, 13, 14, 15, 127, 127, 127, 127, 127, 127, 127, 127},
	},
};

KUNIT_ARRAY_PARAM_DESC(psd_reorder, psd_reorder_cases, desc);

static void psd_reorder(struct kunit *test)
{
	const struct psd_reorder_test_case *params = test->param_value;
	struct ieee80211_parsed_tpe_psd tmp = params->psd;

	KUNIT_ASSERT_EQ(test, cfg80211_chandef_valid(&params->ap), true);
	KUNIT_ASSERT_EQ(test, cfg80211_chandef_valid(&params->used), true);

	ieee80211_rearrange_tpe_psd(&tmp, &params->ap, &params->used);
	KUNIT_EXPECT_MEMEQ(test, &tmp, &params->out, sizeof(tmp));
}

static struct kunit_case tpe_test_cases[] = {
	KUNIT_CASE_PARAM(subchan_offset, subchan_offset_gen_params),
	KUNIT_CASE_PARAM(psd_reorder, psd_reorder_gen_params),
	{}
};

static struct kunit_suite tpe = {
	.name = "mac80211-tpe",
	.test_cases = tpe_test_cases,
};

kunit_test_suite(tpe);
