/*
 * es8396.c  --  ES8396 ALSA SoC Audio Codec
 *
 * Copyright (C) 2014 Everest Semiconductor Co., Ltd
 *
 * Authors:  David Yang(yangxiaohua@everest-semi.com)
 *
 *
 * Based on alc5632.c by David Yang(yangxiaohua@everest-semi.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/stddef.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include "es8396.h"

#define INVALID_GPIO -1

static struct snd_soc_codec *tron_codec;
static int es8396_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level);

/*
 * ES8396 register cache
 */
static struct reg_default es8396_reg_defaults[] = {
	{0x00, 0x00},
	{0x01, 0x00},
	{0x02, 0x80},
	{0x03, 0x00},
	{0x04, 0x00},
	{0x05, 0x00},
	{0x06, 0x00},
	{0x07, 0x00},
	{0x08, 0x50},
	{0x09, 0x04},
	{0x0a, 0x00},
	{0x0b, 0x20},
	{0x0c, 0x20},
	{0x0d, 0x00},
	{0x0e, 0x00},
	{0x0f, 0x00},

	{0x10, 0x00},
	{0x11, 0x00},
	{0x12, 0x00},
	{0x13, 0x00},
	{0x14, 0x00},
	{0x15, 0x00},
	{0x16, 0x00},
	{0x17, 0x00},
	{0x18, 0x00},
	{0x19, 0x00},
	{0x1a, 0x00},
	{0x1b, 0x00},
	{0x1c, 0x00},
	{0x1d, 0x00},
	{0x1e, 0x00},
	{0x1f, 0x00},

	{0x20, 0x00},
	{0x21, 0x00},
	{0x22, 0x00},
	{0x23, 0x00},
	{0x24, 0x00},
	{0x25, 0x00},
	{0x26, 0x11},
	{0x27, 0x00},
	{0x28, 0x00},
	{0x29, 0x04},
	{0x2a, 0x00},
	{0x2b, 0x33},
	{0x2c, 0x00},
	{0x2d, 0x04},
	{0x2e, 0x00},
	{0x2f, 0x11},

	{0x30, 0x00},
	{0x31, 0x04},
	{0x32, 0x00},
	{0x33, 0x11},
	{0x34, 0x00},
	{0x35, 0x04},
	{0x36, 0x00},
	{0x37, 0x11},
	{0x38, 0x00},
	{0x39, 0x04},
	{0x3a, 0x44},
	{0x3b, 0x00},
	{0x3c, 0x20},
	{0x3d, 0x00},
	{0x3e, 0x00},
	{0x3f, 0x00},

	{0x40, 0x08},
	{0x41, 0x88},
	{0x42, 0x20},
	{0x43, 0x82},
	{0x44, 0x03},
	{0x45, 0xa0},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x01},
	{0x49, 0x01},
	{0x4a, 0x80},
	{0x4b, 0x80},
	{0x4c, 0x80},
	{0x4d, 0x80},
	{0x4e, 0x84},
	{0x4f, 0x84},

	{0x50, 0x84},
	{0x51, 0x84},
	{0x52, 0xc0},
	{0x53, 0x80},
	{0x54, 0x00},
	{0x55, 0x00},
	{0x56, 0xc0},
	{0x57, 0xc0},
	{0x58, 0x0b},
	{0x59, 0x32},
	{0x5a, 0x00},
	{0x5b, 0x1f},
	{0x5c, 0xc0},
	{0x5d, 0x00},
	{0x5e, 0xfc},
	{0x5f, 0x02},

	{0x60, 0x00},
	{0x61, 0x00},
	{0x62, 0x00},
	{0x63, 0x00},
	{0x64, 0x00},
	{0x65, 0x00},
	{0x66, 0x80},
	{0x67, 0x00},
	{0x68, 0x00},
	{0x69, 0x00},
	{0x6a, 0xc0},
	{0x6b, 0xc0},
	{0x6c, 0x00},
	{0x6d, 0x00},
	{0x6e, 0xc8},
	{0x6f, 0x00},

	{0x70, 0xd3},
	{0x71, 0x90},
	{0x72, 0x00},
	{0x73, 0x00},
	{0x74, 0x88},
	{0x75, 0xc1},
	{0x76, 0x00},
	{0x77, 0x00},
	{0x7a, 0x00},
	{0x7b, 0x00},
};

static u8 es8396_equalizer_lpf_bt_incall[] = {
	0x6D, 0x27, 0x64, 0x09, 0x4C, 0xA3, 0x53, 0x07, 0x6D, 0x27, 0x64, 0x09,
	0x8D, 0xE5, 0x23, 0x00, 0x2A, 0xA3, 0x4A, 0x22,
	0x6D, 0x27, 0x64, 0x09, 0x4C, 0xA3, 0x53, 0x07, 0x6D, 0x27, 0x64, 0x09,
	0x8D, 0xE5, 0x23, 0x00, 0x2A, 0xA3, 0x4A, 0x22,
	0x6D, 0x27, 0x64, 0x09, 0x4C, 0xA3, 0x53, 0x07, 0x6D, 0x27, 0x64, 0x09,
	0x8D, 0xE5, 0x23, 0x00, 0x2A, 0xA3, 0x4A, 0x22,
};

struct sp_config {
	u8 spc, mmcc, spfs;
	u32 srate;
	u8 lrcdiv;
	u8 sclkdiv;
};

/* codec private data */
struct es8396_private {
	struct sp_config config[3];
	struct regmap *regmap;
	u8 sysclk[3];
	u32 mclk[3];
	struct clk *mclk_clock;

	/* platform dependant DVDD voltage configuration */
	u8 dvdd_pwr_vol;

	/* platform dependant CLASS D Mono mode configuration */
	bool spkmono;
	/* platform dependant earpiece mode configuration */
	bool earpiece;
	/* platform dependant monon/p differential mode configuration */
	bool monoin_differential;
	/* platform dependant lout/rout differential mode configuration */
	bool lno_differential;

	/* platform dependant analog ldo level configuration */
	u8 ana_ldo_lvl;
	/* platform dependant speaker ldo level configuration */
	u8 spk_ldo_lvl;
	/* platform dependant mic bias voltage configuration */
	u8 mic_bias_lvl;
	u8 dmic_amic;

	bool jackdet_enable;

	u8 gpio_int_pol;

	int shutdwn_delay;
	int pon_delay;
	int spk_ctl_gpio;
	bool spk_gpio_level;
	int lineout_ctl_gpio;
	bool lineout_gpio_level;

	bool calibrate;
	u8 output_device_selected;
	u8 aif1_select;
	u8 aif2_select;
	/*
	 * Add a delay work-quenue, to debug DC calibration
	 */
	struct mutex adc_depop_mlock;
	struct delayed_work adc_depop_work;

	/* for playback pop noise */
	struct mutex pcm_depop_mlock;
	struct delayed_work pcm_pop_work;
	/* for playback pop noise */
	struct mutex pcm_shutdown_depop_mlock;
	struct delayed_work pcm_shutdown_depop_work;

	/* for voice pop noise */
	struct mutex voice_depop_mlock;
	struct delayed_work voice_pop_work;
	/* for voice pop noise */
	struct mutex voice_shutdown_depop_mlock;
	struct delayed_work voice_shutdown_depop_work;

	/* for hp calibration */
	struct mutex init_cali_mlock;
	struct delayed_work init_cali_work;
	int pcm_pop_work_retry;
};

static bool es8396_valid_micbias(u8 micbias)
{
	switch (micbias) {
	case MICBIAS_3V:
	case MICBIAS_2_8V:
	case MICBIAS_2_5V:
	case MICBIAS_2_3V:
	case MICBIAS_2V:
	case MICBIAS_1_5V:
		return true;
	default:
		break;
	}
	return false;
}

static bool es8396_valid_analdo(u8 ldolvl)
{
	switch (ldolvl) {
	case ANA_LDO_3V:
	case ANA_LDO_2_9V:
	case ANA_LDO_2_8V:
	case ANA_LDO_2_7V:
	case ANA_LDO_2_4V:
	case ANA_LDO_2_3V:
	case ANA_LDO_2_2V:
	case ANA_LDO_2_1V:
		return true;
	default:
		break;
	}
	return false;
}

static bool es8396_valid_spkldo(u8 ldolvl)
{
	switch (ldolvl) {
	case SPK_LDO_3_3V:
	case SPK_LDO_3_2V:
	case SPK_LDO_3V:
	case SPK_LDO_2_9V:
	case SPK_LDO_2_8V:
	case SPK_LDO_2_6V:
	case SPK_LDO_2_5V:
	case SPK_LDO_2_4V:
		return true;
	default:
		break;
	}
	return false;
}

/*
static int es8396_volatile_register(struct snd_soc_codec *codec,
				    unsigned int reg)
{
	switch (reg) {
	case ES8396_SHARED_ADDR_REG1D:
	case ES8396_SHARED_DATA_REG1E:
	case ES8396_GPIO_STA_REG17:
	case ES8396_CPHP_HPL_ICAL_REG3E:
	case ES8396_CPHP_HPR_ICAL_REG3F:
	case ES8396_RESET_REG00:
	case ES8396_PLL_CTRL_1_REG02:
		return true;
	default:
		return false;
	}
}

static int es8396_readable_register(struct snd_soc_codec *codec,
				    unsigned int reg)
{
	if (reg < 0x80)
		return true;

	return false;
}
*/

static void pcm_shutdown_depop_events(struct work_struct *work)
{
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);

	mutex_lock(&es8396->pcm_shutdown_depop_mlock);
	snd_soc_update_bits(tron_codec, ES8396_SDP1_IN_FMT_REG1F, 0x40,
			    0x40);
	es8396->aif1_select &= 0xfe;
	mutex_unlock(&es8396->pcm_shutdown_depop_mlock);
}

static void voice_shutdown_depop_events(struct work_struct *work)
{
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);

	mutex_lock(&es8396->voice_shutdown_depop_mlock);
	snd_soc_update_bits(tron_codec, ES8396_SDP2_IN_FMT_REG22,
			    0x7F, 0x53);
	es8396->aif2_select &= 0xfe;
	if ((es8396->aif1_select) != 0) {
		snd_soc_write(tron_codec, 0x1A, 0x00);
		snd_soc_write(tron_codec, 0x67, 0x00);
		snd_soc_write(tron_codec, 0x69, 0x00);
		snd_soc_write(tron_codec, 0x66, 0x00);
	}
	mutex_unlock(&es8396->voice_shutdown_depop_mlock);
}

static void init_cali_work_events(struct work_struct *work)
{
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);

	mutex_lock(&es8396->init_cali_mlock);
	pr_debug("init_cali_work_events\n");
	if ((es8396->pcm_pop_work_retry) > 0) {
		es8396->pcm_pop_work_retry--;
		pr_debug("Enter into %s  %d\n", __func__, __LINE__);
		snd_soc_write(tron_codec, ES8396_DAC_OFFSET_CALI_REG6F, 0x83);
		if (es8396->pcm_pop_work_retry) {
			schedule_delayed_work(&es8396->init_cali_work,
					      msecs_to_jiffies(100));
		}
	}
	snd_soc_write(tron_codec, ES8396_ADC_ANALOG_CTRL_REG5E, 0x3C);

	/* use line  out */
	msleep(100);
	snd_soc_write(tron_codec, 0x4E, 0x80);
	snd_soc_write(tron_codec, 0x4F, 0x81);
	snd_soc_write(tron_codec, 0x4A, 0x60);
	snd_soc_write(tron_codec, 0x4B, 0x60);

	mutex_unlock(&es8396->init_cali_mlock);
}

static void voice_pop_work_events(struct work_struct *work)
{
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);
	int i;

	mutex_lock(&es8396->voice_depop_mlock);
	pr_debug("voice_pop_work_events\n");
	/*
	 * set the clock source to pll out
	 * set divider for voice playback
	 * set Equalizer
	 * set DAC source from equalizer
	 */
	snd_soc_update_bits(tron_codec, ES8396_SDP2_IN_FMT_REG22,
			    0x3F, 0x13);
	snd_soc_update_bits(tron_codec, ES8396_SDP2_OUT_FMT_REG23,
			    0x7F, 0x33);
	/* use line out */
	snd_soc_write(tron_codec, 0x4E, 0x80);
	snd_soc_write(tron_codec, 0x4F, 0x81);
	snd_soc_write(tron_codec, 0x4A, 0x60);
	snd_soc_write(tron_codec, 0x4B, 0x60);
	snd_soc_write(tron_codec, 0x1A, 0x40);	/* Enable HPOUT */

	/* unmute dac */
	snd_soc_write(tron_codec, 0x66, 0x00);

	for (i = 0; i < 120; i = i + 2) {
		snd_soc_write(tron_codec, 0x6A, 120 - i);
		snd_soc_write(tron_codec, 0x6B, 120 - i);
		usleep_range(100, 200);
	}

	mutex_unlock(&es8396->voice_depop_mlock);
}

static void pcm_pop_work_events(struct work_struct *work)
{
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);
	int i;
	mutex_lock(&es8396->pcm_depop_mlock);
	pr_debug("pcm_pop_work_events\n");

	snd_soc_write(tron_codec, ES8396_SYS_VMID_REF_REG71,
					      0xFC);

	/* use line out */
	snd_soc_write(tron_codec, 0x4E, 0x80);
	snd_soc_write(tron_codec, 0x4F, 0x81);
	snd_soc_write(tron_codec, 0x4A, 0x60);
	snd_soc_write(tron_codec, 0x4B, 0x60);
	snd_soc_update_bits(tron_codec, ES8396_DAC_CSM_REG66,
			    0x03, 0x00);
	snd_soc_update_bits(tron_codec, ES8396_SDP1_IN_FMT_REG1F, 0x40,
			    0x00);
	for (i = 0; i < 120; i = i + 2) {
		snd_soc_write(tron_codec, 0x6A, 120 - i);
		snd_soc_write(tron_codec, 0x6B, 120 - i);
		usleep_range(100, 200);
	}
	mutex_unlock(&es8396->pcm_depop_mlock);
}

/*********************************************************************
 * to power on/off class d with min pop noise
 *********************************************************************/
static int classd_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	unsigned int regv1, regv2, lvl;
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct es8396_private *priv = snd_soc_codec_get_drvdata(codec);
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:	/* prepare power up */
		/* power up class d */
		pr_debug("SND_SOC_DAPM_PRE_PMU = 0x%x\n", event);
		/* read the clock configure */
		regv1 = snd_soc_read(codec, ES8396_CLK_CTRL_REG08);
		regv1 &= 0xcf;
		/* enable class d clock */
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, regv1);
		/* dac csm startup, dac digital still on */
		snd_soc_update_bits(codec, ES8396_DAC_CSM_REG66, 0xFE, 0x00);
		/* dac analog power on */
		snd_soc_update_bits(codec, ES8396_DAC_REF_PWR_CTRL_REG6E,
				    0xff, 0x34);

		regv2 = snd_soc_read(codec, ES8396_SPK_CTRL_1_REG3C);
		/* set speaker ldo level */
		if (es8396_valid_spkldo(priv->spk_ldo_lvl) == false) {
			pr_err("speaker LDO Level error.\n");
			return -EINVAL;
		} else {
			regv1 = regv2 & 0xD8;
			lvl = priv->spk_ldo_lvl;
			lvl &= 0x07;
			regv1 |= lvl;
			regv1 |= 0x10;
		}
		if (priv->spkmono == 1) {	/* speaker in mono mode */
			regv1 = regv1 | 0x40;
		} else {
			regv1 = regv1 & 0xbf;
		}
		snd_soc_write(codec, ES8396_SPK_CTRL_1_REG3C, regv1);

		snd_soc_write(codec, ES8396_SPK_CTRL_2_REG3D, 0x10);

		regv1 = snd_soc_read(codec, ES8396_SPK_MIXER_REG26);
		/* clear pdnspkl_biasgen, clear pdnspkr_biasgen */
		regv1 &= 0xee;
		snd_soc_write(codec, ES8396_SPK_MIXER_REG26, regv1);
		snd_soc_write(codec, ES8396_SPK_MIXER_VOL_REG28, 0x33);

		snd_soc_write(codec, ES8396_SPK_CTRL_SRC_REG3A, 0xA9);
		/* L&R DAC Vol=-6db */
		snd_soc_write(codec, ES8396_DAC_LDAC_VOL_REG6A, 0x00);
		snd_soc_write(codec, ES8396_DAC_RDAC_VOL_REG6B, 0x00);

		regv1 = snd_soc_read(codec, ES8396_HP_MIXER_BOOST_REG2B);
		regv1 &= 0xcc;
		snd_soc_write(codec, ES8396_HP_MIXER_BOOST_REG2B, regv1);

		regv1 = snd_soc_read(codec, ES8396_CPHP_CTRL_3_REG44);
		regv1 &= 0xcc;
		snd_soc_write(codec, ES8396_CPHP_CTRL_3_REG44, regv1);

		regv1 = snd_soc_read(codec, ES8396_CPHP_CTRL_1_REG42);
		regv1 &= 0xdf;
		snd_soc_write(codec, ES8396_CPHP_CTRL_1_REG42, regv1);

		regv1 = snd_soc_read(codec, ES8396_CPHP_CTRL_2_REG43);
		regv1 &= 0x7f;
		snd_soc_write(codec, ES8396_CPHP_CTRL_2_REG43, regv1);
		es8396->output_device_selected = 0;
		break;
	case SND_SOC_DAPM_POST_PMU:	/* after power up */
		pr_debug("SND_SOC_DAPM_POST_PMU = 0x%x\n", event);
		schedule_delayed_work(&es8396->pcm_pop_work,
				      msecs_to_jiffies(50));
		break;
	case SND_SOC_DAPM_PRE_PMD:	/* prepare power down */
		pr_debug("SND_SOC_DAPM_PRE_PMD = 0x%x\n", event);
		/* read the clock configure */
		regv1 = snd_soc_read(codec, ES8396_CLK_CTRL_REG08);
		regv1 |= 0x10;
		/* stop class d clock */
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, regv1);
		/* dac csm startup, dac digital still on */
		/* snd_soc_update_bits(w->codec, ES8396_DAC_CSM_REG66,
				       0x01, 0x01); */
		regv1 = snd_soc_read(codec, ES8396_SPK_EN_VOL_REG3B);
		regv1 &= 0x77;
		/* clear enspk_l,enspk_r */
		snd_soc_write(codec, ES8396_SPK_EN_VOL_REG3B, regv1);

		regv1 = snd_soc_read(codec, ES8396_SPK_CTRL_SRC_REG3A);
		regv1 |= 0x44;	/* set pdnspkl_biasgen, set pdnspkr_biasgen */
		snd_soc_write(codec, ES8396_SPK_CTRL_SRC_REG3A, regv1);
		regv1 = snd_soc_read(codec, ES8396_SPK_MIXER_REG26);
		/* clear pdnspkl_biasgen, clear pdnspkr_biasgen */
		regv1 |= 0x11;
		snd_soc_write(codec, ES8396_SPK_MIXER_REG26, regv1);
		snd_soc_update_bits(codec, ES8396_SPK_CTRL_1_REG3C, 0x20,
				    0x20);
		break;
	case SND_SOC_DAPM_POST_PMD:	/* after power down */
		pr_debug("SND_SOC_DAPM_POST_PMD = 0x%x\n", event);
		break;
	default:
		break;
	}
	return 0;
}

