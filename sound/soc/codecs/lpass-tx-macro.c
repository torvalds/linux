// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <linux/of_clk.h>
#include <linux/clk-provider.h>

#include "lpass-macro-common.h"

#define CDC_TX_CLK_RST_CTRL_MCLK_CONTROL (0x0000)
#define CDC_TX_MCLK_EN_MASK		BIT(0)
#define CDC_TX_MCLK_ENABLE		BIT(0)
#define CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL (0x0004)
#define CDC_TX_FS_CNT_EN_MASK		BIT(0)
#define CDC_TX_FS_CNT_ENABLE		BIT(0)
#define CDC_TX_CLK_RST_CTRL_SWR_CONTROL	(0x0008)
#define CDC_TX_SWR_RESET_MASK		BIT(1)
#define CDC_TX_SWR_RESET_ENABLE		BIT(1)
#define CDC_TX_SWR_CLK_EN_MASK		BIT(0)
#define CDC_TX_SWR_CLK_ENABLE		BIT(0)
#define CDC_TX_TOP_CSR_TOP_CFG0		(0x0080)
#define CDC_TX_TOP_CSR_ANC_CFG		(0x0084)
#define CDC_TX_TOP_CSR_SWR_CTRL		(0x0088)
#define CDC_TX_TOP_CSR_FREQ_MCLK	(0x0090)
#define CDC_TX_TOP_CSR_DEBUG_BUS	(0x0094)
#define CDC_TX_TOP_CSR_DEBUG_EN		(0x0098)
#define CDC_TX_TOP_CSR_TX_I2S_CTL	(0x00A4)
#define CDC_TX_TOP_CSR_I2S_CLK		(0x00A8)
#define CDC_TX_TOP_CSR_I2S_RESET	(0x00AC)
#define CDC_TX_TOP_CSR_SWR_DMICn_CTL(n)	(0x00C0 + n * 0x4)
#define CDC_TX_TOP_CSR_SWR_DMIC0_CTL	(0x00C0)
/* Default divider for AMIC and DMIC clock: DIV2 */
#define CDC_TX_SWR_MIC_CLK_DEFAULT	0
#define CDC_TX_SWR_DMIC_CLK_SEL_MASK	GENMASK(3, 1)
#define CDC_TX_TOP_CSR_SWR_DMIC1_CTL	(0x00C4)
#define CDC_TX_TOP_CSR_SWR_DMIC2_CTL	(0x00C8)
#define CDC_TX_TOP_CSR_SWR_DMIC3_CTL	(0x00CC)
#define CDC_TX_TOP_CSR_SWR_AMIC0_CTL	(0x00D0)
#define CDC_TX_TOP_CSR_SWR_AMIC1_CTL	(0x00D4)
#define CDC_TX_INP_MUX_ADC_MUXn_CFG0(n)	(0x0100 + 0x8 * n)
#define CDC_TX_MACRO_SWR_MIC_MUX_SEL_MASK GENMASK(3, 0)
#define CDC_TX_MACRO_DMIC_MUX_SEL_MASK GENMASK(7, 4)
#define CDC_TX_INP_MUX_ADC_MUX0_CFG0	(0x0100)
#define CDC_TX_INP_MUX_ADC_MUXn_CFG1(n)	(0x0104 + 0x8 * n)
#define CDC_TX_INP_MUX_ADC_MUX0_CFG1	(0x0104)
#define CDC_TX_INP_MUX_ADC_MUX1_CFG0	(0x0108)
#define CDC_TX_INP_MUX_ADC_MUX1_CFG1	(0x010C)
#define CDC_TX_INP_MUX_ADC_MUX2_CFG0	(0x0110)
#define CDC_TX_INP_MUX_ADC_MUX2_CFG1	(0x0114)
#define CDC_TX_INP_MUX_ADC_MUX3_CFG0	(0x0118)
#define CDC_TX_INP_MUX_ADC_MUX3_CFG1	(0x011C)
#define CDC_TX_INP_MUX_ADC_MUX4_CFG0	(0x0120)
#define CDC_TX_INP_MUX_ADC_MUX4_CFG1	(0x0124)
#define CDC_TX_INP_MUX_ADC_MUX5_CFG0	(0x0128)
#define CDC_TX_INP_MUX_ADC_MUX5_CFG1	(0x012C)
#define CDC_TX_INP_MUX_ADC_MUX6_CFG0	(0x0130)
#define CDC_TX_INP_MUX_ADC_MUX6_CFG1	(0x0134)
#define CDC_TX_INP_MUX_ADC_MUX7_CFG0	(0x0138)
#define CDC_TX_INP_MUX_ADC_MUX7_CFG1	(0x013C)
#define CDC_TX_ANC0_CLK_RESET_CTL	(0x0200)
#define CDC_TX_ANC0_MODE_1_CTL		(0x0204)
#define CDC_TX_ANC0_MODE_2_CTL		(0x0208)
#define CDC_TX_ANC0_FF_SHIFT		(0x020C)
#define CDC_TX_ANC0_FB_SHIFT		(0x0210)
#define CDC_TX_ANC0_LPF_FF_A_CTL	(0x0214)
#define CDC_TX_ANC0_LPF_FF_B_CTL	(0x0218)
#define CDC_TX_ANC0_LPF_FB_CTL		(0x021C)
#define CDC_TX_ANC0_SMLPF_CTL		(0x0220)
#define CDC_TX_ANC0_DCFLT_SHIFT_CTL	(0x0224)
#define CDC_TX_ANC0_IIR_ADAPT_CTL	(0x0228)
#define CDC_TX_ANC0_IIR_COEFF_1_CTL	(0x022C)
#define CDC_TX_ANC0_IIR_COEFF_2_CTL	(0x0230)
#define CDC_TX_ANC0_FF_A_GAIN_CTL	(0x0234)
#define CDC_TX_ANC0_FF_B_GAIN_CTL	(0x0238)
#define CDC_TX_ANC0_FB_GAIN_CTL		(0x023C)
#define CDC_TXn_TX_PATH_CTL(n)		(0x0400 + 0x80 * n)
#define CDC_TXn_PCM_RATE_MASK		GENMASK(3, 0)
#define CDC_TXn_PGA_MUTE_MASK		BIT(4)
#define CDC_TXn_CLK_EN_MASK		BIT(5)
#define CDC_TX0_TX_PATH_CTL		(0x0400)
#define CDC_TXn_TX_PATH_CFG0(n)		(0x0404 + 0x80 * n)
#define CDC_TX0_TX_PATH_CFG0		(0x0404)
#define CDC_TXn_PH_EN_MASK		BIT(0)
#define CDC_TXn_ADC_MODE_MASK		GENMASK(2, 1)
#define CDC_TXn_HPF_CUT_FREQ_MASK	GENMASK(6, 5)
#define CDC_TXn_ADC_DMIC_SEL_MASK	BIT(7)
#define CDC_TX0_TX_PATH_CFG1		(0x0408)
#define CDC_TXn_TX_VOL_CTL(n)		(0x040C + 0x80 * n)
#define CDC_TX0_TX_VOL_CTL		(0x040C)
#define CDC_TX0_TX_PATH_SEC0		(0x0410)
#define CDC_TX0_TX_PATH_SEC1		(0x0414)
#define CDC_TXn_TX_PATH_SEC2(n)		(0x0418 + 0x80 * n)
#define CDC_TXn_HPF_F_CHANGE_MASK	 BIT(1)
#define CDC_TXn_HPF_ZERO_GATE_MASK	 BIT(0)
#define CDC_TX0_TX_PATH_SEC2		(0x0418)
#define CDC_TX0_TX_PATH_SEC3		(0x041C)
#define CDC_TX0_TX_PATH_SEC4		(0x0420)
#define CDC_TX0_TX_PATH_SEC5		(0x0424)
#define CDC_TX0_TX_PATH_SEC6		(0x0428)
#define CDC_TX0_TX_PATH_SEC7		(0x042C)
#define CDC_TX0_MBHC_CTL_EN_MASK	BIT(6)
#define CDC_TX1_TX_PATH_CTL		(0x0480)
#define CDC_TX1_TX_PATH_CFG0		(0x0484)
#define CDC_TX1_TX_PATH_CFG1		(0x0488)
#define CDC_TX1_TX_VOL_CTL		(0x048C)
#define CDC_TX1_TX_PATH_SEC0		(0x0490)
#define CDC_TX1_TX_PATH_SEC1		(0x0494)
#define CDC_TX1_TX_PATH_SEC2		(0x0498)
#define CDC_TX1_TX_PATH_SEC3		(0x049C)
#define CDC_TX1_TX_PATH_SEC4		(0x04A0)
#define CDC_TX1_TX_PATH_SEC5		(0x04A4)
#define CDC_TX1_TX_PATH_SEC6		(0x04A8)
#define CDC_TX2_TX_PATH_CTL		(0x0500)
#define CDC_TX2_TX_PATH_CFG0		(0x0504)
#define CDC_TX2_TX_PATH_CFG1		(0x0508)
#define CDC_TX2_TX_VOL_CTL		(0x050C)
#define CDC_TX2_TX_PATH_SEC0		(0x0510)
#define CDC_TX2_TX_PATH_SEC1		(0x0514)
#define CDC_TX2_TX_PATH_SEC2		(0x0518)
#define CDC_TX2_TX_PATH_SEC3		(0x051C)
#define CDC_TX2_TX_PATH_SEC4		(0x0520)
#define CDC_TX2_TX_PATH_SEC5		(0x0524)
#define CDC_TX2_TX_PATH_SEC6		(0x0528)
#define CDC_TX3_TX_PATH_CTL		(0x0580)
#define CDC_TX3_TX_PATH_CFG0		(0x0584)
#define CDC_TX3_TX_PATH_CFG1		(0x0588)
#define CDC_TX3_TX_VOL_CTL		(0x058C)
#define CDC_TX3_TX_PATH_SEC0		(0x0590)
#define CDC_TX3_TX_PATH_SEC1		(0x0594)
#define CDC_TX3_TX_PATH_SEC2		(0x0598)
#define CDC_TX3_TX_PATH_SEC3		(0x059C)
#define CDC_TX3_TX_PATH_SEC4		(0x05A0)
#define CDC_TX3_TX_PATH_SEC5		(0x05A4)
#define CDC_TX3_TX_PATH_SEC6		(0x05A8)
#define CDC_TX4_TX_PATH_CTL		(0x0600)
#define CDC_TX4_TX_PATH_CFG0		(0x0604)
#define CDC_TX4_TX_PATH_CFG1		(0x0608)
#define CDC_TX4_TX_VOL_CTL		(0x060C)
#define CDC_TX4_TX_PATH_SEC0		(0x0610)
#define CDC_TX4_TX_PATH_SEC1		(0x0614)
#define CDC_TX4_TX_PATH_SEC2		(0x0618)
#define CDC_TX4_TX_PATH_SEC3		(0x061C)
#define CDC_TX4_TX_PATH_SEC4		(0x0620)
#define CDC_TX4_TX_PATH_SEC5		(0x0624)
#define CDC_TX4_TX_PATH_SEC6		(0x0628)
#define CDC_TX5_TX_PATH_CTL		(0x0680)
#define CDC_TX5_TX_PATH_CFG0		(0x0684)
#define CDC_TX5_TX_PATH_CFG1		(0x0688)
#define CDC_TX5_TX_VOL_CTL		(0x068C)
#define CDC_TX5_TX_PATH_SEC0		(0x0690)
#define CDC_TX5_TX_PATH_SEC1		(0x0694)
#define CDC_TX5_TX_PATH_SEC2		(0x0698)
#define CDC_TX5_TX_PATH_SEC3		(0x069C)
#define CDC_TX5_TX_PATH_SEC4		(0x06A0)
#define CDC_TX5_TX_PATH_SEC5		(0x06A4)
#define CDC_TX5_TX_PATH_SEC6		(0x06A8)
#define CDC_TX6_TX_PATH_CTL		(0x0700)
#define CDC_TX6_TX_PATH_CFG0		(0x0704)
#define CDC_TX6_TX_PATH_CFG1		(0x0708)
#define CDC_TX6_TX_VOL_CTL		(0x070C)
#define CDC_TX6_TX_PATH_SEC0		(0x0710)
#define CDC_TX6_TX_PATH_SEC1		(0x0714)
#define CDC_TX6_TX_PATH_SEC2		(0x0718)
#define CDC_TX6_TX_PATH_SEC3		(0x071C)
#define CDC_TX6_TX_PATH_SEC4		(0x0720)
#define CDC_TX6_TX_PATH_SEC5		(0x0724)
#define CDC_TX6_TX_PATH_SEC6		(0x0728)
#define CDC_TX7_TX_PATH_CTL		(0x0780)
#define CDC_TX7_TX_PATH_CFG0		(0x0784)
#define CDC_TX7_TX_PATH_CFG1		(0x0788)
#define CDC_TX7_TX_VOL_CTL		(0x078C)
#define CDC_TX7_TX_PATH_SEC0		(0x0790)
#define CDC_TX7_TX_PATH_SEC1		(0x0794)
#define CDC_TX7_TX_PATH_SEC2		(0x0798)
#define CDC_TX7_TX_PATH_SEC3		(0x079C)
#define CDC_TX7_TX_PATH_SEC4		(0x07A0)
#define CDC_TX7_TX_PATH_SEC5		(0x07A4)
#define CDC_TX7_TX_PATH_SEC6		(0x07A8)
#define TX_MAX_OFFSET			(0x07A8)

#define TX_MACRO_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define TX_MACRO_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S24_3LE)

#define  CF_MIN_3DB_4HZ			0x0
#define  CF_MIN_3DB_75HZ		0x1
#define  CF_MIN_3DB_150HZ		0x2
#define	TX_ADC_MAX	5
#define TX_ADC_TO_DMIC(n) ((n - TX_ADC_MAX)/2)
#define NUM_DECIMATORS 8
#define TX_NUM_CLKS_MAX	5
#define TX_MACRO_DMIC_UNMUTE_DELAY_MS	40
#define TX_MACRO_AMIC_UNMUTE_DELAY_MS	100
#define TX_MACRO_DMIC_HPF_DELAY_MS	300
#define TX_MACRO_AMIC_HPF_DELAY_MS	300
#define MCLK_FREQ		19200000

enum {
	TX_MACRO_AIF_INVALID = 0,
	TX_MACRO_AIF1_CAP,
	TX_MACRO_AIF2_CAP,
	TX_MACRO_AIF3_CAP,
	TX_MACRO_MAX_DAIS
};

enum {
	TX_MACRO_DEC0,
	TX_MACRO_DEC1,
	TX_MACRO_DEC2,
	TX_MACRO_DEC3,
	TX_MACRO_DEC4,
	TX_MACRO_DEC5,
	TX_MACRO_DEC6,
	TX_MACRO_DEC7,
	TX_MACRO_DEC_MAX,
};

