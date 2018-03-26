/*
 * rk3308_codec.c -- RK3308 ALSA Soc Audio Driver
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/rockchip/grf.h>
#include <linux/version.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rk3308_codec.h"

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#define CODEC_DRV_NAME			"rk3308-acodec"

#define ADC_LR_GROUP_MAX		4

struct rk3308_codec_priv {
	const struct device *plat_dev;
	struct device dev;
	struct reset_control *reset;
	struct regmap *regmap;
	struct clk *pclk;
	struct clk *mclk_rx;
	struct clk *mclk_tx;
	struct gpio_desc *spk_ctl_gpio;
	/*
	 * To select ADCs for groups:
	 *
	 * grp 0 -- select ADC1 / ADC2
	 * grp 1 -- select ADC3 / ADC4
	 * grp 2 -- select ADC5 / ADC6
	 * grp 3 -- select ADC7 / ADC8
	 */
	int adc_cur_grp;
	int adc_grp0_using_linein;

#if defined(CONFIG_DEBUG_FS)
	struct dentry *dbg_codec;
#endif
};

static const DECLARE_TLV_DB_SCALE(rk3308_codec_alc_agc_grp_gain_tlv,
				  -1800, 150, 2850);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_alc_agc_grp_max_gain_tlv,
				  -1350, 600, 2850);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_alc_agc_grp_min_gain_tlv,
				  -1800, 600, 2400);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_adc_mic_gain_tlv,
				  0, 600, 3000);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_adc_alc_gain_tlv,
				  -1800, 150, 2850);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_dac_lineout_gain_tlv,
				  -600, 150, 0);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_dac_hpout_gain_tlv,
				  -3900, 150, 600);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_dac_hpmix_gain_tlv,
				  -600, 600, 0);

static const struct snd_kcontrol_new rk3308_codec_dapm_controls[] = {
	/* ALC AGC Group */
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Left Volume",
			     RK3308_ALC_L_DIG_CON03(0),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Right Volume",
			     RK3308_ALC_R_DIG_CON03(0),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Left Volume",
			     RK3308_ALC_L_DIG_CON03(1),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Right Volume",
			     RK3308_ALC_R_DIG_CON03(1),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Left Volume",
			     RK3308_ALC_L_DIG_CON03(2),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Right Volume",
			     RK3308_ALC_R_DIG_CON03(2),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Left Volume",
			     RK3308_ALC_L_DIG_CON03(3),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Right Volume",
			     RK3308_ALC_R_DIG_CON03(3),
			     RK3308_AGC_PGA_GAIN_SFT,
			     RK3308_AGC_PGA_GAIN_MIN,
			     RK3308_AGC_PGA_GAIN_MAX,
			     0, rk3308_codec_alc_agc_grp_gain_tlv),

	/* ALC AGC MAX */
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Left Max Volume",
			     RK3308_ALC_L_DIG_CON09(0),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Right Max Volume",
			     RK3308_ALC_R_DIG_CON09(0),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Left Max Volume",
			     RK3308_ALC_L_DIG_CON09(1),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Right Max Volume",
			     RK3308_ALC_R_DIG_CON09(1),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Left Max Volume",
			     RK3308_ALC_L_DIG_CON09(2),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Right Max Volume",
			     RK3308_ALC_R_DIG_CON09(2),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Left Max Volume",
			     RK3308_ALC_L_DIG_CON09(3),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Right Max Volume",
			     RK3308_ALC_R_DIG_CON09(3),
			     RK3308_AGC_MAX_GAIN_PGA_SFT,
			     RK3308_AGC_MAX_GAIN_PGA_MIN,
			     RK3308_AGC_MAX_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_max_gain_tlv),

	/* ALC AGC MIN */
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Left Min Volume",
			     RK3308_ALC_L_DIG_CON09(0),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 0 Right Min Volume",
			     RK3308_ALC_R_DIG_CON09(0),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Left Min Volume",
			     RK3308_ALC_L_DIG_CON09(1),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 1 Right Min Volume",
			     RK3308_ALC_R_DIG_CON09(1),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Left Min Volume",
			     RK3308_ALC_L_DIG_CON09(2),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 2 Right Min Volume",
			     RK3308_ALC_R_DIG_CON09(2),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),

	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Left Min Volume",
			     RK3308_ALC_L_DIG_CON09(3),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ALC AGC Group 3 Right Min Volume",
			     RK3308_ALC_R_DIG_CON09(3),
			     RK3308_AGC_MIN_GAIN_PGA_SFT,
			     RK3308_AGC_MIN_GAIN_PGA_MIN,
			     RK3308_AGC_MIN_GAIN_PGA_MAX,
			     0, rk3308_codec_alc_agc_grp_min_gain_tlv),

	/* ADC MIC */
	SOC_SINGLE_RANGE_TLV("ADC MIC Group 0 Left Volume",
			     RK3308_ADC_ANA_CON01(0),
			     RK3308_ADC_CH1_MIC_GAIN_SFT,
			     RK3308_ADC_CH1_MIC_GAIN_MIN,
			     RK3308_ADC_CH1_MIC_GAIN_MAX,
			     0, rk3308_codec_adc_mic_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC MIC Group 0 Right Volume",
			     RK3308_ADC_ANA_CON01(0),
			     RK3308_ADC_CH2_MIC_GAIN_SFT,
			     RK3308_ADC_CH2_MIC_GAIN_MIN,
			     RK3308_ADC_CH2_MIC_GAIN_MAX,
			     0, rk3308_codec_adc_mic_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC MIC Group 1 Left Volume",
			     RK3308_ADC_ANA_CON01(1),
			     RK3308_ADC_CH1_MIC_GAIN_SFT,
			     RK3308_ADC_CH1_MIC_GAIN_MIN,
			     RK3308_ADC_CH1_MIC_GAIN_MAX,
			     0, rk3308_codec_adc_mic_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC MIC Group 1 Right Volume",
			     RK3308_ADC_ANA_CON01(1),
			     RK3308_ADC_CH2_MIC_GAIN_SFT,
			     RK3308_ADC_CH2_MIC_GAIN_MIN,
			     RK3308_ADC_CH2_MIC_GAIN_MAX,
			     0, rk3308_codec_adc_mic_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC MIC Group 2 Left Volume",
			     RK3308_ADC_ANA_CON01(2),
			     RK3308_ADC_CH1_MIC_GAIN_SFT,
			     RK3308_ADC_CH1_MIC_GAIN_MIN,
			     RK3308_ADC_CH1_MIC_GAIN_MAX,
			     0, rk3308_codec_adc_mic_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC MIC Group 2 Right Volume",
			     RK3308_ADC_ANA_CON01(2),
			     RK3308_ADC_CH2_MIC_GAIN_SFT,
			     RK3308_ADC_CH2_MIC_GAIN_MIN,
			     RK3308_ADC_CH2_MIC_GAIN_MAX,
			     0, rk3308_codec_adc_mic_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC MIC Group 3 Left Volume",
			     RK3308_ADC_ANA_CON01(3),
			     RK3308_ADC_CH1_MIC_GAIN_SFT,
			     RK3308_ADC_CH1_MIC_GAIN_MIN,
			     RK3308_ADC_CH1_MIC_GAIN_MAX,
			     0, rk3308_codec_adc_mic_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC MIC Group 3 Right Volume",
			     RK3308_ADC_ANA_CON01(3),
			     RK3308_ADC_CH2_MIC_GAIN_SFT,
			     RK3308_ADC_CH2_MIC_GAIN_MIN,
			     RK3308_ADC_CH2_MIC_GAIN_MAX,
			     0, rk3308_codec_adc_mic_gain_tlv),

	/* ADC ALC */
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 0 Left Volume",
			     RK3308_ADC_ANA_CON03(0),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 0 Right Volume",
			     RK3308_ADC_ANA_CON04(0),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 1 Left Volume",
			     RK3308_ADC_ANA_CON03(1),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 1 Right Volume",
			     RK3308_ADC_ANA_CON04(1),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 2 Left Volume",
			     RK3308_ADC_ANA_CON03(2),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 2 Right Volume",
			     RK3308_ADC_ANA_CON04(2),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 3 Left Volume",
			     RK3308_ADC_ANA_CON03(3),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("ADC ALC Group 3 Right Volume",
			     RK3308_ADC_ANA_CON04(3),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),

	/* DAC LINEOUT */
	SOC_SINGLE_TLV("DAC LINEOUT Left Volume",
		       RK3308_DAC_ANA_CON04,
		       RK3308_DAC_L_LINEOUT_GAIN_SFT,
		       RK3308_DAC_L_LINEOUT_GAIN_MAX,
		       0, rk3308_codec_dac_lineout_gain_tlv),
	SOC_SINGLE_TLV("DAC LINEOUT Right Volume",
		       RK3308_DAC_ANA_CON04,
		       RK3308_DAC_R_LINEOUT_GAIN_SFT,
		       RK3308_DAC_R_LINEOUT_GAIN_MAX,
		       0, rk3308_codec_dac_lineout_gain_tlv),

	/* DAC HPOUT */
	SOC_SINGLE_TLV("DAC HPOUT Left Volume",
		       RK3308_DAC_ANA_CON05,
		       RK3308_DAC_L_HPOUT_GAIN_SFT,
		       RK3308_DAC_L_HPOUT_GAIN_MAX,
		       0, rk3308_codec_dac_hpout_gain_tlv),
	SOC_SINGLE_TLV("DAC HPOUT Right Volume",
		       RK3308_DAC_ANA_CON06,
		       RK3308_DAC_R_HPOUT_GAIN_SFT,
		       RK3308_DAC_R_HPOUT_GAIN_MAX,
		       0, rk3308_codec_dac_hpout_gain_tlv),

	/* DAC HPMIX */
	SOC_SINGLE_RANGE_TLV("DAC HPMIX Left Volume",
			     RK3308_DAC_ANA_CON12,
			     RK3308_DAC_L_HPMIX_GAIN_SFT,
			     RK3308_DAC_L_HPMIX_GAIN_MIN,
			     RK3308_DAC_L_HPMIX_GAIN_MAX,
			     0, rk3308_codec_dac_hpmix_gain_tlv),
	SOC_SINGLE_RANGE_TLV("DAC HPMIX Right Volume",
			     RK3308_DAC_ANA_CON12,
			     RK3308_DAC_R_HPMIX_GAIN_SFT,
			     RK3308_DAC_R_HPMIX_GAIN_MIN,
			     RK3308_DAC_R_HPMIX_GAIN_MAX,
			     0, rk3308_codec_dac_hpmix_gain_tlv),
};

