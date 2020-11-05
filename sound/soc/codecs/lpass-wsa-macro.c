// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_clk.h>
#include <linux/clk-provider.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/of_platform.h>
#include <sound/tlv.h>
#include "lpass-wsa-macro.h"

#define CDC_WSA_CLK_RST_CTRL_MCLK_CONTROL	(0x0000)
#define CDC_WSA_MCLK_EN_MASK			BIT(0)
#define CDC_WSA_MCLK_ENABLE			BIT(0)
#define CDC_WSA_MCLK_DISABLE			0
#define CDC_WSA_CLK_RST_CTRL_FS_CNT_CONTROL	(0x0004)
#define CDC_WSA_FS_CNT_EN_MASK			BIT(0)
#define CDC_WSA_FS_CNT_ENABLE			BIT(0)
#define CDC_WSA_FS_CNT_DISABLE			0
#define CDC_WSA_CLK_RST_CTRL_SWR_CONTROL	(0x0008)
#define CDC_WSA_SWR_CLK_EN_MASK			BIT(0)
#define CDC_WSA_SWR_CLK_ENABLE			BIT(0)
#define CDC_WSA_SWR_RST_EN_MASK			BIT(1)
#define CDC_WSA_SWR_RST_ENABLE			BIT(1)
#define CDC_WSA_SWR_RST_DISABLE			0
#define CDC_WSA_TOP_TOP_CFG0			(0x0080)
#define CDC_WSA_TOP_TOP_CFG1			(0x0084)
#define CDC_WSA_TOP_FREQ_MCLK			(0x0088)
#define CDC_WSA_TOP_DEBUG_BUS_SEL		(0x008C)
#define CDC_WSA_TOP_DEBUG_EN0			(0x0090)
#define CDC_WSA_TOP_DEBUG_EN1			(0x0094)
#define CDC_WSA_TOP_DEBUG_DSM_LB		(0x0098)
#define CDC_WSA_TOP_RX_I2S_CTL			(0x009C)
#define CDC_WSA_TOP_TX_I2S_CTL			(0x00A0)
#define CDC_WSA_TOP_I2S_CLK			(0x00A4)
#define CDC_WSA_TOP_I2S_RESET			(0x00A8)
#define CDC_WSA_RX_INP_MUX_RX_INT0_CFG0		(0x0100)
#define CDC_WSA_RX_INTX_1_MIX_INP2_SEL_MASK	GENMASK(5, 3)
#define CDC_WSA_RX_INTX_2_SEL_MASK		GENMASK(2, 0)
#define CDC_WSA_RX_INP_MUX_RX_INT0_CFG1		(0x0104)
#define CDC_WSA_RX_INP_MUX_RX_INT1_CFG0		(0x0108)
#define CDC_WSA_RX_INP_MUX_RX_INT1_CFG1		(0x010C)
#define CDC_WSA_RX_INP_MUX_RX_MIX_CFG0		(0x0110)
#define CDC_WSA_RX_MIX_TX1_SEL_MASK		GENMASK(5, 3)
#define CDC_WSA_RX_MIX_TX1_SEL_SHFT		3
#define CDC_WSA_RX_MIX_TX0_SEL_MASK		GENMASK(2, 0)
#define CDC_WSA_RX_INP_MUX_RX_EC_CFG0		(0x0114)
#define CDC_WSA_RX_INP_MUX_SOFTCLIP_CFG0	(0x0118)
#define CDC_WSA_TX0_SPKR_PROT_PATH_CTL		(0x0244)
#define CDC_WSA_TX_SPKR_PROT_RESET_MASK		BIT(5)
#define CDC_WSA_TX_SPKR_PROT_RESET		BIT(5)
#define CDC_WSA_TX_SPKR_PROT_NO_RESET		0
#define CDC_WSA_TX_SPKR_PROT_CLK_EN_MASK	BIT(4)
#define CDC_WSA_TX_SPKR_PROT_CLK_ENABLE		BIT(4)
#define CDC_WSA_TX_SPKR_PROT_CLK_DISABLE	0
#define CDC_WSA_TX_SPKR_PROT_PCM_RATE_MASK	GENMASK(3, 0)
#define CDC_WSA_TX_SPKR_PROT_PCM_RATE_8K	0
#define CDC_WSA_TX0_SPKR_PROT_PATH_CFG0		(0x0248)
#define CDC_WSA_TX1_SPKR_PROT_PATH_CTL		(0x0264)
#define CDC_WSA_TX1_SPKR_PROT_PATH_CFG0		(0x0268)
#define CDC_WSA_TX2_SPKR_PROT_PATH_CTL		(0x0284)
#define CDC_WSA_TX2_SPKR_PROT_PATH_CFG0		(0x0288)
#define CDC_WSA_TX3_SPKR_PROT_PATH_CTL		(0x02A4)
#define CDC_WSA_TX3_SPKR_PROT_PATH_CFG0		(0x02A8)
#define CDC_WSA_INTR_CTRL_CFG			(0x0340)
#define CDC_WSA_INTR_CTRL_CLR_COMMIT		(0x0344)
#define CDC_WSA_INTR_CTRL_PIN1_MASK0		(0x0360)
#define CDC_WSA_INTR_CTRL_PIN1_STATUS0		(0x0368)
#define CDC_WSA_INTR_CTRL_PIN1_CLEAR0		(0x0370)
#define CDC_WSA_INTR_CTRL_PIN2_MASK0		(0x0380)
#define CDC_WSA_INTR_CTRL_PIN2_STATUS0		(0x0388)
#define CDC_WSA_INTR_CTRL_PIN2_CLEAR0		(0x0390)
#define CDC_WSA_INTR_CTRL_LEVEL0		(0x03C0)
#define CDC_WSA_INTR_CTRL_BYPASS0		(0x03C8)
#define CDC_WSA_INTR_CTRL_SET0			(0x03D0)
#define CDC_WSA_RX0_RX_PATH_CTL			(0x0400)
#define CDC_WSA_RX_PATH_CLK_EN_MASK		BIT(5)
#define CDC_WSA_RX_PATH_CLK_ENABLE		BIT(5)
#define CDC_WSA_RX_PATH_CLK_DISABLE		0
#define CDC_WSA_RX_PATH_PGA_MUTE_EN_MASK	BIT(4)
#define CDC_WSA_RX_PATH_PGA_MUTE_ENABLE		BIT(4)
#define CDC_WSA_RX_PATH_PGA_MUTE_DISABLE	0
#define CDC_WSA_RX0_RX_PATH_CFG0		(0x0404)
#define CDC_WSA_RX_PATH_COMP_EN_MASK		BIT(1)
#define CDC_WSA_RX_PATH_COMP_ENABLE		BIT(1)
#define CDC_WSA_RX_PATH_HD2_EN_MASK		BIT(2)
#define CDC_WSA_RX_PATH_HD2_ENABLE		BIT(2)
#define CDC_WSA_RX_PATH_SPKR_RATE_MASK		BIT(3)
#define CDC_WSA_RX_PATH_SPKR_RATE_FS_2P4_3P072	BIT(3)
#define CDC_WSA_RX0_RX_PATH_CFG1		(0x0408)
#define CDC_WSA_RX_PATH_SMART_BST_EN_MASK	BIT(0)
#define CDC_WSA_RX_PATH_SMART_BST_ENABLE	BIT(0)
#define CDC_WSA_RX_PATH_SMART_BST_DISABLE	0
#define CDC_WSA_RX0_RX_PATH_CFG2		(0x040C)
#define CDC_WSA_RX0_RX_PATH_CFG3		(0x0410)
#define CDC_WSA_RX_DC_DCOEFF_MASK		GENMASK(1, 0)
#define CDC_WSA_RX0_RX_VOL_CTL			(0x0414)
#define CDC_WSA_RX0_RX_PATH_MIX_CTL		(0x0418)
#define CDC_WSA_RX_PATH_MIX_CLK_EN_MASK		BIT(5)
#define CDC_WSA_RX_PATH_MIX_CLK_ENABLE		BIT(5)
#define CDC_WSA_RX_PATH_MIX_CLK_DISABLE		0
#define CDC_WSA_RX0_RX_PATH_MIX_CFG		(0x041C)
#define CDC_WSA_RX0_RX_VOL_MIX_CTL		(0x0420)
#define CDC_WSA_RX0_RX_PATH_SEC0		(0x0424)
#define CDC_WSA_RX0_RX_PATH_SEC1		(0x0428)
#define CDC_WSA_RX_PGA_HALF_DB_MASK		BIT(0)
#define CDC_WSA_RX_PGA_HALF_DB_ENABLE		BIT(0)
#define CDC_WSA_RX_PGA_HALF_DB_DISABLE		0
#define CDC_WSA_RX0_RX_PATH_SEC2		(0x042C)
#define CDC_WSA_RX0_RX_PATH_SEC3		(0x0430)
#define CDC_WSA_RX_PATH_HD2_SCALE_MASK		GENMASK(1, 0)
#define CDC_WSA_RX_PATH_HD2_ALPHA_MASK		GENMASK(5, 2)
#define CDC_WSA_RX0_RX_PATH_SEC5		(0x0438)
#define CDC_WSA_RX0_RX_PATH_SEC6		(0x043C)
#define CDC_WSA_RX0_RX_PATH_SEC7		(0x0440)
#define CDC_WSA_RX0_RX_PATH_MIX_SEC0		(0x0444)
#define CDC_WSA_RX0_RX_PATH_MIX_SEC1		(0x0448)
#define CDC_WSA_RX0_RX_PATH_DSMDEM_CTL		(0x044C)
#define CDC_WSA_RX_DSMDEM_CLK_EN_MASK		BIT(0)
#define CDC_WSA_RX_DSMDEM_CLK_ENABLE		BIT(0)
#define CDC_WSA_RX1_RX_PATH_CTL			(0x0480)
#define CDC_WSA_RX1_RX_PATH_CFG0		(0x0484)
#define CDC_WSA_RX1_RX_PATH_CFG1		(0x0488)
#define CDC_WSA_RX1_RX_PATH_CFG2		(0x048C)
#define CDC_WSA_RX1_RX_PATH_CFG3		(0x0490)
#define CDC_WSA_RX1_RX_VOL_CTL			(0x0494)
#define CDC_WSA_RX1_RX_PATH_MIX_CTL		(0x0498)
#define CDC_WSA_RX1_RX_PATH_MIX_CFG		(0x049C)
#define CDC_WSA_RX1_RX_VOL_MIX_CTL		(0x04A0)
#define CDC_WSA_RX1_RX_PATH_SEC0		(0x04A4)
#define CDC_WSA_RX1_RX_PATH_SEC1		(0x04A8)
#define CDC_WSA_RX1_RX_PATH_SEC2		(0x04AC)
#define CDC_WSA_RX1_RX_PATH_SEC3		(0x04B0)
#define CDC_WSA_RX1_RX_PATH_SEC5		(0x04B8)
#define CDC_WSA_RX1_RX_PATH_SEC6		(0x04BC)
#define CDC_WSA_RX1_RX_PATH_SEC7		(0x04C0)
#define CDC_WSA_RX1_RX_PATH_MIX_SEC0		(0x04C4)
#define CDC_WSA_RX1_RX_PATH_MIX_SEC1		(0x04C8)
#define CDC_WSA_RX1_RX_PATH_DSMDEM_CTL		(0x04CC)
#define CDC_WSA_BOOST0_BOOST_PATH_CTL		(0x0500)
#define CDC_WSA_BOOST_PATH_CLK_EN_MASK		BIT(4)
#define CDC_WSA_BOOST_PATH_CLK_ENABLE		BIT(4)
#define CDC_WSA_BOOST_PATH_CLK_DISABLE		0
#define CDC_WSA_BOOST0_BOOST_CTL		(0x0504)
#define CDC_WSA_BOOST0_BOOST_CFG1		(0x0508)
#define CDC_WSA_BOOST0_BOOST_CFG2		(0x050C)
#define CDC_WSA_BOOST1_BOOST_PATH_CTL		(0x0540)
#define CDC_WSA_BOOST1_BOOST_CTL		(0x0544)
#define CDC_WSA_BOOST1_BOOST_CFG1		(0x0548)
#define CDC_WSA_BOOST1_BOOST_CFG2		(0x054C)
#define CDC_WSA_COMPANDER0_CTL0			(0x0580)
#define CDC_WSA_COMPANDER_CLK_EN_MASK		BIT(0)
#define CDC_WSA_COMPANDER_CLK_ENABLE		BIT(0)
#define CDC_WSA_COMPANDER_SOFT_RST_MASK		BIT(1)
#define CDC_WSA_COMPANDER_SOFT_RST_ENABLE	BIT(1)
#define CDC_WSA_COMPANDER_HALT_MASK		BIT(2)
#define CDC_WSA_COMPANDER_HALT			BIT(2)
#define CDC_WSA_COMPANDER0_CTL1			(0x0584)
#define CDC_WSA_COMPANDER0_CTL2			(0x0588)
#define CDC_WSA_COMPANDER0_CTL3			(0x058C)
#define CDC_WSA_COMPANDER0_CTL4			(0x0590)
#define CDC_WSA_COMPANDER0_CTL5			(0x0594)
#define CDC_WSA_COMPANDER0_CTL6			(0x0598)
#define CDC_WSA_COMPANDER0_CTL7			(0x059C)
#define CDC_WSA_COMPANDER1_CTL0			(0x05C0)
#define CDC_WSA_COMPANDER1_CTL1			(0x05C4)
#define CDC_WSA_COMPANDER1_CTL2			(0x05C8)
#define CDC_WSA_COMPANDER1_CTL3			(0x05CC)
#define CDC_WSA_COMPANDER1_CTL4			(0x05D0)
#define CDC_WSA_COMPANDER1_CTL5			(0x05D4)
#define CDC_WSA_COMPANDER1_CTL6			(0x05D8)
#define CDC_WSA_COMPANDER1_CTL7			(0x05DC)
#define CDC_WSA_SOFTCLIP0_CRC			(0x0600)
#define CDC_WSA_SOFTCLIP_CLK_EN_MASK		BIT(0)
#define CDC_WSA_SOFTCLIP_CLK_ENABLE		BIT(0)
#define CDC_WSA_SOFTCLIP0_SOFTCLIP_CTRL		(0x0604)
#define CDC_WSA_SOFTCLIP_EN_MASK		BIT(0)
#define CDC_WSA_SOFTCLIP_ENABLE			BIT(0)
#define CDC_WSA_SOFTCLIP1_CRC			(0x0640)
#define CDC_WSA_SOFTCLIP1_SOFTCLIP_CTRL		(0x0644)
#define CDC_WSA_EC_HQ0_EC_REF_HQ_PATH_CTL	(0x0680)
#define CDC_WSA_EC_HQ_EC_CLK_EN_MASK		BIT(0)
#define CDC_WSA_EC_HQ_EC_CLK_ENABLE		BIT(0)
#define CDC_WSA_EC_HQ0_EC_REF_HQ_CFG0		(0x0684)
#define CDC_WSA_EC_HQ_EC_REF_PCM_RATE_MASK	GENMASK(4, 1)
#define CDC_WSA_EC_HQ_EC_REF_PCM_RATE_48K	BIT(3)
#define CDC_WSA_EC_HQ1_EC_REF_HQ_PATH_CTL	(0x06C0)
#define CDC_WSA_EC_HQ1_EC_REF_HQ_CFG0		(0x06C4)
#define CDC_WSA_SPLINE_ASRC0_CLK_RST_CTL	(0x0700)
#define CDC_WSA_SPLINE_ASRC0_CTL0		(0x0704)
#define CDC_WSA_SPLINE_ASRC0_CTL1		(0x0708)
#define CDC_WSA_SPLINE_ASRC0_FIFO_CTL		(0x070C)
#define CDC_WSA_SPLINE_ASRC0_STATUS_FMIN_CNTR_LSB	(0x0710)
#define CDC_WSA_SPLINE_ASRC0_STATUS_FMIN_CNTR_MSB	(0x0714)
#define CDC_WSA_SPLINE_ASRC0_STATUS_FMAX_CNTR_LSB	(0x0718)
#define CDC_WSA_SPLINE_ASRC0_STATUS_FMAX_CNTR_MSB	(0x071C)
#define CDC_WSA_SPLINE_ASRC0_STATUS_FIFO		(0x0720)
#define CDC_WSA_SPLINE_ASRC1_CLK_RST_CTL		(0x0740)
#define CDC_WSA_SPLINE_ASRC1_CTL0		(0x0744)
#define CDC_WSA_SPLINE_ASRC1_CTL1		(0x0748)
#define CDC_WSA_SPLINE_ASRC1_FIFO_CTL		(0x074C)
#define CDC_WSA_SPLINE_ASRC1_STATUS_FMIN_CNTR_LSB (0x0750)
#define CDC_WSA_SPLINE_ASRC1_STATUS_FMIN_CNTR_MSB (0x0754)
#define CDC_WSA_SPLINE_ASRC1_STATUS_FMAX_CNTR_LSB (0x0758)
#define CDC_WSA_SPLINE_ASRC1_STATUS_FMAX_CNTR_MSB (0x075C)
#define CDC_WSA_SPLINE_ASRC1_STATUS_FIFO	(0x0760)
#define WSA_MAX_OFFSET				(0x0760)