static int micbias_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(codec);
	unsigned int regv;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (es8396_valid_micbias(es8396->mic_bias_lvl) == false) {
			pr_err("MIC BIAS Level error.\n");
			return -EINVAL;
		} else {
			regv = es8396->mic_bias_lvl;
			regv &= 0x07;
			regv = (regv << 4) | 0x08;
			/* enable micbias1 */
			snd_soc_write(codec, ES8396_SYS_MICBIAS_CTRL_REG74,
				      regv);
		}
		regv = snd_soc_read(codec, ES8396_ALRCK_GPIO_SEL_REG15);
		if (es8396->dmic_amic == MIC_DMIC) {
			regv &= 0xf0;	/* enable DMIC CLK */
			regv |= 0x0A;
		} else {
			regv &= 0xf0;	/* disable DMIC CLK */
		}
		snd_soc_write(codec, ES8396_ALRCK_GPIO_SEL_REG15, regv);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regv = snd_soc_read(codec, ES8396_ALRCK_GPIO_SEL_REG15);
		regv &= 0xf0;	/* disable DMIC CLK */
		snd_soc_write(codec, ES8396_ALRCK_GPIO_SEL_REG15, regv);
		break;
	default:
		break;
	}

	return 0;
}

static void adc_depop_work_events(struct work_struct *work)
{
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);

	pr_debug("adc_depop_work_events\n");
	mutex_lock(&es8396->adc_depop_mlock);
	snd_soc_update_bits(tron_codec, ES8396_SDP1_OUT_FMT_REG20, 0x40, 0x00);
	mutex_unlock(&es8396->adc_depop_mlock);
}

static int adc_event(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *kcontrol, int event)
{
	unsigned int regv;
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(codec);

	pr_debug("Enter into %s  %d\n", __func__, __LINE__);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		pr_debug("Enter into SND_SOC_DAPM_PRE_PMU %s  %d\n", __func__,
			 __LINE__);
		snd_soc_update_bits(codec, ES8396_SDP1_OUT_FMT_REG20, 0x40,
				    0x40);
		/* set adc alc */
		snd_soc_write(codec, ES8396_ADC_ALC_CTRL_1_REG58, 0xC6);
		snd_soc_write(codec, ES8396_ADC_ALC_CTRL_2_REG59, 0x12);
		snd_soc_write(codec, ES8396_ADC_ALC_CTRL_4_REG5B, 0x04);
		snd_soc_write(codec, ES8396_ADC_ALC_CTRL_5_REG5C, 0xC8);
		snd_soc_write(codec, ES8396_ADC_ALC_CTRL_6_REG5D, 0x11);
		snd_soc_write(codec, ES8396_ADC_ANALOG_CTRL_REG5E, 0x0);
		/* Enable MIC BOOST */
		snd_soc_write(codec, ES8396_SYS_MIC_IBIAS_EN_REG75, 0x02);

		/* axMixer Gain boost */
		regv = snd_soc_read(codec, ES8396_AX_MIXER_BOOST_REG2F);
		regv |= 0x88;
		snd_soc_write(codec, ES8396_AX_MIXER_BOOST_REG2F, regv);
		/* axmixer vol = +12db */
		snd_soc_write(codec, ES8396_AX_MIXER_VOL_REG30, 0xaa);
		/* axmixer high driver capacility */
		snd_soc_write(codec, ES8396_AX_MIXER_REF_LP_REG31, 0x02);

		/* MNMixer Gain boost */
		regv = snd_soc_read(codec, ES8396_MN_MIXER_BOOST_REG37);
		regv |= 0x88;
		snd_soc_write(codec, ES8396_MN_MIXER_BOOST_REG37, regv);
		/* mnmixer vol = +12db */
		snd_soc_write(codec, ES8396_MN_MIXER_VOL_REG38, 0x44);
		/* mnmixer high driver capacility */
		snd_soc_write(codec, ES8396_MN_MIXER_REF_LP_REG39, 0x02);

		msleep(200);
		/* ADC STM and Digital Startup, ADC DS Mode */
		snd_soc_write(codec, ES8396_ADC_CSM_REG53, 0x00);
		/* force adc stm to normal */
		snd_soc_write(codec, ES8396_ADC_FORCE_REG77, 0x40);
		snd_soc_write(codec, ES8396_ADC_FORCE_REG77, 0x0);
		/* ADC Volume =0db */
		snd_soc_write(codec, ES8396_ADC_LADC_VOL_REG56, 0x0);
		snd_soc_write(codec, ES8396_ADC_RADC_VOL_REG57, 0x0);
		snd_soc_write(codec, ES8396_ADC_CLK_DIV_REG09, 0x04);

		schedule_delayed_work(&es8396->adc_depop_work,
				      msecs_to_jiffies(150));
		break;
	case SND_SOC_DAPM_PRE_PMD:
		pr_debug("Enter into SND_SOC_DAPM_PRE_PMD %s  %d\n", __func__,
			 __LINE__);
		snd_soc_write(codec, ES8396_ADC_CSM_REG53, 0x20);
		snd_soc_write(codec, ES8396_ADC_CLK_DIV_REG09, 0x04);
		break;
	default:
		break;
	}

	return 0;
}

/*********************************************************************
 * to power on/off headphone with min pop noise
 ********************************************************************/
static int hpamp_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol, int event)
{
	unsigned int regv;
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(codec);

	pr_debug("Enter into %s  %d\n", __func__, __LINE__);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		pr_debug("Enter into %s  %d, event = SND_SOC_DAPM_PRE_PMU\n",
			 __func__, __LINE__);
		es8396->output_device_selected = 1;
		break;
	case SND_SOC_DAPM_POST_PMU:
		pr_debug("Enter into %s  %d, event = SND_SOC_DAPM_POST_PMU\n",
			 __func__, __LINE__);
		schedule_delayed_work(&es8396->pcm_pop_work,
				      msecs_to_jiffies(50));
		break;
	case SND_SOC_DAPM_PRE_PMD:
		pr_debug("Enter into %s  %d, event = SND_SOC_DAPM_PRE_PMD\n",
			 __func__, __LINE__);
		/* dac analog power down */
		snd_soc_update_bits(codec, ES8396_DAC_CSM_REG66, 0x42, 0x00);
		break;
	case SND_SOC_DAPM_POST_PMD:
		pr_debug("Enter into %s  %d, event = SND_SOC_DAPM_POST_PMD\n",
			 __func__, __LINE__);
		/* dac analog power down */
		snd_soc_update_bits(codec, ES8396_DAC_CSM_REG66, 0x40, 0x40);
		/* dac analog power down */
		snd_soc_update_bits(codec, ES8396_DAC_REF_PWR_CTRL_REG6E,
				    0xC0, 0xC0);
		/* read the clock configure */
		regv = snd_soc_read(codec, ES8396_CLK_CTRL_REG08);
		regv |= 0x20;
		/* stop charge pump clock */
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, regv);

		regv = snd_soc_read(codec, ES8396_HP_MIXER_BOOST_REG2B);
		regv |= 0x11;
		snd_soc_write(codec, ES8396_HP_MIXER_BOOST_REG2B, regv);
		break;
	default:
		break;
	}

	return 0;
}
/*
 * ES8396 Controls
 */
static const DECLARE_TLV_DB_RANGE(mixvol_tlv,
				  0, 4, TLV_DB_SCALE_ITEM(-1200, 150, 0),
				  8, 11, TLV_DB_SCALE_ITEM(-600, 150, 0));
static const DECLARE_TLV_DB_RANGE(boost_tlv,
				  0, 1, TLV_DB_SCALE_ITEM(0, 2000, 0),
				  1, 3, TLV_DB_SCALE_ITEM(2000, 0, 0));

/* -34.5db min scale, 1.5db steps, no mute */
static const DECLARE_TLV_DB_SCALE(vol_tlv, -600, 150, 0);
/* -34.5db min scale, 1.5db steps, no mute */
static const DECLARE_TLV_DB_SCALE(spk_vol_tlv, 0, 150, 0);
/* -46.5db min scale, 1.5db steps, no mute */
static const DECLARE_TLV_DB_SCALE(hp_tlv, -4800, 1200, 0);
/* -16.5db min scale, 1.5db steps, no mute */
static const DECLARE_TLV_DB_SCALE(adc_rec_tlv, -9600, 50, 0);

static const DECLARE_TLV_DB_SCALE(lineout_tlv, -1200, 1200, 0);

/* 0db min scale, 6 db steps, no mute */
static const DECLARE_TLV_DB_SCALE(dig_tlv, 0, 600, 0);
/* 0db min scalem 0.75db steps, no mute */
static const DECLARE_TLV_DB_SCALE(vdac_tlv, -9600, 50, 0);

static const char *const alc_func_txt[] = { "Off", "LOn", "ROn", "StereoOn" };

static const struct soc_enum alc_func =
SOC_ENUM_SINGLE(ES8396_ADC_ALC_CTRL_1_REG58, 6, 4, alc_func_txt);

/*
 *define the line in,mic in, phone in ,and aif1-2 in volume/switch
 */
static const struct snd_kcontrol_new es8396_snd_controls[] = {
	SOC_DOUBLE_R_TLV("DAC Playback Volume",
			 ES8396_DAC_LDAC_VOL_REG6A, ES8396_DAC_RDAC_VOL_REG6B,
			 0, 127, 1, vdac_tlv),
	SOC_DOUBLE_TLV("MNIN MIXER Volume",
		       ES8396_MN_MIXER_VOL_REG38, 4, 0, 3, 1, mixvol_tlv),

	SOC_DOUBLE_TLV("LIN MIXER Volume",
		       ES8396_LN_MIXER_VOL_REG34, 4, 0, 4, 0, mixvol_tlv),

	SOC_DOUBLE_TLV("AXIN MIXER Volume",
		       ES8396_AX_MIXER_VOL_REG30, 4, 0, 4, 0, mixvol_tlv),
	SOC_DOUBLE_TLV("Mic Boost Volume",
		       ES8396_ADC_MICBOOST_REG60, 4, 0, 3, 0, boost_tlv),

	SOC_DOUBLE_R_TLV("ADC Capture Volume",
			 ES8396_ADC_LADC_VOL_REG56, ES8396_ADC_RADC_VOL_REG57,
			 0, 127, 1, adc_rec_tlv),

	SOC_SINGLE_TLV("Speakerl Playback Volume",
		       ES8396_SPK_EN_VOL_REG3B, 4, 7, 0, spk_vol_tlv),
	SOC_SINGLE_TLV("Speakerr Playback Volume",
		       ES8396_SPK_EN_VOL_REG3B, 0, 7, 0, spk_vol_tlv),

	SOC_SINGLE_TLV("Headphonel Playback Volume",
		       ES8396_CPHP_ICAL_VOL_REG41, 4, 3, 1, hp_tlv),
	SOC_SINGLE_TLV("Headphoner Playback Volume",
		       ES8396_CPHP_ICAL_VOL_REG41, 0, 3, 1, hp_tlv),
	/*
	 * lineout playback volume
	 */
	SOC_SINGLE_TLV("Lineoutp Playback Volume",
		       ES8396_LNOUT_LO1_GAIN_CTRL_REG4E, 5, 1, 1, lineout_tlv),
	SOC_SINGLE_TLV("Lineoutn Playback Volume",
		       ES8396_LNOUT_RO1_GAIN_CTRL_REG4F, 5, 1, 1, lineout_tlv),
	/*
	 * monoout playback volume
	 */
	SOC_SINGLE_TLV("Monooutp Playback Volume",
		       ES8396_MONOHP_P_BOOST_MUTE_REG48, 3, 1, 1, lineout_tlv),
	SOC_SINGLE_TLV("Monooutn Playback Volume",
		       ES8396_MONOHP_N_BOOST_MUTE_REG49, 3, 1, 1, lineout_tlv),
	SOC_ENUM("ALC Capture Function", alc_func),
};

/*
 * DAPM Controls
 */
static const struct snd_kcontrol_new es8396_dac_controls =
SOC_DAPM_SINGLE("Switch", ES8396_DAC_CSM_REG66, 3, 1, 1);

static const struct snd_kcontrol_new hp_amp_ctl =
SOC_DAPM_SINGLE("Switch", ES8396_CP_CLK_DIV_REG0B, 1, 1, 1);

static const struct snd_kcontrol_new es8396_hpl_mixer_controls[] = {
	SOC_DAPM_SINGLE("LNMUX2HPMIX_L Switch", ES8396_HP_MIXER_REG2A, 6, 1, 0),
	SOC_DAPM_SINGLE("AXMUX2HPMIX_L Switch", ES8396_HP_MIXER_REG2A, 5, 1, 0),
	SOC_DAPM_SINGLE("DACL2HPMIX Switch", ES8396_HP_MIXER_REF_LP_REG2D,
			5, 1, 0),
};

static const struct snd_kcontrol_new es8396_hpr_mixer_controls[] = {
	SOC_DAPM_SINGLE("LNMUX2HPMIX_R Switch", ES8396_HP_MIXER_REG2A, 2, 1, 0),
	SOC_DAPM_SINGLE("AXMUX2HPMIX_R Switch", ES8396_HP_MIXER_REG2A, 1, 1, 0),
	SOC_DAPM_SINGLE("DACR2HPMIX Switch", ES8396_HP_MIXER_REF_LP_REG2D,
			4, 1, 0),
};

/*
 * Only used mono out p mixer for differential output
 */
static const struct snd_kcontrol_new es8396_monoP_mixer_controls[] = {
	SOC_DAPM_SINGLE("LHPMIX2MNMIXP Switch", ES8396_MONOHP_P_MIXER_REG47, 7,
			1, 0),
	SOC_DAPM_SINGLE("RHPMIX2MNOMIXP Switch", ES8396_MONOHP_P_MIXER_REG47, 6,
			1, 0),
	SOC_DAPM_SINGLE("RMNMIX2MNOMIXP Switch", ES8396_MONOHP_P_MIXER_REG47, 5,
			1, 0),
	SOC_DAPM_SINGLE("RAXMIX2MNOMIXP Switch",
			ES8396_MONOHP_P_MIXER_REG47, 4, 1, 0),
	SOC_DAPM_SINGLE("LLNMIX2MNOMIXP Switch",
			ES8396_MONOHP_P_MIXER_REG47, 3, 1, 0),
};

static const struct snd_kcontrol_new es8396_monoN_mixer_controls[] = {
	SOC_DAPM_SINGLE("LMNMIX2MNMIXN Switch", ES8396_MONOHP_N_MIXER_REG46, 7,
			1, 0),
	SOC_DAPM_SINGLE("RHPMIX2MNOMIXN Switch", ES8396_MONOHP_N_MIXER_REG46, 6,
			1, 0),
	SOC_DAPM_SINGLE("MOPINV2MNOMIXN Switch", ES8396_MONOHP_N_MIXER_REG46, 5,
			1, 0),
	SOC_DAPM_SINGLE("LLNMIX2MNOMIXN Switch",
			ES8396_MONOHP_N_MIXER_REG46, 4, 1, 0),
	SOC_DAPM_SINGLE("LAXMIX2MNOMIXN Switch",
			ES8396_MONOHP_N_MIXER_REG46, 3, 1, 0),
};

/*
 * define the stereo class d speaker mixer
 */
static const struct snd_kcontrol_new es8396_speaker_lmixer_controls[] = {
	SOC_DAPM_SINGLE("LLNMUX2SPKMIX Switch", ES8396_SPK_MIXER_REG26, 6, 1,
			0),
	SOC_DAPM_SINGLE("LAXMUX2SPKMIX Switch", ES8396_SPK_MIXER_REG26, 5, 1,
			0),
	SOC_DAPM_SINGLE("LDAC2SPKMIX Switch", ES8396_SPK_MIXER_REG26, 7, 1, 0),
};

static const struct snd_kcontrol_new es8396_speaker_rmixer_controls[] = {
	SOC_DAPM_SINGLE("RLNMUX2SPKMIX Switch", ES8396_SPK_MIXER_REG26, 2, 1,
			0),
	SOC_DAPM_SINGLE("RAXMUX2SPKMIX Switch", ES8396_SPK_MIXER_REG26, 1, 1,
			0),
	SOC_DAPM_SINGLE("RDAC2SPKMIX Switch", ES8396_SPK_MIXER_REG26, 3, 1, 0),
};

/*
 * Only used line out1 p mixer for differential output
 */
static const struct snd_kcontrol_new es8396_lout1_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDAC2LO1MIXP Switch", SND_SOC_NOPM,
			5, 1, 0),
	SOC_DAPM_SINGLE("LAXMIX2LO1MIXP Switch",
			ES8396_LNOUT_LO1EN_LO1MIX_REG4A, 4, 1, 0),
	SOC_DAPM_SINGLE("LLNMIX2LO1MIXP Switch",
			ES8396_LNOUT_LO1EN_LO1MIX_REG4A, 3, 1, 0),
	SOC_DAPM_SINGLE("LMNMIX2LO1MIXP Switch",
			ES8396_LNOUT_LO1EN_LO1MIX_REG4A, 2, 1, 0),
	SOC_DAPM_SINGLE("RO1INV2LO1MIXP Switch",
			ES8396_LNOUT_LO1EN_LO1MIX_REG4A, 1, 1, 0),
};