static void rk3308_speaker_ctl(struct rk3308_codec_priv *rk3308, int on)
{
	gpiod_direction_output(rk3308->spk_ctl_gpio, on);
}

static int rk3308_codec_reset(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	reset_control_assert(rk3308->reset);
	usleep_range(200, 300);		/* estimated value */
	reset_control_deassert(rk3308->reset);

	regmap_write(rk3308->regmap, RK3308_GLB_CON, 0x00);
	usleep_range(200, 300);		/* estimated value */
	regmap_write(rk3308->regmap, RK3308_GLB_CON,
		     RK3308_SYS_WORK |
		     RK3308_DAC_DIG_WORK |
		     RK3308_ADC_DIG_WORK);

	return 0;
}

static int rk3308_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		regcache_cache_only(rk3308->regmap, false);
		regcache_sync(rk3308->regmap);
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}

	return 0;
}

static int rk3308_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	unsigned int adc_aif1 = 0, adc_aif2 = 0, dac_aif1 = 0, dac_aif2 = 0;
	int grp;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		adc_aif2 |= RK3308_ADC_IO_MODE_SLAVE;
		adc_aif2 |= RK3308_ADC_MODE_SLAVE;
		dac_aif2 |= RK3308_DAC_IO_MODE_SLAVE;
		dac_aif2 |= RK3308_DAC_MODE_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		adc_aif2 |= RK3308_ADC_IO_MODE_MASTER;
		adc_aif2 |= RK3308_ADC_MODE_MASTER;
		dac_aif2 |= RK3308_DAC_IO_MODE_MASTER;
		dac_aif2 |= RK3308_DAC_MODE_MASTER;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		adc_aif1 |= RK3308_ADC_I2S_MODE_PCM;
		dac_aif1 |= RK3308_DAC_I2S_MODE_PCM;
		break;
	case SND_SOC_DAIFMT_I2S:
		adc_aif1 |= RK3308_ADC_I2S_MODE_I2S;
		dac_aif1 |= RK3308_DAC_I2S_MODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adc_aif1 |= RK3308_ADC_I2S_MODE_RJ;
		dac_aif1 |= RK3308_DAC_I2S_MODE_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adc_aif1 |= RK3308_ADC_I2S_MODE_LJ;
		dac_aif1 |= RK3308_DAC_I2S_MODE_LJ;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_NORMAL;
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_NORMAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_NORMAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_REVERSAL;
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_REVERSAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_REVERSAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_REVERSAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_NORMAL;
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_REVERSAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_NORMAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_REVERSAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_REVERSAL;
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_NORMAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_REVERSAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON01(grp),
				   RK3308_ADC_I2S_LRC_POL_MSK |
				   RK3308_ADC_I2S_MODE_MSK,
				   adc_aif1);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON02(grp),
				   RK3308_ADC_IO_MODE_MSK |
				   RK3308_ADC_MODE_MSK |
				   RK3308_ADC_I2S_BIT_CLK_POL_MSK,
				   adc_aif2);
	}

	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON01,
			   RK3308_DAC_I2S_LRC_POL_MSK |
			   RK3308_DAC_I2S_MODE_MSK,
			   dac_aif1);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON02,
			   RK3308_DAC_IO_MODE_MSK |
			   RK3308_DAC_MODE_MSK |
			   RK3308_DAC_I2S_BIT_CLK_POL_MSK,
			   dac_aif2);

	return 0;
}

