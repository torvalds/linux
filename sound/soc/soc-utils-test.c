// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/test.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <uapi/sound/asound.h>

static const struct {
	u32 rate;
	snd_pcm_format_t fmt;
	u8 channels;
	u8 tdm_width;
	u8 tdm_slots;
	u8 slot_multiple;
	u32 bclk;
} tdm_params_to_bclk_cases[] = {
	/* rate		fmt	   channels tdm_width tdm_slots slot_multiple bclk */

	/* From params only */
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 1,	0,	0,	0,	  128000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 2,	0,	0,	0,	  256000 },
	{   8000,  SNDRV_PCM_FORMAT_S24_LE, 1,	0,	0,	0,	  192000 },
	{   8000,  SNDRV_PCM_FORMAT_S24_LE, 2,	0,	0,	0,	  384000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 1,	0,	0,	0,	  256000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 2,	0,	0,	0,	  512000 },
	{  44100,  SNDRV_PCM_FORMAT_S16_LE, 1,	0,	0,	0,	  705600 },
	{  44100,  SNDRV_PCM_FORMAT_S16_LE, 2,	0,	0,	0,	 1411200 },
	{  44100,  SNDRV_PCM_FORMAT_S24_LE, 1,	0,	0,	0,	 1058400 },
	{  44100,  SNDRV_PCM_FORMAT_S24_LE, 2,	0,	0,	0,	 2116800 },
	{  44100,  SNDRV_PCM_FORMAT_S32_LE, 1,	0,	0,	0,	 1411200 },
	{  44100,  SNDRV_PCM_FORMAT_S32_LE, 2,	0,	0,	0,	 2822400 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 1,	0,	0,	0,	 6144000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 2,	0,	0,	0,	12288000 },
	{ 384000,  SNDRV_PCM_FORMAT_S24_LE, 1,	0,	0,	0,	 9216000 },
	{ 384000,  SNDRV_PCM_FORMAT_S24_LE, 2,	0,	0,	0,	18432000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 1,	0,	0,	0,	12288000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 2,	0,	0,	0,	24576000 },

	/* I2S from params */
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 1,	0,	0,	2,	  256000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 2,	0,	0,	2,	  256000 },
	{   8000,  SNDRV_PCM_FORMAT_S24_LE, 1,	0,	0,	2,	  384000 },
	{   8000,  SNDRV_PCM_FORMAT_S24_LE, 2,	0,	0,	2,	  384000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 1,	0,	0,	2,	  512000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 2,	0,	0,	2,	  512000 },
	{  44100,  SNDRV_PCM_FORMAT_S16_LE, 1,	0,	0,	2,	 1411200 },
	{  44100,  SNDRV_PCM_FORMAT_S16_LE, 2,	0,	0,	2,	 1411200 },
	{  44100,  SNDRV_PCM_FORMAT_S24_LE, 1,	0,	0,	2,	 2116800 },
	{  44100,  SNDRV_PCM_FORMAT_S24_LE, 2,	0,	0,	2,	 2116800 },
	{  44100,  SNDRV_PCM_FORMAT_S32_LE, 1,	0,	0,	2,	 2822400 },
	{  44100,  SNDRV_PCM_FORMAT_S32_LE, 2,	0,	0,	2,	 2822400 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 1,	0,	0,	2,	12288000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 2,	0,	0,	2,	12288000 },
	{ 384000,  SNDRV_PCM_FORMAT_S24_LE, 1,	0,	0,	2,	18432000 },
	{ 384000,  SNDRV_PCM_FORMAT_S24_LE, 2,	0,	0,	2,	18432000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 1,	0,	0,	2,	24576000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 2,	0,	0,	2,	24576000 },

	/* Fixed 8-slot TDM, other values from params */
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 1,	0,	8,	0,	 1024000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 2,	0,	8,	0,	 1024000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 3,	0,	8,	0,	 1024000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 4,	0,	8,	0,	 1024000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 1,	0,	8,	0,	 2048000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 2,	0,	8,	0,	 2048000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 3,	0,	8,	0,	 2048000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 4,	0,	8,	0,	 2048000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 1,	0,	8,	0,	49152000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 2,	0,	8,	0,	49152000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 3,	0,	8,	0,	49152000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 4,	0,	8,	0,	49152000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 1,	0,	8,	0,	98304000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 2,	0,	8,	0,	98304000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 3,	0,	8,	0,	98304000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 4,	0,	8,	0,	98304000 },

	/* Fixed 32-bit TDM, other values from params */
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 1,	32,	0,	0,	  256000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 2,	32,	0,	0,	  512000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 3,	32,	0,	0,	  768000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 4,	32,	0,	0,	 1024000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 1,	32,	0,	0,	  256000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 2,	32,	0,	0,	  512000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 3,	32,	0,	0,	  768000 },
	{   8000,  SNDRV_PCM_FORMAT_S32_LE, 4,	32,	0,	0,	 1024000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 1,	32,	0,	0,	12288000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 2,	32,	0,	0,	24576000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 3,	32,	0,	0,	36864000 },
	{ 384000,  SNDRV_PCM_FORMAT_S16_LE, 4,	32,	0,	0,	49152000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 1,	32,	0,	0,	12288000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 2,	32,	0,	0,	24576000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 3,	32,	0,	0,	36864000 },
	{ 384000,  SNDRV_PCM_FORMAT_S32_LE, 4,	32,	0,	0,	49152000 },

	/* Fixed 6-slot 24-bit TDM, other values from params */
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 1,	24,	6,	0,	 1152000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 2,	24,	6,	0,	 1152000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 3,	24,	6,	0,	 1152000 },
	{   8000,  SNDRV_PCM_FORMAT_S16_LE, 4,	24,	6,	0,	 1152000 },
	{   8000,  SNDRV_PCM_FORMAT_S24_LE, 1,	24,	6,	0,	 1152000 },
	{   8000,  SNDRV_PCM_FORMAT_S24_LE, 2,	24,	6,	0,	 1152000 },
	{   8000,  SNDRV_PCM_FORMAT_S24_LE, 3,	24,	6,	0,	 1152000 },
	{   8000,  SNDRV_PCM_FORMAT_S24_LE, 4,	24,	6,	0,	 1152000 },
	{ 192000,  SNDRV_PCM_FORMAT_S16_LE, 1,	24,	6,	0,	27648000 },
	{ 192000,  SNDRV_PCM_FORMAT_S16_LE, 2,	24,	6,	0,	27648000 },
	{ 192000,  SNDRV_PCM_FORMAT_S16_LE, 3,	24,	6,	0,	27648000 },
	{ 192000,  SNDRV_PCM_FORMAT_S16_LE, 4,	24,	6,	0,	27648000 },
	{ 192000,  SNDRV_PCM_FORMAT_S24_LE, 1,	24,	6,	0,	27648000 },
	{ 192000,  SNDRV_PCM_FORMAT_S24_LE, 2,	24,	6,	0,	27648000 },
	{ 192000,  SNDRV_PCM_FORMAT_S24_LE, 3,	24,	6,	0,	27648000 },
	{ 192000,  SNDRV_PCM_FORMAT_S24_LE, 4,	24,	6,	0,	27648000 },
};

