// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
//
// tegra210_mixer.c - Tegra210 MIXER driver

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra210_mixer.h"
#include "tegra_cif.h"

#define MIXER_REG(reg, id)	((reg) + ((id) * TEGRA210_MIXER_REG_STRIDE))
#define MIXER_REG_BASE(reg)	((reg) % TEGRA210_MIXER_REG_STRIDE)

#define MIXER_GAIN_CFG_RAM_ADDR(id)					\
	(TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_0 +				\
	 ((id) * TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_STRIDE))

#define MIXER_RX_REG_DEFAULTS(id)					\
	{ MIXER_REG(TEGRA210_MIXER_RX1_CIF_CTRL, id), 0x00007700},	\
	{ MIXER_REG(TEGRA210_MIXER_RX1_CTRL, id), 0x00010823},	\
	{ MIXER_REG(TEGRA210_MIXER_RX1_PEAK_CTRL, id), 0x000012c0}

#define MIXER_TX_REG_DEFAULTS(id)					\
	{ MIXER_REG(TEGRA210_MIXER_TX1_INT_MASK, (id)), 0x00000001},	\
	{ MIXER_REG(TEGRA210_MIXER_TX1_CIF_CTRL, (id)), 0x00007700}

#define REG_DURATION_PARAM(reg, i) ((reg) + NUM_GAIN_POLY_COEFFS + 1 + (i))

static const struct reg_default tegra210_mixer_reg_defaults[] = {
	/* Inputs */
	MIXER_RX_REG_DEFAULTS(0),
	MIXER_RX_REG_DEFAULTS(1),
	MIXER_RX_REG_DEFAULTS(2),
	MIXER_RX_REG_DEFAULTS(3),
	MIXER_RX_REG_DEFAULTS(4),
	MIXER_RX_REG_DEFAULTS(5),
	MIXER_RX_REG_DEFAULTS(6),
	MIXER_RX_REG_DEFAULTS(7),
	MIXER_RX_REG_DEFAULTS(8),
	MIXER_RX_REG_DEFAULTS(9),
	/* Outputs */
	MIXER_TX_REG_DEFAULTS(0),
	MIXER_TX_REG_DEFAULTS(1),
	MIXER_TX_REG_DEFAULTS(2),
	MIXER_TX_REG_DEFAULTS(3),
	MIXER_TX_REG_DEFAULTS(4),

	{ TEGRA210_MIXER_CG, 0x00000001},
	{ TEGRA210_MIXER_GAIN_CFG_RAM_CTRL, 0x00004000},
	{ TEGRA210_MIXER_PEAKM_RAM_CTRL, 0x00004000},
	{ TEGRA210_MIXER_ENABLE, 0x1 },
};

/* Default gain parameters */
static const struct tegra210_mixer_gain_params gain_params = {
	/* Polynomial coefficients */
	{ 0, 0, 0, 0, 0, 0, 0, 0x1000000, 0 },
	/* Gain value */
	0x10000,
	/* Duration Parameters */
	{ 0, 0, 0x400, 0x8000000 },
};

static int __maybe_unused tegra210_mixer_runtime_suspend(struct device *dev)
{
	struct tegra210_mixer *mixer = dev_get_drvdata(dev);

	regcache_cache_only(mixer->regmap, true);
	regcache_mark_dirty(mixer->regmap);

	return 0;
}

static int __maybe_unused tegra210_mixer_runtime_resume(struct device *dev)
{
	struct tegra210_mixer *mixer = dev_get_drvdata(dev);

	regcache_cache_only(mixer->regmap, false);
	regcache_sync(mixer->regmap);

	return 0;
}