enum {
	TX_MACRO_CLK_DIV_2,
	TX_MACRO_CLK_DIV_3,
	TX_MACRO_CLK_DIV_4,
	TX_MACRO_CLK_DIV_6,
	TX_MACRO_CLK_DIV_8,
	TX_MACRO_CLK_DIV_16,
};

enum {
	MSM_DMIC,
	SWR_MIC,
	ANC_FB_TUNE1
};

struct tx_mute_work {
	struct tx_macro *tx;
	u8 decimator;
	struct delayed_work dwork;
};

struct hpf_work {
	struct tx_macro *tx;
	u8 decimator;
	u8 hpf_cut_off_freq;
	struct delayed_work dwork;
};

struct tx_macro_data {
	unsigned int flags;
	unsigned int ver;
	const struct snd_soc_dapm_widget *extra_widgets;
	size_t extra_widgets_num;
	const struct snd_soc_dapm_route *extra_routes;
	size_t extra_routes_num;
};

struct tx_macro {
	struct device *dev;
	const struct tx_macro_data *data;
	struct snd_soc_component *component;
	struct hpf_work tx_hpf_work[NUM_DECIMATORS];
	struct tx_mute_work tx_mute_dwork[NUM_DECIMATORS];
	unsigned long active_ch_mask[TX_MACRO_MAX_DAIS];
	unsigned long active_ch_cnt[TX_MACRO_MAX_DAIS];
	int active_decimator[TX_MACRO_MAX_DAIS];
	struct regmap *regmap;
	struct clk *mclk;
	struct clk *npl;
	struct clk *macro;
	struct clk *dcodec;
	struct clk *fsgen;
	struct clk_hw hw;
	bool dec_active[NUM_DECIMATORS];
	int tx_mclk_users;
	bool bcs_enable;
	int dec_mode[NUM_DECIMATORS];
	struct lpass_macro *pds;
	bool bcs_clk_en;
};
#define to_tx_macro(_hw) container_of(_hw, struct tx_macro, hw)

static const DECLARE_TLV_DB_SCALE(digital_gain, -8400, 100, -8400);

static struct reg_default tx_defaults[] = {
	/* TX Macro */
	{ CDC_TX_CLK_RST_CTRL_MCLK_CONTROL, 0x00 },
	{ CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL, 0x00 },
	{ CDC_TX_CLK_RST_CTRL_SWR_CONTROL, 0x00},
	{ CDC_TX_TOP_CSR_TOP_CFG0, 0x00},
	{ CDC_TX_TOP_CSR_ANC_CFG, 0x00},
	{ CDC_TX_TOP_CSR_SWR_CTRL, 0x00},
	{ CDC_TX_TOP_CSR_FREQ_MCLK, 0x00},
	{ CDC_TX_TOP_CSR_DEBUG_BUS, 0x00},
	{ CDC_TX_TOP_CSR_DEBUG_EN, 0x00},
	{ CDC_TX_TOP_CSR_TX_I2S_CTL, 0x0C},
	{ CDC_TX_TOP_CSR_I2S_CLK, 0x00},
	{ CDC_TX_TOP_CSR_I2S_RESET, 0x00},
	{ CDC_TX_TOP_CSR_SWR_DMIC0_CTL, 0x00},
	{ CDC_TX_TOP_CSR_SWR_DMIC1_CTL, 0x00},
	{ CDC_TX_TOP_CSR_SWR_DMIC2_CTL, 0x00},
	{ CDC_TX_TOP_CSR_SWR_DMIC3_CTL, 0x00},
	{ CDC_TX_TOP_CSR_SWR_AMIC0_CTL, 0x00},
	{ CDC_TX_TOP_CSR_SWR_AMIC1_CTL, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX0_CFG0, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX0_CFG1, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX1_CFG0, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX1_CFG1, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX2_CFG0, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX2_CFG1, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX3_CFG0, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX3_CFG1, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX4_CFG0, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX4_CFG1, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX5_CFG0, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX5_CFG1, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX6_CFG0, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX6_CFG1, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX7_CFG0, 0x00},
	{ CDC_TX_INP_MUX_ADC_MUX7_CFG1, 0x00},
	{ CDC_TX_ANC0_CLK_RESET_CTL, 0x00},
	{ CDC_TX_ANC0_MODE_1_CTL, 0x00},
	{ CDC_TX_ANC0_MODE_2_CTL, 0x00},
	{ CDC_TX_ANC0_FF_SHIFT, 0x00},
	{ CDC_TX_ANC0_FB_SHIFT, 0x00},
	{ CDC_TX_ANC0_LPF_FF_A_CTL, 0x00},
	{ CDC_TX_ANC0_LPF_FF_B_CTL, 0x00},
	{ CDC_TX_ANC0_LPF_FB_CTL, 0x00},
	{ CDC_TX_ANC0_SMLPF_CTL, 0x00},
	{ CDC_TX_ANC0_DCFLT_SHIFT_CTL, 0x00},
	{ CDC_TX_ANC0_IIR_ADAPT_CTL, 0x00},
	{ CDC_TX_ANC0_IIR_COEFF_1_CTL, 0x00},
	{ CDC_TX_ANC0_IIR_COEFF_2_CTL, 0x00},
	{ CDC_TX_ANC0_FF_A_GAIN_CTL, 0x00},
	{ CDC_TX_ANC0_FF_B_GAIN_CTL, 0x00},
	{ CDC_TX_ANC0_FB_GAIN_CTL, 0x00},
	{ CDC_TX0_TX_PATH_CTL, 0x04},
	{ CDC_TX0_TX_PATH_CFG0, 0x10},
	{ CDC_TX0_TX_PATH_CFG1, 0x0B},
	{ CDC_TX0_TX_VOL_CTL, 0x00},
	{ CDC_TX0_TX_PATH_SEC0, 0x00},
	{ CDC_TX0_TX_PATH_SEC1, 0x00},
	{ CDC_TX0_TX_PATH_SEC2, 0x01},
	{ CDC_TX0_TX_PATH_SEC3, 0x3C},
	{ CDC_TX0_TX_PATH_SEC4, 0x20},
	{ CDC_TX0_TX_PATH_SEC5, 0x00},
	{ CDC_TX0_TX_PATH_SEC6, 0x00},
	{ CDC_TX0_TX_PATH_SEC7, 0x25},
	{ CDC_TX1_TX_PATH_CTL, 0x04},
	{ CDC_TX1_TX_PATH_CFG0, 0x10},
	{ CDC_TX1_TX_PATH_CFG1, 0x0B},
	{ CDC_TX1_TX_VOL_CTL, 0x00},
	{ CDC_TX1_TX_PATH_SEC0, 0x00},
	{ CDC_TX1_TX_PATH_SEC1, 0x00},
	{ CDC_TX1_TX_PATH_SEC2, 0x01},
	{ CDC_TX1_TX_PATH_SEC3, 0x3C},
	{ CDC_TX1_TX_PATH_SEC4, 0x20},
	{ CDC_TX1_TX_PATH_SEC5, 0x00},
	{ CDC_TX1_TX_PATH_SEC6, 0x00},
	{ CDC_TX2_TX_PATH_CTL, 0x04},
	{ CDC_TX2_TX_PATH_CFG0, 0x10},
	{ CDC_TX2_TX_PATH_CFG1, 0x0B},
	{ CDC_TX2_TX_VOL_CTL, 0x00},
	{ CDC_TX2_TX_PATH_SEC0, 0x00},
	{ CDC_TX2_TX_PATH_SEC1, 0x00},
	{ CDC_TX2_TX_PATH_SEC2, 0x01},
	{ CDC_TX2_TX_PATH_SEC3, 0x3C},
	{ CDC_TX2_TX_PATH_SEC4, 0x20},
	{ CDC_TX2_TX_PATH_SEC5, 0x00},
	{ CDC_TX2_TX_PATH_SEC6, 0x00},
	{ CDC_TX3_TX_PATH_CTL, 0x04},
	{ CDC_TX3_TX_PATH_CFG0, 0x10},
	{ CDC_TX3_TX_PATH_CFG1, 0x0B},
	{ CDC_TX3_TX_VOL_CTL, 0x00},
	{ CDC_TX3_TX_PATH_SEC0, 0x00},
	{ CDC_TX3_TX_PATH_SEC1, 0x00},
	{ CDC_TX3_TX_PATH_SEC2, 0x01},
	{ CDC_TX3_TX_PATH_SEC3, 0x3C},
	{ CDC_TX3_TX_PATH_SEC4, 0x20},
	{ CDC_TX3_TX_PATH_SEC5, 0x00},
	{ CDC_TX3_TX_PATH_SEC6, 0x00},
	{ CDC_TX4_TX_PATH_CTL, 0x04},
	{ CDC_TX4_TX_PATH_CFG0, 0x10},
	{ CDC_TX4_TX_PATH_CFG1, 0x0B},
	{ CDC_TX4_TX_VOL_CTL, 0x00},
	{ CDC_TX4_TX_PATH_SEC0, 0x00},
	{ CDC_TX4_TX_PATH_SEC1, 0x00},
	{ CDC_TX4_TX_PATH_SEC2, 0x01},
	{ CDC_TX4_TX_PATH_SEC3, 0x3C},
	{ CDC_TX4_TX_PATH_SEC4, 0x20},
	{ CDC_TX4_TX_PATH_SEC5, 0x00},
	{ CDC_TX4_TX_PATH_SEC6, 0x00},
	{ CDC_TX5_TX_PATH_CTL, 0x04},
	{ CDC_TX5_TX_PATH_CFG0, 0x10},
	{ CDC_TX5_TX_PATH_CFG1, 0x0B},
	{ CDC_TX5_TX_VOL_CTL, 0x00},
	{ CDC_TX5_TX_PATH_SEC0, 0x00},
	{ CDC_TX5_TX_PATH_SEC1, 0x00},
	{ CDC_TX5_TX_PATH_SEC2, 0x01},
	{ CDC_TX5_TX_PATH_SEC3, 0x3C},
	{ CDC_TX5_TX_PATH_SEC4, 0x20},
	{ CDC_TX5_TX_PATH_SEC5, 0x00},
	{ CDC_TX5_TX_PATH_SEC6, 0x00},
	{ CDC_TX6_TX_PATH_CTL, 0x04},
	{ CDC_TX6_TX_PATH_CFG0, 0x10},
	{ CDC_TX6_TX_PATH_CFG1, 0x0B},
	{ CDC_TX6_TX_VOL_CTL, 0x00},
	{ CDC_TX6_TX_PATH_SEC0, 0x00},
	{ CDC_TX6_TX_PATH_SEC1, 0x00},
	{ CDC_TX6_TX_PATH_SEC2, 0x01},
	{ CDC_TX6_TX_PATH_SEC3, 0x3C},
	{ CDC_TX6_TX_PATH_SEC4, 0x20},
	{ CDC_TX6_TX_PATH_SEC5, 0x00},
	{ CDC_TX6_TX_PATH_SEC6, 0x00},
	{ CDC_TX7_TX_PATH_CTL, 0x04},
	{ CDC_TX7_TX_PATH_CFG0, 0x10},
	{ CDC_TX7_TX_PATH_CFG1, 0x0B},
	{ CDC_TX7_TX_VOL_CTL, 0x00},
	{ CDC_TX7_TX_PATH_SEC0, 0x00},
	{ CDC_TX7_TX_PATH_SEC1, 0x00},
	{ CDC_TX7_TX_PATH_SEC2, 0x01},
	{ CDC_TX7_TX_PATH_SEC3, 0x3C},
	{ CDC_TX7_TX_PATH_SEC4, 0x20},
	{ CDC_TX7_TX_PATH_SEC5, 0x00},
	{ CDC_TX7_TX_PATH_SEC6, 0x00},
};

static bool tx_is_volatile_register(struct device *dev, unsigned int reg)
{
	/* Update volatile list for tx/tx macros */
	switch (reg) {
	case CDC_TX_TOP_CSR_SWR_DMIC0_CTL:
	case CDC_TX_TOP_CSR_SWR_DMIC1_CTL:
	case CDC_TX_TOP_CSR_SWR_DMIC2_CTL:
	case CDC_TX_TOP_CSR_SWR_DMIC3_CTL:
	case CDC_TX_TOP_CSR_SWR_AMIC0_CTL:
	case CDC_TX_TOP_CSR_SWR_AMIC1_CTL:
		return true;
	}
	return false;
}

