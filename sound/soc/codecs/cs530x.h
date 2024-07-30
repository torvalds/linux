/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CS530x CODEC driver internal data
 *
 * Copyright (C) 2023-2024 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#ifndef _CS530X_H
#define _CS530X_H

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

/* Devices */
#define CS530X_2CH_ADC_DEV_ID		 0x5302
#define CS530X_4CH_ADC_DEV_ID		 0x5304
#define CS530X_8CH_ADC_DEV_ID		 0x5308

/* Registers */

#define CS530X_DEVID			0x0000000
#define CS530X_REVID			0x0000004
#define CS530X_SW_RESET			0x0000022

#define CS530X_CLK_CFG_0		0x0000040
#define CS530X_CLK_CFG_1		0x0000042
#define CS530X_CHIP_ENABLE		0x0000044
#define CS530X_ASP_CFG			0x0000048
#define CS530X_SIGNAL_PATH_CFG		0x0000050
#define CS530X_IN_ENABLES		0x0000080
#define CS530X_IN_RAMP_SUM		0x0000082
#define CS530X_IN_FILTER		0x0000086
#define CS530X_IN_HIZ			0x0000088
#define CS530X_IN_INV			0x000008A
#define CS530X_IN_VOL_CTRL1_0	        0x0000090
#define CS530X_IN_VOL_CTRL1_1	        0x0000092
#define CS530X_IN_VOL_CTRL2_0	        0x0000094
#define CS530X_IN_VOL_CTRL2_1	        0x0000096
#define CS530X_IN_VOL_CTRL3_0	        0x0000098
#define CS530X_IN_VOL_CTRL3_1	        0x000009A
#define CS530X_IN_VOL_CTRL4_0	        0x000009C
#define CS530X_IN_VOL_CTRL4_1	        0x000009E
#define CS530X_IN_VOL_CTRL5		0x00000A0

#define CS530X_PAD_FN			0x0003D24
#define CS530X_PAD_LVL			0x0003D28

#define CS530X_MAX_REGISTER		CS530X_PAD_LVL

/* Register Fields */

/* REVID */
#define CS530X_MTLREVID			GENMASK(3, 0)
#define CS530X_AREVID			GENMASK(7, 4)

/* SW_RESET */
#define CS530X_SW_RST_SHIFT		8
#define CS530X_SW_RST_VAL		(0x5A << CS530X_SW_RST_SHIFT)

/* CLK_CFG_0 */
#define CS530X_PLL_REFCLK_SRC_MASK	BIT(0)
#define CS530X_PLL_REFCLK_FREQ_MASK	GENMASK(5, 4)
#define CS530X_SYSCLK_SRC_MASK		BIT(12)
#define CS530X_SYSCLK_SRC_SHIFT		12
#define CS530X_REFCLK_2P822_3P072	0
#define CS530X_REFCLK_5P6448_6P144	0x10
#define CS530X_REFCLK_11P2896_12P288	0x20
#define CS530X_REFCLK_24P5792_24P576	0x30

/* CLK_CFG_1 */
#define CS530X_SAMPLE_RATE_MASK		GENMASK(2, 0)
#define CS530X_FS_32K			0
#define CS530X_FS_48K_44P1K		1
#define CS530X_FS_96K_88P2K		2
#define CS530X_FS_192K_176P4K		3
#define CS530X_FS_384K_356P8K		4
#define CS530X_FS_768K_705P6K		5

/* CHIP_ENABLE */
#define CS530X_GLOBAL_EN		BIT(0)

/* ASP_CFG */
#define CS530X_ASP_BCLK_FREQ_MASK	GENMASK(1, 0)
#define CS530X_ASP_PRIMARY		BIT(5)
#define CS530X_ASP_BCLK_INV		BIT(6)
#define CS530X_BCLK_2P822_3P072		0
#define CS530X_BCLK_5P6448_6P144	1
#define CS530X_BCLK_11P2896_12P288	2
#define CS530X_BCLK_24P5792_24P576	3

/* SIGNAL_PATH_CFG */
#define CS530X_ASP_FMT_MASK		GENMASK(2, 0)
#define CS530X_ASP_TDM_SLOT_MASK	GENMASK(5, 3)
#define CS530X_ASP_TDM_SLOT_SHIFT	3
#define CS530X_ASP_CH_REVERSE		BIT(9)
#define CS530X_TDM_EN_MASK		BIT(2)
#define CS530X_ASP_FMT_I2S		0
#define CS530X_ASP_FMT_LJ		1
#define CS530X_ASP_FMT_DSP_A		0x6

/* TDM Slots */
#define CS530X_0_1_TDM_SLOT_MASK	GENMASK(1, 0)
#define CS530X_0_3_TDM_SLOT_MASK	GENMASK(3, 0)
#define CS530X_0_7_TDM_SLOT_MASK	GENMASK(7, 0)
#define CS530X_0_7_TDM_SLOT_VAL		0