static int tegra210_mixer_write_ram(struct tegra210_mixer *mixer,
				    unsigned int addr,
				    unsigned int coef)
{
	unsigned int reg, val;
	int err;

	/* Check if busy */
	err = regmap_read_poll_timeout(mixer->regmap,
				       TEGRA210_MIXER_GAIN_CFG_RAM_CTRL,
				       val, !(val & 0x80000000), 10, 10000);
	if (err < 0)
		return err;

	reg = (addr << TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_SHIFT) &
	      TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_MASK;
	reg |= TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_INIT_EN;
	reg |= TEGRA210_MIXER_GAIN_CFG_RAM_RW_WRITE;
	reg |= TEGRA210_MIXER_GAIN_CFG_RAM_SEQ_ACCESS_EN;

	regmap_write(mixer->regmap,
		     TEGRA210_MIXER_GAIN_CFG_RAM_CTRL,
		     reg);
	regmap_write(mixer->regmap,
		     TEGRA210_MIXER_GAIN_CFG_RAM_DATA,
		     coef);

	return 0;
}

static int tegra210_mixer_configure_gain(struct snd_soc_component *cmpnt,
					 unsigned int id, bool instant_gain)
{
	struct tegra210_mixer *mixer = snd_soc_component_get_drvdata(cmpnt);
	unsigned int reg = MIXER_GAIN_CFG_RAM_ADDR(id);
	int err, i;

	pm_runtime_get_sync(cmpnt->dev);

	/* Write default gain poly coefficients */
	for (i = 0; i < NUM_GAIN_POLY_COEFFS; i++) {
		err = tegra210_mixer_write_ram(mixer, reg + i,
					       gain_params.poly_coeff[i]);

		if (err < 0)
			goto rpm_put;
	}

	/* Write stored gain value */
	err = tegra210_mixer_write_ram(mixer, reg + NUM_GAIN_POLY_COEFFS,
				       mixer->gain_value[id]);
	if (err < 0)
		goto rpm_put;

	/* Write duration parameters */
	for (i = 0; i < NUM_DURATION_PARMS; i++) {
		int val;

		if (instant_gain)
			val = 1;
		else
			val = gain_params.duration[i];

		err = tegra210_mixer_write_ram(mixer,
					       REG_DURATION_PARAM(reg, i),
					       val);
		if (err < 0)
			goto rpm_put;
	}

	/* Trigger to apply gain configurations */
	err = tegra210_mixer_write_ram(mixer, reg + REG_CFG_DONE_TRIGGER,
				       VAL_CFG_DONE_TRIGGER);

rpm_put:
	pm_runtime_put(cmpnt->dev);

	return err;
}

static int tegra210_mixer_get_gain(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_mixer *mixer = snd_soc_component_get_drvdata(cmpnt);
	unsigned int reg = mc->reg;
	unsigned int i;

	i = (reg - TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_0) /
	    TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_STRIDE;

	ucontrol->value.integer.value[0] = mixer->gain_value[i];

	return 0;
}

static int tegra210_mixer_apply_gain(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol,
				     bool instant_gain)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_mixer *mixer = snd_soc_component_get_drvdata(cmpnt);
	unsigned int reg = mc->reg, id;
	int err;

	/* Save gain value for specific MIXER input */
	id = (reg - TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_0) /
	     TEGRA210_MIXER_GAIN_CFG_RAM_ADDR_STRIDE;

	if (mixer->gain_value[id] == ucontrol->value.integer.value[0])
		return 0;

	mixer->gain_value[id] = ucontrol->value.integer.value[0];

	err = tegra210_mixer_configure_gain(cmpnt, id, instant_gain);
	if (err) {
		dev_err(cmpnt->dev, "Failed to apply gain\n");
		return err;
	}

	return 1;
}

static int tegra210_mixer_put_gain(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	return tegra210_mixer_apply_gain(kcontrol, ucontrol, false);
}

static int tegra210_mixer_put_instant_gain(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	return tegra210_mixer_apply_gain(kcontrol, ucontrol, true);
}