static bool tx_is_rw_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CDC_TX_CLK_RST_CTRL_MCLK_CONTROL:
	case CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL:
	case CDC_TX_CLK_RST_CTRL_SWR_CONTROL:
	case CDC_TX_TOP_CSR_TOP_CFG0:
	case CDC_TX_TOP_CSR_ANC_CFG:
	case CDC_TX_TOP_CSR_SWR_CTRL:
	case CDC_TX_TOP_CSR_FREQ_MCLK:
	case CDC_TX_TOP_CSR_DEBUG_BUS:
	case CDC_TX_TOP_CSR_DEBUG_EN:
	case CDC_TX_TOP_CSR_TX_I2S_CTL:
	case CDC_TX_TOP_CSR_I2S_CLK:
	case CDC_TX_TOP_CSR_I2S_RESET:
	case CDC_TX_TOP_CSR_SWR_DMIC0_CTL:
	case CDC_TX_TOP_CSR_SWR_DMIC1_CTL:
	case CDC_TX_TOP_CSR_SWR_DMIC2_CTL:
	case CDC_TX_TOP_CSR_SWR_DMIC3_CTL:
	case CDC_TX_TOP_CSR_SWR_AMIC0_CTL:
	case CDC_TX_TOP_CSR_SWR_AMIC1_CTL:
	case CDC_TX_ANC0_CLK_RESET_CTL:
	case CDC_TX_ANC0_MODE_1_CTL:
	case CDC_TX_ANC0_MODE_2_CTL:
	case CDC_TX_ANC0_FF_SHIFT:
	case CDC_TX_ANC0_FB_SHIFT:
	case CDC_TX_ANC0_LPF_FF_A_CTL:
	case CDC_TX_ANC0_LPF_FF_B_CTL:
	case CDC_TX_ANC0_LPF_FB_CTL:
	case CDC_TX_ANC0_SMLPF_CTL:
	case CDC_TX_ANC0_DCFLT_SHIFT_CTL:
	case CDC_TX_ANC0_IIR_ADAPT_CTL:
	case CDC_TX_ANC0_IIR_COEFF_1_CTL:
	case CDC_TX_ANC0_IIR_COEFF_2_CTL:
	case CDC_TX_ANC0_FF_A_GAIN_CTL:
	case CDC_TX_ANC0_FF_B_GAIN_CTL:
	case CDC_TX_ANC0_FB_GAIN_CTL:
	case CDC_TX_INP_MUX_ADC_MUX0_CFG0:
	case CDC_TX_INP_MUX_ADC_MUX0_CFG1:
	case CDC_TX_INP_MUX_ADC_MUX1_CFG0:
	case CDC_TX_INP_MUX_ADC_MUX1_CFG1:
	case CDC_TX_INP_MUX_ADC_MUX2_CFG0:
	case CDC_TX_INP_MUX_ADC_MUX2_CFG1:
	case CDC_TX_INP_MUX_ADC_MUX3_CFG0:
	case CDC_TX_INP_MUX_ADC_MUX3_CFG1:
	case CDC_TX_INP_MUX_ADC_MUX4_CFG0:
	case CDC_TX_INP_MUX_ADC_MUX4_CFG1:
	case CDC_TX_INP_MUX_ADC_MUX5_CFG0:
	case CDC_TX_INP_MUX_ADC_MUX5_CFG1:
	case CDC_TX_INP_MUX_ADC_MUX6_CFG0:
	case CDC_TX_INP_MUX_ADC_MUX6_CFG1:
	case CDC_TX_INP_MUX_ADC_MUX7_CFG0:
	case CDC_TX_INP_MUX_ADC_MUX7_CFG1:
	case CDC_TX0_TX_PATH_CTL:
	case CDC_TX0_TX_PATH_CFG0:
	case CDC_TX0_TX_PATH_CFG1:
	case CDC_TX0_TX_VOL_CTL:
	case CDC_TX0_TX_PATH_SEC0:
	case CDC_TX0_TX_PATH_SEC1:
	case CDC_TX0_TX_PATH_SEC2:
	case CDC_TX0_TX_PATH_SEC3:
	case CDC_TX0_TX_PATH_SEC4:
	case CDC_TX0_TX_PATH_SEC5:
	case CDC_TX0_TX_PATH_SEC6:
	case CDC_TX0_TX_PATH_SEC7:
	case CDC_TX1_TX_PATH_CTL:
	case CDC_TX1_TX_PATH_CFG0:
	case CDC_TX1_TX_PATH_CFG1:
	case CDC_TX1_TX_VOL_CTL:
	case CDC_TX1_TX_PATH_SEC0:
	case CDC_TX1_TX_PATH_SEC1:
	case CDC_TX1_TX_PATH_SEC2:
	case CDC_TX1_TX_PATH_SEC3:
	case CDC_TX1_TX_PATH_SEC4:
	case CDC_TX1_TX_PATH_SEC5:
	case CDC_TX1_TX_PATH_SEC6:
	case CDC_TX2_TX_PATH_CTL:
	case CDC_TX2_TX_PATH_CFG0:
	case CDC_TX2_TX_PATH_CFG1:
	case CDC_TX2_TX_VOL_CTL:
	case CDC_TX2_TX_PATH_SEC0:
	case CDC_TX2_TX_PATH_SEC1:
	case CDC_TX2_TX_PATH_SEC2:
	case CDC_TX2_TX_PATH_SEC3:
	case CDC_TX2_TX_PATH_SEC4:
	case CDC_TX2_TX_PATH_SEC5:
	case CDC_TX2_TX_PATH_SEC6:
	case CDC_TX3_TX_PATH_CTL:
	case CDC_TX3_TX_PATH_CFG0:
	case CDC_TX3_TX_PATH_CFG1:
	case CDC_TX3_TX_VOL_CTL:
	case CDC_TX3_TX_PATH_SEC0:
	case CDC_TX3_TX_PATH_SEC1:
	case CDC_TX3_TX_PATH_SEC2:
	case CDC_TX3_TX_PATH_SEC3:
	case CDC_TX3_TX_PATH_SEC4:
	case CDC_TX3_TX_PATH_SEC5:
	case CDC_TX3_TX_PATH_SEC6:
	case CDC_TX4_TX_PATH_CTL:
	case CDC_TX4_TX_PATH_CFG0:
	case CDC_TX4_TX_PATH_CFG1:
	case CDC_TX4_TX_VOL_CTL:
	case CDC_TX4_TX_PATH_SEC0:
	case CDC_TX4_TX_PATH_SEC1:
	case CDC_TX4_TX_PATH_SEC2:
	case CDC_TX4_TX_PATH_SEC3:
	case CDC_TX4_TX_PATH_SEC4:
	case CDC_TX4_TX_PATH_SEC5:
	case CDC_TX4_TX_PATH_SEC6:
	case CDC_TX5_TX_PATH_CTL:
	case CDC_TX5_TX_PATH_CFG0:
	case CDC_TX5_TX_PATH_CFG1:
	case CDC_TX5_TX_VOL_CTL:
	case CDC_TX5_TX_PATH_SEC0:
	case CDC_TX5_TX_PATH_SEC1:
	case CDC_TX5_TX_PATH_SEC2:
	case CDC_TX5_TX_PATH_SEC3:
	case CDC_TX5_TX_PATH_SEC4:
	case CDC_TX5_TX_PATH_SEC5:
	case CDC_TX5_TX_PATH_SEC6:
	case CDC_TX6_TX_PATH_CTL:
	case CDC_TX6_TX_PATH_CFG0:
	case CDC_TX6_TX_PATH_CFG1:
	case CDC_TX6_TX_VOL_CTL:
	case CDC_TX6_TX_PATH_SEC0:
	case CDC_TX6_TX_PATH_SEC1:
	case CDC_TX6_TX_PATH_SEC2:
	case CDC_TX6_TX_PATH_SEC3:
	case CDC_TX6_TX_PATH_SEC4:
	case CDC_TX6_TX_PATH_SEC5:
	case CDC_TX6_TX_PATH_SEC6:
	case CDC_TX7_TX_PATH_CTL:
	case CDC_TX7_TX_PATH_CFG0:
	case CDC_TX7_TX_PATH_CFG1:
	case CDC_TX7_TX_VOL_CTL:
	case CDC_TX7_TX_PATH_SEC0:
	case CDC_TX7_TX_PATH_SEC1:
	case CDC_TX7_TX_PATH_SEC2:
	case CDC_TX7_TX_PATH_SEC3:
	case CDC_TX7_TX_PATH_SEC4:
	case CDC_TX7_TX_PATH_SEC5:
	case CDC_TX7_TX_PATH_SEC6:
		return true;
	}

	return false;
}

static const struct regmap_config tx_regmap_config = {
	.name = "tx_macro",
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.cache_type = REGCACHE_FLAT,
	.max_register = TX_MAX_OFFSET,
	.reg_defaults = tx_defaults,
	.num_reg_defaults = ARRAY_SIZE(tx_defaults),
	.writeable_reg = tx_is_rw_register,
	.volatile_reg = tx_is_volatile_register,
	.readable_reg = tx_is_rw_register,
};

static int tx_macro_mclk_enable(struct tx_macro *tx,
				bool mclk_enable)
{
	struct regmap *regmap = tx->regmap;

	if (mclk_enable) {
		if (tx->tx_mclk_users == 0) {
			/* 9.6MHz MCLK, set value 0x00 if other frequency */
			regmap_update_bits(regmap, CDC_TX_TOP_CSR_FREQ_MCLK, 0x01, 0x01);
			regmap_update_bits(regmap, CDC_TX_CLK_RST_CTRL_MCLK_CONTROL,
					   CDC_TX_MCLK_EN_MASK,
					   CDC_TX_MCLK_ENABLE);
			regmap_update_bits(regmap, CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL,
					   CDC_TX_FS_CNT_EN_MASK,
					   CDC_TX_FS_CNT_ENABLE);
			regcache_mark_dirty(regmap);
			regcache_sync(regmap);
		}
		tx->tx_mclk_users++;
	} else {
		if (tx->tx_mclk_users <= 0) {
			dev_err(tx->dev, "clock already disabled\n");
			tx->tx_mclk_users = 0;
			goto exit;
		}
		tx->tx_mclk_users--;
		if (tx->tx_mclk_users == 0) {
			regmap_update_bits(regmap, CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL,
					   CDC_TX_FS_CNT_EN_MASK, 0x0);
			regmap_update_bits(regmap, CDC_TX_CLK_RST_CTRL_MCLK_CONTROL,
					   CDC_TX_MCLK_EN_MASK, 0x0);
		}
	}
exit:
	return 0;
}

static bool is_amic_enabled(struct snd_soc_component *component,
			    struct tx_macro *tx, u8 decimator)
{
	u16 adc_mux_reg, adc_reg, adc_n;

	adc_mux_reg = CDC_TX_INP_MUX_ADC_MUXn_CFG1(decimator);

	if (snd_soc_component_read(component, adc_mux_reg) & SWR_MIC) {
		if (tx->data->ver > LPASS_VER_9_0_0)
			return true;

		/* else: LPASS <= v9.0.0 */
		adc_reg = CDC_TX_INP_MUX_ADC_MUXn_CFG0(decimator);
		adc_n = snd_soc_component_read_field(component, adc_reg,
					     CDC_TX_MACRO_SWR_MIC_MUX_SEL_MASK);
		if (adc_n < TX_ADC_MAX)
			return true;
	}

	return false;
}

static void tx_macro_tx_hpf_corner_freq_callback(struct work_struct *work)
{
	struct delayed_work *hpf_delayed_work;
	struct hpf_work *hpf_work;
	struct tx_macro *tx;
	struct snd_soc_component *component;
	u16 dec_cfg_reg, hpf_gate_reg;
	u8 hpf_cut_off_freq;

	hpf_delayed_work = to_delayed_work(work);
	hpf_work = container_of(hpf_delayed_work, struct hpf_work, dwork);
	tx = hpf_work->tx;
	component = tx->component;
	hpf_cut_off_freq = hpf_work->hpf_cut_off_freq;

	dec_cfg_reg = CDC_TXn_TX_PATH_CFG0(hpf_work->decimator);
	hpf_gate_reg = CDC_TXn_TX_PATH_SEC2(hpf_work->decimator);

	if (is_amic_enabled(component, tx, hpf_work->decimator)) {
		snd_soc_component_write_field(component,
				dec_cfg_reg,
				CDC_TXn_HPF_CUT_FREQ_MASK,
				hpf_cut_off_freq);
		snd_soc_component_update_bits(component, hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK |
					      CDC_TXn_HPF_ZERO_GATE_MASK,
					      0x02);
		snd_soc_component_update_bits(component, hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK |
					      CDC_TXn_HPF_ZERO_GATE_MASK,
					      0x01);
	} else {
		snd_soc_component_write_field(component, dec_cfg_reg,
					      CDC_TXn_HPF_CUT_FREQ_MASK,
					      hpf_cut_off_freq);
		snd_soc_component_write_field(component, hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK, 0x1);
		/* Minimum 1 clk cycle delay is required as per HW spec */
		usleep_range(1000, 1010);
		snd_soc_component_write_field(component, hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK, 0x0);
	}
}

static void tx_macro_mute_update_callback(struct work_struct *work)
{
	struct tx_mute_work *tx_mute_dwork;
	struct snd_soc_component *component;
	struct tx_macro *tx;
	struct delayed_work *delayed_work;
	u8 decimator;

	delayed_work = to_delayed_work(work);
	tx_mute_dwork = container_of(delayed_work, struct tx_mute_work, dwork);
	tx = tx_mute_dwork->tx;
	component = tx->component;
	decimator = tx_mute_dwork->decimator;

	snd_soc_component_write_field(component, CDC_TXn_TX_PATH_CTL(decimator),
				      CDC_TXn_PGA_MUTE_MASK, 0x0);
}

static int tx_macro_mclk_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		tx_macro_mclk_enable(tx, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		tx_macro_mclk_enable(tx, false);
		break;
	default:
		break;
	}

	return 0;
}

static void tx_macro_update_smic_sel_v9(struct snd_soc_component *component,
					struct snd_soc_dapm_widget *widget,
					struct tx_macro *tx, u16 mic_sel_reg,
					unsigned int val)
{
	unsigned int dmic;
	u16 dmic_clk_reg;

	if (val < 5) {
		snd_soc_component_write_field(component, mic_sel_reg,
					      CDC_TXn_ADC_DMIC_SEL_MASK, 0);
	} else {
		snd_soc_component_write_field(component, mic_sel_reg,
					      CDC_TXn_ADC_DMIC_SEL_MASK, 1);
		dmic = TX_ADC_TO_DMIC(val);
		dmic_clk_reg = CDC_TX_TOP_CSR_SWR_DMICn_CTL(dmic);
		snd_soc_component_write_field(component, dmic_clk_reg,
					      CDC_TX_SWR_DMIC_CLK_SEL_MASK,
					      CDC_TX_SWR_MIC_CLK_DEFAULT);
	}
}

static void tx_macro_update_smic_sel_v9_2(struct snd_soc_component *component,
					  struct snd_soc_dapm_widget *widget,
					  struct tx_macro *tx, u16 mic_sel_reg,
					  unsigned int val)
{
	unsigned int dmic;
	u16 dmic_clk_reg;

	if (widget->shift) {
		/* MSM DMIC */
		snd_soc_component_write_field(component, mic_sel_reg,
					      CDC_TXn_ADC_DMIC_SEL_MASK, 1);

		dmic = TX_ADC_TO_DMIC(val);
		dmic_clk_reg = CDC_TX_TOP_CSR_SWR_DMICn_CTL(dmic);
		snd_soc_component_write_field(component, dmic_clk_reg,
					      CDC_TX_SWR_DMIC_CLK_SEL_MASK,
					      CDC_TX_SWR_MIC_CLK_DEFAULT);
	} else {
		snd_soc_component_write_field(component, mic_sel_reg,
					      CDC_TXn_ADC_DMIC_SEL_MASK, 0);
	}
}

static int tx_macro_put_dec_enum(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);
	unsigned int val;
	u16 mic_sel_reg;

	val = ucontrol->value.enumerated.item[0];
	if (val >= e->items)
		return -EINVAL;

	switch (e->reg) {
	case CDC_TX_INP_MUX_ADC_MUX0_CFG0:
		mic_sel_reg = CDC_TX0_TX_PATH_CFG0;
		break;
	case CDC_TX_INP_MUX_ADC_MUX1_CFG0:
		mic_sel_reg = CDC_TX1_TX_PATH_CFG0;
		break;
	case CDC_TX_INP_MUX_ADC_MUX2_CFG0:
		mic_sel_reg = CDC_TX2_TX_PATH_CFG0;
		break;
	case CDC_TX_INP_MUX_ADC_MUX3_CFG0:
		mic_sel_reg = CDC_TX3_TX_PATH_CFG0;
		break;
	case CDC_TX_INP_MUX_ADC_MUX4_CFG0:
		mic_sel_reg = CDC_TX4_TX_PATH_CFG0;
		break;
	case CDC_TX_INP_MUX_ADC_MUX5_CFG0:
		mic_sel_reg = CDC_TX5_TX_PATH_CFG0;
		break;
	case CDC_TX_INP_MUX_ADC_MUX6_CFG0:
		mic_sel_reg = CDC_TX6_TX_PATH_CFG0;
		break;
	case CDC_TX_INP_MUX_ADC_MUX7_CFG0:
		mic_sel_reg = CDC_TX7_TX_PATH_CFG0;
		break;
	default:
		dev_err(component->dev, "Error in configuration!!\n");
		return -EINVAL;
	}

	if (val != 0) {
		if (widget->shift) /* MSM DMIC */
			snd_soc_component_write_field(component, mic_sel_reg,
						      CDC_TXn_ADC_DMIC_SEL_MASK, 1);
		else if (tx->data->ver <= LPASS_VER_9_0_0)
			tx_macro_update_smic_sel_v9(component, widget, tx,
						    mic_sel_reg, val);
		else
			tx_macro_update_smic_sel_v9_2(component, widget, tx,
						      mic_sel_reg, val);
	}

	return snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
}

