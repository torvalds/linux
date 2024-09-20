/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt1318.h -- Platform data for RT1318
 *
 * Copyright 2024 Realtek Semiconductor Corp.
 */
#include <sound/rt1318.h>

#ifndef __RT1318_H__
#define __RT1318_H__

struct rt1318_priv {
	struct snd_soc_component *component;
	struct rt1318_platform_data pdata;
	struct work_struct cali_work;
	struct regmap *regmap;

	unsigned int r0_l_integer;
	unsigned int r0_l_factor;
	unsigned int r0_r_integer;
	unsigned int r0_r_factor;
	int rt1318_init;
	int rt1318_dvol;
	int sysclk_src;
	int sysclk;
	int lrck;
	int bclk;
	int master;
	int pll_src;
	int pll_in;
	int pll_out;
};

#define RT1318_PLL_INP_MAX	40000000
#define RT1318_PLL_INP_MIN	256000
#define RT1318_PLL_N_MAX	0x1ff
#define RT1318_PLL_K_MAX	0x1f
#define RT1318_PLL_M_MAX	0x1f

#define RT1318_LRCLK_192000 192000
#define RT1318_LRCLK_96000 96000
#define RT1318_LRCLK_48000 48000
#define RT1318_LRCLK_44100 44100
#define RT1318_LRCLK_16000 16000
#define RT1318_DVOL_STEP 383

#define RT1318_CLK1				0xc001
#define RT1318_CLK2				0xc003
#define RT1318_CLK3				0xc004
#define RT1318_CLK4				0xc005
#define RT1318_CLK5				0xc006
#define RT1318_CLK6				0xc007
#define RT1318_CLK7				0xc008
#define RT1318_PWR_STA1				0xc121
#define RT1318_SPK_VOL_TH			0xc130
#define RT1318_TCON				0xc203
#define RT1318_SRC_TCON				0xc204
#define RT1318_TCON_RELATE			0xc206
#define RT1318_DA_VOL_L_8			0xc20b
#define RT1318_DA_VOL_L_1_7			0xc20c
#define RT1318_DA_VOL_R_8			0xc20d
#define RT1318_DA_VOL_R_1_7			0xc20e
#define RT1318_FEEDBACK_PATH			0xc321
#define RT1318_STP_TEMP_L			0xdb00
#define RT1318_STP_SEL_L			0xdb08
#define RT1318_STP_R0_EN_L			0xdb12
#define RT1318_R0_CMP_L_FLAG			0xdb35
#define RT1318_PRE_R0_L_24			0xdbb5
#define RT1318_PRE_R0_L_23_16			0xdbb6
#define RT1318_PRE_R0_L_15_8			0xdbb7
#define RT1318_PRE_R0_L_7_0			0xdbb8
#define RT1318_R0_L_24				0xdbc5
#define RT1318_R0_L_23_16			0xdbc6
#define RT1318_R0_L_15_8			0xdbc7
#define RT1318_R0_L_7_0				0xdbc8
#define RT1318_STP_SEL_R			0xdd08
#define RT1318_STP_R0_EN_R			0xdd12
#define RT1318_R0_CMP_R_FLAG			0xdd35
#define RT1318_PRE_R0_R_24			0xddb5
#define RT1318_PRE_R0_R_23_16			0xddb6
#define RT1318_PRE_R0_R_15_8			0xddb7
#define RT1318_PRE_R0_R_7_0			0xddb8
#define RT1318_R0_R_24				0xddc5
#define RT1318_R0_R_23_16			0xddc6
#define RT1318_R0_R_15_8			0xddc7
#define RT1318_R0_R_7_0				0xddc8
#define RT1318_DEV_ID1				0xf012
#define RT1318_DEV_ID2				0xf013
#define RT1318_PLL1_K				0xf20d
#define RT1318_PLL1_M				0xf20f
#define RT1318_PLL1_N_8				0xf211
#define RT1318_PLL1_N_7_0			0xf212
#define RT1318_SINE_GEN0			0xf800
#define RT1318_TDM_CTRL1			0xf900
#define RT1318_TDM_CTRL2			0xf901
#define RT1318_TDM_CTRL3			0xf902
#define RT1318_TDM_CTRL9			0xf908


