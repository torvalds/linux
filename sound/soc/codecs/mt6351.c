// SPDX-License-Identifier: GPL-2.0
//
// mt6351.c  --  mt6351 ALSA SoC audio codec driver
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/delay.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "mt6351.h"

/* MT6351_TOP_CLKSQ */
#define RG_CLKSQ_EN_AUD_BIT (0)

/* MT6351_TOP_CKPDN_CON0 */
#define RG_AUDNCP_CK_PDN_BIT (12)
#define RG_AUDIF_CK_PDN_BIT (13)
#define RG_AUD_CK_PDN_BIT (14)
#define RG_ZCD13M_CK_PDN_BIT (15)

/* MT6351_AUDDEC_ANA_CON0 */
#define RG_AUDDACLPWRUP_VAUDP32_BIT (0)
#define RG_AUDDACRPWRUP_VAUDP32_BIT (1)
#define RG_AUD_DAC_PWR_UP_VA32_BIT (2)
#define RG_AUD_DAC_PWL_UP_VA32_BIT (3)

#define RG_AUDHSPWRUP_VAUDP32_BIT (4)

#define RG_AUDHPLPWRUP_VAUDP32_BIT (5)
#define RG_AUDHPRPWRUP_VAUDP32_BIT (6)

#define RG_AUDHSMUXINPUTSEL_VAUDP32_SFT (7)
#define RG_AUDHSMUXINPUTSEL_VAUDP32_MASK (0x3)

#define RG_AUDHPLMUXINPUTSEL_VAUDP32_SFT (9)
#define RG_AUDHPLMUXINPUTSEL_VAUDP32_MASK (0x3)

#define RG_AUDHPRMUXINPUTSEL_VAUDP32_SFT (11)
#define RG_AUDHPRMUXINPUTSEL_VAUDP32_MASK (0x3)

#define RG_AUDHSSCDISABLE_VAUDP32 (13)
#define RG_AUDHPLSCDISABLE_VAUDP32_BIT (14)
#define RG_AUDHPRSCDISABLE_VAUDP32_BIT (15)

/* MT6351_AUDDEC_ANA_CON1 */
#define RG_HSOUTPUTSTBENH_VAUDP32_BIT (8)

/* MT6351_AUDDEC_ANA_CON3 */
#define RG_AUDLOLPWRUP_VAUDP32_BIT (2)

#define RG_AUDLOLMUXINPUTSEL_VAUDP32_SFT (3)
#define RG_AUDLOLMUXINPUTSEL_VAUDP32_MASK (0x3)

#define RG_AUDLOLSCDISABLE_VAUDP32_BIT (5)
#define RG_LOOUTPUTSTBENH_VAUDP32_BIT (9)

/* MT6351_AUDDEC_ANA_CON6 */
#define RG_ABIDEC_RSVD0_VAUDP32_HPL_BIT (8)
#define RG_ABIDEC_RSVD0_VAUDP32_HPR_BIT (9)
#define RG_ABIDEC_RSVD0_VAUDP32_HS_BIT (10)
#define RG_ABIDEC_RSVD0_VAUDP32_LOL_BIT (11)

/* MT6351_AUDDEC_ANA_CON9 */
#define RG_AUDIBIASPWRDN_VAUDP32_BIT (8)
#define RG_RSTB_DECODER_VA32_BIT (9)
#define RG_AUDGLB_PWRDN_VA32_BIT (12)

#define RG_LCLDO_DEC_EN_VA32_BIT (13)
#define RG_LCLDO_DEC_REMOTE_SENSE_VA18_BIT (15)
/* MT6351_AUDDEC_ANA_CON10 */
#define RG_NVREG_EN_VAUDP32_BIT (8)

#define RG_AUDGLB_LP2_VOW_EN_VA32 10

/* MT6351_AFE_UL_DL_CON0 */
#define RG_AFE_ON_BIT (0)

/* MT6351_AFE_DL_SRC2_CON0_L */
#define RG_DL_2_SRC_ON_TMP_CTL_PRE_BIT (0)

/* MT6351_AFE_UL_SRC_CON0_L */
#define UL_SRC_ON_TMP_CTL (0)

/* MT6351_AFE_TOP_CON0 */
#define RG_DL_SINE_ON_SFT (0)
#define RG_DL_SINE_ON_MASK (0x1)

#define RG_UL_SINE_ON_SFT (1)
#define RG_UL_SINE_ON_MASK (0x1)

/* MT6351_AUDIO_TOP_CON0 */
#define AUD_TOP_PDN_RESERVED_BIT 0
#define AUD_TOP_PWR_CLK_DIS_CTL_BIT 2
#define AUD_TOP_PDN_ADC_CTL_BIT 5
#define AUD_TOP_PDN_DAC_CTL_BIT 6
#define AUD_TOP_PDN_AFE_CTL_BIT 7

/* MT6351_AFE_SGEN_CFG0 */
#define SGEN_C_MUTE_SW_CTL_BIT 6
#define SGEN_C_DAC_EN_CTL_BIT 7

/* MT6351_AFE_NCP_CFG0 */
#define RG_NCP_ON_BIT 0

/* MT6351_LDO_VUSB33_CON0 */
#define RG_VUSB33_EN 1
#define RG_VUSB33_ON_CTRL 3

/* MT6351_LDO_VA18_CON0 */
#define RG_VA18_EN 1
#define RG_VA18_ON_CTRL 3

/* MT6351_AUDENC_ANA_CON0 */
#define RG_AUDPREAMPLON 0
#define RG_AUDPREAMPLDCCEN 1
#define RG_AUDPREAMPLDCPRECHARGE 2

#define RG_AUDPREAMPLINPUTSEL_SFT (4)
#define RG_AUDPREAMPLINPUTSEL_MASK (0x3)

#define RG_AUDADCLPWRUP 12

#define RG_AUDADCLINPUTSEL_SFT (13)
#define RG_AUDADCLINPUTSEL_MASK (0x3)

/* MT6351_AUDENC_ANA_CON1 */
#define RG_AUDPREAMPRON 0
#define RG_AUDPREAMPRDCCEN 1
#define RG_AUDPREAMPRDCPRECHARGE 2

#define RG_AUDPREAMPRINPUTSEL_SFT (4)
#define RG_AUDPREAMPRINPUTSEL_MASK (0x3)

#define RG_AUDADCRPWRUP 12

#define RG_AUDADCRINPUTSEL_SFT (13)
#define RG_AUDADCRINPUTSEL_MASK (0x3)

/* MT6351_AUDENC_ANA_CON3 */
#define RG_AUDADCCLKRSTB 6

/* MT6351_AUDENC_ANA_CON9 */
#define RG_AUDPWDBMICBIAS0 0
#define RG_AUDMICBIAS0VREF 4
#define RG_AUDMICBIAS0LOWPEN 7

