// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADAU1977/ADAU1978/ADAU1979 driver
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_data/adau1977.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "adau1977.h"

#define ADAU1977_REG_POWER		0x00
#define ADAU1977_REG_PLL		0x01
#define ADAU1977_REG_BOOST		0x02
#define ADAU1977_REG_MICBIAS		0x03
#define ADAU1977_REG_BLOCK_POWER_SAI	0x04
#define ADAU1977_REG_SAI_CTRL0		0x05
#define ADAU1977_REG_SAI_CTRL1		0x06
#define ADAU1977_REG_CMAP12		0x07
#define ADAU1977_REG_CMAP34		0x08
#define ADAU1977_REG_SAI_OVERTEMP	0x09
#define ADAU1977_REG_POST_ADC_GAIN(x)	(0x0a + (x))
#define ADAU1977_REG_MISC_CONTROL	0x0e
#define ADAU1977_REG_DIAG_CONTROL	0x10
#define ADAU1977_REG_STATUS(x)		(0x11 + (x))
#define ADAU1977_REG_DIAG_IRQ1		0x15
#define ADAU1977_REG_DIAG_IRQ2		0x16
#define ADAU1977_REG_ADJUST1		0x17
#define ADAU1977_REG_ADJUST2		0x18
#define ADAU1977_REG_ADC_CLIP		0x19
#define ADAU1977_REG_DC_HPF_CAL		0x1a

#define ADAU1977_POWER_RESET			BIT(7)
#define ADAU1977_POWER_PWUP			BIT(0)

#define ADAU1977_PLL_CLK_S			BIT(4)
#define ADAU1977_PLL_MCS_MASK			0x7

#define ADAU1977_MICBIAS_MB_VOLTS_MASK		0xf0
#define ADAU1977_MICBIAS_MB_VOLTS_OFFSET	4

#define ADAU1977_BLOCK_POWER_SAI_LR_POL		BIT(7)
#define ADAU1977_BLOCK_POWER_SAI_BCLK_EDGE	BIT(6)
#define ADAU1977_BLOCK_POWER_SAI_LDO_EN		BIT(5)

#define ADAU1977_SAI_CTRL0_FMT_MASK		(0x3 << 6)
#define ADAU1977_SAI_CTRL0_FMT_I2S		(0x0 << 6)
#define ADAU1977_SAI_CTRL0_FMT_LJ		(0x1 << 6)
#define ADAU1977_SAI_CTRL0_FMT_RJ_24BIT		(0x2 << 6)
#define ADAU1977_SAI_CTRL0_FMT_RJ_16BIT		(0x3 << 6)

#define ADAU1977_SAI_CTRL0_SAI_MASK		(0x7 << 3)
#define ADAU1977_SAI_CTRL0_SAI_I2S		(0x0 << 3)
#define ADAU1977_SAI_CTRL0_SAI_TDM_2		(0x1 << 3)
#define ADAU1977_SAI_CTRL0_SAI_TDM_4		(0x2 << 3)
#define ADAU1977_SAI_CTRL0_SAI_TDM_8		(0x3 << 3)
#define ADAU1977_SAI_CTRL0_SAI_TDM_16		(0x4 << 3)

#define ADAU1977_SAI_CTRL0_FS_MASK		(0x7)
#define ADAU1977_SAI_CTRL0_FS_8000_12000	(0x0)
#define ADAU1977_SAI_CTRL0_FS_16000_24000	(0x1)
#define ADAU1977_SAI_CTRL0_FS_32000_48000	(0x2)
#define ADAU1977_SAI_CTRL0_FS_64000_96000	(0x3)
#define ADAU1977_SAI_CTRL0_FS_128000_192000	(0x4)

