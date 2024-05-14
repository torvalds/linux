/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt1019.h  --  RT1019 ALSA SoC audio amplifier driver
 *
 * Copyright(c) 2021 Realtek Semiconductor Corp.
 */

#ifndef __RT1019_H__
#define __RT1019_H__

#define RT1019_DEVICE_ID_VAL			0x1019
#define RT1019_DEVICE_ID_VAL2			0x6731

#define RT1019_RESET				0x0000
#define RT1019_IDS_CTRL				0x0011
#define RT1019_ASEL_CTRL			0x0013
#define RT1019_PWR_STRP_2			0x0019
#define RT1019_BEEP_TONE			0x001b
#define RT1019_VER_ID				0x005c
#define RT1019_VEND_ID_1			0x005e
#define RT1019_VEND_ID_2			0x005f
#define RT1019_DEV_ID_1				0x0061
#define RT1019_DEV_ID_2				0x0062
#define RT1019_SDB_CTRL				0x0066
#define RT1019_CLK_TREE_1			0x0100
#define RT1019_CLK_TREE_2			0x0101
#define RT1019_CLK_TREE_3			0x0102
#define RT1019_PLL_1				0x0311
#define RT1019_PLL_2				0x0312
#define RT1019_PLL_3				0x0313
#define RT1019_TDM_1				0x0400
#define RT1019_TDM_2				0x0401
#define RT1019_TDM_3				0x0402
#define RT1019_DMIX_MONO_1			0x0504
#define RT1019_DMIX_MONO_2			0x0505
#define RT1019_BEEP_1				0x0b00
#define RT1019_BEEP_2				0x0b01

/* 0x0019 Power On Strap Control-2 */
#define RT1019_AUTO_BITS_SEL_MASK		(0x1 << 5)
#define RT1019_AUTO_BITS_SEL_AUTO		(0x1 << 5)
#define RT1019_AUTO_BITS_SEL_MANU		(0x0 << 5)
#define RT1019_AUTO_CLK_SEL_MASK		(0x1 << 4)
#define RT1019_AUTO_CLK_SEL_AUTO		(0x1 << 4)
#define RT1019_AUTO_CLK_SEL_MANU		(0x0 << 4)

/* 0x0100 Clock Tree Control-1 */
#define RT1019_CLK_SYS_PRE_SEL_MASK		(0x1 << 7)
#define RT1019_CLK_SYS_PRE_SEL_SFT		7
#define RT1019_CLK_SYS_PRE_SEL_BCLK		(0x0 << 7)
#define RT1019_CLK_SYS_PRE_SEL_PLL		(0x1 << 7)
#define RT1019_PLL_SRC_MASK				(0x1 << 4)
#define RT1019_PLL_SRC_SFT				4
#define RT1019_PLL_SRC_SEL_BCLK			(0x0 << 4)
#define RT1019_PLL_SRC_SEL_RC			(0x1 << 4)
#define RT1019_SEL_FIFO_MASK			(0x3 << 2)
#define RT1019_SEL_FIFO_DIV1			(0x0 << 2)
#define RT1019_SEL_FIFO_DIV2			(0x1 << 2)
#define RT1019_SEL_FIFO_DIV4			(0x2 << 2)

/* 0x0101 clock tree control-2 */
#define RT1019_SYS_DIV_DA_FIL_MASK		(0x7 << 5)
#define RT1019_SYS_DIV_DA_FIL_DIV1		(0x2 << 5)
#define RT1019_SYS_DIV_DA_FIL_DIV2		(0x3 << 5)
#define RT1019_SYS_DIV_DA_FIL_DIV4		(0x4 << 5)
#define RT1019_SYS_DA_OSR_MASK			(0x3 << 2)
#define RT1019_SYS_DA_OSR_DIV1			(0x0 << 2)
#define RT1019_SYS_DA_OSR_DIV2			(0x1 << 2)
#define RT1019_SYS_DA_OSR_DIV4			(0x2 << 2)
#define RT1019_ASRC_256FS_MASK			0x3
#define RT1019_ASRC_256FS_DIV1			0x0
#define RT1019_ASRC_256FS_DIV2			0x1
#define RT1019_ASRC_256FS_DIV4			0x2

