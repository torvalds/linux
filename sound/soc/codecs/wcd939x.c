// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/component.h>
#include <sound/tlv.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_altmode.h>

#include "wcd-clsh-v2.h"
#include "wcd-mbhc-v2.h"
#include "wcd939x.h"

#define WCD939X_MAX_MICBIAS		(4)
#define WCD939X_MAX_SUPPLY		(4)
#define WCD939X_MBHC_MAX_BUTTONS	(8)
#define TX_ADC_MAX			(4)
#define WCD_MBHC_HS_V_MAX		1600

enum {
	WCD939X_VERSION_1_0 = 0,
	WCD939X_VERSION_1_1,
	WCD939X_VERSION_2_0,
};

#define WCD939X_RATES_MASK (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			    SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			    SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000 |\
			    SNDRV_PCM_RATE_384000)
/* Fractional Rates */
#define WCD939X_FRAC_RATES_MASK (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_88200 |\
				 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_352800)
#define WCD939X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S24_3LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

/* Convert from vout ctl to micbias voltage in mV */
#define WCD_VOUT_CTL_TO_MICB(v)		(1000 + (v) * 50)
#define SWR_CLK_RATE_0P6MHZ		(600000)
#define SWR_CLK_RATE_1P2MHZ		(1200000)
#define SWR_CLK_RATE_2P4MHZ		(2400000)
#define SWR_CLK_RATE_4P8MHZ		(4800000)
#define SWR_CLK_RATE_9P6MHZ		(9600000)
#define SWR_CLK_RATE_11P2896MHZ		(1128960)

#define ADC_MODE_VAL_HIFI		0x01
#define ADC_MODE_VAL_LO_HIF		0x02
#define ADC_MODE_VAL_NORMAL		0x03
#define ADC_MODE_VAL_LP			0x05
#define ADC_MODE_VAL_ULP1		0x09
#define ADC_MODE_VAL_ULP2		0x0B

/* Z value defined in milliohm */
#define WCD939X_ZDET_VAL_32		(32000)
#define WCD939X_ZDET_VAL_400		(400000)
#define WCD939X_ZDET_VAL_1200		(1200000)
#define WCD939X_ZDET_VAL_100K		(100000000)

/* Z floating defined in ohms */
#define WCD939X_ZDET_FLOATING_IMPEDANCE	(0x0FFFFFFE)
#define WCD939X_ZDET_NUM_MEASUREMENTS	(900)
#define WCD939X_MBHC_GET_C1(c)		(((c) & 0xC000) >> 14)
#define WCD939X_MBHC_GET_X1(x)		((x) & 0x3FFF)

/* Z value compared in milliOhm */
#define WCD939X_MBHC_IS_SECOND_RAMP_REQUIRED(z) false
#define WCD939X_ANA_MBHC_ZDET_CONST	(1018 * 1024)

enum {
	WCD9390 = 0,
	WCD9395 = 5,
};

enum {
	/* INTR_CTRL_INT_MASK_0 */
	WCD939X_IRQ_MBHC_BUTTON_PRESS_DET = 0,
	WCD939X_IRQ_MBHC_BUTTON_RELEASE_DET,
	WCD939X_IRQ_MBHC_ELECT_INS_REM_DET,
	WCD939X_IRQ_MBHC_ELECT_INS_REM_LEG_DET,
	WCD939X_IRQ_MBHC_SW_DET,
	WCD939X_IRQ_HPHR_OCP_INT,
	WCD939X_IRQ_HPHR_CNP_INT,
	WCD939X_IRQ_HPHL_OCP_INT,

	/* INTR_CTRL_INT_MASK_1 */
	WCD939X_IRQ_HPHL_CNP_INT,
	WCD939X_IRQ_EAR_CNP_INT,
	WCD939X_IRQ_EAR_SCD_INT,
	WCD939X_IRQ_HPHL_PDM_WD_INT,
	WCD939X_IRQ_HPHR_PDM_WD_INT,
	WCD939X_IRQ_EAR_PDM_WD_INT,

	/* INTR_CTRL_INT_MASK_2 */
	WCD939X_IRQ_MBHC_MOISTURE_INT,
	WCD939X_IRQ_HPHL_SURGE_DET_INT,
	WCD939X_IRQ_HPHR_SURGE_DET_INT,
	WCD939X_NUM_IRQS,
};

enum {
	MICB_BIAS_DISABLE = 0,
	MICB_BIAS_ENABLE,
	MICB_BIAS_PULL_UP,
	MICB_BIAS_PULL_DOWN,
};

enum {
	WCD_ADC1 = 0,
	WCD_ADC2,
	WCD_ADC3,
	WCD_ADC4,
	HPH_PA_DELAY,
};

enum {
	ADC_MODE_INVALID = 0,
	ADC_MODE_HIFI,
	ADC_MODE_LO_HIF,
	ADC_MODE_NORMAL,
	ADC_MODE_LP,
	ADC_MODE_ULP1,
	ADC_MODE_ULP2,
};

enum {
	AIF1_PB = 0,
	AIF1_CAP,
	NUM_CODEC_DAIS,
};

static u8 tx_mode_bit[] = {
	[ADC_MODE_INVALID] = 0x00,
	[ADC_MODE_HIFI] = 0x01,
	[ADC_MODE_LO_HIF] = 0x02,
	[ADC_MODE_NORMAL] = 0x04,
	[ADC_MODE_LP] = 0x08,
	[ADC_MODE_ULP1] = 0x10,
	[ADC_MODE_ULP2] = 0x20,
};

struct zdet_param {
	u16 ldo_ctl;
	u16 noff;
	u16 nshift;
	u16 btn5;
	u16 btn6;
	u16 btn7;
};

struct wcd939x_priv {
	struct sdw_slave *tx_sdw_dev;
	struct wcd939x_sdw_priv *sdw_priv[NUM_CODEC_DAIS];
	struct device *txdev;
	struct device *rxdev;
	struct device_node *rxnode, *txnode;
	struct regmap *regmap;
	struct snd_soc_component *component;
	/* micb setup lock */
	struct mutex micb_lock;
	/* typec handling */
	bool typec_analog_mux;
#if IS_ENABLED(CONFIG_TYPEC)
	struct typec_mux_dev *typec_mux;
	struct typec_switch_dev *typec_sw;
	enum typec_orientation typec_orientation;
	unsigned long typec_mode;
	struct typec_switch *typec_switch;
#endif /* CONFIG_TYPEC */
	/* mbhc module */
	struct wcd_mbhc *wcd_mbhc;
	struct wcd_mbhc_config mbhc_cfg;
	struct wcd_mbhc_intr intr_ids;
	struct wcd_clsh_ctrl *clsh_info;
	struct irq_domain *virq;
	struct regmap_irq_chip *wcd_regmap_irq_chip;
	struct regmap_irq_chip_data *irq_chip;
	struct regulator_bulk_data supplies[WCD939X_MAX_SUPPLY];
	struct snd_soc_jack *jack;
	unsigned long status_mask;
	s32 micb_ref[WCD939X_MAX_MICBIAS];
	s32 pullup_ref[WCD939X_MAX_MICBIAS];
	u32 hph_mode;
	u32 tx_mode[TX_ADC_MAX];
	int variant;
	int reset_gpio;
	u32 micb1_mv;
	u32 micb2_mv;
	u32 micb3_mv;
	u32 micb4_mv;
	int hphr_pdm_wd_int;
	int hphl_pdm_wd_int;
	int ear_pdm_wd_int;
	bool comp1_enable;
	bool comp2_enable;
	bool ldoh;
};

static const SNDRV_CTL_TLVD_DECLARE_DB_MINMAX(ear_pa_gain, 600, -1800);
static const DECLARE_TLV_DB_SCALE(line_gain, 0, 7, 1);
static const DECLARE_TLV_DB_SCALE(analog_gain, 0, 25, 1);

static struct wcd_mbhc_field wcd_mbhc_fields[WCD_MBHC_REG_FUNC_MAX] = {
	WCD_MBHC_FIELD(WCD_MBHC_L_DET_EN, WCD939X_ANA_MBHC_MECH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_GND_DET_EN, WCD939X_ANA_MBHC_MECH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_MECH_DETECTION_TYPE, WCD939X_ANA_MBHC_MECH, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_CLAMP_CTL, WCD939X_MBHC_NEW_PLUG_DETECT_CTL, 0x30),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_DETECTION_TYPE, WCD939X_ANA_MBHC_ELECT, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_CTRL, WCD939X_MBHC_NEW_INT_MECH_DET_CURRENT, 0x1F),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_COMP_CTRL, WCD939X_ANA_MBHC_MECH, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PLUG_TYPE, WCD939X_ANA_MBHC_MECH, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_GND_PLUG_TYPE, WCD939X_ANA_MBHC_MECH, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_SW_HPH_LP_100K_TO_GND, WCD939X_ANA_MBHC_MECH, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_SCHMT_ISRC, WCD939X_ANA_MBHC_ELECT, 0x06),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_EN, WCD939X_ANA_MBHC_ELECT, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_INSREM_DBNC, WCD939X_MBHC_NEW_PLUG_DETECT_CTL, 0x0F),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_DBNC, WCD939X_MBHC_NEW_CTL_1, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_VREF, WCD939X_MBHC_NEW_CTL_2, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_COMP_RESULT, WCD939X_ANA_MBHC_RESULT_3, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_IN2P_CLAMP_STATE, WCD939X_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_SCHMT_RESULT, WCD939X_ANA_MBHC_RESULT_3, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_SCHMT_RESULT, WCD939X_ANA_MBHC_RESULT_3, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_SCHMT_RESULT, WCD939X_ANA_MBHC_RESULT_3, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_OCP_FSM_EN, WCD939X_HPH_OCP_CTL, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_RESULT, WCD939X_ANA_MBHC_RESULT_3, 0x07),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_ISRC_CTL, WCD939X_ANA_MBHC_ELECT, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_RESULT, WCD939X_ANA_MBHC_RESULT_3, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB_CTRL, WCD939X_ANA_MICB2, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_CNP_WG_TIME, WCD939X_HPH_CNP_WG_TIME, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_PA_EN, WCD939X_ANA_HPH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PA_EN, WCD939X_ANA_HPH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_PA_EN, WCD939X_ANA_HPH, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_SWCH_LEVEL_REMOVE, WCD939X_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_ANC_DET_EN, WCD939X_MBHC_CTL_BCS, 0x02),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_STATUS, WCD939X_MBHC_NEW_FSM_STATUS, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_MUX_CTL, WCD939X_MBHC_NEW_CTL_2, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_MOISTURE_STATUS, WCD939X_MBHC_NEW_FSM_STATUS, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_GND, WCD939X_HPH_PA_CTL2, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_GND, WCD939X_HPH_PA_CTL2, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_DET_EN, WCD939X_HPH_L_TEST, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_DET_EN, WCD939X_HPH_R_TEST, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_STATUS, WCD939X_DIGITAL_INTR_STATUS_0, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_STATUS, WCD939X_DIGITAL_INTR_STATUS_0, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_EN, WCD939X_MBHC_NEW_CTL_1, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_COMPLETE, WCD939X_MBHC_NEW_FSM_STATUS, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_TIMEOUT, WCD939X_MBHC_NEW_FSM_STATUS, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_RESULT, WCD939X_MBHC_NEW_ADC_RESULT, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB2_VOUT, WCD939X_ANA_MICB2, 0x3F),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_MODE, WCD939X_MBHC_NEW_CTL_1, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_DETECTION_DONE, WCD939X_MBHC_NEW_CTL_1, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_ISRC_EN, WCD939X_ANA_MBHC_ZDET, 0x02),
};

static const struct regmap_irq wcd939x_irqs[WCD939X_NUM_IRQS] = {
	REGMAP_IRQ_REG(WCD939X_IRQ_MBHC_BUTTON_PRESS_DET, 0, 0x01),
	REGMAP_IRQ_REG(WCD939X_IRQ_MBHC_BUTTON_RELEASE_DET, 0, 0x02),
	REGMAP_IRQ_REG(WCD939X_IRQ_MBHC_ELECT_INS_REM_DET, 0, 0x04),
	REGMAP_IRQ_REG(WCD939X_IRQ_MBHC_ELECT_INS_REM_LEG_DET, 0, 0x08),
	REGMAP_IRQ_REG(WCD939X_IRQ_MBHC_SW_DET, 0, 0x10),
	REGMAP_IRQ_REG(WCD939X_IRQ_HPHR_OCP_INT, 0, 0x20),
	REGMAP_IRQ_REG(WCD939X_IRQ_HPHR_CNP_INT, 0, 0x40),
	REGMAP_IRQ_REG(WCD939X_IRQ_HPHL_OCP_INT, 0, 0x80),
	REGMAP_IRQ_REG(WCD939X_IRQ_HPHL_CNP_INT, 1, 0x01),
	REGMAP_IRQ_REG(WCD939X_IRQ_EAR_CNP_INT, 1, 0x02),
	REGMAP_IRQ_REG(WCD939X_IRQ_EAR_SCD_INT, 1, 0x04),
	REGMAP_IRQ_REG(WCD939X_IRQ_HPHL_PDM_WD_INT, 1, 0x20),
	REGMAP_IRQ_REG(WCD939X_IRQ_HPHR_PDM_WD_INT, 1, 0x40),
	REGMAP_IRQ_REG(WCD939X_IRQ_EAR_PDM_WD_INT, 1, 0x80),
	REGMAP_IRQ_REG(WCD939X_IRQ_MBHC_MOISTURE_INT, 2, 0x02),
	REGMAP_IRQ_REG(WCD939X_IRQ_HPHL_SURGE_DET_INT, 2, 0x04),
	REGMAP_IRQ_REG(WCD939X_IRQ_HPHR_SURGE_DET_INT, 2, 0x08),
};

static struct regmap_irq_chip wcd939x_regmap_irq_chip = {
	.name = "wcd939x",
	.irqs = wcd939x_irqs,
	.num_irqs = ARRAY_SIZE(wcd939x_irqs),
	.num_regs = 3,
	.status_base = WCD939X_DIGITAL_INTR_STATUS_0,
	.mask_base = WCD939X_DIGITAL_INTR_MASK_0,
	.ack_base = WCD939X_DIGITAL_INTR_CLEAR_0,
	.use_ack = 1,
	.runtime_pm = true,
	.irq_drv_data = NULL,
};

static int wcd939x_get_clk_rate(int mode)
{
	int rate;

	switch (mode) {
	case ADC_MODE_ULP2:
		rate = SWR_CLK_RATE_0P6MHZ;
		break;
	case ADC_MODE_ULP1:
		rate = SWR_CLK_RATE_1P2MHZ;
		break;
	case ADC_MODE_LP:
		rate = SWR_CLK_RATE_4P8MHZ;
		break;
	case ADC_MODE_NORMAL:
	case ADC_MODE_LO_HIF:
	case ADC_MODE_HIFI:
	case ADC_MODE_INVALID:
	default:
		rate = SWR_CLK_RATE_9P6MHZ;
		break;
	}

	return rate;
}

static int wcd939x_set_swr_clk_rate(struct snd_soc_component *component, int rate, int bank)
{
	u8 mask = (bank ? 0xF0 : 0x0F);
	u8 val = 0;

	switch (rate) {
	case SWR_CLK_RATE_0P6MHZ:
		val = 6;
		break;
	case SWR_CLK_RATE_1P2MHZ:
		val = 5;
		break;
	case SWR_CLK_RATE_2P4MHZ:
		val = 3;
		break;
	case SWR_CLK_RATE_4P8MHZ:
		val = 1;
		break;
	case SWR_CLK_RATE_9P6MHZ:
	default:
		val = 0;
		break;
	}

	snd_soc_component_write_field(component, WCD939X_DIGITAL_SWR_TX_CLK_RATE, mask, val);

	return 0;
}