#define WSA_MACRO_RX_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define WSA_MACRO_RX_MIX_RATES (SNDRV_PCM_RATE_48000 |\
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define WSA_MACRO_RX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
		SNDRV_PCM_FMTBIT_S24_LE |\
		SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

#define WSA_MACRO_ECHO_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_48000)
#define WSA_MACRO_ECHO_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
		SNDRV_PCM_FMTBIT_S24_LE |\
		SNDRV_PCM_FMTBIT_S24_3LE)

#define NUM_INTERPOLATORS 2
#define WSA_NUM_CLKS_MAX	5
#define WSA_MACRO_MCLK_FREQ 19200000
#define WSA_MACRO_MUX_INP_SHFT 0x3
#define WSA_MACRO_MUX_INP_MASK1 0x07
#define WSA_MACRO_MUX_INP_MASK2 0x38
#define WSA_MACRO_MUX_CFG_OFFSET 0x8
#define WSA_MACRO_MUX_CFG1_OFFSET 0x4
#define WSA_MACRO_RX_COMP_OFFSET 0x40
#define WSA_MACRO_RX_SOFTCLIP_OFFSET 0x40
#define WSA_MACRO_RX_PATH_OFFSET 0x80
#define WSA_MACRO_RX_PATH_CFG3_OFFSET 0x10
#define WSA_MACRO_RX_PATH_DSMDEM_OFFSET 0x4C
#define WSA_MACRO_FS_RATE_MASK 0x0F
#define WSA_MACRO_EC_MIX_TX0_MASK 0x03
#define WSA_MACRO_EC_MIX_TX1_MASK 0x18
#define WSA_MACRO_MAX_DMA_CH_PER_PORT 0x2

