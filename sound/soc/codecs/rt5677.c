/*
 * rt5677.c  --  RT5677 ALSA SoC audio codec driver
 *
 * Copyright 2013 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rl6231.h"
#include "rt5677.h"
#include "rt5677-spi.h"

#define RT5677_DEVICE_ID 0x6327

#define RT5677_PR_RANGE_BASE (0xff + 1)
#define RT5677_PR_SPACING 0x100

#define RT5677_PR_BASE (RT5677_PR_RANGE_BASE + (0 * RT5677_PR_SPACING))

static const struct regmap_range_cfg rt5677_ranges[] = {
	{
		.name = "PR",
		.range_min = RT5677_PR_BASE,
		.range_max = RT5677_PR_BASE + 0xfd,
		.selector_reg = RT5677_PRIV_INDEX,
		.selector_mask = 0xff,
		.selector_shift = 0x0,
		.window_start = RT5677_PRIV_DATA,
		.window_len = 0x1,
	},
};

static const struct reg_default init_list[] = {
	{RT5677_ASRC_12,	0x0018},
	{RT5677_PR_BASE + 0x3d,	0x364d},
	{RT5677_PR_BASE + 0x17,	0x4fc0},
	{RT5677_PR_BASE + 0x13,	0x0312},
	{RT5677_PR_BASE + 0x1e,	0x0000},
	{RT5677_PR_BASE + 0x12,	0x0eaa},
	{RT5677_PR_BASE + 0x14,	0x018a},
};
#define RT5677_INIT_REG_LEN ARRAY_SIZE(init_list)

static const struct reg_default rt5677_reg[] = {
	{RT5677_RESET			, 0x0000},
	{RT5677_LOUT1			, 0xa800},
	{RT5677_IN1			, 0x0000},
	{RT5677_MICBIAS			, 0x0000},
	{RT5677_SLIMBUS_PARAM		, 0x0000},
	{RT5677_SLIMBUS_RX		, 0x0000},
	{RT5677_SLIMBUS_CTRL		, 0x0000},
	{RT5677_SIDETONE_CTRL		, 0x000b},
	{RT5677_ANA_DAC1_2_3_SRC	, 0x0000},
	{RT5677_IF_DSP_DAC3_4_MIXER	, 0x1111},
	{RT5677_DAC4_DIG_VOL		, 0xafaf},
	{RT5677_DAC3_DIG_VOL		, 0xafaf},
	{RT5677_DAC1_DIG_VOL		, 0xafaf},
	{RT5677_DAC2_DIG_VOL		, 0xafaf},
	{RT5677_IF_DSP_DAC2_MIXER	, 0x0011},
	{RT5677_STO1_ADC_DIG_VOL	, 0x2f2f},
	{RT5677_MONO_ADC_DIG_VOL	, 0x2f2f},
	{RT5677_STO1_2_ADC_BST		, 0x0000},
	{RT5677_STO2_ADC_DIG_VOL	, 0x2f2f},
	{RT5677_ADC_BST_CTRL2		, 0x0000},
	{RT5677_STO3_4_ADC_BST		, 0x0000},
	{RT5677_STO3_ADC_DIG_VOL	, 0x2f2f},
	{RT5677_STO4_ADC_DIG_VOL	, 0x2f2f},
	{RT5677_STO4_ADC_MIXER		, 0xd4c0},
	{RT5677_STO3_ADC_MIXER		, 0xd4c0},
	{RT5677_STO2_ADC_MIXER		, 0xd4c0},
	{RT5677_STO1_ADC_MIXER		, 0xd4c0},
	{RT5677_MONO_ADC_MIXER		, 0xd4d1},
	{RT5677_ADC_IF_DSP_DAC1_MIXER	, 0x8080},
	{RT5677_STO1_DAC_MIXER		, 0xaaaa},
	{RT5677_MONO_DAC_MIXER		, 0xaaaa},
	{RT5677_DD1_MIXER		, 0xaaaa},
	{RT5677_DD2_MIXER		, 0xaaaa},
	{RT5677_IF3_DATA		, 0x0000},
	{RT5677_IF4_DATA		, 0x0000},
	{RT5677_PDM_OUT_CTRL		, 0x8888},
	{RT5677_PDM_DATA_CTRL1		, 0x0000},
	{RT5677_PDM_DATA_CTRL2		, 0x0000},
	{RT5677_PDM1_DATA_CTRL2		, 0x0000},
	{RT5677_PDM1_DATA_CTRL3		, 0x0000},
	{RT5677_PDM1_DATA_CTRL4		, 0x0000},
	{RT5677_PDM2_DATA_CTRL2		, 0x0000},
	{RT5677_PDM2_DATA_CTRL3		, 0x0000},
	{RT5677_PDM2_DATA_CTRL4		, 0x0000},
	{RT5677_TDM1_CTRL1		, 0x0300},
	{RT5677_TDM1_CTRL2		, 0x0000},
	{RT5677_TDM1_CTRL3		, 0x4000},
	{RT5677_TDM1_CTRL4		, 0x0123},
	{RT5677_TDM1_CTRL5		, 0x4567},
	{RT5677_TDM2_CTRL1		, 0x0300},
	{RT5677_TDM2_CTRL2		, 0x0000},
	{RT5677_TDM2_CTRL3		, 0x4000},
	{RT5677_TDM2_CTRL4		, 0x0123},
	{RT5677_TDM2_CTRL5		, 0x4567},
	{RT5677_I2C_MASTER_CTRL1	, 0x0001},
	{RT5677_I2C_MASTER_CTRL2	, 0x0000},
	{RT5677_I2C_MASTER_CTRL3	, 0x0000},
	{RT5677_I2C_MASTER_CTRL4	, 0x0000},
	{RT5677_I2C_MASTER_CTRL5	, 0x0000},
	{RT5677_I2C_MASTER_CTRL6	, 0x0000},
	{RT5677_I2C_MASTER_CTRL7	, 0x0000},
	{RT5677_I2C_MASTER_CTRL8	, 0x0000},
	{RT5677_DMIC_CTRL1		, 0x1505},
	{RT5677_DMIC_CTRL2		, 0x0055},
	{RT5677_HAP_GENE_CTRL1		, 0x0111},
	{RT5677_HAP_GENE_CTRL2		, 0x0064},
	{RT5677_HAP_GENE_CTRL3		, 0xef0e},
	{RT5677_HAP_GENE_CTRL4		, 0xf0f0},
	{RT5677_HAP_GENE_CTRL5		, 0xef0e},
	{RT5677_HAP_GENE_CTRL6		, 0xf0f0},
	{RT5677_HAP_GENE_CTRL7		, 0xef0e},
	{RT5677_HAP_GENE_CTRL8		, 0xf0f0},
	{RT5677_HAP_GENE_CTRL9		, 0xf000},
	{RT5677_HAP_GENE_CTRL10		, 0x0000},
	{RT5677_PWR_DIG1		, 0x0000},
	{RT5677_PWR_DIG2		, 0x0000},
	{RT5677_PWR_ANLG1		, 0x0055},
	{RT5677_PWR_ANLG2		, 0x0000},
	{RT5677_PWR_DSP1		, 0x0001},
	{RT5677_PWR_DSP_ST		, 0x0000},
	{RT5677_PWR_DSP2		, 0x0000},
	{RT5677_ADC_DAC_HPF_CTRL1	, 0x0e00},
	{RT5677_PRIV_INDEX		, 0x0000},
	{RT5677_PRIV_DATA		, 0x0000},
	{RT5677_I2S4_SDP		, 0x8000},
	{RT5677_I2S1_SDP		, 0x8000},
	{RT5677_I2S2_SDP		, 0x8000},
	{RT5677_I2S3_SDP		, 0x8000},
	{RT5677_CLK_TREE_CTRL1		, 0x1111},
	{RT5677_CLK_TREE_CTRL2		, 0x1111},
	{RT5677_CLK_TREE_CTRL3		, 0x0000},
	{RT5677_PLL1_CTRL1		, 0x0000},
	{RT5677_PLL1_CTRL2		, 0x0000},
	{RT5677_PLL2_CTRL1		, 0x0c60},
	{RT5677_PLL2_CTRL2		, 0x2000},
	{RT5677_GLB_CLK1		, 0x0000},
	{RT5677_GLB_CLK2		, 0x0000},
	{RT5677_ASRC_1			, 0x0000},
	{RT5677_ASRC_2			, 0x0000},
	{RT5677_ASRC_3			, 0x0000},
	{RT5677_ASRC_4			, 0x0000},
	{RT5677_ASRC_5			, 0x0000},
	{RT5677_ASRC_6			, 0x0000},
	{RT5677_ASRC_7			, 0x0000},
	{RT5677_ASRC_8			, 0x0000},
	{RT5677_ASRC_9			, 0x0000},
	{RT5677_ASRC_10			, 0x0000},
	{RT5677_ASRC_11			, 0x0000},
	{RT5677_ASRC_12			, 0x0018},
	{RT5677_ASRC_13			, 0x0000},
	{RT5677_ASRC_14			, 0x0000},
	{RT5677_ASRC_15			, 0x0000},
	{RT5677_ASRC_16			, 0x0000},
	{RT5677_ASRC_17			, 0x0000},
	{RT5677_ASRC_18			, 0x0000},
	{RT5677_ASRC_19			, 0x0000},
	{RT5677_ASRC_20			, 0x0000},
	{RT5677_ASRC_21			, 0x000c},
	{RT5677_ASRC_22			, 0x0000},
	{RT5677_ASRC_23			, 0x0000},
	{RT5677_VAD_CTRL1		, 0x2184},
	{RT5677_VAD_CTRL2		, 0x010a},
	{RT5677_VAD_CTRL3		, 0x0aea},
	{RT5677_VAD_CTRL4		, 0x000c},
	{RT5677_VAD_CTRL5		, 0x0000},
	{RT5677_DSP_INB_CTRL1		, 0x0000},
	{RT5677_DSP_INB_CTRL2		, 0x0000},
	{RT5677_DSP_IN_OUTB_CTRL	, 0x0000},
	{RT5677_DSP_OUTB0_1_DIG_VOL	, 0x2f2f},
	{RT5677_DSP_OUTB2_3_DIG_VOL	, 0x2f2f},
	{RT5677_DSP_OUTB4_5_DIG_VOL	, 0x2f2f},
	{RT5677_DSP_OUTB6_7_DIG_VOL	, 0x2f2f},
	{RT5677_ADC_EQ_CTRL1		, 0x6000},
	{RT5677_ADC_EQ_CTRL2		, 0x0000},
	{RT5677_EQ_CTRL1		, 0xc000},
	{RT5677_EQ_CTRL2		, 0x0000},
	{RT5677_EQ_CTRL3		, 0x0000},
	{RT5677_SOFT_VOL_ZERO_CROSS1	, 0x0009},
	{RT5677_JD_CTRL1		, 0x0000},
	{RT5677_JD_CTRL2		, 0x0000},
	{RT5677_JD_CTRL3		, 0x0000},
	{RT5677_IRQ_CTRL1		, 0x0000},
	{RT5677_IRQ_CTRL2		, 0x0000},
	{RT5677_GPIO_ST			, 0x0000},
	{RT5677_GPIO_CTRL1		, 0x0000},
	{RT5677_GPIO_CTRL2		, 0x0000},
	{RT5677_GPIO_CTRL3		, 0x0000},
	{RT5677_STO1_ADC_HI_FILTER1	, 0xb320},
	{RT5677_STO1_ADC_HI_FILTER2	, 0x0000},
	{RT5677_MONO_ADC_HI_FILTER1	, 0xb300},
	{RT5677_MONO_ADC_HI_FILTER2	, 0x0000},
	{RT5677_STO2_ADC_HI_FILTER1	, 0xb300},
	{RT5677_STO2_ADC_HI_FILTER2	, 0x0000},
	{RT5677_STO3_ADC_HI_FILTER1	, 0xb300},
	{RT5677_STO3_ADC_HI_FILTER2	, 0x0000},
	{RT5677_STO4_ADC_HI_FILTER1	, 0xb300},
	{RT5677_STO4_ADC_HI_FILTER2	, 0x0000},
	{RT5677_MB_DRC_CTRL1		, 0x0f20},
	{RT5677_DRC1_CTRL1		, 0x001f},
	{RT5677_DRC1_CTRL2		, 0x020c},
	{RT5677_DRC1_CTRL3		, 0x1f00},
	{RT5677_DRC1_CTRL4		, 0x0000},
	{RT5677_DRC1_CTRL5		, 0x0000},
	{RT5677_DRC1_CTRL6		, 0x0029},
	{RT5677_DRC2_CTRL1		, 0x001f},
	{RT5677_DRC2_CTRL2		, 0x020c},
	{RT5677_DRC2_CTRL3		, 0x1f00},
	{RT5677_DRC2_CTRL4		, 0x0000},
	{RT5677_DRC2_CTRL5		, 0x0000},
	{RT5677_DRC2_CTRL6		, 0x0029},
	{RT5677_DRC1_HL_CTRL1		, 0x8000},
	{RT5677_DRC1_HL_CTRL2		, 0x0200},
	{RT5677_DRC2_HL_CTRL1		, 0x8000},
	{RT5677_DRC2_HL_CTRL2		, 0x0200},
	{RT5677_DSP_INB1_SRC_CTRL1	, 0x5800},
	{RT5677_DSP_INB1_SRC_CTRL2	, 0x0000},
	{RT5677_DSP_INB1_SRC_CTRL3	, 0x0000},
	{RT5677_DSP_INB1_SRC_CTRL4	, 0x0800},
	{RT5677_DSP_INB2_SRC_CTRL1	, 0x5800},
	{RT5677_DSP_INB2_SRC_CTRL2	, 0x0000},
	{RT5677_DSP_INB2_SRC_CTRL3	, 0x0000},
	{RT5677_DSP_INB2_SRC_CTRL4	, 0x0800},
	{RT5677_DSP_INB3_SRC_CTRL1	, 0x5800},
	{RT5677_DSP_INB3_SRC_CTRL2	, 0x0000},
	{RT5677_DSP_INB3_SRC_CTRL3	, 0x0000},
	{RT5677_DSP_INB3_SRC_CTRL4	, 0x0800},
	{RT5677_DSP_OUTB1_SRC_CTRL1	, 0x5800},
	{RT5677_DSP_OUTB1_SRC_CTRL2	, 0x0000},
	{RT5677_DSP_OUTB1_SRC_CTRL3	, 0x0000},
	{RT5677_DSP_OUTB1_SRC_CTRL4	, 0x0800},
	{RT5677_DSP_OUTB2_SRC_CTRL1	, 0x5800},
	{RT5677_DSP_OUTB2_SRC_CTRL2	, 0x0000},
	{RT5677_DSP_OUTB2_SRC_CTRL3	, 0x0000},
	{RT5677_DSP_OUTB2_SRC_CTRL4	, 0x0800},
	{RT5677_DSP_OUTB_0123_MIXER_CTRL, 0xfefe},
	{RT5677_DSP_OUTB_45_MIXER_CTRL	, 0xfefe},
	{RT5677_DSP_OUTB_67_MIXER_CTRL	, 0xfefe},
	{RT5677_DIG_MISC		, 0x0000},
	{RT5677_GEN_CTRL1		, 0x0000},
	{RT5677_GEN_CTRL2		, 0x0000},
	{RT5677_VENDOR_ID		, 0x0000},
	{RT5677_VENDOR_ID1		, 0x10ec},
	{RT5677_VENDOR_ID2		, 0x6327},
};

static bool rt5677_volatile_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5677_ranges); i++) {
		if (reg >= rt5677_ranges[i].range_min &&
			reg <= rt5677_ranges[i].range_max) {
			return true;
		}
	}

	switch (reg) {
	case RT5677_RESET:
	case RT5677_SLIMBUS_PARAM:
	case RT5677_PDM_DATA_CTRL1:
	case RT5677_PDM_DATA_CTRL2:
	case RT5677_PDM1_DATA_CTRL4:
	case RT5677_PDM2_DATA_CTRL4:
	case RT5677_I2C_MASTER_CTRL1:
	case RT5677_I2C_MASTER_CTRL7:
	case RT5677_I2C_MASTER_CTRL8:
	case RT5677_HAP_GENE_CTRL2:
	case RT5677_PWR_DSP_ST:
	case RT5677_PRIV_DATA:
	case RT5677_PLL1_CTRL2:
	case RT5677_PLL2_CTRL2:
	case RT5677_ASRC_22:
	case RT5677_ASRC_23:
	case RT5677_VAD_CTRL5:
	case RT5677_ADC_EQ_CTRL1:
	case RT5677_EQ_CTRL1:
	case RT5677_IRQ_CTRL1:
	case RT5677_IRQ_CTRL2:
	case RT5677_GPIO_ST:
	case RT5677_DSP_INB1_SRC_CTRL4:
	case RT5677_DSP_INB2_SRC_CTRL4:
	case RT5677_DSP_INB3_SRC_CTRL4:
	case RT5677_DSP_OUTB1_SRC_CTRL4:
	case RT5677_DSP_OUTB2_SRC_CTRL4:
	case RT5677_VENDOR_ID:
	case RT5677_VENDOR_ID1:
	case RT5677_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static bool rt5677_readable_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5677_ranges); i++) {
		if (reg >= rt5677_ranges[i].range_min &&
			reg <= rt5677_ranges[i].range_max) {
			return true;
		}
	}

	switch (reg) {
	case RT5677_RESET:
	case RT5677_LOUT1:
	case RT5677_IN1:
	case RT5677_MICBIAS:
	case RT5677_SLIMBUS_PARAM:
	case RT5677_SLIMBUS_RX:
	case RT5677_SLIMBUS_CTRL:
	case RT5677_SIDETONE_CTRL:
	case RT5677_ANA_DAC1_2_3_SRC:
	case RT5677_IF_DSP_DAC3_4_MIXER:
	case RT5677_DAC4_DIG_VOL:
	case RT5677_DAC3_DIG_VOL:
	case RT5677_DAC1_DIG_VOL:
	case RT5677_DAC2_DIG_VOL:
	case RT5677_IF_DSP_DAC2_MIXER:
	case RT5677_STO1_ADC_DIG_VOL:
	case RT5677_MONO_ADC_DIG_VOL:
	case RT5677_STO1_2_ADC_BST:
	case RT5677_STO2_ADC_DIG_VOL:
	case RT5677_ADC_BST_CTRL2:
	case RT5677_STO3_4_ADC_BST:
	case RT5677_STO3_ADC_DIG_VOL:
	case RT5677_STO4_ADC_DIG_VOL:
	case RT5677_STO4_ADC_MIXER:
	case RT5677_STO3_ADC_MIXER:
	case RT5677_STO2_ADC_MIXER:
	case RT5677_STO1_ADC_MIXER:
	case RT5677_MONO_ADC_MIXER:
	case RT5677_ADC_IF_DSP_DAC1_MIXER:
	case RT5677_STO1_DAC_MIXER:
	case RT5677_MONO_DAC_MIXER:
	case RT5677_DD1_MIXER:
	case RT5677_DD2_MIXER:
	case RT5677_IF3_DATA:
	case RT5677_IF4_DATA:
	case RT5677_PDM_OUT_CTRL:
	case RT5677_PDM_DATA_CTRL1:
	case RT5677_PDM_DATA_CTRL2:
	case RT5677_PDM1_DATA_CTRL2:
	case RT5677_PDM1_DATA_CTRL3:
	case RT5677_PDM1_DATA_CTRL4:
	case RT5677_PDM2_DATA_CTRL2:
	case RT5677_PDM2_DATA_CTRL3:
	case RT5677_PDM2_DATA_CTRL4:
	case RT5677_TDM1_CTRL1:
	case RT5677_TDM1_CTRL2:
	case RT5677_TDM1_CTRL3:
	case RT5677_TDM1_CTRL4:
	case RT5677_TDM1_CTRL5:
	case RT5677_TDM2_CTRL1:
	case RT5677_TDM2_CTRL2:
	case RT5677_TDM2_CTRL3:
	case RT5677_TDM2_CTRL4:
	case RT5677_TDM2_CTRL5:
	case RT5677_I2C_MASTER_CTRL1:
	case RT5677_I2C_MASTER_CTRL2:
	case RT5677_I2C_MASTER_CTRL3:
	case RT5677_I2C_MASTER_CTRL4:
	case RT5677_I2C_MASTER_CTRL5:
	case RT5677_I2C_MASTER_CTRL6:
	case RT5677_I2C_MASTER_CTRL7:
	case RT5677_I2C_MASTER_CTRL8:
	case RT5677_DMIC_CTRL1:
	case RT5677_DMIC_CTRL2:
	case RT5677_HAP_GENE_CTRL1:
	case RT5677_HAP_GENE_CTRL2:
	case RT5677_HAP_GENE_CTRL3:
	case RT5677_HAP_GENE_CTRL4:
	case RT5677_HAP_GENE_CTRL5:
	case RT5677_HAP_GENE_CTRL6:
	case RT5677_HAP_GENE_CTRL7:
	case RT5677_HAP_GENE_CTRL8:
	case RT5677_HAP_GENE_CTRL9:
	case RT5677_HAP_GENE_CTRL10:
	case RT5677_PWR_DIG1:
	case RT5677_PWR_DIG2:
	case RT5677_PWR_ANLG1:
	case RT5677_PWR_ANLG2:
	case RT5677_PWR_DSP1:
	case RT5677_PWR_DSP_ST:
	case RT5677_PWR_DSP2:
	case RT5677_ADC_DAC_HPF_CTRL1:
	case RT5677_PRIV_INDEX:
	case RT5677_PRIV_DATA:
	case RT5677_I2S4_SDP:
	case RT5677_I2S1_SDP:
	case RT5677_I2S2_SDP:
	case RT5677_I2S3_SDP:
	case RT5677_CLK_TREE_CTRL1:
	case RT5677_CLK_TREE_CTRL2:
	case RT5677_CLK_TREE_CTRL3:
	case RT5677_PLL1_CTRL1:
	case RT5677_PLL1_CTRL2:
	case RT5677_PLL2_CTRL1:
	case RT5677_PLL2_CTRL2:
	case RT5677_GLB_CLK1:
	case RT5677_GLB_CLK2:
	case RT5677_ASRC_1:
	case RT5677_ASRC_2:
	case RT5677_ASRC_3:
	case RT5677_ASRC_4:
	case RT5677_ASRC_5:
	case RT5677_ASRC_6:
	case RT5677_ASRC_7:
	case RT5677_ASRC_8:
	case RT5677_ASRC_9:
	case RT5677_ASRC_10:
	case RT5677_ASRC_11:
	case RT5677_ASRC_12:
	case RT5677_ASRC_13:
	case RT5677_ASRC_14:
	case RT5677_ASRC_15:
	case RT5677_ASRC_16:
	case RT5677_ASRC_17:
	case RT5677_ASRC_18:
	case RT5677_ASRC_19:
	case RT5677_ASRC_20:
	case RT5677_ASRC_21:
	case RT5677_ASRC_22:
	case RT5677_ASRC_23:
	case RT5677_VAD_CTRL1:
	case RT5677_VAD_CTRL2:
	case RT5677_VAD_CTRL3:
	case RT5677_VAD_CTRL4:
	case RT5677_VAD_CTRL5:
	case RT5677_DSP_INB_CTRL1:
	case RT5677_DSP_INB_CTRL2:
	case RT5677_DSP_IN_OUTB_CTRL:
	case RT5677_DSP_OUTB0_1_DIG_VOL:
	case RT5677_DSP_OUTB2_3_DIG_VOL:
	case RT5677_DSP_OUTB4_5_DIG_VOL:
	case RT5677_DSP_OUTB6_7_DIG_VOL:
	case RT5677_ADC_EQ_CTRL1:
	case RT5677_ADC_EQ_CTRL2:
	case RT5677_EQ_CTRL1:
	case RT5677_EQ_CTRL2:
	case RT5677_EQ_CTRL3:
	case RT5677_SOFT_VOL_ZERO_CROSS1:
	case RT5677_JD_CTRL1:
	case RT5677_JD_CTRL2:
	case RT5677_JD_CTRL3:
	case RT5677_IRQ_CTRL1:
	case RT5677_IRQ_CTRL2:
	case RT5677_GPIO_ST:
	case RT5677_GPIO_CTRL1:
	case RT5677_GPIO_CTRL2:
	case RT5677_GPIO_CTRL3:
	case RT5677_STO1_ADC_HI_FILTER1:
	case RT5677_STO1_ADC_HI_FILTER2:
	case RT5677_MONO_ADC_HI_FILTER1:
	case RT5677_MONO_ADC_HI_FILTER2:
	case RT5677_STO2_ADC_HI_FILTER1:
	case RT5677_STO2_ADC_HI_FILTER2:
	case RT5677_STO3_ADC_HI_FILTER1:
	case RT5677_STO3_ADC_HI_FILTER2:
	case RT5677_STO4_ADC_HI_FILTER1:
	case RT5677_STO4_ADC_HI_FILTER2:
	case RT5677_MB_DRC_CTRL1:
	case RT5677_DRC1_CTRL1:
	case RT5677_DRC1_CTRL2:
	case RT5677_DRC1_CTRL3:
	case RT5677_DRC1_CTRL4:
	case RT5677_DRC1_CTRL5:
	case RT5677_DRC1_CTRL6:
	case RT5677_DRC2_CTRL1:
	case RT5677_DRC2_CTRL2:
	case RT5677_DRC2_CTRL3:
	case RT5677_DRC2_CTRL4:
	case RT5677_DRC2_CTRL5:
	case RT5677_DRC2_CTRL6:
	case RT5677_DRC1_HL_CTRL1:
	case RT5677_DRC1_HL_CTRL2:
	case RT5677_DRC2_HL_CTRL1:
	case RT5677_DRC2_HL_CTRL2:
	case RT5677_DSP_INB1_SRC_CTRL1:
	case RT5677_DSP_INB1_SRC_CTRL2:
	case RT5677_DSP_INB1_SRC_CTRL3:
	case RT5677_DSP_INB1_SRC_CTRL4:
	case RT5677_DSP_INB2_SRC_CTRL1:
	case RT5677_DSP_INB2_SRC_CTRL2:
	case RT5677_DSP_INB2_SRC_CTRL3:
	case RT5677_DSP_INB2_SRC_CTRL4:
	case RT5677_DSP_INB3_SRC_CTRL1:
	case RT5677_DSP_INB3_SRC_CTRL2:
	case RT5677_DSP_INB3_SRC_CTRL3:
	case RT5677_DSP_INB3_SRC_CTRL4:
	case RT5677_DSP_OUTB1_SRC_CTRL1:
	case RT5677_DSP_OUTB1_SRC_CTRL2:
	case RT5677_DSP_OUTB1_SRC_CTRL3:
	case RT5677_DSP_OUTB1_SRC_CTRL4:
	case RT5677_DSP_OUTB2_SRC_CTRL1:
	case RT5677_DSP_OUTB2_SRC_CTRL2:
	case RT5677_DSP_OUTB2_SRC_CTRL3:
	case RT5677_DSP_OUTB2_SRC_CTRL4:
	case RT5677_DSP_OUTB_0123_MIXER_CTRL:
	case RT5677_DSP_OUTB_45_MIXER_CTRL:
	case RT5677_DSP_OUTB_67_MIXER_CTRL:
	case RT5677_DIG_MISC:
	case RT5677_GEN_CTRL1:
	case RT5677_GEN_CTRL2:
	case RT5677_VENDOR_ID:
	case RT5677_VENDOR_ID1:
	case RT5677_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

/**
 * rt5677_dsp_mode_i2c_write_addr - Write value to address on DSP mode.
 * @rt5677: Private Data.
 * @addr: Address index.
 * @value: Address data.
 *
 *
 * Returns 0 for success or negative error code.
 */
