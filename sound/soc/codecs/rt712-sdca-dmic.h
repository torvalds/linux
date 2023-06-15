/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt712-sdca-dmic.h -- RT712 SDCA DMIC ALSA SoC audio driver header
 *
 * Copyright(c) 2023 Realtek Semiconductor Corp.
 */

#ifndef __RT712_SDW_DMIC_H__
#define __RT712_SDW_DMIC_H__

#include <linux/regmap.h>
#include <linux/soundwire/sdw_registers.h>

struct  rt712_sdca_dmic_priv {
	struct regmap *regmap;
	struct regmap *mbq_regmap;
	struct snd_soc_component *component;
	struct sdw_slave *slave;
	enum sdw_slave_status status;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_hw_init;
	bool fu1e_dapm_mute;
	bool fu1e_mixer_mute[4];
};

struct rt712_sdca_dmic_kctrl_priv {
	unsigned int reg_base;
	unsigned int count;
	unsigned int max;
	unsigned int invert;
};

/* SDCA (Channel) */
#define CH_01	0x01
#define CH_02	0x02
#define CH_03	0x03
#define CH_04	0x04

static const struct reg_default rt712_sdca_dmic_reg_defaults[] = {
	{ 0x201a, 0x00 },
	{ 0x201b, 0x00 },
	{ 0x201c, 0x00 },
	{ 0x201d, 0x00 },
	{ 0x201e, 0x00 },
	{ 0x201f, 0x00 },
	{ 0x2029, 0x00 },
	{ 0x202a, 0x00 },
	{ 0x202d, 0x00 },
	{ 0x202e, 0x00 },
	{ 0x202f, 0x00 },
	{ 0x2030, 0x00 },
	{ 0x2031, 0x00 },
	{ 0x2032, 0x00 },
	{ 0x2033, 0x00 },
	{ 0x2034, 0x00 },
	{ 0x2230, 0x00 },
	{ 0x2231, 0x2f },
	{ 0x2232, 0x80 },
	{ 0x2f01, 0x00 },
	{ 0x2f02, 0x09 },
	{ 0x2f03, 0x00 },
	{ 0x2f04, 0x00 },
	{ 0x2f05, 0x0b },
	{ 0x2f06, 0x01 },
	{ 0x2f08, 0x00 },
	{ 0x2f09, 0x00 },
	{ 0x2f0a, 0x01 },
	{ 0x2f35, 0x02 },
	{ 0x2f36, 0xcf },
	{ 0x2f52, 0x08 },
	{ 0x2f58, 0x07 },
	{ 0x2f59, 0x07 },
	{ 0x3201, 0x01 },
	{ 0x320c, 0x00 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_IT26, RT712_SDCA_CTL_VENDOR_DEF, 0), 0x00 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_USER_FU1E, RT712_SDCA_CTL_FU_MUTE, CH_01), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_USER_FU1E, RT712_SDCA_CTL_FU_MUTE, CH_02), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_USER_FU1E, RT712_SDCA_CTL_FU_MUTE, CH_03), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_USER_FU1E, RT712_SDCA_CTL_FU_MUTE, CH_04), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_CS1F, RT712_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), 0x09 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_CS1C, RT712_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), 0x09 },
};

static const struct reg_default rt712_sdca_dmic_mbq_defaults[] = {
	{ 0x0590001e, 0x0020 },
	{ 0x06100000, 0x0010 },
	{ 0x06100006, 0x0055 },
	{ 0x06100010, 0x2630 },
	{ 0x06100011, 0x152f },
	{ 0x06100013, 0x0102 },
	{ 0x06100015, 0x2219 },
	{ 0x06100018, 0x0102 },
	{ 0x06100026, 0x2c29 },
	{ 0x06100027, 0x2d2b },
	{ 0x0610002b, 0x2a32 },
	{ 0x0610002f, 0x3355 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_PLATFORM_FU15, RT712_SDCA_CTL_FU_CH_GAIN, CH_01), 0x0000 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_PLATFORM_FU15, RT712_SDCA_CTL_FU_CH_GAIN, CH_02), 0x0000 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_PLATFORM_FU15, RT712_SDCA_CTL_FU_CH_GAIN, CH_03), 0x0000 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_PLATFORM_FU15, RT712_SDCA_CTL_FU_CH_GAIN, CH_04), 0x0000 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_USER_FU1E, RT712_SDCA_CTL_FU_VOLUME, CH_01), 0x0000 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_USER_FU1E, RT712_SDCA_CTL_FU_VOLUME, CH_02), 0x0000 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_USER_FU1E, RT712_SDCA_CTL_FU_VOLUME, CH_03), 0x0000 },
	{ SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT712_SDCA_ENT_USER_FU1E, RT712_SDCA_CTL_FU_VOLUME, CH_04), 0x0000 },
};

#endif /* __RT712_SDW_DMIC_H__ */