static void test_tdm_params_to_bclk_one(struct kunit *test,
					unsigned int rate, snd_pcm_format_t fmt,
					unsigned int channels,
					unsigned int tdm_width, unsigned int tdm_slots,
					unsigned int slot_multiple,
					unsigned int expected_bclk)
{
	struct snd_pcm_hw_params params;
	int got_bclk;

	_snd_pcm_hw_params_any(&params);
	snd_mask_none(hw_param_mask(&params, SNDRV_PCM_HW_PARAM_FORMAT));
	hw_param_interval(&params, SNDRV_PCM_HW_PARAM_RATE)->min = rate;
	hw_param_interval(&params, SNDRV_PCM_HW_PARAM_RATE)->max = rate;
	hw_param_interval(&params, SNDRV_PCM_HW_PARAM_CHANNELS)->min = channels;
	hw_param_interval(&params, SNDRV_PCM_HW_PARAM_CHANNELS)->max = channels;
	params_set_format(&params, fmt);

	got_bclk = snd_soc_tdm_params_to_bclk(&params, tdm_width, tdm_slots, slot_multiple);
	pr_debug("%s: r=%u sb=%u ch=%u tw=%u ts=%u sm=%u expected=%u got=%d\n",
		 __func__,
		 rate, params_width(&params), channels, tdm_width, tdm_slots, slot_multiple,
		 expected_bclk, got_bclk);
	KUNIT_ASSERT_EQ(test, expected_bclk, (unsigned int)got_bclk);
}

