// SPDX-License-Identifier: GPL-2.0
//
// Driver for the Texas Instruments TAS2764 CODEC
// Copyright (C) 2020 Texas Instruments Inc.

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
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "tas2764.h"

struct tas2764_priv {
	struct snd_soc_component *component;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *sdz_gpio;
	struct regmap *regmap;
	struct device *dev;
	
	int v_sense_slot;
	int i_sense_slot;
};

static void tas2764_reset(struct tas2764_priv *tas2764)
{
	if (tas2764->reset_gpio) {
		gpiod_set_value_cansleep(tas2764->reset_gpio, 0);
		msleep(20);
		gpiod_set_value_cansleep(tas2764->reset_gpio, 1);
		usleep_range(1000, 2000);
	}

	snd_soc_component_write(tas2764->component, TAS2764_SW_RST,
				TAS2764_RST);
	usleep_range(1000, 2000);
}

static int tas2764_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct tas2764_priv *tas2764 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
					      TAS2764_PWR_CTRL_MASK,
					      TAS2764_PWR_CTRL_ACTIVE);
		break;
	case SND_SOC_BIAS_STANDBY:
	case SND_SOC_BIAS_PREPARE:
		snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
					      TAS2764_PWR_CTRL_MASK,
					      TAS2764_PWR_CTRL_MUTE);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
					      TAS2764_PWR_CTRL_MASK,
					      TAS2764_PWR_CTRL_SHUTDOWN);
		break;

	default:
		dev_err(tas2764->dev,
				"wrong power level setting %d\n", level);
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_PM
static int tas2764_codec_suspend(struct snd_soc_component *component)
{
	struct tas2764_priv *tas2764 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
					    TAS2764_PWR_CTRL_MASK,
					    TAS2764_PWR_CTRL_SHUTDOWN);

	if (ret < 0)
		return ret;

	if (tas2764->sdz_gpio)
		gpiod_set_value_cansleep(tas2764->sdz_gpio, 0);

	regcache_cache_only(tas2764->regmap, true);
	regcache_mark_dirty(tas2764->regmap);

	return 0;
}

static int tas2764_codec_resume(struct snd_soc_component *component)
{
	struct tas2764_priv *tas2764 = snd_soc_component_get_drvdata(component);
	int ret;

	if (tas2764->sdz_gpio) {
		gpiod_set_value_cansleep(tas2764->sdz_gpio, 1);
		usleep_range(1000, 2000);
	}

	ret = snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
					    TAS2764_PWR_CTRL_MASK,
					    TAS2764_PWR_CTRL_ACTIVE);

	if (ret < 0)
		return ret;

	regcache_cache_only(tas2764->regmap, false);

	return regcache_sync(tas2764->regmap);
}
#else
#define tas2764_codec_suspend NULL
#define tas2764_codec_resume NULL
#endif

static const char * const tas2764_ASI1_src[] = {
	"I2C offset", "Left", "Right", "LeftRightDiv2",
};

static SOC_ENUM_SINGLE_DECL(
	tas2764_ASI1_src_enum, TAS2764_TDM_CFG2, TAS2764_TDM_CFG2_SCFG_SHIFT,
	tas2764_ASI1_src);

static const struct snd_kcontrol_new tas2764_asi1_mux =
	SOC_DAPM_ENUM("ASI1 Source", tas2764_ASI1_src_enum);

static int tas2764_dac_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct tas2764_priv *tas2764 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		ret = snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
						    TAS2764_PWR_CTRL_MASK,
						    TAS2764_PWR_CTRL_MUTE);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		ret = snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
						    TAS2764_PWR_CTRL_MASK,
						    TAS2764_PWR_CTRL_SHUTDOWN);
		break;
	default:
		dev_err(tas2764->dev, "Unsupported event\n");
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_kcontrol_new isense_switch =
	SOC_DAPM_SINGLE("Switch", TAS2764_PWR_CTRL, TAS2764_ISENSE_POWER_EN, 1, 1);
static const struct snd_kcontrol_new vsense_switch =
	SOC_DAPM_SINGLE("Switch", TAS2764_PWR_CTRL, TAS2764_VSENSE_POWER_EN, 1, 1);

static const struct snd_soc_dapm_widget tas2764_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("ASI1 Sel", SND_SOC_NOPM, 0, 0, &tas2764_asi1_mux),
	SND_SOC_DAPM_SWITCH("ISENSE", TAS2764_PWR_CTRL, TAS2764_ISENSE_POWER_EN,
			    1, &isense_switch),
	SND_SOC_DAPM_SWITCH("VSENSE", TAS2764_PWR_CTRL, TAS2764_VSENSE_POWER_EN,
			    1, &vsense_switch),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, tas2764_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_SIGGEN("VMON"),
	SND_SOC_DAPM_SIGGEN("IMON")
};

