// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017, Maxim Integrated

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "max98373.h"

static const u32 max98373_i2c_cache_reg[] = {
	MAX98373_R2054_MEAS_ADC_PVDD_CH_READBACK,
	MAX98373_R2055_MEAS_ADC_THERM_CH_READBACK,
	MAX98373_R20B6_BDE_CUR_STATE_READBACK,
};

static struct reg_default max98373_reg[] = {
	{MAX98373_R2000_SW_RESET, 0x00},
	{MAX98373_R2001_INT_RAW1, 0x00},
	{MAX98373_R2002_INT_RAW2, 0x00},
	{MAX98373_R2003_INT_RAW3, 0x00},
	{MAX98373_R2004_INT_STATE1, 0x00},
	{MAX98373_R2005_INT_STATE2, 0x00},
	{MAX98373_R2006_INT_STATE3, 0x00},
	{MAX98373_R2007_INT_FLAG1, 0x00},
	{MAX98373_R2008_INT_FLAG2, 0x00},
	{MAX98373_R2009_INT_FLAG3, 0x00},
	{MAX98373_R200A_INT_EN1, 0x00},
	{MAX98373_R200B_INT_EN2, 0x00},
	{MAX98373_R200C_INT_EN3, 0x00},
	{MAX98373_R200D_INT_FLAG_CLR1, 0x00},
	{MAX98373_R200E_INT_FLAG_CLR2, 0x00},
	{MAX98373_R200F_INT_FLAG_CLR3, 0x00},
	{MAX98373_R2010_IRQ_CTRL, 0x00},
	{MAX98373_R2014_THERM_WARN_THRESH, 0x10},
	{MAX98373_R2015_THERM_SHDN_THRESH, 0x27},
	{MAX98373_R2016_THERM_HYSTERESIS, 0x01},
	{MAX98373_R2017_THERM_FOLDBACK_SET, 0xC0},
	{MAX98373_R2018_THERM_FOLDBACK_EN, 0x00},
	{MAX98373_R201E_PIN_DRIVE_STRENGTH, 0x55},
	{MAX98373_R2020_PCM_TX_HIZ_EN_1, 0xFE},
	{MAX98373_R2021_PCM_TX_HIZ_EN_2, 0xFF},
	{MAX98373_R2022_PCM_TX_SRC_1, 0x00},
	{MAX98373_R2023_PCM_TX_SRC_2, 0x00},
	{MAX98373_R2024_PCM_DATA_FMT_CFG, 0xC0},
	{MAX98373_R2025_AUDIO_IF_MODE, 0x00},
	{MAX98373_R2026_PCM_CLOCK_RATIO, 0x04},
	{MAX98373_R2027_PCM_SR_SETUP_1, 0x08},
	{MAX98373_R2028_PCM_SR_SETUP_2, 0x88},
	{MAX98373_R2029_PCM_TO_SPK_MONO_MIX_1, 0x00},
	{MAX98373_R202A_PCM_TO_SPK_MONO_MIX_2, 0x00},
	{MAX98373_R202B_PCM_RX_EN, 0x00},
	{MAX98373_R202C_PCM_TX_EN, 0x00},
	{MAX98373_R202E_ICC_RX_CH_EN_1, 0x00},
	{MAX98373_R202F_ICC_RX_CH_EN_2, 0x00},
	{MAX98373_R2030_ICC_TX_HIZ_EN_1, 0xFF},
	{MAX98373_R2031_ICC_TX_HIZ_EN_2, 0xFF},
	{MAX98373_R2032_ICC_LINK_EN_CFG, 0x30},
	{MAX98373_R2034_ICC_TX_CNTL, 0x00},
	{MAX98373_R2035_ICC_TX_EN, 0x00},
	{MAX98373_R2036_SOUNDWIRE_CTRL, 0x05},
	{MAX98373_R203D_AMP_DIG_VOL_CTRL, 0x00},
	{MAX98373_R203E_AMP_PATH_GAIN, 0x08},
	{MAX98373_R203F_AMP_DSP_CFG, 0x02},
	{MAX98373_R2040_TONE_GEN_CFG, 0x00},
	{MAX98373_R2041_AMP_CFG, 0x03},
	{MAX98373_R2042_AMP_EDGE_RATE_CFG, 0x00},
	{MAX98373_R2043_AMP_EN, 0x00},
	{MAX98373_R2046_IV_SENSE_ADC_DSP_CFG, 0x04},
	{MAX98373_R2047_IV_SENSE_ADC_EN, 0x00},
	{MAX98373_R2051_MEAS_ADC_SAMPLING_RATE, 0x00},
	{MAX98373_R2052_MEAS_ADC_PVDD_FLT_CFG, 0x00},
	{MAX98373_R2053_MEAS_ADC_THERM_FLT_CFG, 0x00},
	{MAX98373_R2054_MEAS_ADC_PVDD_CH_READBACK, 0x00},
	{MAX98373_R2055_MEAS_ADC_THERM_CH_READBACK, 0x00},
	{MAX98373_R2056_MEAS_ADC_PVDD_CH_EN, 0x00},
	{MAX98373_R2090_BDE_LVL_HOLD, 0x00},
	{MAX98373_R2091_BDE_GAIN_ATK_REL_RATE, 0x00},
	{MAX98373_R2092_BDE_CLIPPER_MODE, 0x00},
	{MAX98373_R2097_BDE_L1_THRESH, 0x00},
	{MAX98373_R2098_BDE_L2_THRESH, 0x00},
	{MAX98373_R2099_BDE_L3_THRESH, 0x00},
	{MAX98373_R209A_BDE_L4_THRESH, 0x00},
	{MAX98373_R209B_BDE_THRESH_HYST, 0x00},
	{MAX98373_R20A8_BDE_L1_CFG_1, 0x00},
	{MAX98373_R20A9_BDE_L1_CFG_2, 0x00},
	{MAX98373_R20AA_BDE_L1_CFG_3, 0x00},
	{MAX98373_R20AB_BDE_L2_CFG_1, 0x00},
	{MAX98373_R20AC_BDE_L2_CFG_2, 0x00},
	{MAX98373_R20AD_BDE_L2_CFG_3, 0x00},
	{MAX98373_R20AE_BDE_L3_CFG_1, 0x00},
	{MAX98373_R20AF_BDE_L3_CFG_2, 0x00},
	{MAX98373_R20B0_BDE_L3_CFG_3, 0x00},
	{MAX98373_R20B1_BDE_L4_CFG_1, 0x00},
	{MAX98373_R20B2_BDE_L4_CFG_2, 0x00},
	{MAX98373_R20B3_BDE_L4_CFG_3, 0x00},
	{MAX98373_R20B4_BDE_INFINITE_HOLD_RELEASE, 0x00},
	{MAX98373_R20B5_BDE_EN, 0x00},
	{MAX98373_R20B6_BDE_CUR_STATE_READBACK, 0x00},
	{MAX98373_R20D1_DHT_CFG, 0x01},
	{MAX98373_R20D2_DHT_ATTACK_CFG, 0x02},
	{MAX98373_R20D3_DHT_RELEASE_CFG, 0x03},
	{MAX98373_R20D4_DHT_EN, 0x00},
	{MAX98373_R20E0_LIMITER_THRESH_CFG, 0x00},
	{MAX98373_R20E1_LIMITER_ATK_REL_RATES, 0x00},
	{MAX98373_R20E2_LIMITER_EN, 0x00},
	{MAX98373_R20FE_DEVICE_AUTO_RESTART_CFG, 0x00},
	{MAX98373_R20FF_GLOBAL_SHDN, 0x00},
	{MAX98373_R21FF_REV_ID, 0x42},
};

