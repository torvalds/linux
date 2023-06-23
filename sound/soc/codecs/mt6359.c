// SPDX-License-Identifier: GPL-2.0
//
// mt6359.c  --  mt6359 ALSA SoC audio codec driver
//
// Copyright (c) 2020 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "mt6359.h"

static void mt6359_set_playback_gpio(struct mt6359_priv *priv)
{
	/* set gpio mosi mode, clk / data mosi */
	regmap_write(priv->regmap, MT6359_GPIO_MODE2_CLR, 0x0ffe);
	regmap_write(priv->regmap, MT6359_GPIO_MODE2_SET, 0x0249);

	/* sync mosi */
	regmap_write(priv->regmap, MT6359_GPIO_MODE3_CLR, 0x6);
	regmap_write(priv->regmap, MT6359_GPIO_MODE3_SET, 0x1);
}

static void mt6359_reset_playback_gpio(struct mt6359_priv *priv)
{
	/* set pad_aud_*_mosi to GPIO mode and dir input
	 * reason:
	 * pad_aud_dat_mosi*, because the pin is used as boot strap
	 * don't clean clk/sync, for mtkaif protocol 2
	 */
	regmap_write(priv->regmap, MT6359_GPIO_MODE2_CLR, 0x0ff8);
	regmap_update_bits(priv->regmap, MT6359_GPIO_DIR0, 0x7 << 9, 0x0);
}

static void mt6359_set_capture_gpio(struct mt6359_priv *priv)
{
	/* set gpio miso mode */
	regmap_write(priv->regmap, MT6359_GPIO_MODE3_CLR, 0x0e00);
	regmap_write(priv->regmap, MT6359_GPIO_MODE3_SET, 0x0200);

	regmap_write(priv->regmap, MT6359_GPIO_MODE4_CLR, 0x003f);
	regmap_write(priv->regmap, MT6359_GPIO_MODE4_SET, 0x0009);
}

static void mt6359_reset_capture_gpio(struct mt6359_priv *priv)
{
	/* set pad_aud_*_miso to GPIO mode and dir input
	 * reason:
	 * pad_aud_clk_miso, because when playback only the miso_clk
	 * will also have 26m, so will have power leak
	 * pad_aud_dat_miso*, because the pin is used as boot strap
	 */
	regmap_write(priv->regmap, MT6359_GPIO_MODE3_CLR, 0x0e00);

	regmap_write(priv->regmap, MT6359_GPIO_MODE4_CLR, 0x003f);

	regmap_update_bits(priv->regmap, MT6359_GPIO_DIR0,
			   0x7 << 13, 0x0);
	regmap_update_bits(priv->regmap, MT6359_GPIO_DIR1,
			   0x3 << 0, 0x0);
}

/* use only when doing mtkaif calibraiton at the boot time */
static void mt6359_set_dcxo(struct mt6359_priv *priv, bool enable)
{
	regmap_update_bits(priv->regmap, MT6359_DCXO_CW12,
			   0x1 << RG_XO_AUDIO_EN_M_SFT,
			   (enable ? 1 : 0) << RG_XO_AUDIO_EN_M_SFT);
}

/* use only when doing mtkaif calibraiton at the boot time */
static void mt6359_set_clksq(struct mt6359_priv *priv, bool enable)
{
	/* Enable/disable CLKSQ 26MHz */
	regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON23,
			   RG_CLKSQ_EN_MASK_SFT,
			   (enable ? 1 : 0) << RG_CLKSQ_EN_SFT);
}

/* use only when doing mtkaif calibraiton at the boot time */
static void mt6359_set_aud_global_bias(struct mt6359_priv *priv, bool enable)
{
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON13,
			   RG_AUDGLB_PWRDN_VA32_MASK_SFT,
			   (enable ? 0 : 1) << RG_AUDGLB_PWRDN_VA32_SFT);
}

/* use only when doing mtkaif calibraiton at the boot time */
static void mt6359_set_topck(struct mt6359_priv *priv, bool enable)
{
	regmap_update_bits(priv->regmap, MT6359_AUD_TOP_CKPDN_CON0,
			   0x0066, enable ? 0x0 : 0x66);
}

static void mt6359_set_decoder_clk(struct mt6359_priv *priv, bool enable)
{
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON13,
			   RG_RSTB_DECODER_VA32_MASK_SFT,
			   (enable ? 1 : 0) << RG_RSTB_DECODER_VA32_SFT);
}

static void mt6359_mtkaif_tx_enable(struct mt6359_priv *priv)
{
	switch (priv->mtkaif_protocol) {
	case MT6359_MTKAIF_PROTOCOL_2_CLK_P2:
		/* MTKAIF TX format setting */
		regmap_update_bits(priv->regmap,
				   MT6359_AFE_ADDA_MTKAIF_CFG0,
				   0xffff, 0x0210);
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap,
				   MT6359_AFE_AUD_PAD_TOP,
				   0xff00, 0x3800);
		regmap_update_bits(priv->regmap,
				   MT6359_AFE_AUD_PAD_TOP,
				   0xff00, 0x3900);
		break;
	case MT6359_MTKAIF_PROTOCOL_2:
		/* MTKAIF TX format setting */
		regmap_update_bits(priv->regmap,
				   MT6359_AFE_ADDA_MTKAIF_CFG0,
				   0xffff, 0x0210);
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap,
				   MT6359_AFE_AUD_PAD_TOP,
				   0xff00, 0x3100);
		break;
	case MT6359_MTKAIF_PROTOCOL_1:
	default:
		/* MTKAIF TX format setting */
		regmap_update_bits(priv->regmap,
				   MT6359_AFE_ADDA_MTKAIF_CFG0,
				   0xffff, 0x0000);
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap,
				   MT6359_AFE_AUD_PAD_TOP,
				   0xff00, 0x3100);
		break;
	}
}

static void mt6359_mtkaif_tx_disable(struct mt6359_priv *priv)
{
	/* disable aud_pad TX fifos */
	regmap_update_bits(priv->regmap, MT6359_AFE_AUD_PAD_TOP,
			   0xff00, 0x3000);
}

void mt6359_set_mtkaif_protocol(struct snd_soc_component *cmpnt,
				int mtkaif_protocol)
{
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	priv->mtkaif_protocol = mtkaif_protocol;
}
EXPORT_SYMBOL_GPL(mt6359_set_mtkaif_protocol);

void mt6359_mtkaif_calibration_enable(struct snd_soc_component *cmpnt)
{
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	mt6359_set_playback_gpio(priv);
	mt6359_set_capture_gpio(priv);
	mt6359_mtkaif_tx_enable(priv);

	mt6359_set_dcxo(priv, true);
	mt6359_set_aud_global_bias(priv, true);
	mt6359_set_clksq(priv, true);
	mt6359_set_topck(priv, true);

	/* set dat_miso_loopback on */
	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_MASK_SFT,
			   1 << RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_SFT);
	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_MASK_SFT,
			   1 << RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_SFT);
	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG1,
			   RG_AUD_PAD_TOP_DAT_MISO3_LOOPBACK_MASK_SFT,
			   1 << RG_AUD_PAD_TOP_DAT_MISO3_LOOPBACK_SFT);
}
EXPORT_SYMBOL_GPL(mt6359_mtkaif_calibration_enable);

void mt6359_mtkaif_calibration_disable(struct snd_soc_component *cmpnt)
{
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	/* set dat_miso_loopback off */
	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_MASK_SFT,
			   0 << RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_SFT);
	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_MASK_SFT,
			   0 << RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_SFT);
	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG1,
			   RG_AUD_PAD_TOP_DAT_MISO3_LOOPBACK_MASK_SFT,
			   0 << RG_AUD_PAD_TOP_DAT_MISO3_LOOPBACK_SFT);

	mt6359_set_topck(priv, false);
	mt6359_set_clksq(priv, false);
	mt6359_set_aud_global_bias(priv, false);
	mt6359_set_dcxo(priv, false);

	mt6359_mtkaif_tx_disable(priv);
	mt6359_reset_playback_gpio(priv);
	mt6359_reset_capture_gpio(priv);
}
EXPORT_SYMBOL_GPL(mt6359_mtkaif_calibration_disable);

void mt6359_set_mtkaif_calibration_phase(struct snd_soc_component *cmpnt,
					 int phase_1, int phase_2, int phase_3)
{
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_PHASE_MODE_MASK_SFT,
			   phase_1 << RG_AUD_PAD_TOP_PHASE_MODE_SFT);
	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_PHASE_MODE2_MASK_SFT,
			   phase_2 << RG_AUD_PAD_TOP_PHASE_MODE2_SFT);
	regmap_update_bits(priv->regmap, MT6359_AUDIO_DIG_CFG1,
			   RG_AUD_PAD_TOP_PHASE_MODE3_MASK_SFT,
			   phase_3 << RG_AUD_PAD_TOP_PHASE_MODE3_SFT);
}
EXPORT_SYMBOL_GPL(mt6359_set_mtkaif_calibration_phase);

static void zcd_disable(struct mt6359_priv *priv)
{
	regmap_write(priv->regmap, MT6359_ZCD_CON0, 0x0000);
}

static void hp_main_output_ramp(struct mt6359_priv *priv, bool up)
{
	int i, stage;
	int target = 7;

	/* Enable/Reduce HPL/R main output stage step by step */
	for (i = 0; i <= target; i++) {
		stage = up ? i : target - i;
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON1,
				   RG_HPLOUTSTGCTRL_VAUDP32_MASK_SFT,
				   stage << RG_HPLOUTSTGCTRL_VAUDP32_SFT);
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON1,
				   RG_HPROUTSTGCTRL_VAUDP32_MASK_SFT,
				   stage << RG_HPROUTSTGCTRL_VAUDP32_SFT);
		usleep_range(600, 650);
	}
}

static void hp_aux_feedback_loop_gain_ramp(struct mt6359_priv *priv, bool up)
{
	int i, stage;
	int target = 0xf;

	/* Enable/Reduce HP aux feedback loop gain step by step */
	for (i = 0; i <= target; i++) {
		stage = up ? i : target - i;
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON9,
				   0xf << 12, stage << 12);
		usleep_range(600, 650);
	}
}

static void hp_in_pair_current(struct mt6359_priv *priv, bool increase)
{
	int i, stage;
	int target = 0x3;

	/* Set input diff pair bias select (Hi-Fi mode) */
	if (priv->hp_hifi_mode) {
		/* Reduce HP aux feedback loop gain step by step */
		for (i = 0; i <= target; i++) {
			stage = increase ? i : target - i;
			regmap_update_bits(priv->regmap,
					   MT6359_AUDDEC_ANA_CON10,
					   0x3 << 3, stage << 3);
			usleep_range(100, 150);
		}
	}
}

static void hp_pull_down(struct mt6359_priv *priv, bool enable)
{
	int i;

	if (enable) {
		for (i = 0x0; i <= 0x7; i++) {
			regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON2,
					   RG_HPPSHORT2VCM_VAUDP32_MASK_SFT,
					   i << RG_HPPSHORT2VCM_VAUDP32_SFT);
			usleep_range(100, 150);
		}
	} else {
		for (i = 0x7; i >= 0x0; i--) {
			regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON2,
					   RG_HPPSHORT2VCM_VAUDP32_MASK_SFT,
					   i << RG_HPPSHORT2VCM_VAUDP32_SFT);
			usleep_range(100, 150);
		}
	}
}

static bool is_valid_hp_pga_idx(int reg_idx)
{
	return (reg_idx >= DL_GAIN_8DB && reg_idx <= DL_GAIN_N_22DB) ||
	       reg_idx == DL_GAIN_N_40DB;
}