/* Clock-1  (0xC001) */
#define RT1318_PLLIN_MASK			(0x7 << 4)
#define RT1318_PLLIN_BCLK0			(0x0 << 4)
#define RT1318_PLLIN_BCLK1			(0x1 << 4)
#define RT1318_PLLIN_RC				(0x2 << 4)
#define RT1318_PLLIN_MCLK			(0x3 << 4)
#define RT1318_PLLIN_SDW1			(0x4 << 4)
#define RT1318_PLLIN_SDW2			(0x5 << 4)
#define RT1318_PLLIN_SDW3			(0x6 << 4)
#define RT1318_PLLIN_SDW4			(0x7 << 4)
#define RT1318_SYSCLK_SEL_MASK			(0x7 << 0)
#define RT1318_SYSCLK_BCLK			(0x0 << 0)
#define RT1318_SYSCLK_SDW			(0x1 << 0)
#define RT1318_SYSCLK_PLL2F			(0x2 << 0)
#define RT1318_SYSCLK_PLL2B			(0x3 << 0)
#define RT1318_SYSCLK_MCLK			(0x4 << 0)
#define RT1318_SYSCLK_RC1			(0x5 << 0)
#define RT1318_SYSCLK_RC2			(0x6 << 0)
#define RT1318_SYSCLK_RC3			(0x7 << 0)
/* Clock-2  (0xC003) */
#define RT1318_DIV_AP_MASK			(0x3 << 4)
#define RT1318_DIV_AP_SFT			4
#define RT1318_DIV_AP_DIV1			(0x0 << 4)
#define RT1318_DIV_AP_DIV2			(0x1 << 4)
#define RT1318_DIV_AP_DIV4			(0x2 << 4)
#define RT1318_DIV_AP_DIV8			(0x3 << 4)
#define RT1318_DIV_DAMOD_MASK			(0x3 << 0)
#define RT1318_DIV_DAMOD_SFT			0
#define RT1318_DIV_DAMOD_DIV1			(0x0 << 0)
#define RT1318_DIV_DAMOD_DIV2			(0x1 << 0)
#define RT1318_DIV_DAMOD_DIV4			(0x2 << 0)
#define RT1318_DIV_DAMOD_DIV8			(0x3 << 0)
/* Clock-3  (0xC004) */
#define RT1318_AD_STO1_MASK			(0x7 << 4)
#define RT1318_AD_STO1_SFT			4
#define RT1318_AD_STO1_DIV1			(0x0 << 4)
#define RT1318_AD_STO1_DIV2			(0x1 << 4)
#define RT1318_AD_STO1_DIV4			(0x2 << 4)
#define RT1318_AD_STO1_DIV8			(0x3 << 4)
#define RT1318_AD_STO1_DIV16			(0x4 << 4)
#define RT1318_AD_STO2_MASK			(0x7 << 0)
#define RT1318_AD_STO2_SFT			0
#define RT1318_AD_STO2_DIV1			(0x0 << 0)
#define RT1318_AD_STO2_DIV2			(0x1 << 0)
#define RT1318_AD_STO2_DIV4			(0x2 << 0)
#define RT1318_AD_STO2_DIV8			(0x3 << 0)
#define RT1318_AD_STO2_DIV16			(0x4 << 0)
#define RT1318_AD_STO2_SFT			0
/* Clock-4  (0xC005) */
#define RT1318_AD_ANA_STO1_MASK			(0x7 << 4)
#define RT1318_AD_ANA_STO1_SFT			4
#define RT1318_AD_ANA_STO1_DIV1			(0x0 << 4)
#define RT1318_AD_ANA_STO1_DIV2			(0x1 << 4)
#define RT1318_AD_ANA_STO1_DIV4			(0x2 << 4)
#define RT1318_AD_ANA_STO1_DIV8			(0x3 << 4)
#define RT1318_AD_ANA_STO1_DIV16		(0x4 << 4)
#define RT1318_AD_ANA_STO2_MASK			(0x7 << 0)
#define RT1318_AD_ANA_STO2_DIV1			(0x0 << 0)
#define RT1318_AD_ANA_STO2_DIV2			(0x1 << 0)
#define RT1318_AD_ANA_STO2_DIV4			(0x2 << 0)
#define RT1318_AD_ANA_STO2_DIV8			(0x3 << 0)
#define RT1318_AD_ANA_STO2_DIV16		(0x4 << 0)
#define RT1318_AD_ANA_STO2_SFT			0
/* Clock-5  (0xC006) */
#define RT1318_DIV_FIFO_IN_MASK			(0x3 << 4)
#define RT1318_DIV_FIFO_IN_SFT			4
#define RT1318_DIV_FIFO_IN_DIV1			(0x0 << 4)
#define RT1318_DIV_FIFO_IN_DIV2			(0x1 << 4)
#define RT1318_DIV_FIFO_IN_DIV4			(0x2 << 4)
#define RT1318_DIV_FIFO_IN_DIV8			(0x3 << 4)
#define RT1318_DIV_FIFO_OUT_MASK		(0x3 << 0)
#define RT1318_DIV_FIFO_OUT_DIV1		(0x0 << 0)
#define RT1318_DIV_FIFO_OUT_DIV2		(0x1 << 0)
#define RT1318_DIV_FIFO_OUT_DIV4		(0x2 << 0)
#define RT1318_DIV_FIFO_OUT_DIV8		(0x3 << 0)
#define RT1318_DIV_FIFO_OUT_SFT			0
/* Clock-6  (0xC007) */
#define RT1318_DIV_NLMS_MASK			(0x3 << 6)
#define RT1318_DIV_NLMS_SFT				6
#define RT1318_DIV_NLMS_DIV1			(0x0 << 6)
#define RT1318_DIV_NLMS_DIV2			(0x1 << 6)
#define RT1318_DIV_NLMS_DIV4			(0x2 << 6)
#define RT1318_DIV_NLMS_DIV8			(0x3 << 6)
#define RT1318_DIV_AD_MONO_MASK			(0x7 << 3)
#define RT1318_DIV_AD_MONO_SFT			3
#define RT1318_DIV_AD_MONO_DIV1			(0x0 << 3)
#define RT1318_DIV_AD_MONO_DIV2			(0x1 << 3)
#define RT1318_DIV_AD_MONO_DIV4			(0x2 << 3)
#define RT1318_DIV_AD_MONO_DIV8			(0x3 << 3)
#define RT1318_DIV_AD_MONO_DIV16		(0x4 << 3)
#define RT1318_DIV_POST_G_MASK			(0x7 << 0)
#define RT1318_DIV_POST_G_SFT			0
#define RT1318_DIV_POST_G_DIV1			(0x0 << 0)
#define RT1318_DIV_POST_G_DIV2			(0x1 << 0)
#define RT1318_DIV_POST_G_DIV4			(0x2 << 0)
#define RT1318_DIV_POST_G_DIV8			(0x3 << 0)
#define RT1318_DIV_POST_G_DIV16			(0x4 << 0)
/* Power Status 1  (0xC121) */
#define RT1318_PDB_CTRL_MASK			(0x1)
#define RT1318_PDB_CTRL_LOW			(0x0)
#define RT1318_PDB_CTRL_HIGH			(0x1)
#define RT1318_PDB_CTRL_SFT			0
/* SRC Tcon(0xc204) */
#define RT1318_SRCIN_IN_SEL_MASK		(0x3 << 6)
#define RT1318_SRCIN_IN_48K			(0x0 << 6)
#define RT1318_SRCIN_IN_44P1			(0x1 << 6)
#define RT1318_SRCIN_IN_32K			(0x2 << 6)
#define RT1318_SRCIN_IN_16K			(0x3 << 6)
#define RT1318_SRCIN_F12288_MASK		(0x3 << 4)
#define RT1318_SRCIN_TCON1			(0x0 << 4)
#define RT1318_SRCIN_TCON2			(0x1 << 4)
#define RT1318_SRCIN_TCON4			(0x2 << 4)
#define RT1318_SRCIN_TCON8			(0x3 << 4)
#define RT1318_SRCIN_DACLK_MASK			(0x3 << 2)
#define RT1318_DACLK_TCON1			(0x0 << 2)
#define RT1318_DACLK_TCON2			(0x1 << 2)
#define RT1318_DACLK_TCON4			(0x2 << 2)
#define RT1318_DACLK_TCON8			(0x3 << 2)
/* R0 Compare Flag  (0xDB35) */
#define RT1318_R0_RANGE_MASK			(0x1)
#define RT1318_R0_OUTOFRANGE			(0x0)
#define RT1318_R0_INRANGE			(0x1)
/* PLL internal setting (0xF20D), K value */
#define RT1318_K_PLL1_MASK			(0x1f << 0)
/* PLL internal setting (0xF20F), M value */
#define RT1318_M_PLL1_MASK			(0x1f << 0)
/* PLL internal setting (0xF211), N_8 value */
#define RT1318_N_8_PLL1_MASK			(0x1 << 0)
/* PLL internal setting (0xF212), N_7_0 value */
#define RT1318_N_7_0_PLL1_MASK			(0xff << 0)
/* TDM CTRL 1  (0xf900) */
#define RT1318_TDM_BCLK_MASK			(0x1 << 7)
#define RT1318_TDM_BCLK_NORM			(0x0 << 7)
#define RT1318_TDM_BCLK_INV			(0x1 << 7)
#define RT1318_I2S_FMT_MASK			(0x7 << 0)
#define RT1318_FMT_I2S				(0x0 << 0)
#define RT1318_FMT_LEFT_J			(0x1 << 0)
#define RT1318_FMT_PCM_A_R			(0x2 << 0)
#define RT1318_FMT_PCM_B_R			(0x3 << 0)
#define RT1318_FMT_PCM_A_F			(0x6 << 0)
#define RT1318_FMT_PCM_B_F			(0x7 << 0)
#define RT1318_I2S_FMT_SFT			0
/* TDM CTRL 2  (0xf901) */
#define RT1318_I2S_CH_TX_MASK			(0x3 << 6)
#define RT1318_I2S_CH_TX_2CH			(0x0 << 6)
#define RT1318_I2S_CH_TX_4CH			(0x1 << 6)
#define RT1318_I2S_CH_TX_6CH			(0x2 << 6)
#define RT1318_I2S_CH_TX_8CH			(0x3 << 6)
#define RT1318_I2S_CH_RX_MASK			(0x3 << 4)
#define RT1318_I2S_CH_RX_2CH			(0x0 << 4)
#define RT1318_I2S_CH_RX_4CH			(0x1 << 4)
#define RT1318_I2S_CH_RX_6CH			(0x2 << 4)
#define RT1318_I2S_CH_RX_8CH			(0x3 << 4)
#define RT1318_I2S_DL_MASK			0x7
#define RT1318_I2S_DL_SFT			0
#define RT1318_I2S_DL_16			0x0
#define RT1318_I2S_DL_20			0x1
#define RT1318_I2S_DL_24			0x2
#define RT1318_I2S_DL_32			0x3
#define RT1318_I2S_DL_8				0x4
/* TDM CTRL 3  (0xf902) */
#define RT1318_I2S_TX_CHL_MASK			(0x7 << 4)
#define RT1318_I2S_TX_CHL_SFT			4
#define RT1318_I2S_TX_CHL_16			(0x0 << 4)
#define RT1318_I2S_TX_CHL_20			(0x1 << 4)
#define RT1318_I2S_TX_CHL_24			(0x2 << 4)
#define RT1318_I2S_TX_CHL_32			(0x3 << 4)
#define RT1318_I2S_TX_CHL_8			(0x4 << 4)
#define RT1318_I2S_RX_CHL_MASK			(0x7 << 0)
#define RT1318_I2S_RX_CHL_SFT			0
#define RT1318_I2S_RX_CHL_16			(0x0 << 0)
#define RT1318_I2S_RX_CHL_20			(0x1 << 0)
#define RT1318_I2S_RX_CHL_24			(0x2 << 0)
#define RT1318_I2S_RX_CHL_32			(0x3 << 0)
#define RT1318_I2S_RX_CHL_8			(0x4 << 0)
/* TDM CTRL 9  (0xf908) */
#define RT1318_TDM_I2S_TX_L_DAC1_1_MASK		(0x7 << 4)
#define RT1318_TDM_I2S_TX_R_DAC1_1_MASK		0x7
#define RT1318_TDM_I2S_TX_L_DAC1_1_SFT		4
#define RT1318_TDM_I2S_TX_R_DAC1_1_SFT		0

