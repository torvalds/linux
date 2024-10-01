// SPDX-License-Identifier: GPL-2.0
/*
 * MT6357 ALSA SoC audio codec driver
 *
 * Copyright (c) 2024 Baylibre
 * Author: Nicolas Belin <nbelin@baylibre.com>
 */

#include <linux/dma-mapping.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/regulator/consumer.h>

#include "mt6357.h"

static void set_playback_gpio(struct mt6357_priv *priv, bool enable)
{
	regmap_write(priv->regmap, MT6357_GPIO_MODE2_CLR, MT6357_GPIO_MODE2_CLEAR_ALL);
	if (enable) {
		/* set gpio mosi mode */
		regmap_write(priv->regmap, MT6357_GPIO_MODE2_SET,
			     MT6357_GPIO8_MODE_SET_AUD_CLK_MOSI |
			     MT6357_GPIO9_MODE_SET_AUD_DAT_MOSI0 |
			     MT6357_GPIO10_MODE_SET_AUD_DAT_MOSI1 |
			     MT6357_GPIO11_MODE_SET_AUD_SYNC_MOSI);
	} else {
		/* pad_aud_*_mosi are GPIO mode after clear and set them to dir input
		 * reason:
		 * pad_aud_dat_mosi*, because the pin is used as boot strap
		 */
		regmap_update_bits(priv->regmap, MT6357_GPIO_DIR0,
				   MT6357_GPIO8_DIR_MASK |
				   MT6357_GPIO9_DIR_MASK |
				   MT6357_GPIO10_DIR_MASK |
				   MT6357_GPIO11_DIR_MASK,
				   MT6357_GPIO8_DIR_INPUT |
				   MT6357_GPIO9_DIR_INPUT |
				   MT6357_GPIO10_DIR_INPUT |
				   MT6357_GPIO11_DIR_INPUT);
	}
}

static void set_capture_gpio(struct mt6357_priv *priv, bool enable)
{
	regmap_write(priv->regmap, MT6357_GPIO_MODE3_CLR, MT6357_GPIO_MODE3_CLEAR_ALL);
	if (enable) {
		/* set gpio miso mode */
		regmap_write(priv->regmap, MT6357_GPIO_MODE3_SET,
			     MT6357_GPIO12_MODE_SET_AUD_CLK_MISO |
			     MT6357_GPIO13_MODE_SET_AUD_DAT_MISO0 |
			     MT6357_GPIO14_MODE_SET_AUD_DAT_MISO1 |
			     MT6357_GPIO15_MODE_SET_AUD_SYNC_MISO);
	} else {
		/* pad_aud_*_mosi are GPIO mode after clear and set them to dir input
		 * reason:
		 * pad_aud_clk_miso, because when playback only the miso_clk
		 * will also have 26m, so will have power leak
		 * pad_aud_dat_miso*, because the pin is used as boot strap
		 */
		regmap_update_bits(priv->regmap, MT6357_GPIO_DIR0,
				   MT6357_GPIO12_DIR_MASK |
				   MT6357_GPIO13_DIR_MASK |
				   MT6357_GPIO14_DIR_MASK |
				   MT6357_GPIO15_DIR_MASK,
				   MT6357_GPIO12_DIR_INPUT |
				   MT6357_GPIO13_DIR_INPUT |
				   MT6357_GPIO14_DIR_INPUT |
				   MT6357_GPIO15_DIR_INPUT);
	}
}

static void hp_main_output_ramp(struct mt6357_priv *priv, bool up)
{
	int i, stage;

	/* Enable/Reduce HPL/R main output stage step by step */
	for (i = 0; i <= MT6357_HPLOUT_STG_CTRL_VAUDP15_MAX; i++) {
		stage = up ? i : MT6357_HPLOUT_STG_CTRL_VAUDP15_MAX - i;
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPLOUT_STG_CTRL_VAUDP15_MASK,
				   stage << MT6357_HPLOUT_STG_CTRL_VAUDP15_SFT);
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPROUT_STG_CTRL_VAUDP15_MASK,
				   stage << MT6357_HPROUT_STG_CTRL_VAUDP15_SFT);
		usleep_range(600, 700);
	}
}

static void hp_aux_feedback_loop_gain_ramp(struct mt6357_priv *priv, bool up)
{
	int i, stage;

	/* Reduce HP aux feedback loop gain step by step */
	for (i = 0; i <= MT6357_HP_AUX_LOOP_GAIN_MAX; i++) {
		stage = up ? i : MT6357_HP_AUX_LOOP_GAIN_MAX - i;
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_HP_AUX_LOOP_GAIN_MASK,
				   stage << MT6357_HP_AUX_LOOP_GAIN_SFT);
		usleep_range(600, 700);
	}
}

static void hp_pull_down(struct mt6357_priv *priv, bool enable)
{
	if (enable)
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON2,
				   MT6357_HPP_SHORT_2VCM_VAUDP15_MASK,
				   MT6357_HPP_SHORT_2VCM_VAUDP15_ENABLE);
	else
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON2,
				   MT6357_HPP_SHORT_2VCM_VAUDP15_MASK,
				   MT6357_HPP_SHORT_2VCM_VAUDP15_DISABLE);
}

static bool is_valid_hp_pga_idx(int reg_idx)
{
	return (reg_idx >= DL_GAIN_8DB && reg_idx <= DL_GAIN_N_12DB) || reg_idx == DL_GAIN_N_40DB;
}

static void volume_ramp(struct mt6357_priv *priv, int lfrom, int lto,
			int rfrom, int rto, unsigned int reg_addr)
{
	int lcount, rcount, sleep = 0;

	if (!is_valid_hp_pga_idx(lfrom) || !is_valid_hp_pga_idx(lto))
		pr_debug("%s(), invalid left volume index, from %d, to %d\n",
			 __func__, lfrom, lto);

	if (!is_valid_hp_pga_idx(rfrom) || !is_valid_hp_pga_idx(rto))
		pr_debug("%s(), invalid right volume index, from %d, to %d\n",
			 __func__, rfrom, rto);

	if (lto > lfrom)
		lcount = 1;
	else
		lcount = -1;

	if (rto > rfrom)
		rcount = 1;
	else
		rcount = -1;

	while ((lto != lfrom) || (rto != rfrom)) {
		if (lto != lfrom) {
			lfrom += lcount;
			if (is_valid_hp_pga_idx(lfrom)) {
				regmap_update_bits(priv->regmap, reg_addr,
						   MT6357_DL_GAIN_REG_LEFT_MASK,
						   lfrom << MT6357_DL_GAIN_REG_LEFT_SHIFT);
				sleep = 1;
			}
		}
		if (rto != rfrom) {
			rfrom += rcount;
			if (is_valid_hp_pga_idx(rfrom)) {
				regmap_update_bits(priv->regmap, reg_addr,
						   MT6357_DL_GAIN_REG_RIGHT_MASK,
						   rfrom << MT6357_DL_GAIN_REG_RIGHT_SHIFT);
				sleep = 1;
			}
		}
		if (sleep)
			usleep_range(200, 300);
	}
}

static void lo_volume_ramp(struct mt6357_priv *priv, int lfrom, int lto, int rfrom, int rto)
{
	volume_ramp(priv, lfrom, lto, rfrom, rto, MT6357_ZCD_CON1);
}

static void hp_volume_ramp(struct mt6357_priv *priv, int lfrom, int lto, int rfrom, int rto)
{
	volume_ramp(priv, lfrom, lto, rfrom, rto, MT6357_ZCD_CON2);
}

static void hs_volume_ramp(struct mt6357_priv *priv, int from, int to)
{
	volume_ramp(priv, from, to, 0, 0, MT6357_ZCD_CON3);
}

