// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/component.h>
#include <sound/soc.h>
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

struct wcd938x_priv {
	struct sdw_slave *tx_sdw_dev;
	struct wcd938x_sdw_priv *sdw_priv[NUM_CODEC_DAIS];
	struct device *txdev;
	struct device *rxdev;
	struct device_node *rxnode, *txnode;
	struct regmap *regmap;
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

enum {
	MIC_BIAS_1 = 1,
	MIC_BIAS_2,
	MIC_BIAS_3,
	MIC_BIAS_4
};

enum {
	MICB_PULLUP_ENABLE,
	MICB_PULLUP_DISABLE,
	MICB_ENABLE,
	MICB_DISABLE,
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
		return 0;

	if (reg == WCD938X_DIGITAL_SWR_TX_CLK_RATE)
		return true;

	if (wcd938x_readonly_register(dev, reg))
		return true;

	return false;
}

struct regmap_config wcd938x_regmap_config = {
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
EXPORT_SYMBOL_GPL(wcd938x_regmap_config);

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
	.type_base = WCD938X_DIGITAL_INTR_LEVEL_0,
	.ack_base = WCD938X_DIGITAL_INTR_CLEAR_0,
	.use_ack = 1,
	.runtime_pm = true,
	.irq_drv_data = NULL,
};

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

static int wcd938x_get_micb_vout_ctl_val(u32 micb_mv)
{
	/* min micbias voltage is 1V and maximum is 2.85V */
	if (micb_mv < 1000 || micb_mv > 2850)
		return -EINVAL;

	return (micb_mv - 1000) / 50;
}

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

	ret = wcd938x_irq_init(wcd938x, component->dev);
	if (ret) {
		dev_err(component->dev, "%s: IRQ init failed: %d\n",
			__func__, ret);
		return ret;
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

	return ret;
}

static const struct snd_soc_component_driver soc_codec_dev_wcd938x = {
	.name = "wcd938x_codec",
	.probe = wcd938x_soc_codec_probe,
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

static int wcd938x_populate_dt_data(struct wcd938x_priv *wcd938x, struct device *dev)
{
	int ret;

	wcd938x->reset_gpio = of_get_named_gpio(dev->of_node, "reset-gpios", 0);
	if (wcd938x->reset_gpio < 0) {
		dev_err(dev, "Failed to get reset gpio: err = %d\n",
			wcd938x->reset_gpio);
		return wcd938x->reset_gpio;
	}

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

int wcd938x_handle_sdw_irq(struct wcd938x_sdw_priv *wcd)
{
	struct wcd938x_priv *wcd938x = wcd->wcd938x;
	struct irq_domain *slave_irq = wcd938x->virq;
	u32 sts1, sts2, sts3;

	do {
		handle_nested_irq(irq_find_mapping(slave_irq, 0));
		regmap_read(wcd938x->regmap, WCD938X_DIGITAL_INTR_STATUS_0, &sts1);
		regmap_read(wcd938x->regmap, WCD938X_DIGITAL_INTR_STATUS_1, &sts2);
		regmap_read(wcd938x->regmap, WCD938X_DIGITAL_INTR_STATUS_2, &sts3);

	} while (sts1 || sts2 || sts3);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wcd938x_handle_sdw_irq);

static struct snd_soc_dai_ops wcd938x_sdw_dai_ops = {
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

	snd_soc_unregister_component(dev);
	component_unbind_all(dev, wcd938x);
}

static const struct component_master_ops wcd938x_comp_ops = {
	.bind   = wcd938x_bind,
	.unbind = wcd938x_unbind,
};

static int wcd938x_compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static void wcd938x_release_of(struct device *dev, void *data)
{
	of_node_put(data);
}

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
	component_match_add_release(dev, matchptr, wcd938x_release_of,
				    wcd938x_compare_of,	wcd938x->rxnode);

	wcd938x->txnode = of_parse_phandle(np, "qcom,tx-device", 0);
	if (!wcd938x->txnode) {
		dev_err(dev, "%s: Tx-device node not defined\n", __func__);
		return -ENODEV;
	}
	of_node_get(wcd938x->txnode);
	component_match_add_release(dev, matchptr, wcd938x_release_of,
				    wcd938x_compare_of,	wcd938x->txnode);
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

	return ret;
}

static int wcd938x_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &wcd938x_comp_ops);

	return 0;
}

static const struct of_device_id wcd938x_dt_match[] = {
	{ .compatible = "qcom,wcd9380-codec" },
	{ .compatible = "qcom,wcd9385-codec" },
	{}
};
MODULE_DEVICE_TABLE(of, wcd938x_dt_match);

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
