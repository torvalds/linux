// SPDX-License-Identifier: GPL-2.0
// Driver for the Texas Instruments TAS2780 Mono
//		Audio amplifier
// Copyright (C) 2022 Texas Instruments Inc.

#include <linux/module.h>
#include <linux/err.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include "tas2780.h"

struct tas2780_priv {
	struct snd_soc_component *component;
	struct gpio_desc *reset_gpio;
	struct regmap *regmap;
	struct device *dev;
	int v_sense_slot;
	int i_sense_slot;
};

static void tas2780_reset(struct tas2780_priv *tas2780)
{
	int ret = 0;

	if (tas2780->reset_gpio) {
		gpiod_set_value_cansleep(tas2780->reset_gpio, 0);
		usleep_range(2000, 2050);
		gpiod_set_value_cansleep(tas2780->reset_gpio, 1);
		usleep_range(2000, 2050);
	}

	ret = snd_soc_component_write(tas2780->component, TAS2780_SW_RST,
				TAS2780_RST);
	if (ret)
		dev_err(tas2780->dev, "%s:errCode:0x%x Reset error!\n",
			__func__, ret);
}

#ifdef CONFIG_PM
static int tas2780_codec_suspend(struct snd_soc_component *component)
{
	struct tas2780_priv *tas2780 =
		snd_soc_component_get_drvdata(component);
	int ret = 0;

	ret = snd_soc_component_update_bits(component, TAS2780_PWR_CTRL,
		TAS2780_PWR_CTRL_MASK, TAS2780_PWR_CTRL_SHUTDOWN);
	if (ret < 0) {
		dev_err(tas2780->dev, "%s:errCode:0x%0x:power down error\n",
			__func__, ret);
		goto err;
	}
	ret = 0;
	regcache_cache_only(tas2780->regmap, true);
	regcache_mark_dirty(tas2780->regmap);
err:
	return ret;
}

static int tas2780_codec_resume(struct snd_soc_component *component)
{
	struct tas2780_priv *tas2780 =
		snd_soc_component_get_drvdata(component);
	int ret;

	ret = snd_soc_component_update_bits(component, TAS2780_PWR_CTRL,
		TAS2780_PWR_CTRL_MASK, TAS2780_PWR_CTRL_ACTIVE);

	if (ret < 0) {
		dev_err(tas2780->dev, "%s:errCode:0x%0x:power down error\n",
			__func__, ret);
		goto err;
	}
	regcache_cache_only(tas2780->regmap, false);
	ret = regcache_sync(tas2780->regmap);
err:
	return ret;
}
#endif

static const char * const tas2780_ASI1_src[] = {
	"I2C offset", "Left", "Right", "LeftRightDiv2",
};

static SOC_ENUM_SINGLE_DECL(
	tas2780_ASI1_src_enum, TAS2780_TDM_CFG2, 4, tas2780_ASI1_src);

static const struct snd_kcontrol_new tas2780_asi1_mux =
	SOC_DAPM_ENUM("ASI1 Source", tas2780_ASI1_src_enum);

static const struct snd_kcontrol_new isense_switch =
	SOC_DAPM_SINGLE("Switch", TAS2780_PWR_CTRL,
			TAS2780_ISENSE_POWER_EN, 1, 1);
static const struct snd_kcontrol_new vsense_switch =
	SOC_DAPM_SINGLE("Switch", TAS2780_PWR_CTRL,
			TAS2780_VSENSE_POWER_EN, 1, 1);

static const struct snd_soc_dapm_widget tas2780_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("ASI1 Sel", SND_SOC_NOPM, 0, 0, &tas2780_asi1_mux),
	SND_SOC_DAPM_SWITCH("ISENSE", TAS2780_PWR_CTRL,
		TAS2780_ISENSE_POWER_EN, 1, &isense_switch),
	SND_SOC_DAPM_SWITCH("VSENSE", TAS2780_PWR_CTRL,
		TAS2780_VSENSE_POWER_EN, 1, &vsense_switch),
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_SIGGEN("VMON"),
	SND_SOC_DAPM_SIGGEN("IMON")
};

static const struct snd_soc_dapm_route tas2780_audio_map[] = {
	{"ASI1 Sel", "I2C offset", "ASI1"},
	{"ASI1 Sel", "Left", "ASI1"},
	{"ASI1 Sel", "Right", "ASI1"},
	{"ASI1 Sel", "LeftRightDiv2", "ASI1"},
	{"OUT", NULL, "ASI1 Sel"},
	{"ISENSE", "Switch", "IMON"},
	{"VSENSE", "Switch", "VMON"},
};