/* Volume and channel swap controls */
static const DECLARE_TLV_DB_SCALE(playback_tlv, -1000, 100, 0);
static const DECLARE_TLV_DB_SCALE(capture_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(hp_degain_tlv, -1200, 1200, 0);

static const struct snd_kcontrol_new mt6357_controls[] = {
	/* dl pga gain */
	SOC_DOUBLE_TLV("Headphone Volume",
		       MT6357_ZCD_CON2, MT6357_AUD_HPL_GAIN_SFT,
		       MT6357_AUD_HPR_GAIN_SFT, MT6357_AUD_HP_GAIN_MAX,
		       1, playback_tlv),
	SOC_SINGLE_TLV("Headphone Vin Volume",
		       MT6357_AUDDEC_ANA_CON7, MT6357_HP_IVBUF_DEGAIN_SFT,
		       MT6357_HP_IVBUF_DEGAIN_MAX, 1, hp_degain_tlv),
	SOC_DOUBLE_TLV("Lineout Volume",
		       MT6357_ZCD_CON1, MT6357_AUD_LOL_GAIN_SFT,
		       MT6357_AUD_LOR_GAIN_SFT, MT6357_AUD_LO_GAIN_MAX,
		       1, playback_tlv),
	SOC_SINGLE_TLV("Handset Volume",
		       MT6357_ZCD_CON3, MT6357_AUD_HS_GAIN_SFT,
		       MT6357_AUD_HS_GAIN_MAX, 1, playback_tlv),
	/* ul pga gain */
	SOC_DOUBLE_R_TLV("Mic Volume",
			 MT6357_AUDENC_ANA_CON0, MT6357_AUDENC_ANA_CON1,
			 MT6357_AUDPREAMPLGAIN_SFT, MT6357_AUDPREAMPLGAIN_MAX,
			 0, capture_tlv),
};

/* Uplink controls */

enum {
	MIC_TYPE_MUX_IDLE,
	MIC_TYPE_MUX_ACC,
	MIC_TYPE_MUX_DMIC,
	MIC_TYPE_MUX_DCC,
	MIC_TYPE_MUX_DCC_ECM_DIFF,
	MIC_TYPE_MUX_DCC_ECM_SINGLE,
	MIC_TYPE_MUX_LPBK,
	MIC_TYPE_MUX_SGEN,
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
	"Loopback",
	"Sine Generator",
};

static SOC_ENUM_SINGLE_DECL(mic_type_mux_map_enum, SND_SOC_NOPM,
			    0, mic_type_mux_map);

static const struct snd_kcontrol_new mic_type_mux_control =
	SOC_DAPM_ENUM("Mic Type Select", mic_type_mux_map_enum);

static const char * const pga_mux_map[] = {
	"None", "AIN0", "AIN1", "AIN2"
};

static SOC_ENUM_SINGLE_DECL(pga_left_mux_map_enum,
			    MT6357_AUDENC_ANA_CON0,
			    MT6357_AUDPREAMPLINPUTSEL_SFT,
			    pga_mux_map);

static const struct snd_kcontrol_new pga_left_mux_control =
	SOC_DAPM_ENUM("PGA L Select", pga_left_mux_map_enum);

static SOC_ENUM_SINGLE_DECL(pga_right_mux_map_enum,
			    MT6357_AUDENC_ANA_CON1,
			    MT6357_AUDPREAMPRINPUTSEL_SFT,
			    pga_mux_map);

static const struct snd_kcontrol_new pga_right_mux_control =
	SOC_DAPM_ENUM("PGA R Select", pga_right_mux_map_enum);

/* Downlink controls */
static const char * const hslo_mux_map[] = {
	"Open", "DACR", "Playback", "Test mode"
};

static SOC_ENUM_SINGLE_DECL(lo_mux_map_enum,
			    MT6357_AUDDEC_ANA_CON4,
			    MT6357_AUD_LOL_MUX_INPUT_VAUDP15_SFT,
			    hslo_mux_map);

static const struct snd_kcontrol_new lo_mux_control =
	SOC_DAPM_ENUM("Line out source", lo_mux_map_enum);

static SOC_ENUM_SINGLE_DECL(hs_mux_map_enum,
			    MT6357_AUDDEC_ANA_CON3,
			    MT6357_AUD_HS_MUX_INPUT_VAUDP15_SFT,
			    hslo_mux_map);

static const struct snd_kcontrol_new hs_mux_control =
	SOC_DAPM_ENUM("Handset source", hs_mux_map_enum);

static const char * const hplr_mux_map[] = {
	"Open", "Line Out", "DAC", "Handset"
};

static SOC_ENUM_SINGLE_DECL(hpr_mux_map_enum,
			    MT6357_AUDDEC_ANA_CON0,
			    MT6357_AUD_HPR_MUX_INPUT_VAUDP15_SFT,
			    hplr_mux_map);

static const struct snd_kcontrol_new hpr_mux_control =
	SOC_DAPM_ENUM("Headphone Right source", hpr_mux_map_enum);

static SOC_ENUM_SINGLE_DECL(hpl_mux_map_enum,
			    MT6357_AUDDEC_ANA_CON0,
			    MT6357_AUD_HPL_MUX_INPUT_VAUDP15_SFT,
			    hplr_mux_map);

static const struct snd_kcontrol_new hpl_mux_control =
	SOC_DAPM_ENUM("Headphone Left source", hpl_mux_map_enum);

static const char * const dac_mux_map[] = {
	"Normal Path", "Sine Generator"
};

static SOC_ENUM_SINGLE_DECL(dac_mux_map_enum,
			    MT6357_AFE_TOP_CON0,
			    MT6357_DL_SINE_ON_SFT,
			    dac_mux_map);

static const struct snd_kcontrol_new dac_mux_control =
	SOC_DAPM_ENUM("DAC Select", dac_mux_map_enum);

static int mt6357_set_dmic(struct mt6357_priv *priv, bool enable)
{
	if (enable) {
		/* DMIC enable */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON7,
				   MT6357_AUDDIGMICBIAS_MASK | MT6357_AUDDIGMICEN_MASK,
				   MT6357_AUDDIGMICBIAS_DEFAULT_VALUE | MT6357_AUDDIGMICEN_ENABLE);
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_MASK,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_ENABLE);
		/* UL dmic setting: dual mode */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_H,
				   MT6357_C_TWO_DIGITAL_MIC_CTL_MASK,
				   MT6357_C_TWO_DIGITAL_MIC_ENABLE);
		/* UL turn on SDM 3 level mode */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SDM_3_LEVEL_CTL_MASK,
				   MT6357_UL_SDM_3_LEVEL_SELECT);
		/* UL turn on */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SRC_ON_TMP_CTL_MASK,
				   MT6357_UL_SRC_ENABLE);
		/* Wait to avoid any pop noises */
		msleep(100);
	} else {
		/* UL turn off */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SRC_ON_TMP_CTL_MASK,
				   MT6357_UL_SRC_DISABLE);
		/* UL turn on SDM 3 level mode */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SDM_3_LEVEL_CTL_MASK,
				   MT6357_UL_SDM_3_LEVEL_DESELECT);
		/* disable aud_pad TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_MASK,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_DISABLE);
		/* UL dmic setting: dual mode */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_H,
				   MT6357_C_TWO_DIGITAL_MIC_CTL_MASK,
				   MT6357_C_TWO_DIGITAL_MIC_DISABLE);
		/* DMIC disable */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON7,
				   MT6357_AUDDIGMICBIAS_MASK | MT6357_AUDDIGMICEN_MASK,
				   MT6357_AUDDIGMICBIAS_OFF | MT6357_AUDDIGMICEN_DISABLE);
	}
	return 0;
}

static int mt6357_set_amic(struct mt6357_priv *priv, bool enable, unsigned int mic_type)
{
	if (enable) {
		if (IS_DCC_BASE(mic_type)) {
			regmap_update_bits(priv->regmap, MT6357_AFE_DCCLK_CFG0,
					   MT6357_DCCLK_DIV_MASK, MT6357_DCCLK_DIV_RUN_VALUE);
			regmap_update_bits(priv->regmap, MT6357_AFE_DCCLK_CFG0,
					   MT6357_DCCLK_PDN_MASK, MT6357_DCCLK_OUTPUT);
			regmap_update_bits(priv->regmap, MT6357_AFE_DCCLK_CFG0,
					   MT6357_DCCLK_GEN_ON_MASK, MT6357_DCCLK_GEN_ON);
			regmap_update_bits(priv->regmap, MT6357_AFE_DCCLK_CFG1,
					   MT6357_DCCLK_RESYNC_BYPASS_MASK,
					   MT6357_DCCLK_RESYNC_BYPASS);

			/* mic bias 0: set the correct DC couple*/
			switch (mic_type) {
			case MIC_TYPE_MUX_DCC_ECM_DIFF:
				regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON8,
						   MT6357_AUD_MICBIAS0_DC_MASK,
						   MT6357_AUD_MICBIAS0_DC_ENABLE_ALL);
				break;
			case MIC_TYPE_MUX_DCC_ECM_SINGLE:
				regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON8,
						   MT6357_AUD_MICBIAS0_DC_MASK,
						   MT6357_AUD_MICBIAS0_DC_ENABLE_P1);
				break;
			default:
				regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON8,
						   MT6357_AUD_MICBIAS0_DC_MASK,
						   MT6357_AUD_MICBIAS0_DC_DISABLE_ALL);
				break;
			}

			/* mic bias 1: set the correct DC couple */
			if (mic_type == MIC_TYPE_MUX_DCC_ECM_SINGLE)
				regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON9,
						   MT6357_AUD_MICBIAS1_DCSW1P_EN_MASK,
						   MT6357_AUD_MICBIAS1_DCSW1P_ENABLE);

			/* Audio L/R preamplifier DCC precharge */
			regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
					   MT6357_AUDPREAMPLDCPRECHARGE_MASK,
					   MT6357_AUDPREAMPLDCPRECHARGE_ENABLE);
			regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
					   MT6357_AUDPREAMPRDCPRECHARGE_MASK,
					   MT6357_AUDPREAMPRDCPRECHARGE_ENABLE);
			/* L preamplifier DCCEN */
			regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
					   MT6357_AUDPREAMPLDCCEN_MASK,
					   MT6357_AUDPREAMPLDCCEN_DC);
			/* R preamplifier DCCEN */
			regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
					   MT6357_AUDPREAMPRDCCEN_MASK,
					   MT6357_AUDPREAMPRDCCEN_DC);
		} else {
			/* Audio L preamplifier DCC precharge disable */
			regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
					   MT6357_AUDPREAMPLDCPRECHARGE_MASK,
					   MT6357_AUDPREAMPLDCPRECHARGE_DISABLE);
			/* L preamplifier ACC */
			regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
					   MT6357_AUDPREAMPLDCCEN_MASK,
					   MT6357_AUDPREAMPLDCCEN_AC);
			/* Audio R preamplifier DCC precharge disable */
			regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
					   MT6357_AUDPREAMPRDCPRECHARGE_MASK,
					   MT6357_AUDPREAMPRDCPRECHARGE_DISABLE);
			/* R preamplifier ACC */
			regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
					   MT6357_AUDPREAMPRDCCEN_MASK,
					   MT6357_AUDPREAMPRDCCEN_AC);
		}
	} else {
		/* disable any Mic Bias 0 DC couple */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON8,
				   MT6357_AUD_MICBIAS0_DC_MASK,
				   MT6357_AUD_MICBIAS0_DC_DISABLE_ALL);
		/* disable any Mic Bias 1 DC couple */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON9,
				   MT6357_AUD_MICBIAS1_DCSW1P_EN_MASK,
				   MT6357_AUD_MICBIAS1_DCSW1P_DISABLE);
		if (IS_DCC_BASE(mic_type)) {
			regmap_update_bits(priv->regmap, MT6357_AFE_DCCLK_CFG0,
					   MT6357_DCCLK_GEN_ON_MASK, MT6357_DCCLK_GEN_OFF);
			regmap_update_bits(priv->regmap, MT6357_AFE_DCCLK_CFG0,
					   MT6357_DCCLK_PDN_MASK, MT6357_DCCLK_PDN);
			regmap_update_bits(priv->regmap, MT6357_AFE_DCCLK_CFG0,
					   MT6357_DCCLK_DIV_MASK, MT6357_DCCLK_DIV_STOP_VALUE);
		}
	}

	return 0;
}