static int max98373_dai_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max98373_priv *max98373 = snd_soc_component_get_drvdata(component);
	unsigned int format = 0;
	unsigned int invert = 0;

	dev_dbg(component->dev, "%s: fmt 0x%08X\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		invert = MAX98373_PCM_MODE_CFG_PCM_BCLKEDGE;
		break;
	default:
		dev_err(component->dev, "DAI invert mode unsupported\n");
		return -EINVAL;
	}

	regmap_update_bits(max98373->regmap,
			   MAX98373_R2026_PCM_CLOCK_RATIO,
			   MAX98373_PCM_MODE_CFG_PCM_BCLKEDGE,
			   invert);

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = MAX98373_PCM_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = MAX98373_PCM_FORMAT_LJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		format = MAX98373_PCM_FORMAT_TDM_MODE1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		format = MAX98373_PCM_FORMAT_TDM_MODE0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(max98373->regmap,
			   MAX98373_R2024_PCM_DATA_FMT_CFG,
			   MAX98373_PCM_MODE_CFG_FORMAT_MASK,
			   format << MAX98373_PCM_MODE_CFG_FORMAT_SHIFT);

	return 0;
}

/* BCLKs per LRCLK */
static const int bclk_sel_table[] = {
	32, 48, 64, 96, 128, 192, 256, 384, 512, 320,
};

