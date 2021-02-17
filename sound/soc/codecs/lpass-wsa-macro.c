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
#define CDC_WSA_RX_INTX_1_MIX_INP0_SEL_MASK	GENMASK(2, 0)
#define CDC_WSA_RX_INTX_1_MIX_INP1_SEL_MASK	GENMASK(5, 3)
#define CDC_WSA_RX_INP_MUX_RX_INT0_CFG1		(0x0104)
#define CDC_WSA_RX_INTX_2_SEL_MASK		GENMASK(2, 0)
#define CDC_WSA_RX_INTX_1_MIX_INP2_SEL_MASK	GENMASK(5, 3)
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

static const char *const rx_text[] = {
	"ZERO", "RX0", "RX1", "RX_MIX0", "RX_MIX1", "DEC0", "DEC1"
};

static const char *const rx_mix_text[] = {
	"ZERO", "RX0", "RX1", "RX_MIX0", "RX_MIX1"
};

static const char *const rx_mix_ec_text[] = {
	"ZERO", "RX_MIX_TX0", "RX_MIX_TX1"
};

static const char *const rx_mux_text[] = {
	"ZERO", "AIF1_PB", "AIF_MIX1_PB"
};

static const char *const rx_sidetone_mix_text[] = {
	"ZERO", "SRC0"
};

static const char * const wsa_macro_ear_spkr_pa_gain_text[] = {
	"G_DEFAULT", "G_0_DB", "G_1_DB", "G_2_DB", "G_3_DB",
	"G_4_DB", "G_5_DB", "G_6_DB"
};

static SOC_ENUM_SINGLE_EXT_DECL(wsa_macro_ear_spkr_pa_gain_enum,
				wsa_macro_ear_spkr_pa_gain_text);

/* RX INT0 */
static const struct soc_enum rx0_prim_inp0_chain_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_INT0_CFG0,
		0, 7, rx_text);

static const struct soc_enum rx0_prim_inp1_chain_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_INT0_CFG0,
		3, 7, rx_text);

static const struct soc_enum rx0_prim_inp2_chain_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_INT0_CFG1,
		3, 7, rx_text);

static const struct soc_enum rx0_mix_chain_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_INT0_CFG1,
		0, 5, rx_mix_text);

static const struct soc_enum rx0_sidetone_mix_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, 2, rx_sidetone_mix_text);

static const struct snd_kcontrol_new rx0_prim_inp0_mux =
	SOC_DAPM_ENUM("WSA_RX0 INP0 Mux", rx0_prim_inp0_chain_enum);

static const struct snd_kcontrol_new rx0_prim_inp1_mux =
	SOC_DAPM_ENUM("WSA_RX0 INP1 Mux", rx0_prim_inp1_chain_enum);

static const struct snd_kcontrol_new rx0_prim_inp2_mux =
	SOC_DAPM_ENUM("WSA_RX0 INP2 Mux", rx0_prim_inp2_chain_enum);

static const struct snd_kcontrol_new rx0_mix_mux =
	SOC_DAPM_ENUM("WSA_RX0 MIX Mux", rx0_mix_chain_enum);

static const struct snd_kcontrol_new rx0_sidetone_mix_mux =
	SOC_DAPM_ENUM("WSA_RX0 SIDETONE MIX Mux", rx0_sidetone_mix_enum);

/* RX INT1 */
static const struct soc_enum rx1_prim_inp0_chain_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_INT1_CFG0,
		0, 7, rx_text);

static const struct soc_enum rx1_prim_inp1_chain_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_INT1_CFG0,
		3, 7, rx_text);

static const struct soc_enum rx1_prim_inp2_chain_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_INT1_CFG1,
		3, 7, rx_text);

static const struct soc_enum rx1_mix_chain_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_INT1_CFG1,
		0, 5, rx_mix_text);

static const struct snd_kcontrol_new rx1_prim_inp0_mux =
	SOC_DAPM_ENUM("WSA_RX1 INP0 Mux", rx1_prim_inp0_chain_enum);

static const struct snd_kcontrol_new rx1_prim_inp1_mux =
	SOC_DAPM_ENUM("WSA_RX1 INP1 Mux", rx1_prim_inp1_chain_enum);

static const struct snd_kcontrol_new rx1_prim_inp2_mux =
	SOC_DAPM_ENUM("WSA_RX1 INP2 Mux", rx1_prim_inp2_chain_enum);

static const struct snd_kcontrol_new rx1_mix_mux =
	SOC_DAPM_ENUM("WSA_RX1 MIX Mux", rx1_mix_chain_enum);

static const struct soc_enum rx_mix_ec0_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_MIX_CFG0,
		0, 3, rx_mix_ec_text);

static const struct soc_enum rx_mix_ec1_enum =
	SOC_ENUM_SINGLE(CDC_WSA_RX_INP_MUX_RX_MIX_CFG0,
		3, 3, rx_mix_ec_text);

static const struct snd_kcontrol_new rx_mix_ec0_mux =
	SOC_DAPM_ENUM("WSA RX_MIX EC0_Mux", rx_mix_ec0_enum);

static const struct snd_kcontrol_new rx_mix_ec1_mux =
	SOC_DAPM_ENUM("WSA RX_MIX EC1_Mux", rx_mix_ec1_enum);

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
			inp0_sel = snd_soc_component_read_field(component, int_mux_cfg0, 
								CDC_WSA_RX_INTX_1_MIX_INP0_SEL_MASK);
			inp1_sel = snd_soc_component_read_field(component, int_mux_cfg0, 
								CDC_WSA_RX_INTX_1_MIX_INP1_SEL_MASK);
			inp2_sel = snd_soc_component_read_field(component, int_mux_cfg1,
								CDC_WSA_RX_INTX_1_MIX_INP2_SEL_MASK);

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
			int_mux_cfg1_val = snd_soc_component_read_field(component, int_mux_cfg1,
									CDC_WSA_RX_INTX_2_SEL_MASK);

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

static int wsa_macro_mclk_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	wsa_macro_mclk_enable(wsa, event == SND_SOC_DAPM_PRE_PMU);
	return 0;
}