static int wcd939x_io_init(struct snd_soc_component *component)
{
	snd_soc_component_write_field(component, WCD939X_ANA_BIAS,
				      WCD939X_BIAS_ANALOG_BIAS_EN, true);
	snd_soc_component_write_field(component, WCD939X_ANA_BIAS,
				      WCD939X_BIAS_PRECHRG_EN, true);

	/* 10 msec delay as per HW requirement */
	usleep_range(10000, 10010);
	snd_soc_component_write_field(component, WCD939X_ANA_BIAS,
				      WCD939X_BIAS_PRECHRG_EN, false);

	snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_RDAC_HD2_CTL_L,
				      WCD939X_RDAC_HD2_CTL_L_HD2_RES_DIV_CTL_L, 0x15);
	snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_RDAC_HD2_CTL_R,
				      WCD939X_RDAC_HD2_CTL_R_HD2_RES_DIV_CTL_R, 0x15);
	snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DMIC_CTL,
				      WCD939X_CDC_DMIC_CTL_CLK_SCALE_EN, true);

	snd_soc_component_write_field(component, WCD939X_TX_COM_NEW_INT_FE_ICTRL_STG2CASC_ULP,
				      WCD939X_FE_ICTRL_STG2CASC_ULP_ICTRL_SCBIAS_ULP0P6M, 1);
	snd_soc_component_write_field(component, WCD939X_TX_COM_NEW_INT_FE_ICTRL_STG2CASC_ULP,
				      WCD939X_FE_ICTRL_STG2CASC_ULP_VALUE, 4);

	snd_soc_component_write_field(component, WCD939X_TX_COM_NEW_INT_FE_ICTRL_STG2MAIN_ULP,
				      WCD939X_FE_ICTRL_STG2MAIN_ULP_VALUE, 8);

	snd_soc_component_write_field(component, WCD939X_MICB1_TEST_CTL_1,
				      WCD939X_TEST_CTL_1_NOISE_FILT_RES_VAL, 7);
	snd_soc_component_write_field(component, WCD939X_MICB2_TEST_CTL_1,
				      WCD939X_TEST_CTL_1_NOISE_FILT_RES_VAL, 7);
	snd_soc_component_write_field(component, WCD939X_MICB3_TEST_CTL_1,
				      WCD939X_TEST_CTL_1_NOISE_FILT_RES_VAL, 7);
	snd_soc_component_write_field(component, WCD939X_MICB4_TEST_CTL_1,
				      WCD939X_TEST_CTL_1_NOISE_FILT_RES_VAL, 7);
	snd_soc_component_write_field(component, WCD939X_TX_3_4_TEST_BLK_EN2,
				      WCD939X_TEST_BLK_EN2_TXFE2_MBHC_CLKRST_EN, false);

	snd_soc_component_write_field(component, WCD939X_HPH_SURGE_EN,
				      WCD939X_EN_EN_SURGE_PROTECTION_HPHL, false);
	snd_soc_component_write_field(component, WCD939X_HPH_SURGE_EN,
				      WCD939X_EN_EN_SURGE_PROTECTION_HPHR, false);

	snd_soc_component_write_field(component, WCD939X_HPH_OCP_CTL,
				      WCD939X_OCP_CTL_OCP_FSM_EN, true);
	snd_soc_component_write_field(component, WCD939X_HPH_OCP_CTL,
				      WCD939X_OCP_CTL_SCD_OP_EN, true);

	snd_soc_component_write(component, WCD939X_E_CFG0,
				WCD939X_CFG0_IDLE_STEREO |
				WCD939X_CFG0_AUTO_DISABLE_ANC);

	return 0;
}

static int wcd939x_sdw_connect_port(struct wcd939x_sdw_ch_info *ch_info,
				    struct sdw_port_config *port_config,
				    u8 enable)
{
	u8 ch_mask, port_num;

	port_num = ch_info->port_num;
	ch_mask = ch_info->ch_mask;

	port_config->num = port_num;

	if (enable)
		port_config->ch_mask |= ch_mask;
	else
		port_config->ch_mask &= ~ch_mask;

	return 0;
}

static int wcd939x_connect_port(struct wcd939x_sdw_priv *wcd, u8 port_num, u8 ch_id, u8 enable)
{
	return wcd939x_sdw_connect_port(&wcd->ch_info[ch_id],
					&wcd->port_config[port_num - 1],
					enable);
}

static int wcd939x_codec_enable_rxclk(struct snd_soc_dapm_widget *w,
				      struct snd_kcontrol *kcontrol,
				      int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD939X_ANA_RX_SUPPLIES,
					      WCD939X_RX_SUPPLIES_RX_BIAS_ENABLE, true);

		/* Analog path clock controls */
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_RX_CLK_EN, true);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_RX_DIV2_CLK_EN,
					      true);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_RX_DIV4_CLK_EN,
					      true);

		/* Digital path clock controls */
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
					      WCD939X_CDC_DIG_CLK_CTL_RXD0_CLK_EN, true);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
					      WCD939X_CDC_DIG_CLK_CTL_RXD1_CLK_EN, true);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
					      WCD939X_CDC_DIG_CLK_CTL_RXD2_CLK_EN, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD939X_ANA_RX_SUPPLIES,
					      WCD939X_RX_SUPPLIES_VNEG_EN, false);
		snd_soc_component_write_field(component, WCD939X_ANA_RX_SUPPLIES,
					      WCD939X_RX_SUPPLIES_VPOS_EN, false);

		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
					      WCD939X_CDC_DIG_CLK_CTL_RXD2_CLK_EN, false);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
					      WCD939X_CDC_DIG_CLK_CTL_RXD1_CLK_EN, false);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
					      WCD939X_CDC_DIG_CLK_CTL_RXD0_CLK_EN, false);

		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_RX_DIV4_CLK_EN,
					      false);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_RX_DIV2_CLK_EN,
					      false);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_RX_CLK_EN, false);

		snd_soc_component_write_field(component, WCD939X_ANA_RX_SUPPLIES,
					      WCD939X_RX_SUPPLIES_RX_BIAS_ENABLE, false);

		break;
	}

	return 0;
}

static int wcd939x_codec_hphl_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD939X_HPH_RDAC_CLK_CTL1,
					      WCD939X_RDAC_CLK_CTL1_OPAMP_CHOP_CLK_EN,
					      false);

		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_HPH_GAIN_CTL,
					      WCD939X_CDC_HPH_GAIN_CTL_HPHL_RX_EN, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_RDAC_HD2_CTL_L,
					      WCD939X_RDAC_HD2_CTL_L_HD2_RES_DIV_CTL_L, 0x1d);
		if (wcd939x->comp1_enable) {
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_COMP_CTL_0,
						      WCD939X_CDC_COMP_CTL_0_HPHL_COMP_EN,
						      true);
			 /* 5msec compander delay as per HW requirement */
			if (!wcd939x->comp2_enable ||
			    snd_soc_component_read_field(component,
							 WCD939X_DIGITAL_CDC_COMP_CTL_0,
							 WCD939X_CDC_COMP_CTL_0_HPHR_COMP_EN))
				usleep_range(5000, 5010);

			snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_TIMER1,
						      WCD939X_TIMER1_AUTOCHOP_TIMER_CTL_EN,
						      false);
		} else {
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_COMP_CTL_0,
						      WCD939X_CDC_COMP_CTL_0_HPHL_COMP_EN,
						      false);
			snd_soc_component_write_field(component, WCD939X_HPH_L_EN,
						      WCD939X_L_EN_GAIN_SOURCE_SEL, true);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_RDAC_HD2_CTL_L,
					      WCD939X_RDAC_HD2_CTL_L_HD2_RES_DIV_CTL_L, 1);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_HPH_GAIN_CTL,
					      WCD939X_CDC_HPH_GAIN_CTL_HPHL_RX_EN, false);
		break;
	}

	return 0;
}

static int wcd939x_codec_hphr_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s wname: %s event: %d\n", __func__,
		w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD939X_HPH_RDAC_CLK_CTL1,
					      WCD939X_RDAC_CLK_CTL1_OPAMP_CHOP_CLK_EN,
					      false);

		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_HPH_GAIN_CTL,
					      WCD939X_CDC_HPH_GAIN_CTL_HPHR_RX_EN, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_RDAC_HD2_CTL_R,
					      WCD939X_RDAC_HD2_CTL_R_HD2_RES_DIV_CTL_R, 0x1d);
		if (wcd939x->comp2_enable) {
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_COMP_CTL_0,
						      WCD939X_CDC_COMP_CTL_0_HPHR_COMP_EN,
						      true);
			/* 5msec compander delay as per HW requirement */
			if (!wcd939x->comp1_enable ||
			    snd_soc_component_read_field(component,
							 WCD939X_DIGITAL_CDC_COMP_CTL_0,
							 WCD939X_CDC_COMP_CTL_0_HPHL_COMP_EN))
				usleep_range(5000, 5010);
			snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_TIMER1,
						      WCD939X_TIMER1_AUTOCHOP_TIMER_CTL_EN,
						      false);
		} else {
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_COMP_CTL_0,
						      WCD939X_CDC_COMP_CTL_0_HPHR_COMP_EN,
						      false);
			snd_soc_component_write_field(component, WCD939X_HPH_R_EN,
						      WCD939X_R_EN_GAIN_SOURCE_SEL, true);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_RDAC_HD2_CTL_R,
					      WCD939X_RDAC_HD2_CTL_R_HD2_RES_DIV_CTL_R, 1);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_HPH_GAIN_CTL,
					      WCD939X_CDC_HPH_GAIN_CTL_HPHR_RX_EN, false);
		break;
	}

	return 0;
}

static int wcd939x_codec_ear_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol,
				       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_EAR_GAIN_CTL,
					      WCD939X_CDC_EAR_GAIN_CTL_EAR_EN, true);

		snd_soc_component_write_field(component, WCD939X_EAR_DAC_CON,
					      WCD939X_DAC_CON_DAC_SAMPLE_EDGE_SEL, false);

		/* 5 msec delay as per HW requirement */
		usleep_range(5000, 5010);
		wcd_clsh_ctrl_set_state(wcd939x->clsh_info, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_EAR, CLS_AB_HIFI);

		snd_soc_component_write_field(component, WCD939X_FLYBACK_VNEG_CTRL_4,
					      WCD939X_VNEG_CTRL_4_ILIM_SEL, 0xd);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD939X_EAR_DAC_CON,
					      WCD939X_DAC_CON_DAC_SAMPLE_EDGE_SEL, true);
		break;
	}

	return 0;
}

static int wcd939x_codec_enable_hphr_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd939x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (wcd939x->ldoh)
			snd_soc_component_write_field(component, WCD939X_LDOH_MODE,
						      WCD939X_MODE_LDOH_EN, true);

		wcd_clsh_ctrl_set_state(wcd939x->clsh_info, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHR, hph_mode);
		wcd_clsh_set_hph_mode(wcd939x->clsh_info, CLS_H_HIFI);

		if (hph_mode == CLS_H_LP || hph_mode == CLS_H_LOHIFI || hph_mode == CLS_H_ULP)
			snd_soc_component_write_field(component,
					WCD939X_HPH_REFBUFF_LP_CTL,
					WCD939X_REFBUFF_LP_CTL_PREREF_FILT_BYPASS, true);
		if (hph_mode == CLS_H_LOHIFI)
			snd_soc_component_write_field(component, WCD939X_ANA_HPH,
						       WCD939X_HPH_PWR_LEVEL, 0);

		snd_soc_component_write_field(component, WCD939X_FLYBACK_VNEG_CTRL_4,
					      WCD939X_VNEG_CTRL_4_ILIM_SEL, 0xd);
		snd_soc_component_write_field(component, WCD939X_ANA_HPH,
					      WCD939X_HPH_HPHR_REF_ENABLE, true);

		if (snd_soc_component_read_field(component, WCD939X_ANA_HPH,
						 WCD939X_HPH_HPHL_REF_ENABLE))
			usleep_range(2500, 2600); /* 2.5msec delay as per HW requirement */

		set_bit(HPH_PA_DELAY, &wcd939x->status_mask);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_PDM_WD_CTL1,
					      WCD939X_PDM_WD_CTL1_PDM_WD_EN, 3);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (test_bit(HPH_PA_DELAY, &wcd939x->status_mask)) {
			if (!wcd939x->comp2_enable)
				usleep_range(20000, 20100);
			else
				usleep_range(7000, 7100);

			if (hph_mode == CLS_H_LP || hph_mode == CLS_H_LOHIFI ||
			    hph_mode == CLS_H_ULP)
				snd_soc_component_write_field(component,
						WCD939X_HPH_REFBUFF_LP_CTL,
						WCD939X_REFBUFF_LP_CTL_PREREF_FILT_BYPASS,
						false);
			clear_bit(HPH_PA_DELAY, &wcd939x->status_mask);
		}
		snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_TIMER1,
					      WCD939X_TIMER1_AUTOCHOP_TIMER_CTL_EN, true);
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI ||
		    hph_mode == CLS_AB_LP || hph_mode == CLS_AB_LOHIFI)
			snd_soc_component_write_field(component, WCD939X_ANA_RX_SUPPLIES,
						      WCD939X_RX_SUPPLIES_REGULATOR_MODE,
						      true);

		enable_irq(wcd939x->hphr_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd939x->hphr_pdm_wd_int);
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (!wcd939x->comp2_enable)
			usleep_range(20000, 20100);
		else
			usleep_range(7000, 7100);

		snd_soc_component_write_field(component, WCD939X_ANA_HPH,
					      WCD939X_HPH_HPHR_ENABLE, false);

		wcd_mbhc_event_notify(wcd939x->wcd_mbhc,
				      WCD_EVENT_PRE_HPHR_PA_OFF);
		set_bit(HPH_PA_DELAY, &wcd939x->status_mask);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (test_bit(HPH_PA_DELAY, &wcd939x->status_mask)) {
			if (!wcd939x->comp2_enable)
				usleep_range(20000, 20100);
			else
				usleep_range(7000, 7100);
			clear_bit(HPH_PA_DELAY, &wcd939x->status_mask);
		}
		wcd_mbhc_event_notify(wcd939x->wcd_mbhc,
				      WCD_EVENT_POST_HPHR_PA_OFF);

		snd_soc_component_write_field(component, WCD939X_ANA_HPH,
					      WCD939X_HPH_HPHR_REF_ENABLE, false);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_PDM_WD_CTL1,
					      WCD939X_PDM_WD_CTL1_PDM_WD_EN, 0);

		wcd_clsh_ctrl_set_state(wcd939x->clsh_info, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_HPHR, hph_mode);
		if (wcd939x->ldoh)
			snd_soc_component_write_field(component, WCD939X_LDOH_MODE,
						      WCD939X_MODE_LDOH_EN, false);
		break;
	}

	return 0;
}

static int wcd939x_codec_enable_hphl_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd939x->hph_mode;

	dev_dbg(component->dev, "%s wname: %s event: %d\n", __func__,
		w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (wcd939x->ldoh)
			snd_soc_component_write_field(component, WCD939X_LDOH_MODE,
						      WCD939X_MODE_LDOH_EN, true);
		wcd_clsh_ctrl_set_state(wcd939x->clsh_info, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHL, hph_mode);
		wcd_clsh_set_hph_mode(wcd939x->clsh_info, CLS_H_HIFI);

		if (hph_mode == CLS_H_LP || hph_mode == CLS_H_LOHIFI || hph_mode == CLS_H_ULP)
			snd_soc_component_write_field(component,
						WCD939X_HPH_REFBUFF_LP_CTL,
						WCD939X_REFBUFF_LP_CTL_PREREF_FILT_BYPASS,
						true);
		if (hph_mode == CLS_H_LOHIFI)
			snd_soc_component_write_field(component, WCD939X_ANA_HPH,
						       WCD939X_HPH_PWR_LEVEL, 0);

		snd_soc_component_write_field(component, WCD939X_FLYBACK_VNEG_CTRL_4,
					      WCD939X_VNEG_CTRL_4_ILIM_SEL, 0xd);
		snd_soc_component_write_field(component, WCD939X_ANA_HPH,
					      WCD939X_HPH_HPHL_REF_ENABLE, true);

		if (snd_soc_component_read_field(component, WCD939X_ANA_HPH,
						 WCD939X_HPH_HPHR_REF_ENABLE))
			usleep_range(2500, 2600); /* 2.5msec delay as per HW requirement */

		set_bit(HPH_PA_DELAY, &wcd939x->status_mask);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_PDM_WD_CTL0,
					      WCD939X_PDM_WD_CTL0_PDM_WD_EN, 3);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (test_bit(HPH_PA_DELAY, &wcd939x->status_mask)) {
			if (!wcd939x->comp1_enable)
				usleep_range(20000, 20100);
			else
				usleep_range(7000, 7100);
			if (hph_mode == CLS_H_LP || hph_mode == CLS_H_LOHIFI ||
			    hph_mode == CLS_H_ULP)
				snd_soc_component_write_field(component,
						WCD939X_HPH_REFBUFF_LP_CTL,
						WCD939X_REFBUFF_LP_CTL_PREREF_FILT_BYPASS,
						false);
			clear_bit(HPH_PA_DELAY, &wcd939x->status_mask);
		}
		snd_soc_component_write_field(component, WCD939X_HPH_NEW_INT_TIMER1,
					      WCD939X_TIMER1_AUTOCHOP_TIMER_CTL_EN, true);
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI ||
		    hph_mode == CLS_AB_LP || hph_mode == CLS_AB_LOHIFI)
			snd_soc_component_write_field(component, WCD939X_ANA_RX_SUPPLIES,
						      WCD939X_RX_SUPPLIES_REGULATOR_MODE,
						      true);
		enable_irq(wcd939x->hphl_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd939x->hphl_pdm_wd_int);
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (!wcd939x->comp1_enable)
			usleep_range(20000, 20100);
		else
			usleep_range(7000, 7100);

		snd_soc_component_write_field(component, WCD939X_ANA_HPH,
					      WCD939X_HPH_HPHL_ENABLE, false);

		wcd_mbhc_event_notify(wcd939x->wcd_mbhc, WCD_EVENT_PRE_HPHL_PA_OFF);
		set_bit(HPH_PA_DELAY, &wcd939x->status_mask);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (test_bit(HPH_PA_DELAY, &wcd939x->status_mask)) {
			if (!wcd939x->comp1_enable)
				usleep_range(21000, 21100);
			else
				usleep_range(7000, 7100);
			clear_bit(HPH_PA_DELAY, &wcd939x->status_mask);
		}
		wcd_mbhc_event_notify(wcd939x->wcd_mbhc,
				      WCD_EVENT_POST_HPHL_PA_OFF);
		snd_soc_component_write_field(component, WCD939X_ANA_HPH,
					      WCD939X_HPH_HPHL_REF_ENABLE, false);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_PDM_WD_CTL0,
					      WCD939X_PDM_WD_CTL0_PDM_WD_EN, 0);
		wcd_clsh_ctrl_set_state(wcd939x->clsh_info, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_HPHL, hph_mode);
		if (wcd939x->ldoh)
			snd_soc_component_write_field(component, WCD939X_LDOH_MODE,
						      WCD939X_MODE_LDOH_EN, false);
		break;
	}

	return 0;
}

