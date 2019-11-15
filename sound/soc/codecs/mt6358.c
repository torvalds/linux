// SPDX-License-Identifier: GPL-2.0
//
// mt6358.c  --  mt6358 ALSA SoC audio codec driver
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/regulator/consumer.h>

#include <sound/soc.h>
#include <sound/tlv.h>

#include "mt6358.h"

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

enum {
	MUX_ADC_L,
	MUX_ADC_R,
	MUX_PGA_L,
	MUX_PGA_R,
	MUX_MIC_TYPE,
	MUX_HP_L,
	MUX_HP_R,
	MUX_NUM,
};

enum {
	DEVICE_HP,
	DEVICE_LO,
	DEVICE_RCV,
	DEVICE_MIC1,
	DEVICE_MIC2,
	DEVICE_NUM
};

/* Supply widget subseq */
enum {
	/* common */
	SUPPLY_SEQ_CLK_BUF,
	SUPPLY_SEQ_AUD_GLB,
	SUPPLY_SEQ_CLKSQ,
	SUPPLY_SEQ_VOW_AUD_LPW,
	SUPPLY_SEQ_AUD_VOW,
	SUPPLY_SEQ_VOW_CLK,
	SUPPLY_SEQ_VOW_LDO,
	SUPPLY_SEQ_TOP_CK,
	SUPPLY_SEQ_TOP_CK_LAST,
	SUPPLY_SEQ_AUD_TOP,
	SUPPLY_SEQ_AUD_TOP_LAST,
	SUPPLY_SEQ_AFE,
	/* capture */
	SUPPLY_SEQ_ADC_SUPPLY,
};

enum {
	CH_L = 0,
	CH_R,
	NUM_CH,
};

#define REG_STRIDE 2

struct mt6358_priv {
	struct device *dev;
	struct regmap *regmap;

	unsigned int dl_rate;
	unsigned int ul_rate;

	int ana_gain[AUDIO_ANALOG_VOLUME_TYPE_MAX];
	unsigned int mux_select[MUX_NUM];

	int dev_counter[DEVICE_NUM];

	int mtkaif_protocol;

	struct regulator *avdd_reg;
};

int mt6358_set_mtkaif_protocol(struct snd_soc_component *cmpnt,
			       int mtkaif_protocol)
{
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	priv->mtkaif_protocol = mtkaif_protocol;
	return 0;
}

static void playback_gpio_set(struct mt6358_priv *priv)
{
	/* set gpio mosi mode */
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE2_CLR,
			   0x01f8, 0x01f8);
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE2_SET,
			   0xffff, 0x0249);
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE2,
			   0xffff, 0x0249);
}

static void playback_gpio_reset(struct mt6358_priv *priv)
{
	/* set pad_aud_*_mosi to GPIO mode and dir input
	 * reason:
	 * pad_aud_dat_mosi*, because the pin is used as boot strap
	 * don't clean clk/sync, for mtkaif protocol 2
	 */
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE2_CLR,
			   0x01f8, 0x01f8);
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE2,
			   0x01f8, 0x0000);
	regmap_update_bits(priv->regmap, MT6358_GPIO_DIR0,
			   0xf << 8, 0x0);
}

static void capture_gpio_set(struct mt6358_priv *priv)
{
	/* set gpio miso mode */
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE3_CLR,
			   0xffff, 0xffff);
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE3_SET,
			   0xffff, 0x0249);
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE3,
			   0xffff, 0x0249);
}

static void capture_gpio_reset(struct mt6358_priv *priv)
{
	/* set pad_aud_*_miso to GPIO mode and dir input
	 * reason:
	 * pad_aud_clk_miso, because when playback only the miso_clk
	 * will also have 26m, so will have power leak
	 * pad_aud_dat_miso*, because the pin is used as boot strap
	 */
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE3_CLR,
			   0xffff, 0xffff);
	regmap_update_bits(priv->regmap, MT6358_GPIO_MODE3,
			   0xffff, 0x0000);
	regmap_update_bits(priv->regmap, MT6358_GPIO_DIR0,
			   0xf << 12, 0x0);
}

/* use only when not govern by DAPM */
static int mt6358_set_dcxo(struct mt6358_priv *priv, bool enable)
{
	regmap_update_bits(priv->regmap, MT6358_DCXO_CW14,
			   0x1 << RG_XO_AUDIO_EN_M_SFT,
			   (enable ? 1 : 0) << RG_XO_AUDIO_EN_M_SFT);
	return 0;
}

/* use only when not govern by DAPM */
static int mt6358_set_clksq(struct mt6358_priv *priv, bool enable)
{
	/* audio clk source from internal dcxo */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON6,
			   RG_CLKSQ_IN_SEL_TEST_MASK_SFT,
			   0x0);

	/* Enable/disable CLKSQ 26MHz */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON6,
			   RG_CLKSQ_EN_MASK_SFT,
			   (enable ? 1 : 0) << RG_CLKSQ_EN_SFT);
	return 0;
}

/* use only when not govern by DAPM */
static int mt6358_set_aud_global_bias(struct mt6358_priv *priv, bool enable)
{
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13,
			   RG_AUDGLB_PWRDN_VA28_MASK_SFT,
			   (enable ? 0 : 1) << RG_AUDGLB_PWRDN_VA28_SFT);
	return 0;
}

/* use only when not govern by DAPM */
static int mt6358_set_topck(struct mt6358_priv *priv, bool enable)
{
	regmap_update_bits(priv->regmap, MT6358_AUD_TOP_CKPDN_CON0,
			   0x0066, enable ? 0x0 : 0x66);
	return 0;
}

static int mt6358_mtkaif_tx_enable(struct mt6358_priv *priv)
{
	switch (priv->mtkaif_protocol) {
	case MT6358_MTKAIF_PROTOCOL_2_CLK_P2:
		/* MTKAIF TX format setting */
		regmap_update_bits(priv->regmap,
				   MT6358_AFE_ADDA_MTKAIF_CFG0,
				   0xffff, 0x0010);
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap,
				   MT6358_AFE_AUD_PAD_TOP,
				   0xff00, 0x3800);
		regmap_update_bits(priv->regmap,
				   MT6358_AFE_AUD_PAD_TOP,
				   0xff00, 0x3900);
		break;
	case MT6358_MTKAIF_PROTOCOL_2:
		/* MTKAIF TX format setting */
		regmap_update_bits(priv->regmap,
				   MT6358_AFE_ADDA_MTKAIF_CFG0,
				   0xffff, 0x0010);
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap,
				   MT6358_AFE_AUD_PAD_TOP,
				   0xff00, 0x3100);
		break;
	case MT6358_MTKAIF_PROTOCOL_1:
	default:
		/* MTKAIF TX format setting */
		regmap_update_bits(priv->regmap,
				   MT6358_AFE_ADDA_MTKAIF_CFG0,
				   0xffff, 0x0000);
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap,
				   MT6358_AFE_AUD_PAD_TOP,
				   0xff00, 0x3100);
		break;
	}
	return 0;
}

static int mt6358_mtkaif_tx_disable(struct mt6358_priv *priv)
{
	/* disable aud_pad TX fifos */
	regmap_update_bits(priv->regmap, MT6358_AFE_AUD_PAD_TOP,
			   0xff00, 0x3000);
	return 0;
}

int mt6358_mtkaif_calibration_enable(struct snd_soc_component *cmpnt)
{
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	playback_gpio_set(priv);
	capture_gpio_set(priv);
	mt6358_mtkaif_tx_enable(priv);

	mt6358_set_dcxo(priv, true);
	mt6358_set_aud_global_bias(priv, true);
	mt6358_set_clksq(priv, true);
	mt6358_set_topck(priv, true);

	/* set dat_miso_loopback on */
	regmap_update_bits(priv->regmap, MT6358_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_MASK_SFT,
			   1 << RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_SFT);
	regmap_update_bits(priv->regmap, MT6358_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_MASK_SFT,
			   1 << RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_SFT);
	return 0;
}

int mt6358_mtkaif_calibration_disable(struct snd_soc_component *cmpnt)
{
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	/* set dat_miso_loopback off */
	regmap_update_bits(priv->regmap, MT6358_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_MASK_SFT,
			   0 << RG_AUD_PAD_TOP_DAT_MISO2_LOOPBACK_SFT);
	regmap_update_bits(priv->regmap, MT6358_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_MASK_SFT,
			   0 << RG_AUD_PAD_TOP_DAT_MISO_LOOPBACK_SFT);

	mt6358_set_topck(priv, false);
	mt6358_set_clksq(priv, false);
	mt6358_set_aud_global_bias(priv, false);
	mt6358_set_dcxo(priv, false);

	mt6358_mtkaif_tx_disable(priv);
	playback_gpio_reset(priv);
	capture_gpio_reset(priv);
	return 0;
}

int mt6358_set_mtkaif_calibration_phase(struct snd_soc_component *cmpnt,
					int phase_1, int phase_2)
{
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	regmap_update_bits(priv->regmap, MT6358_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_PHASE_MODE_MASK_SFT,
			   phase_1 << RG_AUD_PAD_TOP_PHASE_MODE_SFT);
	regmap_update_bits(priv->regmap, MT6358_AUDIO_DIG_CFG,
			   RG_AUD_PAD_TOP_PHASE_MODE2_MASK_SFT,
			   phase_2 << RG_AUD_PAD_TOP_PHASE_MODE2_SFT);
	return 0;
}

