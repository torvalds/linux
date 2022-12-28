// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.

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
#include <linux/of.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/regulator/consumer.h>

#include "wcd-clsh-v2.h"
#include "wcd-mbhc-v2.h"
#include "wcd938x.h"

#define WCD938X_MAX_MICBIAS		(4)
#define WCD938X_MAX_SUPPLY		(4)
#define WCD938X_MBHC_MAX_BUTTONS	(8)
#define TX_ADC_MAX			(4)
#define WCD938X_TX_MAX_SWR_PORTS	(5)

#define WCD938X_RATES_MASK (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			    SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			    SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
/* Fractional Rates */
#define WCD938X_FRAC_RATES_MASK (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_88200 |\
				 SNDRV_PCM_RATE_176400)
#define WCD938X_FORMATS_S16_S24_LE (SNDRV_PCM_FMTBIT_S16_LE | \
				    SNDRV_PCM_FMTBIT_S24_LE)
/* Convert from vout ctl to micbias voltage in mV */
#define  WCD_VOUT_CTL_TO_MICB(v)	(1000 + v * 50)
#define SWR_CLK_RATE_0P6MHZ		(600000)
#define SWR_CLK_RATE_1P2MHZ		(1200000)
#define SWR_CLK_RATE_2P4MHZ		(2400000)
#define SWR_CLK_RATE_4P8MHZ		(4800000)
#define SWR_CLK_RATE_9P6MHZ		(9600000)
#define SWR_CLK_RATE_11P2896MHZ		(1128960)

#define WCD938X_DRV_NAME "wcd938x_codec"
#define WCD938X_VERSION_1_0		(1)
#define EAR_RX_PATH_AUX			(1)

#define ADC_MODE_VAL_HIFI		0x01
#define ADC_MODE_VAL_LO_HIF		0x02
#define ADC_MODE_VAL_NORMAL		0x03
#define ADC_MODE_VAL_LP			0x05
#define ADC_MODE_VAL_ULP1		0x09
#define ADC_MODE_VAL_ULP2		0x0B

/* Z value defined in milliohm */
#define WCD938X_ZDET_VAL_32             (32000)
#define WCD938X_ZDET_VAL_400            (400000)
#define WCD938X_ZDET_VAL_1200           (1200000)
#define WCD938X_ZDET_VAL_100K           (100000000)
/* Z floating defined in ohms */
#define WCD938X_ZDET_FLOATING_IMPEDANCE	(0x0FFFFFFE)
#define WCD938X_ZDET_NUM_MEASUREMENTS   (900)
#define WCD938X_MBHC_GET_C1(c)          ((c & 0xC000) >> 14)
#define WCD938X_MBHC_GET_X1(x)          (x & 0x3FFF)
/* Z value compared in milliOhm */
#define WCD938X_MBHC_IS_SECOND_RAMP_REQUIRED(z) ((z > 400000) || (z < 32000))
#define WCD938X_MBHC_ZDET_CONST         (86 * 16384)
#define WCD938X_MBHC_MOISTURE_RREF      R_24_KOHM
#define WCD_MBHC_HS_V_MAX           1600

#define WCD938X_EAR_PA_GAIN_TLV(xname, reg, shift, max, invert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = wcd938x_ear_pa_put_gain, \
	.private_value = SOC_SINGLE_VALUE(reg, shift, max, invert, 0) }

enum {
	WCD9380 = 0,
	WCD9385 = 5,
};

enum {
	TX_HDR12 = 0,
	TX_HDR34,
	TX_HDR_MAX,
};

enum {
	WCD_RX1,
	WCD_RX2,
	WCD_RX3
};

enum {
	/* INTR_CTRL_INT_MASK_0 */
	WCD938X_IRQ_MBHC_BUTTON_PRESS_DET = 0,
	WCD938X_IRQ_MBHC_BUTTON_RELEASE_DET,
	WCD938X_IRQ_MBHC_ELECT_INS_REM_DET,
	WCD938X_IRQ_MBHC_ELECT_INS_REM_LEG_DET,
	WCD938X_IRQ_MBHC_SW_DET,
	WCD938X_IRQ_HPHR_OCP_INT,
	WCD938X_IRQ_HPHR_CNP_INT,
	WCD938X_IRQ_HPHL_OCP_INT,

	/* INTR_CTRL_INT_MASK_1 */
	WCD938X_IRQ_HPHL_CNP_INT,
	WCD938X_IRQ_EAR_CNP_INT,
	WCD938X_IRQ_EAR_SCD_INT,
	WCD938X_IRQ_AUX_CNP_INT,
	WCD938X_IRQ_AUX_SCD_INT,
	WCD938X_IRQ_HPHL_PDM_WD_INT,
	WCD938X_IRQ_HPHR_PDM_WD_INT,
	WCD938X_IRQ_AUX_PDM_WD_INT,

	/* INTR_CTRL_INT_MASK_2 */
	WCD938X_IRQ_LDORT_SCD_INT,
	WCD938X_IRQ_MBHC_MOISTURE_INT,
	WCD938X_IRQ_HPHL_SURGE_DET_INT,
	WCD938X_IRQ_HPHR_SURGE_DET_INT,
	WCD938X_NUM_IRQS,
};

enum {
	WCD_ADC1 = 0,
	WCD_ADC2,
	WCD_ADC3,
	WCD_ADC4,
	ALLOW_BUCK_DISABLE,
	HPH_COMP_DELAY,
	HPH_PA_DELAY,
	AMIC2_BCS_ENABLE,
	WCD_SUPPLIES_LPM_MODE,
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

struct wcd938x_priv {
	struct sdw_slave *tx_sdw_dev;
	struct wcd938x_sdw_priv *sdw_priv[NUM_CODEC_DAIS];
	struct device *txdev;
	struct device *rxdev;
	struct device_node *rxnode, *txnode;
	struct regmap *regmap;
	struct mutex micb_lock;
	/* mbhc module */
	struct wcd_mbhc *wcd_mbhc;
	struct wcd_mbhc_config mbhc_cfg;
	struct wcd_mbhc_intr intr_ids;
	struct wcd_clsh_ctrl *clsh_info;
	struct irq_domain *virq;
	struct regmap_irq_chip *wcd_regmap_irq_chip;
	struct regmap_irq_chip_data *irq_chip;
	struct regulator_bulk_data supplies[WCD938X_MAX_SUPPLY];
	struct snd_soc_jack *jack;
	unsigned long status_mask;
	s32 micb_ref[WCD938X_MAX_MICBIAS];
	s32 pullup_ref[WCD938X_MAX_MICBIAS];
	u32 hph_mode;
	u32 tx_mode[TX_ADC_MAX];
	int flyback_cur_det_disable;
	int ear_rx_path;
	int variant;
	int reset_gpio;
	struct gpio_desc *us_euro_gpio;
	u32 micb1_mv;
	u32 micb2_mv;
	u32 micb3_mv;
	u32 micb4_mv;
	int hphr_pdm_wd_int;
	int hphl_pdm_wd_int;
	int aux_pdm_wd_int;
	bool comp1_enable;
	bool comp2_enable;
	bool ldoh;
	bool bcs_dis;
};

static const SNDRV_CTL_TLVD_DECLARE_DB_MINMAX(ear_pa_gain, 600, -1800);
static const SNDRV_CTL_TLVD_DECLARE_DB_MINMAX(line_gain, 600, -3000);
static const SNDRV_CTL_TLVD_DECLARE_DB_MINMAX(analog_gain, 0, 3000);

struct wcd938x_mbhc_zdet_param {
	u16 ldo_ctl;
	u16 noff;
	u16 nshift;
	u16 btn5;
	u16 btn6;
	u16 btn7;
};

static struct wcd_mbhc_field wcd_mbhc_fields[WCD_MBHC_REG_FUNC_MAX] = {
	WCD_MBHC_FIELD(WCD_MBHC_L_DET_EN, WCD938X_ANA_MBHC_MECH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_GND_DET_EN, WCD938X_ANA_MBHC_MECH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_MECH_DETECTION_TYPE, WCD938X_ANA_MBHC_MECH, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_CLAMP_CTL, WCD938X_MBHC_NEW_PLUG_DETECT_CTL, 0x30),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_DETECTION_TYPE, WCD938X_ANA_MBHC_ELECT, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_CTRL, WCD938X_MBHC_NEW_INT_MECH_DET_CURRENT, 0x1F),
	WCD_MBHC_FIELD(WCD_MBHC_HS_L_DET_PULL_UP_COMP_CTRL, WCD938X_ANA_MBHC_MECH, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PLUG_TYPE, WCD938X_ANA_MBHC_MECH, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_GND_PLUG_TYPE, WCD938X_ANA_MBHC_MECH, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_SW_HPH_LP_100K_TO_GND, WCD938X_ANA_MBHC_MECH, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_SCHMT_ISRC, WCD938X_ANA_MBHC_ELECT, 0x06),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_EN, WCD938X_ANA_MBHC_ELECT, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_INSREM_DBNC, WCD938X_MBHC_NEW_PLUG_DETECT_CTL, 0x0F),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_DBNC, WCD938X_MBHC_NEW_CTL_1, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_VREF, WCD938X_MBHC_NEW_CTL_2, 0x03),
	WCD_MBHC_FIELD(WCD_MBHC_HS_COMP_RESULT, WCD938X_ANA_MBHC_RESULT_3, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_IN2P_CLAMP_STATE, WCD938X_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_MIC_SCHMT_RESULT, WCD938X_ANA_MBHC_RESULT_3, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_SCHMT_RESULT, WCD938X_ANA_MBHC_RESULT_3, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_SCHMT_RESULT, WCD938X_ANA_MBHC_RESULT_3, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_OCP_FSM_EN, WCD938X_HPH_OCP_CTL, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_RESULT, WCD938X_ANA_MBHC_RESULT_3, 0x07),
	WCD_MBHC_FIELD(WCD_MBHC_BTN_ISRC_CTL, WCD938X_ANA_MBHC_ELECT, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_RESULT, WCD938X_ANA_MBHC_RESULT_3, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB_CTRL, WCD938X_ANA_MICB2, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_CNP_WG_TIME, WCD938X_HPH_CNP_WG_TIME, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_PA_EN, WCD938X_ANA_HPH, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_PA_EN, WCD938X_ANA_HPH, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPH_PA_EN, WCD938X_ANA_HPH, 0xC0),
	WCD_MBHC_FIELD(WCD_MBHC_SWCH_LEVEL_REMOVE, WCD938X_ANA_MBHC_RESULT_3, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_ANC_DET_EN, WCD938X_MBHC_CTL_BCS, 0x02),
	WCD_MBHC_FIELD(WCD_MBHC_FSM_STATUS, WCD938X_MBHC_NEW_FSM_STATUS, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_MUX_CTL, WCD938X_MBHC_NEW_CTL_2, 0x70),
	WCD_MBHC_FIELD(WCD_MBHC_MOISTURE_STATUS, WCD938X_MBHC_NEW_FSM_STATUS, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_GND, WCD938X_HPH_PA_CTL2, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_GND, WCD938X_HPH_PA_CTL2, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_DET_EN, WCD938X_HPH_L_TEST, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_DET_EN, WCD938X_HPH_R_TEST, 0x01),
	WCD_MBHC_FIELD(WCD_MBHC_HPHL_OCP_STATUS, WCD938X_DIGITAL_INTR_STATUS_0, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_HPHR_OCP_STATUS, WCD938X_DIGITAL_INTR_STATUS_0, 0x20),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_EN, WCD938X_MBHC_NEW_CTL_1, 0x08),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_COMPLETE, WCD938X_MBHC_NEW_FSM_STATUS, 0x40),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_TIMEOUT, WCD938X_MBHC_NEW_FSM_STATUS, 0x80),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_RESULT, WCD938X_MBHC_NEW_ADC_RESULT, 0xFF),
	WCD_MBHC_FIELD(WCD_MBHC_MICB2_VOUT, WCD938X_ANA_MICB2, 0x3F),
	WCD_MBHC_FIELD(WCD_MBHC_ADC_MODE, WCD938X_MBHC_NEW_CTL_1, 0x10),
	WCD_MBHC_FIELD(WCD_MBHC_DETECTION_DONE, WCD938X_MBHC_NEW_CTL_1, 0x04),
	WCD_MBHC_FIELD(WCD_MBHC_ELECT_ISRC_EN, WCD938X_ANA_MBHC_ZDET, 0x02),
};