static int tas2780_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas2780_priv *tas2780 =
		snd_soc_component_get_drvdata(component);
	int ret = 0;

	ret = snd_soc_component_update_bits(component, TAS2780_PWR_CTRL,
		TAS2780_PWR_CTRL_MASK,
		mute ? TAS2780_PWR_CTRL_MUTE : 0);
	if (ret < 0) {
		dev_err(tas2780->dev, "%s: Failed to set powercontrol\n",
			__func__);
		goto err;
	}
	ret = 0;
err:
	return ret;
}

static int tas2780_set_bitwidth(struct tas2780_priv *tas2780, int bitwidth)
{
	struct snd_soc_component *component = tas2780->component;
	int sense_en;
	int val;
	int ret;
	int slot_size;

	switch (bitwidth) {
	case SNDRV_PCM_FORMAT_S16_LE:
		ret = snd_soc_component_update_bits(component,
			TAS2780_TDM_CFG2,
			TAS2780_TDM_CFG2_RXW_MASK,
			TAS2780_TDM_CFG2_RXW_16BITS);
		slot_size = TAS2780_TDM_CFG2_RXS_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ret = snd_soc_component_update_bits(component,
			TAS2780_TDM_CFG2,
			TAS2780_TDM_CFG2_RXW_MASK,
			TAS2780_TDM_CFG2_RXW_24BITS);
		slot_size = TAS2780_TDM_CFG2_RXS_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		ret = snd_soc_component_update_bits(component,
			TAS2780_TDM_CFG2,
			TAS2780_TDM_CFG2_RXW_MASK,
			TAS2780_TDM_CFG2_RXW_32BITS);
		slot_size = TAS2780_TDM_CFG2_RXS_32BITS;
		break;

	default:
		ret = -EINVAL;
	}

	if (ret < 0) {
		dev_err(tas2780->dev, "%s:errCode:0x%x set bitwidth error\n",
			__func__, ret);
		goto err;
	}

	ret = snd_soc_component_update_bits(component, TAS2780_TDM_CFG2,
		TAS2780_TDM_CFG2_RXS_MASK, slot_size);
	if (ret < 0) {
		dev_err(tas2780->dev,
			"%s:errCode:0x%x set RX slot size error\n",
			__func__, ret);
		goto err;
	}

	val = snd_soc_component_read(tas2780->component, TAS2780_PWR_CTRL);
	if (val < 0) {
		dev_err(tas2780->dev, "%s:errCode:0x%x read PWR_CTRL error\n",
			__func__, val);
		ret = val;
		goto err;
	}

	if (val & (1 << TAS2780_VSENSE_POWER_EN))
		sense_en = 0;
	else
		sense_en = TAS2780_TDM_CFG5_VSNS_ENABLE;

	ret = snd_soc_component_update_bits(tas2780->component,
		TAS2780_TDM_CFG5, TAS2780_TDM_CFG5_VSNS_ENABLE, sense_en);
	if (ret < 0) {
		dev_err(tas2780->dev, "%s:errCode:0x%x enable vSNS error\n",
			__func__, ret);
		goto err;
	}

	if (val & (1 << TAS2780_ISENSE_POWER_EN))
		sense_en = 0;
	else
		sense_en = TAS2780_TDM_CFG6_ISNS_ENABLE;

	ret = snd_soc_component_update_bits(tas2780->component,
		TAS2780_TDM_CFG6, TAS2780_TDM_CFG6_ISNS_ENABLE, sense_en);
	if (ret < 0) {
		dev_err(tas2780->dev, "%s:errCode:0x%x enable iSNS error\n",
			__func__, ret);
		goto err;
	}
	ret = 0;
err:
	return ret;
}

static int tas2780_set_samplerate(
	struct tas2780_priv *tas2780, int samplerate)
{
	struct snd_soc_component *component = tas2780->component;
	int ramp_rate_val;
	int ret;

