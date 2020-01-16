// SPDX-License-Identifier: GPL-2.0
/*
 * rt715.c -- rt715 ALSA SoC audio driver
 *
 * Copyright(c) 2019 Realtek Semiconductor Corp.
 *
 * ALC715 ASoC Codec Driver based Intel Dummy SdW codec driver
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/soundwire/sdw.h>
#include <linux/gpio.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/hda_verbs.h>

#include "rt715.h"

static int rt715_index_write(struct regmap *regmap, unsigned int reg,
		unsigned int value)
{
	int ret;
	unsigned int addr = ((RT715_PRIV_INDEX_W_H) << 8) | reg;

	ret = regmap_write(regmap, addr, value);
	if (ret < 0) {
		pr_err("Failed to set private value: %08x <= %04x %d\n", ret,
			addr, value);
	}

	return ret;
}

static void rt715_get_gain(struct rt715_priv *rt715, unsigned int addr_h,
				unsigned int addr_l, unsigned int val_h,
				unsigned int *r_val, unsigned int *l_val)
{
	int ret;
	/* R Channel */
	*r_val = (val_h << 8);
	ret = regmap_read(rt715->regmap, addr_l, r_val);
	if (ret < 0)
		pr_err("Failed to get R channel gain.\n");

	/* L Channel */
	val_h |= 0x20;
	*l_val = (val_h << 8);
	ret = regmap_read(rt715->regmap, addr_h, l_val);
	if (ret < 0)
		pr_err("Failed to get L channel gain.\n");
}

/* For Verb-Set Amplifier Gain (Verb ID = 3h) */
static int rt715_set_amp_gain_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt715_priv *rt715 = snd_soc_component_get_drvdata(component);
	unsigned int addr_h, addr_l, val_h, val_ll, val_lr;
	unsigned int read_ll, read_rl;
	int i;

	/* Can't use update bit function, so read the original value first */
	addr_h = mc->reg;
	addr_l = mc->rreg;
	if (mc->shift == RT715_DIR_OUT_SFT) /* output */
		val_h = 0x80;
	else /* input */
		val_h = 0x0;

	rt715_get_gain(rt715, addr_h, addr_l, val_h, &read_rl, &read_ll);

	/* L Channel */
	if (mc->invert) {
		/* for mute */
		val_ll = (mc->max - ucontrol->value.integer.value[0]) << 7;
		/* keep gain */
		read_ll = read_ll & 0x7f;
		val_ll |= read_ll;
	} else {
		/* for gain */
		val_ll = ((ucontrol->value.integer.value[0]) & 0x7f);
		if (val_ll > mc->max)
			val_ll = mc->max;
		/* keep mute status */
		read_ll = read_ll & 0x80;
		val_ll |= read_ll;
	}

	/* R Channel */
	if (mc->invert) {
		regmap_write(rt715->regmap,
			     RT715_SET_AUDIO_POWER_STATE, AC_PWRST_D0);
		/* for mute */
		val_lr = (mc->max - ucontrol->value.integer.value[1]) << 7;
		/* keep gain */
		read_rl = read_rl & 0x7f;
		val_lr |= read_rl;
	} else {
		/* for gain */
		val_lr = ((ucontrol->value.integer.value[1]) & 0x7f);
		if (val_lr > mc->max)
			val_lr = mc->max;
		/* keep mute status */
		read_rl = read_rl & 0x80;
		val_lr |= read_rl;
	}

	for (i = 0; i < 3; i++) { /* retry 3 times at most */

		if (val_ll == val_lr) {
			/* Set both L/R channels at the same time */
			val_h = (1 << mc->shift) | (3 << 4);
			regmap_write(rt715->regmap, addr_h,
				(val_h << 8 | val_ll));
			regmap_write(rt715->regmap, addr_l,
				(val_h << 8 | val_ll));
		} else {
			/* Lch*/
			val_h = (1 << mc->shift) | (1 << 5);
			regmap_write(rt715->regmap, addr_h,
				(val_h << 8 | val_ll));
			/* Rch */
			val_h = (1 << mc->shift) | (1 << 4);
			regmap_write(rt715->regmap, addr_l,
				(val_h << 8 | val_lr));
		}
		/* check result */
		if (mc->shift == RT715_DIR_OUT_SFT) /* output */
			val_h = 0x80;
		else /* input */
			val_h = 0x0;

		rt715_get_gain(rt715, addr_h, addr_l, val_h,
			       &read_rl, &read_ll);
		if (read_rl == val_lr && read_ll == val_ll)
			break;
	}
	/* D0:power on state, D3: power saving mode */
	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt715->regmap,
				RT715_SET_AUDIO_POWER_STATE, AC_PWRST_D3);
	return 0;
}