static const struct reg_default wcd938x_defaults[] = {
	{WCD938X_ANA_PAGE_REGISTER,                            0x00},
	{WCD938X_ANA_BIAS,                                     0x00},
	{WCD938X_ANA_RX_SUPPLIES,                              0x00},
	{WCD938X_ANA_HPH,                                      0x0C},
	{WCD938X_ANA_EAR,                                      0x00},
	{WCD938X_ANA_EAR_COMPANDER_CTL,                        0x02},
	{WCD938X_ANA_TX_CH1,                                   0x20},
	{WCD938X_ANA_TX_CH2,                                   0x00},
	{WCD938X_ANA_TX_CH3,                                   0x20},
	{WCD938X_ANA_TX_CH4,                                   0x00},
	{WCD938X_ANA_MICB1_MICB2_DSP_EN_LOGIC,                 0x00},
	{WCD938X_ANA_MICB3_DSP_EN_LOGIC,                       0x00},
	{WCD938X_ANA_MBHC_MECH,                                0x39},
	{WCD938X_ANA_MBHC_ELECT,                               0x08},
	{WCD938X_ANA_MBHC_ZDET,                                0x00},
	{WCD938X_ANA_MBHC_RESULT_1,                            0x00},
	{WCD938X_ANA_MBHC_RESULT_2,                            0x00},
	{WCD938X_ANA_MBHC_RESULT_3,                            0x00},
	{WCD938X_ANA_MBHC_BTN0,                                0x00},
	{WCD938X_ANA_MBHC_BTN1,                                0x10},
	{WCD938X_ANA_MBHC_BTN2,                                0x20},
	{WCD938X_ANA_MBHC_BTN3,                                0x30},
	{WCD938X_ANA_MBHC_BTN4,                                0x40},
	{WCD938X_ANA_MBHC_BTN5,                                0x50},
	{WCD938X_ANA_MBHC_BTN6,                                0x60},
	{WCD938X_ANA_MBHC_BTN7,                                0x70},
	{WCD938X_ANA_MICB1,                                    0x10},
	{WCD938X_ANA_MICB2,                                    0x10},
	{WCD938X_ANA_MICB2_RAMP,                               0x00},
	{WCD938X_ANA_MICB3,                                    0x10},
	{WCD938X_ANA_MICB4,                                    0x10},
	{WCD938X_BIAS_CTL,                                     0x2A},
	{WCD938X_BIAS_VBG_FINE_ADJ,                            0x55},
	{WCD938X_LDOL_VDDCX_ADJUST,                            0x01},
	{WCD938X_LDOL_DISABLE_LDOL,                            0x00},
	{WCD938X_MBHC_CTL_CLK,                                 0x00},
	{WCD938X_MBHC_CTL_ANA,                                 0x00},
	{WCD938X_MBHC_CTL_SPARE_1,                             0x00},
	{WCD938X_MBHC_CTL_SPARE_2,                             0x00},
	{WCD938X_MBHC_CTL_BCS,                                 0x00},
	{WCD938X_MBHC_MOISTURE_DET_FSM_STATUS,                 0x00},
	{WCD938X_MBHC_TEST_CTL,                                0x00},
	{WCD938X_LDOH_MODE,                                    0x2B},
	{WCD938X_LDOH_BIAS,                                    0x68},
	{WCD938X_LDOH_STB_LOADS,                               0x00},
	{WCD938X_LDOH_SLOWRAMP,                                0x50},
	{WCD938X_MICB1_TEST_CTL_1,                             0x1A},
	{WCD938X_MICB1_TEST_CTL_2,                             0x00},
	{WCD938X_MICB1_TEST_CTL_3,                             0xA4},
	{WCD938X_MICB2_TEST_CTL_1,                             0x1A},
	{WCD938X_MICB2_TEST_CTL_2,                             0x00},
	{WCD938X_MICB2_TEST_CTL_3,                             0x24},
	{WCD938X_MICB3_TEST_CTL_1,                             0x1A},
	{WCD938X_MICB3_TEST_CTL_2,                             0x00},
	{WCD938X_MICB3_TEST_CTL_3,                             0xA4},
	{WCD938X_MICB4_TEST_CTL_1,                             0x1A},
	{WCD938X_MICB4_TEST_CTL_2,                             0x00},
	{WCD938X_MICB4_TEST_CTL_3,                             0xA4},
	{WCD938X_TX_COM_ADC_VCM,                               0x39},
	{WCD938X_TX_COM_BIAS_ATEST,                            0xE0},
	{WCD938X_TX_COM_SPARE1,                                0x00},
	{WCD938X_TX_COM_SPARE2,                                0x00},
	{WCD938X_TX_COM_TXFE_DIV_CTL,                          0x22},
	{WCD938X_TX_COM_TXFE_DIV_START,                        0x00},
	{WCD938X_TX_COM_SPARE3,                                0x00},
	{WCD938X_TX_COM_SPARE4,                                0x00},
	{WCD938X_TX_1_2_TEST_EN,                               0xCC},
	{WCD938X_TX_1_2_ADC_IB,                                0xE9},
	{WCD938X_TX_1_2_ATEST_REFCTL,                          0x0A},
	{WCD938X_TX_1_2_TEST_CTL,                              0x38},
	{WCD938X_TX_1_2_TEST_BLK_EN1,                          0xFF},
	{WCD938X_TX_1_2_TXFE1_CLKDIV,                          0x00},
	{WCD938X_TX_1_2_SAR2_ERR,                              0x00},
	{WCD938X_TX_1_2_SAR1_ERR,                              0x00},
	{WCD938X_TX_3_4_TEST_EN,                               0xCC},
	{WCD938X_TX_3_4_ADC_IB,                                0xE9},
	{WCD938X_TX_3_4_ATEST_REFCTL,                          0x0A},
	{WCD938X_TX_3_4_TEST_CTL,                              0x38},
	{WCD938X_TX_3_4_TEST_BLK_EN3,                          0xFF},
	{WCD938X_TX_3_4_TXFE3_CLKDIV,                          0x00},
	{WCD938X_TX_3_4_SAR4_ERR,                              0x00},
	{WCD938X_TX_3_4_SAR3_ERR,                              0x00},
	{WCD938X_TX_3_4_TEST_BLK_EN2,                          0xFB},
	{WCD938X_TX_3_4_TXFE2_CLKDIV,                          0x00},
	{WCD938X_TX_3_4_SPARE1,                                0x00},
	{WCD938X_TX_3_4_TEST_BLK_EN4,                          0xFB},
	{WCD938X_TX_3_4_TXFE4_CLKDIV,                          0x00},
	{WCD938X_TX_3_4_SPARE2,                                0x00},
	{WCD938X_CLASSH_MODE_1,                                0x40},
	{WCD938X_CLASSH_MODE_2,                                0x3A},
	{WCD938X_CLASSH_MODE_3,                                0x00},
	{WCD938X_CLASSH_CTRL_VCL_1,                            0x70},
	{WCD938X_CLASSH_CTRL_VCL_2,                            0x82},
	{WCD938X_CLASSH_CTRL_CCL_1,                            0x31},
	{WCD938X_CLASSH_CTRL_CCL_2,                            0x80},
	{WCD938X_CLASSH_CTRL_CCL_3,                            0x80},
	{WCD938X_CLASSH_CTRL_CCL_4,                            0x51},
	{WCD938X_CLASSH_CTRL_CCL_5,                            0x00},
	{WCD938X_CLASSH_BUCK_TMUX_A_D,                         0x00},
	{WCD938X_CLASSH_BUCK_SW_DRV_CNTL,                      0x77},
	{WCD938X_CLASSH_SPARE,                                 0x00},
	{WCD938X_FLYBACK_EN,                                   0x4E},
	{WCD938X_FLYBACK_VNEG_CTRL_1,                          0x0B},
	{WCD938X_FLYBACK_VNEG_CTRL_2,                          0x45},
	{WCD938X_FLYBACK_VNEG_CTRL_3,                          0x74},
	{WCD938X_FLYBACK_VNEG_CTRL_4,                          0x7F},
	{WCD938X_FLYBACK_VNEG_CTRL_5,                          0x83},
	{WCD938X_FLYBACK_VNEG_CTRL_6,                          0x98},
	{WCD938X_FLYBACK_VNEG_CTRL_7,                          0xA9},
	{WCD938X_FLYBACK_VNEG_CTRL_8,                          0x68},
	{WCD938X_FLYBACK_VNEG_CTRL_9,                          0x64},
	{WCD938X_FLYBACK_VNEGDAC_CTRL_1,                       0xED},
	{WCD938X_FLYBACK_VNEGDAC_CTRL_2,                       0xF0},
	{WCD938X_FLYBACK_VNEGDAC_CTRL_3,                       0xA6},
	{WCD938X_FLYBACK_CTRL_1,                               0x65},
	{WCD938X_FLYBACK_TEST_CTL,                             0x00},
	{WCD938X_RX_AUX_SW_CTL,                                0x00},
	{WCD938X_RX_PA_AUX_IN_CONN,                            0x01},
	{WCD938X_RX_TIMER_DIV,                                 0x32},
	{WCD938X_RX_OCP_CTL,                                   0x1F},
	{WCD938X_RX_OCP_COUNT,                                 0x77},
	{WCD938X_RX_BIAS_EAR_DAC,                              0xA0},
	{WCD938X_RX_BIAS_EAR_AMP,                              0xAA},
	{WCD938X_RX_BIAS_HPH_LDO,                              0xA9},
	{WCD938X_RX_BIAS_HPH_PA,                               0xAA},
	{WCD938X_RX_BIAS_HPH_RDACBUFF_CNP2,                    0x8A},
	{WCD938X_RX_BIAS_HPH_RDAC_LDO,                         0x88},
	{WCD938X_RX_BIAS_HPH_CNP1,                             0x82},
	{WCD938X_RX_BIAS_HPH_LOWPOWER,                         0x82},
	{WCD938X_RX_BIAS_AUX_DAC,                              0xA0},
	{WCD938X_RX_BIAS_AUX_AMP,                              0xAA},
	{WCD938X_RX_BIAS_VNEGDAC_BLEEDER,                      0x50},
	{WCD938X_RX_BIAS_MISC,                                 0x00},
	{WCD938X_RX_BIAS_BUCK_RST,                             0x08},
	{WCD938X_RX_BIAS_BUCK_VREF_ERRAMP,                     0x44},
	{WCD938X_RX_BIAS_FLYB_ERRAMP,                          0x40},
	{WCD938X_RX_BIAS_FLYB_BUFF,                            0xAA},
	{WCD938X_RX_BIAS_FLYB_MID_RST,                         0x14},
	{WCD938X_HPH_L_STATUS,                                 0x04},
	{WCD938X_HPH_R_STATUS,                                 0x04},
	{WCD938X_HPH_CNP_EN,                                   0x80},
	{WCD938X_HPH_CNP_WG_CTL,                               0x9A},
	{WCD938X_HPH_CNP_WG_TIME,                              0x14},
	{WCD938X_HPH_OCP_CTL,                                  0x28},
	{WCD938X_HPH_AUTO_CHOP,                                0x16},
	{WCD938X_HPH_CHOP_CTL,                                 0x83},
	{WCD938X_HPH_PA_CTL1,                                  0x46},
	{WCD938X_HPH_PA_CTL2,                                  0x50},
	{WCD938X_HPH_L_EN,                                     0x80},
	{WCD938X_HPH_L_TEST,                                   0xE0},
	{WCD938X_HPH_L_ATEST,                                  0x50},
	{WCD938X_HPH_R_EN,                                     0x80},
	{WCD938X_HPH_R_TEST,                                   0xE0},
	{WCD938X_HPH_R_ATEST,                                  0x54},
	{WCD938X_HPH_RDAC_CLK_CTL1,                            0x99},
	{WCD938X_HPH_RDAC_CLK_CTL2,                            0x9B},
	{WCD938X_HPH_RDAC_LDO_CTL,                             0x33},
	{WCD938X_HPH_RDAC_CHOP_CLK_LP_CTL,                     0x00},
	{WCD938X_HPH_REFBUFF_UHQA_CTL,                         0x68},
	{WCD938X_HPH_REFBUFF_LP_CTL,                           0x0E},
	{WCD938X_HPH_L_DAC_CTL,                                0x20},
	{WCD938X_HPH_R_DAC_CTL,                                0x20},
	{WCD938X_HPH_SURGE_HPHLR_SURGE_COMP_SEL,               0x55},
	{WCD938X_HPH_SURGE_HPHLR_SURGE_EN,                     0x19},
	{WCD938X_HPH_SURGE_HPHLR_SURGE_MISC1,                  0xA0},
	{WCD938X_HPH_SURGE_HPHLR_SURGE_STATUS,                 0x00},
	{WCD938X_EAR_EAR_EN_REG,                               0x22},
	{WCD938X_EAR_EAR_PA_CON,                               0x44},
	{WCD938X_EAR_EAR_SP_CON,                               0xDB},
	{WCD938X_EAR_EAR_DAC_CON,                              0x80},
	{WCD938X_EAR_EAR_CNP_FSM_CON,                          0xB2},
	{WCD938X_EAR_TEST_CTL,                                 0x00},
	{WCD938X_EAR_STATUS_REG_1,                             0x00},
	{WCD938X_EAR_STATUS_REG_2,                             0x08},
	{WCD938X_ANA_NEW_PAGE_REGISTER,                        0x00},
	{WCD938X_HPH_NEW_ANA_HPH2,                             0x00},
	{WCD938X_HPH_NEW_ANA_HPH3,                             0x00},
	{WCD938X_SLEEP_CTL,                                    0x16},
	{WCD938X_SLEEP_WATCHDOG_CTL,                           0x00},
	{WCD938X_MBHC_NEW_ELECT_REM_CLAMP_CTL,                 0x00},
	{WCD938X_MBHC_NEW_CTL_1,                               0x02},
	{WCD938X_MBHC_NEW_CTL_2,                               0x05},
	{WCD938X_MBHC_NEW_PLUG_DETECT_CTL,                     0xE9},
	{WCD938X_MBHC_NEW_ZDET_ANA_CTL,                        0x0F},
	{WCD938X_MBHC_NEW_ZDET_RAMP_CTL,                       0x00},
	{WCD938X_MBHC_NEW_FSM_STATUS,                          0x00},
	{WCD938X_MBHC_NEW_ADC_RESULT,                          0x00},
	{WCD938X_TX_NEW_AMIC_MUX_CFG,                          0x00},
	{WCD938X_AUX_AUXPA,                                    0x00},
	{WCD938X_LDORXTX_MODE,                                 0x0C},
	{WCD938X_LDORXTX_CONFIG,                               0x10},
	{WCD938X_DIE_CRACK_DIE_CRK_DET_EN,                     0x00},
	{WCD938X_DIE_CRACK_DIE_CRK_DET_OUT,                    0x00},
	{WCD938X_HPH_NEW_INT_RDAC_GAIN_CTL,                    0x40},
	{WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L,                   0x81},
	{WCD938X_HPH_NEW_INT_RDAC_VREF_CTL,                    0x10},
	{WCD938X_HPH_NEW_INT_RDAC_OVERRIDE_CTL,                0x00},
	{WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R,                   0x81},
	{WCD938X_HPH_NEW_INT_PA_MISC1,                         0x22},
	{WCD938X_HPH_NEW_INT_PA_MISC2,                         0x00},
	{WCD938X_HPH_NEW_INT_PA_RDAC_MISC,                     0x00},
	{WCD938X_HPH_NEW_INT_HPH_TIMER1,                       0xFE},
	{WCD938X_HPH_NEW_INT_HPH_TIMER2,                       0x02},
	{WCD938X_HPH_NEW_INT_HPH_TIMER3,                       0x4E},
	{WCD938X_HPH_NEW_INT_HPH_TIMER4,                       0x54},
	{WCD938X_HPH_NEW_INT_PA_RDAC_MISC2,                    0x00},
	{WCD938X_HPH_NEW_INT_PA_RDAC_MISC3,                    0x00},
	{WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L_NEW,               0x90},
	{WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R_NEW,               0x90},
	{WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_LOHIFI,              0x62},
	{WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_ULP,                 0x01},
	{WCD938X_RX_NEW_INT_HPH_RDAC_LDO_LP,                   0x11},
	{WCD938X_MBHC_NEW_INT_MOISTURE_DET_DC_CTRL,            0x57},
	{WCD938X_MBHC_NEW_INT_MOISTURE_DET_POLLING_CTRL,       0x01},
	{WCD938X_MBHC_NEW_INT_MECH_DET_CURRENT,                0x00},
	{WCD938X_MBHC_NEW_INT_SPARE_2,                         0x00},
	{WCD938X_EAR_INT_NEW_EAR_CHOPPER_CON,                  0xA8},
	{WCD938X_EAR_INT_NEW_CNP_VCM_CON1,                     0x42},
	{WCD938X_EAR_INT_NEW_CNP_VCM_CON2,                     0x22},
	{WCD938X_EAR_INT_NEW_EAR_DYNAMIC_BIAS,                 0x00},
	{WCD938X_AUX_INT_EN_REG,                               0x00},
	{WCD938X_AUX_INT_PA_CTRL,                              0x06},
	{WCD938X_AUX_INT_SP_CTRL,                              0xD2},
	{WCD938X_AUX_INT_DAC_CTRL,                             0x80},
	{WCD938X_AUX_INT_CLK_CTRL,                             0x50},
	{WCD938X_AUX_INT_TEST_CTRL,                            0x00},
	{WCD938X_AUX_INT_STATUS_REG,                           0x00},
	{WCD938X_AUX_INT_MISC,                                 0x00},
	{WCD938X_LDORXTX_INT_BIAS,                             0x6E},
	{WCD938X_LDORXTX_INT_STB_LOADS_DTEST,                  0x50},
	{WCD938X_LDORXTX_INT_TEST0,                            0x1C},
	{WCD938X_LDORXTX_INT_STARTUP_TIMER,                    0xFF},
	{WCD938X_LDORXTX_INT_TEST1,                            0x1F},
	{WCD938X_LDORXTX_INT_STATUS,                           0x00},
	{WCD938X_SLEEP_INT_WATCHDOG_CTL_1,                     0x0A},
	{WCD938X_SLEEP_INT_WATCHDOG_CTL_2,                     0x0A},
	{WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT1,               0x02},
	{WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT2,               0x60},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L2,               0xFF},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L1,               0x7F},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L0,               0x3F},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP1P2M,          0x1F},
	{WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP0P6M,          0x0F},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L2L1,          0xD7},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L0,            0xC8},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_ULP,           0xC6},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L2L1,      0xD5},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L0,        0xCA},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_ULP,       0x05},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_L2L1L0,    0xA5},
	{WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_ULP,       0x13},
	{WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L2L1,             0x88},
	{WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L0ULP,            0x42},
	{WCD938X_TX_COM_NEW_INT_TXADC_INT_L2,                  0xFF},
	{WCD938X_TX_COM_NEW_INT_TXADC_INT_L1,                  0x64},
	{WCD938X_TX_COM_NEW_INT_TXADC_INT_L0,                  0x64},
	{WCD938X_TX_COM_NEW_INT_TXADC_INT_ULP,                 0x77},
	{WCD938X_DIGITAL_PAGE_REGISTER,                        0x00},
	{WCD938X_DIGITAL_CHIP_ID0,                             0x00},
	{WCD938X_DIGITAL_CHIP_ID1,                             0x00},
	{WCD938X_DIGITAL_CHIP_ID2,                             0x0D},
	{WCD938X_DIGITAL_CHIP_ID3,                             0x01},
	{WCD938X_DIGITAL_SWR_TX_CLK_RATE,                      0x00},
	{WCD938X_DIGITAL_CDC_RST_CTL,                          0x03},
	{WCD938X_DIGITAL_TOP_CLK_CFG,                          0x00},
	{WCD938X_DIGITAL_CDC_ANA_CLK_CTL,                      0x00},
	{WCD938X_DIGITAL_CDC_DIG_CLK_CTL,                      0xF0},
	{WCD938X_DIGITAL_SWR_RST_EN,                           0x00},
	{WCD938X_DIGITAL_CDC_PATH_MODE,                        0x55},
	{WCD938X_DIGITAL_CDC_RX_RST,                           0x00},
	{WCD938X_DIGITAL_CDC_RX0_CTL,                          0xFC},
	{WCD938X_DIGITAL_CDC_RX1_CTL,                          0xFC},
	{WCD938X_DIGITAL_CDC_RX2_CTL,                          0xFC},
	{WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1,                  0x00},
	{WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3,                  0x00},
	{WCD938X_DIGITAL_CDC_COMP_CTL_0,                       0x00},
	{WCD938X_DIGITAL_CDC_ANA_TX_CLK_CTL,                   0x1E},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A1_0,                     0x00},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A1_1,                     0x01},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A2_0,                     0x63},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A2_1,                     0x04},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A3_0,                     0xAC},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A3_1,                     0x04},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A4_0,                     0x1A},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A4_1,                     0x03},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A5_0,                     0xBC},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A5_1,                     0x02},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A6_0,                     0xC7},
	{WCD938X_DIGITAL_CDC_HPH_DSM_A7_0,                     0xF8},
	{WCD938X_DIGITAL_CDC_HPH_DSM_C_0,                      0x47},
	{WCD938X_DIGITAL_CDC_HPH_DSM_C_1,                      0x43},
	{WCD938X_DIGITAL_CDC_HPH_DSM_C_2,                      0xB1},
	{WCD938X_DIGITAL_CDC_HPH_DSM_C_3,                      0x17},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R1,                       0x4D},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R2,                       0x29},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R3,                       0x34},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R4,                       0x59},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R5,                       0x66},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R6,                       0x87},
	{WCD938X_DIGITAL_CDC_HPH_DSM_R7,                       0x64},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A1_0,                     0x00},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A1_1,                     0x01},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A2_0,                     0x96},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A2_1,                     0x09},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A3_0,                     0xAB},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A3_1,                     0x05},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A4_0,                     0x1C},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A4_1,                     0x02},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A5_0,                     0x17},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A5_1,                     0x02},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A6_0,                     0xAA},
	{WCD938X_DIGITAL_CDC_AUX_DSM_A7_0,                     0xE3},
	{WCD938X_DIGITAL_CDC_AUX_DSM_C_0,                      0x69},
	{WCD938X_DIGITAL_CDC_AUX_DSM_C_1,                      0x54},
	{WCD938X_DIGITAL_CDC_AUX_DSM_C_2,                      0x02},
	{WCD938X_DIGITAL_CDC_AUX_DSM_C_3,                      0x15},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R1,                       0xA4},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R2,                       0xB5},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R3,                       0x86},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R4,                       0x85},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R5,                       0xAA},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R6,                       0xE2},
	{WCD938X_DIGITAL_CDC_AUX_DSM_R7,                       0x62},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_RX_0,                    0x55},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_RX_1,                    0xA9},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_0,                   0x3D},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_1,                   0x2E},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_2,                   0x01},
	{WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_0,                   0x00},
	{WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_1,                   0xFC},
	{WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_2,                   0x01},
	{WCD938X_DIGITAL_CDC_HPH_GAIN_CTL,                     0x00},
	{WCD938X_DIGITAL_CDC_AUX_GAIN_CTL,                     0x00},
	{WCD938X_DIGITAL_CDC_EAR_PATH_CTL,                     0x00},
	{WCD938X_DIGITAL_CDC_SWR_CLH,                          0x00},
	{WCD938X_DIGITAL_SWR_CLH_BYP,                          0x00},
	{WCD938X_DIGITAL_CDC_TX0_CTL,                          0x68},
	{WCD938X_DIGITAL_CDC_TX1_CTL,                          0x68},
	{WCD938X_DIGITAL_CDC_TX2_CTL,                          0x68},
	{WCD938X_DIGITAL_CDC_TX_RST,                           0x00},
	{WCD938X_DIGITAL_CDC_REQ_CTL,                          0x01},
	{WCD938X_DIGITAL_CDC_RST,                              0x00},
	{WCD938X_DIGITAL_CDC_AMIC_CTL,                         0x0F},
	{WCD938X_DIGITAL_CDC_DMIC_CTL,                         0x04},
	{WCD938X_DIGITAL_CDC_DMIC1_CTL,                        0x01},
	{WCD938X_DIGITAL_CDC_DMIC2_CTL,                        0x01},
	{WCD938X_DIGITAL_CDC_DMIC3_CTL,                        0x01},
	{WCD938X_DIGITAL_CDC_DMIC4_CTL,                        0x01},
	{WCD938X_DIGITAL_EFUSE_PRG_CTL,                        0x00},
	{WCD938X_DIGITAL_EFUSE_CTL,                            0x2B},
	{WCD938X_DIGITAL_CDC_DMIC_RATE_1_2,                    0x11},
	{WCD938X_DIGITAL_CDC_DMIC_RATE_3_4,                    0x11},
	{WCD938X_DIGITAL_PDM_WD_CTL0,                          0x00},
	{WCD938X_DIGITAL_PDM_WD_CTL1,                          0x00},
	{WCD938X_DIGITAL_PDM_WD_CTL2,                          0x00},
	{WCD938X_DIGITAL_INTR_MODE,                            0x00},
	{WCD938X_DIGITAL_INTR_MASK_0,                          0xFF},
	{WCD938X_DIGITAL_INTR_MASK_1,                          0xFF},
	{WCD938X_DIGITAL_INTR_MASK_2,                          0x3F},
	{WCD938X_DIGITAL_INTR_STATUS_0,                        0x00},
	{WCD938X_DIGITAL_INTR_STATUS_1,                        0x00},
	{WCD938X_DIGITAL_INTR_STATUS_2,                        0x00},
	{WCD938X_DIGITAL_INTR_CLEAR_0,                         0x00},
	{WCD938X_DIGITAL_INTR_CLEAR_1,                         0x00},
	{WCD938X_DIGITAL_INTR_CLEAR_2,                         0x00},
	{WCD938X_DIGITAL_INTR_LEVEL_0,                         0x00},
	{WCD938X_DIGITAL_INTR_LEVEL_1,                         0x00},
	{WCD938X_DIGITAL_INTR_LEVEL_2,                         0x00},
	{WCD938X_DIGITAL_INTR_SET_0,                           0x00},
	{WCD938X_DIGITAL_INTR_SET_1,                           0x00},
	{WCD938X_DIGITAL_INTR_SET_2,                           0x00},
	{WCD938X_DIGITAL_INTR_TEST_0,                          0x00},
	{WCD938X_DIGITAL_INTR_TEST_1,                          0x00},
	{WCD938X_DIGITAL_INTR_TEST_2,                          0x00},
	{WCD938X_DIGITAL_TX_MODE_DBG_EN,                       0x00},
	{WCD938X_DIGITAL_TX_MODE_DBG_0_1,                      0x00},
	{WCD938X_DIGITAL_TX_MODE_DBG_2_3,                      0x00},
	{WCD938X_DIGITAL_LB_IN_SEL_CTL,                        0x00},
	{WCD938X_DIGITAL_LOOP_BACK_MODE,                       0x00},
	{WCD938X_DIGITAL_SWR_DAC_TEST,                         0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_RX_0,                     0x40},
	{WCD938X_DIGITAL_SWR_HM_TEST_TX_0,                     0x40},
	{WCD938X_DIGITAL_SWR_HM_TEST_RX_1,                     0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_TX_1,                     0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_TX_2,                     0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_0,                        0x00},
	{WCD938X_DIGITAL_SWR_HM_TEST_1,                        0x00},
	{WCD938X_DIGITAL_PAD_CTL_SWR_0,                        0x8F},
	{WCD938X_DIGITAL_PAD_CTL_SWR_1,                        0x06},
	{WCD938X_DIGITAL_I2C_CTL,                              0x00},
	{WCD938X_DIGITAL_CDC_TX_TANGGU_SW_MODE,                0x00},
	{WCD938X_DIGITAL_EFUSE_TEST_CTL_0,                     0x00},
	{WCD938X_DIGITAL_EFUSE_TEST_CTL_1,                     0x00},
	{WCD938X_DIGITAL_EFUSE_T_DATA_0,                       0x00},
	{WCD938X_DIGITAL_EFUSE_T_DATA_1,                       0x00},
	{WCD938X_DIGITAL_PAD_CTL_PDM_RX0,                      0xF1},
	{WCD938X_DIGITAL_PAD_CTL_PDM_RX1,                      0xF1},
	{WCD938X_DIGITAL_PAD_CTL_PDM_TX0,                      0xF1},
	{WCD938X_DIGITAL_PAD_CTL_PDM_TX1,                      0xF1},
	{WCD938X_DIGITAL_PAD_CTL_PDM_TX2,                      0xF1},
	{WCD938X_DIGITAL_PAD_INP_DIS_0,                        0x00},
	{WCD938X_DIGITAL_PAD_INP_DIS_1,                        0x00},
	{WCD938X_DIGITAL_DRIVE_STRENGTH_0,                     0x00},
	{WCD938X_DIGITAL_DRIVE_STRENGTH_1,                     0x00},
	{WCD938X_DIGITAL_DRIVE_STRENGTH_2,                     0x00},
	{WCD938X_DIGITAL_RX_DATA_EDGE_CTL,                     0x1F},
	{WCD938X_DIGITAL_TX_DATA_EDGE_CTL,                     0x80},
	{WCD938X_DIGITAL_GPIO_MODE,                            0x00},
	{WCD938X_DIGITAL_PIN_CTL_OE,                           0x00},
	{WCD938X_DIGITAL_PIN_CTL_DATA_0,                       0x00},
	{WCD938X_DIGITAL_PIN_CTL_DATA_1,                       0x00},
	{WCD938X_DIGITAL_PIN_STATUS_0,                         0x00},
	{WCD938X_DIGITAL_PIN_STATUS_1,                         0x00},
	{WCD938X_DIGITAL_DIG_DEBUG_CTL,                        0x00},
	{WCD938X_DIGITAL_DIG_DEBUG_EN,                         0x00},
	{WCD938X_DIGITAL_ANA_CSR_DBG_ADD,                      0x00},
	{WCD938X_DIGITAL_ANA_CSR_DBG_CTL,                      0x48},
	{WCD938X_DIGITAL_SSP_DBG,                              0x00},
	{WCD938X_DIGITAL_MODE_STATUS_0,                        0x00},
	{WCD938X_DIGITAL_MODE_STATUS_1,                        0x00},
	{WCD938X_DIGITAL_SPARE_0,                              0x00},
	{WCD938X_DIGITAL_SPARE_1,                              0x00},
	{WCD938X_DIGITAL_SPARE_2,                              0x00},
	{WCD938X_DIGITAL_EFUSE_REG_0,                          0x00},
	{WCD938X_DIGITAL_EFUSE_REG_1,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_2,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_3,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_4,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_5,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_6,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_7,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_8,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_9,                          0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_10,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_11,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_12,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_13,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_14,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_15,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_16,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_17,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_18,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_19,                         0xFF},
	{WCD938X_DIGITAL_EFUSE_REG_20,                         0x0E},
	{WCD938X_DIGITAL_EFUSE_REG_21,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_22,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_23,                         0xF8},
	{WCD938X_DIGITAL_EFUSE_REG_24,                         0x16},
	{WCD938X_DIGITAL_EFUSE_REG_25,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_26,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_27,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_28,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_29,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_30,                         0x00},
	{WCD938X_DIGITAL_EFUSE_REG_31,                         0x00},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_0,                      0x88},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_1,                      0x88},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_2,                      0x88},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_3,                      0x88},
	{WCD938X_DIGITAL_TX_REQ_FB_CTL_4,                      0x88},
	{WCD938X_DIGITAL_DEM_BYPASS_DATA0,                     0x55},
	{WCD938X_DIGITAL_DEM_BYPASS_DATA1,                     0x55},
	{WCD938X_DIGITAL_DEM_BYPASS_DATA2,                     0x55},
	{WCD938X_DIGITAL_DEM_BYPASS_DATA3,                     0x01},
};