	switch (samplerate) {
	case 48000:
		ramp_rate_val = TAS2780_TDM_CFG0_SMP_48KHZ |
				TAS2780_TDM_CFG0_44_1_48KHZ;
		break;
	case 44100:
		ramp_rate_val = TAS2780_TDM_CFG0_SMP_44_1KHZ |
				TAS2780_TDM_CFG0_44_1_48KHZ;
		break;
	case 96000:
		ramp_rate_val = TAS2780_TDM_CFG0_SMP_48KHZ |
				TAS2780_TDM_CFG0_88_2_96KHZ;
		break;
	case 88200:
		ramp_rate_val = TAS2780_TDM_CFG0_SMP_44_1KHZ |
				TAS2780_TDM_CFG0_88_2_96KHZ;
		break;
	default:
		return -EINVAL;
	}
	ret = snd_soc_component_update_bits(component, TAS2780_TDM_CFG0,
		TAS2780_TDM_CFG0_SMP_MASK | TAS2780_TDM_CFG0_MASK,
		ramp_rate_val);
	if (ret < 0) {
		dev_err(tas2780->dev,
			"%s:errCode:0x%x Failed to set ramp_rate_val\n",
			__func__, ret);
		goto err;
	}
	ret = 0;
err:
	return ret;
}

static int tas2780_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas2780_priv *tas2780 =
		snd_soc_component_get_drvdata(component);
	int ret;

	ret = tas2780_set_bitwidth(tas2780, params_format(params));
	if (ret < 0)
		return ret;

	return tas2780_set_samplerate(tas2780, params_rate(params));
}

static int tas2780_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas2780_priv *tas2780 =
		snd_soc_component_get_drvdata(component);
	u8 tdm_rx_start_slot = 0, asi_cfg_1 = 0;
	int iface;
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		asi_cfg_1 = TAS2780_TDM_CFG1_RX_RISING;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		asi_cfg_1 = TAS2780_TDM_CFG1_RX_FALLING;
		break;
	default:
		dev_err(tas2780->dev, "ASI format Inverse is not found\n");
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2780_TDM_CFG1,
		TAS2780_TDM_CFG1_RX_MASK, asi_cfg_1);
	if (ret < 0) {
		dev_err(tas2780->dev,
			"%s:errCode:0x%x Failed to set asi_cfg_1\n",
			__func__, ret);
		goto err;
	}

	if (((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_I2S)
		|| ((fmt & SND_SOC_DAIFMT_FORMAT_MASK)
		== SND_SOC_DAIFMT_DSP_A)){
		iface = TAS2780_TDM_CFG2_SCFG_I2S;
		tdm_rx_start_slot = 1;
	} else {
		if (((fmt & SND_SOC_DAIFMT_FORMAT_MASK)
			== SND_SOC_DAIFMT_DSP_B)
			|| ((fmt & SND_SOC_DAIFMT_FORMAT_MASK)
			== SND_SOC_DAIFMT_LEFT_J)) {
			iface = TAS2780_TDM_CFG2_SCFG_LEFT_J;
			tdm_rx_start_slot = 0;
		} else {
			dev_err(tas2780->dev,
				"%s:DAI Format is not found, fmt=0x%x\n",
				__func__, fmt);
			ret = -EINVAL;
			goto err;
		}
	}
	ret = snd_soc_component_update_bits(component, TAS2780_TDM_CFG1,
		TAS2780_TDM_CFG1_MASK,
		(tdm_rx_start_slot << TAS2780_TDM_CFG1_51_SHIFT));
	if (ret < 0) {
		dev_err(tas2780->dev,
			"%s:errCode:0x%x Failed to set tdm_rx_start_slot\n",
			__func__, ret);
		goto err;
	}

	ret = snd_soc_component_update_bits(component, TAS2780_TDM_CFG2,
		TAS2780_TDM_CFG2_SCFG_MASK, iface);
	if (ret < 0) {
		dev_err(tas2780->dev, "%s:errCode:0x%x Failed to set iface\n",
			__func__, ret);
		goto err;
	}
	ret = 0;
err:
	return ret;
}