/* dl pga gain */
enum {
	DL_GAIN_8DB = 0,
	DL_GAIN_0DB = 8,
	DL_GAIN_N_1DB = 9,
	DL_GAIN_N_10DB = 18,
	DL_GAIN_N_40DB = 0x1f,
};

#define DL_GAIN_N_10DB_REG (DL_GAIN_N_10DB << 7 | DL_GAIN_N_10DB)
#define DL_GAIN_N_40DB_REG (DL_GAIN_N_40DB << 7 | DL_GAIN_N_40DB)
#define DL_GAIN_REG_MASK 0x0f9f

static void hp_zcd_disable(struct mt6358_priv *priv)
{
	regmap_write(priv->regmap, MT6358_ZCD_CON0, 0x0000);
}

static void hp_main_output_ramp(struct mt6358_priv *priv, bool up)
{
	int i = 0, stage = 0;
	int target = 7;

	/* Enable/Reduce HPL/R main output stage step by step */
	for (i = 0; i <= target; i++) {
		stage = up ? i : target - i;
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON1,
				   0x7 << 8, stage << 8);
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON1,
				   0x7 << 11, stage << 11);
		usleep_range(100, 150);
	}
}

static void hp_aux_feedback_loop_gain_ramp(struct mt6358_priv *priv, bool up)
{
	int i = 0, stage = 0;

	/* Reduce HP aux feedback loop gain step by step */
	for (i = 0; i <= 0xf; i++) {
		stage = up ? i : 0xf - i;
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON9,
				   0xf << 12, stage << 12);
		usleep_range(100, 150);
	}
}

static void hp_pull_down(struct mt6358_priv *priv, bool enable)
{
	int i;

	if (enable) {
		for (i = 0x0; i <= 0x6; i++) {
			regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON4,
					   0x7, i);
			usleep_range(600, 700);
		}
	} else {
		for (i = 0x6; i >= 0x1; i--) {
			regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON4,
					   0x7, i);
			usleep_range(600, 700);
		}
	}
}

static bool is_valid_hp_pga_idx(int reg_idx)
{
	return (reg_idx >= DL_GAIN_8DB && reg_idx <= DL_GAIN_N_10DB) ||
	       reg_idx == DL_GAIN_N_40DB;
}

static void headset_volume_ramp(struct mt6358_priv *priv, int from, int to)
{
	int offset = 0, count = 0, reg_idx;

	if (!is_valid_hp_pga_idx(from) || !is_valid_hp_pga_idx(to))
		dev_warn(priv->dev, "%s(), volume index is not valid, from %d, to %d\n",
			 __func__, from, to);

	dev_info(priv->dev, "%s(), from %d, to %d\n",
		 __func__, from, to);

	if (to > from)
		offset = to - from;
	else
		offset = from - to;

	while (offset >= 0) {
		if (to > from)
			reg_idx = from + count;
		else
			reg_idx = from - count;

		if (is_valid_hp_pga_idx(reg_idx)) {
			regmap_update_bits(priv->regmap,
					   MT6358_ZCD_CON2,
					   DL_GAIN_REG_MASK,
					   (reg_idx << 7) | reg_idx);
			usleep_range(200, 300);
		}
		offset--;
		count++;
	}
}

static int mt6358_put_volsw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
			(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg;
	int ret;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	switch (mc->reg) {
	case MT6358_ZCD_CON2:
		regmap_read(priv->regmap, MT6358_ZCD_CON2, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL] =
			(reg >> RG_AUDHPLGAIN_SFT) & RG_AUDHPLGAIN_MASK;
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTR] =
			(reg >> RG_AUDHPRGAIN_SFT) & RG_AUDHPRGAIN_MASK;
		break;
	case MT6358_ZCD_CON1:
		regmap_read(priv->regmap, MT6358_ZCD_CON1, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_LINEOUTL] =
			(reg >> RG_AUDLOLGAIN_SFT) & RG_AUDLOLGAIN_MASK;
		priv->ana_gain[AUDIO_ANALOG_VOLUME_LINEOUTR] =
			(reg >> RG_AUDLORGAIN_SFT) & RG_AUDLORGAIN_MASK;
		break;
	case MT6358_ZCD_CON3:
		regmap_read(priv->regmap, MT6358_ZCD_CON3, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HSOUTL] =
			(reg >> RG_AUDHSGAIN_SFT) & RG_AUDHSGAIN_MASK;
		priv->ana_gain[AUDIO_ANALOG_VOLUME_HSOUTR] =
			(reg >> RG_AUDHSGAIN_SFT) & RG_AUDHSGAIN_MASK;
		break;
	case MT6358_AUDENC_ANA_CON0:
	case MT6358_AUDENC_ANA_CON1:
		regmap_read(priv->regmap, MT6358_AUDENC_ANA_CON0, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP1] =
			(reg >> RG_AUDPREAMPLGAIN_SFT) & RG_AUDPREAMPLGAIN_MASK;
		regmap_read(priv->regmap, MT6358_AUDENC_ANA_CON1, &reg);
		priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP2] =
			(reg >> RG_AUDPREAMPRGAIN_SFT) & RG_AUDPREAMPRGAIN_MASK;
		break;
	}

	return ret;
}

static const DECLARE_TLV_DB_SCALE(playback_tlv, -1000, 100, 0);
static const DECLARE_TLV_DB_SCALE(pga_tlv, 0, 600, 0);

static const struct snd_kcontrol_new mt6358_snd_controls[] = {
	/* dl pga gain */
	SOC_DOUBLE_EXT_TLV("Headphone Volume",
			   MT6358_ZCD_CON2, 0, 7, 0x12, 1,
			   snd_soc_get_volsw, mt6358_put_volsw, playback_tlv),
	SOC_DOUBLE_EXT_TLV("Lineout Volume",
			   MT6358_ZCD_CON1, 0, 7, 0x12, 1,
			   snd_soc_get_volsw, mt6358_put_volsw, playback_tlv),
	SOC_SINGLE_EXT_TLV("Handset Volume",
			   MT6358_ZCD_CON3, 0, 0x12, 1,
			   snd_soc_get_volsw, mt6358_put_volsw, playback_tlv),
	/* ul pga gain */
	SOC_DOUBLE_R_EXT_TLV("PGA Volume",
			     MT6358_AUDENC_ANA_CON0, MT6358_AUDENC_ANA_CON1,
			     8, 4, 0,
			     snd_soc_get_volsw, mt6358_put_volsw, pga_tlv),
};

/* MUX */
/* LOL MUX */
static const char * const lo_in_mux_map[] = {
	"Open", "Mute", "Playback", "Test Mode"
};

static int lo_in_mux_map_value[] = {
	0x0, 0x1, 0x2, 0x3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(lo_in_mux_map_enum,
				  MT6358_AUDDEC_ANA_CON7,
				  RG_AUDLOLMUXINPUTSEL_VAUDP15_SFT,
				  RG_AUDLOLMUXINPUTSEL_VAUDP15_MASK,
				  lo_in_mux_map,
				  lo_in_mux_map_value);

static const struct snd_kcontrol_new lo_in_mux_control =
	SOC_DAPM_ENUM("In Select", lo_in_mux_map_enum);

/*HP MUX */
enum {
	HP_MUX_OPEN = 0,
	HP_MUX_HPSPK,
	HP_MUX_HP,
	HP_MUX_TEST_MODE,
	HP_MUX_HP_IMPEDANCE,
	HP_MUX_MASK = 0x7,
};

static const char * const hp_in_mux_map[] = {
	"Open",
	"LoudSPK Playback",
	"Audio Playback",
	"Test Mode",
	"HP Impedance",
	"undefined1",
	"undefined2",
	"undefined3",
};

static int hp_in_mux_map_value[] = {
	HP_MUX_OPEN,
	HP_MUX_HPSPK,
	HP_MUX_HP,
	HP_MUX_TEST_MODE,
	HP_MUX_HP_IMPEDANCE,
	HP_MUX_OPEN,
	HP_MUX_OPEN,
	HP_MUX_OPEN,
};

static SOC_VALUE_ENUM_SINGLE_DECL(hpl_in_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  HP_MUX_MASK,
				  hp_in_mux_map,
				  hp_in_mux_map_value);

static const struct snd_kcontrol_new hpl_in_mux_control =
	SOC_DAPM_ENUM("HPL Select", hpl_in_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hpr_in_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  HP_MUX_MASK,
				  hp_in_mux_map,
				  hp_in_mux_map_value);

static const struct snd_kcontrol_new hpr_in_mux_control =
	SOC_DAPM_ENUM("HPR Select", hpr_in_mux_map_enum);

/* RCV MUX */
enum {
	RCV_MUX_OPEN = 0,
	RCV_MUX_MUTE,
	RCV_MUX_VOICE_PLAYBACK,
	RCV_MUX_TEST_MODE,
	RCV_MUX_MASK = 0x3,
};

static const char * const rcv_in_mux_map[] = {
	"Open", "Mute", "Voice Playback", "Test Mode"
};

static int rcv_in_mux_map_value[] = {
	RCV_MUX_OPEN,
	RCV_MUX_MUTE,
	RCV_MUX_VOICE_PLAYBACK,
	RCV_MUX_TEST_MODE,
};

static SOC_VALUE_ENUM_SINGLE_DECL(rcv_in_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  RCV_MUX_MASK,
				  rcv_in_mux_map,
				  rcv_in_mux_map_value);

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
				  MT6358_AFE_TOP_CON0,
				  DL_SINE_ON_SFT,
				  DL_SINE_ON_MASK,
				  dac_in_mux_map,
				  dac_in_mux_map_value);

