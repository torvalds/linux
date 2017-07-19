/*
 * rt274.c  --  RT274 ALSA SoC audio codec driver
 *
 * Copyright 2017 Realtek Semiconductor Corp.
 * Author: Bard Liao <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/jack.h>
#include <linux/workqueue.h>

#include "rl6347a.h"
#include "rt274.h"

#define RT274_VENDOR_ID 0x10ec0274

struct rt274_priv {
	struct reg_default *index_cache;
	int index_cache_size;
	struct regmap *regmap;
	struct snd_soc_codec *codec;
	struct i2c_client *i2c;
	struct snd_soc_jack *jack;
	struct delayed_work jack_detect_work;
	int sys_clk;
	int clk_id;
	int fs;
	bool master;
};

static const struct reg_default rt274_index_def[] = {
	{ 0x00, 0x1004 },
	{ 0x01, 0xaaaa },
	{ 0x02, 0x88aa },
	{ 0x03, 0x0002 },
	{ 0x04, 0xaa09 },
	{ 0x05, 0x0700 },
	{ 0x06, 0x6110 },
	{ 0x07, 0x0200 },
	{ 0x08, 0xa807 },
	{ 0x09, 0x0021 },
	{ 0x0a, 0x7770 },
	{ 0x0b, 0x7770 },
	{ 0x0c, 0x002b },
	{ 0x0d, 0x2420 },
	{ 0x0e, 0x65c0 },
	{ 0x0f, 0x7770 },
	{ 0x10, 0x0420 },
	{ 0x11, 0x7418 },
	{ 0x12, 0x6bd0 },
	{ 0x13, 0x645f },
	{ 0x14, 0x0400 },
	{ 0x15, 0x8ccc },
	{ 0x16, 0x4c50 },
	{ 0x17, 0xff00 },
	{ 0x18, 0x0003 },
	{ 0x19, 0x2c11 },
	{ 0x1a, 0x830b },
	{ 0x1b, 0x4e4b },
	{ 0x1c, 0x0000 },
	{ 0x1d, 0x0000 },
	{ 0x1e, 0x0000 },
	{ 0x1f, 0x0000 },
	{ 0x20, 0x51ff },
	{ 0x21, 0x8000 },
	{ 0x22, 0x8f00 },
	{ 0x23, 0x88f4 },
	{ 0x24, 0x0000 },
	{ 0x25, 0x0000 },
	{ 0x26, 0x0000 },
	{ 0x27, 0x0000 },
	{ 0x28, 0x0000 },
	{ 0x29, 0x3000 },
	{ 0x2a, 0x0000 },
	{ 0x2b, 0x0000 },
	{ 0x2c, 0x0f00 },
	{ 0x2d, 0x100f },
	{ 0x2e, 0x2902 },
	{ 0x2f, 0xe280 },
	{ 0x30, 0x1000 },
	{ 0x31, 0x8400 },
	{ 0x32, 0x5aaa },
	{ 0x33, 0x8420 },
	{ 0x34, 0xa20c },
	{ 0x35, 0x096a },
	{ 0x36, 0x5757 },
	{ 0x37, 0xfe05 },
	{ 0x38, 0x4901 },
	{ 0x39, 0x110a },
	{ 0x3a, 0x0010 },
	{ 0x3b, 0x60d9 },
	{ 0x3c, 0xf214 },
	{ 0x3d, 0xc2ba },
	{ 0x3e, 0xa928 },
	{ 0x3f, 0x0000 },
	{ 0x40, 0x9800 },
	{ 0x41, 0x0000 },
	{ 0x42, 0x2000 },
	{ 0x43, 0x3d90 },
	{ 0x44, 0x4900 },
	{ 0x45, 0x5289 },
	{ 0x46, 0x0004 },
	{ 0x47, 0xa47a },
	{ 0x48, 0xd049 },
	{ 0x49, 0x0049 },
	{ 0x4a, 0xa83b },
	{ 0x4b, 0x0777 },
	{ 0x4c, 0x065c },
	{ 0x4d, 0x7fff },
	{ 0x4e, 0x7fff },
	{ 0x4f, 0x0000 },
	{ 0x50, 0x0000 },
	{ 0x51, 0x0000 },
	{ 0x52, 0xbf5f },
	{ 0x53, 0x3320 },
	{ 0x54, 0xcc00 },
	{ 0x55, 0x0000 },
	{ 0x56, 0x3f00 },
	{ 0x57, 0x0000 },
	{ 0x58, 0x0000 },
	{ 0x59, 0x0000 },
	{ 0x5a, 0x1300 },
	{ 0x5b, 0x005f },
	{ 0x5c, 0x0000 },
	{ 0x5d, 0x1001 },
	{ 0x5e, 0x1000 },
	{ 0x5f, 0x0000 },
	{ 0x60, 0x5554 },
	{ 0x61, 0xffc0 },
	{ 0x62, 0xa000 },
	{ 0x63, 0xd010 },
	{ 0x64, 0x0000 },
	{ 0x65, 0x3fb1 },
	{ 0x66, 0x1881 },
	{ 0x67, 0xc810 },
	{ 0x68, 0x2000 },
	{ 0x69, 0xfff0 },
	{ 0x6a, 0x0300 },
	{ 0x6b, 0x5060 },
	{ 0x6c, 0x0000 },
	{ 0x6d, 0x0000 },
	{ 0x6e, 0x0c25 },
	{ 0x6f, 0x0c0b },
	{ 0x70, 0x8000 },
	{ 0x71, 0x4008 },
	{ 0x72, 0x0000 },
	{ 0x73, 0x0800 },
	{ 0x74, 0xa28f },
	{ 0x75, 0xa050 },
	{ 0x76, 0x7fe8 },
	{ 0x77, 0xdb8c },
	{ 0x78, 0x0000 },
	{ 0x79, 0x0000 },
	{ 0x7a, 0x2a96 },
	{ 0x7b, 0x800f },
	{ 0x7c, 0x0200 },
	{ 0x7d, 0x1600 },
	{ 0x7e, 0x0000 },
	{ 0x7f, 0x0000 },
};
#define INDEX_CACHE_SIZE ARRAY_SIZE(rt274_index_def)

static const struct reg_default rt274_reg[] = {
	{ 0x00170500, 0x00000400 },
	{ 0x00220000, 0x00000031 },
	{ 0x00239000, 0x00000057 },
	{ 0x0023a000, 0x00000057 },
	{ 0x00270500, 0x00000400 },
	{ 0x00370500, 0x00000400 },
	{ 0x00870500, 0x00000400 },
	{ 0x00920000, 0x00000031 },
	{ 0x00935000, 0x00000097 },
	{ 0x00936000, 0x00000097 },
	{ 0x00970500, 0x00000400 },
	{ 0x00b37000, 0x00000400 },
	{ 0x00b37200, 0x00000400 },
	{ 0x00b37300, 0x00000400 },
	{ 0x00c37000, 0x00000400 },
	{ 0x00c37100, 0x00000400 },
	{ 0x01270500, 0x00000400 },
	{ 0x01370500, 0x00000400 },
	{ 0x01371f00, 0x411111f0 },
	{ 0x01937000, 0x00000000 },
	{ 0x01970500, 0x00000400 },
	{ 0x02050000, 0x0000001b },
	{ 0x02139000, 0x00000080 },
	{ 0x0213a000, 0x00000080 },
	{ 0x02170100, 0x00000001 },
	{ 0x02170500, 0x00000400 },
	{ 0x02170700, 0x00000000 },
	{ 0x02270100, 0x00000000 },
	{ 0x02370100, 0x00000000 },
	{ 0x01970700, 0x00000020 },
	{ 0x00830000, 0x00000097 },
	{ 0x00930000, 0x00000097 },
	{ 0x01270700, 0x00000000 },
};

static bool rt274_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0 ... 0xff:
	case RT274_GET_PARAM(AC_NODE_ROOT, AC_PAR_VENDOR_ID):
	case RT274_GET_HP_SENSE:
	case RT274_GET_MIC_SENSE:
	case RT274_PROC_COEF:
	case VERB_CMD(AC_VERB_GET_EAPD_BTLENABLE, RT274_MIC, 0):
	case VERB_CMD(AC_VERB_GET_EAPD_BTLENABLE, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_STREAM_FORMAT, RT274_DAC_OUT0, 0):
	case VERB_CMD(AC_VERB_GET_STREAM_FORMAT, RT274_DAC_OUT1, 0):
	case VERB_CMD(AC_VERB_GET_STREAM_FORMAT, RT274_ADC_IN1, 0):
	case VERB_CMD(AC_VERB_GET_STREAM_FORMAT, RT274_ADC_IN2, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_DAC_OUT0, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_DAC_OUT1, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_ADC_IN1, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_ADC_IN2, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_DMIC1, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_DMIC2, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_MIC, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_LINE1, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_LINE2, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_CONNECT_SEL, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_CONNECT_SEL, RT274_MIXER_IN1, 0):
	case VERB_CMD(AC_VERB_GET_CONNECT_SEL, RT274_MIXER_IN2, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_DMIC1, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_DMIC2, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_MIC, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_LINE1, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_LINE2, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_UNSOLICITED_RESPONSE, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_UNSOLICITED_RESPONSE, RT274_MIC, 0):
	case VERB_CMD(AC_VERB_GET_UNSOLICITED_RESPONSE, RT274_INLINE_CMD, 0):
		return true;
	default:
		return false;
	}


}

static bool rt274_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0 ... 0xff:
	case RT274_GET_PARAM(AC_NODE_ROOT, AC_PAR_VENDOR_ID):
	case RT274_GET_HP_SENSE:
	case RT274_GET_MIC_SENSE:
	case RT274_SET_AUDIO_POWER:
	case RT274_SET_HPO_POWER:
	case RT274_SET_DMIC1_POWER:
	case RT274_LOUT_MUX:
	case RT274_HPO_MUX:
	case RT274_ADC0_MUX:
	case RT274_ADC1_MUX:
	case RT274_SET_MIC:
	case RT274_SET_PIN_HPO:
	case RT274_SET_PIN_LOUT3:
	case RT274_SET_PIN_DMIC1:
	case RT274_SET_AMP_GAIN_HPO:
	case RT274_SET_DMIC2_DEFAULT:
	case RT274_DAC0L_GAIN:
	case RT274_DAC0R_GAIN:
	case RT274_DAC1L_GAIN:
	case RT274_DAC1R_GAIN:
	case RT274_ADCL_GAIN:
	case RT274_ADCR_GAIN:
	case RT274_MIC_GAIN:
	case RT274_HPOL_GAIN:
	case RT274_HPOR_GAIN:
	case RT274_LOUTL_GAIN:
	case RT274_LOUTR_GAIN:
	case RT274_DAC_FORMAT:
	case RT274_ADC_FORMAT:
	case RT274_COEF_INDEX:
	case RT274_PROC_COEF:
	case RT274_SET_AMP_GAIN_ADC_IN1:
	case RT274_SET_AMP_GAIN_ADC_IN2:
	case RT274_SET_POWER(RT274_DAC_OUT0):
	case RT274_SET_POWER(RT274_DAC_OUT1):
	case RT274_SET_POWER(RT274_ADC_IN1):
	case RT274_SET_POWER(RT274_ADC_IN2):
	case RT274_SET_POWER(RT274_DMIC2):
	case RT274_SET_POWER(RT274_MIC):
	case VERB_CMD(AC_VERB_GET_EAPD_BTLENABLE, RT274_MIC, 0):
	case VERB_CMD(AC_VERB_GET_EAPD_BTLENABLE, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_STREAM_FORMAT, RT274_DAC_OUT0, 0):
	case VERB_CMD(AC_VERB_GET_STREAM_FORMAT, RT274_DAC_OUT1, 0):
	case VERB_CMD(AC_VERB_GET_STREAM_FORMAT, RT274_ADC_IN1, 0):
	case VERB_CMD(AC_VERB_GET_STREAM_FORMAT, RT274_ADC_IN2, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_DAC_OUT0, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_DAC_OUT1, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_ADC_IN1, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_ADC_IN2, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_DMIC1, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_DMIC2, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_MIC, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_LINE1, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_LINE2, 0):
	case VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_CONNECT_SEL, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_CONNECT_SEL, RT274_MIXER_IN1, 0):
	case VERB_CMD(AC_VERB_GET_CONNECT_SEL, RT274_MIXER_IN2, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_DMIC1, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_DMIC2, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_MIC, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_LINE1, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_LINE2, 0):
	case VERB_CMD(AC_VERB_GET_PIN_WIDGET_CONTROL, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_UNSOLICITED_RESPONSE, RT274_HP_OUT, 0):
	case VERB_CMD(AC_VERB_GET_UNSOLICITED_RESPONSE, RT274_MIC, 0):
	case VERB_CMD(AC_VERB_GET_UNSOLICITED_RESPONSE, RT274_INLINE_CMD, 0):
		return true;
	default:
		return false;
	}
}

#ifdef CONFIG_PM
static void rt274_index_sync(struct snd_soc_codec *codec)
{
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);
	int i;

	for (i = 0; i < INDEX_CACHE_SIZE; i++) {
		snd_soc_write(codec, rt274->index_cache[i].reg,
				  rt274->index_cache[i].def);
	}
}
#endif

static int rt274_jack_detect(struct rt274_priv *rt274, bool *hp, bool *mic)
{
	unsigned int buf;

	*hp = false;
	*mic = false;

	if (!rt274->codec)
		return -EINVAL;

	regmap_read(rt274->regmap, RT274_GET_HP_SENSE, &buf);
	*hp = buf & 0x80000000;
	regmap_read(rt274->regmap, RT274_GET_MIC_SENSE, &buf);
	*mic = buf & 0x80000000;

	pr_debug("*hp = %d *mic = %d\n", *hp, *mic);

	return 0;
}

static void rt274_jack_detect_work(struct work_struct *work)
{
	struct rt274_priv *rt274 =
		container_of(work, struct rt274_priv, jack_detect_work.work);
	int status = 0;
	bool hp = false;
	bool mic = false;

	if (rt274_jack_detect(rt274, &hp, &mic) < 0)
		return;

	if (hp == true)
		status |= SND_JACK_HEADPHONE;

	if (mic == true)
		status |= SND_JACK_MICROPHONE;

	snd_soc_jack_report(rt274->jack, status,
		SND_JACK_MICROPHONE | SND_JACK_HEADPHONE);
}

static irqreturn_t rt274_irq(int irq, void *data);

static int rt274_mic_detect(struct snd_soc_codec *codec,
	struct snd_soc_jack *jack,  void *data)
{
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);

	if (jack == NULL) {
		/* Disable jack detection */
		regmap_update_bits(rt274->regmap, RT274_EAPD_GPIO_IRQ_CTRL,
					RT274_IRQ_EN, RT274_IRQ_DIS);

		return 0;
	}
	rt274->jack = jack;

	regmap_update_bits(rt274->regmap, RT274_EAPD_GPIO_IRQ_CTRL,
				RT274_IRQ_EN, RT274_IRQ_EN);

	/* Send an initial report */
	rt274_irq(0, rt274);

	return 0;
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -6350, 50, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, 0, 1000, 0);