static bool wcd938x_rdwr_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WCD938X_ANA_PAGE_REGISTER:
	case WCD938X_ANA_BIAS:
	case WCD938X_ANA_RX_SUPPLIES:
	case WCD938X_ANA_HPH:
	case WCD938X_ANA_EAR:
	case WCD938X_ANA_EAR_COMPANDER_CTL:
	case WCD938X_ANA_TX_CH1:
	case WCD938X_ANA_TX_CH2:
	case WCD938X_ANA_TX_CH3:
	case WCD938X_ANA_TX_CH4:
	case WCD938X_ANA_MICB1_MICB2_DSP_EN_LOGIC:
	case WCD938X_ANA_MICB3_DSP_EN_LOGIC:
	case WCD938X_ANA_MBHC_MECH:
	case WCD938X_ANA_MBHC_ELECT:
	case WCD938X_ANA_MBHC_ZDET:
	case WCD938X_ANA_MBHC_BTN0:
	case WCD938X_ANA_MBHC_BTN1:
	case WCD938X_ANA_MBHC_BTN2:
	case WCD938X_ANA_MBHC_BTN3:
	case WCD938X_ANA_MBHC_BTN4:
	case WCD938X_ANA_MBHC_BTN5:
	case WCD938X_ANA_MBHC_BTN6:
	case WCD938X_ANA_MBHC_BTN7:
	case WCD938X_ANA_MICB1:
	case WCD938X_ANA_MICB2:
	case WCD938X_ANA_MICB2_RAMP:
	case WCD938X_ANA_MICB3:
	case WCD938X_ANA_MICB4:
	case WCD938X_BIAS_CTL:
	case WCD938X_BIAS_VBG_FINE_ADJ:
	case WCD938X_LDOL_VDDCX_ADJUST:
	case WCD938X_LDOL_DISABLE_LDOL:
	case WCD938X_MBHC_CTL_CLK:
	case WCD938X_MBHC_CTL_ANA:
	case WCD938X_MBHC_CTL_SPARE_1:
	case WCD938X_MBHC_CTL_SPARE_2:
	case WCD938X_MBHC_CTL_BCS:
	case WCD938X_MBHC_TEST_CTL:
	case WCD938X_LDOH_MODE:
	case WCD938X_LDOH_BIAS:
	case WCD938X_LDOH_STB_LOADS:
	case WCD938X_LDOH_SLOWRAMP:
	case WCD938X_MICB1_TEST_CTL_1:
	case WCD938X_MICB1_TEST_CTL_2:
	case WCD938X_MICB1_TEST_CTL_3:
	case WCD938X_MICB2_TEST_CTL_1:
	case WCD938X_MICB2_TEST_CTL_2:
	case WCD938X_MICB2_TEST_CTL_3:
	case WCD938X_MICB3_TEST_CTL_1:
	case WCD938X_MICB3_TEST_CTL_2:
	case WCD938X_MICB3_TEST_CTL_3:
	case WCD938X_MICB4_TEST_CTL_1:
	case WCD938X_MICB4_TEST_CTL_2:
	case WCD938X_MICB4_TEST_CTL_3:
	case WCD938X_TX_COM_ADC_VCM:
	case WCD938X_TX_COM_BIAS_ATEST:
	case WCD938X_TX_COM_SPARE1:
	case WCD938X_TX_COM_SPARE2:
	case WCD938X_TX_COM_TXFE_DIV_CTL:
	case WCD938X_TX_COM_TXFE_DIV_START:
	case WCD938X_TX_COM_SPARE3:
	case WCD938X_TX_COM_SPARE4:
	case WCD938X_TX_1_2_TEST_EN:
	case WCD938X_TX_1_2_ADC_IB:
	case WCD938X_TX_1_2_ATEST_REFCTL:
	case WCD938X_TX_1_2_TEST_CTL:
	case WCD938X_TX_1_2_TEST_BLK_EN1:
	case WCD938X_TX_1_2_TXFE1_CLKDIV:
	case WCD938X_TX_3_4_TEST_EN:
	case WCD938X_TX_3_4_ADC_IB:
	case WCD938X_TX_3_4_ATEST_REFCTL:
	case WCD938X_TX_3_4_TEST_CTL:
	case WCD938X_TX_3_4_TEST_BLK_EN3:
	case WCD938X_TX_3_4_TXFE3_CLKDIV:
	case WCD938X_TX_3_4_TEST_BLK_EN2:
	case WCD938X_TX_3_4_TXFE2_CLKDIV:
	case WCD938X_TX_3_4_SPARE1:
	case WCD938X_TX_3_4_TEST_BLK_EN4:
	case WCD938X_TX_3_4_TXFE4_CLKDIV:
	case WCD938X_TX_3_4_SPARE2:
	case WCD938X_CLASSH_MODE_1:
	case WCD938X_CLASSH_MODE_2:
	case WCD938X_CLASSH_MODE_3:
	case WCD938X_CLASSH_CTRL_VCL_1:
	case WCD938X_CLASSH_CTRL_VCL_2:
	case WCD938X_CLASSH_CTRL_CCL_1:
	case WCD938X_CLASSH_CTRL_CCL_2:
	case WCD938X_CLASSH_CTRL_CCL_3:
	case WCD938X_CLASSH_CTRL_CCL_4:
	case WCD938X_CLASSH_CTRL_CCL_5:
	case WCD938X_CLASSH_BUCK_TMUX_A_D:
	case WCD938X_CLASSH_BUCK_SW_DRV_CNTL:
	case WCD938X_CLASSH_SPARE:
	case WCD938X_FLYBACK_EN:
	case WCD938X_FLYBACK_VNEG_CTRL_1:
	case WCD938X_FLYBACK_VNEG_CTRL_2:
	case WCD938X_FLYBACK_VNEG_CTRL_3:
	case WCD938X_FLYBACK_VNEG_CTRL_4:
	case WCD938X_FLYBACK_VNEG_CTRL_5:
	case WCD938X_FLYBACK_VNEG_CTRL_6:
	case WCD938X_FLYBACK_VNEG_CTRL_7:
	case WCD938X_FLYBACK_VNEG_CTRL_8:
	case WCD938X_FLYBACK_VNEG_CTRL_9:
	case WCD938X_FLYBACK_VNEGDAC_CTRL_1:
	case WCD938X_FLYBACK_VNEGDAC_CTRL_2:
	case WCD938X_FLYBACK_VNEGDAC_CTRL_3:
	case WCD938X_FLYBACK_CTRL_1:
	case WCD938X_FLYBACK_TEST_CTL:
	case WCD938X_RX_AUX_SW_CTL:
	case WCD938X_RX_PA_AUX_IN_CONN:
	case WCD938X_RX_TIMER_DIV:
	case WCD938X_RX_OCP_CTL:
	case WCD938X_RX_OCP_COUNT:
	case WCD938X_RX_BIAS_EAR_DAC:
	case WCD938X_RX_BIAS_EAR_AMP:
	case WCD938X_RX_BIAS_HPH_LDO:
	case WCD938X_RX_BIAS_HPH_PA:
	case WCD938X_RX_BIAS_HPH_RDACBUFF_CNP2:
	case WCD938X_RX_BIAS_HPH_RDAC_LDO:
	case WCD938X_RX_BIAS_HPH_CNP1:
	case WCD938X_RX_BIAS_HPH_LOWPOWER:
	case WCD938X_RX_BIAS_AUX_DAC:
	case WCD938X_RX_BIAS_AUX_AMP:
	case WCD938X_RX_BIAS_VNEGDAC_BLEEDER:
	case WCD938X_RX_BIAS_MISC:
	case WCD938X_RX_BIAS_BUCK_RST:
	case WCD938X_RX_BIAS_BUCK_VREF_ERRAMP:
	case WCD938X_RX_BIAS_FLYB_ERRAMP:
	case WCD938X_RX_BIAS_FLYB_BUFF:
	case WCD938X_RX_BIAS_FLYB_MID_RST:
	case WCD938X_HPH_CNP_EN:
	case WCD938X_HPH_CNP_WG_CTL:
	case WCD938X_HPH_CNP_WG_TIME:
	case WCD938X_HPH_OCP_CTL:
	case WCD938X_HPH_AUTO_CHOP:
	case WCD938X_HPH_CHOP_CTL:
	case WCD938X_HPH_PA_CTL1:
	case WCD938X_HPH_PA_CTL2:
	case WCD938X_HPH_L_EN:
	case WCD938X_HPH_L_TEST:
	case WCD938X_HPH_L_ATEST:
	case WCD938X_HPH_R_EN:
	case WCD938X_HPH_R_TEST:
	case WCD938X_HPH_R_ATEST:
	case WCD938X_HPH_RDAC_CLK_CTL1:
	case WCD938X_HPH_RDAC_CLK_CTL2:
	case WCD938X_HPH_RDAC_LDO_CTL:
	case WCD938X_HPH_RDAC_CHOP_CLK_LP_CTL:
	case WCD938X_HPH_REFBUFF_UHQA_CTL:
	case WCD938X_HPH_REFBUFF_LP_CTL:
	case WCD938X_HPH_L_DAC_CTL:
	case WCD938X_HPH_R_DAC_CTL:
	case WCD938X_HPH_SURGE_HPHLR_SURGE_COMP_SEL:
	case WCD938X_HPH_SURGE_HPHLR_SURGE_EN:
	case WCD938X_HPH_SURGE_HPHLR_SURGE_MISC1:
	case WCD938X_EAR_EAR_EN_REG:
	case WCD938X_EAR_EAR_PA_CON:
	case WCD938X_EAR_EAR_SP_CON:
	case WCD938X_EAR_EAR_DAC_CON:
	case WCD938X_EAR_EAR_CNP_FSM_CON:
	case WCD938X_EAR_TEST_CTL:
	case WCD938X_ANA_NEW_PAGE_REGISTER:
	case WCD938X_HPH_NEW_ANA_HPH2:
	case WCD938X_HPH_NEW_ANA_HPH3:
	case WCD938X_SLEEP_CTL:
	case WCD938X_SLEEP_WATCHDOG_CTL:
	case WCD938X_MBHC_NEW_ELECT_REM_CLAMP_CTL:
	case WCD938X_MBHC_NEW_CTL_1:
	case WCD938X_MBHC_NEW_CTL_2:
	case WCD938X_MBHC_NEW_PLUG_DETECT_CTL:
	case WCD938X_MBHC_NEW_ZDET_ANA_CTL:
	case WCD938X_MBHC_NEW_ZDET_RAMP_CTL:
	case WCD938X_TX_NEW_AMIC_MUX_CFG:
	case WCD938X_AUX_AUXPA:
	case WCD938X_LDORXTX_MODE:
	case WCD938X_LDORXTX_CONFIG:
	case WCD938X_DIE_CRACK_DIE_CRK_DET_EN:
	case WCD938X_HPH_NEW_INT_RDAC_GAIN_CTL:
	case WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L:
	case WCD938X_HPH_NEW_INT_RDAC_VREF_CTL:
	case WCD938X_HPH_NEW_INT_RDAC_OVERRIDE_CTL:
	case WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R:
	case WCD938X_HPH_NEW_INT_PA_MISC1:
	case WCD938X_HPH_NEW_INT_PA_MISC2:
	case WCD938X_HPH_NEW_INT_PA_RDAC_MISC:
	case WCD938X_HPH_NEW_INT_HPH_TIMER1:
	case WCD938X_HPH_NEW_INT_HPH_TIMER2:
	case WCD938X_HPH_NEW_INT_HPH_TIMER3:
	case WCD938X_HPH_NEW_INT_HPH_TIMER4:
	case WCD938X_HPH_NEW_INT_PA_RDAC_MISC2:
	case WCD938X_HPH_NEW_INT_PA_RDAC_MISC3:
	case WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L_NEW:
	case WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R_NEW:
	case WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_LOHIFI:
	case WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_ULP:
	case WCD938X_RX_NEW_INT_HPH_RDAC_LDO_LP:
	case WCD938X_MBHC_NEW_INT_MOISTURE_DET_DC_CTRL:
	case WCD938X_MBHC_NEW_INT_MOISTURE_DET_POLLING_CTRL:
	case WCD938X_MBHC_NEW_INT_MECH_DET_CURRENT:
	case WCD938X_MBHC_NEW_INT_SPARE_2:
	case WCD938X_EAR_INT_NEW_EAR_CHOPPER_CON:
	case WCD938X_EAR_INT_NEW_CNP_VCM_CON1:
	case WCD938X_EAR_INT_NEW_CNP_VCM_CON2:
	case WCD938X_EAR_INT_NEW_EAR_DYNAMIC_BIAS:
	case WCD938X_AUX_INT_EN_REG:
	case WCD938X_AUX_INT_PA_CTRL:
	case WCD938X_AUX_INT_SP_CTRL:
	case WCD938X_AUX_INT_DAC_CTRL:
	case WCD938X_AUX_INT_CLK_CTRL:
	case WCD938X_AUX_INT_TEST_CTRL:
	case WCD938X_AUX_INT_MISC:
	case WCD938X_LDORXTX_INT_BIAS:
	case WCD938X_LDORXTX_INT_STB_LOADS_DTEST:
	case WCD938X_LDORXTX_INT_TEST0:
	case WCD938X_LDORXTX_INT_STARTUP_TIMER:
	case WCD938X_LDORXTX_INT_TEST1:
	case WCD938X_SLEEP_INT_WATCHDOG_CTL_1:
	case WCD938X_SLEEP_INT_WATCHDOG_CTL_2:
	case WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT1:
	case WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT2:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L2:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L1:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L0:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP1P2M:
	case WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP0P6M:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L2L1:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L0:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_ULP:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L2L1:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L0:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_ULP:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_L2L1L0:
	case WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_ULP:
	case WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L2L1:
	case WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L0ULP:
	case WCD938X_TX_COM_NEW_INT_TXADC_INT_L2:
	case WCD938X_TX_COM_NEW_INT_TXADC_INT_L1:
	case WCD938X_TX_COM_NEW_INT_TXADC_INT_L0:
	case WCD938X_TX_COM_NEW_INT_TXADC_INT_ULP:
	case WCD938X_DIGITAL_PAGE_REGISTER:
	case WCD938X_DIGITAL_SWR_TX_CLK_RATE:
	case WCD938X_DIGITAL_CDC_RST_CTL:
	case WCD938X_DIGITAL_TOP_CLK_CFG:
	case WCD938X_DIGITAL_CDC_ANA_CLK_CTL:
	case WCD938X_DIGITAL_CDC_DIG_CLK_CTL:
	case WCD938X_DIGITAL_SWR_RST_EN:
	case WCD938X_DIGITAL_CDC_PATH_MODE:
	case WCD938X_DIGITAL_CDC_RX_RST:
	case WCD938X_DIGITAL_CDC_RX0_CTL:
	case WCD938X_DIGITAL_CDC_RX1_CTL:
	case WCD938X_DIGITAL_CDC_RX2_CTL:
	case WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1:
	case WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3:
	case WCD938X_DIGITAL_CDC_COMP_CTL_0:
	case WCD938X_DIGITAL_CDC_ANA_TX_CLK_CTL:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A1_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A1_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A2_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A2_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A3_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A3_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A4_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A4_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A5_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A5_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A6_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_A7_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_C_0:
	case WCD938X_DIGITAL_CDC_HPH_DSM_C_1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_C_2:
	case WCD938X_DIGITAL_CDC_HPH_DSM_C_3:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R1:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R2:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R3:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R4:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R5:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R6:
	case WCD938X_DIGITAL_CDC_HPH_DSM_R7:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A1_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A1_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A2_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A2_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A3_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A3_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A4_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A4_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A5_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A5_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A6_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_A7_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_C_0:
	case WCD938X_DIGITAL_CDC_AUX_DSM_C_1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_C_2:
	case WCD938X_DIGITAL_CDC_AUX_DSM_C_3:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R1:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R2:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R3:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R4:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R5:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R6:
	case WCD938X_DIGITAL_CDC_AUX_DSM_R7:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_RX_0:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_RX_1:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_0:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_1:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_2:
	case WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_0:
	case WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_1:
	case WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_2:
	case WCD938X_DIGITAL_CDC_HPH_GAIN_CTL:
	case WCD938X_DIGITAL_CDC_AUX_GAIN_CTL:
	case WCD938X_DIGITAL_CDC_EAR_PATH_CTL:
	case WCD938X_DIGITAL_CDC_SWR_CLH:
	case WCD938X_DIGITAL_SWR_CLH_BYP:
	case WCD938X_DIGITAL_CDC_TX0_CTL:
	case WCD938X_DIGITAL_CDC_TX1_CTL:
	case WCD938X_DIGITAL_CDC_TX2_CTL:
	case WCD938X_DIGITAL_CDC_TX_RST:
	case WCD938X_DIGITAL_CDC_REQ_CTL:
	case WCD938X_DIGITAL_CDC_RST:
	case WCD938X_DIGITAL_CDC_AMIC_CTL:
	case WCD938X_DIGITAL_CDC_DMIC_CTL:
	case WCD938X_DIGITAL_CDC_DMIC1_CTL:
	case WCD938X_DIGITAL_CDC_DMIC2_CTL:
	case WCD938X_DIGITAL_CDC_DMIC3_CTL:
	case WCD938X_DIGITAL_CDC_DMIC4_CTL:
	case WCD938X_DIGITAL_EFUSE_PRG_CTL:
	case WCD938X_DIGITAL_EFUSE_CTL:
	case WCD938X_DIGITAL_CDC_DMIC_RATE_1_2:
	case WCD938X_DIGITAL_CDC_DMIC_RATE_3_4:
	case WCD938X_DIGITAL_PDM_WD_CTL0:
	case WCD938X_DIGITAL_PDM_WD_CTL1:
	case WCD938X_DIGITAL_PDM_WD_CTL2:
	case WCD938X_DIGITAL_INTR_MODE:
	case WCD938X_DIGITAL_INTR_MASK_0:
	case WCD938X_DIGITAL_INTR_MASK_1:
	case WCD938X_DIGITAL_INTR_MASK_2:
	case WCD938X_DIGITAL_INTR_CLEAR_0:
	case WCD938X_DIGITAL_INTR_CLEAR_1:
	case WCD938X_DIGITAL_INTR_CLEAR_2:
	case WCD938X_DIGITAL_INTR_LEVEL_0:
	case WCD938X_DIGITAL_INTR_LEVEL_1:
	case WCD938X_DIGITAL_INTR_LEVEL_2:
	case WCD938X_DIGITAL_INTR_SET_0:
	case WCD938X_DIGITAL_INTR_SET_1:
	case WCD938X_DIGITAL_INTR_SET_2:
	case WCD938X_DIGITAL_INTR_TEST_0:
	case WCD938X_DIGITAL_INTR_TEST_1:
	case WCD938X_DIGITAL_INTR_TEST_2:
	case WCD938X_DIGITAL_TX_MODE_DBG_EN:
	case WCD938X_DIGITAL_TX_MODE_DBG_0_1:
	case WCD938X_DIGITAL_TX_MODE_DBG_2_3:
	case WCD938X_DIGITAL_LB_IN_SEL_CTL:
	case WCD938X_DIGITAL_LOOP_BACK_MODE:
	case WCD938X_DIGITAL_SWR_DAC_TEST:
	case WCD938X_DIGITAL_SWR_HM_TEST_RX_0:
	case WCD938X_DIGITAL_SWR_HM_TEST_TX_0:
	case WCD938X_DIGITAL_SWR_HM_TEST_RX_1:
	case WCD938X_DIGITAL_SWR_HM_TEST_TX_1:
	case WCD938X_DIGITAL_SWR_HM_TEST_TX_2:
	case WCD938X_DIGITAL_PAD_CTL_SWR_0:
	case WCD938X_DIGITAL_PAD_CTL_SWR_1:
	case WCD938X_DIGITAL_I2C_CTL:
	case WCD938X_DIGITAL_CDC_TX_TANGGU_SW_MODE:
	case WCD938X_DIGITAL_EFUSE_TEST_CTL_0:
	case WCD938X_DIGITAL_EFUSE_TEST_CTL_1:
	case WCD938X_DIGITAL_PAD_CTL_PDM_RX0:
	case WCD938X_DIGITAL_PAD_CTL_PDM_RX1:
	case WCD938X_DIGITAL_PAD_CTL_PDM_TX0:
	case WCD938X_DIGITAL_PAD_CTL_PDM_TX1:
	case WCD938X_DIGITAL_PAD_CTL_PDM_TX2:
	case WCD938X_DIGITAL_PAD_INP_DIS_0:
	case WCD938X_DIGITAL_PAD_INP_DIS_1:
	case WCD938X_DIGITAL_DRIVE_STRENGTH_0:
	case WCD938X_DIGITAL_DRIVE_STRENGTH_1:
	case WCD938X_DIGITAL_DRIVE_STRENGTH_2:
	case WCD938X_DIGITAL_RX_DATA_EDGE_CTL:
	case WCD938X_DIGITAL_TX_DATA_EDGE_CTL:
	case WCD938X_DIGITAL_GPIO_MODE:
	case WCD938X_DIGITAL_PIN_CTL_OE:
	case WCD938X_DIGITAL_PIN_CTL_DATA_0:
	case WCD938X_DIGITAL_PIN_CTL_DATA_1:
	case WCD938X_DIGITAL_DIG_DEBUG_CTL:
	case WCD938X_DIGITAL_DIG_DEBUG_EN:
	case WCD938X_DIGITAL_ANA_CSR_DBG_ADD:
	case WCD938X_DIGITAL_ANA_CSR_DBG_CTL:
	case WCD938X_DIGITAL_SSP_DBG:
	case WCD938X_DIGITAL_SPARE_0:
	case WCD938X_DIGITAL_SPARE_1:
	case WCD938X_DIGITAL_SPARE_2:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_0:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_1:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_2:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_3:
	case WCD938X_DIGITAL_TX_REQ_FB_CTL_4:
	case WCD938X_DIGITAL_DEM_BYPASS_DATA0:
	case WCD938X_DIGITAL_DEM_BYPASS_DATA1:
	case WCD938X_DIGITAL_DEM_BYPASS_DATA2:
	case WCD938X_DIGITAL_DEM_BYPASS_DATA3:
		return true;
	}

	return false;
}

static bool wcd938x_readonly_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WCD938X_ANA_MBHC_RESULT_1:
	case WCD938X_ANA_MBHC_RESULT_2:
	case WCD938X_ANA_MBHC_RESULT_3:
	case WCD938X_MBHC_MOISTURE_DET_FSM_STATUS:
	case WCD938X_TX_1_2_SAR2_ERR:
	case WCD938X_TX_1_2_SAR1_ERR:
	case WCD938X_TX_3_4_SAR4_ERR:
	case WCD938X_TX_3_4_SAR3_ERR:
	case WCD938X_HPH_L_STATUS:
	case WCD938X_HPH_R_STATUS:
	case WCD938X_HPH_SURGE_HPHLR_SURGE_STATUS:
	case WCD938X_EAR_STATUS_REG_1:
	case WCD938X_EAR_STATUS_REG_2:
	case WCD938X_MBHC_NEW_FSM_STATUS:
	case WCD938X_MBHC_NEW_ADC_RESULT:
	case WCD938X_DIE_CRACK_DIE_CRK_DET_OUT:
	case WCD938X_AUX_INT_STATUS_REG:
	case WCD938X_LDORXTX_INT_STATUS:
	case WCD938X_DIGITAL_CHIP_ID0:
	case WCD938X_DIGITAL_CHIP_ID1:
	case WCD938X_DIGITAL_CHIP_ID2:
	case WCD938X_DIGITAL_CHIP_ID3:
	case WCD938X_DIGITAL_INTR_STATUS_0:
	case WCD938X_DIGITAL_INTR_STATUS_1:
	case WCD938X_DIGITAL_INTR_STATUS_2:
	case WCD938X_DIGITAL_INTR_CLEAR_0:
	case WCD938X_DIGITAL_INTR_CLEAR_1:
	case WCD938X_DIGITAL_INTR_CLEAR_2:
	case WCD938X_DIGITAL_SWR_HM_TEST_0:
	case WCD938X_DIGITAL_SWR_HM_TEST_1:
	case WCD938X_DIGITAL_EFUSE_T_DATA_0:
	case WCD938X_DIGITAL_EFUSE_T_DATA_1:
	case WCD938X_DIGITAL_PIN_STATUS_0:
	case WCD938X_DIGITAL_PIN_STATUS_1:
	case WCD938X_DIGITAL_MODE_STATUS_0:
	case WCD938X_DIGITAL_MODE_STATUS_1:
	case WCD938X_DIGITAL_EFUSE_REG_0:
	case WCD938X_DIGITAL_EFUSE_REG_1:
	case WCD938X_DIGITAL_EFUSE_REG_2:
	case WCD938X_DIGITAL_EFUSE_REG_3:
	case WCD938X_DIGITAL_EFUSE_REG_4:
	case WCD938X_DIGITAL_EFUSE_REG_5:
	case WCD938X_DIGITAL_EFUSE_REG_6:
	case WCD938X_DIGITAL_EFUSE_REG_7:
	case WCD938X_DIGITAL_EFUSE_REG_8:
	case WCD938X_DIGITAL_EFUSE_REG_9:
	case WCD938X_DIGITAL_EFUSE_REG_10:
	case WCD938X_DIGITAL_EFUSE_REG_11:
	case WCD938X_DIGITAL_EFUSE_REG_12:
	case WCD938X_DIGITAL_EFUSE_REG_13:
	case WCD938X_DIGITAL_EFUSE_REG_14:
	case WCD938X_DIGITAL_EFUSE_REG_15:
	case WCD938X_DIGITAL_EFUSE_REG_16:
	case WCD938X_DIGITAL_EFUSE_REG_17:
	case WCD938X_DIGITAL_EFUSE_REG_18:
	case WCD938X_DIGITAL_EFUSE_REG_19:
	case WCD938X_DIGITAL_EFUSE_REG_20:
	case WCD938X_DIGITAL_EFUSE_REG_21:
	case WCD938X_DIGITAL_EFUSE_REG_22:
	case WCD938X_DIGITAL_EFUSE_REG_23:
	case WCD938X_DIGITAL_EFUSE_REG_24:
	case WCD938X_DIGITAL_EFUSE_REG_25:
	case WCD938X_DIGITAL_EFUSE_REG_26:
	case WCD938X_DIGITAL_EFUSE_REG_27:
	case WCD938X_DIGITAL_EFUSE_REG_28:
	case WCD938X_DIGITAL_EFUSE_REG_29:
	case WCD938X_DIGITAL_EFUSE_REG_30:
	case WCD938X_DIGITAL_EFUSE_REG_31:
		return true;
	}
	return false;
}