#define CS530X_2_3_TDM_SLOT_MASK	GENMASK(3, 2)
#define CS530X_2_3_TDM_SLOT_VAL		1

#define CS530X_4_5_TDM_SLOT_MASK	GENMASK(5, 4)
#define CS530X_4_7_TDM_SLOT_MASK	GENMASK(7, 4)
#define CS530X_4_7_TDM_SLOT_VAL		2

#define CS530X_6_7_TDM_SLOT_MASK	GENMASK(7, 6)
#define CS530X_6_7_TDM_SLOT_VAL		3

#define CS530X_8_9_TDM_SLOT_MASK	GENMASK(9, 8)
#define CS530X_8_11_TDM_SLOT_MASK	GENMASK(11, 8)
#define CS530X_8_15_TDM_SLOT_MASK	GENMASK(15, 8)
#define CS530X_8_15_TDM_SLOT_VAL	4

#define CS530X_10_11_TDM_SLOT_MASK	GENMASK(11, 10)
#define CS530X_10_11_TDM_SLOT_VAL	5

#define CS530X_12_13_TDM_SLOT_MASK	GENMASK(13, 12)
#define CS530X_12_15_TDM_SLOT_MASK	GENMASK(15, 12)
#define CS530X_12_15_TDM_SLOT_VAL	6

#define CS530X_14_15_TDM_SLOT_MASK	GENMASK(15, 14)
#define CS530X_14_15_TDM_SLOT_VAL	7

/* IN_RAMP_SUM */
#define CS530X_RAMP_RATE_INC_SHIFT	0
#define CS530X_RAMP_RATE_DEC_SHIFT	4
#define CS530X_IN_SUM_MODE_SHIFT	13

/* IN_FILTER */
#define CS530X_IN_FILTER_SHIFT		8
#define CS530X_IN_HPF_EN_SHIFT		12

/* IN_HIZ */
#define CS530X_IN12_HIZ			BIT(0)
#define CS530X_IN34_HIZ			BIT(1)
#define CS530X_IN56_HIZ			BIT(2)
#define CS530X_IN78_HIZ			BIT(3)

/* IN_INV */
#define CS530X_IN1_INV_SHIFT		0
#define CS530X_IN2_INV_SHIFT		1
#define CS530X_IN3_INV_SHIFT		2
#define CS530X_IN4_INV_SHIFT		3
#define CS530X_IN5_INV_SHIFT		4
#define CS530X_IN6_INV_SHIFT		5
#define CS530X_IN7_INV_SHIFT		6
#define CS530X_IN8_INV_SHIFT		7

/* IN_VOL_CTLy_z */
#define CS530X_IN_MUTE			BIT(15)

/* IN_VOL_CTL5 */
#define CS530X_IN_VU			BIT(0)

/* PAD_FN */
#define CS530X_DOUT2_FN			BIT(0)
#define CS530X_DOUT3_FN			BIT(1)
#define CS530X_DOUT4_FN			BIT(2)
#define CS530X_SPI_CS_FN		BIT(3)
#define CS530X_CONFIG2_FN		BIT(6)
#define CS530X_CONFIG3_FN		BIT(7)
#define CS530X_CONFIG4_FN		BIT(8)
#define CS530X_CONFIG5_FN		BIT(9)

/* PAD_LVL */
#define CS530X_CONFIG2_LVL		BIT(6)
#define CS530X_CONFIG3_LVL		BIT(7)
#define CS530X_CONFIG4_LVL		BIT(8)
#define CS530X_CONFIG5_LVL		BIT(9)

/* System Clock Source */
#define CS530X_SYSCLK_SRC_MCLK		0
#define CS530X_SYSCLK_SRC_PLL		1

/* PLL Reference Clock Source */
#define CS530X_PLL_SRC_BCLK		0
#define CS530X_PLL_SRC_MCLK		1

#define CS530X_NUM_SUPPLIES		2

enum cs530x_type {
	CS5302,
	CS5304,
	CS5308,
};

/* codec private data */
struct cs530x_priv {
	struct regmap *regmap;
	struct device *dev;
	struct snd_soc_dai_driver *dev_dai;

	enum cs530x_type devtype;
	int num_adcs;
	int num_dacs;

	struct regulator_bulk_data supplies[CS530X_NUM_SUPPLIES];

	unsigned int mclk_rate;

	int tdm_width;
	int tdm_slots;
	int fs;
	int adc_pairs_count;

	struct gpio_desc *reset_gpio;
};

extern const struct regmap_config cs530x_regmap;
int cs530x_probe(struct cs530x_priv *cs530x);

#endif
