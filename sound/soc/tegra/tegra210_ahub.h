/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tegra210_ahub.h - TEGRA210 AHUB
 *
 * Copyright (c) 2020-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 */

#ifndef __TEGRA210_AHUB__H__
#define __TEGRA210_AHUB__H__

/* Tegra210 specific */
#define TEGRA210_XBAR_PART1_RX				0x200
#define TEGRA210_XBAR_PART2_RX				0x400
#define TEGRA210_XBAR_RX_STRIDE				0x4
#define TEGRA210_XBAR_AUDIO_RX_COUNT			90
#define TEGRA210_XBAR_REG_MASK_0			0xf1f03ff
#define TEGRA210_XBAR_REG_MASK_1			0x3f30031f
#define TEGRA210_XBAR_REG_MASK_2			0xff1cf313
#define TEGRA210_XBAR_REG_MASK_3			0x0
#define TEGRA210_XBAR_UPDATE_MAX_REG			3
/* Tegra186 specific */
#define TEGRA186_XBAR_PART3_RX				0x600
#define TEGRA186_XBAR_AUDIO_RX_COUNT			115
#define TEGRA186_XBAR_REG_MASK_0			0xf3fffff
#define TEGRA186_XBAR_REG_MASK_1			0x3f310f1f
#define TEGRA186_XBAR_REG_MASK_2			0xff3cf311
#define TEGRA186_XBAR_REG_MASK_3			0x3f0f00ff
#define TEGRA186_XBAR_UPDATE_MAX_REG			4

/* Tegra264 specific */
#define TEGRA264_XBAR_PART1_RX				0x1000
#define TEGRA264_XBAR_PART2_RX				0x2000
#define TEGRA264_XBAR_PART3_RX				0x3000
#define TEGRA264_XBAR_PART4_RX				0x4000
#define TEGRA264_XBAR_PART0_ADX6_RX1			0x224
#define TEGRA264_XBAR_AUDIO_RX_COUNT			((TEGRA264_XBAR_PART0_ADX6_RX1 / 4) + 1)
#define TEGRA264_XBAR_REG_MASK_0			0xfffffff
#define TEGRA264_XBAR_REG_MASK_1			0x3f013f1f
#define TEGRA264_XBAR_REG_MASK_2			0xff3c0301
#define TEGRA264_XBAR_REG_MASK_3			0x3f00ffff
#define TEGRA264_XBAR_REG_MASK_4			0x7fff9f
#define TEGRA264_XBAR_UPDATE_MAX_REG			5

#define TEGRA264_AXBAR_ADMAIF_RX1			0x0
#define TEGRA264_AXBAR_SFC4_RX1				0x6c
#define TEGRA264_AXBAR_MIXER1_RX1			0x80
#define TEGRA264_AXBAR_MIXER1_RX10			0xa4
#define TEGRA264_AXBAR_DSPK1_RX1			0xc0
#define TEGRA264_AXBAR_OPE1_RX1				0x100
#define TEGRA264_AXBAR_MVC1_RX1				0x110
#define TEGRA264_AXBAR_MVC2_RX1				0x114
#define TEGRA264_AXBAR_AMX1_RX1				0x120
#define TEGRA264_AXBAR_AMX3_RX4				0x14c
#define TEGRA264_AXBAR_ADX1_RX1				0x160
#define TEGRA264_AXBAR_ASRC1_RX7			0x1a8
#define TEGRA264_AXBAR_ADMAIF_RX21			0x1d0
#define TEGRA264_AXBAR_ADX6_RX1				0x224

#define TEGRA_XBAR_UPDATE_MAX_REG (TEGRA264_XBAR_UPDATE_MAX_REG)

#define TEGRA264_MAX_REGISTER_ADDR (TEGRA264_XBAR_PART4_RX +		\
	(TEGRA210_XBAR_RX_STRIDE * (TEGRA264_XBAR_AUDIO_RX_COUNT - 1)))

#define TEGRA186_MAX_REGISTER_ADDR (TEGRA186_XBAR_PART3_RX +		\
	(TEGRA210_XBAR_RX_STRIDE * (TEGRA186_XBAR_AUDIO_RX_COUNT - 1)))

#define TEGRA210_MAX_REGISTER_ADDR (TEGRA210_XBAR_PART2_RX +		\
	(TEGRA210_XBAR_RX_STRIDE * (TEGRA210_XBAR_AUDIO_RX_COUNT - 1)))

#define MUX_REG(id) (TEGRA210_XBAR_RX_STRIDE * (id))

#define MUX_VALUE(npart, nbit) (1 + (nbit) + (npart) * 32)