static int rk3308_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	unsigned int adc_aif1 = 0, adc_aif2  = 0, dac_aif1 = 0, dac_aif2  = 0;
	int grp;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_16BITS;
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_20BITS;
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_24BITS;
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_32BITS;
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_32BITS;
		break;
	default:
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case 1:
		adc_aif1 |= RK3308_ADC_I2S_MONO;
		break;
	case 2:
	case 8:
		adc_aif1 |= RK3308_ADC_I2S_STEREO;
		break;
	default:
		return -EINVAL;
	}

	adc_aif1 |= RK3308_ADC_I2S_LR_NORMAL;
	adc_aif2 |= RK3308_ADC_I2S_WORK;
	dac_aif1 |= RK3308_DAC_I2S_LR_NORMAL;
	dac_aif2 |= RK3308_DAC_I2S_WORK;

	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON01(grp),
				   RK3308_ADC_I2S_VALID_LEN_MSK |
				   RK3308_ADC_I2S_LR_MSK |
				   RK3308_ADC_I2S_TYPE_MSK,
				   adc_aif1);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON02(grp),
				   RK3308_ADC_I2S_MSK,
				   adc_aif2);
	}

	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON01,
			   RK3308_DAC_I2S_VALID_LEN_MSK |
			   RK3308_DAC_I2S_LR_MSK,
			   dac_aif1);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON02,
			   RK3308_DAC_I2S_MSK,
			   dac_aif2);

	return 0;
}

static int rk3308_digital_mute(struct snd_soc_dai *dai, int mute)
{
	/* Do nothing */
	return 0;
}

static int rk3308_codec_dac_enable(struct rk3308_codec_priv *rk3308)
{
	/*
	 * 1. Set the ACODEC_DAC_ANA_CON0[0] to 0x1, to enable the current
	 * source of DAC
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON00,
			   RK3308_DAC_CURRENT_MSK,
			   RK3308_DAC_CURRENT_EN);

	/*
	 * 2. Set the ACODEC_DAC_ANA_CON1[6] and ACODEC_DAC_ANA_CON1[2] to 0x1,
	 * to enable the reference voltage buffer
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_BUF_REF_L_MSK |
			   RK3308_DAC_BUF_REF_R_MSK,
			   RK3308_DAC_BUF_REF_L_EN |
			   RK3308_DAC_BUF_REF_R_EN);

	/* Step 03 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_POP_SOUND_L_MSK |
			   RK3308_DAC_POP_SOUND_R_MSK,
			   RK3308_DAC_POP_SOUND_L_WORK |
			   RK3308_DAC_POP_SOUND_R_WORK);

	/* Step 04 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_EN | RK3308_DAC_R_HPMIX_EN,
			   RK3308_DAC_L_HPMIX_EN | RK3308_DAC_R_HPMIX_EN);

	/* Step 05 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_WORK | RK3308_DAC_R_HPMIX_WORK,
			   RK3308_DAC_L_HPMIX_WORK | RK3308_DAC_R_HPMIX_WORK);

	/* Step 06 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_EN | RK3308_DAC_R_LINEOUT_EN,
			   RK3308_DAC_L_LINEOUT_EN | RK3308_DAC_R_LINEOUT_EN);

	/* Step 07 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_EN | RK3308_DAC_R_HPOUT_EN,
			   RK3308_DAC_L_HPOUT_EN | RK3308_DAC_R_HPOUT_EN);

	/* Step 08 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_WORK | RK3308_DAC_R_HPOUT_WORK,
			   RK3308_DAC_L_HPOUT_WORK | RK3308_DAC_R_HPOUT_WORK);

	/* Step 09 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_REF_EN | RK3308_DAC_R_REF_EN,
			   RK3308_DAC_L_REF_EN | RK3308_DAC_R_REF_EN);

	/* Step 10 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_CLK_EN | RK3308_DAC_R_CLK_EN,
			   RK3308_DAC_L_CLK_EN | RK3308_DAC_R_CLK_EN);

	/* Step 11 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_DAC_EN | RK3308_DAC_R_DAC_EN,
			   RK3308_DAC_L_DAC_EN | RK3308_DAC_R_DAC_EN);

	/* Step 12 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_DAC_WORK | RK3308_DAC_R_DAC_WORK,
			   RK3308_DAC_L_DAC_WORK | RK3308_DAC_R_DAC_WORK);

	/* Step 13 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON12,
			   RK3308_DAC_L_HPMIX_SEL_MSK |
			   RK3308_DAC_R_HPMIX_SEL_MSK,
			   RK3308_DAC_L_HPMIX_I2S |
			   RK3308_DAC_R_HPMIX_I2S);

	/* Step 14 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_UNMUTE |
			   RK3308_DAC_R_HPMIX_UNMUTE,
			   RK3308_DAC_L_HPMIX_UNMUTE |
			   RK3308_DAC_R_HPMIX_UNMUTE);

	/* Step 15 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON12,
			   RK3308_DAC_L_HPMIX_GAIN_MSK |
			   RK3308_DAC_R_HPMIX_GAIN_MSK,
			   RK3308_DAC_L_HPMIX_GAIN_NDB_6 |
			   RK3308_DAC_R_HPMIX_GAIN_NDB_6);

	/* Step 16 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_UNMUTE |
			   RK3308_DAC_R_HPOUT_UNMUTE,
			   RK3308_DAC_L_HPOUT_UNMUTE |
			   RK3308_DAC_R_HPOUT_UNMUTE);

	/* Step 17 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_UNMUTE |
			   RK3308_DAC_R_LINEOUT_UNMUTE,
			   RK3308_DAC_L_LINEOUT_UNMUTE |
			   RK3308_DAC_R_LINEOUT_UNMUTE);

	/* Step 18 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON05,
			   RK3308_DAC_L_HPOUT_GAIN_MSK,
			   RK3308_DAC_L_HPOUT_GAIN_NDB_1_5);

	/* Step 18 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON06,
			   RK3308_DAC_R_HPOUT_GAIN_MSK,
			   RK3308_DAC_R_HPOUT_GAIN_NDB_1_5);

	/* Step 19 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_GAIN_MSK |
			   RK3308_DAC_R_LINEOUT_GAIN_MSK,
			   RK3308_DAC_L_LINEOUT_GAIN_NDB_6 |
			   RK3308_DAC_R_LINEOUT_GAIN_NDB_6);

	return 0;
}

static int rk3308_codec_dac_disable(struct rk3308_codec_priv *rk3308)
{
	/* Step 01 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_GAIN_MSK |
			   RK3308_DAC_R_LINEOUT_GAIN_MSK,
			   RK3308_DAC_L_LINEOUT_GAIN_NDB_6 |
			   RK3308_DAC_R_LINEOUT_GAIN_NDB_6);

	/*
	 * Step 02
	 *
	 * Note. In the step2, adjusting the register step by step to the
	 * appropriate value and taking 20ms as time step
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON05,
			   RK3308_DAC_L_HPOUT_GAIN_MSK,
			   RK3308_DAC_L_HPOUT_GAIN_NDB_39);

	/* Step 02 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON06,
			   RK3308_DAC_R_HPOUT_GAIN_MSK,
			   RK3308_DAC_R_HPOUT_GAIN_NDB_39);

	/* Step 03 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_UNMUTE |
			   RK3308_DAC_R_HPMIX_UNMUTE,
			   RK3308_DAC_L_HPMIX_MUTE |
			   RK3308_DAC_R_HPMIX_MUTE);

	/* Step 04 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON12,
			   RK3308_DAC_L_HPMIX_SEL_MSK |
			   RK3308_DAC_R_HPMIX_SEL_MSK,
			   RK3308_DAC_L_HPMIX_NONE |
			   RK3308_DAC_R_HPMIX_NONE);

	/* Step 05 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_UNMUTE |
			   RK3308_DAC_R_HPOUT_UNMUTE,
			   RK3308_DAC_L_HPOUT_MUTE |
			   RK3308_DAC_R_HPOUT_MUTE);

	/* Step 06 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_DAC_WORK | RK3308_DAC_R_DAC_WORK,
			   RK3308_DAC_L_DAC_INIT | RK3308_DAC_R_DAC_INIT);

	/* Step 07 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_EN | RK3308_DAC_R_HPOUT_EN,
			   RK3308_DAC_L_HPOUT_DIS | RK3308_DAC_R_HPOUT_DIS);

	/* Step 08 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_UNMUTE |
			   RK3308_DAC_R_LINEOUT_UNMUTE,
			   RK3308_DAC_L_LINEOUT_MUTE |
			   RK3308_DAC_R_LINEOUT_MUTE);

	/* Step 09 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON04,
			   RK3308_DAC_L_LINEOUT_EN | RK3308_DAC_R_LINEOUT_EN,
			   RK3308_DAC_L_LINEOUT_DIS | RK3308_DAC_R_LINEOUT_DIS);

	/* Step 10 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_EN | RK3308_DAC_R_HPMIX_EN,
			   RK3308_DAC_L_HPMIX_DIS | RK3308_DAC_R_HPMIX_DIS);

	/* Step 11 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_DAC_EN | RK3308_DAC_R_DAC_EN,
			   RK3308_DAC_L_DAC_DIS | RK3308_DAC_R_DAC_DIS);

	/* Step 12 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_CLK_EN | RK3308_DAC_R_CLK_EN,
			   RK3308_DAC_L_CLK_DIS | RK3308_DAC_R_CLK_DIS);

	/* Step 13 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
			   RK3308_DAC_L_REF_EN | RK3308_DAC_R_REF_EN,
			   RK3308_DAC_L_REF_DIS | RK3308_DAC_R_REF_DIS);

	/* Step 14 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_POP_SOUND_L_MSK |
			   RK3308_DAC_POP_SOUND_R_MSK,
			   RK3308_DAC_POP_SOUND_L_INIT |
			   RK3308_DAC_POP_SOUND_R_INIT);

	/* Step 15 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_BUF_REF_L_EN | RK3308_DAC_BUF_REF_R_EN,
			   RK3308_DAC_BUF_REF_L_DIS | RK3308_DAC_BUF_REF_R_DIS);

	/* Step 16 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON00,
			   RK3308_DAC_CURRENT_EN,
			   RK3308_DAC_CURRENT_DIS);

	/* Step 17 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON03,
			   RK3308_DAC_L_HPOUT_WORK | RK3308_DAC_R_HPOUT_WORK,
			   RK3308_DAC_L_HPOUT_INIT | RK3308_DAC_R_HPOUT_INIT);

	/* Step 18 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON13,
			   RK3308_DAC_L_HPMIX_WORK | RK3308_DAC_R_HPMIX_WORK,
			   RK3308_DAC_L_HPMIX_INIT | RK3308_DAC_R_HPMIX_INIT);

	/* Step 19 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON12,
			   RK3308_DAC_L_HPMIX_GAIN_MSK |
			   RK3308_DAC_R_HPMIX_GAIN_MSK,
			   RK3308_DAC_L_HPMIX_GAIN_NDB_6 |
			   RK3308_DAC_R_HPMIX_GAIN_NDB_6);

	/*
	 * Note2. If the ACODEC_DAC_ANA_CON12[7] or ACODEC_DAC_ANA_CON12[3]
	 * is set to 0x1, add the steps from the section Disable DAC
	 * Configuration Standard Usage Flow after complete the step 19
	 */

	return 0;
}