static int mt6357_set_loopback(struct mt6357_priv *priv, bool enable)
{
	if (enable) {
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_MASK,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_ENABLE);
		/* enable aud_pad lpk TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_LPBK_MASK,
				   MT6357_AUD_PAD_TX_FIFO_LPBK_ENABLE);
		/* Set UL Part: enable new lpbk 2 */
		regmap_update_bits(priv->regmap, MT6357_AFE_ADDA_MTKAIF_CFG0,
				   MT6357_ADDA_MTKAIF_LPBK_CTL_MASK,
				   MT6357_ADDA_MTKAIF_LPBK_ENABLE);
		/* UL turn on */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SRC_ON_TMP_CTL_MASK,
				   MT6357_UL_SRC_ENABLE);
	} else {
		/* UL turn off */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SRC_ON_TMP_CTL_MASK,
				   MT6357_UL_SRC_DISABLE);
		/* disable new lpbk 2 */
		regmap_update_bits(priv->regmap, MT6357_AFE_ADDA_MTKAIF_CFG0,
				   MT6357_ADDA_MTKAIF_LPBK_CTL_MASK,
				   MT6357_ADDA_MTKAIF_LPBK_DISABLE);
		/* disable aud_pad lpbk TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_LPBK_MASK,
				   MT6357_AUD_PAD_TX_FIFO_LPBK_DISABLE);
		/* disable aud_pad TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_MASK,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_DISABLE);
	}

	return 0;
}

static int mt6357_set_ul_sine_gen(struct mt6357_priv *priv, bool enable)
{
	if (enable) {
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_MASK,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_ENABLE);
		/* UL turn on */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SRC_ON_TMP_CTL_MASK,
				   MT6357_UL_SRC_ENABLE);
	} else {
		/* UL turn off */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SRC_ON_TMP_CTL_MASK,
				   MT6357_UL_SRC_DISABLE);
		/* disable aud_pad TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_MASK,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_DISABLE);
	}

	return 0;
}

static int mt_aif_out_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		set_capture_gpio(priv, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		set_capture_gpio(priv, false);
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
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable audio ADC CLKGEN  */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON11,
				   MT6357_RSTB_ENCODER_VA28_MASK, MT6357_RSTB_ENCODER_VA28_ENABLE);
		/* Enable  LCLDO_ENC 2P8V */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON12,
				   MT6357_LCLDO_ENC_EN_VA28_MASK, MT6357_LCLDO_ENC_EN_VA28_ENABLE);
		/* LCLDO_ENC remote sense */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON12,
				   MT6357_VA28REFGEN_EN_VA28_MASK |
				   MT6357_LCLDO_ENC_REMOTE_SENSE_VA28_MASK,
				   MT6357_VA28REFGEN_EN_VA28_ENABLE |
				   MT6357_LCLDO_ENC_REMOTE_SENSE_VA28_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* LCLDO_ENC remote sense off */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON12,
				   MT6357_VA28REFGEN_EN_VA28_MASK |
				   MT6357_LCLDO_ENC_REMOTE_SENSE_VA28_MASK,
				   MT6357_VA28REFGEN_EN_VA28_DISABLE |
				   MT6357_LCLDO_ENC_REMOTE_SENSE_VA28_DISABLE);
		/* disable LCLDO_ENC 2P8V */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON12,
				   MT6357_LCLDO_ENC_EN_VA28_MASK,
				   MT6357_LCLDO_ENC_EN_VA28_DISABLE);
		/* disable audio ADC CLKGEN  */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON11,
				   MT6357_RSTB_ENCODER_VA28_MASK,
				   MT6357_RSTB_ENCODER_VA28_DISABLE);
		break;
	default:
		break;
	}

	return 0;
}

static int mt_mic_type_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mic_type = dapm_kcontrol_get_value(w->kcontrols[0]);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (mic_type) {
		case MIC_TYPE_MUX_DMIC:
			mt6357_set_dmic(priv, true);
			break;
		case MIC_TYPE_MUX_LPBK:
			mt6357_set_loopback(priv, true);
			break;
		case MIC_TYPE_MUX_SGEN:
			mt6357_set_ul_sine_gen(priv, true);
			break;
		default:
			mt6357_set_amic(priv, true, mic_type);
			break;
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		switch (mic_type) {
		case MIC_TYPE_MUX_DMIC:
			mt6357_set_dmic(priv, false);
			break;
		case MIC_TYPE_MUX_LPBK:
			mt6357_set_loopback(priv, false);
			break;
		case MIC_TYPE_MUX_SGEN:
			mt6357_set_ul_sine_gen(priv, false);
			break;
		default:
			mt6357_set_amic(priv, false, mic_type);
			break;
		}
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
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* L preamplifier enable */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDPREAMPLON_MASK,
				   MT6357_AUDPREAMPLON_ENABLE);
		/* L ADC input sel : L PGA. Enable audio L ADC */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDADCLINPUTSEL_MASK,
				   MT6357_AUDADCLINPUTSEL_PREAMPLIFIER);
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDADCLPWRUP_MASK,
				   MT6357_AUDADCLPWRUP);
		/* Audio L preamplifier DCC precharge off */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDPREAMPLDCPRECHARGE_MASK,
				   MT6357_AUDPREAMPLDCPRECHARGE_DISABLE);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* Audio L ADC input sel : off, disable audio L ADC */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDADCLPWRUP_MASK,
				   MT6357_AUDADCLPWRDOWN);
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDADCLINPUTSEL_MASK,
				   MT6357_AUDADCLINPUTSEL_IDLE);
		/* L preamplifier ACC */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDPREAMPLDCCEN_MASK,
				   MT6357_AUDPREAMPLDCCEN_AC);
		/* L preamplifier disable */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDPREAMPLON_MASK,
				   MT6357_AUDPREAMPLON_DISABLE);
		/* disable Audio L preamplifier DCC precharge */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDPREAMPLDCPRECHARGE_MASK,
				   MT6357_AUDPREAMPLDCPRECHARGE_DISABLE);
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
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* R preamplifier enable */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDPREAMPRON_MASK, MT6357_AUDPREAMPRON_ENABLE);
		/* R ADC input sel : R PGA. Enable audio R ADC */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDADCRINPUTSEL_MASK,
				   MT6357_AUDADCRINPUTSEL_PREAMPLIFIER);
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDADCRPWRUP_MASK, MT6357_AUDADCRPWRUP);
		/* Audio R preamplifier DCC precharge off */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDPREAMPRDCPRECHARGE_MASK,
				   MT6357_AUDPREAMPRDCPRECHARGE_DISABLE);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* Audio R ADC input sel : off, disable audio R ADC */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDADCRPWRUP_MASK, MT6357_AUDADCRPWRDOWN);
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDADCRINPUTSEL_MASK, MT6357_AUDADCRINPUTSEL_IDLE);
		/* R preamplifier ACC */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDPREAMPRDCCEN_MASK, MT6357_AUDPREAMPRDCCEN_AC);
		/* R preamplifier disable */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDPREAMPRON_MASK, MT6357_AUDPREAMPRON_DISABLE);
		/* disable Audio R preamplifier DCC precharge */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDPREAMPRDCPRECHARGE_MASK,
				   MT6357_AUDPREAMPRDCPRECHARGE_DISABLE);
		break;
	default:
		break;
	}

	return 0;
}