#define RG_AUDPWDBMICBIAS2 8
#define RG_AUDMICBIAS2VREF 12
#define RG_AUDMICBIAS2LOWPEN 15

/* MT6351_AUDENC_ANA_CON10 */
#define RG_AUDPWDBMICBIAS1 0
#define RG_AUDMICBIAS1DCSW1NEN 2
#define RG_AUDMICBIAS1VREF 4
#define RG_AUDMICBIAS1LOWPEN 7

enum {
	AUDIO_ANALOG_VOLUME_HSOUTL,
	AUDIO_ANALOG_VOLUME_HSOUTR,
	AUDIO_ANALOG_VOLUME_HPOUTL,
	AUDIO_ANALOG_VOLUME_HPOUTR,
	AUDIO_ANALOG_VOLUME_LINEOUTL,
	AUDIO_ANALOG_VOLUME_LINEOUTR,
	AUDIO_ANALOG_VOLUME_MICAMP1,
	AUDIO_ANALOG_VOLUME_MICAMP2,
	AUDIO_ANALOG_VOLUME_TYPE_MAX
};

/* Supply subseq */
enum {
	SUPPLY_SUBSEQ_SETTING,
	SUPPLY_SUBSEQ_ENABLE,
	SUPPLY_SUBSEQ_MICBIAS,
};

#define REG_STRIDE 2

struct mt6351_priv {
	struct device *dev;
	struct regmap *regmap;

	unsigned int dl_rate;
	unsigned int ul_rate;

	int ana_gain[AUDIO_ANALOG_VOLUME_TYPE_MAX];

	int hp_en_counter;
};

static void set_hp_gain_zero(struct snd_soc_component *cmpnt)
{
	regmap_update_bits(cmpnt->regmap, MT6351_ZCD_CON2,
			   0x1f << 7, 0x8 << 7);
	regmap_update_bits(cmpnt->regmap, MT6351_ZCD_CON2,
			   0x1f << 0, 0x8 << 0);
}

static unsigned int get_cap_reg_val(struct snd_soc_component *cmpnt,
				    unsigned int rate)
{
	switch (rate) {
	case 8000:
		return 0;
	case 16000:
		return 1;
	case 32000:
		return 2;
	case 48000:
		return 3;
	case 96000:
		return 4;
	case 192000:
		return 5;
	default:
		dev_warn(cmpnt->dev, "%s(), error rate %d, return 3",
			 __func__, rate);
		return 3;
	}
}

static unsigned int get_play_reg_val(struct snd_soc_component *cmpnt,
				     unsigned int rate)
{
	switch (rate) {
	case 8000:
		return 0;
	case 11025:
		return 1;
	case 12000:
		return 2;
	case 16000:
		return 3;
	case 22050:
		return 4;
	case 24000:
		return 5;
	case 32000:
		return 6;
	case 44100:
		return 7;
	case 48000:
	case 96000:
	case 192000:
		return 8;
	default:
		dev_warn(cmpnt->dev, "%s(), error rate %d, return 8",
			 __func__, rate);
		return 8;
	}
}

static int mt6351_codec_dai_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *cmpnt = dai->component;
	struct mt6351_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int rate = params_rate(params);

	dev_dbg(priv->dev, "%s(), substream->stream %d, rate %d\n",
		__func__, substream->stream, rate);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		priv->dl_rate = rate;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		priv->ul_rate = rate;

	return 0;
}

static const struct snd_soc_dai_ops mt6351_codec_dai_ops = {
	.hw_params = mt6351_codec_dai_hw_params,
};

#define MT6351_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE)

static struct snd_soc_dai_driver mt6351_dai_driver[] = {
	{
		.name = "mt6351-snd-codec-aif1",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000 |
				 SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = MT6351_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_48000 |
				 SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = MT6351_FORMATS,
		},
		.ops = &mt6351_codec_dai_ops,
	},
};

enum {
	HP_GAIN_SET_ZERO,
	HP_GAIN_RESTORE,
};

static void hp_gain_ramp_set(struct snd_soc_component *cmpnt, int hp_gain_ctl)
{
	struct mt6351_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int idx, old_idx, offset, reg_idx;

	if (hp_gain_ctl == HP_GAIN_SET_ZERO) {
		idx = 8;	/* 0dB */
		old_idx = priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL];
	} else {
		idx = priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL];
		old_idx = 8;	/* 0dB */
	}
	dev_dbg(priv->dev, "%s(), idx %d, old_idx %d\n",
		__func__, idx, old_idx);

	if (idx > old_idx)
		offset = idx - old_idx;
	else
		offset = old_idx - idx;

	reg_idx = old_idx;

	while (offset > 0) {
		reg_idx = idx > old_idx ? reg_idx + 1 : reg_idx - 1;

		/* check valid range, and set value */
		if ((reg_idx >= 0 && reg_idx <= 0x12) || reg_idx == 0x1f) {
			regmap_update_bits(cmpnt->regmap,
					   MT6351_ZCD_CON2,
					   0xf9f,
					   (reg_idx << 7) | reg_idx);
			usleep_range(100, 120);
		}
		offset--;
	}
}

static void hp_zcd_enable(struct snd_soc_component *cmpnt)
{
	/* Enable ZCD, for minimize pop noise */
	/* when adjust gain during HP buffer on */
	regmap_update_bits(cmpnt->regmap, MT6351_ZCD_CON0, 0x7 << 8, 0x1 << 8);
	regmap_update_bits(cmpnt->regmap, MT6351_ZCD_CON0, 0x1 << 7, 0x0 << 7);

	/* timeout, 1=5ms, 0=30ms */
	regmap_update_bits(cmpnt->regmap, MT6351_ZCD_CON0, 0x1 << 6, 0x1 << 6);

	regmap_update_bits(cmpnt->regmap, MT6351_ZCD_CON0, 0x3 << 4, 0x0 << 4);
	regmap_update_bits(cmpnt->regmap, MT6351_ZCD_CON0, 0x7 << 1, 0x5 << 1);
	regmap_update_bits(cmpnt->regmap, MT6351_ZCD_CON0, 0x1 << 0, 0x1 << 0);
}

static void hp_zcd_disable(struct snd_soc_component *cmpnt)
{
	regmap_write(cmpnt->regmap, MT6351_ZCD_CON0, 0x0000);
}

static const DECLARE_TLV_DB_SCALE(playback_tlv, -1000, 100, 0);
static const DECLARE_TLV_DB_SCALE(pga_tlv, 0, 600, 0);