static int wcd939x_codec_enable_ear_pa(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable watchdog interrupt for HPHL */
		snd_soc_component_write_field(component, WCD939X_DIGITAL_PDM_WD_CTL0,
					      WCD939X_PDM_WD_CTL0_PDM_WD_EN, 3);
		/* For EAR, use CLASS_AB regulator mode */
		snd_soc_component_write_field(component, WCD939X_ANA_RX_SUPPLIES,
					      WCD939X_RX_SUPPLIES_REGULATOR_MODE, true);
		snd_soc_component_write_field(component, WCD939X_ANA_EAR_COMPANDER_CTL,
					      WCD939X_EAR_COMPANDER_CTL_GAIN_OVRD_REG, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* 6 msec delay as per HW requirement */
		usleep_range(6000, 6010);
		enable_irq(wcd939x->ear_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd939x->ear_pdm_wd_int);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD939X_ANA_EAR_COMPANDER_CTL,
					      WCD939X_EAR_COMPANDER_CTL_GAIN_OVRD_REG,
					      false);
		/* 7 msec delay as per HW requirement */
		usleep_range(7000, 7010);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_PDM_WD_CTL0,
					      WCD939X_PDM_WD_CTL0_PDM_WD_EN, 0);
		wcd_clsh_ctrl_set_state(wcd939x->clsh_info, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_EAR, CLS_AB_HIFI);
		break;
	}

	return 0;
}

/* TX Controls */

static int wcd939x_codec_enable_dmic(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol,
				     int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 dmic_clk_reg, dmic_clk_en_reg;
	u8 dmic_clk_en_mask;
	u8 dmic_ctl_mask;
	u8 dmic_clk_mask;

	switch (w->shift) {
	case 0:
	case 1:
		dmic_clk_reg = WCD939X_DIGITAL_CDC_DMIC_RATE_1_2;
		dmic_clk_en_reg = WCD939X_DIGITAL_CDC_DMIC1_CTL;
		dmic_clk_en_mask = WCD939X_CDC_DMIC1_CTL_DMIC_CLK_EN;
		dmic_clk_mask = WCD939X_CDC_DMIC_RATE_1_2_DMIC1_RATE;
		dmic_ctl_mask = WCD939X_CDC_AMIC_CTL_AMIC1_IN_SEL;
		break;
	case 2:
	case 3:
		dmic_clk_reg = WCD939X_DIGITAL_CDC_DMIC_RATE_1_2;
		dmic_clk_en_reg = WCD939X_DIGITAL_CDC_DMIC2_CTL;
		dmic_clk_en_mask = WCD939X_CDC_DMIC2_CTL_DMIC_CLK_EN;
		dmic_clk_mask = WCD939X_CDC_DMIC_RATE_1_2_DMIC2_RATE;
		dmic_ctl_mask = WCD939X_CDC_AMIC_CTL_AMIC3_IN_SEL;
		break;
	case 4:
	case 5:
		dmic_clk_reg = WCD939X_DIGITAL_CDC_DMIC_RATE_3_4;
		dmic_clk_en_reg = WCD939X_DIGITAL_CDC_DMIC3_CTL;
		dmic_clk_en_mask = WCD939X_CDC_DMIC3_CTL_DMIC_CLK_EN;
		dmic_clk_mask = WCD939X_CDC_DMIC_RATE_3_4_DMIC3_RATE;
		dmic_ctl_mask = WCD939X_CDC_AMIC_CTL_AMIC4_IN_SEL;
		break;
	case 6:
	case 7:
		dmic_clk_reg = WCD939X_DIGITAL_CDC_DMIC_RATE_3_4;
		dmic_clk_en_reg = WCD939X_DIGITAL_CDC_DMIC4_CTL;
		dmic_clk_en_mask = WCD939X_CDC_DMIC4_CTL_DMIC_CLK_EN;
		dmic_clk_mask = WCD939X_CDC_DMIC_RATE_3_4_DMIC4_RATE;
		dmic_ctl_mask = WCD939X_CDC_AMIC_CTL_AMIC5_IN_SEL;
		break;
	default:
		dev_err(component->dev, "%s: Invalid DMIC Selection\n", __func__);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_AMIC_CTL,
					      dmic_ctl_mask, false);
		/* 250us sleep as per HW requirement */
		usleep_range(250, 260);
		if (w->shift == 2)
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DMIC2_CTL,
						      WCD939X_CDC_DMIC2_CTL_DMIC_LEFT_EN,
						      true);
		/* Setting DMIC clock rate to 2.4MHz */
		snd_soc_component_write_field(component, dmic_clk_reg,
					      dmic_clk_mask, 3);
		snd_soc_component_write_field(component, dmic_clk_en_reg,
					      dmic_clk_en_mask, true);
		/* enable clock scaling */
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DMIC_CTL,
					      WCD939X_CDC_DMIC_CTL_CLK_SCALE_EN, true);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_DMIC_CTL,
					      WCD939X_CDC_DMIC_CTL_DMIC_DIV_BAK_EN, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_AMIC_CTL,
					      dmic_ctl_mask, 1);
		if (w->shift == 2)
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DMIC2_CTL,
						      WCD939X_CDC_DMIC2_CTL_DMIC_LEFT_EN,
						      false);
		snd_soc_component_write_field(component, dmic_clk_en_reg,
					      dmic_clk_en_mask, 0);
		break;
	}
	return 0;
}

static int wcd939x_tx_swr_ctrl(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	int bank;
	int rate;

	bank = wcd939x_swr_get_current_bank(wcd939x->sdw_priv[AIF1_CAP]->sdev);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (strnstr(w->name, "ADC", sizeof("ADC"))) {
			int mode = 0;

			if (test_bit(WCD_ADC1, &wcd939x->status_mask))
				mode |= tx_mode_bit[wcd939x->tx_mode[WCD_ADC1]];
			if (test_bit(WCD_ADC2, &wcd939x->status_mask))
				mode |= tx_mode_bit[wcd939x->tx_mode[WCD_ADC2]];
			if (test_bit(WCD_ADC3, &wcd939x->status_mask))
				mode |= tx_mode_bit[wcd939x->tx_mode[WCD_ADC3]];
			if (test_bit(WCD_ADC4, &wcd939x->status_mask))
				mode |= tx_mode_bit[wcd939x->tx_mode[WCD_ADC4]];

			if (mode)
				rate = wcd939x_get_clk_rate(ffs(mode) - 1);
			else
				rate = wcd939x_get_clk_rate(ADC_MODE_INVALID);
			wcd939x_set_swr_clk_rate(component, rate, bank);
			wcd939x_set_swr_clk_rate(component, rate, !bank);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (strnstr(w->name, "ADC", sizeof("ADC"))) {
			rate = wcd939x_get_clk_rate(ADC_MODE_INVALID);
			wcd939x_set_swr_clk_rate(component, rate, !bank);
			wcd939x_set_swr_clk_rate(component, rate, bank);
		}
		break;
	}

	return 0;
}

static int wcd939x_get_adc_mode(int val)
{
	int ret = 0;

	switch (val) {
	case ADC_MODE_INVALID:
		ret = ADC_MODE_VAL_NORMAL;
		break;
	case ADC_MODE_HIFI:
		ret = ADC_MODE_VAL_HIFI;
		break;
	case ADC_MODE_LO_HIF:
		ret = ADC_MODE_VAL_LO_HIF;
		break;
	case ADC_MODE_NORMAL:
		ret = ADC_MODE_VAL_NORMAL;
		break;
	case ADC_MODE_LP:
		ret = ADC_MODE_VAL_LP;
		break;
	case ADC_MODE_ULP1:
		ret = ADC_MODE_VAL_ULP1;
		break;
	case ADC_MODE_ULP2:
		ret = ADC_MODE_VAL_ULP2;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int wcd939x_codec_enable_adc(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_TX_CLK_EN, true);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_TX_DIV2_CLK_EN,
					      true);
		set_bit(w->shift, &wcd939x->status_mask);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_TX_DIV2_CLK_EN,
					      false);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD939X_CDC_ANA_CLK_CTL_ANA_TX_CLK_EN,
					      false);
		clear_bit(w->shift, &wcd939x->status_mask);
		break;
	}

	return 0;
}

static void wcd939x_tx_channel_config(struct snd_soc_component *component,
				      int channel, bool init)
{
	int reg, mask;

	switch (channel) {
	case 0:
		reg = WCD939X_ANA_TX_CH2;
		mask = WCD939X_TX_CH2_HPF1_INIT;
		break;
	case 1:
		reg = WCD939X_ANA_TX_CH2;
		mask = WCD939X_TX_CH2_HPF2_INIT;
		break;
	case 2:
		reg = WCD939X_ANA_TX_CH4;
		mask = WCD939X_TX_CH4_HPF3_INIT;
		break;
	case 3:
		reg = WCD939X_ANA_TX_CH4;
		mask = WCD939X_TX_CH4_HPF4_INIT;
		break;
	default:
		return;
	}

	snd_soc_component_write_field(component, reg, mask, init);
}

static int wcd939x_adc_enable_req(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	int mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_REQ_CTL,
					      WCD939X_CDC_REQ_CTL_FS_RATE_4P8, true);
		snd_soc_component_write_field(component, WCD939X_DIGITAL_CDC_REQ_CTL,
					      WCD939X_CDC_REQ_CTL_NO_NOTCH, false);

		wcd939x_tx_channel_config(component, w->shift, true);
		mode = wcd939x_get_adc_mode(wcd939x->tx_mode[w->shift]);
		if (mode < 0) {
			dev_info(component->dev, "Invalid ADC mode\n");
			return -EINVAL;
		}

		switch (w->shift) {
		case 0:
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_TX_ANA_MODE_0_1,
						      WCD939X_CDC_TX_ANA_MODE_0_1_TXD0_MODE,
						      mode);
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						      WCD939X_CDC_DIG_CLK_CTL_TXD0_CLK_EN,
						      true);
			break;
		case 1:
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_TX_ANA_MODE_0_1,
						      WCD939X_CDC_TX_ANA_MODE_0_1_TXD1_MODE,
						      mode);
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						      WCD939X_CDC_DIG_CLK_CTL_TXD1_CLK_EN,
						      true);
			break;
		case 2:
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_TX_ANA_MODE_2_3,
						      WCD939X_CDC_TX_ANA_MODE_2_3_TXD2_MODE,
						      mode);
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						      WCD939X_CDC_DIG_CLK_CTL_TXD2_CLK_EN,
						      true);
			break;
		case 3:
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_TX_ANA_MODE_2_3,
						      WCD939X_CDC_TX_ANA_MODE_2_3_TXD3_MODE,
						      mode);
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						      WCD939X_CDC_DIG_CLK_CTL_TXD3_CLK_EN,
						      true);
			break;
		default:
			break;
		}

		wcd939x_tx_channel_config(component, w->shift, false);
		break;
	case SND_SOC_DAPM_POST_PMD:
		switch (w->shift) {
		case 0:
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_TX_ANA_MODE_0_1,
						      WCD939X_CDC_TX_ANA_MODE_0_1_TXD0_MODE,
						      false);
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						      WCD939X_CDC_DIG_CLK_CTL_TXD0_CLK_EN,
						      false);
			break;
		case 1:
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_TX_ANA_MODE_0_1,
						      WCD939X_CDC_TX_ANA_MODE_0_1_TXD1_MODE,
						      false);
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						      WCD939X_CDC_DIG_CLK_CTL_TXD1_CLK_EN,
						      false);
			break;
		case 2:
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_TX_ANA_MODE_2_3,
						      WCD939X_CDC_TX_ANA_MODE_2_3_TXD2_MODE,
						      false);
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						      WCD939X_CDC_DIG_CLK_CTL_TXD2_CLK_EN,
						      false);
			break;
		case 3:
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_TX_ANA_MODE_2_3,
						      WCD939X_CDC_TX_ANA_MODE_2_3_TXD3_MODE,
						      false);
			snd_soc_component_write_field(component,
						      WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						      WCD939X_CDC_DIG_CLK_CTL_TXD3_CLK_EN,
						      false);
			break;
		default:
			break;
		}
		break;
	}

	return 0;
}

static int wcd939x_micbias_control(struct snd_soc_component *component,
				   int micb_num, int req, bool is_dapm)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	int micb_index = micb_num - 1;
	u16 micb_field;
	u16 micb_reg;

	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = WCD939X_ANA_MICB1;
		micb_field = WCD939X_MICB1_ENABLE;
		break;
	case MIC_BIAS_2:
		micb_reg = WCD939X_ANA_MICB2;
		micb_field = WCD939X_MICB2_ENABLE;
		break;
	case MIC_BIAS_3:
		micb_reg = WCD939X_ANA_MICB3;
		micb_field = WCD939X_MICB3_ENABLE;
		break;
	case MIC_BIAS_4:
		micb_reg = WCD939X_ANA_MICB4;
		micb_field = WCD939X_MICB4_ENABLE;
		break;
	default:
		dev_err(component->dev, "%s: Invalid micbias number: %d\n",
			__func__, micb_num);
		return -EINVAL;
	}

	switch (req) {
	case MICB_PULLUP_ENABLE:
		wcd939x->pullup_ref[micb_index]++;
		if (wcd939x->pullup_ref[micb_index] == 1 &&
		    wcd939x->micb_ref[micb_index] == 0)
			snd_soc_component_write_field(component, micb_reg,
						      micb_field, MICB_BIAS_PULL_UP);
		break;
	case MICB_PULLUP_DISABLE:
		if (wcd939x->pullup_ref[micb_index] > 0)
			wcd939x->pullup_ref[micb_index]--;
		if (wcd939x->pullup_ref[micb_index] == 0 &&
		    wcd939x->micb_ref[micb_index] == 0)
			snd_soc_component_write_field(component, micb_reg,
						      micb_field, MICB_BIAS_DISABLE);
		break;
	case MICB_ENABLE:
		wcd939x->micb_ref[micb_index]++;
		if (wcd939x->micb_ref[micb_index] == 1) {
			snd_soc_component_write_field(component,
						WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						WCD939X_CDC_DIG_CLK_CTL_TXD3_CLK_EN, true);
			snd_soc_component_write_field(component,
						WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						WCD939X_CDC_DIG_CLK_CTL_TXD2_CLK_EN, true);
			snd_soc_component_write_field(component,
						WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						WCD939X_CDC_DIG_CLK_CTL_TXD1_CLK_EN, true);
			snd_soc_component_write_field(component,
						WCD939X_DIGITAL_CDC_DIG_CLK_CTL,
						WCD939X_CDC_DIG_CLK_CTL_TXD0_CLK_EN, true);
			snd_soc_component_write_field(component,
						WCD939X_DIGITAL_CDC_ANA_CLK_CTL,
						WCD939X_CDC_ANA_CLK_CTL_ANA_TX_DIV2_CLK_EN,
						true);
			snd_soc_component_write_field(component,
						WCD939X_DIGITAL_CDC_ANA_TX_CLK_CTL,
						WCD939X_CDC_ANA_TX_CLK_CTL_ANA_TXSCBIAS_CLK_EN,
						true);
			snd_soc_component_write_field(component,
						WCD939X_MICB1_TEST_CTL_2,
						WCD939X_TEST_CTL_2_IBIAS_LDO_DRIVER, true);
			snd_soc_component_write_field(component,
						WCD939X_MICB2_TEST_CTL_2,
						WCD939X_TEST_CTL_2_IBIAS_LDO_DRIVER, true);
			snd_soc_component_write_field(component,
						WCD939X_MICB3_TEST_CTL_2,
						WCD939X_TEST_CTL_2_IBIAS_LDO_DRIVER, true);
			snd_soc_component_write_field(component,
						WCD939X_MICB4_TEST_CTL_2,
						WCD939X_TEST_CTL_2_IBIAS_LDO_DRIVER, true);
			snd_soc_component_write_field(component, micb_reg, micb_field,
						      MICB_BIAS_ENABLE);
			if (micb_num == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd939x->wcd_mbhc,
						      WCD_EVENT_POST_MICBIAS_2_ON);
		}
		if (micb_num == MIC_BIAS_2 && is_dapm)
			wcd_mbhc_event_notify(wcd939x->wcd_mbhc,
					      WCD_EVENT_POST_DAPM_MICBIAS_2_ON);
		break;
	case MICB_DISABLE:
		if (wcd939x->micb_ref[micb_index] > 0)
			wcd939x->micb_ref[micb_index]--;

		if (wcd939x->micb_ref[micb_index] == 0 &&
		    wcd939x->pullup_ref[micb_index] > 0)
			snd_soc_component_write_field(component, micb_reg,
						      micb_field, MICB_BIAS_PULL_UP);
		else if (wcd939x->micb_ref[micb_index] == 0 &&
			 wcd939x->pullup_ref[micb_index] == 0) {
			if (micb_num  == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd939x->wcd_mbhc,
						      WCD_EVENT_PRE_MICBIAS_2_OFF);

			snd_soc_component_write_field(component, micb_reg,
						      micb_field, MICB_BIAS_DISABLE);
			if (micb_num  == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd939x->wcd_mbhc,
						      WCD_EVENT_POST_MICBIAS_2_OFF);
		}
		if (is_dapm && micb_num  == MIC_BIAS_2)
			wcd_mbhc_event_notify(wcd939x->wcd_mbhc,
					      WCD_EVENT_POST_DAPM_MICBIAS_2_OFF);
		break;
	}

	return 0;
}