static const struct snd_kcontrol_new es8396_rout1_mixer_controls[] = {
	SOC_DAPM_SINGLE("RDAC2RO1MIXN Switch", SND_SOC_NOPM,
			5, 1, 0),
	SOC_DAPM_SINGLE("RAXMIX2RO1MIXN Switch",
			ES8396_LNOUT_RO1EN_RO1MIX_REG4B, 4, 1, 0),
	SOC_DAPM_SINGLE("RLNMIX2RO1MIXN Switch",
			ES8396_LNOUT_RO1EN_RO1MIX_REG4B, 3, 1, 0),
	SOC_DAPM_SINGLE("RMNMIX2RO1MIXN Switch",
			ES8396_LNOUT_RO1EN_RO1MIX_REG4B, 2, 1, 0),
	SOC_DAPM_SINGLE("LO1INV2RO1MIXN Switch",
			ES8396_LNOUT_RO1EN_RO1MIX_REG4B, 1, 1, 0),
};

/*
 *left LNMIX mixer
 */
static const struct snd_kcontrol_new es8396_lnmixL_mixer_controls[] = {
	SOC_DAPM_SINGLE("AINL2LLNMIX Switch", ES8396_LN_MIXER_REG32, 7, 1, 0),
	SOC_DAPM_SINGLE("LLNMUX2LLNMIX Switch", ES8396_LN_MIXER_REG32, 6, 1, 0),
	SOC_DAPM_SINGLE("MIC1P2LLNMIX Switch", ES8396_LN_MIXER_REG32, 5, 1, 0),
	SOC_DAPM_SINGLE("PMICDSE2LLNMIX Switch", ES8396_LN_MIXER_REG32, 4, 1,
			0),
};

/*
 *right LNMIX mixer
 */
static const struct snd_kcontrol_new es8396_lnmixR_mixer_controls[] = {
	SOC_DAPM_SINGLE("AINR2RLNMIX Switch", ES8396_LN_MIXER_REG32, 3, 1, 0),
	SOC_DAPM_SINGLE("RLNMUX2RLNMIX Switch", ES8396_LN_MIXER_REG32, 2, 1, 0),
	SOC_DAPM_SINGLE("MIC1N2LLNMIX Switch", ES8396_LN_MIXER_REG32, 1, 1, 0),
	SOC_DAPM_SINGLE("NMICDSE2RLNMIX Switch", ES8396_LN_MIXER_REG32, 0, 1,
			0),
};

/*
 *left AXMIX mixer
 */
static const struct snd_kcontrol_new es8396_axmixL_mixer_controls[] = {
	SOC_DAPM_SINGLE("LAXMUX2LAXMIX Switch", ES8396_AX_MIXER_REG2E, 7, 1, 0),
	SOC_DAPM_SINGLE("MONOP2LAXMIX Switch", ES8396_AX_MIXER_REG2E, 6, 1, 0),
	SOC_DAPM_SINGLE("MIC2P2LAXMIX Switch", ES8396_AX_MIXER_REG2E, 5, 1, 0),
	SOC_DAPM_SINGLE("PMICDSE2LAXMIX Switch", ES8396_AX_MIXER_REG2E, 4, 1,
			0),
};

/*
 *right AXMIX mixer
 */
static const struct snd_kcontrol_new es8396_axmixR_mixer_controls[] = {
	SOC_DAPM_SINGLE("RAXMUX2RAXMIX Switch", ES8396_AX_MIXER_REG2E, 3, 1, 0),
	SOC_DAPM_SINGLE("MONON2RAXMIX Switch", ES8396_AX_MIXER_REG2E, 2, 1, 0),
	SOC_DAPM_SINGLE("MIC2N2RAXMIX Switch", ES8396_AX_MIXER_REG2E, 1, 1, 0),
	SOC_DAPM_SINGLE("NMICDSE2RAXMIX Switch", ES8396_AX_MIXER_REG2E, 0, 1,
			0),
};

/*
 *left MNMIX mixer
 */
static const struct snd_kcontrol_new es8396_mnmixL_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDAC2LMNMIX Switch", ES8396_MN_MIXER_REG36, 7, 1, 0),
	SOC_DAPM_SINGLE("MONOP2LMNMIX Switch", ES8396_MN_MIXER_REG36, 6, 1, 0),
	SOC_DAPM_SINGLE("AINL2LMNMIX Switch", ES8396_MN_MIXER_REG36, 5, 1, 0),
};

/*
 *right MNMIX mixer
 */
static const struct snd_kcontrol_new es8396_mnmixR_mixer_controls[] = {
	SOC_DAPM_SINGLE("RDAC2RMNMIX Switch", ES8396_MN_MIXER_REG36, 3, 1, 0),
	SOC_DAPM_SINGLE("MONON2RMNMIX Switch", ES8396_MN_MIXER_REG36, 2, 1, 0),
	SOC_DAPM_SINGLE("AINR2RMNMIX Switch", ES8396_MN_MIXER_REG36, 1, 1, 0),
};

/*
 * Left Record Mixer
 */
static const struct snd_kcontrol_new es8396_captureL_mixer_controls[] = {
	SOC_DAPM_SINGLE("RLNMIX2LPGA Switch", ES8396_ADC_LPGA_MIXER_REG62, 7, 1,
			0),
	SOC_DAPM_SINGLE("RAXMIX2LPGA Switch", ES8396_ADC_LPGA_MIXER_REG62, 6, 1,
			0),
	SOC_DAPM_SINGLE("RMNMIX2LPGA Switch", ES8396_ADC_LPGA_MIXER_REG62, 5, 1,
			0),
	SOC_DAPM_SINGLE("LMNMIX2LPGA Switch", ES8396_ADC_LPGA_MIXER_REG62, 4, 1,
			0),
	SOC_DAPM_SINGLE("LLNMIX2LPGA Switch", ES8396_ADC_LPGA_MIXER_REG62, 3, 1,
			0),

};

/*
 * Right Record Mixer
 */
static const struct snd_kcontrol_new es8396_captureR_mixer_controls[] = {
	SOC_DAPM_SINGLE("RLNMIX2RPGA Switch", ES8396_ADC_RPGA_MIXER_REG63, 7, 1,
			0),
	SOC_DAPM_SINGLE("RAXMIX2RPGA Switch", ES8396_ADC_RPGA_MIXER_REG63, 6, 1,
			0),
	SOC_DAPM_SINGLE("RMNMIX2RPGA Switch", ES8396_ADC_RPGA_MIXER_REG63, 5, 1,
			0),
	SOC_DAPM_SINGLE("LMNMIX2RPGA Switch", ES8396_ADC_RPGA_MIXER_REG63, 4, 1,
			0),
	SOC_DAPM_SINGLE("LAXMIX2RPGA Switch", ES8396_ADC_RPGA_MIXER_REG63, 3, 1,
			0),
};

static const struct snd_kcontrol_new es8396_adc_controls =
SOC_DAPM_SINGLE("Switch", ES8396_ADC_CSM_REG53, 6, 1, 1);

/*
 * MIC INPUT MUX
 */
static const struct snd_kcontrol_new es8396_micin_mux_controls =
SOC_DAPM_SINGLE("Switch", ES8396_SYS_MIC_IBIAS_EN_REG75, 1, 1, 0);

/*
 * left LN MUX
 */
static const char *const es8396_left_lnmux_txt[] = {
	"NO IN",
	"RPGAP",
	"LPGAP",
	"MONOP",
	"AINL"
};

static const unsigned int es8396_left_lnmux_values[] = {
	0, 1, 2, 4, 8
};

static const struct soc_enum es8396_left_lnmux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_ADC_LN_MUX_REG64, 0, 15,
		      ARRAY_SIZE(es8396_left_lnmux_txt),
		      es8396_left_lnmux_txt,
		      es8396_left_lnmux_values);
static const struct snd_kcontrol_new es8396_left_lnmux_controls =
SOC_DAPM_ENUM("Route", es8396_left_lnmux_enum);

/*
 * Right LN MUX
 */
static const char *const es8396_right_lnmux_txt[] = {
	"NO IN",
	"RPGAP",
	"LPGAP",
	"MONON",
	"AINR"
};

static const struct soc_enum es8396_right_lnmux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_ADC_LN_MUX_REG64, 4, 15,
		      ARRAY_SIZE(es8396_right_lnmux_txt),
		      es8396_right_lnmux_txt,
		      es8396_left_lnmux_values);
static const struct snd_kcontrol_new es8396_right_lnmux_controls =
SOC_DAPM_ENUM("Route", es8396_right_lnmux_enum);

/*
 * left AX MUX
 */
static const struct soc_enum es8396_left_axmux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_ADC_AX_MUX_REG65, 0, 15,
		      ARRAY_SIZE(es8396_left_lnmux_txt),
		      es8396_left_lnmux_txt,
		      es8396_left_lnmux_values);
static const struct snd_kcontrol_new es8396_left_axmux_controls =
SOC_DAPM_ENUM("Route", es8396_left_axmux_enum);

/*
 * Right AX MUX
 */
static const struct soc_enum es8396_right_axmux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_ADC_AX_MUX_REG65, 4, 15,
		      ARRAY_SIZE(es8396_right_lnmux_txt),
		      es8396_right_lnmux_txt,
		      es8396_left_lnmux_values);
static const struct snd_kcontrol_new es8396_right_axmux_controls =
SOC_DAPM_ENUM("Route", es8396_right_axmux_enum);

/*
 * Left SPKOUT MUX
 */
static const char *const es8396_left_spkout_mux_txt[] = {
	"NO Out",
	"SPKR Route",
	"SPKL Route"
};

static const unsigned int es8396_left_spkout_mux_values[] = {
	0, 1, 2
};

static const struct soc_enum es8396_left_spkout_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_SPK_CTRL_SRC_REG3A, 4, 3,
		      ARRAY_SIZE(es8396_left_spkout_mux_txt),
		      es8396_left_spkout_mux_txt,
		      es8396_left_spkout_mux_values);
static const struct snd_kcontrol_new es8396_left_spkout_mux_controls =
SOC_DAPM_ENUM("Route", es8396_left_spkout_mux_enum);

/*
 * Right SPKOUT MUX
 */
static const struct soc_enum es8396_right_spkout_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_SPK_CTRL_SRC_REG3A, 0, 3,
		      ARRAY_SIZE(es8396_left_spkout_mux_txt),
		      es8396_left_spkout_mux_txt,
		      es8396_left_spkout_mux_values);
static const struct snd_kcontrol_new es8396_right_spkout_mux_controls =
SOC_DAPM_ENUM("Route", es8396_right_spkout_mux_enum);

/*
 * SPKLDO POWER SWITCH
 */
static const struct snd_kcontrol_new es8396_spkldo_pwrswitch_controls =
SOC_DAPM_SINGLE("Switch", ES8396_DAMP_CLK_DIV_REG0C, 1, 1, 1);

/*
 * Dmic MUX
 */
static const char *const es8396_dmic_mux_txt[] = {
	/* 0  can be used for stereo amic */
	"dmic disable,use adc",
	"ldata use ladc,rdata use ldmic at low clk",
	"ldata use ladc,rdata use rdmic at low clk",
	"ldata use ladc,rdata use rdmic at high clk",
	"ldata use ldmic at high clk,rdata use radc",
	"ldata use ldmic at high clk,rdata use ldmic at low clk",
	/* can be used for stereo dmic */
	"ldata use ldmic at high clk,rdata use rdmic at low clk",
	"ldata use ldmic at high clk,rdata use rdmic at high clk",
	"ldata use rdmic at high clk,rdata use radc",
	/* can be used for stereo dmic */
	"ldata use rdmic at high clk,rdata use ldmic at low clk",
	"ldata use rdmic at high clk,rdata use rdmic at low clk",
	"ldata use rdmic at high clk,rdata use rdmic at high clk",
	"ldata use ldmic at low clk,rdata use radc",
	"ldata use ldmic at low clk,rdata use ldmic at low clk",
	"ldata use ldmic at low clk,rdata use rdmic at low clk",
	/* can be used for stereo dmic */
	"ldata use ldmic at low clk,rdata use rdmic at high clk",
};

static const unsigned int es8396_dmic_mux_values[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

static const struct soc_enum es8396_dmic_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_ADC_DMIC_RAMPRATE_REG54, 4, 15,
		      ARRAY_SIZE(es8396_dmic_mux_txt),
		      es8396_dmic_mux_txt,
		      es8396_dmic_mux_values);
static const struct snd_kcontrol_new es8396_dmic_mux_controls =
SOC_DAPM_ENUM("Route", es8396_dmic_mux_enum);

/*
 * Digital mixer1 left
 */
static const char *const es8396_left_digital_mixer_txt[] = {
	"left SDP1 in",
	"left SDP2 in",
	"left SDP3 in",
	"left ADC out",
	"right SDP1 in",
	"right SDP2 in",
	"right SDP3 in",
	"right ADC out"
};

static const unsigned int es8396_left_digital_mixer_values[] = {
	0, 1, 2, 3, 4, 5, 6, 7
};

static const struct soc_enum es8396_left_digital_mixer_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_DMIX_SRC_1_REG18, 4, 15,
		      ARRAY_SIZE(es8396_left_digital_mixer_txt),
		      es8396_left_digital_mixer_txt,
		      es8396_left_digital_mixer_values);
static const struct snd_kcontrol_new es8396_left_digital_mixer_controls =
SOC_DAPM_ENUM("Route", es8396_left_digital_mixer_enum);

/*
 * Digital mixer1 right
 */
static const char *const es8396_right_digital_mixer_txt[] = {
	"right SDP1 in",
	"right SDP2 in",
	"right SDP3 in",
	"right ADC out",
	"left SDP1 in",
	"left SDP2 in",
	"left SDP3 in",
	"left ADC out"
};

static const struct soc_enum es8396_right_digital_mixer_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_DMIX_SRC_1_REG18, 0, 15,
		      ARRAY_SIZE(es8396_right_digital_mixer_txt),
		      es8396_right_digital_mixer_txt,
		      es8396_left_digital_mixer_values);
static const struct snd_kcontrol_new es8396_right_digital_mixer_controls =
SOC_DAPM_ENUM("Route", es8396_right_digital_mixer_enum);

/*
 * Digital mixer2 left
 */
static const struct soc_enum es8396_left_digital2_mixer_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_DMIX_SRC_2_REG19, 4, 15,
		      ARRAY_SIZE(es8396_left_digital_mixer_txt),
		      es8396_left_digital_mixer_txt,
		      es8396_left_digital_mixer_values);
static const struct snd_kcontrol_new es8396_left_digital2_mixer_controls =
SOC_DAPM_ENUM("Route", es8396_left_digital2_mixer_enum);

/*
 * Digital mixer2 right
 */
static const struct soc_enum es8396_right_digital2_mixer_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_DMIX_SRC_2_REG19, 0, 15,
		      ARRAY_SIZE(es8396_right_digital_mixer_txt),
		      es8396_right_digital_mixer_txt,
		      es8396_left_digital_mixer_values);
static const struct snd_kcontrol_new es8396_right_digital2_mixer_controls =
SOC_DAPM_ENUM("Route", es8396_right_digital2_mixer_enum);

/*
 * equalizer clk mux
 */
static const char *const es8396_eq_clk_mux_txt[] = {
	"from dac mclk",
	"from adc mclk",
	"from clk1",
	"from clk2"
};

static const unsigned int es8396_eq_clk_mux_values[] = {
	0, 1, 2, 3
};

static const struct soc_enum es8396_eq_clk_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_EQ_CLK_OSR_SEL_REG1C, 4, 3,
		      ARRAY_SIZE(es8396_eq_clk_mux_txt),
		      es8396_eq_clk_mux_txt,
		      es8396_eq_clk_mux_values);
static const struct snd_kcontrol_new es8396_eq_clk_mux_controls =
SOC_DAPM_ENUM("Route", es8396_eq_clk_mux_enum);

/*
 * equalizer osr mux
 */
static const char *const es8396_eq_osr_mux_txt[] = {
	"1FS OSR",
	"2FS OSR",
	"3FS OSR",
	"4FS OSR",
	"5FS OSR",
	"6FS OSR"
};

static const unsigned int es8396_eq_osr_mux_values[] = {
	0, 1, 2, 3, 4, 5
};

static const struct soc_enum es8396_eq_osr_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_EQ_CLK_OSR_SEL_REG1C, 0, 7,
		      ARRAY_SIZE(es8396_eq_osr_mux_txt),
		      es8396_eq_osr_mux_txt,
		      es8396_eq_osr_mux_values);
static const struct snd_kcontrol_new es8396_eq_osr_mux_controls =
SOC_DAPM_ENUM("Route", es8396_eq_osr_mux_enum);

/*
 * DAC source mux
 */
static const char *const es8396_dac_src_mux_txt[] = {
	"SDP1 in",
	"SDP2 in",
	"SDP3 in",
	"ADC out",
	"EQ stereo",
	"EQ left",
	"EQ right",
};

static const unsigned int es8396_dac_src_mux_values[] = {
	0, 1, 2, 3, 4, 5, 6
};

static const struct soc_enum es8396_dac_src_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_DAC_SRC_SDP1O_SRC_REG1A, 4, 7,
		      ARRAY_SIZE(es8396_dac_src_mux_txt),
		      es8396_dac_src_mux_txt,
		      es8396_dac_src_mux_values);
static const struct snd_kcontrol_new es8396_dac_src_mux_controls =
SOC_DAPM_ENUM("Route", es8396_dac_src_mux_enum);

/*
 * I2S1 out mux
 */
static const char *const es8396_i2s1_out_mux_txt[] = {
	"ADC out",
	"SDP1 in",
	"SDP2 in",
	"SDP3 in",
	"EQ stereo",
	"EQ left",
	"EQ right",
};

static const unsigned int es8396_i2s1_out_mux_values[] = {
	0, 1, 2, 3, 4, 5, 6
};

static const struct soc_enum es8396_i2s1_out_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_DAC_SRC_SDP1O_SRC_REG1A, 0, 7,
		      ARRAY_SIZE(es8396_i2s1_out_mux_txt),
		      es8396_i2s1_out_mux_txt,
		      es8396_i2s1_out_mux_values);
static const struct snd_kcontrol_new es8396_i2s1_out_mux_controls =
SOC_DAPM_ENUM("Route", es8396_i2s1_out_mux_enum);

/*
 * I2S2 out mux
 */
static const struct soc_enum es8396_i2s2_out_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_SDP2O_SDP3O_SRC_REG1B, 4, 7,
		      ARRAY_SIZE(es8396_i2s1_out_mux_txt),
		      es8396_i2s1_out_mux_txt,
		      es8396_i2s1_out_mux_values);
