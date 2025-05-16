// SPDX-License-Identifier: GPL-2.0
//
// Driver for the Texas Instruments TAS2562 CODEC
// Copyright (C) 2019 Texas Instruments Inc.


#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "tas2562.h"

#define TAS2562_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FORMAT_S32_LE)

/* DVC equation involves floating point math
 * round(10^(volume in dB/20)*2^30)
 * so create a lookup table for 2dB step
 */
static const unsigned int float_vol_db_lookup[] = {
0x00000d43, 0x000010b2, 0x00001505, 0x00001a67, 0x00002151,
0x000029f1, 0x000034cd, 0x00004279, 0x000053af, 0x0000695b,
0x0000695b, 0x0000a6fa, 0x0000d236, 0x000108a4, 0x00014d2a,
0x0001a36e, 0x00021008, 0x000298c0, 0x000344df, 0x00041d8f,
0x00052e5a, 0x000685c8, 0x00083621, 0x000a566d, 0x000d03a7,
0x0010624d, 0x0014a050, 0x0019f786, 0x0020b0bc, 0x0029279d,
0x0033cf8d, 0x004139d3, 0x00521d50, 0x00676044, 0x0082248a,
0x00a3d70a, 0x00ce4328, 0x0103ab3d, 0x0146e75d, 0x019b8c27,
0x02061b89, 0x028c423f, 0x03352529, 0x0409c2b0, 0x05156d68,
0x080e9f96, 0x0a24b062, 0x0cc509ab, 0x10137987, 0x143d1362,
0x197a967f, 0x2013739e, 0x28619ae9, 0x32d64617, 0x40000000
};

struct tas2562_data {
	struct snd_soc_component *component;
	struct gpio_desc *sdz_gpio;
	struct regmap *regmap;
	struct device *dev;
	struct i2c_client *client;
	int v_sense_slot;
	int i_sense_slot;
	int volume_lvl;
	int model_id;
	bool dac_powered;
	bool unmuted;
};

enum tas256x_model {
	TAS2562,
	TAS2564,
	TAS2110,
};

static int tas2562_set_samplerate(struct tas2562_data *tas2562, int samplerate)
{
	int samp_rate;
	int ramp_rate;

	switch (samplerate) {
	case 7350:
		ramp_rate = TAS2562_TDM_CFG0_RAMPRATE_44_1;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_7305_8KHZ;
		break;
	case 8000:
		ramp_rate = 0;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_7305_8KHZ;
		break;
	case 14700:
		ramp_rate = TAS2562_TDM_CFG0_RAMPRATE_44_1;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_14_7_16KHZ;
		break;
	case 16000:
		ramp_rate = 0;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_14_7_16KHZ;
		break;
	case 22050:
		ramp_rate = TAS2562_TDM_CFG0_RAMPRATE_44_1;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_22_05_24KHZ;
		break;
	case 24000:
		ramp_rate = 0;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_22_05_24KHZ;
		break;
	case 29400:
		ramp_rate = TAS2562_TDM_CFG0_RAMPRATE_44_1;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_29_4_32KHZ;
		break;
	case 32000:
		ramp_rate = 0;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_29_4_32KHZ;
		break;
	case 44100:
		ramp_rate = TAS2562_TDM_CFG0_RAMPRATE_44_1;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_44_1_48KHZ;
		break;
	case 48000:
		ramp_rate = 0;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_44_1_48KHZ;
		break;
	case 88200:
		ramp_rate = TAS2562_TDM_CFG0_RAMPRATE_44_1;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_88_2_96KHZ;
		break;
	case 96000:
		ramp_rate = 0;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_88_2_96KHZ;
		break;
	case 176400:
		ramp_rate = TAS2562_TDM_CFG0_RAMPRATE_44_1;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_176_4_192KHZ;
		break;
	case 192000:
		ramp_rate = 0;
		samp_rate = TAS2562_TDM_CFG0_SAMPRATE_176_4_192KHZ;
		break;
	default:
		dev_info(tas2562->dev, "%s, unsupported sample rate, %d\n",
			__func__, samplerate);
		return -EINVAL;
	}

	snd_soc_component_update_bits(tas2562->component, TAS2562_TDM_CFG0,
		TAS2562_TDM_CFG0_RAMPRATE_MASK,	ramp_rate);
	snd_soc_component_update_bits(tas2562->component, TAS2562_TDM_CFG0,
		TAS2562_TDM_CFG0_SAMPRATE_MASK,	samp_rate);

	return 0;
}