static int wcd939x_codec_enable_micbias(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd939x_micbias_control(component, micb_num, MICB_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* 1 msec delay as per HW requirement */
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd939x_micbias_control(component, micb_num, MICB_DISABLE, true);
		break;
	}

	return 0;
}

static int wcd939x_codec_enable_micbias_pullup(struct snd_soc_dapm_widget *w,
					       struct snd_kcontrol *kcontrol,
					       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd939x_micbias_control(component, micb_num,
					MICB_PULLUP_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* 1 msec delay as per HW requirement */
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd939x_micbias_control(component, micb_num,
					MICB_PULLUP_DISABLE, true);
		break;
	}

	return 0;
}

static int wcd939x_tx_mode_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int path = e->shift_l;

	ucontrol->value.enumerated.item[0] = wcd939x->tx_mode[path];

	return 0;
}

static int wcd939x_tx_mode_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int path = e->shift_l;

	if (wcd939x->tx_mode[path] == ucontrol->value.enumerated.item[0])
		return 0;

	wcd939x->tx_mode[path] = ucontrol->value.enumerated.item[0];

	return 1;
}

/* RX Controls */

static int wcd939x_rx_hph_mode_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd939x->hph_mode;

	return 0;
}

static int wcd939x_rx_hph_mode_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	u32 mode_val;

	mode_val = ucontrol->value.enumerated.item[0];

	if (mode_val == wcd939x->hph_mode)
		return 0;

	if (wcd939x->variant == WCD9390) {
		switch (mode_val) {
		case CLS_H_NORMAL:
		case CLS_H_LP:
		case CLS_AB:
		case CLS_H_LOHIFI:
		case CLS_H_ULP:
		case CLS_AB_LP:
		case CLS_AB_LOHIFI:
			wcd939x->hph_mode = mode_val;
			return 1;
		}
	} else {
		switch (mode_val) {
		case CLS_H_NORMAL:
		case CLS_H_HIFI:
		case CLS_H_LP:
		case CLS_AB:
		case CLS_H_LOHIFI:
		case CLS_H_ULP:
		case CLS_AB_HIFI:
		case CLS_AB_LP:
		case CLS_AB_LOHIFI:
			wcd939x->hph_mode = mode_val;
			return 1;
		}
	}

	dev_dbg(component->dev, "%s: Invalid HPH Mode\n", __func__);
	return -EINVAL;
}

static int wcd939x_get_compander(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)(kcontrol->private_value);
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	if (mc->shift)
		ucontrol->value.integer.value[0] = wcd939x->comp2_enable ? 1 : 0;
	else
		ucontrol->value.integer.value[0] = wcd939x->comp1_enable ? 1 : 0;

	return 0;
}

static int wcd939x_set_compander(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)(kcontrol->private_value);
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	struct wcd939x_sdw_priv *wcd = wcd939x->sdw_priv[AIF1_PB];
	bool value = !!ucontrol->value.integer.value[0];
	int portidx = wcd->ch_info[mc->reg].port_num;

	if (mc->shift)
		wcd939x->comp2_enable = value;
	else
		wcd939x->comp1_enable = value;

	if (value)
		wcd939x_connect_port(wcd, portidx, mc->reg, true);
	else
		wcd939x_connect_port(wcd, portidx, mc->reg, false);

	return 1;
}

static int wcd939x_ldoh_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd939x->ldoh ? 1 : 0;

	return 0;
}

static int wcd939x_ldoh_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	if (wcd939x->ldoh == !!ucontrol->value.integer.value[0])
		return 0;

	wcd939x->ldoh = !!ucontrol->value.integer.value[0];

	return 1;
}

static const char * const tx_mode_mux_text_wcd9390[] = {
	"ADC_INVALID", "ADC_HIFI", "ADC_LO_HIF", "ADC_NORMAL", "ADC_LP",
};

static const struct soc_enum tx0_mode_mux_enum_wcd9390 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(tx_mode_mux_text_wcd9390),
			tx_mode_mux_text_wcd9390);

static const struct soc_enum tx1_mode_mux_enum_wcd9390 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 1, ARRAY_SIZE(tx_mode_mux_text_wcd9390),
			tx_mode_mux_text_wcd9390);

static const struct soc_enum tx2_mode_mux_enum_wcd9390 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 2, ARRAY_SIZE(tx_mode_mux_text_wcd9390),
			tx_mode_mux_text_wcd9390);

static const struct soc_enum tx3_mode_mux_enum_wcd9390 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 3, ARRAY_SIZE(tx_mode_mux_text_wcd9390),
			tx_mode_mux_text_wcd9390);

static const char * const tx_mode_mux_text[] = {
	"ADC_INVALID", "ADC_HIFI", "ADC_LO_HIF", "ADC_NORMAL", "ADC_LP",
	"ADC_ULP1", "ADC_ULP2",
};

static const struct soc_enum tx0_mode_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(tx_mode_mux_text),
			tx_mode_mux_text);

static const struct soc_enum tx1_mode_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 1, ARRAY_SIZE(tx_mode_mux_text),
			tx_mode_mux_text);

static const struct soc_enum tx2_mode_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 2, ARRAY_SIZE(tx_mode_mux_text),
			tx_mode_mux_text);

static const struct soc_enum tx3_mode_mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 3, ARRAY_SIZE(tx_mode_mux_text),
			tx_mode_mux_text);

static const char * const rx_hph_mode_mux_text_wcd9390[] = {
	"CLS_H_NORMAL", "CLS_H_INVALID_1", "CLS_H_LP", "CLS_AB",
	"CLS_H_LOHIFI", "CLS_H_ULP", "CLS_H_INVALID_2", "CLS_AB_LP",
	"CLS_AB_LOHIFI",
};

static const struct soc_enum rx_hph_mode_mux_enum_wcd9390 =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_hph_mode_mux_text_wcd9390),
			    rx_hph_mode_mux_text_wcd9390);

static const char * const rx_hph_mode_mux_text[] = {
	"CLS_H_NORMAL", "CLS_H_HIFI", "CLS_H_LP", "CLS_AB", "CLS_H_LOHIFI",
	"CLS_H_ULP", "CLS_AB_HIFI", "CLS_AB_LP", "CLS_AB_LOHIFI",
};

static const struct soc_enum rx_hph_mode_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_hph_mode_mux_text),
			    rx_hph_mode_mux_text);

static const struct snd_kcontrol_new wcd9390_snd_controls[] = {
	SOC_SINGLE_TLV("EAR_PA Volume", WCD939X_ANA_EAR_COMPANDER_CTL,
		       2, 0x10, 0, ear_pa_gain),

	SOC_ENUM_EXT("RX HPH Mode", rx_hph_mode_mux_enum_wcd9390,
		     wcd939x_rx_hph_mode_get, wcd939x_rx_hph_mode_put),

	SOC_ENUM_EXT("TX0 MODE", tx0_mode_mux_enum_wcd9390,
		     wcd939x_tx_mode_get, wcd939x_tx_mode_put),
	SOC_ENUM_EXT("TX1 MODE", tx1_mode_mux_enum_wcd9390,
		     wcd939x_tx_mode_get, wcd939x_tx_mode_put),
	SOC_ENUM_EXT("TX2 MODE", tx2_mode_mux_enum_wcd9390,
		     wcd939x_tx_mode_get, wcd939x_tx_mode_put),
	SOC_ENUM_EXT("TX3 MODE", tx3_mode_mux_enum_wcd9390,
		     wcd939x_tx_mode_get, wcd939x_tx_mode_put),
};

static const struct snd_kcontrol_new wcd9395_snd_controls[] = {
	SOC_ENUM_EXT("RX HPH Mode", rx_hph_mode_mux_enum,
		     wcd939x_rx_hph_mode_get, wcd939x_rx_hph_mode_put),

	SOC_ENUM_EXT("TX0 MODE", tx0_mode_mux_enum,
		     wcd939x_tx_mode_get, wcd939x_tx_mode_put),
	SOC_ENUM_EXT("TX1 MODE", tx1_mode_mux_enum,
		     wcd939x_tx_mode_get, wcd939x_tx_mode_put),
	SOC_ENUM_EXT("TX2 MODE", tx2_mode_mux_enum,
		     wcd939x_tx_mode_get, wcd939x_tx_mode_put),
	SOC_ENUM_EXT("TX3 MODE", tx3_mode_mux_enum,
		     wcd939x_tx_mode_get, wcd939x_tx_mode_put),
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

static const struct snd_kcontrol_new adc4_switch[] = {
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

static const struct snd_kcontrol_new dmic7_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new dmic8_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new ear_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new hphl_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new hphr_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const char * const adc1_mux_text[] = {
	"CH1_AMIC_DISABLE", "CH1_AMIC1", "CH1_AMIC2", "CH1_AMIC3", "CH1_AMIC4", "CH1_AMIC5"
};

static const struct soc_enum adc1_enum =
	SOC_ENUM_SINGLE(WCD939X_TX_NEW_CH12_MUX, 0,
			ARRAY_SIZE(adc1_mux_text), adc1_mux_text);

static const struct snd_kcontrol_new tx_adc1_mux =
	SOC_DAPM_ENUM("ADC1 MUX Mux", adc1_enum);

static const char * const adc2_mux_text[] = {
	"CH2_AMIC_DISABLE", "CH2_AMIC1", "CH2_AMIC2", "CH2_AMIC3", "CH2_AMIC4", "CH2_AMIC5"
};

static const struct soc_enum adc2_enum =
	SOC_ENUM_SINGLE(WCD939X_TX_NEW_CH12_MUX, 3,
			ARRAY_SIZE(adc2_mux_text), adc2_mux_text);

static const struct snd_kcontrol_new tx_adc2_mux =
	SOC_DAPM_ENUM("ADC2 MUX Mux", adc2_enum);

static const char * const adc3_mux_text[] = {
	"CH3_AMIC_DISABLE", "CH3_AMIC1", "CH3_AMIC3", "CH3_AMIC4", "CH3_AMIC5"
};

static const struct soc_enum adc3_enum =
	SOC_ENUM_SINGLE(WCD939X_TX_NEW_CH34_MUX, 0,
			ARRAY_SIZE(adc3_mux_text), adc3_mux_text);

static const struct snd_kcontrol_new tx_adc3_mux =
	SOC_DAPM_ENUM("ADC3 MUX Mux", adc3_enum);

static const char * const adc4_mux_text[] = {
	"CH4_AMIC_DISABLE", "CH4_AMIC1", "CH4_AMIC3", "CH4_AMIC4", "CH4_AMIC5"
};

static const struct soc_enum adc4_enum =
	SOC_ENUM_SINGLE(WCD939X_TX_NEW_CH34_MUX, 3,
			ARRAY_SIZE(adc4_mux_text), adc4_mux_text);

static const struct snd_kcontrol_new tx_adc4_mux =
	SOC_DAPM_ENUM("ADC4 MUX Mux", adc4_enum);

static const char * const rdac3_mux_text[] = {
	"RX3", "RX1"
};

static const struct soc_enum rdac3_enum =
	SOC_ENUM_SINGLE(WCD939X_DIGITAL_CDC_EAR_PATH_CTL, 0,
			ARRAY_SIZE(rdac3_mux_text), rdac3_mux_text);

static const struct snd_kcontrol_new rx_rdac3_mux =
	SOC_DAPM_ENUM("RDAC3_MUX Mux", rdac3_enum);

static int wcd939x_get_swr_port(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(comp);
	struct wcd939x_sdw_priv *wcd = wcd939x->sdw_priv[mixer->shift];
	unsigned int portidx = wcd->ch_info[mixer->reg].port_num;

	ucontrol->value.integer.value[0] = wcd->port_enable[portidx] ? 1 : 0;

	return 0;
}

static const char *version_to_str(u32 version)
{
	switch (version) {
	case WCD939X_VERSION_1_0:
		return __stringify(WCD939X_1_0);
	case WCD939X_VERSION_1_1:
		return __stringify(WCD939X_1_1);
	case WCD939X_VERSION_2_0:
		return __stringify(WCD939X_2_0);
	}
	return NULL;
}

static int wcd939x_set_swr_port(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(comp);
	struct wcd939x_sdw_priv *wcd = wcd939x->sdw_priv[mixer->shift];
	unsigned int portidx = wcd->ch_info[mixer->reg].port_num;

	wcd->port_enable[portidx] = !!ucontrol->value.integer.value[0];

	wcd939x_connect_port(wcd, portidx, mixer->reg, wcd->port_enable[portidx]);

	return 1;
}

/* MBHC Related */

static void wcd939x_mbhc_clk_setup(struct snd_soc_component *component,
				   bool enable)
{
	snd_soc_component_write_field(component, WCD939X_MBHC_NEW_CTL_1,
				      WCD939X_CTL_1_RCO_EN, enable);
}

static void wcd939x_mbhc_mbhc_bias_control(struct snd_soc_component *component,
					   bool enable)
{
	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ELECT,
				      WCD939X_MBHC_ELECT_BIAS_EN, enable);
}

static void wcd939x_mbhc_program_btn_thr(struct snd_soc_component *component,
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
		vth = (btn_high[i] * 2) / 25;
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_BTN0 + i,
					      WCD939X_MBHC_BTN0_VTH, vth);
		dev_dbg(component->dev, "%s: btn_high[%d]: %d, vth: %d\n",
			__func__, i, btn_high[i], vth);
	}
}

static bool wcd939x_mbhc_micb_en_status(struct snd_soc_component *component, int micb_num)
{

	if (micb_num == MIC_BIAS_2) {
		u8 val;

		val = FIELD_GET(WCD939X_MICB2_ENABLE,
				snd_soc_component_read(component, WCD939X_ANA_MICB2));
		if (val == MICB_BIAS_ENABLE)
			return true;
	}

	return false;
}

static void wcd939x_mbhc_hph_l_pull_up_control(struct snd_soc_component *component,
					       int pull_up_cur)
{
	/* Default pull up current to 2uA */
	if (pull_up_cur > HS_PULLUP_I_OFF ||
	    pull_up_cur < HS_PULLUP_I_3P0_UA ||
	    pull_up_cur == HS_PULLUP_I_DEFAULT)
		pull_up_cur = HS_PULLUP_I_2P0_UA;

	dev_dbg(component->dev, "%s: HS pull up current:%d\n",
		__func__, pull_up_cur);

	snd_soc_component_write_field(component, WCD939X_MBHC_NEW_INT_MECH_DET_CURRENT,
				      WCD939X_MECH_DET_CURRENT_HSDET_PULLUP_CTL, pull_up_cur);
}

static int wcd939x_mbhc_request_micbias(struct snd_soc_component *component,
					int micb_num, int req)
{
	return wcd939x_micbias_control(component, micb_num, req, false);
}

static void wcd939x_mbhc_micb_ramp_control(struct snd_soc_component *component,
					   bool enable)
{
	if (enable) {
		snd_soc_component_write_field(component, WCD939X_ANA_MICB2_RAMP,
					      WCD939X_MICB2_RAMP_SHIFT_CTL, 3);
		snd_soc_component_write_field(component, WCD939X_ANA_MICB2_RAMP,
					      WCD939X_MICB2_RAMP_RAMP_ENABLE, true);
	} else {
		snd_soc_component_write_field(component, WCD939X_ANA_MICB2_RAMP,
					      WCD939X_MICB2_RAMP_RAMP_ENABLE, false);
		snd_soc_component_write_field(component, WCD939X_ANA_MICB2_RAMP,
					      WCD939X_MICB2_RAMP_SHIFT_CTL, 0);
	}
}

static int wcd939x_get_micb_vout_ctl_val(u32 micb_mv)
{
	/* min micbias voltage is 1V and maximum is 2.85V */
	if (micb_mv < 1000 || micb_mv > 2850) {
		pr_err("%s: unsupported micbias voltage\n", __func__);
		return -EINVAL;
	}

	return (micb_mv - 1000) / 50;
}