static const struct snd_kcontrol_new es8396_i2s2_out_mux_controls =
SOC_DAPM_ENUM("Route", es8396_i2s2_out_mux_enum);

/*
 * I2S3 out mux
 */
static const struct soc_enum es8396_i2s3_out_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8396_SDP2O_SDP3O_SRC_REG1B, 0, 7,
		      ARRAY_SIZE(es8396_i2s1_out_mux_txt),
		      es8396_i2s1_out_mux_txt,
		      es8396_i2s1_out_mux_values);
static const struct snd_kcontrol_new es8396_i2s3_out_mux_controls =
SOC_DAPM_ENUM("Route", es8396_i2s3_out_mux_enum);

static const struct snd_soc_dapm_widget es8396_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("DMIC"),
	SND_SOC_DAPM_INPUT("LINP"),
	SND_SOC_DAPM_INPUT("RINN"),
	SND_SOC_DAPM_INPUT("MONOINP"),
	SND_SOC_DAPM_INPUT("MONOINN"),
	SND_SOC_DAPM_INPUT("AINL"),
	SND_SOC_DAPM_INPUT("AINR"),
	SND_SOC_DAPM_INPUT("MIC"),
	SND_SOC_DAPM_SUPPLY("MIC Bias", SND_SOC_NOPM, 0, 0,
			    micbias_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	/*
	 * AIF OUT AND MUX
	 */
	SND_SOC_DAPM_AIF_OUT("VOICESDPOL", "SDP1 Capture", 0,
			     ES8396_SDP1_OUT_FMT_REG20, 6, 1),
	SND_SOC_DAPM_AIF_OUT("VOICESDPOR", "SDP1 Capture", 0,
			     ES8396_SDP1_OUT_FMT_REG20, 6, 1),
	SND_SOC_DAPM_MUX("VOICESDPO Mux", SND_SOC_NOPM, 0, 0, &es8396_i2s1_out_mux_controls),
	SND_SOC_DAPM_AIF_OUT("MASTERSDPOL", "SDP2 Capture", 0,
			     ES8396_SDP2_OUT_FMT_REG23, 6, 1),
	SND_SOC_DAPM_AIF_OUT("MASTERSDPOR", "SDP2 Capture", 0,
			     ES8396_SDP2_OUT_FMT_REG23, 6, 1),
	SND_SOC_DAPM_MUX("MASTERSDPO Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_i2s2_out_mux_controls),

	SND_SOC_DAPM_AIF_OUT("AUXSDPOL", "SDP3 Capture", 0,
			     ES8396_SDP3_OUT_FMT_REG25, 6, 1),
	SND_SOC_DAPM_AIF_OUT("AUXSDPOR", "SDP3 Capture", 0,
			     ES8396_SDP3_OUT_FMT_REG25, 6, 1),
	SND_SOC_DAPM_MUX("AUXSDPO Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_i2s3_out_mux_controls),

	SND_SOC_DAPM_MIXER("VOICEOUT AIF Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("MASTEROUT AIF Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AUXOUT AIF Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* capature  */

	/*
	 *left and right mixer
	 */
	SND_SOC_DAPM_MIXER_NAMED_CTL("PGA Left Mix", SND_SOC_NOPM, 0, 0,
				     &es8396_captureL_mixer_controls[0],
				     ARRAY_SIZE
				     (es8396_captureL_mixer_controls)),
	SND_SOC_DAPM_MIXER_NAMED_CTL("PGA Right Mix", SND_SOC_NOPM, 0, 0,
				     &es8396_captureR_mixer_controls[0],
				     ARRAY_SIZE
				     (es8396_captureR_mixer_controls)),

	SND_SOC_DAPM_PGA("LPGA P", ES8396_ADC_ANALOG_CTRL_REG5E, 4, 1, NULL, 0),
	SND_SOC_DAPM_PGA("RPGA P", ES8396_ADC_ANALOG_CTRL_REG5E, 5, 1, NULL, 0),

	SND_SOC_DAPM_ADC("ADC Left", NULL, ES8396_ADC_ANALOG_CTRL_REG5E, 2, 1),
	SND_SOC_DAPM_ADC("ADC Right", NULL, ES8396_ADC_ANALOG_CTRL_REG5E, 3, 1),
	SND_SOC_DAPM_SWITCH_E("ADC_1", SND_SOC_NOPM, 0, 0,
			      &es8396_adc_controls, adc_event,
			      SND_SOC_DAPM_PRE_PMD),

	/*
	 * Analog MIC Muxes
	 */
	SND_SOC_DAPM_SWITCH("AMIC Mux", ES8396_SYS_MIC_IBIAS_EN_REG75, 0, 1,
			    &es8396_micin_mux_controls),
	SND_SOC_DAPM_PGA("MIC BOOST", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* LN,AX Muxes */
	/*
	 * LN MUX
	 */
	SND_SOC_DAPM_MUX("LLN Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_left_lnmux_controls),
	SND_SOC_DAPM_MUX("RLN Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_right_lnmux_controls),
	/*
	 * AX MUX
	 */
	SND_SOC_DAPM_MUX("LAX Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_left_axmux_controls),
	SND_SOC_DAPM_MUX("RAX Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_right_axmux_controls),
	/*
	 * AIF IN
	 */
	SND_SOC_DAPM_AIF_IN("VOICESDPIL", "SDP1 Playback", 0,
			    ES8396_SDP1_IN_FMT_REG1F, 6, 1),
	SND_SOC_DAPM_AIF_IN("VOICESDPIR", "SDP1 Playback", 0,
			    ES8396_SDP1_IN_FMT_REG1F, 6, 1),
	SND_SOC_DAPM_AIF_IN("MASTERSDPIL", "SDP2 Playback", 0,
			    SND_SOC_NOPM, 6, 1),
	SND_SOC_DAPM_AIF_IN("MASTERSDPIR", "SDP2 Playback", 0,
			    SND_SOC_NOPM, 6, 1),
	SND_SOC_DAPM_AIF_IN("AUXSDPIL", "SDP3 Playback", 0,
			    ES8396_SDP3_IN_FMT_REG24, 6, 1),
	SND_SOC_DAPM_AIF_IN("AUXSDPIR", "SDP3 Playback", 0,
			    ES8396_SDP3_IN_FMT_REG24, 6, 1),
	SND_SOC_DAPM_MIXER("VOICEIN AIF Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("MASTERIN AIF Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AUXIN AIF Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	/*
	 * Digital mixer1,2
	 */
	SND_SOC_DAPM_MUX("LDMIX1 Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_left_digital_mixer_controls),
	SND_SOC_DAPM_MUX("RDMIX1 Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_right_digital_mixer_controls),
	SND_SOC_DAPM_MUX("LDMIX2 Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_left_digital2_mixer_controls),
	SND_SOC_DAPM_MUX("RDMIX2 Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_right_digital2_mixer_controls),

	SND_SOC_DAPM_MIXER("Digital Left Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Digital Right Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Equalizer", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("DACSRC Mux", SND_SOC_NOPM, 0, 0,
			       &es8396_dac_src_mux_controls),
	/*
	 * DAC
	 */
	SND_SOC_DAPM_SWITCH("DAC_1", SND_SOC_NOPM, 0, 0,
			    &es8396_dac_controls),

	SND_SOC_DAPM_DAC("Left DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("Right DAC", NULL, SND_SOC_NOPM, 0, 0),

	/*
	 * mixerMono
	 */
	SND_SOC_DAPM_MIXER("LMONIN Mix", ES8396_MN_MIXER_BOOST_REG37, 4, 1,
			   &es8396_mnmixL_mixer_controls[0],
			   ARRAY_SIZE(es8396_mnmixL_mixer_controls)),
	SND_SOC_DAPM_PGA("LMONINMIX PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RMONIN Mix", ES8396_MN_MIXER_BOOST_REG37, 0, 1,
			   &es8396_mnmixR_mixer_controls[0],
			   ARRAY_SIZE(es8396_mnmixR_mixer_controls)),
	SND_SOC_DAPM_PGA("RMONINMIX PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	/*
	 * mixerLN
	 */
	SND_SOC_DAPM_MIXER("LLNIN Mix", ES8396_LN_MIXER_BOOST_REG33, 4, 1,
			   &es8396_lnmixL_mixer_controls[0],
			   ARRAY_SIZE(es8396_lnmixL_mixer_controls)),
	SND_SOC_DAPM_PGA("LLNINMIX PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RLNIN Mix", ES8396_LN_MIXER_BOOST_REG33, 0, 1,
			   &es8396_lnmixR_mixer_controls[0],
			   ARRAY_SIZE(es8396_lnmixR_mixer_controls)),
	SND_SOC_DAPM_PGA("RLNINMIX PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	/*
	 * mixerAX
	 */
	SND_SOC_DAPM_MIXER("LAXIN Mix", ES8396_AX_MIXER_BOOST_REG2F, 4, 1,
			   &es8396_axmixL_mixer_controls[0],
			   ARRAY_SIZE(es8396_axmixL_mixer_controls)),
	SND_SOC_DAPM_PGA("LAXINMIX PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RAXIN Mix", ES8396_AX_MIXER_BOOST_REG2F, 0, 1,
			   &es8396_axmixR_mixer_controls[0],
			   ARRAY_SIZE(es8396_axmixR_mixer_controls)),
	SND_SOC_DAPM_PGA("RAXINMIX PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	/*
	 * mixerLNOUT
	 */
	SND_SOC_DAPM_MIXER("LOUT1 Mix", SND_SOC_NOPM, 0, 0,
			   &es8396_lout1_mixer_controls[0],
			   ARRAY_SIZE(es8396_lout1_mixer_controls)),
	SND_SOC_DAPM_PGA("LNOUTMIX1 PGA", SND_SOC_NOPM, 6, 0,
			 NULL, 0),
	SND_SOC_DAPM_MIXER("ROUT1 Mix", SND_SOC_NOPM, 0, 0,
			   &es8396_rout1_mixer_controls[0],
			   ARRAY_SIZE(es8396_rout1_mixer_controls)),
	SND_SOC_DAPM_PGA("RNOUTMIX1 PGA", SND_SOC_NOPM, 6, 0,
			 NULL, 0),

	/*
	 * mixerMNOUT
	 */
	SND_SOC_DAPM_MIXER("MNOUTP Mix", SND_SOC_NOPM, 0, 0,
			   &es8396_monoP_mixer_controls[0],
			   ARRAY_SIZE(es8396_monoP_mixer_controls)),
	SND_SOC_DAPM_PGA("MNOUTP PGA", ES8396_MONOHP_P_BOOST_MUTE_REG48, 7, 0,
			 NULL, 0),
	SND_SOC_DAPM_MIXER("MNOUTN Mix", SND_SOC_NOPM, 0, 0,
			   &es8396_monoN_mixer_controls[0],
			   ARRAY_SIZE(es8396_monoN_mixer_controls)),
	SND_SOC_DAPM_PGA("MNOUTN PGA", ES8396_MONOHP_N_BOOST_MUTE_REG49, 7, 0,
			 NULL, 0),

	/*
	 * mixerHP
	 */
	/*
	 * SND_SOC_DAPM_MIXER("HPL Mix", ES8396_HP_MIXER_BOOST_REG2B, 4, 1,
	 */
	SND_SOC_DAPM_MIXER("HPL Mix", SND_SOC_NOPM, 6, 0,
			   &es8396_hpl_mixer_controls[0],
			   ARRAY_SIZE(es8396_hpl_mixer_controls)),
	/*
	 * SND_SOC_DAPM_MIXER("HPR Mix", ES8396_HP_MIXER_BOOST_REG2B, 0, 1,
	 */
	SND_SOC_DAPM_MIXER("HPR Mix", SND_SOC_NOPM, 2, 0,
			   &es8396_hpr_mixer_controls[0],
			   ARRAY_SIZE(es8396_hpr_mixer_controls)),
	SND_SOC_DAPM_SWITCH_E("HP Amp", SND_SOC_NOPM, 0, 0,
			      &hp_amp_ctl, hpamp_event,
			      SND_SOC_DAPM_PRE_PMU),
	/*
	 * mixerSPK
	 */
	SND_SOC_DAPM_MIXER("SPKL Mix", SND_SOC_NOPM, 0, 0,
			   &es8396_speaker_lmixer_controls[0],
			   ARRAY_SIZE(es8396_speaker_lmixer_controls)),
	SND_SOC_DAPM_MIXER("SPKR Mix", SND_SOC_NOPM, 0, 0,
			   &es8396_speaker_rmixer_controls[0],
			   ARRAY_SIZE(es8396_speaker_rmixer_controls)),
	SND_SOC_DAPM_SWITCH_E("SPK Amp", SND_SOC_NOPM, 0, 0,
			      &es8396_spkldo_pwrswitch_controls, classd_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_OUTPUT("MONOOUTP"),
	SND_SOC_DAPM_OUTPUT("MONOOUTN"),
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("SPKOUTL"),
	SND_SOC_DAPM_OUTPUT("SPKOUTR"),
	SND_SOC_DAPM_OUTPUT("LOUTP"),
	SND_SOC_DAPM_OUTPUT("ROUTN"),
};

static const struct snd_soc_dapm_route es8396_dapm_routes[] = {
	/* lln mux */
	{"LLN Mux", "RPGAP", "RPGA P"},
	{"LLN Mux", "LPGAP", "LPGA P"},
	{"LLN Mux", "MONOP", "MONOINP"},
	{"LLN Mux", "AINL", "AINL"},

	/* rln mux */
	{"RLN Mux", "RPGAP", "RPGA P"},
	{"RLN Mux", "LPGAP", "LPGA P"},
	{"RLN Mux", "MONON", "MONOINN"},
	{"RLN Mux", "AINR", "AINR"},

	/* lax mux */
	{"LAX Mux", "RPGAP", "RPGA P"},
	{"LAX Mux", "LPGAP", "LPGA P"},
	{"LAX Mux", "MONOP", "MONOINP"},
	{"LAX Mux", "AINL", "AINL"},

	/* rax mux */
	{"RAX Mux", "RPGAP", "RPGA P"},
	{"RAX Mux", "LPGAP", "LPGA P"},
	{"RAX Mux", "MONON", "MONOINN"},
	{"RAX Mux", "AINR", "AINR"},

	/* Left, right PGA */
	{"LPGA P", NULL, "PGA Left Mix"},
	{"RPGA P", NULL, "PGA Right Mix"},

	{"PGA Left Mix", "RLNMIX2LPGA Switch", "RLNINMIX PGA"},
	{"PGA Left Mix", "RAXMIX2LPGA Switch", "RAXINMIX PGA"},
	{"PGA Left Mix", "RMNMIX2LPGA Switch", "RMONINMIX PGA"},
	{"PGA Left Mix", "LMNMIX2LPGA Switch", "LMONINMIX PGA"},
	{"PGA Left Mix", "LLNMIX2LPGA Switch", "LLNINMIX PGA"},

	{"PGA Right Mix", "RLNMIX2RPGA Switch", "RLNINMIX PGA"},
	{"PGA Right Mix", "RAXMIX2RPGA Switch", "RAXINMIX PGA"},
	{"PGA Right Mix", "RMNMIX2RPGA Switch", "RMONINMIX PGA"},
	{"PGA Right Mix", "LMNMIX2RPGA Switch", "LMONINMIX PGA"},
	{"PGA Right Mix", "LAXMIX2RPGA Switch", "LAXINMIX PGA"},

	/* lnmix */
	{"RLNINMIX PGA", NULL, "RLNIN Mix"},
	{"LLNINMIX PGA", NULL, "LLNIN Mix"},

	{"LLNIN Mix", "AINL2LLNMIX Switch", "AINL"},
	{"LLNIN Mix", "LLNMUX2LLNMIX Switch", "LLN Mux"},
	{"LLNIN Mix", "MIC1P2LLNMIX Switch", "MIC"},
	{"LLNIN Mix", "PMICDSE2LLNMIX Switch", "MIC BOOST"},

	{"RLNIN Mix", "AINR2RLNMIX Switch", "AINR"},
	{"RLNIN Mix", "RLNMUX2RLNMIX Switch", "RLN Mux"},
	{"RLNIN Mix", "MIC1N2LLNMIX Switch", "MIC"},
	{"RLNIN Mix", "NMICDSE2RLNMIX Switch", "MIC BOOST"},

	/* AXmix */
	{"RAXINMIX PGA", NULL, "RAXIN Mix"},
	{"LAXINMIX PGA", NULL, "LAXIN Mix"},

	{"LAXIN Mix", "LAXMUX2LAXMIX Switch", "LAX Mux"},
	{"LAXIN Mix", "MONOP2LAXMIX Switch", "MONOINP"},
	{"LAXIN Mix", "MIC2P2LAXMIX Switch", "MIC"},
	{"LAXIN Mix", "PMICDSE2LAXMIX Switch", "MIC BOOST"},

	{"RAXIN Mix", "RAXMUX2RAXMIX Switch", "RAX Mux"},
	{"RAXIN Mix", "MONON2RAXMIX Switch", "MONOINN"},
	{"RAXIN Mix", "MIC2N2RAXMIX Switch", "MIC"},
	{"RAXIN Mix", "NMICDSE2RAXMIX Switch", "MIC BOOST"},

	/* MNmix */
	{"RMONINMIX PGA", NULL, "RMONIN Mix"},
	{"LMONINMIX PGA", NULL, "LMONIN Mix"},

	{"LMONIN Mix", "LDAC2LMNMIX Switch", "Left DAC"},
	{"LMONIN Mix", "MONOP2LMNMIX Switch", "MONOINP"},
	{"LMONIN Mix", "AINL2LMNMIX Switch", "AINL"},

	{"RMONIN Mix", "RDAC2RMNMIX Switch", "Right DAC"},
	{"RMONIN Mix", "MONON2RMNMIX Switch", "MONOINN"},
	{"RMONIN Mix", "AINR2RMNMIX Switch", "AINR"},

	/* Analog mic mux */
	{"MIC BOOST", NULL, "AMIC Mux"},

	{"AMIC Mux", "Switch", "MIC"},
	{"MIC", NULL, "MIC Bias"},
	/* capature */
	{"ADC Left", NULL, "LPGA P"},
	{"ADC Right", NULL, "RPGA P"},

	{"ADC_1", "Switch", "ADC Left"},
	{"ADC_1", "Switch", "ADC Right"},

	/* digital mixer */
	{"Equalizer", NULL, "Digital Left Mixer"},
	{"Equalizer", NULL, "Digital Right Mixer"},

	{"Digital Left Mixer", NULL, "LDMIX1 Mux"},
	{"Digital Left Mixer", NULL, "LDMIX2 Mux"},

	{"Digital Right Mixer", NULL, "RDMIX1 Mux"},
	{"Digital Right Mixer", NULL, "RDMIX2 Mux"},

	{"LDMIX1 Mux", "left SDP1 in", "VOICESDPIL"},
	{"LDMIX1 Mux", "left SDP2 in", "MASTERSDPIL"},
	{"LDMIX1 Mux", "left SDP3 in", "AUXSDPIL"},
	{"LDMIX1 Mux", "left ADC out", "ADC_1"},
	{"LDMIX1 Mux", "right SDP1 in", "VOICESDPIR"},
	{"LDMIX1 Mux", "right SDP2 in", "MASTERSDPIR"},
	{"LDMIX1 Mux", "right SDP3 in", "AUXSDPIR"},
	{"LDMIX1 Mux", "right ADC out", "ADC_1"},

	{"RDMIX1 Mux", "left SDP1 in", "VOICESDPIL"},
	{"RDMIX1 Mux", "left SDP2 in", "MASTERSDPIL"},
	{"RDMIX1 Mux", "left SDP3 in", "AUXSDPIL"},
	{"RDMIX1 Mux", "left ADC out", "ADC_1"},
	{"RDMIX1 Mux", "right SDP1 in", "VOICESDPIR"},
	{"RDMIX1 Mux", "right SDP2 in", "MASTERSDPIR"},
	{"RDMIX1 Mux", "right SDP3 in", "AUXSDPIR"},
	{"RDMIX1 Mux", "right ADC out", "ADC_1"},

	{"LDMIX2 Mux", "left SDP1 in", "VOICESDPIL"},
	{"LDMIX2 Mux", "left SDP2 in", "MASTERSDPIL"},
	{"LDMIX2 Mux", "left SDP3 in", "AUXSDPIL"},
	{"LDMIX2 Mux", "left ADC out", "ADC_1"},
	{"LDMIX2 Mux", "right SDP1 in", "VOICESDPIR"},
	{"LDMIX2 Mux", "right SDP2 in", "MASTERSDPIR"},
	{"LDMIX2 Mux", "right SDP3 in", "AUXSDPIR"},
	{"LDMIX2 Mux", "right ADC out", "ADC_1"},

	{"RDMIX2 Mux", "left SDP1 in", "VOICESDPIL"},
	{"RDMIX2 Mux", "left SDP2 in", "MASTERSDPIL"},
	{"RDMIX2 Mux", "left SDP3 in", "AUXSDPIL"},
	{"RDMIX2 Mux", "left ADC out", "ADC_1"},
	{"RDMIX2 Mux", "right SDP1 in", "VOICESDPIR"},
	{"RDMIX2 Mux", "right SDP2 in", "MASTERSDPIR"},
	{"RDMIX2 Mux", "right SDP3 in", "AUXSDPIR"},
	{"RDMIX2 Mux", "right ADC out", "ADC_1"},

	/* VOICE/SDP1 AIF IN mixer */
	{"VOICEIN AIF Mixer", NULL, "VOICESDPIL"},
	{"VOICEIN AIF Mixer", NULL, "VOICESDPIR"},
	/* master/SDP2 AIF IN mixer */
	{"MASTERIN AIF Mixer", NULL, "MASTERSDPIL"},
	{"MASTERIN AIF Mixer", NULL, "MASTERSDPIR"},
	/* aux/SDP3 AIF IN mixer */
	{"AUXIN AIF Mixer", NULL, "AUXSDPIL"},
	{"AUXIN AIF Mixer", NULL, "AUXSDPIR"},
	/* VOICE/SDP1 AIF OUT */
	{"VOICESDPOL", NULL, "VOICESDPO Mux"},
	{"VOICESDPOR", NULL, "VOICESDPO Mux"},

	{"VOICESDPO Mux", "ADC out", "ADC_1"},
	{"VOICESDPO Mux", "SDP1 in", "VOICEIN AIF Mixer"},
	{"VOICESDPO Mux", "SDP2 in", "MASTERIN AIF Mixer"},
	{"VOICESDPO Mux", "SDP3 in", "AUXIN AIF Mixer"},
	{"VOICESDPO Mux", "EQ stereo", "Equalizer"},
	{"VOICESDPO Mux", "EQ left", "Digital Left Mixer"},
	{"VOICESDPO Mux", "EQ right", "Digital Right Mixer"},

	/* master/SDP2 AIF OUT */
	{"MASTERSDPOL", NULL, "MASTERSDPO Mux"},
	{"MASTERSDPOR", NULL, "MASTERSDPO Mux"},

	{"MASTERSDPO Mux", "ADC out", "ADC_1"},
	{"MASTERSDPO Mux", "SDP1 in", "VOICEIN AIF Mixer"},
	{"MASTERSDPO Mux", "SDP2 in", "MASTERIN AIF Mixer"},
	{"MASTERSDPO Mux", "SDP3 in", "AUXIN AIF Mixer"},
	{"MASTERSDPO Mux", "EQ stereo", "Equalizer"},
	{"MASTERSDPO Mux", "EQ left", "Digital Left Mixer"},
	{"MASTERSDPO Mux", "EQ right", "Digital Right Mixer"},

	/* AUX/SDP3 AIF OUT */
	{"AUXSDPOL", NULL, "AUXSDPO Mux"},
	{"AUXSDPOR", NULL, "AUXSDPO Mux"},

	{"AUXSDPO Mux", "ADC out", "ADC_1"},
	{"AUXSDPO Mux", "SDP1 in", "VOICEIN AIF Mixer"},
	{"AUXSDPO Mux", "SDP2 in", "MASTERIN AIF Mixer"},
	{"AUXSDPO Mux", "SDP3 in", "AUXIN AIF Mixer"},
	{"AUXSDPO Mux", "EQ stereo", "Equalizer"},
	{"AUXSDPO Mux", "EQ left", "Digital Left Mixer"},
	{"AUXSDPO Mux", "EQ right", "Digital Right Mixer"},

	/* DAC */
	{"Left DAC", NULL, "DAC_1"},
	{"Right DAC", NULL, "DAC_1"},

	{"DAC_1", "Switch", "DACSRC Mux"},

	{"DACSRC Mux", "SDP1 in", "VOICEIN AIF Mixer"},
	{"DACSRC Mux", "SDP2 in", "MASTERIN AIF Mixer"},
	{"DACSRC Mux", "SDP3 in", "AUXIN AIF Mixer"},
	{"DACSRC Mux", "ADC out", "ADC_1"},
	{"DACSRC Mux", "EQ stereo", "Equalizer"},
	{"DACSRC Mux", "EQ left", "Digital Left Mixer"},
	{"DACSRC Mux", "EQ right", "Digital Right Mixer"},

	/* SPEAKER Paths */
	{"SPKOUTL", NULL, "SPK Amp"},
	{"SPKOUTR", NULL, "SPK Amp"},

	{"SPK Amp", "Switch", "SPKL Mix"},
	{"SPK Amp", "Switch", "SPKR Mix"},
	/*
	 * {"SPK Amp", "Switch", "SPKL Mux"},
	 * {"SPK Amp", "Switch", "SPKR Mux"},
	 *
	 * {"SPKL Mux", "SPKR Route", "SPKR Mix"},
	 * {"SPKL Mux", "SPKL Route", "SPKL Mix"},
	 *
	 * {"SPKR Mux", "SPKR Route", "SPKR Mix"},
	 * {"SPKR Mux", "SPKL Route", "SPKL Mix"},
	 */
	{"SPKL Mix", "LLNMUX2SPKMIX Switch", "LLN Mux"},
	{"SPKL Mix", "LAXMUX2SPKMIX Switch", "LAX Mux"},
	{"SPKL Mix", "LDAC2SPKMIX Switch", "Left DAC"},

	{"SPKR Mix", "RLNMUX2SPKMIX Switch", "RLN Mux"},
	{"SPKR Mix", "RAXMUX2SPKMIX Switch", "RAX Mux"},
	{"SPKR Mix", "RDAC2SPKMIX Switch", "Right DAC"},

	/* HEADPHONE Paths */
	{"HPL", NULL, "HP Amp"},
	{"HPR", NULL, "HP Amp"},

	{"HP Amp", "Switch", "HPL Mix"},
	{"HP Amp", "Switch", "HPR Mix"},

	{"HPL Mix", "LNMUX2HPMIX_L Switch", "LLN Mux"},
	{"HPL Mix", "AXMUX2HPMIX_L Switch", "LAX Mux"},
	{"HPL Mix", "DACL2HPMIX Switch", "Left DAC"},

	{"HPR Mix", "LNMUX2HPMIX_R Switch", "RLN Mux"},
	{"HPR Mix", "AXMUX2HPMIX_R Switch", "RAX Mux"},
	{"HPR Mix", "DACR2HPMIX Switch", "Right DAC"},

	/* EARPIECE Paths */
	{"MONOOUTP", NULL, "MNOUTP PGA"},
	{"MONOOUTN", NULL, "MNOUTN PGA"},

	{"MNOUTP PGA", NULL, "MNOUTP Mix"},
	{"MNOUTN PGA", NULL, "MNOUTN Mix"},

	{"MNOUTP Mix", "LHPMIX2MNMIXP Switch", "HPL Mix"},
	{"MNOUTP Mix", "RHPMIX2MNOMIXP Switch", "HPR Mix"},
	{"MNOUTP Mix", "RMNMIX2MNOMIXP Switch", "RMONINMIX PGA"},
	{"MNOUTP Mix", "RAXMIX2MNOMIXP Switch", "RAXINMIX PGA"},
	{"MNOUTP Mix", "LLNMIX2MNOMIXP Switch", "LLNINMIX PGA"},

	{"MNOUTN Mix", "LMNMIX2MNMIXN Switch", "LMONINMIX PGA"},
	{"MNOUTN Mix", "RHPMIX2MNOMIXN Switch", "HPR Mix"},
	{"MNOUTN Mix", "MOPINV2MNOMIXN Switch", "MNOUTP Mix"},
	{"MNOUTN Mix", "LLNMIX2MNOMIXN Switch", "LLNINMIX PGA"},
	{"MNOUTN Mix", "LAXMIX2MNOMIXN Switch", "LAXINMIX PGA"},

	/* LNOUT Paths */
	{"LOUTP", NULL, "LNOUTMIX1 PGA"},
	{"ROUTN", NULL, "RNOUTMIX1 PGA"},

	{"LNOUTMIX1 PGA", NULL, "LOUT1 Mix"},
	{"RNOUTMIX1 PGA", NULL, "ROUT1 Mix"},

	{"LOUT1 Mix", "LDAC2LO1MIXP Switch", "Left DAC"},
	{"LOUT1 Mix", "LAXMIX2LO1MIXP Switch", "LAXINMIX PGA"},
	{"LOUT1 Mix", "LLNMIX2LO1MIXP Switch", "LLNINMIX PGA"},
	{"LOUT1 Mix", "LMNMIX2LO1MIXP Switch", "LMONINMIX PGA"},
	{"LOUT1 Mix", "RO1INV2LO1MIXP Switch", "ROUT1 Mix"},

	{"ROUT1 Mix", "RDAC2RO1MIXN Switch", "Right DAC"},
	{"ROUT1 Mix", "RAXMIX2RO1MIXN Switch", "RAXINMIX PGA"},
	{"ROUT1 Mix", "RLNMIX2RO1MIXN Switch", "RLNINMIX PGA"},
	{"ROUT1 Mix", "RMNMIX2RO1MIXN Switch", "RMONINMIX PGA"},
	{"ROUT1 Mix", "LO1INV2RO1MIXN Switch", "LOUT1 Mix"},
};

struct _pll_div {
	u32 pll_in;
	u32 pll_out;
	u8 mclkdiv;
	u8 plldiv;
	u8 n;
	u8 k1;
	u8 k2;
	u8 k3;
};

static const struct _pll_div codec_pll_div[] = {
	{7500000, 11289600, 1, 8, 12, 0x01, 0xc6, 0xee},
	{7500000, 12288000, 1, 8, 13, 0x04, 0x82, 0x90},

	{7600000, 11289600, 1, 8, 11, 0x25, 0x2e, 0x93},
	{7600000, 12288000, 1, 8, 12, 0x27, 0x53, 0x49},

	{8192000, 11289600, 1, 8, 11, 0x01, 0x0d, 0x41},
	{8192000, 12288000, 1, 8, 12, 0x00, 0x00, 0x01},

	{8380000, 11289600, 1, 8, 10, 0x20, 0xb7, 0x8d},
	{8380000, 12288000, 1, 8, 11, 0x1e, 0xbe, 0xb7},

	{9000000, 11289600, 1, 8, 10, 0x01, 0x7b, 0x1c},
	{9000000, 12288000, 1, 8, 10, 0x26, 0xd1, 0x4a},

	{9600000, 11289600, 1, 8, 9, 0x11, 0x2a, 0x3c},
	{9600000, 12288000, 1, 8, 10, 0x0a, 0x18, 0xd8},

	{9800000, 11289600, 1, 8, 9, 0x09, 0x16, 0x5c},
	{9800000, 12288000, 1, 8, 10, 0x01, 0x4e, 0x18},

	{10000000, 11289600, 1, 8, 9, 0x01, 0x55, 0x33},
	{10000000, 12288000, 1, 8, 9, 0x22, 0xef, 0x8f},

	{11059200, 11289600, 1, 8, 8, 0x07, 0x03, 0x07},
	{11059200, 12288000, 1, 8, 8, 0x25, 0x65, 0x7f},

	{11289600, 11289600, 1, 8, 8, 0x00, 0x00, 0x01},
	{11289600, 12288000, 1, 8, 8, 0x1d, 0xc3, 0xb8},

	{11500000, 11289600, 1, 8, 7, 0x23, 0xe9, 0xcd},
	{11500000, 12288000, 1, 8, 8, 0x17, 0x0f, 0xee},

	{12000000, 11289600, 1, 8, 7, 0x16, 0x25, 0x6c},
	{12000000, 12288000, 1, 8, 8, 0x08, 0x13, 0xe0},

	{12288000, 11289600, 1, 8, 7, 0x0e, 0xb9, 0x90},
	{12288000, 12288000, 1, 8, 8, 0x00, 0x00, 0x01},

	{12500000, 11289600, 1, 8, 7, 0x09, 0x7a, 0xff},
	{12500000, 12288000, 1, 8, 7, 0x24, 0x5c, 0xe2},

	{12800000, 11289600, 1, 8, 7, 0x02, 0x5b, 0x21},
	{12800000, 12288000, 1, 8, 7, 0x1c, 0x9b, 0xb9},

	{13000000, 11289600, 1, 8, 6, 0x27, 0xdc, 0x2b},
	{13000000, 12288000, 1, 8, 7, 0x17, 0xa3, 0x2f},

	{13500000, 11289600, 1, 8, 6, 0x1d, 0x08, 0xdc},
	{13500000, 12288000, 1, 8, 7, 0x0b, 0xda, 0xcc},

	{13560000, 11289600, 1, 8, 6, 0x1b, 0xca, 0x0a},
	{13560000, 12288000, 1, 8, 7, 0x0a, 0x7f, 0xc7},

	{14000000, 11289600, 1, 8, 6, 0x12, 0xfb, 0x81},
	{14000000, 12288000, 1, 8, 7, 0x00, 0xe9, 0xdd},

	{15000000, 11289600, 1, 8, 6, 0x00, 0xe3, 0x77},
	{15000000, 12288000, 1, 8, 6, 0x17, 0x4a, 0x5f},

	{15360000, 11289600, 1, 8, 5, 0x25, 0x05, 0xc2},
	{15360000, 12288000, 1, 8, 6, 0x10, 0xd4, 0x12},

	{16000000, 11289600, 1, 8, 5, 0x1b, 0x20, 0x9d},
	{16000000, 12288000, 1, 8, 6, 0x06, 0x0e, 0xe8},

	{16384000, 11289600, 1, 8, 5, 0x15, 0x8f, 0xb8},
	{16384000, 12288000, 1, 8, 6, 0x00, 0x00, 0x01},

	{16800000, 11289600, 1, 8, 5, 0x0f, 0xd1, 0x96},
	{16800000, 12288000, 1, 8, 5, 0x23, 0xd2, 0x0a},

	{18432000, 11289600, 2, 8, 9, 0x21, 0xa8, 0x25},
	{18432000, 12288000, 2, 8, 10, 0x1c, 0x0c, 0x1f},

	{19200000, 11289600, 2, 8, 9, 0x11, 0x2a, 0x3c},
	{19200000, 12288000, 2, 8, 10, 0x0a, 0x18, 0xd8},

	{19800000, 11289600, 2, 8, 9, 0x05, 0x2b, 0xc0},
	{19800000, 12288000, 2, 8, 9, 0x27, 0x1d, 0x01},

	{20000000, 11289600, 2, 8, 9, 0x01, 0x55, 0x33},
	{20000000, 12288000, 2, 8, 9, 0x22, 0xef, 0x8f},

	{22118400, 11289600, 2, 8, 8, 0x07, 0x03, 0x07},
	{22118400, 12288000, 2, 8, 8, 0x25, 0x65, 0x7f},

	{22579200, 11289600, 2, 8, 8, 0x00, 0x00, 0x01},
	{22579200, 12288000, 2, 8, 8, 0x1d, 0xc3, 0xb8},

	{24000000, 11289600, 2, 8, 7, 0x16, 0x25, 0x6c},
	{24000000, 12288000, 2, 8, 8, 0x08, 0x13, 0xe0},

	{24576000, 11289600, 2, 8, 7, 0x0e, 0xb9, 0x90},
	{24576000, 12288000, 2, 8, 8, 0x00, 0x00, 0x01},

	{25000000, 11289600, 2, 8, 7, 0x09, 0x7a, 0xff},
	{25000000, 12288000, 2, 8, 7, 0x24, 0x5c, 0xe2},

	{26000000, 11289600, 2, 8, 6, 0x27, 0xdc, 0x2b},
	{26000000, 12288000, 2, 8, 7, 0x17, 0xa3, 0x2f},

	{27000000, 11289600, 2, 8, 6, 0x1d, 0x08, 0xdc},
	{27000000, 12288000, 2, 8, 7, 0x0b, 0xda, 0xcc},

	{30000000, 11289600, 2, 8, 6, 0x00, 0xe3, 0x77},
	{30000000, 12288000, 2, 8, 6, 0x17, 0x4a, 0x5f},
};

static int es8396_set_pll(struct snd_soc_dai *dai, int pll_id,
			  int source, unsigned int freq_in,
			  unsigned int freq_out)
{
	int i;
	struct snd_soc_codec *codec = dai->codec;
	struct es8396_private *priv = snd_soc_codec_get_drvdata(codec);
	u16 reg;
	u8 N, K1, K2, K3, mclk_div, pll_div, tmp;

	switch (pll_id) {
	case ES8396_PLL:
		break;
	default:
		return -EINVAL;
	}
	/* Disable PLL, power down and hold in reset state */
	snd_soc_write(codec, ES8396_PLL_CTRL_1_REG02, 0x81);

	if (!freq_in || !freq_out)
		return 0;

	switch (source) {
	case ES8396_PLL_NO_SRC_0:
		/* Allow no source specification when stopping */
		if (freq_out)
			return -EINVAL;
		reg = snd_soc_read(codec, ES8396_CLK_SRC_SEL_REG01);
		reg &= 0xF0;
		if (source == 0)
			reg |= 0x01;	/* clksrc2= 0, clksrc1 = 1 */
		else
			reg |= 0x09;	/* clksrc2= 1, clksrc1 = 1 */

		snd_soc_write(codec, ES8396_CLK_SRC_SEL_REG01, reg);
		reg = snd_soc_read(codec, ES8396_CLK_CTRL_REG08);
		reg |= 0x0F;
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, reg);
		pr_debug("ES8396 PLL No Clock source\n");
		break;
	case ES8396_PLL_SRC_FRM_MCLK:
		reg = snd_soc_read(codec, ES8396_CLK_SRC_SEL_REG01);
		reg &= 0xF3;
		reg |= 0x04;	/* clksrc2= mclk */
		/* use clk2 for pll clk source */
		snd_soc_write(codec, ES8396_CLK_SRC_SEL_REG01, reg);
		reg = snd_soc_read(codec, ES8396_CLK_CTRL_REG08);
		reg |= 0x0F;
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, reg);
		pr_debug("ES8396 PLL Clock Source from MCLK pin\n");
		break;
	case ES8396_PLL_SRC_FRM_BCLK:
		reg = snd_soc_read(codec, ES8396_CLK_SRC_SEL_REG01);
		reg &= 0xF3;
		reg |= 0x0c;	/* clksrc2= bclk, */
		/* use clk2 for pll clk source */
		snd_soc_write(codec, ES8396_CLK_SRC_SEL_REG01, reg);
		reg = snd_soc_read(codec, ES8396_CLK_CTRL_REG08);
		reg |= 0x0F;
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, reg);
		pr_debug("ES8396 PLL Clock Source from BCLK signal\n");
		break;
	default:
		return -EINVAL;
	}
	/* get N & K */
	tmp = 0;
	if ((source == ES8396_PLL_SRC_FRM_MCLK) ||
	    (source == ES8396_PLL_SRC_FRM_BCLK)) {
		for (i = 0; i < ARRAY_SIZE(codec_pll_div); i++) {
			if (codec_pll_div[i].pll_in == freq_in &&
			    codec_pll_div[i].pll_out == freq_out) {
				/* PLL source from MCLK */
				mclk_div = codec_pll_div[i].mclkdiv;
				pll_div = codec_pll_div[i].plldiv;
				N = codec_pll_div[i].n;
				K3 = codec_pll_div[i].k1;
				K2 = codec_pll_div[i].k2;
				K1 = codec_pll_div[i].k3;
				tmp = 1;
				break;
			}
		}

		if (tmp == 1) {
			pr_debug("MCLK DIV=%d PLL DIV=%d PLL CLOCK SOURCE=%dHz\n",
				 mclk_div, pll_div, freq_in);
			pr_debug("N=%d, K3=%d, K2=%d, K1=%d\n", N, K3, K2, K1);

			/* set N & K */
			snd_soc_write(codec, ES8396_PLL_N_REG04, N);
			snd_soc_write(codec, ES8396_PLL_K2_REG05, K3);
			snd_soc_write(codec, ES8396_PLL_K1_REG06, K2);
			snd_soc_write(codec, ES8396_PLL_K0_REG07, K1);
			if (mclk_div == 1)
				/* mclk div2 = 0 */
				snd_soc_update_bits(codec,
						    ES8396_CLK_SRC_SEL_REG01,
						    0x10, 0x00);
			else
				/* mclk div2 = 1 */
				snd_soc_update_bits(codec,
						    ES8396_CLK_SRC_SEL_REG01,
						    0x10, 0x10);

			/* pll div 8 */
			snd_soc_update_bits(codec, ES8396_PLL_CTRL_1_REG02,
					    0x3, 0x01);

			/* configure the pll power voltage  */
			switch (priv->dvdd_pwr_vol) {
			case 0x18:
				/* dvdd=1.8v */
				snd_soc_update_bits(codec,
						    ES8396_PLL_CTRL_2_REG03,
						    0x0c, 0x00);
				break;
			case 0x25:
				/* dvdd=2.5v */
				snd_soc_update_bits(codec,
						    ES8396_PLL_CTRL_2_REG03,
						    0x0c, 0x04);
				break;
			case 0x33:
				/* dvdd=3.3v */
				snd_soc_update_bits(codec,
						    ES8396_PLL_CTRL_2_REG03,
						    0x0c, 0x08);
				break;
			default:
				/* dvdd=1.8v */
				snd_soc_update_bits(codec,
						    ES8396_PLL_CTRL_2_REG03,
						    0x0c, 0x00);
				break;
			}
			/* enable PLL analog power up  */
			snd_soc_update_bits(codec, ES8396_PLL_CTRL_1_REG02,
					    0x80, 0x00);
			/* pll digital on */
			snd_soc_update_bits(codec, ES8396_PLL_CTRL_1_REG02,
					    0x40, 0x40);
			priv->mclk[dai->id - 1] = freq_out;
			snd_soc_write(codec, ES8396_PLL_N_REG04, 0x08);
			snd_soc_write(codec, ES8396_PLL_K2_REG05, 0X1D);
			snd_soc_write(codec, ES8396_PLL_K1_REG06, 0XC3);
			snd_soc_write(codec, ES8396_PLL_K0_REG07, 0XB8);
		} else {
			pr_debug("Can not find the correct clock frequency!!!!!\n");
		}
	}
	return 0;
}

/*
 * if PLL not be used, use internal clk1 for mclk,
 * otherwise, use internal clk2 for PLL source.
 */
static int es8396_set_dai_sysclk(struct snd_soc_dai *dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct es8396_private *priv = snd_soc_codec_get_drvdata(codec);
	u8 reg;

	switch (dai->id) {
	case ES8396_SDP1:
	case ES8396_SDP2:
	case ES8396_SDP3:
		break;
	default:
		return -EINVAL;
		break;
	}
	switch (clk_id) {
	/* the clock source form MCLK pin, don't use PLL */
	case ES8396_CLKID_MCLK:
		reg = snd_soc_read(codec, ES8396_CLK_SRC_SEL_REG01);
		reg &= 0xFC;
		reg |= 0x00;	/* clksrc1= mclk */
		snd_soc_write(codec, ES8396_CLK_SRC_SEL_REG01, reg);

		/* always use clk1 */
		reg = snd_soc_read(codec, ES8396_CLK_CTRL_REG08);
		reg &= 0xf0;
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, reg);

		priv->sysclk[dai->id] = clk_id;
		priv->mclk[dai->id] = freq;
		if (freq > 19600000) {
			/* mclk div2 */
			snd_soc_update_bits(codec, ES8396_CLK_SRC_SEL_REG01,
					    0x10, 0x10);
		}
		switch (dai->id) {
		case ES8396_SDP1:
			/* bclkdiv m1 use clk1 */
			snd_soc_update_bits(codec, ES8396_BCLK_DIV_M1_REG0E,
					    0x20, 0x00);
			/* lrckdiv m3 use clk1 */
			snd_soc_update_bits(codec, ES8396_LRCK_DIV_M3_REG10,
					    0x20, 0x00);
			break;
		case ES8396_SDP2:
		case ES8396_SDP3:
			/* bclkdiv m1 use clk1 */
			snd_soc_update_bits(codec, ES8396_BCLK_DIV_M2_REG0F,
					    0x20, 0x00);
			/* lrckdiv m4 use clk1 */
			snd_soc_update_bits(codec, ES8396_LRCK_DIV_M4_REG11,
					    0x20, 0x00);
			break;
		default:
			break;
		}
		pr_debug("ES8396 using MCLK as SYSCLK at %uHz\n", freq);
		break;
	/* the clock source form internal BCLK signal, don't use PLL */
	case ES8396_CLKID_BCLK:
		reg = snd_soc_read(codec, ES8396_CLK_SRC_SEL_REG01);
		reg &= 0xFC;
		reg |= 0x03;	/* clksrc1= bclk */
		snd_soc_write(codec, ES8396_CLK_SRC_SEL_REG01, reg);
		/* always use clk1 */
		reg = snd_soc_read(codec, ES8396_CLK_CTRL_REG08);
		reg &= 0xf0;
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, reg);

		priv->sysclk[dai->id] = clk_id;
		priv->mclk[dai->id] = freq;
		if (freq > 19600000) {
			/* mclk div2 */
			snd_soc_update_bits(codec, ES8396_CLK_SRC_SEL_REG01,
					    0x10, 0x10);
		}
		switch (dai->id) {
		case ES8396_SDP1:
			/* bclkdiv m1 use clk1 */
			snd_soc_update_bits(codec, ES8396_BCLK_DIV_M1_REG0E,
					    0x20, 0x00);
			/* lrckdiv m3 use clk1 */
			snd_soc_update_bits(codec, ES8396_LRCK_DIV_M3_REG10,
					    0x20, 0x00);
			break;
		case ES8396_SDP2:
		case ES8396_SDP3:
			/* bclkdiv m1 use clk1 */
			snd_soc_update_bits(codec, ES8396_BCLK_DIV_M2_REG0F,
					    0x20, 0x00);
			/* lrckdiv m4 use clk1 */
			snd_soc_update_bits(codec, ES8396_LRCK_DIV_M4_REG11,
					    0x20, 0x00);
			break;
		default:
			break;
		}
		pr_debug("ES8396 using BCLK as SYSCLK at %uHz\n", freq);
		break;
	case ES8396_CLKID_PLLO:
		priv->sysclk[dai->id] = ES8396_CLKID_PLLO;
		switch (dai->id) {
		case ES8396_SDP1:
			/* bclkdiv m1 use clk1 */
			snd_soc_update_bits(codec, ES8396_BCLK_DIV_M1_REG0E,
					    0x20, 0x00);
			/* lrckdiv m3 use clk1 */
			snd_soc_update_bits(codec, ES8396_LRCK_DIV_M3_REG10,
					    0x20, 0x00);
			break;
		case ES8396_SDP2:
			/* bclkdiv m1 use clk2 */
			snd_soc_update_bits(codec, ES8396_BCLK_DIV_M1_REG0E,
					    0x20, 0x20);
			/* lrckdiv m3 use clk2 */
			snd_soc_update_bits(codec, ES8396_LRCK_DIV_M3_REG10,
					    0x20, 0x20);
			break;
		case ES8396_SDP3:
			/* bclkdiv m1 use clk2 */
			snd_soc_update_bits(codec, ES8396_BCLK_DIV_M2_REG0F,
					    0x20, 0x20);
			/* lrckdiv m4 use clk2 */
			snd_soc_update_bits(codec, ES8396_LRCK_DIV_M4_REG11,
					    0x20, 0x20);
			break;
		default:
			break;
		}
		pr_debug("ES8396 using PLL Output as SYSCLK\n");
		break;
	default:
		pr_err("ES8396 System clock error\n");
		return -EINVAL;
	}
	return 0;
}

struct es8396_mclk_div {
	u32 mclk;
	u32 srate;
	u8 lrcdiv;
	u8 bclkdiv;
};

static struct es8396_mclk_div es8396_mclk_coeffs[] = {
	/* MCLK, Sample Rate, lrckdiv, bclkdiv */
	{5644800, 11025, 0x04, 0x08},
	{5644800, 22050, 0x02, 0x04},
	{5644800, 44100, 0x00, 0x02},

	{6000000, 8000, 0x17, 0x0f},
	{6000000, 11025, 0x16, 0x08},
	{6000000, 12000, 0x15, 0x0a},
	{6000000, 16000, 0x14, 0x05},
	{6000000, 22050, 0x13, 0x04},
	{6000000, 24000, 0x12, 0x05},
	{6000000, 44100, 0x11, 0x02},
	{6000000, 48000, 0x10, 0x01},

	{6144000, 8000, 0x06, 0x0c},
	{6144000, 12000, 0x04, 0x08},
	{6144000, 16000, 0x03, 0x06},
	{6144000, 24000, 0x02, 0x04},
	{6144000, 32000, 0x01, 0x03},
	{6144000, 48000, 0x00, 0x02},

	{8192000, 8000, 0x07, 0x10},
	{8192000, 16000, 0x04, 0x08},
	{8192000, 32000, 0x02, 0x04},

	{11289600, 11025, 0x07, 0x10},
	{11289600, 22050, 0x04, 0x08},
	{11289600, 44100, 0x02, 0x04},

	{12000000, 8000, 0x1b, 0x17},
	{12000000, 11025, 0x19, 0x11},
	{12000000, 12000, 0x18, 0x13},
	{12000000, 16000, 0x17, 0x0f},
	{12000000, 22050, 0x16, 0x08},
	{12000000, 24000, 0x15, 0x0a},
	{12000000, 32000, 0x14, 0x05},
	{12000000, 44100, 0x13, 0x04},
	{12000000, 48000, 0x12, 0x05},

	{12288000, 8000, 0x0a, 0x15},
	{12288000, 12000, 0x07, 0x10},
	{12288000, 16000, 0x06, 0x0c},
	{12288000, 24000, 0x04, 0x08},
	{12288000, 32000, 0x03, 0x06},
	{12288000, 48000, 0x02, 0x04},
};

static int es8396_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct es8396_private *priv = snd_soc_codec_get_drvdata(codec);
	u8 id = codec_dai->id;
	unsigned int inv, format;
	u8 spc, mmcc;

	switch (id) {
	case ES8396_SDP1:
		spc = snd_soc_read(codec, ES8396_SDP1_IN_FMT_REG1F) & 0x3f;
		mmcc = snd_soc_read(codec, ES8396_SDP_1_MS_REG12);
		break;
	case ES8396_SDP2:
		spc = snd_soc_read(codec, ES8396_SDP2_IN_FMT_REG22) & 0x3f;
		mmcc = snd_soc_read(codec, ES8396_SDP_2_MS_REG13);
		break;
	case ES8396_SDP3:
		spc = snd_soc_read(codec, ES8396_SDP3_IN_FMT_REG24) & 0x3f;
		mmcc = snd_soc_read(codec, ES8396_SDP_3_MS_REG14);
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		mmcc &= ~MS_MASTER;
		mmcc |= 0x24;
		if (id == ES8396_SDP1) {
			mmcc &= 0xe4;	/* select bclkm1,lrckm3 */
		} else {
			mmcc &= 0xe4;	/* select bclkm2,lrckm4 */
			mmcc |= 0x09;
		}
		break;

	case SND_SOC_DAIFMT_CBS_CFS:
		mmcc &= ~MS_MASTER;
		break;

	default:
		return -EINVAL;
	}

	format = (fmt & SND_SOC_DAIFMT_FORMAT_MASK);
	inv = (fmt & SND_SOC_DAIFMT_INV_MASK);

	switch (format) {
	case SND_SOC_DAIFMT_I2S:
		spc &= 0xC7;
		/* lrck polarity normal, TO CHECK THE L&R Inverted for i-net */
		spc |= 0x00;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		spc &= 0xC7;
		spc |= 0x18;	/* lrck polarity normal */
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		if (id == ES8396_SDP1) {
			spc &= 0xC7;
			spc |= 0x28;	/* lrck polarity normal */
		} else {
			pr_err("ES8396 SDP2&SDP3 don't Support Right Justified\n");
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		spc &= 0xC7;

		if (format == SND_SOC_DAIFMT_DSP_A)
			spc |= 0x30;
		else
			spc |= 0x38;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, ES8396_SDP1_IN_FMT_REG1F, 00);
	pr_debug("es8396_set_dai_fmt-->\n");

	priv->config[id].spc = spc;
	priv->config[id].mmcc = mmcc;

	return 0;
}

static int es8396_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct es8396_private *priv = snd_soc_codec_get_drvdata(codec);
	int id = dai->id;
	int mclk_coeff = 0;
	int srate = params_rate(params);
	u8 bdiv, lrdiv;

	pr_debug("DAI[%d]: MCLK= %u, srate= %u, lrckdiv= %x, bclkdiv= %x\n",
		 id, priv->mclk[0], srate,
		 es8396_mclk_coeffs[mclk_coeff].lrcdiv,
		 es8396_mclk_coeffs[mclk_coeff].bclkdiv);

	switch (id) {
	case ES8396_SDP1:
		bdiv = snd_soc_read(codec, ES8396_BCLK_DIV_M2_REG0F);
		bdiv &= 0xe0;
		bdiv |= es8396_mclk_coeffs[mclk_coeff].bclkdiv;
		lrdiv = snd_soc_read(codec, ES8396_LRCK_DIV_M4_REG11);
		lrdiv &= 0xe0;
		lrdiv |= 0x22;	/* es8396_mclk_coeffs[mclk_coeff].lrcdiv; */
		snd_soc_write(codec, ES8396_BCLK_DIV_M2_REG0F, bdiv);
		snd_soc_write(codec, ES8396_LRCK_DIV_M4_REG11, lrdiv);
		priv->config[id].srate = srate;
		priv->config[id].lrcdiv = lrdiv;
		priv->config[id].sclkdiv = bdiv;
		break;
	case ES8396_SDP2:
	case ES8396_SDP3:
		bdiv = snd_soc_read(codec, ES8396_BCLK_DIV_M1_REG0E);
		bdiv &= 0xe0;
		bdiv |= es8396_mclk_coeffs[mclk_coeff].bclkdiv;
		lrdiv = snd_soc_read(codec, ES8396_LRCK_DIV_M3_REG10);
		lrdiv &= 0xe0;
		lrdiv |= es8396_mclk_coeffs[mclk_coeff].lrcdiv;
		snd_soc_write(codec, ES8396_BCLK_DIV_M1_REG0E, bdiv);
		snd_soc_write(codec, ES8396_LRCK_DIV_M3_REG10, lrdiv);
		priv->config[id].srate = srate;
		priv->config[id].lrcdiv = lrdiv;
		priv->config[id].sclkdiv = bdiv;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int es8396_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u8 value;
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(codec);

	printk("es8396_set_bias_level = 0x%x\n",
                 snd_soc_codec_get_bias_level(codec));
	printk("bias_level = %d\n", level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		/*
		 * dac csm startup, dac digital still oN
		 * snd_soc_update_bits(codec, ES8396_DAC_CSM_REG66, 0xFF, 0x00);
		 * dac analog power on
		 * snd_soc_update_bits(codec, ES8396_DAC_REF_PWR_CTRL_REG6E,
		 * 0xff, 0x00);
		 */
		snd_soc_write(codec, 0x4E, 0x80);
		snd_soc_write(codec, 0x4F, 0x81);
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (es8396->aif1_select == 0 && es8396->aif2_select == 0) {
			if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
				snd_soc_write(codec, ES8396_SYS_VMID_REF_REG71,
					      0xFF);
				if (es8396_valid_analdo(es8396->ana_ldo_lvl)) {
					value = es8396->ana_ldo_lvl;
					value &= 0x07;
					snd_soc_write(codec, 0x70, value);
				}
			}
		}
		/*
		 * dac csm startup, dac digital still stop
		 * snd_soc_update_bits(codec, ES8396_DAC_CSM_REG66, 0xFF, 0x04);
		 * adc csm startup, adc digital still stop
		 * snd_soc_update_bits(codec, ES8396_ADC_CSM_REG53, 0xFF, 0x00);
		 */
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}

	return 0;
}

static int es8396_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_codec *codec = dai->codec;
	int id = dai->id;

	pr_debug("es8396_set_tristate\n");
	pr_debug("ES8396 SDP NUM = %d\n", id);
	switch (id) {
	case ES8396_SDP1:
		return snd_soc_update_bits(codec, ES8396_SDP1_DGAIN_TDM_REG21,
					   0x0a, 0x0a);
	case ES8396_SDP2:
	case ES8396_SDP3:
		pr_err("SDP NUM = %d, Can not support tristate\n", id);
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int es8396_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);
	int ret;
	int regv;
	int i;
	pr_debug(">>>>>>>>es8396_pcm_startup\n");
	ret = snd_soc_read(tron_codec, ES8396_ADC_CSM_REG53);
	pr_debug("ES8396_ADC_CSM_REG53===0x%x\n", ret);
	/*
	 * set the clock source to MCLK pin
	 * set divider for music playback
	 * set DAC source from SDP1 in
	 */
	if ((es8396->aif2_select & 0x01) == 0) {
		pr_debug(">>>>>>>>es8396_pcm_startup, only power on sdp1 for music\n");
		/* if don't have voice requirement */
		snd_soc_write(tron_codec, 0x1A, 0x00);
		snd_soc_write(tron_codec, 0x8, 0x10);
		snd_soc_write(tron_codec, 0xd, 0x00);
		snd_soc_write(tron_codec, 0x9, 0x04);
		snd_soc_write(tron_codec, 0x69, 0x00);
		snd_soc_write(tron_codec, 0x67, 0x00);
	} else {
		pr_debug(">>>>>>>>es8396_pcm_startup, already power on sdp2 for voice\n");
		snd_soc_write(tron_codec, 0x18, 0x00);	/* set eq source */
		snd_soc_write(tron_codec, 0x19, 0x51);	/* set eq source */
		/* if have voice requirement */
		snd_soc_write(tron_codec, 0x1A, 0x40);
		snd_soc_write(tron_codec, 0x8, 0x10);
		snd_soc_write(tron_codec, 0xd, 0x00);
		snd_soc_write(tron_codec, 0x9, 0x04);
		snd_soc_write(tron_codec, 0x67, 0x0c);
		snd_soc_write(tron_codec, 0x69, 0x04);
	}

	if (playback) {
		pr_debug(">>>>>>>>>>>es8396_pcm_startup playback\n");
		es8396->aif1_select |= 0x01;
		snd_soc_write(tron_codec, 0x66, 0x01);
		for (i = 0; i < 120; i = i + 2) {
			snd_soc_write(tron_codec, 0x6A, i + 1);
			snd_soc_write(tron_codec, 0x6B, i + 1);
			usleep_range(100, 200);
		}
		if (es8396->calibrate == 0) {
			pr_debug("Enter into %s  %d\n", __func__, __LINE__);
			es8396->calibrate = true;
		}
		schedule_delayed_work(&es8396->pcm_pop_work,
				      msecs_to_jiffies(10));

	} else {
		pr_debug(">>>>>>>>>>>es8396_pcm_startup capture\n");
		snd_soc_update_bits(tron_codec, ES8396_SDP1_OUT_FMT_REG20, 0x40,
				    0x40);
		/* set adc alc */
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_1_REG58, 0xC6);
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_2_REG59, 0x12);
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_4_REG5B, 0x04);
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_5_REG5C, 0xC8);
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_6_REG5D, 0x11);
		snd_soc_write(tron_codec, ES8396_ADC_ANALOG_CTRL_REG5E, 0x0);
		/* Enable MIC BOOST */
		snd_soc_write(tron_codec, ES8396_SYS_MIC_IBIAS_EN_REG75, 0x02);

		/* axMixer Gain boost */
		regv = snd_soc_read(tron_codec, ES8396_AX_MIXER_BOOST_REG2F);
		regv |= 0x88;
		snd_soc_write(tron_codec, ES8396_AX_MIXER_BOOST_REG2F, regv);
		/* axmixer vol = +12db */
		snd_soc_write(tron_codec, ES8396_AX_MIXER_VOL_REG30, 0xaa);
		/* axmixer high driver capacility */
		snd_soc_write(tron_codec, ES8396_AX_MIXER_REF_LP_REG31, 0x02);
		snd_soc_write(tron_codec, 0x33, 0);
		/* MNMixer Gain boost */
		regv = snd_soc_read(tron_codec, ES8396_MN_MIXER_BOOST_REG37);
		regv |= 0x88;
		snd_soc_write(tron_codec, ES8396_MN_MIXER_BOOST_REG37, regv);
		/* mnmixer vol = +12db */
		snd_soc_write(tron_codec, ES8396_MN_MIXER_VOL_REG38, 0x44);
		/* mnmixer high driver capacility */
		snd_soc_write(tron_codec, ES8396_MN_MIXER_REF_LP_REG39, 0x02);

		/* ADC STM and Digital Startup, ADC DS Mode */
		snd_soc_write(tron_codec, ES8396_ADC_CSM_REG53, 0x00);
		/* force adc stm to normal */
		snd_soc_write(tron_codec, ES8396_ADC_FORCE_REG77, 0x40);
		snd_soc_write(tron_codec, ES8396_ADC_FORCE_REG77, 0x0);
		/* ADC Volume =0db */
		snd_soc_write(tron_codec, ES8396_ADC_LADC_VOL_REG56, 0x0);
		snd_soc_write(tron_codec, ES8396_ADC_RADC_VOL_REG57, 0x0);
		snd_soc_write(tron_codec, ES8396_ADC_CLK_DIV_REG09, 0x04);
		es8396->aif1_select |= 0x02;
		schedule_delayed_work(&es8396->adc_depop_work,
				      msecs_to_jiffies(150));
	}
	return 0;
}

static void es8396_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);

	pr_debug(">>>>>>>>es8396_pcm_shutdown\n");

	/*
	 * mute SDP1 in and mute SDP1 out
	 */
	if (playback) {
		pr_debug(">>>>>>>>es8396_pcm_shutdown, playback\n");
		schedule_delayed_work(&es8396->pcm_shutdown_depop_work,
				      msecs_to_jiffies(20));
	} else {
		pr_debug(">>>>>>>>es8396_pcm_shutdown, capture\n");
		snd_soc_update_bits(tron_codec, ES8396_SDP1_OUT_FMT_REG20, 0x40,
				    0x40);
		es8396->aif1_select &= 0xfd;
	}
}

static int es8396_voice_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	unsigned int index;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);
	int regv;
	int i;

	pr_debug("****************es8396_voice_startup\n");

	if (playback) {
		pr_debug("****************es8396_voice_startup, playback\n");
		es8396->aif2_select |= 0x01;
		snd_soc_write(tron_codec, 0x4e, 0x84);
		snd_soc_write(tron_codec, 0x4f, 0x85);

		for (i = 0; i < 120; i = i + 2) {
			snd_soc_write(tron_codec, 0x6A, i + 1);
			snd_soc_write(tron_codec, 0x6B, i + 1);
			usleep_range(100, 200);
		}

		/* mute dac */
		snd_soc_write(tron_codec, 0x66, 0x01);
		/* DSP-B, 1st SCLK after LRCK edge, I2S2 SDPIN */
		snd_soc_update_bits(tron_codec, ES8396_SDP2_IN_FMT_REG22,
				    0x7F, 0x13);
		snd_soc_write(tron_codec, 0x18, 0x51);	/* set eq source */
		snd_soc_write(tron_codec, 0x19, 0x51);	/* set eq source */
		snd_soc_write(tron_codec, 0x8, 0x10);
		snd_soc_write(tron_codec, 0xd, 0x00);
		snd_soc_write(tron_codec, 0x9, 0x04);
		if ((es8396->aif1_select & 0x01) == 0) {
			/* if only voice */
			snd_soc_write(tron_codec, 0x67, 0x0c);
			snd_soc_write(tron_codec, 0x69, 0x04);
		} else {
			snd_soc_write(tron_codec, 0x67, 0x0c);
			snd_soc_write(tron_codec, 0x69, 0x04);
		}
		/* clk2 used as EQ clk, OSR = 6xFs for 8k resampling to 48k */
		snd_soc_write(tron_codec, ES8396_EQ_CLK_OSR_SEL_REG1C, 0x35);
		snd_soc_write(tron_codec, ES8396_SHARED_ADDR_REG1D, 0x00);

		for (index = 0; index < 59; index++) {
			snd_soc_write(tron_codec, ES8396_SHARED_DATA_REG1E,
				      es8396_equalizer_lpf_bt_incall[index]);
		}
		snd_soc_write(tron_codec, ES8396_SHARED_ADDR_REG1D, 0xbb);
		snd_soc_write(tron_codec, ES8396_SHARED_DATA_REG1E,
			      es8396_equalizer_lpf_bt_incall[59]);

		schedule_delayed_work(&es8396->voice_pop_work,
				      msecs_to_jiffies(50));
	} else {
		pr_debug("****************es8396_voice_startup, capture\n");
		es8396->aif2_select |= 0x02;
		/* set adc alc */
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_1_REG58, 0xC6);
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_2_REG59, 0x12);
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_4_REG5B, 0x04);
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_5_REG5C, 0xC8);
		snd_soc_write(tron_codec, ES8396_ADC_ALC_CTRL_6_REG5D, 0x11);
		snd_soc_write(tron_codec, ES8396_ADC_ANALOG_CTRL_REG5E, 0x0);
		snd_soc_write(tron_codec, ES8396_SYS_MIC_IBIAS_EN_REG75, 0x02);

		/* axMixer Gain boost */
		regv = snd_soc_read(tron_codec, ES8396_AX_MIXER_BOOST_REG2F);
		regv |= 0x88;
		snd_soc_write(tron_codec, ES8396_AX_MIXER_BOOST_REG2F, regv);
		/* axmixer vol = +12db */
		snd_soc_write(tron_codec, ES8396_AX_MIXER_VOL_REG30, 0xaa);
		/* axmixer high driver capacility */
		snd_soc_write(tron_codec, ES8396_AX_MIXER_REF_LP_REG31, 0x02);
		snd_soc_write(tron_codec, 0x33, 0);
		/* MNMixer Gain boost */
		regv = snd_soc_read(tron_codec, ES8396_MN_MIXER_BOOST_REG37);
		regv |= 0x88;
		snd_soc_write(tron_codec, ES8396_MN_MIXER_BOOST_REG37, regv);
		/* mnmixer vol = +12db */
		snd_soc_write(tron_codec, ES8396_MN_MIXER_VOL_REG38, 0x44);
		/* mnmixer high driver capacility */
		snd_soc_write(tron_codec, ES8396_MN_MIXER_REF_LP_REG39, 0x02);

		/* ADC STM and Digital Startup, ADC DS Mode */
		snd_soc_write(tron_codec, ES8396_ADC_CSM_REG53, 0x00);
		/* force adc stm to normal */
		snd_soc_write(tron_codec, ES8396_ADC_FORCE_REG77, 0x40);
		snd_soc_write(tron_codec, ES8396_ADC_FORCE_REG77, 0x0);
		/* ADC Volume =0db */
		snd_soc_write(tron_codec, ES8396_ADC_LADC_VOL_REG56, 0x0);
		snd_soc_write(tron_codec, ES8396_ADC_RADC_VOL_REG57, 0x0);

		/* clk2 used as EQ clk, OSR = 6xFs for 8k resampling to 48k */
		snd_soc_update_bits(tron_codec, ES8396_SDP2_OUT_FMT_REG23,
				    0x7F, 0x33);
	}
	return 0;
}

