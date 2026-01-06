/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt5575.h  --  ALC5575 ALSA SoC audio driver
 *
 * Copyright(c) 2025 Realtek Semiconductor Corp.
 *
 */

#ifndef __RT5575_H__
#define __RT5575_H__

#define RT5575_DEVICE_ID	0x10ec5575
#define RT5575_DSP_MAPPING	0x18000000

#define RT5575_BOOT		0x8004
#define RT5575_ID		0x8008
#define RT5575_ID_1		0x800c
#define RT5575_MIXL_VOL		0x8a14
#define RT5575_MIXR_VOL		0x8a18
#define RT5575_PROMPT_VOL	0x8a84
#define RT5575_SPK01_VOL	0x8a88
#define RT5575_SPK23_VOL	0x8a8c
#define RT5575_MIC1_VOL		0x8a98
#define RT5575_MIC2_VOL		0x8a9c
#define RT5575_WNC_CTRL		0x80ec
#define RT5575_MODE_CTRL	0x80f0
#define RT5575_I2S_RATE_CTRL	0x80f4
#define RT5575_SLEEP_CTRL	0x80f8
#define RT5575_ALG_BYPASS_CTRL	0x80fc
#define RT5575_PINMUX_CTRL_2	0x81a4
#define RT5575_GPIO_CTRL_1	0x8208
#define RT5575_DSP_BUS_CTRL	0x880c
#define RT5575_SW_INT		0x0018
#define RT5575_DSP_BOOT_ERR	0x8e14
#define RT5575_DSP_READY	0x8e24
#define RT5575_DSP_CMD_ADDR	0x8e28
#define RT5575_EFUSE_DATA_2	0xc638
#define RT5575_EFUSE_DATA_3	0xc63c
#define RT5575_EFUSE_PID	0xc660

#define RT5575_BOOT_MASK	0x3
#define RT5575_BOOT_SPI		0x0

enum {
	RT5575_AIF1,
	RT5575_AIF2,
	RT5575_AIF3,
	RT5575_AIF4,
	RT5575_AIFS,
};

struct rt5575_priv {
	struct i2c_client *i2c;
	struct snd_soc_component *component;
	struct regmap *dsp_regmap, *regmap;
};

#endif /* __RT5575_H__ */