static int tas2562_set_dai_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);
	int left_slot, right_slot;
	int slots_cfg;
	int ret;

	if (!tx_mask) {
		dev_err(component->dev, "tx masks must not be 0\n");
		return -EINVAL;
	}

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
		}
	}

	slots_cfg = (right_slot << TAS2562_RIGHT_SLOT_SHIFT) | left_slot;

	ret = snd_soc_component_write(component, TAS2562_TDM_CFG3, slots_cfg);
	if (ret < 0)
		return ret;

	switch (slot_width) {
	case 16:
		ret = snd_soc_component_update_bits(component,
						    TAS2562_TDM_CFG2,
						    TAS2562_TDM_CFG2_RXLEN_MASK,
						    TAS2562_TDM_CFG2_RXLEN_16B);
		break;
	case 24:
		ret = snd_soc_component_update_bits(component,
						    TAS2562_TDM_CFG2,
						    TAS2562_TDM_CFG2_RXLEN_MASK,
						    TAS2562_TDM_CFG2_RXLEN_24B);
		break;
	case 32:
		ret = snd_soc_component_update_bits(component,
						    TAS2562_TDM_CFG2,
						    TAS2562_TDM_CFG2_RXLEN_MASK,
						    TAS2562_TDM_CFG2_RXLEN_32B);
		break;

	case 0:
		/* Do not change slot width */
		break;
	default:
		dev_err(tas2562->dev, "slot width not supported");
		ret = -EINVAL;
	}

	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, TAS2562_TDM_CFG5,
					    TAS2562_TDM_CFG5_VSNS_SLOT_MASK,
					    tas2562->v_sense_slot);
	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, TAS2562_TDM_CFG6,
					    TAS2562_TDM_CFG6_ISNS_SLOT_MASK,
					    tas2562->i_sense_slot);
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2562_set_bitwidth(struct tas2562_data *tas2562, int bitwidth)
{
	int ret;
	int val;
	int sense_en;

	switch (bitwidth) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_component_update_bits(tas2562->component,
					      TAS2562_TDM_CFG2,
					      TAS2562_TDM_CFG2_RXWLEN_MASK,
					      TAS2562_TDM_CFG2_RXWLEN_16B);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		snd_soc_component_update_bits(tas2562->component,
					      TAS2562_TDM_CFG2,
					      TAS2562_TDM_CFG2_RXWLEN_MASK,
					      TAS2562_TDM_CFG2_RXWLEN_24B);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		snd_soc_component_update_bits(tas2562->component,
					      TAS2562_TDM_CFG2,
					      TAS2562_TDM_CFG2_RXWLEN_MASK,
					      TAS2562_TDM_CFG2_RXWLEN_32B);
		break;

	default:
		dev_info(tas2562->dev, "Unsupported bitwidth format\n");
		return -EINVAL;
	}

	val = snd_soc_component_read(tas2562->component, TAS2562_PWR_CTRL);
	if (val < 0)
		return val;

	if (val & (1 << TAS2562_VSENSE_POWER_EN))
		sense_en = 0;
	else
		sense_en = TAS2562_TDM_CFG5_VSNS_EN;

	ret = snd_soc_component_update_bits(tas2562->component, TAS2562_TDM_CFG5,
		TAS2562_TDM_CFG5_VSNS_EN, sense_en);
	if (ret < 0)
		return ret;

	if (val & (1 << TAS2562_ISENSE_POWER_EN))
		sense_en = 0;
	else
		sense_en = TAS2562_TDM_CFG6_ISNS_EN;

	ret = snd_soc_component_update_bits(tas2562->component, TAS2562_TDM_CFG6,
		TAS2562_TDM_CFG6_ISNS_EN, sense_en);
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2562_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = tas2562_set_bitwidth(tas2562, params_format(params));
	if (ret) {
		dev_err(tas2562->dev, "set bitwidth failed, %d\n", ret);
		return ret;
	}

	ret = tas2562_set_samplerate(tas2562, params_rate(params));
	if (ret)
		dev_err(tas2562->dev, "set sample rate failed, %d\n", ret);

	return ret;
}

