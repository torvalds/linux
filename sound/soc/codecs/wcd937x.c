// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/component.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/soc-dapm.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "wcd-clsh-v2.h"
#include "wcd-mbhc-v2.h"
#include "wcd937x.h"

enum {
	CHIPID_WCD9370 = 0,
	CHIPID_WCD9375 = 5,
};

/* Z value defined in milliohm */
#define WCD937X_ZDET_VAL_32		(32000)
#define WCD937X_ZDET_VAL_400		(400000)
#define WCD937X_ZDET_VAL_1200		(1200000)
#define WCD937X_ZDET_VAL_100K		(100000000)
/* Z floating defined in ohms */
#define WCD937X_ZDET_FLOATING_IMPEDANCE	(0x0FFFFFFE)
#define WCD937X_ZDET_NUM_MEASUREMENTS	(900)
#define WCD937X_MBHC_GET_C1(c)		(((c) & 0xC000) >> 14)
#define WCD937X_MBHC_GET_X1(x)		((x) & 0x3FFF)
/* Z value compared in milliOhm */
#define WCD937X_MBHC_IS_SECOND_RAMP_REQUIRED(z)	(((z) > 400000) || ((z) < 32000))
#define WCD937X_MBHC_ZDET_CONST		(86 * 16384)
#define WCD937X_MBHC_MOISTURE_RREF	R_24_KOHM
#define WCD_MBHC_HS_V_MAX		1600
#define EAR_RX_PATH_AUX			1
#define WCD937X_MBHC_MAX_BUTTONS	8

#define WCD937X_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
		       SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
		       SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000 |\
		       SNDRV_PCM_RATE_384000)

/* Fractional Rates */
#define WCD937X_FRAC_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_88200 |\
			    SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_352800)

#define WCD937X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

enum {
	ALLOW_BUCK_DISABLE,
	HPH_COMP_DELAY,
	HPH_PA_DELAY,
	AMIC2_BCS_ENABLE,
};

enum {
	AIF1_PB = 0,
	AIF1_CAP,
	NUM_CODEC_DAIS,
};

struct wcd937x_priv {
	struct sdw_slave *tx_sdw_dev;
	struct wcd937x_sdw_priv *sdw_priv[NUM_CODEC_DAIS];
	struct device *txdev;
	struct device *rxdev;
	struct device_node *rxnode;
	struct device_node *txnode;
	struct regmap *regmap;
	/* micb setup lock */
	struct mutex micb_lock;
	/* mbhc module */
	struct wcd_mbhc *wcd_mbhc;
	struct wcd_mbhc_config mbhc_cfg;
	struct wcd_mbhc_intr intr_ids;
	struct wcd_clsh_ctrl *clsh_info;
	struct irq_domain *virq;
	struct regmap_irq_chip *wcd_regmap_irq_chip;
	struct regmap_irq_chip_data *irq_chip;
	struct regulator_bulk_data supplies[WCD937X_MAX_BULK_SUPPLY];
	struct regulator *buck_supply;
	struct snd_soc_jack *jack;
	unsigned long status_mask;
	s32 micb_ref[WCD937X_MAX_MICBIAS];
	s32 pullup_ref[WCD937X_MAX_MICBIAS];
	u32 hph_mode;
	int ear_rx_path;
	u32 micb1_mv;
	u32 micb2_mv;
	u32 micb3_mv;
	int hphr_pdm_wd_int;
	int hphl_pdm_wd_int;
	int aux_pdm_wd_int;
	bool comp1_enable;
	bool comp2_enable;

	struct gpio_desc *us_euro_gpio;
	struct gpio_desc *reset_gpio;

	atomic_t rx_clk_cnt;
	atomic_t ana_clk_count;
};

static const SNDRV_CTL_TLVD_DECLARE_DB_MINMAX(ear_pa_gain, 600, -1800);
static const DECLARE_TLV_DB_SCALE(line_gain, 0, 7, 1);
static const DECLARE_TLV_DB_SCALE(analog_gain, 0, 25, 1);

struct wcd937x_mbhc_zdet_param {
	u16 ldo_ctl;
	u16 noff;
	u16 nshift;
	u16 btn5;
	u16 btn6;
	u16 btn7;
};

static const struct wcd_mbhc_field wcd_mbhc_fields[WCD_MBHC_REG_FUNC_MAX] = {
	WCD_MBHC_FIELD(WCD_MBHC_L_DET_EN, WCD937X_ANA_MBHC_MECH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_GND_DET_EN, WCD937X_ANA_MBHC_MECH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_MECH_DETECTION_TYPE, WCD937X_ANA_MBHC_MECH, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_CLAMP_CTL, WCD937X_MBHC_NEW_PLUG_DETECT_CTL, 0x30),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_DETECTION_TYPE, WCD937X_ANA_MBHC_ELECT, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_CTRL, WCD937X_MBHC_NEW_INT_MECH_DET_CURRENT, 0x1F),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_COMP_CTRL, WCD937X_ANA_MBHC_MECH, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PLUG_TYPE, WCD937X_ANA_MBHC_MECH, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_GND_PLUG_TYPE, WCD937X_ANA_MBHC_MECH, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_SW_HPH_LP_100K_TO_GND, WCD937X_ANA_MBHC_MECH, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_SCHMT_ISRC, WCD937X_ANA_MBHC_ELECT, 0x06),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_EN, WCD937X_ANA_MBHC_ELECT, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_INSREM_DBNC, WCD937X_MBHC_NEW_PLUG_DETECT_CTL, 0x0F),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_DBNC, WCD937X_MBHC_NEW_CTL_1, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_VREF, WCD937X_MBHC_NEW_CTL_2, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_COMP_RESULT, WCD937X_ANA_MBHC_RESULT_3, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_IN2P_CLAMP_STATE, WCD937X_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_SCHMT_RESULT, WCD937X_ANA_MBHC_RESULT_3, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_SCHMT_RESULT, WCD937X_ANA_MBHC_RESULT_3, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_SCHMT_RESULT, WCD937X_ANA_MBHC_RESULT_3, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_OCP_FSM_EN, WCD937X_HPH_OCP_CTL, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_RESULT, WCD937X_ANA_MBHC_RESULT_3, 0x07),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_ISRC_CTL, WCD937X_ANA_MBHC_ELECT, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_RESULT, WCD937X_ANA_MBHC_RESULT_3, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB_CTRL, WCD937X_ANA_MICB2, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_CNP_WG_TIME, WCD937X_HPH_CNP_WG_TIME, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_PA_EN, WCD937X_ANA_HPH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PA_EN, WCD937X_ANA_HPH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_PA_EN, WCD937X_ANA_HPH, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_SWCH_LEVEL_REMOVE, WCD937X_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_ANC_DET_EN, WCD937X_MBHC_CTL_BCS, 0x02),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_STATUS, WCD937X_MBHC_NEW_FSM_STATUS, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_MUX_CTL, WCD937X_MBHC_NEW_CTL_2, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_MOISTURE_STATUS, WCD937X_MBHC_NEW_FSM_STATUS, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_GND, WCD937X_HPH_PA_CTL2, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_GND, WCD937X_HPH_PA_CTL2, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_DET_EN, WCD937X_HPH_L_TEST, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_DET_EN, WCD937X_HPH_R_TEST, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_STATUS, WCD937X_DIGITAL_INTR_STATUS_0, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_STATUS, WCD937X_DIGITAL_INTR_STATUS_0, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_EN, WCD937X_MBHC_NEW_CTL_1, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_COMPLETE, WCD937X_MBHC_NEW_FSM_STATUS, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_TIMEOUT, WCD937X_MBHC_NEW_FSM_STATUS, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_RESULT, WCD937X_MBHC_NEW_ADC_RESULT, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB2_VOUT, WCD937X_ANA_MICB2, 0x3F),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_MODE, WCD937X_MBHC_NEW_CTL_1, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_DETECTION_DONE, WCD937X_MBHC_NEW_CTL_1, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_ISRC_EN, WCD937X_ANA_MBHC_ZDET, 0x02),
};

static const struct regmap_irq wcd937x_irqs[WCD937X_NUM_IRQS] = {
	REGMAP_IRQ_REG(WCD937X_IRQ_MBHC_BUTTON_PRESS_DET, 0, BIT(0)),
	REGMAP_IRQ_REG(WCD937X_IRQ_MBHC_BUTTON_RELEASE_DET, 0, BIT(1)),
	REGMAP_IRQ_REG(WCD937X_IRQ_MBHC_ELECT_INS_REM_DET, 0, BIT(2)),
	REGMAP_IRQ_REG(WCD937X_IRQ_MBHC_ELECT_INS_REM_LEG_DET, 0, BIT(3)),
	REGMAP_IRQ_REG(WCD937X_IRQ_MBHC_SW_DET, 0, BIT(4)),
	REGMAP_IRQ_REG(WCD937X_IRQ_HPHR_OCP_INT, 0, BIT(5)),
	REGMAP_IRQ_REG(WCD937X_IRQ_HPHR_CNP_INT, 0, BIT(6)),
	REGMAP_IRQ_REG(WCD937X_IRQ_HPHL_OCP_INT, 0, BIT(7)),
	REGMAP_IRQ_REG(WCD937X_IRQ_HPHL_CNP_INT, 1, BIT(0)),
	REGMAP_IRQ_REG(WCD937X_IRQ_EAR_CNP_INT, 1, BIT(1)),
	REGMAP_IRQ_REG(WCD937X_IRQ_EAR_SCD_INT, 1, BIT(2)),
	REGMAP_IRQ_REG(WCD937X_IRQ_AUX_CNP_INT, 1, BIT(3)),
	REGMAP_IRQ_REG(WCD937X_IRQ_AUX_SCD_INT, 1, BIT(4)),
	REGMAP_IRQ_REG(WCD937X_IRQ_HPHL_PDM_WD_INT, 1, BIT(5)),
	REGMAP_IRQ_REG(WCD937X_IRQ_HPHR_PDM_WD_INT, 1, BIT(6)),
	REGMAP_IRQ_REG(WCD937X_IRQ_AUX_PDM_WD_INT, 1, BIT(7)),
	REGMAP_IRQ_REG(WCD937X_IRQ_LDORT_SCD_INT, 2, BIT(0)),
	REGMAP_IRQ_REG(WCD937X_IRQ_MBHC_MOISTURE_INT, 2, BIT(1)),
	REGMAP_IRQ_REG(WCD937X_IRQ_HPHL_SURGE_DET_INT, 2, BIT(2)),
	REGMAP_IRQ_REG(WCD937X_IRQ_HPHR_SURGE_DET_INT, 2, BIT(3)),
};

static int wcd937x_handle_post_irq(void *data)
{
	struct wcd937x_priv *wcd937x;

	if (data)
		wcd937x = (struct wcd937x_priv *)data;
	else
		return IRQ_HANDLED;

	regmap_write(wcd937x->regmap, WCD937X_DIGITAL_INTR_CLEAR_0, 0);
	regmap_write(wcd937x->regmap, WCD937X_DIGITAL_INTR_CLEAR_1, 0);
	regmap_write(wcd937x->regmap, WCD937X_DIGITAL_INTR_CLEAR_2, 0);

	return IRQ_HANDLED;
}

static const u32 wcd937x_config_regs[] = {
	WCD937X_DIGITAL_INTR_LEVEL_0,
};

static const struct regmap_irq_chip wcd937x_regmap_irq_chip = {
	.name = "wcd937x",
	.irqs = wcd937x_irqs,
	.num_irqs = ARRAY_SIZE(wcd937x_irqs),
	.num_regs = 3,
	.status_base = WCD937X_DIGITAL_INTR_STATUS_0,
	.mask_base = WCD937X_DIGITAL_INTR_MASK_0,
	.ack_base = WCD937X_DIGITAL_INTR_CLEAR_0,
	.use_ack = 1,
	.clear_ack = 1,
	.config_base = wcd937x_config_regs,
	.num_config_bases = ARRAY_SIZE(wcd937x_config_regs),
	.num_config_regs = 1,
	.runtime_pm = true,
	.handle_post_irq = wcd937x_handle_post_irq,
	.irq_drv_data = NULL,
};

static void wcd937x_reset(struct wcd937x_priv *wcd937x)
{
	gpiod_set_value(wcd937x->reset_gpio, 1);
	usleep_range(20, 30);
	gpiod_set_value(wcd937x->reset_gpio, 0);
	usleep_range(20, 30);
}