static int adc_enable_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int lgain, rgain;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_read(priv->regmap, MT6357_AUDENC_ANA_CON0, &lgain);
		regmap_read(priv->regmap, MT6357_AUDENC_ANA_CON1, &rgain);
		/* L PGA 0 dB gain */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDPREAMPLGAIN_MASK,
				   UL_GAIN_0DB << MT6357_AUDPREAMPLGAIN_SFT);
		/* R PGA 0 dB gain */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDPREAMPRGAIN_MASK,
				   UL_GAIN_0DB << MT6357_AUDPREAMPRGAIN_SFT);
		/* enable aud_pad TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_MASK,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_ENABLE);
		/* UL turn on */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SRC_ON_TMP_CTL_MASK, MT6357_UL_SRC_ENABLE);
		/* Wait to avoid any pop noises */
		msleep(100);
		/* set the mic gains to the stored values */
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON0,
				   MT6357_AUDPREAMPLGAIN_MASK, lgain);
		regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON1,
				   MT6357_AUDPREAMPRGAIN_MASK, rgain);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* UL turn off */
		regmap_update_bits(priv->regmap, MT6357_AFE_UL_SRC_CON0_L,
				   MT6357_UL_SRC_ON_TMP_CTL_MASK, MT6357_UL_SRC_DISABLE);
		/* disable aud_pad TX fifos */
		regmap_update_bits(priv->regmap, MT6357_AFE_AUD_PAD_TOP,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_MASK,
				   MT6357_AUD_PAD_TX_FIFO_NORMAL_PATH_DISABLE);
		break;
	default:
		break;
	}

	return 0;
}

static void configure_downlinks(struct mt6357_priv *priv, bool enable)
{
	if (enable) {
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ELR_0,
				   MT6357_AUD_HP_TRIM_EN_VAUDP15_MASK,
				   MT6357_AUD_HP_TRIM_EN_VAUDP15_ENABLE);
		/* Disable headphone short-circuit protection */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_HPR_SC_VAUDP15_MASK | MT6357_AUD_HPL_SC_VAUDP15_MASK,
				   MT6357_AUD_HPR_SC_VAUDP15_DISABLE |
				   MT6357_AUD_HPL_SC_VAUDP15_DISABLE);
		/* Disable handset short-circuit protection */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
				   MT6357_AUD_HS_SC_VAUDP15_MASK,
				   MT6357_AUD_HS_SC_VAUDP15_DISABLE);
		/* Disable lineout short-circuit protection */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
				   MT6357_AUD_LOL_SC_VAUDP15_MASK,
				   MT6357_AUD_LOL_SC_VAUDP15_DISABLE);
		/* Reduce ESD resistance of AU_REFN */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON2,
				   MT6357_AUD_REFN_DERES_VAUDP15_MASK,
				   MT6357_AUD_REFN_DERES_VAUDP15_ENABLE);
		/* Turn on DA_600K_NCP_VA18 */
		regmap_write(priv->regmap, MT6357_AUDNCP_CLKDIV_CON1, MT6357_DIVCKS_ON);
		/* Set NCP clock as 604kHz // 26MHz/43 = 604KHz */
		regmap_write(priv->regmap, MT6357_AUDNCP_CLKDIV_CON2, 0x002c);
		/* Toggle DIVCKS_CHG */
		regmap_write(priv->regmap, MT6357_AUDNCP_CLKDIV_CON0, MT6357_DIVCKS_CHG);
		/* Set NCP soft start mode as default mode: 150us */
		regmap_write(priv->regmap, MT6357_AUDNCP_CLKDIV_CON4,
			     MT6357_DIVCKS_PWD_NCP_ST_150US);
		/* Enable NCP */
		regmap_write(priv->regmap, MT6357_AUDNCP_CLKDIV_CON3,
			     MT6357_DIVCKS_PWD_NCP_ENABLE);
		usleep_range(250, 270);
		/* Enable cap-less LDOs (1.5V) */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON12,
				   MT6357_VA33REFGEN_EN_VA18_MASK |
				   MT6357_LCLDO_REMOTE_SENSE_VA18_MASK |
				   MT6357_LCLDO_EN_VA18_MASK |
				   MT6357_HCLDO_REMOTE_SENSE_VA18_MASK |
				   MT6357_HCLDO_EN_VA18_MASK,
				   MT6357_VA33REFGEN_EN_VA18_ENABLE |
				   MT6357_LCLDO_REMOTE_SENSE_VA18_ENABLE |
				   MT6357_LCLDO_EN_VA18_ENABLE |
				   MT6357_HCLDO_REMOTE_SENSE_VA18_ENABLE |
				   MT6357_HCLDO_EN_VA18_ENABLE);
		/* Enable NV regulator (-1.2V) */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON13,
				   MT6357_NVREG_EN_VAUDP15_MASK, MT6357_NVREG_EN_VAUDP15_ENABLE);
		usleep_range(100, 120);
		/* Enable IBIST */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON10,
				   MT6357_AUD_IBIAS_PWRDN_VAUDP15_MASK,
				   MT6357_AUD_IBIAS_PWRDN_VAUDP15_ENABLE);
		/* Enable AUD_CLK */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON11,
				   MT6357_RSTB_DECODER_VA28_MASK,
				   MT6357_RSTB_DECODER_VA28_ENABLE);
		/* Enable low-noise mode of DAC */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_DAC_LOW_NOISE_MODE_MASK,
				   MT6357_DAC_LOW_NOISE_MODE_ENABLE);
		usleep_range(100, 120);
	} else {
		/* Disable low-noise mode of DAC */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_DAC_LOW_NOISE_MODE_MASK,
				   MT6357_DAC_LOW_NOISE_MODE_DISABLE);
		/* Disable AUD_CLK */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON11,
				   MT6357_RSTB_DECODER_VA28_MASK,
				   MT6357_RSTB_DECODER_VA28_DISABLE);
		/* Enable linout short-circuit protection */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
				   MT6357_AUD_LOL_SC_VAUDP15_MASK,
				   MT6357_AUD_LOL_SC_VAUDP15_ENABLE);
		/* Enable handset short-circuit protection */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
				   MT6357_AUD_HS_SC_VAUDP15_MASK,
				   MT6357_AUD_HS_SC_VAUDP15_ENABLE);
		/* Enable headphone short-circuit protection */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_HPR_SC_VAUDP15_MASK |
				   MT6357_AUD_HPL_SC_VAUDP15_MASK,
				   MT6357_AUD_HPR_SC_VAUDP15_ENABLE |
				   MT6357_AUD_HPL_SC_VAUDP15_ENABLE);
		/* Disable IBIST */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON10,
				   MT6357_AUD_IBIAS_PWRDN_VAUDP15_MASK,
				   MT6357_AUD_IBIAS_PWRDN_VAUDP15_DISABLE);
		/* Disable NV regulator (-1.2V) */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON13,
				   MT6357_NVREG_EN_VAUDP15_MASK,
				   MT6357_NVREG_EN_VAUDP15_DISABLE);
		/* Disable cap-less LDOs (1.5V) */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON12,
				   MT6357_VA33REFGEN_EN_VA18_MASK |
				   MT6357_LCLDO_REMOTE_SENSE_VA18_MASK |
				   MT6357_LCLDO_EN_VA18_MASK |
				   MT6357_HCLDO_REMOTE_SENSE_VA18_MASK |
				   MT6357_HCLDO_EN_VA18_MASK,
				   MT6357_VA33REFGEN_EN_VA18_DISABLE |
				   MT6357_LCLDO_REMOTE_SENSE_VA18_DISABLE |
				   MT6357_LCLDO_EN_VA18_DISABLE |
				   MT6357_HCLDO_REMOTE_SENSE_VA18_DISABLE |
				   MT6357_HCLDO_EN_VA18_DISABLE);
		/* Disable NCP */
		regmap_update_bits(priv->regmap, MT6357_AUDNCP_CLKDIV_CON3,
				   MT6357_DIVCKS_PWD_NCP_MASK, MT6357_DIVCKS_PWD_NCP_DISABLE);
	}
}

