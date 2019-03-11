/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCM3060 codec driver
 *
 * Copyright (C) 2018 Kirill Marinushkin <kmarinushkin@birdec.tech>
 */

#ifndef _SND_SOC_PCM3060_H
#define _SND_SOC_PCM3060_H

#include <linux/device.h>
#include <linux/regmap.h>

extern const struct regmap_config pcm3060_regmap;

#define PCM3060_DAI_ID_DAC	0
#define PCM3060_DAI_ID_ADC	1
#define PCM3060_DAI_IDS_NUM	2

/* ADC and DAC can be clocked from separate or same sources CLK1 and CLK2 */
#define PCM3060_CLK_DEF	0 /* default: CLK1->ADC, CLK2->DAC */
#define PCM3060_CLK1		1
#define PCM3060_CLK2		2

struct pcm3060_priv_dai {
	bool is_master;
	unsigned int sclk_freq;
};

struct pcm3060_priv {
	struct regmap *regmap;
	struct pcm3060_priv_dai dai[PCM3060_DAI_IDS_NUM];
	u8 out_se: 1;
};

int pcm3060_probe(struct device *dev);
int pcm3060_remove(struct device *dev);

/* registers */

#define PCM3060_REG64			0x40
#define PCM3060_REG_MRST		0x80
#define PCM3060_REG_SRST		0x40
#define PCM3060_REG_ADPSV		0x20
#define PCM3060_REG_SHIFT_ADPSV	0x05
#define PCM3060_REG_DAPSV		0x10
#define PCM3060_REG_SHIFT_DAPSV	0x04
#define PCM3060_REG_SE			0x01

#define PCM3060_REG65			0x41
#define PCM3060_REG66			0x42
#define PCM3060_REG_AT2_MIN		0x36
#define PCM3060_REG_AT2_MAX		0xFF

#define PCM3060_REG67			0x43
#define PCM3060_REG72			0x48
#define PCM3060_REG_CSEL		0x80
#define PCM3060_REG_MASK_MS		0x70
#define PCM3060_REG_MS_S		0x00
#define PCM3060_REG_MS_M768		(0x01 << 4)
#define PCM3060_REG_MS_M512		(0x02 << 4)
#define PCM3060_REG_MS_M384		(0x03 << 4)
#define PCM3060_REG_MS_M256		(0x04 << 4)
#define PCM3060_REG_MS_M192		(0x05 << 4)
#define PCM3060_REG_MS_M128		(0x06 << 4)
#define PCM3060_REG_MASK_FMT		0x03
#define PCM3060_REG_FMT_I2S		0x00
#define PCM3060_REG_FMT_LJ		0x01
#define PCM3060_REG_FMT_RJ		0x02

#define PCM3060_REG68			0x44
#define PCM3060_REG_OVER		0x40
#define PCM3060_REG_DREV2		0x04
#define PCM3060_REG_SHIFT_MUT21	0x00
#define PCM3060_REG_SHIFT_MUT22	0x01

#define PCM3060_REG69			0x45
#define PCM3060_REG_FLT		0x80
#define PCM3060_REG_MASK_DMF		0x60
#define PCM3060_REG_DMC		0x10
#define PCM3060_REG_ZREV		0x02
#define PCM3060_REG_AZRO		0x01

#define PCM3060_REG70			0x46
#define PCM3060_REG71			0x47
#define PCM3060_REG_AT1_MIN		0x0E
#define PCM3060_REG_AT1_MAX		0xFF

#define PCM3060_REG73			0x49
#define PCM3060_REG_ZCDD		0x10
#define PCM3060_REG_BYP		0x08
#define PCM3060_REG_DREV1		0x04
#define PCM3060_REG_SHIFT_MUT11	0x00
#define PCM3060_REG_SHIFT_MUT12	0x01

#endif /* _SND_SOC_PCM3060_H */