static const struct snd_kcontrol_new rt274_snd_controls[] = {
	SOC_DOUBLE_R_TLV("DAC0 Playback Volume", RT274_DAC0L_GAIN,
			 RT274_DAC0R_GAIN, 0, 0x7f, 0, out_vol_tlv),
	SOC_DOUBLE_R_TLV("DAC1 Playback Volume", RT274_DAC1L_GAIN,
			 RT274_DAC1R_GAIN, 0, 0x7f, 0, out_vol_tlv),
	SOC_DOUBLE_R_TLV("ADC0 Capture Volume", RT274_ADCL_GAIN,
			    RT274_ADCR_GAIN, 0, 0x7f, 0, out_vol_tlv),
	SOC_DOUBLE_R("ADC0 Capture Switch", RT274_ADCL_GAIN,
			    RT274_ADCR_GAIN, RT274_MUTE_SFT, 1, 1),
	SOC_SINGLE_TLV("AMIC Volume", RT274_MIC_GAIN,
			    0, 0x3, 0, mic_vol_tlv),
};

static const struct snd_kcontrol_new hpol_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT274_HPOL_GAIN,
			RT274_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hpor_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT274_HPOR_GAIN,
			RT274_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new loutl_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT274_LOUTL_GAIN,
			RT274_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new loutr_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT274_LOUTR_GAIN,
			RT274_MUTE_SFT, 1, 1);