static int wsa_macro_enable_vi_feedback(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);
	u32 tx_reg0, tx_reg1;

	if (test_bit(WSA_MACRO_TX0, &wsa->active_ch_mask[WSA_MACRO_AIF_VI])) {
		tx_reg0 = CDC_WSA_TX0_SPKR_PROT_PATH_CTL;
		tx_reg1 = CDC_WSA_TX1_SPKR_PROT_PATH_CTL;
	} else if (test_bit(WSA_MACRO_TX1, &wsa->active_ch_mask[WSA_MACRO_AIF_VI])) {
		tx_reg0 = CDC_WSA_TX2_SPKR_PROT_PATH_CTL;
		tx_reg1 = CDC_WSA_TX3_SPKR_PROT_PATH_CTL;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
			/* Enable V&I sensing */
		snd_soc_component_update_bits(component, tx_reg0,
					      CDC_WSA_TX_SPKR_PROT_RESET_MASK,
					      CDC_WSA_TX_SPKR_PROT_RESET);
		snd_soc_component_update_bits(component, tx_reg1,
					      CDC_WSA_TX_SPKR_PROT_RESET_MASK,
					      CDC_WSA_TX_SPKR_PROT_RESET);
		snd_soc_component_update_bits(component, tx_reg0,
					      CDC_WSA_TX_SPKR_PROT_PCM_RATE_MASK,
					      CDC_WSA_TX_SPKR_PROT_PCM_RATE_8K);
		snd_soc_component_update_bits(component, tx_reg1,
					      CDC_WSA_TX_SPKR_PROT_PCM_RATE_MASK,
					      CDC_WSA_TX_SPKR_PROT_PCM_RATE_8K);
		snd_soc_component_update_bits(component, tx_reg0,
					      CDC_WSA_TX_SPKR_PROT_CLK_EN_MASK,
					      CDC_WSA_TX_SPKR_PROT_CLK_ENABLE);
		snd_soc_component_update_bits(component, tx_reg1,
					      CDC_WSA_TX_SPKR_PROT_CLK_EN_MASK,
					      CDC_WSA_TX_SPKR_PROT_CLK_ENABLE);
		snd_soc_component_update_bits(component, tx_reg0,
					      CDC_WSA_TX_SPKR_PROT_RESET_MASK,
					      CDC_WSA_TX_SPKR_PROT_NO_RESET);
		snd_soc_component_update_bits(component, tx_reg1,
					      CDC_WSA_TX_SPKR_PROT_RESET_MASK,
					      CDC_WSA_TX_SPKR_PROT_NO_RESET);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Disable V&I sensing */
		snd_soc_component_update_bits(component, tx_reg0,
					      CDC_WSA_TX_SPKR_PROT_RESET_MASK,
					      CDC_WSA_TX_SPKR_PROT_RESET);
		snd_soc_component_update_bits(component, tx_reg1,
					      CDC_WSA_TX_SPKR_PROT_RESET_MASK,
					      CDC_WSA_TX_SPKR_PROT_RESET);
		snd_soc_component_update_bits(component, tx_reg0,
					      CDC_WSA_TX_SPKR_PROT_CLK_EN_MASK,
					      CDC_WSA_TX_SPKR_PROT_CLK_DISABLE);
		snd_soc_component_update_bits(component, tx_reg1,
					      CDC_WSA_TX_SPKR_PROT_CLK_EN_MASK,
					      CDC_WSA_TX_SPKR_PROT_CLK_DISABLE);
		break;
	}

	return 0;
}

static int wsa_macro_enable_mix_path(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 gain_reg;
	int val;

	switch (w->reg) {
	case CDC_WSA_RX0_RX_PATH_MIX_CTL:
		gain_reg = CDC_WSA_RX0_RX_VOL_MIX_CTL;
		break;
	case CDC_WSA_RX1_RX_PATH_MIX_CTL:
		gain_reg = CDC_WSA_RX1_RX_VOL_MIX_CTL;
		break;
	default:
		return 0;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = snd_soc_component_read(component, gain_reg);
		snd_soc_component_write(component, gain_reg, val);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, w->reg,
					      CDC_WSA_RX_PATH_MIX_CLK_EN_MASK,
					      CDC_WSA_RX_PATH_MIX_CLK_DISABLE);
		break;
	}

	return 0;
}

static void wsa_macro_hd2_control(struct snd_soc_component *component,
				  u16 reg, int event)
{
	u16 hd2_scale_reg;
	u16 hd2_enable_reg;

	if (reg == CDC_WSA_RX0_RX_PATH_CTL) {
		hd2_scale_reg = CDC_WSA_RX0_RX_PATH_SEC3;
		hd2_enable_reg = CDC_WSA_RX0_RX_PATH_CFG0;
	}
	if (reg == CDC_WSA_RX1_RX_PATH_CTL) {
		hd2_scale_reg = CDC_WSA_RX1_RX_PATH_SEC3;
		hd2_enable_reg = CDC_WSA_RX1_RX_PATH_CFG0;
	}

	if (hd2_enable_reg && SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(component, hd2_scale_reg,
					      CDC_WSA_RX_PATH_HD2_ALPHA_MASK,
					      0x10);
		snd_soc_component_update_bits(component, hd2_scale_reg,
					      CDC_WSA_RX_PATH_HD2_SCALE_MASK,
					      0x1);
		snd_soc_component_update_bits(component, hd2_enable_reg,
					      CDC_WSA_RX_PATH_HD2_EN_MASK,
					      CDC_WSA_RX_PATH_HD2_ENABLE);
	}

	if (hd2_enable_reg && SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(component, hd2_enable_reg,
					      CDC_WSA_RX_PATH_HD2_EN_MASK, 0);
		snd_soc_component_update_bits(component, hd2_scale_reg,
					      CDC_WSA_RX_PATH_HD2_SCALE_MASK,
					      0);
		snd_soc_component_update_bits(component, hd2_scale_reg,
					      CDC_WSA_RX_PATH_HD2_ALPHA_MASK,
					      0);
	}
}