/* 0x0102 clock tree control-3 */
#define RT1019_SEL_CLK_CAL_MASK			(0x3 << 6)
#define RT1019_SEL_CLK_CAL_DIV1			(0x0 << 6)
#define RT1019_SEL_CLK_CAL_DIV2			(0x1 << 6)
#define RT1019_SEL_CLK_CAL_DIV4			(0x2 << 6)

/* 0x0311 PLL-1 */
#define RT1019_PLL_M_MASK			(0xf << 4)
#define RT1019_PLL_M_SFT			4
#define RT1019_PLL_M_BP_MASK		(0x1 << 1)
#define RT1019_PLL_M_BP_SFT			1
#define RT1019_PLL_Q_8_8_MASK		(0x1)

/* 0x0312 PLL-2 */
#define RT1019_PLL_Q_7_0_MASK		0xff

/* 0x0313 PLL-3 */
#define RT1019_PLL_K_MASK		0x1f

/* 0x0400 TDM Control-1 */
#define RT1019_TDM_BCLK_MASK		(0x1 << 6)
#define RT1019_TDM_BCLK_NORM		(0x0 << 6)
#define RT1019_TDM_BCLK_INV			(0x1 << 6)
#define RT1019_TDM_CL_MASK			(0x7)
#define RT1019_TDM_CL_8				(0x4)
#define RT1019_TDM_CL_32			(0x3)
#define RT1019_TDM_CL_24			(0x2)
#define RT1019_TDM_CL_20			(0x1)
#define RT1019_TDM_CL_16			(0x0)

/* 0x0401 TDM Control-2 */
#define RT1019_I2S_CH_TX_MASK		(0x3 << 6)
#define RT1019_I2S_CH_TX_SFT		6
#define RT1019_I2S_TX_2CH			(0x0 << 6)
#define RT1019_I2S_TX_4CH			(0x1 << 6)
#define RT1019_I2S_TX_6CH			(0x2 << 6)
#define RT1019_I2S_TX_8CH			(0x3 << 6)
#define RT1019_I2S_DF_MASK			(0x7 << 3)
#define RT1019_I2S_DF_SFT			3
#define RT1019_I2S_DF_I2S			(0x0 << 3)
#define RT1019_I2S_DF_LEFT			(0x1 << 3)
#define RT1019_I2S_DF_PCM_A_R		(0x2 << 3)
#define RT1019_I2S_DF_PCM_B_R		(0x3 << 3)
#define RT1019_I2S_DF_PCM_A_F		(0x6 << 3)
#define RT1019_I2S_DF_PCM_B_F		(0x7 << 3)
#define RT1019_I2S_DL_MASK			0x7
#define RT1019_I2S_DL_SFT			0
#define RT1019_I2S_DL_16			0x0
#define RT1019_I2S_DL_20			0x1
#define RT1019_I2S_DL_24			0x2
#define RT1019_I2S_DL_32			0x3
#define RT1019_I2S_DL_8				0x4

/* TDM1 Control-3 (0x0402) */
#define RT1019_TDM_I2S_TX_L_DAC1_1_MASK		(0x7 << 4)
#define RT1019_TDM_I2S_TX_R_DAC1_1_MASK		0x7
#define RT1019_TDM_I2S_TX_L_DAC1_1_SFT		4
#define RT1019_TDM_I2S_TX_R_DAC1_1_SFT		0

/* System Clock Source */
enum {
	RT1019_SCLK_S_BCLK,
	RT1019_SCLK_S_PLL,
};

/* PLL1 Source */
enum {
	RT1019_PLL_S_BCLK,
	RT1019_PLL_S_RC25M,
};

enum {
	RT1019_AIF1,
	RT1019_AIFS
};

struct rt1019_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;
	int sysclk;
	int sysclk_src;
	int lrck;
	int bclk;
	int pll_src;
	int pll_in;
	int pll_out;
	unsigned int bclk_ratio;
};

#endif /* __RT1019_H__ */