/* ADC0 source */
static const char * const rt274_adc_src[] = {
	"Mic", "Line1", "Line2", "Dmic"
};

static SOC_ENUM_SINGLE_DECL(
	rt274_adc0_enum, RT274_ADC0_MUX, RT274_ADC_SEL_SFT,
	rt274_adc_src);

static const struct snd_kcontrol_new rt274_adc0_mux =
	SOC_DAPM_ENUM("ADC 0 source", rt274_adc0_enum);

static SOC_ENUM_SINGLE_DECL(
	rt274_adc1_enum, RT274_ADC1_MUX, RT274_ADC_SEL_SFT,
	rt274_adc_src);

static const struct snd_kcontrol_new rt274_adc1_mux =
	SOC_DAPM_ENUM("ADC 1 source", rt274_adc1_enum);

static const char * const rt274_dac_src[] = {
	"DAC OUT0", "DAC OUT1"
};
/* HP-OUT source */
static SOC_ENUM_SINGLE_DECL(rt274_hpo_enum, RT274_HPO_MUX,
				0, rt274_dac_src);

static const struct snd_kcontrol_new rt274_hpo_mux =
SOC_DAPM_ENUM("HPO source", rt274_hpo_enum);

/* Line out source */
static SOC_ENUM_SINGLE_DECL(rt274_lout_enum, RT274_LOUT_MUX,
				0, rt274_dac_src);