static int wsa_macro_config_compander(struct snd_soc_component *component,
				      int comp, int event)
{
	u16 comp_ctl0_reg, rx_path_cfg0_reg;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	if (!wsa->comp_enabled[comp])
		return 0;

	comp_ctl0_reg = CDC_WSA_COMPANDER0_CTL0 +
					(comp * WSA_MACRO_RX_COMP_OFFSET);
	rx_path_cfg0_reg = CDC_WSA_RX0_RX_PATH_CFG0 +
					(comp * WSA_MACRO_RX_PATH_OFFSET);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		/* Enable Compander Clock */
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					      CDC_WSA_COMPANDER_CLK_EN_MASK,
					      CDC_WSA_COMPANDER_CLK_ENABLE);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					      CDC_WSA_COMPANDER_SOFT_RST_MASK,
					      CDC_WSA_COMPANDER_SOFT_RST_ENABLE);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					      CDC_WSA_COMPANDER_SOFT_RST_MASK,
					      0);
		snd_soc_component_update_bits(component, rx_path_cfg0_reg,
					      CDC_WSA_RX_PATH_COMP_EN_MASK,
					      CDC_WSA_RX_PATH_COMP_ENABLE);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					      CDC_WSA_COMPANDER_HALT_MASK,
					      CDC_WSA_COMPANDER_HALT);
		snd_soc_component_update_bits(component, rx_path_cfg0_reg,
					      CDC_WSA_RX_PATH_COMP_EN_MASK, 0);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					      CDC_WSA_COMPANDER_SOFT_RST_MASK,
					      CDC_WSA_COMPANDER_SOFT_RST_ENABLE);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					      CDC_WSA_COMPANDER_SOFT_RST_MASK,
					      0);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					      CDC_WSA_COMPANDER_CLK_EN_MASK, 0);
		snd_soc_component_update_bits(component, comp_ctl0_reg,
					      CDC_WSA_COMPANDER_HALT_MASK, 0);
	}

	return 0;
}

static void wsa_macro_enable_softclip_clk(struct snd_soc_component *component,
					 struct wsa_macro *wsa,
					 int path,
					 bool enable)
{
	u16 softclip_clk_reg = CDC_WSA_SOFTCLIP0_CRC +
			(path * WSA_MACRO_RX_SOFTCLIP_OFFSET);
	u8 softclip_mux_mask = (1 << path);
	u8 softclip_mux_value = (1 << path);

	if (enable) {
		if (wsa->softclip_clk_users[path] == 0) {
			snd_soc_component_update_bits(component,
						softclip_clk_reg,
						CDC_WSA_SOFTCLIP_CLK_EN_MASK,
						CDC_WSA_SOFTCLIP_CLK_ENABLE);
			snd_soc_component_update_bits(component,
				CDC_WSA_RX_INP_MUX_SOFTCLIP_CFG0,
				softclip_mux_mask, softclip_mux_value);
		}
		wsa->softclip_clk_users[path]++;
	} else {
		wsa->softclip_clk_users[path]--;
		if (wsa->softclip_clk_users[path] == 0) {
			snd_soc_component_update_bits(component,
						softclip_clk_reg,
						CDC_WSA_SOFTCLIP_CLK_EN_MASK,
						0);
			snd_soc_component_update_bits(component,
				CDC_WSA_RX_INP_MUX_SOFTCLIP_CFG0,
				softclip_mux_mask, 0x00);
		}
	}
}

static int wsa_macro_config_softclip(struct snd_soc_component *component,
				     int path, int event)
{
	u16 softclip_ctrl_reg;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);
	int softclip_path = 0;

	if (path == WSA_MACRO_COMP1)
		softclip_path = WSA_MACRO_SOFTCLIP0;
	else if (path == WSA_MACRO_COMP2)
		softclip_path = WSA_MACRO_SOFTCLIP1;

	if (!wsa->is_softclip_on[softclip_path])
		return 0;

	softclip_ctrl_reg = CDC_WSA_SOFTCLIP0_SOFTCLIP_CTRL +
				(softclip_path * WSA_MACRO_RX_SOFTCLIP_OFFSET);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		/* Enable Softclip clock and mux */
		wsa_macro_enable_softclip_clk(component, wsa, softclip_path,
					      true);
		/* Enable Softclip control */
		snd_soc_component_update_bits(component, softclip_ctrl_reg,
					      CDC_WSA_SOFTCLIP_EN_MASK,
					      CDC_WSA_SOFTCLIP_ENABLE);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(component, softclip_ctrl_reg,
					      CDC_WSA_SOFTCLIP_EN_MASK, 0);
		wsa_macro_enable_softclip_clk(component, wsa, softclip_path,
					      false);
	}

	return 0;
}

static bool wsa_macro_adie_lb(struct snd_soc_component *component,
			      int interp_idx)
{
	u16 int_mux_cfg0,  int_mux_cfg1;
	u8 int_n_inp0, int_n_inp1, int_n_inp2;

	int_mux_cfg0 = CDC_WSA_RX_INP_MUX_RX_INT0_CFG0 + interp_idx * 8;
	int_mux_cfg1 = int_mux_cfg0 + 4;

	int_n_inp0 = snd_soc_component_read_field(component, int_mux_cfg0,
						  CDC_WSA_RX_INTX_1_MIX_INP0_SEL_MASK);
	if (int_n_inp0 == INTn_1_INP_SEL_DEC0 ||
		int_n_inp0 == INTn_1_INP_SEL_DEC1)
		return true;

	int_n_inp1 = snd_soc_component_read_field(component, int_mux_cfg0,
						  CDC_WSA_RX_INTX_1_MIX_INP1_SEL_MASK);
	if (int_n_inp1 == INTn_1_INP_SEL_DEC0 ||
		int_n_inp1 == INTn_1_INP_SEL_DEC1)
		return true;

	int_n_inp2 = snd_soc_component_read_field(component, int_mux_cfg1,
						  CDC_WSA_RX_INTX_1_MIX_INP2_SEL_MASK);
	if (int_n_inp2 == INTn_1_INP_SEL_DEC0 ||
		int_n_inp2 == INTn_1_INP_SEL_DEC1)
		return true;

	return false;
}

static int wsa_macro_enable_main_path(struct snd_soc_dapm_widget *w,
				      struct snd_kcontrol *kcontrol,
				      int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 reg;

	reg = CDC_WSA_RX0_RX_PATH_CTL + WSA_MACRO_RX_PATH_OFFSET * w->shift;
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (wsa_macro_adie_lb(component, w->shift)) {
			snd_soc_component_update_bits(component, reg,
					     CDC_WSA_RX_PATH_CLK_EN_MASK,
					     CDC_WSA_RX_PATH_CLK_ENABLE);
		}
		break;
	default:
		break;
	}
	return 0;
}