static void wcd937x_io_init(struct regmap *regmap)
{
	u32 val = 0, temp = 0, temp1 = 0;

	regmap_read(regmap, WCD937X_DIGITAL_EFUSE_REG_29, &val);

	val = val & 0x0F;

	regmap_read(regmap, WCD937X_DIGITAL_EFUSE_REG_16, &temp);
	regmap_read(regmap, WCD937X_DIGITAL_EFUSE_REG_17, &temp1);

	if (temp == 0x02 || temp1 > 0x09)
		regmap_update_bits(regmap, WCD937X_SLEEP_CTL, 0x0E, val);
	else
		regmap_update_bits(regmap, WCD937X_SLEEP_CTL, 0x0e, 0x0e);

	regmap_update_bits(regmap, WCD937X_SLEEP_CTL, 0x80, 0x80);
	usleep_range(1000, 1010);

	regmap_update_bits(regmap, WCD937X_SLEEP_CTL, 0x40, 0x40);
	usleep_range(1000, 1010);

	regmap_update_bits(regmap, WCD937X_LDORXTX_CONFIG, BIT(4), 0x00);
	regmap_update_bits(regmap, WCD937X_BIAS_VBG_FINE_ADJ, 0xf0, BIT(7));
	regmap_update_bits(regmap, WCD937X_ANA_BIAS, BIT(7), BIT(7));
	regmap_update_bits(regmap, WCD937X_ANA_BIAS, BIT(6), BIT(6));
	usleep_range(10000, 10010);

	regmap_update_bits(regmap, WCD937X_ANA_BIAS, BIT(6), 0x00);
	regmap_update_bits(regmap, WCD937X_HPH_SURGE_HPHLR_SURGE_EN, 0xff, 0xd9);
	regmap_update_bits(regmap, WCD937X_MICB1_TEST_CTL_1, 0xff, 0xfa);
	regmap_update_bits(regmap, WCD937X_MICB2_TEST_CTL_1, 0xff, 0xfa);
	regmap_update_bits(regmap, WCD937X_MICB3_TEST_CTL_1, 0xff, 0xfa);

	regmap_update_bits(regmap, WCD937X_MICB1_TEST_CTL_2, 0x38, 0x00);
	regmap_update_bits(regmap, WCD937X_MICB2_TEST_CTL_2, 0x38, 0x00);
	regmap_update_bits(regmap, WCD937X_MICB3_TEST_CTL_2, 0x38, 0x00);

	/* Set Bandgap Fine Adjustment to +5mV for Tanggu SMIC part */
	regmap_read(regmap, WCD937X_DIGITAL_EFUSE_REG_16, &val);
	if (val == 0x01) {
		regmap_update_bits(regmap, WCD937X_BIAS_VBG_FINE_ADJ, 0xF0, 0xB0);
	} else if (val == 0x02) {
		regmap_update_bits(regmap, WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_L, 0x1F, 0x04);
		regmap_update_bits(regmap, WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_R, 0x1F, 0x04);
		regmap_update_bits(regmap, WCD937X_BIAS_VBG_FINE_ADJ, 0xF0, 0xB0);
		regmap_update_bits(regmap, WCD937X_HPH_NEW_INT_RDAC_GAIN_CTL, 0xF0, 0x50);
	}
}

static int wcd937x_rx_clk_enable(struct snd_soc_component *component)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	if (atomic_read(&wcd937x->rx_clk_cnt))
		return 0;

	snd_soc_component_update_bits(component, WCD937X_DIGITAL_CDC_DIG_CLK_CTL, BIT(3), BIT(3));
	snd_soc_component_update_bits(component, WCD937X_DIGITAL_CDC_ANA_CLK_CTL, BIT(0), BIT(0));
	snd_soc_component_update_bits(component, WCD937X_ANA_RX_SUPPLIES, BIT(0), BIT(0));
	snd_soc_component_update_bits(component, WCD937X_DIGITAL_CDC_RX0_CTL, BIT(6), 0x00);
	snd_soc_component_update_bits(component, WCD937X_DIGITAL_CDC_RX1_CTL, BIT(6), 0x00);
	snd_soc_component_update_bits(component, WCD937X_DIGITAL_CDC_RX2_CTL, BIT(6), 0x00);
	snd_soc_component_update_bits(component, WCD937X_DIGITAL_CDC_ANA_CLK_CTL, BIT(1), BIT(1));

	atomic_inc(&wcd937x->rx_clk_cnt);

	return 0;
}

static int wcd937x_rx_clk_disable(struct snd_soc_component *component)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	if (!atomic_read(&wcd937x->rx_clk_cnt)) {
		dev_err(component->dev, "clk already disabled\n");
		return 0;
	}

	atomic_dec(&wcd937x->rx_clk_cnt);

	snd_soc_component_update_bits(component, WCD937X_ANA_RX_SUPPLIES, BIT(0), 0x00);
	snd_soc_component_update_bits(component, WCD937X_DIGITAL_CDC_ANA_CLK_CTL, BIT(1), 0x00);
	snd_soc_component_update_bits(component, WCD937X_DIGITAL_CDC_ANA_CLK_CTL, BIT(0), 0x00);

	return 0;
}

static int wcd937x_codec_hphl_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd937x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd937x_rx_clk_enable(component);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
					      BIT(0), BIT(0));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_HPH_GAIN_CTL,
					      BIT(2), BIT(2));
		snd_soc_component_update_bits(component,
					      WCD937X_HPH_RDAC_CLK_CTL1,
					      BIT(7), 0x00);
		set_bit(HPH_COMP_DELAY, &wcd937x->status_mask);
		break;
	case SND_SOC_DAPM_POST_PMU:
		if (hph_mode == CLS_AB_HIFI || hph_mode == CLS_H_HIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_L,
						      0x0f, BIT(1));
		else if (hph_mode == CLS_H_LOHIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_L,
						      0x0f, 0x06);

		if (wcd937x->comp1_enable) {
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_COMP_CTL_0,
						      BIT(1), BIT(1));
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_L_EN,
						      BIT(5), 0x00);

			if (wcd937x->comp2_enable) {
				snd_soc_component_update_bits(component,
							      WCD937X_DIGITAL_CDC_COMP_CTL_0,
							      BIT(0), BIT(0));
				snd_soc_component_update_bits(component,
							      WCD937X_HPH_R_EN, BIT(5), 0x00);
			}

			if (test_bit(HPH_COMP_DELAY, &wcd937x->status_mask)) {
				usleep_range(5000, 5110);
				clear_bit(HPH_COMP_DELAY, &wcd937x->status_mask);
			}
		} else {
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_COMP_CTL_0,
						      BIT(1), 0x00);
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_L_EN,
						      BIT(5), BIT(5));
		}

		snd_soc_component_update_bits(component,
					      WCD937X_HPH_NEW_INT_HPH_TIMER1,
					      BIT(1), 0x00);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component,
					      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_L,
					      0x0f, BIT(0));
		break;
	}

	return 0;
}

static int wcd937x_codec_hphr_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd937x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd937x_rx_clk_enable(component);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL, BIT(1), BIT(1));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_HPH_GAIN_CTL, BIT(3), BIT(3));
		snd_soc_component_update_bits(component,
					      WCD937X_HPH_RDAC_CLK_CTL1, BIT(7), 0x00);
		set_bit(HPH_COMP_DELAY, &wcd937x->status_mask);
		break;
	case SND_SOC_DAPM_POST_PMU:
		if (hph_mode == CLS_AB_HIFI || hph_mode == CLS_H_HIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_R,
						      0x0f, BIT(1));
		else if (hph_mode == CLS_H_LOHIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_R,
						      0x0f, 0x06);
		if (wcd937x->comp2_enable) {
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_COMP_CTL_0,
						      BIT(0), BIT(0));
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_R_EN, BIT(5), 0x00);
			if (wcd937x->comp1_enable) {
				snd_soc_component_update_bits(component,
							      WCD937X_DIGITAL_CDC_COMP_CTL_0,
							      BIT(1), BIT(1));
				snd_soc_component_update_bits(component,
							      WCD937X_HPH_L_EN,
							      BIT(5), 0x00);
			}

			if (test_bit(HPH_COMP_DELAY, &wcd937x->status_mask)) {
				usleep_range(5000, 5110);
				clear_bit(HPH_COMP_DELAY, &wcd937x->status_mask);
			}
		} else {
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_COMP_CTL_0,
						      BIT(0), 0x00);
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_R_EN,
						      BIT(5), BIT(5));
		}
		snd_soc_component_update_bits(component,
					      WCD937X_HPH_NEW_INT_HPH_TIMER1,
					      BIT(1), 0x00);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component,
					      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_R,
					      0x0f, BIT(0));
		break;
	}

	return 0;
}

static int wcd937x_codec_ear_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol,
				       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd937x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd937x_rx_clk_enable(component);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_HPH_GAIN_CTL,
					      BIT(2), BIT(2));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
					      BIT(0), BIT(0));

		if (hph_mode == CLS_AB_HIFI || hph_mode == CLS_H_HIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_L,
						      0x0f, BIT(1));
		else if (hph_mode == CLS_H_LOHIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_L,
						      0x0f, 0x06);
		if (wcd937x->comp1_enable)
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_COMP_CTL_0,
						      BIT(1), BIT(1));
		usleep_range(5000, 5010);

		snd_soc_component_update_bits(component, WCD937X_FLYBACK_EN, BIT(2), 0x00);
		wcd_clsh_ctrl_set_state(wcd937x->clsh_info,
					WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_EAR,
					hph_mode);

		break;
	case SND_SOC_DAPM_POST_PMD:
		if (hph_mode == CLS_AB_HIFI || hph_mode == CLS_H_LOHIFI ||
		    hph_mode == CLS_H_HIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_HPH_NEW_INT_RDAC_HD2_CTL_L,
						      0x0f, BIT(0));
		if (wcd937x->comp1_enable)
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_COMP_CTL_0,
						      BIT(1), 0x00);
		break;
	}

	return 0;
}

static int wcd937x_codec_aux_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol,
				       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd937x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd937x_rx_clk_enable(component);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_ANA_CLK_CTL,
					      BIT(2), BIT(2));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
					      BIT(2), BIT(2));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_AUX_GAIN_CTL,
					      BIT(0), BIT(0));
		wcd_clsh_ctrl_set_state(wcd937x->clsh_info,
					WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_AUX,
					hph_mode);

		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_ANA_CLK_CTL,
					      BIT(2), 0x00);
		break;
	}

	return 0;
}

static int wcd937x_codec_enable_hphr_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd937x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd_clsh_ctrl_set_state(wcd937x->clsh_info,
					WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHR,
					hph_mode);
		snd_soc_component_update_bits(component, WCD937X_ANA_HPH,
					      BIT(4), BIT(4));
		usleep_range(100, 110);
		set_bit(HPH_PA_DELAY, &wcd937x->status_mask);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_PDM_WD_CTL1,
					      0x07, 0x03);
		break;
	case SND_SOC_DAPM_POST_PMU:
		if (test_bit(HPH_PA_DELAY, &wcd937x->status_mask)) {
			if (wcd937x->comp2_enable)
				usleep_range(7000, 7100);
			else
				usleep_range(20000, 20100);
			clear_bit(HPH_PA_DELAY, &wcd937x->status_mask);
		}

		snd_soc_component_update_bits(component,
					      WCD937X_HPH_NEW_INT_HPH_TIMER1,
					      BIT(1), BIT(1));
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_ANA_RX_SUPPLIES,
						      BIT(1), BIT(1));
		enable_irq(wcd937x->hphr_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd937x->hphr_pdm_wd_int);
		set_bit(HPH_PA_DELAY, &wcd937x->status_mask);
		wcd_mbhc_event_notify(wcd937x->wcd_mbhc, WCD_EVENT_PRE_HPHR_PA_OFF);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (test_bit(HPH_PA_DELAY, &wcd937x->status_mask)) {
			if (wcd937x->comp2_enable)
				usleep_range(7000, 7100);
			else
				usleep_range(20000, 20100);
			clear_bit(HPH_PA_DELAY, &wcd937x->status_mask);
		}

		wcd_mbhc_event_notify(wcd937x->wcd_mbhc, WCD_EVENT_POST_HPHR_PA_OFF);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_PDM_WD_CTL1, 0x07, 0x00);
		snd_soc_component_update_bits(component, WCD937X_ANA_HPH,
					      BIT(4), 0x00);
		wcd_clsh_ctrl_set_state(wcd937x->clsh_info,
					WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_HPHR,
					hph_mode);
		break;
	}

	return 0;
}

static int wcd937x_codec_enable_hphl_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd937x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd_clsh_ctrl_set_state(wcd937x->clsh_info,
					WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHL,
					hph_mode);
		snd_soc_component_update_bits(component, WCD937X_ANA_HPH,
					      BIT(5), BIT(5));
		usleep_range(100, 110);
		set_bit(HPH_PA_DELAY, &wcd937x->status_mask);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_PDM_WD_CTL0, 0x07, 0x03);
		break;
	case SND_SOC_DAPM_POST_PMU:
		if (test_bit(HPH_PA_DELAY, &wcd937x->status_mask)) {
			if (!wcd937x->comp1_enable)
				usleep_range(20000, 20100);
			else
				usleep_range(7000, 7100);
			clear_bit(HPH_PA_DELAY, &wcd937x->status_mask);
		}

		snd_soc_component_update_bits(component,
					      WCD937X_HPH_NEW_INT_HPH_TIMER1,
					      BIT(1), BIT(1));
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_ANA_RX_SUPPLIES,
						      BIT(1), BIT(1));
		enable_irq(wcd937x->hphl_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd937x->hphl_pdm_wd_int);
		set_bit(HPH_PA_DELAY, &wcd937x->status_mask);
		wcd_mbhc_event_notify(wcd937x->wcd_mbhc, WCD_EVENT_PRE_HPHL_PA_OFF);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (test_bit(HPH_PA_DELAY, &wcd937x->status_mask)) {
			if (!wcd937x->comp1_enable)
				usleep_range(20000, 20100);
			else
				usleep_range(7000, 7100);
			clear_bit(HPH_PA_DELAY, &wcd937x->status_mask);
		}

		wcd_mbhc_event_notify(wcd937x->wcd_mbhc, WCD_EVENT_POST_HPHL_PA_OFF);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_PDM_WD_CTL0, 0x07, 0x00);
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_HPH, BIT(5), 0x00);
		wcd_clsh_ctrl_set_state(wcd937x->clsh_info,
					WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_HPHL,
					hph_mode);
		break;
	}

	return 0;
}