static const struct snd_soc_dapm_route tas2764_audio_map[] = {
	{"ASI1 Sel", "I2C offset", "ASI1"},
	{"ASI1 Sel", "Left", "ASI1"},
	{"ASI1 Sel", "Right", "ASI1"},
	{"ASI1 Sel", "LeftRightDiv2", "ASI1"},
	{"DAC", NULL, "ASI1 Sel"},
	{"OUT", NULL, "DAC"},
	{"ISENSE", "Switch", "IMON"},
	{"VSENSE", "Switch", "VMON"},
};

static int tas2764_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
					    TAS2764_PWR_CTRL_MASK,
					    mute ? TAS2764_PWR_CTRL_MUTE : 0);

	if (ret < 0)
		return ret;

	return 0;
}

static int tas2764_set_bitwidth(struct tas2764_priv *tas2764, int bitwidth)
{
	struct snd_soc_component *component = tas2764->component;
	int sense_en;
	int val;
	int ret;

	switch (bitwidth) {
	case SNDRV_PCM_FORMAT_S16_LE:
		ret = snd_soc_component_update_bits(component,
						    TAS2764_TDM_CFG2,
						    TAS2764_TDM_CFG2_RXW_MASK,
						    TAS2764_TDM_CFG2_RXW_16BITS);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ret = snd_soc_component_update_bits(component,
						    TAS2764_TDM_CFG2,
						    TAS2764_TDM_CFG2_RXW_MASK,
						    TAS2764_TDM_CFG2_RXW_24BITS);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		ret = snd_soc_component_update_bits(component,
						    TAS2764_TDM_CFG2,
						    TAS2764_TDM_CFG2_RXW_MASK,
						    TAS2764_TDM_CFG2_RXW_32BITS);
		break;

	default:
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	val = snd_soc_component_read(tas2764->component, TAS2764_PWR_CTRL);
	if (val < 0)
		return val;

	if (val & (1 << TAS2764_VSENSE_POWER_EN))
		sense_en = 0;
	else
		sense_en = TAS2764_TDM_CFG5_VSNS_ENABLE;

	ret = snd_soc_component_update_bits(tas2764->component, TAS2764_TDM_CFG5,
					    TAS2764_TDM_CFG5_VSNS_ENABLE,
					    sense_en);
	if (ret < 0)
		return ret;

	if (val & (1 << TAS2764_ISENSE_POWER_EN))
		sense_en = 0;
	else
		sense_en = TAS2764_TDM_CFG6_ISNS_ENABLE;

	ret = snd_soc_component_update_bits(tas2764->component, TAS2764_TDM_CFG6,
					    TAS2764_TDM_CFG6_ISNS_ENABLE,
					    sense_en);
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2764_set_samplerate(struct tas2764_priv *tas2764, int samplerate)
{
	struct snd_soc_component *component = tas2764->component;
	int ramp_rate_val;
	int ret;

	switch (samplerate) {
	case 48000:
		ramp_rate_val = TAS2764_TDM_CFG0_SMP_48KHZ |
				TAS2764_TDM_CFG0_44_1_48KHZ;
		break;
	case 44100:
		ramp_rate_val = TAS2764_TDM_CFG0_SMP_44_1KHZ |
				TAS2764_TDM_CFG0_44_1_48KHZ;
		break;
	case 96000:
		ramp_rate_val = TAS2764_TDM_CFG0_SMP_48KHZ |
				TAS2764_TDM_CFG0_88_2_96KHZ;
		break;
	case 88200:
		ramp_rate_val = TAS2764_TDM_CFG0_SMP_44_1KHZ |
				TAS2764_TDM_CFG0_88_2_96KHZ;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2764_TDM_CFG0,
					    TAS2764_TDM_CFG0_SMP_MASK |
					    TAS2764_TDM_CFG0_MASK,
					    ramp_rate_val);
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2764_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas2764_priv *tas2764 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = tas2764_set_bitwidth(tas2764, params_format(params));
	if (ret < 0)
		return ret;

	return tas2764_set_samplerate(tas2764, params_rate(params));
}

static int tas2764_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas2764_priv *tas2764 = snd_soc_component_get_drvdata(component);
	u8 tdm_rx_start_slot = 0, asi_cfg_0 = 0, asi_cfg_1 = 0;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		asi_cfg_0 ^= TAS2764_TDM_CFG0_FRAME_START;
		fallthrough;
	case SND_SOC_DAIFMT_NB_NF:
		asi_cfg_1 = TAS2764_TDM_CFG1_RX_RISING;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		asi_cfg_0 ^= TAS2764_TDM_CFG0_FRAME_START;
		fallthrough;
	case SND_SOC_DAIFMT_IB_NF:
		asi_cfg_1 = TAS2764_TDM_CFG1_RX_FALLING;
		break;
	}

	ret = snd_soc_component_update_bits(component, TAS2764_TDM_CFG1,
					    TAS2764_TDM_CFG1_RX_MASK,
					    asi_cfg_1);
	if (ret < 0)
		return ret;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		asi_cfg_0 ^= TAS2764_TDM_CFG0_FRAME_START;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_A:
		tdm_rx_start_slot = 1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
	case SND_SOC_DAIFMT_LEFT_J:
		tdm_rx_start_slot = 0;
		break;
	default:
		dev_err(tas2764->dev,
			"DAI Format is not found, fmt=0x%x\n", fmt);
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2764_TDM_CFG0,
					    TAS2764_TDM_CFG0_FRAME_START,
					    asi_cfg_0);
	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, TAS2764_TDM_CFG1,
					    TAS2764_TDM_CFG1_MASK,
					    (tdm_rx_start_slot << TAS2764_TDM_CFG1_51_SHIFT));
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2764_set_dai_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask,
				unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct tas2764_priv *tas2764 = snd_soc_component_get_drvdata(component);
	int left_slot, right_slot;
	int slots_cfg;
	int slot_size;
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

	slots_cfg = (right_slot << TAS2764_TDM_CFG3_RXS_SHIFT) | left_slot;

	ret = snd_soc_component_write(component, TAS2764_TDM_CFG3, slots_cfg);
	if (ret)
		return ret;

	switch (slot_width) {
	case 16:
		slot_size = TAS2764_TDM_CFG2_RXS_16BITS;
		break;
	case 24:
		slot_size = TAS2764_TDM_CFG2_RXS_24BITS;
		break;
	case 32:
		slot_size = TAS2764_TDM_CFG2_RXS_32BITS;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2764_TDM_CFG2,
					    TAS2764_TDM_CFG2_RXS_MASK,
					    slot_size);
	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, TAS2764_TDM_CFG5,
					    TAS2764_TDM_CFG5_50_MASK,
					    tas2764->v_sense_slot);
	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, TAS2764_TDM_CFG6,
					    TAS2764_TDM_CFG6_50_MASK,
					    tas2764->i_sense_slot);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_dai_ops tas2764_dai_ops = {
	.mute_stream = tas2764_mute,
	.hw_params  = tas2764_hw_params,
	.set_fmt    = tas2764_set_fmt,
	.set_tdm_slot = tas2764_set_dai_tdm_slot,
	.no_capture_mute = 1,
};