static int max98373_get_bclk_sel(int bclk)
{
	int i;
	/* match BCLKs per LRCLK */
	for (i = 0; i < ARRAY_SIZE(bclk_sel_table); i++) {
		if (bclk_sel_table[i] == bclk)
			return i + 2;
	}
	return 0;
}

static int max98373_set_clock(struct snd_soc_component *component,
			      struct snd_pcm_hw_params *params)
{
	struct max98373_priv *max98373 = snd_soc_component_get_drvdata(component);
	/* BCLK/LRCLK ratio calculation */
	int blr_clk_ratio = params_channels(params) * max98373->ch_size;
	int value;

	if (!max98373->tdm_mode) {
		/* BCLK configuration */
		value = max98373_get_bclk_sel(blr_clk_ratio);
		if (!value) {
			dev_err(component->dev, "format unsupported %d\n",
				params_format(params));
			return -EINVAL;
		}

		regmap_update_bits(max98373->regmap,
				   MAX98373_R2026_PCM_CLOCK_RATIO,
				   MAX98373_PCM_CLK_SETUP_BSEL_MASK,
				   value);
	}
	return 0;
}

static int max98373_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct max98373_priv *max98373 = snd_soc_component_get_drvdata(component);
	unsigned int sampling_rate = 0;
	unsigned int chan_sz = 0;

	/* pcm mode configuration */
	switch (snd_pcm_format_width(params_format(params))) {
	case 16:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "format unsupported %d\n",
			params_format(params));
		goto err;
	}

	max98373->ch_size = snd_pcm_format_width(params_format(params));

	regmap_update_bits(max98373->regmap,
			   MAX98373_R2024_PCM_DATA_FMT_CFG,
			   MAX98373_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);

	dev_dbg(component->dev, "format supported %d",
		params_format(params));

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 8000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_8000;
		break;
	case 11025:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_11025;
		break;
	case 12000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_12000;
		break;
	case 16000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_16000;
		break;
	case 22050:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_22050;
		break;
	case 24000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_24000;
		break;
	case 32000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_32000;
		break;
	case 44100:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_44100;
		break;
	case 48000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_48000;
		break;
	case 88200:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_88200;
		break;
	case 96000:
		sampling_rate = MAX98373_PCM_SR_SET1_SR_96000;
		break;
	default:
		dev_err(component->dev, "rate %d not supported\n",
			params_rate(params));
		goto err;
	}

	/* set DAI_SR to correct LRCLK frequency */
	regmap_update_bits(max98373->regmap,
			   MAX98373_R2027_PCM_SR_SETUP_1,
			   MAX98373_PCM_SR_SET1_SR_MASK,
			   sampling_rate);
	regmap_update_bits(max98373->regmap,
			   MAX98373_R2028_PCM_SR_SETUP_2,
			   MAX98373_PCM_SR_SET2_SR_MASK,
			   sampling_rate << MAX98373_PCM_SR_SET2_SR_SHIFT);

	/* set sampling rate of IV */
	if (max98373->interleave_mode &&
	    sampling_rate > MAX98373_PCM_SR_SET1_SR_16000)
		regmap_update_bits(max98373->regmap,
				   MAX98373_R2028_PCM_SR_SETUP_2,
				   MAX98373_PCM_SR_SET2_IVADC_SR_MASK,
				   sampling_rate - 3);
	else
		regmap_update_bits(max98373->regmap,
				   MAX98373_R2028_PCM_SR_SETUP_2,
				   MAX98373_PCM_SR_SET2_IVADC_SR_MASK,
				   sampling_rate);

	return max98373_set_clock(component, params);
err:
	return -EINVAL;
}

