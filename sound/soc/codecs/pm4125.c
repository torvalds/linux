// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
// Copyright (c) 2025, Linaro Ltd

#include <linux/component.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "pm4125.h"
#include "wcd-mbhc-v2.h"

#define WCD_MBHC_HS_V_MAX		1600
#define PM4125_MBHC_MAX_BUTTONS		8

#define PM4125_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
		      SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
		      SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000 |\
		      SNDRV_PCM_RATE_384000)

/* Fractional Rates */
#define PM4125_FRAC_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_88200 |\
			   SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_352800)

#define PM4125_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

/* Registers in SPMI addr space */
#define PM4125_CODEC_RESET_REG		0xF3DB
#define PM4125_CODEC_OFF		0x1
#define PM4125_CODEC_ON			0x0
#define PM4125_CODEC_FOUNDRY_ID_REG	0x7

enum {
	HPH_COMP_DELAY,
	HPH_PA_DELAY,
	AMIC2_BCS_ENABLE,
};

enum {
	AIF1_PB = 0,
	AIF1_CAP,
	NUM_CODEC_DAIS,
};

struct pm4125_priv {
	struct sdw_slave *tx_sdw_dev;
	struct pm4125_sdw_priv *sdw_priv[NUM_CODEC_DAIS];
	struct device *txdev;
	struct device *rxdev;
	struct device_node *rxnode;
	struct device_node *txnode;
	struct regmap *regmap;
	struct regmap *spmi_regmap;
	/* mbhc module */
	struct wcd_mbhc *wcd_mbhc;
	struct wcd_mbhc_config mbhc_cfg;
	struct wcd_mbhc_intr intr_ids;
	struct irq_domain *virq;
	const struct regmap_irq_chip *pm4125_regmap_irq_chip;
	struct regmap_irq_chip_data *irq_chip;
	struct snd_soc_jack *jack;
	unsigned long status_mask;
	s32 micb_ref[PM4125_MAX_MICBIAS];
	s32 pullup_ref[PM4125_MAX_MICBIAS];
	u32 micb1_mv;
	u32 micb2_mv;
	u32 micb3_mv;

	int hphr_pdm_wd_int;
	int hphl_pdm_wd_int;
	bool comp1_enable;
	bool comp2_enable;

	atomic_t gloal_mbias_cnt;
};

static const char * const pm4125_power_supplies[] = {
	"vdd-io", "vdd-cp", "vdd-mic-bias", "vdd-pa-vpos",
};

static const DECLARE_TLV_DB_SCALE(line_gain, 0, 7, 1);
static const DECLARE_TLV_DB_SCALE(analog_gain, 0, 25, 1);

static const struct wcd_mbhc_field pm4125_mbhc_fields[WCD_MBHC_REG_FUNC_MAX] = {
	WCD_MBHC_FIELD(WCD_MBHC_L_DET_EN, PM4125_ANA_MBHC_MECH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_GND_DET_EN, PM4125_ANA_MBHC_MECH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_MECH_DETECTION_TYPE, PM4125_ANA_MBHC_MECH, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_CLAMP_CTL, PM4125_ANA_MBHC_PLUG_DETECT_CTL, 0x30),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_DETECTION_TYPE, PM4125_ANA_MBHC_ELECT, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_CTRL, PM4125_ANA_MBHC_PLUG_DETECT_CTL, 0x1F),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_COMP_CTRL, PM4125_ANA_MBHC_MECH, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PLUG_TYPE, PM4125_ANA_MBHC_MECH, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_GND_PLUG_TYPE, PM4125_ANA_MBHC_MECH, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_SW_HPH_LP_100K_TO_GND, PM4125_ANA_MBHC_MECH, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_SCHMT_ISRC, PM4125_ANA_MBHC_ELECT, 0x06),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_EN, PM4125_ANA_MBHC_ELECT, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_INSREM_DBNC, PM4125_ANA_MBHC_PLUG_DETECT_CTL, 0x0F),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_DBNC, PM4125_ANA_MBHC_CTL_1, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_VREF, PM4125_ANA_MBHC_CTL_2, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_COMP_RESULT, PM4125_ANA_MBHC_RESULT_3, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_IN2P_CLAMP_STATE, PM4125_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_SCHMT_RESULT, PM4125_ANA_MBHC_RESULT_3, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_SCHMT_RESULT, PM4125_ANA_MBHC_RESULT_3, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_SCHMT_RESULT, PM4125_ANA_MBHC_RESULT_3, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_RESULT, PM4125_ANA_MBHC_RESULT_3, 0x07),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_ISRC_CTL, PM4125_ANA_MBHC_ELECT, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_RESULT, PM4125_ANA_MBHC_RESULT_3, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB_CTRL, PM4125_ANA_MICBIAS_MICB_1_2_EN, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_PA_EN, PM4125_ANA_HPHPA_CNP_CTL_2, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PA_EN, PM4125_ANA_HPHPA_CNP_CTL_2, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_PA_EN, PM4125_ANA_HPHPA_CNP_CTL_2, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_SWCH_LEVEL_REMOVE, PM4125_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_STATUS, PM4125_ANA_MBHC_FSM_STATUS, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_MUX_CTL, PM4125_ANA_MBHC_CTL_2, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_MOISTURE_STATUS, PM4125_ANA_MBHC_FSM_STATUS, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_DET_EN, PM4125_ANA_HPHPA_CNP_CTL_2, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_DET_EN, PM4125_ANA_HPHPA_CNP_CTL_2, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_STATUS, PM4125_DIG_SWR_INTR_STATUS_0, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_STATUS, PM4125_DIG_SWR_INTR_STATUS_0, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_EN, PM4125_ANA_MBHC_CTL_1, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_COMPLETE, PM4125_ANA_MBHC_FSM_STATUS, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_TIMEOUT, PM4125_ANA_MBHC_FSM_STATUS, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_RESULT, PM4125_ANA_MBHC_ADC_RESULT, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB2_VOUT, PM4125_ANA_MICBIAS_LDO_1_SETTING, 0x3F),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_MODE, PM4125_ANA_MBHC_CTL_1, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_DETECTION_DONE, PM4125_ANA_MBHC_CTL_1, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_ISRC_EN, PM4125_ANA_MBHC_ZDET, 0x02),
};

static const struct regmap_irq pm4125_irqs[PM4125_NUM_IRQS] = {
	REGMAP_IRQ_REG(PM4125_IRQ_MBHC_BUTTON_PRESS_DET, 0, BIT(0)),
	REGMAP_IRQ_REG(PM4125_IRQ_MBHC_BUTTON_RELEASE_DET, 0, BIT(1)),
	REGMAP_IRQ_REG(PM4125_IRQ_MBHC_ELECT_INS_REM_DET, 0, BIT(2)),
	REGMAP_IRQ_REG(PM4125_IRQ_MBHC_ELECT_INS_REM_LEG_DET, 0, BIT(3)),
	REGMAP_IRQ_REG(PM4125_IRQ_MBHC_SW_DET, 0, BIT(4)),
	REGMAP_IRQ_REG(PM4125_IRQ_HPHR_OCP_INT, 0, BIT(5)),
	REGMAP_IRQ_REG(PM4125_IRQ_HPHR_CNP_INT, 0, BIT(6)),
	REGMAP_IRQ_REG(PM4125_IRQ_HPHL_OCP_INT, 0, BIT(7)),
	REGMAP_IRQ_REG(PM4125_IRQ_HPHL_CNP_INT, 1, BIT(0)),
	REGMAP_IRQ_REG(PM4125_IRQ_EAR_CNP_INT, 1, BIT(1)),
	REGMAP_IRQ_REG(PM4125_IRQ_EAR_SCD_INT, 1, BIT(2)),
	REGMAP_IRQ_REG(PM4125_IRQ_AUX_CNP_INT, 1, BIT(3)),
	REGMAP_IRQ_REG(PM4125_IRQ_AUX_SCD_INT, 1, BIT(4)),
	REGMAP_IRQ_REG(PM4125_IRQ_HPHL_PDM_WD_INT, 1, BIT(5)),
	REGMAP_IRQ_REG(PM4125_IRQ_HPHR_PDM_WD_INT, 1, BIT(6)),
	REGMAP_IRQ_REG(PM4125_IRQ_AUX_PDM_WD_INT, 1, BIT(7)),
	REGMAP_IRQ_REG(PM4125_IRQ_LDORT_SCD_INT, 2, BIT(0)),
	REGMAP_IRQ_REG(PM4125_IRQ_MBHC_MOISTURE_INT, 2, BIT(1)),
	REGMAP_IRQ_REG(PM4125_IRQ_HPHL_SURGE_DET_INT, 2, BIT(2)),
	REGMAP_IRQ_REG(PM4125_IRQ_HPHR_SURGE_DET_INT, 2, BIT(3)),
};