static bool wcd938x_readable_register(struct device *dev, unsigned int reg)
{
	bool ret;

	ret = wcd938x_readonly_register(dev, reg);
	if (!ret)
		return wcd938x_rdwr_register(dev, reg);

	return ret;
}

static bool wcd938x_writeable_register(struct device *dev, unsigned int reg)
{
	return wcd938x_rdwr_register(dev, reg);
}

static bool wcd938x_volatile_register(struct device *dev, unsigned int reg)
{
	if (reg <= WCD938X_BASE_ADDRESS)
		return false;

	if (reg == WCD938X_DIGITAL_SWR_TX_CLK_RATE)
		return true;

	if (wcd938x_readonly_register(dev, reg))
		return true;

	return false;
}

static struct regmap_config wcd938x_regmap_config = {
	.name = "wcd938x_csr",
	.reg_bits = 32,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wcd938x_defaults,
	.num_reg_defaults = ARRAY_SIZE(wcd938x_defaults),
	.max_register = WCD938X_MAX_REGISTER,
	.readable_reg = wcd938x_readable_register,
	.writeable_reg = wcd938x_writeable_register,
	.volatile_reg = wcd938x_volatile_register,
	.can_multi_write = true,
};

static const struct regmap_irq wcd938x_irqs[WCD938X_NUM_IRQS] = {
	REGMAP_IRQ_REG(WCD938X_IRQ_MBHC_BUTTON_PRESS_DET, 0, 0x01),
	REGMAP_IRQ_REG(WCD938X_IRQ_MBHC_BUTTON_RELEASE_DET, 0, 0x02),
	REGMAP_IRQ_REG(WCD938X_IRQ_MBHC_ELECT_INS_REM_DET, 0, 0x04),
	REGMAP_IRQ_REG(WCD938X_IRQ_MBHC_ELECT_INS_REM_LEG_DET, 0, 0x08),
	REGMAP_IRQ_REG(WCD938X_IRQ_MBHC_SW_DET, 0, 0x10),
	REGMAP_IRQ_REG(WCD938X_IRQ_HPHR_OCP_INT, 0, 0x20),
	REGMAP_IRQ_REG(WCD938X_IRQ_HPHR_CNP_INT, 0, 0x40),
	REGMAP_IRQ_REG(WCD938X_IRQ_HPHL_OCP_INT, 0, 0x80),
	REGMAP_IRQ_REG(WCD938X_IRQ_HPHL_CNP_INT, 1, 0x01),
	REGMAP_IRQ_REG(WCD938X_IRQ_EAR_CNP_INT, 1, 0x02),
	REGMAP_IRQ_REG(WCD938X_IRQ_EAR_SCD_INT, 1, 0x04),
	REGMAP_IRQ_REG(WCD938X_IRQ_AUX_CNP_INT, 1, 0x08),
	REGMAP_IRQ_REG(WCD938X_IRQ_AUX_SCD_INT, 1, 0x10),
	REGMAP_IRQ_REG(WCD938X_IRQ_HPHL_PDM_WD_INT, 1, 0x20),
	REGMAP_IRQ_REG(WCD938X_IRQ_HPHR_PDM_WD_INT, 1, 0x40),
	REGMAP_IRQ_REG(WCD938X_IRQ_AUX_PDM_WD_INT, 1, 0x80),
	REGMAP_IRQ_REG(WCD938X_IRQ_LDORT_SCD_INT, 2, 0x01),
	REGMAP_IRQ_REG(WCD938X_IRQ_MBHC_MOISTURE_INT, 2, 0x02),
	REGMAP_IRQ_REG(WCD938X_IRQ_HPHL_SURGE_DET_INT, 2, 0x04),
	REGMAP_IRQ_REG(WCD938X_IRQ_HPHR_SURGE_DET_INT, 2, 0x08),
};

static struct regmap_irq_chip wcd938x_regmap_irq_chip = {
	.name = "wcd938x",
	.irqs = wcd938x_irqs,
	.num_irqs = ARRAY_SIZE(wcd938x_irqs),
	.num_regs = 3,
	.status_base = WCD938X_DIGITAL_INTR_STATUS_0,
	.mask_base = WCD938X_DIGITAL_INTR_MASK_0,
	.ack_base = WCD938X_DIGITAL_INTR_CLEAR_0,
	.use_ack = 1,
	.runtime_pm = true,
	.irq_drv_data = NULL,
};

static int wcd938x_get_clk_rate(int mode)
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

static int wcd938x_set_swr_clk_rate(struct snd_soc_component *component, int rate, int bank)
{
	u8 mask = (bank ? 0xF0 : 0x0F);
	u8 val = 0;

	switch (rate) {
	case SWR_CLK_RATE_0P6MHZ:
		val = (bank ? 0x60 : 0x06);
		break;
	case SWR_CLK_RATE_1P2MHZ:
		val = (bank ? 0x50 : 0x05);
		break;
	case SWR_CLK_RATE_2P4MHZ:
		val = (bank ? 0x30 : 0x03);
		break;
	case SWR_CLK_RATE_4P8MHZ:
		val = (bank ? 0x10 : 0x01);
		break;
	case SWR_CLK_RATE_9P6MHZ:
	default:
		val = 0x00;
		break;
	}
	snd_soc_component_update_bits(component, WCD938X_DIGITAL_SWR_TX_CLK_RATE,
				      mask, val);

	return 0;
}

static int wcd938x_io_init(struct wcd938x_priv *wcd938x)
{
	struct regmap *rm = wcd938x->regmap;

	regmap_update_bits(rm, WCD938X_SLEEP_CTL, 0x0E, 0x0E);
	regmap_update_bits(rm, WCD938X_SLEEP_CTL, 0x80, 0x80);
	/* 1 msec delay as per HW requirement */
	usleep_range(1000, 1010);
	regmap_update_bits(rm, WCD938X_SLEEP_CTL, 0x40, 0x40);
	/* 1 msec delay as per HW requirement */
	usleep_range(1000, 1010);
	regmap_update_bits(rm, WCD938X_LDORXTX_CONFIG, 0x10, 0x00);
	regmap_update_bits(rm, WCD938X_BIAS_VBG_FINE_ADJ,
								0xF0, 0x80);
	regmap_update_bits(rm, WCD938X_ANA_BIAS, 0x80, 0x80);
	regmap_update_bits(rm, WCD938X_ANA_BIAS, 0x40, 0x40);
	/* 10 msec delay as per HW requirement */
	usleep_range(10000, 10010);

	regmap_update_bits(rm, WCD938X_ANA_BIAS, 0x40, 0x00);
	regmap_update_bits(rm, WCD938X_HPH_NEW_INT_RDAC_GAIN_CTL,
				      0xF0, 0x00);
	regmap_update_bits(rm, WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L_NEW,
				      0x1F, 0x15);
	regmap_update_bits(rm, WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R_NEW,
				      0x1F, 0x15);
	regmap_update_bits(rm, WCD938X_HPH_REFBUFF_UHQA_CTL,
				      0xC0, 0x80);
	regmap_update_bits(rm, WCD938X_DIGITAL_CDC_DMIC_CTL,
				      0x02, 0x02);

	regmap_update_bits(rm, WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_ULP,
			   0xFF, 0x14);
	regmap_update_bits(rm, WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_ULP,
			   0x1F, 0x08);

	regmap_update_bits(rm, WCD938X_DIGITAL_TX_REQ_FB_CTL_0, 0xFF, 0x55);
	regmap_update_bits(rm, WCD938X_DIGITAL_TX_REQ_FB_CTL_1, 0xFF, 0x44);
	regmap_update_bits(rm, WCD938X_DIGITAL_TX_REQ_FB_CTL_2, 0xFF, 0x11);
	regmap_update_bits(rm, WCD938X_DIGITAL_TX_REQ_FB_CTL_3, 0xFF, 0x00);
	regmap_update_bits(rm, WCD938X_DIGITAL_TX_REQ_FB_CTL_4, 0xFF, 0x00);

	/* Set Noise Filter Resistor value */
	regmap_update_bits(rm, WCD938X_MICB1_TEST_CTL_1, 0xE0, 0xE0);
	regmap_update_bits(rm, WCD938X_MICB2_TEST_CTL_1, 0xE0, 0xE0);
	regmap_update_bits(rm, WCD938X_MICB3_TEST_CTL_1, 0xE0, 0xE0);
	regmap_update_bits(rm, WCD938X_MICB4_TEST_CTL_1, 0xE0, 0xE0);

	regmap_update_bits(rm, WCD938X_TX_3_4_TEST_BLK_EN2, 0x01, 0x00);
	regmap_update_bits(rm, WCD938X_HPH_SURGE_HPHLR_SURGE_EN, 0xC0, 0xC0);

	return 0;

}

static int wcd938x_sdw_connect_port(struct wcd938x_sdw_ch_info *ch_info,
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

static int wcd938x_connect_port(struct wcd938x_sdw_priv *wcd, u8 port_num, u8 ch_id, u8 enable)
{
	return wcd938x_sdw_connect_port(&wcd->ch_info[ch_id],
					&wcd->port_config[port_num - 1],
					enable);
}

static int wcd938x_codec_enable_rxclk(struct snd_soc_dapm_widget *w,
				      struct snd_kcontrol *kcontrol,
				      int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
				WCD938X_ANA_RX_CLK_EN_MASK, 1);
		snd_soc_component_write_field(component, WCD938X_ANA_RX_SUPPLIES,
				WCD938X_RX_BIAS_EN_MASK, 1);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_RX0_CTL,
				WCD938X_DEM_DITHER_ENABLE_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_RX1_CTL,
				WCD938X_DEM_DITHER_ENABLE_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_RX2_CTL,
				WCD938X_DEM_DITHER_ENABLE_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
				WCD938X_ANA_RX_DIV2_CLK_EN_MASK, 1);
		snd_soc_component_write_field(component, WCD938X_AUX_AUXPA,
					      WCD938X_AUXPA_CLK_EN_MASK, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD938X_ANA_RX_SUPPLIES,
				WCD938X_VNEG_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_ANA_RX_SUPPLIES,
				WCD938X_VPOS_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_ANA_RX_SUPPLIES,
				WCD938X_RX_BIAS_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
				WCD938X_ANA_RX_DIV2_CLK_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
				WCD938X_ANA_RX_CLK_EN_MASK, 0);
		break;
	}
	return 0;
}

static int wcd938x_codec_hphl_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_RXD0_CLK_EN_MASK, 0x01);
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_HPH_GAIN_CTL,
				WCD938X_HPHL_RX_EN_MASK, 1);
		snd_soc_component_write_field(component,
				WCD938X_HPH_RDAC_CLK_CTL1,
				WCD938X_CHOP_CLK_EN_MASK, 0);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write_field(component,
				WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L,
				WCD938X_HPH_RES_DIV_MASK, 0x02);
		if (wcd938x->comp1_enable) {
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_COMP_CTL_0,
				WCD938X_HPHL_COMP_EN_MASK, 1);
			/* 5msec compander delay as per HW requirement */
			if (!wcd938x->comp2_enable || (snd_soc_component_read(component,
							 WCD938X_DIGITAL_CDC_COMP_CTL_0) & 0x01))
				usleep_range(5000, 5010);
			snd_soc_component_write_field(component, WCD938X_HPH_NEW_INT_HPH_TIMER1,
					      WCD938X_AUTOCHOP_TIMER_EN, 0);
		} else {
			snd_soc_component_write_field(component,
					WCD938X_DIGITAL_CDC_COMP_CTL_0,
					WCD938X_HPHL_COMP_EN_MASK, 0);
			snd_soc_component_write_field(component,
					WCD938X_HPH_L_EN,
					WCD938X_GAIN_SRC_SEL_MASK,
					WCD938X_GAIN_SRC_SEL_REGISTER);

		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component,
			WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R,
			WCD938X_HPH_RES_DIV_MASK, 0x1);
		break;
	}

	return 0;
}

static int wcd938x_codec_hphr_dac_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_RXD1_CLK_EN_MASK, 1);
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_HPH_GAIN_CTL,
				WCD938X_HPHR_RX_EN_MASK, 1);
		snd_soc_component_write_field(component,
				WCD938X_HPH_RDAC_CLK_CTL1,
				WCD938X_CHOP_CLK_EN_MASK, 0);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write_field(component,
				WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R,
				WCD938X_HPH_RES_DIV_MASK, 0x02);
		if (wcd938x->comp2_enable) {
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_COMP_CTL_0,
				WCD938X_HPHR_COMP_EN_MASK, 1);
			/* 5msec compander delay as per HW requirement */
			if (!wcd938x->comp1_enable ||
				(snd_soc_component_read(component,
					WCD938X_DIGITAL_CDC_COMP_CTL_0) & 0x02))
				usleep_range(5000, 5010);
			snd_soc_component_write_field(component, WCD938X_HPH_NEW_INT_HPH_TIMER1,
					      WCD938X_AUTOCHOP_TIMER_EN, 0);
		} else {
			snd_soc_component_write_field(component,
					WCD938X_DIGITAL_CDC_COMP_CTL_0,
					WCD938X_HPHR_COMP_EN_MASK, 0);
			snd_soc_component_write_field(component,
					WCD938X_HPH_R_EN,
					WCD938X_GAIN_SRC_SEL_MASK,
					WCD938X_GAIN_SRC_SEL_REGISTER);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component,
			WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R,
			WCD938X_HPH_RES_DIV_MASK, 0x01);
		break;
	}

	return 0;
}

static int wcd938x_codec_ear_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol,
				       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd938x->ear_rx_path =
			snd_soc_component_read(
				component, WCD938X_DIGITAL_CDC_EAR_PATH_CTL);
		if (wcd938x->ear_rx_path & EAR_RX_PATH_AUX) {
			snd_soc_component_write_field(component,
				WCD938X_EAR_EAR_DAC_CON,
				WCD938X_DAC_SAMPLE_EDGE_SEL_MASK, 0);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_AUX_GAIN_CTL,
				WCD938X_AUX_EN_MASK, 1);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_RXD2_CLK_EN_MASK, 1);
			snd_soc_component_write_field(component,
				WCD938X_ANA_EAR_COMPANDER_CTL,
				WCD938X_GAIN_OVRD_REG_MASK, 1);
		} else {
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_HPH_GAIN_CTL,
				WCD938X_HPHL_RX_EN_MASK, 1);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_RXD0_CLK_EN_MASK, 1);
			if (wcd938x->comp1_enable)
				snd_soc_component_write_field(component,
					WCD938X_DIGITAL_CDC_COMP_CTL_0,
					WCD938X_HPHL_COMP_EN_MASK, 1);
		}
		/* 5 msec delay as per HW requirement */
		usleep_range(5000, 5010);
		if (wcd938x->flyback_cur_det_disable == 0)
			snd_soc_component_write_field(component, WCD938X_FLYBACK_EN,
						      WCD938X_EN_CUR_DET_MASK, 0);
		wcd938x->flyback_cur_det_disable++;
		wcd_clsh_ctrl_set_state(wcd938x->clsh_info,
			     WCD_CLSH_EVENT_PRE_DAC,
			     WCD_CLSH_STATE_EAR,
			     wcd938x->hph_mode);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (wcd938x->ear_rx_path & EAR_RX_PATH_AUX) {
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_AUX_GAIN_CTL,
				WCD938X_AUX_EN_MASK, 0);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_RXD2_CLK_EN_MASK, 0);
		} else {
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_HPH_GAIN_CTL,
				WCD938X_HPHL_RX_EN_MASK, 0);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_RXD0_CLK_EN_MASK, 0);
			if (wcd938x->comp1_enable)
				snd_soc_component_write_field(component,
					WCD938X_DIGITAL_CDC_COMP_CTL_0,
					WCD938X_HPHL_COMP_EN_MASK, 0);
		}
		snd_soc_component_write_field(component, WCD938X_ANA_EAR_COMPANDER_CTL,
					      WCD938X_GAIN_OVRD_REG_MASK, 0);
		snd_soc_component_write_field(component,
				WCD938X_EAR_EAR_DAC_CON,
				WCD938X_DAC_SAMPLE_EDGE_SEL_MASK, 1);
		break;
	}
	return 0;

}

static int wcd938x_codec_aux_dac_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol,
				       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
				WCD938X_ANA_RX_DIV4_CLK_EN_MASK, 1);
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_RXD2_CLK_EN_MASK, 1);
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_AUX_GAIN_CTL,
				WCD938X_AUX_EN_MASK, 1);
		if (wcd938x->flyback_cur_det_disable == 0)
			snd_soc_component_write_field(component, WCD938X_FLYBACK_EN,
						      WCD938X_EN_CUR_DET_MASK, 0);
		wcd938x->flyback_cur_det_disable++;
		wcd_clsh_ctrl_set_state(wcd938x->clsh_info,
			     WCD_CLSH_EVENT_PRE_DAC,
			     WCD_CLSH_STATE_AUX,
			     wcd938x->hph_mode);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
				WCD938X_ANA_RX_DIV4_CLK_EN_MASK, 0);
		break;
	}
	return 0;

}

static int wcd938x_codec_enable_hphr_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd938x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (wcd938x->ldoh)
			snd_soc_component_write_field(component, WCD938X_LDOH_MODE,
						      WCD938X_LDOH_EN_MASK, 1);
		wcd_clsh_ctrl_set_state(wcd938x->clsh_info, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHR, hph_mode);
		wcd_clsh_set_hph_mode(wcd938x->clsh_info, CLS_H_HIFI);

		if (hph_mode == CLS_H_LP || hph_mode == CLS_H_LOHIFI ||
		    hph_mode == CLS_H_ULP) {
			snd_soc_component_write_field(component,
				WCD938X_HPH_REFBUFF_LP_CTL,
				WCD938X_PREREF_FLIT_BYPASS_MASK, 1);
		}
		snd_soc_component_write_field(component, WCD938X_ANA_HPH,
					      WCD938X_HPHR_REF_EN_MASK, 1);
		wcd_clsh_set_hph_mode(wcd938x->clsh_info, hph_mode);
		/* 100 usec delay as per HW requirement */
		usleep_range(100, 110);
		set_bit(HPH_PA_DELAY, &wcd938x->status_mask);
		snd_soc_component_write_field(component,
					      WCD938X_DIGITAL_PDM_WD_CTL1,
					      WCD938X_PDM_WD_EN_MASK, 0x3);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (test_bit(HPH_PA_DELAY, &wcd938x->status_mask)) {
			if (!wcd938x->comp2_enable)
				usleep_range(20000, 20100);
			else
				usleep_range(7000, 7100);

			if (hph_mode == CLS_H_LP || hph_mode == CLS_H_LOHIFI ||
			    hph_mode == CLS_H_ULP)
				snd_soc_component_write_field(component,
						WCD938X_HPH_REFBUFF_LP_CTL,
						WCD938X_PREREF_FLIT_BYPASS_MASK, 0);
			clear_bit(HPH_PA_DELAY, &wcd938x->status_mask);
		}
		snd_soc_component_write_field(component, WCD938X_HPH_NEW_INT_HPH_TIMER1,
					      WCD938X_AUTOCHOP_TIMER_EN, 1);
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI ||
			hph_mode == CLS_AB_LP || hph_mode == CLS_AB_LOHIFI)
			snd_soc_component_write_field(component, WCD938X_ANA_RX_SUPPLIES,
					WCD938X_REGULATOR_MODE_MASK,
					WCD938X_REGULATOR_MODE_CLASS_AB);
		enable_irq(wcd938x->hphr_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd938x->hphr_pdm_wd_int);
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (!wcd938x->comp2_enable)
			usleep_range(20000, 20100);
		else
			usleep_range(7000, 7100);
		snd_soc_component_write_field(component, WCD938X_ANA_HPH,
					      WCD938X_HPHR_EN_MASK, 0);
		wcd_mbhc_event_notify(wcd938x->wcd_mbhc,
					     WCD_EVENT_PRE_HPHR_PA_OFF);
		set_bit(HPH_PA_DELAY, &wcd938x->status_mask);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (test_bit(HPH_PA_DELAY, &wcd938x->status_mask)) {
			if (!wcd938x->comp2_enable)
				usleep_range(20000, 20100);
			else
				usleep_range(7000, 7100);
			clear_bit(HPH_PA_DELAY, &wcd938x->status_mask);
		}
		wcd_mbhc_event_notify(wcd938x->wcd_mbhc,
					     WCD_EVENT_POST_HPHR_PA_OFF);
		snd_soc_component_write_field(component, WCD938X_ANA_HPH,
					      WCD938X_HPHR_REF_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_PDM_WD_CTL1,
					      WCD938X_PDM_WD_EN_MASK, 0);
		wcd_clsh_ctrl_set_state(wcd938x->clsh_info, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_HPHR, hph_mode);
		if (wcd938x->ldoh)
			snd_soc_component_write_field(component, WCD938X_LDOH_MODE,
						      WCD938X_LDOH_EN_MASK, 0);
		break;
	}

	return 0;
}

static int wcd938x_codec_enable_hphl_pa(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd938x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (wcd938x->ldoh)
			snd_soc_component_write_field(component, WCD938X_LDOH_MODE,
						      WCD938X_LDOH_EN_MASK, 1);
		wcd_clsh_ctrl_set_state(wcd938x->clsh_info, WCD_CLSH_EVENT_PRE_DAC,
					WCD_CLSH_STATE_HPHL, hph_mode);
		wcd_clsh_set_hph_mode(wcd938x->clsh_info, CLS_H_HIFI);
		if (hph_mode == CLS_H_LP || hph_mode == CLS_H_LOHIFI ||
		    hph_mode == CLS_H_ULP) {
			snd_soc_component_write_field(component,
					WCD938X_HPH_REFBUFF_LP_CTL,
					WCD938X_PREREF_FLIT_BYPASS_MASK, 1);
		}
		snd_soc_component_write_field(component, WCD938X_ANA_HPH,
					      WCD938X_HPHL_REF_EN_MASK, 1);
		wcd_clsh_set_hph_mode(wcd938x->clsh_info, hph_mode);
		/* 100 usec delay as per HW requirement */
		usleep_range(100, 110);
		set_bit(HPH_PA_DELAY, &wcd938x->status_mask);
		snd_soc_component_write_field(component,
					WCD938X_DIGITAL_PDM_WD_CTL0,
					WCD938X_PDM_WD_EN_MASK, 0x3);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (test_bit(HPH_PA_DELAY, &wcd938x->status_mask)) {
			if (!wcd938x->comp1_enable)
				usleep_range(20000, 20100);
			else
				usleep_range(7000, 7100);
			if (hph_mode == CLS_H_LP || hph_mode == CLS_H_LOHIFI ||
			    hph_mode == CLS_H_ULP)
				snd_soc_component_write_field(component,
					WCD938X_HPH_REFBUFF_LP_CTL,
					WCD938X_PREREF_FLIT_BYPASS_MASK, 0);
			clear_bit(HPH_PA_DELAY, &wcd938x->status_mask);
		}

		snd_soc_component_write_field(component, WCD938X_HPH_NEW_INT_HPH_TIMER1,
					      WCD938X_AUTOCHOP_TIMER_EN, 1);
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI ||
			hph_mode == CLS_AB_LP || hph_mode == CLS_AB_LOHIFI)
			snd_soc_component_write_field(component, WCD938X_ANA_RX_SUPPLIES,
					WCD938X_REGULATOR_MODE_MASK,
					WCD938X_REGULATOR_MODE_CLASS_AB);
		enable_irq(wcd938x->hphl_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd938x->hphl_pdm_wd_int);
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (!wcd938x->comp1_enable)
			usleep_range(20000, 20100);
		else
			usleep_range(7000, 7100);
		snd_soc_component_write_field(component, WCD938X_ANA_HPH,
					      WCD938X_HPHL_EN_MASK, 0);
		wcd_mbhc_event_notify(wcd938x->wcd_mbhc, WCD_EVENT_PRE_HPHL_PA_OFF);
		set_bit(HPH_PA_DELAY, &wcd938x->status_mask);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/*
		 * 7ms sleep is required if compander is enabled as per
		 * HW requirement. If compander is disabled, then
		 * 20ms delay is required.
		 */
		if (test_bit(HPH_PA_DELAY, &wcd938x->status_mask)) {
			if (!wcd938x->comp1_enable)
				usleep_range(21000, 21100);
			else
				usleep_range(7000, 7100);
			clear_bit(HPH_PA_DELAY, &wcd938x->status_mask);
		}
		wcd_mbhc_event_notify(wcd938x->wcd_mbhc,
					     WCD_EVENT_POST_HPHL_PA_OFF);
		snd_soc_component_write_field(component, WCD938X_ANA_HPH,
					      WCD938X_HPHL_REF_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_PDM_WD_CTL0,
					      WCD938X_PDM_WD_EN_MASK, 0);
		wcd_clsh_ctrl_set_state(wcd938x->clsh_info, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_HPHL, hph_mode);
		if (wcd938x->ldoh)
			snd_soc_component_write_field(component, WCD938X_LDOH_MODE,
						      WCD938X_LDOH_EN_MASK, 0);
		break;
	}

	return 0;
}