static int wsa_macro_interp_get_primary_reg(u16 reg, u16 *ind)
{
	u16 prim_int_reg = 0;

	switch (reg) {
	case CDC_WSA_RX0_RX_PATH_CTL:
	case CDC_WSA_RX0_RX_PATH_MIX_CTL:
		prim_int_reg = CDC_WSA_RX0_RX_PATH_CTL;
		*ind = 0;
		break;
	case CDC_WSA_RX1_RX_PATH_CTL:
	case CDC_WSA_RX1_RX_PATH_MIX_CTL:
		prim_int_reg = CDC_WSA_RX1_RX_PATH_CTL;
		*ind = 1;
		break;
	}

	return prim_int_reg;
}

static int wsa_macro_enable_prim_interpolator(struct snd_soc_component *component,
					      u16 reg, int event)
{
	u16 prim_int_reg;
	u16 ind = 0;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	prim_int_reg = wsa_macro_interp_get_primary_reg(reg, &ind);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wsa->prim_int_users[ind]++;
		if (wsa->prim_int_users[ind] == 1) {
			snd_soc_component_update_bits(component,
						      prim_int_reg + WSA_MACRO_RX_PATH_CFG3_OFFSET,
						      CDC_WSA_RX_DC_DCOEFF_MASK,
						      0x3);
			snd_soc_component_update_bits(component, prim_int_reg,
					CDC_WSA_RX_PATH_PGA_MUTE_EN_MASK,
					CDC_WSA_RX_PATH_PGA_MUTE_ENABLE);
			wsa_macro_hd2_control(component, prim_int_reg, event);
			snd_soc_component_update_bits(component,
				prim_int_reg + WSA_MACRO_RX_PATH_DSMDEM_OFFSET,
				CDC_WSA_RX_DSMDEM_CLK_EN_MASK,
				CDC_WSA_RX_DSMDEM_CLK_ENABLE);
		}
		if ((reg != prim_int_reg) &&
		    ((snd_soc_component_read(
				component, prim_int_reg)) & 0x10))
			snd_soc_component_update_bits(component, reg,
					0x10, 0x10);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wsa->prim_int_users[ind]--;
		if (wsa->prim_int_users[ind] == 0) {
			snd_soc_component_update_bits(component,
				prim_int_reg + WSA_MACRO_RX_PATH_DSMDEM_OFFSET,
				CDC_WSA_RX_DSMDEM_CLK_EN_MASK, 0);
			wsa_macro_hd2_control(component, prim_int_reg, event);
		}
		break;
	}

	return 0;
}

static int wsa_macro_config_ear_spkr_gain(struct snd_soc_component *component,
					  struct wsa_macro *wsa,
					  int event, int gain_reg)
{
	int comp_gain_offset, val;

	switch (wsa->spkr_mode) {
	/* Compander gain in WSA_MACRO_SPKR_MODE1 case is 12 dB */
	case WSA_MACRO_SPKR_MODE_1:
		comp_gain_offset = -12;
		break;
	/* Default case compander gain is 15 dB */
	default:
		comp_gain_offset = -15;
		break;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Apply ear spkr gain only if compander is enabled */
		if (wsa->comp_enabled[WSA_MACRO_COMP1] &&
		    (gain_reg == CDC_WSA_RX0_RX_VOL_CTL) &&
		    (wsa->ear_spkr_gain != 0)) {
			/* For example, val is -8(-12+5-1) for 4dB of gain */
			val = comp_gain_offset + wsa->ear_spkr_gain - 1;
			snd_soc_component_write(component, gain_reg, val);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/*
		 * Reset RX0 volume to 0 dB if compander is enabled and
		 * ear_spkr_gain is non-zero.
		 */
		if (wsa->comp_enabled[WSA_MACRO_COMP1] &&
		    (gain_reg == CDC_WSA_RX0_RX_VOL_CTL) &&
		    (wsa->ear_spkr_gain != 0)) {
			snd_soc_component_write(component, gain_reg, 0x0);
		}
		break;
	}

	return 0;
}

static int wsa_macro_enable_interpolator(struct snd_soc_dapm_widget *w,
					 struct snd_kcontrol *kcontrol,
					 int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 gain_reg;
	u16 reg;
	int val;
	int offset_val = 0;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	if (w->shift == WSA_MACRO_COMP1) {
		reg = CDC_WSA_RX0_RX_PATH_CTL;
		gain_reg = CDC_WSA_RX0_RX_VOL_CTL;
	} else if (w->shift == WSA_MACRO_COMP2) {
		reg = CDC_WSA_RX1_RX_PATH_CTL;
		gain_reg = CDC_WSA_RX1_RX_VOL_CTL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Reset if needed */
		wsa_macro_enable_prim_interpolator(component, reg, event);
		break;
	case SND_SOC_DAPM_POST_PMU:
		wsa_macro_config_compander(component, w->shift, event);
		wsa_macro_config_softclip(component, w->shift, event);
		/* apply gain after int clk is enabled */
		if ((wsa->spkr_gain_offset == WSA_MACRO_GAIN_OFFSET_M1P5_DB) &&
		    (wsa->comp_enabled[WSA_MACRO_COMP1] ||
		     wsa->comp_enabled[WSA_MACRO_COMP2])) {
			snd_soc_component_update_bits(component,
					CDC_WSA_RX0_RX_PATH_SEC1,
					CDC_WSA_RX_PGA_HALF_DB_MASK,
					CDC_WSA_RX_PGA_HALF_DB_ENABLE);
			snd_soc_component_update_bits(component,
					CDC_WSA_RX0_RX_PATH_MIX_SEC0,
					CDC_WSA_RX_PGA_HALF_DB_MASK,
					CDC_WSA_RX_PGA_HALF_DB_ENABLE);
			snd_soc_component_update_bits(component,
					CDC_WSA_RX1_RX_PATH_SEC1,
					CDC_WSA_RX_PGA_HALF_DB_MASK,
					CDC_WSA_RX_PGA_HALF_DB_ENABLE);
			snd_soc_component_update_bits(component,
					CDC_WSA_RX1_RX_PATH_MIX_SEC0,
					CDC_WSA_RX_PGA_HALF_DB_MASK,
					CDC_WSA_RX_PGA_HALF_DB_ENABLE);
			offset_val = -2;
		}
		val = snd_soc_component_read(component, gain_reg);
		val += offset_val;
		snd_soc_component_write(component, gain_reg, val);
		wsa_macro_config_ear_spkr_gain(component, wsa,
						event, gain_reg);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wsa_macro_config_compander(component, w->shift, event);
		wsa_macro_config_softclip(component, w->shift, event);
		wsa_macro_enable_prim_interpolator(component, reg, event);
		if ((wsa->spkr_gain_offset == WSA_MACRO_GAIN_OFFSET_M1P5_DB) &&
		    (wsa->comp_enabled[WSA_MACRO_COMP1] ||
		     wsa->comp_enabled[WSA_MACRO_COMP2])) {
			snd_soc_component_update_bits(component,
					CDC_WSA_RX0_RX_PATH_SEC1,
					CDC_WSA_RX_PGA_HALF_DB_MASK,
					CDC_WSA_RX_PGA_HALF_DB_DISABLE);
			snd_soc_component_update_bits(component,
					CDC_WSA_RX0_RX_PATH_MIX_SEC0,
					CDC_WSA_RX_PGA_HALF_DB_MASK,
					CDC_WSA_RX_PGA_HALF_DB_DISABLE);
			snd_soc_component_update_bits(component,
					CDC_WSA_RX1_RX_PATH_SEC1,
					CDC_WSA_RX_PGA_HALF_DB_MASK,
					CDC_WSA_RX_PGA_HALF_DB_DISABLE);
			snd_soc_component_update_bits(component,
					CDC_WSA_RX1_RX_PATH_MIX_SEC0,
					CDC_WSA_RX_PGA_HALF_DB_MASK,
					CDC_WSA_RX_PGA_HALF_DB_DISABLE);
			offset_val = 2;
			val = snd_soc_component_read(component, gain_reg);
			val += offset_val;
			snd_soc_component_write(component, gain_reg, val);
		}
		wsa_macro_config_ear_spkr_gain(component, wsa,
						event, gain_reg);
		break;
	}

	return 0;
}

static int wsa_macro_spk_boost_event(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol,
				     int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 boost_path_ctl, boost_path_cfg1;
	u16 reg, reg_mix;

	if (!strcmp(w->name, "WSA_RX INT0 CHAIN")) {
		boost_path_ctl = CDC_WSA_BOOST0_BOOST_PATH_CTL;
		boost_path_cfg1 = CDC_WSA_RX0_RX_PATH_CFG1;
		reg = CDC_WSA_RX0_RX_PATH_CTL;
		reg_mix = CDC_WSA_RX0_RX_PATH_MIX_CTL;
	} else if (!strcmp(w->name, "WSA_RX INT1 CHAIN")) {
		boost_path_ctl = CDC_WSA_BOOST1_BOOST_PATH_CTL;
		boost_path_cfg1 = CDC_WSA_RX1_RX_PATH_CFG1;
		reg = CDC_WSA_RX1_RX_PATH_CTL;
		reg_mix = CDC_WSA_RX1_RX_PATH_MIX_CTL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, boost_path_cfg1,
					      CDC_WSA_RX_PATH_SMART_BST_EN_MASK,
					      CDC_WSA_RX_PATH_SMART_BST_ENABLE);
		snd_soc_component_update_bits(component, boost_path_ctl,
					      CDC_WSA_BOOST_PATH_CLK_EN_MASK,
					      CDC_WSA_BOOST_PATH_CLK_ENABLE);
		if ((snd_soc_component_read(component, reg_mix)) & 0x10)
			snd_soc_component_update_bits(component, reg_mix,
						0x10, 0x00);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, reg, 0x10, 0x00);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, boost_path_ctl,
					      CDC_WSA_BOOST_PATH_CLK_EN_MASK,
					      CDC_WSA_BOOST_PATH_CLK_DISABLE);
		snd_soc_component_update_bits(component, boost_path_cfg1,
					      CDC_WSA_RX_PATH_SMART_BST_EN_MASK,
					      CDC_WSA_RX_PATH_SMART_BST_DISABLE);
		break;
	}

	return 0;
}

