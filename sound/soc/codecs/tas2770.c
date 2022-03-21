// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Texas Instruments TAS2770 20-W Digital Input Mono Class-D
// Audio Amplifier with Speaker I/V Sense
//
// Copyright (C) 2016-2017 Texas Instruments Incorporated - https://www.ti.com/
//	Author: Tracy Yi <tracy-yi@ti.com>
//	Frank Shi <shifu0704@thundersoft.com>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "tas2770.h"

#define TAS2770_MDELAY 0xFFFFFFFE

static void tas2770_reset(struct tas2770_priv *tas2770)
{
	if (tas2770->reset_gpio) {
		gpiod_set_value_cansleep(tas2770->reset_gpio, 0);
		msleep(20);
		gpiod_set_value_cansleep(tas2770->reset_gpio, 1);
	}

	snd_soc_component_write(tas2770->component, TAS2770_SW_RST,
		TAS2770_RST);
}

static int tas2770_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct tas2770_priv *tas2770 =
			snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
					      TAS2770_PWR_CTRL_MASK,
					      TAS2770_PWR_CTRL_ACTIVE);
		break;
	case SND_SOC_BIAS_STANDBY:
	case SND_SOC_BIAS_PREPARE:
		snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
					      TAS2770_PWR_CTRL_MASK,
					      TAS2770_PWR_CTRL_MUTE);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
					      TAS2770_PWR_CTRL_MASK,
					      TAS2770_PWR_CTRL_SHUTDOWN);
		break;

	default:
		dev_err(tas2770->dev, "wrong power level setting %d\n", level);
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_PM
static int tas2770_codec_suspend(struct snd_soc_component *component)
{
	struct tas2770_priv *tas2770 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	regcache_cache_only(tas2770->regmap, true);
	regcache_mark_dirty(tas2770->regmap);

	if (tas2770->sdz_gpio) {
		gpiod_set_value_cansleep(tas2770->sdz_gpio, 0);
	} else {
		ret = snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
						    TAS2770_PWR_CTRL_MASK,
						    TAS2770_PWR_CTRL_SHUTDOWN);
		if (ret < 0) {
			regcache_cache_only(tas2770->regmap, false);
			regcache_sync(tas2770->regmap);
			return ret;
		}

		ret = 0;
	}

	return ret;
}

static int tas2770_codec_resume(struct snd_soc_component *component)
{
	struct tas2770_priv *tas2770 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	if (tas2770->sdz_gpio) {
		gpiod_set_value_cansleep(tas2770->sdz_gpio, 1);
	} else {
		ret = snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
						    TAS2770_PWR_CTRL_MASK,
						    TAS2770_PWR_CTRL_ACTIVE);
		if (ret < 0)
			return ret;
	}

	regcache_cache_only(tas2770->regmap, false);

	return regcache_sync(tas2770->regmap);
}
#else
#define tas2770_codec_suspend NULL
#define tas2770_codec_resume NULL
#endif

static const char * const tas2770_ASI1_src[] = {
	"I2C offset", "Left", "Right", "LeftRightDiv2",
};

static SOC_ENUM_SINGLE_DECL(
	tas2770_ASI1_src_enum, TAS2770_TDM_CFG_REG2,
	4, tas2770_ASI1_src);

static const struct snd_kcontrol_new tas2770_asi1_mux =
	SOC_DAPM_ENUM("ASI1 Source", tas2770_ASI1_src_enum);

static int tas2770_dac_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
			snd_soc_dapm_to_component(w->dapm);
	struct tas2770_priv *tas2770 =
			snd_soc_component_get_drvdata(component);
	int ret;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		ret = snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
						    TAS2770_PWR_CTRL_MASK,
						    TAS2770_PWR_CTRL_MUTE);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		ret = snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
						    TAS2770_PWR_CTRL_MASK,
						    TAS2770_PWR_CTRL_SHUTDOWN);
		break;
	default:
		dev_err(tas2770->dev, "Not supported evevt\n");
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_kcontrol_new isense_switch =
	SOC_DAPM_SINGLE("Switch", TAS2770_PWR_CTRL, 3, 1, 1);
static const struct snd_kcontrol_new vsense_switch =
	SOC_DAPM_SINGLE("Switch", TAS2770_PWR_CTRL, 2, 1, 1);