static int tas2780_set_dai_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask,
				unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct tas2780_priv *tas2780 =
		snd_soc_component_get_drvdata(component);
	int left_slot, right_slot;
	int slots_cfg;
	int slot_size;
	int ret = 0;

	if (tx_mask == 0 || rx_mask != 0)
		return -EINVAL;

	left_slot = __ffs(tx_mask);
	tx_mask &= ~(1 << left_slot);
	if (tx_mask == 0) {
		right_slot = left_slot;
	} else {
		right_slot = __ffs(tx_mask);
		tx_mask &= ~(1 << right_slot);
	}

	if (tx_mask != 0 || left_slot >= slots || right_slot >= slots)
		return -EINVAL;

	slots_cfg = (right_slot << TAS2780_TDM_CFG3_RXS_SHIFT) | left_slot;
	ret = snd_soc_component_write(component, TAS2780_TDM_CFG3, slots_cfg);
	if (ret) {
		dev_err(tas2780->dev,
			"%s:errCode:0x%x Failed to set slots_cfg\n",
			__func__, ret);
		goto err;
	}

	switch (slot_width) {
	case 16:
		slot_size = TAS2780_TDM_CFG2_RXS_16BITS;
		break;
	case 24:
		slot_size = TAS2780_TDM_CFG2_RXS_24BITS;
		break;
	case 32:
		slot_size = TAS2780_TDM_CFG2_RXS_32BITS;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	ret = snd_soc_component_update_bits(component, TAS2780_TDM_CFG2,
		TAS2780_TDM_CFG2_RXS_MASK, slot_size);
	if (ret < 0) {
		dev_err(tas2780->dev,
			"%s:errCode:0x%x Failed to set slot_size\n",
			__func__, ret);
		goto err;
	}

	ret = snd_soc_component_update_bits(component, TAS2780_TDM_CFG5,
		TAS2780_TDM_CFG5_50_MASK, tas2780->v_sense_slot);
	if (ret < 0) {
		dev_err(tas2780->dev,
			"%s:errCode:0x%x Failed to set v_sense_slot\n",
			__func__, ret);
		goto err;
	}

	ret = snd_soc_component_update_bits(component, TAS2780_TDM_CFG6,
		TAS2780_TDM_CFG6_50_MASK, tas2780->i_sense_slot);
	if (ret < 0) {
		dev_err(tas2780->dev,
			"%s:errCode:0x%x Failed to set i_sense_slot\n",
			__func__, ret);
		goto err;
	}
	ret = 0;
err:
	return ret;
}

static const struct snd_soc_dai_ops tas2780_dai_ops = {
	.mute_stream = tas2780_mute,
	.hw_params  = tas2780_hw_params,
	.set_fmt    = tas2780_set_fmt,
	.set_tdm_slot = tas2780_set_dai_tdm_slot,
	.no_capture_mute = 1,
};

#define TAS2780_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

#define TAS2780_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
		       SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_88200)

static struct snd_soc_dai_driver tas2780_dai_driver[] = {
	{
		.name = "tas2780 ASI1",
		.id = 0,
		.playback = {
			.stream_name    = "ASI1 Playback",
			.channels_min   = 2,
			.channels_max   = 2,
			.rates      = TAS2780_RATES,
			.formats    = TAS2780_FORMATS,
		},
		.capture = {
			.stream_name    = "ASI1 Capture",
			.channels_min   = 1,
			.channels_max   = 2,
			.rates = TAS2780_RATES,
			.formats = TAS2780_FORMATS,
		},
		.ops = &tas2780_dai_ops,
		.symmetric_rate = 1,
	},
};

static int tas2780_codec_probe(struct snd_soc_component *component)
{
	struct tas2780_priv *tas2780 =
		snd_soc_component_get_drvdata(component);
	int ret = 0;

	tas2780->component = component;

	tas2780_reset(tas2780);
	ret = snd_soc_component_update_bits(component,
			TAS2780_IC_CFG, TAS2780_IC_CFG_MASK,
			TAS2780_IC_CFG_ENABLE);
	if (ret < 0)
		dev_err(tas2780->dev, "%s:errCode:0x%0x\n",
			__func__, ret);

	return ret;
}

static DECLARE_TLV_DB_SCALE(tas2780_digital_tlv, 1100, 50, 0);
static DECLARE_TLV_DB_SCALE(tas2780_playback_volume, -10000, 50, 0);

static const struct snd_kcontrol_new tas2780_snd_controls[] = {
	SOC_SINGLE_TLV("Speaker Volume", TAS2780_DVC, 0,
		       TAS2780_DVC_MAX, 1, tas2780_playback_volume),
	SOC_SINGLE_TLV("Amp Gain Volume", TAS2780_CHNL_0, 0, 0x14, 0,
		       tas2780_digital_tlv),
};

