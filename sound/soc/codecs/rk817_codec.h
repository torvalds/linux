/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RK817_CODEC_H__
#define __RK817_CODEC_H__

/* codec register */
#define RK817_CODEC_BASE		0x0000

#define RK817_CODEC_DTOP_VUCTL		(RK817_CODEC_BASE + 0x12)
#define RK817_CODEC_DTOP_VUCTIME	(RK817_CODEC_BASE + 0x13)
#define RK817_CODEC_DTOP_LPT_SRST	(RK817_CODEC_BASE + 0x14)
#define RK817_CODEC_DTOP_DIGEN_CLKE	(RK817_CODEC_BASE + 0x15)
#define RK817_CODEC_AREF_RTCFG0		(RK817_CODEC_BASE + 0x16)
#define RK817_CODEC_AREF_RTCFG1		(RK817_CODEC_BASE + 0x17)
#define RK817_CODEC_AADC_CFG0		(RK817_CODEC_BASE + 0x18)
#define RK817_CODEC_AADC_CFG1		(RK817_CODEC_BASE + 0x19)
#define RK817_CODEC_DADC_VOLL		(RK817_CODEC_BASE + 0x1a)
#define RK817_CODEC_DADC_VOLR		(RK817_CODEC_BASE + 0x1b)
#define RK817_CODEC_DADC_SR_ACL0	(RK817_CODEC_BASE + 0x1e)
#define RK817_CODEC_DADC_ALC1		(RK817_CODEC_BASE + 0x1f)
#define RK817_CODEC_DADC_ALC2		(RK817_CODEC_BASE + 0x20)
#define RK817_CODEC_DADC_NG		(RK817_CODEC_BASE + 0x21)
#define RK817_CODEC_DADC_HPF		(RK817_CODEC_BASE + 0x22)
#define RK817_CODEC_DADC_RVOLL		(RK817_CODEC_BASE + 0x23)
#define RK817_CODEC_DADC_RVOLR		(RK817_CODEC_BASE + 0x24)
#define RK817_CODEC_AMIC_CFG0		(RK817_CODEC_BASE + 0x27)
#define RK817_CODEC_AMIC_CFG1		(RK817_CODEC_BASE + 0x28)
#define RK817_CODEC_DMIC_PGA_GAIN	(RK817_CODEC_BASE + 0x29)
#define RK817_CODEC_DMIC_LMT1		(RK817_CODEC_BASE + 0x2a)
#define RK817_CODEC_DMIC_LMT2		(RK817_CODEC_BASE + 0x2b)
#define RK817_CODEC_DMIC_NG1		(RK817_CODEC_BASE + 0x2c)
#define RK817_CODEC_DMIC_NG2		(RK817_CODEC_BASE + 0x2d)
#define RK817_CODEC_ADAC_CFG0		(RK817_CODEC_BASE + 0x2e)
#define RK817_CODEC_ADAC_CFG1		(RK817_CODEC_BASE + 0x2f)
#define RK817_CODEC_DDAC_POPD_DACST	(RK817_CODEC_BASE + 0x30)
#define RK817_CODEC_DDAC_VOLL		(RK817_CODEC_BASE + 0x31)
#define RK817_CODEC_DDAC_VOLR		(RK817_CODEC_BASE + 0x32)
#define RK817_CODEC_DDAC_SR_LMT0	(RK817_CODEC_BASE + 0x35)
#define RK817_CODEC_DDAC_LMT1		(RK817_CODEC_BASE + 0x36)
#define RK817_CODEC_DDAC_LMT2		(RK817_CODEC_BASE + 0x37)
#define RK817_CODEC_DDAC_MUTE_MIXCTL	(RK817_CODEC_BASE + 0x38)
#define RK817_CODEC_DDAC_RVOLL		(RK817_CODEC_BASE + 0x39)
#define RK817_CODEC_DDAC_RVOLR		(RK817_CODEC_BASE + 0x3a)
#define RK817_CODEC_AHP_ANTI0		(RK817_CODEC_BASE + 0x3b)
#define RK817_CODEC_AHP_ANTI1		(RK817_CODEC_BASE + 0x3c)
#define RK817_CODEC_AHP_CFG0		(RK817_CODEC_BASE + 0x3d)
#define RK817_CODEC_AHP_CFG1		(RK817_CODEC_BASE + 0x3e)
#define RK817_CODEC_AHP_CP		(RK817_CODEC_BASE + 0x3f)
#define RK817_CODEC_ACLASSD_CFG1	(RK817_CODEC_BASE + 0x40)
#define RK817_CODEC_ACLASSD_CFG2	(RK817_CODEC_BASE + 0x41)
#define RK817_CODEC_APLL_CFG0		(RK817_CODEC_BASE + 0x42)
#define RK817_CODEC_APLL_CFG1		(RK817_CODEC_BASE + 0x43)
#define RK817_CODEC_APLL_CFG2		(RK817_CODEC_BASE + 0x44)
#define RK817_CODEC_APLL_CFG3		(RK817_CODEC_BASE + 0x45)
#define RK817_CODEC_APLL_CFG4		(RK817_CODEC_BASE + 0x46)
#define RK817_CODEC_APLL_CFG5		(RK817_CODEC_BASE + 0x47)
#define RK817_CODEC_DI2S_CKM		(RK817_CODEC_BASE + 0x48)
#define RK817_CODEC_DI2S_RSD		(RK817_CODEC_BASE + 0x49)
#define RK817_CODEC_DI2S_RXCR1		(RK817_CODEC_BASE + 0x4a)
#define RK817_CODEC_DI2S_RXCR2		(RK817_CODEC_BASE + 0x4b)
#define RK817_CODEC_DI2S_RXCMD_TSD	(RK817_CODEC_BASE + 0x4c)
#define RK817_CODEC_DI2S_TXCR1		(RK817_CODEC_BASE + 0x4d)
#define RK817_CODEC_DI2S_TXCR2		(RK817_CODEC_BASE + 0x4e)
#define RK817_CODEC_DI2S_TXCR3_TXCMD	(RK817_CODEC_BASE + 0x4f)