static const struct snd_soc_dapm_widget tas2770_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("ASI1 Sel", SND_SOC_NOPM, 0, 0, &tas2770_asi1_mux),
	SND_SOC_DAPM_SWITCH("ISENSE", TAS2770_PWR_CTRL, 3, 1, &isense_switch),
	SND_SOC_DAPM_SWITCH("VSENSE", TAS2770_PWR_CTRL, 2, 1, &vsense_switch),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, tas2770_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_SIGGEN("VMON"),
	SND_SOC_DAPM_SIGGEN("IMON")
};

static const struct snd_soc_dapm_route tas2770_audio_map[] = {
	{"ASI1 Sel", "I2C offset", "ASI1"},
	{"ASI1 Sel", "Left", "ASI1"},
	{"ASI1 Sel", "Right", "ASI1"},
	{"ASI1 Sel", "LeftRightDiv2", "ASI1"},
	{"DAC", NULL, "ASI1 Sel"},
	{"OUT", NULL, "DAC"},
	{"ISENSE", "Switch", "IMON"},
	{"VSENSE", "Switch", "VMON"},
};

static int tas2770_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	if (mute)
		ret = snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
						    TAS2770_PWR_CTRL_MASK,
						    TAS2770_PWR_CTRL_MUTE);
	else
		ret = snd_soc_component_update_bits(component, TAS2770_PWR_CTRL,
						    TAS2770_PWR_CTRL_MASK,
						    TAS2770_PWR_CTRL_ACTIVE);

	if (ret < 0)
		return ret;

	return 0;
}

static int tas2770_set_bitwidth(struct tas2770_priv *tas2770, int bitwidth)
{
	int ret;
	struct snd_soc_component *component = tas2770->component;

	switch (bitwidth) {
	case SNDRV_PCM_FORMAT_S16_LE:
		ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG2,
						    TAS2770_TDM_CFG_REG2_RXW_MASK,
						    TAS2770_TDM_CFG_REG2_RXW_16BITS);
		tas2770->v_sense_slot = tas2770->i_sense_slot + 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG2,
						    TAS2770_TDM_CFG_REG2_RXW_MASK,
						    TAS2770_TDM_CFG_REG2_RXW_24BITS);
		tas2770->v_sense_slot = tas2770->i_sense_slot + 4;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG2,
						    TAS2770_TDM_CFG_REG2_RXW_MASK,
						    TAS2770_TDM_CFG_REG2_RXW_32BITS);
		tas2770->v_sense_slot = tas2770->i_sense_slot + 4;
		break;

	default:
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG5,
					    TAS2770_TDM_CFG_REG5_VSNS_MASK |
					    TAS2770_TDM_CFG_REG5_50_MASK,
					    TAS2770_TDM_CFG_REG5_VSNS_ENABLE |
		tas2770->v_sense_slot);
	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG6,
					    TAS2770_TDM_CFG_REG6_ISNS_MASK |
					    TAS2770_TDM_CFG_REG6_50_MASK,
					    TAS2770_TDM_CFG_REG6_ISNS_ENABLE |
					    tas2770->i_sense_slot);
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2770_set_samplerate(struct tas2770_priv *tas2770, int samplerate)
{
	struct snd_soc_component *component = tas2770->component;
	int ramp_rate_val;
	int ret;

	switch (samplerate) {
	case 48000:
		ramp_rate_val = TAS2770_TDM_CFG_REG0_SMP_48KHZ |
				TAS2770_TDM_CFG_REG0_31_44_1_48KHZ;
		break;
	case 44100:
		ramp_rate_val = TAS2770_TDM_CFG_REG0_SMP_44_1KHZ |
				TAS2770_TDM_CFG_REG0_31_44_1_48KHZ;
		break;
	case 96000:
		ramp_rate_val = TAS2770_TDM_CFG_REG0_SMP_48KHZ |
				TAS2770_TDM_CFG_REG0_31_88_2_96KHZ;
		break;
	case 88200:
		ramp_rate_val = TAS2770_TDM_CFG_REG0_SMP_44_1KHZ |
				TAS2770_TDM_CFG_REG0_31_88_2_96KHZ;
		break;
	case 192000:
		ramp_rate_val = TAS2770_TDM_CFG_REG0_SMP_48KHZ |
				TAS2770_TDM_CFG_REG0_31_176_4_192KHZ;
		break;
	case 176400:
		ramp_rate_val = TAS2770_TDM_CFG_REG0_SMP_44_1KHZ |
				TAS2770_TDM_CFG_REG0_31_176_4_192KHZ;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG0,
					    TAS2770_TDM_CFG_REG0_SMP_MASK |
					    TAS2770_TDM_CFG_REG0_31_MASK,
					    ramp_rate_val);
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2770_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas2770_priv *tas2770 =
			snd_soc_component_get_drvdata(component);
	int ret;

	ret = tas2770_set_bitwidth(tas2770, params_format(params));
	if (ret)
		return ret;

	return tas2770_set_samplerate(tas2770, params_rate(params));
}