enum {
	WSA_MACRO_GAIN_OFFSET_M1P5_DB,
	WSA_MACRO_GAIN_OFFSET_0_DB,
};
enum {
	WSA_MACRO_RX0 = 0,
	WSA_MACRO_RX1,
	WSA_MACRO_RX_MIX,
	WSA_MACRO_RX_MIX0 = WSA_MACRO_RX_MIX,
	WSA_MACRO_RX_MIX1,
	WSA_MACRO_RX_MAX,
};

enum {
	WSA_MACRO_TX0 = 0,
	WSA_MACRO_TX1,
	WSA_MACRO_TX_MAX,
};

enum {
	WSA_MACRO_EC0_MUX = 0,
	WSA_MACRO_EC1_MUX,
	WSA_MACRO_EC_MUX_MAX,
};

enum {
	WSA_MACRO_COMP1, /* SPK_L */
	WSA_MACRO_COMP2, /* SPK_R */
	WSA_MACRO_COMP_MAX
};

enum {
	WSA_MACRO_SOFTCLIP0, /* RX0 */
	WSA_MACRO_SOFTCLIP1, /* RX1 */
	WSA_MACRO_SOFTCLIP_MAX
};

enum {
	INTn_1_INP_SEL_ZERO = 0,
	INTn_1_INP_SEL_RX0,
	INTn_1_INP_SEL_RX1,
	INTn_1_INP_SEL_RX2,
	INTn_1_INP_SEL_RX3,
	INTn_1_INP_SEL_DEC0,
	INTn_1_INP_SEL_DEC1,
};

enum {
	INTn_2_INP_SEL_ZERO = 0,
	INTn_2_INP_SEL_RX0,
	INTn_2_INP_SEL_RX1,
	INTn_2_INP_SEL_RX2,
	INTn_2_INP_SEL_RX3,
};

struct interp_sample_rate {
	int sample_rate;
	int rate_val;
};

static struct interp_sample_rate int_prim_sample_rate_val[] = {
	{8000, 0x0},	/* 8K */
	{16000, 0x1},	/* 16K */
	{24000, -EINVAL},/* 24K */
	{32000, 0x3},	/* 32K */
	{48000, 0x4},	/* 48K */
	{96000, 0x5},	/* 96K */
	{192000, 0x6},	/* 192K */
	{384000, 0x7},	/* 384K */
	{44100, 0x8}, /* 44.1K */
};

static struct interp_sample_rate int_mix_sample_rate_val[] = {
	{48000, 0x4},	/* 48K */
	{96000, 0x5},	/* 96K */
	{192000, 0x6},	/* 192K */
};

enum {
	WSA_MACRO_AIF_INVALID = 0,
	WSA_MACRO_AIF1_PB,
	WSA_MACRO_AIF_MIX1_PB,
	WSA_MACRO_AIF_VI,
	WSA_MACRO_AIF_ECHO,
	WSA_MACRO_MAX_DAIS,
};

struct wsa_macro {
	struct device *dev;
	int comp_enabled[WSA_MACRO_COMP_MAX];
	int ec_hq[WSA_MACRO_RX1 + 1];
	u16 prim_int_users[WSA_MACRO_RX1 + 1];
	u16 wsa_mclk_users;
	bool reset_swr;
	unsigned long active_ch_mask[WSA_MACRO_MAX_DAIS];
	unsigned long active_ch_cnt[WSA_MACRO_MAX_DAIS];
	int rx_port_value[WSA_MACRO_RX_MAX];
	int ear_spkr_gain;
	int spkr_gain_offset;
	int spkr_mode;
	int is_softclip_on[WSA_MACRO_SOFTCLIP_MAX];
	int softclip_clk_users[WSA_MACRO_SOFTCLIP_MAX];
	struct regmap *regmap;
	struct clk_bulk_data clks[WSA_NUM_CLKS_MAX];
	struct clk_hw hw;
};
#define to_wsa_macro(_hw) container_of(_hw, struct wsa_macro, hw)

static const DECLARE_TLV_DB_SCALE(digital_gain, -8400, 100, -8400);

static const char * const wsa_macro_ear_spkr_pa_gain_text[] = {
	"G_DEFAULT", "G_0_DB", "G_1_DB", "G_2_DB", "G_3_DB",
	"G_4_DB", "G_5_DB", "G_6_DB"
};

static SOC_ENUM_SINGLE_EXT_DECL(wsa_macro_ear_spkr_pa_gain_enum,
				wsa_macro_ear_spkr_pa_gain_text);