static const struct snd_kcontrol_new dac_in_mux_control =
	SOC_DAPM_ENUM("DAC Select", dac_in_mux_map_enum);

/* AIF Out MUX */
static SOC_VALUE_ENUM_SINGLE_DECL(aif_out_mux_map_enum,
				  MT6358_AFE_TOP_CON0,
				  UL_SINE_ON_SFT,
				  UL_SINE_ON_MASK,
				  dac_in_mux_map,
				  dac_in_mux_map_value);

static const struct snd_kcontrol_new aif_out_mux_control =
	SOC_DAPM_ENUM("AIF Out Select", aif_out_mux_map_enum);

/* Mic Type MUX */
enum {
	MIC_TYPE_MUX_IDLE = 0,
	MIC_TYPE_MUX_ACC,
	MIC_TYPE_MUX_DMIC,
	MIC_TYPE_MUX_DCC,
	MIC_TYPE_MUX_DCC_ECM_DIFF,
	MIC_TYPE_MUX_DCC_ECM_SINGLE,
	MIC_TYPE_MUX_MASK = 0x7,
};

#define IS_DCC_BASE(type) ((type) == MIC_TYPE_MUX_DCC || \
			(type) == MIC_TYPE_MUX_DCC_ECM_DIFF || \
			(type) == MIC_TYPE_MUX_DCC_ECM_SINGLE)

static const char * const mic_type_mux_map[] = {
	"Idle",
	"ACC",
	"DMIC",
	"DCC",
	"DCC_ECM_DIFF",
	"DCC_ECM_SINGLE",
};

static int mic_type_mux_map_value[] = {
	MIC_TYPE_MUX_IDLE,
	MIC_TYPE_MUX_ACC,
	MIC_TYPE_MUX_DMIC,
	MIC_TYPE_MUX_DCC,
	MIC_TYPE_MUX_DCC_ECM_DIFF,
	MIC_TYPE_MUX_DCC_ECM_SINGLE,
};

static SOC_VALUE_ENUM_SINGLE_DECL(mic_type_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  MIC_TYPE_MUX_MASK,
				  mic_type_mux_map,
				  mic_type_mux_map_value);

static const struct snd_kcontrol_new mic_type_mux_control =
	SOC_DAPM_ENUM("Mic Type Select", mic_type_mux_map_enum);

/* ADC L MUX */
enum {
	ADC_MUX_IDLE = 0,
	ADC_MUX_AIN0,
	ADC_MUX_PREAMPLIFIER,
	ADC_MUX_IDLE1,
	ADC_MUX_MASK = 0x3,
};

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
				  SND_SOC_NOPM,
				  0,
				  ADC_MUX_MASK,
				  adc_left_mux_map,
				  adc_mux_map_value);

static const struct snd_kcontrol_new adc_left_mux_control =
	SOC_DAPM_ENUM("ADC L Select", adc_left_mux_map_enum);

/* ADC R MUX */
static const char * const adc_right_mux_map[] = {
	"Idle", "AIN0", "Right Preamplifier", "Idle_1"
};

static SOC_VALUE_ENUM_SINGLE_DECL(adc_right_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  ADC_MUX_MASK,
				  adc_right_mux_map,
				  adc_mux_map_value);

static const struct snd_kcontrol_new adc_right_mux_control =
	SOC_DAPM_ENUM("ADC R Select", adc_right_mux_map_enum);

/* PGA L MUX */
enum {
	PGA_MUX_NONE = 0,
	PGA_MUX_AIN0,
	PGA_MUX_AIN1,
	PGA_MUX_AIN2,
	PGA_MUX_MASK = 0x3,
};

static const char * const pga_mux_map[] = {
	"None", "AIN0", "AIN1", "AIN2"
};

static int pga_mux_map_value[] = {
	PGA_MUX_NONE,
	PGA_MUX_AIN0,
	PGA_MUX_AIN1,
	PGA_MUX_AIN2,
};

static SOC_VALUE_ENUM_SINGLE_DECL(pga_left_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  PGA_MUX_MASK,
				  pga_mux_map,
				  pga_mux_map_value);

static const struct snd_kcontrol_new pga_left_mux_control =
	SOC_DAPM_ENUM("PGA L Select", pga_left_mux_map_enum);

/* PGA R MUX */
static SOC_VALUE_ENUM_SINGLE_DECL(pga_right_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  PGA_MUX_MASK,
				  pga_mux_map,
				  pga_mux_map_value);

static const struct snd_kcontrol_new pga_right_mux_control =
	SOC_DAPM_ENUM("PGA R Select", pga_right_mux_map_enum);