static int mt_audio_in_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		set_playback_gpio(priv, true);

		/* Pull-down HPL/R to AVSS28_AUD */
		if (priv->pull_down_needed)
			hp_pull_down(priv, true);

		/* Disable HP main CMFB Switch */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_HPRL_MAIN_CMFB_LOOP_MASK,
				   MT6357_HPRL_MAIN_CMFB_LOOP_DISABLE);
		/* Audio system digital clock power down release */
		regmap_write(priv->regmap, MT6357_AFUNC_AUD_CON2,
			     MT6357_CCI_AUDIO_FIFO_DISABLE |
			     MT6357_CCI_ACD_MODE_NORMAL_PATH |
			     MT6357_CCI_AFIFO_CLK_PWDB_ON |
			     MT6357_CCI_ACD_FUNC_RSTB_RESET);
		/* sdm audio fifo clock power on */
		regmap_write(priv->regmap, MT6357_AFUNC_AUD_CON0,
			     MT6357_CCI_AUD_ANACK_INVERT |
			     (4 << MT6357_CCI_AUDIO_FIFO_WPTR_SFT) |
			     MT6357_CCI_SCRAMBLER_CG_ENABLE |
			     MT6357_CCI_RAND_ENABLE |
			     MT6357_CCI_SPLT_SCRMB_CLK_ON |
			     MT6357_CCI_SPLT_SCRMB_ON |
			     MT6357_CCI_ZERO_PADDING_DISABLE |
			     MT6357_CCI_SCRAMBLER_ENABLE);
		/* scrambler clock on enable */
		regmap_write(priv->regmap, MT6357_AFUNC_AUD_CON2,
			     MT6357_CCI_AUDIO_FIFO_DISABLE |
			     MT6357_CCI_ACD_MODE_TEST_PATH |
			     MT6357_CCI_AFIFO_CLK_PWDB_ON |
			     MT6357_CCI_ACD_FUNC_RSTB_RELEASE);
		/* sdm power on */
		regmap_write(priv->regmap, MT6357_AFUNC_AUD_CON2,
			     MT6357_CCI_AUDIO_FIFO_ENABLE |
			     MT6357_CCI_ACD_MODE_TEST_PATH |
			     MT6357_CCI_AFIFO_CLK_PWDB_ON |
			     MT6357_CCI_ACD_FUNC_RSTB_RELEASE);

		configure_downlinks(priv, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		configure_downlinks(priv, false);
		/* DL scrambler disabling sequence */
		regmap_write(priv->regmap, MT6357_AFUNC_AUD_CON2,
			     MT6357_CCI_AUDIO_FIFO_DISABLE |
			     MT6357_CCI_ACD_MODE_TEST_PATH |
			     MT6357_CCI_AFIFO_CLK_PWDB_DOWN |
			     MT6357_CCI_ACD_FUNC_RSTB_RESET);
		regmap_write(priv->regmap, MT6357_AFUNC_AUD_CON0,
			     MT6357_CCI_AUD_ANACK_INVERT |
			     (4 << MT6357_CCI_AUDIO_FIFO_WPTR_SFT) |
			     MT6357_CCI_SCRAMBLER_CG_ENABLE |
			     MT6357_CCI_RAND_ENABLE |
			     MT6357_CCI_SPLT_SCRMB_CLK_ON |
			     MT6357_CCI_SPLT_SCRMB_ON |
			     MT6357_CCI_ZERO_PADDING_DISABLE |
			     MT6357_CCI_SCRAMBLER_DISABLE);

		set_playback_gpio(priv, false);

		/* disable Pull-down HPL/R to AVSS28_AUD */
		if (priv->pull_down_needed)
			hp_pull_down(priv, false);
		break;
	default:
		break;
	}

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

static int lo_mux_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol,
			int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int lgain, rgain;

	/* Get current gain value */
	regmap_read(priv->regmap, MT6357_ZCD_CON1, &lgain);
	rgain = (lgain & MT6357_AUD_LOR_GAIN_MASK) >> MT6357_AUD_LOR_GAIN_SFT;
	lgain = lgain & MT6357_AUD_LOL_GAIN_MASK;
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Set -40dB before enable HS to avoid POP noise */
		regmap_update_bits(priv->regmap, MT6357_ZCD_CON1,
				   MT6357_AUD_LOL_GAIN_MASK |
				   MT6357_AUD_LOR_GAIN_MASK,
				   MT6357_DL_GAIN_N_40DB_REG);
		/* Set LO STB enhance circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
				   MT6357_AUD_LOLOUT_STB_ENH_VAUDP15_MASK,
				   MT6357_AUD_LOLOUT_STB_ENH_VAUDP15_ENABLE);
		/* Enable LO driver bias circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
				   MT6357_AUD_LOL_PWRUP_BIAS_VAUDP15_MASK,
				   MT6357_AUD_LOL_PWRUP_BIAS_VAUDP15_ENABLE);
		/* Enable LO driver core circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
				   MT6357_AUD_LOL_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_LOL_PWRUP_VAUDP15_ENABLE);
		/* Set LOL gain to normal gain step by step */
		lo_volume_ramp(priv, DL_GAIN_N_40DB, lgain,
			       DL_GAIN_N_40DB, rgain);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* decrease LOL gain to minimum gain step by step */

		lo_volume_ramp(priv, lgain, DL_GAIN_N_40DB,
			       rgain, DL_GAIN_N_40DB);
		/* Disable LO driver core circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
				   MT6357_AUD_LOL_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_LOL_PWRUP_VAUDP15_DISABLE);
		/* Disable LO driver bias circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
				   MT6357_AUD_LOL_PWRUP_BIAS_VAUDP15_MASK,
				   MT6357_AUD_LOL_PWRUP_BIAS_VAUDP15_DISABLE);
		/* Clear LO STB enhance circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
				   MT6357_AUD_LOLOUT_STB_ENH_VAUDP15_MASK,
				   MT6357_AUD_LOLOUT_STB_ENH_VAUDP15_DISABLE);
		/* Save the gain value into the register*/
		regmap_update_bits(priv->regmap, MT6357_ZCD_CON1,
				   MT6357_AUD_LOL_GAIN_MASK |
				   MT6357_AUD_LOR_GAIN_MASK,
				   lgain << MT6357_AUD_LOL_GAIN_SFT |
				   rgain << MT6357_AUD_LOR_GAIN_SFT);

		break;
	default:
		break;
	}

	return 0;
}

static int hs_mux_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol,
			int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int gain; /* HS register has only one gain slot */

	/* Get current gain value */
	regmap_read(priv->regmap, MT6357_ZCD_CON3, &gain);
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Set -40dB before enable HS to avoid POP noise */
		regmap_update_bits(priv->regmap, MT6357_ZCD_CON3,
				   MT6357_AUD_HS_GAIN_MASK,
				   DL_GAIN_N_40DB);

		/* Set HS STB enhance circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
				   MT6357_AUD_HSOUT_STB_ENH_VAUDP15_MASK,
				   MT6357_AUD_HSOUT_STB_ENH_VAUDP15_ENABLE);
		/* Enable HS driver bias circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
				   MT6357_AUD_HS_PWRUP_BIAS_VAUDP15_MASK,
				   MT6357_AUD_HS_PWRUP_BIAS_VAUDP15_ENABLE);
		/* Enable HS driver core circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
				   MT6357_AUD_HS_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_HS_PWRUP_VAUDP15_ENABLE);
		/* Set HS gain to normal gain step by step */
		hs_volume_ramp(priv, DL_GAIN_N_40DB, gain);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* decrease HS gain to minimum gain step by step */
		hs_volume_ramp(priv,  gain, DL_GAIN_N_40DB);
		/* Disable HS driver core circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
				   MT6357_AUD_HS_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_HS_PWRUP_VAUDP15_DISABLE);
		/* Disable HS driver bias circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
				   MT6357_AUD_HS_PWRUP_BIAS_VAUDP15_MASK,
				   MT6357_AUD_HS_PWRUP_BIAS_VAUDP15_ENABLE);
		/* Clear HS STB enhance circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
				   MT6357_AUD_HSOUT_STB_ENH_VAUDP15_MASK,
				   MT6357_AUD_HSOUT_STB_ENH_VAUDP15_DISABLE);
		/* Save the gain value into the register*/
		regmap_update_bits(priv->regmap, MT6357_ZCD_CON3,
				   MT6357_AUD_HS_GAIN_MASK, gain);
		break;
	default:
		break;
	}

	return 0;
}