static int rt5677_dsp_mode_i2c_write_addr(struct rt5677_priv *rt5677,
		unsigned int addr, unsigned int value, unsigned int opcode)
{
	struct snd_soc_codec *codec = rt5677->codec;
	int ret;

	mutex_lock(&rt5677->dsp_cmd_lock);

	ret = regmap_write(rt5677->regmap_physical, RT5677_DSP_I2C_ADDR_MSB,
		addr >> 16);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set addr msb value: %d\n", ret);
		goto err;
	}

	ret = regmap_write(rt5677->regmap_physical, RT5677_DSP_I2C_ADDR_LSB,
		addr & 0xffff);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set addr lsb value: %d\n", ret);
		goto err;
	}

	ret = regmap_write(rt5677->regmap_physical, RT5677_DSP_I2C_DATA_MSB,
		value >> 16);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set data msb value: %d\n", ret);
		goto err;
	}

	ret = regmap_write(rt5677->regmap_physical, RT5677_DSP_I2C_DATA_LSB,
		value & 0xffff);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set data lsb value: %d\n", ret);
		goto err;
	}

	ret = regmap_write(rt5677->regmap_physical, RT5677_DSP_I2C_OP_CODE,
		opcode);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set op code value: %d\n", ret);
		goto err;
	}

err:
	mutex_unlock(&rt5677->dsp_cmd_lock);

	return ret;
}

/**
 * rt5677_dsp_mode_i2c_read_addr - Read value from address on DSP mode.
 * rt5677: Private Data.
 * @addr: Address index.
 * @value: Address data.
 *
 *
 * Returns 0 for success or negative error code.
 */
static int rt5677_dsp_mode_i2c_read_addr(
	struct rt5677_priv *rt5677, unsigned int addr, unsigned int *value)
{
	struct snd_soc_codec *codec = rt5677->codec;
	int ret;
	unsigned int msb, lsb;

	mutex_lock(&rt5677->dsp_cmd_lock);

	ret = regmap_write(rt5677->regmap_physical, RT5677_DSP_I2C_ADDR_MSB,
		addr >> 16);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set addr msb value: %d\n", ret);
		goto err;
	}

	ret = regmap_write(rt5677->regmap_physical, RT5677_DSP_I2C_ADDR_LSB,
		addr & 0xffff);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set addr lsb value: %d\n", ret);
		goto err;
	}

	ret = regmap_write(rt5677->regmap_physical, RT5677_DSP_I2C_OP_CODE,
		0x0002);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set op code value: %d\n", ret);
		goto err;
	}

	regmap_read(rt5677->regmap_physical, RT5677_DSP_I2C_DATA_MSB, &msb);
	regmap_read(rt5677->regmap_physical, RT5677_DSP_I2C_DATA_LSB, &lsb);
	*value = (msb << 16) | lsb;

err:
	mutex_unlock(&rt5677->dsp_cmd_lock);

	return ret;
}

/**
 * rt5677_dsp_mode_i2c_write - Write register on DSP mode.
 * rt5677: Private Data.
 * @reg: Register index.
 * @value: Register data.
 *
 *
 * Returns 0 for success or negative error code.
 */
static int rt5677_dsp_mode_i2c_write(struct rt5677_priv *rt5677,
		unsigned int reg, unsigned int value)
{
	return rt5677_dsp_mode_i2c_write_addr(rt5677, 0x18020000 + reg * 2,
		value, 0x0001);
}

/**
 * rt5677_dsp_mode_i2c_read - Read register on DSP mode.
 * @codec: SoC audio codec device.
 * @reg: Register index.
 * @value: Register data.
 *
 *
 * Returns 0 for success or negative error code.
 */
static int rt5677_dsp_mode_i2c_read(
	struct rt5677_priv *rt5677, unsigned int reg, unsigned int *value)
{
	int ret = rt5677_dsp_mode_i2c_read_addr(rt5677, 0x18020000 + reg * 2,
		value);

	*value &= 0xffff;

	return ret;
}

static void rt5677_set_dsp_mode(struct snd_soc_codec *codec, bool on)
{
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	if (on) {
		regmap_update_bits(rt5677->regmap, RT5677_PWR_DSP1, 0x2, 0x2);
		rt5677->is_dsp_mode = true;
	} else {
		regmap_update_bits(rt5677->regmap, RT5677_PWR_DSP1, 0x2, 0x0);
		rt5677->is_dsp_mode = false;
	}
}

static int rt5677_set_dsp_vad(struct snd_soc_codec *codec, bool on)
{
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	static bool activity;
	int ret;

	if (!IS_ENABLED(CONFIG_SND_SOC_RT5677_SPI))
		return -ENXIO;

	if (on && !activity) {
		activity = true;

		regcache_cache_only(rt5677->regmap, false);
		regcache_cache_bypass(rt5677->regmap, true);

		regmap_update_bits(rt5677->regmap, RT5677_DIG_MISC, 0x1, 0x1);
		regmap_update_bits(rt5677->regmap,
			RT5677_PR_BASE + RT5677_BIAS_CUR4, 0x0f00, 0x0f00);
		regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG1,
			RT5677_LDO1_SEL_MASK, 0x0);
		regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG2,
			RT5677_PWR_LDO1, RT5677_PWR_LDO1);
		regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK1,
			RT5677_MCLK_SRC_MASK, RT5677_MCLK2_SRC);
		regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK2,
			RT5677_PLL2_PR_SRC_MASK | RT5677_DSP_CLK_SRC_MASK,
			RT5677_PLL2_PR_SRC_MCLK2 | RT5677_DSP_CLK_SRC_BYPASS);
		regmap_write(rt5677->regmap, RT5677_PWR_DSP2, 0x07ff);
		regmap_write(rt5677->regmap, RT5677_PWR_DSP1, 0x07fd);
		rt5677_set_dsp_mode(codec, true);

		ret = request_firmware(&rt5677->fw1, RT5677_FIRMWARE1,
			codec->dev);
		if (ret == 0) {
			rt5677_spi_burst_write(0x50000000, rt5677->fw1);
			release_firmware(rt5677->fw1);
		}

		ret = request_firmware(&rt5677->fw2, RT5677_FIRMWARE2,
			codec->dev);
		if (ret == 0) {
			rt5677_spi_burst_write(0x60000000, rt5677->fw2);
			release_firmware(rt5677->fw2);
		}

		regmap_update_bits(rt5677->regmap, RT5677_PWR_DSP1, 0x1, 0x0);

		regcache_cache_bypass(rt5677->regmap, false);
		regcache_cache_only(rt5677->regmap, true);
	} else if (!on && activity) {
		activity = false;

		regcache_cache_only(rt5677->regmap, false);
		regcache_cache_bypass(rt5677->regmap, true);

		regmap_update_bits(rt5677->regmap, RT5677_PWR_DSP1, 0x1, 0x1);
		rt5677_set_dsp_mode(codec, false);
		regmap_write(rt5677->regmap, RT5677_PWR_DSP1, 0x0001);

		regmap_write(rt5677->regmap, RT5677_RESET, 0x10ec);

		regcache_cache_bypass(rt5677->regmap, false);
		regcache_mark_dirty(rt5677->regmap);
		regcache_sync(rt5677->regmap);
	}

	return 0;
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);
static const DECLARE_TLV_DB_SCALE(st_vol_tlv, -4650, 150, 0);

/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static unsigned int bst_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0),
};

static int rt5677_dsp_vad_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt5677_priv *rt5677 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rt5677->dsp_vad_en;

	return 0;
}

static int rt5677_dsp_vad_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt5677_priv *rt5677 = snd_soc_component_get_drvdata(component);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);

	rt5677->dsp_vad_en = !!ucontrol->value.integer.value[0];

	if (codec->dapm.bias_level == SND_SOC_BIAS_OFF)
		rt5677_set_dsp_vad(codec, rt5677->dsp_vad_en);

	return 0;
}