static int tegra210_mixer_set_audio_cif(struct tegra210_mixer *mixer,
					struct snd_pcm_hw_params *params,
					unsigned int reg,
					unsigned int id)
{
	unsigned int channels, audio_bits;
	struct tegra_cif_conf cif_conf;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		audio_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		audio_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		return -EINVAL;
	}

	cif_conf.audio_ch = channels;
	cif_conf.client_ch = channels;
	cif_conf.audio_bits = audio_bits;
	cif_conf.client_bits = audio_bits;

	tegra_set_cif(mixer->regmap,
		      reg + (id * TEGRA210_MIXER_REG_STRIDE),
		      &cif_conf);

	return 0;
}

static int tegra210_mixer_in_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params,
				       struct snd_soc_dai *dai)
{
	struct tegra210_mixer *mixer = snd_soc_dai_get_drvdata(dai);
	int err;

	err = tegra210_mixer_set_audio_cif(mixer, params,
					   TEGRA210_MIXER_RX1_CIF_CTRL,
					   dai->id);
	if (err < 0)
		return err;

	return tegra210_mixer_configure_gain(dai->component, dai->id, false);
}

static int tegra210_mixer_out_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params,
					struct snd_soc_dai *dai)
{
	struct tegra210_mixer *mixer = snd_soc_dai_get_drvdata(dai);

	return tegra210_mixer_set_audio_cif(mixer, params,
					    TEGRA210_MIXER_TX1_CIF_CTRL,
					    dai->id - TEGRA210_MIXER_RX_MAX);
}

static const struct snd_soc_dai_ops tegra210_mixer_out_dai_ops = {
	.hw_params	= tegra210_mixer_out_hw_params,
};

static const struct snd_soc_dai_ops tegra210_mixer_in_dai_ops = {
	.hw_params	= tegra210_mixer_in_hw_params,
};