static int max98373_dai_tdm_slot(struct snd_soc_dai *dai,
				 unsigned int tx_mask, unsigned int rx_mask,
				 int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct max98373_priv *max98373 = snd_soc_component_get_drvdata(component);
	int bsel = 0;
	unsigned int chan_sz = 0;
	unsigned int mask;
	int x, slot_found;

	if (!tx_mask && !rx_mask && !slots && !slot_width)
		max98373->tdm_mode = false;
	else
		max98373->tdm_mode = true;

	/* BCLK configuration */
	bsel = max98373_get_bclk_sel(slots * slot_width);
	if (bsel == 0) {
		dev_err(component->dev, "BCLK %d not supported\n",
			slots * slot_width);
		return -EINVAL;
	}

	regmap_update_bits(max98373->regmap,
			   MAX98373_R2026_PCM_CLOCK_RATIO,
			   MAX98373_PCM_CLK_SETUP_BSEL_MASK,
			   bsel);

	/* Channel size configuration */
	switch (slot_width) {
	case 16:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98373_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "format unsupported %d\n",
			slot_width);
		return -EINVAL;
	}

	regmap_update_bits(max98373->regmap,
			   MAX98373_R2024_PCM_DATA_FMT_CFG,
			   MAX98373_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);

	/* Rx slot configuration */
	slot_found = 0;
	mask = rx_mask;
	for (x = 0 ; x < 16 ; x++, mask >>= 1) {
		if (mask & 0x1) {
			if (slot_found == 0)
				regmap_update_bits(max98373->regmap,
						   MAX98373_R2029_PCM_TO_SPK_MONO_MIX_1,
						   MAX98373_PCM_TO_SPK_CH0_SRC_MASK, x);
			else
				regmap_write(max98373->regmap,
					     MAX98373_R202A_PCM_TO_SPK_MONO_MIX_2,
					     x);
			slot_found++;
			if (slot_found > 1)
				break;
		}
	}

	/* Tx slot Hi-Z configuration */
	regmap_write(max98373->regmap,
		     MAX98373_R2020_PCM_TX_HIZ_EN_1,
		     ~tx_mask & 0xFF);
	regmap_write(max98373->regmap,
		     MAX98373_R2021_PCM_TX_HIZ_EN_2,
		     (~tx_mask & 0xFF00) >> 8);

	return 0;
}

#define MAX98373_RATES SNDRV_PCM_RATE_8000_96000

#define MAX98373_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops max98373_dai_ops = {
	.set_fmt = max98373_dai_set_fmt,
	.hw_params = max98373_dai_hw_params,
	.set_tdm_slot = max98373_dai_tdm_slot,
};

static bool max98373_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98373_R2000_SW_RESET:
	case MAX98373_R2001_INT_RAW1 ... MAX98373_R200C_INT_EN3:
	case MAX98373_R2010_IRQ_CTRL:
	case MAX98373_R2014_THERM_WARN_THRESH
		... MAX98373_R2018_THERM_FOLDBACK_EN:
	case MAX98373_R201E_PIN_DRIVE_STRENGTH
		... MAX98373_R2036_SOUNDWIRE_CTRL:
	case MAX98373_R203D_AMP_DIG_VOL_CTRL ... MAX98373_R2043_AMP_EN:
	case MAX98373_R2046_IV_SENSE_ADC_DSP_CFG
		... MAX98373_R2047_IV_SENSE_ADC_EN:
	case MAX98373_R2051_MEAS_ADC_SAMPLING_RATE
		... MAX98373_R2056_MEAS_ADC_PVDD_CH_EN:
	case MAX98373_R2090_BDE_LVL_HOLD ... MAX98373_R2092_BDE_CLIPPER_MODE:
	case MAX98373_R2097_BDE_L1_THRESH
		... MAX98373_R209B_BDE_THRESH_HYST:
	case MAX98373_R20A8_BDE_L1_CFG_1 ... MAX98373_R20B3_BDE_L4_CFG_3:
	case MAX98373_R20B5_BDE_EN ... MAX98373_R20B6_BDE_CUR_STATE_READBACK:
	case MAX98373_R20D1_DHT_CFG ... MAX98373_R20D4_DHT_EN:
	case MAX98373_R20E0_LIMITER_THRESH_CFG ... MAX98373_R20E2_LIMITER_EN:
	case MAX98373_R20FE_DEVICE_AUTO_RESTART_CFG
		... MAX98373_R20FF_GLOBAL_SHDN:
	case MAX98373_R21FF_REV_ID:
		return true;
	default:
		return false;
	}
};

static bool max98373_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98373_R2000_SW_RESET ... MAX98373_R2009_INT_FLAG3:
	case MAX98373_R2054_MEAS_ADC_PVDD_CH_READBACK:
	case MAX98373_R2055_MEAS_ADC_THERM_CH_READBACK:
	case MAX98373_R20B6_BDE_CUR_STATE_READBACK:
	case MAX98373_R20FF_GLOBAL_SHDN:
	case MAX98373_R21FF_REV_ID:
		return true;
	default:
		return false;
	}
}

static struct snd_soc_dai_driver max98373_dai[] = {
	{
		.name = "max98373-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98373_RATES,
			.formats = MAX98373_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98373_RATES,
			.formats = MAX98373_FORMATS,
		},
		.ops = &max98373_dai_ops,
	}
};