static const struct snd_kcontrol_new rt274_lout_mux =
SOC_DAPM_ENUM("LOUT source", rt274_lout_enum);

static const struct snd_soc_dapm_widget rt274_dapm_widgets[] = {
	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC1 Pin"),
	SND_SOC_DAPM_INPUT("DMIC2 Pin"),
	SND_SOC_DAPM_INPUT("MIC"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("LINE2"),

	/* DMIC */
	SND_SOC_DAPM_PGA("DMIC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC2", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC 0", NULL, RT274_SET_STREAMID_ADC1, 4, 0),
	SND_SOC_DAPM_ADC("ADC 1", NULL, RT274_SET_STREAMID_ADC2, 4, 0),

	/* ADC Mux */
	SND_SOC_DAPM_MUX("ADC 0 Mux", SND_SOC_NOPM, 0, 0,
		&rt274_adc0_mux),
	SND_SOC_DAPM_MUX("ADC 1 Mux", SND_SOC_NOPM, 0, 0,
		&rt274_adc1_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RXL", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF1RXR", "AIF1 Playback", 1, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TXL", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TXR", "AIF1 Capture", 1, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RXL", "AIF1 Playback", 2, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RXR", "AIF1 Playback", 3, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TXL", "AIF1 Capture", 2, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TXR", "AIF1 Capture", 3, SND_SOC_NOPM, 0, 0),

	/* Output Side */
	/* DACs */
	SND_SOC_DAPM_DAC("DAC 0", NULL, RT274_SET_STREAMID_DAC0, 4, 0),
	SND_SOC_DAPM_DAC("DAC 1", NULL, RT274_SET_STREAMID_DAC1, 4, 0),

	/* Output Mux */
	SND_SOC_DAPM_MUX("HPO Mux", SND_SOC_NOPM, 0, 0, &rt274_hpo_mux),
	SND_SOC_DAPM_MUX("LOUT Mux", SND_SOC_NOPM, 0, 0, &rt274_lout_mux),

	SND_SOC_DAPM_SUPPLY("HP Power", RT274_SET_PIN_HPO,
		RT274_SET_PIN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LOUT Power", RT274_SET_PIN_LOUT3,
		RT274_SET_PIN_SFT, 0, NULL, 0),

	/* Output Mixer */
	SND_SOC_DAPM_PGA("DAC OUT0", SND_SOC_NOPM, 0, 0,
			NULL, 0),
	SND_SOC_DAPM_PGA("DAC OUT1", SND_SOC_NOPM, 0, 0,
			NULL, 0),

	/* Output Pga */
	SND_SOC_DAPM_SWITCH("LOUT L", SND_SOC_NOPM, 0, 0,
		&loutl_enable_control),
	SND_SOC_DAPM_SWITCH("LOUT R", SND_SOC_NOPM, 0, 0,
		&loutr_enable_control),
	SND_SOC_DAPM_SWITCH("HPO L", SND_SOC_NOPM, 0, 0,
		&hpol_enable_control),
	SND_SOC_DAPM_SWITCH("HPO R", SND_SOC_NOPM, 0, 0,
		&hpor_enable_control),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPO Pin"),
	SND_SOC_DAPM_OUTPUT("SPDIF"),
	SND_SOC_DAPM_OUTPUT("LINE3"),
};

static const struct snd_soc_dapm_route rt274_dapm_routes[] = {
	{"DMIC1", NULL, "DMIC1 Pin"},
	{"DMIC2", NULL, "DMIC2 Pin"},

	{"ADC 0 Mux", "Mic", "MIC"},
	{"ADC 0 Mux", "Dmic", "DMIC1"},
	{"ADC 0 Mux", "Line1", "LINE1"},
	{"ADC 0 Mux", "Line2", "LINE2"},
	{"ADC 1 Mux", "Mic", "MIC"},
	{"ADC 1 Mux", "Dmic", "DMIC2"},
	{"ADC 1 Mux", "Line1", "LINE1"},
	{"ADC 1 Mux", "Line2", "LINE2"},

	{"ADC 0", NULL, "ADC 0 Mux"},
	{"ADC 1", NULL, "ADC 1 Mux"},

	{"AIF1TXL", NULL, "ADC 0"},
	{"AIF1TXR", NULL, "ADC 0"},
	{"AIF2TXL", NULL, "ADC 1"},
	{"AIF2TXR", NULL, "ADC 1"},

	{"DAC 0", NULL, "AIF1RXL"},
	{"DAC 0", NULL, "AIF1RXR"},
	{"DAC 1", NULL, "AIF2RXL"},
	{"DAC 1", NULL, "AIF2RXR"},

	{"DAC OUT0", NULL, "DAC 0"},

	{"DAC OUT1", NULL, "DAC 1"},

	{"LOUT Mux", "DAC OUT0", "DAC OUT0"},
	{"LOUT Mux", "DAC OUT1", "DAC OUT1"},

	{"LOUT L", "Switch", "LOUT Mux"},
	{"LOUT R", "Switch", "LOUT Mux"},
	{"LOUT L", NULL, "LOUT Power"},
	{"LOUT R", NULL, "LOUT Power"},

	{"LINE3", NULL, "LOUT L"},
	{"LINE3", NULL, "LOUT R"},

	{"HPO Mux", "DAC OUT0", "DAC OUT0"},
	{"HPO Mux", "DAC OUT1", "DAC OUT1"},

	{"HPO L", "Switch", "HPO Mux"},
	{"HPO R", "Switch", "HPO Mux"},
	{"HPO L", NULL, "HP Power"},
	{"HPO R", NULL, "HP Power"},

	{"HPO Pin", NULL, "HPO L"},
	{"HPO Pin", NULL, "HPO R"},
};

static int rt274_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0;
	int d_len_code = 0, c_len_code = 0;

	switch (params_rate(params)) {
	/* bit 14 0:48K 1:44.1K */
	case 44100:
	case 48000:
		break;
	default:
		dev_err(codec->dev, "Unsupported sample rate %d\n",
					params_rate(params));
		return -EINVAL;
	}
	switch (rt274->sys_clk) {
	case 12288000:
	case 24576000:
		if (params_rate(params) != 48000) {
			dev_err(codec->dev, "Sys_clk is not matched (%d %d)\n",
					params_rate(params), rt274->sys_clk);
			return -EINVAL;
		}
		break;
	case 11289600:
	case 22579200:
		if (params_rate(params) != 44100) {
			dev_err(codec->dev, "Sys_clk is not matched (%d %d)\n",
					params_rate(params), rt274->sys_clk);
			return -EINVAL;
		}
		break;
	}

	if (params_channels(params) <= 16) {
		/* bit 3:0 Number of Channel */
		val |= (params_channels(params) - 1);
	} else {
		dev_err(codec->dev, "Unsupported channels %d\n",
					params_channels(params));
		return -EINVAL;
	}

	switch (params_width(params)) {
	/* bit 6:4 Bits per Sample */
	case 16:
		d_len_code = 0;
		c_len_code = 0;
		val |= (0x1 << 4);
		break;
	case 32:
		d_len_code = 2;
		c_len_code = 3;
		val |= (0x4 << 4);
		break;
	case 20:
		d_len_code = 1;
		c_len_code = 1;
		val |= (0x2 << 4);
		break;
	case 24:
		d_len_code = 2;
		c_len_code = 2;
		val |= (0x3 << 4);
		break;
	case 8:
		d_len_code = 3;
		c_len_code = 0;
		break;
	default:
		return -EINVAL;
	}

	if (rt274->master)
		c_len_code = 0x3;

	snd_soc_update_bits(codec,
		RT274_I2S_CTRL1, 0xc018, d_len_code << 3 | c_len_code << 14);
	dev_dbg(codec->dev, "format val = 0x%x\n", val);

	snd_soc_update_bits(codec, RT274_DAC_FORMAT, 0x407f, val);
	snd_soc_update_bits(codec, RT274_ADC_FORMAT, 0x407f, val);

	return 0;
}

static int rt274_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL1, RT274_I2S_MODE_MASK, RT274_I2S_MODE_M);
		rt274->master = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL1, RT274_I2S_MODE_MASK, RT274_I2S_MODE_S);
		rt274->master = false;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		snd_soc_update_bits(codec, RT274_I2S_CTRL1,
					RT274_I2S_FMT_MASK, RT274_I2S_FMT_I2S);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		snd_soc_update_bits(codec, RT274_I2S_CTRL1,
					RT274_I2S_FMT_MASK, RT274_I2S_FMT_LJ);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		snd_soc_update_bits(codec, RT274_I2S_CTRL1,
					RT274_I2S_FMT_MASK, RT274_I2S_FMT_PCMA);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		snd_soc_update_bits(codec, RT274_I2S_CTRL1,
					RT274_I2S_FMT_MASK, RT274_I2S_FMT_PCMB);
		break;
	default:
		return -EINVAL;
	}
	/* bit 15 Stream Type 0:PCM 1:Non-PCM */
	snd_soc_update_bits(codec, RT274_DAC_FORMAT, 0x8000, 0);
	snd_soc_update_bits(codec, RT274_ADC_FORMAT, 0x8000, 0);

	return 0;
}