static int wcd937x_codec_enable_aux_pa(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol,
				       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd937x->hph_mode;
	u8 val;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		val = WCD937X_DIGITAL_PDM_WD_CTL2_EN |
		      WCD937X_DIGITAL_PDM_WD_CTL2_TIMEOUT_SEL |
		      WCD937X_DIGITAL_PDM_WD_CTL2_HOLD_OFF;
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_PDM_WD_CTL2,
					      WCD937X_DIGITAL_PDM_WD_CTL2_MASK,
					      val);
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(1000, 1010);
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_ANA_RX_SUPPLIES,
						      BIT(1), BIT(1));
		enable_irq(wcd937x->aux_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd937x->aux_pdm_wd_int);
		break;
	case SND_SOC_DAPM_POST_PMD:
		usleep_range(2000, 2010);
		wcd_clsh_ctrl_set_state(wcd937x->clsh_info,
					WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_AUX,
					hph_mode);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_PDM_WD_CTL2,
					      WCD937X_DIGITAL_PDM_WD_CTL2_MASK,
					      0x00);
		break;
	}

	return 0;
}

static int wcd937x_codec_enable_ear_pa(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol,
				       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd937x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable watchdog interrupt for HPHL or AUX depending on mux value */
		wcd937x->ear_rx_path = snd_soc_component_read(component,
							      WCD937X_DIGITAL_CDC_EAR_PATH_CTL);

		if (wcd937x->ear_rx_path & EAR_RX_PATH_AUX)
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_PDM_WD_CTL2,
						      BIT(0), BIT(0));
		else
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_PDM_WD_CTL0,
						      0x07, 0x03);
		if (!wcd937x->comp1_enable)
			snd_soc_component_update_bits(component,
						      WCD937X_ANA_EAR_COMPANDER_CTL,
						      BIT(7), BIT(7));
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(6000, 6010);
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI)
			snd_soc_component_update_bits(component,
						      WCD937X_ANA_RX_SUPPLIES,
						      BIT(1), BIT(1));

		if (wcd937x->ear_rx_path & EAR_RX_PATH_AUX)
			enable_irq(wcd937x->aux_pdm_wd_int);
		else
			enable_irq(wcd937x->hphl_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		if (wcd937x->ear_rx_path & EAR_RX_PATH_AUX)
			disable_irq_nosync(wcd937x->aux_pdm_wd_int);
		else
			disable_irq_nosync(wcd937x->hphl_pdm_wd_int);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (!wcd937x->comp1_enable)
			snd_soc_component_update_bits(component,
						      WCD937X_ANA_EAR_COMPANDER_CTL,
						      BIT(7), 0x00);
		usleep_range(7000, 7010);
		wcd_clsh_ctrl_set_state(wcd937x->clsh_info,
					WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_EAR,
					hph_mode);
		snd_soc_component_update_bits(component, WCD937X_FLYBACK_EN,
					      BIT(2), BIT(2));

		if (wcd937x->ear_rx_path & EAR_RX_PATH_AUX)
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_PDM_WD_CTL2,
						      BIT(0), 0x00);
		else
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_PDM_WD_CTL0,
						      0x07, 0x00);
		break;
	}

	return 0;
}

static int wcd937x_enable_rx1(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	if (event == SND_SOC_DAPM_POST_PMD) {
		wcd937x_rx_clk_disable(component);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
					      BIT(0), 0x00);
	}

	return 0;
}

static int wcd937x_enable_rx2(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	if (event == SND_SOC_DAPM_POST_PMD) {
		wcd937x_rx_clk_disable(component);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
					      BIT(1), 0x00);
	}

	return 0;
}

static int wcd937x_enable_rx3(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	if (event == SND_SOC_DAPM_POST_PMD) {
		usleep_range(6000, 6010);
		wcd937x_rx_clk_disable(component);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
					      BIT(2), 0x00);
	}

	return 0;
}

static int wcd937x_get_micb_vout_ctl_val(u32 micb_mv)
{
	if (micb_mv < 1000 || micb_mv > 2850) {
		pr_err("Unsupported micbias voltage (%u mV)\n", micb_mv);
		return -EINVAL;
	}

	return (micb_mv - 1000) / 50;
}

static int wcd937x_tx_swr_ctrl(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	bool use_amic3 = snd_soc_component_read(component, WCD937X_TX_NEW_TX_CH2_SEL) & BIT(7);

	/* Enable BCS for Headset mic */
	if (event == SND_SOC_DAPM_PRE_PMU && strnstr(w->name, "ADC", sizeof("ADC")))
		if (w->shift == 1 && !use_amic3)
			set_bit(AMIC2_BCS_ENABLE, &wcd937x->status_mask);

	return 0;
}

static int wcd937x_codec_enable_adc(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		atomic_inc(&wcd937x->ana_clk_count);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL, BIT(7), BIT(7));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_ANA_CLK_CTL, BIT(3), BIT(3));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_ANA_CLK_CTL, BIT(4), BIT(4));
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (w->shift == 1 && test_bit(AMIC2_BCS_ENABLE, &wcd937x->status_mask))
			clear_bit(AMIC2_BCS_ENABLE, &wcd937x->status_mask);

		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_ANA_CLK_CTL, BIT(3), 0x00);
		break;
	}

	return 0;
}

static int wcd937x_enable_req(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_REQ_CTL, BIT(1), BIT(1));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_REQ_CTL, BIT(0), 0x00);
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH2, BIT(6), BIT(6));
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH3_HPF, BIT(6), BIT(6));
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL, 0x70, 0x70);
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH1, BIT(7), BIT(7));
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH2, BIT(6), 0x00);
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH2, BIT(7), BIT(7));
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH3, BIT(7), BIT(7));
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH1, BIT(7), 0x00);
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH2, BIT(7), 0x00);
		snd_soc_component_update_bits(component,
					      WCD937X_ANA_TX_CH3, BIT(7), 0x00);
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL, BIT(4), 0x00);

		atomic_dec(&wcd937x->ana_clk_count);
		if (atomic_read(&wcd937x->ana_clk_count) <= 0) {
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_ANA_CLK_CTL,
						      BIT(4), 0x00);
			atomic_set(&wcd937x->ana_clk_count, 0);
		}

		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
					      BIT(7), 0x00);
		break;
	}

	return 0;
}

static int wcd937x_codec_enable_dmic(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol,
				     int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 dmic_clk_reg;

	switch (w->shift) {
	case 0:
	case 1:
		dmic_clk_reg = WCD937X_DIGITAL_CDC_DMIC1_CTL;
		break;
	case 2:
	case 3:
		dmic_clk_reg = WCD937X_DIGITAL_CDC_DMIC2_CTL;
		break;
	case 4:
	case 5:
		dmic_clk_reg = WCD937X_DIGITAL_CDC_DMIC3_CTL;
		break;
	default:
		dev_err(component->dev, "Invalid DMIC Selection\n");
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component,
					      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
					      BIT(7), BIT(7));
		snd_soc_component_update_bits(component,
					      dmic_clk_reg, 0x07, BIT(1));
		snd_soc_component_update_bits(component,
					      dmic_clk_reg, BIT(3), BIT(3));
		snd_soc_component_update_bits(component,
					      dmic_clk_reg, 0x70, BIT(5));
		break;
	}

	return 0;
}

static int wcd937x_micbias_control(struct snd_soc_component *component,
				   int micb_num, int req, bool is_dapm)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int micb_index = micb_num - 1;
	u16 micb_reg;

	if (micb_index < 0 || (micb_index > WCD937X_MAX_MICBIAS - 1)) {
		dev_err(component->dev, "Invalid micbias index, micb_ind:%d\n", micb_index);
		return -EINVAL;
	}
	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = WCD937X_ANA_MICB1;
		break;
	case MIC_BIAS_2:
		micb_reg = WCD937X_ANA_MICB2;
		break;
	case MIC_BIAS_3:
		micb_reg = WCD937X_ANA_MICB3;
		break;
	default:
		dev_err(component->dev, "Invalid micbias number: %d\n", micb_num);
		return -EINVAL;
	}

	mutex_lock(&wcd937x->micb_lock);
	switch (req) {
	case MICB_PULLUP_ENABLE:
		wcd937x->pullup_ref[micb_index]++;
		if (wcd937x->pullup_ref[micb_index] == 1 &&
		    wcd937x->micb_ref[micb_index] == 0)
			snd_soc_component_update_bits(component, micb_reg,
						      0xc0, BIT(7));
		break;
	case MICB_PULLUP_DISABLE:
		if (wcd937x->pullup_ref[micb_index] > 0)
			wcd937x->pullup_ref[micb_index]++;
		if (wcd937x->pullup_ref[micb_index] == 0 &&
		    wcd937x->micb_ref[micb_index] == 0)
			snd_soc_component_update_bits(component, micb_reg,
						      0xc0, 0x00);
		break;
	case MICB_ENABLE:
		wcd937x->micb_ref[micb_index]++;
		atomic_inc(&wcd937x->ana_clk_count);
		if (wcd937x->micb_ref[micb_index] == 1) {
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_DIG_CLK_CTL,
						      0xf0, 0xf0);
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_ANA_CLK_CTL,
						      BIT(4), BIT(4));
			snd_soc_component_update_bits(component,
						      WCD937X_MICB1_TEST_CTL_2,
						      BIT(0), BIT(0));
			snd_soc_component_update_bits(component,
						      WCD937X_MICB2_TEST_CTL_2,
						      BIT(0), BIT(0));
			snd_soc_component_update_bits(component,
						      WCD937X_MICB3_TEST_CTL_2,
						      BIT(0), BIT(0));
			snd_soc_component_update_bits(component,
						      micb_reg, 0xc0, BIT(6));

			if (micb_num == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd937x->wcd_mbhc,
						      WCD_EVENT_POST_MICBIAS_2_ON);

			if (micb_num == MIC_BIAS_2 && is_dapm)
				wcd_mbhc_event_notify(wcd937x->wcd_mbhc,
						      WCD_EVENT_POST_DAPM_MICBIAS_2_ON);
		}
		break;
	case MICB_DISABLE:
		atomic_dec(&wcd937x->ana_clk_count);
		if (wcd937x->micb_ref[micb_index] > 0)
			wcd937x->micb_ref[micb_index]--;
		if (wcd937x->micb_ref[micb_index] == 0 &&
		    wcd937x->pullup_ref[micb_index] > 0)
			snd_soc_component_update_bits(component, micb_reg,
						      0xc0, BIT(7));
		else if (wcd937x->micb_ref[micb_index] == 0 &&
			 wcd937x->pullup_ref[micb_index] == 0) {
			if (micb_num == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd937x->wcd_mbhc,
						      WCD_EVENT_PRE_MICBIAS_2_OFF);

			snd_soc_component_update_bits(component, micb_reg,
						      0xc0, 0x00);
			if (micb_num == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd937x->wcd_mbhc,
						      WCD_EVENT_POST_MICBIAS_2_OFF);
		}

		if (is_dapm && micb_num == MIC_BIAS_2)
			wcd_mbhc_event_notify(wcd937x->wcd_mbhc,
					      WCD_EVENT_POST_DAPM_MICBIAS_2_OFF);
		if (atomic_read(&wcd937x->ana_clk_count) <= 0) {
			snd_soc_component_update_bits(component,
						      WCD937X_DIGITAL_CDC_ANA_CLK_CTL,
						      BIT(4), 0x00);
			atomic_set(&wcd937x->ana_clk_count, 0);
		}
		break;
	}
	mutex_unlock(&wcd937x->micb_lock);

	return 0;
}

static int __wcd937x_codec_enable_micbias(struct snd_soc_dapm_widget *w,
					  int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd937x_micbias_control(component, micb_num,
					MICB_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd937x_micbias_control(component, micb_num,
					MICB_DISABLE, true);
		break;
	}

	return 0;
}

static int wcd937x_codec_enable_micbias(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	return __wcd937x_codec_enable_micbias(w, event);
}

static int __wcd937x_codec_enable_micbias_pullup(struct snd_soc_dapm_widget *w,
						 int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd937x_micbias_control(component, micb_num, MICB_PULLUP_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd937x_micbias_control(component, micb_num, MICB_PULLUP_DISABLE, true);
		break;
	}

	return 0;
}

static int wcd937x_codec_enable_micbias_pullup(struct snd_soc_dapm_widget *w,
					       struct snd_kcontrol *kcontrol,
					       int event)
{
	return __wcd937x_codec_enable_micbias_pullup(w, event);
}

static int wcd937x_connect_port(struct wcd937x_sdw_priv *wcd, u8 port_idx, u8 ch_id, bool enable)
{
	struct sdw_port_config *port_config = &wcd->port_config[port_idx - 1];
	const struct wcd937x_sdw_ch_info *ch_info = &wcd->ch_info[ch_id];
	u8 port_num = ch_info->port_num;
	u8 ch_mask = ch_info->ch_mask;
	u8 mstr_port_num, mstr_ch_mask;
	struct sdw_slave *sdev = wcd->sdev;

	port_config->num = port_num;

	mstr_port_num = sdev->m_port_map[port_num];
	mstr_ch_mask = ch_info->master_ch_mask;

	if (enable) {
		port_config->ch_mask |= ch_mask;
		wcd->master_channel_map[mstr_port_num] |= mstr_ch_mask;
	} else {
		port_config->ch_mask &= ~ch_mask;
		wcd->master_channel_map[mstr_port_num] &= ~mstr_ch_mask;
	}

	return 0;
}

static int wcd937x_rx_hph_mode_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd937x->hph_mode;
	return 0;
}

static int wcd937x_rx_hph_mode_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	u32 mode_val;

	mode_val = ucontrol->value.enumerated.item[0];

	if (!mode_val)
		mode_val = CLS_AB;

	if (mode_val == wcd937x->hph_mode)
		return 0;

	switch (mode_val) {
	case CLS_H_NORMAL:
	case CLS_H_HIFI:
	case CLS_H_LP:
	case CLS_AB:
	case CLS_H_LOHIFI:
	case CLS_H_ULP:
	case CLS_AB_LP:
	case CLS_AB_HIFI:
		wcd937x->hph_mode = mode_val;
		return 1;
	}

	dev_dbg(component->dev, "%s: Invalid HPH Mode\n", __func__);
	return -EINVAL;
}