static int pm4125_handle_post_irq(void *data)
{
	struct pm4125_priv *pm4125 = (struct pm4125_priv *)data;

	regmap_write(pm4125->regmap, PM4125_DIG_SWR_INTR_CLEAR_0, 0);
	regmap_write(pm4125->regmap, PM4125_DIG_SWR_INTR_CLEAR_1, 0);
	regmap_write(pm4125->regmap, PM4125_DIG_SWR_INTR_CLEAR_2, 0);

	return IRQ_HANDLED;
}

static const u32 pm4125_config_regs[] = {
	PM4125_DIG_SWR_INTR_LEVEL_0,
};

static struct regmap_irq_chip pm4125_regmap_irq_chip = {
	.name = "pm4125",
	.irqs = pm4125_irqs,
	.num_irqs = ARRAY_SIZE(pm4125_irqs),
	.num_regs = 3,
	.status_base = PM4125_DIG_SWR_INTR_STATUS_0,
	.mask_base = PM4125_DIG_SWR_INTR_MASK_0,
	.ack_base = PM4125_DIG_SWR_INTR_CLEAR_0,
	.use_ack = 1,
	.clear_ack = 1,
	.config_base = pm4125_config_regs,
	.num_config_bases = ARRAY_SIZE(pm4125_config_regs),
	.num_config_regs = 1,
	.runtime_pm = true,
	.handle_post_irq = pm4125_handle_post_irq,
};

static void pm4125_reset(struct pm4125_priv *pm4125)
{
	regmap_write(pm4125->spmi_regmap, PM4125_CODEC_RESET_REG, PM4125_CODEC_OFF);
	usleep_range(20, 30);
	regmap_write(pm4125->spmi_regmap, PM4125_CODEC_RESET_REG, PM4125_CODEC_ON);
	usleep_range(5000, 5010);
}

static void pm4125_io_init(struct regmap *regmap)
{
	/* Disable HPH OCP */
	regmap_update_bits(regmap, PM4125_ANA_HPHPA_CNP_CTL_2,
			   PM4125_ANA_HPHPA_CNP_OCP_EN_L_MASK | PM4125_ANA_HPHPA_CNP_OCP_EN_R_MASK,
			   PM4125_ANA_HPHPA_CNP_OCP_DISABLE);

	/* Enable surge protection */
	regmap_update_bits(regmap, PM4125_ANA_SURGE_EN, PM4125_ANA_SURGE_PROTECTION_HPHL_MASK,
			   FIELD_PREP(PM4125_ANA_SURGE_PROTECTION_HPHL_MASK,
				      PM4125_ANA_SURGE_PROTECTION_ENABLE));
	regmap_update_bits(regmap, PM4125_ANA_SURGE_EN, PM4125_ANA_SURGE_PROTECTION_HPHR_MASK,
			   FIELD_PREP(PM4125_ANA_SURGE_PROTECTION_HPHR_MASK,
				      PM4125_ANA_SURGE_PROTECTION_ENABLE));

	/* Disable mic bias 2 pull down */
	regmap_update_bits(regmap, PM4125_ANA_MICBIAS_MICB_1_2_EN,
			   PM4125_ANA_MICBIAS_MICB2_PULL_DN_MASK,
			   FIELD_PREP(PM4125_ANA_MICBIAS_MICB2_PULL_DN_MASK,
				      PM4125_ANA_MICBIAS_MICB_PULL_DISABLE));
}

static int pm4125_global_mbias_disable(struct snd_soc_component *component)
{
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	if (atomic_dec_and_test(&pm4125->gloal_mbias_cnt)) {

		snd_soc_component_write_field(component, PM4125_ANA_MBIAS_EN,
					      PM4125_ANA_MBIAS_EN_V2I_MASK,
					      PM4125_ANA_MBIAS_EN_DISABLE);
		snd_soc_component_write_field(component, PM4125_ANA_MBIAS_EN,
					      PM4125_ANA_MBIAS_EN_GLOBAL_MASK,
					      PM4125_ANA_MBIAS_EN_DISABLE);
	}

	return 0;
}

static int pm4125_global_mbias_enable(struct snd_soc_component *component)
{
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	if (atomic_inc_return(&pm4125->gloal_mbias_cnt) == 1) {
		snd_soc_component_write_field(component, PM4125_ANA_MBIAS_EN,
					      PM4125_ANA_MBIAS_EN_GLOBAL_MASK,
					      PM4125_ANA_MBIAS_EN_ENABLE);
		snd_soc_component_write_field(component, PM4125_ANA_MBIAS_EN,
					      PM4125_ANA_MBIAS_EN_V2I_MASK,
					      PM4125_ANA_MBIAS_EN_ENABLE);
		usleep_range(1000, 1100);
	}

	return 0;
}

static int pm4125_rx_clk_enable(struct snd_soc_component *component)
{
	pm4125_global_mbias_enable(component);

	snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
				      PM4125_DIG_SWR_ANA_RX_CLK_EN_MASK,
				      PM4125_DIG_SWR_RX_CLK_ENABLE);
	snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
				      PM4125_DIG_SWR_ANA_RX_DIV2_CLK_EN_MASK,
				      PM4125_DIG_SWR_RX_CLK_ENABLE);
	usleep_range(5000, 5100);

	snd_soc_component_write_field(component, PM4125_ANA_HPHPA_FSM_CLK,
				      PM4125_ANA_HPHPA_FSM_DIV_RATIO_MASK,
				      PM4125_ANA_HPHPA_FSM_DIV_RATIO_68);
	snd_soc_component_write_field(component, PM4125_ANA_HPHPA_FSM_CLK,
				      PM4125_ANA_HPHPA_FSM_CLK_DIV_EN_MASK,
				      PM4125_ANA_HPHPA_FSM_CLK_DIV_ENABLE);
	snd_soc_component_update_bits(component, PM4125_ANA_NCP_VCTRL, 0x07, 0x06);
	snd_soc_component_write_field(component, PM4125_ANA_NCP_EN,
				      PM4125_ANA_NCP_ENABLE_MASK,
				      PM4125_ANA_NCP_ENABLE);
	usleep_range(500, 510);

	return 0;
}

static int pm4125_rx_clk_disable(struct snd_soc_component *component)
{

	snd_soc_component_write_field(component, PM4125_ANA_HPHPA_FSM_CLK,
				      PM4125_ANA_HPHPA_FSM_CLK_DIV_EN_MASK,
				      PM4125_ANA_HPHPA_FSM_CLK_DIV_DISABLE);
	snd_soc_component_write_field(component, PM4125_ANA_HPHPA_FSM_CLK,
				      PM4125_ANA_HPHPA_FSM_DIV_RATIO_MASK,
				      0x00);
	snd_soc_component_write_field(component, PM4125_ANA_NCP_EN,
				      PM4125_ANA_NCP_ENABLE_MASK,
				      PM4125_ANA_NCP_DISABLE);
	snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
				      PM4125_DIG_SWR_ANA_RX_DIV2_CLK_EN_MASK,
				      PM4125_DIG_SWR_RX_CLK_DISABLE);
	snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
				      PM4125_DIG_SWR_ANA_RX_CLK_EN_MASK,
				      PM4125_DIG_SWR_RX_CLK_DISABLE);

	pm4125_global_mbias_disable(component);

	return 0;
}


static int pm4125_codec_enable_rxclk(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
				     int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		pm4125_rx_clk_enable(component);
		break;
	case SND_SOC_DAPM_POST_PMD:
		pm4125_rx_clk_disable(component);
		break;
	}

	return 0;
}