static int tas2562_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);
	u8 asi_cfg_1 = 0;
	u8 tdm_rx_start_slot = 0;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		asi_cfg_1 = 0;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		asi_cfg_1 |= TAS2562_TDM_CFG1_RX_FALLING;
		break;
	default:
		dev_err(tas2562->dev, "ASI format Inverse is not found\n");
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2562_TDM_CFG1,
					    TAS2562_TDM_CFG1_RX_EDGE_MASK,
					    asi_cfg_1);
	if (ret < 0) {
		dev_err(tas2562->dev, "Failed to set RX edge\n");
		return ret;
	}
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_DSP_B:
		tdm_rx_start_slot = 0;
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_DSP_A:
		tdm_rx_start_slot = 1;
		break;
	default:
		dev_err(tas2562->dev,
			"DAI Format is not found, fmt=0x%x\n", fmt);
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS2562_TDM_CFG1,
				TAS2562_RX_OFF_MASK, (tdm_rx_start_slot << 1));
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2562_update_pwr_ctrl(struct tas2562_data *tas2562)
{
	struct snd_soc_component *component = tas2562->component;
	unsigned int val;
	int ret;

	if (tas2562->dac_powered)
		val = tas2562->unmuted ?
			TAS2562_ACTIVE : TAS2562_MUTE;
	else
		val = TAS2562_SHUTDOWN;

	ret = snd_soc_component_update_bits(component, TAS2562_PWR_CTRL,
					    TAS2562_MODE_MASK, val);
	if (ret < 0)
		return ret;

	return 0;
}

static int tas2562_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(dai->component);

	tas2562->unmuted = !mute;
	return tas2562_update_pwr_ctrl(tas2562);
}

static int tas2562_codec_probe(struct snd_soc_component *component)
{
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);

	tas2562->component = component;

	if (tas2562->sdz_gpio)
		gpiod_set_value_cansleep(tas2562->sdz_gpio, 1);

	return 0;
}

#ifdef CONFIG_PM
static int tas2562_suspend(struct snd_soc_component *component)
{
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(tas2562->regmap, true);
	regcache_mark_dirty(tas2562->regmap);

	if (tas2562->sdz_gpio)
		gpiod_set_value_cansleep(tas2562->sdz_gpio, 0);

	return 0;
}

static int tas2562_resume(struct snd_soc_component *component)
{
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);

	if (tas2562->sdz_gpio)
		gpiod_set_value_cansleep(tas2562->sdz_gpio, 1);

	regcache_cache_only(tas2562->regmap, false);

	return regcache_sync(tas2562->regmap);
}
#else
#define tas2562_suspend NULL
#define tas2562_resume NULL
#endif

static const char * const tas2562_ASI1_src[] = {
	"I2C offset", "Left", "Right", "LeftRightDiv2",
};

static SOC_ENUM_SINGLE_DECL(tas2562_ASI1_src_enum, TAS2562_TDM_CFG2, 4,
			    tas2562_ASI1_src);

static const struct snd_kcontrol_new tas2562_asi1_mux =
	SOC_DAPM_ENUM("ASI1 Source", tas2562_ASI1_src_enum);

static int tas2562_dac_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
					snd_soc_dapm_to_component(w->dapm);
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		tas2562->dac_powered = true;
		ret = tas2562_update_pwr_ctrl(tas2562);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		tas2562->dac_powered = false;
		ret = tas2562_update_pwr_ctrl(tas2562);
		break;
	default:
		dev_err(tas2562->dev, "Not supported evevt\n");
		return -EINVAL;
	}

	return ret;
}

static int tas2562_volume_control_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = tas2562->volume_lvl;
	return 0;
}

static int tas2562_volume_control_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas2562_data *tas2562 = snd_soc_component_get_drvdata(component);
	int ret;
	u32 reg_val;

	reg_val = float_vol_db_lookup[ucontrol->value.integer.value[0]/2];
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG4,
				      (reg_val & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG3,
				      ((reg_val >> 8) & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG2,
				      ((reg_val >> 16) & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG1,
				      ((reg_val >> 24) & 0xff));
	if (ret)
		return ret;

	tas2562->volume_lvl = ucontrol->value.integer.value[0];

	return 0;
}

/* Digital Volume Control. From 0 dB to -110 dB in 1 dB steps */
static const DECLARE_TLV_DB_SCALE(dvc_tlv, -11000, 100, 0);

static DECLARE_TLV_DB_SCALE(tas2562_dac_tlv, 850, 50, 0);

static const struct snd_kcontrol_new isense_switch =
	SOC_DAPM_SINGLE("Switch", TAS2562_PWR_CTRL, TAS2562_ISENSE_POWER_EN,
			1, 1);

static const struct snd_kcontrol_new vsense_switch =
	SOC_DAPM_SINGLE("Switch", TAS2562_PWR_CTRL, TAS2562_VSENSE_POWER_EN,
			1, 1);

static const struct snd_kcontrol_new tas2562_snd_controls[] = {
	SOC_SINGLE_TLV("Amp Gain Volume", TAS2562_PB_CFG1, 1, 0x1c, 0,
		       tas2562_dac_tlv),
	SOC_SINGLE_EXT_TLV("Digital Volume Control", TAS2562_DVC_CFG1, 0, 110, 0,
			   tas2562_volume_control_get, tas2562_volume_control_put,
			   dvc_tlv),
};

static const struct snd_soc_dapm_widget tas2110_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("ASI1 Sel", SND_SOC_NOPM, 0, 0, &tas2562_asi1_mux),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, tas2562_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT"),
};