static int rk3308_codec_power_on(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	/* 1. Supply the power of digital part and reset the Audio Codec */
	/* Do nothing */

	/*
	 * 2. Configure ACODEC_DAC_ANA_CON1[1:0] and ACODEC_DAC_ANA_CON1[5:4]
	 *    to 0x1, to setup dc voltage of the DAC channel output
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_POP_SOUND_L_MSK,
			   RK3308_DAC_POP_SOUND_L_INIT);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   RK3308_DAC_POP_SOUND_R_MSK,
			   RK3308_DAC_POP_SOUND_R_INIT);

	/*
	 * 3. Configure the register ACODEC_ADC_ANA_CON10[6:0] to 0x1
	 *
	 * Note: Only the reg (ADC_ANA_CON10+0x0)[6:0] represent the control
	 * signal to select current to pre-charge/dis_charge
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_CURRENT_CHARGE_MSK,
			   RK3308_ADC_SEL_I_64(1));

	/* 4. Supply the power of the analog part(AVDD,AVDDRV) */

	/*
	 * 5. Configure the register ACODEC_ADC_ANA_CON10[7] to 0x1 to setup
	 *    reference voltage
	 *
	 * Note: Only the reg (ADC_ANA_CON10+0x0)[7] represent the enable
	 * signal of reference voltage module
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_REF_EN, RK3308_ADC_REF_EN);

	/*
	 * 6. Change the register ACODEC_ADC_ANA_CON10[6:0] from the 0x1 to
	 *    0x7f step by step or configure the ACODEC_ADC_ANA_CON10[6:0] to
	 *    0x7f directly. The suggestion slot time of the step is 20ms.
	 */
	mdelay(20);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_CURRENT_CHARGE_MSK,
			   RK3308_ADC_DONT_SEL_ALL);

	/* 7. Wait until the voltage of VCM keeps stable at the AVDD/2 */
	usleep_range(200, 300);	/* estimated value */

	/*
	 * 8. Configure the register ACODEC_ADC_ANA_CON10[6:0] to the
	 *    appropriate value(expect 0x0) for reducing power.
	 */

	 /* TODO: choose an appropriate charge value */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_CURRENT_CHARGE_MSK, 0x7c);

	return 0;
}

static int rk3308_codec_power_off(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	/*
	 * 1. Keep the power on and disable the DAC and ADC path according to
	 *    the section power on configuration standard usage flow.
	 */

	/* 2. Configure the register ACODEC_ADC_ANA_CON10[6:0] to 0x1 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_CURRENT_CHARGE_MSK,
			   RK3308_ADC_SEL_I_64(1));

	/* 3. Configure the register ACODEC_ADC_ANA_CON10[7] to 0x0 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_REF_EN,
			   RK3308_ADC_REF_DIS);

	/*
	 * 4.Change the register ACODEC_ADC_ANA_CON10[6:0] from the 0x1 to 0x7f
	 *   step by step or configure the ACODEC_ADC_ANA_CON10[6:0] to 0x7f
	 *   directly. The suggestion slot time of the step is 20ms
	 */
	mdelay(20);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
			   RK3308_ADC_CURRENT_CHARGE_MSK,
			   RK3308_ADC_DONT_SEL_ALL);

	/* 5. Wait until the voltage of VCM keeps stable at the AGND */
	usleep_range(200, 300);	/* estimated value */

	/* 6. Power off the analog power supply */
	/* 7. Power off the digital power supply */

	/* Do something via hardware */

	return 0;
}