static int pm4125_codec_hphl_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, PM4125_ANA_HPHPA_CNP_CTL_1,
					      PM4125_ANA_HPHPA_CNP_CTL_1_EN_MASK,
					      PM4125_ANA_HPHPA_CNP_CTL_1_EN);
		snd_soc_component_write_field(component, PM4125_SWR_HPHPA_HD2,
					      PM4125_SWR_HPHPA_HD2_LEFT_MASK,
					      PM4125_SWR_HPHPA_HD2_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMU:
		if (pm4125->comp1_enable) {
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_COMP_CTL_0,
						      PM4125_DIG_SWR_COMP_HPHL_EN_MASK,
						      PM4125_DIG_SWR_COMP_ENABLE);

			if (pm4125->comp2_enable)
				snd_soc_component_write_field(component,
							      PM4125_DIG_SWR_CDC_COMP_CTL_0,
							      PM4125_DIG_SWR_COMP_HPHR_EN_MASK,
							      PM4125_DIG_SWR_COMP_ENABLE);
			/*
			 * 5ms sleep is required after COMP is enabled as per
			 * HW requirement
			 */
			usleep_range(5000, 5100);
		} else {
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_COMP_CTL_0,
						      PM4125_DIG_SWR_COMP_HPHL_EN_MASK,
						      PM4125_DIG_SWR_COMP_DISABLE);
		}
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX0_CTL,
					      PM4125_DIG_SWR_DSM_DITHER_EN_MASK,
					      PM4125_DIG_SWR_DSM_DITHER_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_GAIN_CTL,
					      PM4125_DIG_SWR_RX0_EN_MASK,
					      PM4125_DIG_SWR_RX_INPUT_ENABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
					      PM4125_DIG_SWR_RX0_CLK_EN_MASK,
					      PM4125_DIG_SWR_RX_CLK_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
					      PM4125_DIG_SWR_RX0_CLK_EN_MASK,
					      PM4125_DIG_SWR_RX_CLK_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_GAIN_CTL,
					      PM4125_DIG_SWR_RX0_EN_MASK,
					      PM4125_DIG_SWR_RX_INPUT_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX0_CTL,
					      PM4125_DIG_SWR_DSM_DITHER_EN_MASK,
					      PM4125_DIG_SWR_DSM_DITHER_ENABLE);
		if (pm4125->comp1_enable)
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_COMP_CTL_0,
						      PM4125_DIG_SWR_COMP_HPHL_EN_MASK,
						      PM4125_DIG_SWR_COMP_DISABLE);
		break;
	}

	return 0;
}

static int pm4125_codec_hphr_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, PM4125_ANA_HPHPA_CNP_CTL_1,
					      PM4125_ANA_HPHPA_CNP_CTL_1_EN_MASK,
					      PM4125_ANA_HPHPA_CNP_CTL_1_EN);
		snd_soc_component_write_field(component, PM4125_SWR_HPHPA_HD2,
					      PM4125_SWR_HPHPA_HD2_RIGHT_MASK,
					      PM4125_SWR_HPHPA_HD2_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMU:
		if (pm4125->comp2_enable) {
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_COMP_CTL_0,
						      PM4125_DIG_SWR_COMP_HPHR_EN_MASK,
						      PM4125_DIG_SWR_COMP_ENABLE);
			if (pm4125->comp1_enable)
				snd_soc_component_write_field(component,
							      PM4125_DIG_SWR_CDC_COMP_CTL_0,
							      PM4125_DIG_SWR_COMP_HPHL_EN_MASK,
							      PM4125_DIG_SWR_COMP_ENABLE);
			/*
			 * 5ms sleep is required after COMP is enabled
			 * as per HW requirement
			 */
			usleep_range(5000, 5100);
		} else {
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_COMP_CTL_0,
						      PM4125_DIG_SWR_COMP_HPHR_EN_MASK,
						      PM4125_DIG_SWR_COMP_DISABLE);
		}
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX1_CTL,
					      PM4125_DIG_SWR_DSM_DITHER_EN_MASK,
					      PM4125_DIG_SWR_DSM_DITHER_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_GAIN_CTL,
					      PM4125_DIG_SWR_RX1_EN_MASK,
					      PM4125_DIG_SWR_RX_INPUT_ENABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
					      PM4125_DIG_SWR_RX1_CLK_EN_MASK,
					      PM4125_DIG_SWR_RX_CLK_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
					      PM4125_DIG_SWR_RX1_CLK_EN_MASK,
					      PM4125_DIG_SWR_RX_CLK_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_GAIN_CTL,
					      PM4125_DIG_SWR_RX1_EN_MASK,
					      PM4125_DIG_SWR_RX_INPUT_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX1_CTL,
					      PM4125_DIG_SWR_DSM_DITHER_EN_MASK,
					      PM4125_DIG_SWR_DSM_DITHER_ENABLE);
		if (pm4125->comp2_enable)
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_COMP_CTL_0,
						      PM4125_DIG_SWR_COMP_HPHR_EN_MASK,
						      PM4125_DIG_SWR_COMP_DISABLE);
		break;
	}

	return 0;
}

static int pm4125_codec_ear_lo_dac_event(struct snd_soc_dapm_widget *w,
					 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX0_CTL,
					      PM4125_DIG_SWR_DSM_DITHER_EN_MASK,
					      PM4125_DIG_SWR_DSM_DITHER_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
					      PM4125_DIG_SWR_RX0_CLK_EN_MASK,
					      PM4125_DIG_SWR_RX_CLK_ENABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_GAIN_CTL,
					      PM4125_DIG_SWR_RX0_EN_MASK,
					      PM4125_DIG_SWR_RX_INPUT_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_CLK_CTL,
					      PM4125_DIG_SWR_RX0_CLK_EN_MASK,
					      PM4125_DIG_SWR_RX_CLK_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX_GAIN_CTL,
					      PM4125_DIG_SWR_RX0_EN_MASK,
					      PM4125_DIG_SWR_RX_INPUT_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_RX0_CTL,
					      PM4125_DIG_SWR_DSM_DITHER_EN_MASK,
					      PM4125_DIG_SWR_DSM_DITHER_ENABLE);
		break;
	}

	return 0;
}


static int pm4125_codec_enable_hphl_wdt_irq(struct snd_soc_dapm_widget *w,
					    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(5000, 5100);
		enable_irq(pm4125->hphl_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(pm4125->hphl_pdm_wd_int);
		break;
	}

	return 0;
}

static int pm4125_codec_enable_hphr_wdt_irq(struct snd_soc_dapm_widget *w,
					    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(5000, 5100);
		enable_irq(pm4125->hphr_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(pm4125->hphr_pdm_wd_int);
		break;
	}

	return 0;
}

static int pm4125_codec_enable_hphr_pa(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		usleep_range(200, 210);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_PDM_WD_CTL1,
					      PM4125_WDT_ENABLE_MASK,
					      (PM4125_WDT_ENABLE_RX1_M | PM4125_WDT_ENABLE_RX1_L));
		break;
	case SND_SOC_DAPM_POST_PMD:
		usleep_range(5000, 5100);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_PDM_WD_CTL1,
					      PM4125_WDT_ENABLE_MASK, 0x00);
		break;
	}

	return 0;
}

static int pm4125_codec_enable_hphl_pa(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		usleep_range(200, 210);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_PDM_WD_CTL0,
					      PM4125_WDT_ENABLE_MASK,
					      (PM4125_WDT_ENABLE_RX0_M | PM4125_WDT_ENABLE_RX0_L));
		break;
	case SND_SOC_DAPM_POST_PMD:
		usleep_range(5000, 5100);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_PDM_WD_CTL0,
					      PM4125_WDT_ENABLE_MASK, 0x00);
		break;
	}

	return 0;
}

static int pm4125_codec_enable_lo_pa(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, PM4125_ANA_COMBOPA_CTL_5, 0x04, 0x00);
		usleep_range(1000, 1010);
		snd_soc_component_update_bits(component, PM4125_ANA_COMBOPA_CTL_4, 0x0F, 0x0F);
		usleep_range(1000, 1010);
		snd_soc_component_write_field(component, PM4125_ANA_COMBOPA_CTL,
					      PM4125_ANA_COMBO_PA_SELECT_MASK,
					      PM4125_ANA_COMBO_PA_SELECT_LO);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_PDM_WD_CTL0,
					      PM4125_WDT_ENABLE_MASK,
					      (PM4125_WDT_ENABLE_RX0_M | PM4125_WDT_ENABLE_RX0_L));
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(5000, 5010);
		snd_soc_component_update_bits(component, PM4125_ANA_COMBOPA_CTL_4, 0x0F, 0x04);
		break;
	case SND_SOC_DAPM_POST_PMD:
		usleep_range(2000, 2010);
		snd_soc_component_write_field(component, PM4125_ANA_COMBOPA_CTL,
					      PM4125_ANA_COMBO_PA_SELECT_MASK,
					      PM4125_ANA_COMBO_PA_SELECT_EAR);
		usleep_range(5000, 5100);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_PDM_WD_CTL0,
					      PM4125_WDT_ENABLE_MASK, 0x00);
		break;
	}

	return 0;
}