static int hp_main_mux_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int lgain, rgain;

	/* Get current gain value */
	regmap_read(priv->regmap, MT6357_ZCD_CON2, &lgain);
	rgain = (lgain & MT6357_AUD_HPR_GAIN_MASK) >> MT6357_AUD_HPR_GAIN_SFT;
	lgain = lgain & MT6357_AUD_HPL_GAIN_MASK;
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		priv->hp_channel_number++;
		if (priv->hp_channel_number > 1)
			break;
		/* Set -40dB before enable HS to avoid POP noise */
		regmap_update_bits(priv->regmap, MT6357_ZCD_CON2,
				   MT6357_AUD_HPL_GAIN_MASK |
				   MT6357_AUD_HPR_GAIN_MASK,
				   MT6357_DL_GAIN_N_40DB_REG);
		/* Set HPP/N STB enhance circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON2,
				   MT6357_HPROUT_STB_ENH_VAUDP15_MASK |
				   MT6357_HPLOUT_STB_ENH_VAUDP15_MASK,
				   MT6357_HPROUT_STB_ENH_VAUDP15_N470_P250 |
				   MT6357_HPLOUT_STB_ENH_VAUDP15_N470_P250);
		/* Enable HP aux output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPROUT_AUX_PWRUP_VAUDP15_MASK |
				   MT6357_HPLOUT_AUX_PWRUP_VAUDP15_MASK,
				   MT6357_HPROUT_AUX_PWRUP_VAUDP15_ENABLE |
				   MT6357_HPLOUT_AUX_PWRUP_VAUDP15_ENABLE);
		/* Enable HP aux feedback loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPR_AUX_FBRSW_VAUDP15_MASK |
				   MT6357_HPL_AUX_FBRSW_VAUDP15_MASK,
				   MT6357_HPR_AUX_FBRSW_VAUDP15_ENABLE |
				   MT6357_HPL_AUX_FBRSW_VAUDP15_ENABLE);
		/* Enable HP aux CMFB loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_HP_CMFB_RST_MASK |
				   MT6357_HPL_AUX_CMFB_LOOP_MASK |
				   MT6357_HPR_AUX_CMFB_LOOP_MASK,
				   MT6357_HP_CMFB_RST_NORMAL |
				   MT6357_HPL_AUX_CMFB_LOOP_ENABLE |
				   MT6357_HPR_AUX_CMFB_LOOP_ENABLE);
		/* Enable HP driver bias circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_HPR_BIAS_VAUDP15_MASK |
				   MT6357_AUD_HPL_BIAS_VAUDP15_MASK,
				   MT6357_AUD_HPR_BIAS_VAUDP15_ENABLE |
				   MT6357_AUD_HPL_BIAS_VAUDP15_ENABLE);
		/* Enable HP driver core circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_HPR_PWRUP_VAUDP15_MASK |
				   MT6357_AUD_HPL_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_HPR_PWRUP_VAUDP15_ENABLE |
				   MT6357_AUD_HPL_PWRUP_VAUDP15_ENABLE);
		/* Short HP main output to HP aux output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPR_SHORT2HPR_AUX_VAUDP15_MASK |
				   MT6357_HPL_SHORT2HPR_AUX_VAUDP15_MASK,
				   MT6357_HPR_SHORT2HPR_AUX_VAUDP15_ENABLE |
				   MT6357_HPL_SHORT2HPR_AUX_VAUDP15_ENABLE);
		/* Enable HP main CMFB loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_HPRL_MAIN_CMFB_LOOP_MASK,
				   MT6357_HPRL_MAIN_CMFB_LOOP_ENABLE);
		/* Disable HP aux CMFB loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_HPR_AUX_CMFB_LOOP_MASK |
				   MT6357_HPL_AUX_CMFB_LOOP_MASK,
				   MT6357_HPR_AUX_CMFB_LOOP_DISABLE |
				   MT6357_HPL_AUX_CMFB_LOOP_DISABLE);
		/* Enable HP main output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPROUT_PWRUP_VAUDP15_MASK |
				   MT6357_HPLOUT_PWRUP_VAUDP15_MASK,
				   MT6357_HPROUT_PWRUP_VAUDP15_ENABLE |
				   MT6357_HPLOUT_PWRUP_VAUDP15_ENABLE);
		/* Enable HPR/L main output stage step by step */
		hp_main_output_ramp(priv, true);
		usleep_range(1000, 1200);
		/* Reduce HP aux feedback loop gain */
		hp_aux_feedback_loop_gain_ramp(priv, true);
		/* Disable HP aux feedback loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPR_AUX_FBRSW_VAUDP15_MASK |
				   MT6357_HPL_AUX_FBRSW_VAUDP15_MASK,
				   MT6357_HPR_AUX_FBRSW_VAUDP15_DISABLE |
				   MT6357_HPL_AUX_FBRSW_VAUDP15_DISABLE);
		/* apply volume setting */
		hp_volume_ramp(priv, DL_GAIN_N_40DB, lgain,
			       DL_GAIN_N_40DB, rgain);
		/* Disable HP aux output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPROUT_AUX_PWRUP_VAUDP15_MASK |
				   MT6357_HPLOUT_AUX_PWRUP_VAUDP15_MASK,
				   MT6357_HPROUT_AUX_PWRUP_VAUDP15_DISABLE |
				   MT6357_HPLOUT_AUX_PWRUP_VAUDP15_DISABLE);
		/* Unshort HP main output to HP aux output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPR_SHORT2HPR_AUX_VAUDP15_MASK |
				   MT6357_HPL_SHORT2HPR_AUX_VAUDP15_MASK,
				   MT6357_HPR_SHORT2HPR_AUX_VAUDP15_DISABLE |
				   MT6357_HPL_SHORT2HPR_AUX_VAUDP15_DISABLE);
		usleep_range(100, 120);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		priv->hp_channel_number--;
		if (priv->hp_channel_number > 0)
			break;
		/* Short HP main output to HP aux output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPR_SHORT2HPR_AUX_VAUDP15_MASK |
				   MT6357_HPL_SHORT2HPR_AUX_VAUDP15_MASK,
				   MT6357_HPR_SHORT2HPR_AUX_VAUDP15_ENABLE |
				   MT6357_HPL_SHORT2HPR_AUX_VAUDP15_ENABLE);
		/* Enable HP aux output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPROUT_AUX_PWRUP_VAUDP15_MASK |
				   MT6357_HPLOUT_AUX_PWRUP_VAUDP15_MASK,
				   MT6357_HPROUT_AUX_PWRUP_VAUDP15_ENABLE |
				   MT6357_HPLOUT_AUX_PWRUP_VAUDP15_ENABLE);
		/* decrease HPL/R gain to normal gain step by step */
		hp_volume_ramp(priv, lgain, DL_GAIN_N_40DB,
			       rgain, DL_GAIN_N_40DB);
		/* Enable HP aux feedback loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPR_AUX_FBRSW_VAUDP15_MASK |
				   MT6357_HPL_AUX_FBRSW_VAUDP15_MASK,
				   MT6357_HPR_AUX_FBRSW_VAUDP15_ENABLE |
				   MT6357_HPL_AUX_FBRSW_VAUDP15_ENABLE);
		/* Reduce HP aux feedback loop gain */
		hp_aux_feedback_loop_gain_ramp(priv, false);
		/* decrease HPR/L main output stage step by step */
		hp_main_output_ramp(priv, false);
		/* Disable HP main output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPROUT_PWRUP_VAUDP15_MASK |
				   MT6357_HPLOUT_PWRUP_VAUDP15_MASK,
				   MT6357_HPROUT_PWRUP_VAUDP15_DISABLE |
				   MT6357_HPLOUT_PWRUP_VAUDP15_DISABLE);
		/* Enable HP aux CMFB loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_HP_CMFB_RST_MASK |
				   MT6357_HPL_AUX_CMFB_LOOP_MASK |
				   MT6357_HPR_AUX_CMFB_LOOP_MASK,
				   MT6357_HP_CMFB_RST_RESET |
				   MT6357_HPL_AUX_CMFB_LOOP_ENABLE |
				   MT6357_HPR_AUX_CMFB_LOOP_ENABLE);
		/* Disable HP main CMFB loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_HPRL_MAIN_CMFB_LOOP_MASK,
				   MT6357_HPRL_MAIN_CMFB_LOOP_DISABLE);
		/* Unshort HP main output to HP aux output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPR_SHORT2HPR_AUX_VAUDP15_MASK |
				   MT6357_HPL_SHORT2HPR_AUX_VAUDP15_MASK,
				   MT6357_HPR_SHORT2HPR_AUX_VAUDP15_DISABLE |
				   MT6357_HPL_SHORT2HPR_AUX_VAUDP15_DISABLE);
		/* Disable HP driver core circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_HPR_PWRUP_VAUDP15_MASK |
				   MT6357_AUD_HPL_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_HPR_PWRUP_VAUDP15_DISABLE |
				   MT6357_AUD_HPL_PWRUP_VAUDP15_DISABLE);
		/* Disable HP driver bias circuits */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_HPR_BIAS_VAUDP15_MASK |
				   MT6357_AUD_HPL_BIAS_VAUDP15_MASK,
				   MT6357_AUD_HPR_BIAS_VAUDP15_DISABLE |
				   MT6357_AUD_HPL_BIAS_VAUDP15_DISABLE);
		/* Disable HP aux CMFB loop,
		 * Enable HP main CMFB for HP off state
		 */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON6,
				   MT6357_HPRL_MAIN_CMFB_LOOP_MASK |
				   MT6357_HPR_AUX_CMFB_LOOP_MASK |
				   MT6357_HPL_AUX_CMFB_LOOP_MASK,
				   MT6357_HPRL_MAIN_CMFB_LOOP_ENABLE |
				   MT6357_HPR_AUX_CMFB_LOOP_DISABLE |
				   MT6357_HPL_AUX_CMFB_LOOP_DISABLE);
		/* Disable HP aux feedback loop */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPR_AUX_FBRSW_VAUDP15_MASK |
				   MT6357_HPL_AUX_FBRSW_VAUDP15_MASK,
				   MT6357_HPR_AUX_FBRSW_VAUDP15_DISABLE |
				   MT6357_HPL_AUX_FBRSW_VAUDP15_DISABLE);
		/* Disable HP aux output stage */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON1,
				   MT6357_HPROUT_AUX_PWRUP_VAUDP15_MASK |
				   MT6357_HPLOUT_AUX_PWRUP_VAUDP15_MASK,
				   MT6357_HPROUT_AUX_PWRUP_VAUDP15_DISABLE |
				   MT6357_HPLOUT_AUX_PWRUP_VAUDP15_DISABLE);
		/* Save the gain value into the register*/
		regmap_update_bits(priv->regmap, MT6357_ZCD_CON2,
				   MT6357_AUD_HPL_GAIN_MASK |
				   MT6357_AUD_HPR_GAIN_MASK,
				   lgain << MT6357_AUD_HPL_GAIN_SFT |
				   rgain << MT6357_AUD_HPR_GAIN_SFT);
		break;
	default:
		break;
	}

	return 0;
}

