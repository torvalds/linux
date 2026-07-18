// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2023-2024, 2026 Intel Corporation
 */
#include <net/cfg80211.h>
#include <kunit/test.h>

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static struct ieee80211_channel chan_2ghz_1 = {
	.band = NL80211_BAND_2GHZ,
	.center_freq = 2412,
};

static struct ieee80211_channel chan_6ghz_1 = {
	.band = NL80211_BAND_6GHZ,
	.center_freq = 5955,
};

static struct ieee80211_channel chan_6ghz_5 = {
	.band = NL80211_BAND_6GHZ,
	.center_freq = 5975,
};

static struct ieee80211_channel chan_6ghz_105 = {
	.band = NL80211_BAND_6GHZ,
	.center_freq = 6475,
};

static const struct chandef_compat_case {
	const char *desc;
	/* leave c1 empty for tests for identical */
	struct cfg80211_chan_def c1, c2;
	/* we test both ways around, so c2 should always be the compat one */
	bool compat;
} chandef_compat_cases[] = {
	{
		.desc = "identical non-HT",
		.c2 = {
			.width = NL80211_CHAN_WIDTH_20_NOHT,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955,
		},
		.compat = true,
	},
	{
		.desc = "identical 20 MHz",
		.c2 = {
			.width = NL80211_CHAN_WIDTH_20,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955,
		},
		.compat = true,
	},
	{
		.desc = "identical 40 MHz",
		.c2 = {
			.width = NL80211_CHAN_WIDTH_40,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955 + 10,
		},
		.compat = true,
	},
	{
		.desc = "identical 80 MHz",
		.c2 = {
			.width = NL80211_CHAN_WIDTH_80,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955 + 10 + 20,
		},
		.compat = true,
	},
	{
		.desc = "identical 160 MHz",
		.c2 = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955 + 10 + 20 + 40,
		},
		.compat = true,
	},
	{
		.desc = "identical 320 MHz",
		.c2 = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955 + 10 + 20 + 40 + 80,
		},
		.compat = true,
	},
	{
		.desc = "20 MHz in 320 MHz\n",
		.c1 = {
			.width = NL80211_CHAN_WIDTH_20,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955,
		},
		.c2 = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955 + 10 + 20 + 40 + 80,
		},
		.compat = true,
	},
	{
		.desc = "different 20 MHz",
		.c1 = {
			.width = NL80211_CHAN_WIDTH_20,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955,
		},
		.c2 = {
			.width = NL80211_CHAN_WIDTH_20,
			.chan = &chan_6ghz_5,
			.center_freq1 = 5975,
		},
	},
	{
		.desc = "different primary 320 MHz",
		.c1 = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 110,
		},
		.c2 = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 - 50,
		},
	},
	{
		/* similar to previous test but one has lower BW */
		.desc = "matching primary 160 MHz",
		.c1 = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
		},
		.c2 = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 - 50,
		},
		.compat = true,
	},
	{
		.desc = "matching primary 160 MHz & punctured secondary 160 Mhz",
		.c1 = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
		},
		.c2 = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 - 50,
			.punctured = 0xf,
		},
		.compat = true,
	},
	{
		.desc = "matching primary 160 MHz & punctured matching",
		.c1 = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0xc0,
		},
		.c2 = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 - 50,
			.punctured = 0xc000,
		},
		.compat = true,
	},
	{
		.desc = "matching primary 160 MHz & punctured not matching",
		.c1 = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0x80,
		},
		.c2 = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 - 50,
			.punctured = 0xc000,
		},
	},
};

KUNIT_ARRAY_PARAM_DESC(chandef_compat, chandef_compat_cases, desc)

static void test_chandef_compat(struct kunit *test)
{
	const struct chandef_compat_case *params = test->param_value;
	const struct cfg80211_chan_def *ret, *expect;
	struct cfg80211_chan_def c1 = params->c1;

	/* tests with identical ones */
	if (!params->c1.chan)
		c1 = params->c2;

	KUNIT_EXPECT_EQ(test, cfg80211_chandef_valid(&c1), true);
	KUNIT_EXPECT_EQ(test, cfg80211_chandef_valid(&params->c2), true);

	expect = params->compat ? &params->c2 : NULL;

	ret = cfg80211_chandef_compatible(&c1, &params->c2);
	KUNIT_EXPECT_PTR_EQ(test, ret, expect);

	if (!params->c1.chan)
		expect = &c1;

	ret = cfg80211_chandef_compatible(&params->c2, &c1);
	KUNIT_EXPECT_PTR_EQ(test, ret, expect);
}