#define ADAU1977_SAI_CTRL1_SLOT_WIDTH_MASK	(0x3 << 5)
#define ADAU1977_SAI_CTRL1_SLOT_WIDTH_32	(0x0 << 5)
#define ADAU1977_SAI_CTRL1_SLOT_WIDTH_24	(0x1 << 5)
#define ADAU1977_SAI_CTRL1_SLOT_WIDTH_16	(0x2 << 5)
#define ADAU1977_SAI_CTRL1_DATA_WIDTH_MASK	(0x1 << 4)
#define ADAU1977_SAI_CTRL1_DATA_WIDTH_16BIT	(0x1 << 4)
#define ADAU1977_SAI_CTRL1_DATA_WIDTH_24BIT	(0x0 << 4)
#define ADAU1977_SAI_CTRL1_LRCLK_PULSE		BIT(3)
#define ADAU1977_SAI_CTRL1_MSB			BIT(2)
#define ADAU1977_SAI_CTRL1_BCLKRATE_16		(0x1 << 1)
#define ADAU1977_SAI_CTRL1_BCLKRATE_32		(0x0 << 1)
#define ADAU1977_SAI_CTRL1_BCLKRATE_MASK	(0x1 << 1)
#define ADAU1977_SAI_CTRL1_MASTER		BIT(0)

#define ADAU1977_SAI_OVERTEMP_DRV_C(x)		BIT(4 + (x))
#define ADAU1977_SAI_OVERTEMP_DRV_HIZ		BIT(3)

#define ADAU1977_MISC_CONTROL_SUM_MODE_MASK	(0x3 << 6)
#define ADAU1977_MISC_CONTROL_SUM_MODE_1CH	(0x2 << 6)
#define ADAU1977_MISC_CONTROL_SUM_MODE_2CH	(0x1 << 6)
#define ADAU1977_MISC_CONTROL_SUM_MODE_4CH	(0x0 << 6)
#define ADAU1977_MISC_CONTROL_MMUTE		BIT(4)
#define ADAU1977_MISC_CONTROL_DC_CAL		BIT(0)

#define ADAU1977_CHAN_MAP_SECOND_SLOT_OFFSET	4
#define ADAU1977_CHAN_MAP_FIRST_SLOT_OFFSET	0

struct adau1977 {
	struct regmap *regmap;
	bool right_j;
	unsigned int sysclk;
	enum adau1977_sysclk_src sysclk_src;
	struct gpio_desc *reset_gpio;
	enum adau1977_type type;

	struct regulator *avdd_reg;
	struct regulator *dvdd_reg;

	struct snd_pcm_hw_constraint_list constraints;

	struct device *dev;
	void (*switch_mode)(struct device *dev);

	unsigned int max_master_fs;
	unsigned int slot_width;
	bool enabled;
	bool master;
};

static const struct reg_default adau1977_reg_defaults[] = {
	{ 0x00, 0x00 },
	{ 0x01, 0x41 },
	{ 0x02, 0x4a },
	{ 0x03, 0x7d },
	{ 0x04, 0x3d },
	{ 0x05, 0x02 },
	{ 0x06, 0x00 },
	{ 0x07, 0x10 },
	{ 0x08, 0x32 },
	{ 0x09, 0xf0 },
	{ 0x0a, 0xa0 },
	{ 0x0b, 0xa0 },
	{ 0x0c, 0xa0 },
	{ 0x0d, 0xa0 },
	{ 0x0e, 0x02 },
	{ 0x10, 0x0f },
	{ 0x15, 0x20 },
	{ 0x16, 0x00 },
	{ 0x17, 0x00 },
	{ 0x18, 0x00 },
	{ 0x1a, 0x00 },
};

static const DECLARE_TLV_DB_MINMAX_MUTE(adau1977_adc_gain, -3562, 6000);

static const struct snd_soc_dapm_widget adau1977_micbias_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("MICBIAS", ADAU1977_REG_MICBIAS,
		3, 0, NULL, 0)
};

static const struct snd_soc_dapm_widget adau1977_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("Vref", ADAU1977_REG_BLOCK_POWER_SAI,
		4, 0, NULL, 0),

	SND_SOC_DAPM_ADC("ADC1", "Capture", ADAU1977_REG_BLOCK_POWER_SAI, 0, 0),
	SND_SOC_DAPM_ADC("ADC2", "Capture", ADAU1977_REG_BLOCK_POWER_SAI, 1, 0),
	SND_SOC_DAPM_ADC("ADC3", "Capture", ADAU1977_REG_BLOCK_POWER_SAI, 2, 0),
	SND_SOC_DAPM_ADC("ADC4", "Capture", ADAU1977_REG_BLOCK_POWER_SAI, 3, 0),

	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),
	SND_SOC_DAPM_INPUT("AIN3"),
	SND_SOC_DAPM_INPUT("AIN4"),

	SND_SOC_DAPM_OUTPUT("VREF"),
};