static int pm4125_codec_enable_ear_pa(struct snd_soc_dapm_widget *w,
				      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, PM4125_ANA_COMBOPA_CTL_5, 0x04, 0x00);
		usleep_range(1000, 1010);
		snd_soc_component_update_bits(component, PM4125_ANA_COMBOPA_CTL_4, 0x0F, 0x0F);
		usleep_range(1000, 1010);
		snd_soc_component_update_bits(component, PM4125_ANA_COMBOPA_CTL,
					      PM4125_ANA_COMBO_PA_SELECT_MASK,
					      PM4125_ANA_COMBO_PA_SELECT_EAR);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_PDM_WD_CTL0,
					      PM4125_WDT_ENABLE_MASK,
					      (PM4125_WDT_ENABLE_RX0_M | PM4125_WDT_ENABLE_RX0_L));
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(5000, 5010);
		snd_soc_component_update_bits(component, PM4125_ANA_COMBOPA_CTL_4, 0x0F, 0x04);
		break;
	case SND_SOC_DAPM_POST_PMD:
		usleep_range(5000, 5010);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_PDM_WD_CTL0,
					      PM4125_WDT_ENABLE_MASK, 0x00);
		break;
	}

	return 0;
}

static int pm4125_get_micb_vout_ctl_val(struct device *dev, u32 micb_mv)
{
	if (micb_mv < 1600 || micb_mv > 2850) {
		dev_err(dev, "%s: unsupported micbias voltage (%u mV)\n", __func__, micb_mv);
		return -EINVAL;
	}

	return (micb_mv - 1600) / 50;
}

static int pm4125_codec_enable_adc(struct snd_soc_dapm_widget *w,
				   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable BCS for Headset mic */
		if (w->shift == 1 &&
			!(snd_soc_component_read(component, PM4125_ANA_TX_AMIC2) & 0x10)) {
			set_bit(AMIC2_BCS_ENABLE, &pm4125->status_mask);
		}
		pm4125_global_mbias_enable(component);
		if (w->shift)
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_TX_ANA_MODE_0_1,
						      PM4125_DIG_SWR_TX_ANA_TXD1_MODE_MASK,
						      PM4125_DIG_SWR_TXD_MODE_NORMAL);
		else
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_TX_ANA_MODE_0_1,
						      PM4125_DIG_SWR_TX_ANA_TXD0_MODE_MASK,
						      PM4125_DIG_SWR_TXD_MODE_NORMAL);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (w->shift == 1 && test_bit(AMIC2_BCS_ENABLE, &pm4125->status_mask))
			clear_bit(AMIC2_BCS_ENABLE, &pm4125->status_mask);

		if (w->shift)
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_TX_ANA_MODE_0_1,
						      PM4125_DIG_SWR_TX_ANA_TXD1_MODE_MASK,
						      0x00);
		else
			snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_TX_ANA_MODE_0_1,
						      PM4125_DIG_SWR_TX_ANA_TXD0_MODE_MASK,
						      0x00);
		pm4125_global_mbias_disable(component);
		break;
	};

	return 0;
}

static int pm4125_codec_enable_dmic(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 dmic_clk_reg = w->reg;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_AMIC_CTL,
					      PM4125_DIG_SWR_AMIC_SELECT_MASK,
					      PM4125_DIG_SWR_AMIC_SELECT_DMIC1);
		snd_soc_component_update_bits(component, dmic_clk_reg,
					      PM4125_DIG_SWR_DMIC1_CLK_EN_MASK,
					      PM4125_DIG_SWR_DMIC1_CLK_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, dmic_clk_reg,
					      PM4125_DIG_SWR_DMIC1_CLK_EN_MASK,
					      PM4125_DIG_SWR_DMIC1_CLK_DISABLE);
		snd_soc_component_write_field(component, PM4125_DIG_SWR_CDC_AMIC_CTL,
					      PM4125_DIG_SWR_AMIC_SELECT_MASK,
					      PM4125_DIG_SWR_AMIC_SELECT_AMIC3);
		break;
	}

	return 0;
}

static int pm4125_micbias_control(struct snd_soc_component *component, int micb_num, int req,
				  bool is_dapm)
{
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);
	int micb_index = micb_num - 1;
	u16 micb_reg;
	u8 pullup_mask = 0, enable_mask = 0;

	if ((micb_index < 0) || (micb_index > PM4125_MAX_MICBIAS - 1)) {
		dev_err(component->dev, "%s: Invalid micbias index, micb_ind:%d\n",
			__func__, micb_index);
		return -EINVAL;
	}
	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = PM4125_ANA_MICBIAS_MICB_1_2_EN;
		pullup_mask = PM4125_ANA_MICBIAS_MICB1_PULL_UP_MASK;
		enable_mask = 0x40;
		break;
	case MIC_BIAS_2:
		micb_reg = PM4125_ANA_MICBIAS_MICB_1_2_EN;
		pullup_mask = PM4125_ANA_MICBIAS_MICB2_PULL_UP_MASK;
		enable_mask = 0x04;
		break;
	case MIC_BIAS_3:
		micb_reg = PM4125_ANA_MICBIAS_MICB_3_EN;
		pullup_mask = 0x02;
		break;
	default:
		dev_err(component->dev, "%s: Invalid micbias number: %d\n",
			__func__, micb_num);
		return -EINVAL;
	};

	switch (req) {
	case MICB_PULLUP_ENABLE:
		pm4125->pullup_ref[micb_index]++;
		if ((pm4125->pullup_ref[micb_index] == 1) &&
		    (pm4125->micb_ref[micb_index] == 0))
			snd_soc_component_update_bits(component, micb_reg,
						      pullup_mask, pullup_mask);
		break;
	case MICB_PULLUP_DISABLE:
		if (pm4125->pullup_ref[micb_index] > 0)
			pm4125->pullup_ref[micb_index]--;
		if ((pm4125->pullup_ref[micb_index] == 0) &&
		    (pm4125->micb_ref[micb_index] == 0))
			snd_soc_component_update_bits(component, micb_reg,
						      pullup_mask, 0x00);
		break;
	case MICB_ENABLE:
		pm4125->micb_ref[micb_index]++;
		if (pm4125->micb_ref[micb_index] == 1) {
			pm4125_global_mbias_enable(component);
			snd_soc_component_update_bits(component, micb_reg,
						      enable_mask, enable_mask);
		}
		break;
	case MICB_DISABLE:
		if (pm4125->micb_ref[micb_index] > 0)
			pm4125->micb_ref[micb_index]--;
		if ((pm4125->micb_ref[micb_index] == 0) &&
		    (pm4125->pullup_ref[micb_index] > 0)) {
			snd_soc_component_update_bits(component, micb_reg,
						      pullup_mask, pullup_mask);
			snd_soc_component_update_bits(component, micb_reg,
						      enable_mask, 0x00);
			pm4125_global_mbias_disable(component);
		} else if ((pm4125->micb_ref[micb_index] == 0) &&
			   (pm4125->pullup_ref[micb_index] == 0)) {
			snd_soc_component_update_bits(component, micb_reg,
						      enable_mask, 0x00);
			pm4125_global_mbias_disable(component);
		}
		break;
	};

	return 0;
}

static int pm4125_codec_enable_micbias(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
				       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (micb_num == MIC_BIAS_3)
			pm4125_micbias_control(component, micb_num, MICB_PULLUP_ENABLE, true);
		else
			pm4125_micbias_control(component, micb_num, MICB_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (micb_num == MIC_BIAS_3)
			pm4125_micbias_control(component, micb_num, MICB_PULLUP_DISABLE, true);
		else
			pm4125_micbias_control(component, micb_num, MICB_DISABLE, true);
		break;
	}

	return 0;
}

static int pm4125_codec_enable_micbias_pullup(struct snd_soc_dapm_widget *w,
					      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		pm4125_micbias_control(component, micb_num, MICB_PULLUP_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		pm4125_micbias_control(component, micb_num, MICB_PULLUP_DISABLE, true);
		break;
	}

	return 0;
}