static int mt_clksq_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* audio clk source from internal dcxo */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON6,
				   RG_CLKSQ_IN_SEL_TEST_MASK_SFT,
				   0x0);
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
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event = 0x%x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* sdm audio fifo clock power on */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON2, 0x0006);
		/* scrambler clock on enable */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON0, 0xCBA1);
		/* sdm power on */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON2, 0x0003);
		/* sdm fifo enable */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON2, 0x000B);

		regmap_update_bits(priv->regmap, MT6358_AFE_SGEN_CFG0,
				   0xff3f,
				   0x0000);
		regmap_update_bits(priv->regmap, MT6358_AFE_SGEN_CFG1,
				   0xffff,
				   0x0001);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* DL scrambler disabling sequence */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON2, 0x0000);
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON0, 0xcba0);
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
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_info(priv->dev, "%s(), event 0x%x, rate %d\n",
		 __func__, event, priv->dl_rate);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		playback_gpio_set(priv);

		/* sdm audio fifo clock power on */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON2, 0x0006);
		/* scrambler clock on enable */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON0, 0xCBA1);
		/* sdm power on */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON2, 0x0003);
		/* sdm fifo enable */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON2, 0x000B);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* DL scrambler disabling sequence */
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON2, 0x0000);
		regmap_write(priv->regmap, MT6358_AFUNC_AUD_CON0, 0xcba0);

		playback_gpio_reset(priv);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_hp_enable(struct mt6358_priv *priv)
{
	/* Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, true);
	/* release HP CMFB gate rstb */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON4,
			   0x1 << 6, 0x1 << 6);

	/* Reduce ESD resistance of AU_REFN */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON2, 0x4000);

	/* Set HPR/HPL gain as minimum (~ -40dB) */
	regmap_write(priv->regmap, MT6358_ZCD_CON2, DL_GAIN_N_40DB_REG);

	/* Turn on DA_600K_NCP_VA18 */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON1, 0x0001);
	/* Set NCP clock as 604kHz // 26MHz/43 = 604KHz */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON2, 0x002c);
	/* Toggle RG_DIVCKS_CHG */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON0, 0x0001);
	/* Set NCP soft start mode as default mode: 100us */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON4, 0x0003);
	/* Enable NCP */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON3, 0x0000);
	usleep_range(250, 270);

	/* Enable cap-less LDOs (1.5V) */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
			   0x1055, 0x1055);
	/* Enable NV regulator (-1.2V) */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON15, 0x0001);
	usleep_range(100, 120);

	/* Disable AUD_ZCD */
	hp_zcd_disable(priv);

	/* Disable headphone short-circuit protection */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x3000);

	/* Enable IBIST */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON12, 0x0055);

	/* Set HP DR bias current optimization, 010: 6uA */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON11, 0x4900);
	/* Set HP & ZCD bias current optimization */
	/* 01: ZCD: 4uA, HP/HS/LO: 5uA */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON12, 0x0055);
	/* Set HPP/N STB enhance circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON2, 0x4033);

	/* Enable HP aux output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x000c);
	/* Enable HP aux feedback loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x003c);
	/* Enable HP aux CMFB loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0c00);
	/* Enable HP driver bias circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x30c0);
	/* Enable HP driver core circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x30f0);
	/* Short HP main output to HP aux output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x00fc);

	/* Enable HP main CMFB loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0e00);
	/* Disable HP aux CMFB loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0200);

	/* Select CMFB resistor bulk to AC mode */
	/* Selec HS/LO cap size (6.5pF default) */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON10, 0x0000);

	/* Enable HP main output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x00ff);
	/* Enable HPR/L main output stage step by step */
	hp_main_output_ramp(priv, true);

	/* Reduce HP aux feedback loop gain */
	hp_aux_feedback_loop_gain_ramp(priv, true);
	/* Disable HP aux feedback loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3fcf);

	/* apply volume setting */
	headset_volume_ramp(priv,
			    DL_GAIN_N_10DB,
			    priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL]);

	/* Disable HP aux output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3fc3);
	/* Unshort HP main output to HP aux output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3f03);
	usleep_range(100, 120);

	/* Enable AUD_CLK */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13, 0x1, 0x1);
	/* Enable Audio DAC  */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x30ff);
	/* Enable low-noise mode of DAC */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0xf201);
	usleep_range(100, 120);

	/* Switch HPL MUX to audio DAC */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x32ff);
	/* Switch HPR MUX to audio DAC */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x3aff);

	/* Disable Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, false);

	return 0;
}

static int mtk_hp_disable(struct mt6358_priv *priv)
{
	/* Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, true);

	/* HPR/HPL mux to open */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   0x0f00, 0x0000);

	/* Disable low-noise mode of DAC */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON9,
			   0x0001, 0x0000);

	/* Disable Audio DAC */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   0x000f, 0x0000);

	/* Disable AUD_CLK */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13, 0x1, 0x0);

	/* Short HP main output to HP aux output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3fc3);
	/* Enable HP aux output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3fcf);

	/* decrease HPL/R gain to normal gain step by step */
	headset_volume_ramp(priv,
			    priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL],
			    DL_GAIN_N_40DB);

	/* Enable HP aux feedback loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3fff);

	/* Reduce HP aux feedback loop gain */
	hp_aux_feedback_loop_gain_ramp(priv, false);

	/* decrease HPR/L main output stage step by step */
	hp_main_output_ramp(priv, false);

	/* Disable HP main output stage */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3, 0x0);

	/* Enable HP aux CMFB loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0e00);

	/* Disable HP main CMFB loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0c00);

	/* Unshort HP main output to HP aux output stage */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON1,
			   0x3 << 6, 0x0);

	/* Disable HP driver core circuits */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   0x3 << 4, 0x0);

	/* Disable HP driver bias circuits */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   0x3 << 6, 0x0);

	/* Disable HP aux CMFB loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0000);

	/* Disable HP aux feedback loop */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON1,
			   0x3 << 4, 0x0);

	/* Disable HP aux output stage */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON1,
			   0x3 << 2, 0x0);

	/* Disable IBIST */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON12,
			   0x1 << 8, 0x1 << 8);

	/* Disable NV regulator (-1.2V) */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON15, 0x1, 0x0);
	/* Disable cap-less LDOs (1.5V) */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
			   0x1055, 0x0);
	/* Disable NCP */
	regmap_update_bits(priv->regmap, MT6358_AUDNCP_CLKDIV_CON3,
			   0x1, 0x1);

	/* Increase ESD resistance of AU_REFN */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON2,
			   0x1 << 14, 0x0);

	/* Set HP CMFB gate rstb */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON4,
			   0x1 << 6, 0x0);
	/* disable Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, false);

	return 0;
}

static int mtk_hp_spk_enable(struct mt6358_priv *priv)
{
	/* Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, true);
	/* release HP CMFB gate rstb */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON4,
			   0x1 << 6, 0x1 << 6);

	/* Reduce ESD resistance of AU_REFN */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON2, 0x4000);

	/* Set HPR/HPL gain to -10dB */
	regmap_write(priv->regmap, MT6358_ZCD_CON2, DL_GAIN_N_10DB_REG);

	/* Turn on DA_600K_NCP_VA18 */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON1, 0x0001);
	/* Set NCP clock as 604kHz // 26MHz/43 = 604KHz */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON2, 0x002c);
	/* Toggle RG_DIVCKS_CHG */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON0, 0x0001);
	/* Set NCP soft start mode as default mode: 100us */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON4, 0x0003);
	/* Enable NCP */
	regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON3, 0x0000);
	usleep_range(250, 270);

	/* Enable cap-less LDOs (1.5V) */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
			   0x1055, 0x1055);
	/* Enable NV regulator (-1.2V) */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON15, 0x0001);
	usleep_range(100, 120);

	/* Disable AUD_ZCD */
	hp_zcd_disable(priv);

	/* Disable headphone short-circuit protection */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x3000);

	/* Enable IBIST */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON12, 0x0055);

	/* Set HP DR bias current optimization, 010: 6uA */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON11, 0x4900);
	/* Set HP & ZCD bias current optimization */
	/* 01: ZCD: 4uA, HP/HS/LO: 5uA */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON12, 0x0055);
	/* Set HPP/N STB enhance circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON2, 0x4033);

	/* Disable Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, false);

	/* Enable HP driver bias circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x30c0);
	/* Enable HP driver core circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x30f0);
	/* Enable HP main CMFB loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0200);

	/* Select CMFB resistor bulk to AC mode */
	/* Selec HS/LO cap size (6.5pF default) */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON10, 0x0000);

	/* Enable HP main output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x0003);
	/* Enable HPR/L main output stage step by step */
	hp_main_output_ramp(priv, true);

	/* Set LO gain as minimum (~ -40dB) */
	regmap_write(priv->regmap, MT6358_ZCD_CON1, DL_GAIN_N_40DB_REG);
	/* apply volume setting */
	headset_volume_ramp(priv,
			    DL_GAIN_N_10DB,
			    priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL]);

	/* Set LO STB enhance circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON7, 0x0110);
	/* Enable LO driver bias circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON7, 0x0112);
	/* Enable LO driver core circuits */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON7, 0x0113);

	/* Set LOL gain to normal gain step by step */
	regmap_update_bits(priv->regmap, MT6358_ZCD_CON1,
			   RG_AUDLOLGAIN_MASK_SFT,
			   priv->ana_gain[AUDIO_ANALOG_VOLUME_LINEOUTL] <<
			   RG_AUDLOLGAIN_SFT);
	regmap_update_bits(priv->regmap, MT6358_ZCD_CON1,
			   RG_AUDLORGAIN_MASK_SFT,
			   priv->ana_gain[AUDIO_ANALOG_VOLUME_LINEOUTR] <<
			   RG_AUDLORGAIN_SFT);

	/* Enable AUD_CLK */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13, 0x1, 0x1);
	/* Enable Audio DAC  */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x30f9);
	/* Enable low-noise mode of DAC */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0201);
	/* Switch LOL MUX to audio DAC */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON7, 0x011b);
	/* Switch HPL/R MUX to Line-out */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x35f9);

	return 0;
}

static int mtk_hp_spk_disable(struct mt6358_priv *priv)
{
	/* HPR/HPL mux to open */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   0x0f00, 0x0000);
	/* LOL mux to open */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON7,
			   0x3 << 2, 0x0000);

	/* Disable Audio DAC */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   0x000f, 0x0000);

	/* Disable AUD_CLK */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13, 0x1, 0x0);

	/* decrease HPL/R gain to normal gain step by step */
	headset_volume_ramp(priv,
			    priv->ana_gain[AUDIO_ANALOG_VOLUME_HPOUTL],
			    DL_GAIN_N_40DB);

	/* decrease LOL gain to minimum gain step by step */
	regmap_update_bits(priv->regmap, MT6358_ZCD_CON1,
			   DL_GAIN_REG_MASK, DL_GAIN_N_40DB_REG);

	/* decrease HPR/L main output stage step by step */
	hp_main_output_ramp(priv, false);

	/* Disable HP main output stage */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3, 0x0);

	/* Short HP main output to HP aux output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3fc3);
	/* Enable HP aux output stage */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3fcf);

	/* Enable HP aux feedback loop */
	regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON1, 0x3fff);

	/* Reduce HP aux feedback loop gain */
	hp_aux_feedback_loop_gain_ramp(priv, false);

	/* Disable HP driver core circuits */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   0x3 << 4, 0x0);
	/* Disable LO driver core circuits */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON7,
			   0x1, 0x0);

	/* Disable HP driver bias circuits */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   0x3 << 6, 0x0);
	/* Disable LO driver bias circuits */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON7,
			   0x1 << 1, 0x0);

	/* Disable HP aux CMFB loop */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON9,
			   0xff << 8, 0x0000);

	/* Disable IBIST */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON12,
			   0x1 << 8, 0x1 << 8);
	/* Disable NV regulator (-1.2V) */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON15, 0x1, 0x0);
	/* Disable cap-less LDOs (1.5V) */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14, 0x1055, 0x0);
	/* Disable NCP */
	regmap_update_bits(priv->regmap, MT6358_AUDNCP_CLKDIV_CON3, 0x1, 0x1);

	/* Set HP CMFB gate rstb */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON4,
			   0x1 << 6, 0x0);
	/* disable Pull-down HPL/R to AVSS28_AUD */
	hp_pull_down(priv, false);

	return 0;
}