static const struct snd_soc_dapm_route adau1977_dapm_routes[] = {
	{ "ADC1", NULL, "AIN1" },
	{ "ADC2", NULL, "AIN2" },
	{ "ADC3", NULL, "AIN3" },
	{ "ADC4", NULL, "AIN4" },

	{ "ADC1", NULL, "Vref" },
	{ "ADC2", NULL, "Vref" },
	{ "ADC3", NULL, "Vref" },
	{ "ADC4", NULL, "Vref" },

	{ "VREF", NULL, "Vref" },
};

#define ADAU1977_VOLUME(x) \
	SOC_SINGLE_TLV("ADC" #x " Capture Volume", \
		ADAU1977_REG_POST_ADC_GAIN((x) - 1), \
		0, 255, 1, adau1977_adc_gain)

#define ADAU1977_HPF_SWITCH(x) \
	SOC_SINGLE("ADC" #x " Highpass-Filter Capture Switch", \
		ADAU1977_REG_DC_HPF_CAL, (x) - 1, 1, 0)

#define ADAU1977_DC_SUB_SWITCH(x) \
	SOC_SINGLE("ADC" #x " DC Subtraction Capture Switch", \
		ADAU1977_REG_DC_HPF_CAL, (x) + 3, 1, 0)

static const struct snd_kcontrol_new adau1977_snd_controls[] = {
	ADAU1977_VOLUME(1),
	ADAU1977_VOLUME(2),
	ADAU1977_VOLUME(3),
	ADAU1977_VOLUME(4),

	ADAU1977_HPF_SWITCH(1),
	ADAU1977_HPF_SWITCH(2),
	ADAU1977_HPF_SWITCH(3),
	ADAU1977_HPF_SWITCH(4),

	ADAU1977_DC_SUB_SWITCH(1),
	ADAU1977_DC_SUB_SWITCH(2),
	ADAU1977_DC_SUB_SWITCH(3),
	ADAU1977_DC_SUB_SWITCH(4),
};

static int adau1977_reset(struct adau1977 *adau1977)
{
	int ret;

	/*
	 * The reset bit is obviously volatile, but we need to be able to cache
	 * the other bits in the register, so we can't just mark the whole
	 * register as volatile. Since this is the only place where we'll ever
	 * touch the reset bit just bypass the cache for this operation.
	 */
	regcache_cache_bypass(adau1977->regmap, true);
	ret = regmap_write(adau1977->regmap, ADAU1977_REG_POWER,
			ADAU1977_POWER_RESET);
	regcache_cache_bypass(adau1977->regmap, false);
	if (ret)
		return ret;

	return ret;
}

/*
 * Returns the appropriate setting for ths FS field in the CTRL0 register
 * depending on the rate.
 */
static int adau1977_lookup_fs(unsigned int rate)
{
	if (rate >= 8000 && rate <= 12000)
		return ADAU1977_SAI_CTRL0_FS_8000_12000;
	else if (rate >= 16000 && rate <= 24000)
		return ADAU1977_SAI_CTRL0_FS_16000_24000;
	else if (rate >= 32000 && rate <= 48000)
		return ADAU1977_SAI_CTRL0_FS_32000_48000;
	else if (rate >= 64000 && rate <= 96000)
		return ADAU1977_SAI_CTRL0_FS_64000_96000;
	else if (rate >= 128000 && rate <= 192000)
		return ADAU1977_SAI_CTRL0_FS_128000_192000;
	else
		return -EINVAL;
}

static int adau1977_lookup_mcs(struct adau1977 *adau1977, unsigned int rate,
	unsigned int fs)
{
	unsigned int mcs;

	/*
	 * rate = sysclk / (512 * mcs_lut[mcs]) * 2**fs
	 * => mcs_lut[mcs] = sysclk / (512 * rate) * 2**fs
	 * => mcs_lut[mcs] = sysclk / ((512 / 2**fs) * rate)
	 */

	rate *= 512 >> fs;

	if (adau1977->sysclk % rate != 0)
		return -EINVAL;

	mcs = adau1977->sysclk / rate;

	/* The factors configured by MCS are 1, 2, 3, 4, 6 */
	if (mcs < 1 || mcs > 6 || mcs == 5)
		return -EINVAL;

	mcs = mcs - 1;
	if (mcs == 5)
		mcs = 4;

	return mcs;
}