static int wsa_macro_enable_echo(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol,
				 int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);
	u16 val, ec_tx, ec_hq_reg;

	val = snd_soc_component_read(component, CDC_WSA_RX_INP_MUX_RX_MIX_CFG0);

	switch (w->shift) {
	case WSA_MACRO_EC0_MUX:
		val = val & CDC_WSA_RX_MIX_TX0_SEL_MASK;
		ec_tx = val - 1;
		break;
	case WSA_MACRO_EC1_MUX:
		val = val & CDC_WSA_RX_MIX_TX1_SEL_MASK;
		ec_tx = (val >> CDC_WSA_RX_MIX_TX1_SEL_SHFT) - 1;
		break;
	}

	if (wsa->ec_hq[ec_tx]) {
		ec_hq_reg = CDC_WSA_EC_HQ0_EC_REF_HQ_PATH_CTL +	0x40 * ec_tx;
		snd_soc_component_update_bits(component, ec_hq_reg,
					     CDC_WSA_EC_HQ_EC_CLK_EN_MASK,
					     CDC_WSA_EC_HQ_EC_CLK_ENABLE);
		ec_hq_reg = CDC_WSA_EC_HQ0_EC_REF_HQ_CFG0 + 0x40 * ec_tx;
		/* default set to 48k */
		snd_soc_component_update_bits(component, ec_hq_reg,
				      CDC_WSA_EC_HQ_EC_REF_PCM_RATE_MASK,
				      CDC_WSA_EC_HQ_EC_REF_PCM_RATE_48K);
	}

	return 0;
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

static int wsa_macro_rx_mux_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(widget->dapm);
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] =
			wsa->rx_port_value[widget->shift];
	return 0;
}

static int wsa_macro_rx_mux_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(widget->dapm);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_dapm_update *update = NULL;
	u32 rx_port_value = ucontrol->value.integer.value[0];
	u32 bit_input;
	u32 aif_rst;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);

	aif_rst = wsa->rx_port_value[widget->shift];
	if (!rx_port_value) {
		if (aif_rst == 0) {
			dev_err(component->dev, "%s: AIF reset already\n", __func__);
			return 0;
		}
		if (aif_rst >= WSA_MACRO_RX_MAX) {
			dev_err(component->dev, "%s: Invalid AIF reset\n", __func__);
			return 0;
		}
	}
	wsa->rx_port_value[widget->shift] = rx_port_value;

	bit_input = widget->shift;

	switch (rx_port_value) {
	case 0:
		if (wsa->active_ch_cnt[aif_rst]) {
			clear_bit(bit_input,
				  &wsa->active_ch_mask[aif_rst]);
			wsa->active_ch_cnt[aif_rst]--;
		}
		break;
	case 1:
	case 2:
		set_bit(bit_input,
			&wsa->active_ch_mask[rx_port_value]);
		wsa->active_ch_cnt[rx_port_value]++;
		break;
	default:
		dev_err(component->dev,
			"%s: Invalid AIF_ID for WSA RX MUX %d\n",
			__func__, rx_port_value);
		return -EINVAL;
	}

	snd_soc_dapm_mux_update_power(widget->dapm, kcontrol,
					rx_port_value, e, update);
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