static void headset_volume_ramp(struct mt6359_priv *priv,
				int from, int to)
{
	int offset = 0, count = 1, reg_idx;

	if (!is_valid_hp_pga_idx(from) || !is_valid_hp_pga_idx(to)) {
		dev_warn(priv->dev, "%s(), volume index is not valid, from %d, to %d\n",
			 __func__, from, to);
		return;
	}

	dev_dbg(priv->dev, "%s(), from %d, to %d\n", __func__, from, to);

	if (to > from)
		offset = to - from;
	else
		offset = from - to;

	while (offset > 0) {
		if (to > from)
			reg_idx = from + count;
		else
			reg_idx = from - count;

		if (is_valid_hp_pga_idx(reg_idx)) {
			regmap_update_bits(priv->regmap,
					   MT6359_ZCD_CON2,
					   DL_GAIN_REG_MASK,
					   (reg_idx << 7) | reg_idx);
			usleep_range(600, 650);
		}
		offset--;
		count++;
	}
}

static int mt6359_put_volsw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
			(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg;
	int index = ucontrol->value.integer.value[0];
	int ret;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	switch (mc->reg) {
	case MT6359_ZCD_CON2:
		regmap_read(priv->regmap, MT6359_ZCD_CON2, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL] =
			(reg >> RG_AUDHPLGAIN_SFT) & RG_AUDHPLGAIN_MASK;
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTR] =
			(reg >> RG_AUDHPRGAIN_SFT) & RG_AUDHPRGAIN_MASK;
		break;
	case MT6359_ZCD_CON1:
		regmap_read(priv->regmap, MT6359_ZCD_CON1, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_LINEOUTL] =
			(reg >> RG_AUDLOLGAIN_SFT) & RG_AUDLOLGAIN_MASK;
		priv->ana_gain[AUDIO_ANALOG_VOLUME_LINEOUTR] =
			(reg >> RG_AUDLORGAIN_SFT) & RG_AUDLORGAIN_MASK;
		break;
	case MT6359_ZCD_CON3:
		regmap_read(priv->regmap, MT6359_ZCD_CON3, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HSOUTL] =
			(reg >> RG_AUDHSGAIN_SFT) & RG_AUDHSGAIN_MASK;
		break;
	case MT6359_AUDENC_ANA_CON0:
		regmap_read(priv->regmap, MT6359_AUDENC_ANA_CON0, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP1] =
			(reg >> RG_AUDPREAMPLGAIN_SFT) & RG_AUDPREAMPLGAIN_MASK;
		break;
	case MT6359_AUDENC_ANA_CON1:
		regmap_read(priv->regmap, MT6359_AUDENC_ANA_CON1, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP2] =
			(reg >> RG_AUDPREAMPRGAIN_SFT) & RG_AUDPREAMPRGAIN_MASK;
		break;
	case MT6359_AUDENC_ANA_CON2:
		regmap_read(priv->regmap, MT6359_AUDENC_ANA_CON2, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP3] =
			(reg >> RG_AUDPREAMP3GAIN_SFT) & RG_AUDPREAMP3GAIN_MASK;
		break;
	}

	dev_dbg(priv->dev, "%s(), name %s, reg(0x%x) = 0x%x, set index = %x\n",
		__func__, kcontrol->id.name, mc->reg, reg, index);

	return ret;
}

/* MUX */

/* LOL MUX */
static const char * const lo_in_mux_map[] = {
	"Open", "Playback_L_DAC", "Playback", "Test Mode"
};

static SOC_ENUM_SINGLE_DECL(lo_in_mux_map_enum, SND_SOC_NOPM, 0, lo_in_mux_map);

static const struct snd_kcontrol_new lo_in_mux_control =
	SOC_DAPM_ENUM("LO Select", lo_in_mux_map_enum);

/*HP MUX */
static const char * const hp_in_mux_map[] = {
	"Open",
	"LoudSPK Playback",
	"Audio Playback",
	"Test Mode",
	"HP Impedance",
};

static SOC_ENUM_SINGLE_DECL(hp_in_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  hp_in_mux_map);

static const struct snd_kcontrol_new hp_in_mux_control =
	SOC_DAPM_ENUM("HP Select", hp_in_mux_map_enum);

/* RCV MUX */
static const char * const rcv_in_mux_map[] = {
	"Open", "Mute", "Voice Playback", "Test Mode"
};

static SOC_ENUM_SINGLE_DECL(rcv_in_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  rcv_in_mux_map);

static const struct snd_kcontrol_new rcv_in_mux_control =
	SOC_DAPM_ENUM("RCV Select", rcv_in_mux_map_enum);

/* DAC In MUX */
static const char * const dac_in_mux_map[] = {
	"Normal Path", "Sgen"
};

static int dac_in_mux_map_value[] = {
	0x0, 0x1,
};

static SOC_VALUE_ENUM_SINGLE_DECL(dac_in_mux_map_enum,
				  MT6359_AFE_TOP_CON0,
				  DL_SINE_ON_SFT,
				  DL_SINE_ON_MASK,
				  dac_in_mux_map,
				  dac_in_mux_map_value);

static const struct snd_kcontrol_new dac_in_mux_control =
	SOC_DAPM_ENUM("DAC Select", dac_in_mux_map_enum);

/* AIF Out MUX */
static SOC_VALUE_ENUM_SINGLE_DECL(aif_out_mux_map_enum,
				  MT6359_AFE_TOP_CON0,
				  UL_SINE_ON_SFT,
				  UL_SINE_ON_MASK,
				  dac_in_mux_map,
				  dac_in_mux_map_value);

static const struct snd_kcontrol_new aif_out_mux_control =
	SOC_DAPM_ENUM("AIF Out Select", aif_out_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(aif2_out_mux_map_enum,
				  MT6359_AFE_TOP_CON0,
				  ADDA6_UL_SINE_ON_SFT,
				  ADDA6_UL_SINE_ON_MASK,
				  dac_in_mux_map,
				  dac_in_mux_map_value);

static const struct snd_kcontrol_new aif2_out_mux_control =
	SOC_DAPM_ENUM("AIF Out Select", aif2_out_mux_map_enum);

static const char * const ul_src_mux_map[] = {
	"AMIC",
	"DMIC",
};

static int ul_src_mux_map_value[] = {
	UL_SRC_MUX_AMIC,
	UL_SRC_MUX_DMIC,
};

static SOC_VALUE_ENUM_SINGLE_DECL(ul_src_mux_map_enum,
				  MT6359_AFE_UL_SRC_CON0_L,
				  UL_SDM_3_LEVEL_CTL_SFT,
				  UL_SDM_3_LEVEL_CTL_MASK,
				  ul_src_mux_map,
				  ul_src_mux_map_value);

static const struct snd_kcontrol_new ul_src_mux_control =
	SOC_DAPM_ENUM("UL_SRC_MUX Select", ul_src_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(ul2_src_mux_map_enum,
				  MT6359_AFE_ADDA6_UL_SRC_CON0_L,
				  ADDA6_UL_SDM_3_LEVEL_CTL_SFT,
				  ADDA6_UL_SDM_3_LEVEL_CTL_MASK,
				  ul_src_mux_map,
				  ul_src_mux_map_value);

static const struct snd_kcontrol_new ul2_src_mux_control =
	SOC_DAPM_ENUM("UL_SRC_MUX Select", ul2_src_mux_map_enum);

static const char * const miso_mux_map[] = {
	"UL1_CH1",
	"UL1_CH2",
	"UL2_CH1",
	"UL2_CH2",
};

static int miso_mux_map_value[] = {
	MISO_MUX_UL1_CH1,
	MISO_MUX_UL1_CH2,
	MISO_MUX_UL2_CH1,
	MISO_MUX_UL2_CH2,
};

static SOC_VALUE_ENUM_SINGLE_DECL(miso0_mux_map_enum,
				  MT6359_AFE_MTKAIF_MUX_CFG,
				  RG_ADDA_CH1_SEL_SFT,
				  RG_ADDA_CH1_SEL_MASK,
				  miso_mux_map,
				  miso_mux_map_value);

static const struct snd_kcontrol_new miso0_mux_control =
	SOC_DAPM_ENUM("MISO_MUX Select", miso0_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(miso1_mux_map_enum,
				  MT6359_AFE_MTKAIF_MUX_CFG,
				  RG_ADDA_CH2_SEL_SFT,
				  RG_ADDA_CH2_SEL_MASK,
				  miso_mux_map,
				  miso_mux_map_value);

static const struct snd_kcontrol_new miso1_mux_control =
	SOC_DAPM_ENUM("MISO_MUX Select", miso1_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(miso2_mux_map_enum,
				  MT6359_AFE_MTKAIF_MUX_CFG,
				  RG_ADDA6_CH1_SEL_SFT,
				  RG_ADDA6_CH1_SEL_MASK,
				  miso_mux_map,
				  miso_mux_map_value);

static const struct snd_kcontrol_new miso2_mux_control =
	SOC_DAPM_ENUM("MISO_MUX Select", miso2_mux_map_enum);

static const char * const dmic_mux_map[] = {
	"DMIC_DATA0",
	"DMIC_DATA1_L",
	"DMIC_DATA1_L_1",
	"DMIC_DATA1_R",
};

static int dmic_mux_map_value[] = {
	DMIC_MUX_DMIC_DATA0,
	DMIC_MUX_DMIC_DATA1_L,
	DMIC_MUX_DMIC_DATA1_L_1,
	DMIC_MUX_DMIC_DATA1_R,
};

static SOC_VALUE_ENUM_SINGLE_DECL(dmic0_mux_map_enum,
				  MT6359_AFE_MIC_ARRAY_CFG,
				  RG_DMIC_ADC1_SOURCE_SEL_SFT,
				  RG_DMIC_ADC1_SOURCE_SEL_MASK,
				  dmic_mux_map,
				  dmic_mux_map_value);

static const struct snd_kcontrol_new dmic0_mux_control =
	SOC_DAPM_ENUM("DMIC_MUX Select", dmic0_mux_map_enum);

/* ul1 ch2 use RG_DMIC_ADC3_SOURCE_SEL */
static SOC_VALUE_ENUM_SINGLE_DECL(dmic1_mux_map_enum,
				  MT6359_AFE_MIC_ARRAY_CFG,
				  RG_DMIC_ADC3_SOURCE_SEL_SFT,
				  RG_DMIC_ADC3_SOURCE_SEL_MASK,
				  dmic_mux_map,
				  dmic_mux_map_value);

static const struct snd_kcontrol_new dmic1_mux_control =
	SOC_DAPM_ENUM("DMIC_MUX Select", dmic1_mux_map_enum);

/* ul2 ch1 use RG_DMIC_ADC2_SOURCE_SEL */
static SOC_VALUE_ENUM_SINGLE_DECL(dmic2_mux_map_enum,
				  MT6359_AFE_MIC_ARRAY_CFG,
				  RG_DMIC_ADC2_SOURCE_SEL_SFT,
				  RG_DMIC_ADC2_SOURCE_SEL_MASK,
				  dmic_mux_map,
				  dmic_mux_map_value);

static const struct snd_kcontrol_new dmic2_mux_control =
	SOC_DAPM_ENUM("DMIC_MUX Select", dmic2_mux_map_enum);

/* ADC L MUX */
static const char * const adc_left_mux_map[] = {
	"Idle", "AIN0", "Left Preamplifier", "Idle_1"
};

static int adc_mux_map_value[] = {
	ADC_MUX_IDLE,
	ADC_MUX_AIN0,
	ADC_MUX_PREAMPLIFIER,
	ADC_MUX_IDLE1,
};

static SOC_VALUE_ENUM_SINGLE_DECL(adc_left_mux_map_enum,
				  MT6359_AUDENC_ANA_CON0,
				  RG_AUDADCLINPUTSEL_SFT,
				  RG_AUDADCLINPUTSEL_MASK,
				  adc_left_mux_map,
				  adc_mux_map_value);

static const struct snd_kcontrol_new adc_left_mux_control =
	SOC_DAPM_ENUM("ADC L Select", adc_left_mux_map_enum);

/* ADC R MUX */
static const char * const adc_right_mux_map[] = {
	"Idle", "AIN0", "Right Preamplifier", "Idle_1"
};

static SOC_VALUE_ENUM_SINGLE_DECL(adc_right_mux_map_enum,
				  MT6359_AUDENC_ANA_CON1,
				  RG_AUDADCRINPUTSEL_SFT,
				  RG_AUDADCRINPUTSEL_MASK,
				  adc_right_mux_map,
				  adc_mux_map_value);

static const struct snd_kcontrol_new adc_right_mux_control =
	SOC_DAPM_ENUM("ADC R Select", adc_right_mux_map_enum);

/* ADC 3 MUX */
static const char * const adc_3_mux_map[] = {
	"Idle", "AIN0", "Preamplifier", "Idle_1"
};

static SOC_VALUE_ENUM_SINGLE_DECL(adc_3_mux_map_enum,
				  MT6359_AUDENC_ANA_CON2,
				  RG_AUDADC3INPUTSEL_SFT,
				  RG_AUDADC3INPUTSEL_MASK,
				  adc_3_mux_map,
				  adc_mux_map_value);

static const struct snd_kcontrol_new adc_3_mux_control =
	SOC_DAPM_ENUM("ADC 3 Select", adc_3_mux_map_enum);

static const char * const pga_l_mux_map[] = {
	"None", "AIN0", "AIN1"
};

static int pga_l_mux_map_value[] = {
	PGA_L_MUX_NONE,
	PGA_L_MUX_AIN0,
	PGA_L_MUX_AIN1
};

static SOC_VALUE_ENUM_SINGLE_DECL(pga_left_mux_map_enum,
				  MT6359_AUDENC_ANA_CON0,
				  RG_AUDPREAMPLINPUTSEL_SFT,
				  RG_AUDPREAMPLINPUTSEL_MASK,
				  pga_l_mux_map,
				  pga_l_mux_map_value);

static const struct snd_kcontrol_new pga_left_mux_control =
	SOC_DAPM_ENUM("PGA L Select", pga_left_mux_map_enum);

static const char * const pga_r_mux_map[] = {
	"None", "AIN2", "AIN3", "AIN0"
};

static int pga_r_mux_map_value[] = {
	PGA_R_MUX_NONE,
	PGA_R_MUX_AIN2,
	PGA_R_MUX_AIN3,
	PGA_R_MUX_AIN0
};

static SOC_VALUE_ENUM_SINGLE_DECL(pga_right_mux_map_enum,
				  MT6359_AUDENC_ANA_CON1,
				  RG_AUDPREAMPRINPUTSEL_SFT,
				  RG_AUDPREAMPRINPUTSEL_MASK,
				  pga_r_mux_map,
				  pga_r_mux_map_value);

static const struct snd_kcontrol_new pga_right_mux_control =
	SOC_DAPM_ENUM("PGA R Select", pga_right_mux_map_enum);

static const char * const pga_3_mux_map[] = {
	"None", "AIN3", "AIN2"
};

static int pga_3_mux_map_value[] = {
	PGA_3_MUX_NONE,
	PGA_3_MUX_AIN3,
	PGA_3_MUX_AIN2
};

static SOC_VALUE_ENUM_SINGLE_DECL(pga_3_mux_map_enum,
				  MT6359_AUDENC_ANA_CON2,
				  RG_AUDPREAMP3INPUTSEL_SFT,
				  RG_AUDPREAMP3INPUTSEL_MASK,
				  pga_3_mux_map,
				  pga_3_mux_map_value);

static const struct snd_kcontrol_new pga_3_mux_control =
	SOC_DAPM_ENUM("PGA 3 Select", pga_3_mux_map_enum);

static int mt_sgen_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol,
			 int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* sdm audio fifo clock power on */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON2, 0x0006);
		/* scrambler clock on enable */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON0, 0xcba1);
		/* sdm power on */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON2, 0x0003);
		/* sdm fifo enable */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON2, 0x000b);

		regmap_update_bits(priv->regmap, MT6359_AFE_SGEN_CFG0,
				   0xff3f,
				   0x0000);
		regmap_update_bits(priv->regmap, MT6359_AFE_SGEN_CFG1,
				   0xffff,
				   0x0001);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* DL scrambler disabling sequence */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON2, 0x0000);
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON0, 0xcba0);
		break;
	default:
		break;
	}

	return 0;
}