static int wcd938x_codec_enable_aux_pa(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd938x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component, WCD938X_DIGITAL_PDM_WD_CTL2,
					      WCD938X_AUX_PDM_WD_EN_MASK, 1);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* 1 msec delay as per HW requirement */
		usleep_range(1000, 1010);
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI ||
			hph_mode == CLS_AB_LP || hph_mode == CLS_AB_LOHIFI)
			snd_soc_component_write_field(component, WCD938X_ANA_RX_SUPPLIES,
					WCD938X_REGULATOR_MODE_MASK,
					WCD938X_REGULATOR_MODE_CLASS_AB);
		enable_irq(wcd938x->aux_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		disable_irq_nosync(wcd938x->aux_pdm_wd_int);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* 1 msec delay as per HW requirement */
		usleep_range(1000, 1010);
		snd_soc_component_write_field(component, WCD938X_DIGITAL_PDM_WD_CTL2,
					      WCD938X_AUX_PDM_WD_EN_MASK, 0);
		wcd_clsh_ctrl_set_state(wcd938x->clsh_info,
			     WCD_CLSH_EVENT_POST_PA,
			     WCD_CLSH_STATE_AUX,
			     hph_mode);

		wcd938x->flyback_cur_det_disable--;
		if (wcd938x->flyback_cur_det_disable == 0)
			snd_soc_component_write_field(component, WCD938X_FLYBACK_EN,
						      WCD938X_EN_CUR_DET_MASK, 1);
		break;
	}
	return 0;
}

static int wcd938x_codec_enable_ear_pa(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int hph_mode = wcd938x->hph_mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/*
		 * Enable watchdog interrupt for HPHL or AUX
		 * depending on mux value
		 */
		wcd938x->ear_rx_path = snd_soc_component_read(component,
							      WCD938X_DIGITAL_CDC_EAR_PATH_CTL);
		if (wcd938x->ear_rx_path & EAR_RX_PATH_AUX)
			snd_soc_component_write_field(component, WCD938X_DIGITAL_PDM_WD_CTL2,
					      WCD938X_AUX_PDM_WD_EN_MASK, 1);
		else
			snd_soc_component_write_field(component,
						      WCD938X_DIGITAL_PDM_WD_CTL0,
						      WCD938X_PDM_WD_EN_MASK, 0x3);
		if (!wcd938x->comp1_enable)
			snd_soc_component_write_field(component,
						      WCD938X_ANA_EAR_COMPANDER_CTL,
						      WCD938X_GAIN_OVRD_REG_MASK, 1);

		break;
	case SND_SOC_DAPM_POST_PMU:
		/* 6 msec delay as per HW requirement */
		usleep_range(6000, 6010);
		if (hph_mode == CLS_AB || hph_mode == CLS_AB_HIFI ||
			hph_mode == CLS_AB_LP || hph_mode == CLS_AB_LOHIFI)
			snd_soc_component_write_field(component, WCD938X_ANA_RX_SUPPLIES,
					WCD938X_REGULATOR_MODE_MASK,
					WCD938X_REGULATOR_MODE_CLASS_AB);
		if (wcd938x->ear_rx_path & EAR_RX_PATH_AUX)
			enable_irq(wcd938x->aux_pdm_wd_int);
		else
			enable_irq(wcd938x->hphl_pdm_wd_int);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		if (wcd938x->ear_rx_path & EAR_RX_PATH_AUX)
			disable_irq_nosync(wcd938x->aux_pdm_wd_int);
		else
			disable_irq_nosync(wcd938x->hphl_pdm_wd_int);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (!wcd938x->comp1_enable)
			snd_soc_component_write_field(component, WCD938X_ANA_EAR_COMPANDER_CTL,
						      WCD938X_GAIN_OVRD_REG_MASK, 0);
		/* 7 msec delay as per HW requirement */
		usleep_range(7000, 7010);
		if (wcd938x->ear_rx_path & EAR_RX_PATH_AUX)
			snd_soc_component_write_field(component, WCD938X_DIGITAL_PDM_WD_CTL2,
					      WCD938X_AUX_PDM_WD_EN_MASK, 0);
		else
			snd_soc_component_write_field(component, WCD938X_DIGITAL_PDM_WD_CTL0,
					WCD938X_PDM_WD_EN_MASK, 0);

		wcd_clsh_ctrl_set_state(wcd938x->clsh_info, WCD_CLSH_EVENT_POST_PA,
					WCD_CLSH_STATE_EAR, hph_mode);

		wcd938x->flyback_cur_det_disable--;
		if (wcd938x->flyback_cur_det_disable == 0)
			snd_soc_component_write_field(component, WCD938X_FLYBACK_EN,
						      WCD938X_EN_CUR_DET_MASK, 1);
		break;
	}

	return 0;
}

static int wcd938x_codec_enable_dmic(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol,
				     int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 dmic_clk_reg, dmic_clk_en_reg;
	u8 dmic_sel_mask, dmic_clk_mask;

	switch (w->shift) {
	case 0:
	case 1:
		dmic_clk_reg = WCD938X_DIGITAL_CDC_DMIC_RATE_1_2;
		dmic_clk_en_reg = WCD938X_DIGITAL_CDC_DMIC1_CTL;
		dmic_clk_mask = WCD938X_DMIC1_RATE_MASK;
		dmic_sel_mask = WCD938X_AMIC1_IN_SEL_MASK;
		break;
	case 2:
	case 3:
		dmic_clk_reg = WCD938X_DIGITAL_CDC_DMIC_RATE_1_2;
		dmic_clk_en_reg = WCD938X_DIGITAL_CDC_DMIC2_CTL;
		dmic_clk_mask = WCD938X_DMIC2_RATE_MASK;
		dmic_sel_mask = WCD938X_AMIC3_IN_SEL_MASK;
		break;
	case 4:
	case 5:
		dmic_clk_reg = WCD938X_DIGITAL_CDC_DMIC_RATE_3_4;
		dmic_clk_en_reg = WCD938X_DIGITAL_CDC_DMIC3_CTL;
		dmic_clk_mask = WCD938X_DMIC3_RATE_MASK;
		dmic_sel_mask = WCD938X_AMIC4_IN_SEL_MASK;
		break;
	case 6:
	case 7:
		dmic_clk_reg = WCD938X_DIGITAL_CDC_DMIC_RATE_3_4;
		dmic_clk_en_reg = WCD938X_DIGITAL_CDC_DMIC4_CTL;
		dmic_clk_mask = WCD938X_DMIC4_RATE_MASK;
		dmic_sel_mask = WCD938X_AMIC5_IN_SEL_MASK;
		break;
	default:
		dev_err(component->dev, "%s: Invalid DMIC Selection\n",
			__func__);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_AMIC_CTL,
				dmic_sel_mask,
				WCD938X_AMIC1_IN_SEL_DMIC);
		/* 250us sleep as per HW requirement */
		usleep_range(250, 260);
		/* Setting DMIC clock rate to 2.4MHz */
		snd_soc_component_write_field(component, dmic_clk_reg,
					      dmic_clk_mask,
					      WCD938X_DMIC4_RATE_2P4MHZ);
		snd_soc_component_write_field(component, dmic_clk_en_reg,
					      WCD938X_DMIC_CLK_EN_MASK, 1);
		/* enable clock scaling */
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_DMIC_CTL,
					      WCD938X_DMIC_CLK_SCALING_EN_MASK, 0x3);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_AMIC_CTL,
				dmic_sel_mask, WCD938X_AMIC1_IN_SEL_AMIC);
		snd_soc_component_write_field(component, dmic_clk_en_reg,
					      WCD938X_DMIC_CLK_EN_MASK, 0);
		break;
	}
	return 0;
}

static int wcd938x_tx_swr_ctrl(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int bank;
	int rate;

	bank = (wcd938x_swr_get_current_bank(wcd938x->sdw_priv[AIF1_CAP]->sdev)) ? 0 : 1;
	bank = bank ? 0 : 1;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (strnstr(w->name, "ADC", sizeof("ADC"))) {
			int i = 0, mode = 0;

			if (test_bit(WCD_ADC1, &wcd938x->status_mask))
				mode |= tx_mode_bit[wcd938x->tx_mode[WCD_ADC1]];
			if (test_bit(WCD_ADC2, &wcd938x->status_mask))
				mode |= tx_mode_bit[wcd938x->tx_mode[WCD_ADC2]];
			if (test_bit(WCD_ADC3, &wcd938x->status_mask))
				mode |= tx_mode_bit[wcd938x->tx_mode[WCD_ADC3]];
			if (test_bit(WCD_ADC4, &wcd938x->status_mask))
				mode |= tx_mode_bit[wcd938x->tx_mode[WCD_ADC4]];

			if (mode != 0) {
				for (i = 0; i < ADC_MODE_ULP2; i++) {
					if (mode & (1 << i)) {
						i++;
						break;
					}
				}
			}
			rate = wcd938x_get_clk_rate(i);
			wcd938x_set_swr_clk_rate(component, rate, bank);
			/* Copy clk settings to active bank */
			wcd938x_set_swr_clk_rate(component, rate, !bank);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (strnstr(w->name, "ADC", sizeof("ADC"))) {
			rate = wcd938x_get_clk_rate(ADC_MODE_INVALID);
			wcd938x_set_swr_clk_rate(component, rate, !bank);
			wcd938x_set_swr_clk_rate(component, rate, bank);
		}
		break;
	}

	return 0;
}

static int wcd938x_get_adc_mode(int val)
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

static int wcd938x_codec_enable_adc(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component,
					      WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD938X_ANA_TX_CLK_EN_MASK, 1);
		snd_soc_component_write_field(component,
					      WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD938X_ANA_TX_DIV2_CLK_EN_MASK, 1);
		set_bit(w->shift, &wcd938x->status_mask);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
					      WCD938X_ANA_TX_CLK_EN_MASK, 0);
		clear_bit(w->shift, &wcd938x->status_mask);
		break;
	}

	return 0;
}

static void wcd938x_tx_channel_config(struct snd_soc_component *component,
				     int channel, int mode)
{
	int reg, mask;

	switch (channel) {
	case 0:
		reg = WCD938X_ANA_TX_CH2;
		mask = WCD938X_HPF1_INIT_MASK;
		break;
	case 1:
		reg = WCD938X_ANA_TX_CH2;
		mask = WCD938X_HPF2_INIT_MASK;
		break;
	case 2:
		reg = WCD938X_ANA_TX_CH4;
		mask = WCD938X_HPF3_INIT_MASK;
		break;
	case 3:
		reg = WCD938X_ANA_TX_CH4;
		mask = WCD938X_HPF4_INIT_MASK;
		break;
	default:
		return;
	}

	snd_soc_component_write_field(component, reg, mask, mode);
}

static int wcd938x_adc_enable_req(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int mode;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_REQ_CTL,
				WCD938X_FS_RATE_4P8_MASK, 1);
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_REQ_CTL,
				WCD938X_NO_NOTCH_MASK, 0);
		wcd938x_tx_channel_config(component, w->shift, 1);
		mode = wcd938x_get_adc_mode(wcd938x->tx_mode[w->shift]);
		if (mode < 0) {
			dev_info(component->dev, "Invalid ADC mode\n");
			return -EINVAL;
		}
		switch (w->shift) {
		case 0:
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1,
				WCD938X_TXD0_MODE_MASK, mode);
			snd_soc_component_write_field(component,
						WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
						WCD938X_TXD0_CLK_EN_MASK, 1);
			break;
		case 1:
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1,
				WCD938X_TXD1_MODE_MASK, mode);
			snd_soc_component_write_field(component,
					      WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
					      WCD938X_TXD1_CLK_EN_MASK, 1);
			break;
		case 2:
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3,
				WCD938X_TXD2_MODE_MASK, mode);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_TXD2_CLK_EN_MASK, 1);
			break;
		case 3:
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3,
				WCD938X_TXD3_MODE_MASK, mode);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_TXD3_CLK_EN_MASK, 1);
			break;
		default:
			break;
		}

		wcd938x_tx_channel_config(component, w->shift, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		switch (w->shift) {
		case 0:
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1,
				WCD938X_TXD0_MODE_MASK, 0);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_TXD0_CLK_EN_MASK, 0);
			break;
		case 1:
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1,
				WCD938X_TXD1_MODE_MASK, 0);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_TXD1_CLK_EN_MASK, 0);
			break;
		case 2:
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3,
				WCD938X_TXD2_MODE_MASK, 0);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_TXD2_CLK_EN_MASK, 0);
			break;
		case 3:
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3,
				WCD938X_TXD3_MODE_MASK, 0);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_TXD3_CLK_EN_MASK, 0);
			break;
		default:
			break;
		}
		snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
				WCD938X_ANA_TX_DIV2_CLK_EN_MASK, 0);
		break;
	}

	return 0;
}

static int wcd938x_micbias_control(struct snd_soc_component *component,
				   int micb_num, int req, bool is_dapm)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int micb_index = micb_num - 1;
	u16 micb_reg;

	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = WCD938X_ANA_MICB1;
		break;
	case MIC_BIAS_2:
		micb_reg = WCD938X_ANA_MICB2;
		break;
	case MIC_BIAS_3:
		micb_reg = WCD938X_ANA_MICB3;
		break;
	case MIC_BIAS_4:
		micb_reg = WCD938X_ANA_MICB4;
		break;
	default:
		dev_err(component->dev, "%s: Invalid micbias number: %d\n",
			__func__, micb_num);
		return -EINVAL;
	}

	switch (req) {
	case MICB_PULLUP_ENABLE:
		wcd938x->pullup_ref[micb_index]++;
		if ((wcd938x->pullup_ref[micb_index] == 1) &&
		    (wcd938x->micb_ref[micb_index] == 0))
			snd_soc_component_write_field(component, micb_reg,
						      WCD938X_MICB_EN_MASK,
						      WCD938X_MICB_PULL_UP);
		break;
	case MICB_PULLUP_DISABLE:
		if (wcd938x->pullup_ref[micb_index] > 0)
			wcd938x->pullup_ref[micb_index]--;

		if ((wcd938x->pullup_ref[micb_index] == 0) &&
		    (wcd938x->micb_ref[micb_index] == 0))
			snd_soc_component_write_field(component, micb_reg,
						      WCD938X_MICB_EN_MASK, 0);
		break;
	case MICB_ENABLE:
		wcd938x->micb_ref[micb_index]++;
		if (wcd938x->micb_ref[micb_index] == 1) {
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_DIG_CLK_CTL,
				WCD938X_TX_CLK_EN_MASK, 0xF);
			snd_soc_component_write_field(component,
				WCD938X_DIGITAL_CDC_ANA_CLK_CTL,
				WCD938X_ANA_TX_DIV2_CLK_EN_MASK, 1);
			snd_soc_component_write_field(component,
			       WCD938X_DIGITAL_CDC_ANA_TX_CLK_CTL,
			       WCD938X_TX_SC_CLK_EN_MASK, 1);

			snd_soc_component_write_field(component, micb_reg,
						      WCD938X_MICB_EN_MASK,
						      WCD938X_MICB_ENABLE);
			if (micb_num  == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd938x->wcd_mbhc,
						      WCD_EVENT_POST_MICBIAS_2_ON);
		}
		if (micb_num  == MIC_BIAS_2 && is_dapm)
			wcd_mbhc_event_notify(wcd938x->wcd_mbhc,
					      WCD_EVENT_POST_DAPM_MICBIAS_2_ON);


		break;
	case MICB_DISABLE:
		if (wcd938x->micb_ref[micb_index] > 0)
			wcd938x->micb_ref[micb_index]--;

		if ((wcd938x->micb_ref[micb_index] == 0) &&
		    (wcd938x->pullup_ref[micb_index] > 0))
			snd_soc_component_write_field(component, micb_reg,
						      WCD938X_MICB_EN_MASK,
						      WCD938X_MICB_PULL_UP);
		else if ((wcd938x->micb_ref[micb_index] == 0) &&
			 (wcd938x->pullup_ref[micb_index] == 0)) {
			if (micb_num  == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd938x->wcd_mbhc,
						      WCD_EVENT_PRE_MICBIAS_2_OFF);

			snd_soc_component_write_field(component, micb_reg,
						      WCD938X_MICB_EN_MASK, 0);
			if (micb_num  == MIC_BIAS_2)
				wcd_mbhc_event_notify(wcd938x->wcd_mbhc,
						      WCD_EVENT_POST_MICBIAS_2_OFF);
		}
		if (is_dapm && micb_num  == MIC_BIAS_2)
			wcd_mbhc_event_notify(wcd938x->wcd_mbhc,
					      WCD_EVENT_POST_DAPM_MICBIAS_2_OFF);
		break;
	}

	return 0;
}

static int wcd938x_codec_enable_micbias(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd938x_micbias_control(component, micb_num, MICB_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* 1 msec delay as per HW requirement */
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd938x_micbias_control(component, micb_num, MICB_DISABLE, true);
		break;
	}

	return 0;
}

static int wcd938x_codec_enable_micbias_pullup(struct snd_soc_dapm_widget *w,
					       struct snd_kcontrol *kcontrol,
					       int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int micb_num = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wcd938x_micbias_control(component, micb_num,
					MICB_PULLUP_ENABLE, true);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* 1 msec delay as per HW requirement */
		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wcd938x_micbias_control(component, micb_num,
					MICB_PULLUP_DISABLE, true);
		break;
	}

	return 0;
}

static int wcd938x_tx_mode_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int path = e->shift_l;

	ucontrol->value.enumerated.item[0] = wcd938x->tx_mode[path];

	return 0;
}

static int wcd938x_tx_mode_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int path = e->shift_l;

	if (wcd938x->tx_mode[path] == ucontrol->value.enumerated.item[0])
		return 0;

	wcd938x->tx_mode[path] = ucontrol->value.enumerated.item[0];

	return 1;
}

static int wcd938x_rx_hph_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = wcd938x->hph_mode;

	return 0;
}

static int wcd938x_rx_hph_mode_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	if (wcd938x->hph_mode == ucontrol->value.enumerated.item[0])
		return 0;

	wcd938x->hph_mode = ucontrol->value.enumerated.item[0];

	return 1;
}

static int wcd938x_ear_pa_put_gain(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	if (wcd938x->comp1_enable) {
		dev_err(component->dev, "Can not set EAR PA Gain, compander1 is enabled\n");
		return -EINVAL;
	}

	snd_soc_component_write_field(component, WCD938X_ANA_EAR_COMPANDER_CTL,
				      WCD938X_EAR_GAIN_MASK,
				      ucontrol->value.integer.value[0]);

	return 1;
}

static int wcd938x_get_compander(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{

	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc;
	bool hphr;

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;

	if (hphr)
		ucontrol->value.integer.value[0] = wcd938x->comp2_enable;
	else
		ucontrol->value.integer.value[0] = wcd938x->comp1_enable;

	return 0;
}

static int wcd938x_set_compander(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	struct wcd938x_sdw_priv *wcd;
	int value = ucontrol->value.integer.value[0];
	int portidx;
	struct soc_mixer_control *mc;
	bool hphr;

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;

	wcd = wcd938x->sdw_priv[AIF1_PB];

	if (hphr)
		wcd938x->comp2_enable = value;
	else
		wcd938x->comp1_enable = value;

	portidx = wcd->ch_info[mc->reg].port_num;

	if (value)
		wcd938x_connect_port(wcd, portidx, mc->reg, true);
	else
		wcd938x_connect_port(wcd, portidx, mc->reg, false);

	return 1;
}

static int wcd938x_ldoh_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd938x->ldoh;

	return 0;
}

static int wcd938x_ldoh_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	if (wcd938x->ldoh == ucontrol->value.integer.value[0])
		return 0;

	wcd938x->ldoh = ucontrol->value.integer.value[0];

	return 1;
}

static int wcd938x_bcs_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd938x->bcs_dis;

	return 0;
}

static int wcd938x_bcs_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	if (wcd938x->bcs_dis == ucontrol->value.integer.value[0])
		return 0;

	wcd938x->bcs_dis = ucontrol->value.integer.value[0];

	return 1;
}

static const char * const tx_mode_mux_text_wcd9380[] = {
	"ADC_INVALID", "ADC_HIFI", "ADC_LO_HIF", "ADC_NORMAL", "ADC_LP",
};

static const char * const tx_mode_mux_text[] = {
	"ADC_INVALID", "ADC_HIFI", "ADC_LO_HIF", "ADC_NORMAL", "ADC_LP",
	"ADC_ULP1", "ADC_ULP2",
};

static const char * const rx_hph_mode_mux_text_wcd9380[] = {
	"CLS_H_INVALID", "CLS_H_INVALID_1", "CLS_H_LP", "CLS_AB",
	"CLS_H_LOHIFI", "CLS_H_ULP", "CLS_H_INVALID_2", "CLS_AB_LP",
	"CLS_AB_LOHIFI",
};

static const char * const rx_hph_mode_mux_text[] = {
	"CLS_H_INVALID", "CLS_H_HIFI", "CLS_H_LP", "CLS_AB", "CLS_H_LOHIFI",
	"CLS_H_ULP", "CLS_AB_HIFI", "CLS_AB_LP", "CLS_AB_LOHIFI",
};

static const char * const adc2_mux_text[] = {
	"INP2", "INP3"
};

static const char * const adc3_mux_text[] = {
	"INP4", "INP6"
};

static const char * const adc4_mux_text[] = {
	"INP5", "INP7"
};

static const char * const rdac3_mux_text[] = {
	"RX1", "RX3"
};

static const char * const hdr12_mux_text[] = {
	"NO_HDR12", "HDR12"
};

static const char * const hdr34_mux_text[] = {
	"NO_HDR34", "HDR34"
};

static const struct soc_enum tx0_mode_enum_wcd9380 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(tx_mode_mux_text_wcd9380),
			tx_mode_mux_text_wcd9380);

static const struct soc_enum tx1_mode_enum_wcd9380 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 1, ARRAY_SIZE(tx_mode_mux_text_wcd9380),
			tx_mode_mux_text_wcd9380);

static const struct soc_enum tx2_mode_enum_wcd9380 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 2, ARRAY_SIZE(tx_mode_mux_text_wcd9380),
			tx_mode_mux_text_wcd9380);