static int adau1977_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(component);
	unsigned int rate = params_rate(params);
	unsigned int slot_width;
	unsigned int ctrl0, ctrl0_mask;
	unsigned int ctrl1;
	int mcs, fs;
	int ret;

	fs = adau1977_lookup_fs(rate);
	if (fs < 0)
		return fs;

	if (adau1977->sysclk_src == ADAU1977_SYSCLK_SRC_MCLK) {
		mcs = adau1977_lookup_mcs(adau1977, rate, fs);
		if (mcs < 0)
			return mcs;
	} else {
		mcs = 0;
	}

	ctrl0_mask = ADAU1977_SAI_CTRL0_FS_MASK;
	ctrl0 = fs;

	if (adau1977->right_j) {
		switch (params_width(params)) {
		case 16:
			ctrl0 |= ADAU1977_SAI_CTRL0_FMT_RJ_16BIT;
			break;
		case 24:
			ctrl0 |= ADAU1977_SAI_CTRL0_FMT_RJ_24BIT;
			break;
		default:
			return -EINVAL;
		}
		ctrl0_mask |= ADAU1977_SAI_CTRL0_FMT_MASK;
	}

	if (adau1977->master) {
		switch (params_width(params)) {
		case 16:
			ctrl1 = ADAU1977_SAI_CTRL1_DATA_WIDTH_16BIT;
			slot_width = 16;
			break;
		case 24:
		case 32:
			ctrl1 = ADAU1977_SAI_CTRL1_DATA_WIDTH_24BIT;
			slot_width = 32;
			break;
		default:
			return -EINVAL;
		}

		/* In TDM mode there is a fixed slot width */
		if (adau1977->slot_width)
			slot_width = adau1977->slot_width;

		if (slot_width == 16)
			ctrl1 |= ADAU1977_SAI_CTRL1_BCLKRATE_16;
		else
			ctrl1 |= ADAU1977_SAI_CTRL1_BCLKRATE_32;

		ret = regmap_update_bits(adau1977->regmap,
			ADAU1977_REG_SAI_CTRL1,
			ADAU1977_SAI_CTRL1_DATA_WIDTH_MASK |
			ADAU1977_SAI_CTRL1_BCLKRATE_MASK,
			ctrl1);
		if (ret < 0)
			return ret;
	}

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_SAI_CTRL0,
				ctrl0_mask, ctrl0);
	if (ret < 0)
		return ret;

	return regmap_update_bits(adau1977->regmap, ADAU1977_REG_PLL,
				ADAU1977_PLL_MCS_MASK, mcs);
}

static int adau1977_power_disable(struct adau1977 *adau1977)
{
	int ret = 0;

	if (!adau1977->enabled)
		return 0;

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_POWER,
		ADAU1977_POWER_PWUP, 0);
	if (ret)
		return ret;

	regcache_mark_dirty(adau1977->regmap);

	gpiod_set_value_cansleep(adau1977->reset_gpio, 0);

	regcache_cache_only(adau1977->regmap, true);

	regulator_disable(adau1977->avdd_reg);
	if (adau1977->dvdd_reg)
		regulator_disable(adau1977->dvdd_reg);

	adau1977->enabled = false;

	return 0;
}