static int rt715_set_amp_gain_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt715_priv *rt715 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int addr_h, addr_l, val_h;
	unsigned int read_ll, read_rl;

	addr_h = mc->reg;
	addr_l = mc->rreg;
	if (mc->shift == RT715_DIR_OUT_SFT) /* output */
		val_h = 0x80;
	else /* input */
		val_h = 0x0;

	rt715_get_gain(rt715, addr_h, addr_l, val_h, &read_rl, &read_ll);

	if (mc->invert) {
		/* for mute status */
		read_ll = !((read_ll & 0x80) >> RT715_MUTE_SFT);
		read_rl = !((read_rl & 0x80) >> RT715_MUTE_SFT);
	} else {
		/* for gain */
		read_ll = read_ll & 0x7f;
		read_rl = read_rl & 0x7f;
	}
	ucontrol->value.integer.value[0] = read_ll;
	ucontrol->value.integer.value[1] = read_rl;

	return 0;
}

static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, 0, 1000, 0);

#define SOC_DOUBLE_R_EXT(xname, reg_left, reg_right, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_DOUBLE_R_VALUE(reg_left, reg_right, xshift, \
					    xmax, xinvert) }

static const struct snd_kcontrol_new rt715_snd_controls[] = {
	/* Capture switch */
	SOC_DOUBLE_R_EXT("ADC 07 Capture Switch", RT715_SET_GAIN_MIC_ADC_H,
			RT715_SET_GAIN_MIC_ADC_L, RT715_DIR_IN_SFT, 1, 1,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put),
	SOC_DOUBLE_R_EXT("ADC 08 Capture Switch", RT715_SET_GAIN_LINE_ADC_H,
			RT715_SET_GAIN_LINE_ADC_L, RT715_DIR_IN_SFT, 1, 1,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put),
	SOC_DOUBLE_R_EXT("ADC 09 Capture Switch", RT715_SET_GAIN_MIX_ADC_H,
			RT715_SET_GAIN_MIX_ADC_L, RT715_DIR_IN_SFT, 1, 1,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put),
	SOC_DOUBLE_R_EXT("ADC 27 Capture Switch", RT715_SET_GAIN_MIX_ADC2_H,
			RT715_SET_GAIN_MIX_ADC2_L, RT715_DIR_IN_SFT, 1, 1,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put),
	/* Volume Control */
	SOC_DOUBLE_R_EXT_TLV("ADC 07 Capture Volume", RT715_SET_GAIN_MIC_ADC_H,
			RT715_SET_GAIN_MIC_ADC_L, RT715_DIR_IN_SFT, 0x3f, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("ADC 08 Capture Volume", RT715_SET_GAIN_LINE_ADC_H,
			RT715_SET_GAIN_LINE_ADC_L, RT715_DIR_IN_SFT, 0x3f, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("ADC 09 Capture Volume", RT715_SET_GAIN_MIX_ADC_H,
			RT715_SET_GAIN_MIX_ADC_L, RT715_DIR_IN_SFT, 0x3f, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("ADC 27 Capture Volume", RT715_SET_GAIN_MIX_ADC2_H,
			RT715_SET_GAIN_MIX_ADC2_L, RT715_DIR_IN_SFT, 0x3f, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			in_vol_tlv),
	/* MIC Boost Control */
	SOC_DOUBLE_R_EXT_TLV("DMIC1 Boost", RT715_SET_GAIN_DMIC1_H,
			RT715_SET_GAIN_DMIC1_L, RT715_DIR_IN_SFT, 3, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("DMIC2 Boost", RT715_SET_GAIN_DMIC2_H,
			RT715_SET_GAIN_DMIC2_L, RT715_DIR_IN_SFT, 3, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("DMIC3 Boost", RT715_SET_GAIN_DMIC3_H,
			RT715_SET_GAIN_DMIC3_L, RT715_DIR_IN_SFT, 3, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("DMIC4 Boost", RT715_SET_GAIN_DMIC4_H,
			RT715_SET_GAIN_DMIC4_L, RT715_DIR_IN_SFT, 3, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("MIC1 Boost", RT715_SET_GAIN_MIC1_H,
			RT715_SET_GAIN_MIC1_L, RT715_DIR_IN_SFT, 3, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("MIC2 Boost", RT715_SET_GAIN_MIC2_H,
			RT715_SET_GAIN_MIC2_L, RT715_DIR_IN_SFT, 3, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("LINE1 Boost", RT715_SET_GAIN_LINE1_H,
			RT715_SET_GAIN_LINE1_L, RT715_DIR_IN_SFT, 3, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("LINE2 Boost", RT715_SET_GAIN_LINE2_H,
			RT715_SET_GAIN_LINE2_L, RT715_DIR_IN_SFT, 3, 0,
			rt715_set_amp_gain_get, rt715_set_amp_gain_put,
			mic_vol_tlv),
};

static int rt715_mux_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct rt715_priv *rt715 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg, val;
	int ret;

	/* nid = e->reg, vid = 0xf01 */
	reg = RT715_VERB_SET_CONNECT_SEL | e->reg;
	ret = regmap_read(rt715->regmap, reg, &val);
	if (ret < 0) {
		dev_err(component->dev, "%s: sdw read failed: %d\n",
			__func__, ret);
		return ret;
	}

	/*
	 * The first two indices of ADC Mux 24/25 are routed to the same
	 * hardware source. ie, ADC Mux 24 0/1 will both connect to MIC2.
	 * To have a unique set of inputs, we skip the index1 of the muxes.
	 */
	if ((e->reg == RT715_MUX_IN3 || e->reg == RT715_MUX_IN4) && (val > 0))
		val -= 1;
	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

static int rt715_mux_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
				snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct rt715_priv *rt715 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, val2 = 0, change, reg;
	int ret;

	if (item[0] >= e->items)
		return -EINVAL;

	/* Verb ID = 0x701h, nid = e->reg */
	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;

	reg = RT715_VERB_SET_CONNECT_SEL | e->reg;
	ret = regmap_read(rt715->regmap, reg, &val2);
	if (ret < 0) {
		dev_err(component->dev, "%s: sdw read failed: %d\n",
			__func__, ret);
		return ret;
	}

	if (val == val2)
		change = 0;
	else
		change = 1;

	if (change) {
		reg = RT715_VERB_SET_CONNECT_SEL | e->reg;
		regmap_write(rt715->regmap, reg, val);
	}

	snd_soc_dapm_mux_update_power(dapm, kcontrol,
						item[0], e, NULL);

	return change;
}

static const char * const adc_22_23_mux_text[] = {
	"MIC1",
	"MIC2",
	"LINE1",
	"LINE2",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"DMIC4",
};

/*
 * Due to mux design for nid 24 (MUX_IN3)/25 (MUX_IN4), connection index 0 and
 * 1 will be connected to the same dmic source, therefore we skip index 1 to
 * avoid misunderstanding on usage of dapm routing.
 */
static const unsigned int rt715_adc_24_25_values[] = {
	0,
	2,
	3,
	4,
	5,
};

static const char * const adc_24_mux_text[] = {
	"MIC2",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"DMIC4",
};

static const char * const adc_25_mux_text[] = {
	"MIC1",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"DMIC4",
};

static SOC_ENUM_SINGLE_DECL(
	rt715_adc22_enum, RT715_MUX_IN1, 0, adc_22_23_mux_text);

static SOC_ENUM_SINGLE_DECL(
	rt715_adc23_enum, RT715_MUX_IN2, 0, adc_22_23_mux_text);

static SOC_VALUE_ENUM_SINGLE_DECL(rt715_adc24_enum,
	RT715_MUX_IN3, 0, 0xf,
	adc_24_mux_text, rt715_adc_24_25_values);

static SOC_VALUE_ENUM_SINGLE_DECL(rt715_adc25_enum,
	RT715_MUX_IN4, 0, 0xf,
	adc_25_mux_text, rt715_adc_24_25_values);

static const struct snd_kcontrol_new rt715_adc22_mux =
	SOC_DAPM_ENUM_EXT("ADC 22 Mux", rt715_adc22_enum,
			rt715_mux_get, rt715_mux_put);

static const struct snd_kcontrol_new rt715_adc23_mux =
	SOC_DAPM_ENUM_EXT("ADC 23 Mux", rt715_adc23_enum,
			rt715_mux_get, rt715_mux_put);

static const struct snd_kcontrol_new rt715_adc24_mux =
	SOC_DAPM_ENUM_EXT("ADC 24 Mux", rt715_adc24_enum,
			rt715_mux_get, rt715_mux_put);

static const struct snd_kcontrol_new rt715_adc25_mux =
	SOC_DAPM_ENUM_EXT("ADC 25 Mux", rt715_adc25_enum,
			rt715_mux_get, rt715_mux_put);

static const struct snd_soc_dapm_widget rt715_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("DMIC3"),
	SND_SOC_DAPM_INPUT("DMIC4"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("LINE2"),
	SND_SOC_DAPM_ADC("ADC 07", NULL, RT715_SET_STREAMID_MIC_ADC, 4, 0),
	SND_SOC_DAPM_ADC("ADC 08", NULL, RT715_SET_STREAMID_LINE_ADC, 4, 0),
	SND_SOC_DAPM_ADC("ADC 09", NULL, RT715_SET_STREAMID_MIX_ADC, 4, 0),
	SND_SOC_DAPM_ADC("ADC 27", NULL, RT715_SET_STREAMID_MIX_ADC2, 4, 0),
	SND_SOC_DAPM_MUX("ADC 22 Mux", SND_SOC_NOPM, 0, 0,
		&rt715_adc22_mux),
	SND_SOC_DAPM_MUX("ADC 23 Mux", SND_SOC_NOPM, 0, 0,
		&rt715_adc23_mux),
	SND_SOC_DAPM_MUX("ADC 24 Mux", SND_SOC_NOPM, 0, 0,
		&rt715_adc24_mux),
	SND_SOC_DAPM_MUX("ADC 25 Mux", SND_SOC_NOPM, 0, 0,
		&rt715_adc25_mux),
	SND_SOC_DAPM_AIF_OUT("DP4TX", "DP4 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP6TX", "DP6 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt715_audio_map[] = {
	{"DP6TX", NULL, "ADC 09"},
	{"DP6TX", NULL, "ADC 08"},
	{"DP4TX", NULL, "ADC 07"},
	{"DP4TX", NULL, "ADC 27"},
	{"ADC 09", NULL, "ADC 22 Mux"},
	{"ADC 08", NULL, "ADC 23 Mux"},
	{"ADC 07", NULL, "ADC 24 Mux"},
	{"ADC 27", NULL, "ADC 25 Mux"},
	{"ADC 22 Mux", "MIC1", "MIC1"},
	{"ADC 22 Mux", "MIC2", "MIC2"},
	{"ADC 22 Mux", "LINE1", "LINE1"},
	{"ADC 22 Mux", "LINE2", "LINE2"},
	{"ADC 22 Mux", "DMIC1", "DMIC1"},
	{"ADC 22 Mux", "DMIC2", "DMIC2"},
	{"ADC 22 Mux", "DMIC3", "DMIC3"},
	{"ADC 22 Mux", "DMIC4", "DMIC4"},
	{"ADC 23 Mux", "MIC1", "MIC1"},
	{"ADC 23 Mux", "MIC2", "MIC2"},
	{"ADC 23 Mux", "LINE1", "LINE1"},
	{"ADC 23 Mux", "LINE2", "LINE2"},
	{"ADC 23 Mux", "DMIC1", "DMIC1"},
	{"ADC 23 Mux", "DMIC2", "DMIC2"},
	{"ADC 23 Mux", "DMIC3", "DMIC3"},
	{"ADC 23 Mux", "DMIC4", "DMIC4"},
	{"ADC 24 Mux", "MIC2", "MIC2"},
	{"ADC 24 Mux", "DMIC1", "DMIC1"},
	{"ADC 24 Mux", "DMIC2", "DMIC2"},
	{"ADC 24 Mux", "DMIC3", "DMIC3"},
	{"ADC 24 Mux", "DMIC4", "DMIC4"},
	{"ADC 25 Mux", "MIC1", "MIC1"},
	{"ADC 25 Mux", "DMIC1", "DMIC1"},
	{"ADC 25 Mux", "DMIC2", "DMIC2"},
	{"ADC 25 Mux", "DMIC3", "DMIC3"},
	{"ADC 25 Mux", "DMIC4", "DMIC4"},
};

static int rt715_set_bias_level(struct snd_soc_component *component,
				enum snd_soc_bias_level level)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct rt715_priv *rt715 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level == SND_SOC_BIAS_STANDBY) {
			regmap_write(rt715->regmap,
						RT715_SET_AUDIO_POWER_STATE,
						AC_PWRST_D0);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		regmap_write(rt715->regmap,
					RT715_SET_AUDIO_POWER_STATE,
					AC_PWRST_D3);
		break;

	default:
		break;
	}
	dapm->bias_level = level;
	return 0;
}

static const struct snd_soc_component_driver soc_codec_dev_rt715 = {
	.set_bias_level = rt715_set_bias_level,
	.controls = rt715_snd_controls,
	.num_controls = ARRAY_SIZE(rt715_snd_controls),
	.dapm_widgets = rt715_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt715_dapm_widgets),
	.dapm_routes = rt715_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt715_audio_map),
};

static int rt715_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{

	struct sdw_stream_data *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	stream->sdw_stream = (struct sdw_stream_runtime *)sdw_stream;

	/* Use tx_mask or rx_mask to configure stream tag and set dma_data */
	if (direction == SNDRV_PCM_STREAM_PLAYBACK)
		dai->playback_dma_data = stream;
	else
		dai->capture_dma_data = stream;

	return 0;
}

static void rt715_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)

{
	struct sdw_stream_data *stream;

	stream = snd_soc_dai_get_dma_data(dai, substream);
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(stream);
}

static int rt715_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt715_priv *rt715 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_data *stream;
	int retval, port, num_channels;
	unsigned int val = 0;

	stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!stream)
		return -EINVAL;

	if (!rt715->slave)
		return -EINVAL;

	switch (dai->id) {
	case RT715_AIF1:
		direction = SDW_DATA_DIR_TX;
		port = 6;
		rt715_index_write(rt715->regmap, RT715_SDW_INPUT_SEL, 0xa500);
		break;
	case RT715_AIF2:
		direction = SDW_DATA_DIR_TX;
		port = 4;
		rt715_index_write(rt715->regmap, RT715_SDW_INPUT_SEL, 0xa000);
		break;
	default:
		dev_err(component->dev, "Invalid DAI id %d\n", dai->id);
		return -EINVAL;
	}

	stream_config.frame_rate =  params_rate(params);
	stream_config.ch_count = params_channels(params);
	stream_config.bps = snd_pcm_format_width(params_format(params));
	stream_config.direction = direction;

	num_channels = params_channels(params);
	port_config.ch_mask = (1 << (num_channels)) - 1;
	port_config.num = port;

	retval = sdw_stream_add_slave(rt715->slave, &stream_config,
					&port_config, 1, stream->sdw_stream);
	if (retval) {
		dev_err(dai->dev, "Unable to configure port\n");
		return retval;
	}

	switch (params_rate(params)) {
	/* bit 14 0:48K 1:44.1K */
	/* bit 15 Stream Type 0:PCM 1:Non-PCM, should always be PCM */
	case 44100:
		val |= 0x40 << 8;
		break;
	case 48000:
		val |= 0x0 << 8;
		break;
	default:
		dev_err(component->dev, "Unsupported sample rate %d\n",
			params_rate(params));
		return -EINVAL;
	}

	if (params_channels(params) <= 16) {
		/* bit 3:0 Number of Channel */
		val |= (params_channels(params) - 1);
	} else {
		dev_err(component->dev, "Unsupported channels %d\n",
			params_channels(params));
		return -EINVAL;
	}

	switch (params_width(params)) {
	/* bit 6:4 Bits per Sample */
	case 8:
		break;
	case 16:
		val |= (0x1 << 4);
		break;
	case 20:
		val |= (0x2 << 4);
		break;
	case 24:
		val |= (0x3 << 4);
		break;
	case 32:
		val |= (0x4 << 4);
		break;
	default:
		return -EINVAL;
	}

	regmap_write(rt715->regmap, RT715_MIC_ADC_FORMAT_H, val);
	regmap_write(rt715->regmap, RT715_MIC_LINE_FORMAT_H, val);
	regmap_write(rt715->regmap, RT715_MIX_ADC_FORMAT_H, val);
	regmap_write(rt715->regmap, RT715_MIX_ADC2_FORMAT_H, val);

	return retval;
}

static int rt715_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt715_priv *rt715 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_data *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt715->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt715->slave, stream->sdw_stream);
	return 0;
}

#define RT715_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RT715_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static struct snd_soc_dai_ops rt715_ops = {
	.hw_params	= rt715_pcm_hw_params,
	.hw_free	= rt715_pcm_hw_free,
	.set_sdw_stream	= rt715_set_sdw_stream,
	.shutdown	= rt715_shutdown,
};