static const struct snd_soc_dapm_route tas2110_audio_map[] = {
	{"ASI1 Sel", "I2C offset", "ASI1"},
	{"ASI1 Sel", "Left", "ASI1"},
	{"ASI1 Sel", "Right", "ASI1"},
	{"ASI1 Sel", "LeftRightDiv2", "ASI1"},
	{ "DAC", NULL, "ASI1 Sel" },
	{ "OUT", NULL, "DAC" },
};

static const struct snd_soc_component_driver soc_component_dev_tas2110 = {
	.probe			= tas2562_codec_probe,
	.suspend		= tas2562_suspend,
	.resume			= tas2562_resume,
	.controls		= tas2562_snd_controls,
	.num_controls		= ARRAY_SIZE(tas2562_snd_controls),
	.dapm_widgets		= tas2110_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas2110_dapm_widgets),
	.dapm_routes		= tas2110_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas2110_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct snd_soc_dapm_widget tas2562_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("ASI1 Sel", SND_SOC_NOPM, 0, 0, &tas2562_asi1_mux),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, tas2562_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SWITCH("ISENSE", TAS2562_PWR_CTRL, 3, 1, &isense_switch),
	SND_SOC_DAPM_SWITCH("VSENSE", TAS2562_PWR_CTRL, 2, 1, &vsense_switch),
	SND_SOC_DAPM_SIGGEN("VMON"),
	SND_SOC_DAPM_SIGGEN("IMON"),
	SND_SOC_DAPM_OUTPUT("OUT"),
};

static const struct snd_soc_dapm_route tas2562_audio_map[] = {
	{"ASI1 Sel", "I2C offset", "ASI1"},
	{"ASI1 Sel", "Left", "ASI1"},
	{"ASI1 Sel", "Right", "ASI1"},
	{"ASI1 Sel", "LeftRightDiv2", "ASI1"},
	{ "DAC", NULL, "ASI1 Sel" },
	{ "OUT", NULL, "DAC" },
	{"ISENSE", "Switch", "IMON"},
	{"VSENSE", "Switch", "VMON"},
};

static const struct snd_soc_component_driver soc_component_dev_tas2562 = {
	.probe			= tas2562_codec_probe,
	.suspend		= tas2562_suspend,
	.resume			= tas2562_resume,
	.controls		= tas2562_snd_controls,
	.num_controls		= ARRAY_SIZE(tas2562_snd_controls),
	.dapm_widgets		= tas2562_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas2562_dapm_widgets),
	.dapm_routes		= tas2562_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas2562_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct snd_soc_dai_ops tas2562_speaker_dai_ops = {
	.hw_params	= tas2562_hw_params,
	.set_fmt	= tas2562_set_dai_fmt,
	.set_tdm_slot	= tas2562_set_dai_tdm_slot,
	.mute_stream	= tas2562_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver tas2562_dai[] = {
	{
		.name = "tas2562-amplifier",
		.id = 0,
		.playback = {
			.stream_name    = "ASI1 Playback",
			.channels_min   = 2,
			.channels_max   = 2,
			.rates      = SNDRV_PCM_RATE_8000_192000,
			.formats    = TAS2562_FORMATS,
		},
		.capture = {
			.stream_name    = "ASI1 Capture",
			.channels_min   = 0,
			.channels_max   = 2,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.formats	= TAS2562_FORMATS,
		},
		.ops = &tas2562_speaker_dai_ops,
	},
};

static const struct regmap_range_cfg tas2562_ranges[] = {
	{
		.range_min = 0,
		.range_max = 5 * 128,
		.selector_reg = TAS2562_PAGE_CTRL,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 128,
	},
};

static const struct reg_default tas2562_reg_defaults[] = {
	{ TAS2562_PAGE_CTRL, 0x00 },
	{ TAS2562_SW_RESET, 0x00 },
	{ TAS2562_PWR_CTRL, 0x0e },
	{ TAS2562_PB_CFG1, 0x20 },
	{ TAS2562_TDM_CFG0, 0x09 },
	{ TAS2562_TDM_CFG1, 0x02 },
	{ TAS2562_DVC_CFG1, 0x40 },
	{ TAS2562_DVC_CFG2, 0x40 },
	{ TAS2562_DVC_CFG3, 0x00 },
	{ TAS2562_DVC_CFG4, 0x00 },
};

static const struct regmap_config tas2562_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 5 * 128,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = tas2562_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tas2562_reg_defaults),
	.ranges = tas2562_ranges,
	.num_ranges = ARRAY_SIZE(tas2562_ranges),
};