static int adau1977_power_enable(struct adau1977 *adau1977)
{
	unsigned int val;
	int ret = 0;

	if (adau1977->enabled)
		return 0;

	ret = regulator_enable(adau1977->avdd_reg);
	if (ret)
		return ret;

	if (adau1977->dvdd_reg) {
		ret = regulator_enable(adau1977->dvdd_reg);
		if (ret)
			goto err_disable_avdd;
	}

	gpiod_set_value_cansleep(adau1977->reset_gpio, 1);

	regcache_cache_only(adau1977->regmap, false);

	if (adau1977->switch_mode)
		adau1977->switch_mode(adau1977->dev);

	ret = adau1977_reset(adau1977);
	if (ret)
		goto err_disable_dvdd;

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_POWER,
		ADAU1977_POWER_PWUP, ADAU1977_POWER_PWUP);
	if (ret)
		goto err_disable_dvdd;

	ret = regcache_sync(adau1977->regmap);
	if (ret)
		goto err_disable_dvdd;

	/*
	 * The PLL register is not affected by the software reset. It is
	 * possible that the value of the register was changed to the
	 * default value while we were in cache only mode. In this case
	 * regcache_sync will skip over it and we have to manually sync
	 * it.
	 */
	ret = regmap_read(adau1977->regmap, ADAU1977_REG_PLL, &val);
	if (ret)
		goto err_disable_dvdd;

	if (val == 0x41) {
		regcache_cache_bypass(adau1977->regmap, true);
		ret = regmap_write(adau1977->regmap, ADAU1977_REG_PLL,
			0x41);
		if (ret)
			goto err_disable_dvdd;
		regcache_cache_bypass(adau1977->regmap, false);
	}

	adau1977->enabled = true;

	return ret;

err_disable_dvdd:
	if (adau1977->dvdd_reg)
		regulator_disable(adau1977->dvdd_reg);
err_disable_avdd:
		regulator_disable(adau1977->avdd_reg);
	return ret;
}

static int adau1977_set_bias_level(struct snd_soc_component *component,
	enum snd_soc_bias_level level)
{
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			ret = adau1977_power_enable(adau1977);
		break;
	case SND_SOC_BIAS_OFF:
		ret = adau1977_power_disable(adau1977);
		break;
	}

	return ret;
}

static int adau1977_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
	unsigned int rx_mask, int slots, int width)
{
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(dai->component);
	unsigned int ctrl0, ctrl1, drv;
	unsigned int slot[4];
	unsigned int i;
	int ret;

	if (slots == 0) {
		/* 0 = No fixed slot width */
		adau1977->slot_width = 0;
		adau1977->max_master_fs = 192000;
		return regmap_update_bits(adau1977->regmap,
			ADAU1977_REG_SAI_CTRL0, ADAU1977_SAI_CTRL0_SAI_MASK,
			ADAU1977_SAI_CTRL0_SAI_I2S);
	}

	if (rx_mask == 0 || tx_mask != 0)
		return -EINVAL;

	drv = 0;
	for (i = 0; i < 4; i++) {
		slot[i] = __ffs(rx_mask);
		drv |= ADAU1977_SAI_OVERTEMP_DRV_C(i);
		rx_mask &= ~(1 << slot[i]);
		if (slot[i] >= slots)
			return -EINVAL;
		if (rx_mask == 0)
			break;
	}

	if (rx_mask != 0)
		return -EINVAL;

	switch (width) {
	case 16:
		ctrl1 = ADAU1977_SAI_CTRL1_SLOT_WIDTH_16;
		break;
	case 24:
		/* We can only generate 16 bit or 32 bit wide slots */
		if (adau1977->master)
			return -EINVAL;
		ctrl1 = ADAU1977_SAI_CTRL1_SLOT_WIDTH_24;
		break;
	case 32:
		ctrl1 = ADAU1977_SAI_CTRL1_SLOT_WIDTH_32;
		break;
	default:
		return -EINVAL;
	}

	switch (slots) {
	case 2:
		ctrl0 = ADAU1977_SAI_CTRL0_SAI_TDM_2;
		break;
	case 4:
		ctrl0 = ADAU1977_SAI_CTRL0_SAI_TDM_4;
		break;
	case 8:
		ctrl0 = ADAU1977_SAI_CTRL0_SAI_TDM_8;
		break;
	case 16:
		ctrl0 = ADAU1977_SAI_CTRL0_SAI_TDM_16;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_SAI_OVERTEMP,
		ADAU1977_SAI_OVERTEMP_DRV_C(0) |
		ADAU1977_SAI_OVERTEMP_DRV_C(1) |
		ADAU1977_SAI_OVERTEMP_DRV_C(2) |
		ADAU1977_SAI_OVERTEMP_DRV_C(3), drv);
	if (ret)
		return ret;

	ret = regmap_write(adau1977->regmap, ADAU1977_REG_CMAP12,
		(slot[1] << ADAU1977_CHAN_MAP_SECOND_SLOT_OFFSET) |
		(slot[0] << ADAU1977_CHAN_MAP_FIRST_SLOT_OFFSET));
	if (ret)
		return ret;

	ret = regmap_write(adau1977->regmap, ADAU1977_REG_CMAP34,
		(slot[3] << ADAU1977_CHAN_MAP_SECOND_SLOT_OFFSET) |
		(slot[2] << ADAU1977_CHAN_MAP_FIRST_SLOT_OFFSET));
	if (ret)
		return ret;

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_SAI_CTRL0,
		ADAU1977_SAI_CTRL0_SAI_MASK, ctrl0);
	if (ret)
		return ret;

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_SAI_CTRL1,
		ADAU1977_SAI_CTRL1_SLOT_WIDTH_MASK, ctrl1);
	if (ret)
		return ret;

	adau1977->slot_width = width;

	/* In master mode the maximum bitclock is 24.576 MHz */
	adau1977->max_master_fs = min(192000, 24576000 / width / slots);

	return 0;
}