static int wcd939x_mbhc_micb_adjust_voltage(struct snd_soc_component *component,
					    int req_volt, int micb_num)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	unsigned int micb_en_field, micb_vout_ctl_field;
	unsigned int micb_reg, cur_vout_ctl, micb_en;
	int req_vout_ctl;
	int ret = 0;

	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = WCD939X_ANA_MICB1;
		micb_en_field = WCD939X_MICB1_ENABLE;
		micb_vout_ctl_field = WCD939X_MICB1_VOUT_CTL;
		break;
	case MIC_BIAS_2:
		micb_reg = WCD939X_ANA_MICB2;
		micb_en_field = WCD939X_MICB2_ENABLE;
		micb_vout_ctl_field = WCD939X_MICB2_VOUT_CTL;
		break;
	case MIC_BIAS_3:
		micb_reg = WCD939X_ANA_MICB3;
		micb_en_field = WCD939X_MICB3_ENABLE;
		micb_vout_ctl_field = WCD939X_MICB1_VOUT_CTL;
		break;
	case MIC_BIAS_4:
		micb_reg = WCD939X_ANA_MICB4;
		micb_en_field = WCD939X_MICB4_ENABLE;
		micb_vout_ctl_field = WCD939X_MICB2_VOUT_CTL;
		break;
	default:
		return -EINVAL;
	}
	mutex_lock(&wcd939x->micb_lock);

	/*
	 * If requested micbias voltage is same as current micbias
	 * voltage, then just return. Otherwise, adjust voltage as
	 * per requested value. If micbias is already enabled, then
	 * to avoid slow micbias ramp-up or down enable pull-up
	 * momentarily, change the micbias value and then re-enable
	 * micbias.
	 */
	micb_en = snd_soc_component_read_field(component, micb_reg,
					       micb_en_field);
	cur_vout_ctl = snd_soc_component_read_field(component, micb_reg,
						    micb_vout_ctl_field);

	req_vout_ctl = wcd939x_get_micb_vout_ctl_val(req_volt);
	if (req_vout_ctl < 0) {
		ret = req_vout_ctl;
		goto exit;
	}

	if (cur_vout_ctl == req_vout_ctl) {
		ret = 0;
		goto exit;
	}

	dev_dbg(component->dev, "%s: micb_num: %d, cur_mv: %d, req_mv: %d, micb_en: %d\n",
		__func__, micb_num, WCD_VOUT_CTL_TO_MICB(cur_vout_ctl),
		 req_volt, micb_en);

	if (micb_en == MICB_BIAS_ENABLE)
		snd_soc_component_write_field(component, micb_reg,
					      micb_en_field, MICB_BIAS_PULL_DOWN);

	snd_soc_component_write_field(component, micb_reg,
				      micb_vout_ctl_field, req_vout_ctl);

	if (micb_en == MICB_BIAS_ENABLE) {
		snd_soc_component_write_field(component, micb_reg,
					      micb_en_field, MICB_BIAS_ENABLE);
		/*
		 * Add 2ms delay as per HW requirement after enabling
		 * micbias
		 */
		usleep_range(2000, 2100);
	}

exit:
	mutex_unlock(&wcd939x->micb_lock);
	return ret;
}

static int wcd939x_mbhc_micb_ctrl_threshold_mic(struct snd_soc_component *component,
						int micb_num, bool req_en)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	int micb_mv;

	if (micb_num != MIC_BIAS_2)
		return -EINVAL;
	/*
	 * If device tree micbias level is already above the minimum
	 * voltage needed to detect threshold microphone, then do
	 * not change the micbias, just return.
	 */
	if (wcd939x->micb2_mv >= WCD_MBHC_THR_HS_MICB_MV)
		return 0;

	micb_mv = req_en ? WCD_MBHC_THR_HS_MICB_MV : wcd939x->micb2_mv;

	return wcd939x_mbhc_micb_adjust_voltage(component, micb_mv, MIC_BIAS_2);
}

/* Selected by WCD939X_MBHC_GET_C1() */
static const s16 wcd939x_wcd_mbhc_d1_a[4] = {
	0, 30, 30, 6
};

/* Selected by zdet_param.noff */
static const int wcd939x_mbhc_mincode_param[] = {
	3277, 1639, 820, 410, 205, 103, 52, 26
};

static const struct zdet_param wcd939x_mbhc_zdet_param = {
	.ldo_ctl = 4,
	.noff = 0,
	.nshift = 6,
	.btn5 = 0x18,
	.btn6 = 0x60,
	.btn7 = 0x78,
};

static void wcd939x_mbhc_get_result_params(struct snd_soc_component *component,
					   int32_t *zdet)
{
	const struct zdet_param *zdet_param = &wcd939x_mbhc_zdet_param;
	s32 x1, d1, denom;
	int val;
	s16 c1;
	int i;

	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ZDET,
				      WCD939X_MBHC_ZDET_ZDET_CHG_EN, true);
	for (i = 0; i < WCD939X_ZDET_NUM_MEASUREMENTS; i++) {
		val = snd_soc_component_read_field(component, WCD939X_ANA_MBHC_RESULT_2,
						   WCD939X_MBHC_RESULT_2_Z_RESULT_MSB);
		if (val & BIT(7))
			break;
	}
	val = val << 8;
	val |= snd_soc_component_read_field(component, WCD939X_ANA_MBHC_RESULT_1,
					    WCD939X_MBHC_RESULT_1_Z_RESULT_LSB);
	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ZDET,
				      WCD939X_MBHC_ZDET_ZDET_CHG_EN, false);
	x1 = WCD939X_MBHC_GET_X1(val);
	c1 = WCD939X_MBHC_GET_C1(val);

	/* If ramp is not complete, give additional 5ms */
	if (c1 < 2 && x1)
		mdelay(5);

	if (!c1 || !x1) {
		dev_dbg(component->dev,
			"%s: Impedance detect ramp error, c1=%d, x1=0x%x\n",
			__func__, c1, x1);
		goto ramp_down;
	}

	d1 = wcd939x_wcd_mbhc_d1_a[c1];
	denom = (x1 * d1) - (1 << (14 - zdet_param->noff));
	if (denom > 0)
		*zdet = (WCD939X_ANA_MBHC_ZDET_CONST * 1000) / denom;
	else if (x1 <  wcd939x_mbhc_mincode_param[zdet_param->noff])
		*zdet = WCD939X_ZDET_FLOATING_IMPEDANCE;

	dev_dbg(component->dev, "%s: d1=%d, c1=%d, x1=0x%x, z_val=%d(milliOhm)\n",
		__func__, d1, c1, x1, *zdet);
ramp_down:
	i = 0;
	while (x1) {
		val = snd_soc_component_read_field(component, WCD939X_ANA_MBHC_RESULT_1,
						   WCD939X_MBHC_RESULT_1_Z_RESULT_LSB) << 8;
		val |= snd_soc_component_read_field(component, WCD939X_ANA_MBHC_RESULT_2,
						    WCD939X_MBHC_RESULT_2_Z_RESULT_MSB);
		x1 = WCD939X_MBHC_GET_X1(val);
		i++;
		if (i == WCD939X_ZDET_NUM_MEASUREMENTS)
			break;
	}
}

static void wcd939x_mbhc_zdet_ramp(struct snd_soc_component *component,
				   s32 *zl, int32_t *zr)
{
	const struct zdet_param *zdet_param = &wcd939x_mbhc_zdet_param;
	s32 zdet = 0;

	snd_soc_component_write_field(component, WCD939X_MBHC_NEW_ZDET_ANA_CTL,
				      WCD939X_ZDET_ANA_CTL_MAXV_CTL, zdet_param->ldo_ctl);
	snd_soc_component_update_bits(component, WCD939X_ANA_MBHC_BTN5, WCD939X_MBHC_BTN5_VTH,
				      zdet_param->btn5);
	snd_soc_component_update_bits(component, WCD939X_ANA_MBHC_BTN6, WCD939X_MBHC_BTN6_VTH,
				      zdet_param->btn6);
	snd_soc_component_update_bits(component, WCD939X_ANA_MBHC_BTN7, WCD939X_MBHC_BTN7_VTH,
				      zdet_param->btn7);
	snd_soc_component_write_field(component, WCD939X_MBHC_NEW_ZDET_ANA_CTL,
				      WCD939X_ZDET_ANA_CTL_RANGE_CTL, zdet_param->noff);
	snd_soc_component_write_field(component, WCD939X_MBHC_NEW_ZDET_RAMP_CTL,
				      WCD939X_ZDET_RAMP_CTL_TIME_CTL, zdet_param->nshift);
	snd_soc_component_write_field(component, WCD939X_MBHC_NEW_ZDET_RAMP_CTL,
				      WCD939X_ZDET_RAMP_CTL_ACC1_MIN_CTL, 6); /*acc1_min_63 */

	if (!zl)
		goto z_right;

	/* Start impedance measurement for HPH_L */
	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ZDET,
				      WCD939X_MBHC_ZDET_ZDET_L_MEAS_EN, true);
	dev_dbg(component->dev, "%s: ramp for HPH_L, noff = %d\n",
		__func__, zdet_param->noff);
	wcd939x_mbhc_get_result_params(component, &zdet);
	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ZDET,
				      WCD939X_MBHC_ZDET_ZDET_L_MEAS_EN, false);

	*zl = zdet;

z_right:
	if (!zr)
		return;

	/* Start impedance measurement for HPH_R */
	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ZDET,
				      WCD939X_MBHC_ZDET_ZDET_R_MEAS_EN, true);
	dev_dbg(component->dev, "%s: ramp for HPH_R, noff = %d\n",
		__func__, zdet_param->noff);
	wcd939x_mbhc_get_result_params(component, &zdet);
	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ZDET,
				      WCD939X_MBHC_ZDET_ZDET_R_MEAS_EN, false);

	*zr = zdet;
}

static void wcd939x_wcd_mbhc_qfuse_cal(struct snd_soc_component *component,
				       s32 *z_val, int flag_l_r)
{
	int q1_cal;
	s16 q1;

	q1 = snd_soc_component_read(component, WCD939X_DIGITAL_EFUSE_REG_21 + flag_l_r);
	if (q1 & BIT(7))
		q1_cal = (10000 - ((q1 & GENMASK(6, 0)) * 10));
	else
		q1_cal = (10000 + (q1 * 10));

	if (q1_cal > 0)
		*z_val = ((*z_val) * 10000) / q1_cal;
}

static void wcd939x_wcd_mbhc_calc_impedance(struct snd_soc_component *component,
					    u32 *zl, uint32_t *zr)
{
	struct wcd939x_priv *wcd939x = dev_get_drvdata(component->dev);
	unsigned int reg0, reg1, reg2, reg3, reg4;
	int z_mono, z_diff1, z_diff2;
	bool is_fsm_disable = false;
	s32 z1l, z1r, z1ls;

	reg0 = snd_soc_component_read(component, WCD939X_ANA_MBHC_BTN5);
	reg1 = snd_soc_component_read(component, WCD939X_ANA_MBHC_BTN6);
	reg2 = snd_soc_component_read(component, WCD939X_ANA_MBHC_BTN7);
	reg3 = snd_soc_component_read(component, WCD939X_MBHC_CTL_CLK);
	reg4 = snd_soc_component_read(component, WCD939X_MBHC_NEW_ZDET_ANA_CTL);

	if (snd_soc_component_read_field(component, WCD939X_ANA_MBHC_ELECT,
					 WCD939X_MBHC_ELECT_FSM_EN)) {
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ELECT,
					      WCD939X_MBHC_ELECT_FSM_EN, false);
		is_fsm_disable = true;
	}

	/* For NO-jack, disable L_DET_EN before Z-det measurements */
	if (wcd939x->mbhc_cfg.hphl_swh)
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_MECH,
					      WCD939X_MBHC_MECH_L_DET_EN, false);

	/* Turn off 100k pull down on HPHL */
	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_MECH,
				      WCD939X_MBHC_MECH_SW_HPH_L_P_100K_TO_GND,
				      false);

	/*
	 * Disable surge protection before impedance detection.
	 * This is done to give correct value for high impedance.
	 */
	snd_soc_component_write_field(component, WCD939X_HPH_SURGE_EN,
				      WCD939X_EN_EN_SURGE_PROTECTION_HPHR, false);
	snd_soc_component_write_field(component, WCD939X_HPH_SURGE_EN,
				      WCD939X_EN_EN_SURGE_PROTECTION_HPHL, false);

	/* 1ms delay needed after disable surge protection */
	usleep_range(1000, 1010);

	/* First get impedance on Left */
	wcd939x_mbhc_zdet_ramp(component, &z1l, NULL);
	if (z1l == WCD939X_ZDET_FLOATING_IMPEDANCE || z1l > WCD939X_ZDET_VAL_100K) {
		*zl = WCD939X_ZDET_FLOATING_IMPEDANCE;
	} else {
		*zl = z1l / 1000;
		wcd939x_wcd_mbhc_qfuse_cal(component, zl, 0);
	}
	dev_dbg(component->dev, "%s: impedance on HPH_L = %d(ohms)\n",
		__func__, *zl);

	/* Start of right impedance ramp and calculation */
	wcd939x_mbhc_zdet_ramp(component, NULL, &z1r);
	if (z1r == WCD939X_ZDET_FLOATING_IMPEDANCE || z1r > WCD939X_ZDET_VAL_100K) {
		*zr = WCD939X_ZDET_FLOATING_IMPEDANCE;
	} else {
		*zr = z1r / 1000;
		wcd939x_wcd_mbhc_qfuse_cal(component, zr, 1);
	}
	dev_dbg(component->dev, "%s: impedance on HPH_R = %d(ohms)\n",
		__func__, *zr);

	/* Mono/stereo detection */
	if (*zl == WCD939X_ZDET_FLOATING_IMPEDANCE &&
	    *zr == WCD939X_ZDET_FLOATING_IMPEDANCE) {
		dev_dbg(component->dev,
			"%s: plug type is invalid or extension cable\n",
			__func__);
		goto zdet_complete;
	}

	if (*zl == WCD939X_ZDET_FLOATING_IMPEDANCE ||
	    *zr == WCD939X_ZDET_FLOATING_IMPEDANCE ||
	    (*zl < WCD_MONO_HS_MIN_THR && *zr > WCD_MONO_HS_MIN_THR) ||
	    (*zl > WCD_MONO_HS_MIN_THR && *zr < WCD_MONO_HS_MIN_THR)) {
		dev_dbg(component->dev,
			"%s: Mono plug type with one ch floating or shorted to GND\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd939x->wcd_mbhc, WCD_MBHC_HPH_MONO);
		goto zdet_complete;
	}

	snd_soc_component_write_field(component, WCD939X_HPH_R_ATEST,
				      WCD939X_R_ATEST_HPH_GND_OVR, true);
	snd_soc_component_write_field(component, WCD939X_HPH_PA_CTL2,
				      WCD939X_PA_CTL2_HPHPA_GND_R, true);
	wcd939x_mbhc_zdet_ramp(component, &z1ls, NULL);
	snd_soc_component_write_field(component, WCD939X_HPH_PA_CTL2,
				      WCD939X_PA_CTL2_HPHPA_GND_R, false);
	snd_soc_component_write_field(component, WCD939X_HPH_R_ATEST,
				      WCD939X_R_ATEST_HPH_GND_OVR, false);

	z1ls /= 1000;
	wcd939x_wcd_mbhc_qfuse_cal(component, &z1ls, 0);

	/* Parallel of left Z and 9 ohm pull down resistor */
	z_mono = (*zl * 9) / (*zl + 9);
	z_diff1 = z1ls > z_mono ? z1ls - z_mono : z_mono - z1ls;
	z_diff2 = *zl > z1ls ? *zl - z1ls : z1ls - *zl;
	if ((z_diff1 * (*zl + z1ls)) > (z_diff2 * (z1ls + z_mono))) {
		dev_dbg(component->dev, "%s: stereo plug type detected\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd939x->wcd_mbhc, WCD_MBHC_HPH_STEREO);
	} else {
		dev_dbg(component->dev, "%s: MONO plug type detected\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd939x->wcd_mbhc, WCD_MBHC_HPH_MONO);
	}

	/* Enable surge protection again after impedance detection */
	snd_soc_component_write_field(component, WCD939X_HPH_SURGE_EN,
				      WCD939X_EN_EN_SURGE_PROTECTION_HPHR, true);
	snd_soc_component_write_field(component, WCD939X_HPH_SURGE_EN,
				      WCD939X_EN_EN_SURGE_PROTECTION_HPHL, true);

zdet_complete:
	snd_soc_component_write(component, WCD939X_ANA_MBHC_BTN5, reg0);
	snd_soc_component_write(component, WCD939X_ANA_MBHC_BTN6, reg1);
	snd_soc_component_write(component, WCD939X_ANA_MBHC_BTN7, reg2);

	/* Turn on 100k pull down on HPHL */
	snd_soc_component_write_field(component, WCD939X_ANA_MBHC_MECH,
				      WCD939X_MBHC_MECH_SW_HPH_L_P_100K_TO_GND, true);

	/* For NO-jack, re-enable L_DET_EN after Z-det measurements */
	if (wcd939x->mbhc_cfg.hphl_swh)
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_MECH,
					      WCD939X_MBHC_MECH_L_DET_EN, true);

	snd_soc_component_write(component, WCD939X_MBHC_NEW_ZDET_ANA_CTL, reg4);
	snd_soc_component_write(component, WCD939X_MBHC_CTL_CLK, reg3);

	if (is_fsm_disable)
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_ELECT,
					      WCD939X_MBHC_ELECT_FSM_EN, true);
}

static void wcd939x_mbhc_gnd_det_ctrl(struct snd_soc_component *component,
				      bool enable)
{
	if (enable) {
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_MECH,
					      WCD939X_MBHC_MECH_MECH_HS_G_PULLUP_COMP_EN,
					      true);
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_MECH,
					      WCD939X_MBHC_MECH_GND_DET_EN, true);
	} else {
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_MECH,
					      WCD939X_MBHC_MECH_GND_DET_EN, false);
		snd_soc_component_write_field(component, WCD939X_ANA_MBHC_MECH,
					      WCD939X_MBHC_MECH_MECH_HS_G_PULLUP_COMP_EN,
					      false);
	}
}

static void wcd939x_mbhc_hph_pull_down_ctrl(struct snd_soc_component *component,
					    bool enable)
{
	snd_soc_component_write_field(component, WCD939X_HPH_PA_CTL2,
				      WCD939X_PA_CTL2_HPHPA_GND_R, enable);
	snd_soc_component_write_field(component, WCD939X_HPH_PA_CTL2,
				      WCD939X_PA_CTL2_HPHPA_GND_L, enable);
}