static const struct snd_kcontrol_new mt6351_snd_controls[] = {
	/* dl pga gain */
	SOC_DOUBLE_TLV("Headphone Volume",
		       MT6351_ZCD_CON2, 0, 7, 0x12, 1,
		       playback_tlv),
	SOC_DOUBLE_TLV("Lineout Volume",
		       MT6351_ZCD_CON1, 0, 7, 0x12, 1,
		       playback_tlv),
	SOC_SINGLE_TLV("Handset Volume",
		       MT6351_ZCD_CON3, 0, 0x12, 1,
		       playback_tlv),
       /* ul pga gain */
	SOC_DOUBLE_R_TLV("PGA Volume",
			 MT6351_AUDENC_ANA_CON0, MT6351_AUDENC_ANA_CON1,
			 8, 4, 0,
			 pga_tlv),
};

/* MUX */

/* LOL MUX */
static const char *const lo_in_mux_map[] = {
	"Open", "Mute", "Playback", "Test Mode",
};

static int lo_in_mux_map_value[] = {
	0x0, 0x1, 0x2, 0x3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(lo_in_mux_map_enum,
				  MT6351_AUDDEC_ANA_CON3,
				  RG_AUDLOLMUXINPUTSEL_VAUDP32_SFT,
				  RG_AUDLOLMUXINPUTSEL_VAUDP32_MASK,
				  lo_in_mux_map,
				  lo_in_mux_map_value);

static const struct snd_kcontrol_new lo_in_mux_control =
	SOC_DAPM_ENUM("In Select", lo_in_mux_map_enum);

/*HP MUX */
static const char *const hp_in_mux_map[] = {
	"Open", "LoudSPK Playback", "Audio Playback", "Test Mode",
};

static int hp_in_mux_map_value[] = {
	0x0, 0x1, 0x2, 0x3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(hpl_in_mux_map_enum,
				  MT6351_AUDDEC_ANA_CON0,
				  RG_AUDHPLMUXINPUTSEL_VAUDP32_SFT,
				  RG_AUDHPLMUXINPUTSEL_VAUDP32_MASK,
				  hp_in_mux_map,
				  hp_in_mux_map_value);

static const struct snd_kcontrol_new hpl_in_mux_control =
	SOC_DAPM_ENUM("HPL Select", hpl_in_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hpr_in_mux_map_enum,
				  MT6351_AUDDEC_ANA_CON0,
				  RG_AUDHPRMUXINPUTSEL_VAUDP32_SFT,
				  RG_AUDHPRMUXINPUTSEL_VAUDP32_MASK,
				  hp_in_mux_map,
				  hp_in_mux_map_value);

static const struct snd_kcontrol_new hpr_in_mux_control =
	SOC_DAPM_ENUM("HPR Select", hpr_in_mux_map_enum);

/* RCV MUX */
static const char *const rcv_in_mux_map[] = {
	"Open", "Mute", "Voice Playback", "Test Mode",
};

static int rcv_in_mux_map_value[] = {
	0x0, 0x1, 0x2, 0x3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(rcv_in_mux_map_enum,
				  MT6351_AUDDEC_ANA_CON0,
				  RG_AUDHSMUXINPUTSEL_VAUDP32_SFT,
				  RG_AUDHSMUXINPUTSEL_VAUDP32_MASK,
				  rcv_in_mux_map,
				  rcv_in_mux_map_value);

static const struct snd_kcontrol_new rcv_in_mux_control =
	SOC_DAPM_ENUM("RCV Select", rcv_in_mux_map_enum);

/* DAC In MUX */
static const char *const dac_in_mux_map[] = {
	"Normal Path", "Sgen",
};

static int dac_in_mux_map_value[] = {
	0x0, 0x1,
};

static SOC_VALUE_ENUM_SINGLE_DECL(dac_in_mux_map_enum,
				  MT6351_AFE_TOP_CON0,
				  RG_DL_SINE_ON_SFT,
				  RG_DL_SINE_ON_MASK,
				  dac_in_mux_map,
				  dac_in_mux_map_value);

static const struct snd_kcontrol_new dac_in_mux_control =
	SOC_DAPM_ENUM("DAC Select", dac_in_mux_map_enum);

/* AIF Out MUX */
static SOC_VALUE_ENUM_SINGLE_DECL(aif_out_mux_map_enum,
				  MT6351_AFE_TOP_CON0,
				  RG_UL_SINE_ON_SFT,
				  RG_UL_SINE_ON_MASK,
				  dac_in_mux_map,
				  dac_in_mux_map_value);

static const struct snd_kcontrol_new aif_out_mux_control =
	SOC_DAPM_ENUM("AIF Out Select", aif_out_mux_map_enum);

/* ADC L MUX */
static const char *const adc_left_mux_map[] = {
	"Idle", "AIN0", "Left Preamplifier", "Idle_1",
};

static int adc_left_mux_map_value[] = {
	0x0, 0x1, 0x2, 0x3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(adc_left_mux_map_enum,
				  MT6351_AUDENC_ANA_CON0,
				  RG_AUDADCLINPUTSEL_SFT,
				  RG_AUDADCLINPUTSEL_MASK,
				  adc_left_mux_map,
				  adc_left_mux_map_value);

static const struct snd_kcontrol_new adc_left_mux_control =
	SOC_DAPM_ENUM("ADC L Select", adc_left_mux_map_enum);

/* ADC R MUX */
static const char *const adc_right_mux_map[] = {
	"Idle", "AIN0", "Right Preamplifier", "Idle_1",
};

static int adc_right_mux_map_value[] = {
	0x0, 0x1, 0x2, 0x3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(adc_right_mux_map_enum,
				  MT6351_AUDENC_ANA_CON1,
				  RG_AUDADCRINPUTSEL_SFT,
				  RG_AUDADCRINPUTSEL_MASK,
				  adc_right_mux_map,
				  adc_right_mux_map_value);

static const struct snd_kcontrol_new adc_right_mux_control =
	SOC_DAPM_ENUM("ADC R Select", adc_right_mux_map_enum);

/* PGA L MUX */
static const char *const pga_left_mux_map[] = {
	"None", "AIN0", "AIN1", "AIN2",
};

static int pga_left_mux_map_value[] = {
	0x0, 0x1, 0x2, 0x3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(pga_left_mux_map_enum,
				  MT6351_AUDENC_ANA_CON0,
				  RG_AUDPREAMPLINPUTSEL_SFT,
				  RG_AUDPREAMPLINPUTSEL_MASK,
				  pga_left_mux_map,
				  pga_left_mux_map_value);

static const struct snd_kcontrol_new pga_left_mux_control =
	SOC_DAPM_ENUM("PGA L Select", pga_left_mux_map_enum);

/* PGA R MUX */
static const char *const pga_right_mux_map[] = {
	"None", "AIN0", "AIN3", "AIN2",
};

static int pga_right_mux_map_value[] = {
	0x0, 0x1, 0x2, 0x3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(pga_right_mux_map_enum,
				  MT6351_AUDENC_ANA_CON1,
				  RG_AUDPREAMPRINPUTSEL_SFT,
				  RG_AUDPREAMPRINPUTSEL_MASK,
				  pga_right_mux_map,
				  pga_right_mux_map_value);

static const struct snd_kcontrol_new pga_right_mux_control =
	SOC_DAPM_ENUM("PGA R Select", pga_right_mux_map_enum);

static int mt_reg_set_clr_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol,
				int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (w->on_val) {
			/* SET REG */
			regmap_update_bits(cmpnt->regmap,
					   w->reg + REG_STRIDE,
					   0x1 << w->shift,
					   0x1 << w->shift);
		} else {
			/* CLR REG */
			regmap_update_bits(cmpnt->regmap,
					   w->reg + REG_STRIDE * 2,
					   0x1 << w->shift,
					   0x1 << w->shift);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		if (w->off_val) {
			/* SET REG */
			regmap_update_bits(cmpnt->regmap,
					   w->reg + REG_STRIDE,
					   0x1 << w->shift,
					   0x1 << w->shift);
		} else {
			/* CLR REG */
			regmap_update_bits(cmpnt->regmap,
					   w->reg + REG_STRIDE * 2,
					   0x1 << w->shift,
					   0x1 << w->shift);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int mt_ncp_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol,
			int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_NCP_CFG1,
				   0xffff, 0x1515);
		/* NCP: ck1 and ck2 clock frequecy adjust configure */
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_NCP_CFG0,
				   0xfffe, 0x8C00);
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(250, 270);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_sgen_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol,
			 int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_SGEN_CFG0,
				   0xffef, 0x0008);
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_SGEN_CFG1,
				   0xffff, 0x0101);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_aif_in_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol,
			   int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6351_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event 0x%x, rate %d\n",
		__func__, event, priv->dl_rate);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* sdm audio fifo clock power on */
		regmap_update_bits(cmpnt->regmap, MT6351_AFUNC_AUD_CON2,
				   0xffff, 0x0006);
		/* scrambler clock on enable */
		regmap_update_bits(cmpnt->regmap, MT6351_AFUNC_AUD_CON0,
				   0xffff, 0xC3A1);
		/* sdm power on */
		regmap_update_bits(cmpnt->regmap, MT6351_AFUNC_AUD_CON2,
				   0xffff, 0x0003);
		/* sdm fifo enable */
		regmap_update_bits(cmpnt->regmap, MT6351_AFUNC_AUD_CON2,
				   0xffff, 0x000B);
		/* set attenuation gain */
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_DL_SDM_CON1,
				   0xffff, 0x001E);

		regmap_write(cmpnt->regmap, MT6351_AFE_PMIC_NEWIF_CFG0,
			     (get_play_reg_val(cmpnt, priv->dl_rate) << 12) |
			     0x330);
		regmap_write(cmpnt->regmap, MT6351_AFE_DL_SRC2_CON0_H,
			     (get_play_reg_val(cmpnt, priv->dl_rate) << 12) |
			     0x300);

		regmap_update_bits(cmpnt->regmap, MT6351_AFE_PMIC_NEWIF_CFG2,
				   0x8000, 0x8000);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_hp_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol,
		       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6351_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int reg;

	dev_dbg(priv->dev, "%s(), event 0x%x, hp_en_counter %d\n",
		__func__, event, priv->hp_en_counter);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		priv->hp_en_counter++;
		if (priv->hp_en_counter > 1)
			break;	/* already enabled, do nothing */
		else if (priv->hp_en_counter <= 0)
			dev_err(priv->dev, "%s(), hp_en_counter %d <= 0\n",
				__func__,
				priv->hp_en_counter);

		hp_zcd_disable(cmpnt);

		/* from yoyo HQA script */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON6,
				   0x0700, 0x0700);

		/* save target gain to restore after hardware open complete */
		regmap_read(cmpnt->regmap, MT6351_ZCD_CON2, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL] = reg & 0x1f;
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTR] = (reg >> 7) & 0x1f;

		/* Set HPR/HPL gain as minimum (~ -40dB) */
		regmap_update_bits(cmpnt->regmap,
				   MT6351_ZCD_CON2, 0xffff, 0x0F9F);
		/* Set HS gain as minimum (~ -40dB) */
		regmap_update_bits(cmpnt->regmap,
				   MT6351_ZCD_CON3, 0xffff, 0x001F);
		/* De_OSC of HP */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON2,
				   0x0001, 0x0001);
		/* enable output STBENH */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON1,
				   0xffff, 0x2000);
		/* De_OSC of voice, enable output STBENH */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON1,
				   0xffff, 0x2100);
		/* Enable voice driver */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON0,
				   0x0010, 0xE090);
		/* Enable pre-charge buffer  */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON1,
				   0xffff, 0x2140);

		usleep_range(50, 60);

		/* Apply digital DC compensation value to DAC */
		set_hp_gain_zero(cmpnt);

		/* Enable HPR/HPL */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON1,
				   0xffff, 0x2100);
		/* Disable pre-charge buffer */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON1,
				   0xffff, 0x2000);
		/* Disable De_OSC of voice */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON0,
				   0x0010, 0xF4EF);
		/* Disable voice buffer */

		/* from yoyo HQ */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON6,
				   0x0700, 0x0300);

		/* Enable ZCD, for minimize pop noise */
		/* when adjust gain during HP buffer on */
		hp_zcd_enable(cmpnt);

		/* apply volume setting */
		hp_gain_ramp_set(cmpnt, HP_GAIN_RESTORE);

		break;
	case SND_SOC_DAPM_PRE_PMD:
		priv->hp_en_counter--;
		if (priv->hp_en_counter > 0)
			break;	/* still being used, don't close */
		else if (priv->hp_en_counter < 0)
			dev_err(priv->dev, "%s(), hp_en_counter %d <= 0\n",
				__func__,
				priv->hp_en_counter);

		/* Disable AUD_ZCD */
		hp_zcd_disable(cmpnt);

		/* Set HPR/HPL gain as -1dB, step by step */
		hp_gain_ramp_set(cmpnt, HP_GAIN_SET_ZERO);

		set_hp_gain_zero(cmpnt);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (priv->hp_en_counter > 0)
			break;	/* still being used, don't close */
		else if (priv->hp_en_counter < 0)
			dev_err(priv->dev, "%s(), hp_en_counter %d <= 0\n",
				__func__,
				priv->hp_en_counter);

		/* reset*/
		regmap_update_bits(cmpnt->regmap,
				   MT6351_AUDDEC_ANA_CON6,
				   0x0700,
				   0x0000);
		/* De_OSC of HP */
		regmap_update_bits(cmpnt->regmap,
				   MT6351_AUDDEC_ANA_CON2,
				   0x0001,
				   0x0000);

		/* apply volume setting */
		hp_gain_ramp_set(cmpnt, HP_GAIN_RESTORE);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_aif_out_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6351_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event 0x%x, rate %d\n",
		__func__, event, priv->ul_rate);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* dcclk_div=11'b00100000011, dcclk_ref_ck_sel=2'b00 */
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_DCCLK_CFG0,
				   0xffff, 0x2062);
		/* dcclk_pdn=1'b0 */
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_DCCLK_CFG0,
				   0xffff, 0x2060);
		/* dcclk_gen_on=1'b1 */
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_DCCLK_CFG0,
				   0xffff, 0x2061);

		/* UL sample rate and mode configure */
		regmap_update_bits(cmpnt->regmap, MT6351_AFE_UL_SRC_CON0_H,
				   0x000E,
				   get_cap_reg_val(cmpnt, priv->ul_rate) << 1);

		/* fixed 260k path for 8/16/32/48 */
		if (priv->ul_rate <= 48000) {
			/* anc ul path src on */
			regmap_update_bits(cmpnt->regmap,
					   MT6351_AFE_HPANC_CFG0,
					   0x1 << 1,
					   0x1 << 1);
			/* ANC clk pdn release */
			regmap_update_bits(cmpnt->regmap,
					   MT6351_AFE_HPANC_CFG0,
					   0x1 << 0,
					   0x0 << 0);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* fixed 260k path for 8/16/32/48 */
		if (priv->ul_rate <= 48000) {
			/* anc ul path src on */
			regmap_update_bits(cmpnt->regmap,
					   MT6351_AFE_HPANC_CFG0,
					   0x1 << 1,
					   0x0 << 1);
			/* ANC clk pdn release */
			regmap_update_bits(cmpnt->regmap,
					   MT6351_AFE_HPANC_CFG0,
					   0x1 << 0,
					   0x1 << 0);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int mt_adc_clkgen_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol,
			       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Audio ADC clock gen. mode: 00_divided by 2 (Normal) */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON3,
				   0x3 << 4, 0x0);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* ADC CLK from: 00_13MHz from CLKSQ (Default) */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON3,
				   0x3 << 2, 0x0);
		break;
	default:
		break;
	}
	return 0;
}