static const struct snd_soc_component_driver soc_component_driver_tas2780 = {
	.probe			= tas2780_codec_probe,
#ifdef CONFIG_PM
	.suspend		= tas2780_codec_suspend,
	.resume			= tas2780_codec_resume,
#endif
	.controls		= tas2780_snd_controls,
	.num_controls		= ARRAY_SIZE(tas2780_snd_controls),
	.dapm_widgets		= tas2780_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas2780_dapm_widgets),
	.dapm_routes		= tas2780_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas2780_audio_map),
	.idle_bias_on		= 1,
	.endianness		= 1,
};

static const struct reg_default tas2780_reg_defaults[] = {
	{ TAS2780_PAGE, 0x00 },
	{ TAS2780_SW_RST, 0x00 },
	{ TAS2780_PWR_CTRL, 0x1a },
	{ TAS2780_DVC, 0x00 },
	{ TAS2780_CHNL_0, 0x00 },
	{ TAS2780_TDM_CFG0, 0x09 },
	{ TAS2780_TDM_CFG1, 0x02 },
	{ TAS2780_TDM_CFG2, 0x0a },
	{ TAS2780_TDM_CFG3, 0x10 },
	{ TAS2780_TDM_CFG5, 0x42 },
};

static const struct regmap_range_cfg tas2780_regmap_ranges[] = {
	{
		.range_min = 0,
		.range_max = 1 * 128,
		.selector_reg = TAS2780_PAGE,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 128,
	},
};

static const struct regmap_config tas2780_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_defaults = tas2780_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tas2780_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.ranges = tas2780_regmap_ranges,
	.num_ranges = ARRAY_SIZE(tas2780_regmap_ranges),
	.max_register = 1 * 128,
};

static int tas2780_parse_dt(struct device *dev, struct tas2780_priv *tas2780)
{
	int ret = 0;

	tas2780->reset_gpio = devm_gpiod_get_optional(tas2780->dev, "reset",
		GPIOD_OUT_HIGH);
	if (IS_ERR(tas2780->reset_gpio)) {
		if (PTR_ERR(tas2780->reset_gpio) == -EPROBE_DEFER) {
			tas2780->reset_gpio = NULL;
			return -EPROBE_DEFER;
		}
	}

	ret = fwnode_property_read_u32(dev->fwnode, "ti,imon-slot-no",
		&tas2780->i_sense_slot);
	if (ret)
		tas2780->i_sense_slot = 0;

	ret = fwnode_property_read_u32(dev->fwnode, "ti,vmon-slot-no",
		&tas2780->v_sense_slot);
	if (ret)
		tas2780->v_sense_slot = 2;

	return 0;
}

static int tas2780_i2c_probe(struct i2c_client *client)
{
	struct tas2780_priv *tas2780;
	int result;

	tas2780 = devm_kzalloc(&client->dev, sizeof(struct tas2780_priv),
		GFP_KERNEL);
	if (!tas2780)
		return -ENOMEM;
	tas2780->dev = &client->dev;
	i2c_set_clientdata(client, tas2780);
	dev_set_drvdata(&client->dev, tas2780);

	tas2780->regmap = devm_regmap_init_i2c(client, &tas2780_i2c_regmap);
	if (IS_ERR(tas2780->regmap)) {
		result = PTR_ERR(tas2780->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			result);
		return result;
	}

	if (client->dev.of_node) {
		result = tas2780_parse_dt(&client->dev, tas2780);
		if (result) {
			dev_err(tas2780->dev,
				"%s: Failed to parse devicetree\n", __func__);
			return result;
		}
	}

	return devm_snd_soc_register_component(tas2780->dev,
		&soc_component_driver_tas2780, tas2780_dai_driver,
		ARRAY_SIZE(tas2780_dai_driver));
}

static const struct i2c_device_id tas2780_i2c_id[] = {
	{ "tas2780"},
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2780_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas2780_of_match[] = {
	{ .compatible = "ti,tas2780" },
	{},
};
MODULE_DEVICE_TABLE(of, tas2780_of_match);
#endif

static struct i2c_driver tas2780_i2c_driver = {
	.driver = {
		.name   = "tas2780",
		.of_match_table = of_match_ptr(tas2780_of_match),
	},
	.probe      = tas2780_i2c_probe,
	.id_table   = tas2780_i2c_id,
};
module_i2c_driver(tas2780_i2c_driver);

MODULE_AUTHOR("Raphael Xu <raphael-xu@ti.com>");
MODULE_DESCRIPTION("TAS2780 I2C Smart Amplifier driver");
MODULE_LICENSE("GPL");