static int wcd937x_get_compander(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc;
	bool hphr;

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;

	ucontrol->value.integer.value[0] = hphr ? wcd937x->comp2_enable :
						  wcd937x->comp1_enable;
	return 0;
}

static int wcd937x_set_compander(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	struct wcd937x_sdw_priv *wcd = wcd937x->sdw_priv[AIF1_PB];
	int value = ucontrol->value.integer.value[0];
	struct soc_mixer_control *mc;
	int portidx;
	bool hphr;

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;

	if (hphr) {
		if (value == wcd937x->comp2_enable)
			return 0;

		wcd937x->comp2_enable = value;
	} else {
		if (value == wcd937x->comp1_enable)
			return 0;

		wcd937x->comp1_enable = value;
	}

	portidx = wcd->ch_info[mc->reg].port_num;

	if (value)
		wcd937x_connect_port(wcd, portidx, mc->reg, true);
	else
		wcd937x_connect_port(wcd, portidx, mc->reg, false);

	return 1;
}

static int wcd937x_get_swr_port(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(comp);
	struct wcd937x_sdw_priv *wcd;
	int dai_id = mixer->shift;
	int ch_idx = mixer->reg;
	int portidx;

	wcd = wcd937x->sdw_priv[dai_id];
	portidx = wcd->ch_info[ch_idx].port_num;

	ucontrol->value.integer.value[0] = wcd->port_enable[portidx];

	return 0;
}

static int wcd937x_set_swr_port(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(comp);
	struct wcd937x_sdw_priv *wcd;
	int dai_id = mixer->shift;
	int ch_idx = mixer->reg;
	int portidx;
	bool enable;

	wcd = wcd937x->sdw_priv[dai_id];

	portidx = wcd->ch_info[ch_idx].port_num;

	enable = ucontrol->value.integer.value[0];

	if (enable == wcd->port_enable[portidx]) {
		wcd937x_connect_port(wcd, portidx, ch_idx, enable);
		return 0;
	}

	wcd->port_enable[portidx] = enable;
	wcd937x_connect_port(wcd, portidx, ch_idx, enable);

	return 1;
}

static const char * const rx_hph_mode_mux_text[] = {
	"CLS_H_NORMAL", "CLS_H_INVALID", "CLS_H_HIFI", "CLS_H_LP", "CLS_AB",
	"CLS_H_LOHIFI", "CLS_H_ULP", "CLS_AB_LP", "CLS_AB_HIFI",
};

static const struct soc_enum rx_hph_mode_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_hph_mode_mux_text), rx_hph_mode_mux_text);

/* MBHC related */
static void wcd937x_mbhc_clk_setup(struct snd_soc_component *component,
				   bool enable)
{
	snd_soc_component_write_field(component, WCD937X_MBHC_NEW_CTL_1,
				      WCD937X_MBHC_CTL_RCO_EN_MASK, enable);
}

static void wcd937x_mbhc_mbhc_bias_control(struct snd_soc_component *component,
					   bool enable)
{
	snd_soc_component_write_field(component, WCD937X_ANA_MBHC_ELECT,
				      WCD937X_ANA_MBHC_BIAS_EN, enable);
}

static void wcd937x_mbhc_program_btn_thr(struct snd_soc_component *component,
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
		snd_soc_component_write_field(component, WCD937X_ANA_MBHC_BTN0 + i,
					      WCD937X_MBHC_BTN_VTH_MASK, vth);
	}
}

static bool wcd937x_mbhc_micb_en_status(struct snd_soc_component *component, int micb_num)
{
	u8 val;

	if (micb_num == MIC_BIAS_2) {
		val = snd_soc_component_read_field(component,
						   WCD937X_ANA_MICB2,
						   WCD937X_ANA_MICB2_ENABLE_MASK);
		if (val == WCD937X_MICB_ENABLE)
			return true;
	}
	return false;
}

static void wcd937x_mbhc_hph_l_pull_up_control(struct snd_soc_component *component,
					       int pull_up_cur)
{
	/* Default pull up current to 2uA */
	if (pull_up_cur > HS_PULLUP_I_OFF || pull_up_cur < HS_PULLUP_I_3P0_UA)
		pull_up_cur = HS_PULLUP_I_2P0_UA;

	snd_soc_component_write_field(component,
				      WCD937X_MBHC_NEW_INT_MECH_DET_CURRENT,
				      WCD937X_HSDET_PULLUP_C_MASK, pull_up_cur);
}

static int wcd937x_mbhc_request_micbias(struct snd_soc_component *component,
					int micb_num, int req)
{
	return wcd937x_micbias_control(component, micb_num, req, false);
}

static void wcd937x_mbhc_micb_ramp_control(struct snd_soc_component *component,
					   bool enable)
{
	if (enable) {
		snd_soc_component_write_field(component, WCD937X_ANA_MICB2_RAMP,
					      WCD937X_RAMP_SHIFT_CTRL_MASK, 0x0C);
		snd_soc_component_write_field(component, WCD937X_ANA_MICB2_RAMP,
					      WCD937X_RAMP_EN_MASK, 1);
	} else {
		snd_soc_component_write_field(component, WCD937X_ANA_MICB2_RAMP,
					      WCD937X_RAMP_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD937X_ANA_MICB2_RAMP,
					      WCD937X_RAMP_SHIFT_CTRL_MASK, 0);
	}
}

static int wcd937x_mbhc_micb_adjust_voltage(struct snd_soc_component *component,
					    int req_volt, int micb_num)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int cur_vout_ctl, req_vout_ctl, micb_reg, micb_en, ret = 0;

	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = WCD937X_ANA_MICB1;
		break;
	case MIC_BIAS_2:
		micb_reg = WCD937X_ANA_MICB2;
		break;
	case MIC_BIAS_3:
		micb_reg = WCD937X_ANA_MICB3;
		break;
	default:
		return -EINVAL;
	}
	mutex_lock(&wcd937x->micb_lock);
	/*
	 * If requested micbias voltage is same as current micbias
	 * voltage, then just return. Otherwise, adjust voltage as
	 * per requested value. If micbias is already enabled, then
	 * to avoid slow micbias ramp-up or down enable pull-up
	 * momentarily, change the micbias value and then re-enable
	 * micbias.
	 */
	micb_en = snd_soc_component_read_field(component, micb_reg,
					       WCD937X_MICB_EN_MASK);
	cur_vout_ctl = snd_soc_component_read_field(component, micb_reg,
						    WCD937X_MICB_VOUT_MASK);

	req_vout_ctl = wcd937x_get_micb_vout_ctl_val(req_volt);
	if (req_vout_ctl < 0) {
		ret = -EINVAL;
		goto exit;
	}

	if (cur_vout_ctl == req_vout_ctl) {
		ret = 0;
		goto exit;
	}

	if (micb_en == WCD937X_MICB_ENABLE)
		snd_soc_component_write_field(component, micb_reg,
					      WCD937X_MICB_EN_MASK,
					      WCD937X_MICB_PULL_UP);

	snd_soc_component_write_field(component, micb_reg,
				      WCD937X_MICB_VOUT_MASK,
				      req_vout_ctl);

	if (micb_en == WCD937X_MICB_ENABLE) {
		snd_soc_component_write_field(component, micb_reg,
					      WCD937X_MICB_EN_MASK,
					      WCD937X_MICB_ENABLE);
		/*
		 * Add 2ms delay as per HW requirement after enabling
		 * micbias
		 */
		usleep_range(2000, 2100);
	}
exit:
	mutex_unlock(&wcd937x->micb_lock);
	return ret;
}

static int wcd937x_mbhc_micb_ctrl_threshold_mic(struct snd_soc_component *component,
						int micb_num, bool req_en)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int micb_mv;

	if (micb_num != MIC_BIAS_2)
		return -EINVAL;
	/*
	 * If device tree micbias level is already above the minimum
	 * voltage needed to detect threshold microphone, then do
	 * not change the micbias, just return.
	 */
	if (wcd937x->micb2_mv >= WCD_MBHC_THR_HS_MICB_MV)
		return 0;

	micb_mv = req_en ? WCD_MBHC_THR_HS_MICB_MV : wcd937x->micb2_mv;

	return wcd937x_mbhc_micb_adjust_voltage(component, micb_mv, MIC_BIAS_2);
}

static void wcd937x_mbhc_get_result_params(struct snd_soc_component *component,
					   s16 *d1_a, u16 noff,
					   int32_t *zdet)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	int i;
	int val, val1;
	s16 c1;
	s32 x1, d1;
	s32 denom;
	static const int minCode_param[] = {
		3277, 1639, 820, 410, 205, 103, 52, 26
	};

	regmap_update_bits(wcd937x->regmap, WCD937X_ANA_MBHC_ZDET, 0x20, 0x20);
	for (i = 0; i < WCD937X_ZDET_NUM_MEASUREMENTS; i++) {
		regmap_read(wcd937x->regmap, WCD937X_ANA_MBHC_RESULT_2, &val);
		if (val & 0x80)
			break;
	}
	val = val << 0x8;
	regmap_read(wcd937x->regmap, WCD937X_ANA_MBHC_RESULT_1, &val1);
	val |= val1;
	regmap_update_bits(wcd937x->regmap, WCD937X_ANA_MBHC_ZDET, 0x20, 0x00);
	x1 = WCD937X_MBHC_GET_X1(val);
	c1 = WCD937X_MBHC_GET_C1(val);
	/* If ramp is not complete, give additional 5ms */
	if (c1 < 2 && x1)
		usleep_range(5000, 5050);

	if (!c1 || !x1) {
		dev_err(component->dev, "Impedance detect ramp error, c1=%d, x1=0x%x\n",
			c1, x1);
		goto ramp_down;
	}
	d1 = d1_a[c1];
	denom = (x1 * d1) - (1 << (14 - noff));
	if (denom > 0)
		*zdet = (WCD937X_MBHC_ZDET_CONST * 1000) / denom;
	else if (x1 < minCode_param[noff])
		*zdet = WCD937X_ZDET_FLOATING_IMPEDANCE;

	dev_err(component->dev, "%s: d1=%d, c1=%d, x1=0x%x, z_val=%d (milliohm)\n",
		__func__, d1, c1, x1, *zdet);
ramp_down:
	i = 0;
	while (x1) {
		regmap_read(wcd937x->regmap,
			    WCD937X_ANA_MBHC_RESULT_1, &val);
		regmap_read(wcd937x->regmap,
			    WCD937X_ANA_MBHC_RESULT_2, &val1);
		val = val << 0x08;
		val |= val1;
		x1 = WCD937X_MBHC_GET_X1(val);
		i++;
		if (i == WCD937X_ZDET_NUM_MEASUREMENTS)
			break;
	}
}

static void wcd937x_mbhc_zdet_ramp(struct snd_soc_component *component,
				   struct wcd937x_mbhc_zdet_param *zdet_param,
				   s32 *zl, s32 *zr, s16 *d1_a)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	s32 zdet = 0;

	snd_soc_component_write_field(component, WCD937X_MBHC_NEW_ZDET_ANA_CTL,
				      WCD937X_ZDET_MAXV_CTL_MASK, zdet_param->ldo_ctl);
	snd_soc_component_update_bits(component, WCD937X_ANA_MBHC_BTN5,
				      WCD937X_VTH_MASK, zdet_param->btn5);
	snd_soc_component_update_bits(component, WCD937X_ANA_MBHC_BTN6,
				      WCD937X_VTH_MASK, zdet_param->btn6);
	snd_soc_component_update_bits(component, WCD937X_ANA_MBHC_BTN7,
				      WCD937X_VTH_MASK, zdet_param->btn7);
	snd_soc_component_write_field(component, WCD937X_MBHC_NEW_ZDET_ANA_CTL,
				      WCD937X_ZDET_RANGE_CTL_MASK, zdet_param->noff);
	snd_soc_component_update_bits(component, WCD937X_MBHC_NEW_ZDET_RAMP_CTL,
				      0x0F, zdet_param->nshift);

	if (!zl)
		goto z_right;
	/* Start impedance measurement for HPH_L */
	regmap_update_bits(wcd937x->regmap,
			   WCD937X_ANA_MBHC_ZDET, 0x80, 0x80);
	wcd937x_mbhc_get_result_params(component, d1_a, zdet_param->noff, &zdet);
	regmap_update_bits(wcd937x->regmap,
			   WCD937X_ANA_MBHC_ZDET, 0x80, 0x00);

	*zl = zdet;

z_right:
	if (!zr)
		return;
	/* Start impedance measurement for HPH_R */
	regmap_update_bits(wcd937x->regmap,
			   WCD937X_ANA_MBHC_ZDET, 0x40, 0x40);
	wcd937x_mbhc_get_result_params(component, d1_a, zdet_param->noff, &zdet);
	regmap_update_bits(wcd937x->regmap,
			   WCD937X_ANA_MBHC_ZDET, 0x40, 0x00);

	*zr = zdet;
}

static void wcd937x_wcd_mbhc_qfuse_cal(struct snd_soc_component *component,
				       s32 *z_val, int flag_l_r)
{
	s16 q1;
	int q1_cal;

	if (*z_val < (WCD937X_ZDET_VAL_400 / 1000))
		q1 = snd_soc_component_read(component,
					    WCD937X_DIGITAL_EFUSE_REG_23 + (2 * flag_l_r));
	else
		q1 = snd_soc_component_read(component,
					    WCD937X_DIGITAL_EFUSE_REG_24 + (2 * flag_l_r));
	if (q1 & 0x80)
		q1_cal = (10000 - ((q1 & 0x7F) * 25));
	else
		q1_cal = (10000 + (q1 * 25));
	if (q1_cal > 0)
		*z_val = ((*z_val) * 10000) / q1_cal;
}