static const struct soc_enum tx3_mode_enum_wcd9380 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 3, ARRAY_SIZE(tx_mode_mux_text_wcd9380),
			tx_mode_mux_text_wcd9380);

static const struct soc_enum tx0_mode_enum_wcd9385 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(tx_mode_mux_text),
			tx_mode_mux_text);

static const struct soc_enum tx1_mode_enum_wcd9385 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 1, ARRAY_SIZE(tx_mode_mux_text),
			tx_mode_mux_text);

static const struct soc_enum tx2_mode_enum_wcd9385 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 2, ARRAY_SIZE(tx_mode_mux_text),
			tx_mode_mux_text);

static const struct soc_enum tx3_mode_enum_wcd9385 =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 3, ARRAY_SIZE(tx_mode_mux_text),
			tx_mode_mux_text);

static const struct soc_enum rx_hph_mode_mux_enum_wcd9380 =
		SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_hph_mode_mux_text_wcd9380),
				    rx_hph_mode_mux_text_wcd9380);

static const struct soc_enum rx_hph_mode_mux_enum =
		SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_hph_mode_mux_text),
				    rx_hph_mode_mux_text);

static const struct soc_enum adc2_enum =
		SOC_ENUM_SINGLE(WCD938X_TX_NEW_AMIC_MUX_CFG, 7,
				ARRAY_SIZE(adc2_mux_text), adc2_mux_text);

static const struct soc_enum adc3_enum =
		SOC_ENUM_SINGLE(WCD938X_TX_NEW_AMIC_MUX_CFG, 6,
				ARRAY_SIZE(adc3_mux_text), adc3_mux_text);

static const struct soc_enum adc4_enum =
		SOC_ENUM_SINGLE(WCD938X_TX_NEW_AMIC_MUX_CFG, 5,
				ARRAY_SIZE(adc4_mux_text), adc4_mux_text);

static const struct soc_enum hdr12_enum =
		SOC_ENUM_SINGLE(WCD938X_TX_NEW_AMIC_MUX_CFG, 4,
				ARRAY_SIZE(hdr12_mux_text), hdr12_mux_text);

static const struct soc_enum hdr34_enum =
		SOC_ENUM_SINGLE(WCD938X_TX_NEW_AMIC_MUX_CFG, 3,
				ARRAY_SIZE(hdr34_mux_text), hdr34_mux_text);

static const struct soc_enum rdac3_enum =
		SOC_ENUM_SINGLE(WCD938X_DIGITAL_CDC_EAR_PATH_CTL, 0,
				ARRAY_SIZE(rdac3_mux_text), rdac3_mux_text);

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

static const struct snd_kcontrol_new aux_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new hphl_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new hphr_rdac_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)
};

static const struct snd_kcontrol_new tx_adc2_mux =
	SOC_DAPM_ENUM("ADC2 MUX Mux", adc2_enum);

static const struct snd_kcontrol_new tx_adc3_mux =
	SOC_DAPM_ENUM("ADC3 MUX Mux", adc3_enum);

static const struct snd_kcontrol_new tx_adc4_mux =
	SOC_DAPM_ENUM("ADC4 MUX Mux", adc4_enum);

static const struct snd_kcontrol_new tx_hdr12_mux =
	SOC_DAPM_ENUM("HDR12 MUX Mux", hdr12_enum);

static const struct snd_kcontrol_new tx_hdr34_mux =
	SOC_DAPM_ENUM("HDR34 MUX Mux", hdr34_enum);

static const struct snd_kcontrol_new rx_rdac3_mux =
	SOC_DAPM_ENUM("RDAC3_MUX Mux", rdac3_enum);

static const struct snd_kcontrol_new wcd9380_snd_controls[] = {
	SOC_ENUM_EXT("RX HPH Mode", rx_hph_mode_mux_enum_wcd9380,
		     wcd938x_rx_hph_mode_get, wcd938x_rx_hph_mode_put),
	SOC_ENUM_EXT("TX0 MODE", tx0_mode_enum_wcd9380,
		     wcd938x_tx_mode_get, wcd938x_tx_mode_put),
	SOC_ENUM_EXT("TX1 MODE", tx1_mode_enum_wcd9380,
		     wcd938x_tx_mode_get, wcd938x_tx_mode_put),
	SOC_ENUM_EXT("TX2 MODE", tx2_mode_enum_wcd9380,
		     wcd938x_tx_mode_get, wcd938x_tx_mode_put),
	SOC_ENUM_EXT("TX3 MODE", tx3_mode_enum_wcd9380,
		     wcd938x_tx_mode_get, wcd938x_tx_mode_put),
};

static const struct snd_kcontrol_new wcd9385_snd_controls[] = {
	SOC_ENUM_EXT("RX HPH Mode", rx_hph_mode_mux_enum,
		     wcd938x_rx_hph_mode_get, wcd938x_rx_hph_mode_put),
	SOC_ENUM_EXT("TX0 MODE", tx0_mode_enum_wcd9385,
		     wcd938x_tx_mode_get, wcd938x_tx_mode_put),
	SOC_ENUM_EXT("TX1 MODE", tx1_mode_enum_wcd9385,
		     wcd938x_tx_mode_get, wcd938x_tx_mode_put),
	SOC_ENUM_EXT("TX2 MODE", tx2_mode_enum_wcd9385,
		     wcd938x_tx_mode_get, wcd938x_tx_mode_put),
	SOC_ENUM_EXT("TX3 MODE", tx3_mode_enum_wcd9385,
		     wcd938x_tx_mode_get, wcd938x_tx_mode_put),
};

static int wcd938x_get_swr_port(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(comp);
	struct wcd938x_sdw_priv *wcd;
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	int dai_id = mixer->shift;
	int portidx, ch_idx = mixer->reg;


	wcd = wcd938x->sdw_priv[dai_id];
	portidx = wcd->ch_info[ch_idx].port_num;

	ucontrol->value.integer.value[0] = wcd->port_enable[portidx];

	return 0;
}

static int wcd938x_set_swr_port(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(comp);
	struct wcd938x_sdw_priv *wcd;
	struct soc_mixer_control *mixer =
		(struct soc_mixer_control *)kcontrol->private_value;
	int ch_idx = mixer->reg;
	int portidx;
	int dai_id = mixer->shift;
	bool enable;

	wcd = wcd938x->sdw_priv[dai_id];

	portidx = wcd->ch_info[ch_idx].port_num;
	if (ucontrol->value.integer.value[0])
		enable = true;
	else
		enable = false;

	wcd->port_enable[portidx] = enable;

	wcd938x_connect_port(wcd, portidx, ch_idx, enable);

	return 1;

}

/* MBHC related */
static void wcd938x_mbhc_clk_setup(struct snd_soc_component *component,
				   bool enable)
{
	snd_soc_component_write_field(component, WCD938X_MBHC_NEW_CTL_1,
				      WCD938X_MBHC_CTL_RCO_EN_MASK, enable);
}

static void wcd938x_mbhc_mbhc_bias_control(struct snd_soc_component *component,
					   bool enable)
{
	snd_soc_component_write_field(component, WCD938X_ANA_MBHC_ELECT,
				      WCD938X_ANA_MBHC_BIAS_EN, enable);
}

static void wcd938x_mbhc_program_btn_thr(struct snd_soc_component *component,
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
		snd_soc_component_write_field(component, WCD938X_ANA_MBHC_BTN0 + i,
					   WCD938X_MBHC_BTN_VTH_MASK, vth);
		dev_dbg(component->dev, "%s: btn_high[%d]: %d, vth: %d\n",
			__func__, i, btn_high[i], vth);
	}
}

static bool wcd938x_mbhc_micb_en_status(struct snd_soc_component *component, int micb_num)
{
	u8 val;

	if (micb_num == MIC_BIAS_2) {
		val = snd_soc_component_read_field(component,
						   WCD938X_ANA_MICB2,
						   WCD938X_ANA_MICB2_ENABLE_MASK);
		if (val == WCD938X_MICB_ENABLE)
			return true;
	}
	return false;
}

static void wcd938x_mbhc_hph_l_pull_up_control(struct snd_soc_component *component,
							int pull_up_cur)
{
	/* Default pull up current to 2uA */
	if (pull_up_cur > HS_PULLUP_I_OFF || pull_up_cur < HS_PULLUP_I_3P0_UA)
		pull_up_cur = HS_PULLUP_I_2P0_UA;

	snd_soc_component_write_field(component,
				      WCD938X_MBHC_NEW_INT_MECH_DET_CURRENT,
				      WCD938X_HSDET_PULLUP_C_MASK, pull_up_cur);
}

static int wcd938x_mbhc_request_micbias(struct snd_soc_component *component,
					int micb_num, int req)
{
	return wcd938x_micbias_control(component, micb_num, req, false);
}

static void wcd938x_mbhc_micb_ramp_control(struct snd_soc_component *component,
					   bool enable)
{
	if (enable) {
		snd_soc_component_write_field(component, WCD938X_ANA_MICB2_RAMP,
				    WCD938X_RAMP_SHIFT_CTRL_MASK, 0x0C);
		snd_soc_component_write_field(component, WCD938X_ANA_MICB2_RAMP,
				    WCD938X_RAMP_EN_MASK, 1);
	} else {
		snd_soc_component_write_field(component, WCD938X_ANA_MICB2_RAMP,
				    WCD938X_RAMP_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_ANA_MICB2_RAMP,
				    WCD938X_RAMP_SHIFT_CTRL_MASK, 0);
	}
}

static int wcd938x_get_micb_vout_ctl_val(u32 micb_mv)
{
	/* min micbias voltage is 1V and maximum is 2.85V */
	if (micb_mv < 1000 || micb_mv > 2850)
		return -EINVAL;

	return (micb_mv - 1000) / 50;
}

static int wcd938x_mbhc_micb_adjust_voltage(struct snd_soc_component *component,
					    int req_volt, int micb_num)
{
	struct wcd938x_priv *wcd938x =  snd_soc_component_get_drvdata(component);
	int cur_vout_ctl, req_vout_ctl, micb_reg, micb_en, ret = 0;

	switch (micb_num) {
	case MIC_BIAS_1:
		micb_reg = WCD938X_ANA_MICB1;
		break;
	case MIC_BIAS_2:
		micb_reg = WCD938X_ANA_MICB2;
		break;
	case MIC_BIAS_3:
		micb_reg = WCD938X_ANA_MICB3;
		break;
	case MIC_BIAS_4:
		micb_reg = WCD938X_ANA_MICB4;
		break;
	default:
		return -EINVAL;
	}
	mutex_lock(&wcd938x->micb_lock);
	/*
	 * If requested micbias voltage is same as current micbias
	 * voltage, then just return. Otherwise, adjust voltage as
	 * per requested value. If micbias is already enabled, then
	 * to avoid slow micbias ramp-up or down enable pull-up
	 * momentarily, change the micbias value and then re-enable
	 * micbias.
	 */
	micb_en = snd_soc_component_read_field(component, micb_reg,
						WCD938X_MICB_EN_MASK);
	cur_vout_ctl = snd_soc_component_read_field(component, micb_reg,
						    WCD938X_MICB_VOUT_MASK);

	req_vout_ctl = wcd938x_get_micb_vout_ctl_val(req_volt);
	if (req_vout_ctl < 0) {
		ret = -EINVAL;
		goto exit;
	}

	if (cur_vout_ctl == req_vout_ctl) {
		ret = 0;
		goto exit;
	}

	if (micb_en == WCD938X_MICB_ENABLE)
		snd_soc_component_write_field(component, micb_reg,
					      WCD938X_MICB_EN_MASK,
					      WCD938X_MICB_PULL_UP);

	snd_soc_component_write_field(component, micb_reg,
				      WCD938X_MICB_VOUT_MASK,
				      req_vout_ctl);

	if (micb_en == WCD938X_MICB_ENABLE) {
		snd_soc_component_write_field(component, micb_reg,
					      WCD938X_MICB_EN_MASK,
					      WCD938X_MICB_ENABLE);
		/*
		 * Add 2ms delay as per HW requirement after enabling
		 * micbias
		 */
		usleep_range(2000, 2100);
	}
exit:
	mutex_unlock(&wcd938x->micb_lock);
	return ret;
}

static int wcd938x_mbhc_micb_ctrl_threshold_mic(struct snd_soc_component *component,
						int micb_num, bool req_en)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int micb_mv;

	if (micb_num != MIC_BIAS_2)
		return -EINVAL;
	/*
	 * If device tree micbias level is already above the minimum
	 * voltage needed to detect threshold microphone, then do
	 * not change the micbias, just return.
	 */
	if (wcd938x->micb2_mv >= WCD_MBHC_THR_HS_MICB_MV)
		return 0;

	micb_mv = req_en ? WCD_MBHC_THR_HS_MICB_MV : wcd938x->micb2_mv;

	return wcd938x_mbhc_micb_adjust_voltage(component, micb_mv, MIC_BIAS_2);
}

static inline void wcd938x_mbhc_get_result_params(struct wcd938x_priv *wcd938x,
						s16 *d1_a, u16 noff,
						int32_t *zdet)
{
	int i;
	int val, val1;
	s16 c1;
	s32 x1, d1;
	int32_t denom;
	static const int minCode_param[] = {
		3277, 1639, 820, 410, 205, 103, 52, 26
	};

	regmap_update_bits(wcd938x->regmap, WCD938X_ANA_MBHC_ZDET, 0x20, 0x20);
	for (i = 0; i < WCD938X_ZDET_NUM_MEASUREMENTS; i++) {
		regmap_read(wcd938x->regmap, WCD938X_ANA_MBHC_RESULT_2, &val);
		if (val & 0x80)
			break;
	}
	val = val << 0x8;
	regmap_read(wcd938x->regmap, WCD938X_ANA_MBHC_RESULT_1, &val1);
	val |= val1;
	regmap_update_bits(wcd938x->regmap, WCD938X_ANA_MBHC_ZDET, 0x20, 0x00);
	x1 = WCD938X_MBHC_GET_X1(val);
	c1 = WCD938X_MBHC_GET_C1(val);
	/* If ramp is not complete, give additional 5ms */
	if ((c1 < 2) && x1)
		usleep_range(5000, 5050);

	if (!c1 || !x1) {
		pr_err("%s: Impedance detect ramp error, c1=%d, x1=0x%x\n",
			__func__, c1, x1);
		goto ramp_down;
	}
	d1 = d1_a[c1];
	denom = (x1 * d1) - (1 << (14 - noff));
	if (denom > 0)
		*zdet = (WCD938X_MBHC_ZDET_CONST * 1000) / denom;
	else if (x1 < minCode_param[noff])
		*zdet = WCD938X_ZDET_FLOATING_IMPEDANCE;

	pr_err("%s: d1=%d, c1=%d, x1=0x%x, z_val=%d(milliOhm)\n",
		__func__, d1, c1, x1, *zdet);
ramp_down:
	i = 0;
	while (x1) {
		regmap_read(wcd938x->regmap,
				 WCD938X_ANA_MBHC_RESULT_1, &val);
		regmap_read(wcd938x->regmap,
				 WCD938X_ANA_MBHC_RESULT_2, &val1);
		val = val << 0x08;
		val |= val1;
		x1 = WCD938X_MBHC_GET_X1(val);
		i++;
		if (i == WCD938X_ZDET_NUM_MEASUREMENTS)
			break;
	}
}

static void wcd938x_mbhc_zdet_ramp(struct snd_soc_component *component,
				 struct wcd938x_mbhc_zdet_param *zdet_param,
				 int32_t *zl, int32_t *zr, s16 *d1_a)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	int32_t zdet = 0;

	snd_soc_component_write_field(component, WCD938X_MBHC_NEW_ZDET_ANA_CTL,
				WCD938X_ZDET_MAXV_CTL_MASK, zdet_param->ldo_ctl);
	snd_soc_component_update_bits(component, WCD938X_ANA_MBHC_BTN5,
				    WCD938X_VTH_MASK, zdet_param->btn5);
	snd_soc_component_update_bits(component, WCD938X_ANA_MBHC_BTN6,
				      WCD938X_VTH_MASK, zdet_param->btn6);
	snd_soc_component_update_bits(component, WCD938X_ANA_MBHC_BTN7,
				     WCD938X_VTH_MASK, zdet_param->btn7);
	snd_soc_component_write_field(component, WCD938X_MBHC_NEW_ZDET_ANA_CTL,
				WCD938X_ZDET_RANGE_CTL_MASK, zdet_param->noff);
	snd_soc_component_update_bits(component, WCD938X_MBHC_NEW_ZDET_RAMP_CTL,
				0x0F, zdet_param->nshift);

	if (!zl)
		goto z_right;
	/* Start impedance measurement for HPH_L */
	regmap_update_bits(wcd938x->regmap,
			   WCD938X_ANA_MBHC_ZDET, 0x80, 0x80);
	dev_dbg(component->dev, "%s: ramp for HPH_L, noff = %d\n",
		__func__, zdet_param->noff);
	wcd938x_mbhc_get_result_params(wcd938x, d1_a, zdet_param->noff, &zdet);
	regmap_update_bits(wcd938x->regmap,
			   WCD938X_ANA_MBHC_ZDET, 0x80, 0x00);

	*zl = zdet;

z_right:
	if (!zr)
		return;
	/* Start impedance measurement for HPH_R */
	regmap_update_bits(wcd938x->regmap,
			   WCD938X_ANA_MBHC_ZDET, 0x40, 0x40);
	dev_dbg(component->dev, "%s: ramp for HPH_R, noff = %d\n",
		__func__, zdet_param->noff);
	wcd938x_mbhc_get_result_params(wcd938x, d1_a, zdet_param->noff, &zdet);
	regmap_update_bits(wcd938x->regmap,
			   WCD938X_ANA_MBHC_ZDET, 0x40, 0x00);

	*zr = zdet;
}

static inline void wcd938x_wcd_mbhc_qfuse_cal(struct snd_soc_component *component,
					      int32_t *z_val, int flag_l_r)
{
	s16 q1;
	int q1_cal;

	if (*z_val < (WCD938X_ZDET_VAL_400/1000))
		q1 = snd_soc_component_read(component,
			WCD938X_DIGITAL_EFUSE_REG_23 + (2 * flag_l_r));
	else
		q1 = snd_soc_component_read(component,
			WCD938X_DIGITAL_EFUSE_REG_24 + (2 * flag_l_r));
	if (q1 & 0x80)
		q1_cal = (10000 - ((q1 & 0x7F) * 25));
	else
		q1_cal = (10000 + (q1 * 25));
	if (q1_cal > 0)
		*z_val = ((*z_val) * 10000) / q1_cal;
}

static void wcd938x_wcd_mbhc_calc_impedance(struct snd_soc_component *component,
					    uint32_t *zl, uint32_t *zr)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	s16 reg0, reg1, reg2, reg3, reg4;
	int32_t z1L, z1R, z1Ls;
	int zMono, z_diff1, z_diff2;
	bool is_fsm_disable = false;
	struct wcd938x_mbhc_zdet_param zdet_param[] = {
		{4, 0, 4, 0x08, 0x14, 0x18}, /* < 32ohm */
		{2, 0, 3, 0x18, 0x7C, 0x90}, /* 32ohm < Z < 400ohm */
		{1, 4, 5, 0x18, 0x7C, 0x90}, /* 400ohm < Z < 1200ohm */
		{1, 6, 7, 0x18, 0x7C, 0x90}, /* >1200ohm */
	};
	struct wcd938x_mbhc_zdet_param *zdet_param_ptr = NULL;
	s16 d1_a[][4] = {
		{0, 30, 90, 30},
		{0, 30, 30, 5},
		{0, 30, 30, 5},
		{0, 30, 30, 5},
	};
	s16 *d1 = NULL;

	reg0 = snd_soc_component_read(component, WCD938X_ANA_MBHC_BTN5);
	reg1 = snd_soc_component_read(component, WCD938X_ANA_MBHC_BTN6);
	reg2 = snd_soc_component_read(component, WCD938X_ANA_MBHC_BTN7);
	reg3 = snd_soc_component_read(component, WCD938X_MBHC_CTL_CLK);
	reg4 = snd_soc_component_read(component, WCD938X_MBHC_NEW_ZDET_ANA_CTL);

	if (snd_soc_component_read(component, WCD938X_ANA_MBHC_ELECT) & 0x80) {
		is_fsm_disable = true;
		regmap_update_bits(wcd938x->regmap,
				   WCD938X_ANA_MBHC_ELECT, 0x80, 0x00);
	}

	/* For NO-jack, disable L_DET_EN before Z-det measurements */
	if (wcd938x->mbhc_cfg.hphl_swh)
		regmap_update_bits(wcd938x->regmap,
				   WCD938X_ANA_MBHC_MECH, 0x80, 0x00);

	/* Turn off 100k pull down on HPHL */
	regmap_update_bits(wcd938x->regmap,
			   WCD938X_ANA_MBHC_MECH, 0x01, 0x00);

	/* Disable surge protection before impedance detection.
	 * This is done to give correct value for high impedance.
	 */
	regmap_update_bits(wcd938x->regmap,
			   WCD938X_HPH_SURGE_HPHLR_SURGE_EN, 0xC0, 0x00);
	/* 1ms delay needed after disable surge protection */
	usleep_range(1000, 1010);

	/* First get impedance on Left */
	d1 = d1_a[1];
	zdet_param_ptr = &zdet_param[1];
	wcd938x_mbhc_zdet_ramp(component, zdet_param_ptr, &z1L, NULL, d1);

	if (!WCD938X_MBHC_IS_SECOND_RAMP_REQUIRED(z1L))
		goto left_ch_impedance;

	/* Second ramp for left ch */
	if (z1L < WCD938X_ZDET_VAL_32) {
		zdet_param_ptr = &zdet_param[0];
		d1 = d1_a[0];
	} else if ((z1L > WCD938X_ZDET_VAL_400) &&
		  (z1L <= WCD938X_ZDET_VAL_1200)) {
		zdet_param_ptr = &zdet_param[2];
		d1 = d1_a[2];
	} else if (z1L > WCD938X_ZDET_VAL_1200) {
		zdet_param_ptr = &zdet_param[3];
		d1 = d1_a[3];
	}
	wcd938x_mbhc_zdet_ramp(component, zdet_param_ptr, &z1L, NULL, d1);

left_ch_impedance:
	if ((z1L == WCD938X_ZDET_FLOATING_IMPEDANCE) ||
		(z1L > WCD938X_ZDET_VAL_100K)) {
		*zl = WCD938X_ZDET_FLOATING_IMPEDANCE;
		zdet_param_ptr = &zdet_param[1];
		d1 = d1_a[1];
	} else {
		*zl = z1L/1000;
		wcd938x_wcd_mbhc_qfuse_cal(component, zl, 0);
	}
	dev_dbg(component->dev, "%s: impedance on HPH_L = %d(ohms)\n",
		__func__, *zl);

	/* Start of right impedance ramp and calculation */
	wcd938x_mbhc_zdet_ramp(component, zdet_param_ptr, NULL, &z1R, d1);
	if (WCD938X_MBHC_IS_SECOND_RAMP_REQUIRED(z1R)) {
		if (((z1R > WCD938X_ZDET_VAL_1200) &&
			(zdet_param_ptr->noff == 0x6)) ||
			((*zl) != WCD938X_ZDET_FLOATING_IMPEDANCE))
			goto right_ch_impedance;
		/* Second ramp for right ch */
		if (z1R < WCD938X_ZDET_VAL_32) {
			zdet_param_ptr = &zdet_param[0];
			d1 = d1_a[0];
		} else if ((z1R > WCD938X_ZDET_VAL_400) &&
			(z1R <= WCD938X_ZDET_VAL_1200)) {
			zdet_param_ptr = &zdet_param[2];
			d1 = d1_a[2];
		} else if (z1R > WCD938X_ZDET_VAL_1200) {
			zdet_param_ptr = &zdet_param[3];
			d1 = d1_a[3];
		}
		wcd938x_mbhc_zdet_ramp(component, zdet_param_ptr, NULL, &z1R, d1);
	}
