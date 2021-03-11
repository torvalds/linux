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
#define RT1019_PAD_DRV_1			0x0002
#define RT1019_PAD_DRV_2			0x0003
#define RT1019_PAD_PULL_1			0x0005
#define RT1019_PAD_PULL_2			0x0006
#define RT1019_PAD_PULL_3			0x0007
#define RT1019_I2C_CTRL_1			0x0008
#define RT1019_I2C_CTRL_2			0x0009
#define RT1019_I2C_CTRL_3			0x000a
#define RT1019_IDS_CTRL				0x0011
#define RT1019_ASEL_CTRL			0x0013
#define RT1019_PLL_RESET			0x0015
#define RT1019_PWR_STRP_1			0x0017
#define RT1019_PWR_STRP_2			0x0019
#define RT1019_BEEP_TONE			0x001b
#define RT1019_SIL_DET_GAT			0x001d
#define RT1019_CLASSD_TIME			0x001f
#define RT1019_CLASSD_OCP			0x0021
#define RT1019_PHASE_SYNC			0x0023
#define RT1019_STAT_MACH_1			0x0025
#define RT1019_STAT_MACH_2			0x0026
#define RT1019_EFF_CTRL				0x0028
#define RT1019_FS_DET_1				0x002a
#define RT1019_FS_DET_2				0x002b
#define RT1019_FS_DET_3				0x002c
#define RT1019_FS_DET_4				0x002d
#define RT1019_FS_DET_5				0x002e
#define RT1019_FS_DET_6				0x002f
#define RT1019_FS_DET_7				0x0030
#define RT1019_ANA_CTRL				0x0053
#define RT1019_DUMMY_A				0x0055
#define RT1019_DUMMY_B				0x0056
#define RT1019_DUMMY_C				0x0057
#define RT1019_DUMMY_D				0x0058
#define RT1019_ANA_READ				0x005a
#define RT1019_VER_ID				0x005c
#define RT1019_CUSTOM_ID			0x005d
#define RT1019_VEND_ID_1			0x005e
#define RT1019_VEND_ID_2			0x005f
#define RT1019_DEV_ID_1				0x0061
#define RT1019_DEV_ID_2				0x0062
#define RT1019_TEST_PAD				0x0064
#define RT1019_SDB_CTRL				0x0066
#define RT1019_TEST_CTRL_1			0x0068
#define RT1019_TEST_CTRL_2			0x006a
#define RT1019_TEST_CTRL_3			0x006c
#define RT1019_SCAN_MODE			0x006e
#define RT1019_CLK_TREE_1			0x0100
#define RT1019_CLK_TREE_2			0x0101
#define RT1019_CLK_TREE_3			0x0102
#define RT1019_CLK_TREE_4			0x0103
#define RT1019_CLK_TREE_5			0x0104
#define RT1019_CLK_TREE_6			0x0105
#define RT1019_CLK_TREE_7			0x0106
#define RT1019_CLK_TREE_8			0x0107
#define RT1019_CLK_TREE_9			0x0108
#define RT1019_ASRC_1				0x0200
#define RT1019_ASRC_2				0x0201
#define RT1019_ASRC_3				0x0202
#define RT1019_ASRC_4				0x0203
#define RT1019_SYS_CLK				0x0300
#define RT1019_BIAS_CUR_1			0x0301
#define RT1019_BIAS_CUR_2			0x0302
#define RT1019_BIAS_CUR_3			0x0303
#define RT1019_BIAS_CUR_4			0x0304
#define RT1019_CHOP_CLK_DAC			0x0306
#define RT1019_CHOP_CLK_ADC			0x0308
#define RT1019_LDO_CTRL_1			0x030a
#define RT1019_LDO_CTRL_2			0x030b
#define RT1019_PM_ANA_1				0x030d
#define RT1019_PM_ANA_2				0x030e
#define RT1019_PM_ANA_3				0x030f
#define RT1019_PLL_1				0x0311
#define RT1019_PLL_2				0x0312
#define RT1019_PLL_3				0x0313
#define RT1019_PLL_INT_1			0x0315
#define RT1019_PLL_INT_3			0x0318
#define RT1019_MIXER				0x031a
#define RT1019_CLD_OUT_1			0x031c
#define RT1019_CLD_OUT_2			0x031d
#define RT1019_CLD_OUT_3			0x031e
#define RT1019_CLD_OUT_4			0x031f
#define RT1019_CLD_OUT_5			0x0320
#define RT1019_CLD_OUT_6			0x0321
#define RT1019_CLS_INT_REG_1		0x0323
#define RT1019_CLS_INT_REG_2		0x0324
#define RT1019_CLS_INT_REG_3		0x0325
#define RT1019_CLS_INT_REG_4		0x0326
#define RT1019_CLS_INT_REG_5		0x0327
#define RT1019_CLS_INT_REG_6		0x0328
#define RT1019_CLS_INT_REG_7		0x0329
#define RT1019_CLS_INT_REG_8		0x0330
#define RT1019_CLS_INT_REG_9		0x0331
#define RT1019_CLS_INT_REG_10		0x0332
#define RT1019_TDM_1				0x0400
#define RT1019_TDM_2				0x0401
#define RT1019_TDM_3				0x0402
#define RT1019_TDM_4				0x0403
#define RT1019_TDM_5				0x0404
#define RT1019_TDM_6				0x0405
#define RT1019_DVOL_1				0x0500
#define RT1019_DVOL_2				0x0501
#define RT1019_DVOL_3				0x0502
#define RT1019_DVOL_4				0x0503
#define RT1019_DMIX_MONO_1			0x0504
#define RT1019_DMIX_MONO_2			0x0505
#define RT1019_CAL_TOP_1			0x0600
#define RT1019_CAL_TOP_2			0x0601
#define RT1019_CAL_TOP_3			0x0602
#define RT1019_CAL_TOP_4			0x0603
#define RT1019_CAL_TOP_5			0x0604
#define RT1019_CAL_TOP_6			0x0605
#define RT1019_CAL_TOP_7			0x0606
#define RT1019_CAL_TOP_8			0x0607
#define RT1019_CAL_TOP_9			0x0608
#define RT1019_CAL_TOP_10			0x0609
#define RT1019_CAL_TOP_11			0x060a
#define RT1019_CAL_TOP_12			0x060b
#define RT1019_CAL_TOP_13			0x060c
#define RT1019_CAL_TOP_14			0x060d
#define RT1019_CAL_TOP_15			0x060e
#define RT1019_CAL_TOP_16			0x060f
#define RT1019_CAL_TOP_17			0x0610
#define RT1019_CAL_TOP_18			0x0611
#define RT1019_CAL_TOP_19			0x0612
#define RT1019_CAL_TOP_20			0x0613
#define RT1019_CAL_TOP_21			0x0614
#define RT1019_CAL_TOP_22			0x0615
#define RT1019_MDRE_CTRL_1			0x0700
#define RT1019_MDRE_CTRL_2			0x0701
#define RT1019_MDRE_CTRL_3			0x0702
#define RT1019_MDRE_CTRL_4			0x0703
#define RT1019_MDRE_CTRL_5			0x0704
#define RT1019_MDRE_CTRL_6			0x0705
#define RT1019_MDRE_CTRL_7			0x0706
#define RT1019_MDRE_CTRL_8			0x0707
#define RT1019_MDRE_CTRL_9			0x0708
#define RT1019_MDRE_CTRL_10			0x0709
#define RT1019_SCC_CTRL_1			0x0800
#define RT1019_SCC_CTRL_2			0x0801
#define RT1019_SCC_CTRL_3			0x0802
#define RT1019_SCC_DUMMY			0x0803
#define RT1019_SIL_DET_1			0x0900
#define RT1019_SIL_DET_2			0x0901
#define RT1019_PWM_DC_DET_1			0x0a00
#define RT1019_PWM_DC_DET_2			0x0a01
#define RT1019_PWM_DC_DET_3			0x0a02
#define RT1019_PWM_DC_DET_4			0x0a03
#define RT1019_BEEP_1				0x0b00
#define RT1019_BEEP_2				0x0b01
#define RT1019_PMC_1				0x0c00
#define RT1019_PMC_2				0x0c01
#define RT1019_PMC_3				0x0c02
#define RT1019_PMC_4				0x0c03
#define RT1019_PMC_5				0x0c04
#define RT1019_PMC_6				0x0c05
#define RT1019_PMC_7				0x0c06
#define RT1019_PMC_8				0x0c07
#define RT1019_PMC_9				0x0c08
#define RT1019_SPKDRC_1				0x0d00
#define RT1019_SPKDRC_2				0x0d01
#define RT1019_SPKDRC_3				0x0d02
#define RT1019_SPKDRC_4				0x0d03
#define RT1019_SPKDRC_5				0x0d04
#define RT1019_SPKDRC_6				0x0d05
#define RT1019_SPKDRC_7				0x0d06
#define RT1019_HALF_FREQ_1			0x0e00
#define RT1019_HALF_FREQ_2			0x0e01
#define RT1019_HALF_FREQ_3			0x0e02
#define RT1019_HALF_FREQ_4			0x0e03
#define RT1019_HALF_FREQ_5			0x0e04
#define RT1019_HALF_FREQ_6			0x0e05
#define RT1019_HALF_FREQ_7			0x0e06
#define RT1019_CUR_CTRL_1			0x0f00
#define RT1019_CUR_CTRL_2			0x0f01
#define RT1019_CUR_CTRL_3			0x0f02
#define RT1019_CUR_CTRL_4			0x0f03
#define RT1019_CUR_CTRL_5			0x0f04
#define RT1019_CUR_CTRL_6			0x0f05
#define RT1019_CUR_CTRL_7			0x0f06
#define RT1019_CUR_CTRL_8			0x0f07
#define RT1019_CUR_CTRL_9			0x0f08
#define RT1019_CUR_CTRL_10			0x0f09
#define RT1019_CUR_CTRL_11			0x0f0a
#define RT1019_CUR_CTRL_12			0x0f0b
#define RT1019_CUR_CTRL_13			0x0f0c

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