#ifdef CONFIG_PM_SLEEP
static int max98373_suspend(struct device *dev)
{
	struct max98373_priv *max98373 = dev_get_drvdata(dev);
	int i;

	/* cache feedback register values before suspend */
	for (i = 0; i < max98373->cache_num; i++)
		regmap_read(max98373->regmap, max98373->cache[i].reg, &max98373->cache[i].val);

	regcache_cache_only(max98373->regmap, true);
	regcache_mark_dirty(max98373->regmap);
	return 0;
}

static int max98373_resume(struct device *dev)
{
	struct max98373_priv *max98373 = dev_get_drvdata(dev);

	regcache_cache_only(max98373->regmap, false);
	max98373_reset(max98373, dev);
	regcache_sync(max98373->regmap);
	return 0;
}
#endif

static const struct dev_pm_ops max98373_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(max98373_suspend, max98373_resume)
};

static const struct regmap_config max98373_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = MAX98373_R21FF_REV_ID,
	.reg_defaults  = max98373_reg,
	.num_reg_defaults = ARRAY_SIZE(max98373_reg),
	.readable_reg = max98373_readable_register,
	.volatile_reg = max98373_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static int max98373_i2c_probe(struct i2c_client *i2c)
{
	int ret = 0;
	int reg = 0;
	int i;
	struct max98373_priv *max98373 = NULL;

	max98373 = devm_kzalloc(&i2c->dev, sizeof(*max98373), GFP_KERNEL);

	if (!max98373) {
		ret = -ENOMEM;
		return ret;
	}
	i2c_set_clientdata(i2c, max98373);

	/* update interleave mode info */
	if (device_property_read_bool(&i2c->dev, "maxim,interleave_mode"))
		max98373->interleave_mode = true;
	else
		max98373->interleave_mode = false;

	/* regmap initialization */
	max98373->regmap = devm_regmap_init_i2c(i2c, &max98373_regmap);
	if (IS_ERR(max98373->regmap)) {
		ret = PTR_ERR(max98373->regmap);
		dev_err(&i2c->dev,
			"Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	max98373->cache_num = ARRAY_SIZE(max98373_i2c_cache_reg);
	max98373->cache = devm_kcalloc(&i2c->dev, max98373->cache_num,
				       sizeof(*max98373->cache),
				       GFP_KERNEL);

	for (i = 0; i < max98373->cache_num; i++)
		max98373->cache[i].reg = max98373_i2c_cache_reg[i];

	/* voltage/current slot & gpio configuration */
	max98373_slot_config(&i2c->dev, max98373);

	/* Power on device */
	if (gpio_is_valid(max98373->reset_gpio)) {
		ret = devm_gpio_request(&i2c->dev, max98373->reset_gpio,
					"MAX98373_RESET");
		if (ret) {
			dev_err(&i2c->dev, "%s: Failed to request gpio %d\n",
				__func__, max98373->reset_gpio);
			return -EINVAL;
		}
		gpio_direction_output(max98373->reset_gpio, 0);
		msleep(50);
		gpio_direction_output(max98373->reset_gpio, 1);
		msleep(20);
	}

	/* Check Revision ID */
	ret = regmap_read(max98373->regmap,
			  MAX98373_R21FF_REV_ID, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev,
			"Failed to read: 0x%02X\n", MAX98373_R21FF_REV_ID);
		return ret;
	}
	dev_info(&i2c->dev, "MAX98373 revisionID: 0x%02X\n", reg);

	/* codec registration */
	ret = devm_snd_soc_register_component(&i2c->dev, &soc_codec_dev_max98373,
					      max98373_dai, ARRAY_SIZE(max98373_dai));
	if (ret < 0)
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);

	return ret;
}

static const struct i2c_device_id max98373_i2c_id[] = {
	{ "max98373", 0},
	{ },
};

MODULE_DEVICE_TABLE(i2c, max98373_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id max98373_of_match[] = {
	{ .compatible = "maxim,max98373", },
	{ }
};
MODULE_DEVICE_TABLE(of, max98373_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id max98373_acpi_match[] = {
	{ "MX98373", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, max98373_acpi_match);
#endif

static struct i2c_driver max98373_i2c_driver = {
	.driver = {
		.name = "max98373",
		.of_match_table = of_match_ptr(max98373_of_match),
		.acpi_match_table = ACPI_PTR(max98373_acpi_match),
		.pm = &max98373_pm,
	},
	.probe_new = max98373_i2c_probe,
	.id_table = max98373_i2c_id,
};

module_i2c_driver(max98373_i2c_driver)

MODULE_DESCRIPTION("ALSA SoC MAX98373 driver");
MODULE_AUTHOR("Ryan Lee <ryans.lee@maximintegrated.com>");
MODULE_LICENSE("GPL");