static int mt_pga_left_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Audio L PGA precharge on */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON0,
				   0x3 << RG_AUDPREAMPLDCPRECHARGE,
				   0x1 << RG_AUDPREAMPLDCPRECHARGE);
		/* Audio L PGA mode: 1_DCC */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON0,
				   0x3 << RG_AUDPREAMPLDCCEN,
				   0x1 << RG_AUDPREAMPLDCCEN);
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(100, 120);
		/* Audio L PGA precharge off */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON0,
				   0x3 << RG_AUDPREAMPLDCPRECHARGE,
				   0x0 << RG_AUDPREAMPLDCPRECHARGE);
		break;
	default:
		break;
	}
	return 0;
}

static int mt_pga_right_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Audio R PGA precharge on */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON1,
				   0x3 << RG_AUDPREAMPRDCPRECHARGE,
				   0x1 << RG_AUDPREAMPRDCPRECHARGE);
		/* Audio R PGA mode: 1_DCC */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON1,
				   0x3 << RG_AUDPREAMPRDCCEN,
				   0x1 << RG_AUDPREAMPRDCCEN);
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(100, 120);
		/* Audio R PGA precharge off */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON1,
				   0x3 << RG_AUDPREAMPRDCPRECHARGE,
				   0x0 << RG_AUDPREAMPRDCPRECHARGE);
		break;
	default:
		break;
	}
	return 0;
}