static int right_dac_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol,
			   int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable Audio DAC and control audio bias gen */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_DACR_PWRUP_VA28_MASK |
				   MT6357_AUD_DACR_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_DACR_PWRUP_VA28_ENABLE |
				   MT6357_AUD_DACR_PWRUP_VAUDP15_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* disable Pull-down HPL/R to AVSS28_AUD */
		if (priv->pull_down_needed)
			hp_pull_down(priv, false);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* Pull-down HPL/R to AVSS28_AUD */
		if (priv->pull_down_needed)
			hp_pull_down(priv, true);
		/* Disable Audio DAC and control audio bias gen  */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_DACR_PWRUP_VA28_MASK |
				   MT6357_AUD_DACR_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_DACR_PWRUP_VA28_DISABLE |
				   MT6357_AUD_DACR_PWRUP_VAUDP15_DISABLE);
		break;
	default:
		break;
	}

	return 0;
}

static int left_dac_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable Audio DAC and control audio bias gen  */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_DACL_PWRUP_VA28_MASK |
				   MT6357_AUD_DACL_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_DACL_PWRUP_VA28_ENABLE |
				   MT6357_AUD_DACL_PWRUP_VAUDP15_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* disable Pull-down HPL/R to AVSS28_AUD */
		if (priv->pull_down_needed)
			hp_pull_down(priv, false);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* Pull-down HPL/R to AVSS28_AUD */
		if (priv->pull_down_needed)
			hp_pull_down(priv, true);
		/* Disable Audio DAC and control audio bias gen  */
		regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
				   MT6357_AUD_DACL_PWRUP_VA28_MASK |
				   MT6357_AUD_DACL_PWRUP_VAUDP15_MASK,
				   MT6357_AUD_DACL_PWRUP_VA28_DISABLE |
				   MT6357_AUD_DACL_PWRUP_VAUDP15_DISABLE);
		break;
	default:
		break;
	}

	return 0;
}

/* Supply widgets subsequence */
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