static int rk3308_codec_headset_detect_enable(struct rk3308_codec_priv *rk3308)
{
	/* HACK: headset_detect bypass */
	return 0;

	/*
	 * Set ACODEC_DAC_ANA_CON0[1] to 0x1, to enable the headset insert
	 * detection
	 *
	 * Note. When the voltage of PAD HPDET> 8*AVDD/9, the output value of
	 * the pin_hpdet will be set to 0x1 and assert a interrupt
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON00,
			   RK3308_DAC_HEADPHONE_DET_MSK,
			   RK3308_DAC_HEADPHONE_DET_EN);

	return 0;
}

static int rk3308_codec_headset_detect_disable(struct rk3308_codec_priv *rk3308)
{
	/* HACK: headset_detect bypass */
	return 0;

	/*
	 * Set ACODEC_DAC_ANA_CON0[1] to 0x0, to disable the headset insert
	 * detection
	 */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON00,
			   RK3308_DAC_HEADPHONE_DET_MSK,
			   RK3308_DAC_HEADPHONE_DET_DIS);

	return 0;
}

static int check_micbias(int micbias)
{
	switch (micbias) {
	case RK3308_ADC_MICBIAS_VOLT_0_85:
	case RK3308_ADC_MICBIAS_VOLT_0_8:
	case RK3308_ADC_MICBIAS_VOLT_0_75:
	case RK3308_ADC_MICBIAS_VOLT_0_7:
	case RK3308_ADC_MICBIAS_VOLT_0_65:
	case RK3308_ADC_MICBIAS_VOLT_0_6:
	case RK3308_ADC_MICBIAS_VOLT_0_55:
	case RK3308_ADC_MICBIAS_VOLT_0_5:
		return 0;
	}

	return -EINVAL;
}

static int rk3308_codec_micbias_enable(struct rk3308_codec_priv *rk3308,
				       int micbias)
{
	int ret;

	/* 1. Power up the ACODEC and keep the AVDDH stable */

	/* 2. Configure ACODEC_ADC_ANA_CON7[2:0] to the certain value */
	ret = check_micbias(micbias);
	if (ret < 0) {
		dev_err(rk3308->plat_dev, "This is an invalid micbias: %d\n",
			micbias);
		return ret;
	}

	/*
	 * Note: Only the reg (ADC_ANA_CON7+0x0)[2:0] represent the level range
	 * control signal of MICBIAS voltage
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(0),
			   RK3308_ADC_LEVEL_RANGE_MICBIAS_MSK,
			   micbias);

	/* 3. Wait until the VCMH keep stable */
	usleep_range(200, 300);	/* estimated value */

	/*
	 * 4. Configure ACODEC_ADC_ANA_CON8[4] to 0x1
	 *
	 * Note: Only the reg (ADC_ANA_CON8+0x0)[4] represent the enable
	 * signal of current source for MICBIAS
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON08(0),
			   RK3308_ADC_MICBIAS_CURRENT_MSK,
			   RK3308_ADC_MICBIAS_CURRENT_EN);

	/*
	 * 5. Configure the (ADC_ANA_CON7+0x40)[3] or (ADC_ANA_CON7+0x80)[3]
	 * to 0x1.
	 * (ADC_ANA_CON7+0x40)[3] used to control the MICBIAS1, and
	 * (ADC_ANA_CON7+0x80)[3] used to control the MICBIAS2
	 */

	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(1),
			   RK3308_ADC_MIC_BIAS_BUF_EN,
			   RK3308_ADC_MIC_BIAS_BUF_EN);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(2),
			   RK3308_ADC_MIC_BIAS_BUF_EN,
			   RK3308_ADC_MIC_BIAS_BUF_EN);

	usleep_range(200, 300);	/* estimated value */

	return 0;
}

static int rk3308_codec_micbias_disable(struct rk3308_codec_priv *rk3308)
{
	/* 1. Enable the MICBIAS and keep the Audio Codec stable */
	/* Do nothing */

	/*
	 * 2. Configure the (ADC_ANA_CON7+0x40)[3] or
	 * (ADC_ANA_CON7+0x80)[3] to 0x0
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(1),
			   RK3308_ADC_MIC_BIAS_BUF_EN,
			   RK3308_ADC_MIC_BIAS_BUF_DIS);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(2),
			   RK3308_ADC_MIC_BIAS_BUF_EN,
			   RK3308_ADC_MIC_BIAS_BUF_DIS);

	/*
	 * 3. Configure ACODEC_ADC_ANA_CON8[4] to 0x0
	 *
	 * Note: Only the reg (ADC_ANA_CON8+0x0)[4] represent the enable
	 * signal of current source for MICBIAS
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON08(0),
			   RK3308_ADC_MICBIAS_CURRENT_MSK,
			   RK3308_ADC_MICBIAS_CURRENT_DIS);

	return 0;
}

static int rk3308_codec_alc_enable(struct rk3308_codec_priv *rk3308)
{
	int grp = rk3308->adc_cur_grp;

	/*
	 * 1. Set he max level and min level of the ALC need to control.
	 *
	 * These values are estimated
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ALC_L_DIG_CON05(grp),
			   RK3308_AGC_LO_8BITS_AGC_MIN_MSK,
			   0x16);
	regmap_update_bits(rk3308->regmap, RK3308_ALC_L_DIG_CON05(grp),
			   RK3308_AGC_HI_8BITS_AGC_MIN_MSK,
			   0x40);

	regmap_update_bits(rk3308->regmap, RK3308_ALC_R_DIG_CON05(grp),
			   RK3308_AGC_LO_8BITS_AGC_MAX_MSK,
			   0x26);
	regmap_update_bits(rk3308->regmap, RK3308_ALC_R_DIG_CON05(grp),
			   RK3308_AGC_HI_8BITS_AGC_MAX_MSK,
			   0x40);

	/*
	 * 2. Set ACODEC_ALC_DIG_CON4[2:0] according to the sample rate
	 *
	 * By default is 44.1KHz for sample.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ALC_L_DIG_CON04(grp),
			   RK3308_AGC_APPROX_RATE_MSK,
			   RK3308_AGC_APPROX_RATE_44_1K);
	regmap_update_bits(rk3308->regmap, RK3308_ALC_R_DIG_CON04(grp),
			   RK3308_AGC_APPROX_RATE_MSK,
			   RK3308_AGC_APPROX_RATE_44_1K);

	/* 3. Set ACODEC_ALC_DIG_CON9[6] to 0x1, to enable the ALC module */
	regmap_update_bits(rk3308->regmap, RK3308_ALC_L_DIG_CON09(grp),
			   RK3308_AGC_FUNC_SEL_MSK,
			   RK3308_AGC_FUNC_SEL_EN);
	regmap_update_bits(rk3308->regmap, RK3308_ALC_R_DIG_CON09(grp),
			   RK3308_AGC_FUNC_SEL_MSK,
			   RK3308_AGC_FUNC_SEL_EN);

	/*
	 * 4. Set ACODEC_ADC_ANA_CON11[1:0], (ACODEC_ADC_ANA_CON11+0x40)[1:0],
	 * (ACODEC_ADC_ANA_CON11+0x80)[1:0] and (ACODEC_ADC_ANA_CON11+0xc0)[1:0]
	 * to 0x3, to enable the ALC module to control the gain of PGA.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON11(grp),
			   RK3308_ADC_ALCL_CON_GAIN_PGAL_MSK |
			   RK3308_ADC_ALCR_CON_GAIN_PGAR_MSK,
			   RK3308_ADC_ALCL_CON_GAIN_PGAL_EN |
			   RK3308_ADC_ALCR_CON_GAIN_PGAR_EN);

	/*
	 * 5.Observe the current ALC output gain by reading
	 * ACODEC_ALC_DIG_CON12[4:0]
	 */

	return 0;
}