static int tas2770_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas2770_priv *tas2770 =
			snd_soc_component_get_drvdata(component);
	u8 tdm_rx_start_slot = 0, asi_cfg_1 = 0;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		dev_err(tas2770->dev, "ASI format master is not found\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		asi_cfg_1 |= TAS2770_TDM_CFG_REG1_RX_RSING;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		asi_cfg_1 |= TAS2770_TDM_CFG_REG1_RX_FALING;
		break;
	default:
		dev_err(tas2770->dev, "ASI format Inverse is not found\n");
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG1,
					    TAS2770_TDM_CFG_REG1_RX_MASK,
					    asi_cfg_1);
	if (ret < 0)
		return ret;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		tdm_rx_start_slot = 1;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		tdm_rx_start_slot = 0;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		tdm_rx_start_slot = 1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		tdm_rx_start_slot = 0;
		break;
	default:
		dev_err(tas2770->dev,
			"DAI Format is not found, fmt=0x%x\n", fmt);
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG1,
					    TAS2770_TDM_CFG_REG1_MASK,
					    (tdm_rx_start_slot << TAS2770_TDM_CFG_REG1_51_SHIFT));
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2770_set_dai_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask,
				unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	int left_slot, right_slot;
	int ret;

	if (tx_mask == 0 || rx_mask != 0)
		return -EINVAL;

	if (slots == 1) {
		if (tx_mask != 1)
			return -EINVAL;

		left_slot = 0;
		right_slot = 0;
	} else {
		left_slot = __ffs(tx_mask);
		tx_mask &= ~(1 << left_slot);
		if (tx_mask == 0) {
			right_slot = left_slot;
		} else {
			right_slot = __ffs(tx_mask);
			tx_mask &= ~(1 << right_slot);
		}
	}

	if (tx_mask != 0 || left_slot >= slots || right_slot >= slots)
		return -EINVAL;

	ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG3,
					    TAS2770_TDM_CFG_REG3_30_MASK,
					    (left_slot << TAS2770_TDM_CFG_REG3_30_SHIFT));
	if (ret < 0)
		return ret;
	ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG3,
					    TAS2770_TDM_CFG_REG3_RXS_MASK,
					    (right_slot << TAS2770_TDM_CFG_REG3_RXS_SHIFT));
	if (ret < 0)
		return ret;

	switch (slot_width) {
	case 16:
		ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG2,
						    TAS2770_TDM_CFG_REG2_RXS_MASK,
						    TAS2770_TDM_CFG_REG2_RXS_16BITS);
		break;
	case 24:
		ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG2,
						    TAS2770_TDM_CFG_REG2_RXS_MASK,
						    TAS2770_TDM_CFG_REG2_RXS_24BITS);
		break;
	case 32:
		ret = snd_soc_component_update_bits(component, TAS2770_TDM_CFG_REG2,
						    TAS2770_TDM_CFG_REG2_RXS_MASK,
						    TAS2770_TDM_CFG_REG2_RXS_32BITS);
		break;
	case 0:
		/* Do not change slot width */
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_dai_ops tas2770_dai_ops = {
	.mute_stream = tas2770_mute,
	.hw_params  = tas2770_hw_params,
	.set_fmt    = tas2770_set_fmt,
	.set_tdm_slot = tas2770_set_dai_tdm_slot,
	.no_capture_mute = 1,
};

#define TAS2770_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

#define TAS2770_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
					   SNDRV_PCM_RATE_96000 |\
					    SNDRV_PCM_RATE_192000\
					  )