static void es8396_voice_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(tron_codec);
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	pr_debug("****************es8396_voice_shutdown\n");

	/* DSP-B, 1st SCLK after LRCK edge, I2S2 SDPIN */
	if (playback) {
		snd_soc_write(tron_codec, 0x66, 0x01);
		pr_debug("****************es8396_voice_shutdown, playback\n");
		schedule_delayed_work(&es8396->voice_shutdown_depop_work,
				      msecs_to_jiffies(10));
	} else {
		pr_debug("****************es8396_voice_shutdown, captuer\n");
		/* //DSP-B, 1st SCLK after LRCK edge, I2S2 SDPO */
		snd_soc_update_bits(tron_codec, ES8396_SDP2_OUT_FMT_REG23,
				    0x7F, 0x73);
		es8396->aif2_select &= 0xfd;
	}
}

/*
 * Only mute SDP IN(for dac)
 */
static int es8396_aif1_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(codec);
	pr_debug("es8396_aif1_mute id = %d, mute = %d", codec_dai->id, mute);
	if (mute) {
		if (es8396->spk_ctl_gpio != INVALID_GPIO &&
		    es8396->aif2_select == 0) {
			pr_debug("spk_ctl_gpio set %d\n", es8396->spk_ctl_gpio);
			gpio_set_value(es8396->spk_ctl_gpio, 0);
		}
		if (es8396->lineout_ctl_gpio != INVALID_GPIO &&
		    es8396->aif2_select == 0) {
			pr_debug("lineout_ctl_gpio set %d\n",
				 es8396->lineout_ctl_gpio);
			gpio_set_value(es8396->lineout_ctl_gpio, 0);
		}
		msleep(100);
	} else {
		if (es8396->spk_ctl_gpio != INVALID_GPIO) {
			pr_debug("spk_ctl_gpio set %d\n", es8396->spk_ctl_gpio);
			gpio_set_value(es8396->spk_ctl_gpio, 1);
		}
		if (es8396->lineout_ctl_gpio != INVALID_GPIO) {
			pr_debug("lineout_ctl_gpio set %d\n",
				 es8396->lineout_ctl_gpio);
			gpio_set_value(es8396->lineout_ctl_gpio, 1);
		}
	}

	return 0;
}