static int tx_macro_tx_mixer_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	u32 dai_id = widget->shift;
	u32 dec_id = mc->shift;
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	if (test_bit(dec_id, &tx->active_ch_mask[dai_id]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int tx_macro_tx_mixer_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct snd_soc_dapm_update *update = NULL;
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	u32 dai_id = widget->shift;
	u32 dec_id = mc->shift;
	u32 enable = ucontrol->value.integer.value[0];
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	if (enable) {
		if (tx->active_decimator[dai_id] == dec_id)
			return 0;

		set_bit(dec_id, &tx->active_ch_mask[dai_id]);
		tx->active_ch_cnt[dai_id]++;
		tx->active_decimator[dai_id] = dec_id;
	} else {
		if (tx->active_decimator[dai_id] == -1)
			return 0;

		tx->active_ch_cnt[dai_id]--;
		clear_bit(dec_id, &tx->active_ch_mask[dai_id]);
		tx->active_decimator[dai_id] = -1;
	}
	snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, enable, update);

	return 1;
}

static int tx_macro_enable_dec(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u8 decimator;
	u16 tx_vol_ctl_reg, dec_cfg_reg, hpf_gate_reg, tx_gain_ctl_reg;
	u8 hpf_cut_off_freq;
	int hpf_delay = TX_MACRO_DMIC_HPF_DELAY_MS;
	int unmute_delay = TX_MACRO_DMIC_UNMUTE_DELAY_MS;
	u16 adc_mux_reg, adc_reg, adc_n, dmic;
	u16 dmic_clk_reg;
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	decimator = w->shift;
	tx_vol_ctl_reg = CDC_TXn_TX_PATH_CTL(decimator);
	hpf_gate_reg = CDC_TXn_TX_PATH_SEC2(decimator);
	dec_cfg_reg = CDC_TXn_TX_PATH_CFG0(decimator);
	tx_gain_ctl_reg = CDC_TXn_TX_VOL_CTL(decimator);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		adc_mux_reg = CDC_TX_INP_MUX_ADC_MUXn_CFG1(decimator);
		if (snd_soc_component_read(component, adc_mux_reg) & SWR_MIC) {
			adc_reg = CDC_TX_INP_MUX_ADC_MUXn_CFG0(decimator);
			adc_n = snd_soc_component_read(component, adc_reg) &
				CDC_TX_MACRO_SWR_MIC_MUX_SEL_MASK;
			if (adc_n >= TX_ADC_MAX) {
				dmic = TX_ADC_TO_DMIC(adc_n);
				dmic_clk_reg = CDC_TX_TOP_CSR_SWR_DMICn_CTL(dmic);

				snd_soc_component_write_field(component, dmic_clk_reg,
							CDC_TX_SWR_DMIC_CLK_SEL_MASK,
							CDC_TX_SWR_MIC_CLK_DEFAULT);
			}
		}
		snd_soc_component_write_field(component, dec_cfg_reg,
					      CDC_TXn_ADC_MODE_MASK,
					      tx->dec_mode[decimator]);
		/* Enable TX PGA Mute */
		snd_soc_component_write_field(component, tx_vol_ctl_reg,
					      CDC_TXn_PGA_MUTE_MASK, 0x1);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write_field(component, tx_vol_ctl_reg,
					     CDC_TXn_CLK_EN_MASK, 0x1);
		if (!is_amic_enabled(component, tx, decimator)) {
			snd_soc_component_update_bits(component, hpf_gate_reg, 0x01, 0x00);
			/* Minimum 1 clk cycle delay is required as per HW spec */
			usleep_range(1000, 1010);
		}
		hpf_cut_off_freq = snd_soc_component_read_field(component, dec_cfg_reg,
								CDC_TXn_HPF_CUT_FREQ_MASK);

		tx->tx_hpf_work[decimator].hpf_cut_off_freq =
						hpf_cut_off_freq;

		if (hpf_cut_off_freq != CF_MIN_3DB_150HZ)
			snd_soc_component_write_field(component, dec_cfg_reg,
						      CDC_TXn_HPF_CUT_FREQ_MASK,
						      CF_MIN_3DB_150HZ);

		if (is_amic_enabled(component, tx, decimator)) {
			hpf_delay = TX_MACRO_AMIC_HPF_DELAY_MS;
			unmute_delay = TX_MACRO_AMIC_UNMUTE_DELAY_MS;
		}
		/* schedule work queue to Remove Mute */
		queue_delayed_work(system_freezable_wq,
				   &tx->tx_mute_dwork[decimator].dwork,
				   msecs_to_jiffies(unmute_delay));
		if (tx->tx_hpf_work[decimator].hpf_cut_off_freq != CF_MIN_3DB_150HZ) {
			queue_delayed_work(system_freezable_wq,
				&tx->tx_hpf_work[decimator].dwork,
				msecs_to_jiffies(hpf_delay));
			snd_soc_component_update_bits(component, hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK |
					      CDC_TXn_HPF_ZERO_GATE_MASK,
					      0x02);
			if (!is_amic_enabled(component, tx, decimator))
				snd_soc_component_update_bits(component, hpf_gate_reg,
						      CDC_TXn_HPF_F_CHANGE_MASK |
						      CDC_TXn_HPF_ZERO_GATE_MASK,
						      0x00);
			snd_soc_component_update_bits(component, hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK |
					      CDC_TXn_HPF_ZERO_GATE_MASK,
					      0x01);

			/*
			 * 6ms delay is required as per HW spec
			 */
			usleep_range(6000, 6010);
		}
		/* apply gain after decimator is enabled */
		snd_soc_component_write(component, tx_gain_ctl_reg,
			      snd_soc_component_read(component,
					tx_gain_ctl_reg));
		if (tx->bcs_enable) {
			snd_soc_component_update_bits(component, dec_cfg_reg,
					0x01, 0x01);
			tx->bcs_clk_en = true;
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		hpf_cut_off_freq =
			tx->tx_hpf_work[decimator].hpf_cut_off_freq;
		snd_soc_component_write_field(component, tx_vol_ctl_reg,
					      CDC_TXn_PGA_MUTE_MASK, 0x1);
		if (cancel_delayed_work_sync(
		    &tx->tx_hpf_work[decimator].dwork)) {
			if (hpf_cut_off_freq != CF_MIN_3DB_150HZ) {
				snd_soc_component_write_field(
						component, dec_cfg_reg,
						CDC_TXn_HPF_CUT_FREQ_MASK,
						hpf_cut_off_freq);
				if (is_amic_enabled(component, tx, decimator))
					snd_soc_component_update_bits(component,
					      hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK |
					      CDC_TXn_HPF_ZERO_GATE_MASK,
					      0x02);
				else
					snd_soc_component_update_bits(component,
					      hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK |
					      CDC_TXn_HPF_ZERO_GATE_MASK,
					      0x03);

				/*
				 * Minimum 1 clk cycle delay is required
				 * as per HW spec
				 */
				usleep_range(1000, 1010);
				snd_soc_component_update_bits(component, hpf_gate_reg,
					      CDC_TXn_HPF_F_CHANGE_MASK |
					      CDC_TXn_HPF_ZERO_GATE_MASK,
					      0x1);
			}
		}
		cancel_delayed_work_sync(&tx->tx_mute_dwork[decimator].dwork);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, tx_vol_ctl_reg,
					      CDC_TXn_CLK_EN_MASK, 0x0);
		snd_soc_component_write_field(component, dec_cfg_reg,
					      CDC_TXn_ADC_MODE_MASK, 0x0);
		snd_soc_component_write_field(component, tx_vol_ctl_reg,
					      CDC_TXn_PGA_MUTE_MASK, 0x0);
		if (tx->bcs_enable) {
			snd_soc_component_write_field(component, dec_cfg_reg,
						      CDC_TXn_PH_EN_MASK, 0x0);
			snd_soc_component_write_field(component,
						      CDC_TX0_TX_PATH_SEC7,
						      CDC_TX0_MBHC_CTL_EN_MASK,
						      0x0);
			tx->bcs_clk_en = false;
		}
		break;
	}
	return 0;
}

static int tx_macro_dec_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int path = e->shift_l;

	ucontrol->value.integer.value[0] = tx->dec_mode[path];

	return 0;
}

static int tx_macro_dec_mode_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int value = ucontrol->value.integer.value[0];
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int path = e->shift_l;
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	if (tx->dec_mode[path] == value)
		return 0;

	tx->dec_mode[path] = value;

	return 1;
}

static int tx_macro_get_bcs(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = tx->bcs_enable;

	return 0;
}

static int tx_macro_set_bcs(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int value = ucontrol->value.integer.value[0];
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	tx->bcs_enable = value;

	return 0;
}

static int tx_macro_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	u32 sample_rate;
	u8 decimator;
	int tx_fs_rate;
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	sample_rate = params_rate(params);
	switch (sample_rate) {
	case 8000:
		tx_fs_rate = 0;
		break;
	case 16000:
		tx_fs_rate = 1;
		break;
	case 32000:
		tx_fs_rate = 3;
		break;
	case 48000:
		tx_fs_rate = 4;
		break;
	case 96000:
		tx_fs_rate = 5;
		break;
	case 192000:
		tx_fs_rate = 6;
		break;
	case 384000:
		tx_fs_rate = 7;
		break;
	default:
		dev_err(component->dev, "%s: Invalid TX sample rate: %d\n",
			__func__, params_rate(params));
		return -EINVAL;
	}

	for_each_set_bit(decimator, &tx->active_ch_mask[dai->id], TX_MACRO_DEC_MAX)
		snd_soc_component_update_bits(component, CDC_TXn_TX_PATH_CTL(decimator),
					      CDC_TXn_PCM_RATE_MASK,
					      tx_fs_rate);
	return 0;
}

static int tx_macro_get_channel_map(const struct snd_soc_dai *dai,
				    unsigned int *tx_num, unsigned int *tx_slot,
				    unsigned int *rx_num, unsigned int *rx_slot)
{
	struct snd_soc_component *component = dai->component;
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);

	switch (dai->id) {
	case TX_MACRO_AIF1_CAP:
	case TX_MACRO_AIF2_CAP:
	case TX_MACRO_AIF3_CAP:
		*tx_slot = tx->active_ch_mask[dai->id];
		*tx_num = tx->active_ch_cnt[dai->id];
		break;
	default:
		break;
	}
	return 0;
}

static int tx_macro_digital_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct tx_macro *tx = snd_soc_component_get_drvdata(component);
	u8 decimator;

	/* active decimator not set yet */
	if (tx->active_decimator[dai->id] == -1)
		return 0;

	decimator = tx->active_decimator[dai->id];

	if (mute)
		snd_soc_component_write_field(component,
					      CDC_TXn_TX_PATH_CTL(decimator),
					      CDC_TXn_PGA_MUTE_MASK, 0x1);
	else
		snd_soc_component_update_bits(component,
					      CDC_TXn_TX_PATH_CTL(decimator),
					      CDC_TXn_PGA_MUTE_MASK, 0x0);

	return 0;
}

static const struct snd_soc_dai_ops tx_macro_dai_ops = {
	.hw_params = tx_macro_hw_params,
	.get_channel_map = tx_macro_get_channel_map,
	.mute_stream = tx_macro_digital_mute,
};