static int adau1977_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(dai->component);
	unsigned int val;

	if (mute)
		val = ADAU1977_MISC_CONTROL_MMUTE;
	else
		val = 0;

	return regmap_update_bits(adau1977->regmap, ADAU1977_REG_MISC_CONTROL,
			ADAU1977_MISC_CONTROL_MMUTE, val);
}

static int adau1977_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(dai->component);
	unsigned int ctrl0 = 0, ctrl1 = 0, block_power = 0;
	bool invert_lrclk;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		adau1977->master = false;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		ctrl1 |= ADAU1977_SAI_CTRL1_MASTER;
		adau1977->master = true;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		invert_lrclk = false;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		block_power |= ADAU1977_BLOCK_POWER_SAI_BCLK_EDGE;
		invert_lrclk = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		invert_lrclk = true;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		block_power |= ADAU1977_BLOCK_POWER_SAI_BCLK_EDGE;
		invert_lrclk = true;
		break;
	default:
		return -EINVAL;
	}

	adau1977->right_j = false;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ctrl0 |= ADAU1977_SAI_CTRL0_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl0 |= ADAU1977_SAI_CTRL0_FMT_LJ;
		invert_lrclk = !invert_lrclk;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl0 |= ADAU1977_SAI_CTRL0_FMT_RJ_24BIT;
		adau1977->right_j = true;
		invert_lrclk = !invert_lrclk;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ctrl1 |= ADAU1977_SAI_CTRL1_LRCLK_PULSE;
		ctrl0 |= ADAU1977_SAI_CTRL0_FMT_I2S;
		invert_lrclk = false;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		ctrl1 |= ADAU1977_SAI_CTRL1_LRCLK_PULSE;
		ctrl0 |= ADAU1977_SAI_CTRL0_FMT_LJ;
		invert_lrclk = false;
		break;
	default:
		return -EINVAL;
	}

	if (invert_lrclk)
		block_power |= ADAU1977_BLOCK_POWER_SAI_LR_POL;

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_BLOCK_POWER_SAI,
		ADAU1977_BLOCK_POWER_SAI_LR_POL |
		ADAU1977_BLOCK_POWER_SAI_BCLK_EDGE, block_power);
	if (ret)
		return ret;

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_SAI_CTRL0,
		ADAU1977_SAI_CTRL0_FMT_MASK,
		ctrl0);
	if (ret)
		return ret;

	return regmap_update_bits(adau1977->regmap, ADAU1977_REG_SAI_CTRL1,
		ADAU1977_SAI_CTRL1_MASTER | ADAU1977_SAI_CTRL1_LRCLK_PULSE,
		ctrl1);
}

static int adau1977_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(dai->component);
	u64 formats = 0;

	if (adau1977->slot_width == 16)
		formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE;
	else if (adau1977->right_j || adau1977->slot_width == 24)
		formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE;

	snd_pcm_hw_constraint_list(substream->runtime, 0,
		SNDRV_PCM_HW_PARAM_RATE, &adau1977->constraints);

	if (adau1977->master)
		snd_pcm_hw_constraint_minmax(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 8000, adau1977->max_master_fs);

	if (formats != 0)
		snd_pcm_hw_constraint_mask64(substream->runtime,
			SNDRV_PCM_HW_PARAM_FORMAT, formats);

	return 0;
}