static int rk3308_codec_alc_disable(struct rk3308_codec_priv *rk3308)
{
	int grp = rk3308->adc_cur_grp;

	/*
	 * 1. Set ACODEC_ALC_DIG_CON9[6] to 0x0, to disable the ALC module,
	 * then the ALC output gain will keep to the last value
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ALC_L_DIG_CON09(grp),
			   RK3308_AGC_FUNC_SEL_MSK,
			   RK3308_AGC_FUNC_SEL_DIS);
	regmap_update_bits(rk3308->regmap, RK3308_ALC_R_DIG_CON09(grp),
			   RK3308_AGC_FUNC_SEL_MSK,
			   RK3308_AGC_FUNC_SEL_DIS);

	/*
	 * 2. Set ACODEC_ADC_ANA_CON11[1:0], (ACODEC_ADC_ANA_CON11+0x40)[1:0],
	 * (ACODEC_ADC_ANA_CON11+0x80)[1:0] and (ACODEC_ADC_ANA_CON11+0xc0)[1:0]
	 * to 0x0, to disable the ALC module to control the gain of PGA.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON11(grp),
			   RK3308_ADC_ALCL_CON_GAIN_PGAL_MSK |
			   RK3308_ADC_ALCR_CON_GAIN_PGAR_MSK,
			   RK3308_ADC_ALCL_CON_GAIN_PGAL_DIS |
			   RK3308_ADC_ALCR_CON_GAIN_PGAR_DIS);

	return 0;
}

static int rk3308_codec_adc_ana_enable(struct rk3308_codec_priv *rk3308)
{
	unsigned int adc_aif1 = 0, adc_aif2 = 0;
	unsigned int agc_func_en;
	int grp = rk3308->adc_cur_grp;

	/*
	 * 1. Set the ACODEC_ADC_ANA_CON7[7:6] and ACODEC_ADC_ANA_CON7[5:4],
	 * to select the line-in or microphone as input of ADC
	 *
	 * Note1. Please ignore the step1 for enabling ADC3, ADC4, ADC5,
	 * ADC6, ADC7, and ADC8
	 */
	if (grp == 0) {
		if (rk3308->adc_grp0_using_linein) {
			adc_aif1 = RK3308_ADC_CH1_IN_LINEIN_MIC;
			adc_aif2 = RK3308_ADC_CH2_IN_LINEIN_MIC;
		} else {
			adc_aif1 = RK3308_ADC_CH1_IN_MIC;
			adc_aif2 = RK3308_ADC_CH2_IN_MIC;
		}
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(grp),
				   RK3308_ADC_CH1_IN_SEL_MSK,
				   adc_aif1);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(grp),
				   RK3308_ADC_CH2_IN_SEL_MSK,
				   adc_aif2);
	}

	/*
	 * 2. Set ACODEC_ADC_ANA_CON0[7:0] to 0xff, to end the mute station
	 * of ADC, to enable the MIC module, to enable the reference voltage
	 * buffer, and to end the initialization of MIC
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
			   RK3308_ADC_CH1_CH2_MIC_ALL_MSK,
			   RK3308_ADC_CH1_CH2_MIC_ALL);

	/*
	 * 3. Set ACODEC_ADC_ANA_CON6[0] to 0x1, to enable the current source
	 * of audio
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON06(grp),
			   RK3308_ADC_CURRENT_MSK,
			   RK3308_ADC_CURRENT_EN);

	/*
	 * 4. Set ACODEC_ADC_ANA_CON2[7:0] to 0x77, to enable the ALC module,
	 * to enable the zero-crossing detection function, and to end the
	 * initialization of ALC
	 *
	 * Note2. Please set ACODEC_ADC_ANA_CON2[7:0] to 0x33 in step4
	 * if the AGC function is closed
	 */

	adc_aif1 = RK3308_ADC_CH1_ALC_EN | RK3308_ADC_CH1_ALC_WORK;
	regmap_read(rk3308->regmap, RK3308_ALC_L_DIG_CON09(grp), &agc_func_en);

	if (agc_func_en & RK3308_AGC_FUNC_SEL_EN)
		adc_aif1 |= RK3308_ADC_CH1_ZEROCROSS_DET_EN;

	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
			   RK3308_ADC_CH1_ALC_ZC_MSK,
			   adc_aif1);

	adc_aif2 = RK3308_ADC_CH2_ALC_EN | RK3308_ADC_CH2_ALC_WORK;
	regmap_read(rk3308->regmap, RK3308_ALC_R_DIG_CON09(grp), &agc_func_en);
	if (agc_func_en & RK3308_AGC_FUNC_SEL_EN)
		adc_aif2 |= RK3308_ADC_CH2_ZEROCROSS_DET_EN;

	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
			   RK3308_ADC_CH2_ALC_ZC_MSK,
			   adc_aif2);

	/*
	 * 5. Set ACODEC_ADC_ANA_CON5[7:0] to 0x77, to enable the clock and
	 * ADC module, and to end the initialization of ADC
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
			   RK3308_ADC_CH1_ADC_CLK_MSK |
			   RK3308_ADC_CH2_ADC_CLK_MSK,
			   0x77);

	/*
	 * 6. Set ACODEC_ADC_ANA_CON1[5:4] and ACODEC_ADC_ANA_CON1[1:0],
	 * to select the gain of the MIC
	 *
	 * By default is 0db.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON01(grp),
			   RK3308_ADC_CH1_MIC_GAIN_MSK,
			   RK3308_ADC_CH1_MIC_GAIN_0DB);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON01(grp),
			   RK3308_ADC_CH2_MIC_GAIN_MSK,
			   RK3308_ADC_CH2_MIC_GAIN_0DB);

	/*
	 * 7.Set ACODEC_ADC_ANA_CON3[4:0] and ACODEC_ADC_ANA_CON4[3:0] to
	 * select the gain of ALC
	 *
	 * By default is 0db.
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON03(grp),
			   RK3308_ADC_CH1_ALC_GAIN_MSK,
			   RK3308_ADC_CH1_ALC_GAIN_0DB);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON04(grp),
			   RK3308_ADC_CH2_ALC_GAIN_MSK,
			   RK3308_ADC_CH2_ALC_GAIN_0DB);

	/* 8.Begin recording */

	return 0;
}

static int rk3308_codec_adc_ana_disable(struct rk3308_codec_priv *rk3308)
{
	int grp = rk3308->adc_cur_grp;

	/*
	 * 1. Set ACODEC_ADC_ANA_CON2[7:0] to 0x0, to disable the ALC module,
	 * to disable the zero-crossing detection function, and to begin the
	 * initialization of ALC
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
			   RK3308_ADC_CH1_ALC_ZC_MSK,
			   0);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
			   RK3308_ADC_CH2_ALC_ZC_MSK,
			   0);

	/*
	 * 2. Set ACODEC_ADC_ANA_CON5[7:0] to 0x0, to disable the clock and
	 * ADC module, and to begin the initialization of ADC
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
			   RK3308_ADC_CH1_ADC_CLK_MSK,
			   0);
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON05(grp),
			   RK3308_ADC_CH2_ADC_CLK_MSK,
			   0);

	/*
	 * 3. Set ACODEC_ADC_ANA_CON0[7:0] to 0x88, to disable the MIC module,
	 * to disable the reference voltage buffer, and to begin the
	 * initialization of MIC
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON00(grp),
			   RK3308_ADC_CH1_CH2_MIC_ALL_MSK,
			   RK3308_ADC_CH1_MIC_MUTE |
			   RK3308_ADC_CH2_MIC_MUTE);

	/*
	 * 4. Set ACODEC_ADC_ANA_CON6[0] to 0x0, to disable the current
	 * source of audio
	 */
	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON06(grp),
			   RK3308_ADC_CURRENT_MSK,
			   RK3308_ADC_CURRENT_DIS);

	return 0;
}