static int rt274_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);

	switch (source) {
	case RT274_PLL2_S_MCLK:
		snd_soc_update_bits(codec, RT274_PLL2_CTRL,
				RT274_PLL2_SRC_MASK, RT274_PLL2_SRC_MCLK);
		break;
	default:
		dev_warn(codec->dev, "invalid pll source, use BCLK\n");
	case RT274_PLL2_S_BCLK:
		snd_soc_update_bits(codec, RT274_PLL2_CTRL,
				RT274_PLL2_SRC_MASK, RT274_PLL2_SRC_BCLK);
		break;
	}

	if (source == RT274_PLL2_S_BCLK) {
		snd_soc_update_bits(codec, RT274_MCLK_CTRL,
				(0x3 << 12), (0x3 << 12));
		switch (rt274->fs) {
		case 50:
			snd_soc_write(codec, 0x7a, 0xaab6);
			snd_soc_write(codec, 0x7b, 0x0301);
			snd_soc_write(codec, 0x7c, 0x04fe);
			break;
		case 64:
			snd_soc_write(codec, 0x7a, 0xaa96);
			snd_soc_write(codec, 0x7b, 0x8003);
			snd_soc_write(codec, 0x7c, 0x081e);
			break;
		case 128:
			snd_soc_write(codec, 0x7a, 0xaa96);
			snd_soc_write(codec, 0x7b, 0x8003);
			snd_soc_write(codec, 0x7c, 0x080e);
			break;
		default:
			dev_warn(codec->dev, "invalid freq_in, assume 4.8M\n");
		case 100:
			snd_soc_write(codec, 0x7a, 0xaab6);
			snd_soc_write(codec, 0x7b, 0x0301);
			snd_soc_write(codec, 0x7c, 0x047e);
			break;
		}
	}

	return 0;
}