#define TAS2764_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

#define TAS2764_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
		       SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_88200)

static struct snd_soc_dai_driver tas2764_dai_driver[] = {
	{
		.name = "tas2764 ASI1",
		.id = 0,
		.playback = {
			.stream_name    = "ASI1 Playback",
			.channels_min   = 2,
			.channels_max   = 2,
			.rates      = TAS2764_RATES,
			.formats    = TAS2764_FORMATS,
		},
		.capture = {
			.stream_name    = "ASI1 Capture",
			.channels_min   = 0,
			.channels_max   = 2,
			.rates = TAS2764_RATES,
			.formats = TAS2764_FORMATS,
		},
		.ops = &tas2764_dai_ops,
		.symmetric_rate = 1,
	},
};

static int tas2764_codec_probe(struct snd_soc_component *component)
{
	struct tas2764_priv *tas2764 = snd_soc_component_get_drvdata(component);
	int ret;

	tas2764->component = component;

	if (tas2764->sdz_gpio) {
		gpiod_set_value_cansleep(tas2764->sdz_gpio, 1);
		usleep_range(1000, 2000);
	}

	tas2764_reset(tas2764);

	ret = snd_soc_component_update_bits(tas2764->component, TAS2764_TDM_CFG5,
					    TAS2764_TDM_CFG5_VSNS_ENABLE, 0);
	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(tas2764->component, TAS2764_TDM_CFG6,
					    TAS2764_TDM_CFG6_ISNS_ENABLE, 0);
	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, TAS2764_PWR_CTRL,
					    TAS2764_PWR_CTRL_MASK,
					    TAS2764_PWR_CTRL_MUTE);
	if (ret < 0)
		return ret;

	return 0;
}

static DECLARE_TLV_DB_SCALE(tas2764_digital_tlv, 1100, 50, 0);
static DECLARE_TLV_DB_SCALE(tas2764_playback_volume, -10050, 50, 1);

static const struct snd_kcontrol_new tas2764_snd_controls[] = {
	SOC_SINGLE_TLV("Speaker Volume", TAS2764_DVC, 0,
		       TAS2764_DVC_MAX, 1, tas2764_playback_volume),
	SOC_SINGLE_TLV("Amp Gain Volume", TAS2764_CHNL_0, 1, 0x14, 0,
		       tas2764_digital_tlv),
};