static void mtk_hp_enable(struct mt6359_priv *priv)
{
	if (priv->hp_hifi_mode) {
		/* Set HP DR bias current optimization, 010: 6uA */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON11,
				   DRBIAS_HP_MASK_SFT,
				   DRBIAS_6UA << DRBIAS_HP_SFT);
		/* Set HP & ZCD bias current optimization */
		/* 01: ZCD: 4uA, HP/HS/LO: 5uA */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON12,
				   IBIAS_ZCD_MASK_SFT,
				   IBIAS_ZCD_4UA << IBIAS_ZCD_SFT);
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON12,
				   IBIAS_HP_MASK_SFT,
				   IBIAS_5UA << IBIAS_HP_SFT);
	} else {
		/* Set HP DR bias current optimization, 001: 5uA */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON11,
				   DRBIAS_HP_MASK_SFT,
				   DRBIAS_5UA << DRBIAS_HP_SFT);
		/* Set HP & ZCD bias current optimization */
		/* 00: ZCD: 3uA, HP/HS/LO: 4uA */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON12,
				   IBIAS_ZCD_MASK_SFT,
				   IBIAS_ZCD_3UA << IBIAS_ZCD_SFT);
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON12,
				   IBIAS_HP_MASK_SFT,
				   IBIAS_4UA << IBIAS_HP_SFT);
	}

	/* HP damp circuit enable */
	/* Enable HPRN/HPLN output 4K to VCM */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON10, 0x0087);

	/* HP Feedback Cap select 2'b00: 15pF */
	/* for >= 96KHz sampling rate: 2'b01: 10.5pF */
	if (priv->dl_rate[MT6359_AIF_1] >= 96000)
		regmap_update_bits(priv->regmap,
				   MT6359_AUDDEC_ANA_CON4,
				   RG_AUDHPHFCOMPBUFGAINSEL_VAUDP32_MASK_SFT,
				   0x1 << RG_AUDHPHFCOMPBUFGAINSEL_VAUDP32_SFT);
	else
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON4, 0x0000);

	/* Set HPP/N STB enhance circuits */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON2, 0xf133);

	/* Enable HP aux output stage */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x000c);
	/* Enable HP aux feedback loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x003c);
	/* Enable HP aux CMFB loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0x0c00);
	/* Enable HP driver bias circuits */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON0, 0x30c0);
	/* Enable HP driver core circuits */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON0, 0x30f0);
	/* Short HP main output to HP aux output stage */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x00fc);

	/* Increase HP input pair current to HPM step by step */
	hp_in_pair_current(priv, true);

	/* Enable HP main CMFB loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0x0e00);
	/* Disable HP aux CMFB loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0x0200);

	/* Enable HP main output stage */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x00ff);
	/* Enable HPR/L main output stage step by step */
	hp_main_output_ramp(priv, true);

	/* Reduce HP aux feedback loop gain */
	hp_aux_feedback_loop_gain_ramp(priv, true);
	/* Disable HP aux feedback loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x77cf);

	/* apply volume setting */
	headset_volume_ramp(priv,
			    DL_GAIN_N_22DB,
			    priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL]);

	/* Disable HP aux output stage */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x77c3);
	/* Unshort HP main output to HP aux output stage */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x7703);
	usleep_range(100, 120);

	/* Enable AUD_CLK */
	mt6359_set_decoder_clk(priv, true);

	/* Enable Audio DAC  */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON0, 0x30ff);
	if (priv->hp_hifi_mode) {
		/* Enable low-noise mode of DAC */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0xf201);
	} else {
		/* Disable low-noise mode of DAC */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0xf200);
	}
	usleep_range(100, 120);

	/* Switch HPL MUX to audio DAC */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON0, 0x32ff);
	/* Switch HPR MUX to audio DAC */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON0, 0x3aff);

	/* Disable Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, false);
}

static void mtk_hp_disable(struct mt6359_priv *priv)
{
	/* Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, true);

	/* HPR/HPL mux to open */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON0,
			   0x0f00, 0x0000);

	/* Disable low-noise mode of DAC */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON9,
			   0x0001, 0x0000);

	/* Disable Audio DAC */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON0,
			   0x000f, 0x0000);

	/* Disable AUD_CLK */
	mt6359_set_decoder_clk(priv, false);

	/* Short HP main output to HP aux output stage */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x77c3);
	/* Enable HP aux output stage */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x77cf);

	/* decrease HPL/R gain to normal gain step by step */
	headset_volume_ramp(priv,
			    priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL],
			    DL_GAIN_N_22DB);

	/* Enable HP aux feedback loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x77ff);

	/* Reduce HP aux feedback loop gain */
	hp_aux_feedback_loop_gain_ramp(priv, false);

	/* decrease HPR/L main output stage step by step */
	hp_main_output_ramp(priv, false);

	/* Disable HP main output stage */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON1, 0x3, 0x0);

	/* Enable HP aux CMFB loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0x0e01);

	/* Disable HP main CMFB loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0x0c01);

	/* Decrease HP input pair current to 2'b00 step by step */
	hp_in_pair_current(priv, false);

	/* Unshort HP main output to HP aux output stage */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON1,
			   0x3 << 6, 0x0);

	/* Disable HP driver core circuits */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON0,
			   0x3 << 4, 0x0);

	/* Disable HP driver bias circuits */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON0,
			   0x3 << 6, 0x0);

	/* Disable HP aux CMFB loop */
	regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0x201);

	/* Disable HP aux feedback loop */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON1,
			   0x3 << 4, 0x0);

	/* Disable HP aux output stage */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON1,
			   0x3 << 2, 0x0);
}