static const struct snd_kcontrol_new rt5677_snd_controls[] = {
	/* OUTPUT Control */
	SOC_SINGLE("OUT1 Playback Switch", RT5677_LOUT1,
		RT5677_LOUT1_L_MUTE_SFT, 1, 1),
	SOC_SINGLE("OUT2 Playback Switch", RT5677_LOUT1,
		RT5677_LOUT2_L_MUTE_SFT, 1, 1),
	SOC_SINGLE("OUT3 Playback Switch", RT5677_LOUT1,
		RT5677_LOUT3_L_MUTE_SFT, 1, 1),

	/* DAC Digital Volume */
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5677_DAC1_DIG_VOL,
		RT5677_L_VOL_SFT, RT5677_R_VOL_SFT, 87, 0, dac_vol_tlv),
	SOC_DOUBLE_TLV("DAC2 Playback Volume", RT5677_DAC2_DIG_VOL,
		RT5677_L_VOL_SFT, RT5677_R_VOL_SFT, 87, 0, dac_vol_tlv),
	SOC_DOUBLE_TLV("DAC3 Playback Volume", RT5677_DAC3_DIG_VOL,
		RT5677_L_VOL_SFT, RT5677_R_VOL_SFT, 87, 0, dac_vol_tlv),
	SOC_DOUBLE_TLV("DAC4 Playback Volume", RT5677_DAC4_DIG_VOL,
		RT5677_L_VOL_SFT, RT5677_R_VOL_SFT, 87, 0, dac_vol_tlv),

	/* IN1/IN2 Control */
	SOC_SINGLE_TLV("IN1 Boost", RT5677_IN1, RT5677_BST_SFT1, 8, 0, bst_tlv),
	SOC_SINGLE_TLV("IN2 Boost", RT5677_IN1, RT5677_BST_SFT2, 8, 0, bst_tlv),

	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC1 Capture Switch", RT5677_STO1_ADC_DIG_VOL,
		RT5677_L_MUTE_SFT, RT5677_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("ADC2 Capture Switch", RT5677_STO2_ADC_DIG_VOL,
		RT5677_L_MUTE_SFT, RT5677_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("ADC3 Capture Switch", RT5677_STO3_ADC_DIG_VOL,
		RT5677_L_MUTE_SFT, RT5677_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("ADC4 Capture Switch", RT5677_STO4_ADC_DIG_VOL,
		RT5677_L_MUTE_SFT, RT5677_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("Mono ADC Capture Switch", RT5677_MONO_ADC_DIG_VOL,
		RT5677_L_MUTE_SFT, RT5677_R_MUTE_SFT, 1, 1),

	SOC_DOUBLE_TLV("ADC1 Capture Volume", RT5677_STO1_ADC_DIG_VOL,
		RT5677_STO1_ADC_L_VOL_SFT, RT5677_STO1_ADC_R_VOL_SFT, 63, 0,
		adc_vol_tlv),
	SOC_DOUBLE_TLV("ADC2 Capture Volume", RT5677_STO2_ADC_DIG_VOL,
		RT5677_STO1_ADC_L_VOL_SFT, RT5677_STO1_ADC_R_VOL_SFT, 63, 0,
		adc_vol_tlv),
	SOC_DOUBLE_TLV("ADC3 Capture Volume", RT5677_STO3_ADC_DIG_VOL,
		RT5677_STO1_ADC_L_VOL_SFT, RT5677_STO1_ADC_R_VOL_SFT, 63, 0,
		adc_vol_tlv),
	SOC_DOUBLE_TLV("ADC4 Capture Volume", RT5677_STO4_ADC_DIG_VOL,
		RT5677_STO1_ADC_L_VOL_SFT, RT5677_STO1_ADC_R_VOL_SFT, 63, 0,
		adc_vol_tlv),
	SOC_DOUBLE_TLV("Mono ADC Capture Volume", RT5677_MONO_ADC_DIG_VOL,
		RT5677_MONO_ADC_L_VOL_SFT, RT5677_MONO_ADC_R_VOL_SFT, 63, 0,
		adc_vol_tlv),

	/* Sidetone Control */
	SOC_SINGLE_TLV("Sidetone Volume", RT5677_SIDETONE_CTRL,
		RT5677_ST_VOL_SFT, 31, 0, st_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("STO1 ADC Boost Volume", RT5677_STO1_2_ADC_BST,
		RT5677_STO1_ADC_L_BST_SFT, RT5677_STO1_ADC_R_BST_SFT, 3, 0,
		adc_bst_tlv),
	SOC_DOUBLE_TLV("STO2 ADC Boost Volume", RT5677_STO1_2_ADC_BST,
		RT5677_STO2_ADC_L_BST_SFT, RT5677_STO2_ADC_R_BST_SFT, 3, 0,
		adc_bst_tlv),
	SOC_DOUBLE_TLV("STO3 ADC Boost Volume", RT5677_STO3_4_ADC_BST,
		RT5677_STO3_ADC_L_BST_SFT, RT5677_STO3_ADC_R_BST_SFT, 3, 0,
		adc_bst_tlv),
	SOC_DOUBLE_TLV("STO4 ADC Boost Volume", RT5677_STO3_4_ADC_BST,
		RT5677_STO4_ADC_L_BST_SFT, RT5677_STO4_ADC_R_BST_SFT, 3, 0,
		adc_bst_tlv),
	SOC_DOUBLE_TLV("Mono ADC Boost Volume", RT5677_ADC_BST_CTRL2,
		RT5677_MONO_ADC_L_BST_SFT, RT5677_MONO_ADC_R_BST_SFT, 3, 0,
		adc_bst_tlv),

	SOC_SINGLE_EXT("DSP VAD Switch", SND_SOC_NOPM, 0, 1, 0,
		rt5677_dsp_vad_get, rt5677_dsp_vad_put),
};

/**
 * set_dmic_clk - Set parameter of dmic.
 *
 * @w: DAPM widget.
 * @kcontrol: The kcontrol of this widget.
 * @event: Event id.
 *
 * Choose dmic clock between 1MHz and 3MHz.
 * It is better for clock to approximate 3MHz.
 */
static int set_dmic_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	int idx = rl6231_calc_dmic_clk(rt5677->sysclk);

	if (idx < 0)
		dev_err(codec->dev, "Failed to set DMIC clock\n");
	else
		regmap_update_bits(rt5677->regmap, RT5677_DMIC_CTRL1,
			RT5677_DMIC_CLK_MASK, idx << RT5677_DMIC_CLK_SFT);
	return idx;
}

static int is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(source->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	unsigned int val;

	regmap_read(rt5677->regmap, RT5677_GLB_CLK1, &val);
	val &= RT5677_SCLK_SRC_MASK;
	if (val == RT5677_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

static int is_using_asrc(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(source->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg, shift, val;

	if (source->reg == RT5677_ASRC_1) {
		switch (source->shift) {
		case 12:
			reg = RT5677_ASRC_4;
			shift = 0;
			break;
		case 13:
			reg = RT5677_ASRC_4;
			shift = 4;
			break;
		case 14:
			reg = RT5677_ASRC_4;
			shift = 8;
			break;
		case 15:
			reg = RT5677_ASRC_4;
			shift = 12;
			break;
		default:
			return 0;
		}
	} else {
		switch (source->shift) {
		case 0:
			reg = RT5677_ASRC_6;
			shift = 8;
			break;
		case 1:
			reg = RT5677_ASRC_6;
			shift = 12;
			break;
		case 2:
			reg = RT5677_ASRC_5;
			shift = 0;
			break;
		case 3:
			reg = RT5677_ASRC_5;
			shift = 4;
			break;
		case 4:
			reg = RT5677_ASRC_5;
			shift = 8;
			break;
		case 5:
			reg = RT5677_ASRC_5;
			shift = 12;
			break;
		case 12:
			reg = RT5677_ASRC_3;
			shift = 0;
			break;
		case 13:
			reg = RT5677_ASRC_3;
			shift = 4;
			break;
		case 14:
			reg = RT5677_ASRC_3;
			shift = 12;
			break;
		default:
			return 0;
		}
	}

	regmap_read(rt5677->regmap, reg, &val);
	val = (val >> shift) & 0xf;

	switch (val) {
	case 1 ... 6:
		return 1;
	default:
		return 0;
	}

}

static int can_use_asrc(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(source->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	if (rt5677->sysclk > rt5677->lrck[RT5677_AIF1] * 384)
		return 1;

	return 0;
}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5677_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_STO1_ADC_MIXER,
			RT5677_M_STO1_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_STO1_ADC_MIXER,
			RT5677_M_STO1_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_STO1_ADC_MIXER,
			RT5677_M_STO1_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_STO1_ADC_MIXER,
			RT5677_M_STO1_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto2_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_STO2_ADC_MIXER,
			RT5677_M_STO2_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_STO2_ADC_MIXER,
			RT5677_M_STO2_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto2_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_STO2_ADC_MIXER,
			RT5677_M_STO2_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_STO2_ADC_MIXER,
			RT5677_M_STO2_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto3_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_STO3_ADC_MIXER,
			RT5677_M_STO3_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_STO3_ADC_MIXER,
			RT5677_M_STO3_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto3_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_STO3_ADC_MIXER,
			RT5677_M_STO3_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_STO3_ADC_MIXER,
			RT5677_M_STO3_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto4_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_STO4_ADC_MIXER,
			RT5677_M_STO4_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_STO4_ADC_MIXER,
			RT5677_M_STO4_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto4_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_STO4_ADC_MIXER,
			RT5677_M_STO4_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_STO4_ADC_MIXER,
			RT5677_M_STO4_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_mono_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_MONO_ADC_MIXER,
			RT5677_M_MONO_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_MONO_ADC_MIXER,
			RT5677_M_MONO_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_mono_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5677_MONO_ADC_MIXER,
			RT5677_M_MONO_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5677_MONO_ADC_MIXER,
			RT5677_M_MONO_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5677_ADC_IF_DSP_DAC1_MIXER,
			RT5677_M_ADDA_MIXER1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5677_ADC_IF_DSP_DAC1_MIXER,
			RT5677_M_DAC1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5677_ADC_IF_DSP_DAC1_MIXER,
			RT5677_M_ADDA_MIXER1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5677_ADC_IF_DSP_DAC1_MIXER,
			RT5677_M_DAC1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto1_dac_l_mix[] = {
	SOC_DAPM_SINGLE("ST L Switch", RT5677_STO1_DAC_MIXER,
			RT5677_M_ST_DAC1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 L Switch", RT5677_STO1_DAC_MIXER,
			RT5677_M_DAC1_L_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC2 L Switch", RT5677_STO1_DAC_MIXER,
			RT5677_M_DAC2_L_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 R Switch", RT5677_STO1_DAC_MIXER,
			RT5677_M_DAC1_R_STO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_sto1_dac_r_mix[] = {
	SOC_DAPM_SINGLE("ST R Switch", RT5677_STO1_DAC_MIXER,
			RT5677_M_ST_DAC1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 R Switch", RT5677_STO1_DAC_MIXER,
			RT5677_M_DAC1_R_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC2 R Switch", RT5677_STO1_DAC_MIXER,
			RT5677_M_DAC2_R_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 L Switch", RT5677_STO1_DAC_MIXER,
			RT5677_M_DAC1_L_STO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_mono_dac_l_mix[] = {
	SOC_DAPM_SINGLE("ST L Switch", RT5677_MONO_DAC_MIXER,
			RT5677_M_ST_DAC2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 L Switch", RT5677_MONO_DAC_MIXER,
			RT5677_M_DAC1_L_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC2 L Switch", RT5677_MONO_DAC_MIXER,
			RT5677_M_DAC2_L_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC2 R Switch", RT5677_MONO_DAC_MIXER,
			RT5677_M_DAC2_R_MONO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_mono_dac_r_mix[] = {
	SOC_DAPM_SINGLE("ST R Switch", RT5677_MONO_DAC_MIXER,
			RT5677_M_ST_DAC2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 R Switch", RT5677_MONO_DAC_MIXER,
			RT5677_M_DAC1_R_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC2 R Switch", RT5677_MONO_DAC_MIXER,
			RT5677_M_DAC2_R_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC2 L Switch", RT5677_MONO_DAC_MIXER,
			RT5677_M_DAC2_L_MONO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_dd1_l_mix[] = {
	SOC_DAPM_SINGLE("Sto DAC Mix L Switch", RT5677_DD1_MIXER,
			RT5677_M_STO_L_DD1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mono DAC Mix L Switch", RT5677_DD1_MIXER,
			RT5677_M_MONO_L_DD1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC3 L Switch", RT5677_DD1_MIXER,
			RT5677_M_DAC3_L_DD1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC3 R Switch", RT5677_DD1_MIXER,
			RT5677_M_DAC3_R_DD1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_dd1_r_mix[] = {
	SOC_DAPM_SINGLE("Sto DAC Mix R Switch", RT5677_DD1_MIXER,
			RT5677_M_STO_R_DD1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mono DAC Mix R Switch", RT5677_DD1_MIXER,
			RT5677_M_MONO_R_DD1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC3 R Switch", RT5677_DD1_MIXER,
			RT5677_M_DAC3_R_DD1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC3 L Switch", RT5677_DD1_MIXER,
			RT5677_M_DAC3_L_DD1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_dd2_l_mix[] = {
	SOC_DAPM_SINGLE("Sto DAC Mix L Switch", RT5677_DD2_MIXER,
			RT5677_M_STO_L_DD2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mono DAC Mix L Switch", RT5677_DD2_MIXER,
			RT5677_M_MONO_L_DD2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC4 L Switch", RT5677_DD2_MIXER,
			RT5677_M_DAC4_L_DD2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC4 R Switch", RT5677_DD2_MIXER,
			RT5677_M_DAC4_R_DD2_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_dd2_r_mix[] = {
	SOC_DAPM_SINGLE("Sto DAC Mix R Switch", RT5677_DD2_MIXER,
			RT5677_M_STO_R_DD2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mono DAC Mix R Switch", RT5677_DD2_MIXER,
			RT5677_M_MONO_R_DD2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC4 R Switch", RT5677_DD2_MIXER,
			RT5677_M_DAC4_R_DD2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC4 L Switch", RT5677_DD2_MIXER,
			RT5677_M_DAC4_L_DD2_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_ob_01_mix[] = {
	SOC_DAPM_SINGLE("IB01 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_01_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB23 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_23_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB45 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_45_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB6 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_6_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB7 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_7_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB8 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_8_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB9 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_9_H_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_ob_23_mix[] = {
	SOC_DAPM_SINGLE("IB01 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_01_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB23 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_23_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB45 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_45_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB6 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_6_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB7 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_7_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB8 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_8_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB9 Switch", RT5677_DSP_OUTB_0123_MIXER_CTRL,
			RT5677_DSP_IB_9_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_ob_4_mix[] = {
	SOC_DAPM_SINGLE("IB01 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_01_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB23 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_23_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB45 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_45_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB6 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_6_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB7 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_7_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB8 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_8_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB9 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_9_H_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_ob_5_mix[] = {
	SOC_DAPM_SINGLE("IB01 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_01_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB23 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_23_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB45 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_45_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB6 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_6_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB7 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_7_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB8 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_8_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB9 Switch", RT5677_DSP_OUTB_45_MIXER_CTRL,
			RT5677_DSP_IB_9_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_ob_6_mix[] = {
	SOC_DAPM_SINGLE("IB01 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_01_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB23 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_23_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB45 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_45_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB6 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_6_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB7 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_7_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB8 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_8_H_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB9 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_9_H_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5677_ob_7_mix[] = {
	SOC_DAPM_SINGLE("IB01 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_01_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB23 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_23_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB45 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_45_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB6 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_6_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB7 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_7_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB8 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_8_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("IB9 Switch", RT5677_DSP_OUTB_67_MIXER_CTRL,
			RT5677_DSP_IB_9_L_SFT, 1, 1),
};


/* Mux */
/* DAC1 L/R Source */ /* MX-29 [10:8] */
static const char * const rt5677_dac1_src[] = {
	"IF1 DAC 01", "IF2 DAC 01", "IF3 DAC LR", "IF4 DAC LR", "SLB DAC 01",
	"OB 01"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac1_enum, RT5677_ADC_IF_DSP_DAC1_MIXER,
	RT5677_DAC1_L_SEL_SFT, rt5677_dac1_src);

static const struct snd_kcontrol_new rt5677_dac1_mux =
	SOC_DAPM_ENUM("DAC1 Source", rt5677_dac1_enum);

/* ADDA1 L/R Source */ /* MX-29 [1:0] */
static const char * const rt5677_adda1_src[] = {
	"STO1 ADC MIX", "STO2 ADC MIX", "OB 67",
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_adda1_enum, RT5677_ADC_IF_DSP_DAC1_MIXER,
	RT5677_ADDA1_SEL_SFT, rt5677_adda1_src);

static const struct snd_kcontrol_new rt5677_adda1_mux =
	SOC_DAPM_ENUM("ADDA1 Source", rt5677_adda1_enum);


/*DAC2 L/R Source*/ /* MX-1B [6:4] [2:0] */
static const char * const rt5677_dac2l_src[] = {
	"IF1 DAC 2", "IF2 DAC 2", "IF3 DAC L", "IF4 DAC L", "SLB DAC 2",
	"OB 2",
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac2l_enum, RT5677_IF_DSP_DAC2_MIXER,
	RT5677_SEL_DAC2_L_SRC_SFT, rt5677_dac2l_src);

static const struct snd_kcontrol_new rt5677_dac2_l_mux =
	SOC_DAPM_ENUM("DAC2 L Source", rt5677_dac2l_enum);

static const char * const rt5677_dac2r_src[] = {
	"IF1 DAC 3", "IF2 DAC 3", "IF3 DAC R", "IF4 DAC R", "SLB DAC 3",
	"OB 3", "Haptic Generator", "VAD ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac2r_enum, RT5677_IF_DSP_DAC2_MIXER,
	RT5677_SEL_DAC2_R_SRC_SFT, rt5677_dac2r_src);

static const struct snd_kcontrol_new rt5677_dac2_r_mux =
	SOC_DAPM_ENUM("DAC2 R Source", rt5677_dac2r_enum);

/*DAC3 L/R Source*/ /* MX-16 [6:4] [2:0] */
static const char * const rt5677_dac3l_src[] = {
	"IF1 DAC 4", "IF2 DAC 4", "IF3 DAC L", "IF4 DAC L",
	"SLB DAC 4", "OB 4"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac3l_enum, RT5677_IF_DSP_DAC3_4_MIXER,
	RT5677_SEL_DAC3_L_SRC_SFT, rt5677_dac3l_src);

static const struct snd_kcontrol_new rt5677_dac3_l_mux =
	SOC_DAPM_ENUM("DAC3 L Source", rt5677_dac3l_enum);

static const char * const rt5677_dac3r_src[] = {
	"IF1 DAC 5", "IF2 DAC 5", "IF3 DAC R", "IF4 DAC R",
	"SLB DAC 5", "OB 5"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac3r_enum, RT5677_IF_DSP_DAC3_4_MIXER,
	RT5677_SEL_DAC3_R_SRC_SFT, rt5677_dac3r_src);

static const struct snd_kcontrol_new rt5677_dac3_r_mux =
	SOC_DAPM_ENUM("DAC3 R Source", rt5677_dac3r_enum);

/*DAC4 L/R Source*/ /* MX-16 [14:12] [10:8] */
static const char * const rt5677_dac4l_src[] = {
	"IF1 DAC 6", "IF2 DAC 6", "IF3 DAC L", "IF4 DAC L",
	"SLB DAC 6", "OB 6"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac4l_enum, RT5677_IF_DSP_DAC3_4_MIXER,
	RT5677_SEL_DAC4_L_SRC_SFT, rt5677_dac4l_src);

static const struct snd_kcontrol_new rt5677_dac4_l_mux =
	SOC_DAPM_ENUM("DAC4 L Source", rt5677_dac4l_enum);

static const char * const rt5677_dac4r_src[] = {
	"IF1 DAC 7", "IF2 DAC 7", "IF3 DAC R", "IF4 DAC R",
	"SLB DAC 7", "OB 7"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac4r_enum, RT5677_IF_DSP_DAC3_4_MIXER,
	RT5677_SEL_DAC4_R_SRC_SFT, rt5677_dac4r_src);

static const struct snd_kcontrol_new rt5677_dac4_r_mux =
	SOC_DAPM_ENUM("DAC4 R Source", rt5677_dac4r_enum);

/* In/OutBound Source Pass SRC */ /* MX-A5 [3] [4] [0] [1] [2] */
static const char * const rt5677_iob_bypass_src[] = {
	"Bypass", "Pass SRC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_ob01_bypass_src_enum, RT5677_DSP_IN_OUTB_CTRL,
	RT5677_SEL_SRC_OB01_SFT, rt5677_iob_bypass_src);

static const struct snd_kcontrol_new rt5677_ob01_bypass_src_mux =
	SOC_DAPM_ENUM("OB01 Bypass Source", rt5677_ob01_bypass_src_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_ob23_bypass_src_enum, RT5677_DSP_IN_OUTB_CTRL,
	RT5677_SEL_SRC_OB23_SFT, rt5677_iob_bypass_src);

static const struct snd_kcontrol_new rt5677_ob23_bypass_src_mux =
	SOC_DAPM_ENUM("OB23 Bypass Source", rt5677_ob23_bypass_src_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_ib01_bypass_src_enum, RT5677_DSP_IN_OUTB_CTRL,
	RT5677_SEL_SRC_IB01_SFT, rt5677_iob_bypass_src);

static const struct snd_kcontrol_new rt5677_ib01_bypass_src_mux =
	SOC_DAPM_ENUM("IB01 Bypass Source", rt5677_ib01_bypass_src_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_ib23_bypass_src_enum, RT5677_DSP_IN_OUTB_CTRL,
	RT5677_SEL_SRC_IB23_SFT, rt5677_iob_bypass_src);

static const struct snd_kcontrol_new rt5677_ib23_bypass_src_mux =
	SOC_DAPM_ENUM("IB23 Bypass Source", rt5677_ib23_bypass_src_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_ib45_bypass_src_enum, RT5677_DSP_IN_OUTB_CTRL,
	RT5677_SEL_SRC_IB45_SFT, rt5677_iob_bypass_src);

static const struct snd_kcontrol_new rt5677_ib45_bypass_src_mux =
	SOC_DAPM_ENUM("IB45 Bypass Source", rt5677_ib45_bypass_src_enum);

/* Stereo ADC Source 2 */ /* MX-27 MX26 MX25 [11:10] */
static const char * const rt5677_stereo_adc2_src[] = {
	"DD MIX1", "DMIC", "Stereo DAC MIX"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo1_adc2_enum, RT5677_STO1_ADC_MIXER,
	RT5677_SEL_STO1_ADC2_SFT, rt5677_stereo_adc2_src);

static const struct snd_kcontrol_new rt5677_sto1_adc2_mux =
	SOC_DAPM_ENUM("Stereo1 ADC2 Source", rt5677_stereo1_adc2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo2_adc2_enum, RT5677_STO2_ADC_MIXER,
	RT5677_SEL_STO2_ADC2_SFT, rt5677_stereo_adc2_src);

static const struct snd_kcontrol_new rt5677_sto2_adc2_mux =
	SOC_DAPM_ENUM("Stereo2 ADC2 Source", rt5677_stereo2_adc2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo3_adc2_enum, RT5677_STO3_ADC_MIXER,
	RT5677_SEL_STO3_ADC2_SFT, rt5677_stereo_adc2_src);

static const struct snd_kcontrol_new rt5677_sto3_adc2_mux =
	SOC_DAPM_ENUM("Stereo3 ADC2 Source", rt5677_stereo3_adc2_enum);

/* DMIC Source */ /* MX-28 [9:8][1:0] MX-27 MX-26 MX-25 MX-24 [9:8] */
static const char * const rt5677_dmic_src[] = {
	"DMIC1", "DMIC2", "DMIC3", "DMIC4"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_mono_dmic_l_enum, RT5677_MONO_ADC_MIXER,
	RT5677_SEL_MONO_DMIC_L_SFT, rt5677_dmic_src);

static const struct snd_kcontrol_new rt5677_mono_dmic_l_mux =
	SOC_DAPM_ENUM("Mono DMIC L Source", rt5677_mono_dmic_l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_mono_dmic_r_enum, RT5677_MONO_ADC_MIXER,
	RT5677_SEL_MONO_DMIC_R_SFT, rt5677_dmic_src);

static const struct snd_kcontrol_new rt5677_mono_dmic_r_mux =
	SOC_DAPM_ENUM("Mono DMIC R Source", rt5677_mono_dmic_r_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo1_dmic_enum, RT5677_STO1_ADC_MIXER,
	RT5677_SEL_STO1_DMIC_SFT, rt5677_dmic_src);

static const struct snd_kcontrol_new rt5677_sto1_dmic_mux =
	SOC_DAPM_ENUM("Stereo1 DMIC Source", rt5677_stereo1_dmic_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo2_dmic_enum, RT5677_STO2_ADC_MIXER,
	RT5677_SEL_STO2_DMIC_SFT, rt5677_dmic_src);

static const struct snd_kcontrol_new rt5677_sto2_dmic_mux =
	SOC_DAPM_ENUM("Stereo2 DMIC Source", rt5677_stereo2_dmic_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo3_dmic_enum, RT5677_STO3_ADC_MIXER,
	RT5677_SEL_STO3_DMIC_SFT, rt5677_dmic_src);

static const struct snd_kcontrol_new rt5677_sto3_dmic_mux =
	SOC_DAPM_ENUM("Stereo3 DMIC Source", rt5677_stereo3_dmic_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo4_dmic_enum, RT5677_STO4_ADC_MIXER,
	RT5677_SEL_STO4_DMIC_SFT, rt5677_dmic_src);

static const struct snd_kcontrol_new rt5677_sto4_dmic_mux =
	SOC_DAPM_ENUM("Stereo4 DMIC Source", rt5677_stereo4_dmic_enum);

/* Stereo2 ADC Source */ /* MX-26 [0] */
static const char * const rt5677_stereo2_adc_lr_src[] = {
	"L", "LR"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo2_adc_lr_enum, RT5677_STO2_ADC_MIXER,
	RT5677_SEL_STO2_LR_MIX_SFT, rt5677_stereo2_adc_lr_src);

static const struct snd_kcontrol_new rt5677_sto2_adc_lr_mux =
	SOC_DAPM_ENUM("Stereo2 ADC LR Source", rt5677_stereo2_adc_lr_enum);

/* Stereo1 ADC Source 1 */ /* MX-27 MX26 MX25 [13:12] */
static const char * const rt5677_stereo_adc1_src[] = {
	"DD MIX1", "ADC1/2", "Stereo DAC MIX"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo1_adc1_enum, RT5677_STO1_ADC_MIXER,
	RT5677_SEL_STO1_ADC1_SFT, rt5677_stereo_adc1_src);

static const struct snd_kcontrol_new rt5677_sto1_adc1_mux =
	SOC_DAPM_ENUM("Stereo1 ADC1 Source", rt5677_stereo1_adc1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo2_adc1_enum, RT5677_STO2_ADC_MIXER,
	RT5677_SEL_STO2_ADC1_SFT, rt5677_stereo_adc1_src);

static const struct snd_kcontrol_new rt5677_sto2_adc1_mux =
	SOC_DAPM_ENUM("Stereo2 ADC1 Source", rt5677_stereo2_adc1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo3_adc1_enum, RT5677_STO3_ADC_MIXER,
	RT5677_SEL_STO3_ADC1_SFT, rt5677_stereo_adc1_src);

static const struct snd_kcontrol_new rt5677_sto3_adc1_mux =
	SOC_DAPM_ENUM("Stereo3 ADC1 Source", rt5677_stereo3_adc1_enum);

/* Mono ADC Left Source 2 */ /* MX-28 [11:10] */
static const char * const rt5677_mono_adc2_l_src[] = {
	"DD MIX1L", "DMIC", "MONO DAC MIXL"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_mono_adc2_l_enum, RT5677_MONO_ADC_MIXER,
	RT5677_SEL_MONO_ADC_L2_SFT, rt5677_mono_adc2_l_src);

static const struct snd_kcontrol_new rt5677_mono_adc2_l_mux =
	SOC_DAPM_ENUM("Mono ADC2 L Source", rt5677_mono_adc2_l_enum);

/* Mono ADC Left Source 1 */ /* MX-28 [13:12] */
static const char * const rt5677_mono_adc1_l_src[] = {
	"DD MIX1L", "ADC1", "MONO DAC MIXL"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_mono_adc1_l_enum, RT5677_MONO_ADC_MIXER,
	RT5677_SEL_MONO_ADC_L1_SFT, rt5677_mono_adc1_l_src);

static const struct snd_kcontrol_new rt5677_mono_adc1_l_mux =
	SOC_DAPM_ENUM("Mono ADC1 L Source", rt5677_mono_adc1_l_enum);

/* Mono ADC Right Source 2 */ /* MX-28 [3:2] */
static const char * const rt5677_mono_adc2_r_src[] = {
	"DD MIX1R", "DMIC", "MONO DAC MIXR"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_mono_adc2_r_enum, RT5677_MONO_ADC_MIXER,
	RT5677_SEL_MONO_ADC_R2_SFT, rt5677_mono_adc2_r_src);

static const struct snd_kcontrol_new rt5677_mono_adc2_r_mux =
	SOC_DAPM_ENUM("Mono ADC2 R Source", rt5677_mono_adc2_r_enum);

/* Mono ADC Right Source 1 */ /* MX-28 [5:4] */
static const char * const rt5677_mono_adc1_r_src[] = {
	"DD MIX1R", "ADC2", "MONO DAC MIXR"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_mono_adc1_r_enum, RT5677_MONO_ADC_MIXER,
	RT5677_SEL_MONO_ADC_R1_SFT, rt5677_mono_adc1_r_src);

static const struct snd_kcontrol_new rt5677_mono_adc1_r_mux =
	SOC_DAPM_ENUM("Mono ADC1 R Source", rt5677_mono_adc1_r_enum);

/* Stereo4 ADC Source 2 */ /* MX-24 [11:10] */
static const char * const rt5677_stereo4_adc2_src[] = {
	"DD MIX1", "DMIC", "DD MIX2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo4_adc2_enum, RT5677_STO4_ADC_MIXER,
	RT5677_SEL_STO4_ADC2_SFT, rt5677_stereo4_adc2_src);

static const struct snd_kcontrol_new rt5677_sto4_adc2_mux =
	SOC_DAPM_ENUM("Stereo4 ADC2 Source", rt5677_stereo4_adc2_enum);


/* Stereo4 ADC Source 1 */ /* MX-24 [13:12] */
static const char * const rt5677_stereo4_adc1_src[] = {
	"DD MIX1", "ADC1/2", "DD MIX2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_stereo4_adc1_enum, RT5677_STO4_ADC_MIXER,
	RT5677_SEL_STO4_ADC1_SFT, rt5677_stereo4_adc1_src);

static const struct snd_kcontrol_new rt5677_sto4_adc1_mux =
	SOC_DAPM_ENUM("Stereo4 ADC1 Source", rt5677_stereo4_adc1_enum);

/* InBound0/1 Source */ /* MX-A3 [14:12] */
static const char * const rt5677_inbound01_src[] = {
	"IF1 DAC 01", "IF2 DAC 01", "SLB DAC 01", "STO1 ADC MIX",
	"VAD ADC/DAC1 FS"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_inbound01_enum, RT5677_DSP_INB_CTRL1,
	RT5677_IB01_SRC_SFT, rt5677_inbound01_src);

static const struct snd_kcontrol_new rt5677_ib01_src_mux =
	SOC_DAPM_ENUM("InBound0/1 Source", rt5677_inbound01_enum);

/* InBound2/3 Source */ /* MX-A3 [10:8] */
static const char * const rt5677_inbound23_src[] = {
	"IF1 DAC 23", "IF2 DAC 23", "SLB DAC 23", "STO2 ADC MIX",
	"DAC1 FS", "IF4 DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_inbound23_enum, RT5677_DSP_INB_CTRL1,
	RT5677_IB23_SRC_SFT, rt5677_inbound23_src);

static const struct snd_kcontrol_new rt5677_ib23_src_mux =
	SOC_DAPM_ENUM("InBound2/3 Source", rt5677_inbound23_enum);

/* InBound4/5 Source */ /* MX-A3 [6:4] */
static const char * const rt5677_inbound45_src[] = {
	"IF1 DAC 45", "IF2 DAC 45", "SLB DAC 45", "STO3 ADC MIX",
	"IF3 DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_inbound45_enum, RT5677_DSP_INB_CTRL1,
	RT5677_IB45_SRC_SFT, rt5677_inbound45_src);

static const struct snd_kcontrol_new rt5677_ib45_src_mux =
	SOC_DAPM_ENUM("InBound4/5 Source", rt5677_inbound45_enum);

/* InBound6 Source */ /* MX-A3 [2:0] */
static const char * const rt5677_inbound6_src[] = {
	"IF1 DAC 6", "IF2 DAC 6", "SLB DAC 6", "STO4 ADC MIX L",
	"IF4 DAC L", "STO1 ADC MIX L", "STO2 ADC MIX L", "STO3 ADC MIX L"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_inbound6_enum, RT5677_DSP_INB_CTRL1,
	RT5677_IB6_SRC_SFT, rt5677_inbound6_src);

static const struct snd_kcontrol_new rt5677_ib6_src_mux =
	SOC_DAPM_ENUM("InBound6 Source", rt5677_inbound6_enum);

/* InBound7 Source */ /* MX-A4 [14:12] */
static const char * const rt5677_inbound7_src[] = {
	"IF1 DAC 7", "IF2 DAC 7", "SLB DAC 7", "STO4 ADC MIX R",
	"IF4 DAC R", "STO1 ADC MIX R", "STO2 ADC MIX R", "STO3 ADC MIX R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_inbound7_enum, RT5677_DSP_INB_CTRL2,
	RT5677_IB7_SRC_SFT, rt5677_inbound7_src);

static const struct snd_kcontrol_new rt5677_ib7_src_mux =
	SOC_DAPM_ENUM("InBound7 Source", rt5677_inbound7_enum);

/* InBound8 Source */ /* MX-A4 [10:8] */
static const char * const rt5677_inbound8_src[] = {
	"STO1 ADC MIX L", "STO2 ADC MIX L", "STO3 ADC MIX L", "STO4 ADC MIX L",
	"MONO ADC MIX L", "DACL1 FS"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_inbound8_enum, RT5677_DSP_INB_CTRL2,
	RT5677_IB8_SRC_SFT, rt5677_inbound8_src);

static const struct snd_kcontrol_new rt5677_ib8_src_mux =
	SOC_DAPM_ENUM("InBound8 Source", rt5677_inbound8_enum);

/* InBound9 Source */ /* MX-A4 [6:4] */
static const char * const rt5677_inbound9_src[] = {
	"STO1 ADC MIX R", "STO2 ADC MIX R", "STO3 ADC MIX R", "STO4 ADC MIX R",
	"MONO ADC MIX R", "DACR1 FS", "DAC1 FS"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_inbound9_enum, RT5677_DSP_INB_CTRL2,
	RT5677_IB9_SRC_SFT, rt5677_inbound9_src);

static const struct snd_kcontrol_new rt5677_ib9_src_mux =
	SOC_DAPM_ENUM("InBound9 Source", rt5677_inbound9_enum);

/* VAD Source */ /* MX-9F [6:4] */
static const char * const rt5677_vad_src[] = {
	"STO1 ADC MIX L", "MONO ADC MIX L", "MONO ADC MIX R", "STO2 ADC MIX L",
	"STO3 ADC MIX L"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_vad_enum, RT5677_VAD_CTRL4,
	RT5677_VAD_SRC_SFT, rt5677_vad_src);

static const struct snd_kcontrol_new rt5677_vad_src_mux =
	SOC_DAPM_ENUM("VAD Source", rt5677_vad_enum);

/* Sidetone Source */ /* MX-13 [11:9] */
static const char * const rt5677_sidetone_src[] = {
	"DMIC1 L", "DMIC2 L", "DMIC3 L", "DMIC4 L", "ADC1", "ADC2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_sidetone_enum, RT5677_SIDETONE_CTRL,
	RT5677_ST_SEL_SFT, rt5677_sidetone_src);

static const struct snd_kcontrol_new rt5677_sidetone_mux =
	SOC_DAPM_ENUM("Sidetone Source", rt5677_sidetone_enum);

/* DAC1/2 Source */ /* MX-15 [1:0] */
static const char * const rt5677_dac12_src[] = {
	"STO1 DAC MIX", "MONO DAC MIX", "DD MIX1", "DD MIX2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac12_enum, RT5677_ANA_DAC1_2_3_SRC,
	RT5677_ANA_DAC1_2_SRC_SEL_SFT, rt5677_dac12_src);

static const struct snd_kcontrol_new rt5677_dac12_mux =
	SOC_DAPM_ENUM("Analog DAC1/2 Source", rt5677_dac12_enum);

/* DAC3 Source */ /* MX-15 [5:4] */
static const char * const rt5677_dac3_src[] = {
	"MONO DAC MIXL", "MONO DAC MIXR", "DD MIX1L", "DD MIX2L"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_dac3_enum, RT5677_ANA_DAC1_2_3_SRC,
	RT5677_ANA_DAC3_SRC_SEL_SFT, rt5677_dac3_src);

static const struct snd_kcontrol_new rt5677_dac3_mux =
	SOC_DAPM_ENUM("Analog DAC3 Source", rt5677_dac3_enum);

/* PDM channel Source */ /* MX-31 [13:12][9:8][5:4][1:0] */
static const char * const rt5677_pdm_src[] = {
	"STO1 DAC MIX", "MONO DAC MIX", "DD MIX1", "DD MIX2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_pdm1_l_enum, RT5677_PDM_OUT_CTRL,
	RT5677_SEL_PDM1_L_SFT, rt5677_pdm_src);

static const struct snd_kcontrol_new rt5677_pdm1_l_mux =
	SOC_DAPM_ENUM("PDM1 Source", rt5677_pdm1_l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_pdm2_l_enum, RT5677_PDM_OUT_CTRL,
	RT5677_SEL_PDM2_L_SFT, rt5677_pdm_src);

static const struct snd_kcontrol_new rt5677_pdm2_l_mux =
	SOC_DAPM_ENUM("PDM2 Source", rt5677_pdm2_l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_pdm1_r_enum, RT5677_PDM_OUT_CTRL,
	RT5677_SEL_PDM1_R_SFT, rt5677_pdm_src);

static const struct snd_kcontrol_new rt5677_pdm1_r_mux =
	SOC_DAPM_ENUM("PDM1 Source", rt5677_pdm1_r_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_pdm2_r_enum, RT5677_PDM_OUT_CTRL,
	RT5677_SEL_PDM2_R_SFT, rt5677_pdm_src);

static const struct snd_kcontrol_new rt5677_pdm2_r_mux =
	SOC_DAPM_ENUM("PDM2 Source", rt5677_pdm2_r_enum);

/* TDM IF1/2 SLB ADC1 Data Selection */ /* MX-3C MX-41 [5:4] MX-08 [1:0] */
static const char * const rt5677_if12_adc1_src[] = {
	"STO1 ADC MIX", "OB01", "VAD ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc1_enum, RT5677_TDM1_CTRL2,
	RT5677_IF1_ADC1_SFT, rt5677_if12_adc1_src);

static const struct snd_kcontrol_new rt5677_if1_adc1_mux =
	SOC_DAPM_ENUM("IF1 ADC1 Source", rt5677_if1_adc1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc1_enum, RT5677_TDM2_CTRL2,
	RT5677_IF2_ADC1_SFT, rt5677_if12_adc1_src);

static const struct snd_kcontrol_new rt5677_if2_adc1_mux =
	SOC_DAPM_ENUM("IF2 ADC1 Source", rt5677_if2_adc1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_slb_adc1_enum, RT5677_SLIMBUS_RX,
	RT5677_SLB_ADC1_SFT, rt5677_if12_adc1_src);

static const struct snd_kcontrol_new rt5677_slb_adc1_mux =
	SOC_DAPM_ENUM("SLB ADC1 Source", rt5677_slb_adc1_enum);

/* TDM IF1/2 SLB ADC2 Data Selection */ /* MX-3C MX-41 [7:6] MX-08 [3:2] */
static const char * const rt5677_if12_adc2_src[] = {
	"STO2 ADC MIX", "OB23"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc2_enum, RT5677_TDM1_CTRL2,
	RT5677_IF1_ADC2_SFT, rt5677_if12_adc2_src);

static const struct snd_kcontrol_new rt5677_if1_adc2_mux =
	SOC_DAPM_ENUM("IF1 ADC2 Source", rt5677_if1_adc2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc2_enum, RT5677_TDM2_CTRL2,
	RT5677_IF2_ADC2_SFT, rt5677_if12_adc2_src);

static const struct snd_kcontrol_new rt5677_if2_adc2_mux =
	SOC_DAPM_ENUM("IF2 ADC2 Source", rt5677_if2_adc2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_slb_adc2_enum, RT5677_SLIMBUS_RX,
	RT5677_SLB_ADC2_SFT, rt5677_if12_adc2_src);

static const struct snd_kcontrol_new rt5677_slb_adc2_mux =
	SOC_DAPM_ENUM("SLB ADC2 Source", rt5677_slb_adc2_enum);

/* TDM IF1/2 SLB ADC3 Data Selection */ /* MX-3C MX-41 [9:8] MX-08 [5:4] */
static const char * const rt5677_if12_adc3_src[] = {
	"STO3 ADC MIX", "MONO ADC MIX", "OB45"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc3_enum, RT5677_TDM1_CTRL2,
	RT5677_IF1_ADC3_SFT, rt5677_if12_adc3_src);

static const struct snd_kcontrol_new rt5677_if1_adc3_mux =
	SOC_DAPM_ENUM("IF1 ADC3 Source", rt5677_if1_adc3_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc3_enum, RT5677_TDM2_CTRL2,
	RT5677_IF2_ADC3_SFT, rt5677_if12_adc3_src);

static const struct snd_kcontrol_new rt5677_if2_adc3_mux =
	SOC_DAPM_ENUM("IF2 ADC3 Source", rt5677_if2_adc3_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_slb_adc3_enum, RT5677_SLIMBUS_RX,
	RT5677_SLB_ADC3_SFT, rt5677_if12_adc3_src);

static const struct snd_kcontrol_new rt5677_slb_adc3_mux =
	SOC_DAPM_ENUM("SLB ADC3 Source", rt5677_slb_adc3_enum);

/* TDM IF1/2 SLB ADC4 Data Selection */ /* MX-3C MX-41 [11:10] MX-08 [7:6] */
static const char * const rt5677_if12_adc4_src[] = {
	"STO4 ADC MIX", "OB67", "OB01"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc4_enum, RT5677_TDM1_CTRL2,
	RT5677_IF1_ADC4_SFT, rt5677_if12_adc4_src);

static const struct snd_kcontrol_new rt5677_if1_adc4_mux =
	SOC_DAPM_ENUM("IF1 ADC4 Source", rt5677_if1_adc4_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc4_enum, RT5677_TDM2_CTRL2,
	RT5677_IF2_ADC4_SFT, rt5677_if12_adc4_src);

static const struct snd_kcontrol_new rt5677_if2_adc4_mux =
	SOC_DAPM_ENUM("IF2 ADC4 Source", rt5677_if2_adc4_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_slb_adc4_enum, RT5677_SLIMBUS_RX,
	RT5677_SLB_ADC4_SFT, rt5677_if12_adc4_src);

static const struct snd_kcontrol_new rt5677_slb_adc4_mux =
	SOC_DAPM_ENUM("SLB ADC4 Source", rt5677_slb_adc4_enum);

/* Interface3/4 ADC Data Input */ /* MX-2F [3:0] MX-30 [7:4] */
static const char * const rt5677_if34_adc_src[] = {
	"STO1 ADC MIX", "STO2 ADC MIX", "STO3 ADC MIX", "STO4 ADC MIX",
	"MONO ADC MIX", "OB01", "OB23", "VAD ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if3_adc_enum, RT5677_IF3_DATA,
	RT5677_IF3_ADC_IN_SFT, rt5677_if34_adc_src);

static const struct snd_kcontrol_new rt5677_if3_adc_mux =
	SOC_DAPM_ENUM("IF3 ADC Source", rt5677_if3_adc_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if4_adc_enum, RT5677_IF4_DATA,
	RT5677_IF4_ADC_IN_SFT, rt5677_if34_adc_src);

static const struct snd_kcontrol_new rt5677_if4_adc_mux =
	SOC_DAPM_ENUM("IF4 ADC Source", rt5677_if4_adc_enum);

/* TDM IF1/2 ADC Data Selection */ /* MX-3B MX-40 [7:6][5:4][3:2][1:0] */
static const char * const rt5677_if12_adc_swap_src[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc1_swap_enum, RT5677_TDM1_CTRL1,
	RT5677_IF1_ADC1_SWAP_SFT, rt5677_if12_adc_swap_src);

static const struct snd_kcontrol_new rt5677_if1_adc1_swap_mux =
	SOC_DAPM_ENUM("IF1 ADC1 Swap Source", rt5677_if1_adc1_swap_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc2_swap_enum, RT5677_TDM1_CTRL1,
	RT5677_IF1_ADC2_SWAP_SFT, rt5677_if12_adc_swap_src);

static const struct snd_kcontrol_new rt5677_if1_adc2_swap_mux =
	SOC_DAPM_ENUM("IF1 ADC2 Swap Source", rt5677_if1_adc2_swap_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc3_swap_enum, RT5677_TDM1_CTRL1,
	RT5677_IF1_ADC3_SWAP_SFT, rt5677_if12_adc_swap_src);

static const struct snd_kcontrol_new rt5677_if1_adc3_swap_mux =
	SOC_DAPM_ENUM("IF1 ADC3 Swap Source", rt5677_if1_adc3_swap_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc4_swap_enum, RT5677_TDM1_CTRL1,
	RT5677_IF1_ADC4_SWAP_SFT, rt5677_if12_adc_swap_src);

static const struct snd_kcontrol_new rt5677_if1_adc4_swap_mux =
	SOC_DAPM_ENUM("IF1 ADC4 Swap Source", rt5677_if1_adc4_swap_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc1_swap_enum, RT5677_TDM2_CTRL1,
	RT5677_IF1_ADC2_SWAP_SFT, rt5677_if12_adc_swap_src);

static const struct snd_kcontrol_new rt5677_if2_adc1_swap_mux =
	SOC_DAPM_ENUM("IF1 ADC2 Swap Source", rt5677_if2_adc1_swap_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc2_swap_enum, RT5677_TDM2_CTRL1,
	RT5677_IF2_ADC2_SWAP_SFT, rt5677_if12_adc_swap_src);

static const struct snd_kcontrol_new rt5677_if2_adc2_swap_mux =
	SOC_DAPM_ENUM("IF2 ADC2 Swap Source", rt5677_if2_adc2_swap_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc3_swap_enum, RT5677_TDM2_CTRL1,
	RT5677_IF2_ADC3_SWAP_SFT, rt5677_if12_adc_swap_src);

static const struct snd_kcontrol_new rt5677_if2_adc3_swap_mux =
	SOC_DAPM_ENUM("IF2 ADC3 Swap Source", rt5677_if2_adc3_swap_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc4_swap_enum, RT5677_TDM2_CTRL1,
	RT5677_IF2_ADC4_SWAP_SFT, rt5677_if12_adc_swap_src);

static const struct snd_kcontrol_new rt5677_if2_adc4_swap_mux =
	SOC_DAPM_ENUM("IF2 ADC4 Swap Source", rt5677_if2_adc4_swap_enum);

/* TDM IF1 ADC Data Selection */ /* MX-3C [2:0] */
static const char * const rt5677_if1_adc_tdm_swap_src[] = {
	"1/2/3/4", "2/1/3/4", "2/3/1/4", "4/1/2/3", "1/3/2/4", "1/4/2/3",
	"3/1/2/4", "3/4/1/2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_adc_tdm_swap_enum, RT5677_TDM1_CTRL2,
	RT5677_IF1_ADC_CTRL_SFT, rt5677_if1_adc_tdm_swap_src);

static const struct snd_kcontrol_new rt5677_if1_adc_tdm_swap_mux =
	SOC_DAPM_ENUM("IF1 ADC TDM Swap Source", rt5677_if1_adc_tdm_swap_enum);

/* TDM IF2 ADC Data Selection */ /* MX-41[2:0] */
static const char * const rt5677_if2_adc_tdm_swap_src[] = {
	"1/2/3/4", "2/1/3/4", "3/1/2/4", "4/1/2/3", "1/3/2/4", "1/4/2/3",
	"2/3/1/4", "3/4/1/2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_adc_tdm_swap_enum, RT5677_TDM2_CTRL2,
	RT5677_IF2_ADC_CTRL_SFT, rt5677_if2_adc_tdm_swap_src);

static const struct snd_kcontrol_new rt5677_if2_adc_tdm_swap_mux =
	SOC_DAPM_ENUM("IF2 ADC TDM Swap Source", rt5677_if2_adc_tdm_swap_enum);

/* TDM IF1/2 DAC Data Selection */ /* MX-3E[14:12][10:8][6:4][2:0]
					MX-3F[14:12][10:8][6:4][2:0]
					MX-43[14:12][10:8][6:4][2:0]
					MX-44[14:12][10:8][6:4][2:0] */
static const char * const rt5677_if12_dac_tdm_sel_src[] = {
	"Slot0", "Slot1", "Slot2", "Slot3", "Slot4", "Slot5", "Slot6", "Slot7"
};

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_dac0_tdm_sel_enum, RT5677_TDM1_CTRL4,
	RT5677_IF1_DAC0_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if1_dac0_tdm_sel_mux =
	SOC_DAPM_ENUM("IF1 DAC0 TDM Source", rt5677_if1_dac0_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_dac1_tdm_sel_enum, RT5677_TDM1_CTRL4,
	RT5677_IF1_DAC1_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if1_dac1_tdm_sel_mux =
	SOC_DAPM_ENUM("IF1 DAC1 TDM Source", rt5677_if1_dac1_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_dac2_tdm_sel_enum, RT5677_TDM1_CTRL4,
	RT5677_IF1_DAC2_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if1_dac2_tdm_sel_mux =
	SOC_DAPM_ENUM("IF1 DAC2 TDM Source", rt5677_if1_dac2_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_dac3_tdm_sel_enum, RT5677_TDM1_CTRL4,
	RT5677_IF1_DAC3_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if1_dac3_tdm_sel_mux =
	SOC_DAPM_ENUM("IF1 DAC3 TDM Source", rt5677_if1_dac3_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_dac4_tdm_sel_enum, RT5677_TDM1_CTRL5,
	RT5677_IF1_DAC4_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if1_dac4_tdm_sel_mux =
	SOC_DAPM_ENUM("IF1 DAC4 TDM Source", rt5677_if1_dac4_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_dac5_tdm_sel_enum, RT5677_TDM1_CTRL5,
	RT5677_IF1_DAC5_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if1_dac5_tdm_sel_mux =
	SOC_DAPM_ENUM("IF1 DAC5 TDM Source", rt5677_if1_dac5_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_dac6_tdm_sel_enum, RT5677_TDM1_CTRL5,
	RT5677_IF1_DAC6_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if1_dac6_tdm_sel_mux =
	SOC_DAPM_ENUM("IF1 DAC6 TDM Source", rt5677_if1_dac6_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if1_dac7_tdm_sel_enum, RT5677_TDM1_CTRL5,
	RT5677_IF1_DAC7_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if1_dac7_tdm_sel_mux =
	SOC_DAPM_ENUM("IF1 DAC7 TDM Source", rt5677_if1_dac7_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_dac0_tdm_sel_enum, RT5677_TDM2_CTRL4,
	RT5677_IF2_DAC0_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if2_dac0_tdm_sel_mux =
	SOC_DAPM_ENUM("IF2 DAC0 TDM Source", rt5677_if2_dac0_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_dac1_tdm_sel_enum, RT5677_TDM2_CTRL4,
	RT5677_IF2_DAC1_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if2_dac1_tdm_sel_mux =
	SOC_DAPM_ENUM("IF2 DAC1 TDM Source", rt5677_if2_dac1_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_dac2_tdm_sel_enum, RT5677_TDM2_CTRL4,
	RT5677_IF2_DAC2_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if2_dac2_tdm_sel_mux =
	SOC_DAPM_ENUM("IF2 DAC2 TDM Source", rt5677_if2_dac2_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_dac3_tdm_sel_enum, RT5677_TDM2_CTRL4,
	RT5677_IF2_DAC3_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if2_dac3_tdm_sel_mux =
	SOC_DAPM_ENUM("IF2 DAC3 TDM Source", rt5677_if2_dac3_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_dac4_tdm_sel_enum, RT5677_TDM2_CTRL5,
	RT5677_IF2_DAC4_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if2_dac4_tdm_sel_mux =
	SOC_DAPM_ENUM("IF2 DAC4 TDM Source", rt5677_if2_dac4_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_dac5_tdm_sel_enum, RT5677_TDM2_CTRL5,
	RT5677_IF2_DAC5_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if2_dac5_tdm_sel_mux =
	SOC_DAPM_ENUM("IF2 DAC5 TDM Source", rt5677_if2_dac5_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_dac6_tdm_sel_enum, RT5677_TDM2_CTRL5,
	RT5677_IF2_DAC6_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if2_dac6_tdm_sel_mux =
	SOC_DAPM_ENUM("IF2 DAC6 TDM Source", rt5677_if2_dac6_tdm_sel_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5677_if2_dac7_tdm_sel_enum, RT5677_TDM2_CTRL5,
	RT5677_IF2_DAC7_SFT, rt5677_if12_dac_tdm_sel_src);

static const struct snd_kcontrol_new rt5677_if2_dac7_tdm_sel_mux =
	SOC_DAPM_ENUM("IF2 DAC7 TDM Source", rt5677_if2_dac7_tdm_sel_enum);

static int rt5677_bst1_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG2,
			RT5677_PWR_BST1_P, RT5677_PWR_BST1_P);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG2,
			RT5677_PWR_BST1_P, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5677_bst2_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG2,
			RT5677_PWR_BST2_P, RT5677_PWR_BST2_P);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG2,
			RT5677_PWR_BST2_P, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5677_set_pll1_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(rt5677->regmap, RT5677_PLL1_CTRL2, 0x2, 0x2);
		break;

	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(rt5677->regmap, RT5677_PLL1_CTRL2, 0x2, 0x0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5677_set_pll2_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(rt5677->regmap, RT5677_PLL2_CTRL2, 0x2, 0x2);
		break;

	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(rt5677->regmap, RT5677_PLL2_CTRL2, 0x2, 0x0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5677_set_micbias1_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG2,
			RT5677_PWR_CLK_MB1 | RT5677_PWR_PP_MB1 |
			RT5677_PWR_CLK_MB, RT5677_PWR_CLK_MB1 |
			RT5677_PWR_PP_MB1 | RT5677_PWR_CLK_MB);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG2,
			RT5677_PWR_CLK_MB1 | RT5677_PWR_PP_MB1 |
			RT5677_PWR_CLK_MB, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5677_if1_adc_tdm_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	unsigned int value;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_read(rt5677->regmap, RT5677_TDM1_CTRL2, &value);
		if (value & RT5677_IF1_ADC_CTRL_MASK)
			regmap_update_bits(rt5677->regmap, RT5677_TDM1_CTRL1,
				RT5677_IF1_ADC_MODE_MASK,
				RT5677_IF1_ADC_MODE_TDM);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5677_if2_adc_tdm_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	unsigned int value;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_read(rt5677->regmap, RT5677_TDM2_CTRL2, &value);
		if (value & RT5677_IF2_ADC_CTRL_MASK)
			regmap_update_bits(rt5677->regmap, RT5677_TDM2_CTRL1,
				RT5677_IF2_ADC_MODE_MASK,
				RT5677_IF2_ADC_MODE_TDM);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5677_vref_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (codec->dapm.bias_level != SND_SOC_BIAS_ON &&
			!rt5677->is_vref_slow) {
			mdelay(20);
			regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG1,
				RT5677_PWR_FV1 | RT5677_PWR_FV2,
				RT5677_PWR_FV1 | RT5677_PWR_FV2);
			rt5677->is_vref_slow = true;
		}
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5677_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL1", RT5677_PWR_ANLG2, RT5677_PWR_PLL1_BIT,
		0, rt5677_set_pll1_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("PLL2", RT5677_PWR_ANLG2, RT5677_PWR_PLL2_BIT,
		0, rt5677_set_pll2_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMU),

	/* ASRC */
	SND_SOC_DAPM_SUPPLY_S("I2S1 ASRC", 1, RT5677_ASRC_1, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S2 ASRC", 1, RT5677_ASRC_1, 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S3 ASRC", 1, RT5677_ASRC_1, 2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S4 ASRC", 1, RT5677_ASRC_1, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC STO ASRC", 1, RT5677_ASRC_2, 14, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC MONO2 L ASRC", 1, RT5677_ASRC_2, 13, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DAC MONO2 R ASRC", 1, RT5677_ASRC_2, 12, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DAC MONO3 L ASRC", 1, RT5677_ASRC_1, 15, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DAC MONO3 R ASRC", 1, RT5677_ASRC_1, 14, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DAC MONO4 L ASRC", 1, RT5677_ASRC_1, 13, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DAC MONO4 R ASRC", 1, RT5677_ASRC_1, 12, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DMIC STO1 ASRC", 1, RT5677_ASRC_2, 11, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DMIC STO2 ASRC", 1, RT5677_ASRC_2, 10, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DMIC STO3 ASRC", 1, RT5677_ASRC_2, 9, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DMIC STO4 ASRC", 1, RT5677_ASRC_2, 8, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DMIC MONO L ASRC", 1, RT5677_ASRC_2, 7, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("DMIC MONO R ASRC", 1, RT5677_ASRC_2, 6, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO1 ASRC", 1, RT5677_ASRC_2, 5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO2 ASRC", 1, RT5677_ASRC_2, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO3 ASRC", 1, RT5677_ASRC_2, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO4 ASRC", 1, RT5677_ASRC_2, 2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC MONO L ASRC", 1, RT5677_ASRC_2, 1, 0, NULL,
		0),
	SND_SOC_DAPM_SUPPLY_S("ADC MONO R ASRC", 1, RT5677_ASRC_2, 0, 0, NULL,
		0),

	/* Input Side */
	/* micbias */
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RT5677_PWR_ANLG2, RT5677_PWR_MB1_BIT,
		0, rt5677_set_micbias1_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),
	SND_SOC_DAPM_INPUT("DMIC L2"),
	SND_SOC_DAPM_INPUT("DMIC R2"),
	SND_SOC_DAPM_INPUT("DMIC L3"),
	SND_SOC_DAPM_INPUT("DMIC R3"),
	SND_SOC_DAPM_INPUT("DMIC L4"),
	SND_SOC_DAPM_INPUT("DMIC R4"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),

	SND_SOC_DAPM_INPUT("Haptic Generator"),

	SND_SOC_DAPM_PGA("DMIC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC4", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DMIC1 power", RT5677_DMIC_CTRL1,
		RT5677_DMIC_1_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC2 power", RT5677_DMIC_CTRL1,
		RT5677_DMIC_2_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC3 power", RT5677_DMIC_CTRL1,
		RT5677_DMIC_3_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC4 power", RT5677_DMIC_CTRL2,
		RT5677_DMIC_4_EN_SFT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		set_dmic_clk, SND_SOC_DAPM_PRE_PMU),

	/* Boost */
	SND_SOC_DAPM_PGA_E("BST1", RT5677_PWR_ANLG2,
		RT5677_PWR_BST1_BIT, 0, NULL, 0, rt5677_bst1_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("BST2", RT5677_PWR_ANLG2,
		RT5677_PWR_BST2_BIT, 0, NULL, 0, rt5677_bst2_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC 1", NULL, SND_SOC_NOPM,
		0, 0),
	SND_SOC_DAPM_ADC("ADC 2", NULL, SND_SOC_NOPM,
		0, 0),
	SND_SOC_DAPM_PGA("ADC 1_2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("ADC 1 power", RT5677_PWR_DIG1,
		RT5677_PWR_ADC_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC 2 power", RT5677_PWR_DIG1,
		RT5677_PWR_ADC_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 clock", RT5677_PWR_DIG1,
		RT5677_PWR_ADCFED1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2 clock", RT5677_PWR_DIG1,
		RT5677_PWR_ADCFED2_BIT, 0, NULL, 0),

	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 DMIC Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto1_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto1_adc1_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto1_adc2_mux),
	SND_SOC_DAPM_MUX("Stereo2 DMIC Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto2_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto2_adc1_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto2_adc2_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC LR Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto2_adc_lr_mux),
	SND_SOC_DAPM_MUX("Stereo3 DMIC Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto3_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo3 ADC1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto3_adc1_mux),
	SND_SOC_DAPM_MUX("Stereo3 ADC2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto3_adc2_mux),
	SND_SOC_DAPM_MUX("Stereo4 DMIC Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto4_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo4 ADC1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto4_adc1_mux),
	SND_SOC_DAPM_MUX("Stereo4 ADC2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_sto4_adc2_mux),
	SND_SOC_DAPM_MUX("Mono DMIC L Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_mono_dmic_l_mux),
	SND_SOC_DAPM_MUX("Mono DMIC R Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_mono_dmic_r_mux),
	SND_SOC_DAPM_MUX("Mono ADC2 L Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_mono_adc2_l_mux),
	SND_SOC_DAPM_MUX("Mono ADC1 L Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_mono_adc1_l_mux),
	SND_SOC_DAPM_MUX("Mono ADC1 R Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_mono_adc1_r_mux),
	SND_SOC_DAPM_MUX("Mono ADC2 R Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_mono_adc2_r_mux),

	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("adc stereo1 filter", RT5677_PWR_DIG2,
		RT5677_PWR_ADC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("adc stereo2 filter", RT5677_PWR_DIG2,
		RT5677_PWR_ADC_S2F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("adc stereo3 filter", RT5677_PWR_DIG2,
		RT5677_PWR_ADC_S3F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("adc stereo4 filter", RT5677_PWR_DIG2,
		RT5677_PWR_ADC_S4F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Sto1 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_sto1_adc_l_mix, ARRAY_SIZE(rt5677_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto1 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_sto1_adc_r_mix, ARRAY_SIZE(rt5677_sto1_adc_r_mix)),
	SND_SOC_DAPM_MIXER("Sto2 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_sto2_adc_l_mix, ARRAY_SIZE(rt5677_sto2_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto2 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_sto2_adc_r_mix, ARRAY_SIZE(rt5677_sto2_adc_r_mix)),
	SND_SOC_DAPM_MIXER("Sto3 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_sto3_adc_l_mix, ARRAY_SIZE(rt5677_sto3_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto3 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_sto3_adc_r_mix, ARRAY_SIZE(rt5677_sto3_adc_r_mix)),
	SND_SOC_DAPM_MIXER("Sto4 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_sto4_adc_l_mix, ARRAY_SIZE(rt5677_sto4_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto4 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_sto4_adc_r_mix, ARRAY_SIZE(rt5677_sto4_adc_r_mix)),
	SND_SOC_DAPM_SUPPLY("adc mono left filter", RT5677_PWR_DIG2,
		RT5677_PWR_ADC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_mono_adc_l_mix, ARRAY_SIZE(rt5677_mono_adc_l_mix)),
	SND_SOC_DAPM_SUPPLY("adc mono right filter", RT5677_PWR_DIG2,
		RT5677_PWR_ADC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_mono_adc_r_mix, ARRAY_SIZE(rt5677_mono_adc_r_mix)),

	/* ADC PGA */
	SND_SOC_DAPM_PGA("Stereo1 ADC MIXL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo1 ADC MIXR", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo1 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC MIXL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC MIXR", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo3 ADC MIXL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo3 ADC MIXR", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo3 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo4 ADC MIXL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo4 ADC MIXR", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo4 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Sto2 ADC LR MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mono ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DSP */
	SND_SOC_DAPM_MUX("IB9 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib9_src_mux),
	SND_SOC_DAPM_MUX("IB8 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib8_src_mux),
	SND_SOC_DAPM_MUX("IB7 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib7_src_mux),
	SND_SOC_DAPM_MUX("IB6 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib6_src_mux),
	SND_SOC_DAPM_MUX("IB45 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib45_src_mux),
	SND_SOC_DAPM_MUX("IB23 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib23_src_mux),
	SND_SOC_DAPM_MUX("IB01 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib01_src_mux),
	SND_SOC_DAPM_MUX("IB45 Bypass Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib45_bypass_src_mux),
	SND_SOC_DAPM_MUX("IB23 Bypass Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib23_bypass_src_mux),
	SND_SOC_DAPM_MUX("IB01 Bypass Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ib01_bypass_src_mux),
	SND_SOC_DAPM_MUX("OB23 Bypass Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ob23_bypass_src_mux),
	SND_SOC_DAPM_MUX("OB01 Bypass Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_ob01_bypass_src_mux),

	SND_SOC_DAPM_PGA("OB45", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OB67", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA("OutBound2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OutBound3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OutBound4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OutBound5", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OutBound6", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OutBound7", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5677_PWR_DIG1,
		RT5677_PWR_I2S1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC5", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC6", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC7", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC01", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC23", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC45", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC67", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC4", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("I2S2", RT5677_PWR_DIG1,
		RT5677_PWR_I2S2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC5", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC6", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC7", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC01", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC23", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC45", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC67", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC4", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("I2S3", RT5677_PWR_DIG1,
		RT5677_PWR_I2S3_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("I2S4", RT5677_PWR_DIG1,
		RT5677_PWR_I2S4_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF4 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF4 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF4 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF4 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF4 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF4 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("SLB", RT5677_PWR_DIG1,
		RT5677_PWR_SLB_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC5", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC6", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC7", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC01", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC23", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC45", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB DAC67", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SLB ADC4", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */
	SND_SOC_DAPM_MUX("IF1 ADC1 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc1_mux),
	SND_SOC_DAPM_MUX("IF1 ADC2 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc2_mux),
	SND_SOC_DAPM_MUX("IF1 ADC3 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc3_mux),
	SND_SOC_DAPM_MUX("IF1 ADC4 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc4_mux),
	SND_SOC_DAPM_MUX("IF1 ADC1 Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc1_swap_mux),
	SND_SOC_DAPM_MUX("IF1 ADC2 Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc2_swap_mux),
	SND_SOC_DAPM_MUX("IF1 ADC3 Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc3_swap_mux),
	SND_SOC_DAPM_MUX("IF1 ADC4 Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc4_swap_mux),
	SND_SOC_DAPM_MUX_E("IF1 ADC TDM Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_adc_tdm_swap_mux, rt5677_if1_adc_tdm_event,
			SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MUX("IF2 ADC1 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc1_mux),
	SND_SOC_DAPM_MUX("IF2 ADC2 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc2_mux),
	SND_SOC_DAPM_MUX("IF2 ADC3 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc3_mux),
	SND_SOC_DAPM_MUX("IF2 ADC4 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc4_mux),
	SND_SOC_DAPM_MUX("IF2 ADC1 Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc1_swap_mux),
	SND_SOC_DAPM_MUX("IF2 ADC2 Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc2_swap_mux),
	SND_SOC_DAPM_MUX("IF2 ADC3 Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc3_swap_mux),
	SND_SOC_DAPM_MUX("IF2 ADC4 Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc4_swap_mux),
	SND_SOC_DAPM_MUX_E("IF2 ADC TDM Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_adc_tdm_swap_mux, rt5677_if2_adc_tdm_event,
			SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MUX("IF3 ADC Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if3_adc_mux),
	SND_SOC_DAPM_MUX("IF4 ADC Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if4_adc_mux),
	SND_SOC_DAPM_MUX("SLB ADC1 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_slb_adc1_mux),
	SND_SOC_DAPM_MUX("SLB ADC2 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_slb_adc2_mux),
	SND_SOC_DAPM_MUX("SLB ADC3 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_slb_adc3_mux),
	SND_SOC_DAPM_MUX("SLB ADC4 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_slb_adc4_mux),

	SND_SOC_DAPM_MUX("IF1 DAC0 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_dac0_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF1 DAC1 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_dac1_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF1 DAC2 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_dac2_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF1 DAC3 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_dac3_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF1 DAC4 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_dac4_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF1 DAC5 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_dac5_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF1 DAC6 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_dac6_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF1 DAC7 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if1_dac7_tdm_sel_mux),

	SND_SOC_DAPM_MUX("IF2 DAC0 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_dac0_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF2 DAC1 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_dac1_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF2 DAC2 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_dac2_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF2 DAC3 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_dac3_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF2 DAC4 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_dac4_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF2 DAC5 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_dac5_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF2 DAC6 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_dac6_tdm_sel_mux),
	SND_SOC_DAPM_MUX("IF2 DAC7 Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_if2_dac7_tdm_sel_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF3RX", "AIF3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF3TX", "AIF3 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF4RX", "AIF4 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF4TX", "AIF4 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SLBRX", "SLIMBus Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SLBTX", "SLIMBus Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Sidetone Mux */
	SND_SOC_DAPM_MUX("Sidetone Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_sidetone_mux),
	SND_SOC_DAPM_SUPPLY("Sidetone Power", RT5677_SIDETONE_CTRL,
		RT5677_ST_EN_SFT, 0, NULL, 0),

	/* VAD Mux*/
	SND_SOC_DAPM_MUX("VAD ADC Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_vad_src_mux),

	/* Tensilica DSP */
	SND_SOC_DAPM_PGA("Tensilica DSP", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("OB01 MIX", SND_SOC_NOPM, 0, 0,
		rt5677_ob_01_mix, ARRAY_SIZE(rt5677_ob_01_mix)),
	SND_SOC_DAPM_MIXER("OB23 MIX", SND_SOC_NOPM, 0, 0,
		rt5677_ob_23_mix, ARRAY_SIZE(rt5677_ob_23_mix)),
	SND_SOC_DAPM_MIXER("OB4 MIX", SND_SOC_NOPM, 0, 0,
		rt5677_ob_4_mix, ARRAY_SIZE(rt5677_ob_4_mix)),
	SND_SOC_DAPM_MIXER("OB5 MIX", SND_SOC_NOPM, 0, 0,
		rt5677_ob_5_mix, ARRAY_SIZE(rt5677_ob_5_mix)),
	SND_SOC_DAPM_MIXER("OB6 MIX", SND_SOC_NOPM, 0, 0,
		rt5677_ob_6_mix, ARRAY_SIZE(rt5677_ob_6_mix)),
	SND_SOC_DAPM_MIXER("OB7 MIX", SND_SOC_NOPM, 0, 0,
		rt5677_ob_7_mix, ARRAY_SIZE(rt5677_ob_7_mix)),

	/* Output Side */
	/* DAC mixer before sound effect */
	SND_SOC_DAPM_MIXER("DAC1 MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_dac_l_mix, ARRAY_SIZE(rt5677_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC1 MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_dac_r_mix, ARRAY_SIZE(rt5677_dac_r_mix)),
	SND_SOC_DAPM_PGA("DAC1 FS", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DAC Mux */
	SND_SOC_DAPM_MUX("DAC1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_dac1_mux),
	SND_SOC_DAPM_MUX("ADDA1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_adda1_mux),
	SND_SOC_DAPM_MUX("DAC12 SRC Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_dac12_mux),
	SND_SOC_DAPM_MUX("DAC3 SRC Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_dac3_mux),

	/* DAC2 channel Mux */
	SND_SOC_DAPM_MUX("DAC2 L Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_dac2_l_mux),
	SND_SOC_DAPM_MUX("DAC2 R Mux", SND_SOC_NOPM, 0, 0,
				&rt5677_dac2_r_mux),

	/* DAC3 channel Mux */
	SND_SOC_DAPM_MUX("DAC3 L Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_dac3_l_mux),
	SND_SOC_DAPM_MUX("DAC3 R Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_dac3_r_mux),

	/* DAC4 channel Mux */
	SND_SOC_DAPM_MUX("DAC4 L Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_dac4_l_mux),
	SND_SOC_DAPM_MUX("DAC4 R Mux", SND_SOC_NOPM, 0, 0,
			&rt5677_dac4_r_mux),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("dac stereo1 filter", RT5677_PWR_DIG2,
		RT5677_PWR_DAC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("dac mono2 left filter", RT5677_PWR_DIG2,
		RT5677_PWR_DAC_M2F_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("dac mono2 right filter", RT5677_PWR_DIG2,
		RT5677_PWR_DAC_M2F_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("dac mono3 left filter", RT5677_PWR_DIG2,
		RT5677_PWR_DAC_M3F_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("dac mono3 right filter", RT5677_PWR_DIG2,
		RT5677_PWR_DAC_M3F_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("dac mono4 left filter", RT5677_PWR_DIG2,
		RT5677_PWR_DAC_M4F_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("dac mono4 right filter", RT5677_PWR_DIG2,
		RT5677_PWR_DAC_M4F_R_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_sto1_dac_l_mix, ARRAY_SIZE(rt5677_sto1_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_sto1_dac_r_mix, ARRAY_SIZE(rt5677_sto1_dac_r_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_mono_dac_l_mix, ARRAY_SIZE(rt5677_mono_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_mono_dac_r_mix, ARRAY_SIZE(rt5677_mono_dac_r_mix)),
	SND_SOC_DAPM_MIXER("DD1 MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_dd1_l_mix, ARRAY_SIZE(rt5677_dd1_l_mix)),
	SND_SOC_DAPM_MIXER("DD1 MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_dd1_r_mix, ARRAY_SIZE(rt5677_dd1_r_mix)),
	SND_SOC_DAPM_MIXER("DD2 MIXL", SND_SOC_NOPM, 0, 0,
		rt5677_dd2_l_mix, ARRAY_SIZE(rt5677_dd2_l_mix)),
	SND_SOC_DAPM_MIXER("DD2 MIXR", SND_SOC_NOPM, 0, 0,
		rt5677_dd2_r_mix, ARRAY_SIZE(rt5677_dd2_r_mix)),
	SND_SOC_DAPM_PGA("Stereo DAC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mono DAC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DD1 MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DD2 MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC 1", NULL, RT5677_PWR_DIG1,
		RT5677_PWR_DAC1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC 2", NULL, RT5677_PWR_DIG1,
		RT5677_PWR_DAC2_BIT, 0),
	SND_SOC_DAPM_DAC("DAC 3", NULL, RT5677_PWR_DIG1,
		RT5677_PWR_DAC3_BIT, 0),

	/* PDM */
	SND_SOC_DAPM_SUPPLY("PDM1 Power", RT5677_PWR_DIG2,
		RT5677_PWR_PDM1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PDM2 Power", RT5677_PWR_DIG2,
		RT5677_PWR_PDM2_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MUX("PDM1 L Mux", RT5677_PDM_OUT_CTRL, RT5677_M_PDM1_L_SFT,
		1, &rt5677_pdm1_l_mux),
	SND_SOC_DAPM_MUX("PDM1 R Mux", RT5677_PDM_OUT_CTRL, RT5677_M_PDM1_R_SFT,
		1, &rt5677_pdm1_r_mux),
	SND_SOC_DAPM_MUX("PDM2 L Mux", RT5677_PDM_OUT_CTRL, RT5677_M_PDM2_L_SFT,
		1, &rt5677_pdm2_l_mux),
	SND_SOC_DAPM_MUX("PDM2 R Mux", RT5677_PDM_OUT_CTRL, RT5677_M_PDM2_R_SFT,
		1, &rt5677_pdm2_r_mux),

	SND_SOC_DAPM_PGA_S("LOUT1 amp", 0, RT5677_PWR_ANLG1, RT5677_PWR_LO1_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT2 amp", 0, RT5677_PWR_ANLG1, RT5677_PWR_LO2_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT3 amp", 0, RT5677_PWR_ANLG1, RT5677_PWR_LO3_BIT,
		0, NULL, 0),

	SND_SOC_DAPM_PGA_S("LOUT1 vref", 1, SND_SOC_NOPM, 0, 0,
		rt5677_vref_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("LOUT2 vref", 1, SND_SOC_NOPM, 0, 0,
		rt5677_vref_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("LOUT3 vref", 1, SND_SOC_NOPM, 0, 0,
		rt5677_vref_event, SND_SOC_DAPM_POST_PMU),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("LOUT3"),
	SND_SOC_DAPM_OUTPUT("PDM1L"),
	SND_SOC_DAPM_OUTPUT("PDM1R"),
	SND_SOC_DAPM_OUTPUT("PDM2L"),
	SND_SOC_DAPM_OUTPUT("PDM2R"),

	SND_SOC_DAPM_POST("vref", rt5677_vref_event),
};

static const struct snd_soc_dapm_route rt5677_dapm_routes[] = {
	{ "Stereo1 DMIC Mux", NULL, "DMIC STO1 ASRC", can_use_asrc },
	{ "Stereo2 DMIC Mux", NULL, "DMIC STO2 ASRC", can_use_asrc },
	{ "Stereo3 DMIC Mux", NULL, "DMIC STO3 ASRC", can_use_asrc },
	{ "Stereo4 DMIC Mux", NULL, "DMIC STO4 ASRC", can_use_asrc },
	{ "Mono DMIC L Mux", NULL, "DMIC MONO L ASRC", can_use_asrc },
	{ "Mono DMIC R Mux", NULL, "DMIC MONO R ASRC", can_use_asrc },
	{ "I2S1", NULL, "I2S1 ASRC", can_use_asrc},
	{ "I2S2", NULL, "I2S2 ASRC", can_use_asrc},
	{ "I2S3", NULL, "I2S3 ASRC", can_use_asrc},
	{ "I2S4", NULL, "I2S4 ASRC", can_use_asrc},

	{ "dac stereo1 filter", NULL, "DAC STO ASRC", is_using_asrc },
	{ "dac mono2 left filter", NULL, "DAC MONO2 L ASRC", is_using_asrc },
	{ "dac mono2 right filter", NULL, "DAC MONO2 R ASRC", is_using_asrc },
	{ "dac mono3 left filter", NULL, "DAC MONO3 L ASRC", is_using_asrc },
	{ "dac mono3 right filter", NULL, "DAC MONO3 R ASRC", is_using_asrc },
	{ "dac mono4 left filter", NULL, "DAC MONO4 L ASRC", is_using_asrc },
	{ "dac mono4 right filter", NULL, "DAC MONO4 R ASRC", is_using_asrc },
	{ "adc stereo1 filter", NULL, "ADC STO1 ASRC", is_using_asrc },
	{ "adc stereo2 filter", NULL, "ADC STO2 ASRC", is_using_asrc },
	{ "adc stereo3 filter", NULL, "ADC STO3 ASRC", is_using_asrc },
	{ "adc stereo4 filter", NULL, "ADC STO4 ASRC", is_using_asrc },
	{ "adc mono left filter", NULL, "ADC MONO L ASRC", is_using_asrc },
	{ "adc mono right filter", NULL, "ADC MONO R ASRC", is_using_asrc },

	{ "DMIC1", NULL, "DMIC L1" },
	{ "DMIC1", NULL, "DMIC R1" },
	{ "DMIC2", NULL, "DMIC L2" },
	{ "DMIC2", NULL, "DMIC R2" },
	{ "DMIC3", NULL, "DMIC L3" },
	{ "DMIC3", NULL, "DMIC R3" },
	{ "DMIC4", NULL, "DMIC L4" },
	{ "DMIC4", NULL, "DMIC R4" },

	{ "DMIC L1", NULL, "DMIC CLK" },
	{ "DMIC R1", NULL, "DMIC CLK" },
	{ "DMIC L2", NULL, "DMIC CLK" },
	{ "DMIC R2", NULL, "DMIC CLK" },
	{ "DMIC L3", NULL, "DMIC CLK" },
	{ "DMIC R3", NULL, "DMIC CLK" },
	{ "DMIC L4", NULL, "DMIC CLK" },
	{ "DMIC R4", NULL, "DMIC CLK" },

	{ "DMIC L1", NULL, "DMIC1 power" },
	{ "DMIC R1", NULL, "DMIC1 power" },
	{ "DMIC L3", NULL, "DMIC3 power" },
	{ "DMIC R3", NULL, "DMIC3 power" },
	{ "DMIC L4", NULL, "DMIC4 power" },
	{ "DMIC R4", NULL, "DMIC4 power" },

	{ "BST1", NULL, "IN1P" },
	{ "BST1", NULL, "IN1N" },
	{ "BST2", NULL, "IN2P" },
	{ "BST2", NULL, "IN2N" },

	{ "IN1P", NULL, "MICBIAS1" },
	{ "IN1N", NULL, "MICBIAS1" },
	{ "IN2P", NULL, "MICBIAS1" },
	{ "IN2N", NULL, "MICBIAS1" },

	{ "ADC 1", NULL, "BST1" },
	{ "ADC 1", NULL, "ADC 1 power" },
	{ "ADC 1", NULL, "ADC1 clock" },
	{ "ADC 2", NULL, "BST2" },
	{ "ADC 2", NULL, "ADC 2 power" },
	{ "ADC 2", NULL, "ADC2 clock" },

	{ "Stereo1 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo1 DMIC Mux", "DMIC2", "DMIC2" },
	{ "Stereo1 DMIC Mux", "DMIC3", "DMIC3" },
	{ "Stereo1 DMIC Mux", "DMIC4", "DMIC4" },

	{ "Stereo2 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo2 DMIC Mux", "DMIC2", "DMIC2" },
	{ "Stereo2 DMIC Mux", "DMIC3", "DMIC3" },
	{ "Stereo2 DMIC Mux", "DMIC4", "DMIC4" },

	{ "Stereo3 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo3 DMIC Mux", "DMIC2", "DMIC2" },
	{ "Stereo3 DMIC Mux", "DMIC3", "DMIC3" },
	{ "Stereo3 DMIC Mux", "DMIC4", "DMIC4" },

	{ "Stereo4 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo4 DMIC Mux", "DMIC2", "DMIC2" },
	{ "Stereo4 DMIC Mux", "DMIC3", "DMIC3" },
	{ "Stereo4 DMIC Mux", "DMIC4", "DMIC4" },

	{ "Mono DMIC L Mux", "DMIC1", "DMIC1" },
	{ "Mono DMIC L Mux", "DMIC2", "DMIC2" },
	{ "Mono DMIC L Mux", "DMIC3", "DMIC3" },
	{ "Mono DMIC L Mux", "DMIC4", "DMIC4" },

	{ "Mono DMIC R Mux", "DMIC1", "DMIC1" },
	{ "Mono DMIC R Mux", "DMIC2", "DMIC2" },
	{ "Mono DMIC R Mux", "DMIC3", "DMIC3" },
	{ "Mono DMIC R Mux", "DMIC4", "DMIC4" },

	{ "ADC 1_2", NULL, "ADC 1" },
	{ "ADC 1_2", NULL, "ADC 2" },

	{ "Stereo1 ADC1 Mux", "DD MIX1", "DD1 MIX" },
	{ "Stereo1 ADC1 Mux", "ADC1/2", "ADC 1_2" },
	{ "Stereo1 ADC1 Mux", "Stereo DAC MIX", "Stereo DAC MIX" },

	{ "Stereo1 ADC2 Mux", "DD MIX1", "DD1 MIX" },
	{ "Stereo1 ADC2 Mux", "DMIC", "Stereo1 DMIC Mux" },
	{ "Stereo1 ADC2 Mux", "Stereo DAC MIX", "Stereo DAC MIX" },

	{ "Stereo2 ADC1 Mux", "DD MIX1", "DD1 MIX" },
	{ "Stereo2 ADC1 Mux", "ADC1/2", "ADC 1_2" },
	{ "Stereo2 ADC1 Mux", "Stereo DAC MIX", "Stereo DAC MIX" },

	{ "Stereo2 ADC2 Mux", "DD MIX1", "DD1 MIX" },
	{ "Stereo2 ADC2 Mux", "DMIC", "Stereo2 DMIC Mux" },
	{ "Stereo2 ADC2 Mux", "Stereo DAC MIX", "Stereo DAC MIX" },

	{ "Stereo3 ADC1 Mux", "DD MIX1", "DD1 MIX" },
	{ "Stereo3 ADC1 Mux", "ADC1/2", "ADC 1_2" },
	{ "Stereo3 ADC1 Mux", "Stereo DAC MIX", "Stereo DAC MIX" },

	{ "Stereo3 ADC2 Mux", "DD MIX1", "DD1 MIX" },
	{ "Stereo3 ADC2 Mux", "DMIC", "Stereo3 DMIC Mux" },
	{ "Stereo3 ADC2 Mux", "Stereo DAC MIX", "Stereo DAC MIX" },

	{ "Stereo4 ADC1 Mux", "DD MIX1", "DD1 MIX" },
	{ "Stereo4 ADC1 Mux", "ADC1/2", "ADC 1_2" },
	{ "Stereo4 ADC1 Mux", "DD MIX2", "DD2 MIX" },

	{ "Stereo4 ADC2 Mux", "DD MIX1", "DD1 MIX" },
	{ "Stereo4 ADC2 Mux", "DMIC", "Stereo3 DMIC Mux" },
	{ "Stereo4 ADC2 Mux", "DD MIX2", "DD2 MIX" },

	{ "Mono ADC2 L Mux", "DD MIX1L", "DD1 MIXL" },
	{ "Mono ADC2 L Mux", "DMIC", "Mono DMIC L Mux" },
	{ "Mono ADC2 L Mux", "MONO DAC MIXL", "Mono DAC MIXL" },

	{ "Mono ADC1 L Mux", "DD MIX1L", "DD1 MIXL" },
	{ "Mono ADC1 L Mux", "ADC1", "ADC 1" },
	{ "Mono ADC1 L Mux", "MONO DAC MIXL", "Mono DAC MIXL" },

	{ "Mono ADC1 R Mux", "DD MIX1R", "DD1 MIXR" },
	{ "Mono ADC1 R Mux", "ADC2", "ADC 2" },
	{ "Mono ADC1 R Mux", "MONO DAC MIXR", "Mono DAC MIXR" },

	{ "Mono ADC2 R Mux", "DD MIX1R", "DD1 MIXR" },
	{ "Mono ADC2 R Mux", "DMIC", "Mono DMIC R Mux" },
	{ "Mono ADC2 R Mux", "MONO DAC MIXR", "Mono DAC MIXR" },

	{ "Sto1 ADC MIXL", "ADC1 Switch", "Stereo1 ADC1 Mux" },
	{ "Sto1 ADC MIXL", "ADC2 Switch", "Stereo1 ADC2 Mux" },
	{ "Sto1 ADC MIXR", "ADC1 Switch", "Stereo1 ADC1 Mux" },
	{ "Sto1 ADC MIXR", "ADC2 Switch", "Stereo1 ADC2 Mux" },

	{ "Stereo1 ADC MIXL", NULL, "Sto1 ADC MIXL" },
	{ "Stereo1 ADC MIXL", NULL, "adc stereo1 filter" },
	{ "Stereo1 ADC MIXR", NULL, "Sto1 ADC MIXR" },
	{ "Stereo1 ADC MIXR", NULL, "adc stereo1 filter" },
	{ "adc stereo1 filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXL" },
	{ "Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXR" },

	{ "Sto2 ADC MIXL", "ADC1 Switch", "Stereo2 ADC1 Mux" },
	{ "Sto2 ADC MIXL", "ADC2 Switch", "Stereo2 ADC2 Mux" },
	{ "Sto2 ADC MIXR", "ADC1 Switch", "Stereo2 ADC1 Mux" },
	{ "Sto2 ADC MIXR", "ADC2 Switch", "Stereo2 ADC2 Mux" },

	{ "Sto2 ADC LR MIX", NULL, "Sto2 ADC MIXL" },
	{ "Sto2 ADC LR MIX", NULL, "Sto2 ADC MIXR" },

	{ "Stereo2 ADC LR Mux", "L", "Sto2 ADC MIXL" },
	{ "Stereo2 ADC LR Mux", "LR", "Sto2 ADC LR MIX" },

	{ "Stereo2 ADC MIXL", NULL, "Stereo2 ADC LR Mux" },
	{ "Stereo2 ADC MIXL", NULL, "adc stereo2 filter" },
	{ "Stereo2 ADC MIXR", NULL, "Sto2 ADC MIXR" },
	{ "Stereo2 ADC MIXR", NULL, "adc stereo2 filter" },
	{ "adc stereo2 filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Stereo2 ADC MIX", NULL, "Stereo2 ADC MIXL" },
	{ "Stereo2 ADC MIX", NULL, "Stereo2 ADC MIXR" },

	{ "Sto3 ADC MIXL", "ADC1 Switch", "Stereo3 ADC1 Mux" },
	{ "Sto3 ADC MIXL", "ADC2 Switch", "Stereo3 ADC2 Mux" },
	{ "Sto3 ADC MIXR", "ADC1 Switch", "Stereo3 ADC1 Mux" },
	{ "Sto3 ADC MIXR", "ADC2 Switch", "Stereo3 ADC2 Mux" },

	{ "Stereo3 ADC MIXL", NULL, "Sto3 ADC MIXL" },
	{ "Stereo3 ADC MIXL", NULL, "adc stereo3 filter" },
	{ "Stereo3 ADC MIXR", NULL, "Sto3 ADC MIXR" },
	{ "Stereo3 ADC MIXR", NULL, "adc stereo3 filter" },
	{ "adc stereo3 filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Stereo3 ADC MIX", NULL, "Stereo3 ADC MIXL" },
	{ "Stereo3 ADC MIX", NULL, "Stereo3 ADC MIXR" },

	{ "Sto4 ADC MIXL", "ADC1 Switch", "Stereo4 ADC1 Mux" },
	{ "Sto4 ADC MIXL", "ADC2 Switch", "Stereo4 ADC2 Mux" },
	{ "Sto4 ADC MIXR", "ADC1 Switch", "Stereo4 ADC1 Mux" },
	{ "Sto4 ADC MIXR", "ADC2 Switch", "Stereo4 ADC2 Mux" },

	{ "Stereo4 ADC MIXL", NULL, "Sto4 ADC MIXL" },
	{ "Stereo4 ADC MIXL", NULL, "adc stereo4 filter" },
	{ "Stereo4 ADC MIXR", NULL, "Sto4 ADC MIXR" },
	{ "Stereo4 ADC MIXR", NULL, "adc stereo4 filter" },
	{ "adc stereo4 filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Stereo4 ADC MIX", NULL, "Stereo4 ADC MIXL" },
	{ "Stereo4 ADC MIX", NULL, "Stereo4 ADC MIXR" },

	{ "Mono ADC MIXL", "ADC1 Switch", "Mono ADC1 L Mux" },
	{ "Mono ADC MIXL", "ADC2 Switch", "Mono ADC2 L Mux" },
	{ "Mono ADC MIXL", NULL, "adc mono left filter" },
	{ "adc mono left filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Mono ADC MIXR", "ADC1 Switch", "Mono ADC1 R Mux" },
	{ "Mono ADC MIXR", "ADC2 Switch", "Mono ADC2 R Mux" },
	{ "Mono ADC MIXR", NULL, "adc mono right filter" },
	{ "adc mono right filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Mono ADC MIX", NULL, "Mono ADC MIXL" },
	{ "Mono ADC MIX", NULL, "Mono ADC MIXR" },

	{ "VAD ADC Mux", "STO1 ADC MIX L", "Stereo1 ADC MIXL" },
	{ "VAD ADC Mux", "MONO ADC MIX L", "Mono ADC MIXL" },
	{ "VAD ADC Mux", "MONO ADC MIX R", "Mono ADC MIXR" },
	{ "VAD ADC Mux", "STO2 ADC MIX L", "Stereo2 ADC MIXL" },
	{ "VAD ADC Mux", "STO3 ADC MIX L", "Stereo3 ADC MIXL" },

	{ "IF1 ADC1 Mux", "STO1 ADC MIX", "Stereo1 ADC MIX" },
	{ "IF1 ADC1 Mux", "OB01", "OB01 Bypass Mux" },
	{ "IF1 ADC1 Mux", "VAD ADC", "VAD ADC Mux" },

	{ "IF1 ADC2 Mux", "STO2 ADC MIX", "Stereo2 ADC MIX" },
	{ "IF1 ADC2 Mux", "OB23", "OB23 Bypass Mux" },

	{ "IF1 ADC3 Mux", "STO3 ADC MIX", "Stereo3 ADC MIX" },
	{ "IF1 ADC3 Mux", "MONO ADC MIX", "Mono ADC MIX" },
	{ "IF1 ADC3 Mux", "OB45", "OB45" },

	{ "IF1 ADC4 Mux", "STO4 ADC MIX", "Stereo4 ADC MIX" },
	{ "IF1 ADC4 Mux", "OB67", "OB67" },
	{ "IF1 ADC4 Mux", "OB01", "OB01 Bypass Mux" },

	{ "IF1 ADC1 Swap Mux", "L/R", "IF1 ADC1 Mux" },
	{ "IF1 ADC1 Swap Mux", "R/L", "IF1 ADC1 Mux" },
	{ "IF1 ADC1 Swap Mux", "L/L", "IF1 ADC1 Mux" },
	{ "IF1 ADC1 Swap Mux", "R/R", "IF1 ADC1 Mux" },

	{ "IF1 ADC2 Swap Mux", "L/R", "IF1 ADC2 Mux" },
	{ "IF1 ADC2 Swap Mux", "R/L", "IF1 ADC2 Mux" },
	{ "IF1 ADC2 Swap Mux", "L/L", "IF1 ADC2 Mux" },
	{ "IF1 ADC2 Swap Mux", "R/R", "IF1 ADC2 Mux" },

	{ "IF1 ADC3 Swap Mux", "L/R", "IF1 ADC3 Mux" },
	{ "IF1 ADC3 Swap Mux", "R/L", "IF1 ADC3 Mux" },
	{ "IF1 ADC3 Swap Mux", "L/L", "IF1 ADC3 Mux" },
	{ "IF1 ADC3 Swap Mux", "R/R", "IF1 ADC3 Mux" },

	{ "IF1 ADC4 Swap Mux", "L/R", "IF1 ADC4 Mux" },
	{ "IF1 ADC4 Swap Mux", "R/L", "IF1 ADC4 Mux" },
	{ "IF1 ADC4 Swap Mux", "L/L", "IF1 ADC4 Mux" },
	{ "IF1 ADC4 Swap Mux", "R/R", "IF1 ADC4 Mux" },

	{ "IF1 ADC", NULL, "IF1 ADC1 Swap Mux" },
	{ "IF1 ADC", NULL, "IF1 ADC2 Swap Mux" },
	{ "IF1 ADC", NULL, "IF1 ADC3 Swap Mux" },
	{ "IF1 ADC", NULL, "IF1 ADC4 Swap Mux" },

	{ "IF1 ADC TDM Swap Mux", "1/2/3/4", "IF1 ADC" },
	{ "IF1 ADC TDM Swap Mux", "2/1/3/4", "IF1 ADC" },
	{ "IF1 ADC TDM Swap Mux", "2/3/1/4", "IF1 ADC" },
	{ "IF1 ADC TDM Swap Mux", "4/1/2/3", "IF1 ADC" },
	{ "IF1 ADC TDM Swap Mux", "1/3/2/4", "IF1 ADC" },
	{ "IF1 ADC TDM Swap Mux", "1/4/2/3", "IF1 ADC" },
	{ "IF1 ADC TDM Swap Mux", "3/1/2/4", "IF1 ADC" },
	{ "IF1 ADC TDM Swap Mux", "3/4/1/2", "IF1 ADC" },

	{ "AIF1TX", NULL, "I2S1" },
	{ "AIF1TX", NULL, "IF1 ADC TDM Swap Mux" },

	{ "IF2 ADC1 Mux", "STO1 ADC MIX", "Stereo1 ADC MIX" },
	{ "IF2 ADC1 Mux", "OB01", "OB01 Bypass Mux" },
	{ "IF2 ADC1 Mux", "VAD ADC", "VAD ADC Mux" },

	{ "IF2 ADC2 Mux", "STO2 ADC MIX", "Stereo2 ADC MIX" },
	{ "IF2 ADC2 Mux", "OB23", "OB23 Bypass Mux" },

	{ "IF2 ADC3 Mux", "STO3 ADC MIX", "Stereo3 ADC MIX" },
	{ "IF2 ADC3 Mux", "MONO ADC MIX", "Mono ADC MIX" },
	{ "IF2 ADC3 Mux", "OB45", "OB45" },

	{ "IF2 ADC4 Mux", "STO4 ADC MIX", "Stereo4 ADC MIX" },
	{ "IF2 ADC4 Mux", "OB67", "OB67" },
	{ "IF2 ADC4 Mux", "OB01", "OB01 Bypass Mux" },

	{ "IF2 ADC1 Swap Mux", "L/R", "IF2 ADC1 Mux" },
	{ "IF2 ADC1 Swap Mux", "R/L", "IF2 ADC1 Mux" },
	{ "IF2 ADC1 Swap Mux", "L/L", "IF2 ADC1 Mux" },
	{ "IF2 ADC1 Swap Mux", "R/R", "IF2 ADC1 Mux" },

	{ "IF2 ADC2 Swap Mux", "L/R", "IF2 ADC2 Mux" },
	{ "IF2 ADC2 Swap Mux", "R/L", "IF2 ADC2 Mux" },
	{ "IF2 ADC2 Swap Mux", "L/L", "IF2 ADC2 Mux" },
	{ "IF2 ADC2 Swap Mux", "R/R", "IF2 ADC2 Mux" },

	{ "IF2 ADC3 Swap Mux", "L/R", "IF2 ADC3 Mux" },
	{ "IF2 ADC3 Swap Mux", "R/L", "IF2 ADC3 Mux" },
	{ "IF2 ADC3 Swap Mux", "L/L", "IF2 ADC3 Mux" },
	{ "IF2 ADC3 Swap Mux", "R/R", "IF2 ADC3 Mux" },

	{ "IF2 ADC4 Swap Mux", "L/R", "IF2 ADC4 Mux" },
	{ "IF2 ADC4 Swap Mux", "R/L", "IF2 ADC4 Mux" },
	{ "IF2 ADC4 Swap Mux", "L/L", "IF2 ADC4 Mux" },
	{ "IF2 ADC4 Swap Mux", "R/R", "IF2 ADC4 Mux" },

	{ "IF2 ADC", NULL, "IF2 ADC1 Swap Mux" },
	{ "IF2 ADC", NULL, "IF2 ADC2 Swap Mux" },
	{ "IF2 ADC", NULL, "IF2 ADC3 Swap Mux" },
	{ "IF2 ADC", NULL, "IF2 ADC4 Swap Mux" },

	{ "IF2 ADC TDM Swap Mux", "1/2/3/4", "IF2 ADC" },
	{ "IF2 ADC TDM Swap Mux", "2/1/3/4", "IF2 ADC" },
	{ "IF2 ADC TDM Swap Mux", "3/1/2/4", "IF2 ADC" },
	{ "IF2 ADC TDM Swap Mux", "4/1/2/3", "IF2 ADC" },
	{ "IF2 ADC TDM Swap Mux", "1/3/2/4", "IF2 ADC" },
	{ "IF2 ADC TDM Swap Mux", "1/4/2/3", "IF2 ADC" },
	{ "IF2 ADC TDM Swap Mux", "2/3/1/4", "IF2 ADC" },
	{ "IF2 ADC TDM Swap Mux", "3/4/1/2", "IF2 ADC" },

	{ "AIF2TX", NULL, "I2S2" },
	{ "AIF2TX", NULL, "IF2 ADC TDM Swap Mux" },

	{ "IF3 ADC Mux", "STO1 ADC MIX", "Stereo1 ADC MIX" },
	{ "IF3 ADC Mux", "STO2 ADC MIX", "Stereo2 ADC MIX" },
	{ "IF3 ADC Mux", "STO3 ADC MIX", "Stereo3 ADC MIX" },
	{ "IF3 ADC Mux", "STO4 ADC MIX", "Stereo4 ADC MIX" },
	{ "IF3 ADC Mux", "MONO ADC MIX", "Mono ADC MIX" },
	{ "IF3 ADC Mux", "OB01", "OB01 Bypass Mux" },
	{ "IF3 ADC Mux", "OB23", "OB23 Bypass Mux" },
	{ "IF3 ADC Mux", "VAD ADC", "VAD ADC Mux" },

	{ "AIF3TX", NULL, "I2S3" },
	{ "AIF3TX", NULL, "IF3 ADC Mux" },

	{ "IF4 ADC Mux", "STO1 ADC MIX", "Stereo1 ADC MIX" },
	{ "IF4 ADC Mux", "STO2 ADC MIX", "Stereo2 ADC MIX" },
	{ "IF4 ADC Mux", "STO3 ADC MIX", "Stereo3 ADC MIX" },
	{ "IF4 ADC Mux", "STO4 ADC MIX", "Stereo4 ADC MIX" },
	{ "IF4 ADC Mux", "MONO ADC MIX", "Mono ADC MIX" },
	{ "IF4 ADC Mux", "OB01", "OB01 Bypass Mux" },
	{ "IF4 ADC Mux", "OB23", "OB23 Bypass Mux" },
	{ "IF4 ADC Mux", "VAD ADC", "VAD ADC Mux" },

	{ "AIF4TX", NULL, "I2S4" },
	{ "AIF4TX", NULL, "IF4 ADC Mux" },

	{ "SLB ADC1 Mux", "STO1 ADC MIX", "Stereo1 ADC MIX" },
	{ "SLB ADC1 Mux", "OB01", "OB01 Bypass Mux" },
	{ "SLB ADC1 Mux", "VAD ADC", "VAD ADC Mux" },

	{ "SLB ADC2 Mux", "STO2 ADC MIX", "Stereo2 ADC MIX" },
	{ "SLB ADC2 Mux", "OB23", "OB23 Bypass Mux" },

	{ "SLB ADC3 Mux", "STO3 ADC MIX", "Stereo3 ADC MIX" },
	{ "SLB ADC3 Mux", "MONO ADC MIX", "Mono ADC MIX" },
	{ "SLB ADC3 Mux", "OB45", "OB45" },

	{ "SLB ADC4 Mux", "STO4 ADC MIX", "Stereo4 ADC MIX" },
	{ "SLB ADC4 Mux", "OB67", "OB67" },
	{ "SLB ADC4 Mux", "OB01", "OB01 Bypass Mux" },

	{ "SLBTX", NULL, "SLB" },
	{ "SLBTX", NULL, "SLB ADC1 Mux" },
	{ "SLBTX", NULL, "SLB ADC2 Mux" },
	{ "SLBTX", NULL, "SLB ADC3 Mux" },
	{ "SLBTX", NULL, "SLB ADC4 Mux" },

	{ "IB01 Mux", "IF1 DAC 01", "IF1 DAC01" },
	{ "IB01 Mux", "IF2 DAC 01", "IF2 DAC01" },
	{ "IB01 Mux", "SLB DAC 01", "SLB DAC01" },
	{ "IB01 Mux", "STO1 ADC MIX", "Stereo1 ADC MIX" },
	{ "IB01 Mux", "VAD ADC/DAC1 FS", "DAC1 FS" },

	{ "IB01 Bypass Mux", "Bypass", "IB01 Mux" },
	{ "IB01 Bypass Mux", "Pass SRC", "IB01 Mux" },

	{ "IB23 Mux", "IF1 DAC 23", "IF1 DAC23" },
	{ "IB23 Mux", "IF2 DAC 23", "IF2 DAC23" },
	{ "IB23 Mux", "SLB DAC 23", "SLB DAC23" },
	{ "IB23 Mux", "STO2 ADC MIX", "Stereo2 ADC MIX" },
	{ "IB23 Mux", "DAC1 FS", "DAC1 FS" },
	{ "IB23 Mux", "IF4 DAC", "IF4 DAC" },

	{ "IB23 Bypass Mux", "Bypass", "IB23 Mux" },
	{ "IB23 Bypass Mux", "Pass SRC", "IB23 Mux" },

	{ "IB45 Mux", "IF1 DAC 45", "IF1 DAC45" },
	{ "IB45 Mux", "IF2 DAC 45", "IF2 DAC45" },
	{ "IB45 Mux", "SLB DAC 45", "SLB DAC45" },
	{ "IB45 Mux", "STO3 ADC MIX", "Stereo3 ADC MIX" },
	{ "IB45 Mux", "IF3 DAC", "IF3 DAC" },

	{ "IB45 Bypass Mux", "Bypass", "IB45 Mux" },
	{ "IB45 Bypass Mux", "Pass SRC", "IB45 Mux" },

	{ "IB6 Mux", "IF1 DAC 6", "IF1 DAC6 Mux" },
	{ "IB6 Mux", "IF2 DAC 6", "IF2 DAC6 Mux" },
	{ "IB6 Mux", "SLB DAC 6", "SLB DAC6" },
	{ "IB6 Mux", "STO4 ADC MIX L", "Stereo4 ADC MIXL" },
	{ "IB6 Mux", "IF4 DAC L", "IF4 DAC L" },
	{ "IB6 Mux", "STO1 ADC MIX L", "Stereo1 ADC MIXL" },
	{ "IB6 Mux", "STO2 ADC MIX L", "Stereo2 ADC MIXL" },
	{ "IB6 Mux", "STO3 ADC MIX L", "Stereo3 ADC MIXL" },

	{ "IB7 Mux", "IF1 DAC 7", "IF1 DAC7 Mux" },
	{ "IB7 Mux", "IF2 DAC 7", "IF2 DAC7 Mux" },
	{ "IB7 Mux", "SLB DAC 7", "SLB DAC7" },
	{ "IB7 Mux", "STO4 ADC MIX R", "Stereo4 ADC MIXR" },
	{ "IB7 Mux", "IF4 DAC R", "IF4 DAC R" },
	{ "IB7 Mux", "STO1 ADC MIX R", "Stereo1 ADC MIXR" },
	{ "IB7 Mux", "STO2 ADC MIX R", "Stereo2 ADC MIXR" },
	{ "IB7 Mux", "STO3 ADC MIX R", "Stereo3 ADC MIXR" },

	{ "IB8 Mux", "STO1 ADC MIX L", "Stereo1 ADC MIXL" },
	{ "IB8 Mux", "STO2 ADC MIX L", "Stereo2 ADC MIXL" },
	{ "IB8 Mux", "STO3 ADC MIX L", "Stereo3 ADC MIXL" },
	{ "IB8 Mux", "STO4 ADC MIX L", "Stereo4 ADC MIXL" },
	{ "IB8 Mux", "MONO ADC MIX L", "Mono ADC MIXL" },
	{ "IB8 Mux", "DACL1 FS", "DAC1 MIXL" },

	{ "IB9 Mux", "STO1 ADC MIX R", "Stereo1 ADC MIXR" },
	{ "IB9 Mux", "STO2 ADC MIX R", "Stereo2 ADC MIXR" },
	{ "IB9 Mux", "STO3 ADC MIX R", "Stereo3 ADC MIXR" },
	{ "IB9 Mux", "STO4 ADC MIX R", "Stereo4 ADC MIXR" },
	{ "IB9 Mux", "MONO ADC MIX R", "Mono ADC MIXR" },
	{ "IB9 Mux", "DACR1 FS", "DAC1 MIXR" },
	{ "IB9 Mux", "DAC1 FS", "DAC1 FS" },

	{ "OB01 MIX", "IB01 Switch", "IB01 Bypass Mux" },
	{ "OB01 MIX", "IB23 Switch", "IB23 Bypass Mux" },
	{ "OB01 MIX", "IB45 Switch", "IB45 Bypass Mux" },
	{ "OB01 MIX", "IB6 Switch", "IB6 Mux" },
	{ "OB01 MIX", "IB7 Switch", "IB7 Mux" },
	{ "OB01 MIX", "IB8 Switch", "IB8 Mux" },
	{ "OB01 MIX", "IB9 Switch", "IB9 Mux" },

	{ "OB23 MIX", "IB01 Switch", "IB01 Bypass Mux" },
	{ "OB23 MIX", "IB23 Switch", "IB23 Bypass Mux" },
	{ "OB23 MIX", "IB45 Switch", "IB45 Bypass Mux" },
	{ "OB23 MIX", "IB6 Switch", "IB6 Mux" },
	{ "OB23 MIX", "IB7 Switch", "IB7 Mux" },
	{ "OB23 MIX", "IB8 Switch", "IB8 Mux" },
	{ "OB23 MIX", "IB9 Switch", "IB9 Mux" },

	{ "OB4 MIX", "IB01 Switch", "IB01 Bypass Mux" },
	{ "OB4 MIX", "IB23 Switch", "IB23 Bypass Mux" },
	{ "OB4 MIX", "IB45 Switch", "IB45 Bypass Mux" },
	{ "OB4 MIX", "IB6 Switch", "IB6 Mux" },
	{ "OB4 MIX", "IB7 Switch", "IB7 Mux" },
	{ "OB4 MIX", "IB8 Switch", "IB8 Mux" },
	{ "OB4 MIX", "IB9 Switch", "IB9 Mux" },

	{ "OB5 MIX", "IB01 Switch", "IB01 Bypass Mux" },
	{ "OB5 MIX", "IB23 Switch", "IB23 Bypass Mux" },
	{ "OB5 MIX", "IB45 Switch", "IB45 Bypass Mux" },
	{ "OB5 MIX", "IB6 Switch", "IB6 Mux" },
	{ "OB5 MIX", "IB7 Switch", "IB7 Mux" },
	{ "OB5 MIX", "IB8 Switch", "IB8 Mux" },
	{ "OB5 MIX", "IB9 Switch", "IB9 Mux" },

	{ "OB6 MIX", "IB01 Switch", "IB01 Bypass Mux" },
	{ "OB6 MIX", "IB23 Switch", "IB23 Bypass Mux" },
	{ "OB6 MIX", "IB45 Switch", "IB45 Bypass Mux" },
	{ "OB6 MIX", "IB6 Switch", "IB6 Mux" },
	{ "OB6 MIX", "IB7 Switch", "IB7 Mux" },
	{ "OB6 MIX", "IB8 Switch", "IB8 Mux" },
	{ "OB6 MIX", "IB9 Switch", "IB9 Mux" },

	{ "OB7 MIX", "IB01 Switch", "IB01 Bypass Mux" },
	{ "OB7 MIX", "IB23 Switch", "IB23 Bypass Mux" },
	{ "OB7 MIX", "IB45 Switch", "IB45 Bypass Mux" },
	{ "OB7 MIX", "IB6 Switch", "IB6 Mux" },
	{ "OB7 MIX", "IB7 Switch", "IB7 Mux" },
	{ "OB7 MIX", "IB8 Switch", "IB8 Mux" },
	{ "OB7 MIX", "IB9 Switch", "IB9 Mux" },

	{ "OB01 Bypass Mux", "Bypass", "OB01 MIX" },
	{ "OB01 Bypass Mux", "Pass SRC", "OB01 MIX" },
	{ "OB23 Bypass Mux", "Bypass", "OB23 MIX" },
	{ "OB23 Bypass Mux", "Pass SRC", "OB23 MIX" },

	{ "OutBound2", NULL, "OB23 Bypass Mux" },
	{ "OutBound3", NULL, "OB23 Bypass Mux" },
	{ "OutBound4", NULL, "OB4 MIX" },
	{ "OutBound5", NULL, "OB5 MIX" },
	{ "OutBound6", NULL, "OB6 MIX" },
	{ "OutBound7", NULL, "OB7 MIX" },

	{ "OB45", NULL, "OutBound4" },
	{ "OB45", NULL, "OutBound5" },
	{ "OB67", NULL, "OutBound6" },
	{ "OB67", NULL, "OutBound7" },

	{ "IF1 DAC0", NULL, "AIF1RX" },
	{ "IF1 DAC1", NULL, "AIF1RX" },
	{ "IF1 DAC2", NULL, "AIF1RX" },
	{ "IF1 DAC3", NULL, "AIF1RX" },
	{ "IF1 DAC4", NULL, "AIF1RX" },
	{ "IF1 DAC5", NULL, "AIF1RX" },
	{ "IF1 DAC6", NULL, "AIF1RX" },
	{ "IF1 DAC7", NULL, "AIF1RX" },
	{ "IF1 DAC0", NULL, "I2S1" },
	{ "IF1 DAC1", NULL, "I2S1" },
	{ "IF1 DAC2", NULL, "I2S1" },
	{ "IF1 DAC3", NULL, "I2S1" },
	{ "IF1 DAC4", NULL, "I2S1" },
	{ "IF1 DAC5", NULL, "I2S1" },
	{ "IF1 DAC6", NULL, "I2S1" },
	{ "IF1 DAC7", NULL, "I2S1" },

	{ "IF1 DAC0 Mux", "Slot0", "IF1 DAC0" },
	{ "IF1 DAC0 Mux", "Slot1", "IF1 DAC1" },
	{ "IF1 DAC0 Mux", "Slot2", "IF1 DAC2" },
	{ "IF1 DAC0 Mux", "Slot3", "IF1 DAC3" },
	{ "IF1 DAC0 Mux", "Slot4", "IF1 DAC4" },
	{ "IF1 DAC0 Mux", "Slot5", "IF1 DAC5" },
	{ "IF1 DAC0 Mux", "Slot6", "IF1 DAC6" },
	{ "IF1 DAC0 Mux", "Slot7", "IF1 DAC7" },

	{ "IF1 DAC1 Mux", "Slot0", "IF1 DAC0" },
	{ "IF1 DAC1 Mux", "Slot1", "IF1 DAC1" },
	{ "IF1 DAC1 Mux", "Slot2", "IF1 DAC2" },
	{ "IF1 DAC1 Mux", "Slot3", "IF1 DAC3" },
	{ "IF1 DAC1 Mux", "Slot4", "IF1 DAC4" },
	{ "IF1 DAC1 Mux", "Slot5", "IF1 DAC5" },
	{ "IF1 DAC1 Mux", "Slot6", "IF1 DAC6" },
	{ "IF1 DAC1 Mux", "Slot7", "IF1 DAC7" },

	{ "IF1 DAC2 Mux", "Slot0", "IF1 DAC0" },
	{ "IF1 DAC2 Mux", "Slot1", "IF1 DAC1" },
	{ "IF1 DAC2 Mux", "Slot2", "IF1 DAC2" },
	{ "IF1 DAC2 Mux", "Slot3", "IF1 DAC3" },
	{ "IF1 DAC2 Mux", "Slot4", "IF1 DAC4" },
	{ "IF1 DAC2 Mux", "Slot5", "IF1 DAC5" },
	{ "IF1 DAC2 Mux", "Slot6", "IF1 DAC6" },
	{ "IF1 DAC2 Mux", "Slot7", "IF1 DAC7" },

	{ "IF1 DAC3 Mux", "Slot0", "IF1 DAC0" },
	{ "IF1 DAC3 Mux", "Slot1", "IF1 DAC1" },
	{ "IF1 DAC3 Mux", "Slot2", "IF1 DAC2" },
	{ "IF1 DAC3 Mux", "Slot3", "IF1 DAC3" },
	{ "IF1 DAC3 Mux", "Slot4", "IF1 DAC4" },
	{ "IF1 DAC3 Mux", "Slot5", "IF1 DAC5" },
	{ "IF1 DAC3 Mux", "Slot6", "IF1 DAC6" },
	{ "IF1 DAC3 Mux", "Slot7", "IF1 DAC7" },

	{ "IF1 DAC4 Mux", "Slot0", "IF1 DAC0" },
	{ "IF1 DAC4 Mux", "Slot1", "IF1 DAC1" },
	{ "IF1 DAC4 Mux", "Slot2", "IF1 DAC2" },
	{ "IF1 DAC4 Mux", "Slot3", "IF1 DAC3" },
	{ "IF1 DAC4 Mux", "Slot4", "IF1 DAC4" },
	{ "IF1 DAC4 Mux", "Slot5", "IF1 DAC5" },
	{ "IF1 DAC4 Mux", "Slot6", "IF1 DAC6" },
	{ "IF1 DAC4 Mux", "Slot7", "IF1 DAC7" },

	{ "IF1 DAC5 Mux", "Slot0", "IF1 DAC0" },
	{ "IF1 DAC5 Mux", "Slot1", "IF1 DAC1" },
	{ "IF1 DAC5 Mux", "Slot2", "IF1 DAC2" },
	{ "IF1 DAC5 Mux", "Slot3", "IF1 DAC3" },
	{ "IF1 DAC5 Mux", "Slot4", "IF1 DAC4" },
	{ "IF1 DAC5 Mux", "Slot5", "IF1 DAC5" },
	{ "IF1 DAC5 Mux", "Slot6", "IF1 DAC6" },
	{ "IF1 DAC5 Mux", "Slot7", "IF1 DAC7" },

	{ "IF1 DAC6 Mux", "Slot0", "IF1 DAC0" },
	{ "IF1 DAC6 Mux", "Slot1", "IF1 DAC1" },
	{ "IF1 DAC6 Mux", "Slot2", "IF1 DAC2" },
	{ "IF1 DAC6 Mux", "Slot3", "IF1 DAC3" },
	{ "IF1 DAC6 Mux", "Slot4", "IF1 DAC4" },
	{ "IF1 DAC6 Mux", "Slot5", "IF1 DAC5" },
	{ "IF1 DAC6 Mux", "Slot6", "IF1 DAC6" },
	{ "IF1 DAC6 Mux", "Slot7", "IF1 DAC7" },

	{ "IF1 DAC7 Mux", "Slot0", "IF1 DAC0" },
	{ "IF1 DAC7 Mux", "Slot1", "IF1 DAC1" },
	{ "IF1 DAC7 Mux", "Slot2", "IF1 DAC2" },
	{ "IF1 DAC7 Mux", "Slot3", "IF1 DAC3" },
	{ "IF1 DAC7 Mux", "Slot4", "IF1 DAC4" },
	{ "IF1 DAC7 Mux", "Slot5", "IF1 DAC5" },
	{ "IF1 DAC7 Mux", "Slot6", "IF1 DAC6" },
	{ "IF1 DAC7 Mux", "Slot7", "IF1 DAC7" },

	{ "IF1 DAC01", NULL, "IF1 DAC0 Mux" },
	{ "IF1 DAC01", NULL, "IF1 DAC1 Mux" },
	{ "IF1 DAC23", NULL, "IF1 DAC2 Mux" },
	{ "IF1 DAC23", NULL, "IF1 DAC3 Mux" },
	{ "IF1 DAC45", NULL, "IF1 DAC4 Mux" },
	{ "IF1 DAC45", NULL, "IF1 DAC5 Mux" },
	{ "IF1 DAC67", NULL, "IF1 DAC6 Mux" },
	{ "IF1 DAC67", NULL, "IF1 DAC7 Mux" },

	{ "IF2 DAC0", NULL, "AIF2RX" },
	{ "IF2 DAC1", NULL, "AIF2RX" },
	{ "IF2 DAC2", NULL, "AIF2RX" },
	{ "IF2 DAC3", NULL, "AIF2RX" },
	{ "IF2 DAC4", NULL, "AIF2RX" },
	{ "IF2 DAC5", NULL, "AIF2RX" },
	{ "IF2 DAC6", NULL, "AIF2RX" },
	{ "IF2 DAC7", NULL, "AIF2RX" },
	{ "IF2 DAC0", NULL, "I2S2" },
	{ "IF2 DAC1", NULL, "I2S2" },
	{ "IF2 DAC2", NULL, "I2S2" },
	{ "IF2 DAC3", NULL, "I2S2" },
	{ "IF2 DAC4", NULL, "I2S2" },
	{ "IF2 DAC5", NULL, "I2S2" },
	{ "IF2 DAC6", NULL, "I2S2" },
	{ "IF2 DAC7", NULL, "I2S2" },

	{ "IF2 DAC0 Mux", "Slot0", "IF2 DAC0" },
	{ "IF2 DAC0 Mux", "Slot1", "IF2 DAC1" },
	{ "IF2 DAC0 Mux", "Slot2", "IF2 DAC2" },
	{ "IF2 DAC0 Mux", "Slot3", "IF2 DAC3" },
	{ "IF2 DAC0 Mux", "Slot4", "IF2 DAC4" },
	{ "IF2 DAC0 Mux", "Slot5", "IF2 DAC5" },
	{ "IF2 DAC0 Mux", "Slot6", "IF2 DAC6" },
	{ "IF2 DAC0 Mux", "Slot7", "IF2 DAC7" },

	{ "IF2 DAC1 Mux", "Slot0", "IF2 DAC0" },
	{ "IF2 DAC1 Mux", "Slot1", "IF2 DAC1" },
	{ "IF2 DAC1 Mux", "Slot2", "IF2 DAC2" },
	{ "IF2 DAC1 Mux", "Slot3", "IF2 DAC3" },
	{ "IF2 DAC1 Mux", "Slot4", "IF2 DAC4" },
	{ "IF2 DAC1 Mux", "Slot5", "IF2 DAC5" },
	{ "IF2 DAC1 Mux", "Slot6", "IF2 DAC6" },
	{ "IF2 DAC1 Mux", "Slot7", "IF2 DAC7" },

	{ "IF2 DAC2 Mux", "Slot0", "IF2 DAC0" },
	{ "IF2 DAC2 Mux", "Slot1", "IF2 DAC1" },
	{ "IF2 DAC2 Mux", "Slot2", "IF2 DAC2" },
	{ "IF2 DAC2 Mux", "Slot3", "IF2 DAC3" },
	{ "IF2 DAC2 Mux", "Slot4", "IF2 DAC4" },
	{ "IF2 DAC2 Mux", "Slot5", "IF2 DAC5" },
	{ "IF2 DAC2 Mux", "Slot6", "IF2 DAC6" },
	{ "IF2 DAC2 Mux", "Slot7", "IF2 DAC7" },

	{ "IF2 DAC3 Mux", "Slot0", "IF2 DAC0" },
	{ "IF2 DAC3 Mux", "Slot1", "IF2 DAC1" },
	{ "IF2 DAC3 Mux", "Slot2", "IF2 DAC2" },
	{ "IF2 DAC3 Mux", "Slot3", "IF2 DAC3" },
	{ "IF2 DAC3 Mux", "Slot4", "IF2 DAC4" },
	{ "IF2 DAC3 Mux", "Slot5", "IF2 DAC5" },
	{ "IF2 DAC3 Mux", "Slot6", "IF2 DAC6" },
	{ "IF2 DAC3 Mux", "Slot7", "IF2 DAC7" },

	{ "IF2 DAC4 Mux", "Slot0", "IF2 DAC0" },
	{ "IF2 DAC4 Mux", "Slot1", "IF2 DAC1" },
	{ "IF2 DAC4 Mux", "Slot2", "IF2 DAC2" },
	{ "IF2 DAC4 Mux", "Slot3", "IF2 DAC3" },
	{ "IF2 DAC4 Mux", "Slot4", "IF2 DAC4" },
	{ "IF2 DAC4 Mux", "Slot5", "IF2 DAC5" },
	{ "IF2 DAC4 Mux", "Slot6", "IF2 DAC6" },
	{ "IF2 DAC4 Mux", "Slot7", "IF2 DAC7" },

	{ "IF2 DAC5 Mux", "Slot0", "IF2 DAC0" },
	{ "IF2 DAC5 Mux", "Slot1", "IF2 DAC1" },
	{ "IF2 DAC5 Mux", "Slot2", "IF2 DAC2" },
	{ "IF2 DAC5 Mux", "Slot3", "IF2 DAC3" },
	{ "IF2 DAC5 Mux", "Slot4", "IF2 DAC4" },
	{ "IF2 DAC5 Mux", "Slot5", "IF2 DAC5" },
	{ "IF2 DAC5 Mux", "Slot6", "IF2 DAC6" },
	{ "IF2 DAC5 Mux", "Slot7", "IF2 DAC7" },

	{ "IF2 DAC6 Mux", "Slot0", "IF2 DAC0" },
	{ "IF2 DAC6 Mux", "Slot1", "IF2 DAC1" },
	{ "IF2 DAC6 Mux", "Slot2", "IF2 DAC2" },
	{ "IF2 DAC6 Mux", "Slot3", "IF2 DAC3" },
	{ "IF2 DAC6 Mux", "Slot4", "IF2 DAC4" },
	{ "IF2 DAC6 Mux", "Slot5", "IF2 DAC5" },
	{ "IF2 DAC6 Mux", "Slot6", "IF2 DAC6" },
	{ "IF2 DAC6 Mux", "Slot7", "IF2 DAC7" },

	{ "IF2 DAC7 Mux", "Slot0", "IF2 DAC0" },
	{ "IF2 DAC7 Mux", "Slot1", "IF2 DAC1" },
	{ "IF2 DAC7 Mux", "Slot2", "IF2 DAC2" },
	{ "IF2 DAC7 Mux", "Slot3", "IF2 DAC3" },
	{ "IF2 DAC7 Mux", "Slot4", "IF2 DAC4" },
	{ "IF2 DAC7 Mux", "Slot5", "IF2 DAC5" },
	{ "IF2 DAC7 Mux", "Slot6", "IF2 DAC6" },
	{ "IF2 DAC7 Mux", "Slot7", "IF2 DAC7" },

	{ "IF2 DAC01", NULL, "IF2 DAC0 Mux" },
	{ "IF2 DAC01", NULL, "IF2 DAC1 Mux" },
	{ "IF2 DAC23", NULL, "IF2 DAC2 Mux" },
	{ "IF2 DAC23", NULL, "IF2 DAC3 Mux" },
	{ "IF2 DAC45", NULL, "IF2 DAC4 Mux" },
	{ "IF2 DAC45", NULL, "IF2 DAC5 Mux" },
	{ "IF2 DAC67", NULL, "IF2 DAC6 Mux" },
	{ "IF2 DAC67", NULL, "IF2 DAC7 Mux" },

	{ "IF3 DAC", NULL, "AIF3RX" },
	{ "IF3 DAC", NULL, "I2S3" },

	{ "IF4 DAC", NULL, "AIF4RX" },
	{ "IF4 DAC", NULL, "I2S4" },

	{ "IF3 DAC L", NULL, "IF3 DAC" },
	{ "IF3 DAC R", NULL, "IF3 DAC" },

	{ "IF4 DAC L", NULL, "IF4 DAC" },
	{ "IF4 DAC R", NULL, "IF4 DAC" },

	{ "SLB DAC0", NULL, "SLBRX" },
	{ "SLB DAC1", NULL, "SLBRX" },
	{ "SLB DAC2", NULL, "SLBRX" },
	{ "SLB DAC3", NULL, "SLBRX" },
	{ "SLB DAC4", NULL, "SLBRX" },
	{ "SLB DAC5", NULL, "SLBRX" },
	{ "SLB DAC6", NULL, "SLBRX" },
	{ "SLB DAC7", NULL, "SLBRX" },
	{ "SLB DAC0", NULL, "SLB" },
	{ "SLB DAC1", NULL, "SLB" },
	{ "SLB DAC2", NULL, "SLB" },
	{ "SLB DAC3", NULL, "SLB" },
	{ "SLB DAC4", NULL, "SLB" },
	{ "SLB DAC5", NULL, "SLB" },
	{ "SLB DAC6", NULL, "SLB" },
	{ "SLB DAC7", NULL, "SLB" },

	{ "SLB DAC01", NULL, "SLB DAC0" },
	{ "SLB DAC01", NULL, "SLB DAC1" },
	{ "SLB DAC23", NULL, "SLB DAC2" },
	{ "SLB DAC23", NULL, "SLB DAC3" },
	{ "SLB DAC45", NULL, "SLB DAC4" },
	{ "SLB DAC45", NULL, "SLB DAC5" },
	{ "SLB DAC67", NULL, "SLB DAC6" },
	{ "SLB DAC67", NULL, "SLB DAC7" },

	{ "ADDA1 Mux", "STO1 ADC MIX", "Stereo1 ADC MIX" },
	{ "ADDA1 Mux", "STO2 ADC MIX", "Stereo2 ADC MIX" },
	{ "ADDA1 Mux", "OB 67", "OB67" },

	{ "DAC1 Mux", "IF1 DAC 01", "IF1 DAC01" },
	{ "DAC1 Mux", "IF2 DAC 01", "IF2 DAC01" },
	{ "DAC1 Mux", "IF3 DAC LR", "IF3 DAC" },
	{ "DAC1 Mux", "IF4 DAC LR", "IF4 DAC" },
	{ "DAC1 Mux", "SLB DAC 01", "SLB DAC01" },
	{ "DAC1 Mux", "OB 01", "OB01 Bypass Mux" },

	{ "DAC1 MIXL", "Stereo ADC Switch", "ADDA1 Mux" },
	{ "DAC1 MIXL", "DAC1 Switch", "DAC1 Mux" },
	{ "DAC1 MIXR", "Stereo ADC Switch", "ADDA1 Mux" },
	{ "DAC1 MIXR", "DAC1 Switch", "DAC1 Mux" },

	{ "DAC1 FS", NULL, "DAC1 MIXL" },
	{ "DAC1 FS", NULL, "DAC1 MIXR" },

	{ "DAC2 L Mux", "IF1 DAC 2", "IF1 DAC2 Mux" },
	{ "DAC2 L Mux", "IF2 DAC 2", "IF2 DAC2 Mux" },
	{ "DAC2 L Mux", "IF3 DAC L", "IF3 DAC L" },
	{ "DAC2 L Mux", "IF4 DAC L", "IF4 DAC L" },
	{ "DAC2 L Mux", "SLB DAC 2", "SLB DAC2" },
	{ "DAC2 L Mux", "OB 2", "OutBound2" },

	{ "DAC2 R Mux", "IF1 DAC 3", "IF1 DAC3 Mux" },
	{ "DAC2 R Mux", "IF2 DAC 3", "IF2 DAC3 Mux" },
	{ "DAC2 R Mux", "IF3 DAC R", "IF3 DAC R" },
	{ "DAC2 R Mux", "IF4 DAC R", "IF4 DAC R" },
	{ "DAC2 R Mux", "SLB DAC 3", "SLB DAC3" },
	{ "DAC2 R Mux", "OB 3", "OutBound3" },
	{ "DAC2 R Mux", "Haptic Generator", "Haptic Generator" },
	{ "DAC2 R Mux", "VAD ADC", "VAD ADC Mux" },

	{ "DAC3 L Mux", "IF1 DAC 4", "IF1 DAC4 Mux" },
	{ "DAC3 L Mux", "IF2 DAC 4", "IF2 DAC4 Mux" },
	{ "DAC3 L Mux", "IF3 DAC L", "IF3 DAC L" },
	{ "DAC3 L Mux", "IF4 DAC L", "IF4 DAC L" },
	{ "DAC3 L Mux", "SLB DAC 4", "SLB DAC4" },
	{ "DAC3 L Mux", "OB 4", "OutBound4" },

	{ "DAC3 R Mux", "IF1 DAC 5", "IF1 DAC5 Mux" },
	{ "DAC3 R Mux", "IF2 DAC 5", "IF2 DAC5 Mux" },
	{ "DAC3 R Mux", "IF3 DAC R", "IF3 DAC R" },
	{ "DAC3 R Mux", "IF4 DAC R", "IF4 DAC R" },
	{ "DAC3 R Mux", "SLB DAC 5", "SLB DAC5" },
	{ "DAC3 R Mux", "OB 5", "OutBound5" },

	{ "DAC4 L Mux", "IF1 DAC 6", "IF1 DAC6 Mux" },
	{ "DAC4 L Mux", "IF2 DAC 6", "IF2 DAC6 Mux" },
	{ "DAC4 L Mux", "IF3 DAC L", "IF3 DAC L" },
	{ "DAC4 L Mux", "IF4 DAC L", "IF4 DAC L" },
	{ "DAC4 L Mux", "SLB DAC 6", "SLB DAC6" },
	{ "DAC4 L Mux", "OB 6", "OutBound6" },

	{ "DAC4 R Mux", "IF1 DAC 7", "IF1 DAC7 Mux" },
	{ "DAC4 R Mux", "IF2 DAC 7", "IF2 DAC7 Mux" },
	{ "DAC4 R Mux", "IF3 DAC R", "IF3 DAC R" },
	{ "DAC4 R Mux", "IF4 DAC R", "IF4 DAC R" },
	{ "DAC4 R Mux", "SLB DAC 7", "SLB DAC7" },
	{ "DAC4 R Mux", "OB 7", "OutBound7" },

	{ "Sidetone Mux", "DMIC1 L", "DMIC L1" },
	{ "Sidetone Mux", "DMIC2 L", "DMIC L2" },
	{ "Sidetone Mux", "DMIC3 L", "DMIC L3" },
	{ "Sidetone Mux", "DMIC4 L", "DMIC L4" },
	{ "Sidetone Mux", "ADC1", "ADC 1" },
	{ "Sidetone Mux", "ADC2", "ADC 2" },
	{ "Sidetone Mux", NULL, "Sidetone Power" },

	{ "Stereo DAC MIXL", "ST L Switch", "Sidetone Mux" },
	{ "Stereo DAC MIXL", "DAC1 L Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXL", "DAC2 L Switch", "DAC2 L Mux" },
	{ "Stereo DAC MIXL", "DAC1 R Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXL", NULL, "dac stereo1 filter" },
	{ "Stereo DAC MIXR", "ST R Switch", "Sidetone Mux" },
	{ "Stereo DAC MIXR", "DAC1 R Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXR", "DAC2 R Switch", "DAC2 R Mux" },
	{ "Stereo DAC MIXR", "DAC1 L Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXR", NULL, "dac stereo1 filter" },
	{ "dac stereo1 filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Mono DAC MIXL", "ST L Switch", "Sidetone Mux" },
	{ "Mono DAC MIXL", "DAC1 L Switch", "DAC1 MIXL" },
	{ "Mono DAC MIXL", "DAC2 L Switch", "DAC2 L Mux" },
	{ "Mono DAC MIXL", "DAC2 R Switch", "DAC2 R Mux" },
	{ "Mono DAC MIXL", NULL, "dac mono2 left filter" },
	{ "dac mono2 left filter", NULL, "PLL1", is_sys_clk_from_pll },
	{ "Mono DAC MIXR", "ST R Switch", "Sidetone Mux" },
	{ "Mono DAC MIXR", "DAC1 R Switch", "DAC1 MIXR" },
	{ "Mono DAC MIXR", "DAC2 R Switch", "DAC2 R Mux" },
	{ "Mono DAC MIXR", "DAC2 L Switch", "DAC2 L Mux" },
	{ "Mono DAC MIXR", NULL, "dac mono2 right filter" },
	{ "dac mono2 right filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "DD1 MIXL", "Sto DAC Mix L Switch", "Stereo DAC MIXL" },
	{ "DD1 MIXL", "Mono DAC Mix L Switch", "Mono DAC MIXL" },
	{ "DD1 MIXL", "DAC3 L Switch", "DAC3 L Mux" },
	{ "DD1 MIXL", "DAC3 R Switch", "DAC3 R Mux" },
	{ "DD1 MIXL", NULL, "dac mono3 left filter" },
	{ "dac mono3 left filter", NULL, "PLL1", is_sys_clk_from_pll },
	{ "DD1 MIXR", "Sto DAC Mix R Switch", "Stereo DAC MIXR" },
	{ "DD1 MIXR", "Mono DAC Mix R Switch", "Mono DAC MIXR" },
	{ "DD1 MIXR", "DAC3 L Switch", "DAC3 L Mux" },
	{ "DD1 MIXR", "DAC3 R Switch", "DAC3 R Mux" },
	{ "DD1 MIXR", NULL, "dac mono3 right filter" },
	{ "dac mono3 right filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "DD2 MIXL", "Sto DAC Mix L Switch", "Stereo DAC MIXL" },
	{ "DD2 MIXL", "Mono DAC Mix L Switch", "Mono DAC MIXL" },
	{ "DD2 MIXL", "DAC4 L Switch", "DAC4 L Mux" },
	{ "DD2 MIXL", "DAC4 R Switch", "DAC4 R Mux" },
	{ "DD2 MIXL", NULL, "dac mono4 left filter" },
	{ "dac mono4 left filter", NULL, "PLL1", is_sys_clk_from_pll },
	{ "DD2 MIXR", "Sto DAC Mix R Switch", "Stereo DAC MIXR" },
	{ "DD2 MIXR", "Mono DAC Mix R Switch", "Mono DAC MIXR" },
	{ "DD2 MIXR", "DAC4 L Switch", "DAC4 L Mux" },
	{ "DD2 MIXR", "DAC4 R Switch", "DAC4 R Mux" },
	{ "DD2 MIXR", NULL, "dac mono4 right filter" },
	{ "dac mono4 right filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Stereo DAC MIX", NULL, "Stereo DAC MIXL" },
	{ "Stereo DAC MIX", NULL, "Stereo DAC MIXR" },
	{ "Mono DAC MIX", NULL, "Mono DAC MIXL" },
	{ "Mono DAC MIX", NULL, "Mono DAC MIXR" },
	{ "DD1 MIX", NULL, "DD1 MIXL" },
	{ "DD1 MIX", NULL, "DD1 MIXR" },
	{ "DD2 MIX", NULL, "DD2 MIXL" },
	{ "DD2 MIX", NULL, "DD2 MIXR" },

	{ "DAC12 SRC Mux", "STO1 DAC MIX", "Stereo DAC MIX" },
	{ "DAC12 SRC Mux", "MONO DAC MIX", "Mono DAC MIX" },
	{ "DAC12 SRC Mux", "DD MIX1", "DD1 MIX" },
	{ "DAC12 SRC Mux", "DD MIX2", "DD2 MIX" },

	{ "DAC3 SRC Mux", "MONO DAC MIXL", "Mono DAC MIXL" },
	{ "DAC3 SRC Mux", "MONO DAC MIXR", "Mono DAC MIXR" },
	{ "DAC3 SRC Mux", "DD MIX1L", "DD1 MIXL" },
	{ "DAC3 SRC Mux", "DD MIX2L", "DD2 MIXL" },

	{ "DAC 1", NULL, "DAC12 SRC Mux" },
	{ "DAC 2", NULL, "DAC12 SRC Mux" },
	{ "DAC 3", NULL, "DAC3 SRC Mux" },

	{ "PDM1 L Mux", "STO1 DAC MIX", "Stereo DAC MIXL" },
	{ "PDM1 L Mux", "MONO DAC MIX", "Mono DAC MIXL" },
	{ "PDM1 L Mux", "DD MIX1", "DD1 MIXL" },
	{ "PDM1 L Mux", "DD MIX2", "DD2 MIXL" },
	{ "PDM1 L Mux", NULL, "PDM1 Power" },
	{ "PDM1 R Mux", "STO1 DAC MIX", "Stereo DAC MIXR" },
	{ "PDM1 R Mux", "MONO DAC MIX", "Mono DAC MIXR" },
	{ "PDM1 R Mux", "DD MIX1", "DD1 MIXR" },
	{ "PDM1 R Mux", "DD MIX2", "DD2 MIXR" },
	{ "PDM1 R Mux", NULL, "PDM1 Power" },
	{ "PDM2 L Mux", "STO1 DAC MIX", "Stereo DAC MIXL" },
	{ "PDM2 L Mux", "MONO DAC MIX", "Mono DAC MIXL" },
	{ "PDM2 L Mux", "DD MIX1", "DD1 MIXL" },
	{ "PDM2 L Mux", "DD MIX2", "DD2 MIXL" },
	{ "PDM2 L Mux", NULL, "PDM2 Power" },
	{ "PDM2 R Mux", "STO1 DAC MIX", "Stereo DAC MIXR" },
	{ "PDM2 R Mux", "MONO DAC MIX", "Mono DAC MIXR" },
	{ "PDM2 R Mux", "DD MIX1", "DD1 MIXR" },
	{ "PDM2 R Mux", "DD MIX1", "DD2 MIXR" },
	{ "PDM2 R Mux", NULL, "PDM2 Power" },

	{ "LOUT1 amp", NULL, "DAC 1" },
	{ "LOUT2 amp", NULL, "DAC 2" },
	{ "LOUT3 amp", NULL, "DAC 3" },

	{ "LOUT1 vref", NULL, "LOUT1 amp" },
	{ "LOUT2 vref", NULL, "LOUT2 amp" },
	{ "LOUT3 vref", NULL, "LOUT3 amp" },

	{ "LOUT1", NULL, "LOUT1 vref" },
	{ "LOUT2", NULL, "LOUT2 vref" },
	{ "LOUT3", NULL, "LOUT3 vref" },

	{ "PDM1L", NULL, "PDM1 L Mux" },
	{ "PDM1R", NULL, "PDM1 R Mux" },
	{ "PDM2L", NULL, "PDM2 L Mux" },
	{ "PDM2R", NULL, "PDM2 R Mux" },
};

static const struct snd_soc_dapm_route rt5677_dmic2_clk_1[] = {
	{ "DMIC L2", NULL, "DMIC1 power" },
	{ "DMIC R2", NULL, "DMIC1 power" },
};

static const struct snd_soc_dapm_route rt5677_dmic2_clk_2[] = {
	{ "DMIC L2", NULL, "DMIC2 power" },
	{ "DMIC R2", NULL, "DMIC2 power" },
};

static int rt5677_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

	rt5677->lrck[dai->id] = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5677->sysclk, rt5677->lrck[dai->id]);
	if (pre_div < 0) {
		dev_err(codec->dev, "Unsupported clock setting: sysclk=%dHz lrck=%dHz\n",
			rt5677->sysclk, rt5677->lrck[dai->id]);
		return -EINVAL;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(codec->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}
	bclk_ms = frame_size > 32;
	rt5677->bclk[dai->id] = rt5677->lrck[dai->id] * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt5677->bclk[dai->id], rt5677->lrck[dai->id]);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		val_len |= RT5677_I2S_DL_20;
		break;
	case 24:
		val_len |= RT5677_I2S_DL_24;
		break;
	case 8:
		val_len |= RT5677_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5677_AIF1:
		mask_clk = RT5677_I2S_PD1_MASK;
		val_clk = pre_div << RT5677_I2S_PD1_SFT;
		regmap_update_bits(rt5677->regmap, RT5677_I2S1_SDP,
			RT5677_I2S_DL_MASK, val_len);
		regmap_update_bits(rt5677->regmap, RT5677_CLK_TREE_CTRL1,
			mask_clk, val_clk);
		break;
	case RT5677_AIF2:
		mask_clk = RT5677_I2S_PD2_MASK;
		val_clk = pre_div << RT5677_I2S_PD2_SFT;
		regmap_update_bits(rt5677->regmap, RT5677_I2S2_SDP,
			RT5677_I2S_DL_MASK, val_len);
		regmap_update_bits(rt5677->regmap, RT5677_CLK_TREE_CTRL1,
			mask_clk, val_clk);
		break;
	case RT5677_AIF3:
		mask_clk = RT5677_I2S_BCLK_MS3_MASK | RT5677_I2S_PD3_MASK;
		val_clk = bclk_ms << RT5677_I2S_BCLK_MS3_SFT |
			pre_div << RT5677_I2S_PD3_SFT;
		regmap_update_bits(rt5677->regmap, RT5677_I2S3_SDP,
			RT5677_I2S_DL_MASK, val_len);
		regmap_update_bits(rt5677->regmap, RT5677_CLK_TREE_CTRL1,
			mask_clk, val_clk);
		break;
	case RT5677_AIF4:
		mask_clk = RT5677_I2S_BCLK_MS4_MASK | RT5677_I2S_PD4_MASK;
		val_clk = bclk_ms << RT5677_I2S_BCLK_MS4_SFT |
			pre_div << RT5677_I2S_PD4_SFT;
		regmap_update_bits(rt5677->regmap, RT5677_I2S4_SDP,
			RT5677_I2S_DL_MASK, val_len);
		regmap_update_bits(rt5677->regmap, RT5677_CLK_TREE_CTRL1,
			mask_clk, val_clk);
		break;
	default:
		break;
	}

	return 0;
}

static int rt5677_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5677->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5677_I2S_MS_S;
		rt5677->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5677_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5677_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5677_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5677_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5677_AIF1:
		regmap_update_bits(rt5677->regmap, RT5677_I2S1_SDP,
			RT5677_I2S_MS_MASK | RT5677_I2S_BP_MASK |
			RT5677_I2S_DF_MASK, reg_val);
		break;
	case RT5677_AIF2:
		regmap_update_bits(rt5677->regmap, RT5677_I2S2_SDP,
			RT5677_I2S_MS_MASK | RT5677_I2S_BP_MASK |
			RT5677_I2S_DF_MASK, reg_val);
		break;
	case RT5677_AIF3:
		regmap_update_bits(rt5677->regmap, RT5677_I2S3_SDP,
			RT5677_I2S_MS_MASK | RT5677_I2S_BP_MASK |
			RT5677_I2S_DF_MASK, reg_val);
		break;
	case RT5677_AIF4:
		regmap_update_bits(rt5677->regmap, RT5677_I2S4_SDP,
			RT5677_I2S_MS_MASK | RT5677_I2S_BP_MASK |
			RT5677_I2S_DF_MASK, reg_val);
		break;
	default:
		break;
	}


	return 0;
}

static int rt5677_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	if (freq == rt5677->sysclk && clk_id == rt5677->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5677_SCLK_S_MCLK:
		reg_val |= RT5677_SCLK_SRC_MCLK;
		break;
	case RT5677_SCLK_S_PLL1:
		reg_val |= RT5677_SCLK_SRC_PLL1;
		break;
	case RT5677_SCLK_S_RCCLK:
		reg_val |= RT5677_SCLK_SRC_RCCLK;
		break;
	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK1,
		RT5677_SCLK_SRC_MASK, reg_val);
	rt5677->sysclk = freq;
	rt5677->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

/**
 * rt5677_pll_calc - Calcualte PLL M/N/K code.
 * @freq_in: external clock provided to codec.
 * @freq_out: target clock which codec works on.
 * @pll_code: Pointer to structure with M, N, K, bypass K and bypass M flag.
 *
 * Calcualte M/N/K code and bypass K/M flag to configure PLL for codec.
 *
 * Returns 0 for success or negative error code.
 */
static int rt5677_pll_calc(const unsigned int freq_in,
	const unsigned int freq_out, struct rl6231_pll_code *pll_code)
{
	if (RT5677_PLL_INP_MIN > freq_in)
		return -EINVAL;

	return rl6231_pll_calc(freq_in, freq_out, pll_code);
}

static int rt5677_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt5677->pll_src && freq_in == rt5677->pll_in &&
	    freq_out == rt5677->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");

		rt5677->pll_in = 0;
		rt5677->pll_out = 0;
		regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK1,
			RT5677_SCLK_SRC_MASK, RT5677_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5677_PLL1_S_MCLK:
		regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK1,
			RT5677_PLL1_SRC_MASK, RT5677_PLL1_SRC_MCLK);
		break;
	case RT5677_PLL1_S_BCLK1:
	case RT5677_PLL1_S_BCLK2:
	case RT5677_PLL1_S_BCLK3:
	case RT5677_PLL1_S_BCLK4:
		switch (dai->id) {
		case RT5677_AIF1:
			regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK1,
				RT5677_PLL1_SRC_MASK, RT5677_PLL1_SRC_BCLK1);
			break;
		case RT5677_AIF2:
			regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK1,
				RT5677_PLL1_SRC_MASK, RT5677_PLL1_SRC_BCLK2);
			break;
		case RT5677_AIF3:
			regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK1,
				RT5677_PLL1_SRC_MASK, RT5677_PLL1_SRC_BCLK3);
			break;
		case RT5677_AIF4:
			regmap_update_bits(rt5677->regmap, RT5677_GLB_CLK1,
				RT5677_PLL1_SRC_MASK, RT5677_PLL1_SRC_BCLK4);
			break;
		default:
			break;
		}
		break;
	default:
		dev_err(codec->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rt5677_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(codec->dev, "m_bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	regmap_write(rt5677->regmap, RT5677_PLL1_CTRL1,
		pll_code.n_code << RT5677_PLL_N_SFT | pll_code.k_code);
	regmap_write(rt5677->regmap, RT5677_PLL1_CTRL2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5677_PLL_M_SFT |
		pll_code.m_bp << RT5677_PLL_M_BP_SFT);

	rt5677->pll_in = freq_in;
	rt5677->pll_out = freq_out;
	rt5677->pll_src = source;

	return 0;
}

static int rt5677_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0, slot_width_25 = 0;

	if (rx_mask || tx_mask)
		val |= (1 << 12);

	switch (slots) {
	case 4:
		val |= (1 << 10);
		break;
	case 6:
		val |= (2 << 10);
		break;
	case 8:
		val |= (3 << 10);
		break;
	case 2:
	default:
		break;
	}

	switch (slot_width) {
	case 20:
		val |= (1 << 8);
		break;
	case 25:
		slot_width_25 = 0x8080;
	case 24:
		val |= (2 << 8);
		break;
	case 32:
		val |= (3 << 8);
		break;
	case 16:
	default:
		break;
	}

	switch (dai->id) {
	case RT5677_AIF1:
		regmap_update_bits(rt5677->regmap, RT5677_TDM1_CTRL1, 0x1f00,
			val);
		regmap_update_bits(rt5677->regmap, RT5677_DIG_MISC, 0x8000,
			slot_width_25);
		break;
	case RT5677_AIF2:
		regmap_update_bits(rt5677->regmap, RT5677_TDM2_CTRL1, 0x1f00,
			val);
		regmap_update_bits(rt5677->regmap, RT5677_DIG_MISC, 0x80,
			slot_width_25);
		break;
	default:
		break;
	}

	return 0;
}

static int rt5677_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		if (codec->dapm.bias_level == SND_SOC_BIAS_STANDBY) {
			rt5677_set_dsp_vad(codec, false);

			regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG1,
				RT5677_LDO1_SEL_MASK | RT5677_LDO2_SEL_MASK,
				0x0055);
			regmap_update_bits(rt5677->regmap,
				RT5677_PR_BASE + RT5677_BIAS_CUR4,
				0x0f00, 0x0f00);
			regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG1,
				RT5677_PWR_FV1 | RT5677_PWR_FV2 |
				RT5677_PWR_VREF1 | RT5677_PWR_MB |
				RT5677_PWR_BG | RT5677_PWR_VREF2,
				RT5677_PWR_VREF1 | RT5677_PWR_MB |
				RT5677_PWR_BG | RT5677_PWR_VREF2);
			rt5677->is_vref_slow = false;
			regmap_update_bits(rt5677->regmap, RT5677_PWR_ANLG2,
				RT5677_PWR_CORE, RT5677_PWR_CORE);
			regmap_update_bits(rt5677->regmap, RT5677_DIG_MISC,
				0x1, 0x1);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		break;

	case SND_SOC_BIAS_OFF:
		regmap_update_bits(rt5677->regmap, RT5677_DIG_MISC, 0x1, 0x0);
		regmap_write(rt5677->regmap, RT5677_PWR_DIG1, 0x0000);
		regmap_write(rt5677->regmap, RT5677_PWR_DIG2, 0x0000);
		regmap_write(rt5677->regmap, RT5677_PWR_ANLG1, 0x0022);
		regmap_write(rt5677->regmap, RT5677_PWR_ANLG2, 0x0000);
		regmap_update_bits(rt5677->regmap,
			RT5677_PR_BASE + RT5677_BIAS_CUR4, 0x0f00, 0x0000);

		if (rt5677->dsp_vad_en)
			rt5677_set_dsp_vad(codec, true);
		break;

	default:
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

#ifdef CONFIG_GPIOLIB
static inline struct rt5677_priv *gpio_to_rt5677(struct gpio_chip *chip)
{
	return container_of(chip, struct rt5677_priv, gpio_chip);
}

static void rt5677_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct rt5677_priv *rt5677 = gpio_to_rt5677(chip);

	switch (offset) {
	case RT5677_GPIO1 ... RT5677_GPIO5:
		regmap_update_bits(rt5677->regmap, RT5677_GPIO_CTRL2,
			0x1 << (offset * 3 + 1), !!value << (offset * 3 + 1));
		break;

	case RT5677_GPIO6:
		regmap_update_bits(rt5677->regmap, RT5677_GPIO_CTRL3,
			RT5677_GPIO6_OUT_MASK, !!value << RT5677_GPIO6_OUT_SFT);
		break;

	default:
		break;
	}
}

static int rt5677_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct rt5677_priv *rt5677 = gpio_to_rt5677(chip);

	switch (offset) {
	case RT5677_GPIO1 ... RT5677_GPIO5:
		regmap_update_bits(rt5677->regmap, RT5677_GPIO_CTRL2,
			0x3 << (offset * 3 + 1),
			(0x2 | !!value) << (offset * 3 + 1));
		break;

	case RT5677_GPIO6:
		regmap_update_bits(rt5677->regmap, RT5677_GPIO_CTRL3,
			RT5677_GPIO6_DIR_MASK | RT5677_GPIO6_OUT_MASK,
			RT5677_GPIO6_DIR_OUT | !!value << RT5677_GPIO6_OUT_SFT);
		break;

	default:
		break;
	}

	return 0;
}

static int rt5677_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct rt5677_priv *rt5677 = gpio_to_rt5677(chip);
	int value, ret;

	ret = regmap_read(rt5677->regmap, RT5677_GPIO_ST, &value);
	if (ret < 0)
		return ret;

	return (value & (0x1 << offset)) >> offset;
}

static int rt5677_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct rt5677_priv *rt5677 = gpio_to_rt5677(chip);

	switch (offset) {
	case RT5677_GPIO1 ... RT5677_GPIO5:
		regmap_update_bits(rt5677->regmap, RT5677_GPIO_CTRL2,
			0x1 << (offset * 3 + 2), 0x0);
		break;

	case RT5677_GPIO6:
		regmap_update_bits(rt5677->regmap, RT5677_GPIO_CTRL3,
			RT5677_GPIO6_DIR_MASK, RT5677_GPIO6_DIR_IN);
		break;

	default:
		break;
	}

	return 0;
}

/** Configures the gpio as
 *   0 - floating
 *   1 - pull down
 *   2 - pull up
 */
static void rt5677_gpio_config(struct rt5677_priv *rt5677, unsigned offset,
		int value)
{
	int shift;

	switch (offset) {
	case RT5677_GPIO1 ... RT5677_GPIO2:
		shift = 2 * (1 - offset);
		regmap_update_bits(rt5677->regmap,
			RT5677_PR_BASE + RT5677_DIG_IN_PIN_ST_CTRL2,
			0x3 << shift,
			(value & 0x3) << shift);
		break;

	case RT5677_GPIO3 ... RT5677_GPIO6:
		shift = 2 * (9 - offset);
		regmap_update_bits(rt5677->regmap,
			RT5677_PR_BASE + RT5677_DIG_IN_PIN_ST_CTRL3,
			0x3 << shift,
			(value & 0x3) << shift);
		break;

	default:
		break;
	}
}

static int rt5677_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct rt5677_priv *rt5677 = gpio_to_rt5677(chip);
	struct regmap_irq_chip_data *data = rt5677->irq_data;
	int irq;

	if (offset >= RT5677_GPIO1 && offset <= RT5677_GPIO3) {
		if ((rt5677->pdata.jd1_gpio == 1 && offset == RT5677_GPIO1) ||
			(rt5677->pdata.jd1_gpio == 2 &&
				offset == RT5677_GPIO2) ||
			(rt5677->pdata.jd1_gpio == 3 &&
				offset == RT5677_GPIO3)) {
			irq = RT5677_IRQ_JD1;
		} else {
			return -ENXIO;
		}
	}

	if (offset >= RT5677_GPIO4 && offset <= RT5677_GPIO6) {
		if ((rt5677->pdata.jd2_gpio == 1 && offset == RT5677_GPIO4) ||
			(rt5677->pdata.jd2_gpio == 2 &&
				offset == RT5677_GPIO5) ||
			(rt5677->pdata.jd2_gpio == 3 &&
				offset == RT5677_GPIO6)) {
			irq = RT5677_IRQ_JD2;
		} else if ((rt5677->pdata.jd3_gpio == 1 &&
				offset == RT5677_GPIO4) ||
			(rt5677->pdata.jd3_gpio == 2 &&
				offset == RT5677_GPIO5) ||
			(rt5677->pdata.jd3_gpio == 3 &&
				offset == RT5677_GPIO6)) {
			irq = RT5677_IRQ_JD3;
		} else {
			return -ENXIO;
		}
	}

	return regmap_irq_get_virq(data, irq);
}

static struct gpio_chip rt5677_template_chip = {
	.label			= "rt5677",
	.owner			= THIS_MODULE,
	.direction_output	= rt5677_gpio_direction_out,
	.set			= rt5677_gpio_set,
	.direction_input	= rt5677_gpio_direction_in,
	.get			= rt5677_gpio_get,
	.to_irq			= rt5677_to_irq,
	.can_sleep		= 1,
};

static void rt5677_init_gpio(struct i2c_client *i2c)
{
	struct rt5677_priv *rt5677 = i2c_get_clientdata(i2c);
	int ret;

	rt5677->gpio_chip = rt5677_template_chip;
	rt5677->gpio_chip.ngpio = RT5677_GPIO_NUM;
	rt5677->gpio_chip.dev = &i2c->dev;
	rt5677->gpio_chip.base = -1;

	ret = gpiochip_add(&rt5677->gpio_chip);
	if (ret != 0)
		dev_err(&i2c->dev, "Failed to add GPIOs: %d\n", ret);
}

static void rt5677_free_gpio(struct i2c_client *i2c)
{
	struct rt5677_priv *rt5677 = i2c_get_clientdata(i2c);

	gpiochip_remove(&rt5677->gpio_chip);
}
#else
static void rt5677_gpio_config(struct rt5677_priv *rt5677, unsigned offset,
		int value)
{
}

static void rt5677_init_gpio(struct i2c_client *i2c)
{
}

static void rt5677_free_gpio(struct i2c_client *i2c)
{
}
#endif

static int rt5677_probe(struct snd_soc_codec *codec)
{
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);
	int i;

	rt5677->codec = codec;

	if (rt5677->pdata.dmic2_clk_pin == RT5677_DMIC_CLK2) {
		snd_soc_dapm_add_routes(&codec->dapm,
			rt5677_dmic2_clk_2,
			ARRAY_SIZE(rt5677_dmic2_clk_2));
	} else { /*use dmic1 clock by default*/
		snd_soc_dapm_add_routes(&codec->dapm,
			rt5677_dmic2_clk_1,
			ARRAY_SIZE(rt5677_dmic2_clk_1));
	}

	rt5677_set_bias_level(codec, SND_SOC_BIAS_OFF);

	regmap_write(rt5677->regmap, RT5677_DIG_MISC, 0x0020);
	regmap_write(rt5677->regmap, RT5677_PWR_DSP2, 0x0c00);

	for (i = 0; i < RT5677_GPIO_NUM; i++)
		rt5677_gpio_config(rt5677, i, rt5677->pdata.gpio_config[i]);

	if (rt5677->irq_data) {
		regmap_update_bits(rt5677->regmap, RT5677_GPIO_CTRL1, 0x8000,
			0x8000);
		regmap_update_bits(rt5677->regmap, RT5677_DIG_MISC, 0x0018,
			0x0008);

		if (rt5677->pdata.jd1_gpio)
			regmap_update_bits(rt5677->regmap, RT5677_JD_CTRL1,
				RT5677_SEL_GPIO_JD1_MASK,
				rt5677->pdata.jd1_gpio <<
				RT5677_SEL_GPIO_JD1_SFT);

		if (rt5677->pdata.jd2_gpio)
			regmap_update_bits(rt5677->regmap, RT5677_JD_CTRL1,
				RT5677_SEL_GPIO_JD2_MASK,
				rt5677->pdata.jd2_gpio <<
				RT5677_SEL_GPIO_JD2_SFT);

		if (rt5677->pdata.jd3_gpio)
			regmap_update_bits(rt5677->regmap, RT5677_JD_CTRL1,
				RT5677_SEL_GPIO_JD3_MASK,
				rt5677->pdata.jd3_gpio <<
				RT5677_SEL_GPIO_JD3_SFT);
	}

	mutex_init(&rt5677->dsp_cmd_lock);
	mutex_init(&rt5677->dsp_pri_lock);

	return 0;
}

static int rt5677_remove(struct snd_soc_codec *codec)
{
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	regmap_write(rt5677->regmap, RT5677_RESET, 0x10ec);
	if (gpio_is_valid(rt5677->pow_ldo2))
		gpio_set_value_cansleep(rt5677->pow_ldo2, 0);

	return 0;
}

#ifdef CONFIG_PM
static int rt5677_suspend(struct snd_soc_codec *codec)
{
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	if (!rt5677->dsp_vad_en) {
		regcache_cache_only(rt5677->regmap, true);
		regcache_mark_dirty(rt5677->regmap);
	}

	if (gpio_is_valid(rt5677->pow_ldo2))
		gpio_set_value_cansleep(rt5677->pow_ldo2, 0);

	return 0;
}

static int rt5677_resume(struct snd_soc_codec *codec)
{
	struct rt5677_priv *rt5677 = snd_soc_codec_get_drvdata(codec);

	if (gpio_is_valid(rt5677->pow_ldo2)) {
		gpio_set_value_cansleep(rt5677->pow_ldo2, 1);
		msleep(10);
	}

	if (!rt5677->dsp_vad_en) {
		regcache_cache_only(rt5677->regmap, false);
		regcache_sync(rt5677->regmap);
	}

	return 0;
}
#else
#define rt5677_suspend NULL
#define rt5677_resume NULL
#endif

static int rt5677_read(void *context, unsigned int reg, unsigned int *val)
{
	struct i2c_client *client = context;
	struct rt5677_priv *rt5677 = i2c_get_clientdata(client);

	if (rt5677->is_dsp_mode) {
		if (reg > 0xff) {
			mutex_lock(&rt5677->dsp_pri_lock);
			rt5677_dsp_mode_i2c_write(rt5677, RT5677_PRIV_INDEX,
				reg & 0xff);
			rt5677_dsp_mode_i2c_read(rt5677, RT5677_PRIV_DATA, val);
			mutex_unlock(&rt5677->dsp_pri_lock);
		} else {
			rt5677_dsp_mode_i2c_read(rt5677, reg, val);
		}
	} else {
		regmap_read(rt5677->regmap_physical, reg, val);
	}

	return 0;
}

static int rt5677_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *client = context;
	struct rt5677_priv *rt5677 = i2c_get_clientdata(client);

	if (rt5677->is_dsp_mode) {
		if (reg > 0xff) {
			mutex_lock(&rt5677->dsp_pri_lock);
			rt5677_dsp_mode_i2c_write(rt5677, RT5677_PRIV_INDEX,
				reg & 0xff);
			rt5677_dsp_mode_i2c_write(rt5677, RT5677_PRIV_DATA,
				val);
			mutex_unlock(&rt5677->dsp_pri_lock);
		} else {
			rt5677_dsp_mode_i2c_write(rt5677, reg, val);
		}
	} else {
		regmap_write(rt5677->regmap_physical, reg, val);
	}

	return 0;
}

#define RT5677_STEREO_RATES SNDRV_PCM_RATE_8000_96000
#define RT5677_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static struct snd_soc_dai_ops rt5677_aif_dai_ops = {
	.hw_params = rt5677_hw_params,
	.set_fmt = rt5677_set_dai_fmt,
	.set_sysclk = rt5677_set_dai_sysclk,
	.set_pll = rt5677_set_dai_pll,
	.set_tdm_slot = rt5677_set_tdm_slot,
};

static struct snd_soc_dai_driver rt5677_dai[] = {
	{
		.name = "rt5677-aif1",
		.id = RT5677_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.ops = &rt5677_aif_dai_ops,
	},
	{
		.name = "rt5677-aif2",
		.id = RT5677_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.ops = &rt5677_aif_dai_ops,
	},
	{
		.name = "rt5677-aif3",
		.id = RT5677_AIF3,
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.ops = &rt5677_aif_dai_ops,
	},
	{
		.name = "rt5677-aif4",
		.id = RT5677_AIF4,
		.playback = {
			.stream_name = "AIF4 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.capture = {
			.stream_name = "AIF4 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.ops = &rt5677_aif_dai_ops,
	},
	{
		.name = "rt5677-slimbus",
		.id = RT5677_AIF5,
		.playback = {
			.stream_name = "SLIMBus Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.capture = {
			.stream_name = "SLIMBus Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5677_STEREO_RATES,
			.formats = RT5677_FORMATS,
		},
		.ops = &rt5677_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5677 = {
	.probe = rt5677_probe,
	.remove = rt5677_remove,
	.suspend = rt5677_suspend,
	.resume = rt5677_resume,
	.set_bias_level = rt5677_set_bias_level,
	.idle_bias_off = true,
	.controls = rt5677_snd_controls,
	.num_controls = ARRAY_SIZE(rt5677_snd_controls),
	.dapm_widgets = rt5677_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5677_dapm_widgets),
	.dapm_routes = rt5677_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5677_dapm_routes),
};

static const struct regmap_config rt5677_regmap_physical = {
	.name = "physical",
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = RT5677_VENDOR_ID2 + 1 + (ARRAY_SIZE(rt5677_ranges) *
						RT5677_PR_SPACING),
	.readable_reg = rt5677_readable_register,

	.cache_type = REGCACHE_NONE,
	.ranges = rt5677_ranges,
	.num_ranges = ARRAY_SIZE(rt5677_ranges),
};

static const struct regmap_config rt5677_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = RT5677_VENDOR_ID2 + 1 + (ARRAY_SIZE(rt5677_ranges) *
						RT5677_PR_SPACING),

	.volatile_reg = rt5677_volatile_register,
	.readable_reg = rt5677_readable_register,
	.reg_read = rt5677_read,
	.reg_write = rt5677_write,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5677_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5677_reg),
	.ranges = rt5677_ranges,
	.num_ranges = ARRAY_SIZE(rt5677_ranges),
};

static const struct i2c_device_id rt5677_i2c_id[] = {
	{ "rt5677", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5677_i2c_id);

static int rt5677_parse_dt(struct rt5677_priv *rt5677, struct device_node *np)
{
	rt5677->pdata.in1_diff = of_property_read_bool(np,
					"realtek,in1-differential");
	rt5677->pdata.in2_diff = of_property_read_bool(np,
					"realtek,in2-differential");
	rt5677->pdata.lout1_diff = of_property_read_bool(np,
					"realtek,lout1-differential");
	rt5677->pdata.lout2_diff = of_property_read_bool(np,
					"realtek,lout2-differential");
	rt5677->pdata.lout3_diff = of_property_read_bool(np,
					"realtek,lout3-differential");

	rt5677->pow_ldo2 = of_get_named_gpio(np,
					"realtek,pow-ldo2-gpio", 0);

	/*
	 * POW_LDO2 is optional (it may be statically tied on the board).
	 * -ENOENT means that the property doesn't exist, i.e. there is no
	 * GPIO, so is not an error. Any other error code means the property
	 * exists, but could not be parsed.
	 */
	if (!gpio_is_valid(rt5677->pow_ldo2) &&
			(rt5677->pow_ldo2 != -ENOENT))
		return rt5677->pow_ldo2;

	of_property_read_u8_array(np, "realtek,gpio-config",
		rt5677->pdata.gpio_config, RT5677_GPIO_NUM);

	of_property_read_u32(np, "realtek,jd1-gpio", &rt5677->pdata.jd1_gpio);
	of_property_read_u32(np, "realtek,jd2-gpio", &rt5677->pdata.jd2_gpio);
	of_property_read_u32(np, "realtek,jd3-gpio", &rt5677->pdata.jd3_gpio);

	return 0;
}

static struct regmap_irq rt5677_irqs[] = {
	[RT5677_IRQ_JD1] = {
		.reg_offset = 0,
		.mask = RT5677_EN_IRQ_GPIO_JD1,
	},
	[RT5677_IRQ_JD2] = {
		.reg_offset = 0,
		.mask = RT5677_EN_IRQ_GPIO_JD2,
	},
	[RT5677_IRQ_JD3] = {
		.reg_offset = 0,
		.mask = RT5677_EN_IRQ_GPIO_JD3,
	},
};

static struct regmap_irq_chip rt5677_irq_chip = {
	.name = "rt5677",
	.irqs = rt5677_irqs,
	.num_irqs = ARRAY_SIZE(rt5677_irqs),

	.num_regs = 1,
	.status_base = RT5677_IRQ_CTRL1,
	.mask_base = RT5677_IRQ_CTRL1,
	.mask_invert = 1,
};

static int rt5677_init_irq(struct i2c_client *i2c)
{
	int ret;
	struct rt5677_priv *rt5677 = i2c_get_clientdata(i2c);

	if (!rt5677->pdata.jd1_gpio &&
		!rt5677->pdata.jd2_gpio &&
		!rt5677->pdata.jd3_gpio)
		return 0;

	if (!i2c->irq) {
		dev_err(&i2c->dev, "No interrupt specified\n");
		return -EINVAL;
	}

	ret = regmap_add_irq_chip(rt5677->regmap, i2c->irq,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT, 0,
		&rt5677_irq_chip, &rt5677->irq_data);

	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register IRQ chip: %d\n", ret);
		return ret;
	}

	return 0;
}

static void rt5677_free_irq(struct i2c_client *i2c)
{
	struct rt5677_priv *rt5677 = i2c_get_clientdata(i2c);

	if (rt5677->irq_data)
		regmap_del_irq_chip(i2c->irq, rt5677->irq_data);
}

static int rt5677_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5677_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5677_priv *rt5677;
	int ret;
	unsigned int val;

	rt5677 = devm_kzalloc(&i2c->dev, sizeof(struct rt5677_priv),
				GFP_KERNEL);
	if (rt5677 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5677);

	if (pdata)
		rt5677->pdata = *pdata;

	if (i2c->dev.of_node) {
		ret = rt5677_parse_dt(rt5677, i2c->dev.of_node);
		if (ret) {
			dev_err(&i2c->dev, "Failed to parse device tree: %d\n",
				ret);
			return ret;
		}
	} else {
		rt5677->pow_ldo2 = -EINVAL;
	}

	if (gpio_is_valid(rt5677->pow_ldo2)) {
		ret = devm_gpio_request_one(&i2c->dev, rt5677->pow_ldo2,
					    GPIOF_OUT_INIT_HIGH,
					    "RT5677 POW_LDO2");
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to request POW_LDO2 %d: %d\n",
				rt5677->pow_ldo2, ret);
			return ret;
		}
		/* Wait a while until I2C bus becomes available. The datasheet
		 * does not specify the exact we should wait but startup
		 * sequence mentiones at least a few milliseconds.
		 */
		msleep(10);
	}

	rt5677->regmap_physical = devm_regmap_init_i2c(i2c,
					&rt5677_regmap_physical);
	if (IS_ERR(rt5677->regmap_physical)) {
		ret = PTR_ERR(rt5677->regmap_physical);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	rt5677->regmap = devm_regmap_init(&i2c->dev, NULL, i2c, &rt5677_regmap);
	if (IS_ERR(rt5677->regmap)) {
		ret = PTR_ERR(rt5677->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt5677->regmap, RT5677_VENDOR_ID2, &val);
	if (val != RT5677_DEVICE_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt5677\n", val);
		return -ENODEV;
	}

	regmap_write(rt5677->regmap, RT5677_RESET, 0x10ec);

	ret = regmap_register_patch(rt5677->regmap, init_list,
				    ARRAY_SIZE(init_list));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	if (rt5677->pdata.in1_diff)
		regmap_update_bits(rt5677->regmap, RT5677_IN1,
					RT5677_IN_DF1, RT5677_IN_DF1);

	if (rt5677->pdata.in2_diff)
		regmap_update_bits(rt5677->regmap, RT5677_IN1,
					RT5677_IN_DF2, RT5677_IN_DF2);

	if (rt5677->pdata.lout1_diff)
		regmap_update_bits(rt5677->regmap, RT5677_LOUT1,
					RT5677_LOUT1_L_DF, RT5677_LOUT1_L_DF);

	if (rt5677->pdata.lout2_diff)
		regmap_update_bits(rt5677->regmap, RT5677_LOUT1,
					RT5677_LOUT2_L_DF, RT5677_LOUT2_L_DF);

	if (rt5677->pdata.lout3_diff)
		regmap_update_bits(rt5677->regmap, RT5677_LOUT1,
					RT5677_LOUT3_L_DF, RT5677_LOUT3_L_DF);

	if (rt5677->pdata.dmic2_clk_pin == RT5677_DMIC_CLK2) {
		regmap_update_bits(rt5677->regmap, RT5677_GEN_CTRL2,
					RT5677_GPIO5_FUNC_MASK,
					RT5677_GPIO5_FUNC_DMIC);
		regmap_update_bits(rt5677->regmap, RT5677_GPIO_CTRL2,
					RT5677_GPIO5_DIR_MASK,
					RT5677_GPIO5_DIR_OUT);
	}

	if (rt5677->pdata.micbias1_vdd_3v3)
		regmap_update_bits(rt5677->regmap, RT5677_MICBIAS,
			RT5677_MICBIAS1_CTRL_VDD_MASK,
			RT5677_MICBIAS1_CTRL_VDD_3_3V);

	rt5677_init_gpio(i2c);
	rt5677_init_irq(i2c);

	return snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5677,
				      rt5677_dai, ARRAY_SIZE(rt5677_dai));
}

static int rt5677_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	rt5677_free_irq(i2c);
	rt5677_free_gpio(i2c);

	return 0;
}

static struct i2c_driver rt5677_i2c_driver = {
	.driver = {
		.name = "rt5677",
		.owner = THIS_MODULE,
	},
	.probe = rt5677_i2c_probe,
	.remove   = rt5677_i2c_remove,
	.id_table = rt5677_i2c_id,
};
module_i2c_driver(rt5677_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5677 driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