static int tas2562_parse_dt(struct tas2562_data *tas2562)
{
	struct device *dev = tas2562->dev;
	int ret = 0;

	tas2562->sdz_gpio = devm_gpiod_get_optional(dev, "shutdown", GPIOD_OUT_HIGH);
	if (IS_ERR(tas2562->sdz_gpio)) {
		if (PTR_ERR(tas2562->sdz_gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		tas2562->sdz_gpio = NULL;
	}

	/*
	 * The shut-down property is deprecated but needs to be checked for
	 * backwards compatibility.
	 */
	if (tas2562->sdz_gpio == NULL) {
		tas2562->sdz_gpio = devm_gpiod_get_optional(dev, "shut-down",
							      GPIOD_OUT_HIGH);
		if (IS_ERR(tas2562->sdz_gpio))
			if (PTR_ERR(tas2562->sdz_gpio) == -EPROBE_DEFER)
				return -EPROBE_DEFER;

		tas2562->sdz_gpio = NULL;
	}

	if (tas2562->model_id == TAS2110)
		return ret;

	ret = fwnode_property_read_u32(dev->fwnode, "ti,imon-slot-no",
			&tas2562->i_sense_slot);
	if (ret) {
		dev_err(dev, "Property %s is missing setting default slot\n",
			"ti,imon-slot-no");
		tas2562->i_sense_slot = 0;
	}


	ret = fwnode_property_read_u32(dev->fwnode, "ti,vmon-slot-no",
			&tas2562->v_sense_slot);
	if (ret) {
		dev_info(dev, "Property %s is missing setting default slot\n",
			"ti,vmon-slot-no");
		tas2562->v_sense_slot = 2;
	}

	if (tas2562->v_sense_slot < tas2562->i_sense_slot) {
		dev_err(dev, "Vsense slot must be greater than Isense slot\n");
		return -EINVAL;
	}

	return ret;
}

static const struct i2c_device_id tas2562_id[] = {
	{ "tas2562", TAS2562 },
	{ "tas2564", TAS2564 },
	{ "tas2110", TAS2110 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2562_id);

static int tas2562_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tas2562_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->dev = &client->dev;
	data->model_id = (uintptr_t)i2c_get_match_data(client);

	tas2562_parse_dt(data);

	data->regmap = devm_regmap_init_i2c(client, &tas2562_regmap_config);
	if (IS_ERR(data->regmap)) {
		ret = PTR_ERR(data->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	dev_set_drvdata(&client->dev, data);

	if (data->model_id == TAS2110)
		return devm_snd_soc_register_component(dev,
						       &soc_component_dev_tas2110,
						       tas2562_dai,
						       ARRAY_SIZE(tas2562_dai));

	return devm_snd_soc_register_component(dev, &soc_component_dev_tas2562,
					       tas2562_dai,
					       ARRAY_SIZE(tas2562_dai));

}

#ifdef CONFIG_OF
static const struct of_device_id tas2562_of_match[] = {
	{ .compatible = "ti,tas2562", },
	{ .compatible = "ti,tas2564", },
	{ .compatible = "ti,tas2110", },
	{ },
};
MODULE_DEVICE_TABLE(of, tas2562_of_match);
#endif

static struct i2c_driver tas2562_i2c_driver = {
	.driver = {
		.name = "tas2562",
		.of_match_table = of_match_ptr(tas2562_of_match),
	},
	.probe = tas2562_probe,
	.id_table = tas2562_id,
};

module_i2c_driver(tas2562_i2c_driver);

MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_DESCRIPTION("TAS2562 Audio amplifier driver");
MODULE_LICENSE("GPL");