static const struct reg_default wsa_defaults[] = {
	/* WSA Macro */
	{ CDC_WSA_CLK_RST_CTRL_MCLK_CONTROL, 0x00},
	{ CDC_WSA_CLK_RST_CTRL_FS_CNT_CONTROL, 0x00},
	{ CDC_WSA_CLK_RST_CTRL_SWR_CONTROL, 0x00},
	{ CDC_WSA_TOP_TOP_CFG0, 0x00},
	{ CDC_WSA_TOP_TOP_CFG1, 0x00},
	{ CDC_WSA_TOP_FREQ_MCLK, 0x00},
	{ CDC_WSA_TOP_DEBUG_BUS_SEL, 0x00},
	{ CDC_WSA_TOP_DEBUG_EN0, 0x00},
	{ CDC_WSA_TOP_DEBUG_EN1, 0x00},
	{ CDC_WSA_TOP_DEBUG_DSM_LB, 0x88},
	{ CDC_WSA_TOP_RX_I2S_CTL, 0x0C},
	{ CDC_WSA_TOP_TX_I2S_CTL, 0x0C},
	{ CDC_WSA_TOP_I2S_CLK, 0x02},
	{ CDC_WSA_TOP_I2S_RESET, 0x00},
	{ CDC_WSA_RX_INP_MUX_RX_INT0_CFG0, 0x00},
	{ CDC_WSA_RX_INP_MUX_RX_INT0_CFG1, 0x00},
	{ CDC_WSA_RX_INP_MUX_RX_INT1_CFG0, 0x00},
	{ CDC_WSA_RX_INP_MUX_RX_INT1_CFG1, 0x00},
	{ CDC_WSA_RX_INP_MUX_RX_MIX_CFG0, 0x00},
	{ CDC_WSA_RX_INP_MUX_RX_EC_CFG0, 0x00},
	{ CDC_WSA_RX_INP_MUX_SOFTCLIP_CFG0, 0x00},
	{ CDC_WSA_TX0_SPKR_PROT_PATH_CTL, 0x02},
	{ CDC_WSA_TX0_SPKR_PROT_PATH_CFG0, 0x00},
	{ CDC_WSA_TX1_SPKR_PROT_PATH_CTL, 0x02},
	{ CDC_WSA_TX1_SPKR_PROT_PATH_CFG0, 0x00},
	{ CDC_WSA_TX2_SPKR_PROT_PATH_CTL, 0x02},
	{ CDC_WSA_TX2_SPKR_PROT_PATH_CFG0, 0x00},
	{ CDC_WSA_TX3_SPKR_PROT_PATH_CTL, 0x02},
	{ CDC_WSA_TX3_SPKR_PROT_PATH_CFG0, 0x00},
	{ CDC_WSA_INTR_CTRL_CFG, 0x00},
	{ CDC_WSA_INTR_CTRL_CLR_COMMIT, 0x00},
	{ CDC_WSA_INTR_CTRL_PIN1_MASK0, 0xFF},
	{ CDC_WSA_INTR_CTRL_PIN1_STATUS0, 0x00},
	{ CDC_WSA_INTR_CTRL_PIN1_CLEAR0, 0x00},
	{ CDC_WSA_INTR_CTRL_PIN2_MASK0, 0xFF},
	{ CDC_WSA_INTR_CTRL_PIN2_STATUS0, 0x00},
	{ CDC_WSA_INTR_CTRL_PIN2_CLEAR0, 0x00},
	{ CDC_WSA_INTR_CTRL_LEVEL0, 0x00},
	{ CDC_WSA_INTR_CTRL_BYPASS0, 0x00},
	{ CDC_WSA_INTR_CTRL_SET0, 0x00},
	{ CDC_WSA_RX0_RX_PATH_CTL, 0x04},
	{ CDC_WSA_RX0_RX_PATH_CFG0, 0x00},
	{ CDC_WSA_RX0_RX_PATH_CFG1, 0x64},
	{ CDC_WSA_RX0_RX_PATH_CFG2, 0x8F},
	{ CDC_WSA_RX0_RX_PATH_CFG3, 0x00},
	{ CDC_WSA_RX0_RX_VOL_CTL, 0x00},
	{ CDC_WSA_RX0_RX_PATH_MIX_CTL, 0x04},
	{ CDC_WSA_RX0_RX_PATH_MIX_CFG, 0x7E},
	{ CDC_WSA_RX0_RX_VOL_MIX_CTL, 0x00},
	{ CDC_WSA_RX0_RX_PATH_SEC0, 0x04},
	{ CDC_WSA_RX0_RX_PATH_SEC1, 0x08},
	{ CDC_WSA_RX0_RX_PATH_SEC2, 0x00},
	{ CDC_WSA_RX0_RX_PATH_SEC3, 0x00},
	{ CDC_WSA_RX0_RX_PATH_SEC5, 0x00},
	{ CDC_WSA_RX0_RX_PATH_SEC6, 0x00},
	{ CDC_WSA_RX0_RX_PATH_SEC7, 0x00},
	{ CDC_WSA_RX0_RX_PATH_MIX_SEC0, 0x08},
	{ CDC_WSA_RX0_RX_PATH_MIX_SEC1, 0x00},
	{ CDC_WSA_RX0_RX_PATH_DSMDEM_CTL, 0x00},
	{ CDC_WSA_RX1_RX_PATH_CFG0, 0x00},
	{ CDC_WSA_RX1_RX_PATH_CFG1, 0x64},
	{ CDC_WSA_RX1_RX_PATH_CFG2, 0x8F},
	{ CDC_WSA_RX1_RX_PATH_CFG3, 0x00},
	{ CDC_WSA_RX1_RX_VOL_CTL, 0x00},
	{ CDC_WSA_RX1_RX_PATH_MIX_CTL, 0x04},
	{ CDC_WSA_RX1_RX_PATH_MIX_CFG, 0x7E},
	{ CDC_WSA_RX1_RX_VOL_MIX_CTL, 0x00},
	{ CDC_WSA_RX1_RX_PATH_SEC0, 0x04},
	{ CDC_WSA_RX1_RX_PATH_SEC1, 0x08},
	{ CDC_WSA_RX1_RX_PATH_SEC2, 0x00},
	{ CDC_WSA_RX1_RX_PATH_SEC3, 0x00},
	{ CDC_WSA_RX1_RX_PATH_SEC5, 0x00},
	{ CDC_WSA_RX1_RX_PATH_SEC6, 0x00},
	{ CDC_WSA_RX1_RX_PATH_SEC7, 0x00},
	{ CDC_WSA_RX1_RX_PATH_MIX_SEC0, 0x08},
	{ CDC_WSA_RX1_RX_PATH_MIX_SEC1, 0x00},
	{ CDC_WSA_RX1_RX_PATH_DSMDEM_CTL, 0x00},
	{ CDC_WSA_BOOST0_BOOST_PATH_CTL, 0x00},
	{ CDC_WSA_BOOST0_BOOST_CTL, 0xD0},
	{ CDC_WSA_BOOST0_BOOST_CFG1, 0x89},
	{ CDC_WSA_BOOST0_BOOST_CFG2, 0x04},
	{ CDC_WSA_BOOST1_BOOST_PATH_CTL, 0x00},
	{ CDC_WSA_BOOST1_BOOST_CTL, 0xD0},
	{ CDC_WSA_BOOST1_BOOST_CFG1, 0x89},
	{ CDC_WSA_BOOST1_BOOST_CFG2, 0x04},
	{ CDC_WSA_COMPANDER0_CTL0, 0x60},
	{ CDC_WSA_COMPANDER0_CTL1, 0xDB},
	{ CDC_WSA_COMPANDER0_CTL2, 0xFF},
	{ CDC_WSA_COMPANDER0_CTL3, 0x35},
	{ CDC_WSA_COMPANDER0_CTL4, 0xFF},
	{ CDC_WSA_COMPANDER0_CTL5, 0x00},
	{ CDC_WSA_COMPANDER0_CTL6, 0x01},
	{ CDC_WSA_COMPANDER0_CTL7, 0x28},
	{ CDC_WSA_COMPANDER1_CTL0, 0x60},
	{ CDC_WSA_COMPANDER1_CTL1, 0xDB},
	{ CDC_WSA_COMPANDER1_CTL2, 0xFF},
	{ CDC_WSA_COMPANDER1_CTL3, 0x35},
	{ CDC_WSA_COMPANDER1_CTL4, 0xFF},
	{ CDC_WSA_COMPANDER1_CTL5, 0x00},
	{ CDC_WSA_COMPANDER1_CTL6, 0x01},
	{ CDC_WSA_COMPANDER1_CTL7, 0x28},
	{ CDC_WSA_SOFTCLIP0_CRC, 0x00},
	{ CDC_WSA_SOFTCLIP0_SOFTCLIP_CTRL, 0x38},
	{ CDC_WSA_SOFTCLIP1_CRC, 0x00},
	{ CDC_WSA_SOFTCLIP1_SOFTCLIP_CTRL, 0x38},
	{ CDC_WSA_EC_HQ0_EC_REF_HQ_PATH_CTL, 0x00},
	{ CDC_WSA_EC_HQ0_EC_REF_HQ_CFG0, 0x01},
	{ CDC_WSA_EC_HQ1_EC_REF_HQ_PATH_CTL, 0x00},
	{ CDC_WSA_EC_HQ1_EC_REF_HQ_CFG0, 0x01},
	{ CDC_WSA_SPLINE_ASRC0_CLK_RST_CTL, 0x00},
	{ CDC_WSA_SPLINE_ASRC0_CTL0, 0x00},
	{ CDC_WSA_SPLINE_ASRC0_CTL1, 0x00},
	{ CDC_WSA_SPLINE_ASRC0_FIFO_CTL, 0xA8},
	{ CDC_WSA_SPLINE_ASRC0_STATUS_FMIN_CNTR_LSB, 0x00},
	{ CDC_WSA_SPLINE_ASRC0_STATUS_FMIN_CNTR_MSB, 0x00},
	{ CDC_WSA_SPLINE_ASRC0_STATUS_FMAX_CNTR_LSB, 0x00},
	{ CDC_WSA_SPLINE_ASRC0_STATUS_FMAX_CNTR_MSB, 0x00},
	{ CDC_WSA_SPLINE_ASRC0_STATUS_FIFO, 0x00},
	{ CDC_WSA_SPLINE_ASRC1_CLK_RST_CTL, 0x00},
	{ CDC_WSA_SPLINE_ASRC1_CTL0, 0x00},
	{ CDC_WSA_SPLINE_ASRC1_CTL1, 0x00},
	{ CDC_WSA_SPLINE_ASRC1_FIFO_CTL, 0xA8},
	{ CDC_WSA_SPLINE_ASRC1_STATUS_FMIN_CNTR_LSB, 0x00},
	{ CDC_WSA_SPLINE_ASRC1_STATUS_FMIN_CNTR_MSB, 0x00},
	{ CDC_WSA_SPLINE_ASRC1_STATUS_FMAX_CNTR_LSB, 0x00},
	{ CDC_WSA_SPLINE_ASRC1_STATUS_FMAX_CNTR_MSB, 0x00},
	{ CDC_WSA_SPLINE_ASRC1_STATUS_FIFO, 0x00},
};