static const struct snd_soc_component_driver soc_component_driver_tas2764 = {
	.probe			= tas2764_codec_probe,
	.suspend		= tas2764_codec_suspend,
	.resume			= tas2764_codec_resume,
	.set_bias_level		= tas2764_set_bias_level,
	.controls		= tas2764_snd_controls,
	.num_controls		= ARRAY_SIZE(tas2764_snd_controls),
	.dapm_widgets		= tas2764_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas2764_dapm_widgets),
	.dapm_routes		= tas2764_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas2764_audio_map),
	.idle_bias_on		= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct reg_default tas2764_reg_defaults[] = {
	{ TAS2764_PAGE, 0x00 },
	{ TAS2764_SW_RST, 0x00 },
	{ TAS2764_PWR_CTRL, 0x1a },
	{ TAS2764_DVC, 0x00 },
	{ TAS2764_CHNL_0, 0x28 },
	{ TAS2764_TDM_CFG0, 0x09 },
	{ TAS2764_TDM_CFG1, 0x02 },
	{ TAS2764_TDM_CFG2, 0x0a },
	{ TAS2764_TDM_CFG3, 0x10 },
	{ TAS2764_TDM_CFG5, 0x42 },
};

static const struct regmap_range_cfg tas2764_regmap_ranges[] = {
	{
		.range_min = 0,
		.range_max = 1 * 128,
		.selector_reg = TAS2764_PAGE,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 128,
	},
};

static const struct regmap_config tas2764_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_defaults = tas2764_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tas2764_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.ranges = tas2764_regmap_ranges,
	.num_ranges = ARRAY_SIZE(tas2764_regmap_ranges),
	.max_register = 1 * 128,
};

static int tas2764_parse_dt(struct device *dev, struct tas2764_priv *tas2764)
{
	int ret = 0;

	tas2764->reset_gpio = devm_gpiod_get_optional(tas2764->dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(tas2764->reset_gpio)) {
		if (PTR_ERR(tas2764->reset_gpio) == -EPROBE_DEFER) {
			tas2764->reset_gpio = NULL;
			return -EPROBE_DEFER;
		}
	}

	tas2764->sdz_gpio = devm_gpiod_get_optional(dev, "shutdown", GPIOD_OUT_HIGH);
	if (IS_ERR(tas2764->sdz_gpio)) {
		if (PTR_ERR(tas2764->sdz_gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		tas2764->sdz_gpio = NULL;
	}

	ret = fwnode_property_read_u32(dev->fwnode, "ti,imon-slot-no",
				       &tas2764->i_sense_slot);
	if (ret)
		tas2764->i_sense_slot = 0;

	ret = fwnode_property_read_u32(dev->fwnode, "ti,vmon-slot-no",
				       &tas2764->v_sense_slot);
	if (ret)
		tas2764->v_sense_slot = 2;

	return 0;
}

static int tas2764_i2c_probe(struct i2c_client *client)
{
	struct tas2764_priv *tas2764;
	int result;

	tas2764 = devm_kzalloc(&client->dev, sizeof(struct tas2764_priv),
			       GFP_KERNEL);
	if (!tas2764)
		return -ENOMEM;

	tas2764->dev = &client->dev;
	i2c_set_clientdata(client, tas2764);
	dev_set_drvdata(&client->dev, tas2764);

	tas2764->regmap = devm_regmap_init_i2c(client, &tas2764_i2c_regmap);
	if (IS_ERR(tas2764->regmap)) {
		result = PTR_ERR(tas2764->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
					result);
		return result;
	}

	if (client->dev.of_node) {
		result = tas2764_parse_dt(&client->dev, tas2764);
		if (result) {
			dev_err(tas2764->dev, "%s: Failed to parse devicetree\n",
				__func__);
			return result;
		}
	}

	return devm_snd_soc_register_component(tas2764->dev,
					       &soc_component_driver_tas2764,
					       tas2764_dai_driver,
					       ARRAY_SIZE(tas2764_dai_driver));
}

static const struct i2c_device_id tas2764_i2c_id[] = {
	{ "tas2764", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2764_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas2764_of_match[] = {
	{ .compatible = "ti,tas2764" },
	{},
};
MODULE_DEVICE_TABLE(of, tas2764_of_match);
#endif

static struct i2c_driver tas2764_i2c_driver = {
	.driver = {
		.name   = "tas2764",
		.of_match_table = of_match_ptr(tas2764_of_match),
	},
	.probe_new  = tas2764_i2c_probe,
	.id_table   = tas2764_i2c_id,
};
module_i2c_driver(tas2764_i2c_driver);

MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_DESCRIPTION("TAS2764 I2C Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