static int rt274_set_dai_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);
	unsigned int clk_src, mclk_en;

	dev_dbg(codec->dev, "%s freq=%d\n", __func__, freq);

	switch (clk_id) {
	case RT274_SCLK_S_MCLK:
		mclk_en = RT274_MCLK_MODE_EN;
		clk_src = RT274_CLK_SRC_MCLK;
		break;
	case RT274_SCLK_S_PLL1:
		mclk_en = RT274_MCLK_MODE_DIS;
		clk_src = RT274_CLK_SRC_MCLK;
		break;
	case RT274_SCLK_S_PLL2:
		mclk_en = RT274_MCLK_MODE_EN;
		clk_src = RT274_CLK_SRC_PLL2;
		break;
	default:
		mclk_en = RT274_MCLK_MODE_DIS;
		clk_src = RT274_CLK_SRC_MCLK;
		dev_warn(codec->dev, "invalid sysclk source, use PLL1\n");
		break;
	}
	snd_soc_update_bits(codec, RT274_MCLK_CTRL,
			RT274_MCLK_MODE_MASK, mclk_en);
	snd_soc_update_bits(codec, RT274_CLK_CTRL,
			RT274_CLK_SRC_MASK, clk_src);

	switch (freq) {
	case 19200000:
		if (clk_id == RT274_SCLK_S_MCLK) {
			dev_err(codec->dev, "Should not use MCLK\n");
			return -EINVAL;
		}
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL2, 0x40, 0x40);
		break;
	case 24000000:
		if (clk_id == RT274_SCLK_S_MCLK) {
			dev_err(codec->dev, "Should not use MCLK\n");
			return -EINVAL;
		}
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL2, 0x40, 0x0);
		break;
	case 12288000:
	case 11289600:
		snd_soc_update_bits(codec,
			RT274_MCLK_CTRL, 0x1fcf, 0x0008);
		break;
	case 24576000:
	case 22579200:
		snd_soc_update_bits(codec,
			RT274_MCLK_CTRL, 0x1fcf, 0x1543);
		break;
	default:
		dev_err(codec->dev, "Unsupported system clock\n");
		return -EINVAL;
	}

	rt274->sys_clk = freq;
	rt274->clk_id = clk_id;

	return 0;
}