static void wcd937x_wcd_mbhc_calc_impedance(struct snd_soc_component *component,
					    u32 *zl, u32 *zr)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	s16 reg0, reg1, reg2, reg3, reg4;
	s32 z1l, z1r, z1ls;
	int zMono, z_diff1, z_diff2;
	bool is_fsm_disable = false;
	struct wcd937x_mbhc_zdet_param zdet_param[] = {
		{4, 0, 4, 0x08, 0x14, 0x18}, /* < 32ohm */
		{2, 0, 3, 0x18, 0x7C, 0x90}, /* 32ohm < Z < 400ohm */
		{1, 4, 5, 0x18, 0x7C, 0x90}, /* 400ohm < Z < 1200ohm */
		{1, 6, 7, 0x18, 0x7C, 0x90}, /* >1200ohm */
	};
	struct wcd937x_mbhc_zdet_param *zdet_param_ptr = NULL;
	s16 d1_a[][4] = {
		{0, 30, 90, 30},
		{0, 30, 30, 5},
		{0, 30, 30, 5},
		{0, 30, 30, 5},
	};
	s16 *d1 = NULL;

	reg0 = snd_soc_component_read(component, WCD937X_ANA_MBHC_BTN5);
	reg1 = snd_soc_component_read(component, WCD937X_ANA_MBHC_BTN6);
	reg2 = snd_soc_component_read(component, WCD937X_ANA_MBHC_BTN7);
	reg3 = snd_soc_component_read(component, WCD937X_MBHC_CTL_CLK);
	reg4 = snd_soc_component_read(component, WCD937X_MBHC_NEW_ZDET_ANA_CTL);

	if (snd_soc_component_read(component, WCD937X_ANA_MBHC_ELECT) & 0x80) {
		is_fsm_disable = true;
		regmap_update_bits(wcd937x->regmap,
				   WCD937X_ANA_MBHC_ELECT, 0x80, 0x00);
	}

	/* For NO-jack, disable L_DET_EN before Z-det measurements */
	if (wcd937x->mbhc_cfg.hphl_swh)
		regmap_update_bits(wcd937x->regmap,
				   WCD937X_ANA_MBHC_MECH, 0x80, 0x00);

	/* Turn off 100k pull down on HPHL */
	regmap_update_bits(wcd937x->regmap,
			   WCD937X_ANA_MBHC_MECH, 0x01, 0x00);

	/* Disable surge protection before impedance detection.
	 * This is done to give correct value for high impedance.
	 */
	regmap_update_bits(wcd937x->regmap,
			   WCD937X_HPH_SURGE_HPHLR_SURGE_EN, 0xC0, 0x00);
	/* 1ms delay needed after disable surge protection */
	usleep_range(1000, 1010);

	/* First get impedance on Left */
	d1 = d1_a[1];
	zdet_param_ptr = &zdet_param[1];
	wcd937x_mbhc_zdet_ramp(component, zdet_param_ptr, &z1l, NULL, d1);

	if (!WCD937X_MBHC_IS_SECOND_RAMP_REQUIRED(z1l))
		goto left_ch_impedance;

	/* Second ramp for left ch */
	if (z1l < WCD937X_ZDET_VAL_32) {
		zdet_param_ptr = &zdet_param[0];
		d1 = d1_a[0];
	} else if ((z1l > WCD937X_ZDET_VAL_400) &&
		  (z1l <= WCD937X_ZDET_VAL_1200)) {
		zdet_param_ptr = &zdet_param[2];
		d1 = d1_a[2];
	} else if (z1l > WCD937X_ZDET_VAL_1200) {
		zdet_param_ptr = &zdet_param[3];
		d1 = d1_a[3];
	}
	wcd937x_mbhc_zdet_ramp(component, zdet_param_ptr, &z1l, NULL, d1);

left_ch_impedance:
	if (z1l == WCD937X_ZDET_FLOATING_IMPEDANCE ||
	    z1l > WCD937X_ZDET_VAL_100K) {
		*zl = WCD937X_ZDET_FLOATING_IMPEDANCE;
		zdet_param_ptr = &zdet_param[1];
		d1 = d1_a[1];
	} else {
		*zl = z1l / 1000;
		wcd937x_wcd_mbhc_qfuse_cal(component, zl, 0);
	}

	/* Start of right impedance ramp and calculation */
	wcd937x_mbhc_zdet_ramp(component, zdet_param_ptr, NULL, &z1r, d1);
	if (WCD937X_MBHC_IS_SECOND_RAMP_REQUIRED(z1r)) {
		if ((z1r > WCD937X_ZDET_VAL_1200 &&
		     zdet_param_ptr->noff == 0x6) ||
		     ((*zl) != WCD937X_ZDET_FLOATING_IMPEDANCE))
			goto right_ch_impedance;
		/* Second ramp for right ch */
		if (z1r < WCD937X_ZDET_VAL_32) {
			zdet_param_ptr = &zdet_param[0];
			d1 = d1_a[0];
		} else if ((z1r > WCD937X_ZDET_VAL_400) &&
			(z1r <= WCD937X_ZDET_VAL_1200)) {
			zdet_param_ptr = &zdet_param[2];
			d1 = d1_a[2];
		} else if (z1r > WCD937X_ZDET_VAL_1200) {
			zdet_param_ptr = &zdet_param[3];
			d1 = d1_a[3];
		}
		wcd937x_mbhc_zdet_ramp(component, zdet_param_ptr, NULL, &z1r, d1);
	}
right_ch_impedance:
	if (z1r == WCD937X_ZDET_FLOATING_IMPEDANCE ||
	    z1r > WCD937X_ZDET_VAL_100K) {
		*zr = WCD937X_ZDET_FLOATING_IMPEDANCE;
	} else {
		*zr = z1r / 1000;
		wcd937x_wcd_mbhc_qfuse_cal(component, zr, 1);
	}

	/* Mono/stereo detection */
	if ((*zl == WCD937X_ZDET_FLOATING_IMPEDANCE) &&
	    (*zr == WCD937X_ZDET_FLOATING_IMPEDANCE)) {
		dev_err(component->dev,
			"%s: plug type is invalid or extension cable\n",
			__func__);
		goto zdet_complete;
	}
	if ((*zl == WCD937X_ZDET_FLOATING_IMPEDANCE) ||
	    (*zr == WCD937X_ZDET_FLOATING_IMPEDANCE) ||
	    ((*zl < WCD_MONO_HS_MIN_THR) && (*zr > WCD_MONO_HS_MIN_THR)) ||
	    ((*zl > WCD_MONO_HS_MIN_THR) && (*zr < WCD_MONO_HS_MIN_THR))) {
		wcd_mbhc_set_hph_type(wcd937x->wcd_mbhc, WCD_MBHC_HPH_MONO);
		goto zdet_complete;
	}
	snd_soc_component_write_field(component, WCD937X_HPH_R_ATEST,
				      WCD937X_HPHPA_GND_OVR_MASK, 1);
	snd_soc_component_write_field(component, WCD937X_HPH_PA_CTL2,
				      WCD937X_HPHPA_GND_R_MASK, 1);
	if (*zl < (WCD937X_ZDET_VAL_32 / 1000))
		wcd937x_mbhc_zdet_ramp(component, &zdet_param[0], &z1ls, NULL, d1);
	else
		wcd937x_mbhc_zdet_ramp(component, &zdet_param[1], &z1ls, NULL, d1);
	snd_soc_component_write_field(component, WCD937X_HPH_PA_CTL2,
				      WCD937X_HPHPA_GND_R_MASK, 0);
	snd_soc_component_write_field(component, WCD937X_HPH_R_ATEST,
				      WCD937X_HPHPA_GND_OVR_MASK, 0);
	z1ls /= 1000;
	wcd937x_wcd_mbhc_qfuse_cal(component, &z1ls, 0);
	/* Parallel of left Z and 9 ohm pull down resistor */
	zMono = ((*zl) * 9) / ((*zl) + 9);
	z_diff1 = (z1ls > zMono) ? (z1ls - zMono) : (zMono - z1ls);
	z_diff2 = ((*zl) > z1ls) ? ((*zl) - z1ls) : (z1ls - (*zl));
	if ((z_diff1 * (*zl + z1ls)) > (z_diff2 * (z1ls + zMono)))
		wcd_mbhc_set_hph_type(wcd937x->wcd_mbhc, WCD_MBHC_HPH_STEREO);
	else
		wcd_mbhc_set_hph_type(wcd937x->wcd_mbhc, WCD_MBHC_HPH_MONO);

	/* Enable surge protection again after impedance detection */
	regmap_update_bits(wcd937x->regmap,
			   WCD937X_HPH_SURGE_HPHLR_SURGE_EN, 0xC0, 0xC0);
zdet_complete:
	snd_soc_component_write(component, WCD937X_ANA_MBHC_BTN5, reg0);
	snd_soc_component_write(component, WCD937X_ANA_MBHC_BTN6, reg1);
	snd_soc_component_write(component, WCD937X_ANA_MBHC_BTN7, reg2);
	/* Turn on 100k pull down on HPHL */
	regmap_update_bits(wcd937x->regmap,
			   WCD937X_ANA_MBHC_MECH, 0x01, 0x01);

	/* For NO-jack, re-enable L_DET_EN after Z-det measurements */
	if (wcd937x->mbhc_cfg.hphl_swh)
		regmap_update_bits(wcd937x->regmap,
				   WCD937X_ANA_MBHC_MECH, 0x80, 0x80);

	snd_soc_component_write(component, WCD937X_MBHC_NEW_ZDET_ANA_CTL, reg4);
	snd_soc_component_write(component, WCD937X_MBHC_CTL_CLK, reg3);
	if (is_fsm_disable)
		regmap_update_bits(wcd937x->regmap,
				   WCD937X_ANA_MBHC_ELECT, 0x80, 0x80);
}

static void wcd937x_mbhc_gnd_det_ctrl(struct snd_soc_component *component,
				      bool enable)
{
	if (enable) {
		snd_soc_component_write_field(component, WCD937X_ANA_MBHC_MECH,
					      WCD937X_MBHC_HSG_PULLUP_COMP_EN, 1);
		snd_soc_component_write_field(component, WCD937X_ANA_MBHC_MECH,
					      WCD937X_MBHC_GND_DET_EN_MASK, 1);
	} else {
		snd_soc_component_write_field(component, WCD937X_ANA_MBHC_MECH,
					      WCD937X_MBHC_GND_DET_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD937X_ANA_MBHC_MECH,
					      WCD937X_MBHC_HSG_PULLUP_COMP_EN, 0);
	}
}

static void wcd937x_mbhc_hph_pull_down_ctrl(struct snd_soc_component *component,
					    bool enable)
{
	snd_soc_component_write_field(component, WCD937X_HPH_PA_CTL2,
				      WCD937X_HPHPA_GND_R_MASK, enable);
	snd_soc_component_write_field(component, WCD937X_HPH_PA_CTL2,
				      WCD937X_HPHPA_GND_L_MASK, enable);
}

static void wcd937x_mbhc_moisture_config(struct snd_soc_component *component)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	if (wcd937x->mbhc_cfg.moist_rref == R_OFF) {
		snd_soc_component_write_field(component, WCD937X_MBHC_NEW_CTL_2,
					      WCD937X_M_RTH_CTL_MASK, R_OFF);
		return;
	}

	/* Do not enable moisture detection if jack type is NC */
	if (!wcd937x->mbhc_cfg.hphl_swh) {
		dev_err(component->dev, "%s: disable moisture detection for NC\n",
			__func__);
		snd_soc_component_write_field(component, WCD937X_MBHC_NEW_CTL_2,
					      WCD937X_M_RTH_CTL_MASK, R_OFF);
		return;
	}

	snd_soc_component_write_field(component, WCD937X_MBHC_NEW_CTL_2,
				      WCD937X_M_RTH_CTL_MASK, wcd937x->mbhc_cfg.moist_rref);
}

static void wcd937x_mbhc_moisture_detect_en(struct snd_soc_component *component, bool enable)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	if (enable)
		snd_soc_component_write_field(component, WCD937X_MBHC_NEW_CTL_2,
					      WCD937X_M_RTH_CTL_MASK, wcd937x->mbhc_cfg.moist_rref);
	else
		snd_soc_component_write_field(component, WCD937X_MBHC_NEW_CTL_2,
					      WCD937X_M_RTH_CTL_MASK, R_OFF);
}

static bool wcd937x_mbhc_get_moisture_status(struct snd_soc_component *component)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	bool ret = false;

	if (wcd937x->mbhc_cfg.moist_rref == R_OFF) {
		snd_soc_component_write_field(component, WCD937X_MBHC_NEW_CTL_2,
					      WCD937X_M_RTH_CTL_MASK, R_OFF);
		goto done;
	}

	/* Do not enable moisture detection if jack type is NC */
	if (!wcd937x->mbhc_cfg.hphl_swh) {
		dev_err(component->dev, "%s: disable moisture detection for NC\n",
			__func__);
		snd_soc_component_write_field(component, WCD937X_MBHC_NEW_CTL_2,
					      WCD937X_M_RTH_CTL_MASK, R_OFF);
		goto done;
	}

	/*
	 * If moisture_en is already enabled, then skip to plug type
	 * detection.
	 */
	if (snd_soc_component_read_field(component, WCD937X_MBHC_NEW_CTL_2, WCD937X_M_RTH_CTL_MASK))
		goto done;

	wcd937x_mbhc_moisture_detect_en(component, true);
	/* Read moisture comparator status */
	ret = ((snd_soc_component_read(component, WCD937X_MBHC_NEW_FSM_STATUS)
				       & 0x20) ? 0 : 1);