static struct snd_soc_dai_driver tx_macro_dai[] = {
	{
		.name = "tx_macro_tx1",
		.id = TX_MACRO_AIF1_CAP,
		.capture = {
			.stream_name = "TX_AIF1 Capture",
			.rates = TX_MACRO_RATES,
			.formats = TX_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &tx_macro_dai_ops,
	},
	{
		.name = "tx_macro_tx2",
		.id = TX_MACRO_AIF2_CAP,
		.capture = {
			.stream_name = "TX_AIF2 Capture",
			.rates = TX_MACRO_RATES,
			.formats = TX_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &tx_macro_dai_ops,
	},
	{
		.name = "tx_macro_tx3",
		.id = TX_MACRO_AIF3_CAP,
		.capture = {
			.stream_name = "TX_AIF3 Capture",
			.rates = TX_MACRO_RATES,
			.formats = TX_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &tx_macro_dai_ops,
	},
};

static const char * const adc_mux_text[] = {
	"MSM_DMIC", "SWR_MIC", "ANC_FB_TUNE1"
};

static SOC_ENUM_SINGLE_DECL(tx_dec0_enum, CDC_TX_INP_MUX_ADC_MUX0_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(tx_dec1_enum, CDC_TX_INP_MUX_ADC_MUX1_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(tx_dec2_enum, CDC_TX_INP_MUX_ADC_MUX2_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(tx_dec3_enum, CDC_TX_INP_MUX_ADC_MUX3_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(tx_dec4_enum, CDC_TX_INP_MUX_ADC_MUX4_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(tx_dec5_enum, CDC_TX_INP_MUX_ADC_MUX5_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(tx_dec6_enum, CDC_TX_INP_MUX_ADC_MUX6_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(tx_dec7_enum, CDC_TX_INP_MUX_ADC_MUX7_CFG1,
		   0, adc_mux_text);

static const struct snd_kcontrol_new tx_dec0_mux = SOC_DAPM_ENUM("tx_dec0", tx_dec0_enum);
static const struct snd_kcontrol_new tx_dec1_mux = SOC_DAPM_ENUM("tx_dec1", tx_dec1_enum);
static const struct snd_kcontrol_new tx_dec2_mux = SOC_DAPM_ENUM("tx_dec2", tx_dec2_enum);
static const struct snd_kcontrol_new tx_dec3_mux = SOC_DAPM_ENUM("tx_dec3", tx_dec3_enum);
static const struct snd_kcontrol_new tx_dec4_mux = SOC_DAPM_ENUM("tx_dec4", tx_dec4_enum);
static const struct snd_kcontrol_new tx_dec5_mux = SOC_DAPM_ENUM("tx_dec5", tx_dec5_enum);
static const struct snd_kcontrol_new tx_dec6_mux = SOC_DAPM_ENUM("tx_dec6", tx_dec6_enum);
static const struct snd_kcontrol_new tx_dec7_mux = SOC_DAPM_ENUM("tx_dec7", tx_dec7_enum);

static const char * const dmic_mux_text[] = {
	"ZERO", "DMIC0", "DMIC1", "DMIC2", "DMIC3",
	"DMIC4", "DMIC5", "DMIC6", "DMIC7"
};

static SOC_ENUM_SINGLE_DECL(tx_dmic0_enum, CDC_TX_INP_MUX_ADC_MUX0_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(tx_dmic1_enum, CDC_TX_INP_MUX_ADC_MUX1_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(tx_dmic2_enum, CDC_TX_INP_MUX_ADC_MUX2_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(tx_dmic3_enum, CDC_TX_INP_MUX_ADC_MUX3_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(tx_dmic4_enum, CDC_TX_INP_MUX_ADC_MUX4_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(tx_dmic5_enum, CDC_TX_INP_MUX_ADC_MUX5_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(tx_dmic6_enum, CDC_TX_INP_MUX_ADC_MUX6_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(tx_dmic7_enum, CDC_TX_INP_MUX_ADC_MUX7_CFG0,
			4, dmic_mux_text);

static const struct snd_kcontrol_new tx_dmic0_mux = SOC_DAPM_ENUM_EXT("tx_dmic0", tx_dmic0_enum,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_dmic1_mux = SOC_DAPM_ENUM_EXT("tx_dmic1", tx_dmic1_enum,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_dmic2_mux = SOC_DAPM_ENUM_EXT("tx_dmic2", tx_dmic2_enum,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_dmic3_mux = SOC_DAPM_ENUM_EXT("tx_dmic3", tx_dmic3_enum,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_dmic4_mux = SOC_DAPM_ENUM_EXT("tx_dmic4", tx_dmic4_enum,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_dmic5_mux = SOC_DAPM_ENUM_EXT("tx_dmic5", tx_dmic5_enum,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_dmic6_mux = SOC_DAPM_ENUM_EXT("tx_dmic6", tx_dmic6_enum,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_dmic7_mux = SOC_DAPM_ENUM_EXT("tx_dmic7", tx_dmic7_enum,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);

static const char * const dec_mode_mux_text[] = {
	"ADC_DEFAULT", "ADC_LOW_PWR", "ADC_HIGH_PERF",
};

static const struct soc_enum dec_mode_mux_enum[] = {
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 1, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 2,  ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 3, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 4, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 5, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 6, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 7, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
};

static const struct snd_kcontrol_new tx_aif1_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, TX_MACRO_DEC0, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, TX_MACRO_DEC1, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, TX_MACRO_DEC2, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, TX_MACRO_DEC3, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, TX_MACRO_DEC4, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, TX_MACRO_DEC5, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, TX_MACRO_DEC6, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, TX_MACRO_DEC7, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
};

static const struct snd_kcontrol_new tx_aif2_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, TX_MACRO_DEC0, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, TX_MACRO_DEC1, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, TX_MACRO_DEC2, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, TX_MACRO_DEC3, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, TX_MACRO_DEC4, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, TX_MACRO_DEC5, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, TX_MACRO_DEC6, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, TX_MACRO_DEC7, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
};

static const struct snd_kcontrol_new tx_aif3_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, TX_MACRO_DEC0, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, TX_MACRO_DEC1, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, TX_MACRO_DEC2, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, TX_MACRO_DEC3, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, TX_MACRO_DEC4, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, TX_MACRO_DEC5, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, TX_MACRO_DEC6, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, TX_MACRO_DEC7, 1, 0,
			tx_macro_tx_mixer_get, tx_macro_tx_mixer_put),
};

static const struct snd_soc_dapm_widget tx_macro_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("TX_AIF1 CAP", "TX_AIF1 Capture", 0,
		SND_SOC_NOPM, TX_MACRO_AIF1_CAP, 0),

	SND_SOC_DAPM_AIF_OUT("TX_AIF2 CAP", "TX_AIF2 Capture", 0,
		SND_SOC_NOPM, TX_MACRO_AIF2_CAP, 0),

	SND_SOC_DAPM_AIF_OUT("TX_AIF3 CAP", "TX_AIF3 Capture", 0,
		SND_SOC_NOPM, TX_MACRO_AIF3_CAP, 0),

	SND_SOC_DAPM_MIXER("TX_AIF1_CAP Mixer", SND_SOC_NOPM, TX_MACRO_AIF1_CAP, 0,
		tx_aif1_cap_mixer, ARRAY_SIZE(tx_aif1_cap_mixer)),

	SND_SOC_DAPM_MIXER("TX_AIF2_CAP Mixer", SND_SOC_NOPM, TX_MACRO_AIF2_CAP, 0,
		tx_aif2_cap_mixer, ARRAY_SIZE(tx_aif2_cap_mixer)),

	SND_SOC_DAPM_MIXER("TX_AIF3_CAP Mixer", SND_SOC_NOPM, TX_MACRO_AIF3_CAP, 0,
		tx_aif3_cap_mixer, ARRAY_SIZE(tx_aif3_cap_mixer)),

	SND_SOC_DAPM_MUX("TX DMIC MUX0", SND_SOC_NOPM, 4, 0, &tx_dmic0_mux),
	SND_SOC_DAPM_MUX("TX DMIC MUX1", SND_SOC_NOPM, 4, 0, &tx_dmic1_mux),
	SND_SOC_DAPM_MUX("TX DMIC MUX2", SND_SOC_NOPM, 4, 0, &tx_dmic2_mux),
	SND_SOC_DAPM_MUX("TX DMIC MUX3", SND_SOC_NOPM, 4, 0, &tx_dmic3_mux),
	SND_SOC_DAPM_MUX("TX DMIC MUX4", SND_SOC_NOPM, 4, 0, &tx_dmic4_mux),
	SND_SOC_DAPM_MUX("TX DMIC MUX5", SND_SOC_NOPM, 4, 0, &tx_dmic5_mux),
	SND_SOC_DAPM_MUX("TX DMIC MUX6", SND_SOC_NOPM, 4, 0, &tx_dmic6_mux),
	SND_SOC_DAPM_MUX("TX DMIC MUX7", SND_SOC_NOPM, 4, 0, &tx_dmic7_mux),

	SND_SOC_DAPM_INPUT("TX DMIC0"),
	SND_SOC_DAPM_INPUT("TX DMIC1"),
	SND_SOC_DAPM_INPUT("TX DMIC2"),
	SND_SOC_DAPM_INPUT("TX DMIC3"),
	SND_SOC_DAPM_INPUT("TX DMIC4"),
	SND_SOC_DAPM_INPUT("TX DMIC5"),
	SND_SOC_DAPM_INPUT("TX DMIC6"),
	SND_SOC_DAPM_INPUT("TX DMIC7"),

	SND_SOC_DAPM_MUX_E("TX DEC0 MUX", SND_SOC_NOPM,
			   TX_MACRO_DEC0, 0,
			   &tx_dec0_mux, tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC1 MUX", SND_SOC_NOPM,
			   TX_MACRO_DEC1, 0,
			   &tx_dec1_mux, tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC2 MUX", SND_SOC_NOPM,
			   TX_MACRO_DEC2, 0,
			   &tx_dec2_mux, tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC3 MUX", SND_SOC_NOPM,
			   TX_MACRO_DEC3, 0,
			   &tx_dec3_mux, tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC4 MUX", SND_SOC_NOPM,
			   TX_MACRO_DEC4, 0,
			   &tx_dec4_mux, tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC5 MUX", SND_SOC_NOPM,
			   TX_MACRO_DEC5, 0,
			   &tx_dec5_mux, tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC6 MUX", SND_SOC_NOPM,
			   TX_MACRO_DEC6, 0,
			   &tx_dec6_mux, tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC7 MUX", SND_SOC_NOPM,
			   TX_MACRO_DEC7, 0,
			   &tx_dec7_mux, tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("TX_MCLK", 0, SND_SOC_NOPM, 0, 0,
	tx_macro_mclk_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("TX_SWR_CLK", 0, SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("VA_SWR_CLK", 0, SND_SOC_NOPM, 0, 0,
			NULL, 0),
};

static const struct snd_soc_dapm_route tx_audio_map[] = {
	{"TX_AIF1 CAP", NULL, "TX_MCLK"},
	{"TX_AIF2 CAP", NULL, "TX_MCLK"},
	{"TX_AIF3 CAP", NULL, "TX_MCLK"},

	{"TX_AIF1 CAP", NULL, "TX_AIF1_CAP Mixer"},
	{"TX_AIF2 CAP", NULL, "TX_AIF2_CAP Mixer"},
	{"TX_AIF3 CAP", NULL, "TX_AIF3_CAP Mixer"},

	{"TX_AIF1_CAP Mixer", "DEC0", "TX DEC0 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC1", "TX DEC1 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC2", "TX DEC2 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC3", "TX DEC3 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC4", "TX DEC4 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC5", "TX DEC5 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC6", "TX DEC6 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC7", "TX DEC7 MUX"},

	{"TX_AIF2_CAP Mixer", "DEC0", "TX DEC0 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC1", "TX DEC1 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC2", "TX DEC2 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC3", "TX DEC3 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC4", "TX DEC4 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC5", "TX DEC5 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC6", "TX DEC6 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC7", "TX DEC7 MUX"},

	{"TX_AIF3_CAP Mixer", "DEC0", "TX DEC0 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC1", "TX DEC1 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC2", "TX DEC2 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC3", "TX DEC3 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC4", "TX DEC4 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC5", "TX DEC5 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC6", "TX DEC6 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC7", "TX DEC7 MUX"},

	{"TX DEC0 MUX", NULL, "TX_MCLK"},
	{"TX DEC1 MUX", NULL, "TX_MCLK"},
	{"TX DEC2 MUX", NULL, "TX_MCLK"},
	{"TX DEC3 MUX", NULL, "TX_MCLK"},
	{"TX DEC4 MUX", NULL, "TX_MCLK"},
	{"TX DEC5 MUX", NULL, "TX_MCLK"},
	{"TX DEC6 MUX", NULL, "TX_MCLK"},
	{"TX DEC7 MUX", NULL, "TX_MCLK"},

	{"TX DEC0 MUX", "MSM_DMIC", "TX DMIC MUX0"},
	{"TX DMIC MUX0", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX0", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX0", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX0", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX0", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX0", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX0", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX0", "DMIC7", "TX DMIC7"},

	{"TX DEC1 MUX", "MSM_DMIC", "TX DMIC MUX1"},
	{"TX DMIC MUX1", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX1", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX1", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX1", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX1", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX1", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX1", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX1", "DMIC7", "TX DMIC7"},

	{"TX DEC2 MUX", "MSM_DMIC", "TX DMIC MUX2"},
	{"TX DMIC MUX2", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX2", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX2", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX2", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX2", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX2", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX2", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX2", "DMIC7", "TX DMIC7"},

	{"TX DEC3 MUX", "MSM_DMIC", "TX DMIC MUX3"},
	{"TX DMIC MUX3", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX3", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX3", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX3", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX3", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX3", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX3", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX3", "DMIC7", "TX DMIC7"},

	{"TX DEC4 MUX", "MSM_DMIC", "TX DMIC MUX4"},
	{"TX DMIC MUX4", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX4", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX4", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX4", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX4", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX4", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX4", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX4", "DMIC7", "TX DMIC7"},

	{"TX DEC5 MUX", "MSM_DMIC", "TX DMIC MUX5"},
	{"TX DMIC MUX5", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX5", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX5", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX5", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX5", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX5", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX5", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX5", "DMIC7", "TX DMIC7"},

	{"TX DEC6 MUX", "MSM_DMIC", "TX DMIC MUX6"},
	{"TX DMIC MUX6", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX6", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX6", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX6", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX6", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX6", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX6", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX6", "DMIC7", "TX DMIC7"},

	{"TX DEC7 MUX", "MSM_DMIC", "TX DMIC MUX7"},
	{"TX DMIC MUX7", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX7", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX7", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX7", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX7", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX7", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX7", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX7", "DMIC7", "TX DMIC7"},
};

/* Controls and routes specific to LPASS <= v9.0.0 */
static const char * const smic_mux_text_v9[] = {
	"ZERO", "ADC0", "ADC1", "ADC2", "ADC3", "SWR_DMIC0",
	"SWR_DMIC1", "SWR_DMIC2", "SWR_DMIC3", "SWR_DMIC4",
	"SWR_DMIC5", "SWR_DMIC6", "SWR_DMIC7"
};

static SOC_ENUM_SINGLE_DECL(tx_smic0_enum_v9, CDC_TX_INP_MUX_ADC_MUX0_CFG0,
			0, smic_mux_text_v9);

static SOC_ENUM_SINGLE_DECL(tx_smic1_enum_v9, CDC_TX_INP_MUX_ADC_MUX1_CFG0,
			0, smic_mux_text_v9);

static SOC_ENUM_SINGLE_DECL(tx_smic2_enum_v9, CDC_TX_INP_MUX_ADC_MUX2_CFG0,
			0, smic_mux_text_v9);

static SOC_ENUM_SINGLE_DECL(tx_smic3_enum_v9, CDC_TX_INP_MUX_ADC_MUX3_CFG0,
			0, smic_mux_text_v9);

static SOC_ENUM_SINGLE_DECL(tx_smic4_enum_v9, CDC_TX_INP_MUX_ADC_MUX4_CFG0,
			0, smic_mux_text_v9);

static SOC_ENUM_SINGLE_DECL(tx_smic5_enum_v9, CDC_TX_INP_MUX_ADC_MUX5_CFG0,
			0, smic_mux_text_v9);

static SOC_ENUM_SINGLE_DECL(tx_smic6_enum_v9, CDC_TX_INP_MUX_ADC_MUX6_CFG0,
			0, smic_mux_text_v9);

static SOC_ENUM_SINGLE_DECL(tx_smic7_enum_v9, CDC_TX_INP_MUX_ADC_MUX7_CFG0,
			0, smic_mux_text_v9);

static const struct snd_kcontrol_new tx_smic0_mux_v9 = SOC_DAPM_ENUM_EXT("tx_smic0", tx_smic0_enum_v9,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic1_mux_v9 = SOC_DAPM_ENUM_EXT("tx_smic1", tx_smic1_enum_v9,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic2_mux_v9 = SOC_DAPM_ENUM_EXT("tx_smic2", tx_smic2_enum_v9,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic3_mux_v9 = SOC_DAPM_ENUM_EXT("tx_smic3", tx_smic3_enum_v9,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic4_mux_v9 = SOC_DAPM_ENUM_EXT("tx_smic4", tx_smic4_enum_v9,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic5_mux_v9 = SOC_DAPM_ENUM_EXT("tx_smic5", tx_smic5_enum_v9,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic6_mux_v9 = SOC_DAPM_ENUM_EXT("tx_smic6", tx_smic6_enum_v9,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic7_mux_v9 = SOC_DAPM_ENUM_EXT("tx_smic7", tx_smic7_enum_v9,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);

static const struct snd_soc_dapm_widget tx_macro_dapm_widgets_v9[] = {
	SND_SOC_DAPM_MUX("TX SMIC MUX0", SND_SOC_NOPM, 0, 0, &tx_smic0_mux_v9),
	SND_SOC_DAPM_MUX("TX SMIC MUX1", SND_SOC_NOPM, 0, 0, &tx_smic1_mux_v9),
	SND_SOC_DAPM_MUX("TX SMIC MUX2", SND_SOC_NOPM, 0, 0, &tx_smic2_mux_v9),
	SND_SOC_DAPM_MUX("TX SMIC MUX3", SND_SOC_NOPM, 0, 0, &tx_smic3_mux_v9),
	SND_SOC_DAPM_MUX("TX SMIC MUX4", SND_SOC_NOPM, 0, 0, &tx_smic4_mux_v9),
	SND_SOC_DAPM_MUX("TX SMIC MUX5", SND_SOC_NOPM, 0, 0, &tx_smic5_mux_v9),
	SND_SOC_DAPM_MUX("TX SMIC MUX6", SND_SOC_NOPM, 0, 0, &tx_smic6_mux_v9),
	SND_SOC_DAPM_MUX("TX SMIC MUX7", SND_SOC_NOPM, 0, 0, &tx_smic7_mux_v9),

	SND_SOC_DAPM_INPUT("TX SWR_ADC0"),
	SND_SOC_DAPM_INPUT("TX SWR_ADC1"),
	SND_SOC_DAPM_INPUT("TX SWR_ADC2"),
	SND_SOC_DAPM_INPUT("TX SWR_ADC3"),
	SND_SOC_DAPM_INPUT("TX SWR_DMIC0"),
	SND_SOC_DAPM_INPUT("TX SWR_DMIC1"),
	SND_SOC_DAPM_INPUT("TX SWR_DMIC2"),
	SND_SOC_DAPM_INPUT("TX SWR_DMIC3"),
	SND_SOC_DAPM_INPUT("TX SWR_DMIC4"),
	SND_SOC_DAPM_INPUT("TX SWR_DMIC5"),
	SND_SOC_DAPM_INPUT("TX SWR_DMIC6"),
	SND_SOC_DAPM_INPUT("TX SWR_DMIC7"),
};

static const struct snd_soc_dapm_route tx_audio_map_v9[] = {
	{"TX DEC0 MUX", "SWR_MIC", "TX SMIC MUX0"},
	{"TX SMIC MUX0", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX0", "ADC0", "TX SWR_ADC0"},
	{"TX SMIC MUX0", "ADC1", "TX SWR_ADC1"},
	{"TX SMIC MUX0", "ADC2", "TX SWR_ADC2"},
	{"TX SMIC MUX0", "ADC3", "TX SWR_ADC3"},
	{"TX SMIC MUX0", "SWR_DMIC0", "TX SWR_DMIC0"},
	{"TX SMIC MUX0", "SWR_DMIC1", "TX SWR_DMIC1"},
	{"TX SMIC MUX0", "SWR_DMIC2", "TX SWR_DMIC2"},
	{"TX SMIC MUX0", "SWR_DMIC3", "TX SWR_DMIC3"},
	{"TX SMIC MUX0", "SWR_DMIC4", "TX SWR_DMIC4"},
	{"TX SMIC MUX0", "SWR_DMIC5", "TX SWR_DMIC5"},
	{"TX SMIC MUX0", "SWR_DMIC6", "TX SWR_DMIC6"},
	{"TX SMIC MUX0", "SWR_DMIC7", "TX SWR_DMIC7"},

	{"TX DEC1 MUX", "SWR_MIC", "TX SMIC MUX1"},
	{"TX SMIC MUX1", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX1", "ADC0", "TX SWR_ADC0"},
	{"TX SMIC MUX1", "ADC1", "TX SWR_ADC1"},
	{"TX SMIC MUX1", "ADC2", "TX SWR_ADC2"},
	{"TX SMIC MUX1", "ADC3", "TX SWR_ADC3"},
	{"TX SMIC MUX1", "SWR_DMIC0", "TX SWR_DMIC0"},
	{"TX SMIC MUX1", "SWR_DMIC1", "TX SWR_DMIC1"},
	{"TX SMIC MUX1", "SWR_DMIC2", "TX SWR_DMIC2"},
	{"TX SMIC MUX1", "SWR_DMIC3", "TX SWR_DMIC3"},
	{"TX SMIC MUX1", "SWR_DMIC4", "TX SWR_DMIC4"},
	{"TX SMIC MUX1", "SWR_DMIC5", "TX SWR_DMIC5"},
	{"TX SMIC MUX1", "SWR_DMIC6", "TX SWR_DMIC6"},
	{"TX SMIC MUX1", "SWR_DMIC7", "TX SWR_DMIC7"},

	{"TX DEC2 MUX", "SWR_MIC", "TX SMIC MUX2"},
	{"TX SMIC MUX2", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX2", "ADC0", "TX SWR_ADC0"},
	{"TX SMIC MUX2", "ADC1", "TX SWR_ADC1"},
	{"TX SMIC MUX2", "ADC2", "TX SWR_ADC2"},
	{"TX SMIC MUX2", "ADC3", "TX SWR_ADC3"},
	{"TX SMIC MUX2", "SWR_DMIC0", "TX SWR_DMIC0"},
	{"TX SMIC MUX2", "SWR_DMIC1", "TX SWR_DMIC1"},
	{"TX SMIC MUX2", "SWR_DMIC2", "TX SWR_DMIC2"},
	{"TX SMIC MUX2", "SWR_DMIC3", "TX SWR_DMIC3"},
	{"TX SMIC MUX2", "SWR_DMIC4", "TX SWR_DMIC4"},
	{"TX SMIC MUX2", "SWR_DMIC5", "TX SWR_DMIC5"},
	{"TX SMIC MUX2", "SWR_DMIC6", "TX SWR_DMIC6"},
	{"TX SMIC MUX2", "SWR_DMIC7", "TX SWR_DMIC7"},

	{"TX DEC3 MUX", "SWR_MIC", "TX SMIC MUX3"},
	{"TX SMIC MUX3", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX3", "ADC0", "TX SWR_ADC0"},
	{"TX SMIC MUX3", "ADC1", "TX SWR_ADC1"},
	{"TX SMIC MUX3", "ADC2", "TX SWR_ADC2"},
	{"TX SMIC MUX3", "ADC3", "TX SWR_ADC3"},
	{"TX SMIC MUX3", "SWR_DMIC0", "TX SWR_DMIC0"},
	{"TX SMIC MUX3", "SWR_DMIC1", "TX SWR_DMIC1"},
	{"TX SMIC MUX3", "SWR_DMIC2", "TX SWR_DMIC2"},
	{"TX SMIC MUX3", "SWR_DMIC3", "TX SWR_DMIC3"},
	{"TX SMIC MUX3", "SWR_DMIC4", "TX SWR_DMIC4"},
	{"TX SMIC MUX3", "SWR_DMIC5", "TX SWR_DMIC5"},
	{"TX SMIC MUX3", "SWR_DMIC6", "TX SWR_DMIC6"},
	{"TX SMIC MUX3", "SWR_DMIC7", "TX SWR_DMIC7"},

	{"TX DEC4 MUX", "SWR_MIC", "TX SMIC MUX4"},
	{"TX SMIC MUX4", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX4", "ADC0", "TX SWR_ADC0"},
	{"TX SMIC MUX4", "ADC1", "TX SWR_ADC1"},
	{"TX SMIC MUX4", "ADC2", "TX SWR_ADC2"},
	{"TX SMIC MUX4", "ADC3", "TX SWR_ADC3"},
	{"TX SMIC MUX4", "SWR_DMIC0", "TX SWR_DMIC0"},
	{"TX SMIC MUX4", "SWR_DMIC1", "TX SWR_DMIC1"},
	{"TX SMIC MUX4", "SWR_DMIC2", "TX SWR_DMIC2"},
	{"TX SMIC MUX4", "SWR_DMIC3", "TX SWR_DMIC3"},
	{"TX SMIC MUX4", "SWR_DMIC4", "TX SWR_DMIC4"},
	{"TX SMIC MUX4", "SWR_DMIC5", "TX SWR_DMIC5"},
	{"TX SMIC MUX4", "SWR_DMIC6", "TX SWR_DMIC6"},
	{"TX SMIC MUX4", "SWR_DMIC7", "TX SWR_DMIC7"},

	{"TX DEC5 MUX", "SWR_MIC", "TX SMIC MUX5"},
	{"TX SMIC MUX5", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX5", "ADC0", "TX SWR_ADC0"},
	{"TX SMIC MUX5", "ADC1", "TX SWR_ADC1"},
	{"TX SMIC MUX5", "ADC2", "TX SWR_ADC2"},
	{"TX SMIC MUX5", "ADC3", "TX SWR_ADC3"},
	{"TX SMIC MUX5", "SWR_DMIC0", "TX SWR_DMIC0"},
	{"TX SMIC MUX5", "SWR_DMIC1", "TX SWR_DMIC1"},
	{"TX SMIC MUX5", "SWR_DMIC2", "TX SWR_DMIC2"},
	{"TX SMIC MUX5", "SWR_DMIC3", "TX SWR_DMIC3"},
	{"TX SMIC MUX5", "SWR_DMIC4", "TX SWR_DMIC4"},
	{"TX SMIC MUX5", "SWR_DMIC5", "TX SWR_DMIC5"},
	{"TX SMIC MUX5", "SWR_DMIC6", "TX SWR_DMIC6"},
	{"TX SMIC MUX5", "SWR_DMIC7", "TX SWR_DMIC7"},

	{"TX DEC6 MUX", "SWR_MIC", "TX SMIC MUX6"},
	{"TX SMIC MUX6", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX6", "ADC0", "TX SWR_ADC0"},
	{"TX SMIC MUX6", "ADC1", "TX SWR_ADC1"},
	{"TX SMIC MUX6", "ADC2", "TX SWR_ADC2"},
	{"TX SMIC MUX6", "ADC3", "TX SWR_ADC3"},
	{"TX SMIC MUX6", "SWR_DMIC0", "TX SWR_DMIC0"},
	{"TX SMIC MUX6", "SWR_DMIC1", "TX SWR_DMIC1"},
	{"TX SMIC MUX6", "SWR_DMIC2", "TX SWR_DMIC2"},
	{"TX SMIC MUX6", "SWR_DMIC3", "TX SWR_DMIC3"},
	{"TX SMIC MUX6", "SWR_DMIC4", "TX SWR_DMIC4"},
	{"TX SMIC MUX6", "SWR_DMIC5", "TX SWR_DMIC5"},
	{"TX SMIC MUX6", "SWR_DMIC6", "TX SWR_DMIC6"},
	{"TX SMIC MUX6", "SWR_DMIC7", "TX SWR_DMIC7"},

	{"TX DEC7 MUX", "SWR_MIC", "TX SMIC MUX7"},
	{"TX SMIC MUX7", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX7", "ADC0", "TX SWR_ADC0"},
	{"TX SMIC MUX7", "ADC1", "TX SWR_ADC1"},
	{"TX SMIC MUX7", "ADC2", "TX SWR_ADC2"},
	{"TX SMIC MUX7", "ADC3", "TX SWR_ADC3"},
	{"TX SMIC MUX7", "SWR_DMIC0", "TX SWR_DMIC0"},
	{"TX SMIC MUX7", "SWR_DMIC1", "TX SWR_DMIC1"},
	{"TX SMIC MUX7", "SWR_DMIC2", "TX SWR_DMIC2"},
	{"TX SMIC MUX7", "SWR_DMIC3", "TX SWR_DMIC3"},
	{"TX SMIC MUX7", "SWR_DMIC4", "TX SWR_DMIC4"},
	{"TX SMIC MUX7", "SWR_DMIC5", "TX SWR_DMIC5"},
	{"TX SMIC MUX7", "SWR_DMIC6", "TX SWR_DMIC6"},
	{"TX SMIC MUX7", "SWR_DMIC7", "TX SWR_DMIC7"},
};

/* Controls and routes specific to LPASS >= v9.2.0 */
static const char * const smic_mux_text_v9_2[] = {
	"ZERO", "SWR_MIC0", "SWR_MIC1", "SWR_MIC2", "SWR_MIC3",
	"SWR_MIC4", "SWR_MIC5", "SWR_MIC6", "SWR_MIC7",
	"SWR_MIC8", "SWR_MIC9", "SWR_MIC10", "SWR_MIC11"
};

static SOC_ENUM_SINGLE_DECL(tx_smic0_enum_v9_2, CDC_TX_INP_MUX_ADC_MUX0_CFG0,
			0, smic_mux_text_v9_2);

static SOC_ENUM_SINGLE_DECL(tx_smic1_enum_v9_2, CDC_TX_INP_MUX_ADC_MUX1_CFG0,
			0, smic_mux_text_v9_2);

static SOC_ENUM_SINGLE_DECL(tx_smic2_enum_v9_2, CDC_TX_INP_MUX_ADC_MUX2_CFG0,
			0, smic_mux_text_v9_2);

static SOC_ENUM_SINGLE_DECL(tx_smic3_enum_v9_2, CDC_TX_INP_MUX_ADC_MUX3_CFG0,
			0, smic_mux_text_v9_2);

static SOC_ENUM_SINGLE_DECL(tx_smic4_enum_v9_2, CDC_TX_INP_MUX_ADC_MUX4_CFG0,
			0, smic_mux_text_v9_2);

static SOC_ENUM_SINGLE_DECL(tx_smic5_enum_v9_2, CDC_TX_INP_MUX_ADC_MUX5_CFG0,
			0, smic_mux_text_v9_2);

static SOC_ENUM_SINGLE_DECL(tx_smic6_enum_v9_2, CDC_TX_INP_MUX_ADC_MUX6_CFG0,
			0, smic_mux_text_v9_2);

static SOC_ENUM_SINGLE_DECL(tx_smic7_enum_v9_2, CDC_TX_INP_MUX_ADC_MUX7_CFG0,
			0, smic_mux_text_v9_2);

static const struct snd_kcontrol_new tx_smic0_mux_v9_2 = SOC_DAPM_ENUM_EXT("tx_smic0", tx_smic0_enum_v9_2,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic1_mux_v9_2 = SOC_DAPM_ENUM_EXT("tx_smic1", tx_smic1_enum_v9_2,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic2_mux_v9_2 = SOC_DAPM_ENUM_EXT("tx_smic2", tx_smic2_enum_v9_2,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic3_mux_v9_2 = SOC_DAPM_ENUM_EXT("tx_smic3", tx_smic3_enum_v9_2,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic4_mux_v9_2 = SOC_DAPM_ENUM_EXT("tx_smic4", tx_smic4_enum_v9_2,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic5_mux_v9_2 = SOC_DAPM_ENUM_EXT("tx_smic5", tx_smic5_enum_v9_2,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic6_mux_v9_2 = SOC_DAPM_ENUM_EXT("tx_smic6", tx_smic6_enum_v9_2,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);
static const struct snd_kcontrol_new tx_smic7_mux_v9_2 = SOC_DAPM_ENUM_EXT("tx_smic7", tx_smic7_enum_v9_2,
			snd_soc_dapm_get_enum_double, tx_macro_put_dec_enum);

static const struct snd_soc_dapm_widget tx_macro_dapm_widgets_v9_2[] = {
	SND_SOC_DAPM_MUX("TX SMIC MUX0", SND_SOC_NOPM, 0, 0, &tx_smic0_mux_v9_2),
	SND_SOC_DAPM_MUX("TX SMIC MUX1", SND_SOC_NOPM, 0, 0, &tx_smic1_mux_v9_2),
	SND_SOC_DAPM_MUX("TX SMIC MUX2", SND_SOC_NOPM, 0, 0, &tx_smic2_mux_v9_2),
	SND_SOC_DAPM_MUX("TX SMIC MUX3", SND_SOC_NOPM, 0, 0, &tx_smic3_mux_v9_2),
	SND_SOC_DAPM_MUX("TX SMIC MUX4", SND_SOC_NOPM, 0, 0, &tx_smic4_mux_v9_2),
	SND_SOC_DAPM_MUX("TX SMIC MUX5", SND_SOC_NOPM, 0, 0, &tx_smic5_mux_v9_2),
	SND_SOC_DAPM_MUX("TX SMIC MUX6", SND_SOC_NOPM, 0, 0, &tx_smic6_mux_v9_2),
	SND_SOC_DAPM_MUX("TX SMIC MUX7", SND_SOC_NOPM, 0, 0, &tx_smic7_mux_v9_2),

	SND_SOC_DAPM_INPUT("TX SWR_INPUT0"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT1"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT2"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT3"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT4"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT5"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT6"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT7"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT8"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT9"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT10"),
	SND_SOC_DAPM_INPUT("TX SWR_INPUT11"),
};

static const struct snd_soc_dapm_route tx_audio_map_v9_2[] = {
	{"TX DEC0 MUX", "SWR_MIC", "TX SMIC MUX0"},
	{"TX SMIC MUX0", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX0", "SWR_MIC0", "TX SWR_INPUT0"},
	{"TX SMIC MUX0", "SWR_MIC1", "TX SWR_INPUT1"},
	{"TX SMIC MUX0", "SWR_MIC2", "TX SWR_INPUT2"},
	{"TX SMIC MUX0", "SWR_MIC3", "TX SWR_INPUT3"},
	{"TX SMIC MUX0", "SWR_MIC4", "TX SWR_INPUT4"},
	{"TX SMIC MUX0", "SWR_MIC5", "TX SWR_INPUT5"},
	{"TX SMIC MUX0", "SWR_MIC6", "TX SWR_INPUT6"},
	{"TX SMIC MUX0", "SWR_MIC7", "TX SWR_INPUT7"},
	{"TX SMIC MUX0", "SWR_MIC8", "TX SWR_INPUT8"},
	{"TX SMIC MUX0", "SWR_MIC9", "TX SWR_INPUT9"},
	{"TX SMIC MUX0", "SWR_MIC10", "TX SWR_INPUT11"},
	{"TX SMIC MUX0", "SWR_MIC11", "TX SWR_INPUT10"},

	{"TX DEC1 MUX", "SWR_MIC", "TX SMIC MUX1"},
	{"TX SMIC MUX1", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX1", "SWR_MIC0", "TX SWR_INPUT0"},
	{"TX SMIC MUX1", "SWR_MIC1", "TX SWR_INPUT1"},
	{"TX SMIC MUX1", "SWR_MIC2", "TX SWR_INPUT2"},
	{"TX SMIC MUX1", "SWR_MIC3", "TX SWR_INPUT3"},
	{"TX SMIC MUX1", "SWR_MIC4", "TX SWR_INPUT4"},
	{"TX SMIC MUX1", "SWR_MIC5", "TX SWR_INPUT5"},
	{"TX SMIC MUX1", "SWR_MIC6", "TX SWR_INPUT6"},
	{"TX SMIC MUX1", "SWR_MIC7", "TX SWR_INPUT7"},
	{"TX SMIC MUX1", "SWR_MIC8", "TX SWR_INPUT8"},
	{"TX SMIC MUX1", "SWR_MIC9", "TX SWR_INPUT9"},
	{"TX SMIC MUX1", "SWR_MIC10", "TX SWR_INPUT10"},
	{"TX SMIC MUX1", "SWR_MIC11", "TX SWR_INPUT11"},

	{"TX DEC2 MUX", "SWR_MIC", "TX SMIC MUX2"},
	{"TX SMIC MUX2", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX2", "SWR_MIC0", "TX SWR_INPUT0"},
	{"TX SMIC MUX2", "SWR_MIC1", "TX SWR_INPUT1"},
	{"TX SMIC MUX2", "SWR_MIC2", "TX SWR_INPUT2"},
	{"TX SMIC MUX2", "SWR_MIC3", "TX SWR_INPUT3"},
	{"TX SMIC MUX2", "SWR_MIC4", "TX SWR_INPUT4"},
	{"TX SMIC MUX2", "SWR_MIC5", "TX SWR_INPUT5"},
	{"TX SMIC MUX2", "SWR_MIC6", "TX SWR_INPUT6"},
	{"TX SMIC MUX2", "SWR_MIC7", "TX SWR_INPUT7"},
	{"TX SMIC MUX2", "SWR_MIC8", "TX SWR_INPUT8"},
	{"TX SMIC MUX2", "SWR_MIC9", "TX SWR_INPUT9"},
	{"TX SMIC MUX2", "SWR_MIC10", "TX SWR_INPUT10"},
	{"TX SMIC MUX2", "SWR_MIC11", "TX SWR_INPUT11"},

	{"TX DEC3 MUX", "SWR_MIC", "TX SMIC MUX3"},
	{"TX SMIC MUX3", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX3", "SWR_MIC0", "TX SWR_INPUT0"},
	{"TX SMIC MUX3", "SWR_MIC1", "TX SWR_INPUT1"},
	{"TX SMIC MUX3", "SWR_MIC2", "TX SWR_INPUT2"},
	{"TX SMIC MUX3", "SWR_MIC3", "TX SWR_INPUT3"},
	{"TX SMIC MUX3", "SWR_MIC4", "TX SWR_INPUT4"},
	{"TX SMIC MUX3", "SWR_MIC5", "TX SWR_INPUT5"},
	{"TX SMIC MUX3", "SWR_MIC6", "TX SWR_INPUT6"},
	{"TX SMIC MUX3", "SWR_MIC7", "TX SWR_INPUT7"},
	{"TX SMIC MUX3", "SWR_MIC8", "TX SWR_INPUT8"},
	{"TX SMIC MUX3", "SWR_MIC9", "TX SWR_INPUT9"},
	{"TX SMIC MUX3", "SWR_MIC10", "TX SWR_INPUT10"},
	{"TX SMIC MUX3", "SWR_MIC11", "TX SWR_INPUT11"},

	{"TX DEC4 MUX", "SWR_MIC", "TX SMIC MUX4"},
	{"TX SMIC MUX4", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX4", "SWR_MIC0", "TX SWR_INPUT0"},
	{"TX SMIC MUX4", "SWR_MIC1", "TX SWR_INPUT1"},
	{"TX SMIC MUX4", "SWR_MIC2", "TX SWR_INPUT2"},
	{"TX SMIC MUX4", "SWR_MIC3", "TX SWR_INPUT3"},
	{"TX SMIC MUX4", "SWR_MIC4", "TX SWR_INPUT4"},
	{"TX SMIC MUX4", "SWR_MIC5", "TX SWR_INPUT5"},
	{"TX SMIC MUX4", "SWR_MIC6", "TX SWR_INPUT6"},
	{"TX SMIC MUX4", "SWR_MIC7", "TX SWR_INPUT7"},
	{"TX SMIC MUX4", "SWR_MIC8", "TX SWR_INPUT8"},
	{"TX SMIC MUX4", "SWR_MIC9", "TX SWR_INPUT9"},
	{"TX SMIC MUX4", "SWR_MIC10", "TX SWR_INPUT10"},
	{"TX SMIC MUX4", "SWR_MIC11", "TX SWR_INPUT11"},

	{"TX DEC5 MUX", "SWR_MIC", "TX SMIC MUX5"},
	{"TX SMIC MUX5", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX5", "SWR_MIC0", "TX SWR_INPUT0"},
	{"TX SMIC MUX5", "SWR_MIC1", "TX SWR_INPUT1"},
	{"TX SMIC MUX5", "SWR_MIC2", "TX SWR_INPUT2"},
	{"TX SMIC MUX5", "SWR_MIC3", "TX SWR_INPUT3"},
	{"TX SMIC MUX5", "SWR_MIC4", "TX SWR_INPUT4"},
	{"TX SMIC MUX5", "SWR_MIC5", "TX SWR_INPUT5"},
	{"TX SMIC MUX5", "SWR_MIC6", "TX SWR_INPUT6"},
	{"TX SMIC MUX5", "SWR_MIC7", "TX SWR_INPUT7"},
	{"TX SMIC MUX5", "SWR_MIC8", "TX SWR_INPUT8"},
	{"TX SMIC MUX5", "SWR_MIC9", "TX SWR_INPUT9"},
	{"TX SMIC MUX5", "SWR_MIC10", "TX SWR_INPUT10"},
	{"TX SMIC MUX5", "SWR_MIC11", "TX SWR_INPUT11"},

	{"TX DEC6 MUX", "SWR_MIC", "TX SMIC MUX6"},
	{"TX SMIC MUX6", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX6", "SWR_MIC0", "TX SWR_INPUT0"},
	{"TX SMIC MUX6", "SWR_MIC1", "TX SWR_INPUT1"},
	{"TX SMIC MUX6", "SWR_MIC2", "TX SWR_INPUT2"},
	{"TX SMIC MUX6", "SWR_MIC3", "TX SWR_INPUT3"},
	{"TX SMIC MUX6", "SWR_MIC4", "TX SWR_INPUT4"},
	{"TX SMIC MUX6", "SWR_MIC5", "TX SWR_INPUT5"},
	{"TX SMIC MUX6", "SWR_MIC6", "TX SWR_INPUT6"},
	{"TX SMIC MUX6", "SWR_MIC7", "TX SWR_INPUT7"},
	{"TX SMIC MUX6", "SWR_MIC8", "TX SWR_INPUT8"},
	{"TX SMIC MUX6", "SWR_MIC9", "TX SWR_INPUT9"},
	{"TX SMIC MUX6", "SWR_MIC10", "TX SWR_INPUT10"},
	{"TX SMIC MUX6", "SWR_MIC11", "TX SWR_INPUT11"},

	{"TX DEC7 MUX", "SWR_MIC", "TX SMIC MUX7"},
	{"TX SMIC MUX7", NULL, "TX_SWR_CLK"},
	{"TX SMIC MUX7", "SWR_MIC0", "TX SWR_INPUT0"},
	{"TX SMIC MUX7", "SWR_MIC1", "TX SWR_INPUT1"},
	{"TX SMIC MUX7", "SWR_MIC2", "TX SWR_INPUT2"},
	{"TX SMIC MUX7", "SWR_MIC3", "TX SWR_INPUT3"},
	{"TX SMIC MUX7", "SWR_MIC4", "TX SWR_INPUT4"},
	{"TX SMIC MUX7", "SWR_MIC5", "TX SWR_INPUT5"},
	{"TX SMIC MUX7", "SWR_MIC6", "TX SWR_INPUT6"},
	{"TX SMIC MUX7", "SWR_MIC7", "TX SWR_INPUT7"},
	{"TX SMIC MUX7", "SWR_MIC8", "TX SWR_INPUT8"},
	{"TX SMIC MUX7", "SWR_MIC9", "TX SWR_INPUT9"},
	{"TX SMIC MUX7", "SWR_MIC10", "TX SWR_INPUT10"},
	{"TX SMIC MUX7", "SWR_MIC11", "TX SWR_INPUT11"},
};

static const struct snd_kcontrol_new tx_macro_snd_controls[] = {
	SOC_SINGLE_S8_TLV("TX_DEC0 Volume",
			  CDC_TX0_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC1 Volume",
			  CDC_TX1_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC2 Volume",
			  CDC_TX2_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC3 Volume",
			  CDC_TX3_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC4 Volume",
			  CDC_TX4_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC5 Volume",
			  CDC_TX5_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC6 Volume",
			  CDC_TX6_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC7 Volume",
			  CDC_TX7_TX_VOL_CTL,
			  -84, 40, digital_gain),

	SOC_ENUM_EXT("DEC0 MODE", dec_mode_mux_enum[0],
			tx_macro_dec_mode_get, tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC1 MODE", dec_mode_mux_enum[1],
			tx_macro_dec_mode_get, tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC2 MODE", dec_mode_mux_enum[2],
			tx_macro_dec_mode_get, tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC3 MODE", dec_mode_mux_enum[3],
			tx_macro_dec_mode_get, tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC4 MODE", dec_mode_mux_enum[4],
			tx_macro_dec_mode_get, tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC5 MODE", dec_mode_mux_enum[5],
			tx_macro_dec_mode_get, tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC6 MODE", dec_mode_mux_enum[6],
			tx_macro_dec_mode_get, tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC7 MODE", dec_mode_mux_enum[7],
			tx_macro_dec_mode_get, tx_macro_dec_mode_put),

	SOC_SINGLE_EXT("DEC0_BCS Switch", SND_SOC_NOPM, 0, 1, 0,
		       tx_macro_get_bcs, tx_macro_set_bcs),
};

static int tx_macro_component_extend(struct snd_soc_component *comp)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(comp);
	struct tx_macro *tx = snd_soc_component_get_drvdata(comp);
	int ret;

	if (tx->data->extra_widgets_num) {
		ret = snd_soc_dapm_new_controls(dapm, tx->data->extra_widgets,
						tx->data->extra_widgets_num);
		if (ret) {
			dev_err(tx->dev, "failed to add extra widgets: %d\n", ret);
			return ret;
		}
	}

	if (tx->data->extra_routes_num) {
		ret = snd_soc_dapm_add_routes(dapm, tx->data->extra_routes,
					      tx->data->extra_routes_num);
		if (ret) {
			dev_err(tx->dev, "failed to add extra routes: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int tx_macro_component_probe(struct snd_soc_component *comp)
{
	struct tx_macro *tx = snd_soc_component_get_drvdata(comp);
	int i, ret;

	ret = tx_macro_component_extend(comp);
	if (ret)
		return ret;

	snd_soc_component_init_regmap(comp, tx->regmap);

	for (i = 0; i < NUM_DECIMATORS; i++) {
		tx->tx_hpf_work[i].tx = tx;
		tx->tx_hpf_work[i].decimator = i;
		INIT_DELAYED_WORK(&tx->tx_hpf_work[i].dwork,
			tx_macro_tx_hpf_corner_freq_callback);
	}

	for (i = 0; i < NUM_DECIMATORS; i++) {
		tx->tx_mute_dwork[i].tx = tx;
		tx->tx_mute_dwork[i].decimator = i;
		INIT_DELAYED_WORK(&tx->tx_mute_dwork[i].dwork,
			  tx_macro_mute_update_callback);
	}
	tx->component = comp;

	snd_soc_component_update_bits(comp, CDC_TX0_TX_PATH_SEC7, 0x3F,
				      0x0A);
	/* Enable swr mic0 and mic1 clock */
	snd_soc_component_write(comp, CDC_TX_TOP_CSR_SWR_AMIC0_CTL,
				CDC_TX_SWR_MIC_CLK_DEFAULT);
	snd_soc_component_write(comp, CDC_TX_TOP_CSR_SWR_AMIC1_CTL,
				CDC_TX_SWR_MIC_CLK_DEFAULT);

	return 0;
}

static int swclk_gate_enable(struct clk_hw *hw)
{
	struct tx_macro *tx = to_tx_macro(hw);
	struct regmap *regmap = tx->regmap;
	int ret;

	ret = clk_prepare_enable(tx->mclk);
	if (ret) {
		dev_err(tx->dev, "failed to enable mclk\n");
		return ret;
	}

	tx_macro_mclk_enable(tx, true);

	regmap_update_bits(regmap, CDC_TX_CLK_RST_CTRL_SWR_CONTROL,
			   CDC_TX_SWR_CLK_EN_MASK,
			   CDC_TX_SWR_CLK_ENABLE);
	return 0;
}

static void swclk_gate_disable(struct clk_hw *hw)
{
	struct tx_macro *tx = to_tx_macro(hw);
	struct regmap *regmap = tx->regmap;

	regmap_update_bits(regmap, CDC_TX_CLK_RST_CTRL_SWR_CONTROL,
			   CDC_TX_SWR_CLK_EN_MASK, 0x0);

	tx_macro_mclk_enable(tx, false);
	clk_disable_unprepare(tx->mclk);
}

static int swclk_gate_is_enabled(struct clk_hw *hw)
{
	struct tx_macro *tx = to_tx_macro(hw);
	int ret, val;

	regmap_read(tx->regmap, CDC_TX_CLK_RST_CTRL_SWR_CONTROL, &val);
	ret = val & BIT(0);

	return ret;
}

static unsigned long swclk_recalc_rate(struct clk_hw *hw,
				       unsigned long parent_rate)
{
	return parent_rate / 2;
}

static const struct clk_ops swclk_gate_ops = {
	.prepare = swclk_gate_enable,
	.unprepare = swclk_gate_disable,
	.is_enabled = swclk_gate_is_enabled,
	.recalc_rate = swclk_recalc_rate,

};

static int tx_macro_register_mclk_output(struct tx_macro *tx)
{
	struct device *dev = tx->dev;
	const char *parent_clk_name = NULL;
	const char *clk_name = "lpass-tx-mclk";
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	if (tx->npl)
		parent_clk_name = __clk_get_name(tx->npl);
	else
		parent_clk_name = __clk_get_name(tx->mclk);

	init.name = clk_name;
	init.ops = &swclk_gate_ops;
	init.flags = 0;
	init.parent_names = &parent_clk_name;
	init.num_parents = 1;
	tx->hw.init = &init;
	hw = &tx->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
}

static const struct snd_soc_component_driver tx_macro_component_drv = {
	.name = "RX-MACRO",
	.probe = tx_macro_component_probe,
	.controls = tx_macro_snd_controls,
	.num_controls = ARRAY_SIZE(tx_macro_snd_controls),
	.dapm_widgets = tx_macro_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tx_macro_dapm_widgets),
	.dapm_routes = tx_audio_map,
	.num_dapm_routes = ARRAY_SIZE(tx_audio_map),
};

static int tx_macro_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct tx_macro *tx;
	void __iomem *base;
	int ret, reg;

	tx = devm_kzalloc(dev, sizeof(*tx), GFP_KERNEL);
	if (!tx)
		return -ENOMEM;

	tx->data = device_get_match_data(dev);

	tx->macro = devm_clk_get_optional(dev, "macro");
	if (IS_ERR(tx->macro))
		return dev_err_probe(dev, PTR_ERR(tx->macro), "unable to get macro clock\n");

	tx->dcodec = devm_clk_get_optional(dev, "dcodec");
	if (IS_ERR(tx->dcodec))
		return dev_err_probe(dev, PTR_ERR(tx->dcodec), "unable to get dcodec clock\n");

	tx->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(tx->mclk))
		return dev_err_probe(dev, PTR_ERR(tx->mclk), "unable to get mclk clock\n");

	if (tx->data->flags & LPASS_MACRO_FLAG_HAS_NPL_CLOCK) {
		tx->npl = devm_clk_get(dev, "npl");
		if (IS_ERR(tx->npl))
			return dev_err_probe(dev, PTR_ERR(tx->npl), "unable to get npl clock\n");
	}

	tx->fsgen = devm_clk_get(dev, "fsgen");
	if (IS_ERR(tx->fsgen))
		return dev_err_probe(dev, PTR_ERR(tx->fsgen), "unable to get fsgen clock\n");

	tx->pds = lpass_macro_pds_init(dev);
	if (IS_ERR(tx->pds))
		return PTR_ERR(tx->pds);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto err;
	}

	/* Update defaults for lpass sc7280 */
	if (of_device_is_compatible(np, "qcom,sc7280-lpass-tx-macro")) {
		for (reg = 0; reg < ARRAY_SIZE(tx_defaults); reg++) {
			switch (tx_defaults[reg].reg) {
			case CDC_TX_TOP_CSR_SWR_AMIC0_CTL:
			case CDC_TX_TOP_CSR_SWR_AMIC1_CTL:
				tx_defaults[reg].def = 0x0E;
				break;
			default:
				break;
			}
		}
	}

	tx->regmap = devm_regmap_init_mmio(dev, base, &tx_regmap_config);
	if (IS_ERR(tx->regmap)) {
		ret = PTR_ERR(tx->regmap);
		goto err;
	}

	dev_set_drvdata(dev, tx);

	tx->dev = dev;

	/* Set active_decimator default value */
	tx->active_decimator[TX_MACRO_AIF1_CAP] = -1;
	tx->active_decimator[TX_MACRO_AIF2_CAP] = -1;
	tx->active_decimator[TX_MACRO_AIF3_CAP] = -1;

	/* set MCLK and NPL rates */
	clk_set_rate(tx->mclk, MCLK_FREQ);
	clk_set_rate(tx->npl, MCLK_FREQ);

	ret = clk_prepare_enable(tx->macro);
	if (ret)
		goto err;

	ret = clk_prepare_enable(tx->dcodec);
	if (ret)
		goto err_dcodec;

	ret = clk_prepare_enable(tx->mclk);
	if (ret)
		goto err_mclk;

	ret = clk_prepare_enable(tx->npl);
	if (ret)
		goto err_npl;

	ret = clk_prepare_enable(tx->fsgen);
	if (ret)
		goto err_fsgen;


	/* reset soundwire block */
	if (tx->data->flags & LPASS_MACRO_FLAG_RESET_SWR)
		regmap_update_bits(tx->regmap, CDC_TX_CLK_RST_CTRL_SWR_CONTROL,
				   CDC_TX_SWR_RESET_MASK, CDC_TX_SWR_RESET_ENABLE);

	regmap_update_bits(tx->regmap, CDC_TX_CLK_RST_CTRL_SWR_CONTROL,
			   CDC_TX_SWR_CLK_EN_MASK,
			   CDC_TX_SWR_CLK_ENABLE);

	if (tx->data->flags & LPASS_MACRO_FLAG_RESET_SWR)
		regmap_update_bits(tx->regmap, CDC_TX_CLK_RST_CTRL_SWR_CONTROL,
				   CDC_TX_SWR_RESET_MASK, 0x0);

	ret = devm_snd_soc_register_component(dev, &tx_macro_component_drv,
					      tx_macro_dai,
					      ARRAY_SIZE(tx_macro_dai));
	if (ret)
		goto err_clkout;

	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = tx_macro_register_mclk_output(tx);
	if (ret)
		goto err_clkout;

	return 0;

err_clkout:
	clk_disable_unprepare(tx->fsgen);
err_fsgen:
	clk_disable_unprepare(tx->npl);
err_npl:
	clk_disable_unprepare(tx->mclk);
err_mclk:
	clk_disable_unprepare(tx->dcodec);
err_dcodec:
	clk_disable_unprepare(tx->macro);
err:
	lpass_macro_pds_exit(tx->pds);

	return ret;
}

static void tx_macro_remove(struct platform_device *pdev)
{
	struct tx_macro *tx = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(tx->macro);
	clk_disable_unprepare(tx->dcodec);
	clk_disable_unprepare(tx->mclk);
	clk_disable_unprepare(tx->npl);
	clk_disable_unprepare(tx->fsgen);

	lpass_macro_pds_exit(tx->pds);
}

static int __maybe_unused tx_macro_runtime_suspend(struct device *dev)
{
	struct tx_macro *tx = dev_get_drvdata(dev);

	regcache_cache_only(tx->regmap, true);
	regcache_mark_dirty(tx->regmap);

	clk_disable_unprepare(tx->fsgen);
	clk_disable_unprepare(tx->npl);
	clk_disable_unprepare(tx->mclk);

	return 0;
}

static int __maybe_unused tx_macro_runtime_resume(struct device *dev)
{
	struct tx_macro *tx = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(tx->mclk);
	if (ret) {
		dev_err(dev, "unable to prepare mclk\n");
		return ret;
	}

	ret = clk_prepare_enable(tx->npl);
	if (ret) {
		dev_err(dev, "unable to prepare npl\n");
		goto err_npl;
	}

	ret = clk_prepare_enable(tx->fsgen);
	if (ret) {
		dev_err(dev, "unable to prepare fsgen\n");
		goto err_fsgen;
	}

	regcache_cache_only(tx->regmap, false);
	regcache_sync(tx->regmap);

	return 0;
err_fsgen:
	clk_disable_unprepare(tx->npl);
err_npl:
	clk_disable_unprepare(tx->mclk);

	return ret;
}

static const struct dev_pm_ops tx_macro_pm_ops = {
	SET_RUNTIME_PM_OPS(tx_macro_runtime_suspend, tx_macro_runtime_resume, NULL)
};

static const struct tx_macro_data lpass_ver_9 = {
	.flags			= LPASS_MACRO_FLAG_HAS_NPL_CLOCK |
				  LPASS_MACRO_FLAG_RESET_SWR,
	.ver			= LPASS_VER_9_0_0,
	.extra_widgets		= tx_macro_dapm_widgets_v9,
	.extra_widgets_num	= ARRAY_SIZE(tx_macro_dapm_widgets_v9),
	.extra_routes		= tx_audio_map_v9,
	.extra_routes_num	= ARRAY_SIZE(tx_audio_map_v9),
};

static const struct tx_macro_data lpass_ver_9_2 = {
	.flags			= LPASS_MACRO_FLAG_HAS_NPL_CLOCK |
				  LPASS_MACRO_FLAG_RESET_SWR,
	.ver			= LPASS_VER_9_2_0,
	.extra_widgets		= tx_macro_dapm_widgets_v9_2,
	.extra_widgets_num	= ARRAY_SIZE(tx_macro_dapm_widgets_v9_2),
	.extra_routes		= tx_audio_map_v9_2,
	.extra_routes_num	= ARRAY_SIZE(tx_audio_map_v9_2),
};

static const struct tx_macro_data lpass_ver_10_sm6115 = {
	.flags			= LPASS_MACRO_FLAG_HAS_NPL_CLOCK,
	.ver			= LPASS_VER_10_0_0,
	.extra_widgets		= tx_macro_dapm_widgets_v9_2,
	.extra_widgets_num	= ARRAY_SIZE(tx_macro_dapm_widgets_v9_2),
	.extra_routes		= tx_audio_map_v9_2,
	.extra_routes_num	= ARRAY_SIZE(tx_audio_map_v9_2),
};

static const struct tx_macro_data lpass_ver_11 = {
	.flags			= LPASS_MACRO_FLAG_RESET_SWR,
	.ver			= LPASS_VER_11_0_0,
	.extra_widgets		= tx_macro_dapm_widgets_v9_2,
	.extra_widgets_num	= ARRAY_SIZE(tx_macro_dapm_widgets_v9_2),
	.extra_routes		= tx_audio_map_v9_2,
	.extra_routes_num	= ARRAY_SIZE(tx_audio_map_v9_2),
};

static const struct of_device_id tx_macro_dt_match[] = {
	{
		/*
		 * The block is actually LPASS v9.4, but keep LPASS v9 match
		 * data and audio widgets, due to compatibility reasons.
		 * Microphones are working on SC7280 fine, so apparently the fix
		 * is not necessary.
		 */
		.compatible = "qcom,sc7280-lpass-tx-macro",
		.data = &lpass_ver_9,
	}, {
		.compatible = "qcom,sm6115-lpass-tx-macro",
		.data = &lpass_ver_10_sm6115,
	}, {
		.compatible = "qcom,sm8250-lpass-tx-macro",
		.data = &lpass_ver_9,
	}, {
		.compatible = "qcom,sm8450-lpass-tx-macro",
		.data = &lpass_ver_9_2,
	}, {
		.compatible = "qcom,sm8550-lpass-tx-macro",
		.data = &lpass_ver_11,
	}, {
		.compatible = "qcom,sc8280xp-lpass-tx-macro",
		/*
		 * The block is actually LPASS v9.3, but keep LPASS v9 match
		 * data and audio widgets, due to compatibility reasons.
		 * Microphones are working on SC8280xp fine, so apparently the
		 * fix is not necessary.
		 */
		.data = &lpass_ver_9,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, tx_macro_dt_match);
static struct platform_driver tx_macro_driver = {
	.driver = {
		.name = "tx_macro",
		.of_match_table = tx_macro_dt_match,
		.suppress_bind_attrs = true,
		.pm = &tx_macro_pm_ops,
	},
	.probe = tx_macro_probe,
	.remove = tx_macro_remove,
};

module_platform_driver(tx_macro_driver);

MODULE_DESCRIPTION("TX macro driver");
MODULE_LICENSE("GPL");