static int mt_hp_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol,
		       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);
	int device = DEVICE_HP;

	dev_info(priv->dev, "%s(), event 0x%x, dev_counter[DEV_HP] %d, mux %u\n",
		 __func__,
		 event,
		 priv->dev_counter[device],
		 mux);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		priv->dev_counter[device]++;
		if (priv->dev_counter[device] > 1)
			break;	/* already enabled, do nothing */
		else if (priv->dev_counter[device] <= 0)
			dev_warn(priv->dev, "%s(), dev_counter[DEV_HP] %d <= 0\n",
				 __func__,
				 priv->dev_counter[device]);

		priv->mux_select[MUX_HP_L] = mux;

		if (mux == HP_MUX_HP)
			mtk_hp_enable(priv);
		else if (mux == HP_MUX_HPSPK)
			mtk_hp_spk_enable(priv);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		priv->dev_counter[device]--;
		if (priv->dev_counter[device] > 0) {
			break;	/* still being used, don't close */
		} else if (priv->dev_counter[device] < 0) {
			dev_warn(priv->dev, "%s(), dev_counter[DEV_HP] %d < 0\n",
				 __func__,
				 priv->dev_counter[device]);
			priv->dev_counter[device] = 0;
			break;
		}

		if (priv->mux_select[MUX_HP_L] == HP_MUX_HP)
			mtk_hp_disable(priv);
		else if (priv->mux_select[MUX_HP_L] == HP_MUX_HPSPK)
			mtk_hp_spk_disable(priv);

		priv->mux_select[MUX_HP_L] = mux;
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
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_info(priv->dev, "%s(), event 0x%x, mux %u\n",
		 __func__,
		 event,
		 dapm_kcontrol_get_value(w->kcontrols[0]));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Reduce ESD resistance of AU_REFN */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON2, 0x4000);

		/* Turn on DA_600K_NCP_VA18 */
		regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON1, 0x0001);
		/* Set NCP clock as 604kHz // 26MHz/43 = 604KHz */
		regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON2, 0x002c);
		/* Toggle RG_DIVCKS_CHG */
		regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON0, 0x0001);
		/* Set NCP soft start mode as default mode: 100us */
		regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON4, 0x0003);
		/* Enable NCP */
		regmap_write(priv->regmap, MT6358_AUDNCP_CLKDIV_CON3, 0x0000);
		usleep_range(250, 270);

		/* Enable cap-less LDOs (1.5V) */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
				   0x1055, 0x1055);
		/* Enable NV regulator (-1.2V) */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON15, 0x0001);
		usleep_range(100, 120);

		/* Disable AUD_ZCD */
		hp_zcd_disable(priv);

		/* Disable handset short-circuit protection */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON6, 0x0010);

		/* Enable IBIST */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON12, 0x0055);
		/* Set HP DR bias current optimization, 010: 6uA */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON11, 0x4900);
		/* Set HP & ZCD bias current optimization */
		/* 01: ZCD: 4uA, HP/HS/LO: 5uA */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON12, 0x0055);
		/* Set HS STB enhance circuits */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON6, 0x0090);

		/* Disable HP main CMFB loop */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0000);
		/* Select CMFB resistor bulk to AC mode */
		/* Selec HS/LO cap size (6.5pF default) */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON10, 0x0000);

		/* Enable HS driver bias circuits */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON6, 0x0092);
		/* Enable HS driver core circuits */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON6, 0x0093);

		/* Enable AUD_CLK */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13,
				   0x1, 0x1);

		/* Enable Audio DAC  */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON0, 0x0009);
		/* Enable low-noise mode of DAC */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON9, 0x0001);
		/* Switch HS MUX to audio DAC */
		regmap_write(priv->regmap, MT6358_AUDDEC_ANA_CON6, 0x009b);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* HS mux to open */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON6,
				   RG_AUDHSMUXINPUTSEL_VAUDP15_MASK_SFT,
				   RCV_MUX_OPEN);

		/* Disable Audio DAC */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
				   0x000f, 0x0000);

		/* Disable AUD_CLK */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13,
				   0x1, 0x0);

		/* decrease HS gain to minimum gain step by step */
		regmap_write(priv->regmap, MT6358_ZCD_CON3, DL_GAIN_N_40DB);

		/* Disable HS driver core circuits */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON6,
				   0x1, 0x0);

		/* Disable HS driver bias circuits */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON6,
				   0x1 << 1, 0x0000);

		/* Disable HP aux CMFB loop */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON9,
				   0xff << 8, 0x0);

		/* Enable HP main CMFB Switch */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON9,
				   0xff << 8, 0x2 << 8);

		/* Disable IBIST */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON12,
				   0x1 << 8, 0x1 << 8);

		/* Disable NV regulator (-1.2V) */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON15,
				   0x1, 0x0);
		/* Disable cap-less LDOs (1.5V) */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
				   0x1055, 0x0);
		/* Disable NCP */
		regmap_update_bits(priv->regmap, MT6358_AUDNCP_CLKDIV_CON3,
				   0x1, 0x1);
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
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event 0x%x, rate %d\n",
		__func__, event, priv->ul_rate);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		capture_gpio_set(priv);
		break;
	case SND_SOC_DAPM_POST_PMD:
		capture_gpio_reset(priv);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_adc_supply_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol,
			       int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(priv->dev, "%s(), event 0x%x\n",
		__func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable audio ADC CLKGEN  */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13,
				   0x1 << 5, 0x1 << 5);
		/* ADC CLK from CLKGEN (13MHz) */
		regmap_write(priv->regmap, MT6358_AUDENC_ANA_CON3,
			     0x0000);
		/* Enable  LCLDO_ENC 1P8V */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
				   0x2500, 0x0100);
		/* LCLDO_ENC remote sense */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
				   0x2500, 0x2500);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* LCLDO_ENC remote sense off */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
				   0x2500, 0x0100);
		/* disable LCLDO_ENC 1P8V */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON14,
				   0x2500, 0x0000);

		/* ADC CLK from CLKGEN (13MHz) */
		regmap_write(priv->regmap, MT6358_AUDENC_ANA_CON3, 0x0000);
		/* disable audio ADC CLKGEN  */
		regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON13,
				   0x1 << 5, 0x0 << 5);
		break;
	default:
		break;
	}

	return 0;
}

static int mt6358_amic_enable(struct mt6358_priv *priv)
{
	unsigned int mic_type = priv->mux_select[MUX_MIC_TYPE];
	unsigned int mux_pga_l = priv->mux_select[MUX_PGA_L];
	unsigned int mux_pga_r = priv->mux_select[MUX_PGA_R];

	dev_info(priv->dev, "%s(), mux, mic %u, pga l %u, pga r %u\n",
		 __func__, mic_type, mux_pga_l, mux_pga_r);

	if (IS_DCC_BASE(mic_type)) {
		/* DCC 50k CLK (from 26M) */
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG0, 0x2062);
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG0, 0x2062);
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG0, 0x2060);
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG0, 0x2061);
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG1, 0x0100);
	}

	/* mic bias 0 */
	if (mux_pga_l == PGA_MUX_AIN0 || mux_pga_l == PGA_MUX_AIN2 ||
	    mux_pga_r == PGA_MUX_AIN0 || mux_pga_r == PGA_MUX_AIN2) {
		switch (mic_type) {
		case MIC_TYPE_MUX_DCC_ECM_DIFF:
			regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON9,
					   0xff00, 0x7700);
			break;
		case MIC_TYPE_MUX_DCC_ECM_SINGLE:
			regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON9,
					   0xff00, 0x1100);
			break;
		default:
			regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON9,
					   0xff00, 0x0000);
			break;
		}
		/* Enable MICBIAS0, MISBIAS0 = 1P9V */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON9,
				   0xff, 0x21);
	}

	/* mic bias 1 */
	if (mux_pga_l == PGA_MUX_AIN1 || mux_pga_r == PGA_MUX_AIN1) {
		/* Enable MICBIAS1, MISBIAS1 = 2P6V */
		if (mic_type == MIC_TYPE_MUX_DCC_ECM_SINGLE)
			regmap_write(priv->regmap,
				     MT6358_AUDENC_ANA_CON10, 0x0161);
		else
			regmap_write(priv->regmap,
				     MT6358_AUDENC_ANA_CON10, 0x0061);
	}

	if (IS_DCC_BASE(mic_type)) {
		/* Audio L/R preamplifier DCC precharge */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
				   0xf8ff, 0x0004);
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
				   0xf8ff, 0x0004);
	} else {
		/* reset reg */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
				   0xf8ff, 0x0000);
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
				   0xf8ff, 0x0000);
	}

	if (mux_pga_l != PGA_MUX_NONE) {
		/* L preamplifier input sel */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
				   RG_AUDPREAMPLINPUTSEL_MASK_SFT,
				   mux_pga_l << RG_AUDPREAMPLINPUTSEL_SFT);

		/* L preamplifier enable */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
				   RG_AUDPREAMPLON_MASK_SFT,
				   0x1 << RG_AUDPREAMPLON_SFT);

		if (IS_DCC_BASE(mic_type)) {
			/* L preamplifier DCCEN */
			regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
					   RG_AUDPREAMPLDCCEN_MASK_SFT,
					   0x1 << RG_AUDPREAMPLDCCEN_SFT);
		}

		/* L ADC input sel : L PGA. Enable audio L ADC */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
				   RG_AUDADCLINPUTSEL_MASK_SFT,
				   ADC_MUX_PREAMPLIFIER <<
				   RG_AUDADCLINPUTSEL_SFT);
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
				   RG_AUDADCLPWRUP_MASK_SFT,
				   0x1 << RG_AUDADCLPWRUP_SFT);
	}

	if (mux_pga_r != PGA_MUX_NONE) {
		/* R preamplifier input sel */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
				   RG_AUDPREAMPRINPUTSEL_MASK_SFT,
				   mux_pga_r << RG_AUDPREAMPRINPUTSEL_SFT);

		/* R preamplifier enable */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
				   RG_AUDPREAMPRON_MASK_SFT,
				   0x1 << RG_AUDPREAMPRON_SFT);

		if (IS_DCC_BASE(mic_type)) {
			/* R preamplifier DCCEN */
			regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
					   RG_AUDPREAMPRDCCEN_MASK_SFT,
					   0x1 << RG_AUDPREAMPRDCCEN_SFT);
		}

		/* R ADC input sel : R PGA. Enable audio R ADC */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
				   RG_AUDADCRINPUTSEL_MASK_SFT,
				   ADC_MUX_PREAMPLIFIER <<
				   RG_AUDADCRINPUTSEL_SFT);
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
				   RG_AUDADCRPWRUP_MASK_SFT,
				   0x1 << RG_AUDADCRPWRUP_SFT);
	}

	if (IS_DCC_BASE(mic_type)) {
		usleep_range(100, 150);
		/* Audio L preamplifier DCC precharge off */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
				   RG_AUDPREAMPLDCPRECHARGE_MASK_SFT, 0x0);
		/* Audio R preamplifier DCC precharge off */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
				   RG_AUDPREAMPRDCPRECHARGE_MASK_SFT, 0x0);

		/* Short body to ground in PGA */
		regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON3,
				   0x1 << 12, 0x0);
	}

	/* here to set digital part */
	mt6358_mtkaif_tx_enable(priv);

	/* UL dmic setting off */
	regmap_write(priv->regmap, MT6358_AFE_UL_SRC_CON0_H, 0x0000);

	/* UL turn on */
	regmap_write(priv->regmap, MT6358_AFE_UL_SRC_CON0_L, 0x0001);

	return 0;
}