static int adau1977_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(dai->component);
	unsigned int val;

	if (tristate)
		val = ADAU1977_SAI_OVERTEMP_DRV_HIZ;
	else
		val = 0;

	return regmap_update_bits(adau1977->regmap, ADAU1977_REG_SAI_OVERTEMP,
		ADAU1977_SAI_OVERTEMP_DRV_HIZ, val);
}

static const struct snd_soc_dai_ops adau1977_dai_ops = {
	.startup	= adau1977_startup,
	.hw_params	= adau1977_hw_params,
	.mute_stream	= adau1977_mute,
	.set_fmt	= adau1977_set_dai_fmt,
	.set_tdm_slot	= adau1977_set_tdm_slot,
	.set_tristate	= adau1977_set_tristate,
};

static struct snd_soc_dai_driver adau1977_dai = {
	.name = "adau1977-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
		    SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 24,
	},
	.ops = &adau1977_dai_ops,
};

static const unsigned int adau1977_rates[] = {
	8000, 16000, 32000, 64000, 128000,
	11025, 22050, 44100, 88200, 172400,
	12000, 24000, 48000, 96000, 192000,
};

#define ADAU1977_RATE_CONSTRAINT_MASK_32000 0x001f
#define ADAU1977_RATE_CONSTRAINT_MASK_44100 0x03e0
#define ADAU1977_RATE_CONSTRAINT_MASK_48000 0x7c00
/* All rates >= 32000 */
#define ADAU1977_RATE_CONSTRAINT_MASK_LRCLK 0x739c

static bool adau1977_check_sysclk(unsigned int mclk, unsigned int base_freq)
{
	unsigned int mcs;

	if (mclk % (base_freq * 128) != 0)
		return false;

	mcs = mclk / (128 * base_freq);
	if (mcs < 1 || mcs > 6 || mcs == 5)
		return false;

	return true;
}

static int adau1977_set_sysclk(struct snd_soc_component *component,
	int clk_id, int source, unsigned int freq, int dir)
{
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(component);
	unsigned int mask = 0;
	unsigned int clk_src;
	unsigned int ret;

	if (dir != SND_SOC_CLOCK_IN)
		return -EINVAL;

	if (clk_id != ADAU1977_SYSCLK)
		return -EINVAL;

	switch (source) {
	case ADAU1977_SYSCLK_SRC_MCLK:
		clk_src = 0;
		break;
	case ADAU1977_SYSCLK_SRC_LRCLK:
		clk_src = ADAU1977_PLL_CLK_S;
		break;
	default:
		return -EINVAL;
	}

	if (freq != 0 && source == ADAU1977_SYSCLK_SRC_MCLK) {
		if (freq < 4000000 || freq > 36864000)
			return -EINVAL;

		if (adau1977_check_sysclk(freq, 32000))
			mask |= ADAU1977_RATE_CONSTRAINT_MASK_32000;
		if (adau1977_check_sysclk(freq, 44100))
			mask |= ADAU1977_RATE_CONSTRAINT_MASK_44100;
		if (adau1977_check_sysclk(freq, 48000))
			mask |= ADAU1977_RATE_CONSTRAINT_MASK_48000;

		if (mask == 0)
			return -EINVAL;
	} else if (source == ADAU1977_SYSCLK_SRC_LRCLK) {
		mask = ADAU1977_RATE_CONSTRAINT_MASK_LRCLK;
	}

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_PLL,
		ADAU1977_PLL_CLK_S, clk_src);
	if (ret)
		return ret;

	adau1977->constraints.mask = mask;
	adau1977->sysclk_src = source;
	adau1977->sysclk = freq;

	return 0;
}