static bool wsa_is_wronly_register(struct device *dev,
					unsigned int reg)
{
	switch (reg) {
	case CDC_WSA_INTR_CTRL_CLR_COMMIT:
	case CDC_WSA_INTR_CTRL_PIN1_CLEAR0:
	case CDC_WSA_INTR_CTRL_PIN2_CLEAR0:
		return true;
	}

	return false;
}

static bool wsa_is_rw_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CDC_WSA_CLK_RST_CTRL_MCLK_CONTROL:
	case CDC_WSA_CLK_RST_CTRL_FS_CNT_CONTROL:
	case CDC_WSA_CLK_RST_CTRL_SWR_CONTROL:
	case CDC_WSA_TOP_TOP_CFG0:
	case CDC_WSA_TOP_TOP_CFG1:
	case CDC_WSA_TOP_FREQ_MCLK:
	case CDC_WSA_TOP_DEBUG_BUS_SEL:
	case CDC_WSA_TOP_DEBUG_EN0:
	case CDC_WSA_TOP_DEBUG_EN1:
	case CDC_WSA_TOP_DEBUG_DSM_LB:
	case CDC_WSA_TOP_RX_I2S_CTL:
	case CDC_WSA_TOP_TX_I2S_CTL:
	case CDC_WSA_TOP_I2S_CLK:
	case CDC_WSA_TOP_I2S_RESET:
	case CDC_WSA_RX_INP_MUX_RX_INT0_CFG0:
	case CDC_WSA_RX_INP_MUX_RX_INT0_CFG1:
	case CDC_WSA_RX_INP_MUX_RX_INT1_CFG0:
	case CDC_WSA_RX_INP_MUX_RX_INT1_CFG1:
	case CDC_WSA_RX_INP_MUX_RX_MIX_CFG0:
	case CDC_WSA_RX_INP_MUX_RX_EC_CFG0:
	case CDC_WSA_RX_INP_MUX_SOFTCLIP_CFG0:
	case CDC_WSA_TX0_SPKR_PROT_PATH_CTL:
	case CDC_WSA_TX0_SPKR_PROT_PATH_CFG0:
	case CDC_WSA_TX1_SPKR_PROT_PATH_CTL:
	case CDC_WSA_TX1_SPKR_PROT_PATH_CFG0:
	case CDC_WSA_TX2_SPKR_PROT_PATH_CTL:
	case CDC_WSA_TX2_SPKR_PROT_PATH_CFG0:
	case CDC_WSA_TX3_SPKR_PROT_PATH_CTL:
	case CDC_WSA_TX3_SPKR_PROT_PATH_CFG0:
	case CDC_WSA_INTR_CTRL_CFG:
	case CDC_WSA_INTR_CTRL_PIN1_MASK0:
	case CDC_WSA_INTR_CTRL_PIN2_MASK0:
	case CDC_WSA_INTR_CTRL_LEVEL0:
	case CDC_WSA_INTR_CTRL_BYPASS0:
	case CDC_WSA_INTR_CTRL_SET0:
	case CDC_WSA_RX0_RX_PATH_CTL:
	case CDC_WSA_RX0_RX_PATH_CFG0:
	case CDC_WSA_RX0_RX_PATH_CFG1:
	case CDC_WSA_RX0_RX_PATH_CFG2:
	case CDC_WSA_RX0_RX_PATH_CFG3:
	case CDC_WSA_RX0_RX_VOL_CTL:
	case CDC_WSA_RX0_RX_PATH_MIX_CTL:
	case CDC_WSA_RX0_RX_PATH_MIX_CFG:
	case CDC_WSA_RX0_RX_VOL_MIX_CTL:
	case CDC_WSA_RX0_RX_PATH_SEC0:
	case CDC_WSA_RX0_RX_PATH_SEC1:
	case CDC_WSA_RX0_RX_PATH_SEC2:
	case CDC_WSA_RX0_RX_PATH_SEC3:
	case CDC_WSA_RX0_RX_PATH_SEC5:
	case CDC_WSA_RX0_RX_PATH_SEC6:
	case CDC_WSA_RX0_RX_PATH_SEC7:
	case CDC_WSA_RX0_RX_PATH_MIX_SEC0:
	case CDC_WSA_RX0_RX_PATH_MIX_SEC1:
	case CDC_WSA_RX0_RX_PATH_DSMDEM_CTL:
	case CDC_WSA_RX1_RX_PATH_CTL:
	case CDC_WSA_RX1_RX_PATH_CFG0:
	case CDC_WSA_RX1_RX_PATH_CFG1:
	case CDC_WSA_RX1_RX_PATH_CFG2:
	case CDC_WSA_RX1_RX_PATH_CFG3:
	case CDC_WSA_RX1_RX_VOL_CTL:
	case CDC_WSA_RX1_RX_PATH_MIX_CTL:
	case CDC_WSA_RX1_RX_PATH_MIX_CFG:
	case CDC_WSA_RX1_RX_VOL_MIX_CTL:
	case CDC_WSA_RX1_RX_PATH_SEC0:
	case CDC_WSA_RX1_RX_PATH_SEC1:
	case CDC_WSA_RX1_RX_PATH_SEC2:
	case CDC_WSA_RX1_RX_PATH_SEC3:
	case CDC_WSA_RX1_RX_PATH_SEC5:
	case CDC_WSA_RX1_RX_PATH_SEC6:
	case CDC_WSA_RX1_RX_PATH_SEC7:
	case CDC_WSA_RX1_RX_PATH_MIX_SEC0:
	case CDC_WSA_RX1_RX_PATH_MIX_SEC1:
	case CDC_WSA_RX1_RX_PATH_DSMDEM_CTL:
	case CDC_WSA_BOOST0_BOOST_PATH_CTL:
	case CDC_WSA_BOOST0_BOOST_CTL:
	case CDC_WSA_BOOST0_BOOST_CFG1:
	case CDC_WSA_BOOST0_BOOST_CFG2:
	case CDC_WSA_BOOST1_BOOST_PATH_CTL:
	case CDC_WSA_BOOST1_BOOST_CTL:
	case CDC_WSA_BOOST1_BOOST_CFG1:
	case CDC_WSA_BOOST1_BOOST_CFG2:
	case CDC_WSA_COMPANDER0_CTL0:
	case CDC_WSA_COMPANDER0_CTL1:
	case CDC_WSA_COMPANDER0_CTL2:
	case CDC_WSA_COMPANDER0_CTL3:
	case CDC_WSA_COMPANDER0_CTL4:
	case CDC_WSA_COMPANDER0_CTL5:
	case CDC_WSA_COMPANDER0_CTL7:
	case CDC_WSA_COMPANDER1_CTL0:
	case CDC_WSA_COMPANDER1_CTL1:
	case CDC_WSA_COMPANDER1_CTL2:
	case CDC_WSA_COMPANDER1_CTL3:
	case CDC_WSA_COMPANDER1_CTL4:
	case CDC_WSA_COMPANDER1_CTL5:
	case CDC_WSA_COMPANDER1_CTL7:
	case CDC_WSA_SOFTCLIP0_CRC:
	case CDC_WSA_SOFTCLIP0_SOFTCLIP_CTRL:
	case CDC_WSA_SOFTCLIP1_CRC:
	case CDC_WSA_SOFTCLIP1_SOFTCLIP_CTRL:
	case CDC_WSA_EC_HQ0_EC_REF_HQ_PATH_CTL:
	case CDC_WSA_EC_HQ0_EC_REF_HQ_CFG0:
	case CDC_WSA_EC_HQ1_EC_REF_HQ_PATH_CTL:
	case CDC_WSA_EC_HQ1_EC_REF_HQ_CFG0:
	case CDC_WSA_SPLINE_ASRC0_CLK_RST_CTL:
	case CDC_WSA_SPLINE_ASRC0_CTL0:
	case CDC_WSA_SPLINE_ASRC0_CTL1:
	case CDC_WSA_SPLINE_ASRC0_FIFO_CTL:
	case CDC_WSA_SPLINE_ASRC1_CLK_RST_CTL:
	case CDC_WSA_SPLINE_ASRC1_CTL0:
	case CDC_WSA_SPLINE_ASRC1_CTL1:
	case CDC_WSA_SPLINE_ASRC1_FIFO_CTL:
		return true;
	}

	return false;
}

static bool wsa_is_writeable_register(struct device *dev, unsigned int reg)
{
	bool ret;

	ret = wsa_is_rw_register(dev, reg);
	if (!ret)
		return wsa_is_wronly_register(dev, reg);

	return ret;
}