static int rk3308_codec_open_capture(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	int grp, temp_grp;

	rk3308_codec_micbias_enable(rk3308, RK3308_ADC_MICBIAS_VOLT_0_7);

	temp_grp = rk3308->adc_cur_grp;
	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
		rk3308->adc_cur_grp = grp;
		rk3308_codec_alc_enable(rk3308);
		rk3308_codec_adc_ana_enable(rk3308);
	}
	rk3308->adc_cur_grp = temp_grp;

	return 0;
}

static int rk3308_codec_close_capture(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	int grp, temp_grp;

	temp_grp = rk3308->adc_cur_grp;
	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
		rk3308->adc_cur_grp = grp;
		rk3308_codec_alc_disable(rk3308);
		rk3308_codec_adc_ana_disable(rk3308);
	}
	rk3308->adc_cur_grp = temp_grp;

	rk3308_codec_micbias_disable(rk3308);

	return 0;
}

static int rk3308_codec_open_playback(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	rk3308_codec_dac_enable(rk3308);
	rk3308_speaker_ctl(rk3308, 1);

	return 0;
}

static int rk3308_codec_close_playback(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	rk3308_speaker_ctl(rk3308, 0);
	rk3308_codec_dac_disable(rk3308);

	return 0;
}

static int rk3308_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = rk3308_codec_open_playback(codec);
	else
		ret = rk3308_codec_open_capture(codec);

	regcache_cache_only(rk3308->regmap, false);
	regcache_sync(rk3308->regmap);

	return ret;
}

static void rk3308_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		rk3308_codec_close_playback(codec);
	else
		rk3308_codec_close_capture(codec);

	regcache_cache_only(rk3308->regmap, false);
	regcache_sync(rk3308->regmap);
}

static struct snd_soc_dai_ops rk3308_dai_ops = {
	.hw_params = rk3308_hw_params,
	.set_fmt = rk3308_set_dai_fmt,
	.digital_mute = rk3308_digital_mute,
	.startup = rk3308_pcm_startup,
	.shutdown = rk3308_pcm_shutdown,
};

static struct snd_soc_dai_driver rk3308_dai[] = {
	{
		.name = "rk3308-hifi",
		.id = RK3308_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rk3308_dai_ops,
	},
};

static int rk3308_suspend(struct snd_soc_codec *codec)
{
	rk3308_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int rk3308_resume(struct snd_soc_codec *codec)
{
	rk3308_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static int rk3308_probe(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	rk3308_codec_reset(codec);
	rk3308_codec_power_on(codec);
	rk3308_codec_headset_detect_enable(rk3308);

	regcache_cache_only(rk3308->regmap, false);
	regcache_sync(rk3308->regmap);

	return 0;
}

static int rk3308_remove(struct snd_soc_codec *codec)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_codec_get_drvdata(codec);

	rk3308_speaker_ctl(rk3308, 0);
	rk3308_codec_headset_detect_disable(rk3308);
	rk3308_codec_power_off(codec);

	regcache_cache_only(rk3308->regmap, false);
	regcache_sync(rk3308->regmap);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_rk3308 = {
	.probe = rk3308_probe,
	.remove = rk3308_remove,
	.suspend = rk3308_suspend,
	.resume = rk3308_resume,
	.set_bias_level = rk3308_set_bias_level,
	.controls = rk3308_codec_dapm_controls,
	.num_controls = ARRAY_SIZE(rk3308_codec_dapm_controls),
};

static const struct reg_default rk3308_codec_reg_defaults[] = {
	{ RK3308_GLB_CON, 0x07 },
};

static bool rk3308_codec_write_read_reg(struct device *dev, unsigned int reg)
{
	/* All registers can be read / write */
	return true;
}

static bool rk3308_codec_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static irqreturn_t rk3308_codec_headset_isr(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static const struct regmap_config rk3308_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = RK3308_DAC_ANA_CON13,
	.writeable_reg = rk3308_codec_write_read_reg,
	.readable_reg = rk3308_codec_write_read_reg,
	.volatile_reg = rk3308_codec_volatile_reg,
	.reg_defaults = rk3308_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk3308_codec_reg_defaults),
	.cache_type = REGCACHE_FLAT,
};

static ssize_t adc_ch_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);

	return sprintf(buf, "adc_cur_grp: %d\n", rk3308->adc_cur_grp);
}

static ssize_t adc_ch_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	unsigned long grp;
	int ret = kstrtoul(buf, 10, &grp);

	if (ret < 0 || grp > 4) {
		dev_err(dev, "Invalid LR grp: %ld, ret: %d\n", grp, ret);
		return -EINVAL;
	}

	rk3308->adc_cur_grp = grp;

	dev_info(dev, "store grp: %d\n", rk3308->adc_cur_grp);

	return count;
}

static ssize_t adc_grp0_in_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);

	return sprintf(buf, "adc ch0 using: %s\n",
		       rk3308->adc_grp0_using_linein ? "line in" : "mic in");
}

static ssize_t adc_grp0_in_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct rk3308_codec_priv *rk3308 =
		container_of(dev, struct rk3308_codec_priv, dev);
	unsigned long using_linein;
	int ret = kstrtoul(buf, 10, &using_linein);

	if (ret < 0 || using_linein > 1) {
		dev_err(dev, "Invalid input status: %ld, ret: %d\n",
			using_linein, ret);
		return -EINVAL;
	}

	rk3308->adc_grp0_using_linein = using_linein;

	dev_info(dev, "store using_linein: %d\n",
		 rk3308->adc_grp0_using_linein);

	return count;
}

static const struct device_attribute adc_ch_attrs[] = {
	__ATTR(adc_cur_grp, 0644, adc_ch_show, adc_ch_store),
	__ATTR(adc_grp0_in, 0644, adc_grp0_in_show, adc_grp0_in_store),
};

static void rk3308_codec_device_release(struct device *dev)
{
	/* Do nothing */
}

static int rk3308_codec_sysfs_init(struct platform_device *pdev,
				   struct rk3308_codec_priv *rk3308)
{
	struct device *dev = &rk3308->dev;
	int i;

	dev->release = rk3308_codec_device_release;
	dev->parent = &pdev->dev;
	set_dev_node(dev, dev_to_node(&pdev->dev));
	dev_set_name(dev, "rk3308-acodec-dev");

	if (device_register(dev)) {
		dev_err(&pdev->dev,
			"Register 'rk3308-acodec-dev' failed\n");
		dev->parent = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(adc_ch_attrs); i++) {
		if (device_create_file(dev, &adc_ch_attrs[i])) {
			dev_err(&pdev->dev,
				"Create 'rk3308-acodec-dev' attr failed\n");
			device_unregister(dev);
			return -ENOMEM;
		}
	}

	return 0;
}