static void wcd939x_mbhc_moisture_config(struct snd_soc_component *component)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	if (wcd939x->mbhc_cfg.moist_rref == R_OFF || wcd939x->typec_analog_mux) {
		snd_soc_component_write_field(component, WCD939X_MBHC_NEW_CTL_2,
					      WCD939X_CTL_2_M_RTH_CTL, R_OFF);
		return;
	}

	/* Do not enable moisture detection if jack type is NC */
	if (!wcd939x->mbhc_cfg.hphl_swh) {
		dev_dbg(component->dev, "%s: disable moisture detection for NC\n",
			__func__);
		snd_soc_component_write_field(component, WCD939X_MBHC_NEW_CTL_2,
					      WCD939X_CTL_2_M_RTH_CTL, R_OFF);
		return;
	}

	snd_soc_component_write_field(component, WCD939X_MBHC_NEW_CTL_2,
				      WCD939X_CTL_2_M_RTH_CTL, wcd939x->mbhc_cfg.moist_rref);
}

static void wcd939x_mbhc_moisture_detect_en(struct snd_soc_component *component, bool enable)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	if (enable)
		snd_soc_component_write_field(component, WCD939X_MBHC_NEW_CTL_2,
					      WCD939X_CTL_2_M_RTH_CTL,
					      wcd939x->mbhc_cfg.moist_rref);
	else
		snd_soc_component_write_field(component, WCD939X_MBHC_NEW_CTL_2,
					      WCD939X_CTL_2_M_RTH_CTL, R_OFF);
}

static bool wcd939x_mbhc_get_moisture_status(struct snd_soc_component *component)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	bool ret = false;

	if (wcd939x->mbhc_cfg.moist_rref == R_OFF || wcd939x->typec_analog_mux) {
		snd_soc_component_write_field(component, WCD939X_MBHC_NEW_CTL_2,
					      WCD939X_CTL_2_M_RTH_CTL, R_OFF);
		goto done;
	}

	/* Do not enable moisture detection if jack type is NC */
	if (!wcd939x->mbhc_cfg.hphl_swh) {
		dev_dbg(component->dev, "%s: disable moisture detection for NC\n",
			__func__);
		snd_soc_component_write_field(component, WCD939X_MBHC_NEW_CTL_2,
					      WCD939X_CTL_2_M_RTH_CTL, R_OFF);
		goto done;
	}

	/*
	 * If moisture_en is already enabled, then skip to plug type
	 * detection.
	 */
	if (snd_soc_component_read_field(component, WCD939X_MBHC_NEW_CTL_2,
					 WCD939X_CTL_2_M_RTH_CTL))
		goto done;

	wcd939x_mbhc_moisture_detect_en(component, true);

	/* Read moisture comparator status, invert of status bit */
	ret = !snd_soc_component_read_field(component, WCD939X_MBHC_NEW_FSM_STATUS,
					    WCD939X_FSM_STATUS_HS_M_COMP_STATUS);
done:
	return ret;
}

static void wcd939x_mbhc_moisture_polling_ctrl(struct snd_soc_component *component,
					       bool enable)
{
	snd_soc_component_write_field(component,
				      WCD939X_MBHC_NEW_INT_MOISTURE_DET_POLLING_CTRL,
				      WCD939X_MOISTURE_DET_POLLING_CTRL_MOIST_EN_POLLING,
				      enable);
}

static const struct wcd_mbhc_cb mbhc_cb = {
	.clk_setup = wcd939x_mbhc_clk_setup,
	.mbhc_bias = wcd939x_mbhc_mbhc_bias_control,
	.set_btn_thr = wcd939x_mbhc_program_btn_thr,
	.micbias_enable_status = wcd939x_mbhc_micb_en_status,
	.hph_pull_up_control_v2 = wcd939x_mbhc_hph_l_pull_up_control,
	.mbhc_micbias_control = wcd939x_mbhc_request_micbias,
	.mbhc_micb_ramp_control = wcd939x_mbhc_micb_ramp_control,
	.mbhc_micb_ctrl_thr_mic = wcd939x_mbhc_micb_ctrl_threshold_mic,
	.compute_impedance = wcd939x_wcd_mbhc_calc_impedance,
	.mbhc_gnd_det_ctrl = wcd939x_mbhc_gnd_det_ctrl,
	.hph_pull_down_ctrl = wcd939x_mbhc_hph_pull_down_ctrl,
	.mbhc_moisture_config = wcd939x_mbhc_moisture_config,
	.mbhc_get_moisture_status = wcd939x_mbhc_get_moisture_status,
	.mbhc_moisture_polling_ctrl = wcd939x_mbhc_moisture_polling_ctrl,
	.mbhc_moisture_detect_en = wcd939x_mbhc_moisture_detect_en,
};

static int wcd939x_get_hph_type(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd_mbhc_get_hph_type(wcd939x->wcd_mbhc);

	return 0;
}

static int wcd939x_hph_impedance_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)(kcontrol->private_value);
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	bool hphr = mc->shift;
	u32 zl, zr;

	wcd_mbhc_get_impedance(wcd939x->wcd_mbhc, &zl, &zr);
	dev_dbg(component->dev, "%s: zl=%u(ohms), zr=%u(ohms)\n", __func__, zl, zr);
	ucontrol->value.integer.value[0] = hphr ? zr : zl;

	return 0;
}

static const struct snd_kcontrol_new hph_type_detect_controls[] = {
	SOC_SINGLE_EXT("HPH Type", 0, 0, UINT_MAX, 0,
		       wcd939x_get_hph_type, NULL),
};

static const struct snd_kcontrol_new impedance_detect_controls[] = {
	SOC_SINGLE_EXT("HPHL Impedance", 0, 0, UINT_MAX, 0,
		       wcd939x_hph_impedance_get, NULL),
	SOC_SINGLE_EXT("HPHR Impedance", 0, 1, UINT_MAX, 0,
		       wcd939x_hph_impedance_get, NULL),
};

static int wcd939x_mbhc_init(struct snd_soc_component *component)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	struct wcd_mbhc_intr *intr_ids = &wcd939x->intr_ids;

	intr_ids->mbhc_sw_intr = regmap_irq_get_virq(wcd939x->irq_chip,
						     WCD939X_IRQ_MBHC_SW_DET);
	intr_ids->mbhc_btn_press_intr = regmap_irq_get_virq(wcd939x->irq_chip,
							    WCD939X_IRQ_MBHC_BUTTON_PRESS_DET);
	intr_ids->mbhc_btn_release_intr = regmap_irq_get_virq(wcd939x->irq_chip,
							      WCD939X_IRQ_MBHC_BUTTON_RELEASE_DET);
	intr_ids->mbhc_hs_ins_intr = regmap_irq_get_virq(wcd939x->irq_chip,
							 WCD939X_IRQ_MBHC_ELECT_INS_REM_LEG_DET);
	intr_ids->mbhc_hs_rem_intr = regmap_irq_get_virq(wcd939x->irq_chip,
							 WCD939X_IRQ_MBHC_ELECT_INS_REM_DET);
	intr_ids->hph_left_ocp = regmap_irq_get_virq(wcd939x->irq_chip,
						     WCD939X_IRQ_HPHL_OCP_INT);
	intr_ids->hph_right_ocp = regmap_irq_get_virq(wcd939x->irq_chip,
						      WCD939X_IRQ_HPHR_OCP_INT);

	wcd939x->wcd_mbhc = wcd_mbhc_init(component, &mbhc_cb, intr_ids, wcd_mbhc_fields, true);
	if (IS_ERR(wcd939x->wcd_mbhc))
		return PTR_ERR(wcd939x->wcd_mbhc);

	snd_soc_add_component_controls(component, impedance_detect_controls,
				       ARRAY_SIZE(impedance_detect_controls));
	snd_soc_add_component_controls(component, hph_type_detect_controls,
				       ARRAY_SIZE(hph_type_detect_controls));

	return 0;
}

static void wcd939x_mbhc_deinit(struct snd_soc_component *component)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	wcd_mbhc_deinit(wcd939x->wcd_mbhc);
}

/* END MBHC */