static const struct chandef_dbe_case {
	const char *desc;
	struct cfg80211_chan_def c;
	u8 dbe[3];
	bool fails;
	u16 cf1;
} chandef_dbe_cases[] = {
	{
		.desc = "non-HT failure",
		.c = {
			.width = NL80211_CHAN_WIDTH_20_NOHT,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_40,
		.fails = true,
	},
	{
		.desc = "2.4 GHz fails",
		.c = {
			.width = NL80211_CHAN_WIDTH_20,
			.chan = &chan_2ghz_1,
			.center_freq1 = 2412,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_40,
		.fails = true,
	},
	{
		.desc = "DBE narrower",
		.c = {
			.width = NL80211_CHAN_WIDTH_320,
			.chan = &chan_6ghz_1,
			.center_freq1 = 5955 + 10 + 20 + 40 + 80,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_160,
		.fails = true,
	},
	{
		.desc = "DBE to 320-1",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_1,
		.cf1 = 6425,
	},
	{
		.desc = "DBE to 320-2",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_2,
		.cf1 = 6585,
	},
	{
		.desc = "bad disabled subchannel bitmap - not enough in BSS (1)",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0x0001,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_1 |
			  IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES,
		/* DBE disabled subchannel bitmap == 0 */
		.fails = true,
	},
	{
		.desc = "bad disabled subchannel bitmap - too much in BSS (1)",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0x0001,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_1 |
			  IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES,
		/* DBE disabled subchannel bitmap == 0x0300 */
		.dbe[2] = 0x03,
		.fails = true,
	},
	{
		.desc = "bad disabled subchannel bitmap - not enough in BSS (2)",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0x0001,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_2 |
			  IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES,
		/* DBE disabled subchannel bitmap == 0 */
		.fails = true,
	},
	{
		.desc = "bad disabled subchannel bitmap - too much in BSS (2)",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0x0001,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_2 |
			  IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES,
		/* DBE disabled subchannel bitmap == 0x03 */
		.dbe[1] = 0x03,
		.fails = true,
	},
	{
		.desc = "bad disabled subchannel bitmap - bad bitmap",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0x0001,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_1 |
			  IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES,
		/* DBE disabled subchannel bitmap == 0x1100 */
		.dbe[2] = 0x11,
		.fails = true,
	},
	{
		.desc = "good disabled subchannel bitmap (1)",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0x0003,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_1 |
			  IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES,
		/* DBE disabled subchannel bitmap == 0x0300 */
		.dbe[2] = 0x03,
		.cf1 = 6425,
	},
	{
		.desc = "good disabled subchannel bitmap (2)",
		.c = {
			.width = NL80211_CHAN_WIDTH_160,
			.chan = &chan_6ghz_105,
			.center_freq1 = 6475 + 30,
			.punctured = 0x0003,
		},
		.dbe[0] = IEEE80211_UHR_DBE_OPER_BW_320_2 |
			  IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES,
		/* DBE disabled subchannel bitmap == 0x0003 */
		.dbe[1] = 0x03,
		.cf1 = 6585,
	},
};

KUNIT_ARRAY_PARAM_DESC(chandef_dbe, chandef_dbe_cases, desc)

static void test_chandef_dbe(struct kunit *test)
{
	const struct chandef_dbe_case *params = test->param_value;
	struct cfg80211_chan_def c = params->c;
	int ret;

	KUNIT_EXPECT_EQ(test, cfg80211_chandef_valid(&params->c), true);

	ret = cfg80211_chandef_add_dbe(&c, (void *)params->dbe);
	KUNIT_EXPECT_EQ(test, ret != 0, params->fails);

	if (params->fails)
		return;

	KUNIT_EXPECT_EQ(test, c.center_freq1, params->cf1);
}

static struct kunit_case chandef_test_cases[] = {
	KUNIT_CASE_PARAM(test_chandef_compat, chandef_compat_gen_params),
	KUNIT_CASE_PARAM(test_chandef_dbe, chandef_dbe_gen_params),
	{}
};

static struct kunit_suite chandef = {
	.name = "cfg80211-chandef",
	.test_cases = chandef_test_cases,
};

kunit_test_suite(chandef);