#define IN_DAI(id)						\
	{							\
		.name = "MIXER-RX-CIF"#id,			\
		.playback = {					\
			.stream_name = "RX" #id "-CIF-Playback",\
			.channels_min = 1,			\
			.channels_max = 8,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "RX" #id "-CIF-Capture",	\
			.channels_min = 1,			\
			.channels_max = 8,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra210_mixer_in_dai_ops,		\
	}

#define OUT_DAI(id)						\
	{							\
		.name = "MIXER-TX-CIF" #id,			\
		.playback = {					\
			.stream_name = "TX" #id "-CIF-Playback",\
			.channels_min = 1,			\
			.channels_max = 8,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "TX" #id "-CIF-Capture",	\
			.channels_min = 1,			\
			.channels_max = 8,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra210_mixer_out_dai_ops,		\
	}

static struct snd_soc_dai_driver tegra210_mixer_dais[] = {
	/* Mixer Input */
	IN_DAI(1),
	IN_DAI(2),
	IN_DAI(3),
	IN_DAI(4),
	IN_DAI(5),
	IN_DAI(6),
	IN_DAI(7),
	IN_DAI(8),
	IN_DAI(9),
	IN_DAI(10),

	/* Mixer Output */
	OUT_DAI(1),
	OUT_DAI(2),
	OUT_DAI(3),
	OUT_DAI(4),
	OUT_DAI(5),
};

#define ADDER_CTRL_DECL(name, reg)			\
	static const struct snd_kcontrol_new name[] = {	\
		SOC_DAPM_SINGLE("RX1", reg, 0, 1, 0),	\
		SOC_DAPM_SINGLE("RX2", reg, 1, 1, 0),	\
		SOC_DAPM_SINGLE("RX3", reg, 2, 1, 0),	\
		SOC_DAPM_SINGLE("RX4", reg, 3, 1, 0),	\
		SOC_DAPM_SINGLE("RX5", reg, 4, 1, 0),	\
		SOC_DAPM_SINGLE("RX6", reg, 5, 1, 0),	\
		SOC_DAPM_SINGLE("RX7", reg, 6, 1, 0),	\
		SOC_DAPM_SINGLE("RX8", reg, 7, 1, 0),	\
		SOC_DAPM_SINGLE("RX9", reg, 8, 1, 0),	\
		SOC_DAPM_SINGLE("RX10", reg, 9, 1, 0),	\
	}

ADDER_CTRL_DECL(adder1, TEGRA210_MIXER_TX1_ADDER_CONFIG);
ADDER_CTRL_DECL(adder2, TEGRA210_MIXER_TX2_ADDER_CONFIG);
ADDER_CTRL_DECL(adder3, TEGRA210_MIXER_TX3_ADDER_CONFIG);
ADDER_CTRL_DECL(adder4, TEGRA210_MIXER_TX4_ADDER_CONFIG);
ADDER_CTRL_DECL(adder5, TEGRA210_MIXER_TX5_ADDER_CONFIG);

#define GAIN_CTRL(id)	\
	SOC_SINGLE_EXT("RX" #id " Gain Volume",			\
		       MIXER_GAIN_CFG_RAM_ADDR((id) - 1), 0,	\
		       0x20000, 0, tegra210_mixer_get_gain,	\
		       tegra210_mixer_put_gain),		\
	SOC_SINGLE_EXT("RX" #id " Instant Gain Volume",		\
		       MIXER_GAIN_CFG_RAM_ADDR((id) - 1), 0,	\
		       0x20000, 0, tegra210_mixer_get_gain,	\
		       tegra210_mixer_put_instant_gain),

/* Volume controls for all MIXER inputs */
static const struct snd_kcontrol_new tegra210_mixer_gain_ctls[] = {
	GAIN_CTRL(1)
	GAIN_CTRL(2)
	GAIN_CTRL(3)
	GAIN_CTRL(4)
	GAIN_CTRL(5)
	GAIN_CTRL(6)
	GAIN_CTRL(7)
	GAIN_CTRL(8)
	GAIN_CTRL(9)
	GAIN_CTRL(10)
};

static const struct snd_soc_dapm_widget tegra210_mixer_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX3", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX4", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX5", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX6", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX7", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX8", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX9", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX10", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX1", NULL, 0, TEGRA210_MIXER_TX1_ENABLE, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX2", NULL, 0, TEGRA210_MIXER_TX2_ENABLE, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX3", NULL, 0, TEGRA210_MIXER_TX3_ENABLE, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX4", NULL, 0, TEGRA210_MIXER_TX4_ENABLE, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX5", NULL, 0, TEGRA210_MIXER_TX5_ENABLE, 0, 0),
	SND_SOC_DAPM_MIXER("Adder1", SND_SOC_NOPM, 1, 0, adder1,
			   ARRAY_SIZE(adder1)),
	SND_SOC_DAPM_MIXER("Adder2", SND_SOC_NOPM, 1, 0, adder2,
			   ARRAY_SIZE(adder2)),
	SND_SOC_DAPM_MIXER("Adder3", SND_SOC_NOPM, 1, 0, adder3,
			   ARRAY_SIZE(adder3)),
	SND_SOC_DAPM_MIXER("Adder4", SND_SOC_NOPM, 1, 0, adder4,
			   ARRAY_SIZE(adder4)),
	SND_SOC_DAPM_MIXER("Adder5", SND_SOC_NOPM, 1, 0, adder5,
			   ARRAY_SIZE(adder5)),
};

#define RX_ROUTES(id, sname)						   \
	{ "RX" #id " XBAR-" sname,	NULL,	"RX" #id " XBAR-TX" },	   \
	{ "RX" #id "-CIF-" sname,	NULL,	"RX" #id " XBAR-" sname }, \
	{ "RX" #id,			NULL,	"RX" #id "-CIF-" sname }

#define MIXER_RX_ROUTES(id)		\
	RX_ROUTES(id, "Playback"),	\
	RX_ROUTES(id, "Capture")

#define ADDER_ROUTES(id, sname)						  \
	{ "Adder" #id,			"RX1",	"RX1" },		  \
	{ "Adder" #id,			"RX2",	"RX2" },		  \
	{ "Adder" #id,			"RX3",	"RX3" },		  \
	{ "Adder" #id,			"RX4",	"RX4" },		  \
	{ "Adder" #id,			"RX5",	"RX5" },		  \
	{ "Adder" #id,			"RX6",	"RX6" },		  \
	{ "Adder" #id,			"RX7",	"RX7" },		  \
	{ "Adder" #id,			"RX8",	"RX8" },		  \
	{ "Adder" #id,			"RX9",	"RX9" },		  \
	{ "Adder" #id,			"RX10",	"RX10" },		  \
	{ "TX" #id,			NULL,	"Adder" #id },		  \
	{ "TX" #id "-CIF-" sname,	NULL,	"TX" #id },		  \
	{ "TX" #id " XBAR-" sname,	NULL,	"TX" #id "-CIF-" sname }, \
	{ "TX" #id " XBAR-RX",		NULL,	"TX" #id " XBAR-" sname } \

#define TX_ROUTES(id, sname)		\
	ADDER_ROUTES(1, sname),		\
	ADDER_ROUTES(2, sname),		\
	ADDER_ROUTES(3, sname),		\
	ADDER_ROUTES(4, sname),		\
	ADDER_ROUTES(5, sname)

#define MIXER_TX_ROUTES(id)		\
	TX_ROUTES(id, "Playback"),	\
	TX_ROUTES(id, "Capture")

static const struct snd_soc_dapm_route tegra210_mixer_routes[] = {
	/* Input */
	MIXER_RX_ROUTES(1),
	MIXER_RX_ROUTES(2),
	MIXER_RX_ROUTES(3),
	MIXER_RX_ROUTES(4),
	MIXER_RX_ROUTES(5),
	MIXER_RX_ROUTES(6),
	MIXER_RX_ROUTES(7),
	MIXER_RX_ROUTES(8),
	MIXER_RX_ROUTES(9),
	MIXER_RX_ROUTES(10),
	/* Output */
	MIXER_TX_ROUTES(1),
	MIXER_TX_ROUTES(2),
	MIXER_TX_ROUTES(3),
	MIXER_TX_ROUTES(4),
	MIXER_TX_ROUTES(5),
};

static const struct snd_soc_component_driver tegra210_mixer_cmpnt = {
	.dapm_widgets		= tegra210_mixer_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_mixer_widgets),
	.dapm_routes		= tegra210_mixer_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_mixer_routes),
	.controls		= tegra210_mixer_gain_ctls,
	.num_controls		= ARRAY_SIZE(tegra210_mixer_gain_ctls),
};

static bool tegra210_mixer_wr_reg(struct device *dev,
				unsigned int reg)
{
	if (reg < TEGRA210_MIXER_RX_LIMIT)
		reg = MIXER_REG_BASE(reg);
	else if (reg < TEGRA210_MIXER_TX_LIMIT)
		reg = MIXER_REG_BASE(reg) + TEGRA210_MIXER_TX1_ENABLE;

	switch (reg) {
	case TEGRA210_MIXER_RX1_SOFT_RESET:
	case TEGRA210_MIXER_RX1_CIF_CTRL ... TEGRA210_MIXER_RX1_PEAK_CTRL:

	case TEGRA210_MIXER_TX1_ENABLE:
	case TEGRA210_MIXER_TX1_SOFT_RESET:
	case TEGRA210_MIXER_TX1_INT_MASK ... TEGRA210_MIXER_TX1_ADDER_CONFIG:

	case TEGRA210_MIXER_ENABLE ... TEGRA210_MIXER_CG:
	case TEGRA210_MIXER_GAIN_CFG_RAM_CTRL ... TEGRA210_MIXER_CTRL:
		return true;
	default:
		return false;
	}
}

static bool tegra210_mixer_rd_reg(struct device *dev,
				unsigned int reg)
{
	if (reg < TEGRA210_MIXER_RX_LIMIT)
		reg = MIXER_REG_BASE(reg);
	else if (reg < TEGRA210_MIXER_TX_LIMIT)
		reg = MIXER_REG_BASE(reg) + TEGRA210_MIXER_TX1_ENABLE;

	switch (reg) {
	case TEGRA210_MIXER_RX1_SOFT_RESET ... TEGRA210_MIXER_RX1_SAMPLE_COUNT:
	case TEGRA210_MIXER_TX1_ENABLE ... TEGRA210_MIXER_TX1_ADDER_CONFIG:
	case TEGRA210_MIXER_ENABLE ... TEGRA210_MIXER_CTRL:
		return true;
	default:
		return false;
	}
}

static bool tegra210_mixer_volatile_reg(struct device *dev,
				unsigned int reg)
{
	if (reg < TEGRA210_MIXER_RX_LIMIT)
		reg = MIXER_REG_BASE(reg);
	else if (reg < TEGRA210_MIXER_TX_LIMIT)
		reg = MIXER_REG_BASE(reg) + TEGRA210_MIXER_TX1_ENABLE;

	switch (reg) {
	case TEGRA210_MIXER_RX1_SOFT_RESET:
	case TEGRA210_MIXER_RX1_STATUS:

	case TEGRA210_MIXER_TX1_SOFT_RESET:
	case TEGRA210_MIXER_TX1_STATUS:
	case TEGRA210_MIXER_TX1_INT_STATUS:
	case TEGRA210_MIXER_TX1_INT_SET:

	case TEGRA210_MIXER_SOFT_RESET:
	case TEGRA210_MIXER_STATUS:
	case TEGRA210_MIXER_INT_STATUS:
	case TEGRA210_MIXER_GAIN_CFG_RAM_CTRL:
	case TEGRA210_MIXER_GAIN_CFG_RAM_DATA:
	case TEGRA210_MIXER_PEAKM_RAM_CTRL:
	case TEGRA210_MIXER_PEAKM_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_mixer_precious_reg(struct device *dev,
				unsigned int reg)
{
	switch (reg) {
	case TEGRA210_MIXER_GAIN_CFG_RAM_DATA:
	case TEGRA210_MIXER_PEAKM_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra210_mixer_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_MIXER_CTRL,
	.writeable_reg		= tegra210_mixer_wr_reg,
	.readable_reg		= tegra210_mixer_rd_reg,
	.volatile_reg		= tegra210_mixer_volatile_reg,
	.precious_reg		= tegra210_mixer_precious_reg,
	.reg_defaults		= tegra210_mixer_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_mixer_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static const struct of_device_id tegra210_mixer_of_match[] = {
	{ .compatible = "nvidia,tegra210-amixer" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_mixer_of_match);

static int tegra210_mixer_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_mixer *mixer;
	void __iomem *regs;
	int err, i;

	mixer = devm_kzalloc(dev, sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return -ENOMEM;

	dev_set_drvdata(dev, mixer);

	/* Use default gain value for all MIXER inputs */
	for (i = 0; i < TEGRA210_MIXER_RX_MAX; i++)
		mixer->gain_value[i] = gain_params.gain_value;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	mixer->regmap = devm_regmap_init_mmio(dev, regs,
					      &tegra210_mixer_regmap_config);
	if (IS_ERR(mixer->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(mixer->regmap);
	}

	regcache_cache_only(mixer->regmap, true);

	err = devm_snd_soc_register_component(dev, &tegra210_mixer_cmpnt,
					      tegra210_mixer_dais,
					      ARRAY_SIZE(tegra210_mixer_dais));
	if (err) {
		dev_err(dev, "can't register MIXER component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(dev);

	return 0;
}

static void tegra210_mixer_platform_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct dev_pm_ops tegra210_mixer_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra210_mixer_runtime_suspend,
			   tegra210_mixer_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver tegra210_mixer_driver = {
	.driver = {
		.name = "tegra210_mixer",
		.of_match_table = tegra210_mixer_of_match,
		.pm = &tegra210_mixer_pm_ops,
	},
	.probe = tegra210_mixer_platform_probe,
	.remove = tegra210_mixer_platform_remove,
};
module_platform_driver(tegra210_mixer_driver);

MODULE_AUTHOR("Arun Shamanna Lakshmi <aruns@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 MIXER ASoC driver");
MODULE_LICENSE("GPL v2");