static int rt274_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s ratio=%d\n", __func__, ratio);
	rt274->fs = ratio;
	if ((ratio / 50) == 0)
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL1, 0x1000, 0x1000);
	else
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL1, 0x1000, 0x0);


	return 0;
}

static int rt274_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			unsigned int rx_mask, int slots, int slot_width)

{
	struct snd_soc_codec *codec = dai->codec;

	if (rx_mask || tx_mask) {
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL1, RT274_TDM_EN, RT274_TDM_EN);
	} else {
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL1, RT274_TDM_EN, RT274_TDM_DIS);
		return 0;
	}

	switch (slots) {
	case 4:
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL1, RT274_TDM_CH_NUM, RT274_TDM_4CH);
		break;
	case 2:
		snd_soc_update_bits(codec,
			RT274_I2S_CTRL1, RT274_TDM_CH_NUM, RT274_TDM_2CH);
		break;
	default:
		dev_err(codec->dev,
			"Support 2 or 4 slots TDM only\n");
		return -EINVAL;
	}

	return 0;
}

static int rt274_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (SND_SOC_BIAS_STANDBY ==
			snd_soc_codec_get_bias_level(codec)) {
			snd_soc_write(codec,
				RT274_SET_AUDIO_POWER, AC_PWRST_D0);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		snd_soc_write(codec,
			RT274_SET_AUDIO_POWER, AC_PWRST_D3);
		break;

	default:
		break;
	}

	return 0;
}

static irqreturn_t rt274_irq(int irq, void *data)
{
	struct rt274_priv *rt274 = data;
	bool hp = false;
	bool mic = false;
	int ret, status = 0;

	/* Clear IRQ */
	regmap_update_bits(rt274->regmap, RT274_EAPD_GPIO_IRQ_CTRL,
				RT274_IRQ_CLR, RT274_IRQ_CLR);

	ret = rt274_jack_detect(rt274, &hp, &mic);

	if (ret == 0) {
		if (hp == true)
			status |= SND_JACK_HEADPHONE;

		if (mic == true)
			status |= SND_JACK_MICROPHONE;

		snd_soc_jack_report(rt274->jack, status,
			SND_JACK_MICROPHONE | SND_JACK_HEADPHONE);

		pm_wakeup_event(&rt274->i2c->dev, 300);
	}

	return IRQ_HANDLED;
}

static int rt274_probe(struct snd_soc_codec *codec)
{
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);

	rt274->codec = codec;

	if (rt274->i2c->irq) {
		INIT_DELAYED_WORK(&rt274->jack_detect_work,
					rt274_jack_detect_work);
		schedule_delayed_work(&rt274->jack_detect_work,
					msecs_to_jiffies(1250));
	}

	return 0;
}

static int rt274_remove(struct snd_soc_codec *codec)
{
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);

	cancel_delayed_work_sync(&rt274->jack_detect_work);

	return 0;
}

#ifdef CONFIG_PM
static int rt274_suspend(struct snd_soc_codec *codec)
{
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt274->regmap, true);
	regcache_mark_dirty(rt274->regmap);

	return 0;
}

static int rt274_resume(struct snd_soc_codec *codec)
{
	struct rt274_priv *rt274 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt274->regmap, false);
	rt274_index_sync(codec);
	regcache_sync(rt274->regmap);

	return 0;
}
#else
#define rt274_suspend NULL
#define rt274_resume NULL
#endif

#define RT274_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RT274_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt274_aif_dai_ops = {
	.hw_params = rt274_hw_params,
	.set_fmt = rt274_set_dai_fmt,
	.set_sysclk = rt274_set_dai_sysclk,
	.set_pll = rt274_set_dai_pll,
	.set_bclk_ratio = rt274_set_bclk_ratio,
	.set_tdm_slot = rt274_set_tdm_slot,
};

static struct snd_soc_dai_driver rt274_dai[] = {
	{
		.name = "rt274-aif1",
		.id = RT274_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT274_STEREO_RATES,
			.formats = RT274_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT274_STEREO_RATES,
			.formats = RT274_FORMATS,
		},
		.ops = &rt274_aif_dai_ops,
		.symmetric_rates = 1,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt274 = {
	.probe = rt274_probe,
	.remove = rt274_remove,
	.suspend = rt274_suspend,
	.resume = rt274_resume,
	.set_bias_level = rt274_set_bias_level,
	.idle_bias_off = true,
	.component_driver = {
		.controls		= rt274_snd_controls,
		.num_controls		= ARRAY_SIZE(rt274_snd_controls),
		.dapm_widgets		= rt274_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(rt274_dapm_widgets),
		.dapm_routes		= rt274_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(rt274_dapm_routes),
	},
	.set_jack = rt274_mic_detect,
};

static const struct regmap_config rt274_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
	.max_register = 0x05bfffff,
	.volatile_reg = rt274_volatile_register,
	.readable_reg = rt274_readable_register,
	.reg_write = rl6347a_hw_write,
	.reg_read = rl6347a_hw_read,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt274_reg,
	.num_reg_defaults = ARRAY_SIZE(rt274_reg),
};