static int pm4125_connect_port(struct pm4125_sdw_priv *sdw_priv, u8 port_idx, u8 ch_id, bool enable)
{
	struct sdw_port_config *port_config = &sdw_priv->port_config[port_idx - 1];
	const struct pm4125_sdw_ch_info *ch_info = &sdw_priv->ch_info[ch_id];
	struct sdw_slave *sdev = sdw_priv->sdev;
	u8 port_num = ch_info->port_num;
	u8 ch_mask = ch_info->ch_mask;
	u8 mstr_port_num, mstr_ch_mask;

	port_config->num = port_num;

	mstr_port_num = sdev->m_port_map[port_num];
	mstr_ch_mask = ch_info->master_ch_mask;

	if (enable) {
		port_config->ch_mask |= ch_mask;
		sdw_priv->master_channel_map[mstr_port_num] |= mstr_ch_mask;
	} else {
		port_config->ch_mask &= ~ch_mask;
		sdw_priv->master_channel_map[mstr_port_num] &= ~mstr_ch_mask;
	}

	return 0;
}

static int pm4125_get_compander(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc;
	bool hphr;

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;

	ucontrol->value.integer.value[0] = hphr ? pm4125->comp2_enable : pm4125->comp1_enable;
	return 0;
}

static int pm4125_set_compander(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);
	struct pm4125_sdw_priv *sdw_priv = pm4125->sdw_priv[AIF1_PB];
	int value = ucontrol->value.integer.value[0];
	struct soc_mixer_control *mc;
	int portidx;
	bool hphr;

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;

	if (hphr) {
		if (value == pm4125->comp2_enable)
			return 0;

		pm4125->comp2_enable = value;
	} else {
		if (value == pm4125->comp1_enable)
			return 0;

		pm4125->comp1_enable = value;
	}

	portidx = sdw_priv->ch_info[mc->reg].port_num;

	pm4125_connect_port(sdw_priv, portidx, mc->reg, value ? true : false);

	return 1;
}

static int pm4125_get_swr_port(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(comp);
	struct pm4125_sdw_priv *sdw_priv;
	int dai_id = mixer->shift;
	int ch_idx = mixer->reg;
	int portidx;

	sdw_priv = pm4125->sdw_priv[dai_id];
	portidx = sdw_priv->ch_info[ch_idx].port_num;

	ucontrol->value.integer.value[0] = sdw_priv->port_enable[portidx];

	return 0;
}

static int pm4125_set_swr_port(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(comp);
	struct pm4125_sdw_priv *sdw_priv;
	int dai_id = mixer->shift;
	int ch_idx = mixer->reg;
	int portidx;
	bool enable;

	sdw_priv = pm4125->sdw_priv[dai_id];

	portidx = sdw_priv->ch_info[ch_idx].port_num;

	enable = ucontrol->value.integer.value[0];

	if (enable == sdw_priv->port_enable[portidx]) {
		pm4125_connect_port(sdw_priv, portidx, ch_idx, enable);
		return 0;
	}

	sdw_priv->port_enable[portidx] = enable;
	pm4125_connect_port(sdw_priv, portidx, ch_idx, enable);

	return 1;
}

static void pm4125_mbhc_bias_control(struct snd_soc_component *component, bool enable)
{
	snd_soc_component_write_field(component, PM4125_ANA_MBHC_ELECT,
				      PM4125_ANA_MBHC_ELECT_BIAS_EN_MASK,
				      enable ? PM4125_ANA_MBHC_ELECT_BIAS_ENABLE :
					       PM4125_ANA_MBHC_ELECT_BIAS_DISABLE);
}

static void pm4125_mbhc_program_btn_thr(struct snd_soc_component *component,
					int *btn_low, int *btn_high,
					int num_btn, bool is_micbias)
{
	int i, vth;

	if (num_btn > WCD_MBHC_DEF_BUTTONS) {
		dev_err(component->dev, "%s: invalid number of buttons: %d\n",
			__func__, num_btn);
		return;
	}

	for (i = 0; i < num_btn; i++) {
		vth = ((btn_high[i] * 2) / 25) & 0x3F;
		snd_soc_component_write_field(component, PM4125_ANA_MBHC_BTN0_ZDET_VREF1 + i,
					      PM4125_ANA_MBHC_BTN0_THRESHOLD_MASK, vth << 2);
	}
}

static const struct wcd_mbhc_cb mbhc_cb = {
	.mbhc_bias = pm4125_mbhc_bias_control,
	.set_btn_thr = pm4125_mbhc_program_btn_thr,
};

static int pm4125_mbhc_init(struct snd_soc_component *component)
{
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);
	struct wcd_mbhc_intr *intr_ids = &pm4125->intr_ids;

	intr_ids->mbhc_sw_intr = regmap_irq_get_virq(pm4125->irq_chip, PM4125_IRQ_MBHC_SW_DET);

	intr_ids->mbhc_btn_press_intr = regmap_irq_get_virq(pm4125->irq_chip,
							    PM4125_IRQ_MBHC_BUTTON_PRESS_DET);

	intr_ids->mbhc_btn_release_intr = regmap_irq_get_virq(pm4125->irq_chip,
							      PM4125_IRQ_MBHC_BUTTON_RELEASE_DET);

	intr_ids->mbhc_hs_ins_intr = regmap_irq_get_virq(pm4125->irq_chip,
							 PM4125_IRQ_MBHC_ELECT_INS_REM_LEG_DET);

	intr_ids->mbhc_hs_rem_intr = regmap_irq_get_virq(pm4125->irq_chip,
							 PM4125_IRQ_MBHC_ELECT_INS_REM_DET);

	intr_ids->hph_left_ocp = regmap_irq_get_virq(pm4125->irq_chip, PM4125_IRQ_HPHL_OCP_INT);

	intr_ids->hph_right_ocp = regmap_irq_get_virq(pm4125->irq_chip, PM4125_IRQ_HPHR_OCP_INT);

	pm4125->wcd_mbhc = wcd_mbhc_init(component, &mbhc_cb, intr_ids, pm4125_mbhc_fields, false);
	if (IS_ERR(pm4125->wcd_mbhc))
		return PTR_ERR(pm4125->wcd_mbhc);

	return 0;
}

static void pm4125_mbhc_deinit(struct snd_soc_component *component)
{
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	wcd_mbhc_deinit(pm4125->wcd_mbhc);
}

static const struct snd_kcontrol_new pm4125_snd_controls[] = {
	SOC_SINGLE_EXT("HPHL_COMP Switch", PM4125_COMP_L, 0, 1, 0,
		       pm4125_get_compander, pm4125_set_compander),
	SOC_SINGLE_EXT("HPHR_COMP Switch", PM4125_COMP_R, 1, 1, 0,
		       pm4125_get_compander, pm4125_set_compander),

	SOC_SINGLE_TLV("HPHL Volume", PM4125_ANA_HPHPA_L_GAIN, 0, 20, 1,
		       line_gain),
	SOC_SINGLE_TLV("HPHR Volume", PM4125_ANA_HPHPA_R_GAIN, 0, 20, 1,
		       line_gain),
	SOC_SINGLE_TLV("ADC1 Volume", PM4125_ANA_TX_AMIC1, 0, 8, 0,
		       analog_gain),
	SOC_SINGLE_TLV("ADC2 Volume", PM4125_ANA_TX_AMIC2, 0, 8, 0,
		       analog_gain),

	SOC_SINGLE_EXT("HPHL Switch", PM4125_HPH_L, 0, 1, 0,
		       pm4125_get_swr_port, pm4125_set_swr_port),
	SOC_SINGLE_EXT("HPHR Switch", PM4125_HPH_R, 0, 1, 0,
		       pm4125_get_swr_port, pm4125_set_swr_port),

	SOC_SINGLE_EXT("ADC1 Switch", PM4125_ADC1, 1, 1, 0,
		       pm4125_get_swr_port, pm4125_set_swr_port),
	SOC_SINGLE_EXT("ADC2 Switch", PM4125_ADC2, 1, 1, 0,
		       pm4125_get_swr_port, pm4125_set_swr_port),
};

static const struct snd_kcontrol_new adc1_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new adc2_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic1_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic2_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new ear_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new lo_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new hphl_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new hphr_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const char * const adc2_mux_text[] = {
	"INP2", "INP3"
};

static const struct soc_enum adc2_enum = SOC_ENUM_SINGLE(PM4125_ANA_TX_AMIC2, 4,
							 ARRAY_SIZE(adc2_mux_text), adc2_mux_text);