static int es8396_aif2_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(codec);

	pr_debug("es8396_aif2_mute id = %d, mute = %d", codec_dai->id, mute);

	if (mute) {
		if (es8396->spk_ctl_gpio != INVALID_GPIO &&
		    es8396->aif1_select == 0) {
			pr_debug("spk_ctl_gpio set %d\n", es8396->spk_ctl_gpio);
			gpio_set_value(es8396->spk_ctl_gpio, 0);
		}
		if (es8396->lineout_ctl_gpio != INVALID_GPIO &&
		    es8396->aif1_select == 0) {
			pr_debug("lineout_ctl_gpio set %d\n",
				 es8396->lineout_ctl_gpio);
			gpio_set_value(es8396->lineout_ctl_gpio, 0);
		}
		msleep(100);
	} else {
		if (es8396->spk_ctl_gpio != INVALID_GPIO) {
			pr_debug("spk_ctl_gpio set %d\n", es8396->spk_ctl_gpio);
			gpio_set_value(es8396->spk_ctl_gpio, 1);
		}
		if (es8396->lineout_ctl_gpio != INVALID_GPIO) {
			pr_debug("lineout_ctl_gpio set %d\n",
				 es8396->lineout_ctl_gpio);
			gpio_set_value(es8396->lineout_ctl_gpio, 1);
		}
	}
	return 0;
}