#define RT1318_REG_DISP_LEN 23

/* System Clock Source */
enum {
	RT1318_SCLK_S_BCLK,
	RT1318_SCLK_S_SDW,
	RT1318_SCLK_S_PLL2F,
	RT1318_SCLK_S_PLL2B,
	RT1318_SCLK_S_MCLK,
	RT1318_SCLK_S_RC0,
	RT1318_SCLK_S_RC1,
	RT1318_SCLK_S_RC2,
};

/* PLL Source */
enum {
	RT1318_PLL_S_BCLK0,
	RT1318_PLL_S_BCLK1,
	RT1318_PLL_S_RC,
	RT1318_PLL_S_MCLK,
	RT1318_PLL_S_SDW_IN_PLL,
	RT1318_PLL_S_SDW_0,
	RT1318_PLL_S_SDW_1,
	RT1318_PLL_S_SDW_2,
};

/* TDM channel */
enum {
	RT1318_2CH,
	RT1318_4CH,
	RT1318_6CH,
	RT1318_8CH,
};

/* R0 calibration result */
enum {
	RT1318_R0_OUT_OF_RANGE,
	RT1318_R0_IN_RANGE,
	RT1318_R0_CALIB_NOT_DONE,
};

/* PLL pre-defined M/N/K */

struct pll_calc_map {
	unsigned int pll_in;
	unsigned int pll_out;
	int k;
	int n;
	int m;
	bool m_bp;
	bool k_bp;
};

struct rt1318_pll_code {
	bool m_bp; /* Indicates bypass m code or not. */
	bool k_bp; /* Indicates bypass k code or not. */
	int m_code;
	int n_code;
	int k_code;
};

#endif /* __RT1318_H__ */