#ifdef CONFIG_OF
static const struct of_device_id rt274_of_match[] = {
	{.compatible = "realtek,rt274"},
	{},
};
MODULE_DEVICE_TABLE(of, rt274_of_match);
#endif

static const struct i2c_device_id rt274_i2c_id[] = {
	{"rt274", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rt274_i2c_id);

static const struct acpi_device_id rt274_acpi_match[] = {
	{ "10EC0274", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, rt274_acpi_match);

static int rt274_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct rt274_priv *rt274;

	int ret;
	unsigned int val;

	rt274 = devm_kzalloc(&i2c->dev,	sizeof(*rt274),
				GFP_KERNEL);
	if (rt274 == NULL)
		return -ENOMEM;

	rt274->regmap = devm_regmap_init(&i2c->dev, NULL, i2c, &rt274_regmap);
	if (IS_ERR(rt274->regmap)) {
		ret = PTR_ERR(rt274->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt274->regmap,
		RT274_GET_PARAM(AC_NODE_ROOT, AC_PAR_VENDOR_ID), &val);
	if (val != RT274_VENDOR_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %#x is not rt274\n", val);
		return -ENODEV;
	}

	rt274->index_cache = devm_kmemdup(&i2c->dev, rt274_index_def,
					  sizeof(rt274_index_def), GFP_KERNEL);
	if (!rt274->index_cache)
		return -ENOMEM;

	rt274->index_cache_size = INDEX_CACHE_SIZE;
	rt274->i2c = i2c;
	i2c_set_clientdata(i2c, rt274);

	/* reset codec */
	regmap_write(rt274->regmap, RT274_RESET, 0);
	regmap_update_bits(rt274->regmap, 0x1a, 0x4000, 0x4000);

	/* Set Pad PDB is floating */
	regmap_update_bits(rt274->regmap, RT274_PAD_CTRL12, 0x3, 0x0);
	regmap_write(rt274->regmap, RT274_COEF5b_INDEX, 0x01);
	regmap_write(rt274->regmap, RT274_COEF5b_COEF, 0x8540);
	regmap_update_bits(rt274->regmap, 0x6f, 0x0100, 0x0100);
	/* Combo jack auto detect */
	regmap_write(rt274->regmap, 0x4a, 0x201b);
	/* Aux mode off */
	regmap_update_bits(rt274->regmap, 0x6f, 0x3000, 0x2000);
	/* HP DC Calibration */
	regmap_update_bits(rt274->regmap, 0x6f, 0xf, 0x0);
	/* Set NID=58h.Index 00h [15]= 1b; */
	regmap_write(rt274->regmap, RT274_COEF58_INDEX, 0x00);
	regmap_write(rt274->regmap, RT274_COEF58_COEF, 0xb888);
	msleep(500);
	regmap_update_bits(rt274->regmap, 0x6f, 0xf, 0xb);
	regmap_write(rt274->regmap, RT274_COEF58_INDEX, 0x00);
	regmap_write(rt274->regmap, RT274_COEF58_COEF, 0x3888);
	/* Set pin widget */
	regmap_write(rt274->regmap, RT274_SET_PIN_HPO, 0x40);
	regmap_write(rt274->regmap, RT274_SET_PIN_LOUT3, 0x40);
	regmap_write(rt274->regmap, RT274_SET_MIC, 0x20);
	regmap_write(rt274->regmap, RT274_SET_PIN_DMIC1, 0x20);

	regmap_update_bits(rt274->regmap, RT274_I2S_CTRL2, 0xc004, 0x4004);
	regmap_update_bits(rt274->regmap, RT274_EAPD_GPIO_IRQ_CTRL,
				RT274_GPI2_SEL_MASK, RT274_GPI2_SEL_DMIC_CLK);

	/* jack detection */
	regmap_write(rt274->regmap, RT274_UNSOLICITED_HP_OUT, 0x81);
	regmap_write(rt274->regmap, RT274_UNSOLICITED_MIC, 0x82);

	if (rt274->i2c->irq) {
		ret = request_threaded_irq(rt274->i2c->irq, NULL, rt274_irq,
			IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "rt274", rt274);
		if (ret != 0) {
			dev_err(&i2c->dev,
				"Failed to reguest IRQ: %d\n", ret);
			return ret;
		}
	}

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt274,
				     rt274_dai, ARRAY_SIZE(rt274_dai));

	return ret;
}

static int rt274_i2c_remove(struct i2c_client *i2c)
{
	struct rt274_priv *rt274 = i2c_get_clientdata(i2c);

	if (i2c->irq)
		free_irq(i2c->irq, rt274);
	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}


static struct i2c_driver rt274_i2c_driver = {
	.driver = {
		   .name = "rt274",
		   .acpi_match_table = ACPI_PTR(rt274_acpi_match),
#ifdef CONFIG_OF
		   .of_match_table = of_match_ptr(rt274_of_match),
#endif
		   },
	.probe = rt274_i2c_probe,
	.remove = rt274_i2c_remove,
	.id_table = rt274_i2c_id,
};

module_i2c_driver(rt274_i2c_driver);

MODULE_DESCRIPTION("ASoC RT274 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL v2");