static void mt6358_amic_disable(struct mt6358_priv *priv)
{
	unsigned int mic_type = priv->mux_select[MUX_MIC_TYPE];
	unsigned int mux_pga_l = priv->mux_select[MUX_PGA_L];
	unsigned int mux_pga_r = priv->mux_select[MUX_PGA_R];

	dev_info(priv->dev, "%s(), mux, mic %u, pga l %u, pga r %u\n",
		 __func__, mic_type, mux_pga_l, mux_pga_r);

	/* UL turn off */
	regmap_update_bits(priv->regmap, MT6358_AFE_UL_SRC_CON0_L,
			   0x0001, 0x0000);

	/* disable aud_pad TX fifos */
	mt6358_mtkaif_tx_disable(priv);

	/* L ADC input sel : off, disable L ADC */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
			   0xf000, 0x0000);
	/* L preamplifier DCCEN */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
			   0x1 << 1, 0x0);
	/* L preamplifier input sel : off, L PGA 0 dB gain */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
			   0xfffb, 0x0000);

	/* disable L preamplifier DCC precharge */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
			   0x1 << 2, 0x0);

	/* R ADC input sel : off, disable R ADC */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
			   0xf000, 0x0000);
	/* R preamplifier DCCEN */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
			   0x1 << 1, 0x0);
	/* R preamplifier input sel : off, R PGA 0 dB gain */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
			   0x0ffb, 0x0000);

	/* disable R preamplifier DCC precharge */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
			   0x1 << 2, 0x0);

	/* mic bias */
	/* Disable MICBIAS0, MISBIAS0 = 1P7V */
	regmap_write(priv->regmap, MT6358_AUDENC_ANA_CON9, 0x0000);

	/* Disable MICBIAS1 */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON10,
			   0x0001, 0x0000);

	if (IS_DCC_BASE(mic_type)) {
		/* dcclk_gen_on=1'b0 */
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG0, 0x2060);
		/* dcclk_pdn=1'b1 */
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG0, 0x2062);
		/* dcclk_ref_ck_sel=2'b00 */
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG0, 0x2062);
		/* dcclk_div=11'b00100000011 */
		regmap_write(priv->regmap, MT6358_AFE_DCCLK_CFG0, 0x2062);
	}
}

static int mt6358_dmic_enable(struct mt6358_priv *priv)
{
	dev_info(priv->dev, "%s()\n", __func__);

	/* mic bias */
	/* Enable MICBIAS0, MISBIAS0 = 1P9V */
	regmap_write(priv->regmap, MT6358_AUDENC_ANA_CON9, 0x0021);

	/* RG_BANDGAPGEN=1'b0 */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON10,
			   0x1 << 12, 0x0);

	/* DMIC enable */
	regmap_write(priv->regmap, MT6358_AUDENC_ANA_CON8, 0x0005);

	/* here to set digital part */
	mt6358_mtkaif_tx_enable(priv);

	/* UL dmic setting */
	regmap_write(priv->regmap, MT6358_AFE_UL_SRC_CON0_H, 0x0080);

	/* UL turn on */
	regmap_write(priv->regmap, MT6358_AFE_UL_SRC_CON0_L, 0x0003);

	/* Prevent pop noise form dmic hw */
	msleep(100);

	return 0;
}

static void mt6358_dmic_disable(struct mt6358_priv *priv)
{
	dev_info(priv->dev, "%s()\n", __func__);

	/* UL turn off */
	regmap_update_bits(priv->regmap, MT6358_AFE_UL_SRC_CON0_L,
			   0x0003, 0x0000);

	/* disable aud_pad TX fifos */
	mt6358_mtkaif_tx_disable(priv);

	/* DMIC disable */
	regmap_write(priv->regmap, MT6358_AUDENC_ANA_CON8, 0x0000);

	/* mic bias */
	/* MISBIAS0 = 1P7V */
	regmap_write(priv->regmap, MT6358_AUDENC_ANA_CON9, 0x0001);

	/* RG_BANDGAPGEN=1'b0 */
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON10,
			   0x1 << 12, 0x0);

	/* MICBIA0 disable */
	regmap_write(priv->regmap, MT6358_AUDENC_ANA_CON9, 0x0000);
}

static void mt6358_restore_pga(struct mt6358_priv *priv)
{
	unsigned int gain_l, gain_r;

	gain_l = priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP1];
	gain_r = priv->ana_gain[AUDIO_ANALOG_VOLUME_MICAMP2];

	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON0,
			   RG_AUDPREAMPLGAIN_MASK_SFT,
			   gain_l << RG_AUDPREAMPLGAIN_SFT);
	regmap_update_bits(priv->regmap, MT6358_AUDENC_ANA_CON1,
			   RG_AUDPREAMPRGAIN_MASK_SFT,
			   gain_r << RG_AUDPREAMPRGAIN_SFT);
}

static int mt_mic_type_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);

	dev_dbg(priv->dev, "%s(), event 0x%x, mux %u\n",
		__func__, event, mux);

	switch (event) {
	case SND_SOC_DAPM_WILL_PMU:
		priv->mux_select[MUX_MIC_TYPE] = mux;
		break;
	case SND_SOC_DAPM_PRE_PMU:
		switch (mux) {
		case MIC_TYPE_MUX_DMIC:
			mt6358_dmic_enable(priv);
			break;
		default:
			mt6358_amic_enable(priv);
			break;
		}
		mt6358_restore_pga(priv);

		break;
	case SND_SOC_DAPM_POST_PMD:
		switch (priv->mux_select[MUX_MIC_TYPE]) {
		case MIC_TYPE_MUX_DMIC:
			mt6358_dmic_disable(priv);
			break;
		default:
			mt6358_amic_disable(priv);
			break;
		}

		priv->mux_select[MUX_MIC_TYPE] = mux;
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
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);

	dev_dbg(priv->dev, "%s(), event = 0x%x, mux %u\n",
		__func__, event, mux);

	priv->mux_select[MUX_ADC_L] = mux;

	return 0;
}

static int mt_adc_r_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);

	dev_dbg(priv->dev, "%s(), event = 0x%x, mux %u\n",
		__func__, event, mux);

	priv->mux_select[MUX_ADC_R] = mux;

	return 0;
}

static int mt_pga_left_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);

	dev_dbg(priv->dev, "%s(), event = 0x%x, mux %u\n",
		__func__, event, mux);

	priv->mux_select[MUX_PGA_L] = mux;

	return 0;
}

static int mt_pga_right_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);

	dev_dbg(priv->dev, "%s(), event = 0x%x, mux %u\n",
		__func__, event, mux);

	priv->mux_select[MUX_PGA_R] = mux;

	return 0;
}

static int mt_delay_250_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(250, 270);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		usleep_range(250, 270);
		break;
	default:
		break;
	}

	return 0;
}