#define SOC_VALUE_ENUM_WIDE(xreg, shift, xmax, xtexts, xvalues)		\
	{								\
		.reg = xreg,						\
		.shift_l = shift,					\
		.shift_r = shift,					\
		.items = xmax,						\
		.texts = xtexts,					\
		.values = xvalues,					\
		.mask = xmax ? roundup_pow_of_two(xmax) - 1 : 0		\
	}

#define SOC_VALUE_ENUM_WIDE_DECL(name, xreg, shift, xtexts, xvalues)	\
	static struct soc_enum name =					\
		SOC_VALUE_ENUM_WIDE(xreg, shift, ARRAY_SIZE(xtexts),	\
				    xtexts, xvalues)

#define MUX_ENUM_CTRL_DECL(ename, id)					\
	SOC_VALUE_ENUM_WIDE_DECL(ename##_enum, MUX_REG(id), 0,		\
				 tegra210_ahub_mux_texts,		\
				 tegra210_ahub_mux_values);		\
	static const struct snd_kcontrol_new ename##_control =		\
		SOC_DAPM_ENUM_EXT("Route", ename##_enum,		\
				  tegra_ahub_get_value_enum,		\
				  tegra_ahub_put_value_enum)

#define MUX_ENUM_CTRL_DECL_186(ename, id)				\
	SOC_VALUE_ENUM_WIDE_DECL(ename##_enum, MUX_REG(id), 0,		\
				 tegra186_ahub_mux_texts,		\
				 tegra186_ahub_mux_values);		\
	static const struct snd_kcontrol_new ename##_control =		\
		SOC_DAPM_ENUM_EXT("Route", ename##_enum,		\
				  tegra_ahub_get_value_enum,		\
				  tegra_ahub_put_value_enum)

#define MUX_ENUM_CTRL_DECL_234(ename, id) MUX_ENUM_CTRL_DECL_186(ename, id)

#define MUX_ENUM_CTRL_DECL_264(ename, id)				\
	SOC_VALUE_ENUM_WIDE_DECL(ename##_enum, MUX_REG(id), 0,		\
				 tegra264_ahub_mux_texts,		\
				 tegra264_ahub_mux_values);		\
	static const struct snd_kcontrol_new ename##_control =		\
		SOC_DAPM_ENUM_EXT("Route", ename##_enum,		\
				  tegra_ahub_get_value_enum,		\
				  tegra_ahub_put_value_enum)

#define WIDGETS(sname, ename)						     \
	SND_SOC_DAPM_AIF_IN(sname " XBAR-RX", NULL, 0, SND_SOC_NOPM, 0, 0),  \
	SND_SOC_DAPM_AIF_OUT(sname " XBAR-TX", NULL, 0, SND_SOC_NOPM, 0, 0), \
	SND_SOC_DAPM_MUX(sname " Mux", SND_SOC_NOPM, 0, 0,		     \
			 &ename##_control)

#define TX_WIDGETS(sname)						    \
	SND_SOC_DAPM_AIF_IN(sname " XBAR-RX", NULL, 0, SND_SOC_NOPM, 0, 0), \
	SND_SOC_DAPM_AIF_OUT(sname " XBAR-TX", NULL, 0, SND_SOC_NOPM, 0, 0)

#define DAI(sname)							\
	{								\
		.name = "XBAR-" #sname,					\
		.playback = {						\
			.stream_name = #sname " XBAR-Playback",		\
			.channels_min = 1,				\
			.channels_max = 32,				\
			.rates = SNDRV_PCM_RATE_8000_192000,		\
			.formats = SNDRV_PCM_FMTBIT_S8 |		\
				SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S24_LE |		\
				SNDRV_PCM_FMTBIT_S32_LE,		\
		},							\
		.capture = {						\
			.stream_name = #sname " XBAR-Capture",		\
			.channels_min = 1,				\
			.channels_max = 32,				\
			.rates = SNDRV_PCM_RATE_8000_192000,		\
			.formats = SNDRV_PCM_FMTBIT_S8 |		\
				SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S24_LE |		\
				SNDRV_PCM_FMTBIT_S32_LE,		\
		},							\
	}

struct tegra_ahub_soc_data {
	const struct regmap_config *regmap_config;
	const struct snd_soc_component_driver *cmpnt_drv;
	struct snd_soc_dai_driver *dai_drv;
	unsigned int mask[TEGRA_XBAR_UPDATE_MAX_REG];
	unsigned int reg_count;
	unsigned int num_dais;
	unsigned int xbar_part_size;
};

struct tegra_ahub {
	const struct tegra_ahub_soc_data *soc_data;
	struct regmap *regmap;
	struct clk *clk;
};

#endif