static const struct soc_enum rx_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_mux_text), rx_mux_text);

static const struct snd_kcontrol_new rx_mux[WSA_MACRO_RX_MAX] = {
	SOC_DAPM_ENUM_EXT("WSA RX0 Mux", rx_mux_enum,
			  wsa_macro_rx_mux_get, wsa_macro_rx_mux_put),
	SOC_DAPM_ENUM_EXT("WSA RX1 Mux", rx_mux_enum,
			  wsa_macro_rx_mux_get, wsa_macro_rx_mux_put),
	SOC_DAPM_ENUM_EXT("WSA RX_MIX0 Mux", rx_mux_enum,
			  wsa_macro_rx_mux_get, wsa_macro_rx_mux_put),
	SOC_DAPM_ENUM_EXT("WSA RX_MIX1 Mux", rx_mux_enum,
			  wsa_macro_rx_mux_get, wsa_macro_rx_mux_put),
};

static int wsa_macro_vi_feed_mixer_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);
	u32 spk_tx_id = mixer->shift;
	u32 dai_id = widget->shift;

	if (test_bit(spk_tx_id, &wsa->active_ch_mask[dai_id]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int wsa_macro_vi_feed_mixer_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct soc_mixer_control *mixer = (struct soc_mixer_control *)kcontrol->private_value;
	struct wsa_macro *wsa = snd_soc_component_get_drvdata(component);
	u32 enable = ucontrol->value.integer.value[0];
	u32 spk_tx_id = mixer->shift;

	if (enable) {
		if (spk_tx_id == WSA_MACRO_TX0 &&
			!test_bit(WSA_MACRO_TX0,
				&wsa->active_ch_mask[WSA_MACRO_AIF_VI])) {
			set_bit(WSA_MACRO_TX0,
				&wsa->active_ch_mask[WSA_MACRO_AIF_VI]);
			wsa->active_ch_cnt[WSA_MACRO_AIF_VI]++;
		}
		if (spk_tx_id == WSA_MACRO_TX1 &&
			!test_bit(WSA_MACRO_TX1,
				&wsa->active_ch_mask[WSA_MACRO_AIF_VI])) {
			set_bit(WSA_MACRO_TX1,
				&wsa->active_ch_mask[WSA_MACRO_AIF_VI]);
			wsa->active_ch_cnt[WSA_MACRO_AIF_VI]++;
		}
	} else {
		if (spk_tx_id == WSA_MACRO_TX0 &&
			test_bit(WSA_MACRO_TX0,
				&wsa->active_ch_mask[WSA_MACRO_AIF_VI])) {
			clear_bit(WSA_MACRO_TX0,
				&wsa->active_ch_mask[WSA_MACRO_AIF_VI]);
			wsa->active_ch_cnt[WSA_MACRO_AIF_VI]--;
		}
		if (spk_tx_id == WSA_MACRO_TX1 &&
			test_bit(WSA_MACRO_TX1,
				&wsa->active_ch_mask[WSA_MACRO_AIF_VI])) {
			clear_bit(WSA_MACRO_TX1,
				&wsa->active_ch_mask[WSA_MACRO_AIF_VI]);
			wsa->active_ch_cnt[WSA_MACRO_AIF_VI]--;
		}
	}
	snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, enable, NULL);

	return 0;
}

static const struct snd_kcontrol_new aif_vi_mixer[] = {
	SOC_SINGLE_EXT("WSA_SPKR_VI_1", SND_SOC_NOPM, WSA_MACRO_TX0, 1, 0,
			wsa_macro_vi_feed_mixer_get,
			wsa_macro_vi_feed_mixer_put),
	SOC_SINGLE_EXT("WSA_SPKR_VI_2", SND_SOC_NOPM, WSA_MACRO_TX1, 1, 0,
			wsa_macro_vi_feed_mixer_get,
			wsa_macro_vi_feed_mixer_put),
};