right_ch_impedance:
	if ((z1R == WCD938X_ZDET_FLOATING_IMPEDANCE) ||
		(z1R > WCD938X_ZDET_VAL_100K)) {
		*zr = WCD938X_ZDET_FLOATING_IMPEDANCE;
	} else {
		*zr = z1R/1000;
		wcd938x_wcd_mbhc_qfuse_cal(component, zr, 1);
	}
	dev_dbg(component->dev, "%s: impedance on HPH_R = %d(ohms)\n",
		__func__, *zr);

	/* Mono/stereo detection */
	if ((*zl == WCD938X_ZDET_FLOATING_IMPEDANCE) &&
		(*zr == WCD938X_ZDET_FLOATING_IMPEDANCE)) {
		dev_dbg(component->dev,
			"%s: plug type is invalid or extension cable\n",
			__func__);
		goto zdet_complete;
	}
	if ((*zl == WCD938X_ZDET_FLOATING_IMPEDANCE) ||
	    (*zr == WCD938X_ZDET_FLOATING_IMPEDANCE) ||
	    ((*zl < WCD_MONO_HS_MIN_THR) && (*zr > WCD_MONO_HS_MIN_THR)) ||
	    ((*zl > WCD_MONO_HS_MIN_THR) && (*zr < WCD_MONO_HS_MIN_THR))) {
		dev_dbg(component->dev,
			"%s: Mono plug type with one ch floating or shorted to GND\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd938x->wcd_mbhc, WCD_MBHC_HPH_MONO);
		goto zdet_complete;
	}
	snd_soc_component_write_field(component, WCD938X_HPH_R_ATEST,
				      WCD938X_HPHPA_GND_OVR_MASK, 1);
	snd_soc_component_write_field(component, WCD938X_HPH_PA_CTL2,
				      WCD938X_HPHPA_GND_R_MASK, 1);
	if (*zl < (WCD938X_ZDET_VAL_32/1000))
		wcd938x_mbhc_zdet_ramp(component, &zdet_param[0], &z1Ls, NULL, d1);
	else
		wcd938x_mbhc_zdet_ramp(component, &zdet_param[1], &z1Ls, NULL, d1);
	snd_soc_component_write_field(component, WCD938X_HPH_PA_CTL2,
				      WCD938X_HPHPA_GND_R_MASK, 0);
	snd_soc_component_write_field(component, WCD938X_HPH_R_ATEST,
				      WCD938X_HPHPA_GND_OVR_MASK, 0);
	z1Ls /= 1000;
	wcd938x_wcd_mbhc_qfuse_cal(component, &z1Ls, 0);
	/* Parallel of left Z and 9 ohm pull down resistor */
	zMono = ((*zl) * 9) / ((*zl) + 9);
	z_diff1 = (z1Ls > zMono) ? (z1Ls - zMono) : (zMono - z1Ls);
	z_diff2 = ((*zl) > z1Ls) ? ((*zl) - z1Ls) : (z1Ls - (*zl));
	if ((z_diff1 * (*zl + z1Ls)) > (z_diff2 * (z1Ls + zMono))) {
		dev_dbg(component->dev, "%s: stereo plug type detected\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd938x->wcd_mbhc, WCD_MBHC_HPH_STEREO);
	} else {
		dev_dbg(component->dev, "%s: MONO plug type detected\n",
			__func__);
		wcd_mbhc_set_hph_type(wcd938x->wcd_mbhc, WCD_MBHC_HPH_MONO);
	}

	/* Enable surge protection again after impedance detection */
	regmap_update_bits(wcd938x->regmap,
			   WCD938X_HPH_SURGE_HPHLR_SURGE_EN, 0xC0, 0xC0);
zdet_complete:
	snd_soc_component_write(component, WCD938X_ANA_MBHC_BTN5, reg0);
	snd_soc_component_write(component, WCD938X_ANA_MBHC_BTN6, reg1);
	snd_soc_component_write(component, WCD938X_ANA_MBHC_BTN7, reg2);
	/* Turn on 100k pull down on HPHL */
	regmap_update_bits(wcd938x->regmap,
			   WCD938X_ANA_MBHC_MECH, 0x01, 0x01);

	/* For NO-jack, re-enable L_DET_EN after Z-det measurements */
	if (wcd938x->mbhc_cfg.hphl_swh)
		regmap_update_bits(wcd938x->regmap,
				   WCD938X_ANA_MBHC_MECH, 0x80, 0x80);

	snd_soc_component_write(component, WCD938X_MBHC_NEW_ZDET_ANA_CTL, reg4);
	snd_soc_component_write(component, WCD938X_MBHC_CTL_CLK, reg3);
	if (is_fsm_disable)
		regmap_update_bits(wcd938x->regmap,
				   WCD938X_ANA_MBHC_ELECT, 0x80, 0x80);
}

static void wcd938x_mbhc_gnd_det_ctrl(struct snd_soc_component *component,
			bool enable)
{
	if (enable) {
		snd_soc_component_write_field(component, WCD938X_ANA_MBHC_MECH,
					      WCD938X_MBHC_HSG_PULLUP_COMP_EN, 1);
		snd_soc_component_write_field(component, WCD938X_ANA_MBHC_MECH,
					      WCD938X_MBHC_GND_DET_EN_MASK, 1);
	} else {
		snd_soc_component_write_field(component, WCD938X_ANA_MBHC_MECH,
					      WCD938X_MBHC_GND_DET_EN_MASK, 0);
		snd_soc_component_write_field(component, WCD938X_ANA_MBHC_MECH,
					      WCD938X_MBHC_HSG_PULLUP_COMP_EN, 0);
	}
}

static void wcd938x_mbhc_hph_pull_down_ctrl(struct snd_soc_component *component,
					  bool enable)
{
	snd_soc_component_write_field(component, WCD938X_HPH_PA_CTL2,
				      WCD938X_HPHPA_GND_R_MASK, enable);
	snd_soc_component_write_field(component, WCD938X_HPH_PA_CTL2,
				      WCD938X_HPHPA_GND_L_MASK, enable);
}

static void wcd938x_mbhc_moisture_config(struct snd_soc_component *component)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	if (wcd938x->mbhc_cfg.moist_rref == R_OFF) {
		snd_soc_component_write_field(component, WCD938X_MBHC_NEW_CTL_2,
				    WCD938X_M_RTH_CTL_MASK, R_OFF);
		return;
	}

	/* Do not enable moisture detection if jack type is NC */
	if (!wcd938x->mbhc_cfg.hphl_swh) {
		dev_dbg(component->dev, "%s: disable moisture detection for NC\n",
			__func__);
		snd_soc_component_write_field(component, WCD938X_MBHC_NEW_CTL_2,
				    WCD938X_M_RTH_CTL_MASK, R_OFF);
		return;
	}

	snd_soc_component_write_field(component, WCD938X_MBHC_NEW_CTL_2,
			    WCD938X_M_RTH_CTL_MASK, wcd938x->mbhc_cfg.moist_rref);
}

static void wcd938x_mbhc_moisture_detect_en(struct snd_soc_component *component, bool enable)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	if (enable)
		snd_soc_component_write_field(component, WCD938X_MBHC_NEW_CTL_2,
					WCD938X_M_RTH_CTL_MASK, wcd938x->mbhc_cfg.moist_rref);
	else
		snd_soc_component_write_field(component, WCD938X_MBHC_NEW_CTL_2,
				    WCD938X_M_RTH_CTL_MASK, R_OFF);
}

static bool wcd938x_mbhc_get_moisture_status(struct snd_soc_component *component)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	bool ret = false;

	if (wcd938x->mbhc_cfg.moist_rref == R_OFF) {
		snd_soc_component_write_field(component, WCD938X_MBHC_NEW_CTL_2,
				    WCD938X_M_RTH_CTL_MASK, R_OFF);
		goto done;
	}

	/* Do not enable moisture detection if jack type is NC */
	if (!wcd938x->mbhc_cfg.hphl_swh) {
		dev_dbg(component->dev, "%s: disable moisture detection for NC\n",
			__func__);
		snd_soc_component_write_field(component, WCD938X_MBHC_NEW_CTL_2,
				    WCD938X_M_RTH_CTL_MASK, R_OFF);
		goto done;
	}

	/*
	 * If moisture_en is already enabled, then skip to plug type
	 * detection.
	 */
	if (snd_soc_component_read_field(component, WCD938X_MBHC_NEW_CTL_2, WCD938X_M_RTH_CTL_MASK))
		goto done;

	wcd938x_mbhc_moisture_detect_en(component, true);
	/* Read moisture comparator status */
	ret = ((snd_soc_component_read(component, WCD938X_MBHC_NEW_FSM_STATUS)
				& 0x20) ? 0 : 1);

done:
	return ret;

}

static void wcd938x_mbhc_moisture_polling_ctrl(struct snd_soc_component *component,
						bool enable)
{
	snd_soc_component_write_field(component,
			      WCD938X_MBHC_NEW_INT_MOISTURE_DET_POLLING_CTRL,
			      WCD938X_MOISTURE_EN_POLLING_MASK, enable);
}

static const struct wcd_mbhc_cb mbhc_cb = {
	.clk_setup = wcd938x_mbhc_clk_setup,
	.mbhc_bias = wcd938x_mbhc_mbhc_bias_control,
	.set_btn_thr = wcd938x_mbhc_program_btn_thr,
	.micbias_enable_status = wcd938x_mbhc_micb_en_status,
	.hph_pull_up_control_v2 = wcd938x_mbhc_hph_l_pull_up_control,
	.mbhc_micbias_control = wcd938x_mbhc_request_micbias,
	.mbhc_micb_ramp_control = wcd938x_mbhc_micb_ramp_control,
	.mbhc_micb_ctrl_thr_mic = wcd938x_mbhc_micb_ctrl_threshold_mic,
	.compute_impedance = wcd938x_wcd_mbhc_calc_impedance,
	.mbhc_gnd_det_ctrl = wcd938x_mbhc_gnd_det_ctrl,
	.hph_pull_down_ctrl = wcd938x_mbhc_hph_pull_down_ctrl,
	.mbhc_moisture_config = wcd938x_mbhc_moisture_config,
	.mbhc_get_moisture_status = wcd938x_mbhc_get_moisture_status,
	.mbhc_moisture_polling_ctrl = wcd938x_mbhc_moisture_polling_ctrl,
	.mbhc_moisture_detect_en = wcd938x_mbhc_moisture_detect_en,
};

static int wcd938x_get_hph_type(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wcd_mbhc_get_hph_type(wcd938x->wcd_mbhc);

	return 0;
}

static int wcd938x_hph_impedance_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	uint32_t zl, zr;
	bool hphr;
	struct soc_mixer_control *mc;
	struct snd_soc_component *component =
					snd_soc_kcontrol_component(kcontrol);
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);

	mc = (struct soc_mixer_control *)(kcontrol->private_value);
	hphr = mc->shift;
	wcd_mbhc_get_impedance(wcd938x->wcd_mbhc, &zl, &zr);
	dev_dbg(component->dev, "%s: zl=%u(ohms), zr=%u(ohms)\n", __func__, zl, zr);
	ucontrol->value.integer.value[0] = hphr ? zr : zl;

	return 0;
}

static const struct snd_kcontrol_new hph_type_detect_controls[] = {
	SOC_SINGLE_EXT("HPH Type", 0, 0, WCD_MBHC_HPH_STEREO, 0,
		       wcd938x_get_hph_type, NULL),
};

static const struct snd_kcontrol_new impedance_detect_controls[] = {
	SOC_SINGLE_EXT("HPHL Impedance", 0, 0, INT_MAX, 0,
		       wcd938x_hph_impedance_get, NULL),
	SOC_SINGLE_EXT("HPHR Impedance", 0, 1, INT_MAX, 0,
		       wcd938x_hph_impedance_get, NULL),
};

static int wcd938x_mbhc_init(struct snd_soc_component *component)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	struct wcd_mbhc_intr *intr_ids = &wcd938x->intr_ids;

	intr_ids->mbhc_sw_intr = regmap_irq_get_virq(wcd938x->irq_chip,
						    WCD938X_IRQ_MBHC_SW_DET);
	intr_ids->mbhc_btn_press_intr = regmap_irq_get_virq(wcd938x->irq_chip,
							   WCD938X_IRQ_MBHC_BUTTON_PRESS_DET);
	intr_ids->mbhc_btn_release_intr = regmap_irq_get_virq(wcd938x->irq_chip,
							     WCD938X_IRQ_MBHC_BUTTON_RELEASE_DET);
	intr_ids->mbhc_hs_ins_intr = regmap_irq_get_virq(wcd938x->irq_chip,
							WCD938X_IRQ_MBHC_ELECT_INS_REM_LEG_DET);
	intr_ids->mbhc_hs_rem_intr = regmap_irq_get_virq(wcd938x->irq_chip,
							WCD938X_IRQ_MBHC_ELECT_INS_REM_DET);
	intr_ids->hph_left_ocp = regmap_irq_get_virq(wcd938x->irq_chip,
						    WCD938X_IRQ_HPHL_OCP_INT);
	intr_ids->hph_right_ocp = regmap_irq_get_virq(wcd938x->irq_chip,
						     WCD938X_IRQ_HPHR_OCP_INT);

	wcd938x->wcd_mbhc = wcd_mbhc_init(component, &mbhc_cb, intr_ids, wcd_mbhc_fields, true);

	snd_soc_add_component_controls(component, impedance_detect_controls,
				       ARRAY_SIZE(impedance_detect_controls));
	snd_soc_add_component_controls(component, hph_type_detect_controls,
				       ARRAY_SIZE(hph_type_detect_controls));

	return 0;
}
/* END MBHC */