done:
	return ret;
}

static void wcd937x_mbhc_moisture_polling_ctrl(struct snd_soc_component *component,
					       bool enable)
{
	snd_soc_component_write_field(component,
				      WCD937X_MBHC_NEW_INT_MOISTURE_DET_POLLING_CTRL,
				      WCD937X_MOISTURE_EN_POLLING_MASK, enable);
}

static const struct wcd_mbhc_cb mbhc_cb = {
	.clk_setup = wcd937x_mbhc_clk_setup,
	.mbhc_bias = wcd937x_mbhc_mbhc_bias_control,
	.set_btn_thr = wcd937x_mbhc_program_btn_thr,
	.micbias_enable_status = wcd937x_mbhc_micb_en_status,
	.hph_pull_up_control_v2 = wcd937x_mbhc_hph_l_pull_up_control,
	.mbhc_micbias_control = wcd937x_mbhc_request_micbias,
	.mbhc_micb_ramp_control = wcd937x_mbhc_micb_ramp_control,
	.mbhc_micb_ctrl_thr_mic = wcd937x_mbhc_micb_ctrl_threshold_mic,
	.compute_impedance = wcd937x_wcd_mbhc_calc_impedance,
	.mbhc_gnd_det_ctrl = wcd937x_mbhc_gnd_det_ctrl,
	.hph_pull_down_ctrl = wcd937x_mbhc_hph_pull_down_ctrl,
	.mbhc_moisture_config = wcd937x_mbhc_moisture_config,
	.mbhc_get_moisture_status = wcd937x_mbhc_get_moisture_status,
	.mbhc_moisture_polling_ctrl = wcd937x_mbhc_moisture_polling_ctrl,
	.mbhc_moisture_detect_en = wcd937x_mbhc_moisture_detect_en,
};

static int wcd937x_get_hph_type(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd_mbhc_get_hph_type(wcd937x->wcd_mbhc);

	return 0;
}

static int wcd937x_hph_impedance_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	u32 zl, zr;
	bool hphr;
	struct soc_mixer_control *mc;
	struct snd_soc_component *component =
					snd_soc_kcontrol_component(kcontrol);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;
	wcd_mbhc_get_impedance(wcd937x->wcd_mbhc, &zl, &zr);
	ucontrol->value.integer.value[0] = hphr ? zr : zl;

	return 0;
}

static const struct snd_kcontrol_new hph_type_detect_controls[] = {
	SOC_SINGLE_EXT("HPH Type", 0, 0, WCD_MBHC_HPH_STEREO, 0,
		       wcd937x_get_hph_type, NULL),
};

static const struct snd_kcontrol_new impedance_detect_controls[] = {
	SOC_SINGLE_EXT("HPHL Impedance", 0, 0, INT_MAX, 0,
		       wcd937x_hph_impedance_get, NULL),
	SOC_SINGLE_EXT("HPHR Impedance", 0, 1, INT_MAX, 0,
		       wcd937x_hph_impedance_get, NULL),
};

static int wcd937x_mbhc_init(struct snd_soc_component *component)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	struct wcd_mbhc_intr *intr_ids = &wcd937x->intr_ids;

	intr_ids->mbhc_sw_intr = regmap_irq_get_virq(wcd937x->irq_chip,
						     WCD937X_IRQ_MBHC_SW_DET);
	intr_ids->mbhc_btn_press_intr = regmap_irq_get_virq(wcd937x->irq_chip,
							    WCD937X_IRQ_MBHC_BUTTON_PRESS_DET);
	intr_ids->mbhc_btn_release_intr = regmap_irq_get_virq(wcd937x->irq_chip,
							      WCD937X_IRQ_MBHC_BUTTON_RELEASE_DET);
	intr_ids->mbhc_hs_ins_intr = regmap_irq_get_virq(wcd937x->irq_chip,
							 WCD937X_IRQ_MBHC_ELECT_INS_REM_LEG_DET);
	intr_ids->mbhc_hs_rem_intr = regmap_irq_get_virq(wcd937x->irq_chip,
							 WCD937X_IRQ_MBHC_ELECT_INS_REM_DET);
	intr_ids->hph_left_ocp = regmap_irq_get_virq(wcd937x->irq_chip,
						     WCD937X_IRQ_HPHL_OCP_INT);
	intr_ids->hph_right_ocp = regmap_irq_get_virq(wcd937x->irq_chip,
						      WCD937X_IRQ_HPHR_OCP_INT);

	wcd937x->wcd_mbhc = wcd_mbhc_init(component, &mbhc_cb, intr_ids, wcd_mbhc_fields, true);
	if (IS_ERR(wcd937x->wcd_mbhc))
		return PTR_ERR(wcd937x->wcd_mbhc);

	snd_soc_add_component_controls(component, impedance_detect_controls,
				       ARRAY_SIZE(impedance_detect_controls));
	snd_soc_add_component_controls(component, hph_type_detect_controls,
				       ARRAY_SIZE(hph_type_detect_controls));

	return 0;
}

static void wcd937x_mbhc_deinit(struct snd_soc_component *component)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	wcd_mbhc_deinit(wcd937x->wcd_mbhc);
}

/* END MBHC */