static int mt_hp_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol,
		       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);
	int device = DEVICE_HP;

	dev_dbg(priv->dev, "%s(), event 0x%x, dev_counter[DEV_HP] %d, mux %u\n",
		__func__, event, priv->dev_counter[device], mux);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		priv->dev_counter[device]++;
		if (mux == HP_MUX_HP)
			mtk_hp_enable(priv);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		priv->dev_counter[device]--;
		if (mux == HP_MUX_HP)
			mtk_hp_disable(priv);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_rcv_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol,
			int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event 0x%x, mux %u\n",
		__func__, event, dapm_kcontrol_get_value(w->kcontrols[0]));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Disable handset short-circuit protection */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON6, 0x0010);

		/* Set RCV DR bias current optimization, 010: 6uA */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON11,
				   DRBIAS_HS_MASK_SFT,
				   DRBIAS_6UA << DRBIAS_HS_SFT);
		/* Set RCV & ZCD bias current optimization */
		/* 01: ZCD: 4uA, HP/HS/LO: 5uA */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON12,
				   IBIAS_ZCD_MASK_SFT,
				   IBIAS_ZCD_4UA << IBIAS_ZCD_SFT);
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON12,
				   IBIAS_HS_MASK_SFT,
				   IBIAS_5UA << IBIAS_HS_SFT);

		/* Set HS STB enhance circuits */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON6, 0x0090);

		/* Set HS output stage (3'b111 = 8x) */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON10, 0x7000);

		/* Enable HS driver bias circuits */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON6, 0x0092);
		/* Enable HS driver core circuits */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON6, 0x0093);

		/* Set HS gain to normal gain step by step */
		regmap_write(priv->regmap, MT6359_ZCD_CON3,
			     priv->ana_gain[AUDIO_ANALOG_VOLUME_HSOUTL]);

		/* Enable AUD_CLK */
		mt6359_set_decoder_clk(priv, true);

		/* Enable Audio DAC  */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON0, 0x0009);
		/* Enable low-noise mode of DAC */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON9, 0x0001);
		/* Switch HS MUX to audio DAC */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON6, 0x009b);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* HS mux to open */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON6,
				   RG_AUDHSMUXINPUTSEL_VAUDP32_MASK_SFT,
				   RCV_MUX_OPEN);

		/* Disable Audio DAC */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON0,
				   0x000f, 0x0000);

		/* Disable AUD_CLK */
		mt6359_set_decoder_clk(priv, false);

		/* decrease HS gain to minimum gain step by step */
		regmap_write(priv->regmap, MT6359_ZCD_CON3, DL_GAIN_N_40DB);

		/* Disable HS driver core circuits */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON6,
				   RG_AUDHSPWRUP_VAUDP32_MASK_SFT, 0x0);

		/* Disable HS driver bias circuits */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON6,
				   RG_AUDHSPWRUP_IBIAS_VAUDP32_MASK_SFT, 0x0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_lo_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol,
		       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event 0x%x, mux %u\n",
		__func__, event, dapm_kcontrol_get_value(w->kcontrols[0]));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Disable handset short-circuit protection */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON7, 0x0010);

		/* Set LO DR bias current optimization, 010: 6uA */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON11,
				   DRBIAS_LO_MASK_SFT,
				   DRBIAS_6UA << DRBIAS_LO_SFT);
		/* Set LO & ZCD bias current optimization */
		/* 01: ZCD: 4uA, HP/HS/LO: 5uA */
		if (priv->dev_counter[DEVICE_HP] == 0)
			regmap_update_bits(priv->regmap,
					   MT6359_AUDDEC_ANA_CON12,
					   IBIAS_ZCD_MASK_SFT,
					   IBIAS_ZCD_4UA << IBIAS_ZCD_SFT);

		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON12,
				   IBIAS_LO_MASK_SFT,
				   IBIAS_5UA << IBIAS_LO_SFT);

		/* Set LO STB enhance circuits */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON7, 0x0110);

		/* Enable LO driver bias circuits */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON7, 0x0112);
		/* Enable LO driver core circuits */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON7, 0x0113);

		/* Set LO gain to normal gain step by step */
		regmap_write(priv->regmap, MT6359_ZCD_CON1,
			     priv->ana_gain[AUDIO_ANALOG_VOLUME_LINEOUTL]);

		/* Enable AUD_CLK */
		mt6359_set_decoder_clk(priv, true);

		/* Enable Audio DAC (3rd DAC) */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON7, 0x3113);
		/* Enable low-noise mode of DAC */
		if (priv->dev_counter[DEVICE_HP] == 0)
			regmap_write(priv->regmap,
				     MT6359_AUDDEC_ANA_CON9, 0x0001);
		/* Switch LOL MUX to audio 3rd DAC */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON7, 0x311b);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* Switch LOL MUX to open */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON7,
				   RG_AUDLOLMUXINPUTSEL_VAUDP32_MASK_SFT,
				   LO_MUX_OPEN);

		/* Disable Audio DAC */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON0,
				   0x000f, 0x0000);

		/* Disable AUD_CLK */
		mt6359_set_decoder_clk(priv, false);

		/* decrease LO gain to minimum gain step by step */
		regmap_write(priv->regmap, MT6359_ZCD_CON1, DL_GAIN_N_40DB);

		/* Disable LO driver core circuits */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON7,
				   RG_AUDLOLPWRUP_VAUDP32_MASK_SFT, 0x0);

		/* Disable LO driver bias circuits */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON7,
				   RG_AUDLOLPWRUP_IBIAS_VAUDP32_MASK_SFT, 0x0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_adc_clk_gen_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol,
				int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* ADC CLK from CLKGEN (6.5MHz) */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON5,
				   RG_AUDADCCLKRSTB_MASK_SFT,
				   0x1 << RG_AUDADCCLKRSTB_SFT);
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON5,
				   RG_AUDADCCLKSOURCE_MASK_SFT, 0x0);
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON5,
				   RG_AUDADCCLKSEL_MASK_SFT, 0x0);
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON5,
				   RG_AUDADCCLKGENMODE_MASK_SFT,
				   0x1 << RG_AUDADCCLKGENMODE_SFT);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON5,
				   RG_AUDADCCLKSOURCE_MASK_SFT, 0x0);
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON5,
				   RG_AUDADCCLKSEL_MASK_SFT, 0x0);
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON5,
				   RG_AUDADCCLKGENMODE_MASK_SFT, 0x0);
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON5,
				   RG_AUDADCCLKRSTB_MASK_SFT, 0x0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_dcc_clk_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* DCC 50k CLK (from 26M) */
		/* MT6359_AFE_DCCLK_CFG0, bit 3 for dm ck swap */
		regmap_update_bits(priv->regmap, MT6359_AFE_DCCLK_CFG0,
				   0xfff7, 0x2062);
		regmap_update_bits(priv->regmap, MT6359_AFE_DCCLK_CFG0,
				   0xfff7, 0x2060);
		regmap_update_bits(priv->regmap, MT6359_AFE_DCCLK_CFG0,
				   0xfff7, 0x2061);

		regmap_write(priv->regmap, MT6359_AFE_DCCLK_CFG1, 0x0100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, MT6359_AFE_DCCLK_CFG0,
				   0xfff7, 0x2060);
		regmap_update_bits(priv->regmap, MT6359_AFE_DCCLK_CFG0,
				   0xfff7, 0x2062);
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
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mic_type = priv->mux_select[MUX_MIC_TYPE_0];

	dev_dbg(priv->dev, "%s(), event 0x%x, mic_type %d\n",
		__func__, event, mic_type);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (mic_type) {
		case MIC_TYPE_MUX_DCC_ECM_DIFF:
			regmap_update_bits(priv->regmap,
					   MT6359_AUDENC_ANA_CON15,
					   0xff00, 0x7700);
			break;
		case MIC_TYPE_MUX_DCC_ECM_SINGLE:
			regmap_update_bits(priv->regmap,
					   MT6359_AUDENC_ANA_CON15,
					   0xff00, 0x1100);
			break;
		default:
			regmap_update_bits(priv->regmap,
					   MT6359_AUDENC_ANA_CON15,
					   0xff00, 0x0000);
			break;
		}

		/* DMIC enable */
		regmap_write(priv->regmap,
			     MT6359_AUDENC_ANA_CON14, 0x0004);
		/* MISBIAS0 = 1P9V */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON15,
				   RG_AUDMICBIAS0VREF_MASK_SFT,
				   MIC_BIAS_1P9 << RG_AUDMICBIAS0VREF_SFT);
		/* normal power select */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON15,
				   RG_AUDMICBIAS0LOWPEN_MASK_SFT,
				   0 << RG_AUDMICBIAS0LOWPEN_SFT);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Disable MICBIAS0, MISBIAS0 = 1P7V */
		regmap_write(priv->regmap, MT6359_AUDENC_ANA_CON15, 0x0000);
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
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mic_type = priv->mux_select[MUX_MIC_TYPE_1];

	dev_dbg(priv->dev, "%s(), event 0x%x, mic_type %d\n",
		__func__, event, mic_type);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* MISBIAS1 = 2P6V */
		if (mic_type == MIC_TYPE_MUX_DCC_ECM_SINGLE)
			regmap_write(priv->regmap,
				     MT6359_AUDENC_ANA_CON16, 0x0160);
		else
			regmap_write(priv->regmap,
				     MT6359_AUDENC_ANA_CON16, 0x0060);

		/* normal power select */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON16,
				   RG_AUDMICBIAS1LOWPEN_MASK_SFT,
				   0 << RG_AUDMICBIAS1LOWPEN_SFT);
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
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mic_type = priv->mux_select[MUX_MIC_TYPE_2];

	dev_dbg(priv->dev, "%s(), event 0x%x, mic_type %d\n",
		__func__, event, mic_type);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (mic_type) {
		case MIC_TYPE_MUX_DCC_ECM_DIFF:
			regmap_update_bits(priv->regmap,
					   MT6359_AUDENC_ANA_CON17,
					   0xff00, 0x7700);
			break;
		case MIC_TYPE_MUX_DCC_ECM_SINGLE:
			regmap_update_bits(priv->regmap,
					   MT6359_AUDENC_ANA_CON17,
					   0xff00, 0x1100);
			break;
		default:
			regmap_update_bits(priv->regmap,
					   MT6359_AUDENC_ANA_CON17,
					   0xff00, 0x0000);
			break;
		}

		/* MISBIAS2 = 1P9V */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON17,
				   RG_AUDMICBIAS2VREF_MASK_SFT,
				   MIC_BIAS_1P9 << RG_AUDMICBIAS2VREF_SFT);
		/* normal power select */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON17,
				   RG_AUDMICBIAS2LOWPEN_MASK_SFT,
				   0 << RG_AUDMICBIAS2LOWPEN_SFT);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Disable MICBIAS2, MISBIAS0 = 1P7V */
		regmap_write(priv->regmap, MT6359_AUDENC_ANA_CON17, 0x0000);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_mtkaif_tx_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt6359_mtkaif_tx_enable(priv);
		break;
	case SND_SOC_DAPM_POST_PMD:
		mt6359_mtkaif_tx_disable(priv);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_ul_src_dmic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol,
				int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* UL dmic setting */
		if (priv->dmic_one_wire_mode)
			regmap_write(priv->regmap, MT6359_AFE_UL_SRC_CON0_H,
				     0x0400);
		else
			regmap_write(priv->regmap, MT6359_AFE_UL_SRC_CON0_H,
				     0x0080);
		/* default one wire, 3.25M */
		regmap_update_bits(priv->regmap, MT6359_AFE_UL_SRC_CON0_L,
				   0xfffc, 0x0000);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_write(priv->regmap,
			     MT6359_AFE_UL_SRC_CON0_H, 0x0000);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_ul_src_34_dmic_event(struct snd_soc_dapm_widget *w,
				   struct snd_kcontrol *kcontrol,
				   int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* default two wire, 3.25M */
		regmap_write(priv->regmap,
			     MT6359_AFE_ADDA6_L_SRC_CON0_H, 0x0080);
		regmap_update_bits(priv->regmap, MT6359_AFE_ADDA6_UL_SRC_CON0_L,
				   0xfffc, 0x0000);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_write(priv->regmap,
			     MT6359_AFE_ADDA6_L_SRC_CON0_H, 0x0000);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_adc_l_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(100, 120);
		/* Audio L preamplifier DCC precharge off */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON0,
				   RG_AUDPREAMPLDCPRECHARGE_MASK_SFT,
				   0x0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_adc_r_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(100, 120);
		/* Audio R preamplifier DCC precharge off */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON1,
				   RG_AUDPREAMPRDCPRECHARGE_MASK_SFT,
				   0x0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_adc_3_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(100, 120);
		/* Audio R preamplifier DCC precharge off */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON2,
				   RG_AUDPREAMP3DCPRECHARGE_MASK_SFT,
				   0x0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_pga_l_mux_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);

	dev_dbg(priv->dev, "%s(), mux %d\n", __func__, mux);
	priv->mux_select[MUX_PGA_L] = mux >> RG_AUDPREAMPLINPUTSEL_SFT;
	return 0;
}