static const struct snd_kcontrol_new tx_adc2_mux = SOC_DAPM_ENUM("ADC2 MUX Mux", adc2_enum);

static const struct snd_soc_dapm_widget pm4125_dapm_widgets[] = {
	/* Input widgets */
	SND_SOC_DAPM_INPUT("AMIC1"),
	SND_SOC_DAPM_INPUT("AMIC2"),
	SND_SOC_DAPM_INPUT("AMIC3"),
	SND_SOC_DAPM_INPUT("IN1_HPHL"),
	SND_SOC_DAPM_INPUT("IN2_HPHR"),

	/* TX widgets */
	SND_SOC_DAPM_ADC_E("ADC1", NULL, SND_SOC_NOPM, 0, 0, pm4125_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC2", NULL, SND_SOC_NOPM, 1, 0, pm4125_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("ADC2 MUX", SND_SOC_NOPM, 0, 0, &tx_adc2_mux),

	/* TX mixers */
	SND_SOC_DAPM_MIXER("ADC1_MIXER", SND_SOC_NOPM, 0, 0, adc1_switch, ARRAY_SIZE(adc1_switch)),
	SND_SOC_DAPM_MIXER("ADC2_MIXER", SND_SOC_NOPM, 1, 0, adc2_switch, ARRAY_SIZE(adc2_switch)),

	/* MIC_BIAS widgets */
	SND_SOC_DAPM_SUPPLY("MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0, pm4125_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0, pm4125_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0, pm4125_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY("PA_VPOS", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* RX widgets */
	SND_SOC_DAPM_PGA_E("EAR PGA", PM4125_ANA_COMBOPA_CTL, 7, 0, NULL, 0,
			   pm4125_codec_enable_ear_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LO PGA", PM4125_ANA_COMBOPA_CTL, 7, 0, NULL, 0,
			   pm4125_codec_enable_lo_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHL PGA", PM4125_ANA_HPHPA_CNP_CTL_2, 7, 0, NULL, 0,
			   pm4125_codec_enable_hphl_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHR PGA", PM4125_ANA_HPHPA_CNP_CTL_2, 6, 0, NULL, 0,
			   pm4125_codec_enable_hphr_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_DAC_E("RDAC1", NULL, SND_SOC_NOPM, 0, 0, pm4125_codec_hphl_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC2", NULL, SND_SOC_NOPM, 0, 0, pm4125_codec_hphr_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC3", NULL, SND_SOC_NOPM, 0, 0, pm4125_codec_ear_lo_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),


	SND_SOC_DAPM_SUPPLY("HPHL_WDT_IRQ", SND_SOC_NOPM, 0, 0, pm4125_codec_enable_hphl_wdt_irq,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("HPHR_WDT_IRQ", SND_SOC_NOPM, 0, 0, pm4125_codec_enable_hphr_wdt_irq,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("RXCLK", SND_SOC_NOPM, 0, 0, pm4125_codec_enable_rxclk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("RX1", SND_SOC_NOPM, 0, 0, NULL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("RX2", SND_SOC_NOPM, 0, 0, NULL, 0, NULL, 0),

	/* RX mixer widgets */
	SND_SOC_DAPM_MIXER("EAR_RDAC", SND_SOC_NOPM, 0, 0, ear_rdac_switch,
			   ARRAY_SIZE(ear_rdac_switch)),
	SND_SOC_DAPM_MIXER("LO_RDAC", SND_SOC_NOPM, 0, 0, lo_rdac_switch,
			   ARRAY_SIZE(lo_rdac_switch)),
	SND_SOC_DAPM_MIXER("HPHL_RDAC", SND_SOC_NOPM, 0, 0, hphl_rdac_switch,
			   ARRAY_SIZE(hphl_rdac_switch)),
	SND_SOC_DAPM_MIXER("HPHR_RDAC", SND_SOC_NOPM, 0, 0, hphr_rdac_switch,
			   ARRAY_SIZE(hphr_rdac_switch)),

	/* TX output widgets */
	SND_SOC_DAPM_OUTPUT("ADC1_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("ADC2_OUTPUT"),

	/* RX output widgets */
	SND_SOC_DAPM_OUTPUT("EAR"),
	SND_SOC_DAPM_OUTPUT("LO"),
	SND_SOC_DAPM_OUTPUT("HPHL"),
	SND_SOC_DAPM_OUTPUT("HPHR"),

	/* MIC_BIAS pull up widgets */
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0,
			    pm4125_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0,
			    pm4125_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0,
			    pm4125_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	/* TX widgets */
	SND_SOC_DAPM_ADC_E("DMIC1", NULL, PM4125_DIG_SWR_CDC_DMIC1_CTL, 0, 0,
			   pm4125_codec_enable_dmic, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC2", NULL, PM4125_DIG_SWR_CDC_DMIC1_CTL, 1, 0,
			   pm4125_codec_enable_dmic, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* TX mixer widgets */
	SND_SOC_DAPM_MIXER("DMIC1_MIXER", SND_SOC_NOPM, 0, 0, dmic1_switch,
			   ARRAY_SIZE(dmic1_switch)),
	SND_SOC_DAPM_MIXER("DMIC2_MIXER", SND_SOC_NOPM, 1, 0, dmic2_switch,
			   ARRAY_SIZE(dmic2_switch)),

	/* Output widgets */
	SND_SOC_DAPM_OUTPUT("DMIC1_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC2_OUTPUT"),
};

static const struct snd_soc_dapm_route pm4125_audio_map[] = {
	{ "ADC1_OUTPUT", NULL, "ADC1_MIXER" },
	{ "ADC1_MIXER", "Switch", "ADC1" },
	{ "ADC1", NULL, "AMIC1" },

	{ "ADC2_OUTPUT", NULL, "ADC2_MIXER" },
	{ "ADC2_MIXER", "Switch", "ADC2" },
	{ "ADC2", NULL, "ADC2 MUX" },
	{ "ADC2 MUX", "INP3", "AMIC3" },
	{ "ADC2 MUX", "INP2", "AMIC2" },

	{ "IN1_HPHL", NULL, "PA_VPOS" },
	{ "RX1", NULL, "IN1_HPHL" },
	{ "RX1", NULL, "RXCLK" },
	{ "RX1", NULL, "HPHL_WDT_IRQ" },
	{ "RDAC1", NULL, "RX1" },
	{ "HPHL_RDAC", "Switch", "RDAC1" },
	{ "HPHL PGA", NULL, "HPHL_RDAC" },
	{ "HPHL", NULL, "HPHL PGA" },

	{ "IN2_HPHR", NULL, "PA_VPOS" },
	{ "RX2", NULL, "IN2_HPHR" },
	{ "RX2", NULL, "RXCLK" },
	{ "RX2", NULL, "HPHR_WDT_IRQ" },
	{ "RDAC2", NULL, "RX2" },
	{ "HPHR_RDAC", "Switch", "RDAC2" },
	{ "HPHR PGA", NULL, "HPHR_RDAC" },
	{ "HPHR", NULL, "HPHR PGA" },

	{ "RDAC3", NULL, "RX1" },
	{ "EAR_RDAC", "Switch", "RDAC3" },
	{ "EAR PGA", NULL, "EAR_RDAC" },
	{ "EAR", NULL, "EAR PGA" },

	{ "LO_RDAC", "Switch", "RDAC3" },
	{ "LO PGA", NULL, "LO_RDAC" },
	{ "LO", NULL, "LO PGA" },

	{ "DMIC1_OUTPUT", NULL, "DMIC1_MIXER" },
	{ "DMIC1_MIXER", "Switch", "DMIC1" },

	{ "DMIC2_OUTPUT", NULL, "DMIC2_MIXER" },
	{ "DMIC2_MIXER", "Switch", "DMIC2" },
};

static int pm4125_set_micbias_data(struct device *dev, struct pm4125_priv *pm4125)
{
	int vout_ctl;

	/* Set micbias voltage */
	vout_ctl = pm4125_get_micb_vout_ctl_val(dev, pm4125->micb1_mv);
	if (vout_ctl < 0)
		return -EINVAL;

	regmap_update_bits(pm4125->regmap, PM4125_ANA_MICBIAS_LDO_1_SETTING,
			   PM4125_ANA_MICBIAS_MICB_OUT_VAL_MASK, vout_ctl << 3);
	return 0;
}

static irqreturn_t pm4125_wd_handle_irq(int irq, void *data)
{
	/*
	 * HPHR/HPHL Watchdog interrupt threaded handler
	 * Watchdog interrupts are expected to be enabled when switching on the HPHL/R
	 * in order to make sure the interrupts are acked by the regmap_irq handler
	 * io allow PDM sync. We could leave those interrupts masked but we would
	 * not haveany valid way to enable/disable them without violating irq layers.
	 *
	 * The HPHR/HPHL Watchdog interrupts are handled by regmap_irq, so requesting
	 * a threaded handler is the safest way to be able to ack those interrupts
	 * without colliding with the regmap_irq setup.
	 */
	return IRQ_HANDLED;
}

static const struct irq_chip pm4125_codec_irq_chip = {
	.name = "pm4125_codec",
};

static int pm4125_codec_irq_chip_map(struct irq_domain *irqd, unsigned int virq,
				     irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &pm4125_codec_irq_chip, handle_simple_irq);
	irq_set_nested_thread(virq, 1);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops pm4125_domain_ops = {
	.map = pm4125_codec_irq_chip_map,
};

static int pm4125_irq_init(struct pm4125_priv *pm4125, struct device *dev)
{
	pm4125->virq = irq_domain_add_linear(NULL, 1, &pm4125_domain_ops, NULL);
	if (!(pm4125->virq)) {
		dev_err(dev, "%s: Failed to add IRQ domain\n", __func__);
		return -EINVAL;
	}

	pm4125_regmap_irq_chip.irq_drv_data = pm4125;

	return devm_regmap_add_irq_chip(dev, pm4125->regmap, irq_create_mapping(pm4125->virq, 0),
					IRQF_ONESHOT, 0, &pm4125_regmap_irq_chip,
					&pm4125->irq_chip);
}

static int pm4125_soc_codec_probe(struct snd_soc_component *component)
{
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);
	struct sdw_slave *tx_sdw_dev = pm4125->tx_sdw_dev;
	struct device *dev = component->dev;
	unsigned long time_left;
	int i, ret;

	time_left = wait_for_completion_timeout(&tx_sdw_dev->initialization_complete,
						msecs_to_jiffies(5000));
	if (!time_left) {
		dev_err(dev, "soundwire device init timeout\n");
		return -ETIMEDOUT;
	}

	snd_soc_component_init_regmap(component, pm4125->regmap);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	pm4125_io_init(pm4125->regmap);

	/* Set all interrupts as edge triggered */
	for (i = 0; i < pm4125_regmap_irq_chip.num_regs; i++)
		regmap_write(pm4125->regmap, (PM4125_DIG_SWR_INTR_LEVEL_0 + i), 0);

	pm_runtime_put(dev);

	pm4125->hphr_pdm_wd_int = regmap_irq_get_virq(pm4125->irq_chip, PM4125_IRQ_HPHR_PDM_WD_INT);
	pm4125->hphl_pdm_wd_int = regmap_irq_get_virq(pm4125->irq_chip, PM4125_IRQ_HPHL_PDM_WD_INT);

	/* Request for watchdog interrupts */
	ret = devm_request_threaded_irq(dev, pm4125->hphr_pdm_wd_int, NULL, pm4125_wd_handle_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"HPHR PDM WDOG INT", pm4125);
	if (ret)
		dev_err(dev, "Failed to request HPHR wdt interrupt: %d\n", ret);

	ret = devm_request_threaded_irq(dev, pm4125->hphl_pdm_wd_int, NULL, pm4125_wd_handle_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"HPHL PDM WDOG INT", pm4125);
	if (ret)
		dev_err(dev, "Failed to request HPHL wdt interrupt: %d\n", ret);

	disable_irq_nosync(pm4125->hphr_pdm_wd_int);
	disable_irq_nosync(pm4125->hphl_pdm_wd_int);

	ret = pm4125_mbhc_init(component);
	if (ret)
		dev_err(component->dev, "mbhc initialization failed\n");

	return ret;
}

static void pm4125_soc_codec_remove(struct snd_soc_component *component)
{
	struct pm4125_priv *pm4125 = snd_soc_component_get_drvdata(component);

	pm4125_mbhc_deinit(component);
	free_irq(pm4125->hphl_pdm_wd_int, pm4125);
	free_irq(pm4125->hphr_pdm_wd_int, pm4125);
}

static int pm4125_codec_set_jack(struct snd_soc_component *comp, struct snd_soc_jack *jack,
				 void *data)
{
	struct pm4125_priv *pm4125 = dev_get_drvdata(comp->dev);
	int ret = 0;

	if (jack)
		ret = wcd_mbhc_start(pm4125->wcd_mbhc, &pm4125->mbhc_cfg, jack);
	else
		wcd_mbhc_stop(pm4125->wcd_mbhc);

	return ret;
}

static const struct snd_soc_component_driver soc_codec_dev_pm4125 = {
	.name = "pm4125_codec",
	.probe = pm4125_soc_codec_probe,
	.remove = pm4125_soc_codec_remove,
	.controls = pm4125_snd_controls,
	.num_controls = ARRAY_SIZE(pm4125_snd_controls),
	.dapm_widgets = pm4125_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(pm4125_dapm_widgets),
	.dapm_routes = pm4125_audio_map,
	.num_dapm_routes = ARRAY_SIZE(pm4125_audio_map),
	.set_jack = pm4125_codec_set_jack,
	.endianness = 1,
};

static void pm4125_dt_parse_micbias_info(struct device *dev, struct pm4125_priv *priv)
{
	struct device_node *np = dev->of_node;
	u32 prop_val = 0;
	int ret;

	ret = of_property_read_u32(np, "qcom,micbias1-microvolt", &prop_val);
	if (!ret)
		priv->micb1_mv = prop_val / 1000;
	else
		dev_warn(dev, "Micbias1 DT property not found\n");

	ret = of_property_read_u32(np, "qcom,micbias2-microvolt", &prop_val);
	if (!ret)
		priv->micb2_mv = prop_val / 1000;
	else
		dev_warn(dev, "Micbias2 DT property not found\n");

	ret = of_property_read_u32(np, "qcom,micbias3-microvolt", &prop_val);
	if (!ret)
		priv->micb3_mv = prop_val / 1000;
	else
		dev_warn(dev, "Micbias3 DT property not found\n");
}

static int pm4125_codec_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct pm4125_priv *pm4125 = dev_get_drvdata(dai->dev);
	struct pm4125_sdw_priv *sdw_priv = pm4125->sdw_priv[dai->id];

	return pm4125_sdw_hw_params(sdw_priv, substream, params, dai);
}

static int pm4125_codec_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct pm4125_priv *pm4125 = dev_get_drvdata(dai->dev);
	struct pm4125_sdw_priv *sdw_priv = pm4125->sdw_priv[dai->id];

	return sdw_stream_remove_slave(sdw_priv->sdev, sdw_priv->sruntime);
}

static int pm4125_codec_set_sdw_stream(struct snd_soc_dai *dai, void *stream, int direction)
{
	struct pm4125_priv *pm4125 = dev_get_drvdata(dai->dev);
	struct pm4125_sdw_priv *sdw_priv = pm4125->sdw_priv[dai->id];

	sdw_priv->sruntime = stream;

	return 0;
}

static int pm4125_get_channel_map(const struct snd_soc_dai *dai,
				  unsigned int *tx_num, unsigned int *tx_slot,
				  unsigned int *rx_num, unsigned int *rx_slot)
{
	struct pm4125_priv *pm4125 = dev_get_drvdata(dai->dev);
	struct pm4125_sdw_priv *sdw_priv = pm4125->sdw_priv[dai->id];
	int i;

	switch (dai->id) {
	case AIF1_PB:
		if (!rx_slot || !rx_num) {
			dev_err(dai->dev, "Invalid rx_slot %p or rx_num %p\n", rx_slot, rx_num);
			return -EINVAL;
		}

		for (i = 0; i < SDW_MAX_PORTS; i++)
			rx_slot[i] = sdw_priv->master_channel_map[i];

		*rx_num = i;
		break;
	case AIF1_CAP:
		if (!tx_slot || !tx_num) {
			dev_err(dai->dev, "Invalid tx_slot %p or tx_num %p\n", tx_slot, tx_num);
			return -EINVAL;
		}

		for (i = 0; i < SDW_MAX_PORTS; i++)
			tx_slot[i] = sdw_priv->master_channel_map[i];

		*tx_num = i;
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops pm4125_sdw_dai_ops = {
	.hw_params = pm4125_codec_hw_params,
	.hw_free = pm4125_codec_free,
	.set_stream = pm4125_codec_set_sdw_stream,
	.get_channel_map = pm4125_get_channel_map,
};

static struct snd_soc_dai_driver pm4125_dais[] = {
	[0] = {
		.name = "pm4125-sdw-rx",
		.playback = {
			.stream_name = "PM4125 AIF Playback",
			.rates = PM4125_RATES | PM4125_FRAC_RATES,
			.formats = PM4125_FORMATS,
			.rate_min = 8000,
			.rate_max = 384000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &pm4125_sdw_dai_ops,
	},
	[1] = {
		.name = "pm4125-sdw-tx",
		.capture = {
			.stream_name = "PM4125 AIF Capture",
			.rates = PM4125_RATES,
			.formats = PM4125_FORMATS,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &pm4125_sdw_dai_ops,
	},
};

static int pm4125_bind(struct device *dev)
{
	struct pm4125_priv *pm4125 = dev_get_drvdata(dev);
	struct device_link *devlink;
	int ret;

	/* Give the soundwire subdevices some more time to settle */
	usleep_range(15000, 15010);

	ret = component_bind_all(dev, pm4125);
	if (ret) {
		dev_err(dev, "Slave bind failed, ret = %d\n", ret);
		return ret;
	}

	pm4125->rxdev = pm4125_sdw_device_get(pm4125->rxnode);
	if (!pm4125->rxdev) {
		dev_err(dev, "could not find rxslave with matching of node\n");
		ret = -EINVAL;
		goto error_unbind_all;
	}

	pm4125->sdw_priv[AIF1_PB] = dev_get_drvdata(pm4125->rxdev);
	pm4125->sdw_priv[AIF1_PB]->pm4125 = pm4125;

	pm4125->txdev = pm4125_sdw_device_get(pm4125->txnode);
	if (!pm4125->txdev) {
		dev_err(dev, "could not find txslave with matching of node\n");
		ret = -EINVAL;
		goto error_unbind_all;
	}

	pm4125->sdw_priv[AIF1_CAP] = dev_get_drvdata(pm4125->txdev);
	pm4125->sdw_priv[AIF1_CAP]->pm4125 = pm4125;

	pm4125->tx_sdw_dev = dev_to_sdw_dev(pm4125->txdev);
	if (!pm4125->tx_sdw_dev) {
		dev_err(dev, "could not get txslave with matching of dev\n");
		ret = -EINVAL;
		goto error_unbind_all;
	}

	/*
	 * As TX is the main CSR reg interface, which should not be suspended first.
	 * expicilty add the dependency link
	 */
	devlink = device_link_add(pm4125->rxdev, pm4125->txdev,
				  DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
	if (!devlink) {
		dev_err(dev, "Could not devlink TX and RX\n");
		ret = -EINVAL;
		goto error_unbind_all;
	}

	devlink = device_link_add(dev, pm4125->txdev,
				  DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
	if (!devlink) {
		dev_err(dev, "Could not devlink PM4125 and TX\n");
		ret = -EINVAL;
		goto link_remove_rx_tx;
	}

	devlink = device_link_add(dev, pm4125->rxdev,
				  DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
	if (!devlink) {
		dev_err(dev, "Could not devlink PM4125 and RX\n");
		ret = -EINVAL;
		goto link_remove_dev_tx;
	}

	pm4125->regmap = dev_get_regmap(&pm4125->tx_sdw_dev->dev, NULL);
	if (!pm4125->regmap) {
		dev_err(dev, "could not get TX device regmap\n");
		ret = -EINVAL;
		goto link_remove_dev_rx;
	}

	ret = pm4125_irq_init(pm4125, dev);
	if (ret) {
		dev_err(dev, "IRQ init failed: %d\n", ret);
		goto link_remove_dev_rx;
	}

	pm4125->sdw_priv[AIF1_PB]->slave_irq = pm4125->virq;
	pm4125->sdw_priv[AIF1_CAP]->slave_irq = pm4125->virq;

	ret = pm4125_set_micbias_data(dev, pm4125);
	if (ret < 0) {
		dev_err(dev, "Bad micbias pdata\n");
		goto link_remove_dev_rx;
	}

	ret = snd_soc_register_component(dev, &soc_codec_dev_pm4125,
					 pm4125_dais, ARRAY_SIZE(pm4125_dais));
	if (!ret)
		return ret;

	dev_err(dev, "Codec registration failed\n");

link_remove_dev_rx:
	device_link_remove(dev, pm4125->rxdev);
link_remove_dev_tx:
	device_link_remove(dev, pm4125->txdev);
link_remove_rx_tx:
	device_link_remove(pm4125->rxdev, pm4125->txdev);
error_unbind_all:
	component_unbind_all(dev, pm4125);
	return ret;
}

static void pm4125_unbind(struct device *dev)
{
	struct pm4125_priv *pm4125 = dev_get_drvdata(dev);

	snd_soc_unregister_component(dev);
	device_link_remove(dev, pm4125->txdev);
	device_link_remove(dev, pm4125->rxdev);
	device_link_remove(pm4125->rxdev, pm4125->txdev);
	component_unbind_all(dev, pm4125);
}

static const struct component_master_ops pm4125_comp_ops = {
	.bind = pm4125_bind,
	.unbind = pm4125_unbind,
};

static int pm4125_add_slave_components(struct pm4125_priv *pm4125, struct device *dev,
				       struct component_match **matchptr)
{
	struct device_node *np = dev->of_node;

	pm4125->rxnode = of_parse_phandle(np, "qcom,rx-device", 0);
	if (!pm4125->rxnode)
		return dev_err_probe(dev, -ENODEV, "Couldn't parse phandle to qcom,rx-device\n");
	component_match_add_release(dev, matchptr, component_release_of, component_compare_of,
				    pm4125->rxnode);

	pm4125->txnode = of_parse_phandle(np, "qcom,tx-device", 0);
	if (!pm4125->txnode)
		return dev_err_probe(dev, -ENODEV, "Couldn't parse phandle to qcom,tx-device\n");
	component_match_add_release(dev, matchptr, component_release_of, component_compare_of,
				    pm4125->txnode);

	return 0;
}

static int pm4125_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct device *dev = &pdev->dev;
	struct pm4125_priv *pm4125;
	struct wcd_mbhc_config *cfg;
	int ret;

	pm4125 = devm_kzalloc(dev, sizeof(*pm4125), GFP_KERNEL);
	if (!pm4125)
		return -ENOMEM;

	dev_set_drvdata(dev, pm4125);

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(pm4125_power_supplies),
					     pm4125_power_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get and enable supplies\n");

	pm4125->spmi_regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pm4125->spmi_regmap)
		return -ENXIO;

	pm4125_reset(pm4125);

	pm4125_dt_parse_micbias_info(dev, pm4125);
	atomic_set(&pm4125->gloal_mbias_cnt, 0);

	cfg = &pm4125->mbhc_cfg;
	cfg->mbhc_micbias = MIC_BIAS_2;
	cfg->anc_micbias = MIC_BIAS_2;
	cfg->v_hs_max = WCD_MBHC_HS_V_MAX;
	cfg->num_btn = PM4125_MBHC_MAX_BUTTONS;
	cfg->micb_mv = pm4125->micb2_mv;
	cfg->linein_th = 5000;
	cfg->hs_thr = 1700;
	cfg->hph_thr = 50;

	wcd_dt_parse_mbhc_data(dev, &pm4125->mbhc_cfg);

	ret = pm4125_add_slave_components(pm4125, dev, &match);
	if (ret)
		return ret;

	ret = component_master_add_with_match(dev, &pm4125_comp_ops, match);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;
}

static void pm4125_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	component_master_del(&pdev->dev, &pm4125_comp_ops);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_dont_use_autosuspend(dev);
}

static const struct of_device_id pm4125_of_match[] = {
	{ .compatible = "qcom,pm4125-codec" },
	{ }
};
MODULE_DEVICE_TABLE(of, pm4125_of_match);

static struct platform_driver pm4125_codec_driver = {
	.probe = pm4125_probe,
	.remove = pm4125_remove,
	.driver = {
		.name = "pm4125_codec",
		.of_match_table = pm4125_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(pm4125_codec_driver);
MODULE_DESCRIPTION("PM4125 audio codec driver");
MODULE_LICENSE("GPL");