static const struct snd_kcontrol_new wcd938x_snd_controls[] = {
	SOC_SINGLE_EXT("HPHL_COMP Switch", WCD938X_COMP_L, 0, 1, 0,
		       wcd938x_get_compander, wcd938x_set_compander),
	SOC_SINGLE_EXT("HPHR_COMP Switch", WCD938X_COMP_R, 1, 1, 0,
		       wcd938x_get_compander, wcd938x_set_compander),
	SOC_SINGLE_EXT("HPHL Switch", WCD938X_HPH_L, 0, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("HPHR Switch", WCD938X_HPH_R, 0, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("CLSH Switch", WCD938X_CLSH, 0, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("LO Switch", WCD938X_LO, 0, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DSD_L Switch", WCD938X_DSD_L, 0, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DSD_R Switch", WCD938X_DSD_R, 0, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_TLV("HPHL Volume", WCD938X_HPH_L_EN, 0, 0x18, 0, line_gain),
	SOC_SINGLE_TLV("HPHR Volume", WCD938X_HPH_R_EN, 0, 0x18, 0, line_gain),
	WCD938X_EAR_PA_GAIN_TLV("EAR_PA Volume", WCD938X_ANA_EAR_COMPANDER_CTL,
				2, 0x10, 0, ear_pa_gain),
	SOC_SINGLE_EXT("ADC1 Switch", WCD938X_ADC1, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("ADC2 Switch", WCD938X_ADC2, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("ADC3 Switch", WCD938X_ADC3, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("ADC4 Switch", WCD938X_ADC4, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DMIC0 Switch", WCD938X_DMIC0, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DMIC1 Switch", WCD938X_DMIC1, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("MBHC Switch", WCD938X_MBHC, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DMIC2 Switch", WCD938X_DMIC2, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DMIC3 Switch", WCD938X_DMIC3, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DMIC4 Switch", WCD938X_DMIC4, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DMIC5 Switch", WCD938X_DMIC5, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DMIC6 Switch", WCD938X_DMIC6, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("DMIC7 Switch", WCD938X_DMIC7, 1, 1, 0,
		       wcd938x_get_swr_port, wcd938x_set_swr_port),
	SOC_SINGLE_EXT("LDOH Enable Switch", SND_SOC_NOPM, 0, 1, 0,
		       wcd938x_ldoh_get, wcd938x_ldoh_put),
	SOC_SINGLE_EXT("ADC2_BCS Disable Switch", SND_SOC_NOPM, 0, 1, 0,
		       wcd938x_bcs_get, wcd938x_bcs_put),

	SOC_SINGLE_TLV("ADC1 Volume", WCD938X_ANA_TX_CH1, 0, 20, 0, analog_gain),
	SOC_SINGLE_TLV("ADC2 Volume", WCD938X_ANA_TX_CH2, 0, 20, 0, analog_gain),
	SOC_SINGLE_TLV("ADC3 Volume", WCD938X_ANA_TX_CH3, 0, 20, 0, analog_gain),
	SOC_SINGLE_TLV("ADC4 Volume", WCD938X_ANA_TX_CH4, 0, 20, 0, analog_gain),
};

static const struct snd_soc_dapm_widget wcd938x_dapm_widgets[] = {

	/*input widgets*/
	SND_SOC_DAPM_INPUT("AMIC1"),
	SND_SOC_DAPM_INPUT("AMIC2"),
	SND_SOC_DAPM_INPUT("AMIC3"),
	SND_SOC_DAPM_INPUT("AMIC4"),
	SND_SOC_DAPM_INPUT("AMIC5"),
	SND_SOC_DAPM_INPUT("AMIC6"),
	SND_SOC_DAPM_INPUT("AMIC7"),
	SND_SOC_DAPM_MIC("Analog Mic1", NULL),
	SND_SOC_DAPM_MIC("Analog Mic2", NULL),
	SND_SOC_DAPM_MIC("Analog Mic3", NULL),
	SND_SOC_DAPM_MIC("Analog Mic4", NULL),
	SND_SOC_DAPM_MIC("Analog Mic5", NULL),

	/*tx widgets*/
	SND_SOC_DAPM_ADC_E("ADC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd938x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC2", NULL, SND_SOC_NOPM, 1, 0,
			   wcd938x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC3", NULL, SND_SOC_NOPM, 2, 0,
			   wcd938x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC4", NULL, SND_SOC_NOPM, 3, 0,
			   wcd938x_codec_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd938x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC2", NULL, SND_SOC_NOPM, 1, 0,
			   wcd938x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC3", NULL, SND_SOC_NOPM, 2, 0,
			   wcd938x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC4", NULL, SND_SOC_NOPM, 3, 0,
			   wcd938x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC5", NULL, SND_SOC_NOPM, 4, 0,
			   wcd938x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC6", NULL, SND_SOC_NOPM, 5, 0,
			   wcd938x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC7", NULL, SND_SOC_NOPM, 6, 0,
			   wcd938x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC8", NULL, SND_SOC_NOPM, 7, 0,
			   wcd938x_codec_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("ADC1 REQ", SND_SOC_NOPM, 0, 0,
			     NULL, 0, wcd938x_adc_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC2 REQ", SND_SOC_NOPM, 1, 0,
			     NULL, 0, wcd938x_adc_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC3 REQ", SND_SOC_NOPM, 2, 0,
			     NULL, 0, wcd938x_adc_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC4 REQ", SND_SOC_NOPM, 3, 0, NULL, 0,
			     wcd938x_adc_enable_req,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("ADC2 MUX", SND_SOC_NOPM, 0, 0, &tx_adc2_mux),
	SND_SOC_DAPM_MUX("ADC3 MUX", SND_SOC_NOPM, 0, 0, &tx_adc3_mux),
	SND_SOC_DAPM_MUX("ADC4 MUX", SND_SOC_NOPM, 0, 0, &tx_adc4_mux),
	SND_SOC_DAPM_MUX("HDR12 MUX", SND_SOC_NOPM, 0, 0, &tx_hdr12_mux),
	SND_SOC_DAPM_MUX("HDR34 MUX", SND_SOC_NOPM, 0, 0, &tx_hdr34_mux),

	/*tx mixers*/
	SND_SOC_DAPM_MIXER_E("ADC1_MIXER", SND_SOC_NOPM, 0, 0, adc1_switch,
			     ARRAY_SIZE(adc1_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC2_MIXER", SND_SOC_NOPM, 0, 0, adc2_switch,
			     ARRAY_SIZE(adc2_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC3_MIXER", SND_SOC_NOPM, 0, 0, adc3_switch,
			     ARRAY_SIZE(adc3_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("ADC4_MIXER", SND_SOC_NOPM, 0, 0, adc4_switch,
			     ARRAY_SIZE(adc4_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC1_MIXER", SND_SOC_NOPM, 0, 0, dmic1_switch,
			     ARRAY_SIZE(dmic1_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC2_MIXER", SND_SOC_NOPM, 0, 0, dmic2_switch,
			     ARRAY_SIZE(dmic2_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC3_MIXER", SND_SOC_NOPM, 0, 0, dmic3_switch,
			     ARRAY_SIZE(dmic3_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC4_MIXER", SND_SOC_NOPM, 0, 0, dmic4_switch,
			     ARRAY_SIZE(dmic4_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC5_MIXER", SND_SOC_NOPM, 0, 0, dmic5_switch,
			     ARRAY_SIZE(dmic5_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC6_MIXER", SND_SOC_NOPM, 0, 0, dmic6_switch,
			     ARRAY_SIZE(dmic6_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC7_MIXER", SND_SOC_NOPM, 0, 0, dmic7_switch,
			     ARRAY_SIZE(dmic7_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("DMIC8_MIXER", SND_SOC_NOPM, 0, 0, dmic8_switch,
			     ARRAY_SIZE(dmic8_switch), wcd938x_tx_swr_ctrl,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	/* micbias widgets*/
	SND_SOC_DAPM_SUPPLY("MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0,
			    wcd938x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0,
			    wcd938x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0,
			    wcd938x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS4", SND_SOC_NOPM, MIC_BIAS_4, 0,
			    wcd938x_codec_enable_micbias,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),

	/* micbias pull up widgets*/
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS1", SND_SOC_NOPM, MIC_BIAS_1, 0,
				wcd938x_codec_enable_micbias_pullup,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
				SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS2", SND_SOC_NOPM, MIC_BIAS_2, 0,
				wcd938x_codec_enable_micbias_pullup,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
				SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS3", SND_SOC_NOPM, MIC_BIAS_3, 0,
				wcd938x_codec_enable_micbias_pullup,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
				SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VA MIC BIAS4", SND_SOC_NOPM, MIC_BIAS_4, 0,
				wcd938x_codec_enable_micbias_pullup,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
				SND_SOC_DAPM_POST_PMD),

	/*output widgets tx*/
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
	SND_SOC_DAPM_INPUT("IN3_AUX"),

	/*rx widgets*/
	SND_SOC_DAPM_PGA_E("EAR PGA", WCD938X_ANA_EAR, 7, 0, NULL, 0,
			   wcd938x_codec_enable_ear_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("AUX PGA", WCD938X_AUX_AUXPA, 7, 0, NULL, 0,
			   wcd938x_codec_enable_aux_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHL PGA", WCD938X_ANA_HPH, 7, 0, NULL, 0,
			   wcd938x_codec_enable_hphl_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPHR PGA", WCD938X_ANA_HPH, 6, 0, NULL, 0,
			   wcd938x_codec_enable_hphr_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_DAC_E("RDAC1", NULL, SND_SOC_NOPM, 0, 0,
			   wcd938x_codec_hphl_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC2", NULL, SND_SOC_NOPM, 0, 0,
			   wcd938x_codec_hphr_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC3", NULL, SND_SOC_NOPM, 0, 0,
			   wcd938x_codec_ear_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("RDAC4", NULL, SND_SOC_NOPM, 0, 0,
			   wcd938x_codec_aux_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("RDAC3_MUX", SND_SOC_NOPM, 0, 0, &rx_rdac3_mux),

	SND_SOC_DAPM_SUPPLY("VDD_BUCK", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RXCLK", SND_SOC_NOPM, 0, 0,
			    wcd938x_codec_enable_rxclk,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("CLS_H_PORT", 1, SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER_E("RX1", SND_SOC_NOPM, 0, 0, NULL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("RX2", SND_SOC_NOPM, 0, 0, NULL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("RX3", SND_SOC_NOPM, 0, 0, NULL, 0, NULL, 0),

	/* rx mixer widgets*/
	SND_SOC_DAPM_MIXER("EAR_RDAC", SND_SOC_NOPM, 0, 0,
			   ear_rdac_switch, ARRAY_SIZE(ear_rdac_switch)),
	SND_SOC_DAPM_MIXER("AUX_RDAC", SND_SOC_NOPM, 0, 0,
			   aux_rdac_switch, ARRAY_SIZE(aux_rdac_switch)),
	SND_SOC_DAPM_MIXER("HPHL_RDAC", SND_SOC_NOPM, 0, 0,
			   hphl_rdac_switch, ARRAY_SIZE(hphl_rdac_switch)),
	SND_SOC_DAPM_MIXER("HPHR_RDAC", SND_SOC_NOPM, 0, 0,
			   hphr_rdac_switch, ARRAY_SIZE(hphr_rdac_switch)),

	/*output widgets rx*/
	SND_SOC_DAPM_OUTPUT("EAR"),
	SND_SOC_DAPM_OUTPUT("AUX"),
	SND_SOC_DAPM_OUTPUT("HPHL"),
	SND_SOC_DAPM_OUTPUT("HPHR"),

};

static const struct snd_soc_dapm_route wcd938x_audio_map[] = {
	{"ADC1_OUTPUT", NULL, "ADC1_MIXER"},
	{"ADC1_MIXER", "Switch", "ADC1 REQ"},
	{"ADC1 REQ", NULL, "ADC1"},
	{"ADC1", NULL, "AMIC1"},

	{"ADC2_OUTPUT", NULL, "ADC2_MIXER"},
	{"ADC2_MIXER", "Switch", "ADC2 REQ"},
	{"ADC2 REQ", NULL, "ADC2"},
	{"ADC2", NULL, "HDR12 MUX"},
	{"HDR12 MUX", "NO_HDR12", "ADC2 MUX"},
	{"HDR12 MUX", "HDR12", "AMIC1"},
	{"ADC2 MUX", "INP3", "AMIC3"},
	{"ADC2 MUX", "INP2", "AMIC2"},

	{"ADC3_OUTPUT", NULL, "ADC3_MIXER"},
	{"ADC3_MIXER", "Switch", "ADC3 REQ"},
	{"ADC3 REQ", NULL, "ADC3"},
	{"ADC3", NULL, "HDR34 MUX"},
	{"HDR34 MUX", "NO_HDR34", "ADC3 MUX"},
	{"HDR34 MUX", "HDR34", "AMIC5"},
	{"ADC3 MUX", "INP4", "AMIC4"},
	{"ADC3 MUX", "INP6", "AMIC6"},

	{"ADC4_OUTPUT", NULL, "ADC4_MIXER"},
	{"ADC4_MIXER", "Switch", "ADC4 REQ"},
	{"ADC4 REQ", NULL, "ADC4"},
	{"ADC4", NULL, "ADC4 MUX"},
	{"ADC4 MUX", "INP5", "AMIC5"},
	{"ADC4 MUX", "INP7", "AMIC7"},

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

	{"IN3_AUX", NULL, "VDD_BUCK"},
	{"IN3_AUX", NULL, "CLS_H_PORT"},
	{"RX3", NULL, "IN3_AUX"},
	{"RDAC4", NULL, "RX3"},
	{"RX3", NULL, "RXCLK"},
	{"AUX_RDAC", "Switch", "RDAC4"},
	{"AUX PGA", NULL, "AUX_RDAC"},
	{"AUX", NULL, "AUX PGA"},

	{"RDAC3_MUX", "RX3", "RX3"},
	{"RDAC3_MUX", "RX1", "RX1"},
	{"RDAC3", NULL, "RDAC3_MUX"},
	{"EAR_RDAC", "Switch", "RDAC3"},
	{"EAR PGA", NULL, "EAR_RDAC"},
	{"EAR", NULL, "EAR PGA"},
};

static int wcd938x_set_micbias_data(struct wcd938x_priv *wcd938x)
{
	int vout_ctl_1, vout_ctl_2, vout_ctl_3, vout_ctl_4;

	/* set micbias voltage */
	vout_ctl_1 = wcd938x_get_micb_vout_ctl_val(wcd938x->micb1_mv);
	vout_ctl_2 = wcd938x_get_micb_vout_ctl_val(wcd938x->micb2_mv);
	vout_ctl_3 = wcd938x_get_micb_vout_ctl_val(wcd938x->micb3_mv);
	vout_ctl_4 = wcd938x_get_micb_vout_ctl_val(wcd938x->micb4_mv);
	if (vout_ctl_1 < 0 || vout_ctl_2 < 0 || vout_ctl_3 < 0 || vout_ctl_4 < 0)
		return -EINVAL;

	regmap_update_bits(wcd938x->regmap, WCD938X_ANA_MICB1,
			   WCD938X_MICB_VOUT_MASK, vout_ctl_1);
	regmap_update_bits(wcd938x->regmap, WCD938X_ANA_MICB2,
			   WCD938X_MICB_VOUT_MASK, vout_ctl_2);
	regmap_update_bits(wcd938x->regmap, WCD938X_ANA_MICB3,
			   WCD938X_MICB_VOUT_MASK, vout_ctl_3);
	regmap_update_bits(wcd938x->regmap, WCD938X_ANA_MICB4,
			   WCD938X_MICB_VOUT_MASK, vout_ctl_4);

	return 0;
}

static irqreturn_t wcd938x_wd_handle_irq(int irq, void *data)
{
	return IRQ_HANDLED;
}

static struct irq_chip wcd_irq_chip = {
	.name = "WCD938x",
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

static int wcd938x_irq_init(struct wcd938x_priv *wcd, struct device *dev)
{

	wcd->virq = irq_domain_add_linear(NULL, 1, &wcd_domain_ops, NULL);
	if (!(wcd->virq)) {
		dev_err(dev, "%s: Failed to add IRQ domain\n", __func__);
		return -EINVAL;
	}

	return devm_regmap_add_irq_chip(dev, wcd->regmap,
					irq_create_mapping(wcd->virq, 0),
					IRQF_ONESHOT, 0, &wcd938x_regmap_irq_chip,
					&wcd->irq_chip);
}

static int wcd938x_soc_codec_probe(struct snd_soc_component *component)
{
	struct wcd938x_priv *wcd938x = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	int ret, i;

	snd_soc_component_init_regmap(component, wcd938x->regmap);

	wcd938x->variant = snd_soc_component_read_field(component,
						 WCD938X_DIGITAL_EFUSE_REG_0,
						 WCD938X_ID_MASK);

	wcd938x->clsh_info = wcd_clsh_ctrl_alloc(component, WCD938X);

	wcd938x_io_init(wcd938x);
	/* Set all interrupts as edge triggered */
	for (i = 0; i < wcd938x_regmap_irq_chip.num_regs; i++) {
		regmap_write(wcd938x->regmap,
			     (WCD938X_DIGITAL_INTR_LEVEL_0 + i), 0);
	}

	wcd938x->hphr_pdm_wd_int = regmap_irq_get_virq(wcd938x->irq_chip,
						       WCD938X_IRQ_HPHR_PDM_WD_INT);
	wcd938x->hphl_pdm_wd_int = regmap_irq_get_virq(wcd938x->irq_chip,
						       WCD938X_IRQ_HPHL_PDM_WD_INT);
	wcd938x->aux_pdm_wd_int = regmap_irq_get_virq(wcd938x->irq_chip,
						       WCD938X_IRQ_AUX_PDM_WD_INT);

	/* Request for watchdog interrupt */
	ret = request_threaded_irq(wcd938x->hphr_pdm_wd_int, NULL, wcd938x_wd_handle_irq,
				   IRQF_ONESHOT | IRQF_TRIGGER_RISING,
				   "HPHR PDM WD INT", wcd938x);
	if (ret)
		dev_err(dev, "Failed to request HPHR WD interrupt (%d)\n", ret);

	ret = request_threaded_irq(wcd938x->hphl_pdm_wd_int, NULL, wcd938x_wd_handle_irq,
				   IRQF_ONESHOT | IRQF_TRIGGER_RISING,
				   "HPHL PDM WD INT", wcd938x);
	if (ret)
		dev_err(dev, "Failed to request HPHL WD interrupt (%d)\n", ret);

	ret = request_threaded_irq(wcd938x->aux_pdm_wd_int, NULL, wcd938x_wd_handle_irq,
				   IRQF_ONESHOT | IRQF_TRIGGER_RISING,
				   "AUX PDM WD INT", wcd938x);
	if (ret)
		dev_err(dev, "Failed to request Aux WD interrupt (%d)\n", ret);

	/* Disable watchdog interrupt for HPH and AUX */
	disable_irq_nosync(wcd938x->hphr_pdm_wd_int);
	disable_irq_nosync(wcd938x->hphl_pdm_wd_int);
	disable_irq_nosync(wcd938x->aux_pdm_wd_int);

	switch (wcd938x->variant) {
	case WCD9380:
		ret = snd_soc_add_component_controls(component, wcd9380_snd_controls,
					ARRAY_SIZE(wcd9380_snd_controls));
		if (ret < 0) {
			dev_err(component->dev,
				"%s: Failed to add snd ctrls for variant: %d\n",
				__func__, wcd938x->variant);
			goto err;
		}
		break;
	case WCD9385:
		ret = snd_soc_add_component_controls(component, wcd9385_snd_controls,
					ARRAY_SIZE(wcd9385_snd_controls));
		if (ret < 0) {
			dev_err(component->dev,
				"%s: Failed to add snd ctrls for variant: %d\n",
				__func__, wcd938x->variant);
			goto err;
		}
		break;
	default:
		break;
	}

	ret = wcd938x_mbhc_init(component);
	if (ret)
		dev_err(component->dev,  "mbhc initialization failed\n");
err:
	return ret;
}

static int wcd938x_codec_set_jack(struct snd_soc_component *comp,
				  struct snd_soc_jack *jack, void *data)
{
	struct wcd938x_priv *wcd = dev_get_drvdata(comp->dev);

	if (jack)
		return wcd_mbhc_start(wcd->wcd_mbhc, &wcd->mbhc_cfg, jack);
	else
		wcd_mbhc_stop(wcd->wcd_mbhc);

	return 0;
}

static const struct snd_soc_component_driver soc_codec_dev_wcd938x = {
	.name = "wcd938x_codec",
	.probe = wcd938x_soc_codec_probe,
	.controls = wcd938x_snd_controls,
	.num_controls = ARRAY_SIZE(wcd938x_snd_controls),
	.dapm_widgets = wcd938x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wcd938x_dapm_widgets),
	.dapm_routes = wcd938x_audio_map,
	.num_dapm_routes = ARRAY_SIZE(wcd938x_audio_map),
	.set_jack = wcd938x_codec_set_jack,
	.endianness = 1,
};

static void wcd938x_dt_parse_micbias_info(struct device *dev, struct wcd938x_priv *wcd)
{
	struct device_node *np = dev->of_node;
	u32 prop_val = 0;
	int rc = 0;

	rc = of_property_read_u32(np, "qcom,micbias1-microvolt",  &prop_val);
	if (!rc)
		wcd->micb1_mv = prop_val/1000;
	else
		dev_info(dev, "%s: Micbias1 DT property not found\n", __func__);

	rc = of_property_read_u32(np, "qcom,micbias2-microvolt",  &prop_val);
	if (!rc)
		wcd->micb2_mv = prop_val/1000;
	else
		dev_info(dev, "%s: Micbias2 DT property not found\n", __func__);

	rc = of_property_read_u32(np, "qcom,micbias3-microvolt", &prop_val);
	if (!rc)
		wcd->micb3_mv = prop_val/1000;
	else
		dev_info(dev, "%s: Micbias3 DT property not found\n", __func__);

	rc = of_property_read_u32(np, "qcom,micbias4-microvolt",  &prop_val);
	if (!rc)
		wcd->micb4_mv = prop_val/1000;
	else
		dev_info(dev, "%s: Micbias4 DT property not found\n", __func__);
}

static bool wcd938x_swap_gnd_mic(struct snd_soc_component *component, bool active)
{
	int value;

	struct wcd938x_priv *wcd938x;

	wcd938x = snd_soc_component_get_drvdata(component);

	value = gpiod_get_value(wcd938x->us_euro_gpio);

	gpiod_set_value(wcd938x->us_euro_gpio, !value);

	return true;
}


static int wcd938x_populate_dt_data(struct wcd938x_priv *wcd938x, struct device *dev)
{
	struct wcd_mbhc_config *cfg = &wcd938x->mbhc_cfg;
	int ret;

	wcd938x->reset_gpio = of_get_named_gpio(dev->of_node, "reset-gpios", 0);
	if (wcd938x->reset_gpio < 0) {
		dev_err(dev, "Failed to get reset gpio: err = %d\n",
			wcd938x->reset_gpio);
		return wcd938x->reset_gpio;
	}

	wcd938x->us_euro_gpio = devm_gpiod_get_optional(dev, "us-euro",
						GPIOD_OUT_LOW);
	if (IS_ERR(wcd938x->us_euro_gpio)) {
		dev_err(dev, "us-euro swap Control GPIO not found\n");
		return PTR_ERR(wcd938x->us_euro_gpio);
	}

	cfg->swap_gnd_mic = wcd938x_swap_gnd_mic;

	wcd938x->supplies[0].supply = "vdd-rxtx";
	wcd938x->supplies[1].supply = "vdd-io";
	wcd938x->supplies[2].supply = "vdd-buck";
	wcd938x->supplies[3].supply = "vdd-mic-bias";

	ret = regulator_bulk_get(dev, WCD938X_MAX_SUPPLY, wcd938x->supplies);
	if (ret) {
		dev_err(dev, "Failed to get supplies: err = %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(WCD938X_MAX_SUPPLY, wcd938x->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable supplies: err = %d\n", ret);
		return ret;
	}

	wcd938x_dt_parse_micbias_info(dev, wcd938x);

	cfg->mbhc_micbias = MIC_BIAS_2;
	cfg->anc_micbias = MIC_BIAS_2;
	cfg->v_hs_max = WCD_MBHC_HS_V_MAX;
	cfg->num_btn = WCD938X_MBHC_MAX_BUTTONS;
	cfg->micb_mv = wcd938x->micb2_mv;
	cfg->linein_th = 5000;
	cfg->hs_thr = 1700;
	cfg->hph_thr = 50;

	wcd_dt_parse_mbhc_data(dev, cfg);

	return 0;
}

static int wcd938x_reset(struct wcd938x_priv *wcd938x)
{
	gpio_direction_output(wcd938x->reset_gpio, 0);
	/* 20us sleep required after pulling the reset gpio to LOW */
	usleep_range(20, 30);
	gpio_set_value(wcd938x->reset_gpio, 1);
	/* 20us sleep required after pulling the reset gpio to HIGH */
	usleep_range(20, 30);

	return 0;
}

static int wcd938x_codec_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct wcd938x_priv *wcd938x = dev_get_drvdata(dai->dev);
	struct wcd938x_sdw_priv *wcd = wcd938x->sdw_priv[dai->id];

	return wcd938x_sdw_hw_params(wcd, substream, params, dai);
}

static int wcd938x_codec_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct wcd938x_priv *wcd938x = dev_get_drvdata(dai->dev);
	struct wcd938x_sdw_priv *wcd = wcd938x->sdw_priv[dai->id];

	return wcd938x_sdw_free(wcd, substream, dai);
}

static int wcd938x_codec_set_sdw_stream(struct snd_soc_dai *dai,
				  void *stream, int direction)
{
	struct wcd938x_priv *wcd938x = dev_get_drvdata(dai->dev);
	struct wcd938x_sdw_priv *wcd = wcd938x->sdw_priv[dai->id];

	return wcd938x_sdw_set_sdw_stream(wcd, dai, stream, direction);

}

static const struct snd_soc_dai_ops wcd938x_sdw_dai_ops = {
	.hw_params = wcd938x_codec_hw_params,
	.hw_free = wcd938x_codec_free,
	.set_stream = wcd938x_codec_set_sdw_stream,
};

static struct snd_soc_dai_driver wcd938x_dais[] = {
	[0] = {
		.name = "wcd938x-sdw-rx",
		.playback = {
			.stream_name = "WCD AIF1 Playback",
			.rates = WCD938X_RATES_MASK | WCD938X_FRAC_RATES_MASK,
			.formats = WCD938X_FORMATS_S16_S24_LE,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wcd938x_sdw_dai_ops,
	},
	[1] = {
		.name = "wcd938x-sdw-tx",
		.capture = {
			.stream_name = "WCD AIF1 Capture",
			.rates = WCD938X_RATES_MASK,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wcd938x_sdw_dai_ops,
	},
};

static int wcd938x_bind(struct device *dev)
{
	struct wcd938x_priv *wcd938x = dev_get_drvdata(dev);
	int ret;

	ret = component_bind_all(dev, wcd938x);
	if (ret) {
		dev_err(dev, "%s: Slave bind failed, ret = %d\n",
			__func__, ret);
		return ret;
	}

	wcd938x->rxdev = wcd938x_sdw_device_get(wcd938x->rxnode);
	if (!wcd938x->rxdev) {
		dev_err(dev, "could not find slave with matching of node\n");
		return -EINVAL;
	}
	wcd938x->sdw_priv[AIF1_PB] = dev_get_drvdata(wcd938x->rxdev);
	wcd938x->sdw_priv[AIF1_PB]->wcd938x = wcd938x;

	wcd938x->txdev = wcd938x_sdw_device_get(wcd938x->txnode);
	if (!wcd938x->txdev) {
		dev_err(dev, "could not find txslave with matching of node\n");
		return -EINVAL;
	}
	wcd938x->sdw_priv[AIF1_CAP] = dev_get_drvdata(wcd938x->txdev);
	wcd938x->sdw_priv[AIF1_CAP]->wcd938x = wcd938x;
	wcd938x->tx_sdw_dev = dev_to_sdw_dev(wcd938x->txdev);
	if (!wcd938x->tx_sdw_dev) {
		dev_err(dev, "could not get txslave with matching of dev\n");
		return -EINVAL;
	}

	/* As TX is main CSR reg interface, which should not be suspended first.
	 * expicilty add the dependency link */
	if (!device_link_add(wcd938x->rxdev, wcd938x->txdev, DL_FLAG_STATELESS |
			    DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "could not devlink tx and rx\n");
		return -EINVAL;
	}

	if (!device_link_add(dev, wcd938x->txdev, DL_FLAG_STATELESS |
					DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "could not devlink wcd and tx\n");
		return -EINVAL;
	}

	if (!device_link_add(dev, wcd938x->rxdev, DL_FLAG_STATELESS |
					DL_FLAG_PM_RUNTIME)) {
		dev_err(dev, "could not devlink wcd and rx\n");
		return -EINVAL;
	}

	wcd938x->regmap = devm_regmap_init_sdw(wcd938x->tx_sdw_dev, &wcd938x_regmap_config);
	if (IS_ERR(wcd938x->regmap)) {
		dev_err(dev, "%s: tx csr regmap not found\n", __func__);
		return PTR_ERR(wcd938x->regmap);
	}

	ret = wcd938x_irq_init(wcd938x, dev);
	if (ret) {
		dev_err(dev, "%s: IRQ init failed: %d\n", __func__, ret);
		return ret;
	}

	wcd938x->sdw_priv[AIF1_PB]->slave_irq = wcd938x->virq;
	wcd938x->sdw_priv[AIF1_CAP]->slave_irq = wcd938x->virq;

	ret = wcd938x_set_micbias_data(wcd938x);
	if (ret < 0) {
		dev_err(dev, "%s: bad micbias pdata\n", __func__);
		return ret;
	}

	ret = snd_soc_register_component(dev, &soc_codec_dev_wcd938x,
					 wcd938x_dais, ARRAY_SIZE(wcd938x_dais));
	if (ret)
		dev_err(dev, "%s: Codec registration failed\n",
				__func__);

	return ret;

}

static void wcd938x_unbind(struct device *dev)
{
	struct wcd938x_priv *wcd938x = dev_get_drvdata(dev);

	device_link_remove(dev, wcd938x->txdev);
	device_link_remove(dev, wcd938x->rxdev);
	device_link_remove(wcd938x->rxdev, wcd938x->txdev);
	snd_soc_unregister_component(dev);
	component_unbind_all(dev, wcd938x);
}

static const struct component_master_ops wcd938x_comp_ops = {
	.bind   = wcd938x_bind,
	.unbind = wcd938x_unbind,
};

static int wcd938x_add_slave_components(struct wcd938x_priv *wcd938x,
					struct device *dev,
					struct component_match **matchptr)
{
	struct device_node *np;

	np = dev->of_node;

	wcd938x->rxnode = of_parse_phandle(np, "qcom,rx-device", 0);
	if (!wcd938x->rxnode) {
		dev_err(dev, "%s: Rx-device node not defined\n", __func__);
		return -ENODEV;
	}

	of_node_get(wcd938x->rxnode);
	component_match_add_release(dev, matchptr, component_release_of,
				    component_compare_of, wcd938x->rxnode);

	wcd938x->txnode = of_parse_phandle(np, "qcom,tx-device", 0);
	if (!wcd938x->txnode) {
		dev_err(dev, "%s: Tx-device node not defined\n", __func__);
		return -ENODEV;
	}
	of_node_get(wcd938x->txnode);
	component_match_add_release(dev, matchptr, component_release_of,
				    component_compare_of, wcd938x->txnode);
	return 0;
}

static int wcd938x_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct wcd938x_priv *wcd938x = NULL;
	struct device *dev = &pdev->dev;
	int ret;

	wcd938x = devm_kzalloc(dev, sizeof(struct wcd938x_priv),
				GFP_KERNEL);
	if (!wcd938x)
		return -ENOMEM;

	dev_set_drvdata(dev, wcd938x);
	mutex_init(&wcd938x->micb_lock);

	ret = wcd938x_populate_dt_data(wcd938x, dev);
	if (ret) {
		dev_err(dev, "%s: Fail to obtain platform data\n", __func__);
		return -EINVAL;
	}

	ret = wcd938x_add_slave_components(wcd938x, dev, &match);
	if (ret)
		return ret;

	wcd938x_reset(wcd938x);

	ret = component_master_add_with_match(dev, &wcd938x_comp_ops, match);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;
}

static int wcd938x_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &wcd938x_comp_ops);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id wcd938x_dt_match[] = {
	{ .compatible = "qcom,wcd9380-codec" },
	{ .compatible = "qcom,wcd9385-codec" },
	{}
};
MODULE_DEVICE_TABLE(of, wcd938x_dt_match);
#endif

static struct platform_driver wcd938x_codec_driver = {
	.probe = wcd938x_probe,
	.remove = wcd938x_remove,
	.driver = {
		.name = "wcd938x_codec",
		.of_match_table = of_match_ptr(wcd938x_dt_match),
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(wcd938x_codec_driver);
MODULE_DESCRIPTION("WCD938X Codec driver");
MODULE_LICENSE("GPL");