static const struct snd_kcontrol_new wcd937x_snd_controls[] = {
	SOC_SINGLE_TLV("EAR_PA Volume", WCD937X_ANA_EAR_COMPANDER_CTL,
		       2, 0x10, 0, ear_pa_gain),
	SOC_ENUM_EXT("RX HPH Mode", rx_hph_mode_mux_enum,
		     wcd937x_rx_hph_mode_get, wcd937x_rx_hph_mode_put),

	SOC_SINGLE_EXT("HPHL_COMP Switch", SND_SOC_NOPM, 0, 1, 0,
		       wcd937x_get_compander, wcd937x_set_compander),
	SOC_SINGLE_EXT("HPHR_COMP Switch", SND_SOC_NOPM, 1, 1, 0,
		       wcd937x_get_compander, wcd937x_set_compander),

	SOC_SINGLE_TLV("HPHL Volume", WCD937X_HPH_L_EN, 0, 20, 1, line_gain),
	SOC_SINGLE_TLV("HPHR Volume", WCD937X_HPH_R_EN, 0, 20, 1, line_gain),
	SOC_SINGLE_TLV("ADC1 Volume", WCD937X_ANA_TX_CH1, 0, 20, 0, analog_gain),
	SOC_SINGLE_TLV("ADC2 Volume", WCD937X_ANA_TX_CH2, 0, 20, 0, analog_gain),
	SOC_SINGLE_TLV("ADC3 Volume", WCD937X_ANA_TX_CH3, 0, 20, 0, analog_gain),

	SOC_SINGLE_EXT("HPHL Switch", WCD937X_HPH_L, 0, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("HPHR Switch", WCD937X_HPH_R, 0, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("LO Switch", WCD937X_LO, 0, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),

	SOC_SINGLE_EXT("ADC1 Switch", WCD937X_ADC1, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("ADC2 Switch", WCD937X_ADC2, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("ADC3 Switch", WCD937X_ADC3, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("DMIC0 Switch", WCD937X_DMIC0, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("DMIC1 Switch", WCD937X_DMIC1, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("MBHC Switch", WCD937X_MBHC, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("DMIC2 Switch", WCD937X_DMIC2, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("DMIC3 Switch", WCD937X_DMIC3, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("DMIC4 Switch", WCD937X_DMIC4, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
	SOC_SINGLE_EXT("DMIC5 Switch", WCD937X_DMIC5, 1, 1, 0,
		       wcd937x_get_swr_port, wcd937x_set_swr_port),
};

static const struct snd_kcontrol_new adc1_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new adc2_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new adc3_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic1_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic2_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic3_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic4_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic5_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic6_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new ear_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new aux_rdac_switch[] = {
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

static const char * const rdac3_mux_text[] = {
	"RX1", "RX3"
};

static const struct soc_enum adc2_enum =
	SOC_ENUM_SINGLE(WCD937X_TX_NEW_TX_CH2_SEL, 7,
			ARRAY_SIZE(adc2_mux_text), adc2_mux_text);

static const struct soc_enum rdac3_enum =
	SOC_ENUM_SINGLE(WCD937X_DIGITAL_CDC_EAR_PATH_CTL, 0,
			ARRAY_SIZE(rdac3_mux_text), rdac3_mux_text);

static const struct snd_kcontrol_new tx_adc2_mux = SOC_DAPM_ENUM("ADC2 MUX Mux", adc2_enum);

static const struct snd_kcontrol_new rx_rdac3_mux = SOC_DAPM_ENUM("RDAC3_MUX Mux", rdac3_enum);

static const struct snd_soc_dapm_widget wcd937x_dapm_widgets[] = {
	/* Input widgets */
	SND_SOC_DAPM_INPUT("AMIC1"),
	SND_SOC_DAPM_INPUT("AMIC2"),
	SND_SOC_DAPM_INPUT("AMIC3"),
	SND_SOC_DAPM_INPUT("IN1_HPHL"),
	SND_SOC_DAPM_INPUT("IN2_HPHR"),
	SND_SOC_DAPM_INPUT("IN3_AUX"),

	/* TX widgets */
	SND_SOC_DAPM_ADC_E("ADC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd937x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC2", NULL, SND_SOC_NOPM, 1, 0,
			   wcd937x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("ADC1 REQ", SND_SOC_NOPM, 0, 0,
			     NULL, 0, wcd937x_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC2 REQ", SND_SOC_NOPM, 0, 0,
			     NULL, 0, wcd937x_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("ADC2 MUX", SND_SOC_NOPM, 0, 0, &tx_adc2_mux),

	/* TX mixers */
	SND_SOC_DAPM_MIXER_E("ADC1_MIXER", SND_SOC_NOPM, 0, 0,
			     adc1_switch, ARRAY_SIZE(adc1_switch),
			     wcd937x_tx_swr_ctrl, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC2_MIXER", SND_SOC_NOPM, 1, 0,
			     adc2_switch, ARRAY_SIZE(adc2_switch),
			     wcd937x_tx_swr_ctrl, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),

	/* MIC_BIAS widgets */
	SND_SOC_DAPM_SUPPLY("MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0,
			    wcd937x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0,
			    wcd937x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0,
			    wcd937x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY("VDD_BUCK", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("CLS_H_PORT", 1, SND_SOC_NOPM, 0, 0, NULL, 0),

	/* RX widgets */
	SND_SOC_DAPM_PGA_E("EAR PGA", WCD937X_ANA_EAR, 7, 0, NULL, 0,
			   wcd937x_codec_enable_ear_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("AUX PGA", WCD937X_AUX_AUXPA, 7, 0, NULL, 0,
			   wcd937x_codec_enable_aux_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHL PGA", WCD937X_ANA_HPH, 7, 0, NULL, 0,
			   wcd937x_codec_enable_hphl_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHR PGA", WCD937X_ANA_HPH, 6, 0, NULL, 0,
			   wcd937x_codec_enable_hphr_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_DAC_E("RDAC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd937x_codec_hphl_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC2", NULL, SND_SOC_NOPM, 0, 0,
			   wcd937x_codec_hphr_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC3", NULL, SND_SOC_NOPM, 0, 0,
			   wcd937x_codec_ear_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC4", NULL, SND_SOC_NOPM, 0, 0,
			   wcd937x_codec_aux_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("RDAC3_MUX", SND_SOC_NOPM, 0, 0, &rx_rdac3_mux),

	SND_SOC_DAPM_MIXER_E("RX1", SND_SOC_NOPM, 0, 0, NULL, 0,
			     wcd937x_enable_rx1, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("RX2", SND_SOC_NOPM, 0, 0, NULL, 0,
			     wcd937x_enable_rx2, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("RX3", SND_SOC_NOPM, 0, 0, NULL, 0,
			     wcd937x_enable_rx3, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),

	/* RX mixer widgets*/
	SND_SOC_DAPM_MIXER("EAR_RDAC", SND_SOC_NOPM, 0, 0,
			   ear_rdac_switch, ARRAY_SIZE(ear_rdac_switch)),
	SND_SOC_DAPM_MIXER("AUX_RDAC", SND_SOC_NOPM, 0, 0,
			   aux_rdac_switch, ARRAY_SIZE(aux_rdac_switch)),
	SND_SOC_DAPM_MIXER("HPHL_RDAC", SND_SOC_NOPM, 0, 0,
			   hphl_rdac_switch, ARRAY_SIZE(hphl_rdac_switch)),
	SND_SOC_DAPM_MIXER("HPHR_RDAC", SND_SOC_NOPM, 0, 0,
			   hphr_rdac_switch, ARRAY_SIZE(hphr_rdac_switch)),

	/* TX output widgets */
	SND_SOC_DAPM_OUTPUT("ADC1_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("ADC2_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("ADC3_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("WCD_TX_OUTPUT"),

	/* RX output widgets */
	SND_SOC_DAPM_OUTPUT("EAR"),
	SND_SOC_DAPM_OUTPUT("AUX"),
	SND_SOC_DAPM_OUTPUT("HPHL"),
	SND_SOC_DAPM_OUTPUT("HPHR"),

	/* MIC_BIAS pull up widgets */
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0,
			    wcd937x_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0,
			    wcd937x_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0,
			    wcd937x_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_widget wcd9375_dapm_widgets[] = {
	/* Input widgets */
	SND_SOC_DAPM_INPUT("AMIC4"),

	/* TX widgets */
	SND_SOC_DAPM_ADC_E("ADC3", NULL, SND_SOC_NOPM, 2, 0,
			   wcd937x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("ADC3 REQ", SND_SOC_NOPM, 0, 0,
			     NULL, 0, wcd937x_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("DMIC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd937x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC2", NULL, SND_SOC_NOPM, 1, 0,
			   wcd937x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC3", NULL, SND_SOC_NOPM, 2, 0,
			   wcd937x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC4", NULL, SND_SOC_NOPM, 3, 0,
			   wcd937x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC5", NULL, SND_SOC_NOPM, 4, 0,
			   wcd937x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC6", NULL, SND_SOC_NOPM, 5, 0,
			   wcd937x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* TX mixer widgets */
	SND_SOC_DAPM_MIXER_E("DMIC1_MIXER", SND_SOC_NOPM, 0,
			     0, dmic1_switch, ARRAY_SIZE(dmic1_switch),
			     wcd937x_tx_swr_ctrl, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC2_MIXER", SND_SOC_NOPM, 1,
			     0, dmic2_switch, ARRAY_SIZE(dmic2_switch),
			     wcd937x_tx_swr_ctrl, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC3_MIXER", SND_SOC_NOPM, 2,
			     0, dmic3_switch, ARRAY_SIZE(dmic3_switch),
			     wcd937x_tx_swr_ctrl, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC4_MIXER", SND_SOC_NOPM, 3,
			     0, dmic4_switch, ARRAY_SIZE(dmic4_switch),
			     wcd937x_tx_swr_ctrl, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC5_MIXER", SND_SOC_NOPM, 4,
			     0, dmic5_switch, ARRAY_SIZE(dmic5_switch),
			     wcd937x_tx_swr_ctrl, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC6_MIXER", SND_SOC_NOPM, 5,
			     0, dmic6_switch, ARRAY_SIZE(dmic6_switch),
			     wcd937x_tx_swr_ctrl, SND_SOC_DAPM_PRE_PMU |
			     SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC3_MIXER", SND_SOC_NOPM, 2, 0, adc3_switch,
			     ARRAY_SIZE(adc3_switch), wcd937x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* Output widgets */
	SND_SOC_DAPM_OUTPUT("DMIC1_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC2_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC3_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC4_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC5_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC6_OUTPUT"),
};

static const struct snd_soc_dapm_route wcd937x_audio_map[] = {
	{ "ADC1_OUTPUT", NULL, "ADC1_MIXER" },
	{ "ADC1_MIXER", "Switch", "ADC1 REQ" },
	{ "ADC1 REQ", NULL, "ADC1" },
	{ "ADC1", NULL, "AMIC1" },

	{ "ADC2_OUTPUT", NULL, "ADC2_MIXER" },
	{ "ADC2_MIXER", "Switch", "ADC2 REQ" },
	{ "ADC2 REQ", NULL, "ADC2" },
	{ "ADC2", NULL, "ADC2 MUX" },
	{ "ADC2 MUX", "INP3", "AMIC3" },
	{ "ADC2 MUX", "INP2", "AMIC2" },

	{ "IN1_HPHL", NULL, "VDD_BUCK" },
	{ "IN1_HPHL", NULL, "CLS_H_PORT" },
	{ "RX1", NULL, "IN1_HPHL" },
	{ "RDAC1", NULL, "RX1" },
	{ "HPHL_RDAC", "Switch", "RDAC1" },
	{ "HPHL PGA", NULL, "HPHL_RDAC" },
	{ "HPHL", NULL, "HPHL PGA" },

	{ "IN2_HPHR", NULL, "VDD_BUCK" },
	{ "IN2_HPHR", NULL, "CLS_H_PORT" },
	{ "RX2", NULL, "IN2_HPHR" },
	{ "RDAC2", NULL, "RX2" },
	{ "HPHR_RDAC", "Switch", "RDAC2" },
	{ "HPHR PGA", NULL, "HPHR_RDAC" },
	{ "HPHR", NULL, "HPHR PGA" },

	{ "IN3_AUX", NULL, "VDD_BUCK" },
	{ "IN3_AUX", NULL, "CLS_H_PORT" },
	{ "RX3", NULL, "IN3_AUX" },
	{ "RDAC4", NULL, "RX3" },
	{ "AUX_RDAC", "Switch", "RDAC4" },
	{ "AUX PGA", NULL, "AUX_RDAC" },
	{ "AUX", NULL, "AUX PGA" },

	{ "RDAC3_MUX", "RX3", "RX3" },
	{ "RDAC3_MUX", "RX1", "RX1" },
	{ "RDAC3", NULL, "RDAC3_MUX" },
	{ "EAR_RDAC", "Switch", "RDAC3" },
	{ "EAR PGA", NULL, "EAR_RDAC" },
	{ "EAR", NULL, "EAR PGA" },
};

static const struct snd_soc_dapm_route wcd9375_audio_map[] = {
	{ "ADC3_OUTPUT", NULL, "ADC3_MIXER" },
	{ "ADC3_OUTPUT", NULL, "ADC3_MIXER" },
	{ "ADC3_MIXER", "Switch", "ADC3 REQ" },
	{ "ADC3 REQ", NULL, "ADC3" },
	{ "ADC3", NULL, "AMIC4" },

	{ "DMIC1_OUTPUT", NULL, "DMIC1_MIXER" },
	{ "DMIC1_MIXER", "Switch", "DMIC1" },

	{ "DMIC2_OUTPUT", NULL, "DMIC2_MIXER" },
	{ "DMIC2_MIXER", "Switch", "DMIC2" },

	{ "DMIC3_OUTPUT", NULL, "DMIC3_MIXER" },
	{ "DMIC3_MIXER", "Switch", "DMIC3" },

	{ "DMIC4_OUTPUT", NULL, "DMIC4_MIXER" },
	{ "DMIC4_MIXER", "Switch", "DMIC4" },

	{ "DMIC5_OUTPUT", NULL, "DMIC5_MIXER" },
	{ "DMIC5_MIXER", "Switch", "DMIC5" },

	{ "DMIC6_OUTPUT", NULL, "DMIC6_MIXER" },
	{ "DMIC6_MIXER", "Switch", "DMIC6" },
};

static int wcd937x_set_micbias_data(struct wcd937x_priv *wcd937x)
{
	int vout_ctl[3];

	/* Set micbias voltage */
	vout_ctl[0] = wcd937x_get_micb_vout_ctl_val(wcd937x->micb1_mv);
	vout_ctl[1] = wcd937x_get_micb_vout_ctl_val(wcd937x->micb2_mv);
	vout_ctl[2] = wcd937x_get_micb_vout_ctl_val(wcd937x->micb3_mv);
	if ((vout_ctl[0] | vout_ctl[1] | vout_ctl[2]) < 0)
		return -EINVAL;

	regmap_update_bits(wcd937x->regmap, WCD937X_ANA_MICB1, WCD937X_ANA_MICB_VOUT, vout_ctl[0]);
	regmap_update_bits(wcd937x->regmap, WCD937X_ANA_MICB2, WCD937X_ANA_MICB_VOUT, vout_ctl[1]);
	regmap_update_bits(wcd937x->regmap, WCD937X_ANA_MICB3, WCD937X_ANA_MICB_VOUT, vout_ctl[2]);

	return 0;
}

static irqreturn_t wcd937x_wd_handle_irq(int irq, void *data)
{
	return IRQ_HANDLED;
}

static const struct irq_chip wcd_irq_chip = {
	.name = "WCD937x",
};

static int wcd_irq_chip_map(struct irq_domain *irqd, unsigned int virq,
			    irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &wcd_irq_chip, handle_simple_irq);
	irq_set_nested_thread(virq, 1);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops wcd_domain_ops = {
	.map = wcd_irq_chip_map,
};

static int wcd937x_irq_init(struct wcd937x_priv *wcd, struct device *dev)
{
	wcd->virq = irq_domain_create_linear(NULL, 1, &wcd_domain_ops, NULL);
	if (!(wcd->virq)) {
		dev_err(dev, "%s: Failed to add IRQ domain\n", __func__);
		return -EINVAL;
	}

	return devm_regmap_add_irq_chip(dev, wcd->regmap,
					irq_create_mapping(wcd->virq, 0),
					IRQF_ONESHOT, 0, &wcd937x_regmap_irq_chip,
					&wcd->irq_chip);
}

static int wcd937x_soc_codec_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);
	struct sdw_slave *tx_sdw_dev = wcd937x->tx_sdw_dev;
	struct device *dev = component->dev;
	unsigned long time_left;
	int i, ret;
	u32 chipid;

	time_left = wait_for_completion_timeout(&tx_sdw_dev->initialization_complete,
						msecs_to_jiffies(5000));
	if (!time_left) {
		dev_err(dev, "soundwire device init timeout\n");
		return -ETIMEDOUT;
	}

	snd_soc_component_init_regmap(component, wcd937x->regmap);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	chipid = (snd_soc_component_read(component,
					 WCD937X_DIGITAL_EFUSE_REG_0) & 0x1e) >> 1;
	if (chipid != CHIPID_WCD9370 && chipid != CHIPID_WCD9375) {
		dev_err(dev, "Got unknown chip id: 0x%x\n", chipid);
		pm_runtime_put(dev);
		return -EINVAL;
	}

	wcd937x->clsh_info = wcd_clsh_ctrl_alloc(component, WCD937X);
	if (IS_ERR(wcd937x->clsh_info)) {
		pm_runtime_put(dev);
		return PTR_ERR(wcd937x->clsh_info);
	}

	wcd937x_io_init(wcd937x->regmap);
	/* Set all interrupts as edge triggered */
	for (i = 0; i < wcd937x_regmap_irq_chip.num_regs; i++)
		regmap_write(wcd937x->regmap, (WCD937X_DIGITAL_INTR_LEVEL_0 + i), 0);

	pm_runtime_put(dev);

	wcd937x->hphr_pdm_wd_int = regmap_irq_get_virq(wcd937x->irq_chip,
						       WCD937X_IRQ_HPHR_PDM_WD_INT);
	wcd937x->hphl_pdm_wd_int = regmap_irq_get_virq(wcd937x->irq_chip,
						       WCD937X_IRQ_HPHL_PDM_WD_INT);
	wcd937x->aux_pdm_wd_int = regmap_irq_get_virq(wcd937x->irq_chip,
						      WCD937X_IRQ_AUX_PDM_WD_INT);

	/* Request for watchdog interrupt */
	ret = devm_request_threaded_irq(dev, wcd937x->hphr_pdm_wd_int, NULL, wcd937x_wd_handle_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"HPHR PDM WDOG INT", wcd937x);
	if (ret)
		dev_err(dev, "Failed to request HPHR watchdog interrupt (%d)\n", ret);

	ret = devm_request_threaded_irq(dev, wcd937x->hphl_pdm_wd_int, NULL, wcd937x_wd_handle_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"HPHL PDM WDOG INT", wcd937x);
	if (ret)
		dev_err(dev, "Failed to request HPHL watchdog interrupt (%d)\n", ret);

	ret = devm_request_threaded_irq(dev, wcd937x->aux_pdm_wd_int, NULL, wcd937x_wd_handle_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"AUX PDM WDOG INT", wcd937x);
	if (ret)
		dev_err(dev, "Failed to request Aux watchdog interrupt (%d)\n", ret);

	/* Disable watchdog interrupt for HPH and AUX */
	disable_irq_nosync(wcd937x->hphr_pdm_wd_int);
	disable_irq_nosync(wcd937x->hphl_pdm_wd_int);
	disable_irq_nosync(wcd937x->aux_pdm_wd_int);

	if (chipid == CHIPID_WCD9375) {
		ret = snd_soc_dapm_new_controls(dapm, wcd9375_dapm_widgets,
						ARRAY_SIZE(wcd9375_dapm_widgets));
		if (ret < 0) {
			dev_err(component->dev, "Failed to add snd_ctls\n");
			wcd_clsh_ctrl_free(wcd937x->clsh_info);
			return ret;
		}

		ret = snd_soc_dapm_add_routes(dapm, wcd9375_audio_map,
					      ARRAY_SIZE(wcd9375_audio_map));
		if (ret < 0) {
			dev_err(component->dev, "Failed to add routes\n");
			wcd_clsh_ctrl_free(wcd937x->clsh_info);
			return ret;
		}
	}

	ret = wcd937x_mbhc_init(component);
	if (ret)
		dev_err(component->dev, "mbhc initialization failed\n");

	return ret;
}

static void wcd937x_soc_codec_remove(struct snd_soc_component *component)
{
	struct wcd937x_priv *wcd937x = snd_soc_component_get_drvdata(component);

	wcd937x_mbhc_deinit(component);
	free_irq(wcd937x->aux_pdm_wd_int, wcd937x);
	free_irq(wcd937x->hphl_pdm_wd_int, wcd937x);
	free_irq(wcd937x->hphr_pdm_wd_int, wcd937x);

	wcd_clsh_ctrl_free(wcd937x->clsh_info);
}

static int wcd937x_codec_set_jack(struct snd_soc_component *comp,
				  struct snd_soc_jack *jack, void *data)
{
	struct wcd937x_priv *wcd = dev_get_drvdata(comp->dev);
	int ret = 0;

	if (jack)
		ret = wcd_mbhc_start(wcd->wcd_mbhc, &wcd->mbhc_cfg, jack);
	else
		wcd_mbhc_stop(wcd->wcd_mbhc);

	return ret;
}

static const struct snd_soc_component_driver soc_codec_dev_wcd937x = {
	.name = "wcd937x_codec",
	.probe = wcd937x_soc_codec_probe,
	.remove = wcd937x_soc_codec_remove,
	.controls = wcd937x_snd_controls,
	.num_controls = ARRAY_SIZE(wcd937x_snd_controls),
	.dapm_widgets = wcd937x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wcd937x_dapm_widgets),
	.dapm_routes = wcd937x_audio_map,
	.num_dapm_routes = ARRAY_SIZE(wcd937x_audio_map),
	.set_jack = wcd937x_codec_set_jack,
	.endianness = 1,
};

static void wcd937x_dt_parse_micbias_info(struct device *dev, struct wcd937x_priv *wcd)
{
	struct device_node *np = dev->of_node;
	u32 prop_val = 0;
	int ret = 0;

	ret = of_property_read_u32(np, "qcom,micbias1-microvolt", &prop_val);
	if (!ret)
		wcd->micb1_mv = prop_val / 1000;
	else
		dev_warn(dev, "Micbias1 DT property not found\n");

	ret = of_property_read_u32(np, "qcom,micbias2-microvolt", &prop_val);
	if (!ret)
		wcd->micb2_mv = prop_val / 1000;
	else
		dev_warn(dev, "Micbias2 DT property not found\n");

	ret = of_property_read_u32(np, "qcom,micbias3-microvolt", &prop_val);
	if (!ret)
		wcd->micb3_mv = prop_val / 1000;
	else
		dev_warn(dev, "Micbias3 DT property not found\n");
}

static bool wcd937x_swap_gnd_mic(struct snd_soc_component *component, bool active)
{
	int value;
	struct wcd937x_priv *wcd937x;

	wcd937x = snd_soc_component_get_drvdata(component);

	value = gpiod_get_value(wcd937x->us_euro_gpio);
	gpiod_set_value(wcd937x->us_euro_gpio, !value);

	return true;
}

static int wcd937x_codec_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct wcd937x_priv *wcd937x = dev_get_drvdata(dai->dev);
	struct wcd937x_sdw_priv *wcd = wcd937x->sdw_priv[dai->id];

	return wcd937x_sdw_hw_params(wcd, substream, params, dai);
}

static int wcd937x_codec_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct wcd937x_priv *wcd937x = dev_get_drvdata(dai->dev);
	struct wcd937x_sdw_priv *wcd = wcd937x->sdw_priv[dai->id];

	return sdw_stream_remove_slave(wcd->sdev, wcd->sruntime);
}

static int wcd937x_codec_set_sdw_stream(struct snd_soc_dai *dai,
					void *stream, int direction)
{
	struct wcd937x_priv *wcd937x = dev_get_drvdata(dai->dev);
	struct wcd937x_sdw_priv *wcd = wcd937x->sdw_priv[dai->id];

	wcd->sruntime = stream;

	return 0;
}

static int wcd937x_get_channel_map(const struct snd_soc_dai *dai,
				   unsigned int *tx_num, unsigned int *tx_slot,
				   unsigned int *rx_num, unsigned int *rx_slot)
{
	struct wcd937x_priv *wcd937x = dev_get_drvdata(dai->dev);
	struct wcd937x_sdw_priv *wcd = wcd937x->sdw_priv[dai->id];
	int i;

	switch (dai->id) {
	case AIF1_PB:
		if (!rx_slot || !rx_num) {
			dev_err(dai->dev, "Invalid rx_slot %p or rx_num %p\n",
				rx_slot, rx_num);
			return -EINVAL;
		}

		for (i = 0; i < SDW_MAX_PORTS; i++)
			rx_slot[i] = wcd->master_channel_map[i];

		*rx_num = i;
		break;
	case AIF1_CAP:
		if (!tx_slot || !tx_num) {
			dev_err(dai->dev, "Invalid tx_slot %p or tx_num %p\n",
				tx_slot, tx_num);
			return -EINVAL;
		}

		for (i = 0; i < SDW_MAX_PORTS; i++)
			tx_slot[i] = wcd->master_channel_map[i];

		*tx_num = i;
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops wcd937x_sdw_dai_ops = {
	.hw_params = wcd937x_codec_hw_params,
	.hw_free = wcd937x_codec_free,
	.set_stream = wcd937x_codec_set_sdw_stream,
	.get_channel_map = wcd937x_get_channel_map,
};

static struct snd_soc_dai_driver wcd937x_dais[] = {
	[0] = {
		.name = "wcd937x-sdw-rx",
		.playback = {
			.stream_name = "WCD AIF Playback",
			.rates = WCD937X_RATES | WCD937X_FRAC_RATES,
			.formats = WCD937X_FORMATS,
			.rate_min = 8000,
			.rate_max = 384000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd937x_sdw_dai_ops,
	},
	[1] = {
		.name = "wcd937x-sdw-tx",
		.capture = {
			.stream_name = "WCD AIF Capture",
			.rates = WCD937X_RATES,
			.formats = WCD937X_FORMATS,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd937x_sdw_dai_ops,
	},
};

static int wcd937x_bind(struct device *dev)
{
	struct wcd937x_priv *wcd937x = dev_get_drvdata(dev);
	int ret;

	/* Give the SDW subdevices some more time to settle */
	usleep_range(5000, 5010);

	ret = component_bind_all(dev, wcd937x);
	if (ret) {
		dev_err(dev, "Slave bind failed, ret = %d\n", ret);
		return ret;
	}

	wcd937x->rxdev = wcd937x_sdw_device_get(wcd937x->rxnode);
	if (!wcd937x->rxdev) {
		dev_err(dev, "could not find slave with matching of node\n");
		return -EINVAL;
	}

	wcd937x->sdw_priv[AIF1_PB] = dev_get_drvdata(wcd937x->rxdev);
	wcd937x->sdw_priv[AIF1_PB]->wcd937x = wcd937x;

	wcd937x->txdev = wcd937x_sdw_device_get(wcd937x->txnode);
	if (!wcd937x->txdev) {
		dev_err(dev, "could not find txslave with matching of node\n");
		return -EINVAL;
	}

	wcd937x->sdw_priv[AIF1_CAP] = dev_get_drvdata(wcd937x->txdev);
	wcd937x->sdw_priv[AIF1_CAP]->wcd937x = wcd937x;
	wcd937x->tx_sdw_dev = dev_to_sdw_dev(wcd937x->txdev);
	if (!wcd937x->tx_sdw_dev) {
		dev_err(dev, "could not get txslave with matching of dev\n");
		return -EINVAL;
	}

	/*
	 * As TX is the main CSR reg interface, which should not be suspended first.
	 * expicilty add the dependency link
	 */
	if (!device_link_add(wcd937x->rxdev, wcd937x->txdev,
			     DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "Could not devlink TX and RX\n");
		return -EINVAL;
	}

	if (!device_link_add(dev, wcd937x->txdev,
			     DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "Could not devlink WCD and TX\n");
		return -EINVAL;
	}

	if (!device_link_add(dev, wcd937x->rxdev,
			     DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "Could not devlink WCD and RX\n");
		return -EINVAL;
	}

	wcd937x->regmap = dev_get_regmap(&wcd937x->tx_sdw_dev->dev, NULL);
	if (!wcd937x->regmap) {
		dev_err(dev, "could not get TX device regmap\n");
		return -EINVAL;
	}

	ret = wcd937x_irq_init(wcd937x, dev);
	if (ret) {
		dev_err(dev, "IRQ init failed: %d\n", ret);
		return ret;
	}

	wcd937x->sdw_priv[AIF1_PB]->slave_irq = wcd937x->virq;
	wcd937x->sdw_priv[AIF1_CAP]->slave_irq = wcd937x->virq;

	ret = wcd937x_set_micbias_data(wcd937x);
	if (ret < 0) {
		dev_err(dev, "Bad micbias pdata\n");
		return ret;
	}

	ret = snd_soc_register_component(dev, &soc_codec_dev_wcd937x,
					 wcd937x_dais, ARRAY_SIZE(wcd937x_dais));
	if (ret)
		dev_err(dev, "Codec registration failed\n");

	return ret;
}

static void wcd937x_unbind(struct device *dev)
{
	struct wcd937x_priv *wcd937x = dev_get_drvdata(dev);

	snd_soc_unregister_component(dev);
	device_link_remove(dev, wcd937x->txdev);
	device_link_remove(dev, wcd937x->rxdev);
	device_link_remove(wcd937x->rxdev, wcd937x->txdev);
	component_unbind_all(dev, wcd937x);
	mutex_destroy(&wcd937x->micb_lock);
}

static const struct component_master_ops wcd937x_comp_ops = {
	.bind = wcd937x_bind,
	.unbind = wcd937x_unbind,
};

static int wcd937x_add_slave_components(struct wcd937x_priv *wcd937x,
					struct device *dev,
					struct component_match **matchptr)
{
	struct device_node *np = dev->of_node;

	wcd937x->rxnode = of_parse_phandle(np, "qcom,rx-device", 0);
	if (!wcd937x->rxnode) {
		dev_err(dev, "Couldn't parse phandle to qcom,rx-device!\n");
		return -ENODEV;
	}
	of_node_get(wcd937x->rxnode);
	component_match_add_release(dev, matchptr, component_release_of,
				    component_compare_of, wcd937x->rxnode);

	wcd937x->txnode = of_parse_phandle(np, "qcom,tx-device", 0);
	if (!wcd937x->txnode) {
		dev_err(dev, "Couldn't parse phandle to qcom,tx-device\n");
			return -ENODEV;
	}
	of_node_get(wcd937x->txnode);
	component_match_add_release(dev, matchptr, component_release_of,
				    component_compare_of, wcd937x->txnode);

	return 0;
}

static int wcd937x_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct device *dev = &pdev->dev;
	struct wcd937x_priv *wcd937x;
	struct wcd_mbhc_config *cfg;
	int ret;

	wcd937x = devm_kzalloc(dev, sizeof(*wcd937x), GFP_KERNEL);
	if (!wcd937x)
		return -ENOMEM;

	dev_set_drvdata(dev, wcd937x);
	mutex_init(&wcd937x->micb_lock);

	wcd937x->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(wcd937x->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(wcd937x->reset_gpio),
				     "failed to reset wcd gpio\n");

	wcd937x->us_euro_gpio = devm_gpiod_get_optional(dev, "us-euro", GPIOD_OUT_LOW);
	if (IS_ERR(wcd937x->us_euro_gpio))
		return dev_err_probe(dev, PTR_ERR(wcd937x->us_euro_gpio),
				"us-euro swap Control GPIO not found\n");

	cfg = &wcd937x->mbhc_cfg;
	cfg->swap_gnd_mic = wcd937x_swap_gnd_mic;

	wcd937x->supplies[0].supply = "vdd-rxtx";
	wcd937x->supplies[1].supply = "vdd-px";
	wcd937x->supplies[2].supply = "vdd-mic-bias";
	wcd937x->supplies[3].supply = "vdd-buck";

	ret = devm_regulator_bulk_get(dev, WCD937X_MAX_BULK_SUPPLY, wcd937x->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get supplies\n");

	ret = regulator_bulk_enable(WCD937X_MAX_BULK_SUPPLY, wcd937x->supplies);
	if (ret) {
		regulator_bulk_free(WCD937X_MAX_BULK_SUPPLY, wcd937x->supplies);
		return dev_err_probe(dev, ret, "Failed to enable supplies\n");
	}

	wcd937x_dt_parse_micbias_info(dev, wcd937x);

	cfg->mbhc_micbias = MIC_BIAS_2;
	cfg->anc_micbias = MIC_BIAS_2;
	cfg->v_hs_max = WCD_MBHC_HS_V_MAX;
	cfg->num_btn = WCD937X_MBHC_MAX_BUTTONS;
	cfg->micb_mv = wcd937x->micb2_mv;
	cfg->linein_th = 5000;
	cfg->hs_thr = 1700;
	cfg->hph_thr = 50;

	wcd_dt_parse_mbhc_data(dev, &wcd937x->mbhc_cfg);

	ret = wcd937x_add_slave_components(wcd937x, dev, &match);
	if (ret)
		goto err_disable_regulators;

	wcd937x_reset(wcd937x);

	ret = component_master_add_with_match(dev, &wcd937x_comp_ops, match);
	if (ret)
		goto err_disable_regulators;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_disable_regulators:
	regulator_bulk_disable(WCD937X_MAX_BULK_SUPPLY, wcd937x->supplies);
	regulator_bulk_free(WCD937X_MAX_BULK_SUPPLY, wcd937x->supplies);

	return ret;
}

static void wcd937x_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wcd937x_priv *wcd937x = dev_get_drvdata(dev);

	component_master_del(&pdev->dev, &wcd937x_comp_ops);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_dont_use_autosuspend(dev);

	regulator_bulk_disable(WCD937X_MAX_BULK_SUPPLY, wcd937x->supplies);
	regulator_bulk_free(WCD937X_MAX_BULK_SUPPLY, wcd937x->supplies);
}

#if defined(CONFIG_OF)
static const struct of_device_id wcd937x_of_match[] = {
	{ .compatible = "qcom,wcd9370-codec" },
	{ .compatible = "qcom,wcd9375-codec" },
	{ }
};
MODULE_DEVICE_TABLE(of, wcd937x_of_match);
#endif

static struct platform_driver wcd937x_codec_driver = {
	.probe = wcd937x_probe,
	.remove = wcd937x_remove,
	.driver = {
		.name = "wcd937x_codec",
		.of_match_table = of_match_ptr(wcd937x_of_match),
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(wcd937x_codec_driver);
MODULE_DESCRIPTION("WCD937X Codec driver");
MODULE_LICENSE("GPL");