static bool wsa_is_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CDC_WSA_INTR_CTRL_CLR_COMMIT:
	case CDC_WSA_INTR_CTRL_PIN1_CLEAR0:
	case CDC_WSA_INTR_CTRL_PIN2_CLEAR0:
	case CDC_WSA_INTR_CTRL_PIN1_STATUS0:
	case CDC_WSA_INTR_CTRL_PIN2_STATUS0:
	case CDC_WSA_COMPANDER0_CTL6:
	case CDC_WSA_COMPANDER1_CTL6:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FMIN_CNTR_LSB:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FMIN_CNTR_MSB:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FMAX_CNTR_LSB:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FMAX_CNTR_MSB:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FIFO:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FMIN_CNTR_LSB:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FMIN_CNTR_MSB:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FMAX_CNTR_LSB:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FMAX_CNTR_MSB:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FIFO:
		return true;
	}

	return wsa_is_rw_register(dev, reg);
}

static bool wsa_is_volatile_register(struct device *dev, unsigned int reg)
{
	/* Update volatile list for rx/tx macros */
	switch (reg) {
	case CDC_WSA_INTR_CTRL_PIN1_STATUS0:
	case CDC_WSA_INTR_CTRL_PIN2_STATUS0:
	case CDC_WSA_COMPANDER0_CTL6:
	case CDC_WSA_COMPANDER1_CTL6:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FMIN_CNTR_LSB:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FMIN_CNTR_MSB:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FMAX_CNTR_LSB:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FMAX_CNTR_MSB:
	case CDC_WSA_SPLINE_ASRC0_STATUS_FIFO:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FMIN_CNTR_LSB:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FMIN_CNTR_MSB:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FMAX_CNTR_LSB:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FMAX_CNTR_MSB:
	case CDC_WSA_SPLINE_ASRC1_STATUS_FIFO:
		return true;
	}
	return false;
}

static const struct regmap_config wsa_regmap_config = {
	.name = "wsa_macro",
	.reg_bits = 16,
	.val_bits = 32, /* 8 but with 32 bit read/write */
	.reg_stride = 4,
	.cache_type = REGCACHE_FLAT,
	.reg_defaults = wsa_defaults,
	.num_reg_defaults = ARRAY_SIZE(wsa_defaults),
	.max_register = WSA_MAX_OFFSET,
	.writeable_reg = wsa_is_writeable_register,
	.volatile_reg = wsa_is_volatile_register,
	.readable_reg = wsa_is_readable_register,
};

/**
 * wsa_macro_set_spkr_mode - Configures speaker compander and smartboost
 * settings based on speaker mode.
 *
 * @component: codec instance
 * @mode: Indicates speaker configuration mode.
 *
 * Returns 0 on success or -EINVAL on error.
 */
int wsa_macro_set_spkr_mode(struct snd_soc_component *component, int mode)
{
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	wsa->spkr_mode = mode;

	switch (mode) {
	case WSA_MACRO_SPKR_MODE_1:
		snd_soc_component_update_bits(component, CDC_WSA_COMPANDER0_CTL3, 0x80, 0x00);
		snd_soc_component_update_bits(component, CDC_WSA_COMPANDER1_CTL3, 0x80, 0x00);
		snd_soc_component_update_bits(component, CDC_WSA_COMPANDER0_CTL7, 0x01, 0x00);
		snd_soc_component_update_bits(component, CDC_WSA_COMPANDER1_CTL7, 0x01, 0x00);
		snd_soc_component_update_bits(component, CDC_WSA_BOOST0_BOOST_CTL, 0x7C, 0x44);
		snd_soc_component_update_bits(component, CDC_WSA_BOOST1_BOOST_CTL, 0x7C, 0x44);
		break;
	default:
		snd_soc_component_update_bits(component, CDC_WSA_COMPANDER0_CTL3, 0x80, 0x80);
		snd_soc_component_update_bits(component, CDC_WSA_COMPANDER1_CTL3, 0x80, 0x80);
		snd_soc_component_update_bits(component, CDC_WSA_COMPANDER0_CTL7, 0x01, 0x01);
		snd_soc_component_update_bits(component, CDC_WSA_COMPANDER1_CTL7, 0x01, 0x01);
		snd_soc_component_update_bits(component, CDC_WSA_BOOST0_BOOST_CTL, 0x7C, 0x58);
		snd_soc_component_update_bits(component, CDC_WSA_BOOST1_BOOST_CTL, 0x7C, 0x58);
		break;
	}
	return 0;
}
EXPORT_SYMBOL(wsa_macro_set_spkr_mode);

static int wsa_macro_set_prim_interpolator_rate(struct snd_soc_dai *dai,
						u8 int_prim_fs_rate_reg_val,
						u32 sample_rate)
{
	u8 int_1_mix1_inp;
	u32 j, port;
	u16 int_mux_cfg0, int_mux_cfg1;
	u16 int_fs_reg;
	u8 int_mux_cfg0_val, int_mux_cfg1_val;
	u8 inp0_sel, inp1_sel, inp2_sel;
	struct snd_soc_component *component = dai->component;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	for_each_set_bit(port, &wsa->active_ch_mask[dai->id], WSA_MACRO_RX_MAX) {
		int_1_mix1_inp = port;
		if ((int_1_mix1_inp < WSA_MACRO_RX0) || (int_1_mix1_inp > WSA_MACRO_RX_MIX1)) {
			dev_err(component->dev,	"%s: Invalid RX port, Dai ID is %d\n",
				__func__, dai->id);
			return -EINVAL;
		}

		int_mux_cfg0 = CDC_WSA_RX_INP_MUX_RX_INT0_CFG0;

		/*
		 * Loop through all interpolator MUX inputs and find out
		 * to which interpolator input, the cdc_dma rx port
		 * is connected
		 */
		for (j = 0; j < NUM_INTERPOLATORS; j++) {
			int_mux_cfg1 = int_mux_cfg0 + WSA_MACRO_MUX_CFG1_OFFSET;
			int_mux_cfg0_val = snd_soc_component_read(component,
								  int_mux_cfg0);
			int_mux_cfg1_val = snd_soc_component_read(component,
								  int_mux_cfg1);
			inp0_sel = int_mux_cfg0_val & WSA_MACRO_MUX_INP_MASK1;
			inp1_sel = (int_mux_cfg0_val >> WSA_MACRO_MUX_INP_SHFT) &
						WSA_MACRO_MUX_INP_MASK1;
			inp2_sel = (int_mux_cfg1_val >> WSA_MACRO_MUX_INP_SHFT) &
						WSA_MACRO_MUX_INP_MASK1;
			if ((inp0_sel == int_1_mix1_inp + INTn_1_INP_SEL_RX0) ||
			    (inp1_sel == int_1_mix1_inp + INTn_1_INP_SEL_RX0) ||
			    (inp2_sel == int_1_mix1_inp + INTn_1_INP_SEL_RX0)) {
				int_fs_reg = CDC_WSA_RX0_RX_PATH_CTL +
					     WSA_MACRO_RX_PATH_OFFSET * j;
				/* sample_rate is in Hz */
				snd_soc_component_update_bits(component, int_fs_reg,
							      WSA_MACRO_FS_RATE_MASK,
							      int_prim_fs_rate_reg_val);
			}
			int_mux_cfg0 += WSA_MACRO_MUX_CFG_OFFSET;
		}
	}

	return 0;
}

static int wsa_macro_set_mix_interpolator_rate(struct snd_soc_dai *dai,
					       u8 int_mix_fs_rate_reg_val,
					       u32 sample_rate)
{
	u8 int_2_inp;
	u32 j, port;
	u16 int_mux_cfg1, int_fs_reg;
	u8 int_mux_cfg1_val;
	struct snd_soc_component *component = dai->component;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	for_each_set_bit(port, &wsa->active_ch_mask[dai->id], WSA_MACRO_RX_MAX) {
		int_2_inp = port;
		if ((int_2_inp < WSA_MACRO_RX0) || (int_2_inp > WSA_MACRO_RX_MIX1)) {
			dev_err(component->dev,	"%s: Invalid RX port, Dai ID is %d\n",
				__func__, dai->id);
			return -EINVAL;
		}

		int_mux_cfg1 = CDC_WSA_RX_INP_MUX_RX_INT0_CFG1;
		for (j = 0; j < NUM_INTERPOLATORS; j++) {
			int_mux_cfg1_val = snd_soc_component_read(component,
							int_mux_cfg1) &
							WSA_MACRO_MUX_INP_MASK1;
			if (int_mux_cfg1_val == int_2_inp + INTn_2_INP_SEL_RX0) {
				int_fs_reg = CDC_WSA_RX0_RX_PATH_MIX_CTL +
					WSA_MACRO_RX_PATH_OFFSET * j;

				snd_soc_component_update_bits(component,
						      int_fs_reg,
						      WSA_MACRO_FS_RATE_MASK,
						      int_mix_fs_rate_reg_val);
			}
			int_mux_cfg1 += WSA_MACRO_MUX_CFG_OFFSET;
		}
	}
	return 0;
}