/* SNDRV_PCM_RATE_KNOT -> 12000, 24000 Hz, limit with constraint list */
#define ES8396_RATES (SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_KNOT)
#define ES8396_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops es8396_aif1_dai_ops = {
	.startup = es8396_pcm_startup,
	.shutdown = es8396_pcm_shutdown,
	.set_sysclk = es8396_set_dai_sysclk,
	.set_fmt = es8396_set_dai_fmt,
	.hw_params = es8396_pcm_hw_params,
	.digital_mute = es8396_aif1_mute,
	.set_pll = es8396_set_pll,
	.set_tristate = es8396_set_tristate,
};

static const struct snd_soc_dai_ops es8396_aif2_dai_ops = {
	.startup = es8396_voice_startup,
	.shutdown = es8396_voice_shutdown,
	.set_sysclk = es8396_set_dai_sysclk,
	.set_fmt = es8396_set_dai_fmt,
	.hw_params = es8396_pcm_hw_params,
	.digital_mute = es8396_aif2_mute,
	.set_pll = es8396_set_pll,
};

static struct snd_soc_dai_driver es8396_dai[] = {
	{
		.name = "es8396-aif1",
		.playback = {
			.stream_name = "SDP1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES8396_RATES,
			.formats = ES8396_FORMATS,
		},
		.capture = {
			.stream_name = "SDP1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES8396_RATES,
			.formats = ES8396_FORMATS,
		},
		.ops = &es8396_aif1_dai_ops,
	},
/*
	{
		.name = "es8396-aif2",
		.playback = {
			.stream_name = "SDP2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES8396_RATES,
			.formats = ES8396_FORMATS,
		},
		.capture = {
			.stream_name = "SDP2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES8396_RATES,
			.formats = ES8396_FORMATS,
		},
		.ops = &es8396_aif2_dai_ops,
	},
*/
};