static const struct snd_soc_dapm_widget wsa_macro_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("WSA AIF1 PB", "WSA_AIF1 Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("WSA AIF_MIX1 PB", "WSA_AIF_MIX1 Playback", 0,
			    SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_OUT_E("WSA AIF_VI", "WSA_AIF_VI Capture", 0,
			       SND_SOC_NOPM, WSA_MACRO_AIF_VI, 0,
			       wsa_macro_enable_vi_feedback,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT("WSA AIF_ECHO", "WSA_AIF_ECHO Capture", 0,
			     SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MIXER("WSA_AIF_VI Mixer", SND_SOC_NOPM, WSA_MACRO_AIF_VI,
			   0, aif_vi_mixer, ARRAY_SIZE(aif_vi_mixer)),
	SND_SOC_DAPM_MUX_E("WSA RX_MIX EC0_MUX", SND_SOC_NOPM,
			   WSA_MACRO_EC0_MUX, 0,
			   &rx_mix_ec0_mux, wsa_macro_enable_echo,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("WSA RX_MIX EC1_MUX", SND_SOC_NOPM,
			   WSA_MACRO_EC1_MUX, 0,
			   &rx_mix_ec1_mux, wsa_macro_enable_echo,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("WSA RX0 MUX", SND_SOC_NOPM, WSA_MACRO_RX0, 0,
			 &rx_mux[WSA_MACRO_RX0]),
	SND_SOC_DAPM_MUX("WSA RX1 MUX", SND_SOC_NOPM, WSA_MACRO_RX1, 0,
			 &rx_mux[WSA_MACRO_RX1]),
	SND_SOC_DAPM_MUX("WSA RX_MIX0 MUX", SND_SOC_NOPM, WSA_MACRO_RX_MIX0, 0,
			 &rx_mux[WSA_MACRO_RX_MIX0]),
	SND_SOC_DAPM_MUX("WSA RX_MIX1 MUX", SND_SOC_NOPM, WSA_MACRO_RX_MIX1, 0,
			 &rx_mux[WSA_MACRO_RX_MIX1]),

	SND_SOC_DAPM_MIXER("WSA RX0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("WSA RX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("WSA RX_MIX0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("WSA RX_MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("WSA_RX0 INP0", SND_SOC_NOPM, 0, 0, &rx0_prim_inp0_mux),
	SND_SOC_DAPM_MUX("WSA_RX0 INP1", SND_SOC_NOPM, 0, 0, &rx0_prim_inp1_mux),
	SND_SOC_DAPM_MUX("WSA_RX0 INP2", SND_SOC_NOPM, 0, 0, &rx0_prim_inp2_mux),
	SND_SOC_DAPM_MUX_E("WSA_RX0 MIX INP", CDC_WSA_RX0_RX_PATH_MIX_CTL,
			   0, 0, &rx0_mix_mux, wsa_macro_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX("WSA_RX1 INP0", SND_SOC_NOPM, 0, 0, &rx1_prim_inp0_mux),
	SND_SOC_DAPM_MUX("WSA_RX1 INP1", SND_SOC_NOPM, 0, 0, &rx1_prim_inp1_mux),
	SND_SOC_DAPM_MUX("WSA_RX1 INP2", SND_SOC_NOPM, 0, 0, &rx1_prim_inp2_mux),
	SND_SOC_DAPM_MUX_E("WSA_RX1 MIX INP", CDC_WSA_RX1_RX_PATH_MIX_CTL,
			   0, 0, &rx1_mix_mux, wsa_macro_enable_mix_path,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("WSA_RX INT0 MIX", SND_SOC_NOPM, 0, 0, NULL, 0,
			     wsa_macro_enable_main_path, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MIXER_E("WSA_RX INT1 MIX", SND_SOC_NOPM, 1, 0, NULL, 0,
			     wsa_macro_enable_main_path, SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_MIXER("WSA_RX INT0 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("WSA_RX INT1 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("WSA_RX0 INT0 SIDETONE MIX", CDC_WSA_RX0_RX_PATH_CFG1,
			 4, 0, &rx0_sidetone_mix_mux),

	SND_SOC_DAPM_INPUT("WSA SRC0_INP"),
	SND_SOC_DAPM_INPUT("WSA_TX DEC0_INP"),
	SND_SOC_DAPM_INPUT("WSA_TX DEC1_INP"),

	SND_SOC_DAPM_MIXER_E("WSA_RX INT0 INTERP", SND_SOC_NOPM,
			     WSA_MACRO_COMP1, 0, NULL, 0,
			     wsa_macro_enable_interpolator,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			     SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("WSA_RX INT1 INTERP", SND_SOC_NOPM,
			     WSA_MACRO_COMP2, 0, NULL, 0,
			     wsa_macro_enable_interpolator,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			     SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("WSA_RX INT0 CHAIN", SND_SOC_NOPM, 0, 0,
			     NULL, 0, wsa_macro_spk_boost_event,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			     SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("WSA_RX INT1 CHAIN", SND_SOC_NOPM, 0, 0,
			     NULL, 0, wsa_macro_spk_boost_event,
			     SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			     SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_INPUT("VIINPUT_WSA"),
	SND_SOC_DAPM_OUTPUT("WSA_SPK1 OUT"),
	SND_SOC_DAPM_OUTPUT("WSA_SPK2 OUT"),

	SND_SOC_DAPM_SUPPLY("WSA_RX0_CLK", CDC_WSA_RX0_RX_PATH_CTL, 5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("WSA_RX1_CLK", CDC_WSA_RX1_RX_PATH_CTL, 5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("WSA_RX_MIX0_CLK", CDC_WSA_RX0_RX_PATH_MIX_CTL, 5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("WSA_RX_MIX1_CLK", CDC_WSA_RX1_RX_PATH_MIX_CTL, 5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("WSA_MCLK", 0, SND_SOC_NOPM, 0, 0,
			      wsa_macro_mclk_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route wsa_audio_map[] = {
	/* VI Feedback */
	{"WSA_AIF_VI Mixer", "WSA_SPKR_VI_1", "VIINPUT_WSA"},
	{"WSA_AIF_VI Mixer", "WSA_SPKR_VI_2", "VIINPUT_WSA"},
	{"WSA AIF_VI", NULL, "WSA_AIF_VI Mixer"},
	{"WSA AIF_VI", NULL, "WSA_MCLK"},

	{"WSA RX_MIX EC0_MUX", "RX_MIX_TX0", "WSA_RX INT0 SEC MIX"},
	{"WSA RX_MIX EC1_MUX", "RX_MIX_TX0", "WSA_RX INT0 SEC MIX"},
	{"WSA RX_MIX EC0_MUX", "RX_MIX_TX1", "WSA_RX INT1 SEC MIX"},
	{"WSA RX_MIX EC1_MUX", "RX_MIX_TX1", "WSA_RX INT1 SEC MIX"},
	{"WSA AIF_ECHO", NULL, "WSA RX_MIX EC0_MUX"},
	{"WSA AIF_ECHO", NULL, "WSA RX_MIX EC1_MUX"},
	{"WSA AIF_ECHO", NULL, "WSA_MCLK"},

	{"WSA AIF1 PB", NULL, "WSA_MCLK"},
	{"WSA AIF_MIX1 PB", NULL, "WSA_MCLK"},

	{"WSA RX0 MUX", "AIF1_PB", "WSA AIF1 PB"},
	{"WSA RX1 MUX", "AIF1_PB", "WSA AIF1 PB"},
	{"WSA RX_MIX0 MUX", "AIF1_PB", "WSA AIF1 PB"},
	{"WSA RX_MIX1 MUX", "AIF1_PB", "WSA AIF1 PB"},

	{"WSA RX0 MUX", "AIF_MIX1_PB", "WSA AIF_MIX1 PB"},
	{"WSA RX1 MUX", "AIF_MIX1_PB", "WSA AIF_MIX1 PB"},
	{"WSA RX_MIX0 MUX", "AIF_MIX1_PB", "WSA AIF_MIX1 PB"},
	{"WSA RX_MIX1 MUX", "AIF_MIX1_PB", "WSA AIF_MIX1 PB"},

	{"WSA RX0", NULL, "WSA RX0 MUX"},
	{"WSA RX1", NULL, "WSA RX1 MUX"},
	{"WSA RX_MIX0", NULL, "WSA RX_MIX0 MUX"},
	{"WSA RX_MIX1", NULL, "WSA RX_MIX1 MUX"},

	{"WSA RX0", NULL, "WSA_RX0_CLK"},
	{"WSA RX1", NULL, "WSA_RX1_CLK"},
	{"WSA RX_MIX0", NULL, "WSA_RX_MIX0_CLK"},
	{"WSA RX_MIX1", NULL, "WSA_RX_MIX1_CLK"},

	{"WSA_RX0 INP0", "RX0", "WSA RX0"},
	{"WSA_RX0 INP0", "RX1", "WSA RX1"},
	{"WSA_RX0 INP0", "RX_MIX0", "WSA RX_MIX0"},
	{"WSA_RX0 INP0", "RX_MIX1", "WSA RX_MIX1"},
	{"WSA_RX0 INP0", "DEC0", "WSA_TX DEC0_INP"},
	{"WSA_RX0 INP0", "DEC1", "WSA_TX DEC1_INP"},
	{"WSA_RX INT0 MIX", NULL, "WSA_RX0 INP0"},

	{"WSA_RX0 INP1", "RX0", "WSA RX0"},
	{"WSA_RX0 INP1", "RX1", "WSA RX1"},
	{"WSA_RX0 INP1", "RX_MIX0", "WSA RX_MIX0"},
	{"WSA_RX0 INP1", "RX_MIX1", "WSA RX_MIX1"},
	{"WSA_RX0 INP1", "DEC0", "WSA_TX DEC0_INP"},
	{"WSA_RX0 INP1", "DEC1", "WSA_TX DEC1_INP"},
	{"WSA_RX INT0 MIX", NULL, "WSA_RX0 INP1"},

	{"WSA_RX0 INP2", "RX0", "WSA RX0"},
	{"WSA_RX0 INP2", "RX1", "WSA RX1"},
	{"WSA_RX0 INP2", "RX_MIX0", "WSA RX_MIX0"},
	{"WSA_RX0 INP2", "RX_MIX1", "WSA RX_MIX1"},
	{"WSA_RX0 INP2", "DEC0", "WSA_TX DEC0_INP"},
	{"WSA_RX0 INP2", "DEC1", "WSA_TX DEC1_INP"},
	{"WSA_RX INT0 MIX", NULL, "WSA_RX0 INP2"},

	{"WSA_RX0 MIX INP", "RX0", "WSA RX0"},
	{"WSA_RX0 MIX INP", "RX1", "WSA RX1"},
	{"WSA_RX0 MIX INP", "RX_MIX0", "WSA RX_MIX0"},
	{"WSA_RX0 MIX INP", "RX_MIX1", "WSA RX_MIX1"},
	{"WSA_RX INT0 SEC MIX", NULL, "WSA_RX0 MIX INP"},

	{"WSA_RX INT0 SEC MIX", NULL, "WSA_RX INT0 MIX"},
	{"WSA_RX INT0 INTERP", NULL, "WSA_RX INT0 SEC MIX"},
	{"WSA_RX0 INT0 SIDETONE MIX", "SRC0", "WSA SRC0_INP"},
	{"WSA_RX INT0 INTERP", NULL, "WSA_RX0 INT0 SIDETONE MIX"},
	{"WSA_RX INT0 CHAIN", NULL, "WSA_RX INT0 INTERP"},

	{"WSA_SPK1 OUT", NULL, "WSA_RX INT0 CHAIN"},
	{"WSA_SPK1 OUT", NULL, "WSA_MCLK"},

	{"WSA_RX1 INP0", "RX0", "WSA RX0"},
	{"WSA_RX1 INP0", "RX1", "WSA RX1"},
	{"WSA_RX1 INP0", "RX_MIX0", "WSA RX_MIX0"},
	{"WSA_RX1 INP0", "RX_MIX1", "WSA RX_MIX1"},
	{"WSA_RX1 INP0", "DEC0", "WSA_TX DEC0_INP"},
	{"WSA_RX1 INP0", "DEC1", "WSA_TX DEC1_INP"},
	{"WSA_RX INT1 MIX", NULL, "WSA_RX1 INP0"},

	{"WSA_RX1 INP1", "RX0", "WSA RX0"},
	{"WSA_RX1 INP1", "RX1", "WSA RX1"},
	{"WSA_RX1 INP1", "RX_MIX0", "WSA RX_MIX0"},
	{"WSA_RX1 INP1", "RX_MIX1", "WSA RX_MIX1"},
	{"WSA_RX1 INP1", "DEC0", "WSA_TX DEC0_INP"},
	{"WSA_RX1 INP1", "DEC1", "WSA_TX DEC1_INP"},
	{"WSA_RX INT1 MIX", NULL, "WSA_RX1 INP1"},

	{"WSA_RX1 INP2", "RX0", "WSA RX0"},
	{"WSA_RX1 INP2", "RX1", "WSA RX1"},
	{"WSA_RX1 INP2", "RX_MIX0", "WSA RX_MIX0"},
	{"WSA_RX1 INP2", "RX_MIX1", "WSA RX_MIX1"},
	{"WSA_RX1 INP2", "DEC0", "WSA_TX DEC0_INP"},
	{"WSA_RX1 INP2", "DEC1", "WSA_TX DEC1_INP"},
	{"WSA_RX INT1 MIX", NULL, "WSA_RX1 INP2"},

	{"WSA_RX1 MIX INP", "RX0", "WSA RX0"},
	{"WSA_RX1 MIX INP", "RX1", "WSA RX1"},
	{"WSA_RX1 MIX INP", "RX_MIX0", "WSA RX_MIX0"},
	{"WSA_RX1 MIX INP", "RX_MIX1", "WSA RX_MIX1"},
	{"WSA_RX INT1 SEC MIX", NULL, "WSA_RX1 MIX INP"},

	{"WSA_RX INT1 SEC MIX", NULL, "WSA_RX INT1 MIX"},
	{"WSA_RX INT1 INTERP", NULL, "WSA_RX INT1 SEC MIX"},

	{"WSA_RX INT1 CHAIN", NULL, "WSA_RX INT1 INTERP"},
	{"WSA_SPK2 OUT", NULL, "WSA_RX INT1 CHAIN"},
	{"WSA_SPK2 OUT", NULL, "WSA_MCLK"},
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
	.dapm_widgets = wsa_macro_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wsa_macro_dapm_widgets),
	.dapm_routes = wsa_audio_map,
	.num_dapm_routes = ARRAY_SIZE(wsa_audio_map),
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