static int adau1977_component_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct adau1977 *adau1977 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (adau1977->type) {
	case ADAU1977:
		ret = snd_soc_dapm_new_controls(dapm,
			adau1977_micbias_dapm_widgets,
			ARRAY_SIZE(adau1977_micbias_dapm_widgets));
		if (ret < 0)
			return ret;
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_component_driver adau1977_component_driver = {
	.probe			= adau1977_component_probe,
	.set_bias_level		= adau1977_set_bias_level,
	.set_sysclk		= adau1977_set_sysclk,
	.controls		= adau1977_snd_controls,
	.num_controls		= ARRAY_SIZE(adau1977_snd_controls),
	.dapm_widgets		= adau1977_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(adau1977_dapm_widgets),
	.dapm_routes		= adau1977_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(adau1977_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int adau1977_setup_micbias(struct adau1977 *adau1977)
{
	struct adau1977_platform_data *pdata = adau1977->dev->platform_data;
	unsigned int micbias;

	if (pdata)
		micbias = pdata->micbias;
	else if (device_property_read_u32(adau1977->dev, "adi,micbias",
					  &micbias))
		micbias = ADAU1977_MICBIAS_8V5;

	if (micbias > ADAU1977_MICBIAS_9V0) {
		dev_err(adau1977->dev, "Invalid value for 'adi,micbias'\n");
		return -EINVAL;
	}

	return regmap_update_bits(adau1977->regmap, ADAU1977_REG_MICBIAS,
		ADAU1977_MICBIAS_MB_VOLTS_MASK,
		micbias << ADAU1977_MICBIAS_MB_VOLTS_OFFSET);
}

int adau1977_probe(struct device *dev, struct regmap *regmap,
	enum adau1977_type type, void (*switch_mode)(struct device *dev))
{
	unsigned int power_off_mask;
	struct adau1977 *adau1977;
	int ret;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	adau1977 = devm_kzalloc(dev, sizeof(*adau1977), GFP_KERNEL);
	if (adau1977 == NULL)
		return -ENOMEM;

	adau1977->dev = dev;
	adau1977->type = type;
	adau1977->regmap = regmap;
	adau1977->switch_mode = switch_mode;
	adau1977->max_master_fs = 192000;

	adau1977->constraints.list = adau1977_rates;
	adau1977->constraints.count = ARRAY_SIZE(adau1977_rates);

	adau1977->avdd_reg = devm_regulator_get(dev, "AVDD");
	if (IS_ERR(adau1977->avdd_reg))
		return PTR_ERR(adau1977->avdd_reg);

	adau1977->dvdd_reg = devm_regulator_get_optional(dev, "DVDD");
	if (IS_ERR(adau1977->dvdd_reg)) {
		if (PTR_ERR(adau1977->dvdd_reg) != -ENODEV)
			return PTR_ERR(adau1977->dvdd_reg);
		adau1977->dvdd_reg = NULL;
	}

	adau1977->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						       GPIOD_OUT_LOW);
	if (IS_ERR(adau1977->reset_gpio))
		return PTR_ERR(adau1977->reset_gpio);

	dev_set_drvdata(dev, adau1977);

	if (adau1977->reset_gpio)
		ndelay(100);

	ret = adau1977_power_enable(adau1977);
	if (ret)
		return ret;

	if (type == ADAU1977) {
		ret = adau1977_setup_micbias(adau1977);
		if (ret)
			goto err_poweroff;
	}

	if (adau1977->dvdd_reg)
		power_off_mask = ~0;
	else
		power_off_mask = (unsigned int)~ADAU1977_BLOCK_POWER_SAI_LDO_EN;

	ret = regmap_update_bits(adau1977->regmap, ADAU1977_REG_BLOCK_POWER_SAI,
				power_off_mask, 0x00);
	if (ret)
		goto err_poweroff;

	ret = adau1977_power_disable(adau1977);
	if (ret)
		return ret;

	return devm_snd_soc_register_component(dev, &adau1977_component_driver,
			&adau1977_dai, 1);

err_poweroff:
	adau1977_power_disable(adau1977);
	return ret;

}
EXPORT_SYMBOL_GPL(adau1977_probe);

static bool adau1977_register_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADAU1977_REG_STATUS(0):
	case ADAU1977_REG_STATUS(1):
	case ADAU1977_REG_STATUS(2):
	case ADAU1977_REG_STATUS(3):
	case ADAU1977_REG_ADC_CLIP:
		return true;
	}

	return false;
}

const struct regmap_config adau1977_regmap_config = {
	.max_register = ADAU1977_REG_DC_HPF_CAL,
	.volatile_reg = adau1977_register_volatile,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = adau1977_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(adau1977_reg_defaults),
};
EXPORT_SYMBOL_GPL(adau1977_regmap_config);

MODULE_DESCRIPTION("ASoC ADAU1977/ADAU1978/ADAU1979 driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