/* DAPM Widgets */
static const struct snd_soc_dapm_widget mt6357_dapm_widgets[] = {
	/* Analog Clocks */
	SND_SOC_DAPM_SUPPLY_S("CLK_BUF", SUPPLY_SEQ_CLK_BUF,
			      MT6357_DCXO_CW14,
			      MT6357_XO_AUDIO_EN_M_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDGLB", SUPPLY_SEQ_AUD_GLB,
			      MT6357_AUDDEC_ANA_CON11,
			      MT6357_AUDGLB_PWRDN_VA28_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("CLKSQ Audio", SUPPLY_SEQ_CLKSQ,
			      MT6357_AUDENC_ANA_CON6,
			      MT6357_CLKSQ_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDNCP_CK", SUPPLY_SEQ_TOP_CK,
			      MT6357_AUD_TOP_CKPDN_CON0,
			      MT6357_AUDNCP_CK_PDN_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ZCD13M_CK", SUPPLY_SEQ_TOP_CK,
			      MT6357_AUD_TOP_CKPDN_CON0,
			      MT6357_ZCD13M_CK_PDN_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUD_CK", SUPPLY_SEQ_TOP_CK_LAST,
			      MT6357_AUD_TOP_CKPDN_CON0,
			      MT6357_AUD_CK_PDN_SFT, 1,
			      mt_delay_250_event,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY_S("AUDIF_CK", SUPPLY_SEQ_TOP_CK,
			      MT6357_AUD_TOP_CKPDN_CON0,
			      MT6357_AUDIF_CK_PDN_SFT, 1, NULL, 0),

	/* Digital Clocks */
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_AFE_CTL", SUPPLY_SEQ_AUD_TOP_LAST,
			      MT6357_AUDIO_TOP_CON0,
			      MT6357_PDN_AFE_CTL_SFT, 1,
			      mt_delay_250_event,
			      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_DAC_CTL", SUPPLY_SEQ_AUD_TOP,
			      MT6357_AUDIO_TOP_CON0,
			      MT6357_PDN_DAC_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_ADC_CTL", SUPPLY_SEQ_AUD_TOP,
			      MT6357_AUDIO_TOP_CON0,
			      MT6357_PDN_ADC_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_I2S_DL", SUPPLY_SEQ_AUD_TOP,
			      MT6357_AUDIO_TOP_CON0,
			      MT6357_PDN_I2S_DL_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PWR_CLK", SUPPLY_SEQ_AUD_TOP,
			      MT6357_AUDIO_TOP_CON0,
			      MT6357_PWR_CLK_DIS_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PDN_AFE_TESTMODEL", SUPPLY_SEQ_AUD_TOP,
			      MT6357_AUDIO_TOP_CON0,
			      MT6357_PDN_AFE_TESTMODEL_CTL_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_PDN_RESERVED", SUPPLY_SEQ_AUD_TOP,
			      MT6357_AUDIO_TOP_CON0,
			      MT6357_PDN_RESERVED_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AUDIO_TOP_LPBK", SUPPLY_SEQ_AUD_TOP,
			      MT6357_AUDIO_TOP_CON0,
			      MT6357_PDN_LPBK_CTL_SFT, 1, NULL, 0),

	/* General */
	SND_SOC_DAPM_SUPPLY_S("AFE_ON", SUPPLY_SEQ_AFE,
			      MT6357_AFE_UL_DL_CON0,
			      MT6357_AFE_ON_SFT, 0, NULL, 0),

	/* Uplinks */
	SND_SOC_DAPM_AIF_OUT_E("AIF1TX", "MT6357 Capture", 0,
			       SND_SOC_NOPM, 0, 0,
			       mt_aif_out_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("ADC Supply", SUPPLY_SEQ_ADC_SUPPLY,
			      SND_SOC_NOPM, 0, 0,
			      mt_adc_supply_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC", NULL, SND_SOC_NOPM, 0, 0, adc_enable_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("PGA L Mux", SND_SOC_NOPM, 0, 0,
			   &pga_left_mux_control,
			   mt_pga_left_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX_E("PGA R Mux", SND_SOC_NOPM, 0, 0,
			   &pga_right_mux_control,
			   mt_pga_right_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA("PGA L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PGA R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MUX_E("Mic Type Mux", SND_SOC_NOPM, 0, 0,
			   &mic_type_mux_control,
			   mt_mic_type_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MICBIAS0", MT6357_AUDENC_ANA_CON8,
			    MT6357_AUD_MICBIAS0_PWD_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS1", MT6357_AUDENC_ANA_CON9,
			    MT6357_AUD_MICBIAS1_PWD_SFT, 0, NULL, 0),

	/* UL inputs */
	SND_SOC_DAPM_INPUT("AIN0"),
	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),
	SND_SOC_DAPM_INPUT("LPBK"),
	SND_SOC_DAPM_INPUT("SGEN UL"),

	/* Downlinks */
	SND_SOC_DAPM_AIF_IN_E("AIF_RX", "MT6357 Playback", 0,
			      SND_SOC_NOPM, 0, 0,
			      mt_audio_in_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_INPUT("SGEN DL"),
	SND_SOC_DAPM_MUX("DAC Mux", SND_SOC_NOPM, 0, 0, &dac_mux_control),

	SND_SOC_DAPM_DAC_E("DACR", NULL, SND_SOC_NOPM, 0, 0, right_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("DACL", NULL, SND_SOC_NOPM, 0, 0, left_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY("DL Digital Supply", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DL Analog Supply", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DL SRC", MT6357_AFE_DL_SRC2_CON0_L,
			    MT6357_DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0, NULL, 0),

	SND_SOC_DAPM_MUX_E("Line Out Source", SND_SOC_NOPM, 0, 0, &lo_mux_control,
			   lo_mux_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MUX_E("Handset Source", SND_SOC_NOPM, 0, 0, &hs_mux_control,
			   hs_mux_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MUX_E("Headphone Right Source", SND_SOC_NOPM, 0, 0, &hpr_mux_control,
			   hp_main_mux_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MUX_E("Headphone Left Source", SND_SOC_NOPM, 0, 0, &hpl_mux_control,
			   hp_main_mux_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	/* DL outputs */
	SND_SOC_DAPM_OUTPUT("Headphones"),
	SND_SOC_DAPM_OUTPUT("Hansdet"),
	SND_SOC_DAPM_OUTPUT("Line out"),

	/* Sine generator */
	SND_SOC_DAPM_SUPPLY("SGEN UL Enable",
			    MT6357_AFE_TOP_CON0, MT6357_UL_SINE_ON_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SGEN Enable",
			    MT6357_AFE_SGEN_CFG0,
			    MT6357_SGEN_DAC_EN_CTL_SFT, 0, mt_audio_in_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("SGEN MUTE",
			    MT6357_AFE_SGEN_CFG0,
			    MT6357_SGEN_MUTE_SW_CTL_SFT, 1, NULL, 0)
};

static const struct snd_soc_dapm_route mt6357_dapm_routes[] = {
	/* Capture */
	{"AIF1TX", NULL, "Mic Type Mux"},
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

	{"Mic Type Mux", "ACC", "ADC"},
	{"Mic Type Mux", "DCC", "ADC"},
	{"Mic Type Mux", "DCC_ECM_DIFF", "ADC"},
	{"Mic Type Mux", "DCC_ECM_SINGLE", "ADC"},
	{"Mic Type Mux", "DMIC", "AIN0"},
	{"Mic Type Mux", "DMIC", "AIN2"},
	{"Mic Type Mux", "Loopback", "LPBK"},
	{"Mic Type Mux", "Sine Generator", "SGEN UL"},

	{"SGEN UL", NULL, "AUDIO_TOP_PDN_AFE_TESTMODEL"},
	{"SGEN UL", NULL, "SGEN UL Enable"},
	{"SGEN UL", NULL, "SGEN MUTE"},
	{"SGEN UL", NULL, "SGEN Enable"},

	{"ADC", NULL, "PGA L Mux"},
	{"ADC", NULL, "PGA R Mux"},
	{"ADC", NULL, "ADC Supply"},

	{"PGA L Mux", "AIN0", "AIN0"},
	{"PGA L Mux", "AIN1", "AIN1"},
	{"PGA L Mux", "AIN2", "AIN2"},

	{"PGA R Mux", "AIN0", "AIN0"},
	{"PGA R Mux", "AIN1", "AIN1"},
	{"PGA R Mux", "AIN2", "AIN2"},

	{"AIN0", NULL, "MICBIAS0"},
	{"AIN1", NULL, "MICBIAS1"},
	{"AIN2", NULL, "MICBIAS0"},
	{"LPBK", NULL, "AUDIO_TOP_LPBK"},

	/* Playback */
	{"DAC Mux", "Normal Path", "AIF_RX"},
	{"DAC Mux", "Sine Generator", "SGEN DL"},

	{"AIF_RX", NULL, "DL SRC"},

	{"SGEN DL", NULL, "DL SRC"},
	{"SGEN DL", NULL, "SGEN MUTE"},
	{"SGEN DL", NULL, "SGEN Enable"},
	{"SGEN DL", NULL, "DL Digital Supply"},
	{"SGEN DL", NULL, "AUDIO_TOP_PDN_AFE_TESTMODEL"},

	{"DACL", NULL, "DAC Mux"},
	{"DACR", NULL, "DAC Mux"},

	{"DL Analog Supply", NULL, "CLK_BUF"},
	{"DL Analog Supply", NULL, "AUDGLB"},
	{"DL Analog Supply", NULL, "CLKSQ Audio"},
	{"DL Analog Supply", NULL, "AUDNCP_CK"},
	{"DL Analog Supply", NULL, "ZCD13M_CK"},
	{"DL Analog Supply", NULL, "AUD_CK"},
	{"DL Analog Supply", NULL, "AUDIF_CK"},

	{"DL Digital Supply", NULL, "AUDIO_TOP_AFE_CTL"},
	{"DL Digital Supply", NULL, "AUDIO_TOP_DAC_CTL"},
	{"DL Digital Supply", NULL, "AUDIO_TOP_PWR_CLK"},
	{"DL Digital Supply", NULL, "AFE_ON"},

	{"DACR", NULL, "DL Digital Supply"},
	{"DACR", NULL, "DL Analog Supply"},
	{"DACL", NULL, "DL Digital Supply"},
	{"DACL", NULL, "DL Analog Supply"},

	{"Line Out Source", "DACR", "DACR"},
	{"Line Out Source", "Playback", "DACL"},
	{"Line Out Source", "Test mode", "DACL"},

	{"Handset Source", "DACR", "DACR"},
	{"Handset Source", "Playback", "DACL"},
	{"Handset Source", "Test mode", "DACL"},

	{"Headphone Right Source", "DAC", "DACR"},
	{"Headphone Right Source", "Line Out", "Line Out Source"},
	{"Headphone Right Source", "Handset", "Handset Source"},

	{"Headphone Left Source", "DAC", "DACL"},
	{"Headphone Left Source", "Line Out", "Line Out Source"},
	{"Headphone Left Source", "Handset", "Handset Source"},

	{"Line out", NULL, "Line Out Source"},
	{"Hansdet", NULL, "Handset Source"},

	{"Headphones", NULL, "Headphone Right Source"},
	{"Headphones", NULL, "Headphone Left Source"},
};

static struct snd_soc_dai_driver mtk_6357_dai_codecs[] = {
	{
		.name = "mt6357-snd-codec-aif1",
		.playback = {
			.stream_name = "MT6357 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = MT6357_SND_SOC_ADV_MT_FMTS,
			},
		.capture = {
			.stream_name = "MT6357 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT6357_SOC_HIGH_USE_RATE,
			.formats = MT6357_SND_SOC_ADV_MT_FMTS,
			},
	},
};

static int mt6357_codec_probe(struct snd_soc_component *codec)
{
	struct mt6357_priv *priv = snd_soc_component_get_drvdata(codec);

	snd_soc_component_init_regmap(codec, priv->regmap);

	/* Enable audio part */
	regmap_update_bits(priv->regmap, MT6357_DCXO_CW14,
			   MT6357_XO_AUDIO_EN_M_MASK, MT6357_XO_AUDIO_EN_M_ENABLE);
	/* Disable HeadphoneL/HeadphoneR short circuit protection */
	regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON0,
			   MT6357_AUD_HPR_SC_VAUDP15_MASK |
			   MT6357_AUD_HPL_SC_VAUDP15_MASK,
			   MT6357_AUD_HPR_SC_VAUDP15_DISABLE |
			   MT6357_AUD_HPL_SC_VAUDP15_DISABLE);
	/* Disable voice short circuit protection */
	regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON3,
			   MT6357_AUD_HS_SC_VAUDP15_MASK,
			   MT6357_AUD_HS_SC_VAUDP15_DISABLE);
	/* disable LO buffer left short circuit protection */
	regmap_update_bits(priv->regmap, MT6357_AUDDEC_ANA_CON4,
			   MT6357_AUD_LOL_SC_VAUDP15_MASK,
			   MT6357_AUD_LOL_SC_VAUDP15_DISABLE);
	/* set gpio */
	set_playback_gpio(priv, false);
	set_capture_gpio(priv, false);
	/* Disable audio part */
	regmap_update_bits(priv->regmap, MT6357_DCXO_CW14,
			   MT6357_XO_AUDIO_EN_M_MASK,
			   MT6357_XO_AUDIO_EN_M_DISABLE);

	return 0;
}

static const struct snd_soc_component_driver mt6357_soc_component_driver = {
	.probe = mt6357_codec_probe,
	.read = snd_soc_component_read,
	.write = snd_soc_component_write,
	.controls = mt6357_controls,
	.num_controls = ARRAY_SIZE(mt6357_controls),
	.dapm_widgets = mt6357_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6357_dapm_widgets),
	.dapm_routes = mt6357_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6357_dapm_routes),
};

static const u32 micbias_values[] = {
	1700000, 1800000, 1900000, 2000000,
	2100000, 2500000, 2600000, 2700000
};

static u32 mt6357_get_micbias_idx(struct device_node *np, const char *micbias)
{
	int err;
	u32 idx, val;

	err = of_property_read_u32(np, micbias,  &val);
	if (err)
		return 0;

	for (idx = 0; idx < ARRAY_SIZE(micbias_values); idx++) {
		if (val == micbias_values[idx])
			return idx;
	}
	return 0;
}

static int mt6357_parse_dt(struct mt6357_priv *priv)
{
	u32 micbias_voltage_index = 0;
	struct device_node *np = priv->dev->parent->of_node;

	if (!np)
		return -EINVAL;

	priv->pull_down_needed = false;
	if (of_property_read_bool(np, "mediatek,hp-pull-down"))
		priv->pull_down_needed = true;

	micbias_voltage_index = mt6357_get_micbias_idx(np, "mediatek,micbias0-microvolt");
	regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON8,
			   MT6357_AUD_MICBIAS0_VREF_MASK,
			   micbias_voltage_index << MT6357_AUD_MICBIAS0_VREF_SFT);

	micbias_voltage_index = mt6357_get_micbias_idx(np, "mediatek,micbias1-microvolt");
	regmap_update_bits(priv->regmap, MT6357_AUDENC_ANA_CON9,
			   MT6357_AUD_MICBIAS1_VREF_MASK,
			   micbias_voltage_index << MT6357_AUD_MICBIAS1_VREF_SFT);

	return 0;
}

static int mt6357_platform_driver_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	struct mt6357_priv *priv;
	int ret;

	ret = devm_regulator_get_enable(&pdev->dev, "vaud28");
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to enable vaud28 regulator\n");

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, priv);
	priv->dev = &pdev->dev;

	priv->regmap = mt6397->regmap;
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	ret = mt6357_parse_dt(priv);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to parse dts\n");

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	return devm_snd_soc_register_component(&pdev->dev,
					       &mt6357_soc_component_driver,
					       mtk_6357_dai_codecs,
					       ARRAY_SIZE(mtk_6357_dai_codecs));
}

static const struct platform_device_id mt6357_platform_ids[] = {
	{"mt6357-sound", 0},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mt6357_platform_ids);

static struct platform_driver mt6357_platform_driver = {
	.driver = {
		.name = "mt6357-sound",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = mt6357_platform_driver_probe,
	.id_table = mt6357_platform_ids,
};

module_platform_driver(mt6357_platform_driver)

MODULE_DESCRIPTION("MT6357 ALSA SoC codec driver");
MODULE_AUTHOR("Nicolas Belin <nbelin@baylibre.com>");
MODULE_LICENSE("GPL");