static void test_tdm_params_to_bclk(struct kunit *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tdm_params_to_bclk_cases); ++i) {
		test_tdm_params_to_bclk_one(test,
					    tdm_params_to_bclk_cases[i].rate,
					    tdm_params_to_bclk_cases[i].fmt,
					    tdm_params_to_bclk_cases[i].channels,
					    tdm_params_to_bclk_cases[i].tdm_width,
					    tdm_params_to_bclk_cases[i].tdm_slots,
					    tdm_params_to_bclk_cases[i].slot_multiple,
					    tdm_params_to_bclk_cases[i].bclk);

		if (tdm_params_to_bclk_cases[i].slot_multiple > 0)
			continue;

		/* Slot multiple 1 should have the same effect as multiple 0 */
		test_tdm_params_to_bclk_one(test,
					    tdm_params_to_bclk_cases[i].rate,
					    tdm_params_to_bclk_cases[i].fmt,
					    tdm_params_to_bclk_cases[i].channels,
					    tdm_params_to_bclk_cases[i].tdm_width,
					    tdm_params_to_bclk_cases[i].tdm_slots,
					    1,
					    tdm_params_to_bclk_cases[i].bclk);
	}
}

static void test_snd_soc_params_to_bclk_one(struct kunit *test,
					    unsigned int rate, snd_pcm_format_t fmt,
					    unsigned int channels,
					    unsigned int expected_bclk)
{
	struct snd_pcm_hw_params params;
	int got_bclk;

	_snd_pcm_hw_params_any(&params);
	snd_mask_none(hw_param_mask(&params, SNDRV_PCM_HW_PARAM_FORMAT));
	hw_param_interval(&params, SNDRV_PCM_HW_PARAM_RATE)->min = rate;
	hw_param_interval(&params, SNDRV_PCM_HW_PARAM_RATE)->max = rate;
	hw_param_interval(&params, SNDRV_PCM_HW_PARAM_CHANNELS)->min = channels;
	hw_param_interval(&params, SNDRV_PCM_HW_PARAM_CHANNELS)->max = channels;
	params_set_format(&params, fmt);

	got_bclk = snd_soc_params_to_bclk(&params);
	pr_debug("%s: r=%u sb=%u ch=%u expected=%u got=%d\n",
		 __func__,
		 rate, params_width(&params), channels, expected_bclk, got_bclk);
	KUNIT_ASSERT_EQ(test, expected_bclk, (unsigned int)got_bclk);
}

static void test_snd_soc_params_to_bclk(struct kunit *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tdm_params_to_bclk_cases); ++i) {
		/*
		 * snd_soc_params_to_bclk() is all the test cases where
		 * snd_pcm_hw_params values are not overridden.
		 */
		if (tdm_params_to_bclk_cases[i].tdm_width |
		    tdm_params_to_bclk_cases[i].tdm_slots |
		    tdm_params_to_bclk_cases[i].slot_multiple)
			continue;

		test_snd_soc_params_to_bclk_one(test,
						tdm_params_to_bclk_cases[i].rate,
						tdm_params_to_bclk_cases[i].fmt,
						tdm_params_to_bclk_cases[i].channels,
						tdm_params_to_bclk_cases[i].bclk);
	}
}

static struct kunit_case soc_utils_test_cases[] = {
	KUNIT_CASE(test_tdm_params_to_bclk),
	KUNIT_CASE(test_snd_soc_params_to_bclk),
	{}
};

static struct kunit_suite soc_utils_test_suite = {
	.name = "soc-utils",
	.test_cases = soc_utils_test_cases,
};

kunit_test_suites(&soc_utils_test_suite);

MODULE_DESCRIPTION("ASoC soc-utils kunit test");
MODULE_LICENSE("GPL");