static int wsa_macro_set_interpolator_rate(struct snd_soc_dai *dai,
					   u32 sample_rate)
{
	int rate_val = 0;
	int i, ret;

	/* set mixing path rate */
	for (i = 0; i < ARRAY_SIZE(int_mix_sample_rate_val); i++) {
		if (sample_rate == int_mix_sample_rate_val[i].sample_rate) {
			rate_val = int_mix_sample_rate_val[i].rate_val;
			break;
		}
	}
	if ((i == ARRAY_SIZE(int_mix_sample_rate_val)) || (rate_val < 0))
		goto prim_rate;

	ret = wsa_macro_set_mix_interpolator_rate(dai, (u8) rate_val, sample_rate);
prim_rate:
	/* set primary path sample rate */
	for (i = 0; i < ARRAY_SIZE(int_prim_sample_rate_val); i++) {
		if (sample_rate == int_prim_sample_rate_val[i].sample_rate) {
			rate_val = int_prim_sample_rate_val[i].rate_val;
			break;
		}
	}
	if ((i == ARRAY_SIZE(int_prim_sample_rate_val)) || (rate_val < 0))
		return -EINVAL;

	ret = wsa_macro_set_prim_interpolator_rate(dai, (u8) rate_val, sample_rate);

	return ret;
}

static int wsa_macro_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		ret = wsa_macro_set_interpolator_rate(dai, params_rate(params));
		if (ret) {
			dev_err(component->dev,
				"%s: cannot set sample rate: %u\n",
				__func__, params_rate(params));
			return ret;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int wsa_macro_get_channel_map(struct snd_soc_dai *dai,
				     unsigned int *tx_num, unsigned int *tx_slot,
				     unsigned int *rx_num, unsigned int *rx_slot)
{
	struct snd_soc_component *component = dai->component;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);
	u16 val, mask = 0, cnt = 0, temp;

	switch (dai->id) {
	case WSA_MACRO_AIF_VI:
		*tx_slot = wsa->active_ch_mask[dai->id];
		*tx_num = wsa->active_ch_cnt[dai->id];
		break;
	case WSA_MACRO_AIF1_PB:
	case WSA_MACRO_AIF_MIX1_PB:
		for_each_set_bit(temp, &wsa->active_ch_mask[dai->id],
					WSA_MACRO_RX_MAX) {
			mask |= (1 << temp);
			if (++cnt == WSA_MACRO_MAX_DMA_CH_PER_PORT)
				break;
		}
		if (mask & 0x0C)
			mask = mask >> 0x2;
		*rx_slot = mask;
		*rx_num = cnt;
		break;
	case WSA_MACRO_AIF_ECHO:
		val = snd_soc_component_read(component, CDC_WSA_RX_INP_MUX_RX_MIX_CFG0);
		if (val & WSA_MACRO_EC_MIX_TX1_MASK) {
			mask |= 0x2;
			cnt++;
		}
		if (val & WSA_MACRO_EC_MIX_TX0_MASK) {
			mask |= 0x1;
			cnt++;
		}
		*tx_slot = mask;
		*tx_num = cnt;
		break;
	default:
		dev_err(component->dev, "%s: Invalid AIF\n", __func__);
		break;
	}
	return 0;
}

static struct snd_soc_dai_ops wsa_macro_dai_ops = {
	.hw_params = wsa_macro_hw_params,
	.get_channel_map = wsa_macro_get_channel_map,
};

static struct snd_soc_dai_driver wsa_macro_dai[] = {
	{
		.name = "wsa_macro_rx1",
		.id = WSA_MACRO_AIF1_PB,
		.playback = {
			.stream_name = "WSA_AIF1 Playback",
			.rates = WSA_MACRO_RX_RATES,
			.formats = WSA_MACRO_RX_FORMATS,
			.rate_max = 384000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wsa_macro_dai_ops,
	},
	{
		.name = "wsa_macro_rx_mix",
		.id = WSA_MACRO_AIF_MIX1_PB,
		.playback = {
			.stream_name = "WSA_AIF_MIX1 Playback",
			.rates = WSA_MACRO_RX_MIX_RATES,
			.formats = WSA_MACRO_RX_FORMATS,
			.rate_max = 192000,
			.rate_min = 48000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wsa_macro_dai_ops,
	},
	{
		.name = "wsa_macro_vifeedback",
		.id = WSA_MACRO_AIF_VI,
		.capture = {
			.stream_name = "WSA_AIF_VI Capture",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_48000,
			.formats = WSA_MACRO_RX_FORMATS,
			.rate_max = 48000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 4,
		},
		.ops = &wsa_macro_dai_ops,
	},
	{
		.name = "wsa_macro_echo",
		.id = WSA_MACRO_AIF_ECHO,
		.capture = {
			.stream_name = "WSA_AIF_ECHO Capture",
			.rates = WSA_MACRO_ECHO_RATES,
			.formats = WSA_MACRO_ECHO_FORMATS,
			.rate_max = 48000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &wsa_macro_dai_ops,
	},
};

static void wsa_macro_mclk_enable(struct wsa_macro *wsa, bool mclk_enable)
{
	struct regmap *regmap = wsa->regmap;

	if (mclk_enable) {
		if (wsa->wsa_mclk_users == 0) {
			regcache_mark_dirty(regmap);
			regcache_sync(regmap);
			/* 9.6MHz MCLK, set value 0x00 if other frequency */
			regmap_update_bits(regmap, CDC_WSA_TOP_FREQ_MCLK, 0x01, 0x01);
			regmap_update_bits(regmap,
					   CDC_WSA_CLK_RST_CTRL_MCLK_CONTROL,
					   CDC_WSA_MCLK_EN_MASK,
					   CDC_WSA_MCLK_ENABLE);
			regmap_update_bits(regmap,
					   CDC_WSA_CLK_RST_CTRL_FS_CNT_CONTROL,
					   CDC_WSA_FS_CNT_EN_MASK,
					   CDC_WSA_FS_CNT_ENABLE);
		}
		wsa->wsa_mclk_users++;
	} else {
		if (wsa->wsa_mclk_users <= 0) {
			dev_err(wsa->dev, "clock already disabled\n");
			wsa->wsa_mclk_users = 0;
			return;
		}
		wsa->wsa_mclk_users--;
		if (wsa->wsa_mclk_users == 0) {
			regmap_update_bits(regmap,
					   CDC_WSA_CLK_RST_CTRL_FS_CNT_CONTROL,
					   CDC_WSA_FS_CNT_EN_MASK,
					   CDC_WSA_FS_CNT_DISABLE);
			regmap_update_bits(regmap,
					   CDC_WSA_CLK_RST_CTRL_MCLK_CONTROL,
					   CDC_WSA_MCLK_EN_MASK,
					   CDC_WSA_MCLK_DISABLE);
		}
	}
}

static int wsa_macro_get_ec_hq(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{

	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int ec_tx = ((struct soc_mixer_control *) kcontrol->private_value)->shift;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wsa->ec_hq[ec_tx];

	return 0;
}

static int wsa_macro_set_ec_hq(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int ec_tx = ((struct soc_mixer_control *) kcontrol->private_value)->shift;
	int value = ucontrol->value.integer.value[0];
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	wsa->ec_hq[ec_tx] = value;

	return 0;
}

static int wsa_macro_get_compander(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{

	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int comp = ((struct soc_mixer_control *) kcontrol->private_value)->shift;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wsa->comp_enabled[comp];
	return 0;
}

static int wsa_macro_set_compander(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int comp = ((struct soc_mixer_control *) kcontrol->private_value)->shift;
	int value = ucontrol->value.integer.value[0];
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	wsa->comp_enabled[comp] = value;

	return 0;
}

static int wsa_macro_ear_spkr_pa_gain_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wsa->ear_spkr_gain;

	return 0;
}

static int wsa_macro_ear_spkr_pa_gain_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	wsa->ear_spkr_gain =  ucontrol->value.integer.value[0];

	return 0;
}

static int wsa_macro_soft_clip_enable_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);
	int path = ((struct soc_mixer_control *)kcontrol->private_value)->shift;

	ucontrol->value.integer.value[0] = wsa->is_softclip_on[path];

	return 0;
}

static int wsa_macro_soft_clip_enable_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);
	int path = ((struct soc_mixer_control *) kcontrol->private_value)->shift;

	wsa->is_softclip_on[path] =  ucontrol->value.integer.value[0];

	return 0;
}