static int mt_pga_r_mux_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);

	dev_dbg(priv->dev, "%s(), mux %d\n", __func__, mux);
	priv->mux_select[MUX_PGA_R] = mux >> RG_AUDPREAMPRINPUTSEL_SFT;
	return 0;
}

static int mt_pga_3_mux_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);

	dev_dbg(priv->dev, "%s(), mux %d\n", __func__, mux);
	priv->mux_select[MUX_PGA_3] = mux >> RG_AUDPREAMP3INPUTSEL_SFT;
	return 0;
}

static int mt_pga_l_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int mic_gain_l = priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP1];
	unsigned int mux_pga = priv->mux_select[MUX_PGA_L];
	unsigned int mic_type;

	switch (mux_pga) {
	case PGA_L_MUX_AIN0:
		mic_type = priv->mux_select[MUX_MIC_TYPE_0];
		break;
	case PGA_L_MUX_AIN1:
		mic_type = priv->mux_select[MUX_MIC_TYPE_1];
		break;
	default:
		dev_err(priv->dev, "%s(), invalid pga mux %d\n",
			__func__, mux_pga);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (IS_DCC_BASE(mic_type)) {
			/* Audio L preamplifier DCC precharge */
			regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON0,
					   RG_AUDPREAMPLDCPRECHARGE_MASK_SFT,
					   0x1 << RG_AUDPREAMPLDCPRECHARGE_SFT);
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* set mic pga gain */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON0,
				   RG_AUDPREAMPLGAIN_MASK_SFT,
				   mic_gain_l << RG_AUDPREAMPLGAIN_SFT);

		if (IS_DCC_BASE(mic_type)) {
			/* L preamplifier DCCEN */
			regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON0,
					   RG_AUDPREAMPLDCCEN_MASK_SFT,
					   0x1 << RG_AUDPREAMPLDCCEN_SFT);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* L preamplifier DCCEN */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON0,
				   RG_AUDPREAMPLDCCEN_MASK_SFT,
				   0x0 << RG_AUDPREAMPLDCCEN_SFT);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_pga_r_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int mic_gain_r = priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP2];
	unsigned int mux_pga = priv->mux_select[MUX_PGA_R];
	unsigned int mic_type;

	switch (mux_pga) {
	case PGA_R_MUX_AIN0:
		mic_type = priv->mux_select[MUX_MIC_TYPE_0];
		break;
	case PGA_R_MUX_AIN2:
	case PGA_R_MUX_AIN3:
		mic_type = priv->mux_select[MUX_MIC_TYPE_2];
		break;
	default:
		dev_err(priv->dev, "%s(), invalid pga mux %d\n",
			__func__, mux_pga);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (IS_DCC_BASE(mic_type)) {
			/* Audio R preamplifier DCC precharge */
			regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON1,
					   RG_AUDPREAMPRDCPRECHARGE_MASK_SFT,
					   0x1 << RG_AUDPREAMPRDCPRECHARGE_SFT);
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* set mic pga gain */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON1,
				   RG_AUDPREAMPRGAIN_MASK_SFT,
				   mic_gain_r << RG_AUDPREAMPRGAIN_SFT);

		if (IS_DCC_BASE(mic_type)) {
			/* R preamplifier DCCEN */
			regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON1,
					   RG_AUDPREAMPRDCCEN_MASK_SFT,
					   0x1 << RG_AUDPREAMPRDCCEN_SFT);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* R preamplifier DCCEN */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON1,
				   RG_AUDPREAMPRDCCEN_MASK_SFT,
				   0x0 << RG_AUDPREAMPRDCCEN_SFT);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_pga_3_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int mic_gain_3 = priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP3];
	unsigned int mux_pga = priv->mux_select[MUX_PGA_3];
	unsigned int mic_type;

	switch (mux_pga) {
	case PGA_3_MUX_AIN2:
	case PGA_3_MUX_AIN3:
		mic_type = priv->mux_select[MUX_MIC_TYPE_2];
		break;
	default:
		dev_err(priv->dev, "%s(), invalid pga mux %d\n",
			__func__, mux_pga);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (IS_DCC_BASE(mic_type)) {
			/* Audio 3 preamplifier DCC precharge */
			regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON2,
					   RG_AUDPREAMP3DCPRECHARGE_MASK_SFT,
					   0x1 << RG_AUDPREAMP3DCPRECHARGE_SFT);
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* set mic pga gain */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON2,
				   RG_AUDPREAMP3GAIN_MASK_SFT,
				   mic_gain_3 << RG_AUDPREAMP3GAIN_SFT);

		if (IS_DCC_BASE(mic_type)) {
			/* 3 preamplifier DCCEN */
			regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON2,
					   RG_AUDPREAMP3DCCEN_MASK_SFT,
					   0x1 << RG_AUDPREAMP3DCCEN_SFT);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 3 preamplifier DCCEN */
		regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON2,
				   RG_AUDPREAMP3DCCEN_MASK_SFT,
				   0x0 << RG_AUDPREAMP3DCCEN_SFT);
		break;
	default:
		break;
	}

	return 0;
}

/* It is based on hw's control sequenece to add some delay when PMU/PMD */
static int mt_delay_250_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
	case SND_SOC_DAPM_PRE_PMD:
		usleep_range(250, 270);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_delay_100_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
	case SND_SOC_DAPM_PRE_PMD:
		usleep_range(100, 120);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_hp_pull_down_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol,
				 int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		hp_pull_down(priv, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		hp_pull_down(priv, false);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_hp_mute_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Set HPR/HPL gain to -22dB */
		regmap_write(priv->regmap, MT6359_ZCD_CON2, DL_GAIN_N_22DB_REG);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Set HPL/HPR gain to mute */
		regmap_write(priv->regmap, MT6359_ZCD_CON2, DL_GAIN_N_40DB_REG);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_hp_damp_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		/* Disable HP damping circuit & HPN 4K load */
		/* reset CMFB PW level */
		regmap_write(priv->regmap, MT6359_AUDDEC_ANA_CON10, 0x0000);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_esd_resist_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol,
			       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Reduce ESD resistance of AU_REFN */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON2,
				   RG_AUDREFN_DERES_EN_VAUDP32_MASK_SFT,
				   0x1 << RG_AUDREFN_DERES_EN_VAUDP32_SFT);
		usleep_range(250, 270);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Increase ESD resistance of AU_REFN */
		regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON2,
				   RG_AUDREFN_DERES_EN_VAUDP32_MASK_SFT, 0x0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_sdm_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol,
			int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* sdm audio fifo clock power on */
		regmap_update_bits(priv->regmap, MT6359_AFUNC_AUD_CON2,
				   0xfffd, 0x0006);
		/* scrambler clock on enable */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON0, 0xcba1);
		/* sdm power on */
		regmap_update_bits(priv->regmap, MT6359_AFUNC_AUD_CON2,
				   0xfffd, 0x0003);
		/* sdm fifo enable */
		regmap_update_bits(priv->regmap, MT6359_AFUNC_AUD_CON2,
				   0xfffd, 0x000B);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* DL scrambler disabling sequence */
		regmap_update_bits(priv->regmap, MT6359_AFUNC_AUD_CON2,
				   0xfffd, 0x0000);
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON0, 0xcba0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_sdm_3rd_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* sdm audio fifo clock power on */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON11, 0x0006);
		/* scrambler clock on enable */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON9, 0xcba1);
		/* sdm power on */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON11, 0x0003);
		/* sdm fifo enable */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON11, 0x000b);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* DL scrambler disabling sequence */
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON11, 0x0000);
		regmap_write(priv->regmap, MT6359_AFUNC_AUD_CON9, 0xcba0);
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
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_write(priv->regmap, MT6359_AFE_NCP_CFG0, 0xc800);
		break;
	default:
		break;
	}

	return 0;
}