static struct snd_soc_dai_driver rt715_dai[] = {
	{
		.name = "rt715-aif1",
		.id = RT715_AIF1,
		.capture = {
			.stream_name = "DP6 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT715_STEREO_RATES,
			.formats = RT715_FORMATS,
		},
		.ops = &rt715_ops,
	},
	{
		.name = "rt715-aif2",
		.id = RT715_AIF2,
		.capture = {
			.stream_name = "DP4 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT715_STEREO_RATES,
			.formats = RT715_FORMATS,
		},
		.ops = &rt715_ops,
	},
};

/* Bus clock frequency */
#define RT715_CLK_FREQ_9600000HZ 9600000
#define RT715_CLK_FREQ_12000000HZ 12000000
#define RT715_CLK_FREQ_6000000HZ 6000000
#define RT715_CLK_FREQ_4800000HZ 4800000
#define RT715_CLK_FREQ_2400000HZ 2400000
#define RT715_CLK_FREQ_12288000HZ 12288000

int rt715_clock_config(struct device *dev)
{
	struct rt715_priv *rt715 = dev_get_drvdata(dev);
	unsigned int clk_freq, value;

	clk_freq = (rt715->params.curr_dr_freq >> 1);

	switch (clk_freq) {
	case RT715_CLK_FREQ_12000000HZ:
		value = 0x0;
		break;
	case RT715_CLK_FREQ_6000000HZ:
		value = 0x1;
		break;
	case RT715_CLK_FREQ_9600000HZ:
		value = 0x2;
		break;
	case RT715_CLK_FREQ_4800000HZ:
		value = 0x3;
		break;
	case RT715_CLK_FREQ_2400000HZ:
		value = 0x4;
		break;
	case RT715_CLK_FREQ_12288000HZ:
		value = 0x5;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(rt715->regmap, 0xe0, value);
	regmap_write(rt715->regmap, 0xf0, value);

	return 0;
}

int rt715_init(struct device *dev, struct regmap *sdw_regmap,
	struct regmap *regmap, struct sdw_slave *slave)
{
	struct rt715_priv *rt715;
	int ret;

	rt715 = devm_kzalloc(dev, sizeof(*rt715), GFP_KERNEL);
	if (!rt715)
		return -ENOMEM;

	dev_set_drvdata(dev, rt715);
	rt715->slave = slave;
	rt715->regmap = regmap;
	rt715->sdw_regmap = sdw_regmap;

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt715->hw_init = false;
	rt715->first_hw_init = false;

	ret = devm_snd_soc_register_component(dev,
						&soc_codec_dev_rt715,
						rt715_dai,
						ARRAY_SIZE(rt715_dai));

	return ret;
}

int rt715_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt715_priv *rt715 = dev_get_drvdata(dev);

	if (rt715->hw_init)
		return 0;

	/*
	 * PM runtime is only enabled when a Slave reports as Attached
	 */
	if (!rt715->first_hw_init) {
		/* set autosuspend parameters */
		pm_runtime_set_autosuspend_delay(&slave->dev, 3000);
		pm_runtime_use_autosuspend(&slave->dev);

		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);

		/* make sure the device does not suspend immediately */
		pm_runtime_mark_last_busy(&slave->dev);

		pm_runtime_enable(&slave->dev);
	}

	pm_runtime_get_noresume(&slave->dev);

	/* Mute nid=08h/09h */
	regmap_write(rt715->regmap, RT715_SET_GAIN_LINE_ADC_H, 0xb080);
	regmap_write(rt715->regmap, RT715_SET_GAIN_MIX_ADC_H, 0xb080);
	/* Mute nid=07h/27h */
	regmap_write(rt715->regmap, RT715_SET_GAIN_MIC_ADC_H, 0xb080);
	regmap_write(rt715->regmap, RT715_SET_GAIN_MIX_ADC2_H, 0xb080);

	/* Set Pin Widget */
	regmap_write(rt715->regmap, RT715_SET_PIN_DMIC1, 0x20);
	regmap_write(rt715->regmap, RT715_SET_PIN_DMIC2, 0x20);
	regmap_write(rt715->regmap, RT715_SET_PIN_DMIC3, 0x20);
	regmap_write(rt715->regmap, RT715_SET_PIN_DMIC4, 0x20);
	/* Set Converter Stream */
	regmap_write(rt715->regmap, RT715_SET_STREAMID_LINE_ADC, 0x10);
	regmap_write(rt715->regmap, RT715_SET_STREAMID_MIX_ADC, 0x10);
	regmap_write(rt715->regmap, RT715_SET_STREAMID_MIC_ADC, 0x10);
	regmap_write(rt715->regmap, RT715_SET_STREAMID_MIX_ADC2, 0x10);
	/* Set Configuration Default */
	regmap_write(rt715->regmap, RT715_SET_DMIC1_CONFIG_DEFAULT1, 0xd0);
	regmap_write(rt715->regmap, RT715_SET_DMIC1_CONFIG_DEFAULT2, 0x11);
	regmap_write(rt715->regmap, RT715_SET_DMIC1_CONFIG_DEFAULT3, 0xa1);
	regmap_write(rt715->regmap, RT715_SET_DMIC1_CONFIG_DEFAULT4, 0x81);
	regmap_write(rt715->regmap, RT715_SET_DMIC2_CONFIG_DEFAULT1, 0xd1);
	regmap_write(rt715->regmap, RT715_SET_DMIC2_CONFIG_DEFAULT2, 0x11);
	regmap_write(rt715->regmap, RT715_SET_DMIC2_CONFIG_DEFAULT3, 0xa1);
	regmap_write(rt715->regmap, RT715_SET_DMIC2_CONFIG_DEFAULT4, 0x81);
	regmap_write(rt715->regmap, RT715_SET_DMIC3_CONFIG_DEFAULT1, 0xd0);
	regmap_write(rt715->regmap, RT715_SET_DMIC3_CONFIG_DEFAULT2, 0x11);
	regmap_write(rt715->regmap, RT715_SET_DMIC3_CONFIG_DEFAULT3, 0xa1);
	regmap_write(rt715->regmap, RT715_SET_DMIC3_CONFIG_DEFAULT4, 0x81);
	regmap_write(rt715->regmap, RT715_SET_DMIC4_CONFIG_DEFAULT1, 0xd1);
	regmap_write(rt715->regmap, RT715_SET_DMIC4_CONFIG_DEFAULT2, 0x11);
	regmap_write(rt715->regmap, RT715_SET_DMIC4_CONFIG_DEFAULT3, 0xa1);
	regmap_write(rt715->regmap, RT715_SET_DMIC4_CONFIG_DEFAULT4, 0x81);

	/* Finish Initial Settings, set power to D3 */
	regmap_write(rt715->regmap, RT715_SET_AUDIO_POWER_STATE, AC_PWRST_D3);

	if (rt715->first_hw_init)
		regcache_mark_dirty(rt715->regmap);
	else
		rt715->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt715->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	return 0;
}

MODULE_DESCRIPTION("ASoC rt715 driver");
MODULE_DESCRIPTION("ASoC rt715 driver SDW");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL v2");