/* RK817_CODEC_DTOP_DIGEN_CLKE */
#define ADC_DIG_CLK_MASK		(0xf << 4)
#define ADC_DIG_CLK_SFT			4
#define ADC_DIG_CLK_DIS			(0x0 << 4)
#define ADC_DIG_CLK_EN			(0xf << 4)

#define DAC_DIG_CLK_MASK		(0xf << 0)
#define DAC_DIG_CLK_SFT			0
#define DAC_DIG_CLK_DIS			(0x0 << 0)
#define DAC_DIG_CLK_EN			(0xf << 0)

/* RK817_CODEC_APLL_CFG5 */
#define PLL_PW_DOWN			(0x01 << 0)
#define PLL_PW_UP			(0x00 << 0)

/* RK817_CODEC_DI2S_CKM */
#define PDM_EN_MASK			(0x1 << 3)
#define PDM_EN_SFT			3
#define PDM_EN_DISABLE			(0x0 << 3)
#define PDM_EN_ENABLE			(0x1 << 3)

#define SCK_EN_ENABLE			(0x1 << 2)
#define SCK_EN_DISABLE			(0x0 << 2)

#define RK817_I2S_MODE_MASK		(0x1 << 0)
#define RK817_I2S_MODE_SFT		0
#define RK817_I2S_MODE_MST		(0x1 << 0)
#define RK817_I2S_MODE_SLV		(0x0 << 0)

/* RK817_CODEC_DDAC_MUTE_MIXCTL */
#define DACMT_ENABLE			(0x1 << 0)
#define DACMT_DISABLE			(0x0 << 0)

/* RK817_CODEC_DI2S_RXCR2 */
#define VDW_RX_24BITS			(0x17)
#define VDW_RX_16BITS			(0x0f)
/* RK817_CODEC_DI2S_TXCR2 */
#define VDW_TX_24BITS			(0x17)
#define VDW_TX_16BITS			(0x0f)

/* RK817_CODEC_AHP_CFG1 */
#define HP_ANTIPOP_ENABLE		(0x1 << 4)
#define HP_ANTIPOP_DISABLE		(0x0 << 4)

/* RK817_CODEC_ADAC_CFG1 */
#define PWD_DACBIAS_MASK		(0x1 << 3)
#define PWD_DACBIAS_SFT			3
#define PWD_DACBIAS_DOWN		(0x1 << 3)
#define PWD_DACBIAS_ON			(0x0 << 3)

#define PWD_DACD_MASK			(0x1 << 2)
#define PWD_DACD_SFT			2
#define PWD_DACD_DOWN			(0x1 << 2)
#define PWD_DACD_ON			(0x0 << 2)

#define PWD_DACL_MASK			(0x1 << 1)
#define PWD_DACL_SFT			1
#define PWD_DACL_DOWN			(0x1 << 1)
#define PWD_DACL_ON			(0x0 << 1)

#define PWD_DACR_MASK			(0x1 << 0)
#define PWD_DACR_SFT			0
#define PWD_DACR_DOWN			(0x1 << 0)
#define PWD_DACR_ON			(0x0 << 0)

/* RK817_CODEC_AADC_CFG0 */
#define ADC_L_PWD_MASK			(0x1 << 7)
#define ADC_L_PWD_SFT			7
#define ADC_L_PWD_DIS			(0x0 << 7)
#define ADC_L_PWD_EN			(0x1 << 7)

#define ADC_R_PWD_MASK			(0x1 << 6)
#define ADC_R_PWD_SFT			6
#define ADC_R_PWD_DIS			(0x0 << 6)
#define ADC_R_PWD_EN			(0x1 << 6)

/* RK817_CODEC_AMIC_CFG0 */
#define MIC_DIFF_MASK			(0x1 << 7)
#define MIC_DIFF_SFT			7
#define MIC_DIFF_DIS			(0x0 << 7)
#define MIC_DIFF_EN			(0x1 << 7)

#define PWD_PGA_L_MASK			(0x1 << 5)
#define PWD_PGA_L_SFT			5
#define PWD_PGA_L_DIS			(0x0 << 5)
#define PWD_PGA_L_EN			(0x1 << 5)

#define PWD_PGA_R_MASK			(0x1 << 4)
#define PWD_PGA_R_SFT			4
#define PWD_PGA_R_DIS			(0x0 << 4)
#define PWD_PGA_R_EN			(0x1 << 4)

enum {
	RK817_HIFI,
	RK817_VOICE,
};

enum {
	RK817_MONO = 1,
	RK817_STEREO,
};

enum {
	OFF,
	RCV,
	SPK_PATH,
	HP_PATH,
	HP_NO_MIC,
	BT,
	SPK_HP,
	RING_SPK,
	RING_HP,
	RING_HP_NO_MIC,
	RING_SPK_HP,
};

enum {
	MIC_OFF,
	MAIN_MIC,
	HANDS_FREE_MIC,
	BT_SCO_MIC,
};

struct rk817_reg_val_typ {
	unsigned int reg;
	unsigned int value;
};

struct rk817_init_bit_typ {
	unsigned int reg;
	unsigned int power_bit;
	unsigned int init_bit;
};

#endif /* __RK817_CODEC_H__ */