static const struct snd_kcontrol_new wsa_macro_snd_controls[] = {
	SOC_ENUM_EXT("EAR SPKR PA Gain", wsa_macro_ear_spkr_pa_gain_enum,
		     wsa_macro_ear_spkr_pa_gain_get,
		     wsa_macro_ear_spkr_pa_gain_put),
	SOC_SINGLE_EXT("WSA_Softclip0 Enable", SND_SOC_NOPM,
			WSA_MACRO_SOFTCLIP0, 1, 0,
			wsa_macro_soft_clip_enable_get,
			wsa_macro_soft_clip_enable_put),
	SOC_SINGLE_EXT("WSA_Softclip1 Enable", SND_SOC_NOPM,
			WSA_MACRO_SOFTCLIP1, 1, 0,
			wsa_macro_soft_clip_enable_get,
			wsa_macro_soft_clip_enable_put),

	SOC_SINGLE_S8_TLV("WSA_RX0 Digital Volume", CDC_WSA_RX0_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("WSA_RX1 Digital Volume", CDC_WSA_RX1_RX_VOL_CTL,
			  -84, 40, digital_gain),

	SOC_SINGLE("WSA_RX0 Digital Mute", CDC_WSA_RX0_RX_PATH_CTL, 4, 1, 0),
	SOC_SINGLE("WSA_RX1 Digital Mute", CDC_WSA_RX1_RX_PATH_CTL, 4, 1, 0),
	SOC_SINGLE("WSA_RX0_MIX Digital Mute", CDC_WSA_RX0_RX_PATH_MIX_CTL, 4,
		   1, 0),
	SOC_SINGLE("WSA_RX1_MIX Digital Mute", CDC_WSA_RX1_RX_PATH_MIX_CTL, 4,
		   1, 0),
	SOC_SINGLE_EXT("WSA_COMP1 Switch", SND_SOC_NOPM, WSA_MACRO_COMP1, 1, 0,
		       wsa_macro_get_compander, wsa_macro_set_compander),
	SOC_SINGLE_EXT("WSA_COMP2 Switch", SND_SOC_NOPM, WSA_MACRO_COMP2, 1, 0,
		       wsa_macro_get_compander, wsa_macro_set_compander),
	SOC_SINGLE_EXT("WSA_RX0 EC_HQ Switch", SND_SOC_NOPM, WSA_MACRO_RX0, 1, 0,
		       wsa_macro_get_ec_hq, wsa_macro_set_ec_hq),
	SOC_SINGLE_EXT("WSA_RX1 EC_HQ Switch", SND_SOC_NOPM, WSA_MACRO_RX1, 1, 0,
		       wsa_macro_get_ec_hq, wsa_macro_set_ec_hq),
};

static int wsa_swrm_clock(struct wsa_macro *wsa, bool enable)
{
	struct regmap *regmap = wsa->regmap;

	if (enable) {
		wsa_macro_mclk_enable(wsa, true);

		/* reset swr ip */
		if (wsa->reset_swr)
			regmap_update_bits(regmap,
					   CDC_WSA_CLK_RST_CTRL_SWR_CONTROL,
					   CDC_WSA_SWR_RST_EN_MASK,
					   CDC_WSA_SWR_RST_ENABLE);

		regmap_update_bits(regmap, CDC_WSA_CLK_RST_CTRL_SWR_CONTROL,
				   CDC_WSA_SWR_CLK_EN_MASK,
				   CDC_WSA_SWR_CLK_ENABLE);

		/* Bring out of reset */
		if (wsa->reset_swr)
			regmap_update_bits(regmap,
					   CDC_WSA_CLK_RST_CTRL_SWR_CONTROL,
					   CDC_WSA_SWR_RST_EN_MASK,
					   CDC_WSA_SWR_RST_DISABLE);
		wsa->reset_swr = false;
	} else {
		regmap_update_bits(regmap, CDC_WSA_CLK_RST_CTRL_SWR_CONTROL,
				   CDC_WSA_SWR_CLK_EN_MASK, 0);
		wsa_macro_mclk_enable(wsa, false);
	}

	return 0;
}

static int wsa_macro_component_probe(struct snd_soc_component *comp)
{
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(comp);

	snd_soc_component_init_regmap(comp, wsa->regmap);

	wsa->spkr_gain_offset = WSA_MACRO_GAIN_OFFSET_M1P5_DB;

	/* set SPKR rate to FS_2P4_3P072 */
	snd_soc_component_update_bits(comp, CDC_WSA_RX0_RX_PATH_CFG1,
				CDC_WSA_RX_PATH_SPKR_RATE_MASK,
				CDC_WSA_RX_PATH_SPKR_RATE_FS_2P4_3P072);

	snd_soc_component_update_bits(comp, CDC_WSA_RX1_RX_PATH_CFG1,
				CDC_WSA_RX_PATH_SPKR_RATE_MASK,
				CDC_WSA_RX_PATH_SPKR_RATE_FS_2P4_3P072);

	wsa_macro_set_spkr_mode(comp, WSA_MACRO_SPKR_MODE_1);

	return 0;
}

static int swclk_gate_enable(struct clk_hw *hw)
{
	return wsa_swrm_clock(to_wsa_macro(hw), true);
}

static void swclk_gate_disable(struct clk_hw *hw)
{
	wsa_swrm_clock(to_wsa_macro(hw), false);
}

static int swclk_gate_is_enabled(struct clk_hw *hw)
{
	struct wsa_macro *wsa = to_wsa_macro(hw);
	int ret, val;

	regmap_read(wsa->regmap, CDC_WSA_CLK_RST_CTRL_SWR_CONTROL, &val);
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

static struct clk *wsa_macro_register_mclk_output(struct wsa_macro *wsa)
{
	struct device *dev = wsa->dev;
	struct device_node *np = dev->of_node;
	const char *parent_clk_name;
	const char *clk_name = "mclk";
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	parent_clk_name = __clk_get_name(wsa->clks[2].clk);

	init.name = clk_name;
	init.ops = &swclk_gate_ops;
	init.flags = 0;
	init.parent_names = &parent_clk_name;
	init.num_parents = 1;
	wsa->hw.init = &init;
	hw = &wsa->hw;
	ret = clk_hw_register(wsa->dev, hw);
	if (ret)
		return ERR_PTR(ret);

	of_clk_add_provider(np, of_clk_src_simple_get, hw->clk);

	return NULL;
}

static const struct snd_soc_component_driver wsa_macro_component_drv = {
	.name = "WSA MACRO",
	.probe = wsa_macro_component_probe,
	.controls = wsa_macro_snd_controls,
	.num_controls = ARRAY_SIZE(wsa_macro_snd_controls),
};

static int wsa_macro_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wsa_macro *wsa;
	void __iomem *base;
	int ret;

	wsa = devm_kzalloc(dev, sizeof(*wsa), GFP_KERNEL);
	if (!wsa)
		return -ENOMEM;

	wsa->clks[0].id = "macro";
	wsa->clks[1].id = "dcodec";
	wsa->clks[2].id = "mclk";
	wsa->clks[3].id = "npl";
	wsa->clks[4].id = "fsgen";

	ret = devm_clk_bulk_get(dev, WSA_NUM_CLKS_MAX, wsa->clks);
	if (ret) {
		dev_err(dev, "Error getting WSA Clocks (%d)\n", ret);
		return ret;
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	wsa->regmap = devm_regmap_init_mmio(dev, base, &wsa_regmap_config);

	dev_set_drvdata(dev, wsa);

	wsa->reset_swr = true;
	wsa->dev = dev;

	/* set MCLK and NPL rates */
	clk_set_rate(wsa->clks[2].clk, WSA_MACRO_MCLK_FREQ);
	clk_set_rate(wsa->clks[3].clk, WSA_MACRO_MCLK_FREQ);

	ret = clk_bulk_prepare_enable(WSA_NUM_CLKS_MAX, wsa->clks);
	if (ret)
		return ret;

	wsa_macro_register_mclk_output(wsa);

	ret = devm_snd_soc_register_component(dev, &wsa_macro_component_drv,
					      wsa_macro_dai,
					      ARRAY_SIZE(wsa_macro_dai));
	if (ret)
		goto err;

	return ret;
err:
	clk_bulk_disable_unprepare(WSA_NUM_CLKS_MAX, wsa->clks);

	return ret;

}

static int wsa_macro_remove(struct platform_device *pdev)
{
	struct wsa_macro *wsa = dev_get_drvdata(&pdev->dev);

	of_clk_del_provider(pdev->dev.of_node);

	clk_bulk_disable_unprepare(WSA_NUM_CLKS_MAX, wsa->clks);

	return 0;
}

static const struct of_device_id wsa_macro_dt_match[] = {
	{.compatible = "qcom,sm8250-lpass-wsa-macro"},
	{}
};
MODULE_DEVICE_TABLE(of, wsa_macro_dt_match);

static struct platform_driver wsa_macro_driver = {
	.driver = {
		.name = "wsa_macro",
		.of_match_table = wsa_macro_dt_match,
	},
	.probe = wsa_macro_probe,
	.remove = wsa_macro_remove,
};

module_platform_driver(wsa_macro_driver);
MODULE_DESCRIPTION("WSA macro driver");
MODULE_LICENSE("GPL");