#if defined(CONFIG_DEBUG_FS)
static int rk3308_codec_debugfs_reg_show(struct seq_file *s, void *v)
{
	struct rk3308_codec_priv *rk3308 = s->private;
	unsigned int i;
	unsigned int val;

	for (i = RK3308_GLB_CON; i <= RK3308_DAC_ANA_CON13; i += 4) {
		regmap_read(rk3308->regmap, i, &val);
		if (!(i % 16))
			seq_printf(s, "\nR:%04x: ", i);
		seq_printf(s, "%08x ", val);
	}

	seq_puts(s, "\n");

	return 0;
}

static ssize_t rk3308_codec_debugfs_reg_operate(struct file *file,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct rk3308_codec_priv *rk3308 =
		((struct seq_file *)file->private_data)->private;
	unsigned int reg, val;
	char op;
	char kbuf[32];
	int ret;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	ret = sscanf(kbuf, "%c,%x,%x", &op, &reg, &val);
	if (ret != 3) {
		pr_err("sscanf failed: %d\n", ret);
		return -EFAULT;
	}

	if (op == 'w') {
		pr_info("Write reg: 0x%04x with val: 0x%08x\n", reg, val);
		regmap_write(rk3308->regmap, reg, val);
		regcache_cache_only(rk3308->regmap, false);
		regcache_sync(rk3308->regmap);
		pr_info("Read back reg: 0x%04x with val: 0x%08x\n", reg, val);
	} else if (op == 'r') {
		regmap_read(rk3308->regmap, reg, &val);
		pr_info("Read reg: 0x%04x with val: 0x%08x\n", reg, val);
	} else {
		pr_err("This is an invalid operation: %c\n", op);
	}

	return count;
}

static int rk3308_codec_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file,
			   rk3308_codec_debugfs_reg_show, inode->i_private);
}

static const struct file_operations rk3308_codec_reg_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rk3308_codec_debugfs_open,
	.read = seq_read,
	.write = rk3308_codec_debugfs_reg_operate,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif /* CONFIG_DEBUG_FS */

static int rk3308_platform_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk3308_codec_priv *rk3308;
	struct resource *res;
	struct regmap *grf;
	void __iomem *base;
	int irq;
	int ret;

	grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(&pdev->dev,
			"Missing 'rockchip,grf' property\n");
		return PTR_ERR(grf);
	}

	rk3308 = devm_kzalloc(&pdev->dev, sizeof(*rk3308), GFP_KERNEL);
	if (!rk3308)
		return -ENOMEM;

	ret = rk3308_codec_sysfs_init(pdev, rk3308);
	if (ret < 0) {
		dev_err(&pdev->dev, "Sysfs init failed\n");
		return ret;
	}

#if defined(CONFIG_DEBUG_FS)
	rk3308->dbg_codec = debugfs_create_dir(CODEC_DRV_NAME, NULL);
	if (IS_ERR(rk3308->dbg_codec))
		dev_err(&pdev->dev,
			"Failed to create debugfs dir for rk3308!\n");
	else
		debugfs_create_file("reg", 0644, rk3308->dbg_codec,
				    rk3308, &rk3308_codec_reg_debugfs_fops);
#endif
	rk3308->plat_dev = &pdev->dev;

	rk3308->reset = devm_reset_control_get(&pdev->dev, "acodec-reset");
	if (IS_ERR(rk3308->reset)) {
		ret = PTR_ERR(rk3308->reset);
		if (ret != -ENOENT)
			return ret;

		dev_dbg(&pdev->dev, "No reset control found\n");
		rk3308->reset = NULL;
	}

	/* GPIO0_A5 control speaker on RK3308 EVB */
	rk3308->spk_ctl_gpio = devm_gpiod_get_optional(&pdev->dev, "spk_ctl",
						       GPIOD_OUT_HIGH);
	if (IS_ERR(rk3308->spk_ctl_gpio)) {
		ret = PTR_ERR(rk3308->spk_ctl_gpio);
		dev_err(&pdev->dev, "Unable to claim gpio spk_ctl\n");
		return ret;
	}

	rk3308->pclk = devm_clk_get(&pdev->dev, "acodec");
	if (IS_ERR(rk3308->pclk)) {
		dev_err(&pdev->dev, "Can't get acodec pclk\n");
		return PTR_ERR(rk3308->pclk);
	}

	rk3308->mclk_rx = devm_clk_get(&pdev->dev, "mclk_rx");
	if (IS_ERR(rk3308->mclk_rx)) {
		dev_err(&pdev->dev, "Can't get acodec mclk_rx\n");
		return PTR_ERR(rk3308->mclk_rx);
	}

	rk3308->mclk_tx = devm_clk_get(&pdev->dev, "mclk_tx");
	if (IS_ERR(rk3308->mclk_tx)) {
		dev_err(&pdev->dev, "Can't get acodec mclk_tx\n");
		return PTR_ERR(rk3308->mclk_tx);
	}

	ret = clk_prepare_enable(rk3308->pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable acodec pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(rk3308->mclk_rx);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable i2s mclk_rx: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(rk3308->mclk_tx);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable i2s mclk_tx: %d\n", ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		dev_err(&pdev->dev, "Failed to ioremap resource\n");
		goto failed;
	}

	rk3308->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					       &rk3308_codec_regmap_config);
	if (IS_ERR(rk3308->regmap)) {
		ret = PTR_ERR(rk3308->regmap);
		dev_err(&pdev->dev, "Failed to regmap mmio\n");
		goto failed;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Can not get codec irq\n");
		goto failed;
	}

	ret = devm_request_irq(&pdev->dev, irq, rk3308_codec_headset_isr,
			       0, "headset-irq", rk3308);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request IRQ: %d\n", ret);
		goto failed;
	}

	rk3308->adc_grp0_using_linein = 0;

	platform_set_drvdata(pdev, rk3308);

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_rk3308,
				     rk3308_dai, ARRAY_SIZE(rk3308_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register codec: %d\n", ret);
		goto failed;
	}

	return ret;

failed:
	clk_disable_unprepare(rk3308->mclk_rx);
	clk_disable_unprepare(rk3308->mclk_tx);
	clk_disable_unprepare(rk3308->pclk);

	return ret;
}

static int rk3308_platform_remove(struct platform_device *pdev)
{
	struct rk3308_codec_priv *rk3308 =
		(struct rk3308_codec_priv *)platform_get_drvdata(pdev);

	clk_disable_unprepare(rk3308->mclk_rx);
	clk_disable_unprepare(rk3308->mclk_tx);
	clk_disable_unprepare(rk3308->pclk);
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static const struct of_device_id rk3308codec_of_match[] = {
	{ .compatible = "rockchip,rk3308-codec", },
	{},
};
MODULE_DEVICE_TABLE(of, rk3308codec_of_match);

static struct platform_driver rk3308_codec_driver = {
	.driver = {
		   .name = CODEC_DRV_NAME,
		   .of_match_table = of_match_ptr(rk3308codec_of_match),
	},
	.probe = rk3308_platform_probe,
	.remove = rk3308_platform_remove,
};
module_platform_driver(rk3308_codec_driver);

MODULE_AUTHOR("Xing Zheng <zhengxing@rock-chips.com>");
MODULE_DESCRIPTION("ASoC RK3308 Codec Driver");
MODULE_LICENSE("GPL v2");