static int mt_mic_bias_0_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol,
			       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* MIC Bias 0 LowPower: 0_Normal */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON9,
				   0x3 << RG_AUDMICBIAS0LOWPEN, 0x0);
		/* MISBIAS0 = 1P9V */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON9,
				   0x7 << RG_AUDMICBIAS0VREF,
				   0x2 << RG_AUDMICBIAS0VREF);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* MISBIAS0 = 1P97 */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON9,
				   0x7 << RG_AUDMICBIAS0VREF,
				   0x0 << RG_AUDMICBIAS0VREF);
		break;
	default:
		break;
	}
	return 0;
}

static int mt_mic_bias_1_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol,
			       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* MIC Bias 1 LowPower: 0_Normal */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON10,
				   0x3 << RG_AUDMICBIAS1LOWPEN, 0x0);
		/* MISBIAS1 = 2P7V */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON10,
				   0x7 << RG_AUDMICBIAS1VREF,
				   0x7 << RG_AUDMICBIAS1VREF);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* MISBIAS1 = 1P7V */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON10,
				   0x7 << RG_AUDMICBIAS1VREF,
				   0x0 << RG_AUDMICBIAS1VREF);
		break;
	default:
		break;
	}
	return 0;
}

static int mt_mic_bias_2_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol,
			       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* MIC Bias 2 LowPower: 0_Normal */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON9,
				   0x3 << RG_AUDMICBIAS2LOWPEN, 0x0);
		/* MISBIAS2 = 1P9V */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON9,
				   0x7 << RG_AUDMICBIAS2VREF,
				   0x2 << RG_AUDMICBIAS2VREF);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* MISBIAS2 = 1P97 */
		regmap_update_bits(cmpnt->regmap, MT6351_AUDENC_ANA_CON9,
				   0x7 << RG_AUDMICBIAS2VREF,
				   0x0 << RG_AUDMICBIAS2VREF);
		break;
	default:
		break;
	}
	return 0;
}