static struct snd_soc_dai_driver tas2770_dai_driver[] = {
	{
		.name = "tas2770 ASI1",
		.id = 0,
		.playback = {
			.stream_name    = "ASI1 Playback",
			.channels_min   = 2,
			.channels_max   = 2,
			.rates      = TAS2770_RATES,
			.formats    = TAS2770_FORMATS,
		},
		.capture = {
			.stream_name    = "ASI1 Capture",
			.channels_min   = 0,
			.channels_max   = 2,
			.rates          = TAS2770_RATES,
			.formats    = TAS2770_FORMATS,
		},
		.ops = &tas2770_dai_ops,
		.symmetric_rates = 1,
	},
};

static int tas2770_codec_probe(struct snd_soc_component *component)
{
	struct tas2770_priv *tas2770 =
			snd_soc_component_get_drvdata(component);

	tas2770->component = component;

	if (tas2770->sdz_gpio)
		gpiod_set_value_cansleep(tas2770->sdz_gpio, 1);

	tas2770_reset(tas2770);

	return 0;
}

static DECLARE_TLV_DB_SCALE(tas2770_digital_tlv, 1100, 50, 0);
static DECLARE_TLV_DB_SCALE(tas2770_playback_volume, -12750, 50, 0);

static const struct snd_kcontrol_new tas2770_snd_controls[] = {
	SOC_SINGLE_TLV("Speaker Playback Volume", TAS2770_PLAY_CFG_REG2,
		       0, TAS2770_PLAY_CFG_REG2_VMAX, 1, tas2770_playback_volume),
	SOC_SINGLE_TLV("Amp Gain Volume", TAS2770_PLAY_CFG_REG0, 0, 0x14, 0,
		       tas2770_digital_tlv),
};

static const struct snd_soc_component_driver soc_component_driver_tas2770 = {
	.probe			= tas2770_codec_probe,
	.suspend		= tas2770_codec_suspend,
	.resume			= tas2770_codec_resume,
	.set_bias_level = tas2770_set_bias_level,
	.controls		= tas2770_snd_controls,
	.num_controls		= ARRAY_SIZE(tas2770_snd_controls),
	.dapm_widgets		= tas2770_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas2770_dapm_widgets),
	.dapm_routes		= tas2770_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas2770_audio_map),
	.idle_bias_on		= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int tas2770_register_codec(struct tas2770_priv *tas2770)
{
	return devm_snd_soc_register_component(tas2770->dev,
		&soc_component_driver_tas2770,
		tas2770_dai_driver, ARRAY_SIZE(tas2770_dai_driver));
}

static const struct reg_default tas2770_reg_defaults[] = {
	{ TAS2770_PAGE, 0x00 },
	{ TAS2770_SW_RST, 0x00 },
	{ TAS2770_PWR_CTRL, 0x0e },
	{ TAS2770_PLAY_CFG_REG0, 0x10 },
	{ TAS2770_PLAY_CFG_REG1, 0x01 },
	{ TAS2770_PLAY_CFG_REG2, 0x00 },
	{ TAS2770_MSC_CFG_REG0, 0x07 },
	{ TAS2770_TDM_CFG_REG1, 0x02 },
	{ TAS2770_TDM_CFG_REG2, 0x0a },
	{ TAS2770_TDM_CFG_REG3, 0x10 },
	{ TAS2770_INT_MASK_REG0, 0xfc },
	{ TAS2770_INT_MASK_REG1, 0xb1 },
	{ TAS2770_INT_CFG, 0x05 },
	{ TAS2770_MISC_IRQ, 0x81 },
	{ TAS2770_CLK_CGF, 0x0c },

};

static bool tas2770_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TAS2770_PAGE: /* regmap implementation requires this */
	case TAS2770_SW_RST: /* always clears after write */
	case TAS2770_BO_PRV_REG0:/* has a self clearing bit */
	case TAS2770_LVE_INT_REG0:
	case TAS2770_LVE_INT_REG1:
	case TAS2770_LAT_INT_REG0:/* Sticky interrupt flags */
	case TAS2770_LAT_INT_REG1:/* Sticky interrupt flags */
	case TAS2770_VBAT_MSB:
	case TAS2770_VBAT_LSB:
	case TAS2770_TEMP_MSB:
	case TAS2770_TEMP_LSB:
		return true;
	}

	return false;
}