/* DAPM Widgets */
static const struct snd_soc_dapm_widget mt6359_dapm_widgets[] = {
	/* Global Supply*/
	SND_SOC_DAPM_SUPPLY_S("CLK_BUF", SUPPLY_SEQ_CLK_BUF,
			      MT6359_DCXO_CW12,
			      RG_XO_AUDIO_EN_M_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDGLB", SUPPLY_SEQ_AUD_GLB,
			      MT6359_AUDDEC_ANA_CON13,
			      RG_AUDGLB_PWRDN_VA32_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("CLKSQ Audio", SUPPLY_SEQ_CLKSQ,
			      MT6359_AUDENC_ANA_CON23,
			      RG_CLKSQ_EN_SFT, 0, NULL, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY_S("AUDNCP_CK", SUPPLY_SEQ_TOP_CK,
			      MT6359_AUD_TOP_CKPDN_CON0,
			      RG_AUDNCP_CK_PDN_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ZCD13M_CK", SUPPLY_SEQ_TOP_CK,
			      MT6359_AUD_TOP_CKPDN_CON0,
			      RG_ZCD13M_CK_PDN_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUD_CK", SUPPLY_SEQ_TOP_CK_LAST,
			      MT6359_AUD_TOP_CKPDN_CON0,
			      RG_AUD_CK_PDN_SFT, 1, mt_delay_250_event,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY_S("AUDIF_CK", SUPPLY_SEQ_TOP_CK,
			      MT6359_AUD_TOP_CKPDN_CON0,
			      RG_AUDIF_CK_PDN_SFT, 1, NULL, 0),
	SND_SOC_DAPM_REGULATOR_SUPPLY("vaud18", 0, 0),

	/* Digital Clock */
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_AFE_CTL", SUPPLY_SEQ_AUD_TOP_LAST,
			      MT6359_AUDIO_TOP_CON0,
			      PDN_AFE_CTL_SFT, 1,
			      mt_delay_250_event,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_DAC_CTL", SUPPLY_SEQ_AUD_TOP,
			      MT6359_AUDIO_TOP_CON0,
			      PDN_DAC_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_ADC_CTL", SUPPLY_SEQ_AUD_TOP,
			      MT6359_AUDIO_TOP_CON0,
			      PDN_ADC_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_ADDA6_ADC_CTL", SUPPLY_SEQ_AUD_TOP,
			      MT6359_AUDIO_TOP_CON0,
			      PDN_ADDA6_ADC_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_I2S_DL", SUPPLY_SEQ_AUD_TOP,
			      MT6359_AUDIO_TOP_CON0,
			      PDN_I2S_DL_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PWR_CLK", SUPPLY_SEQ_AUD_TOP,
			      MT6359_AUDIO_TOP_CON0,
			      PWR_CLK_DIS_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PDN_AFE_TESTMODEL", SUPPLY_SEQ_AUD_TOP,
			      MT6359_AUDIO_TOP_CON0,
			      PDN_AFE_TESTMODEL_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PDN_RESERVED", SUPPLY_SEQ_AUD_TOP,
			      MT6359_AUDIO_TOP_CON0,
			      PDN_RESERVED_SFT, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("SDM", SUPPLY_SEQ_DL_SDM,
			      SND_SOC_NOPM, 0, 0,
			      mt_sdm_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("SDM_3RD", SUPPLY_SEQ_DL_SDM,
			      SND_SOC_NOPM, 0, 0,
			      mt_sdm_3rd_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* ch123 share SDM FIFO CLK */
	SND_SOC_DAPM_SUPPLY_S("SDM_FIFO_CLK", SUPPLY_SEQ_DL_SDM_FIFO_CLK,
			      MT6359_AFUNC_AUD_CON2,
			      CCI_AFIFO_CLK_PWDB_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("NCP", SUPPLY_SEQ_DL_NCP,
			      MT6359_AFE_NCP_CFG0,
			      RG_NCP_ON_SFT, 0,
			      mt_ncp_event,
			      SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_SUPPLY("DL Digital Clock", SND_SOC_NOPM,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DL Digital Clock CH_1_2", SND_SOC_NOPM,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DL Digital Clock CH_3", SND_SOC_NOPM,
			    0, 0, NULL, 0),

	/* AFE ON */
	SND_SOC_DAPM_SUPPLY_S("AFE_ON", SUPPLY_SEQ_AFE,
			      MT6359_AFE_UL_DL_CON0, AFE_ON_SFT, 0,
			      NULL, 0),

	/* AIF Rx*/
	SND_SOC_DAPM_AIF_IN("AIF_RX", "AIF1 Playback", 0,
			    SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("AIF2_RX", "AIF2 Playback", 0,
			    SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY_S("AFE_DL_SRC", SUPPLY_SEQ_DL_SRC,
			      MT6359_AFE_DL_SRC2_CON0_L,
			      DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0,
			      NULL, 0),

	/* DL Supply */
	SND_SOC_DAPM_SUPPLY("DL Power Supply", SND_SOC_NOPM,
			    0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ESD_RESIST", SUPPLY_SEQ_DL_ESD_RESIST,
			      SND_SOC_NOPM,
			      0, 0,
			      mt_esd_resist_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("LDO", SUPPLY_SEQ_DL_LDO,
			      MT6359_AUDDEC_ANA_CON14,
			      RG_LCLDO_DEC_EN_VA32_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("LDO_REMOTE", SUPPLY_SEQ_DL_LDO_REMOTE_SENSE,
			      MT6359_AUDDEC_ANA_CON14,
			      RG_LCLDO_DEC_REMOTE_SENSE_VA18_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("NV_REGULATOR", SUPPLY_SEQ_DL_NV,
			      MT6359_AUDDEC_ANA_CON14,
			      RG_NVREG_EN_VAUDP32_SFT, 0,
			      mt_delay_100_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY_S("IBIST", SUPPLY_SEQ_DL_IBIST,
			      MT6359_AUDDEC_ANA_CON12,
			      RG_AUDIBIASPWRDN_VAUDP32_SFT, 1,
			      NULL, 0),

	/* DAC */
	SND_SOC_DAPM_MUX("DAC In Mux", SND_SOC_NOPM, 0, 0, &dac_in_mux_control),

	SND_SOC_DAPM_DAC("DACL", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DACR", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DAC_3RD", NULL, SND_SOC_NOPM, 0, 0),

	/* Headphone */
	SND_SOC_DAPM_MUX_E("HP Mux", SND_SOC_NOPM, 0, 0,
			   &hp_in_mux_control,
			   mt_hp_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY("HP_Supply", SND_SOC_NOPM,
			    0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("HP_PULL_DOWN", SUPPLY_SEQ_HP_PULL_DOWN,
			      SND_SOC_NOPM,
			      0, 0,
			      mt_hp_pull_down_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("HP_MUTE", SUPPLY_SEQ_HP_MUTE,
			      SND_SOC_NOPM,
			      0, 0,
			      mt_hp_mute_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("HP_DAMP", SUPPLY_SEQ_HP_DAMPING_OFF_RESET_CMFB,
			      SND_SOC_NOPM,
			      0, 0,
			      mt_hp_damp_event,
			      SND_SOC_DAPM_POST_PMD),

	/* Receiver */
	SND_SOC_DAPM_MUX_E("RCV Mux", SND_SOC_NOPM, 0, 0,
			   &rcv_in_mux_control,
			   mt_rcv_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	/* LOL */
	SND_SOC_DAPM_MUX_E("LOL Mux", SND_SOC_NOPM, 0, 0,
			   &lo_in_mux_control,
			   mt_lo_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("Receiver"),
	SND_SOC_DAPM_OUTPUT("Headphone L"),
	SND_SOC_DAPM_OUTPUT("Headphone R"),
	SND_SOC_DAPM_OUTPUT("Headphone L Ext Spk Amp"),
	SND_SOC_DAPM_OUTPUT("Headphone R Ext Spk Amp"),
	SND_SOC_DAPM_OUTPUT("LINEOUT L"),

	/* SGEN */
	SND_SOC_DAPM_SUPPLY("SGEN DL Enable", MT6359_AFE_SGEN_CFG0,
			    SGEN_DAC_EN_CTL_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SGEN MUTE", MT6359_AFE_SGEN_CFG0,
			    SGEN_MUTE_SW_CTL_SFT, 1,
			    mt_sgen_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("SGEN DL SRC", MT6359_AFE_DL_SRC2_CON0_L,
			    DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("SGEN DL"),

	/* Uplinks */
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0,
			     SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0,
			     SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY_S("ADC_CLKGEN", SUPPLY_SEQ_ADC_CLKGEN,
			      SND_SOC_NOPM, 0, 0,
			      mt_adc_clk_gen_event,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY_S("DCC_CLK", SUPPLY_SEQ_DCC_CLK,
			      SND_SOC_NOPM, 0, 0,
			      mt_dcc_clk_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* Uplinks MUX */
	SND_SOC_DAPM_MUX("AIF Out Mux", SND_SOC_NOPM, 0, 0,
			 &aif_out_mux_control),

	SND_SOC_DAPM_MUX("AIF2 Out Mux", SND_SOC_NOPM, 0, 0,
			 &aif2_out_mux_control),

	SND_SOC_DAPM_SUPPLY("AIFTX_Supply", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("MTKAIF_TX", SUPPLY_SEQ_UL_MTKAIF,
			      SND_SOC_NOPM, 0, 0,
			      mt_mtkaif_tx_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("UL_SRC", SUPPLY_SEQ_UL_SRC,
			      MT6359_AFE_UL_SRC_CON0_L,
			      UL_SRC_ON_TMP_CTL_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("UL_SRC_DMIC", SUPPLY_SEQ_UL_SRC_DMIC,
			      SND_SOC_NOPM, 0, 0,
			      mt_ul_src_dmic_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("UL_SRC_34", SUPPLY_SEQ_UL_SRC,
			      MT6359_AFE_ADDA6_UL_SRC_CON0_L,
			      ADDA6_UL_SRC_ON_TMP_CTL_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("UL_SRC_34_DMIC", SUPPLY_SEQ_UL_SRC_DMIC,
			      SND_SOC_NOPM, 0, 0,
			      mt_ul_src_34_dmic_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("MISO0_MUX", SND_SOC_NOPM, 0, 0, &miso0_mux_control),
	SND_SOC_DAPM_MUX("MISO1_MUX", SND_SOC_NOPM, 0, 0, &miso1_mux_control),
	SND_SOC_DAPM_MUX("MISO2_MUX", SND_SOC_NOPM, 0, 0, &miso2_mux_control),

	SND_SOC_DAPM_MUX("UL_SRC_MUX", SND_SOC_NOPM, 0, 0,
			 &ul_src_mux_control),
	SND_SOC_DAPM_MUX("UL2_SRC_MUX", SND_SOC_NOPM, 0, 0,
			 &ul2_src_mux_control),

	SND_SOC_DAPM_MUX("DMIC0_MUX", SND_SOC_NOPM, 0, 0, &dmic0_mux_control),
	SND_SOC_DAPM_MUX("DMIC1_MUX", SND_SOC_NOPM, 0, 0, &dmic1_mux_control),
	SND_SOC_DAPM_MUX("DMIC2_MUX", SND_SOC_NOPM, 0, 0, &dmic2_mux_control),

	SND_SOC_DAPM_MUX_E("ADC_L_Mux", SND_SOC_NOPM, 0, 0,
			   &adc_left_mux_control, NULL, 0),
	SND_SOC_DAPM_MUX_E("ADC_R_Mux", SND_SOC_NOPM, 0, 0,
			   &adc_right_mux_control, NULL, 0),
	SND_SOC_DAPM_MUX_E("ADC_3_Mux", SND_SOC_NOPM, 0, 0,
			   &adc_3_mux_control, NULL, 0),

	SND_SOC_DAPM_ADC("ADC_L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC_R", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC_3", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY_S("ADC_L_EN", SUPPLY_SEQ_UL_ADC,
			      MT6359_AUDENC_ANA_CON0,
			      RG_AUDADCLPWRUP_SFT, 0,
			      mt_adc_l_event,
			      SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY_S("ADC_R_EN", SUPPLY_SEQ_UL_ADC,
			      MT6359_AUDENC_ANA_CON1,
			      RG_AUDADCRPWRUP_SFT, 0,
			      mt_adc_r_event,
			      SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY_S("ADC_3_EN", SUPPLY_SEQ_UL_ADC,
			      MT6359_AUDENC_ANA_CON2,
			      RG_AUDADC3PWRUP_SFT, 0,
			      mt_adc_3_event,
			      SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_MUX_E("PGA_L_Mux", SND_SOC_NOPM, 0, 0,
			   &pga_left_mux_control,
			   mt_pga_l_mux_event,
			   SND_SOC_DAPM_WILL_PMU),
	SND_SOC_DAPM_MUX_E("PGA_R_Mux", SND_SOC_NOPM, 0, 0,
			   &pga_right_mux_control,
			   mt_pga_r_mux_event,
			   SND_SOC_DAPM_WILL_PMU),
	SND_SOC_DAPM_MUX_E("PGA_3_Mux", SND_SOC_NOPM, 0, 0,
			   &pga_3_mux_control,
			   mt_pga_3_mux_event,
			   SND_SOC_DAPM_WILL_PMU),

	SND_SOC_DAPM_PGA("PGA_L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PGA_R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PGA_3", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("PGA_L_EN", SUPPLY_SEQ_UL_PGA,
			      MT6359_AUDENC_ANA_CON0,
			      RG_AUDPREAMPLON_SFT, 0,
			      mt_pga_l_event,
			      SND_SOC_DAPM_PRE_PMU |
			      SND_SOC_DAPM_POST_PMU |
			      SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("PGA_R_EN", SUPPLY_SEQ_UL_PGA,
			      MT6359_AUDENC_ANA_CON1,
			      RG_AUDPREAMPRON_SFT, 0,
			      mt_pga_r_event,
			      SND_SOC_DAPM_PRE_PMU |
			      SND_SOC_DAPM_POST_PMU |
			      SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("PGA_3_EN", SUPPLY_SEQ_UL_PGA,
			      MT6359_AUDENC_ANA_CON2,
			      RG_AUDPREAMP3ON_SFT, 0,
			      mt_pga_3_event,
			      SND_SOC_DAPM_PRE_PMU |
			      SND_SOC_DAPM_POST_PMU |
			      SND_SOC_DAPM_POST_PMD),

	/* UL input */
	SND_SOC_DAPM_INPUT("AIN0"),
	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),
	SND_SOC_DAPM_INPUT("AIN3"),

	SND_SOC_DAPM_INPUT("AIN0_DMIC"),
	SND_SOC_DAPM_INPUT("AIN2_DMIC"),
	SND_SOC_DAPM_INPUT("AIN3_DMIC"),

	/* mic bias */
	SND_SOC_DAPM_SUPPLY_S("MIC_BIAS_0", SUPPLY_SEQ_MIC_BIAS,
			      MT6359_AUDENC_ANA_CON15,
			      RG_AUDPWDBMICBIAS0_SFT, 0,
			      mt_mic_bias_0_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("MIC_BIAS_1", SUPPLY_SEQ_MIC_BIAS,
			      MT6359_AUDENC_ANA_CON16,
			      RG_AUDPWDBMICBIAS1_SFT, 0,
			      mt_mic_bias_1_event,
			      SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY_S("MIC_BIAS_2", SUPPLY_SEQ_MIC_BIAS,
			      MT6359_AUDENC_ANA_CON17,
			      RG_AUDPWDBMICBIAS2_SFT, 0,
			      mt_mic_bias_2_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* dmic */
	SND_SOC_DAPM_SUPPLY_S("DMIC_0", SUPPLY_SEQ_DMIC,
			      MT6359_AUDENC_ANA_CON13,
			      RG_AUDDIGMICEN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC_1", SUPPLY_SEQ_DMIC,
			      MT6359_AUDENC_ANA_CON14,
			      RG_AUDDIGMIC1EN_SFT, 0,
			      NULL, 0),
};

static int mt_dcc_clk_connect(struct snd_soc_dapm_widget *source,
			      struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	if (IS_DCC_BASE(priv->mux_select[MUX_MIC_TYPE_0]) ||
	    IS_DCC_BASE(priv->mux_select[MUX_MIC_TYPE_1]) ||
	    IS_DCC_BASE(priv->mux_select[MUX_MIC_TYPE_2]))
		return 1;
	else
		return 0;
}

static const struct snd_soc_dapm_route mt6359_dapm_routes[] = {
	/* Capture */
	{"AIFTX_Supply", NULL, "CLK_BUF"},
	{"AIFTX_Supply", NULL, "vaud18"},
	{"AIFTX_Supply", NULL, "AUDGLB"},
	{"AIFTX_Supply", NULL, "CLKSQ Audio"},
	{"AIFTX_Supply", NULL, "AUD_CK"},
	{"AIFTX_Supply", NULL, "AUDIF_CK"},
	{"AIFTX_Supply", NULL, "AUDIO_TOP_AFE_CTL"},
	{"AIFTX_Supply", NULL, "AUDIO_TOP_PWR_CLK"},
	{"AIFTX_Supply", NULL, "AUDIO_TOP_PDN_RESERVED"},
	{"AIFTX_Supply", NULL, "AUDIO_TOP_I2S_DL"},
	/*
	 * *_ADC_CTL should enable only if UL_SRC in use,
	 * but dm ck may be needed even UL_SRC_x not in use
	 */
	{"AIFTX_Supply", NULL, "AUDIO_TOP_ADC_CTL"},
	{"AIFTX_Supply", NULL, "AUDIO_TOP_ADDA6_ADC_CTL"},
	{"AIFTX_Supply", NULL, "AFE_ON"},

	/* ul ch 12 */
	{"AIF1TX", NULL, "AIF Out Mux"},
	{"AIF1TX", NULL, "AIFTX_Supply"},
	{"AIF1TX", NULL, "MTKAIF_TX"},

	{"AIF2TX", NULL, "AIF2 Out Mux"},
	{"AIF2TX", NULL, "AIFTX_Supply"},
	{"AIF2TX", NULL, "MTKAIF_TX"},

	{"AIF Out Mux", "Normal Path", "MISO0_MUX"},
	{"AIF Out Mux", "Normal Path", "MISO1_MUX"},
	{"AIF2 Out Mux", "Normal Path", "MISO2_MUX"},

	{"MISO0_MUX", "UL1_CH1", "UL_SRC_MUX"},
	{"MISO0_MUX", "UL1_CH2", "UL_SRC_MUX"},
	{"MISO0_MUX", "UL2_CH1", "UL2_SRC_MUX"},
	{"MISO0_MUX", "UL2_CH2", "UL2_SRC_MUX"},

	{"MISO1_MUX", "UL1_CH1", "UL_SRC_MUX"},
	{"MISO1_MUX", "UL1_CH2", "UL_SRC_MUX"},
	{"MISO1_MUX", "UL2_CH1", "UL2_SRC_MUX"},
	{"MISO1_MUX", "UL2_CH2", "UL2_SRC_MUX"},

	{"MISO2_MUX", "UL1_CH1", "UL_SRC_MUX"},
	{"MISO2_MUX", "UL1_CH2", "UL_SRC_MUX"},
	{"MISO2_MUX", "UL2_CH1", "UL2_SRC_MUX"},
	{"MISO2_MUX", "UL2_CH2", "UL2_SRC_MUX"},

	{"UL_SRC_MUX", "AMIC", "ADC_L"},
	{"UL_SRC_MUX", "AMIC", "ADC_R"},
	{"UL_SRC_MUX", "DMIC", "DMIC0_MUX"},
	{"UL_SRC_MUX", "DMIC", "DMIC1_MUX"},
	{"UL_SRC_MUX", NULL, "UL_SRC"},

	{"UL2_SRC_MUX", "AMIC", "ADC_3"},
	{"UL2_SRC_MUX", "DMIC", "DMIC2_MUX"},
	{"UL2_SRC_MUX", NULL, "UL_SRC_34"},

	{"DMIC0_MUX", "DMIC_DATA0", "AIN0_DMIC"},
	{"DMIC0_MUX", "DMIC_DATA1_L", "AIN2_DMIC"},
	{"DMIC0_MUX", "DMIC_DATA1_L_1", "AIN2_DMIC"},
	{"DMIC0_MUX", "DMIC_DATA1_R", "AIN3_DMIC"},
	{"DMIC1_MUX", "DMIC_DATA0", "AIN0_DMIC"},
	{"DMIC1_MUX", "DMIC_DATA1_L", "AIN2_DMIC"},
	{"DMIC1_MUX", "DMIC_DATA1_L_1", "AIN2_DMIC"},
	{"DMIC1_MUX", "DMIC_DATA1_R", "AIN3_DMIC"},
	{"DMIC2_MUX", "DMIC_DATA0", "AIN0_DMIC"},
	{"DMIC2_MUX", "DMIC_DATA1_L", "AIN2_DMIC"},
	{"DMIC2_MUX", "DMIC_DATA1_L_1", "AIN2_DMIC"},
	{"DMIC2_MUX", "DMIC_DATA1_R", "AIN3_DMIC"},

	{"DMIC0_MUX", NULL, "UL_SRC_DMIC"},
	{"DMIC1_MUX", NULL, "UL_SRC_DMIC"},
	{"DMIC2_MUX", NULL, "UL_SRC_34_DMIC"},

	{"AIN0_DMIC", NULL, "DMIC_0"},
	{"AIN2_DMIC", NULL, "DMIC_1"},
	{"AIN3_DMIC", NULL, "DMIC_1"},
	{"AIN0_DMIC", NULL, "MIC_BIAS_0"},
	{"AIN2_DMIC", NULL, "MIC_BIAS_2"},
	{"AIN3_DMIC", NULL, "MIC_BIAS_2"},

	/* adc */
	{"ADC_L", NULL, "ADC_L_Mux"},
	{"ADC_L", NULL, "ADC_CLKGEN"},
	{"ADC_L", NULL, "ADC_L_EN"},
	{"ADC_R", NULL, "ADC_R_Mux"},
	{"ADC_R", NULL, "ADC_CLKGEN"},
	{"ADC_R", NULL, "ADC_R_EN"},
	/*
	 * amic fifo ch1/2 clk from ADC_L,
	 * enable ADC_L even use ADC_R only
	 */
	{"ADC_R", NULL, "ADC_L_EN"},
	{"ADC_3", NULL, "ADC_3_Mux"},
	{"ADC_3", NULL, "ADC_CLKGEN"},
	{"ADC_3", NULL, "ADC_3_EN"},

	{"ADC_L_Mux", "Left Preamplifier", "PGA_L"},
	{"ADC_R_Mux", "Right Preamplifier", "PGA_R"},
	{"ADC_3_Mux", "Preamplifier", "PGA_3"},

	{"PGA_L", NULL, "PGA_L_Mux"},
	{"PGA_L", NULL, "PGA_L_EN"},
	{"PGA_R", NULL, "PGA_R_Mux"},
	{"PGA_R", NULL, "PGA_R_EN"},
	{"PGA_3", NULL, "PGA_3_Mux"},
	{"PGA_3", NULL, "PGA_3_EN"},

	{"PGA_L", NULL, "DCC_CLK", mt_dcc_clk_connect},
	{"PGA_R", NULL, "DCC_CLK", mt_dcc_clk_connect},
	{"PGA_3", NULL, "DCC_CLK", mt_dcc_clk_connect},

	{"PGA_L_Mux", "AIN0", "AIN0"},
	{"PGA_L_Mux", "AIN1", "AIN1"},

	{"PGA_R_Mux", "AIN0", "AIN0"},
	{"PGA_R_Mux", "AIN2", "AIN2"},
	{"PGA_R_Mux", "AIN3", "AIN3"},

	{"PGA_3_Mux", "AIN2", "AIN2"},
	{"PGA_3_Mux", "AIN3", "AIN3"},

	{"AIN0", NULL, "MIC_BIAS_0"},
	{"AIN1", NULL, "MIC_BIAS_1"},
	{"AIN2", NULL, "MIC_BIAS_0"},
	{"AIN2", NULL, "MIC_BIAS_2"},
	{"AIN3", NULL, "MIC_BIAS_2"},

	/* DL Supply */
	{"DL Power Supply", NULL, "CLK_BUF"},
	{"DL Power Supply", NULL, "vaud18"},
	{"DL Power Supply", NULL, "AUDGLB"},
	{"DL Power Supply", NULL, "CLKSQ Audio"},
	{"DL Power Supply", NULL, "AUDNCP_CK"},
	{"DL Power Supply", NULL, "ZCD13M_CK"},
	{"DL Power Supply", NULL, "AUD_CK"},
	{"DL Power Supply", NULL, "AUDIF_CK"},
	{"DL Power Supply", NULL, "ESD_RESIST"},
	{"DL Power Supply", NULL, "LDO"},
	{"DL Power Supply", NULL, "LDO_REMOTE"},
	{"DL Power Supply", NULL, "NV_REGULATOR"},
	{"DL Power Supply", NULL, "IBIST"},

	/* DL Digital Supply */
	{"DL Digital Clock", NULL, "AUDIO_TOP_AFE_CTL"},
	{"DL Digital Clock", NULL, "AUDIO_TOP_DAC_CTL"},
	{"DL Digital Clock", NULL, "AUDIO_TOP_PWR_CLK"},
	{"DL Digital Clock", NULL, "AUDIO_TOP_PDN_RESERVED"},
	{"DL Digital Clock", NULL, "SDM_FIFO_CLK"},
	{"DL Digital Clock", NULL, "NCP"},
	{"DL Digital Clock", NULL, "AFE_ON"},
	{"DL Digital Clock", NULL, "AFE_DL_SRC"},

	{"DL Digital Clock CH_1_2", NULL, "DL Digital Clock"},
	{"DL Digital Clock CH_1_2", NULL, "SDM"},

	{"DL Digital Clock CH_3", NULL, "DL Digital Clock"},
	{"DL Digital Clock CH_3", NULL, "SDM_3RD"},

	{"AIF_RX", NULL, "DL Digital Clock CH_1_2"},

	{"AIF2_RX", NULL, "DL Digital Clock CH_3"},

	/* DL Path */
	{"DAC In Mux", "Normal Path", "AIF_RX"},
	{"DAC In Mux", "Sgen", "SGEN DL"},
	{"SGEN DL", NULL, "SGEN DL SRC"},
	{"SGEN DL", NULL, "SGEN MUTE"},
	{"SGEN DL", NULL, "SGEN DL Enable"},
	{"SGEN DL", NULL, "DL Digital Clock CH_1_2"},
	{"SGEN DL", NULL, "DL Digital Clock CH_3"},
	{"SGEN DL", NULL, "AUDIO_TOP_PDN_AFE_TESTMODEL"},

	{"DACL", NULL, "DAC In Mux"},
	{"DACL", NULL, "DL Power Supply"},

	{"DACR", NULL, "DAC In Mux"},
	{"DACR", NULL, "DL Power Supply"},

	/* DAC 3RD */
	{"DAC In Mux", "Normal Path", "AIF2_RX"},
	{"DAC_3RD", NULL, "DAC In Mux"},
	{"DAC_3RD", NULL, "DL Power Supply"},

	/* Lineout Path */
	{"LOL Mux", "Playback", "DAC_3RD"},
	{"LINEOUT L", NULL, "LOL Mux"},

	/* Headphone Path */
	{"HP_Supply", NULL, "HP_PULL_DOWN"},
	{"HP_Supply", NULL, "HP_MUTE"},
	{"HP_Supply", NULL, "HP_DAMP"},
	{"HP Mux", NULL, "HP_Supply"},

	{"HP Mux", "Audio Playback", "DACL"},
	{"HP Mux", "Audio Playback", "DACR"},
	{"HP Mux", "HP Impedance", "DACL"},
	{"HP Mux", "HP Impedance", "DACR"},
	{"HP Mux", "LoudSPK Playback", "DACL"},
	{"HP Mux", "LoudSPK Playback", "DACR"},

	{"Headphone L", NULL, "HP Mux"},
	{"Headphone R", NULL, "HP Mux"},
	{"Headphone L Ext Spk Amp", NULL, "HP Mux"},
	{"Headphone R Ext Spk Amp", NULL, "HP Mux"},

	/* Receiver Path */
	{"RCV Mux", "Voice Playback", "DACL"},
	{"Receiver", NULL, "RCV Mux"},
};

static int mt6359_codec_dai_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *cmpnt = dai->component;
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int rate = params_rate(params);
	int id = dai->id;

	dev_dbg(priv->dev, "%s(), id %d, substream->stream %d, rate %d, number %d\n",
		__func__, id, substream->stream, rate, substream->number);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		priv->dl_rate[id] = rate;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		priv->ul_rate[id] = rate;

	return 0;
}

static int mt6359_codec_dai_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct snd_soc_component *cmpnt = dai->component;
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s stream %d\n", __func__, substream->stream);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mt6359_set_playback_gpio(priv);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		mt6359_set_capture_gpio(priv);

	return 0;
}

static void mt6359_codec_dai_shutdown(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *cmpnt = dai->component;
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s stream %d\n", __func__, substream->stream);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mt6359_reset_playback_gpio(priv);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		mt6359_reset_capture_gpio(priv);
}

static const struct snd_soc_dai_ops mt6359_codec_dai_ops = {
	.hw_params = mt6359_codec_dai_hw_params,
	.startup = mt6359_codec_dai_startup,
	.shutdown = mt6359_codec_dai_shutdown,
};

#define MT6359_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE)

static struct snd_soc_dai_driver mt6359_dai_driver[] = {
	{
		.id = MT6359_AIF_1,
		.name = "mt6359-snd-codec-aif1",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000 |
				 SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = MT6359_FORMATS,
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
			.formats = MT6359_FORMATS,
		},
		.ops = &mt6359_codec_dai_ops,
	},
	{
		.id = MT6359_AIF_2,
		.name = "mt6359-snd-codec-aif2",
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000 |
				 SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = MT6359_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_48000,
			.formats = MT6359_FORMATS,
		},
		.ops = &mt6359_codec_dai_ops,
	},
};

static int mt6359_codec_init_reg(struct snd_soc_component *cmpnt)
{
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	/* enable clk buf */
	regmap_update_bits(priv->regmap, MT6359_DCXO_CW12,
			   0x1 << RG_XO_AUDIO_EN_M_SFT,
			   0x1 << RG_XO_AUDIO_EN_M_SFT);

	/* set those not controlled by dapm widget */

	/* audio clk source from internal dcxo */
	regmap_update_bits(priv->regmap, MT6359_AUDENC_ANA_CON23,
			   RG_CLKSQ_IN_SEL_TEST_MASK_SFT,
			   0x0);

	/* Disable HeadphoneL/HeadphoneR short circuit protection */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON0,
			   RG_AUDHPLSCDISABLE_VAUDP32_MASK_SFT,
			   0x1 << RG_AUDHPLSCDISABLE_VAUDP32_SFT);
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON0,
			   RG_AUDHPRSCDISABLE_VAUDP32_MASK_SFT,
			   0x1 << RG_AUDHPRSCDISABLE_VAUDP32_SFT);
	/* Disable voice short circuit protection */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON6,
			   RG_AUDHSSCDISABLE_VAUDP32_MASK_SFT,
			   0x1 << RG_AUDHSSCDISABLE_VAUDP32_SFT);
	/* disable LO buffer left short circuit protection */
	regmap_update_bits(priv->regmap, MT6359_AUDDEC_ANA_CON7,
			   RG_AUDLOLSCDISABLE_VAUDP32_MASK_SFT,
			   0x1 << RG_AUDLOLSCDISABLE_VAUDP32_SFT);

	/* set gpio */
	mt6359_reset_playback_gpio(priv);
	mt6359_reset_capture_gpio(priv);

	/* hp hifi mode, default normal mode */
	priv->hp_hifi_mode = 0;

	/* Disable AUD_ZCD */
	zcd_disable(priv);

	/* disable clk buf */
	regmap_update_bits(priv->regmap, MT6359_DCXO_CW12,
			   0x1 << RG_XO_AUDIO_EN_M_SFT,
			   0x0 << RG_XO_AUDIO_EN_M_SFT);

	return 0;
}

static int mt6359_codec_probe(struct snd_soc_component *cmpnt)
{
	struct mt6359_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	snd_soc_component_init_regmap(cmpnt, priv->regmap);

	return mt6359_codec_init_reg(cmpnt);
}

static void mt6359_codec_remove(struct snd_soc_component *cmpnt)
{
	cmpnt->regmap = NULL;
}

static const DECLARE_TLV_DB_SCALE(hp_playback_tlv, -2200, 100, 0);
static const DECLARE_TLV_DB_SCALE(playback_tlv, -1000, 100, 0);
static const DECLARE_TLV_DB_SCALE(capture_tlv, 0, 600, 0);

static const struct snd_kcontrol_new mt6359_snd_controls[] = {
	/* dl pga gain */
	SOC_DOUBLE_EXT_TLV("Headset Volume",
			   MT6359_ZCD_CON2, 0, 7, 0x1E, 0,
			   snd_soc_get_volsw, mt6359_put_volsw,
			   hp_playback_tlv),
	SOC_DOUBLE_EXT_TLV("Lineout Volume",
			   MT6359_ZCD_CON1, 0, 7, 0x12, 0,
			   snd_soc_get_volsw, mt6359_put_volsw, playback_tlv),
	SOC_SINGLE_EXT_TLV("Handset Volume",
			   MT6359_ZCD_CON3, 0, 0x12, 0,
			   snd_soc_get_volsw, mt6359_put_volsw, playback_tlv),

	/* ul pga gain */
	SOC_SINGLE_EXT_TLV("PGA1 Volume",
			   MT6359_AUDENC_ANA_CON0, RG_AUDPREAMPLGAIN_SFT, 4, 0,
			   snd_soc_get_volsw, mt6359_put_volsw, capture_tlv),
	SOC_SINGLE_EXT_TLV("PGA2 Volume",
			   MT6359_AUDENC_ANA_CON1, RG_AUDPREAMPRGAIN_SFT, 4, 0,
			   snd_soc_get_volsw, mt6359_put_volsw, capture_tlv),
	SOC_SINGLE_EXT_TLV("PGA3 Volume",
			   MT6359_AUDENC_ANA_CON2, RG_AUDPREAMP3GAIN_SFT, 4, 0,
			   snd_soc_get_volsw, mt6359_put_volsw, capture_tlv),
};

static const struct snd_soc_component_driver mt6359_soc_component_driver = {
	.name = CODEC_MT6359_NAME,
	.probe = mt6359_codec_probe,
	.remove = mt6359_codec_remove,
	.controls = mt6359_snd_controls,
	.num_controls = ARRAY_SIZE(mt6359_snd_controls),
	.dapm_widgets = mt6359_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6359_dapm_widgets),
	.dapm_routes = mt6359_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6359_dapm_routes),
	.endianness = 1,
};

static int mt6359_parse_dt(struct mt6359_priv *priv)
{
	int ret;
	struct device *dev = priv->dev;
	struct device_node *np;

	np = of_get_child_by_name(dev->parent->of_node, "mt6359codec");
	if (!np)
		return -EINVAL;

	ret = of_property_read_u32(np, "mediatek,dmic-mode",
				   &priv->dmic_one_wire_mode);
	if (ret) {
		dev_info(priv->dev,
			 "%s() failed to read dmic-mode, use default (0)\n",
			 __func__);
		priv->dmic_one_wire_mode = 0;
	}

	ret = of_property_read_u32(np, "mediatek,mic-type-0",
				   &priv->mux_select[MUX_MIC_TYPE_0]);
	if (ret) {
		dev_info(priv->dev,
			 "%s() failed to read mic-type-0, use default (%d)\n",
			 __func__, MIC_TYPE_MUX_IDLE);
		priv->mux_select[MUX_MIC_TYPE_0] = MIC_TYPE_MUX_IDLE;
	}

	ret = of_property_read_u32(np, "mediatek,mic-type-1",
				   &priv->mux_select[MUX_MIC_TYPE_1]);
	if (ret) {
		dev_info(priv->dev,
			 "%s() failed to read mic-type-1, use default (%d)\n",
			 __func__, MIC_TYPE_MUX_IDLE);
		priv->mux_select[MUX_MIC_TYPE_1] = MIC_TYPE_MUX_IDLE;
	}

	ret = of_property_read_u32(np, "mediatek,mic-type-2",
				   &priv->mux_select[MUX_MIC_TYPE_2]);
	of_node_put(np);
	if (ret) {
		dev_info(priv->dev,
			 "%s() failed to read mic-type-2, use default (%d)\n",
			 __func__, MIC_TYPE_MUX_IDLE);
		priv->mux_select[MUX_MIC_TYPE_2] = MIC_TYPE_MUX_IDLE;
	}

	return 0;
}

static int mt6359_platform_driver_probe(struct platform_device *pdev)
{
	struct mt6359_priv *priv;
	int ret;
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);

	dev_dbg(&pdev->dev, "%s(), dev name %s\n",
		__func__, dev_name(&pdev->dev));

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = mt6397->regmap;
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	dev_set_drvdata(&pdev->dev, priv);
	priv->dev = &pdev->dev;

	ret = mt6359_parse_dt(priv);
	if (ret) {
		dev_warn(&pdev->dev, "%s() failed to parse dts\n", __func__);
		return ret;
	}

	return devm_snd_soc_register_component(&pdev->dev,
					       &mt6359_soc_component_driver,
					       mt6359_dai_driver,
					       ARRAY_SIZE(mt6359_dai_driver));
}

static struct platform_driver mt6359_platform_driver = {
	.driver = {
		.name = "mt6359-sound",
	},
	.probe = mt6359_platform_driver_probe,
};

module_platform_driver(mt6359_platform_driver)

/* Module information */
MODULE_DESCRIPTION("MT6359 ALSA SoC codec driver");
MODULE_AUTHOR("KaiChieh Chuang <kaichieh.chuang@mediatek.com>");
MODULE_AUTHOR("Eason Yen <eason.yen@mediatek.com>");
MODULE_LICENSE("GPL v2");