/* DAPM Widgets */
static const struct snd_soc_dapm_widget mt6351_dapm_widgets[] = {
	/* Digital Clock */
	SND_SOC_DAPM_SUPPLY("AUDIO_TOP_AFE_CTL", MT6351_AUDIO_TOP_CON0,
			    AUD_TOP_PDN_AFE_CTL_BIT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AUDIO_TOP_DAC_CTL", MT6351_AUDIO_TOP_CON0,
			    AUD_TOP_PDN_DAC_CTL_BIT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AUDIO_TOP_ADC_CTL", MT6351_AUDIO_TOP_CON0,
			    AUD_TOP_PDN_ADC_CTL_BIT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AUDIO_TOP_PWR_CLK", MT6351_AUDIO_TOP_CON0,
			    AUD_TOP_PWR_CLK_DIS_CTL_BIT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AUDIO_TOP_PDN_RESERVED", MT6351_AUDIO_TOP_CON0,
			    AUD_TOP_PDN_RESERVED_BIT, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("NCP", MT6351_AFE_NCP_CFG0,
			    RG_NCP_ON_BIT, 0,
			    mt_ncp_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_SUPPLY("DL Digital Clock", SND_SOC_NOPM,
			    0, 0, NULL, 0),

	/* Global Supply*/
	SND_SOC_DAPM_SUPPLY("AUDGLB", MT6351_AUDDEC_ANA_CON9,
			    RG_AUDGLB_PWRDN_VA32_BIT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKSQ Audio", MT6351_TOP_CLKSQ,
			    RG_CLKSQ_EN_AUD_BIT, 0,
			    mt_reg_set_clr_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("ZCD13M_CK", MT6351_TOP_CKPDN_CON0,
			    RG_ZCD13M_CK_PDN_BIT, 1,
			    mt_reg_set_clr_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("AUD_CK", MT6351_TOP_CKPDN_CON0,
			    RG_AUD_CK_PDN_BIT, 1,
			    mt_reg_set_clr_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("AUDIF_CK", MT6351_TOP_CKPDN_CON0,
			    RG_AUDIF_CK_PDN_BIT, 1,
			    mt_reg_set_clr_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("AUDNCP_CK", MT6351_TOP_CKPDN_CON0,
			    RG_AUDNCP_CK_PDN_BIT, 1,
			    mt_reg_set_clr_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY("AFE_ON", MT6351_AFE_UL_DL_CON0, RG_AFE_ON_BIT, 0,
			    NULL, 0),

	/* AIF Rx*/
	SND_SOC_DAPM_AIF_IN_E("AIF_RX", "AIF1 Playback", 0,
			      MT6351_AFE_DL_SRC2_CON0_L,
			      RG_DL_2_SRC_ON_TMP_CTL_PRE_BIT, 0,
			      mt_aif_in_event, SND_SOC_DAPM_PRE_PMU),

	/* DL Supply */
	SND_SOC_DAPM_SUPPLY("DL Power Supply", SND_SOC_NOPM,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("NV Regulator", MT6351_AUDDEC_ANA_CON10,
			    RG_NVREG_EN_VAUDP32_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AUD_CLK", MT6351_AUDDEC_ANA_CON9,
			    RG_RSTB_DECODER_VA32_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("IBIST", MT6351_AUDDEC_ANA_CON9,
			    RG_AUDIBIASPWRDN_VAUDP32_BIT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO", MT6351_AUDDEC_ANA_CON9,
			    RG_LCLDO_DEC_EN_VA32_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO_REMOTE_SENSE", MT6351_AUDDEC_ANA_CON9,
			    RG_LCLDO_DEC_REMOTE_SENSE_VA18_BIT, 0, NULL, 0),

	/* DAC */
	SND_SOC_DAPM_MUX("DAC In Mux", SND_SOC_NOPM, 0, 0, &dac_in_mux_control),

	SND_SOC_DAPM_DAC("DACL", NULL, MT6351_AUDDEC_ANA_CON0,
			 RG_AUDDACLPWRUP_VAUDP32_BIT, 0),
	SND_SOC_DAPM_SUPPLY("DACL_BIASGEN", MT6351_AUDDEC_ANA_CON0,
			    RG_AUD_DAC_PWL_UP_VA32_BIT, 0, NULL, 0),

	SND_SOC_DAPM_DAC("DACR", NULL, MT6351_AUDDEC_ANA_CON0,
			 RG_AUDDACRPWRUP_VAUDP32_BIT, 0),
	SND_SOC_DAPM_SUPPLY("DACR_BIASGEN", MT6351_AUDDEC_ANA_CON0,
			    RG_AUD_DAC_PWR_UP_VA32_BIT, 0, NULL, 0),
	/* LOL */
	SND_SOC_DAPM_MUX("LOL Mux", SND_SOC_NOPM, 0, 0, &lo_in_mux_control),

	SND_SOC_DAPM_SUPPLY("LO Stability Enh", MT6351_AUDDEC_ANA_CON3,
			    RG_LOOUTPUTSTBENH_VAUDP32_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LOL Bias Gen", MT6351_AUDDEC_ANA_CON6,
			    RG_ABIDEC_RSVD0_VAUDP32_LOL_BIT, 0, NULL, 0),

	SND_SOC_DAPM_OUT_DRV("LOL Buffer", MT6351_AUDDEC_ANA_CON3,
			     RG_AUDLOLPWRUP_VAUDP32_BIT, 0, NULL, 0),

	/* Headphone */
	SND_SOC_DAPM_MUX("HPL Mux", SND_SOC_NOPM, 0, 0, &hpl_in_mux_control),
	SND_SOC_DAPM_MUX("HPR Mux", SND_SOC_NOPM, 0, 0, &hpr_in_mux_control),

	SND_SOC_DAPM_OUT_DRV_E("HPL Power", MT6351_AUDDEC_ANA_CON0,
			       RG_AUDHPLPWRUP_VAUDP32_BIT, 0, NULL, 0,
			       mt_hp_event,
			       SND_SOC_DAPM_PRE_PMU |
			       SND_SOC_DAPM_PRE_PMD |
			       SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("HPR Power", MT6351_AUDDEC_ANA_CON0,
			       RG_AUDHPRPWRUP_VAUDP32_BIT, 0, NULL, 0,
			       mt_hp_event,
			       SND_SOC_DAPM_PRE_PMU |
			       SND_SOC_DAPM_PRE_PMD |
			       SND_SOC_DAPM_POST_PMD),

	/* Receiver */
	SND_SOC_DAPM_MUX("RCV Mux", SND_SOC_NOPM, 0, 0, &rcv_in_mux_control),

	SND_SOC_DAPM_SUPPLY("RCV Stability Enh", MT6351_AUDDEC_ANA_CON1,
			    RG_HSOUTPUTSTBENH_VAUDP32_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RCV Bias Gen", MT6351_AUDDEC_ANA_CON6,
			    RG_ABIDEC_RSVD0_VAUDP32_HS_BIT, 0, NULL, 0),

	SND_SOC_DAPM_OUT_DRV("RCV Buffer", MT6351_AUDDEC_ANA_CON0,
			     RG_AUDHSPWRUP_VAUDP32_BIT, 0, NULL, 0),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("Receiver"),
	SND_SOC_DAPM_OUTPUT("Headphone L"),
	SND_SOC_DAPM_OUTPUT("Headphone R"),
	SND_SOC_DAPM_OUTPUT("LINEOUT L"),

	/* SGEN */
	SND_SOC_DAPM_SUPPLY("SGEN DL Enable", MT6351_AFE_SGEN_CFG0,
			    SGEN_C_DAC_EN_CTL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SGEN MUTE", MT6351_AFE_SGEN_CFG0,
			    SGEN_C_MUTE_SW_CTL_BIT, 1,
			    mt_sgen_event, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("SGEN DL SRC", MT6351_AFE_DL_SRC2_CON0_L,
			    RG_DL_2_SRC_ON_TMP_CTL_PRE_BIT, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("SGEN DL"),

	/* Uplinks */
	SND_SOC_DAPM_AIF_OUT_E("AIF1TX", "AIF1 Capture", 0,
			       MT6351_AFE_UL_SRC_CON0_L,
			       UL_SRC_ON_TMP_CTL, 0,
			       mt_aif_out_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY_S("VUSB33_LDO", SUPPLY_SUBSEQ_ENABLE,
			      MT6351_LDO_VUSB33_CON0, RG_VUSB33_EN, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("VUSB33_LDO_CTRL", SUPPLY_SUBSEQ_SETTING,
			      MT6351_LDO_VUSB33_CON0, RG_VUSB33_ON_CTRL, 1,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("VA18_LDO", SUPPLY_SUBSEQ_ENABLE,
			      MT6351_LDO_VA18_CON0, RG_VA18_EN, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("VA18_LDO_CTRL", SUPPLY_SUBSEQ_SETTING,
			      MT6351_LDO_VA18_CON0, RG_VA18_ON_CTRL, 1,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADC CLKGEN", SUPPLY_SUBSEQ_ENABLE,
			      MT6351_AUDENC_ANA_CON3, RG_AUDADCCLKRSTB, 0,
			      mt_adc_clkgen_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

	/* Uplinks MUX */
	SND_SOC_DAPM_MUX("AIF Out Mux", SND_SOC_NOPM, 0, 0,
			 &aif_out_mux_control),

	SND_SOC_DAPM_MUX("ADC L Mux", SND_SOC_NOPM, 0, 0,
			 &adc_left_mux_control),
	SND_SOC_DAPM_MUX("ADC R Mux", SND_SOC_NOPM, 0, 0,
			 &adc_right_mux_control),

	SND_SOC_DAPM_ADC("ADC L", NULL,
			 MT6351_AUDENC_ANA_CON0, RG_AUDADCLPWRUP, 0),
	SND_SOC_DAPM_ADC("ADC R", NULL,
			 MT6351_AUDENC_ANA_CON1, RG_AUDADCRPWRUP, 0),

	SND_SOC_DAPM_MUX("PGA L Mux", SND_SOC_NOPM, 0, 0,
			 &pga_left_mux_control),
	SND_SOC_DAPM_MUX("PGA R Mux", SND_SOC_NOPM, 0, 0,
			 &pga_right_mux_control),

	SND_SOC_DAPM_PGA_E("PGA L", MT6351_AUDENC_ANA_CON0, RG_AUDPREAMPLON, 0,
			   NULL, 0,
			   mt_pga_left_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("PGA R", MT6351_AUDENC_ANA_CON1, RG_AUDPREAMPRON, 0,
			   NULL, 0,
			   mt_pga_right_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

	/* main mic mic bias */
	SND_SOC_DAPM_SUPPLY_S("Mic Bias 0", SUPPLY_SUBSEQ_MICBIAS,
			      MT6351_AUDENC_ANA_CON9, RG_AUDPWDBMICBIAS0, 0,
			      mt_mic_bias_0_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	/* ref mic mic bias */
	SND_SOC_DAPM_SUPPLY_S("Mic Bias 2", SUPPLY_SUBSEQ_MICBIAS,
			      MT6351_AUDENC_ANA_CON9, RG_AUDPWDBMICBIAS2, 0,
			      mt_mic_bias_2_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	/* headset mic1/2 mic bias */
	SND_SOC_DAPM_SUPPLY_S("Mic Bias 1", SUPPLY_SUBSEQ_MICBIAS,
			      MT6351_AUDENC_ANA_CON10, RG_AUDPWDBMICBIAS1, 0,
			      mt_mic_bias_1_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("Mic Bias 1 DCC pull high", SUPPLY_SUBSEQ_MICBIAS,
			      MT6351_AUDENC_ANA_CON10,
			      RG_AUDMICBIAS1DCSW1NEN, 0,
			      NULL, 0),

	/* UL input */
	SND_SOC_DAPM_INPUT("AIN0"),
	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),
	SND_SOC_DAPM_INPUT("AIN3"),
};

static const struct snd_soc_dapm_route mt6351_dapm_routes[] = {
	/* Capture */
	{"AIF1TX", NULL, "AIF Out Mux"},
	{"AIF1TX", NULL, "VUSB33_LDO"},
	{"VUSB33_LDO", NULL, "VUSB33_LDO_CTRL"},
	{"AIF1TX", NULL, "VA18_LDO"},
	{"VA18_LDO", NULL, "VA18_LDO_CTRL"},

	{"AIF1TX", NULL, "AUDGLB"},
	{"AIF1TX", NULL, "CLKSQ Audio"},

	{"AIF1TX", NULL, "AFE_ON"},

	{"AIF1TX", NULL, "AUDIO_TOP_AFE_CTL"},
	{"AIF1TX", NULL, "AUDIO_TOP_ADC_CTL"},
	{"AIF1TX", NULL, "AUDIO_TOP_PWR_CLK"},
	{"AIF1TX", NULL, "AUDIO_TOP_PDN_RESERVED"},

	{"AIF Out Mux", "Normal Path", "ADC L"},
	{"AIF Out Mux", "Normal Path", "ADC R"},

	{"ADC L", NULL, "ADC L Mux"},
	{"ADC L", NULL, "AUD_CK"},
	{"ADC L", NULL, "AUDIF_CK"},
	{"ADC L", NULL, "ADC CLKGEN"},
	{"ADC R", NULL, "ADC R Mux"},
	{"ADC R", NULL, "AUD_CK"},
	{"ADC R", NULL, "AUDIF_CK"},
	{"ADC R", NULL, "ADC CLKGEN"},

	{"ADC L Mux", "AIN0", "AIN0"},
	{"ADC L Mux", "Left Preamplifier", "PGA L"},

	{"ADC R Mux", "AIN0", "AIN0"},
	{"ADC R Mux", "Right Preamplifier", "PGA R"},

	{"PGA L", NULL, "PGA L Mux"},
	{"PGA R", NULL, "PGA R Mux"},

	{"PGA L Mux", "AIN0", "AIN0"},
	{"PGA L Mux", "AIN1", "AIN1"},
	{"PGA L Mux", "AIN2", "AIN2"},

	{"PGA R Mux", "AIN0", "AIN0"},
	{"PGA R Mux", "AIN3", "AIN3"},
	{"PGA R Mux", "AIN2", "AIN2"},

	{"AIN0", NULL, "Mic Bias 0"},
	{"AIN2", NULL, "Mic Bias 2"},

	{"AIN1", NULL, "Mic Bias 1"},
	{"AIN1", NULL, "Mic Bias 1 DCC pull high"},

	/* DL Supply */
	{"DL Power Supply", NULL, "AUDGLB"},
	{"DL Power Supply", NULL, "CLKSQ Audio"},
	{"DL Power Supply", NULL, "ZCD13M_CK"},
	{"DL Power Supply", NULL, "AUD_CK"},
	{"DL Power Supply", NULL, "AUDIF_CK"},
	{"DL Power Supply", NULL, "AUDNCP_CK"},

	{"DL Power Supply", NULL, "NV Regulator"},
	{"DL Power Supply", NULL, "AUD_CLK"},
	{"DL Power Supply", NULL, "IBIST"},
	{"DL Power Supply", NULL, "LDO"},
	{"LDO", NULL, "LDO_REMOTE_SENSE"},

	/* DL Digital Supply */
	{"DL Digital Clock", NULL, "AUDIO_TOP_AFE_CTL"},
	{"DL Digital Clock", NULL, "AUDIO_TOP_DAC_CTL"},
	{"DL Digital Clock", NULL, "AUDIO_TOP_PWR_CLK"},
	{"DL Digital Clock", NULL, "AUDIO_TOP_PDN_RESERVED"},
	{"DL Digital Clock", NULL, "NCP"},
	{"DL Digital Clock", NULL, "AFE_ON"},

	{"AIF_RX", NULL, "DL Digital Clock"},

	/* DL Path */
	{"DAC In Mux", "Normal Path", "AIF_RX"},

	{"DAC In Mux", "Sgen", "SGEN DL"},
	{"SGEN DL", NULL, "SGEN DL SRC"},
	{"SGEN DL", NULL, "SGEN MUTE"},
	{"SGEN DL", NULL, "SGEN DL Enable"},
	{"SGEN DL", NULL, "DL Digital Clock"},

	{"DACL", NULL, "DAC In Mux"},
	{"DACL", NULL, "DL Power Supply"},
	{"DACL", NULL, "DACL_BIASGEN"},

	{"DACR", NULL, "DAC In Mux"},
	{"DACR", NULL, "DL Power Supply"},
	{"DACR", NULL, "DACR_BIASGEN"},

	{"LOL Mux", "Playback", "DACL"},

	{"LOL Buffer", NULL, "LOL Mux"},
	{"LOL Buffer", NULL, "LO Stability Enh"},
	{"LOL Buffer", NULL, "LOL Bias Gen"},

	{"LINEOUT L", NULL, "LOL Buffer"},

	/* Headphone Path */
	{"HPL Mux", "Audio Playback", "DACL"},
	{"HPR Mux", "Audio Playback", "DACR"},

	{"HPL Mux", "LoudSPK Playback", "DACL"},
	{"HPR Mux", "LoudSPK Playback", "DACR"},

	{"HPL Power", NULL, "HPL Mux"},
	{"HPR Power", NULL, "HPR Mux"},

	{"Headphone L", NULL, "HPL Power"},
	{"Headphone R", NULL, "HPR Power"},

	/* Receiver Path */
	{"RCV Mux", "Voice Playback", "DACL"},

	{"RCV Buffer", NULL, "RCV Mux"},
	{"RCV Buffer", NULL, "RCV Stability Enh"},
	{"RCV Buffer", NULL, "RCV Bias Gen"},

	{"Receiver", NULL, "RCV Buffer"},
};

static int mt6351_codec_init_reg(struct snd_soc_component *cmpnt)
{
	/* Disable CLKSQ 26MHz */
	regmap_update_bits(cmpnt->regmap, MT6351_TOP_CLKSQ, 0x0001, 0x0);
	/* disable AUDGLB */
	regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON9,
			   0x1000, 0x1000);
	/* Turn off AUDNCP_CLKDIV engine clock,Turn off AUD 26M */
	regmap_update_bits(cmpnt->regmap, MT6351_TOP_CKPDN_CON0_SET,
			   0x3800, 0x3800);
	/* Disable HeadphoneL/HeadphoneR/voice short circuit protection */
	regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON0,
			   0xe000, 0xe000);
	/* [5] = 1, disable LO buffer left short circuit protection */
	regmap_update_bits(cmpnt->regmap, MT6351_AUDDEC_ANA_CON3,
			   0x20, 0x20);
	/* Reverse the PMIC clock*/
	regmap_update_bits(cmpnt->regmap, MT6351_AFE_PMIC_NEWIF_CFG2,
			   0x8000, 0x8000);
	return 0;
}

static int mt6351_codec_probe(struct snd_soc_component *cmpnt)
{
	struct mt6351_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	snd_soc_component_init_regmap(cmpnt, priv->regmap);

	mt6351_codec_init_reg(cmpnt);
	return 0;
}

static const struct snd_soc_component_driver mt6351_soc_component_driver = {
	.probe = mt6351_codec_probe,
	.controls = mt6351_snd_controls,
	.num_controls = ARRAY_SIZE(mt6351_snd_controls),
	.dapm_widgets = mt6351_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6351_dapm_widgets),
	.dapm_routes = mt6351_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6351_dapm_routes),
	.endianness = 1,
};

static int mt6351_codec_driver_probe(struct platform_device *pdev)
{
	struct mt6351_priv *priv;

	priv = devm_kzalloc(&pdev->dev,
			    sizeof(struct mt6351_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, priv);

	priv->dev = &pdev->dev;

	priv->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!priv->regmap)
		return -ENODEV;

	dev_dbg(priv->dev, "%s(), dev name %s\n",
		__func__, dev_name(&pdev->dev));

	return devm_snd_soc_register_component(&pdev->dev,
					       &mt6351_soc_component_driver,
					       mt6351_dai_driver,
					       ARRAY_SIZE(mt6351_dai_driver));
}

static const struct of_device_id mt6351_of_match[] = {
	{.compatible = "mediatek,mt6351-sound",},
	{}
};

static struct platform_driver mt6351_codec_driver = {
	.driver = {
		.name = "mt6351-sound",
		.of_match_table = mt6351_of_match,
	},
	.probe = mt6351_codec_driver_probe,
};

module_platform_driver(mt6351_codec_driver)

/* Module information */
MODULE_DESCRIPTION("MT6351 ALSA SoC codec driver");
MODULE_AUTHOR("KaiChieh Chuang <kaichieh.chuang@mediatek.com>");
MODULE_LICENSE("GPL v2");