static int es8396_suspend(struct device *dev)
{
	pr_debug("CODEC going into suspend mode\n");

	snd_soc_write(tron_codec, 0x4e, 0x84);
	snd_soc_write(tron_codec, 0x4f, 0x85);
	snd_soc_write(tron_codec, 0x66, 0x01);
	snd_soc_write(tron_codec, 0x6e, 0x00);

	return 0;
}

static int es8396_resume(struct device *dev)
{
	pr_debug("CODEC going into es8396_resume mode\n");

	usleep_range(20000, 21000);

	snd_soc_write(tron_codec, 0x6e, 0x00);
	snd_soc_write(tron_codec, 0x66, 0x00);
	snd_soc_write(tron_codec, 0x4e, 0x80);
	snd_soc_write(tron_codec, 0x4f, 0x81);
	return 0;
}

static int es8396_probe(struct snd_soc_codec *codec)
{
	int ret = 0, regv;
	u8 value;

	struct es8396_private *es8396 = snd_soc_codec_get_drvdata(codec);

	es8396->mclk_clock = devm_clk_get(codec->dev, "mclk");
        if (PTR_ERR(es8396->mclk_clock) == -EPROBE_DEFER)
                return -EPROBE_DEFER;

        ret = clk_prepare_enable(es8396->mclk_clock);
        if (ret)
                return ret;
/*
	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret < 0) {
		pr_err("Failed to set cache I/O: %d\n", ret);
		return ret;
	}
*/
	tron_codec = codec;
	regv = snd_soc_read(codec, ES8396_PLL_K2_REG05);

	if (regv == 0x00) {
		/*
		 * setup system analog control
		 */
		snd_soc_write(codec, ES8396_DLL_CTRL_REG0D, 0x20);
		snd_soc_write(codec, ES8396_CLK_SRC_SEL_REG01, 0x04);
		snd_soc_write(codec, ES8396_PLL_CTRL_1_REG02, 0xc1);
		snd_soc_write(codec, ES8396_PLL_CTRL_2_REG03, 0x00);
		snd_soc_write(codec, ES8396_PLL_N_REG04, 0x08);
		snd_soc_write(codec, ES8396_PLL_K2_REG05, 0X1d);
		snd_soc_write(codec, ES8396_PLL_K1_REG06, 0Xc3);
		snd_soc_write(codec, ES8396_PLL_K0_REG07, 0Xb8);
		snd_soc_write(codec, ES8396_PLL_CTRL_1_REG02, 0x41);

		/* adc,dac,cphp,class d clk enable,from clk2 */
		snd_soc_write(codec, ES8396_CLK_CTRL_REG08, 0x00);
		/* adc clk ratio=1 */
		snd_soc_write(codec, ES8396_ADC_CLK_DIV_REG09, 0x04);
		/* dac clk ratio=1 */
		snd_soc_write(codec, ES8396_DAC_CLK_DIV_REG0A, 0x01);
		snd_soc_write(codec, ES8396_BCLK_DIV_M2_REG0F, 0x24);
		snd_soc_write(codec, ES8396_LRCK_DIV_M4_REG11, 0x22);

		msleep(50);
		snd_soc_write(codec, ES8396_SYS_VMID_REF_REG71, 0xFC);
		snd_soc_write(codec, 0x72, 0xFF);
		snd_soc_write(codec, 0x73, 0xFF);
		if (es8396_valid_analdo(es8396->ana_ldo_lvl) == false) {
			pr_err("Analog LDO Level error.\n");
			return -EINVAL;
		} else {
			value = es8396->ana_ldo_lvl;
			value &= 0x07;
			snd_soc_write(codec, ES8396_SYS_CHIP_ANA_CTL_REG70,
				      value);
		}
		/* mic enable, mic d2se enable */
		snd_soc_write(codec, ES8396_SYS_MIC_IBIAS_EN_REG75, 0x01);
		msleep(50);
		snd_soc_write(codec, ES8396_TEST_MODE_REG76, 0xA0);
		snd_soc_write(codec, ES8396_NGTH_REG7A, 0x20);
		msleep(50);

		/* power up adc and dac analog */
		snd_soc_write(codec, ES8396_ADC_ANALOG_CTRL_REG5E, 0x00);
		snd_soc_write(codec, ES8396_DAC_REF_PWR_CTRL_REG6E, 0x00);
		/* set L,R DAC volume */
		snd_soc_write(codec, ES8396_DAC_LDAC_VOL_REG6A, 0x01);
		snd_soc_write(codec, ES8396_DAC_RDAC_VOL_REG6B, 0x01);
		/* setup charge current for calibrate */
		snd_soc_write(codec, ES8396_ADC_LADC_VOL_REG56, 0x84);
		snd_soc_write(codec, ES8396_ADC_RADC_VOL_REG57, 0xdc);
		snd_soc_write(codec, ES8396_DAC_OFFSET_CALI_REG6F, 0x06);
		snd_soc_write(codec, ES8396_DAC_RAMP_RATE_REG67, 0x00);
		/* enable adc and dac stm for calibrate */
		snd_soc_write(codec, ES8396_DAC_CSM_REG66, 0x00);
		snd_soc_write(codec, ES8396_ADC_CSM_REG53, 0x00);
		snd_soc_write(codec, ES8396_ADC_FORCE_REG77, 0x40);
		snd_soc_write(codec, ES8396_ADC_FORCE_REG77, 0x00);
		snd_soc_write(codec, ES8396_DLL_CTRL_REG0D, 0x00);
		msleep(100);
		snd_soc_write(codec, ES8396_DAC_CSM_REG66, 0x00);
		snd_soc_write(codec, ES8396_ADC_ANALOG_CTRL_REG5E, 0x00);
		snd_soc_write(codec, 0x5f, 0xf2);
		snd_soc_write(tron_codec, 0x1f, 0x00);
		snd_soc_write(tron_codec, 0x20, 0x40);
		/* FM */
		msleep(100);
		snd_soc_write(tron_codec, 0x65, 0x88);
		snd_soc_write(tron_codec, 0x2E, 0x88);
		snd_soc_write(tron_codec, 0x2F, 0x00);
		snd_soc_write(tron_codec, 0x30, 0xBB);

		if (es8396_valid_micbias(es8396->mic_bias_lvl) == false) {
			pr_err("MIC BIAS Level error.\n");
			return -EINVAL;
		} else {
			value = es8396->mic_bias_lvl;
			value &= 0x07;
			value = (value << 4) | 0x08;
			/* enable micbias1 */
			snd_soc_write(codec, ES8396_SYS_MICBIAS_CTRL_REG74,
				      value);
		}

		snd_soc_write(codec, ES8396_ADC_CSM_REG53, 0x20);
		snd_soc_write(codec, ES8396_ADC_PGA_GAIN_REG61, 0x33);
		snd_soc_write(codec, ES8396_ADC_MICBOOST_REG60, 0x22);
		if (es8396->dmic_amic == MIC_AMIC)
			/*use analog mic */
			snd_soc_write(codec, ES8396_ADC_DMIC_RAMPRATE_REG54,
				      0x00);
		else
			/*use digital mic */
			snd_soc_write(codec, ES8396_ADC_DMIC_RAMPRATE_REG54,
				      0xf0);

		/*Enable HPF, LDATA= LADC, RDATA = LADC */
		snd_soc_write(codec, ES8396_ADC_HPF_COMP_DASEL_REG55, 0x31);

		/*
		 * setup hp detection
		 */

		/* gpio 2 for irq, AINL as irq src, gpio1 for dmic clk */
		snd_soc_write(codec, ES8396_ALRCK_GPIO_SEL_REG15, 0xfa);
		snd_soc_write(codec, ES8396_DAC_JACK_DET_COMP_REG69, 0x00);
		if (es8396->jackdet_enable == 1) {
			/* jack detection from AINL pin, AINL=0, HP Insert */
			snd_soc_write(codec, ES8396_DAC_JACK_DET_COMP_REG69,
				      0x04);
			if (es8396->gpio_int_pol == 0)
				/* if HP insert, GPIO2=Low */
				snd_soc_write(codec, ES8396_GPIO_IRQ_REG16,
					      0x80);
			else
				/* if HP insert, GPIO2=High */
				snd_soc_write(codec, ES8396_GPIO_IRQ_REG16,
					      0xc0);
		} else {
			snd_soc_write(codec, ES8396_GPIO_IRQ_REG16, 0x00);
		}

		/*
		 * setup mono in in differential mode or stereo mode
		 */

		/* monoin in differential mode */
		if (es8396->monoin_differential == 1)
			snd_soc_update_bits(codec, ES8396_MN_MIXER_REF_LP_REG39,
					    0x08, 0x08);
		else
			snd_soc_update_bits(codec, ES8396_MN_MIXER_REF_LP_REG39,
					    0x08, 0x00);

		snd_soc_write(codec, ES8396_DAC_JACK_DET_COMP_REG69, 0x00);
		snd_soc_write(codec, ES8396_BCLK_DIV_M1_REG0E, 0x24);
		snd_soc_write(codec, ES8396_LRCK_DIV_M3_REG10, 0x22);
		snd_soc_write(codec, ES8396_SDP_2_MS_REG13, 0x00);
		//codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;
	}

	INIT_DELAYED_WORK(&es8396->adc_depop_work, adc_depop_work_events);
	mutex_init(&es8396->adc_depop_mlock);
	INIT_DELAYED_WORK(&es8396->pcm_pop_work, pcm_pop_work_events);
	mutex_init(&es8396->pcm_depop_mlock);

	INIT_DELAYED_WORK(&es8396->voice_pop_work, voice_pop_work_events);
	mutex_init(&es8396->voice_depop_mlock);

	INIT_DELAYED_WORK(&es8396->init_cali_work, init_cali_work_events);
	mutex_init(&es8396->init_cali_mlock);

	INIT_DELAYED_WORK(&es8396->pcm_shutdown_depop_work,
			  pcm_shutdown_depop_events);
	mutex_init(&es8396->pcm_shutdown_depop_mlock);

	INIT_DELAYED_WORK(&es8396->voice_shutdown_depop_work,
			  voice_shutdown_depop_events);
	mutex_init(&es8396->voice_shutdown_depop_mlock);

	snd_soc_write(codec, 0x6f, 0x83);
	snd_soc_write(codec, ES8396_SYS_VMID_REF_REG71, 0xFC);
	msleep(100);
	snd_soc_write(codec, 0x4E, 0x84);
	snd_soc_write(codec, 0x4F, 0x85);
	snd_soc_write(codec, 0x4A, 0x60);
	snd_soc_write(codec, 0x4B, 0x60);
	/*
	 * TODO: pop noise occur when HS calibration during probe
	 * increase the delay of a period of time if necessary
	 * msleep(50);
	 */
	return ret;
}

static int es8396_remove(struct snd_soc_codec *codec)
{
	pr_debug("!!!!!!CODEC going into es8396_remove!!!!!!\n");

	snd_soc_write(codec, 0X4E, 0x84);
	snd_soc_write(codec, 0X4F, 0x85);
	snd_soc_write(codec, 0X4A, 0x80);
	snd_soc_write(codec, 0X4B, 0x80);
	snd_soc_write(codec, 0x70, 0xd4);
	msleep(300);

	return 0;
}

const struct regmap_config es8396_regmap_config = {
        .reg_bits       = 8,
        .val_bits       = 8,
        .max_register   = ES8396_MAX_REGISTER,
        .cache_type     = REGCACHE_RBTREE,
        .reg_defaults = es8396_reg_defaults,
        .num_reg_defaults = ARRAY_SIZE(es8396_reg_defaults),
};

static struct snd_soc_codec_driver soc_codec_dev_es8396 = {
	.probe = es8396_probe,
	.remove = es8396_remove,
	.set_bias_level = es8396_set_bias_level,

	.dapm_widgets = es8396_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(es8396_dapm_widgets),
	.dapm_routes = es8396_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(es8396_dapm_routes),

	.controls = es8396_snd_controls,
	.num_controls = ARRAY_SIZE(es8396_snd_controls),
/*
	.reg_cache_size = ARRAY_SIZE(es8396_reg_defaults),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = es8396_reg_defaults,
	.volatile_register = es8396_volatile_register,
	.readable_register = es8396_readable_register,
*/
};

static int init_es8396_prv(struct es8396_private *es8396)
{
	if (es8396 == NULL)
		return -EINVAL;

	es8396->dvdd_pwr_vol = 0x18;
	es8396->spkmono = false;
	es8396->earpiece = true;
	es8396->monoin_differential = true;
	es8396->lno_differential = 0;
	es8396->ana_ldo_lvl = ANA_LDO_2_1V;
	es8396->spk_ldo_lvl = SPK_LDO_3V;
	es8396->mic_bias_lvl = MICBIAS_3V;
	es8396->jackdet_enable = true;
	es8396->gpio_int_pol = 0;
	es8396->dmic_amic = MIC_AMIC;
	es8396->calibrate = false;
	es8396->pcm_pop_work_retry = 1;
	es8396->output_device_selected = 0;
	es8396->aif1_select = 0;
	es8396->aif2_select = 0;
	return 0;
}

static int es8396_i2c_probe(struct i2c_client *i2c_client,
			    const struct i2c_device_id *id)
{
	struct es8396_private *es8396;
	int ret;
	enum of_gpio_flags flags;
	struct device_node *np = i2c_client->dev.of_node;

	pr_debug("%s %d\n", __func__, __LINE__);
	es8396 = devm_kzalloc(&i2c_client->dev, sizeof(struct es8396_private),
			      GFP_KERNEL);
	if (!es8396) {
		dev_err(&i2c_client->dev, "could not allocate codec\n");

		return -ENOMEM;
	}
	/*
	 * INIT ES8396 private here
	 * TODO
	 * id->driver_data
	 */
	ret = init_es8396_prv(es8396);
	if (ret < 0)
		return -EINVAL;

	es8396->regmap = devm_regmap_init_i2c(i2c_client, &es8396_regmap_config);
        if (IS_ERR(es8396->regmap)) {
                ret = PTR_ERR(es8396->regmap);
                dev_err(&i2c_client->dev, "Failed to init regmap: %d\n", ret);
                return ret;
        }

	/* initialize codec */
	i2c_set_clientdata(i2c_client, es8396);

	/* external speaker amp controller */
	es8396->spk_ctl_gpio = of_get_named_gpio_flags(np,
						       "spk-con-gpio", 0,
						       &flags);
	if (es8396->spk_ctl_gpio < 0) {
		pr_err("%s() Can not read property spk codec-en-gpio\n",
		       __func__);
		es8396->spk_ctl_gpio = INVALID_GPIO;
	} else {
		pr_debug("%d() spk codec-en-gpio\n", es8396->spk_ctl_gpio);
		es8396->spk_gpio_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = devm_gpio_request(&i2c_client->dev, es8396->spk_ctl_gpio,
					"spk_ctl");
		if (!ret) {
			pr_debug("requset codec-en-gpio success!:%d\n", ret);
			gpio_direction_output(es8396->spk_ctl_gpio,
					      !es8396->spk_gpio_level);
		} else {
			pr_err("requset codec-en-gpio failed:%d\n", ret);
			return ret;
		}
	}

	/* lineout output  controller*/
	es8396->lineout_ctl_gpio = of_get_named_gpio_flags(np,
							   "lineout-con-gpio",
							   0, &flags);
	if (es8396->lineout_ctl_gpio < 0) {
		pr_err("%s() Can not read property lineout en-gpio\n",
		       __func__);
		es8396->lineout_ctl_gpio = INVALID_GPIO;
	} else {
		pr_debug("%d() lineout en-gpio\n", es8396->lineout_ctl_gpio);
		es8396->lineout_gpio_level = (flags & OF_GPIO_ACTIVE_LOW) ?
					      0 : 1;
		ret = devm_gpio_request(&i2c_client->dev,
					es8396->lineout_ctl_gpio,
					"lineout_ctl");
		if (!ret) {
			pr_debug("requset lineout en-gpio success!:%d\n", ret);
			gpio_direction_output(es8396->lineout_ctl_gpio,
					      !es8396->lineout_gpio_level);
		} else {
			pr_err("requset lineout en-gpio failed:%d\n", ret);
			return ret;
		}
	}

	ret = snd_soc_register_codec(&i2c_client->dev,
				     &soc_codec_dev_es8396, es8396_dai,
				     ARRAY_SIZE(es8396_dai));

	return ret;
}

static void es8396_i2c_shutdown(struct i2c_client *client)
{
	struct es8396_private *es8396 = i2c_get_clientdata(client);
	pr_debug("Enter into %s\n", __func__);

	if (es8396->spk_ctl_gpio != INVALID_GPIO)
		gpio_set_value(es8396->spk_ctl_gpio, 0);

	usleep_range(20000, 21000);
	snd_soc_write(tron_codec, 0X4E, 0x84);
	snd_soc_write(tron_codec, 0X4F, 0x85);
	snd_soc_write(tron_codec, 0X4a, 0x80);
	snd_soc_write(tron_codec, 0X4b, 0x80);
	snd_soc_write(tron_codec, 0x70, 0xd4);
	msleep(300);
}

static int es8396_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id es8396_id[] = {
	{"es8396", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, es8396_id);
static const struct dev_pm_ops es8396_pm_ops = {
	.suspend = es8396_suspend,
	.resume = es8396_resume,
};

static struct i2c_driver es8396_i2c_driver = {
	.driver = {
		   .name = "es8396",
		   .owner = THIS_MODULE,
		   .pm = &es8396_pm_ops,
		   },
	.id_table = es8396_id,
	.probe = es8396_i2c_probe,
	.shutdown = es8396_i2c_shutdown,
	.remove = es8396_i2c_remove,
};

module_i2c_driver(es8396_i2c_driver);

MODULE_DESCRIPTION("ASoC ES8396 driver");
MODULE_AUTHOR("DavidYang, Everest Semiconductor Co., Ltd, <yangxiaohua@everest-semi.com>");
MODULE_LICENSE("GPL");