static const struct snd_kcontrol_new wcd939x_snd_controls[] = {
	/* RX Path */
	SOC_SINGLE_EXT("HPHL_COMP Switch", WCD939X_COMP_L, 0, 1, 0,
		       wcd939x_get_compander, wcd939x_set_compander),
	SOC_SINGLE_EXT("HPHR_COMP Switch", WCD939X_COMP_R, 1, 1, 0,
		       wcd939x_get_compander, wcd939x_set_compander),
	SOC_SINGLE_EXT("HPHL Switch", WCD939X_HPH_L, 0, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("HPHR Switch", WCD939X_HPH_R, 0, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("CLSH Switch", WCD939X_CLSH, 0, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("LO Switch", WCD939X_LO, 0, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DSD_L Switch", WCD939X_DSD_L, 0, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DSD_R Switch", WCD939X_DSD_R, 0, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_TLV("HPHL Volume", WCD939X_HPH_L_EN, 0, 20, 1, line_gain),
	SOC_SINGLE_TLV("HPHR Volume", WCD939X_HPH_R_EN, 0, 20, 1, line_gain),
	SOC_SINGLE_EXT("LDOH Enable Switch", SND_SOC_NOPM, 0, 1, 0,
		       wcd939x_ldoh_get, wcd939x_ldoh_put),

	/* TX Path */
	SOC_SINGLE_EXT("ADC1 Switch", WCD939X_ADC1, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("ADC2 Switch", WCD939X_ADC2, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("ADC3 Switch", WCD939X_ADC3, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("ADC4 Switch", WCD939X_ADC4, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DMIC0 Switch", WCD939X_DMIC0, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DMIC1 Switch", WCD939X_DMIC1, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("MBHC Switch", WCD939X_MBHC, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DMIC2 Switch", WCD939X_DMIC2, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DMIC3 Switch", WCD939X_DMIC3, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DMIC4 Switch", WCD939X_DMIC4, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DMIC5 Switch", WCD939X_DMIC5, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DMIC6 Switch", WCD939X_DMIC6, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_EXT("DMIC7 Switch", WCD939X_DMIC7, 1, 1, 0,
		       wcd939x_get_swr_port, wcd939x_set_swr_port),
	SOC_SINGLE_TLV("ADC1 Volume", WCD939X_ANA_TX_CH1, 0, 20, 0,
		       analog_gain),
	SOC_SINGLE_TLV("ADC2 Volume", WCD939X_ANA_TX_CH2, 0, 20, 0,
		       analog_gain),
	SOC_SINGLE_TLV("ADC3 Volume", WCD939X_ANA_TX_CH3, 0, 20, 0,
		       analog_gain),
	SOC_SINGLE_TLV("ADC4 Volume", WCD939X_ANA_TX_CH4, 0, 20, 0,
		       analog_gain),
};

static const struct snd_soc_dapm_widget wcd939x_dapm_widgets[] = {
	/*input widgets*/
	SND_SOC_DAPM_INPUT("AMIC1"),
	SND_SOC_DAPM_INPUT("AMIC2"),
	SND_SOC_DAPM_INPUT("AMIC3"),
	SND_SOC_DAPM_INPUT("AMIC4"),
	SND_SOC_DAPM_INPUT("AMIC5"),

	SND_SOC_DAPM_MIC("Analog Mic1", NULL),
	SND_SOC_DAPM_MIC("Analog Mic2", NULL),
	SND_SOC_DAPM_MIC("Analog Mic3", NULL),
	SND_SOC_DAPM_MIC("Analog Mic4", NULL),
	SND_SOC_DAPM_MIC("Analog Mic5", NULL),

	/* TX widgets */
	SND_SOC_DAPM_ADC_E("ADC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd939x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC2", NULL, SND_SOC_NOPM, 1, 0,
			   wcd939x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC3", NULL, SND_SOC_NOPM, 2, 0,
			   wcd939x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC4", NULL, SND_SOC_NOPM, 3, 0,
			   wcd939x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd939x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC2", NULL, SND_SOC_NOPM, 1, 0,
			   wcd939x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC3", NULL, SND_SOC_NOPM, 2, 0,
			   wcd939x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC4", NULL, SND_SOC_NOPM, 3, 0,
			   wcd939x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC5", NULL, SND_SOC_NOPM, 4, 0,
			   wcd939x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC6", NULL, SND_SOC_NOPM, 5, 0,
			   wcd939x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC7", NULL, SND_SOC_NOPM, 6, 0,
			   wcd939x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC8", NULL, SND_SOC_NOPM, 7, 0,
			   wcd939x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("ADC1 REQ", SND_SOC_NOPM, 0, 0, NULL, 0,
			     wcd939x_adc_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC2 REQ", SND_SOC_NOPM, 1, 0, NULL, 0,
			     wcd939x_adc_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC3 REQ", SND_SOC_NOPM, 2, 0, NULL, 0,
			     wcd939x_adc_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC4 REQ", SND_SOC_NOPM, 3, 0, NULL, 0,
			     wcd939x_adc_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("ADC1 MUX", SND_SOC_NOPM, 0, 0, &tx_adc1_mux),
	SND_SOC_DAPM_MUX("ADC2 MUX", SND_SOC_NOPM, 0, 0, &tx_adc2_mux),
	SND_SOC_DAPM_MUX("ADC3 MUX", SND_SOC_NOPM, 0, 0, &tx_adc3_mux),
	SND_SOC_DAPM_MUX("ADC4 MUX", SND_SOC_NOPM, 0, 0, &tx_adc4_mux),

	/* tx mixers */
	SND_SOC_DAPM_MIXER_E("ADC1_MIXER", SND_SOC_NOPM, 0, 0,
			     adc1_switch, ARRAY_SIZE(adc1_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC2_MIXER", SND_SOC_NOPM, 0, 0,
			     adc2_switch, ARRAY_SIZE(adc2_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC3_MIXER", SND_SOC_NOPM, 0, 0,
			     adc3_switch, ARRAY_SIZE(adc3_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC4_MIXER", SND_SOC_NOPM, 0, 0,
			     adc4_switch, ARRAY_SIZE(adc4_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC1_MIXER", SND_SOC_NOPM, 0, 0,
			     dmic1_switch, ARRAY_SIZE(dmic1_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC2_MIXER", SND_SOC_NOPM, 0, 0,
			     dmic2_switch, ARRAY_SIZE(dmic2_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC3_MIXER", SND_SOC_NOPM, 0, 0,
			     dmic3_switch, ARRAY_SIZE(dmic3_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC4_MIXER", SND_SOC_NOPM, 0, 0,
			     dmic4_switch, ARRAY_SIZE(dmic4_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC5_MIXER", SND_SOC_NOPM, 0, 0,
			     dmic5_switch, ARRAY_SIZE(dmic5_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC6_MIXER", SND_SOC_NOPM, 0, 0,
			     dmic6_switch, ARRAY_SIZE(dmic6_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC7_MIXER", SND_SOC_NOPM, 0, 0,
			     dmic7_switch, ARRAY_SIZE(dmic7_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC8_MIXER", SND_SOC_NOPM, 0, 0,
			     dmic8_switch, ARRAY_SIZE(dmic8_switch), wcd939x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* micbias widgets */
	SND_SOC_DAPM_SUPPLY("MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0,
			    wcd939x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0,
			    wcd939x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0,
			    wcd939x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS4", SND_SOC_NOPM, MIC_BIAS_4, 0,
			    wcd939x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),

	/* micbias pull up widgets */
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0,
			    wcd939x_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0,
			    wcd939x_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0,
			    wcd939x_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS4", SND_SOC_NOPM, MIC_BIAS_4, 0,
			    wcd939x_codec_enable_micbias_pullup,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),

	/* output widgets tx */
	SND_SOC_DAPM_OUTPUT("ADC1_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("ADC2_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("ADC3_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("ADC4_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC1_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC2_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC3_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC4_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC5_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC6_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC7_OUTPUT"),
	SND_SOC_DAPM_OUTPUT("DMIC8_OUTPUT"),

	SND_SOC_DAPM_INPUT("IN1_HPHL"),
	SND_SOC_DAPM_INPUT("IN2_HPHR"),
	SND_SOC_DAPM_INPUT("IN3_EAR"),

	/* rx widgets */
	SND_SOC_DAPM_PGA_E("EAR PGA", WCD939X_ANA_EAR, 7, 0, NULL, 0,
			   wcd939x_codec_enable_ear_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHL PGA", WCD939X_ANA_HPH, 7, 0, NULL, 0,
			   wcd939x_codec_enable_hphl_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHR PGA", WCD939X_ANA_HPH, 6, 0, NULL, 0,
			   wcd939x_codec_enable_hphr_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_DAC_E("RDAC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd939x_codec_hphl_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC2", NULL, SND_SOC_NOPM, 0, 0,
			   wcd939x_codec_hphr_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC3", NULL, SND_SOC_NOPM, 0, 0,
			   wcd939x_codec_ear_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("RDAC3_MUX", SND_SOC_NOPM, 0, 0, &rx_rdac3_mux),

	SND_SOC_DAPM_SUPPLY("VDD_BUCK", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RXCLK", SND_SOC_NOPM, 0, 0,
			    wcd939x_codec_enable_rxclk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("CLS_H_PORT", 1, SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER_E("RX1", SND_SOC_NOPM, 0, 0, NULL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("RX2", SND_SOC_NOPM, 0, 0, NULL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("RX3", SND_SOC_NOPM, 0, 0, NULL, 0, NULL, 0),

	/* rx mixer widgets */
	SND_SOC_DAPM_MIXER("EAR_RDAC", SND_SOC_NOPM, 0, 0,
			   ear_rdac_switch, ARRAY_SIZE(ear_rdac_switch)),
	SND_SOC_DAPM_MIXER("HPHL_RDAC", SND_SOC_NOPM, 0, 0,
			   hphl_rdac_switch, ARRAY_SIZE(hphl_rdac_switch)),
	SND_SOC_DAPM_MIXER("HPHR_RDAC", SND_SOC_NOPM, 0, 0,
			   hphr_rdac_switch, ARRAY_SIZE(hphr_rdac_switch)),

	/* output widgets rx */
	SND_SOC_DAPM_OUTPUT("EAR"),
	SND_SOC_DAPM_OUTPUT("HPHL"),
	SND_SOC_DAPM_OUTPUT("HPHR"),
};

static const struct snd_soc_dapm_route wcd939x_audio_map[] = {
	/* TX Path */
	{"ADC1_OUTPUT", NULL, "ADC1_MIXER"},
	{"ADC1_MIXER", "Switch", "ADC1 REQ"},
	{"ADC1 REQ", NULL, "ADC1"},
	{"ADC1", NULL, "ADC1 MUX"},
	{"ADC1 MUX", "CH1_AMIC1", "AMIC1"},
	{"ADC1 MUX", "CH1_AMIC2", "AMIC2"},
	{"ADC1 MUX", "CH1_AMIC3", "AMIC3"},
	{"ADC1 MUX", "CH1_AMIC4", "AMIC4"},
	{"ADC1 MUX", "CH1_AMIC5", "AMIC5"},

	{"ADC2_OUTPUT", NULL, "ADC2_MIXER"},
	{"ADC2_MIXER", "Switch", "ADC2 REQ"},
	{"ADC2 REQ", NULL, "ADC2"},
	{"ADC2", NULL, "ADC2 MUX"},
	{"ADC2 MUX", "CH2_AMIC1", "AMIC1"},
	{"ADC2 MUX", "CH2_AMIC2", "AMIC2"},
	{"ADC2 MUX", "CH2_AMIC3", "AMIC3"},
	{"ADC2 MUX", "CH2_AMIC4", "AMIC4"},
	{"ADC2 MUX", "CH2_AMIC5", "AMIC5"},

	{"ADC3_OUTPUT", NULL, "ADC3_MIXER"},
	{"ADC3_MIXER", "Switch", "ADC3 REQ"},
	{"ADC3 REQ", NULL, "ADC3"},
	{"ADC3", NULL, "ADC3 MUX"},
	{"ADC3 MUX", "CH3_AMIC1", "AMIC1"},
	{"ADC3 MUX", "CH3_AMIC3", "AMIC3"},
	{"ADC3 MUX", "CH3_AMIC4", "AMIC4"},
	{"ADC3 MUX", "CH3_AMIC5", "AMIC5"},

	{"ADC4_OUTPUT", NULL, "ADC4_MIXER"},
	{"ADC4_MIXER", "Switch", "ADC4 REQ"},
	{"ADC4 REQ", NULL, "ADC4"},
	{"ADC4", NULL, "ADC4 MUX"},
	{"ADC4 MUX", "CH4_AMIC1", "AMIC1"},
	{"ADC4 MUX", "CH4_AMIC3", "AMIC3"},
	{"ADC4 MUX", "CH4_AMIC4", "AMIC4"},
	{"ADC4 MUX", "CH4_AMIC5", "AMIC5"},

	{"DMIC1_OUTPUT", NULL, "DMIC1_MIXER"},
	{"DMIC1_MIXER", "Switch", "DMIC1"},

	{"DMIC2_OUTPUT", NULL, "DMIC2_MIXER"},
	{"DMIC2_MIXER", "Switch", "DMIC2"},

	{"DMIC3_OUTPUT", NULL, "DMIC3_MIXER"},
	{"DMIC3_MIXER", "Switch", "DMIC3"},

	{"DMIC4_OUTPUT", NULL, "DMIC4_MIXER"},
	{"DMIC4_MIXER", "Switch", "DMIC4"},

	{"DMIC5_OUTPUT", NULL, "DMIC5_MIXER"},
	{"DMIC5_MIXER", "Switch", "DMIC5"},

	{"DMIC6_OUTPUT", NULL, "DMIC6_MIXER"},
	{"DMIC6_MIXER", "Switch", "DMIC6"},

	{"DMIC7_OUTPUT", NULL, "DMIC7_MIXER"},
	{"DMIC7_MIXER", "Switch", "DMIC7"},

	{"DMIC8_OUTPUT", NULL, "DMIC8_MIXER"},
	{"DMIC8_MIXER", "Switch", "DMIC8"},

	/* RX Path */
	{"IN1_HPHL", NULL, "VDD_BUCK"},
	{"IN1_HPHL", NULL, "CLS_H_PORT"},

	{"RX1", NULL, "IN1_HPHL"},
	{"RX1", NULL, "RXCLK"},
	{"RDAC1", NULL, "RX1"},
	{"HPHL_RDAC", "Switch", "RDAC1"},
	{"HPHL PGA", NULL, "HPHL_RDAC"},
	{"HPHL", NULL, "HPHL PGA"},

	{"IN2_HPHR", NULL, "VDD_BUCK"},
	{"IN2_HPHR", NULL, "CLS_H_PORT"},
	{"RX2", NULL, "IN2_HPHR"},
	{"RDAC2", NULL, "RX2"},
	{"RX2", NULL, "RXCLK"},
	{"HPHR_RDAC", "Switch", "RDAC2"},
	{"HPHR PGA", NULL, "HPHR_RDAC"},
	{"HPHR", NULL, "HPHR PGA"},

	{"IN3_EAR", NULL, "VDD_BUCK"},
	{"RX3", NULL, "IN3_EAR"},
	{"RX3", NULL, "RXCLK"},

	{"RDAC3_MUX", "RX3", "RX3"},
	{"RDAC3_MUX", "RX1", "RX1"},
	{"RDAC3", NULL, "RDAC3_MUX"},
	{"EAR_RDAC", "Switch", "RDAC3"},
	{"EAR PGA", NULL, "EAR_RDAC"},
	{"EAR", NULL, "EAR PGA"},
};

static int wcd939x_set_micbias_data(struct wcd939x_priv *wcd939x)
{
	int vout_ctl_1, vout_ctl_2, vout_ctl_3, vout_ctl_4;

	/* set micbias voltage */
	vout_ctl_1 = wcd939x_get_micb_vout_ctl_val(wcd939x->micb1_mv);
	vout_ctl_2 = wcd939x_get_micb_vout_ctl_val(wcd939x->micb2_mv);
	vout_ctl_3 = wcd939x_get_micb_vout_ctl_val(wcd939x->micb3_mv);
	vout_ctl_4 = wcd939x_get_micb_vout_ctl_val(wcd939x->micb4_mv);
	if (vout_ctl_1 < 0 || vout_ctl_2 < 0 || vout_ctl_3 < 0 || vout_ctl_4 < 0)
		return -EINVAL;

	regmap_update_bits(wcd939x->regmap, WCD939X_ANA_MICB1,
			   WCD939X_MICB1_VOUT_CTL, vout_ctl_1);
	regmap_update_bits(wcd939x->regmap, WCD939X_ANA_MICB2,
			   WCD939X_MICB2_VOUT_CTL, vout_ctl_2);
	regmap_update_bits(wcd939x->regmap, WCD939X_ANA_MICB3,
			   WCD939X_MICB3_VOUT_CTL, vout_ctl_3);
	regmap_update_bits(wcd939x->regmap, WCD939X_ANA_MICB4,
			   WCD939X_MICB4_VOUT_CTL, vout_ctl_4);

	return 0;
}

static irqreturn_t wcd939x_wd_handle_irq(int irq, void *data)
{
	/*
	 * HPHR/HPHL/EAR Watchdog interrupt threaded handler
	 *
	 * Watchdog interrupts are expected to be enabled when switching
	 * on the HPHL/R and EAR RX PGA in order to make sure the interrupts
	 * are acked by the regmap_irq handler to allow PDM sync.
	 * We could leave those interrupts masked but we would not have
	 * any valid way to enable/disable them without violating irq layers.
	 *
	 * The HPHR/HPHL/EAR Watchdog interrupts are handled
	 * by regmap_irq, so requesting a threaded handler is the
	 * safest way to be able to ack those interrupts without
	 * colliding with the regmap_irq setup.
	 */

	return IRQ_HANDLED;
}

/*
 * Setup a virtual interrupt domain to hook regmap_irq
 * The root domain will have a single interrupt which mapping
 * will trigger the regmap_irq handler.
 *
 * root:
 *   wcd_irq_chip
 *     [0] wcd939x_regmap_irq_chip
 *       [0] MBHC_BUTTON_PRESS_DET
 *       [1] MBHC_BUTTON_RELEASE_DET
 *       ...
 *       [16] HPHR_SURGE_DET_INT
 *
 * Interrupt trigger:
 *   soundwire_interrupt_callback()
 *   \-handle_nested_irq(0)
 *     \- regmap_irq_thread()
 *         \- handle_nested_irq(i)
 */
static struct irq_chip wcd_irq_chip = {
	.name = "WCD939x",
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

static int wcd939x_irq_init(struct wcd939x_priv *wcd, struct device *dev)
{
	wcd->virq = irq_domain_add_linear(NULL, 1, &wcd_domain_ops, NULL);
	if (!(wcd->virq)) {
		dev_err(dev, "%s: Failed to add IRQ domain\n", __func__);
		return -EINVAL;
	}

	return devm_regmap_add_irq_chip(dev, wcd->regmap,
					irq_create_mapping(wcd->virq, 0),
					IRQF_ONESHOT, 0, &wcd939x_regmap_irq_chip,
					&wcd->irq_chip);
}

static int wcd939x_soc_codec_probe(struct snd_soc_component *component)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);
	struct sdw_slave *tx_sdw_dev = wcd939x->tx_sdw_dev;
	struct device *dev = component->dev;
	unsigned long time_left;
	int ret, i;

	time_left = wait_for_completion_timeout(&tx_sdw_dev->initialization_complete,
						msecs_to_jiffies(2000));
	if (!time_left) {
		dev_err(dev, "soundwire device init timeout\n");
		return -ETIMEDOUT;
	}

	snd_soc_component_init_regmap(component, wcd939x->regmap);

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	wcd939x->variant = snd_soc_component_read_field(component,
							WCD939X_DIGITAL_EFUSE_REG_0,
							WCD939X_EFUSE_REG_0_WCD939X_ID);

	wcd939x->clsh_info = wcd_clsh_ctrl_alloc(component, WCD939X);
	if (IS_ERR(wcd939x->clsh_info)) {
		pm_runtime_put(dev);
		return PTR_ERR(wcd939x->clsh_info);
	}

	wcd939x_io_init(component);

	/* Set all interrupts as edge triggered */
	for (i = 0; i < wcd939x_regmap_irq_chip.num_regs; i++)
		regmap_write(wcd939x->regmap,
			     (WCD939X_DIGITAL_INTR_LEVEL_0 + i), 0);

	pm_runtime_put(dev);

	/* Request for watchdog interrupt */
	wcd939x->hphr_pdm_wd_int = regmap_irq_get_virq(wcd939x->irq_chip,
						       WCD939X_IRQ_HPHR_PDM_WD_INT);
	wcd939x->hphl_pdm_wd_int = regmap_irq_get_virq(wcd939x->irq_chip,
						       WCD939X_IRQ_HPHL_PDM_WD_INT);
	wcd939x->ear_pdm_wd_int = regmap_irq_get_virq(wcd939x->irq_chip,
						      WCD939X_IRQ_EAR_PDM_WD_INT);

	ret = request_threaded_irq(wcd939x->hphr_pdm_wd_int, NULL, wcd939x_wd_handle_irq,
				   IRQF_ONESHOT | IRQF_TRIGGER_RISING,
				   "HPHR PDM WD INT", wcd939x);
	if (ret) {
		dev_err(dev, "Failed to request HPHR WD interrupt (%d)\n", ret);
		goto err_free_clsh_ctrl;
	}

	ret = request_threaded_irq(wcd939x->hphl_pdm_wd_int, NULL, wcd939x_wd_handle_irq,
				   IRQF_ONESHOT | IRQF_TRIGGER_RISING,
				   "HPHL PDM WD INT", wcd939x);
	if (ret) {
		dev_err(dev, "Failed to request HPHL WD interrupt (%d)\n", ret);
		goto err_free_hphr_pdm_wd_int;
	}

	ret = request_threaded_irq(wcd939x->ear_pdm_wd_int, NULL, wcd939x_wd_handle_irq,
				   IRQF_ONESHOT | IRQF_TRIGGER_RISING,
				   "AUX PDM WD INT", wcd939x);
	if (ret) {
		dev_err(dev, "Failed to request Aux WD interrupt (%d)\n", ret);
		goto err_free_hphl_pdm_wd_int;
	}

	/* Disable watchdog interrupt for HPH and AUX */
	disable_irq_nosync(wcd939x->hphr_pdm_wd_int);
	disable_irq_nosync(wcd939x->hphl_pdm_wd_int);
	disable_irq_nosync(wcd939x->ear_pdm_wd_int);

	switch (wcd939x->variant) {
	case WCD9390:
		ret = snd_soc_add_component_controls(component, wcd9390_snd_controls,
						     ARRAY_SIZE(wcd9390_snd_controls));
		if (ret < 0) {
			dev_err(component->dev,
				"%s: Failed to add snd ctrls for variant: %d\n",
				__func__, wcd939x->variant);
			goto err_free_ear_pdm_wd_int;
		}
		break;
	case WCD9395:
		ret = snd_soc_add_component_controls(component, wcd9395_snd_controls,
						     ARRAY_SIZE(wcd9395_snd_controls));
		if (ret < 0) {
			dev_err(component->dev,
				"%s: Failed to add snd ctrls for variant: %d\n",
				__func__, wcd939x->variant);
			goto err_free_ear_pdm_wd_int;
		}
		break;
	default:
		break;
	}

	ret = wcd939x_mbhc_init(component);
	if (ret) {
		dev_err(component->dev,  "mbhc initialization failed\n");
		goto err_free_ear_pdm_wd_int;
	}

	return 0;

err_free_ear_pdm_wd_int:
	free_irq(wcd939x->ear_pdm_wd_int, wcd939x);
err_free_hphl_pdm_wd_int:
	free_irq(wcd939x->hphl_pdm_wd_int, wcd939x);
err_free_hphr_pdm_wd_int:
	free_irq(wcd939x->hphr_pdm_wd_int, wcd939x);
err_free_clsh_ctrl:
	wcd_clsh_ctrl_free(wcd939x->clsh_info);

	return ret;
}

static void wcd939x_soc_codec_remove(struct snd_soc_component *component)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	wcd939x_mbhc_deinit(component);

	free_irq(wcd939x->ear_pdm_wd_int, wcd939x);
	free_irq(wcd939x->hphl_pdm_wd_int, wcd939x);
	free_irq(wcd939x->hphr_pdm_wd_int, wcd939x);

	wcd_clsh_ctrl_free(wcd939x->clsh_info);
}

static int wcd939x_codec_set_jack(struct snd_soc_component *comp,
				  struct snd_soc_jack *jack, void *data)
{
	struct wcd939x_priv *wcd = dev_get_drvdata(comp->dev);

	if (jack)
		return wcd_mbhc_start(wcd->wcd_mbhc, &wcd->mbhc_cfg, jack);

	wcd_mbhc_stop(wcd->wcd_mbhc);

	return 0;
}

static const struct snd_soc_component_driver soc_codec_dev_wcd939x = {
	.name = "wcd939x_codec",
	.probe = wcd939x_soc_codec_probe,
	.remove = wcd939x_soc_codec_remove,
	.controls = wcd939x_snd_controls,
	.num_controls = ARRAY_SIZE(wcd939x_snd_controls),
	.dapm_widgets = wcd939x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wcd939x_dapm_widgets),
	.dapm_routes = wcd939x_audio_map,
	.num_dapm_routes = ARRAY_SIZE(wcd939x_audio_map),
	.set_jack = wcd939x_codec_set_jack,
	.endianness = 1,
};

#if IS_ENABLED(CONFIG_TYPEC)
/* Get USB-C plug orientation to provide swap event for MBHC */
static int wcd939x_typec_switch_set(struct typec_switch_dev *sw,
				    enum typec_orientation orientation)
{
	struct wcd939x_priv *wcd939x = typec_switch_get_drvdata(sw);

	wcd939x->typec_orientation = orientation;

	return 0;
}

static int wcd939x_typec_mux_set(struct typec_mux_dev *mux,
				 struct typec_mux_state *state)
{
	struct wcd939x_priv *wcd939x = typec_mux_get_drvdata(mux);
	unsigned int previous_mode = wcd939x->typec_mode;

	if (!wcd939x->wcd_mbhc)
		return -EINVAL;

	if (wcd939x->typec_mode != state->mode) {
		wcd939x->typec_mode = state->mode;

		if (wcd939x->typec_mode == TYPEC_MODE_AUDIO)
			return wcd_mbhc_typec_report_plug(wcd939x->wcd_mbhc);
		else if (previous_mode == TYPEC_MODE_AUDIO)
			return wcd_mbhc_typec_report_unplug(wcd939x->wcd_mbhc);
	}

	return 0;
}
#endif /* CONFIG_TYPEC */

static void wcd939x_dt_parse_micbias_info(struct device *dev, struct wcd939x_priv *wcd)
{
	struct device_node *np = dev->of_node;
	u32 prop_val = 0;
	int rc = 0;

	rc = of_property_read_u32(np, "qcom,micbias1-microvolt",  &prop_val);
	if (!rc)
		wcd->micb1_mv = prop_val / 1000;
	else
		dev_info(dev, "%s: Micbias1 DT property not found\n", __func__);

	rc = of_property_read_u32(np, "qcom,micbias2-microvolt",  &prop_val);
	if (!rc)
		wcd->micb2_mv = prop_val / 1000;
	else
		dev_info(dev, "%s: Micbias2 DT property not found\n", __func__);

	rc = of_property_read_u32(np, "qcom,micbias3-microvolt", &prop_val);
	if (!rc)
		wcd->micb3_mv = prop_val / 1000;
	else
		dev_info(dev, "%s: Micbias3 DT property not found\n", __func__);

	rc = of_property_read_u32(np, "qcom,micbias4-microvolt",  &prop_val);
	if (!rc)
		wcd->micb4_mv = prop_val / 1000;
	else
		dev_info(dev, "%s: Micbias4 DT property not found\n", __func__);
}

#if IS_ENABLED(CONFIG_TYPEC)
static bool wcd939x_swap_gnd_mic(struct snd_soc_component *component, bool active)
{
	struct wcd939x_priv *wcd939x = snd_soc_component_get_drvdata(component);

	if (!wcd939x->typec_analog_mux || !wcd939x->typec_switch)
		return false;

	/* Report inversion via Type Switch of USBSS */
	typec_switch_set(wcd939x->typec_switch,
			 wcd939x->typec_orientation == TYPEC_ORIENTATION_REVERSE ?
				TYPEC_ORIENTATION_NORMAL : TYPEC_ORIENTATION_REVERSE);

	return true;
}
#endif /* CONFIG_TYPEC */

static int wcd939x_populate_dt_data(struct wcd939x_priv *wcd939x, struct device *dev)
{
	struct wcd_mbhc_config *cfg = &wcd939x->mbhc_cfg;
#if IS_ENABLED(CONFIG_TYPEC)
	struct device_node *np;
#endif /* CONFIG_TYPEC */
	int ret;

	wcd939x->reset_gpio = of_get_named_gpio(dev->of_node, "reset-gpios", 0);
	if (wcd939x->reset_gpio < 0)
		return dev_err_probe(dev, wcd939x->reset_gpio,
				     "Failed to get reset gpio\n");

	wcd939x->supplies[0].supply = "vdd-rxtx";
	wcd939x->supplies[1].supply = "vdd-io";
	wcd939x->supplies[2].supply = "vdd-buck";
	wcd939x->supplies[3].supply = "vdd-mic-bias";

	ret = regulator_bulk_get(dev, WCD939X_MAX_SUPPLY, wcd939x->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get supplies\n");

	ret = regulator_bulk_enable(WCD939X_MAX_SUPPLY, wcd939x->supplies);
	if (ret) {
		regulator_bulk_free(WCD939X_MAX_SUPPLY, wcd939x->supplies);
		return dev_err_probe(dev, ret, "Failed to enable supplies\n");
	}

	wcd939x_dt_parse_micbias_info(dev, wcd939x);

	cfg->mbhc_micbias = MIC_BIAS_2;
	cfg->anc_micbias = MIC_BIAS_2;
	cfg->v_hs_max = WCD_MBHC_HS_V_MAX;
	cfg->num_btn = WCD939X_MBHC_MAX_BUTTONS;
	cfg->micb_mv = wcd939x->micb2_mv;
	cfg->linein_th = 5000;
	cfg->hs_thr = 1700;
	cfg->hph_thr = 50;

	wcd_dt_parse_mbhc_data(dev, cfg);

#if IS_ENABLED(CONFIG_TYPEC)
	/*
	 * Is node has a port and a valid remote endpoint
	 * consider HP lines are connected to the USBSS part
	 */
	np = of_graph_get_remote_node(dev->of_node, 0, 0);
	if (np) {
		wcd939x->typec_analog_mux = true;
		cfg->typec_analog_mux = true;
		cfg->swap_gnd_mic = wcd939x_swap_gnd_mic;
	}
#endif /* CONFIG_TYPEC */

	return 0;
}

static int wcd939x_reset(struct wcd939x_priv *wcd939x)
{
	gpio_direction_output(wcd939x->reset_gpio, 0);
	/* 20us sleep required after pulling the reset gpio to LOW */
	usleep_range(20, 30);
	gpio_set_value(wcd939x->reset_gpio, 1);
	/* 20us sleep required after pulling the reset gpio to HIGH */
	usleep_range(20, 30);

	return 0;
}

static int wcd939x_codec_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct wcd939x_priv *wcd939x = dev_get_drvdata(dai->dev);
	struct wcd939x_sdw_priv *wcd = wcd939x->sdw_priv[dai->id];

	return wcd939x_sdw_hw_params(wcd, substream, params, dai);
}

static int wcd939x_codec_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct wcd939x_priv *wcd939x = dev_get_drvdata(dai->dev);
	struct wcd939x_sdw_priv *wcd = wcd939x->sdw_priv[dai->id];

	return wcd939x_sdw_free(wcd, substream, dai);
}

static int wcd939x_codec_set_sdw_stream(struct snd_soc_dai *dai,
					void *stream, int direction)
{
	struct wcd939x_priv *wcd939x = dev_get_drvdata(dai->dev);
	struct wcd939x_sdw_priv *wcd = wcd939x->sdw_priv[dai->id];

	return wcd939x_sdw_set_sdw_stream(wcd, dai, stream, direction);
}

static const struct snd_soc_dai_ops wcd939x_sdw_dai_ops = {
	.hw_params = wcd939x_codec_hw_params,
	.hw_free = wcd939x_codec_free,
	.set_stream = wcd939x_codec_set_sdw_stream,
};

static struct snd_soc_dai_driver wcd939x_dais[] = {
	[0] = {
		.name = "wcd939x-sdw-rx",
		.playback = {
			.stream_name = "WCD AIF1 Playback",
			.rates = WCD939X_RATES_MASK | WCD939X_FRAC_RATES_MASK,
			.formats = WCD939X_FORMATS,
			.rate_max = 384000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd939x_sdw_dai_ops,
	},
	[1] = {
		.name = "wcd939x-sdw-tx",
		.capture = {
			.stream_name = "WCD AIF1 Capture",
			.rates = WCD939X_RATES_MASK | WCD939X_FRAC_RATES_MASK,
			.formats = WCD939X_FORMATS,
			.rate_min = 8000,
			.rate_max = 384000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd939x_sdw_dai_ops,
	},
};

static int wcd939x_bind(struct device *dev)
{
	struct wcd939x_priv *wcd939x = dev_get_drvdata(dev);
	unsigned int version, id1, status1;
	int ret;

#if IS_ENABLED(CONFIG_TYPEC)
	/*
	 * Get USBSS type-c switch to send gnd/mic swap events
	 * typec_switch is fetched now to avoid a probe deadlock since
	 * the USBSS depends on the typec_mux register in wcd939x_probe()
	 */
	if (wcd939x->typec_analog_mux) {
		wcd939x->typec_switch = fwnode_typec_switch_get(dev->fwnode);
		if (IS_ERR(wcd939x->typec_switch))
			return dev_err_probe(dev, PTR_ERR(wcd939x->typec_switch),
					     "failed to acquire orientation-switch\n");
	}
#endif /* CONFIG_TYPEC */

	ret = component_bind_all(dev, wcd939x);
	if (ret) {
		dev_err(dev, "%s: Slave bind failed, ret = %d\n",
			__func__, ret);
		goto err_put_typec_switch;
	}

	wcd939x->rxdev = wcd939x_sdw_device_get(wcd939x->rxnode);
	if (!wcd939x->rxdev) {
		dev_err(dev, "could not find slave with matching of node\n");
		ret = -EINVAL;
		goto err_unbind;
	}
	wcd939x->sdw_priv[AIF1_PB] = dev_get_drvdata(wcd939x->rxdev);
	wcd939x->sdw_priv[AIF1_PB]->wcd939x = wcd939x;

	wcd939x->txdev = wcd939x_sdw_device_get(wcd939x->txnode);
	if (!wcd939x->txdev) {
		dev_err(dev, "could not find txslave with matching of node\n");
		ret = -EINVAL;
		goto err_put_rxdev;
	}
	wcd939x->sdw_priv[AIF1_CAP] = dev_get_drvdata(wcd939x->txdev);
	wcd939x->sdw_priv[AIF1_CAP]->wcd939x = wcd939x;
	wcd939x->tx_sdw_dev = dev_to_sdw_dev(wcd939x->txdev);

	/*
	 * As TX is main CSR reg interface, which should not be suspended first.
	 * explicitly add the dependency link
	 */
	if (!device_link_add(wcd939x->rxdev, wcd939x->txdev, DL_FLAG_STATELESS |
			    DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "could not devlink tx and rx\n");
		ret = -EINVAL;
		goto err_put_txdev;
	}

	if (!device_link_add(dev, wcd939x->txdev, DL_FLAG_STATELESS |
					DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "could not devlink wcd and tx\n");
		ret = -EINVAL;
		goto err_remove_rxtx_link;
	}

	if (!device_link_add(dev, wcd939x->rxdev, DL_FLAG_STATELESS |
					DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "could not devlink wcd and rx\n");
		ret = -EINVAL;
		goto err_remove_tx_link;
	}

	/* Get regmap from TX SoundWire device */
	wcd939x->regmap = wcd939x_swr_get_regmap(wcd939x->sdw_priv[AIF1_CAP]);
	if (IS_ERR(wcd939x->regmap)) {
		dev_err(dev, "could not get TX device regmap\n");
		ret = PTR_ERR(wcd939x->regmap);
		goto err_remove_rx_link;
	}

	ret = wcd939x_irq_init(wcd939x, dev);
	if (ret) {
		dev_err(dev, "%s: IRQ init failed: %d\n", __func__, ret);
		goto err_remove_rx_link;
	}

	wcd939x->sdw_priv[AIF1_PB]->slave_irq = wcd939x->virq;
	wcd939x->sdw_priv[AIF1_CAP]->slave_irq = wcd939x->virq;

	ret = wcd939x_set_micbias_data(wcd939x);
	if (ret < 0) {
		dev_err(dev, "%s: bad micbias pdata\n", __func__);
		goto err_remove_rx_link;
	}

	/* Check WCD9395 version */
	regmap_read(wcd939x->regmap, WCD939X_DIGITAL_CHIP_ID1, &id1);
	regmap_read(wcd939x->regmap, WCD939X_EAR_STATUS_REG_1, &status1);

	if (id1 == 0)
		version = ((status1 & 0x3) ? WCD939X_VERSION_1_1 : WCD939X_VERSION_1_0);
	else
		version = WCD939X_VERSION_2_0;

	dev_dbg(dev, "wcd939x version: %s\n", version_to_str(version));

	ret = snd_soc_register_component(dev, &soc_codec_dev_wcd939x,
					 wcd939x_dais, ARRAY_SIZE(wcd939x_dais));
	if (ret) {
		dev_err(dev, "%s: Codec registration failed\n",
			__func__);
		goto err_remove_rx_link;
	}

	return 0;

err_remove_rx_link:
	device_link_remove(dev, wcd939x->rxdev);
err_remove_tx_link:
	device_link_remove(dev, wcd939x->txdev);
err_remove_rxtx_link:
	device_link_remove(wcd939x->rxdev, wcd939x->txdev);
err_put_txdev:
	put_device(wcd939x->txdev);
err_put_rxdev:
	put_device(wcd939x->rxdev);
err_unbind:
	component_unbind_all(dev, wcd939x);
err_put_typec_switch:
#if IS_ENABLED(CONFIG_TYPEC)
	if (wcd939x->typec_analog_mux)
		typec_switch_put(wcd939x->typec_switch);
#endif /* CONFIG_TYPEC */

	return ret;
}

static void wcd939x_unbind(struct device *dev)
{
	struct wcd939x_priv *wcd939x = dev_get_drvdata(dev);

	snd_soc_unregister_component(dev);
	device_link_remove(dev, wcd939x->txdev);
	device_link_remove(dev, wcd939x->rxdev);
	device_link_remove(wcd939x->rxdev, wcd939x->txdev);
	put_device(wcd939x->txdev);
	put_device(wcd939x->rxdev);
	component_unbind_all(dev, wcd939x);
}

static const struct component_master_ops wcd939x_comp_ops = {
	.bind   = wcd939x_bind,
	.unbind = wcd939x_unbind,
};

static int wcd939x_add_slave_components(struct wcd939x_priv *wcd939x,
					struct device *dev,
					struct component_match **matchptr)
{
	struct device_node *np = dev->of_node;

	wcd939x->rxnode = of_parse_phandle(np, "qcom,rx-device", 0);
	if (!wcd939x->rxnode) {
		dev_err(dev, "%s: Rx-device node not defined\n", __func__);
		return -ENODEV;
	}

	of_node_get(wcd939x->rxnode);
	component_match_add_release(dev, matchptr, component_release_of,
				    component_compare_of, wcd939x->rxnode);

	wcd939x->txnode = of_parse_phandle(np, "qcom,tx-device", 0);
	if (!wcd939x->txnode) {
		dev_err(dev, "%s: Tx-device node not defined\n", __func__);
		return -ENODEV;
	}
	of_node_get(wcd939x->txnode);
	component_match_add_release(dev, matchptr, component_release_of,
				    component_compare_of, wcd939x->txnode);
	return 0;
}

static int wcd939x_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct wcd939x_priv *wcd939x = NULL;
	struct device *dev = &pdev->dev;
	int ret;

	wcd939x = devm_kzalloc(dev, sizeof(struct wcd939x_priv),
			       GFP_KERNEL);
	if (!wcd939x)
		return -ENOMEM;

	dev_set_drvdata(dev, wcd939x);
	mutex_init(&wcd939x->micb_lock);

	ret = wcd939x_populate_dt_data(wcd939x, dev);
	if (ret) {
		dev_err(dev, "%s: Fail to obtain platform data\n", __func__);
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_TYPEC)
	/*
	 * Is USBSS is used to mux analog lines,
	 * register a typec mux/switch to get typec events
	 */
	if (wcd939x->typec_analog_mux) {
		struct typec_mux_desc mux_desc = {
			.drvdata = wcd939x,
			.fwnode = dev_fwnode(dev),
			.set = wcd939x_typec_mux_set,
		};
		struct typec_switch_desc sw_desc = {
			.drvdata = wcd939x,
			.fwnode = dev_fwnode(dev),
			.set = wcd939x_typec_switch_set,
		};

		wcd939x->typec_mux = typec_mux_register(dev, &mux_desc);
		if (IS_ERR(wcd939x->typec_mux)) {
			ret = dev_err_probe(dev, PTR_ERR(wcd939x->typec_mux),
					    "failed to register typec mux\n");
			goto err_disable_regulators;
		}

		wcd939x->typec_sw = typec_switch_register(dev, &sw_desc);
		if (IS_ERR(wcd939x->typec_sw)) {
			ret = dev_err_probe(dev, PTR_ERR(wcd939x->typec_sw),
					    "failed to register typec switch\n");
			goto err_unregister_typec_mux;
		}
	}
#endif /* CONFIG_TYPEC */

	ret = wcd939x_add_slave_components(wcd939x, dev, &match);
	if (ret)
		goto err_unregister_typec_switch;

	wcd939x_reset(wcd939x);

	ret = component_master_add_with_match(dev, &wcd939x_comp_ops, match);
	if (ret)
		goto err_disable_regulators;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

#if IS_ENABLED(CONFIG_TYPEC)
err_unregister_typec_mux:
	if (wcd939x->typec_analog_mux)
		typec_mux_unregister(wcd939x->typec_mux);
#endif /* CONFIG_TYPEC */

err_unregister_typec_switch:
#if IS_ENABLED(CONFIG_TYPEC)
	if (wcd939x->typec_analog_mux)
		typec_switch_unregister(wcd939x->typec_sw);
#endif /* CONFIG_TYPEC */

err_disable_regulators:
	regulator_bulk_disable(WCD939X_MAX_SUPPLY, wcd939x->supplies);
	regulator_bulk_free(WCD939X_MAX_SUPPLY, wcd939x->supplies);

	return ret;
}

static void wcd939x_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wcd939x_priv *wcd939x = dev_get_drvdata(dev);

	component_master_del(dev, &wcd939x_comp_ops);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_dont_use_autosuspend(dev);

	regulator_bulk_disable(WCD939X_MAX_SUPPLY, wcd939x->supplies);
	regulator_bulk_free(WCD939X_MAX_SUPPLY, wcd939x->supplies);
}

#if defined(CONFIG_OF)
static const struct of_device_id wcd939x_dt_match[] = {
	{ .compatible = "qcom,wcd9390-codec" },
	{ .compatible = "qcom,wcd9395-codec" },
	{}
};
MODULE_DEVICE_TABLE(of, wcd939x_dt_match);
#endif

static struct platform_driver wcd939x_codec_driver = {
	.probe = wcd939x_probe,
	.remove_new = wcd939x_remove,
	.driver = {
		.name = "wcd939x_codec",
		.of_match_table = of_match_ptr(wcd939x_dt_match),
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(wcd939x_codec_driver);
MODULE_DESCRIPTION("WCD939X Codec driver");
MODULE_LICENSE("GPL");