static bool tas2770_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TAS2770_LVE_INT_REG0:
	case TAS2770_LVE_INT_REG1:
	case TAS2770_LAT_INT_REG0:
	case TAS2770_LAT_INT_REG1:
	case TAS2770_VBAT_MSB:
	case TAS2770_VBAT_LSB:
	case TAS2770_TEMP_MSB:
	case TAS2770_TEMP_LSB:
	case TAS2770_TDM_CLK_DETC:
	case TAS2770_REV_AND_GPID:
		return false;
	}

	return true;
}

static const struct regmap_range_cfg tas2770_regmap_ranges[] = {
	{
		.range_min = 0,
		.range_max = 1 * 128,
		.selector_reg = TAS2770_PAGE,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 128,
	},
};

static const struct regmap_config tas2770_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = tas2770_writeable,
	.volatile_reg = tas2770_volatile,
	.reg_defaults = tas2770_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tas2770_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.ranges = tas2770_regmap_ranges,
	.num_ranges = ARRAY_SIZE(tas2770_regmap_ranges),
	.max_register = 1 * 128,
};

static int tas2770_parse_dt(struct device *dev, struct tas2770_priv *tas2770)
{
	int rc = 0;

	rc = fwnode_property_read_u32(dev->fwnode, "ti,imon-slot-no",
				      &tas2770->i_sense_slot);
	if (rc) {
		dev_info(tas2770->dev, "Property %s is missing setting default slot\n",
			 "ti,imon-slot-no");

		tas2770->i_sense_slot = 0;
	}

	rc = fwnode_property_read_u32(dev->fwnode, "ti,vmon-slot-no",
				      &tas2770->v_sense_slot);
	if (rc) {
		dev_info(tas2770->dev, "Property %s is missing setting default slot\n",
			 "ti,vmon-slot-no");

		tas2770->v_sense_slot = 2;
	}

	tas2770->sdz_gpio = devm_gpiod_get_optional(dev, "shutdown", GPIOD_OUT_HIGH);
	if (IS_ERR(tas2770->sdz_gpio)) {
		if (PTR_ERR(tas2770->sdz_gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		tas2770->sdz_gpio = NULL;
	}

	return 0;
}

static int tas2770_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tas2770_priv *tas2770;
	int result;

	tas2770 = devm_kzalloc(&client->dev, sizeof(struct tas2770_priv),
			       GFP_KERNEL);
	if (!tas2770)
		return -ENOMEM;

	tas2770->dev = &client->dev;
	i2c_set_clientdata(client, tas2770);
	dev_set_drvdata(&client->dev, tas2770);

	tas2770->regmap = devm_regmap_init_i2c(client, &tas2770_i2c_regmap);
	if (IS_ERR(tas2770->regmap)) {
		result = PTR_ERR(tas2770->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			result);
		return result;
	}

	if (client->dev.of_node) {
		result = tas2770_parse_dt(&client->dev, tas2770);
		if (result) {
			dev_err(tas2770->dev, "%s: Failed to parse devicetree\n",
				__func__);
			return result;
		}
	}

	tas2770->reset_gpio = devm_gpiod_get_optional(tas2770->dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(tas2770->reset_gpio)) {
		if (PTR_ERR(tas2770->reset_gpio) == -EPROBE_DEFER) {
			tas2770->reset_gpio = NULL;
			return -EPROBE_DEFER;
		}
	}

	result = tas2770_register_codec(tas2770);
	if (result)
		dev_err(tas2770->dev, "Register codec failed.\n");

	return result;
}

static const struct i2c_device_id tas2770_i2c_id[] = {
	{ "tas2770", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2770_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas2770_of_match[] = {
	{ .compatible = "ti,tas2770" },
	{},
};
MODULE_DEVICE_TABLE(of, tas2770_of_match);
#endif

static struct i2c_driver tas2770_i2c_driver = {
	.driver = {
		.name   = "tas2770",
		.of_match_table = of_match_ptr(tas2770_of_match),
	},
	.probe      = tas2770_i2c_probe,
	.id_table   = tas2770_i2c_id,
};
module_i2c_driver(tas2770_i2c_driver);

MODULE_AUTHOR("Shi Fu <shifu0704@thundersoft.com>");
MODULE_DESCRIPTION("TAS2770 I2C Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