/* DAPM Widgets */
static const struct snd_soc_dapm_widget mt6358_dapm_widgets[] = {
	/* Global Supply*/
	SND_SOC_DAPM_SUPPLY_S("CLK_BUF", SUPPLY_SEQ_CLK_BUF,
			      MT6358_DCXO_CW14,
			      RG_XO_AUDIO_EN_M_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDGLB", SUPPLY_SEQ_AUD_GLB,
			      MT6358_AUDDEC_ANA_CON13,
			      RG_AUDGLB_PWRDN_VA28_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("CLKSQ Audio", SUPPLY_SEQ_CLKSQ,
			      MT6358_AUDENC_ANA_CON6,
			      RG_CLKSQ_EN_SFT, 0,
			      mt_clksq_event,
			      SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY_S("AUDNCP_CK", SUPPLY_SEQ_TOP_CK,
			      MT6358_AUD_TOP_CKPDN_CON0,
			      RG_AUDNCP_CK_PDN_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ZCD13M_CK", SUPPLY_SEQ_TOP_CK,
			      MT6358_AUD_TOP_CKPDN_CON0,
			      RG_ZCD13M_CK_PDN_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUD_CK", SUPPLY_SEQ_TOP_CK_LAST,
			      MT6358_AUD_TOP_CKPDN_CON0,
			      RG_AUD_CK_PDN_SFT, 1,
			      mt_delay_250_event,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY_S("AUDIF_CK", SUPPLY_SEQ_TOP_CK,
			      MT6358_AUD_TOP_CKPDN_CON0,
			      RG_AUDIF_CK_PDN_SFT, 1, NULL, 0),

	/* Digital Clock */
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_AFE_CTL", SUPPLY_SEQ_AUD_TOP_LAST,
			      MT6358_AUDIO_TOP_CON0,
			      PDN_AFE_CTL_SFT, 1,
			      mt_delay_250_event,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_DAC_CTL", SUPPLY_SEQ_AUD_TOP,
			      MT6358_AUDIO_TOP_CON0,
			      PDN_DAC_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_ADC_CTL", SUPPLY_SEQ_AUD_TOP,
			      MT6358_AUDIO_TOP_CON0,
			      PDN_ADC_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_I2S_DL", SUPPLY_SEQ_AUD_TOP,
			      MT6358_AUDIO_TOP_CON0,
			      PDN_I2S_DL_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PWR_CLK", SUPPLY_SEQ_AUD_TOP,
			      MT6358_AUDIO_TOP_CON0,
			      PWR_CLK_DIS_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PDN_AFE_TESTMODEL", SUPPLY_SEQ_AUD_TOP,
			      MT6358_AUDIO_TOP_CON0,
			      PDN_AFE_TESTMODEL_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PDN_RESERVED", SUPPLY_SEQ_AUD_TOP,
			      MT6358_AUDIO_TOP_CON0,
			      PDN_RESERVED_SFT, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DL Digital Clock", SND_SOC_NOPM,
			    0, 0, NULL, 0),

	/* AFE ON */
	SND_SOC_DAPM_SUPPLY_S("AFE_ON", SUPPLY_SEQ_AFE,
			      MT6358_AFE_UL_DL_CON0, AFE_ON_SFT, 0,
			      NULL, 0),

	/* AIF Rx*/
	SND_SOC_DAPM_AIF_IN_E("AIF_RX", "AIF1 Playback", 0,
			      MT6358_AFE_DL_SRC2_CON0_L,
			      DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0,
			      mt_aif_in_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* DL Supply */
	SND_SOC_DAPM_SUPPLY("DL Power Supply", SND_SOC_NOPM,
			    0, 0, NULL, 0),

	/* DAC */
	SND_SOC_DAPM_MUX("DAC In Mux", SND_SOC_NOPM, 0, 0, &dac_in_mux_control),

	SND_SOC_DAPM_DAC("DACL", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DACR", NULL, SND_SOC_NOPM, 0, 0),

	/* LOL */
	SND_SOC_DAPM_MUX("LOL Mux", SND_SOC_NOPM, 0, 0, &lo_in_mux_control),

	SND_SOC_DAPM_SUPPLY("LO Stability Enh", MT6358_AUDDEC_ANA_CON7,
			    RG_LOOUTPUTSTBENH_VAUDP15_SFT, 0, NULL, 0),

	SND_SOC_DAPM_OUT_DRV("LOL Buffer", MT6358_AUDDEC_ANA_CON7,
			     RG_AUDLOLPWRUP_VAUDP15_SFT, 0, NULL, 0),

	/* Headphone */
	SND_SOC_DAPM_MUX_E("HPL Mux", SND_SOC_NOPM, 0, 0,
			   &hpl_in_mux_control,
			   mt_hp_event,
			   SND_SOC_DAPM_PRE_PMU |
			   SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MUX_E("HPR Mux", SND_SOC_NOPM, 0, 0,
			   &hpr_in_mux_control,
			   mt_hp_event,
			   SND_SOC_DAPM_PRE_PMU |
			   SND_SOC_DAPM_PRE_PMD),

	/* Receiver */
	SND_SOC_DAPM_MUX_E("RCV Mux", SND_SOC_NOPM, 0, 0,
			   &rcv_in_mux_control,
			   mt_rcv_event,
			   SND_SOC_DAPM_PRE_PMU |
			   SND_SOC_DAPM_PRE_PMD),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("Receiver"),
	SND_SOC_DAPM_OUTPUT("Headphone L"),
	SND_SOC_DAPM_OUTPUT("Headphone R"),
	SND_SOC_DAPM_OUTPUT("Headphone L Ext Spk Amp"),
	SND_SOC_DAPM_OUTPUT("Headphone R Ext Spk Amp"),
	SND_SOC_DAPM_OUTPUT("LINEOUT L"),
	SND_SOC_DAPM_OUTPUT("LINEOUT L HSSPK"),

	/* SGEN */
	SND_SOC_DAPM_SUPPLY("SGEN DL Enable", MT6358_AFE_SGEN_CFG0,
			    SGEN_DAC_EN_CTL_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SGEN MUTE", MT6358_AFE_SGEN_CFG0,
			    SGEN_MUTE_SW_CTL_SFT, 1,
			    mt_sgen_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("SGEN DL SRC", MT6358_AFE_DL_SRC2_CON0_L,
			    DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("SGEN DL"),

	/* Uplinks */
	SND_SOC_DAPM_AIF_OUT_E("AIF1TX", "AIF1 Capture", 0,
			       SND_SOC_NOPM, 0, 0,
			       mt_aif_out_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("ADC Supply", SUPPLY_SEQ_ADC_SUPPLY,
			      SND_SOC_NOPM, 0, 0,
			      mt_adc_supply_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* Uplinks MUX */
	SND_SOC_DAPM_MUX("AIF Out Mux", SND_SOC_NOPM, 0, 0,
			 &aif_out_mux_control),

	SND_SOC_DAPM_MUX_E("Mic Type Mux", SND_SOC_NOPM, 0, 0,
			   &mic_type_mux_control,
			   mt_mic_type_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD |
			   SND_SOC_DAPM_WILL_PMU),

	SND_SOC_DAPM_MUX_E("ADC L Mux", SND_SOC_NOPM, 0, 0,
			   &adc_left_mux_control,
			   mt_adc_l_event,
			   SND_SOC_DAPM_WILL_PMU),
	SND_SOC_DAPM_MUX_E("ADC R Mux", SND_SOC_NOPM, 0, 0,
			   &adc_right_mux_control,
			   mt_adc_r_event,
			   SND_SOC_DAPM_WILL_PMU),

	SND_SOC_DAPM_ADC("ADC L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC R", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MUX_E("PGA L Mux", SND_SOC_NOPM, 0, 0,
			   &pga_left_mux_control,
			   mt_pga_left_event,
			   SND_SOC_DAPM_WILL_PMU),
	SND_SOC_DAPM_MUX_E("PGA R Mux", SND_SOC_NOPM, 0, 0,
			   &pga_right_mux_control,
			   mt_pga_right_event,
			   SND_SOC_DAPM_WILL_PMU),

	SND_SOC_DAPM_PGA("PGA L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PGA R", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* UL input */
	SND_SOC_DAPM_INPUT("AIN0"),
	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),
};

static const struct snd_soc_dapm_route mt6358_dapm_routes[] = {
	/* Capture */
	{"AIF1TX", NULL, "AIF Out Mux"},
	{"AIF1TX", NULL, "CLK_BUF"},
	{"AIF1TX", NULL, "AUDGLB"},
	{"AIF1TX", NULL, "CLKSQ Audio"},

	{"AIF1TX", NULL, "AUD_CK"},
	{"AIF1TX", NULL, "AUDIF_CK"},

	{"AIF1TX", NULL, "AUDIO_TOP_AFE_CTL"},
	{"AIF1TX", NULL, "AUDIO_TOP_ADC_CTL"},
	{"AIF1TX", NULL, "AUDIO_TOP_PWR_CLK"},
	{"AIF1TX", NULL, "AUDIO_TOP_PDN_RESERVED"},
	{"AIF1TX", NULL, "AUDIO_TOP_I2S_DL"},

	{"AIF1TX", NULL, "AFE_ON"},

	{"AIF Out Mux", NULL, "Mic Type Mux"},

	{"Mic Type Mux", "ACC", "ADC L"},
	{"Mic Type Mux", "ACC", "ADC R"},
	{"Mic Type Mux", "DCC", "ADC L"},
	{"Mic Type Mux", "DCC", "ADC R"},
	{"Mic Type Mux", "DCC_ECM_DIFF", "ADC L"},
	{"Mic Type Mux", "DCC_ECM_DIFF", "ADC R"},
	{"Mic Type Mux", "DCC_ECM_SINGLE", "ADC L"},
	{"Mic Type Mux", "DCC_ECM_SINGLE", "ADC R"},
	{"Mic Type Mux", "DMIC", "AIN0"},
	{"Mic Type Mux", "DMIC", "AIN2"},

	{"ADC L", NULL, "ADC L Mux"},
	{"ADC L", NULL, "ADC Supply"},
	{"ADC R", NULL, "ADC R Mux"},
	{"ADC R", NULL, "ADC Supply"},

	{"ADC L Mux", "Left Preamplifier", "PGA L"},

	{"ADC R Mux", "Right Preamplifier", "PGA R"},

	{"PGA L", NULL, "PGA L Mux"},
	{"PGA R", NULL, "PGA R Mux"},

	{"PGA L Mux", "AIN0", "AIN0"},
	{"PGA L Mux", "AIN1", "AIN1"},
	{"PGA L Mux", "AIN2", "AIN2"},

	{"PGA R Mux", "AIN0", "AIN0"},
	{"PGA R Mux", "AIN1", "AIN1"},
	{"PGA R Mux", "AIN2", "AIN2"},

	/* DL Supply */
	{"DL Power Supply", NULL, "CLK_BUF"},
	{"DL Power Supply", NULL, "AUDGLB"},
	{"DL Power Supply", NULL, "CLKSQ Audio"},

	{"DL Power Supply", NULL, "AUDNCP_CK"},
	{"DL Power Supply", NULL, "ZCD13M_CK"},
	{"DL Power Supply", NULL, "AUD_CK"},
	{"DL Power Supply", NULL, "AUDIF_CK"},

	/* DL Digital Supply */
	{"DL Digital Clock", NULL, "AUDIO_TOP_AFE_CTL"},
	{"DL Digital Clock", NULL, "AUDIO_TOP_DAC_CTL"},
	{"DL Digital Clock", NULL, "AUDIO_TOP_PWR_CLK"},

	{"DL Digital Clock", NULL, "AFE_ON"},

	{"AIF_RX", NULL, "DL Digital Clock"},

	/* DL Path */
	{"DAC In Mux", "Normal Path", "AIF_RX"},

	{"DAC In Mux", "Sgen", "SGEN DL"},
	{"SGEN DL", NULL, "SGEN DL SRC"},
	{"SGEN DL", NULL, "SGEN MUTE"},
	{"SGEN DL", NULL, "SGEN DL Enable"},
	{"SGEN DL", NULL, "DL Digital Clock"},
	{"SGEN DL", NULL, "AUDIO_TOP_PDN_AFE_TESTMODEL"},

	{"DACL", NULL, "DAC In Mux"},
	{"DACL", NULL, "DL Power Supply"},

	{"DACR", NULL, "DAC In Mux"},
	{"DACR", NULL, "DL Power Supply"},

	/* Lineout Path */
	{"LOL Mux", "Playback", "DACL"},

	{"LOL Buffer", NULL, "LOL Mux"},
	{"LOL Buffer", NULL, "LO Stability Enh"},

	{"LINEOUT L", NULL, "LOL Buffer"},

	/* Headphone Path */
	{"HPL Mux", "Audio Playback", "DACL"},
	{"HPR Mux", "Audio Playback", "DACR"},
	{"HPL Mux", "HP Impedance", "DACL"},
	{"HPR Mux", "HP Impedance", "DACR"},
	{"HPL Mux", "LoudSPK Playback", "DACL"},
	{"HPR Mux", "LoudSPK Playback", "DACR"},

	{"Headphone L", NULL, "HPL Mux"},
	{"Headphone R", NULL, "HPR Mux"},
	{"Headphone L Ext Spk Amp", NULL, "HPL Mux"},
	{"Headphone R Ext Spk Amp", NULL, "HPR Mux"},
	{"LINEOUT L HSSPK", NULL, "HPL Mux"},

	/* Receiver Path */
	{"RCV Mux", "Voice Playback", "DACL"},
	{"Receiver", NULL, "RCV Mux"},
};

static int mt6358_codec_dai_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *cmpnt = dai->component;
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int rate = params_rate(params);

	dev_info(priv->dev, "%s(), substream->stream %d, rate %d, number %d\n",
		 __func__,
		 substream->stream,
		 rate,
		 substream->number);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		priv->dl_rate = rate;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		priv->ul_rate = rate;

	return 0;
}

static const struct snd_soc_dai_ops mt6358_codec_dai_ops = {
	.hw_params = mt6358_codec_dai_hw_params,
};

#define MT6358_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
			SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_U16_BE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |\
			SNDRV_PCM_FMTBIT_U24_LE | SNDRV_PCM_FMTBIT_U24_BE |\
			SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |\
			SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_U32_BE)

static struct snd_soc_dai_driver mt6358_dai_driver[] = {
	{
		.name = "mt6358-snd-codec-aif1",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000 |
				 SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = MT6358_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_48000,
			.formats = MT6358_FORMATS,
		},
		.ops = &mt6358_codec_dai_ops,
	},
};

static void mt6358_codec_init_reg(struct mt6358_priv *priv)
{
	/* Disable HeadphoneL/HeadphoneR short circuit protection */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   RG_AUDHPLSCDISABLE_VAUDP15_MASK_SFT,
			   0x1 << RG_AUDHPLSCDISABLE_VAUDP15_SFT);
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON0,
			   RG_AUDHPRSCDISABLE_VAUDP15_MASK_SFT,
			   0x1 << RG_AUDHPRSCDISABLE_VAUDP15_SFT);
	/* Disable voice short circuit protection */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON6,
			   RG_AUDHSSCDISABLE_VAUDP15_MASK_SFT,
			   0x1 << RG_AUDHSSCDISABLE_VAUDP15_SFT);
	/* disable LO buffer left short circuit protection */
	regmap_update_bits(priv->regmap, MT6358_AUDDEC_ANA_CON7,
			   RG_AUDLOLSCDISABLE_VAUDP15_MASK_SFT,
			   0x1 << RG_AUDLOLSCDISABLE_VAUDP15_SFT);

	/* accdet s/w enable */
	regmap_update_bits(priv->regmap, MT6358_ACCDET_CON13,
			   0xFFFF, 0x700E);

	/* gpio miso driving set to 4mA */
	regmap_write(priv->regmap, MT6358_DRV_CON3, 0x8888);

	/* set gpio */
	playback_gpio_reset(priv);
	capture_gpio_reset(priv);
}

static int mt6358_codec_probe(struct snd_soc_component *cmpnt)
{
	struct mt6358_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int ret;

	snd_soc_component_init_regmap(cmpnt, priv->regmap);

	mt6358_codec_init_reg(priv);

	priv->avdd_reg = devm_regulator_get(priv->dev, "Avdd");
	if (IS_ERR(priv->avdd_reg)) {
		dev_err(priv->dev, "%s() have no Avdd supply", __func__);
		return PTR_ERR(priv->avdd_reg);
	}

	ret = regulator_enable(priv->avdd_reg);
	if (ret)
		return  ret;

	return 0;
}

static const struct snd_soc_component_driver mt6358_soc_component_driver = {
	.probe = mt6358_codec_probe,
	.controls = mt6358_snd_controls,
	.num_controls = ARRAY_SIZE(mt6358_snd_controls),
	.dapm_widgets = mt6358_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6358_dapm_widgets),
	.dapm_routes = mt6358_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6358_dapm_routes),
};

static int mt6358_platform_driver_probe(struct platform_device *pdev)
{
	struct mt6358_priv *priv;
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);

	priv = devm_kzalloc(&pdev->dev,
			    sizeof(struct mt6358_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, priv);

	priv->dev = &pdev->dev;

	priv->regmap = mt6397->regmap;
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	dev_info(priv->dev, "%s(), dev name %s\n",
		 __func__, dev_name(&pdev->dev));

	return devm_snd_soc_register_component(&pdev->dev,
				      &mt6358_soc_component_driver,
				      mt6358_dai_driver,
				      ARRAY_SIZE(mt6358_dai_driver));
}

static const struct of_device_id mt6358_of_match[] = {
	{.compatible = "mediatek,mt6358-sound",},
	{}
};
MODULE_DEVICE_TABLE(of, mt6358_of_match);

static struct platform_driver mt6358_platform_driver = {
	.driver = {
		.name = "mt6358-sound",
		.of_match_table = mt6358_of_match,
	},
	.probe = mt6358_platform_driver_probe,
};

module_platform_driver(mt6358_platform_driver)

/* Module information */
MODULE_DESCRIPTION("MT6358 ALSA SoC codec driver");
MODULE_AUTHOR("KaiChieh Chuang <kaichieh.chuang@mediatek.com>");
MODULE_LICENSE("GPL v2");
